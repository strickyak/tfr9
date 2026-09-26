set(PROJECT centipede)
cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD solderparty_rp2350_stamp_xl)
set(PICO_PLATFORM rp2350-arm-s)
set(PICO_RP2350A 0)
set(PICO_RP2350B 1)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()

add_executable(${PROJECT} centipede.cpp)
pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_LIST_DIR}/gerbil.pio)

# Use libraries at /three/ inside local workspace:
set(THREE ${CMAKE_CURRENT_LIST_DIR}/../three)
target_sources(${PROJECT} PRIVATE
    centipede.cpp

    ${THREE}/littlefs/lfs.c
    ${THREE}/littlefs/lfs_util.c
    ${THREE}/littlefs/lfs-centipede.c
    
    ${THREE}/miniz/miniz.c

    # ${THREE}/tcl6.7c/panic.c
    ${THREE}/tcl6.7c/regexp.c
    ${THREE}/tcl6.7c/tclAssem.c
    ${THREE}/tcl6.7c/tclBasic.c
    ${THREE}/tcl6.7c/tclCkalloc.c
    ${THREE}/tcl6.7c/tclCmdAH.c
    ${THREE}/tcl6.7c/tclCmdIL.c
    ${THREE}/tcl6.7c/tclCmdMZ.c
    ${THREE}/tcl6.7c/tclEnv.c
    ${THREE}/tcl6.7c/tclExpr.c
    ${THREE}/tcl6.7c/tclGet.c
    ${THREE}/tcl6.7c/tclGlob.c
    ${THREE}/tcl6.7c/tclHash.c
    ${THREE}/tcl6.7c/tclHistory.c
    ${THREE}/tcl6.7c/tclParse.c
    ${THREE}/tcl6.7c/tclProc.c
    # ${THREE}/tcl6.7c/tclTest.c
    # ${THREE}/tcl6.7c/tclUnixAZ.c
    # ${THREE}/tcl6.7c/tclUnixStr.c
    # ${THREE}/tcl6.7c/tclUnixUtil.c
    ${THREE}/tcl6.7c/tclUtil.c
    ${THREE}/tcl6.7c/tclVar.c
)
target_include_directories(${PROJECT} PRIVATE
    ${THREE}
    ${THREE}/firmware
)
target_link_libraries(${PROJECT} PRIVATE
    cmsis_core
    hardware_pio
    hardware_pwm
    hardware_watchdog
    pico_bootrom
    pico_multicore
    pico_rand
    pico_stdlib
)

pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

add_compile_definitions(TCL_MEM_DEBUG=1)
add_compile_definitions(TCL_GENERIC_ONLY=1)
add_compile_definitions(PICO_PANIC_FUNCTION=centipede_abort)

# The Pi Pico can be overclocked if the binary gets copied to ram.
# pico_set_binary_type(${PROJECT} copy_to_ram)
