#ifndef _FLASH_H_
#define _FLASH_H_

#include <ctype.h>
#include <math.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef FLASH_DEBUG
#define FLASH_DEBUG 0
#endif

extern uint32_t __DRIVE_START[];
extern uint32_t __DRIVE_LEN[];
extern uint32_t __DRIVE_END[];

#ifdef __cplusplus
extern "C" {
#endif
bool flash_format();
bool flash_init();
bool flash_read(int block, uint8_t *buffer);
bool flash_write(int block, const uint8_t *buffer);
uint16_t get_lba_count(); 
uint16_t get_lba_size(); 
void flash_persist();
void flash_trim(int);
#ifdef __cplusplus
}
#endif
#endif
