#include "flash.h"
#include <FlashInterfaceRP2040_SDK.h>
#include <SPIFTL.h>

FlashInterfaceRP2040_SDK fi( (uint8_t *)__DRIVE_START,  (uint8_t *)__DRIVE_END);
SPIFTL ftl(&fi);


// e.g. https://github.com/earlephilhower/SPIFTL

bool flash_format() {
    #if FLASH_DEBUG
    printf("flash_format()\n");
    #endif
    return ftl.format();
}

bool flash_init() {
    static bool init_done = false;
    #if FLASH_DEBUG
    printf("flash_init() init_done=%d\n", init_done);
    #endif
    if (init_done) {
        return init_done;
    }
    init_done = ftl.start();
    return init_done;
}
bool flash_read(int block, uint8_t *buffer) {
    #if FLASH_DEBUG
    printf("flash_read(%d, buffer)\n", block);
    #endif
    return ftl.read(block, buffer);
}

bool flash_write(int block, const uint8_t *buffer) {
    #if FLASH_DEBUG
    printf("flash_write(%d, buffer)\n", block);
    #endif
    return ftl.write(block, buffer);
}

void flash_persist() {
    #if FLASH_DEBUG
    printf("flash_persist()\n");
    #endif
    ftl.persist();
}

void flash_trim(int lba) {
    #if FLASH_DEBUG
    printf("flash_trim(%d)\n", lba);
    #endif
    ftl.trim(lba);
}

uint16_t get_lba_count() { 
    return ftl.lbaCount();
}

uint16_t get_lba_size() { 
    return ftl.lbaBytes;
}