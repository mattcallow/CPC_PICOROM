cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(cpc_rom)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# rest of your project
# set(PICO_DEFAULT_BINARY_TYPE copy_to_ram)

add_executable(cpc_rom_emulator
    main.c
)

# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(cpc_rom_emulator pico_stdlib)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(cpc_rom_emulator)

# pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
# target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
# pico_set_boot_stage2(cpc_rom_emulator slower_boot2)