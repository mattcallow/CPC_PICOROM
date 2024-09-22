#include "ff.h"
#include "diskio.h"

#include "flash.h"

#define FAT_MAGIC  (0x55AA)

static DSTATUS Stat = RES_OK; // STA_NOINIT;


typedef struct {
  uint8_t DIR_Name[11];
  uint8_t DIR_Attr;
  uint8_t DIR_NTRes;
  uint8_t DIR_CrtTimeTenth;
  uint16_t DIR_CrtTime;
  uint16_t DIR_CrtDate;
  uint16_t DIR_LstAccDate;
  uint16_t DIR_FstClusHI;
  uint16_t DIR_WrtTime;
  uint16_t DIR_WrtDate;
  uint16_t DIR_FstClusLO;
  uint32_t DIR_FileSize;
} fat_dir_entry_t;


DSTATUS disk_status(BYTE drv) {
    return Stat;
}

DSTATUS disk_initialize(BYTE drv) {
    uint8_t block[FAT_BLOCK_SIZE];
    flash_fat_read(0, block);

    uint16_t magic = block[FAT_BLOCK_SIZE - 2] << 8 | block[FAT_BLOCK_SIZE - 1];
    if (magic == FAT_MAGIC) {
        Stat = RES_OK;
        return Stat;
    }

    flash_fat_read(2, block);
    fat_dir_entry_t *dir = (fat_dir_entry_t *)block;
    dir++;
    if (strcmp(dir->DIR_Name, "README  TXT") == 0) {
        Stat = RES_OK;
        return Stat;
    }

    printf("initialize flash FAT12\n");
    flash_fat_initialize();

    Stat = RES_OK;
    return Stat;
}

DRESULT disk_read(BYTE drv, BYTE *buff, LBA_t sector, UINT count) {
    if (sector > FAT_BLOCK_NUM) {
        return RES_ERROR;
    }
    flash_fat_read(sector, (uint8_t *)buff);
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
