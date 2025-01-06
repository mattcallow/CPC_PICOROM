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

foreach(target  cpc_rom_emulator) # cpc_rom_emulator_200 cpc_rom_emulator_210 cpc_rom_emulator_220 cpc_rom_emulator_230 cpc_rom_emulator_240 cpc_rom_emulator_250 cpc_rom_emulator_260 cpc_rom_emulator_270)
    add_executable(${target}
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
    pico_set_linker_script(${target} ${CMAKE_SOURCE_DIR}/memmap_custom.ld)
    pico_generate_pio_header(${target} ${CMAKE_CURRENT_LIST_DIR}/latch.pio)

    target_include_directories(${target} PUBLIC
    fatfs/source
    SPIFTL
    ${CMAKE_CURRENT_LIST_DIR}
    )

    # Add pico_stdlib library which aggregates commonly used features
    target_link_libraries(${target} 
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

    pico_enable_stdio_usb(${target} 1)
    pico_enable_stdio_uart(${target} 0)

    # create map/bin/hex/uf2 file in addition to ELF.
    pico_add_extra_outputs(${target})

    # pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
    # target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
    # pico_set_boot_stage2(cpc_rom_emulator slower_boot2)

    # Make a copy of the firmware file
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${target}.uf2  ../firmware/${target}.uf2
    )

endforeach()

#target_compile_definitions(cpc_rom_emulator_200 PRIVATE CLOCK_SPEED_KHZ=200000)
#target_compile_definitions(cpc_rom_emulator_210 PRIVATE CLOCK_SPEED_KHZ=210000)
#target_compile_definitions(cpc_rom_emulator_220 PRIVATE CLOCK_SPEED_KHZ=220000)
#target_compile_definitions(cpc_rom_emulator_230 PRIVATE CLOCK_SPEED_KHZ=230000)
#target_compile_definitions(cpc_rom_emulator_240 PRIVATE CLOCK_SPEED_KHZ=240000)
#target_compile_definitions(cpc_rom_emulator_250 PRIVATE CLOCK_SPEED_KHZ=250000)
#target_compile_definitions(cpc_rom_emulator_260 PRIVATE CLOCK_SPEED_KHZ=260000)
#target_compile_definitions(cpc_rom_emulator_270 PRIVATE CLOCK_SPEED_KHZ=270000)


add_custom_command(OUTPUT "picorom.rom" picorom.map
    COMMAND z88dk.z88dk-z80asm 
        -v -s -l -m -b -mz80       
        -o../../build/picorom.rom 
        picorom.s
    WORKING_DIRECTORY "../src/z80"
    DEPENDS z80/picorom.s
)

add_custom_command(OUTPUT "../firmware/picorom.rom"
    COMMAND cp picorom.rom ../firmware/picorom.rom
    DEPENDS picorom.rom
)

add_custom_target(generate_rom ALL
    DEPENDS "../firmware/picorom.rom")

add_compile_definitions(
    FTL_DEBUG=0
    FLASH_DEBUG=0
    MSC_DRIVER_DEBUG=0
)
