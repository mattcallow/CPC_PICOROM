// CPC ROM emulator
// Matt Callow March 2023
// Updated Jan 2025
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

#undef DEBUG_TO_SERIAL
#undef DEBUG_TO_FILE
#define VER_MAJOR 3
#define VER_MINOR 1
#define VER_PATCH 1
#ifndef CLOCK_SPEED_KHZ
// overclock speed - pick the lowest freq that works reliably
//#define CLOCK_SPEED_KHZ 200000
//#define CLOCK_SPEED_KHZ 225000
#define CLOCK_SPEED_KHZ 250000
//#define CLOCK_SPEED_KHZ 260000
#endif

// not enough RAM for 16
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
#define NO_ROM 0xff
static volatile uint8_t rom_bank = 0; // index into upper ROMS, 0xff = no ROM
static volatile  uint16_t upper_roms = 0; // bitmask to indicate which ROM banks are active

static FATFS filesystem;

// values from the linker
extern uint32_t __FLASH_START[];
extern uint32_t __FLASH_LEN[];
extern uint32_t __DRIVE_START[];
extern uint32_t __DRIVE_LEN[];


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

#define CPC_ASSERT_RESET()   do { debug("CPC_ASSERT_RESET"); gpio_set_dir(RESET_GPIO, GPIO_OUT); } while(0)
#define CPC_RELEASE_RESET()  do { debug("CPC_RELEASE_RESET"); gpio_set_dir(RESET_GPIO, GPIO_IN); } while(0)

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

void format() {
    FRESULT res;        /* API result code */
    FIL fp;
    BYTE work[FF_MAX_SS]; /* Work area (larger is better for processing time) */
    MKFS_PARM params = {
        FM_FAT,
        1,
        0,
        0,
        0
    };
    res = f_mkfs("", &params, work, sizeof(work));
    if (res) fatal(res);
    f_mount(&filesystem, "", 1);
    f_setlabel("PICOROM");
    f_open(&fp, "README.TXT", FA_CREATE_ALWAYS|FA_WRITE);
    f_printf(&fp, "Welcome to PICOROM %d.%d.%d\n", VER_MAJOR, VER_MINOR, VER_PATCH );
    f_printf(&fp, "Copy your ROMs and config files here.\n");
    f_close(&fp);

}
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
        flash_format();
        flash_init();
        format();
        // and reset
        watchdog_enable(1, 1);
        while(1);
    }
}

// flash the LED 
void led_task() {
    static int64_t timer = 0;
    static bool led = false;

    if (to_ms_since_boot(get_absolute_time()) -  timer > 500) {
        gpio_put(PICO_DEFAULT_LED_PIN, led);
        timer = to_ms_since_boot(get_absolute_time()) + 500;
        led = !led;
    }
}

void usb_mode() {
    board_init();
    tud_init(BOARD_TUD_RHPORT);
    stdio_init_all();  
    f_unmount("");
    while(1) // the mainloop
    {
        button_task();
        tud_task(); // device task
        led_task();
    } 
}

void debug(const char *msg) {
#ifdef DEBUG_TO_FILE
    FIL fp;
    UINT l;
    f_open(&fp, "DEBUG.TXT", FA_WRITE|FA_OPEN_APPEND);
    f_printf(&fp, "%06d: %s\n", to_ms_since_boot(get_absolute_time()), msg);
    f_close(&fp);
#endif
}

void fdebug(const char *fmt, ...) {
#ifdef DEBUG_TO_FILE
    FIL fp;
    UINT l;
    char buf[256];
    va_list args;
    va_start (args, fmt);
    vsprintf (buf, fmt, args);
    va_end (args);
    f_open(&fp, "DEBUG.TXT", FA_WRITE|FA_OPEN_APPEND);
    f_printf(&fp, "%06d: %s\n", to_ms_since_boot(get_absolute_time()), buf);
    f_close(&fp);
#endif
}


#pragma pack(1)
typedef struct {
    uint8_t user_number;
    char filename[8];
    char ext[3];
    char zeros[4];
    uint8_t block_number;
    uint8_t last_block;
    uint8_t file_type;
    uint16_t data_length;
    uint16_t data_location;
    uint8_t first_block;
    uint16_t logical_length;
    uint16_t entry_address;
    char unused[36];
    uint8_t real_length_h;
    uint16_t real_length;
    uint16_t checksum;
    char unused2[59];
} amsdos_header_t;
#pragma pack()

bool load_rom(const TCHAR* path, void* dest) {
    FIL fp;
    FRESULT fr;
    UINT bytes_read;
    FILINFO fno;
    fdebug("Loading %s", path);
    if (f_stat(path, &fno) != FR_OK) return false;
    if (f_open(&fp, path, FA_READ) != FR_OK) return false;
    fr = f_read(&fp, dest, 128, &bytes_read);
    if (fr != FR_OK) {
        f_close(&fp);
        fdebug("Failed to read header from %s", path);
        return false;
    }
    amsdos_header_t *hdr = (amsdos_header_t *)dest;
    uint16_t chksum = 0;
    for (int i=0;i<67;i++) {
        chksum += *(uint8_t *)(dest+i);
    }
    UINT btr;
    fdebug("Calculated chksum 0X%02X header chksum 0X%02X", chksum, hdr->checksum);
    if (chksum == hdr->checksum) {
        btr = hdr->logical_length;
    } else {
        f_rewind(&fp);
        btr = ROM_SIZE;
    }
    fr = f_read(&fp, dest, btr, &bytes_read);
    fdebug("btr=%d bytes_read=%d fr=%d", btr, bytes_read, fr);
    f_close(&fp);
    if (fr != FR_OK) return false;
    return true;
}

bool load_lower_rom(const TCHAR* path) {
    return load_rom(path, (void *)LOWER_ROM);
}

void remove_upper_rom(int bank) {
    upper_roms &= ~(1<<bank);
}

bool load_upper_rom(const TCHAR* path, int bank) {
    if ((bank < 0) || (bank >= NUM_ROM_BANKS)) return false;
    bool ret = load_rom(path, (void *)UPPER_ROMS[bank]);
    if (ret) {
        upper_roms |= (1<<bank);
    }
    return ret;
}

bool load_config(const TCHAR *filename) 
{
    FIL fp;
    TCHAR buf[256];
    char *token;
	const char delim[]=": 	";
 	int bank;

    debug(filename);
    if (f_open(&fp, filename, FA_READ)) {
        return false;
    }
    upper_roms = 0;
    while(!f_eof(&fp)) {
        f_gets(buf, sizeof(buf), &fp);
        token=strtok(buf, delim);
        if (token == NULL) {
            continue;
        } else if (*token == 'L') {
            load_lower_rom(strtok(NULL, delim));
        } else if (isdigit(*token)) {
            bank = atoi(token);
            load_upper_rom(strtok(NULL, delim), bank);
        }
    }
    f_close(&fp);
    return true;
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
                if (rom_bank == NO_ROM) {
                     // set data bus as input (HiZ)
                    gpio_set_dir_in_masked(DATA_BUS_MASK);
                } else {
                    // output upper ROM data
                    gpio_put_masked(DATA_BUS_MASK, UPPER_ROMS[rom_bank][gpio&ADDRESS_BUS_MASK] << 14);
                    gpio_set_dir_out_masked(DATA_BUS_MASK);
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

#define RESP_BUF        0x3F00
#define CMD_PREFIX_BYTE 0xfc

#define CMD_PICOLOAD    0xff
#define CMD_LED         0xfe
#define CMD_ROMDIR1		0xfd
#define CMD_ROMDIR2		0xfc
#define CMD_ROMLIST1	0xfb
#define CMD_ROMLIST2    0xfa
#define CMD_ROMIN       0xf9
#define CMD_ROMOUT      0xf8
#define CMD_ROMSET      0xf7

void __not_in_flash_func(handle_latch)(void)
{
    int cmd = 0;
    int list_index = 0;
    int num_params = 0;
    int params[4];
    char buf[256];
    FRESULT res;
    DIR dir;
    FILINFO fno;
    while(1) {
        uint8_t latch =  pio_sm_get_blocking(pio, sm)  & 0xff;
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
                        if (rom_bank >= NUM_ROM_BANKS) rom_bank = NO_ROM;
                        else if ((upper_roms & (1<<rom_bank)) == 0) rom_bank = NO_ROM;
                }
                break;
            case -1:
                cmd = latch;
                num_params = 0;
                sprintf((char *)&UPPER_ROMS[rom_bank][RESP_BUF+0x40], "cmd:%d list_index:%d rom_bank:%d NUM_ROM_BANKS:%d upper_roms:0x%02x", 
                    cmd, list_index, rom_bank, NUM_ROM_BANKS, upper_roms);
                debug((char *)&UPPER_ROMS[rom_bank][RESP_BUF+0x40]);
                switch(cmd) {
                    case CMD_ROMDIR1: // dir
                        res = f_opendir(&dir, "/");  
                        // fall through
                    case CMD_ROMDIR2: // next dir
                        res = f_readdir(&dir, &fno);  
                        if (res != FR_OK || fno.fname[0] == 0) {
                            f_closedir(&dir);
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 1; // done
                        } else {
                            sprintf((char *)&UPPER_ROMS[rom_bank][RESP_BUF+3], "%-32s %6u", fno.fname, fno.fsize);
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                            UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                        }
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        cmd = 0;
                        break;
                    case CMD_ROMLIST1:
                        list_index = 0;
                        sprintf((char *)&UPPER_ROMS[rom_bank][RESP_BUF+3], "FW: %d.%d.%d %d MHz ROM: %d.%d%d ROMS: %04X", 
                                VER_MAJOR, VER_MINOR, VER_PATCH,
                                clock_get_hz(clk_sys)/1000000,
                                UPPER_ROMS[rom_bank][1],UPPER_ROMS[rom_bank][2],UPPER_ROMS[rom_bank][3],
                                upper_roms
                            );
                        debug((char *)&UPPER_ROMS[rom_bank][RESP_BUF+3]);
                        UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                        UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        cmd = 0;
                        break;
                    case CMD_ROMLIST2: // next rom
                        if (list_index < NUM_ROM_BANKS) {
                            uint8_t type = UPPER_ROMS[list_index][0];
                            uint8_t major = UPPER_ROMS[list_index][1];
                            uint8_t minor = UPPER_ROMS[list_index][2];
                            uint8_t patch = UPPER_ROMS[list_index][3];
                            if (upper_roms & (1<<list_index)) {
                                buf[0] = 0; // ensure buf is null terminated
                                if (type < 2 || type == 0x80) {
                                    uint16_t name_table = (((uint16_t)UPPER_ROMS[list_index][5] << 8) + UPPER_ROMS[list_index][4]) - 0xc000;
                                    int i=0;
                                    do {
                                        buf[i] = UPPER_ROMS[list_index][name_table+i] & 0x7f;
                                    } while(i <31 && UPPER_ROMS[list_index][name_table+i++]< 0x80);
                                    buf[i] = 0;
                                } else if (type == 2) {
                                    strcpy(buf, "-extension ROM- ");
                                }
                                sprintf((char *)&UPPER_ROMS[rom_bank][RESP_BUF+3], "%2d: %02x %-16s %d.%d%d", 
                                    list_index, 
                                    type, 
                                    buf,
                                    major, 
                                    minor, 
                                    patch
                                );
                            } else {
                                sprintf((char *)&UPPER_ROMS[rom_bank][RESP_BUF+3], "%2d: -- Not present", list_index);
                            }
                            debug((char *)&UPPER_ROMS[rom_bank][RESP_BUF+3]);
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                            UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                            list_index++;
                        } else {
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 1; // status
                            debug("End of ROM list");
                        }
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        cmd = 0;
                        break;              
                    case CMD_ROMIN: // Load ROM into bank
                        num_params = 1;
                        break;
                    case CMD_ROMOUT: // unload ROM from bank
                    case CMD_LED: 
                        num_params = 1;
                        break;
                    case CMD_ROMSET:
                        memset(buf, 0, sizeof(buf));
                        num_params =  pio_sm_get_blocking(pio, sm)  & 0xff; // get string length
                        for (int i=0;i<num_params;i++) {
                            buf[i] = pio_sm_get_blocking(pio, sm)  & 0xff;  // read string info buffer
                        }
                        sprintf((char *)&UPPER_ROMS[rom_bank][RESP_BUF+0x80], "np:%d buf:%s", num_params, buf); // debug
                        if (!load_config(buf)) {
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                            UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                            strcpy((char *)&UPPER_ROMS[rom_bank][RESP_BUF+3], "Failed to load Config");
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                        } else {
                            CPC_ASSERT_RESET();
                            sleep_ms(10);
                            CPC_RELEASE_RESET();
                        }
                        cmd = 0;
                        break;
                    case CMD_PICOLOAD:
                        CPC_ASSERT_RESET();
                        usb_mode();
                        //reset_usb_boot(0, 0);
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
                            // get filename
                            memset(buf, 0, sizeof(buf));
                            num_params =  pio_sm_get_blocking(pio, sm)  & 0xff; // get string length
                            for (int i=0;i<num_params;i++) {
                                buf[i] = pio_sm_get_blocking(pio, sm)  & 0xff;  // read string info buffer
                            }
                            sprintf((char *)&UPPER_ROMS[rom_bank][RESP_BUF+3], "ROMIN,%d, %s", params[0], buf);
                            if ((params[0] >= NUM_ROM_BANKS) || (params[0] < 0)) {
                                UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                                UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                                strcpy((char *)&UPPER_ROMS[rom_bank][RESP_BUF+3], "Invalid bank number");
                                UPPER_ROMS[rom_bank][RESP_BUF]++;
                            } else if (!load_upper_rom(buf, params[0])) {
                                UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                                UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                                strcpy((char *)&UPPER_ROMS[rom_bank][RESP_BUF+3], "Failed to load ROM");
                                UPPER_ROMS[rom_bank][RESP_BUF]++;
                            } else {
                                CPC_ASSERT_RESET();
                                sleep_ms(10);
                                CPC_RELEASE_RESET();
                            }
                            break;
                        case CMD_ROMOUT:
                            CPC_ASSERT_RESET();
                            sprintf((char *)&UPPER_ROMS[rom_bank][RESP_BUF+3], "ROMOUT,%d", params[0]);
                            if (params[0] >= NUM_ROM_BANKS) params[0] = NUM_ROM_BANKS-1;
                            if (params[0] < 0) params[0] = 0;
                            remove_upper_rom(params[0]);
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                            CPC_RELEASE_RESET();
                            break;
                        case CMD_LED:
                            //printf("LED,%d latch=%d num_params=%d\n", params[0], latch, num_params);
                            gpio_put(PICO_DEFAULT_LED_PIN, params[0]!=0);
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                            break;

                    }
                    cmd = 0;
                }
        }
    }
}

void cpc_mode() {
    CPC_ASSERT_RESET();
    upper_roms = 0;
    if (f_mount(&filesystem, "", 1)) {
        format();
    }
    debug("Drive mounted");
    if (!load_config("DEFAULT.CFG")) {
        debug("default config failed");
        if (!load_lower_rom("OS_6128.ROM")) fatal(4);
        debug("OS loaded");
        if (!load_upper_rom("BASIC_1.1.ROM", 0)) fatal(5);
        debug("basic loaded");
        load_upper_rom("picorom.rom", 1);
    }
    set_sys_clock_khz(CLOCK_SPEED_KHZ, true);
    multicore_launch_core1(emulate);
    uint offset = pio_add_program(pio, &latch_program);
    latch_program_init(pio, sm, offset);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    CPC_RELEASE_RESET();
    handle_latch();
    debug("ERROR - should never reach here");
}


int main() {
#ifdef USE_XIP_CACHE_AS_RAM
    // disable XIP cache - this frees up the cache RAM for variable storage
    hw_clear_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_EN_BITS);
#endif
#ifdef DEBUG_TO_SERIAL
    board_init();
    tud_init(BOARD_TUD_RHPORT);
    stdio_init_all();
    /*
    for (int i=0;i<50;i++) {
        printf("%d ", i);
        sleep_ms(500);
    }
    */
    //while (!tud_cdc_connected()) { sleep_ms(100);  };
    printf("tud_cdc_connected() %s\n", __TIMESTAMP__);
    printf("__FLASH_START  0x%08x __FLASH_LEN  0x%08x\n", __FLASH_START, __FLASH_LEN);
    printf("__DRIVE_START    0x%08x __DRIVE_LEN    0x%08x\n", __DRIVE_START, __DRIVE_LEN);
#endif
    gpio_init_mask(FULL_MASK);
    gpio_set_dir_in_masked(FULL_MASK);
    gpio_pull_down(WRITE_LATCH_GPIO); // pull down for diode OR
    gpio_pull_up(ROMEN_GPIO); // pull ROMEN high. If this goes low within xxms of startup, then assume we are connected to CPC
    gpio_put(RESET_GPIO, 0);
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    flash_init();

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

