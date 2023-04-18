// CPC ROM emulator
// Matt Callow March 2023
#undef DEBUG_CONFIG
#include <string.h>
#include <stdio.h>
#include <tusb.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "hardware/vreg.h"
#include "hardware/pio.h"
#include "hardware/flash.h"
#include "hardware/dma.h"
#include "latch.pio.h"

// Menu ROM
#include "z80/picorom.rom.h"
// OS ROMS
#include "roms/OS_664.ROM.h"
#include "roms/OS_464.ROM.h"
#include "roms/OS_6128.ROM.h"
#include "roms/FW315EN.ROM.h"

// Basic ROMS
#include "roms/BASIC_1.0.ROM.h"
#include "roms/BASIC_664.ROM.h"
#include "roms/BASIC_1.1.ROM.h"

// Diag ROMS
#include "roms/AmstradDiagUpper.rom.h"

// DOS ROMS
#include "roms/AMSDOS_0.5.ROM.h"
#include "roms/AMSDOS_0.7.ROM.h"
#include "roms/PARADOS.ROM.h"

// Util ROMS
#include "roms/pt12a.rom.h"
#include "roms/SYSX21.ROM.h"
#include "roms/TOOLKIT.ROM.h"
#include "roms/Utopia_v1_25b.ROM.h"

// App ROMS
#include "roms/Protext.rom.h"
#include "roms/maxam15.rom.h"

// Game ROMS
#include "roms/Arkanoid.rom.h"
#include "roms/PACMAN.ROM.h"
#include "roms/Classic_Invaders.rom.h"
#include "roms/Roland_On_The_Ropes.rom.h"
#include "roms/Roland_Goes_Digging.rom.h"
#include "roms/Roland_Ahoy.rom.h"
#include "roms/Roland_Goes_Square_Bashing.rom.h"
#include "roms/Donkey_Kong.rom.h"  
#include "roms/Gauntlet.rom.h"
#include "roms/Blagger.rom.h"
#include "roms/Hunchback.rom.h"
#include "roms/Tempest.rom.h"
#include "roms/EGGRSX.ROM.h"
#include "roms/Manic_Miner.rom.h"
#include "roms/One_Man_And_His_Droid.rom.h"
#include "roms/Ohmummy.rom.h"
#include "roms/Tapper.rom.h"
#include "roms/Thrust.rom.h"
#include "roms/Starfire.rom.h"

#define VER_MAJOR 1
#define VER_MINOR 2
#define VER_PATCH 0
// not enough RAM for 16 banks, but could do 12
#define NUM_ROM_BANKS 8
#define ROM_SIZE 16384
// don't CRC that last page of ROM as the PicoROM modifies this
#define CRC_SIZE (ROM_SIZE - 0x100)
static uint8_t LOWER_ROM[ROM_SIZE];
static uint8_t UPPER_ROMS[NUM_ROM_BANKS][ROM_SIZE];
static volatile uint8_t rom_bank = 0;
static volatile bool rom7_enable = false;

typedef struct {
    const uint8_t *rom;
    char *name;
    uint32_t crc;
} rom_entry_t;

static rom_entry_t LOWER_ROMLIST[] = {
    { OS_464_ROM, "OS 1.0"},
    { OS_664_ROM ,"OS 1.1"},
    { OS_6128_ROM, "OS 1.2"},
    { FW315EN_ROM, "FW 3.1"},
    {0,0}
};
static int num_lower_roms = 0;

static rom_entry_t ROMLIST[] = {
    { picorom_rom ,"PicoROM"},      // picorom must be the first in the list
    { BASIC_1_0_ROM, "Basic 1.0"},
    { BASIC_664_ROM, "664 Basic"},
    { BASIC_1_1_ROM, "Basic 1.1"},
    { Protext_rom, "Protext"},
    { maxam15_rom, "Maxam 1.5"},
    { Utopia_v1_25b_ROM, "Utopia 1.25"},
    { pt12a_rom, "Programmers Toolbox V1.2a"},
    { AmstradDiagUpper_rom, "CPC Diag"},
    { AMSDOS_0_5_ROM, "AMS DOS 0.5"},
    { AMSDOS_0_7_ROM, "AMS DOS 0.7"},
    { PARADOS_ROM, "PARADOS"},
    { Donkey_Kong_rom, "Donkey Kong" },
    { Blagger_rom, "Blagger" },
    { PACMAN_ROM, "PacMan" },
    { Arkanoid_rom, "Arkanoid" },
    { Gauntlet_rom, "Gauntlet"},
    { Hunchback_rom, "Hunchback"},
    { Manic_Miner_rom, "Manic Miner"},
    { One_Man_And_His_Droid_rom, "One Man and his Droid"},
    { Roland_Goes_Digging_rom, "Roland Goes Digging"},
    { Roland_On_The_Ropes_rom, "Roland On The Ropes"},
    { Classic_Invaders_rom, "Classic Invaders"},
    { Tapper_rom, "Tapper"},
    { Thrust_rom, "Thrust"},
    { Ohmummy_rom, "Oh Mummy"},
    { Starfire_rom, "Starfile"},
    { 0, 0}
};
static int num_upper_roms = 0;

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

static inline void CPC_ASSERT_RESET()
{
    gpio_set_dir(RESET_GPIO, GPIO_OUT);
}

static inline void CPC_RELEASE_RESET()
{
    gpio_set_dir(RESET_GPIO, GPIO_IN);
}

#define CRC32_INIT                  ((uint32_t)-1l)

static uint8_t sink;

// use a DMA channel to calc the CRC
uint32_t calc_crc32(const void* buf, uint32_t size)
{
    // Get a free channel, panic() if there are none
    int chan = dma_claim_unused_channel(true);

    // 8 bit transfers. The read address increments after each transfer but
    // the write address remains unchanged pointing to the dummy destination.
    // No DREQ is selected, so the DMA transfers as fast as it can.
    dma_channel_config c = dma_channel_get_default_config(chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);

    // (bit-reverse) CRC32 specific sniff set-up
    channel_config_set_sniff_enable(&c, true);
    dma_sniffer_set_data_accumulator(CRC32_INIT);
    dma_sniffer_enable(chan, DMA_SNIFF_CTRL_CALC_VALUE_CRC32, true);

    dma_channel_configure(
        chan,          // Channel to be configured
        &c,            // The configuration we just created
        &sink,     // The (unchanging) write address
        buf,           // The initial read address
        size,     // Total number of transfers inc. appended crc; each is 1 byte
        true           // Start immediately.
    );

    // We could choose to go and do something else whilst the DMA is doing its
    // thing. In this case the processor has nothing else to do, so we just
    // wait for the DMA to finish.
    dma_channel_wait_for_finish_blocking(chan);

    uint32_t crc = dma_sniffer_get_data_accumulator();
    dma_channel_unclaim(chan);
    return crc;
}
// use the last sector (4k) of flash to store config
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
const uint8_t *config_pages = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);
int current_config = -1;

// store 4 config blocks at each on 1K boundary
#define MAX_CONFIG 4
#define CONFIG_SIZE 1024
#define CONFIG_MAGIC 0x7b0
typedef struct {
    uint16_t magic;
    uint32_t ver;
    bool rom7_enable;
    uint32_t lower_rom_crc;
    uint32_t rom_crcs[NUM_ROM_BANKS];
    uint32_t crc;
} config_t;

bool __not_in_flash_func(load_config)(int slot) 
{
    config_t *config = (config_t *)(config_pages+CONFIG_SIZE*slot);
    if (config->magic != CONFIG_MAGIC) {
        return false;
    }
    uint32_t crc = calc_crc32(config, sizeof(config)-sizeof(uint32_t));
#ifdef DEBUG_CONFIG
    printf("calculated crc %lu stored crc %lu\n", crc, config->crc);
#endif
    if (crc != config->crc) {
        return false;
    }
    if (config->ver != VER_MAJOR << 16 + VER_MINOR << 8 + VER_PATCH) {
        return false;
    }
#ifdef DEBUG_CONFIG
    printf("Lower ROM   ");
#endif
    int idx = 0;
    while(LOWER_ROMLIST[idx].rom) {
        if (config->lower_rom_crc == LOWER_ROMLIST[idx].crc) {
            memcpy(LOWER_ROM, LOWER_ROMLIST[idx].rom, ROM_SIZE);
#ifdef DEBUG_CONFIG
            printf("Loaded %d\n", idx);
#endif
            break;
        }
        idx++;
    }
    if (!LOWER_ROMLIST[idx].rom) {
#ifdef DEBUG_CONFIG
        printf("Failed to load lower ROM\n");
#endif
        return false;
    }
    bool picorom_loaded = false;
    for (int i=0;i<NUM_ROM_BANKS;i++) {
#ifdef DEBUG_CONFIG
        printf("Upper ROM %d ", i);
#endif
        rom_entry_t *r = ROMLIST;
        while(r && r->rom) {
            if (config->rom_crcs[i] == r->crc) {
                memcpy(UPPER_ROMS[i], r->rom, ROM_SIZE);
                if (r == ROMLIST) {
                    picorom_loaded = true;
                }
#ifdef DEBUG_CONFIG
                printf("Loaded %d\n", idx);
#endif
                break;
            }
            r++;
        }
        if (!r || !r->rom) {
            // no rom loaded
            memset(UPPER_ROMS[i], 0xff, ROM_SIZE);
        }
    }
    if (!picorom_loaded) {
#ifdef DEBUG_CONFIG
        printf("Failsafe, loading picorom into slot 6\n");
#endif 
        memcpy(UPPER_ROMS[6], ROMLIST[0].rom, ROM_SIZE);
    }
    rom7_enable = config->rom7_enable;
    current_config = slot;
    return true;
}

bool __not_in_flash_func(save_config)(int slot)
{
    uint8_t buf[FLASH_SECTOR_SIZE];
    uint32_t irq_status;
    // cpoy existing config pages to RAM
    memcpy(buf, config_pages, sizeof(buf));
    // update config at slot 'slot'
    config_t *config = (config_t *)(buf+CONFIG_SIZE*slot);
    config->magic = CONFIG_MAGIC;
    config->ver = VER_MAJOR << 16 + VER_MINOR << 8 + VER_PATCH;
    config->rom7_enable = rom7_enable;
    config->lower_rom_crc = calc_crc32(LOWER_ROM, CRC_SIZE);
    for (int i=0;i<NUM_ROM_BANKS;i++) {
        config->rom_crcs[i] = calc_crc32(UPPER_ROMS[i], CRC_SIZE);
    }
    config->crc = calc_crc32(config, sizeof(config)-sizeof(uint32_t));
    // erase and re-save all config
#ifdef DEBUG_CONFIG
    printf("Erasing config 0x%x 0x%x\n", FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
#endif
    irq_status = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(irq_status);
#ifdef DEBUG_CONFIG
    printf("region erased\n");
#endif
    irq_status = save_and_disable_interrupts();
    flash_range_program(FLASH_TARGET_OFFSET, buf, FLASH_SECTOR_SIZE);
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
                if ((rom7_enable == true)|| (rom_bank != 7)) {
                    gpio_put_masked(DATA_BUS_MASK, UPPER_ROMS[rom_bank][gpio&ADDRESS_BUS_MASK] << 14);
                    gpio_set_dir_out_masked(DATA_BUS_MASK);
                } else {
                    gpio_set_dir_in_masked(DATA_BUS_MASK);
                }
            } else {
                gpio_put_masked(DATA_BUS_MASK, LOWER_ROM[gpio&ADDRESS_BUS_MASK] << 14);
                gpio_set_dir_out_masked(DATA_BUS_MASK);

            }
        } else {
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
#define CMD_ROM7 0xFB
#define CMD_ROMDIR1		0x1
#define CMD_ROMDIR2		0x2
#define CMD_464 3
#define CMD_6128 4
#define CMD_664 5
#define CMD_FW31 6
#define CMD_ROMLIST1	0x7
#define CMD_ROMLIST2	0x8

#define CMD_ROMIN 0x10
#define CMD_ROMOUT 0x11

void __not_in_flash_func(handle_latch)(void)
{
    int cmd = 0;
    int list_index = 0;
    int num_params = 0;
    int params[4];
    char buf[32];
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
                        if (strlen(ROMLIST[list_index].name)) {
                            sprintf(&UPPER_ROMS[rom_bank][RESP_BUF+3], "%2d: %s", list_index, ROMLIST[list_index].name);
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                            UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                            list_index++;
                        } else {
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 1; // status
                        }
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        cmd = 0;
                        break;
                    case 3: // switch to Basic 1.0
                        CPC_ASSERT_RESET();
                        memcpy(UPPER_ROMS[0], BASIC_1_0_ROM, ROM_SIZE);
                        memcpy(LOWER_ROM, OS_464_ROM, ROM_SIZE);
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        CPC_RELEASE_RESET();
                        cmd = 0;
                        break;
                    case 4: // switch to Basic 1.1
                        CPC_ASSERT_RESET();
                        memcpy(UPPER_ROMS[0], BASIC_1_1_ROM, ROM_SIZE);
                        memcpy(LOWER_ROM, OS_6128_ROM, ROM_SIZE);
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        CPC_RELEASE_RESET();
                        cmd = 0;
                        break;
                    case 5: // switch to 664 mode
                        CPC_ASSERT_RESET();
                        memcpy(UPPER_ROMS[0], BASIC_664_ROM, ROM_SIZE);
                        memcpy(LOWER_ROM, OS_664_ROM, ROM_SIZE);
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        CPC_RELEASE_RESET();
                        cmd = 0;
                        break;
                    case CMD_FW31:
                        CPC_ASSERT_RESET();
                        memcpy(UPPER_ROMS[0], BASIC_1_1_ROM, ROM_SIZE);
                        memcpy(LOWER_ROM, FW315EN_ROM, ROM_SIZE);
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        CPC_RELEASE_RESET();
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
                            if ((type < 3 || type == 0x80) && (list_index !=7 || rom7_enable)) {
                                uint16_t name_table = (((uint16_t)UPPER_ROMS[list_index][5] << 8) + UPPER_ROMS[list_index][4]) - 0xc000;
                                int i=0;
                                do {
                                    buf[i] = UPPER_ROMS[list_index][name_table+i] & 0x7f;
                                } while(UPPER_ROMS[list_index][name_table+i++]< 0x80);
                                buf[i] = 0;
                            } else {
                                if (list_index == 7 && !rom7_enable) {
                                    strcpy(buf, " - Disabled  -  ");
                                } else {
                                    strcpy(buf, " -Not present-  ");
                                }
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
                    case CMD_ROMIN: // Load ROM into slot
                        num_params = 2;
                        break;
                    case CMD_ROMOUT: // unload ROM from slot
                    case CMD_LED: // LED
                    case CMD_CFGLOAD:
                    case CMD_CFGSAVE:
                    case CMD_ROM7:
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
                            if (params[1] >= num_upper_roms) params[1] = num_upper_roms-1;
                            if (params[1] < 0) params[1] = 0;
                            memcpy(UPPER_ROMS[params[0]], ROMLIST[params[1]].rom, ROM_SIZE);
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                            CPC_RELEASE_RESET();
                            break;
                        case CMD_ROMOUT:
                            CPC_ASSERT_RESET();
                            sprintf(&UPPER_ROMS[rom_bank][RESP_BUF+3], "ROMOUT,%d", params[0]);
                            if (params[0] >= NUM_ROM_BANKS) params[0] = NUM_ROM_BANKS-1;
                            if (params[0] < 0) params[0] = 0;
                            memset(UPPER_ROMS[params[0]], 0xff, ROM_SIZE);
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                            CPC_RELEASE_RESET();
                            break;
                        case CMD_CFGSAVE:
                            // TODO check bounds
                            save_config(params[0]);
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
                        case CMD_ROM7:
                            //printf("LED,%d latch=%d num_params=%d\n", params[0], latch, num_params);
                            rom7_enable = params[0]!=0;
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                            UPPER_ROMS[rom_bank][RESP_BUF]++;
                            break;                        
                    }
                    cmd = 0;
                }
        }
    }
}

int main() {
#ifdef DEBUG_CONFIG
    stdio_init_all();
    while (!tud_cdc_connected()) { sleep_ms(100);  }
    printf("tud_cdc_connected()\n");
    printf("flash size = %d\n", PICO_FLASH_SIZE_BYTES);
#endif
    gpio_init_mask(FULL_MASK);
    gpio_set_dir_in_masked(FULL_MASK);
    gpio_pull_down(WRITE_LATCH_GPIO); // pull down for diode OR
    gpio_put(RESET_GPIO, 0);
    CPC_ASSERT_RESET();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);

    // count roms, calc crcs
    for (rom_entry_t *r=LOWER_ROMLIST;r && r->rom;r++) {
        num_lower_roms++;
        r->crc = calc_crc32(r->rom, CRC_SIZE);
    }
    for (rom_entry_t *r=ROMLIST;r && r->rom;r++) {
        num_upper_roms++;
        r->crc = calc_crc32(r->rom, CRC_SIZE);
    }
    if (load_config(0)) {
#ifdef DEBUG_CONFIG
        printf("Config loaded\n");
#endif
    } else {
#ifdef DEBUG_CONFIG
        printf("Config invalid - loading defaults\n");
#endif
        memcpy(UPPER_ROMS[0],  BASIC_1_0_ROM, ROM_SIZE);
        for (int i=1;i<NUM_ROM_BANKS;i++) {
            memset(UPPER_ROMS[i],  0xff, ROM_SIZE);
        }
        memcpy(LOWER_ROM, OS_464_ROM, OS_464_ROM_len);
        memcpy(UPPER_ROMS[6], picorom_rom, ROM_SIZE);
        rom7_enable = false;
    }
    // overclock - pick the lowest freq that works reliably
    //set_sys_clock_khz(200000, true);
    set_sys_clock_khz(225000, true);
    // set_sys_clock_khz(250000, true);
    multicore_launch_core1(emulate);

    uint offset = pio_add_program(pio, &latch_program);
    latch_program_init(pio, sm, offset);
    CPC_RELEASE_RESET();
    handle_latch();
}

