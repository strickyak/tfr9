set(PROJECT demo)
cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD solderparty_rp2350_stamp_xl)
set(PICO_PLATFORM rp2350)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()


add_executable(${PROJECT} demo.cpp)
target_sources(${PROJECT} PRIVATE demo.cpp )
target_link_libraries(${PROJECT} PRIVATE
    pico_stdlib
    hardware_pio
)
pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

add_compile_definitions(OS_LEVEL=${OS_LEVEL})
add_compile_definitions(TRACKING=${TRACKING})

# The Pi Pico can be overclocked if the binary gets copied to ram.
pico_set_binary_type(${PROJECT} copy_to_ram)
