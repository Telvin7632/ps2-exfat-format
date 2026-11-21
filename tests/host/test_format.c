/*
 * Host tests for exfatfmt.irx.
 *
 * The real iop/main.c (with _start renamed to exfatfmt_start) and iop/diskio.c
 * are linked with the same FatFs build used for the IOP. A file-backed block
 * device plays the role of PS2SDK's ata_bd "ata0"; decoy devices (USB mass0,
 * ata1, the partition ata0p1) fail the test if anything touches them.
 *
 * Sparse disk images are created ONLY under /tmp/ps2-exfat-test/.
 * After the run, images.txt there lists "<image> <offset> <size>" of every
 * exFAT volume for fsck_images.sh (fsck.exfat through a read-only loop device).
 */
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "irx_imports.h"
#include "ff.h"
#include "iop_stubs.h"

/* Must match iop/main.c */
#define ERR_NO_DEVICE   100
#define ERR_BAD_ARGS    102
#define ERR_SECTOR_SIZE 104

#define IMG_DIR "/tmp/ps2-exfat-test"
#define SS      512u

static int failures;

#define CHECK(cond, ...)                                       \
    do {                                                       \
        if (!(cond)) {                                         \
            failures++;                                        \
            printf("    FAIL (%s:%d) ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                               \
            printf("\n");                                      \
        }                                                      \
    } while (0)

/* ------------------------------------------------------------ devices */

struct img
{
    int fd;
    unsigned long reads, writes, flushes;
    u64 max_lba_written;
};

static int img_read(struct block_device *bd, u64 sector, void *buf, u16 count)
{
    struct img *im = bd->priv;
    size_t len = (size_t)count * SS;

    if (sector + count > bd->sectorCount)
        return -EIO; /* the drive would reject it too */
    memset(buf, 0, len); /* reads past the end of a sparse file return 0 bytes */
    if (pread(im->fd, buf, len, (off_t)(sector * SS)) < 0)
        return -EIO;
    im->reads++;
    return count;
}

static int img_write(struct block_device *bd, u64 sector, const void *buf, u16 count)
{
    struct img *im = bd->priv;
    size_t len = (size_t)count * SS;

    if (sector + count > bd->sectorCount)
        return -EIO;
    if (pwrite(im->fd, buf, len, (off_t)(sector * SS)) != (ssize_t)len)
        return -EIO;
    im->writes++;
    if (sector + count - 1 > im->max_lba_written)
        im->max_lba_written = sector + count - 1;
    return count;
}

static void img_flush(struct block_device *bd)
{
    ((struct img *)bd->priv)->flushes++;
}

static int decoy_hits;

static int decoy_read(struct block_device *bd, u64 s, void *b, u16 c)
{
    (void)s; (void)b; (void)c;
    printf("    !! read from decoy %s%up%u\n", bd->name, bd->devNr, bd->parNr);
    decoy_hits++;
    return -EIO;
}

static int decoy_write(struct block_device *bd, u64 s, const void *b, u16 c)
{
    (void)s; (void)b; (void)c;
    printf("    !! WRITE to decoy %s%up%u\n", bd->name, bd->devNr, bd->parNr);
    decoy_hits++;
    return -EIO;
}

static void decoy_flush(struct block_device *bd)
{
    printf("    !! flush of decoy %s%up%u\n", bd->name, bd->devNr, bd->parNr);
    decoy_hits++;
}

static struct block_device decoy(const char *name, unsigned dev, unsigned par)
{
    struct block_device bd;

    memset(&bd, 0, sizeof(bd));
    bd.name        = (char *)name;
    bd.devNr       = dev;
    bd.parNr       = par;
    bd.sectorSize  = SS;
    bd.sectorCount = 1000000000ull;
    bd.read        = decoy_read;
    bd.write       = decoy_write;
    bd.flush       = decoy_flush;
    return bd;
}

static struct block_device dec_mass0, dec_ata1, dec_ata0p1;

static void set_bd_list(struct block_device *ata0)
{
    memset(stub_bd_list, 0, sizeof(stub_bd_list));
    /* decoys first, the way BDM fills its table in connection order */
    stub_bd_list[0] = &dec_mass0;
    stub_bd_list[1] = &dec_ata0p1;
    stub_bd_list[2] = &dec_ata1;
    stub_bd_list[5] = ata0; /* holes in the table are normal */
}

/* ------------------------------------------------------------ module runner */

/* Runs the module like SifExecModuleBuffer() and decodes the result the way
   ee/main.c run_formatter() does. */
static int run_module(const char *cmd)
{
    char a0[] = "exfatfmt.irx";
    char a1[32];
    char *argv[3] = {a0, a1, NULL};
    int argc = 1, r;

    if (cmd != NULL) {
        snprintf(a1, sizeof(a1), "%s", cmd);
        argc = 2;
    } else {
        argv[1] = NULL;
    }

    r = exfatfmt_start(argc, argv);

    CHECK((r & 3) == MODULE_NO_RESIDENT_END, "module must unload itself, ret=0x%x", r);
    CHECK(stub_live_threads == 0, "worker thread not deleted before _start returned");
    CHECK(stub_live_semas == 0, "semaphore leaked");
    CHECK(stub_live_allocs == 0, "IOP memory leaked (%d blocks)", stub_live_allocs);
    return (int)((unsigned int)r >> 2);
}

/* ------------------------------------------------------------ protocol tests */

static void test_protocol(void)
{
    struct block_device ata0_4k = decoy("ata", 0, 0);
    int v;

    printf("== module protocol / device selection\n");

    v = run_module(NULL);
    CHECK(v == ERR_BAD_ARGS, "no args -> %d", v);
    v = run_module("FOO");
    CHECK(v == ERR_BAD_ARGS, "bad command -> %d", v);

    /* only decoys: nothing found, nothing touched */
    set_bd_list(NULL);
    v = run_module("INFO");
    CHECK(v == 0, "INFO without ata0 -> %d (want 0)", v);
    v = run_module("FORMAT");
    CHECK(v == ERR_NO_DEVICE, "FORMAT without ata0 -> %d (want %d)", v, ERR_NO_DEVICE);

    /* ata0 with 4096-byte sectors: refused before any I/O */
    ata0_4k.sectorSize  = 4096;
    ata0_4k.sectorCount = 1u << 20; /* 4 GiB */
    set_bd_list(&ata0_4k);
    v = run_module("INFO");
    CHECK(v == 4096, "INFO 4Kn -> %d MiB (want 4096)", v);
    v = run_module("FORMAT");
    CHECK(v == ERR_SECTOR_SIZE, "FORMAT 4Kn -> %d (want %d)", v, ERR_SECTOR_SIZE);

    /* the worker is slow to leave worker(): _start must keep waiting */
    set_bd_list(NULL);
    stub_delete_calls     = 0;
    stub_not_dormant_left = 5000;
    v = run_module("INFO");
    CHECK(stub_delete_calls == 5001, "DeleteThread called %d times (want 5001)", stub_delete_calls);

    CHECK(decoy_hits == 0, "decoy devices were accessed %d times", decoy_hits);
    printf("   done\n");
}

/* ------------------------------------------------------------ disk image tests */

/* reads one sector of the image for layout checks */
static void read_sec(int fd, u64 lba, u8 *buf)
{
    if (pread(fd, buf, SS, (off_t)(lba * SS)) != (ssize_t)SS) {
        CHECK(0, "pread LBA %" PRIu64 " failed", lba);
        memset(buf, 0, SS);
    }
}

static u32 le32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u64 le64(const u8 *p) { return le32(p) | (u64)le32(p + 4) << 32; }

static u32 rng_state;
static u32 rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static void fill(u8 *buf, size_t len, u32 seed)
{
    size_t i;
    rng_state = seed ? seed : 1;
    for (i = 0; i < len; i++)
        buf[i] = (u8)rng();
}

#define CHUNK     (64 * 1024)
#define HEAD_SIZE (3 * 1024 * 1024)
#define TAIL_POS  ((FSIZE_t)4608 * 1024 * 1024) /* 4.5 GiB: needs exFAT (> 4 GiB file) */
#define GAME_PATH "0:/DVD/Test Game - long file name over 4 GiB (PS2).iso"

static void fatfs_roundtrip(void)
{
    static FATFS fs;
    static u8 a[CHUNK], b[CHUNK];
    FIL f;
    FILINFO fi;
    UINT n;
    FRESULT fr;
    DWORD nfree;
    FATFS *pfs;
    FSIZE_t pos;
    int bad = 0;

    fr = f_mount(&fs, "0:", 1);
    CHECK(fr == FR_OK, "f_mount -> %d", fr);
    if (fr != FR_OK)
        return;
    CHECK(fs.fs_type == FS_EXFAT, "fs_type %d is not exFAT", fs.fs_type);

    CHECK(f_mkdir("0:/DVD") == FR_OK, "f_mkdir DVD");
    CHECK(f_mkdir("0:/CD") == FR_OK, "f_mkdir CD");

    fr = f_open(&f, GAME_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    CHECK(fr == FR_OK, "f_open for write -> %d", fr);
    if (fr != FR_OK)
        return;
    for (pos = 0; pos < HEAD_SIZE; pos += CHUNK) {
        fill(a, CHUNK, (u32)(pos / CHUNK) + 1);
        if (f_write(&f, a, CHUNK, &n) != FR_OK || n != CHUNK)
            bad++;
    }
    /* extend past 4 GiB without writing the gap, then write a tail block */
    CHECK(f_lseek(&f, TAIL_POS) == FR_OK && f_tell(&f) == TAIL_POS, "f_lseek past 4 GiB");
    fill(a, CHUNK, 0xC0FFEE);
    if (f_write(&f, a, CHUNK, &n) != FR_OK || n != CHUNK)
        bad++;
    CHECK(f_close(&f) == FR_OK, "f_close");
    CHECK(bad == 0, "%d write errors", bad);
    CHECK(f_unmount("0:") == FR_OK, "f_unmount");

    /* remount and read everything back */
    fr = f_mount(&fs, "0:", 1);
    CHECK(fr == FR_OK, "re-mount -> %d", fr);
    if (fr != FR_OK)
        return;
    fr = f_stat(GAME_PATH, &fi);
    CHECK(fr == FR_OK && fi.fsize == TAIL_POS + CHUNK, "f_stat size %" PRIu64, (u64)fi.fsize);

    fr = f_open(&f, GAME_PATH, FA_READ);
    CHECK(fr == FR_OK, "f_open for read -> %d", fr);
    if (fr == FR_OK) {
        bad = 0;
        for (pos = 0; pos < HEAD_SIZE; pos += CHUNK) {
            fill(a, CHUNK, (u32)(pos / CHUNK) + 1);
            if (f_read(&f, b, CHUNK, &n) != FR_OK || n != CHUNK || memcmp(a, b, CHUNK) != 0)
                bad++;
        }
        fill(a, CHUNK, 0xC0FFEE);
        if (f_lseek(&f, TAIL_POS) != FR_OK || f_read(&f, b, CHUNK, &n) != FR_OK || n != CHUNK ||
            memcmp(a, b, CHUNK) != 0)
            bad++;
        CHECK(bad == 0, "%d chunks read back wrong", bad);
        f_close(&f);
    }

    fr = f_getfree("0:", &nfree, &pfs);
    CHECK(fr == FR_OK, "f_getfree -> %d", fr);
    if (fr == FR_OK)
        printf("   files OK: %" PRIu64 " MiB used, %" PRIu64 " MiB free\n",
               (u64)(pfs->n_fatent - 2 - nfree) * pfs->csize * SS >> 20,
               (u64)nfree * pfs->csize * SS >> 20);
    f_unmount("0:");
}

struct disk
{
    const char *name;
    u64 file_sectors;  /* size of the image file */
    u64 bd_sectors;    /* what the block device reports (ata_bd caps at 0xFFFFFFFF) */
    int expect_gpt;
    const char *note;
};

static void test_disk(const struct disk *d, FILE *list)
{
    char path[256];
    struct img im;
    struct block_device bd;
    u8 sec[SS];
    u64 part_start, part_size, vol_len;
    u32 heap_off;
    int v;

    printf("== %s: %" PRIu64 " sectors (%s)\n", d->name, d->bd_sectors, d->note);

    snprintf(path, sizeof(path), IMG_DIR "/%s.img", d->name);
    memset(&im, 0, sizeof(im));
    unlink(path);
    im.fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
    CHECK(im.fd >= 0, "create %s: %s", path, strerror(errno));
    if (im.fd < 0)
        return;
    CHECK(ftruncate(im.fd, (off_t)(d->file_sectors * SS)) == 0, "ftruncate: %s", strerror(errno));

    memset(&bd, 0, sizeof(bd));
    bd.priv        = &im;
    bd.name        = "ata";
    bd.path        = "ata";
    bd.devNr       = 0;
    bd.parNr       = 0;
    bd.sectorSize  = SS;
    bd.sectorCount = d->bd_sectors;
    bd.read        = img_read;
    bd.write       = img_write;
    bd.flush       = img_flush;
    set_bd_list(&bd);

    v = run_module("INFO");
    CHECK(v == (int)((d->bd_sectors * SS) >> 20), "INFO -> %d MiB", v);
    CHECK(im.writes == 0, "INFO wrote to the disk");

    v = run_module("FORMAT");
    CHECK(v == 0, "FORMAT -> %d (want 0 = FR_OK)", v);
    CHECK(im.flushes > 0, "no CTRL_SYNC flush after f_mkfs");
    CHECK(decoy_hits == 0, "decoy devices were accessed");
    printf("   f_mkfs: %lu write calls, highest LBA written %" PRIu64 "\n", im.writes,
           im.max_lba_written);
    if (v != 0)
        goto out;

    /* partition table */
    read_sec(im.fd, 0, sec);
    CHECK(sec[510] == 0x55 && sec[511] == 0xAA, "no MBR signature");
    if (d->expect_gpt) {
        u8 hdr[SS], ent[SS];
        CHECK(sec[446 + 4] == 0xEE, "protective MBR type 0x%02x", sec[446 + 4]);
        read_sec(im.fd, 1, hdr);
        CHECK(memcmp(hdr, "EFI PART", 8) == 0, "no GPT header");
        read_sec(im.fd, le64(hdr + 72), ent);
        part_start = le64(ent + 32);
        part_size  = le64(ent + 40) - part_start + 1;
        printf("   GPT, partition at LBA %" PRIu64 "\n", part_start);
    } else {
        CHECK(sec[446 + 4] == 0x07, "MBR partition type 0x%02x (want 0x07)", sec[446 + 4]);
        part_start = le32(sec + 446 + 8);
        part_size  = le32(sec + 446 + 12);
        CHECK(sec[462 + 4] == 0 && sec[478 + 4] == 0 && sec[494 + 4] == 0, "extra MBR entries");
        printf("   MBR, partition at LBA %" PRIu64 "\n", part_start);
    }

    /* exFAT boot sector */
    read_sec(im.fd, part_start, sec);
    CHECK(memcmp(sec + 3, "EXFAT   ", 8) == 0, "no exFAT boot sector at LBA %" PRIu64, part_start);
    CHECK(le64(sec + 64) == part_start, "PartitionOffset %" PRIu64, le64(sec + 64));
    vol_len  = le64(sec + 72);
    heap_off = le32(sec + 88);
    CHECK(vol_len <= part_size, "volume longer than partition");
    CHECK(part_start + vol_len <= d->bd_sectors, "volume ends past the device");
    CHECK(sec[108] == 9, "BytesPerSectorShift %u", sec[108]);
    CHECK((part_start + heap_off) % 2048 == 0, "cluster heap at LBA %" PRIu64 " is not 1 MiB aligned",
          part_start + heap_off);
    printf("   exFAT: cluster %u KiB, %u clusters, heap at LBA %" PRIu64 " (1 MiB aligned)\n",
           (SS << sec[109]) / 1024, le32(sec + 92), part_start + heap_off);

    fatfs_roundtrip();
    CHECK(decoy_hits == 0, "decoy devices were accessed");

    fprintf(list, "%s %" PRIu64 " %" PRIu64 "\n", path, part_start * SS, vol_len * SS);
out:
    close(im.fd);
}

extern struct block_device *g_bd; /* iop/main.c */

int main(void)
{
    static const struct disk disks[] = {
        {"40GB", 78165360ull, 78165360ull, 0, "stock-size PS2 HDD, MBR"},
        {"500GB", 976773168ull, 976773168ull, 0, "MBR"},
        {"3TB-ata_bd", 5860533168ull, 0xFFFFFFFFull, 0,
         "3 TB disk as PS2SDK ata_bd reports it: capped at 2^32-1 sectors -> MBR"},
        {"3TB-raw", 5860533168ull, 5860533168ull, 1,
         "full 3 TB size, NOT reachable via ata_bd today -> GPT"},
    };
    FILE *list;
    size_t i;

    setvbuf(stdout, NULL, _IONBF, 0);
    dec_mass0  = decoy("mass", 0, 0);
    dec_ata1   = decoy("ata", 1, 0);
    dec_ata0p1 = decoy("ata", 0, 1);

    test_protocol();

    if (mkdir(IMG_DIR, 0755) != 0 && errno != EEXIST) {
        printf("cannot create " IMG_DIR ": %s\n", strerror(errno));
        return 1;
    }
    list = fopen(IMG_DIR "/images.txt", "w");
    if (list == NULL) {
        printf("cannot write " IMG_DIR "/images.txt\n");
        return 1;
    }
    for (i = 0; i < sizeof(disks) / sizeof(disks[0]); i++) {
        test_disk(&disks[i], list);
        g_bd = NULL;
    }
    fclose(list);

    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    if (!failures)
        printf("Images and volume offsets: " IMG_DIR "/images.txt (see tests/host/fsck_images.sh)\n");
    return failures ? 1 : 0;
}
