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

target_sources(${PROJECT} PRIVATE tmanager.cpp ../ram.h ../level2.rom.h ../tfr9ports.gen.h)
target_link_libraries(${PROJECT} PRIVATE pico_stdlib hardware_pio)
pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

add_compile_definitions(OS_LEVEL=${OS_LEVEL})
add_compile_definitions(TRACK_RAM=${TRACK_RAM})
add_compile_definitions(TRACE_CYCLES=${TRACE_CYCLES})
add_compile_definitions(TRACE_SWI2=${TRACE_SWI2})

# The Pi Pico can be overclocked better if the binary gets copied to ram.
pico_set_binary_type(tmanager copy_to_ram)
