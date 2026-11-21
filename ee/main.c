/*
 * exfat-format.elf - formats the internal PS2 HDD as exFAT, right on the console.
 *
 * EE side: resets the IOP, loads the drivers (DEV9 + BDM + ATA block device),
 * shows the disk size, asks for confirmation twice and runs exfatfmt.irx.
 */
#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <libpad.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>

/* IRX files embedded by bin2c (see Makefile) */
#define IRX_DECL(name)                 \
    extern unsigned char name##_irx[]; \
    extern unsigned int size_##name##_irx

IRX_DECL(iomanX);
IRX_DECL(poweroff);
IRX_DECL(ps2dev9);
IRX_DECL(bdm);
IRX_DECL(ata_bd);
IRX_DECL(exfatfmt);

#define MOD_NO_RESIDENT_END 1

static const char *const fresult_names[] = {
    "FR_OK", "FR_DISK_ERR", "FR_INT_ERR", "FR_NOT_READY", "FR_NO_FILE",
    "FR_NO_PATH", "FR_INVALID_NAME", "FR_DENIED", "FR_EXIST", "FR_INVALID_OBJECT",
    "FR_WRITE_PROTECTED", "FR_INVALID_DRIVE", "FR_NOT_ENABLED", "FR_NO_FILESYSTEM",
    "FR_MKFS_ABORTED", "FR_TIMEOUT", "FR_LOCKED", "FR_NOT_ENOUGH_CORE",
    "FR_TOO_MANY_OPEN_FILES", "FR_INVALID_PARAMETER",
};

/* ---------------------------------------------------------------- IOP */

static void reset_iop(void)
{
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0);

    /* needed to load modules from EE memory (SifExecModuleBuffer) */
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
}

/* Loads a driver; returns 0 if it loaded and stayed resident. */
static int load_driver(const char *name, void *irx, unsigned int size)
{
    int res = 0;
    int id = SifExecModuleBuffer(irx, size, 0, NULL, &res);
    int ok = (id >= 0) && ((res & 3) != MOD_NO_RESIDENT_END);

    scr_printf("    %-10s %s (id=%d, ret=%d)\n", name, ok ? "OK  " : "FAIL", id, res);
    return ok ? 0 : -1;
}

/* Runs exfatfmt.irx with one command; returns the decoded value or -1. */
static int run_formatter(const char *cmd)
{
    char args[32];
    int len = (int)strlen(cmd) + 1; /* args are '\0'-separated strings */
    int res = 0;

    memcpy(args, cmd, len);
    SifExecModuleBuffer(exfatfmt_irx, size_exfatfmt_irx, len, args, &res);

    if ((res & 3) != MOD_NO_RESIDENT_END)
        return -1;
    return (int)((unsigned int)res >> 2);
}

/* ---------------------------------------------------------------- pad */

static char pad_buf[256] __attribute__((aligned(64)));
static u32 pad_old;

static int pad_ready(void)
{
    int s = padGetState(0, 0);
    return s == PAD_STATE_STABLE || s == PAD_STATE_FINDCTP1;
}

static int pad_held(u32 *out)
{
    struct padButtonStatus b;

    if (!pad_ready() || padRead(0, 0, &b) == 0)
        return 0;
    *out = 0xffff ^ b.btns;
    return 1;
}

static void pad_init(void)
{
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, pad_buf);

    scr_printf("  Waiting for a controller in port 1...\n");
    while (!pad_held(&pad_old)) {}
}

/* Waits for a button press made AFTER this call and returns its mask.
   Buttons held (or pressed earlier, e.g. while the drivers were loading)
   are ignored until they are released and pressed again. */
static u32 pad_wait_press(void)
{
    u32 now, pressed;

    while (!pad_held(&pad_old)) {}

    for (;;) {
        if (!pad_held(&now))
            continue;
        pressed = now & ~pad_old;
        pad_old = now;
        if (pressed)
            return pressed;
    }
}

/* ---------------------------------------------------------------- main */

static void stop(const char *msg)
{
    scr_printf("\n  %s\n  You can turn the console off now.\n", msg);
    SleepThread();
}

int main(int argc, char *argv[])
{
    int mb, res;
    (void)argc;
    (void)argv;

    reset_iop();
    init_scr();
    scr_printf("\n  PS2 exFAT HDD formatter v1.0\n");
    scr_printf("  ============================\n\n");

    pad_init();

    scr_printf("  Loading drivers:\n");
    if (load_driver("iomanX", iomanX_irx, size_iomanX_irx) ||
        load_driver("poweroff", poweroff_irx, size_poweroff_irx) ||
        load_driver("ps2dev9", ps2dev9_irx, size_ps2dev9_irx) ||
        load_driver("bdm", bdm_irx, size_bdm_irx) ||
        load_driver("ata_bd", ata_bd_irx, size_ata_bd_irx))
        stop("Driver error. Is the network adapter / HDD connected?");

    scr_printf("\n  Looking for the internal HDD...\n");
    mb = run_formatter("INFO");
    if (mb <= 0)
        stop("Internal HDD (ata0) not found.");

    scr_printf("  Found: %d.%d GB (%d MiB)\n\n", mb / 1024, (mb % 1024) * 10 / 1024, mb);
    scr_printf("  !!! ALL DATA ON THIS DISK WILL BE ERASED !!!\n");
    scr_printf("  APA partitions, games, FMCB/PS2BBL on HDD - everything.\n\n");
    scr_printf("  X = continue      any other button = cancel\n");

    if (!(pad_wait_press() & PAD_CROSS))
        stop("Cancelled. Nothing was written.");

    scr_printf("\n  LAST CHANCE: press START to format, any other button cancels.\n");
    if (!(pad_wait_press() & PAD_START))
        stop("Cancelled. Nothing was written.");

    scr_printf("\n  Formatting... do NOT turn off the console.\n");
    res = run_formatter("FORMAT");

    if (res == 0)
        stop("DONE: disk is now MBR + exFAT.\n  Power-cycle the console before using it.");
    else if (res > 0 && res < (int)(sizeof(fresult_names) / sizeof(fresult_names[0])))
        scr_printf("\n  ERROR: f_mkfs failed: %s (%d)\n", fresult_names[res], res);
    else
        scr_printf("\n  ERROR: formatter returned %d\n", res);

    stop("Format failed. The disk may be in an unusable state.");
    return 0;
}
