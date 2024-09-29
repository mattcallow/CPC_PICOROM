#ifndef _FLASH_H_
#define _FLASH_H_

#include <ctype.h>
#include <math.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>

extern uint32_t __DRIVE_START[];
extern uint32_t __DRIVE_LEN[];

// 1.5MB disk
#define FAT_BLOCK_SIZE         512
#define FAT_BLOCK_NUM          3072

bool flash_fat_read(int block, uint8_t *buffer);
bool flash_fat_write(int block, uint8_t *buffer);

#endif
