#include "flash.h"

// TODO - add wear leveling
// e.g. https://github.com/earlephilhower/SPIFTL

bool flash_fat_read(int block, uint8_t *buffer) {
    const uint8_t *data = (uint8_t *)((uint32_t)__DRIVE_START + FAT_BLOCK_SIZE * block);
    memcpy(buffer, data, FAT_BLOCK_SIZE);
    return true;
}

bool flash_fat_write(int block, uint8_t *buffer) {
    /*
     * NOTE: Flash memory must be erased and updated in blocks of 4096 bytes
     *       from the head, and updating at the halfway boundary will (probably)
     *       lead to undefined results.
     */
    uint8_t data[FLASH_SECTOR_SIZE];  // 4096 byte

    // Obtain the location of the FAT sector(512 byte) in the flash memory sector(4096 byte).
    int flash_sector = floor((block * FAT_BLOCK_SIZE) / FLASH_SECTOR_SIZE);
    int flash_sector_fat_offset = (block * FAT_BLOCK_SIZE) % FLASH_SECTOR_SIZE;
    // Retrieve the data in the flash memory sector and update only the data for the FAT sector.
    memcpy(data, (uint8_t *)((uint32_t)__DRIVE_START + FLASH_SECTOR_SIZE * flash_sector), sizeof(data));
    memcpy(data + flash_sector_fat_offset, buffer, FAT_BLOCK_SIZE);

    // Clear and update flash sectors.
    // stdio_set_driver_enabled(&stdio_usb, false);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase((uint32_t)__DRIVE_START - XIP_BASE + flash_sector * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    flash_range_program((uint32_t)__DRIVE_START - XIP_BASE + flash_sector * FLASH_SECTOR_SIZE, data, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    // stdio_set_driver_enabled(&stdio_usb, true);

    return true;
}
