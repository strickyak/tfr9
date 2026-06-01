set(PROJECT tman911)
cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()

add_executable(${PROJECT} tman911.cpp)
pico_generate_pio_header(${PROJECT}  ${CMAKE_CURRENT_LIST_DIR}/t911.pio)

add_compile_definitions(BIG_RAM_KILOS=128)
target_sources(${PROJECT} PRIVATE tman911.cpp
    tmanager.h
    ssd1306.h
    cardkb.h
    cyberterm.h
    ram.h
    cocosdc.h
    emudsk.h
    samvdg.h
    turbo9sim.h
    ../generated/level1.rom.h
    ../generated/level2.rom.h
    tfr9ports.gen.h
)
add_subdirectory(../tmanager905/pico-ssd1306 subdir-ssd1306)
target_link_libraries(${PROJECT} PRIVATE
    pico_stdlib
    pico_rand
    hardware_pio

    pico_ssd1306 hardware_i2c
)
pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

add_compile_definitions(OS_LEVEL=${OS_LEVEL})
add_compile_definitions(TRACKING=${TRACKING})

# The Pi Pico can be overclocked if the binary gets copied to ram.
pico_set_binary_type(${PROJECT} copy_to_ram)
