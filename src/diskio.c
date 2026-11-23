// FatFs disk I/O glue for the SD card driver
#include "ff.h"
#include "diskio.h"
#include "sdcard.h"

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    return sd_ready() ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    if (!sd_ready() && !sd_init()) return STA_NOINIT;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv || !sd_ready()) return RES_NOTRDY;
    return sd_read((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv || !sd_ready()) return RES_NOTRDY;
    return sd_write((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv) return RES_PARERR;
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buff = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    default: return RES_PARERR;
    }
}
