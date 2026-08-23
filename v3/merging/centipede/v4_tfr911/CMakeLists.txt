set(PROJECT tfr911h_v4)
cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD solderparty_rp2350_stamp_xl)
set(PICO_PLATFORM rp2350-arm-s)
set(PICO_RP2350A 0)
set(PICO_RP2350B 1)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()

# Main source lives in ../v4/
add_executable(${PROJECT} ../v4/v4_tfr911h_main.cpp)

# Generate PIO header from TFR911's PIO program
pico_generate_pio_header(${PROJECT}
    ${CMAKE_CURRENT_LIST_DIR}/../../../tmanager911/very-turbos/pio_veryturbos.pio)

target_sources(${PROJECT} PRIVATE
    ../v4/v4_tfr911h_main.cpp
)

target_include_directories(${PROJECT} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../v4
    ${CMAKE_CURRENT_LIST_DIR}/../v1/util
)

target_link_libraries(${PROJECT} PRIVATE
    hardware_pio
    hardware_watchdog
    pico_bootrom
    pico_multicore
    pico_rand
    pico_stdlib
)

pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

### pico_set_binary_type(${PROJECT} copy_to_ram)

