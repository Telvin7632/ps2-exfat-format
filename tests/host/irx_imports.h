/*
 * Host (PC) stand-in for iop/irx_imports.h.
 *
 * Lets the real iop/main.c and iop/diskio.c be compiled for the PC: IOP kernel
 * services (threads, semaphores, sysmem) and BDM's bdm_get_bd() are provided
 * by iop_stubs.c.
 *
 * struct block_device MUST stay identical to $PS2SDK/iop/include/bdm.h;
 * check_bd_struct.sh (run by "make test") compares the two.
 */
#ifndef HOST_IRX_IMPORTS_H
#define HOST_IRX_IMPORTS_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* irx.h / loadcore.h */
#define IRX_ID(name, major, minor) extern int host_irx_id_unused
#define MODULE_RESIDENT_END    0
#define MODULE_NO_RESIDENT_END 1
#define MODULE_REMOVABLE_END   2

/* bdm.h */
struct block_device
{
    // Private driver data
    void *priv;

    // Device name + number + partition number
    // Can be used to create device names like:
    //  - mass0p1
    char *name;
    unsigned int devNr;
    unsigned int parNr;
    unsigned char parId;

    unsigned int sectorSize;        // Size of a sector in bytes
    u64 sectorOffset;               // Starting sector number
    u64 sectorCount;                // Maximum number of sectors usable

    int (*read)(struct block_device *bd, u64 sector, void *buffer, u16 count);
    int (*write)(struct block_device *bd, u64 sector, const void *buffer, u16 count);
    void (*flush)(struct block_device *bd);
    int (*stop)(struct block_device *bd);

    // Desired iomanX path prefix for this device type (e.g. "usb", "ata").
    // When set, the fatfs driver exposes the device under this prefix (e.g. usb0:, ata0:).
    // Partition drivers should copy this from the parent block device.
    // NULL means the device is only accessible via the legacy "mass:" prefix.
    char *path;
};

void bdm_get_bd(struct block_device **pbd, unsigned int count);

/* sysmem.h */
#define ALLOC_FIRST 0
void *AllocSysMemory(int mode, int size, void *ptr);
int FreeSysMemory(void *ptr);

/* thbase.h */
#define TH_C 0x02000000

typedef struct _iop_thread
{
    unsigned int attr;
    unsigned int option;
    void (*thread)(void *);
    unsigned int stacksize;
    unsigned int priority;
} iop_thread_t;

int CreateThread(iop_thread_t *thread);
int DeleteThread(int thid);
int StartThread(int thid, void *arg);
int DelayThread(int usec);

/* thsemap.h */
typedef struct
{
    unsigned int attr;
    unsigned int option;
    int initial;
    int max;
} iop_sema_t;

int CreateSema(iop_sema_t *sema);
int DeleteSema(int semid);
int SignalSema(int semid);
int WaitSema(int semid);

#endif /* HOST_IRX_IMPORTS_H */
