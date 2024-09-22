// CPC ROM emulator
// Matt Callow March 2023
// Updated Nov 2023
#undef DEBUG_CONFIG
#include <string.h>
#include <stdio.h>
#include <tusb.h>
#include <bsp/board.h>
#include <ff.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include "hardware/vreg.h"
#include "hardware/pio.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/regs/xip.h"
#include "hardware/flash.h"
#include "hardware/dma.h"
#include "latch.pio.h"
#include "bootsel_button.h"
#include "flash.h"

#define VER_MAJOR 3
#define VER_MINOR 0
#define VER_PATCH 0
// not enough RAM for 16 banks
#define NUM_ROM_BANKS 12
#define ROM_SIZE 16384
// RAM copies of the ROMs
#undef USE_XIP_CACHE_AS_RAM
#ifdef USE_XIP_CACHE_AS_RAM
static uint8_t *LOWER_ROM = (uint8_t *)0x15000000;
#else
static uint8_t  LOWER_ROM[ROM_SIZE];
#endif
static uint8_t UPPER_ROMS[NUM_ROM_BANKS][ROM_SIZE];
static volatile uint8_t rom_bank = 0;

// values from the linker
extern uint32_t __FLASH_START[];
extern uint32_t __FLASH_LEN[];
extern uint32_t __ROM_START[];
extern uint32_t __ROM_LEN[];
extern uint32_t __CONFIG_START[];
extern uint32_t __CONFIG_LEN[];


#pragma pack(push,1)
typedef struct {
    uint8_t  rom_type;  // 1
    char name[33];      // 33
} rom_index_t;

typedef struct {
    uint16_t magic;
    uint16_t ver;
    uint8_t active;
    uint8_t spare2;
    int8_t lower_rom;
    int8_t upper_roms[NUM_ROM_BANKS];
    char desc[33];
    char spare[10];
} config_t;
#pragma pack(pop)

rom_index_t *rom_index = (rom_index_t *)__ROM_START;


#define CONFIG_VER 1
#define MAX_ROMS 120
#define INDEX_ENTRY_SIZE sizeof(rom_index_t)
#define INDEX_SIZE 4096
#define CONFIG_SIZE sizeof(config_t)
#define MAX_CONFIG 16
#define CONFIG_MAGIC 0x7b0

static int upper_roms[NUM_ROM_BANKS];
static int lower_rom;
const uint32_t ADDRESS_BUS_MASK = 0x3fff;
const uint32_t DATA_BUS_MASK    = 0xff << 14;
const uint32_t ROMEN_GPIO = 22;
const uint32_t ROMEN_MASK       = 1 << ROMEN_GPIO;
const uint32_t A15_GPIO = 26;
const uint32_t A15_MASK         = 1 << A15_GPIO;
const uint32_t WRITE_LATCH_GPIO = 27;
const uint32_t WRITE_LATCH_MASK = 1 << WRITE_LATCH_GPIO;
const uint32_t RESET_GPIO = 28;
const uint32_t RESET_MASK = 1 << RESET_GPIO;
const uint32_t FULL_MASK = ADDRESS_BUS_MASK|DATA_BUS_MASK|ROMEN_MASK|A15_MASK|WRITE_LATCH_MASK|RESET_MASK;

#define INSERT_UPPER_ROM(bank, rom_number) { \
    upper_roms[bank] = rom_number; \
    if (rom_number < 0) { \
        memset(UPPER_ROMS[bank], 0xff, ROM_SIZE);\
    } else {\
        memcpy(UPPER_ROMS[bank],  (void *)__ROM_START + INDEX_SIZE+ROM_SIZE*rom_number, ROM_SIZE);\
    }\
} 
#define INSERT_LOWER_ROM(rom_number) { \
    lower_rom = rom_number; \
    if (rom_number < 0) { \
        memset(LOWER_ROM, 0xff, ROM_SIZE);\
    } else {\
        memcpy(LOWER_ROM,  (void *)__ROM_START + INDEX_SIZE+ROM_SIZE*rom_number, ROM_SIZE);\
    }\
} 

#define CPC_ASSERT_RESET()    gpio_set_dir(RESET_GPIO, GPIO_OUT)
#define CPC_RELEASE_RESET()   gpio_set_dir(RESET_GPIO, GPIO_IN)

int current_config = -1;

int __not_in_flash_func(find_active_config)() 
{
    config_t *config = (config_t *)__CONFIG_START;
    for (int i=0;i<MAX_CONFIG;i++, config++) {
        if (config->magic == CONFIG_MAGIC && config->ver == CONFIG_VER && config->active) {
            return i;
        }
    }
    return -1;
}

bool __not_in_flash_func(load_config)(int slot) 
{
    config_t *config = &((config_t *)__CONFIG_START)[slot];
#ifdef DEBUG_CONFIG
    printf("loading config %d magic=%x ver=%x\n", slot, config->magic, config->ver);
#endif
    if (config->magic != CONFIG_MAGIC) {
        return false;
    }
    if (config->ver != CONFIG_VER) {
        return false;
    }
#ifdef DEBUG_CONFIG
    printf("ROM %d -> Lower ROM\n", config->lower_rom);
#endif
    if (config->lower_rom < 0) {
        return false;
    }
    INSERT_LOWER_ROM(config->lower_rom);
    for (int i=0;i<NUM_ROM_BANKS;i++) {
#ifdef DEBUG_CONFIG
        printf("ROM %d -> Upper ROM %d\n", config->upper_roms[i], i);
#endif
        INSERT_UPPER_ROM(i, config->upper_roms[i]);
    }
    current_config = slot;
    return true;
}

static uint8_t flash_buf[FLASH_SECTOR_SIZE];

bool __not_in_flash_func(save_config)(int slot, bool make_active)
{
    uint32_t irq_status;
    // copy existing config pages to RAM
    memcpy(flash_buf, __CONFIG_START, sizeof(flash_buf));
    // update config at slot 'slot'
    config_t *config;
    if (make_active) {
        config = (config_t *)(flash_buf);
        for (int i=0;i<MAX_CONFIG;i++, config++) {
            if (i == slot) {
                config->active = 1;
            } else {
                config->active = 0;
            }
        }
    } else {
        config = (config_t *)(flash_buf+CONFIG_SIZE*slot);
        config->magic = CONFIG_MAGIC;
        config->ver = CONFIG_VER;
        config->lower_rom = lower_rom;
        for (int i=0;i<NUM_ROM_BANKS;i++) {
            config->upper_roms[i] = upper_roms[i];
        }
    }
    // erase and re-save all config
#ifdef DEBUG_CONFIG
    printf("Erasing config 0x%x 0x%x\n", (uint32_t)__CONFIG_START - XIP_BASE, (size_t)__CONFIG_LEN);
#endif
    irq_status = save_and_disable_interrupts();
    flash_range_erase((uint32_t)__CONFIG_START - XIP_BASE, (size_t)__CONFIG_LEN);
    restore_interrupts(irq_status);
#ifdef DEBUG_CONFIG
    printf("region erased\n");
#endif
    irq_status = save_and_disable_interrupts();
    flash_range_program((uint32_t)__CONFIG_START - XIP_BASE, flash_buf, (size_t)__CONFIG_LEN);
    restore_interrupts(irq_status);
#ifdef DEBUG_CONFIG
    printf("config stored\n");
#endif
}

PIO pio = pio0;
uint sm = 0;

void __not_in_flash_func(emulate)(void)
{
    while(1) {
        uint32_t gpio = gpio_get_all();
        uint8_t data;
        if ((gpio & ROMEN_MASK) == 0) {
            if (gpio & A15_MASK) {
                if (upper_roms[rom_bank] >= 0) {
                    // output upper ROM data
                    gpio_put_masked(DATA_BUS_MASK, UPPER_ROMS[rom_bank][gpio&ADDRESS_BUS_MASK] << 14);
                    gpio_set_dir_out_masked(DATA_BUS_MASK);
                } else {
                    // set data bus as input (HiZ)
                    gpio_set_dir_in_masked(DATA_BUS_MASK);
                }
            } else {
                // output lower ROM data
                gpio_put_masked(DATA_BUS_MASK, LOWER_ROM[gpio&ADDRESS_BUS_MASK] << 14);
                gpio_set_dir_out_masked(DATA_BUS_MASK);

            }
        } else {                           
            // set data bus as input (HiZ)
            gpio_set_dir_in_masked(DATA_BUS_MASK);
        }
    }
}

#define RESP_BUF 0x3F00
#define CMD_PREFIX_BYTE 0xfc
#define CMD_PICOLOAD 0xff
#define CMD_LED 0xfe
#define CMD_CFGLOAD 0xFD
#define CMD_CFGSAVE 0xFC
#define CMD_CFGDEF 0xFA

#define CMD_ROMDIR1		0x1
#define CMD_ROMDIR2		0x2
#define CMD_ROMLIST1	0x7
#define CMD_ROMLIST2	0x8
#define CMD_CFGLIST1     0x09
#define CMD_CFGLIST2     0x0a

#define CMD_ROMIN 0x10
#define CMD_ROMOUT 0x11

void __not_in_flash_func(handle_latch)(void)
{
    int cmd = 0;
    int list_index = 0;
    int num_params = 0;
    int params[4];
    char buf[32];
    rom_index_t *idx;
    config_t *cfg;
    while(1) {
        uint8_t latch =  pio_sm_get_blocking(pio, sm)  & 0xff;
        // printf("l:%d c:%d rb:%d\n", latch, cmd, rom_bank);
        switch(cmd) {
            case 0:
                switch (latch) {
                    case CMD_PREFIX_BYTE:
                        cmd = -1;
                        break;
                    case 0xfd:
                    case 0xfe:
                    case 0xff:
                        break;
                    default:
                        rom_bank = latch;
                        if (rom_bank >= NUM_ROM_BANKS) rom_bank = 0;
                }
                break;
            case -1:
                sprintf(&UPPER_ROMS[rom_bank][RESP_BUF+0x40], "l:%d c:%d np:%d ri:%d p[0]:%d p[1]:%d", latch, cmd, num_params, list_index, params[0], params[1]);
                cmd = latch;
                num_params = 0;
                switch(cmd) {
                    case CMD_ROMDIR1: // dir
                        list_index = 0;
                        // fall through
                    case CMD_ROMDIR2: // next dir
                        idx = &rom_index[list_index];
                        while (idx->rom_type == 0xff && list_index < MAX_ROMS) {
                            list_index++;
                            idx = &rom_index[list_index];
                        }
                        if (list_index == MAX_ROMS) {
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 1; // done
                        } else {
                            sprintf(&UPPER_ROMS[rom_bank][RESP_BUF+3], "%3d: %s", list_index, idx->name);
                            list_index++;
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                            UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                        }
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        cmd = 0;
                        break;
                    case CMD_ROMLIST1: 
                        sprintf(&UPPER_ROMS[rom_bank][RESP_BUF+3], "FW: %d.%d.%d ROM: %d.%d%d Config: %d Banks: %d", 
                                VER_MAJOR, VER_MINOR, VER_PATCH,
                                UPPER_ROMS[rom_bank][1],UPPER_ROMS[rom_bank][2],UPPER_ROMS[rom_bank][3],
                                current_config,
                                NUM_ROM_BANKS
                            );
                        UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                        UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        list_index = 0;
                        cmd = 0;
                        break;
                    case CMD_ROMLIST2: // next rom
                        if (list_index < NUM_ROM_BANKS) {
                            uint8_t type = UPPER_ROMS[list_index][0];
                            uint8_t major = UPPER_ROMS[list_index][1];
                            uint8_t minor = UPPER_ROMS[list_index][2];
                            uint8_t patch = UPPER_ROMS[list_index][3];
                            if (type < 2 || type == 0x80) {
                                uint16_t name_table = (((uint16_t)UPPER_ROMS[list_index][5] << 8) + UPPER_ROMS[list_index][4]) - 0xc000;
                                int i=0;
                                do {
                                    buf[i] = UPPER_ROMS[list_index][name_table+i] & 0x7f;
                                } while(i <31 && UPPER_ROMS[list_index][name_table+i++]< 0x80);
                                buf[i] = 0;
                            } else if (type == 2) {
                                strcpy(buf, "-extension ROM- ");
                            } else {
                                strcpy(buf, " -Not present-  ");
                                major = 0;
                                minor = 0;
                                patch = 0;
                            }
                            sprintf(&UPPER_ROMS[rom_bank][RESP_BUF+3], "%2d: %02x %-16s %d.%d%d", 
                                list_index, 
                                type, 
                                buf,
                                major, 
                                minor, 
                                patch
                            );
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                            UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                            list_index++;
                        } else {
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 1; // status
                        }
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        cmd = 0;
                        break;
                    case CMD_CFGLIST1: // list config
                        list_index = 0;
                        cfg = (config_t *)__CONFIG_START;
                        // fall through
                    case CMD_CFGLIST2: // next config
                        if (cfg->magic == CONFIG_MAGIC && cfg->ver == CONFIG_VER) {
                            sprintf(&UPPER_ROMS[rom_bank][RESP_BUF+3], "%2d:%c%s", list_index, cfg->active?'*':' ', cfg->desc);
                        } else {
                            sprintf(&UPPER_ROMS[rom_bank][RESP_BUF+3], "%2d: -unused-", list_index);
                        }
                        UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                        UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                        if (list_index < MAX_CONFIG) {
                            list_index++;
                            cfg++;
                        } else {
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 1; // done
                        }
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        cmd = 0;
                        break;                        
                    case CMD_ROMIN: // Load ROM into bank
                        num_params = 2;
                        break;
                    case CMD_ROMOUT: // unload ROM from bank
                    case CMD_LED: // LED
                    case CMD_CFGLOAD:
                    case CMD_CFGSAVE:
                    case CMD_CFGDEF:
                        num_params = 1;
                        break;
                    case CMD_PICOLOAD:
                        reset_usb_boot(0, 0);
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        cmd = 0;
                        break;
                    default:
                        break;
                }
                break;
            default:
                // load params
                if (num_params > 0) {
                    num_params--;
                    params[num_params] = latch;
                }
                if (num_params == 0) {
                    switch (cmd) {
                        case CMD_ROMIN:
                            CPC_ASSERT_RESET();
                            sprintf(&UPPER_ROMS[rom_bank][RESP_BUF+3], "ROMLOAD,%d, %d", params[0], params[1]);
                            if (params[0] >= NUM_ROM_BANKS) params[0] = NUM_ROM_BANKS-1;
                            if (params[0] < 0) params[0] = 0;
                            if (params[1] >= MAX_ROMS) params[1] = MAX_ROMS-1;
                            if (params[1] < 0) params[1] = 0;
                            INSERT_UPPER_ROM(params[0], params[1]);
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                            CPC_RELEASE_RESET();
                            break;
                        case CMD_ROMOUT:
                            CPC_ASSERT_RESET();
                            sprintf(&UPPER_ROMS[rom_bank][RESP_BUF+3], "ROMOUT,%d", params[0]);
                            if (params[0] >= NUM_ROM_BANKS) params[0] = NUM_ROM_BANKS-1;
                            if (params[0] < 0) params[0] = 0;
                            INSERT_UPPER_ROM(params[0], -1); // remove ROM
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                            CPC_RELEASE_RESET();
                            break;
                        case CMD_CFGSAVE:
                            // TODO check bounds
                            save_config(params[0], false);
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                            break; 
                        case CMD_CFGLOAD:
                            // TODO check bounds
                            CPC_ASSERT_RESET();
                            load_config(params[0]);
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                            CPC_RELEASE_RESET();
                            break; 
                        case CMD_LED:
                            //printf("LED,%d latch=%d num_params=%d\n", params[0], latch, num_params);
                            gpio_put(PICO_DEFAULT_LED_PIN, params[0]!=0);
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                            break;
                        case CMD_CFGDEF:
                            // TODO check bounds
                            #ifdef DEBUG_CONFIG
                            printf("active,%d latch=%d num_params=%d\n", params[0], latch, num_params);
                            #endif
                            save_config(params[0], true);
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                            break;                      
                    }
                    cmd = 0;
                }
        }
    }
}


void fatal(int flashes) {
    while(1) {
        for (int i=0;i<flashes;i++) {
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
            sleep_ms(100);
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            sleep_ms(200);   
        }
        sleep_ms(500);
    }
}

static FATFS filesystem;


// some code from https://github.com/oyama/pico-usb-flash-drive

// Check the bootsel button. If pressed for ~10 seconds, reformat and reboot
static void button_task(void) {
    static uint64_t long_push = 0;

    if (bb_get_bootsel_button()) {
        long_push++;
    } else {
        long_push = 0;
    }
    if (long_push > 125000) { // Long-push BOOTSEL button
        // turn on the LED
        gpio_put(PICO_DEFAULT_LED_PIN, true);
        // wait for button release
        while(bb_get_bootsel_button());
        // turn off LED
        gpio_put(PICO_DEFAULT_LED_PIN, false);
        // re-init the flash
        flash_fat_initialize();
        // and reset
        watchdog_enable(1, 1);
        while(1);
    }
}



void usb_mode() {
    board_init();
    tud_init(BOARD_TUD_RHPORT);
    stdio_init_all();  
    f_mount(&filesystem, "/", 1);
    while(1) // the mainloop
    {
        button_task();
        tud_task(); // device task
    } 
}

int load_rom(const TCHAR* path, void* dest) {
    FIL fp;
    FRESULT fr;
    UINT bytes_read;
    fr = f_open(&fp, path, FA_READ);
    if (fr) return (int)fr;
    fr = f_read(&fp, dest, ROM_SIZE, &bytes_read);
    f_close(&fp);
    if (fr) return (int)fr;

    return 0;
}


int load_lower_rom(const TCHAR* path) {
    return load_rom(path, (void *)LOWER_ROM);
}

int load_upper_rom(const TCHAR* path, int bank) {
    int ret = load_rom(path, (void *)UPPER_ROMS[bank]);
    if (ret == 0) {
        upper_roms[bank] = 1;
    }
}

void cpc_mode() {
    CPC_ASSERT_RESET();
    for (int i=0;i<NUM_ROM_BANKS;i++) {
        upper_roms[i] = -1;
        INSERT_UPPER_ROM(i , -1);
    }
    if (f_mount(&filesystem, "/", 1)) fatal(3);
    if (load_lower_rom("OS_464.ROM")) fatal(4);
    if (load_upper_rom("BASIC_1.0.ROM", 0)) fatal(5);
    load_upper_rom("picorom.rom", 2);

/*
#ifdef DEBUG_CONFIG
    printf("rom_index is 0x%0x - listing ROMs\n", rom_index);
    for (int i=0;i<MAX_ROMS;i++) {
        rom_index_t *idx = &rom_index[i];
        if (idx->rom_type != 0xff) {
            printf("%3d 0x%02x %s 0x%0x %x %x %x\n", i, idx->rom_type, idx->name, (void *)__ROM_START+INDEX_SIZE, __ROM_START, INDEX_SIZE, ROM_SIZE);
        }
    }
#endif
    int c = find_active_config();
    if (c<0) fatal(1);
    if (load_config(c)) {
#ifdef DEBUG_CONFIG
        printf("Config loaded\n");
#endif
    } else {
#ifdef DEBUG_CONFIG
        printf("Config invalid\n");
#endif
        fatal(2);
    }
*/
    // overclock - pick the lowest freq that works reliably
    //set_sys_clock_khz(200000, true);
    //set_sys_clock_khz(225000, true);
    set_sys_clock_khz(250000, true);
    multicore_launch_core1(emulate);
    uint offset = pio_add_program(pio, &latch_program);
    latch_program_init(pio, sm, offset);
    CPC_RELEASE_RESET();
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    handle_latch();
}


int main() {
#ifdef USE_XIP_CACHE_AS_RAM
    // disable XIP cache - this frees up the cache RAM for variable storage
    hw_clear_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_EN_BITS);
#endif
#ifdef DEBUG_CONFIG
    stdio_init_all();
    while (!tud_cdc_connected()) { sleep_ms(100);  }
    printf("tud_cdc_connected() %s\n", __TIMESTAMP__);
    printf("__FLASH_START  0x%08x __FLASH_LEN  0x%08x\n", __FLASH_START, __FLASH_LEN);
    printf("__ROM_START    0x%08x __ROM_LEN    0x%08x\n", __ROM_START, __ROM_LEN);
    printf("__CONFIG_START 0x%08x __CONFIG_LEN 0x%08x\n", __CONFIG_START, __CONFIG_LEN);
#endif
    gpio_init_mask(FULL_MASK);
    gpio_set_dir_in_masked(FULL_MASK);
    gpio_pull_down(WRITE_LATCH_GPIO); // pull down for diode OR
    gpio_pull_up(ROMEN_GPIO); // pull ROMEN high. If this goes low within xxms of startup, then assume we are connected to CPC
    gpio_put(RESET_GPIO, 0);
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);

    CPC_RELEASE_RESET();

    while (to_ms_since_boot(get_absolute_time()) < 500) {
        // if ROMEN goes low, start emulation
        if (gpio_get(ROMEN_GPIO) == 0) {
            cpc_mode();
        }
    }
    // If we get here assume that we are not plugged in to a CPC - emuulate a USB drive
    usb_mode();
}

