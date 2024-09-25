#include "ff.h"
#include "diskio.h"

#include "flash.h"

#define FAT_MAGIC  (0x55AA)

static DSTATUS Stat = RES_OK; // STA_NOINIT;


DSTATUS disk_status(BYTE drv) {
    return Stat;
}

DSTATUS disk_initialize(BYTE drv) {
    return RES_OK;
}

DRESULT disk_read(BYTE drv, BYTE *buff, LBA_t sector, UINT count) {
    if (sector > FAT_BLOCK_NUM) {
        return RES_ERROR;
    }
    while(count-- > 0) {
        flash_fat_read(sector, (uint8_t *)buff);
        sector++;
        buff += FF_MAX_SS;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE *buff, LBA_t sector, UINT count) {
    flash_fat_write(sector, (uint8_t *)buff);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE drv, BYTE ctrl, void *buff) {
    return RES_OK;
}

DWORD get_fattime (void) {
    return 0;
}
