set(PROJECT wifi-scanner)
cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD pico_w)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)

########### set(CMAKE_C_FLAGS_RELEASE     "-O2")

pico_sdk_init()

add_executable(${PROJECT} scanner.cpp)
# pico_generate_pio_header(${PROJECT}  ${CMAKE_CURRENT_LIST_DIR}/tpio.pio)

target_sources(${PROJECT} PRIVATE scanner.cpp )
target_link_libraries(${PROJECT} PRIVATE pico_stdlib hardware_pio pico_cyw43_arch_none pico_lwip_arch)
##### target_link_libraries(${PROJECT} PRIVATE pico_stdlib hardware_pio pico_cyw43_arch_none pico_lwip_arch cyw43-driver)
pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)
