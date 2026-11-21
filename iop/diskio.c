/*
 * FatFs low-level disk I/O glue: maps FatFs drive 0 to the BDM block device g_bd.
 */
#include "irx_imports.h"
#include "ff.h"
#include "diskio.h"

extern struct block_device *g_bd;

#define SECTOR_SIZE 512
#define MAX_XFER    128 /* sectors per driver call (64 KiB) */

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0 && g_bd != NULL) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (disk_status(pdrv) & STA_NOINIT)
        return RES_NOTRDY;

    while (count > 0) {
        u16 n = (count > MAX_XFER) ? MAX_XFER : (u16)count;

        if (g_bd->read(g_bd, (u64)sector, buff, n) < 0)
            return RES_ERROR;
        sector += n;
        buff += (UINT)n * SECTOR_SIZE;
        count -= n;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (disk_status(pdrv) & STA_NOINIT)
        return RES_NOTRDY;

    while (count > 0) {
        u16 n = (count > MAX_XFER) ? MAX_XFER : (u16)count;

        if (g_bd->write(g_bd, (u64)sector, buff, n) < 0)
            return RES_ERROR;
        sector += n;
        buff += (UINT)n * SECTOR_SIZE;
        count -= n;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (disk_status(pdrv) & STA_NOINIT)
        return RES_NOTRDY;

    switch (cmd) {
        case CTRL_SYNC:
            if (g_bd->flush != NULL)
                g_bd->flush(g_bd);
            return RES_OK;

        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = (LBA_t)g_bd->sectorCount;
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD *)buff = SECTOR_SIZE;
            return RES_OK;

        case GET_BLOCK_SIZE:
            /* 2048 sectors = 1 MiB: aligns the data area like Windows does
               (good for 4K-sector "512e" drives and SSDs). */
            *(DWORD *)buff = 2048;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}
