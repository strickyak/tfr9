set(PROJECT tmanager)
cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()

add_executable(${PROJECT} tmanager.cpp)
pico_generate_pio_header(${PROJECT}  ${CMAKE_CURRENT_LIST_DIR}/tpio.pio)
pico_generate_pio_header(${PROJECT}  ${CMAKE_CURRENT_LIST_DIR}/latch.pio)

target_sources(${PROJECT} PRIVATE tmanager.cpp ram.inc.h level1.disk.h level1.rom.h level2.disk.h level2.rom.h tfr9ports.gen.h)
target_link_libraries(${PROJECT} PRIVATE pico_stdlib hardware_pio)
pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

#pico_set_binary_type(tmanager no_flash)
#pico_set_binary_type(tmanager copy_to_ram)
