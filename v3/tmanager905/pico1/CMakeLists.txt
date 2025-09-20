set(PROJECT tmanager)
cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD pico)
set(PICO_PLATFORM rp2040)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()

add_executable(${PROJECT} tmanager.cpp)
pico_generate_pio_header(${PROJECT}  ${CMAKE_CURRENT_LIST_DIR}/tpio.pio)
pico_generate_pio_header(${PROJECT}  ${CMAKE_CURRENT_LIST_DIR}/latch.pio)

target_sources(${PROJECT} PRIVATE tmanager.cpp
    ../ssd1306.h
    ../ram.h
    ../cocosdc.h
    ../emudsk.h
    ../samvdg.h
    ../turbo9sim.h
    ../../generated/level1.rom.h
    ../../generated/level2.rom.h
    ../tfr9ports.gen.h
)
add_subdirectory(pico-ssd1306)
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

## not for pico1 ## # The Pi Pico can be overclocked if the binary gets copied to ram.
## not for pico1 ## pico_set_binary_type(tmanager copy_to_ram)
