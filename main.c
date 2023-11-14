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


#define VER_MAJOR 2
#define VER_MINOR 0
#define VER_PATCH 0
// not enough RAM for 16 banks, but could do 12
#define NUM_ROM_BANKS 12
#define ROM_SIZE 16384
// don't CRC that last page of ROM as the PicoROM modifies this
#define CRC_SIZE (ROM_SIZE - 0x100)
// RAM copies of the ROMs
static uint8_t LOWER_ROM[ROM_SIZE];
static uint8_t UPPER_ROMS[NUM_ROM_BANKS][ROM_SIZE];
static volatile uint8_t rom_bank = 0;
static volatile bool rom7_enable = false;

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
    uint8_t rom7_enable;
    int8_t lower_rom;
    int8_t upper_roms[NUM_ROM_BANKS];
    char desc[33];
    char spare[12];
} config_t;
#pragma pack(pop)

#define CONFIG_VER 1

#define MAX_ROMS 120
#define INDEX_ENTRY_SIZE sizeof(rom_index_t)
#define INDEX_SIZE 4096
#define CONFIG_SIZE sizeof(config_t)
#define MAX_CONFIG 16
#define CONFIG_MAGIC 0x7b0

static int num_lower_roms = 0;
static int num_upper_roms = 0;

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

#define INSERT_UPPER_ROM(slot, rom_number) { \
    upper_roms[slot] = rom_number; \
    if (rom_number < 0) { \
        memset(UPPER_ROMS[slot], 0xff, ROM_SIZE);\
    } else {\
        memcpy(UPPER_ROMS[slot],  (void *)__ROM_START + INDEX_SIZE+ROM_SIZE*rom_number, ROM_SIZE);\
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
    hw_set_bits(&dma_hw->sniff_ctrl,
		    (DMA_SNIFF_CTRL_OUT_INV_BITS | DMA_SNIFF_CTRL_OUT_REV_BITS));
	
    // dma_hw->sniff_data = 0xffffffff;
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

//uint8_t *config_pages = (uint8_t *) (__CONFIG_START);

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
    rom7_enable = config->rom7_enable;
    current_config = slot;
    return true;
}

bool __not_in_flash_func(save_config)(int slot)
{
    uint8_t buf[FLASH_SECTOR_SIZE];
    uint32_t irq_status;
    // copy existing config pages to RAM
    memcpy(buf, __CONFIG_START, sizeof(buf));
    // update config at slot 'slot'
    config_t *config = (config_t *)(buf+CONFIG_SIZE*slot);
    config->magic = CONFIG_MAGIC;
    config->ver = CONFIG_VER;
    config->rom7_enable = rom7_enable;
    config->lower_rom = lower_rom;
    for (int i=0;i<NUM_ROM_BANKS;i++) {
        config->upper_roms[i] = upper_roms[i];
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
    flash_range_program((uint32_t)__CONFIG_START - XIP_BASE, buf, (size_t)__CONFIG_LEN);
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
                    // TODO 
                    # if 0
                        if (strlen(ROMLIST[list_index].name)) {
                            sprintf(&UPPER_ROMS[rom_bank][RESP_BUF+3], "%2d: %s", list_index, ROMLIST[list_index].name);
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 0; // status=OK
                            UPPER_ROMS[rom_bank][RESP_BUF+2] = 1; // string
                            list_index++;
                        } else {
                            UPPER_ROMS[rom_bank][RESP_BUF+1] = 1; // status
                        }
                    #endif 
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        cmd = 0;
                        break;
                    case 3: // switch to Basic 1.0
                        CPC_ASSERT_RESET();
                        // memcpy(UPPER_ROMS[0], BASIC_1_0_ROM, ROM_SIZE);
                        // memcpy(LOWER_ROM, OS_464_ROM, ROM_SIZE);
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        CPC_RELEASE_RESET();
                        cmd = 0;
                        break;
                    case 4: // switch to Basic 1.1
                        CPC_ASSERT_RESET();
                        // memcpy(UPPER_ROMS[0], BASIC_1_1_ROM, ROM_SIZE);
                        // memcpy(LOWER_ROM, OS_6128_ROM, ROM_SIZE);
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        CPC_RELEASE_RESET();
                        cmd = 0;
                        break;
                    case 5: // switch to 664 mode
                        CPC_ASSERT_RESET();
                        // memcpy(UPPER_ROMS[0], BASIC_664_ROM, ROM_SIZE);
                        // memcpy(LOWER_ROM, OS_664_ROM, ROM_SIZE);
                        UPPER_ROMS[rom_bank][RESP_BUF]++;
                        CPC_RELEASE_RESET();
                        cmd = 0;
                        break;
                    case CMD_FW31:
                        CPC_ASSERT_RESET();
                        // memcpy(UPPER_ROMS[0], BASIC_1_1_ROM, ROM_SIZE);
                        // memcpy(LOWER_ROM, FW315EN_ROM, ROM_SIZE);
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

rom_index_t *rom_index = (rom_index_t *)__ROM_START;

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

int main() {
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
    gpio_put(RESET_GPIO, 0);
    CPC_ASSERT_RESET();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);

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

    // overclock - pick the lowest freq that works reliably
    //set_sys_clock_khz(200000, true);
    //set_sys_clock_khz(225000, true);
    set_sys_clock_khz(250000, true);
    multicore_launch_core1(emulate);
    uint offset = pio_add_program(pio, &latch_program);
    latch_program_init(pio, sm, offset);
    CPC_RELEASE_RESET();
    handle_latch();
}

