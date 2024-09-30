cmake_minimum_required(VERSION 3.13)


# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(cpc_rom)

set(PICO_BOARD pico)
set(FAMILY rp2040)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()


set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPICO_USE_MALLOC_MUTEX=1")
# rest of your project
# set(PICO_DEFAULT_BINARY_TYPE copy_to_ram)

add_executable(cpc_rom_emulator
    main.c
    fatfs_driver.c
    flash.cpp
    usb_msc_driver.c
    usb_descriptors.c
    ${PICO_SDK_PATH}/lib/tinyusb/src/tusb.c
    fatfs/source/ff.c
    fatfs/source/ffsystem.c
    fatfs/source/ffunicode.c
)

pico_set_linker_script(cpc_rom_emulator ${CMAKE_SOURCE_DIR}/memmap_custom.ld)


pico_generate_pio_header(cpc_rom_emulator ${CMAKE_CURRENT_LIST_DIR}/latch.pio)

add_custom_command(OUTPUT "z80/picorom.rom"
	COMMAND "make"
    WORKING_DIRECTORY "../src/z80"
	DEPENDS z80/picorom.s
	)

add_custom_target(generate_rom ALL
    DEPENDS "z80/picorom.rom")

# add fatfs includes
target_include_directories(cpc_rom_emulator PUBLIC
  fatfs/source
  SPIFTL
  ${CMAKE_CURRENT_LIST_DIR}
)

# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(cpc_rom_emulator 
    pico_stdlib
    pico_multicore
    pico_bootrom
    hardware_pio
    hardware_dma
    tinyusb_additions
    tinyusb_board
    tinyusb_device
    hardware_flash
    hardware_sync
   )

pico_enable_stdio_usb(cpc_rom_emulator 1)
pico_enable_stdio_uart(cpc_rom_emulator 0)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(cpc_rom_emulator)

# pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
# target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
# pico_set_boot_stage2(cpc_rom_emulator slower_boot2)

# Make a copy of the firmware file
add_custom_command(TARGET cpc_rom_emulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy cpc_rom_emulator.uf2  ../firmware/cpc_rom_emulator.uf2
)
add_compile_definitions(
    FTL_DEBUG=1
    FLASH_DEBUG=1
    MSC_DRIVER_DEBUG=1
)
