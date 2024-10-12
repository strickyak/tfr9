set(PROJECT picow_blink)
cmake_minimum_required(VERSION 3.12)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)

set(PICO_BOARD pico_w)
pico_sdk_init()

add_executable(${PROJECT} picow_blink.c)

target_sources(${PROJECT} PRIVATE picow_blink.c)
target_link_libraries(${PROJECT} PRIVATE
        pico_stdlib              # for core functionality
        pico_cyw43_arch_none     # we need Wifi to access the GPIO, but we don't need anything else
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)
