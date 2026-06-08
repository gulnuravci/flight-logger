#include "ff.h"
#include "diskio.h"
#include "sd_spi.h"

static volatile DSTATUS drv_status = STA_NOINIT;

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    drv_status = (sd_init() == 0) ? 0 : STA_NOINIT;
    return drv_status;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return drv_status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || (drv_status & STA_NOINIT)) return RES_NOTRDY;
    for (UINT i = 0; i < count; i++) {
        if (sd_read_block(sector + i, buff + i * SD_BLOCK_SIZE) != 0)
            return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || (drv_status & STA_NOINIT)) return RES_NOTRDY;
    for (UINT i = 0; i < count; i++) {
        if (sd_write_block(sector + i, buff + i * SD_BLOCK_SIZE) != 0)
            return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0 || (drv_status & STA_NOINIT)) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT: {
        uint32_t n = sd_get_sector_count();
        if (!n) return RES_ERROR;
        *(LBA_t *)buff = n;
        return RES_OK;
    }
    case GET_SECTOR_SIZE:
        *(WORD *)buff = SD_BLOCK_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;  // erase granularity unknown — report 1 sector
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
