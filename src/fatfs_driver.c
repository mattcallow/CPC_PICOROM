#include "ff.h"
#include "diskio.h"
#include "flash.h"


DSTATUS disk_status(BYTE drv) {
    return RES_OK;
}

DSTATUS disk_initialize(BYTE drv) {
    if (flash_init()) return RES_OK;
    return RES_ERROR;
}

DRESULT disk_read(BYTE drv, BYTE *buff, LBA_t sector, UINT count) {
    if (sector > get_lba_count()) {
        return RES_ERROR;
    }
    for (unsigned int i = 0; i < count; i++) {
        flash_read(sector + i, buff + i * get_lba_size());
    }
    return RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE *buff, LBA_t sector, UINT count) {
    if (sector > get_lba_count()) {
        return RES_ERROR;
    }
    for (unsigned int i = 0; i < count; i++) {
        flash_write(sector + i, buff + i * get_lba_size());
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE drv, BYTE ctrl, void *buff) {
    if (ctrl == GET_SECTOR_COUNT) {
        *(LBA_t *)buff = get_lba_count();
        return RES_OK;
    }
    if (ctrl == GET_BLOCK_SIZE) {
        *(DWORD *)buff = get_lba_size();
        return RES_OK;
    } 
    if (ctrl == CTRL_SYNC) {
        flash_persist();
        return RES_OK;
    }
    if (ctrl ==  GET_SECTOR_SIZE) {
        *(WORD *)buff = get_lba_size();
        return RES_OK;
    }
    if (ctrl == CTRL_TRIM) {
        LBA_t *lba = (LBA_t *)buff;
        for (unsigned int i = lba[0]; i < lba[1]; i++) {
            flash_trim(i);
        }
        return RES_OK;
    }
    return RES_PARERR;
}

DWORD get_fattime (void) {
    return 0;
}
