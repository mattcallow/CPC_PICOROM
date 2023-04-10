// CPC ROM emulator
// Matt Callow March 2023
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/vreg.h"

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
#include "roms/AmstradDiagLower.rom.h"
#include "roms/AmstradDiagUpper.rom.h"

// DOS ROMS
#include "roms/AMSDOS_0.5.ROM.h"
#include "roms/AMSDOS_0.7.ROM.h"
#include "roms/PARADOS.ROM.h"

// Util ROMS
#include "roms/pt12a.rom.h"
#include "roms/SYSX21.ROM.h"
#include "roms/TOOLKIT.ROM.h"
#include "roms/SUPER.ROM.h"
#include "roms/UTOP107.ROM.h"
#include "roms/Utopia_v1_25b.ROM.h"

#include "roms/SuperRplus.rom.h"

// App ROMS
#include "roms/Protext.rom.h"
#include "roms/maxam15.rom.h"

// Game ROMS
#include "roms/Arkanoid.rom.h"
#include "roms/PACMAN.ROM.h"
#include "roms/Classic_Invaders.rom.h"
#include "roms/Roland_On_The_Ropes.rom.h"
#include "roms/Donkey_Kong.rom.h"   // needs OS 1.1
#include "roms/Gauntlet.rom.h"
#include "roms/Blagger.rom.h"
#include "roms/Hunchback.rom.h"
#include "roms/Tempest.rom.h"
#include "roms/EGGRSX.ROM.h"
#include "roms/Manic_Miner.rom.h"

#define NUM_BANKS 8
static const uint8_t const* LOWER_ROM;
static const uint8_t const* UPPER_ROMS[NUM_BANKS];

#define BASIC_ROM BASIC_1_0_ROM
#define OS_ROM OS_464_ROM
// #define DIAG_ROM AmstradDiagLower_rom

const uint32_t ADDRESS_BUS_MASK = 0x3fff;
const uint32_t DATA_BUS_MASK    = 0xff << 14;
const uint32_t ROMEN_GPIO = 22;
const uint32_t ROMEN_MASK       = 1 << ROMEN_GPIO;
const uint32_t A15_GPIO = 26;
const uint32_t A15_MASK         = 1 << A15_GPIO;
const uint32_t WRITE_LATCH_GPIO = 27;
const uint32_t WRITE_LATCH_MASK = 1 << WRITE_LATCH_GPIO;
const uint32_t BUTTON_GPIO = 28;
const uint32_t BUTTON_MASK = 1 << BUTTON_GPIO;

const uint32_t FULL_MASK = ADDRESS_BUS_MASK|DATA_BUS_MASK|ROMEN_MASK|A15_MASK|WRITE_LATCH_MASK|BUTTON_MASK;

void emulate(void)
{
    int rom_bank = 0;
    while(1) {
        uint32_t gpio = gpio_get_all();
        uint8_t data;
        if ((gpio & ROMEN_MASK) == 0) {
            if (gpio & A15_MASK) {
                data = UPPER_ROMS[rom_bank][gpio&ADDRESS_BUS_MASK];
            } else {
                data = LOWER_ROM[gpio&ADDRESS_BUS_MASK];
            }
            gpio_put_masked(DATA_BUS_MASK, data << 14);
            gpio_set_dir_out_masked(DATA_BUS_MASK);
        } else {
            gpio_set_dir_in_masked(DATA_BUS_MASK);
            if ((gpio & WRITE_LATCH_MASK ) == 0) {
                rom_bank = (gpio >> 14) & 0xff;
                if (rom_bank >= NUM_BANKS) rom_bank = 0;
            } 
        }
    }
}

int main() {
    // Set required ROMs, for blank roms, replace with BASIC_ROM
    UPPER_ROMS[0] = BASIC_ROM;
    UPPER_ROMS[1] = Protext_rom;
    UPPER_ROMS[2] = maxam15_rom;
    UPPER_ROMS[3] = BASIC_ROM;
    UPPER_ROMS[4] = Arkanoid_rom;
    UPPER_ROMS[5] = BASIC_ROM;
    UPPER_ROMS[6] = Utopia_v1_25b_ROM;
    UPPER_ROMS[7] = AMSDOS_0_7_ROM;

    gpio_init_mask(FULL_MASK);
    gpio_set_dir_in_masked(FULL_MASK);
    gpio_pull_down(WRITE_LATCH_GPIO); // pull down for diode OR

#ifdef DIAG_ROM
    gpio_disable_pulls(BUTTON_GPIO);
    gpio_pull_up(BUTTON_GPIO); // pull up for button
    if (gpio_get(BUTTON_GPIO) == 0) {
        LOWER_ROM = DIAG_ROM;
    } else {
        LOWER_ROM = OS_ROM;
    }
#else
    LOWER_ROM = OS_ROM;
#endif
    // overclock
    set_sys_clock_khz(200000, true);

    emulate();
}

