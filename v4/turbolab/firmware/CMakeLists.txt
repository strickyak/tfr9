set(PROJECT turbolab_v4)
cmake_minimum_required(VERSION 3.13)

list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD tfr911h_rp2350b)

set(PICO_PLATFORM rp2350-arm-s)
set(PICO_RP2350A 0)
set(PICO_RP2350B 1)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()

add_executable(${PROJECT} main.cpp)

# Generate PIO header from hamster.pio
pico_generate_pio_header(${PROJECT}
    ${CMAKE_CURRENT_LIST_DIR}/hamster.pio)

target_sources(${PROJECT} PRIVATE
    main.cpp
)

target_include_directories(${PROJECT} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)

target_link_libraries(${PROJECT} PRIVATE
    hardware_flash
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
