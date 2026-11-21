/*
 * exfatfmt.irx - IOP module that formats the internal PS2 HDD (BDM device "ata0")
 * as exFAT using ChaN's FatFs f_mkfs(). Result: MBR + one exFAT partition.
 * FatFs would switch to GPT for >= 2^32 sectors, but PS2SDK's ata_bd caps
 * sectorCount at 0xFFFFFFFF (u32 total_sectors_lba48), so on the console the
 * volume is always MBR and at most 2 TiB.
 *
 * Arguments (passed by the EE through SifExecModuleBuffer):
 *   INFO              -> returns disk size in MiB (0 = disk not found)
 *   FORMAT [au_kib]   -> formats; returns 0 on success, FatFs FRESULT (1..19)
 *                        or one of the ERR_* codes below
 *
 * The value is packed into the module return value:
 *   (value << 2) | MODULE_NO_RESIDENT_END
 * so the module unloads itself and the EE reads the value from mod_res >> 2.
 */
#include "irx_imports.h"
#include <kerr.h>
#include "ff.h"

#define MODNAME "exfatfmt"
IRX_ID(MODNAME, 1, 0);

#define MAX_BD    20          /* size of BDM's mount table */
#define WORK_SIZE (64 * 1024) /* f_mkfs work buffer */

#define ERR_NO_DEVICE   100
#define ERR_NO_MEMORY   101
#define ERR_BAD_ARGS    102
#define ERR_THREAD      103
#define ERR_SECTOR_SIZE 104

#define MODE_INFO   0
#define MODE_FORMAT 1

#define PACK(v) ((int)((((unsigned int)(v)) << 2) | MODULE_NO_RESIDENT_END))

struct block_device *g_bd = NULL; /* used by diskio.c */

static int g_mode;
static DWORD g_au_bytes;
static int g_result;
static int g_done_sema;

/* Find the raw (whole-disk) block device of the internal HDD: "ata", unit 0, partition 0. */
static struct block_device *find_ata(void)
{
    struct block_device *list[MAX_BD];
    int tries, i;

    /* The HDD may still be spinning up, so poll for up to ~10 s. */
    for (tries = 0; tries < 20; tries++) {
        memset(list, 0, sizeof(list));
        bdm_get_bd(list, MAX_BD);

        for (i = 0; i < MAX_BD; i++) {
            struct block_device *bd = list[i];
            if (bd != NULL && bd->name != NULL && strcmp(bd->name, "ata") == 0 &&
                bd->devNr == 0 && bd->parNr == 0 && bd->sectorCount > 0)
                return bd;
        }
        DelayThread(500 * 1000);
    }
    return NULL;
}

static void worker(void *arg)
{
    (void)arg;

    g_bd = find_ata();

    if (g_bd == NULL) {
        g_result = (g_mode == MODE_INFO) ? 0 : ERR_NO_DEVICE;
    } else if (g_mode == MODE_INFO) {
        g_result = (int)((g_bd->sectorCount * (u64)g_bd->sectorSize) >> 20);
    } else if (g_bd->sectorSize != 512) {
        g_result = ERR_SECTOR_SIZE;
    } else {
        void *work = AllocSysMemory(ALLOC_FIRST, WORK_SIZE, NULL);

        if (work == NULL) {
            g_result = ERR_NO_MEMORY;
        } else {
            MKFS_PARM opt;
            FRESULT fr;

            memset(&opt, 0, sizeof(opt));
            opt.fmt     = FM_EXFAT;
            opt.au_size = g_au_bytes; /* 0 = let FatFs choose (128 KiB for >= 32 GiB) */

            printf(MODNAME ": formatting %s%u as exFAT...\n", g_bd->name, g_bd->devNr);
            fr = f_mkfs("0:", &opt, work, WORK_SIZE);
            printf(MODNAME ": f_mkfs() = %d\n", (int)fr);

            FreeSysMemory(work);
            g_result = (int)fr;
        }
    }

    SignalSema(g_done_sema);
}

static unsigned int parse_uint(const char *s)
{
    unsigned int v = 0;

    while (*s >= '0' && *s <= '9')
        v = v * 10 + (unsigned int)(*s++ - '0');
    return v;
}

int _start(int argc, char *argv[])
{
    iop_sema_t sema;
    iop_thread_t thread;
    int tid;

    if (argc < 2)
        return PACK(ERR_BAD_ARGS);

    if (strcmp(argv[1], "INFO") == 0)
        g_mode = MODE_INFO;
    else if (strcmp(argv[1], "FORMAT") == 0)
        g_mode = MODE_FORMAT;
    else
        return PACK(ERR_BAD_ARGS);

    g_au_bytes = (argc >= 3) ? (DWORD)parse_uint(argv[2]) * 1024 : 0;

    sema.attr    = 0;
    sema.option  = 0;
    sema.initial = 0;
    sema.max     = 1;
    g_done_sema  = CreateSema(&sema);
    if (g_done_sema < 0)
        return PACK(ERR_THREAD);

    /* Run the work on our own thread so we control the stack size. */
    thread.attr      = TH_C;
    thread.option    = 0;
    thread.thread    = worker;
    thread.stacksize = 0x4000;
    thread.priority  = 0x40;
    tid = CreateThread(&thread);
    if (tid < 0) {
        DeleteSema(g_done_sema);
        return PACK(ERR_THREAD);
    }
    StartThread(tid, NULL);

    WaitSema(g_done_sema);

    /* _start runs on MODLOAD's thread (priority 8), so it preempts the worker
       (priority 0x40) right at SignalSema(), before worker() has returned.
       This module's code is freed as soon as _start returns, so wait - without
       a time limit - until the worker is dormant and can be deleted. */
    while (DeleteThread(tid) == KE_NOT_DORMANT)
        DelayThread(1000);

    DeleteSema(g_done_sema);
    return PACK(g_result);
}
