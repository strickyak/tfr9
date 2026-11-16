# Copico Centipede 32z firmware (v5).
# Build with:  make centipede32z   (from v5/; output in build/centipede32z.uf2)
cmake_minimum_required(VERSION 3.13)
set(PROJECT centipede32z)

set(V5_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(LIB     ${V5_ROOT}/firmware/lib)
set(THIRD   ${V5_ROOT}/third_party)

list(APPEND PICO_BOARD_HEADER_DIRS ${V5_ROOT}/boards)
set(PICO_BOARD centipede32z)

set(PICO_PLATFORM rp2350-arm-s)
set(PICO_RP2350A 0)
set(PICO_RP2350B 1)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()

add_executable(${PROJECT} main.cpp)

pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_LIST_DIR}/gerbil.pio)

target_sources(${PROJECT} PRIVATE
    main.cpp

    # LittleFS
    ${THIRD}/littlefs/lfs.c
    ${THIRD}/littlefs/lfs_util.c
    ${LIB}/fs/lfs-centipede.c

    # miniz
    ${THIRD}/miniz/miniz.c

    # Tcl 6.7c interpreter
    ${THIRD}/tcl6.7c/regexp.c
    ${THIRD}/tcl6.7c/tclAssem.c
    ${THIRD}/tcl6.7c/tclBasic.c
    ${THIRD}/tcl6.7c/tclCkalloc.c
    ${THIRD}/tcl6.7c/tclCmdAH.c
    ${THIRD}/tcl6.7c/tclCmdIL.c
    ${THIRD}/tcl6.7c/tclCmdMZ.c
    ${THIRD}/tcl6.7c/tclEnv.c
    ${THIRD}/tcl6.7c/tclExpr.c
    ${THIRD}/tcl6.7c/tclGet.c
    ${THIRD}/tcl6.7c/tclGlob.c
    ${THIRD}/tcl6.7c/tclHash.c
    ${THIRD}/tcl6.7c/tclHistory.c
    ${THIRD}/tcl6.7c/tclParse.c
    ${THIRD}/tcl6.7c/tclProc.c
    ${THIRD}/tcl6.7c/tclUtil.c
    ${THIRD}/tcl6.7c/tclVar.c
)

target_include_directories(${PROJECT} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${LIB}/core
    ${LIB}/fs
    ${LIB}/tcl
    ${LIB}/devices
    ${LIB}/mem
    ${LIB}/cpu_tracker
    ${V5_ROOT}/firmware/engines
    ${V5_ROOT}/build/generated
    ${THIRD}
    ${THIRD}/littlefs
    ${THIRD}/tcl6.7c
)

target_link_libraries(${PROJECT} PRIVATE
    hardware_flash
    hardware_pio
    hardware_pwm
    hardware_uart
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
add_compile_definitions(MZ_ZIP_IO_BUF_SIZE=4096)
add_compile_definitions(PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1)
add_compile_definitions(PICO_MALLOC_PANIC=0)
add_compile_definitions(TCL_GENERIC_ONLY=1)
