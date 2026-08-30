set(PROJECT blink)
cmake_minimum_required(VERSION 3.12)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)

set(PICO_BOARD pico)
pico_sdk_init()

add_executable(${PROJECT}
        blink.c
        )

# pull in common dependencies
target_link_libraries(${PROJECT} pico_stdlib)

# create map/bin/hex file etc.
pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)
