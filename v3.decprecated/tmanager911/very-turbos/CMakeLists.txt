set(PROJECT veryturbos)
cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()

add_executable(${PROJECT} veryturbos.cpp)
pico_generate_pio_header(${PROJECT}  ${CMAKE_CURRENT_LIST_DIR}/pio_veryturbos.pio)

target_sources(${PROJECT} PRIVATE veryturbos.cpp
    pio_veryturbos.pio.h
    circbuf.h
    turbo9os.h
    turbo9os.rom.h
    turbo9sim.h
)
target_link_libraries(${PROJECT} PRIVATE
    pico_stdlib
    pico_rand
    hardware_pio
    pico_multicore
)
pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

# The Pi Pico can be overclocked if the binary gets copied to ram.
pico_set_binary_type(${PROJECT} copy_to_ram)
