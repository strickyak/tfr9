set(PROJECT tfr911h_v4)
cmake_minimum_required(VERSION 3.13)

list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD tfr911h_rp2350b)
# set(PICO_BOARD solderparty_rp2350_stamp_xl)

set(PICO_PLATFORM rp2350-arm-s)
set(PICO_RP2350A 0)
set(PICO_RP2350B 1)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()

# Main source lives in ./
add_executable(${PROJECT} v4_tfr911h_main.cpp)

# Generate PIO header from TFR911's PIO program
pico_generate_pio_header(${PROJECT}
    ${CMAKE_CURRENT_LIST_DIR}/hamster.pio)

target_sources(${PROJECT} PRIVATE
    v4_tfr911h_main.cpp

    # LittleFS
    ../firmware/one/littlefs/lfs.c
    ../firmware/one/littlefs/lfs_util.c
    ../firmware/one/littlefs/lfs-centipede.c

    # miniz (for compressed VFS operations)
    ../firmware/one/miniz/miniz.c

    # Tcl 6.7c interpreter
    ../firmware/one/tcl6.7c/regexp.c
    ../firmware/one/tcl6.7c/tclAssem.c
    ../firmware/one/tcl6.7c/tclBasic.c
    ../firmware/one/tcl6.7c/tclCkalloc.c
    ../firmware/one/tcl6.7c/tclCmdAH.c
    ../firmware/one/tcl6.7c/tclCmdIL.c
    ../firmware/one/tcl6.7c/tclCmdMZ.c
    ../firmware/one/tcl6.7c/tclEnv.c
    ../firmware/one/tcl6.7c/tclExpr.c
    ../firmware/one/tcl6.7c/tclGet.c
    ../firmware/one/tcl6.7c/tclGlob.c
    ../firmware/one/tcl6.7c/tclHash.c
    ../firmware/one/tcl6.7c/tclHistory.c
    ../firmware/one/tcl6.7c/tclParse.c
    ../firmware/one/tcl6.7c/tclProc.c
    ../firmware/one/tcl6.7c/tclUtil.c
    ../firmware/one/tcl6.7c/tclVar.c
)

target_include_directories(${PROJECT} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/one/firmware
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/one/util
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/one/littlefs
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/one/tcl6.7c
)

target_link_libraries(${PROJECT} PRIVATE
    hardware_flash
    hardware_pio
    hardware_watchdog
    pico_bootrom
    pico_multicore
    pico_rand
    pico_stdlib
)

pico_add_extra_outputs(${PROJECT})

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

# Compile definitions
add_compile_definitions(TCL_MEM_DEBUG=1)
add_compile_definitions(TCL_GENERIC_ONLY=1)

### pico_set_binary_type(${PROJECT} copy_to_ram)
