set(PROJECT tmanager)
cmake_minimum_required(VERSION 3.12)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)

set(PICO_BOARD pico_w)
pico_sdk_init()

add_executable(${PROJECT} tmanager.cpp)
pico_generate_pio_header(${PROJECT}  ${CMAKE_CURRENT_LIST_DIR}/tpio.pio)

target_sources(${PROJECT} PRIVATE tmanager.cpp)
target_link_libraries(${PROJECT} PRIVATE pico_stdlib hardware_pio pico_cyw43_arch_none)
pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)
