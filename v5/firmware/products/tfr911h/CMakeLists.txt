# TFR/911H firmware.  Phase 1, step A: a verbatim import of v4's tfr911h build, re-pathed to the v5 tree.
# Build with:  make tfr911h   (from v5/; output in build/)
cmake_minimum_required(VERSION 3.13)
set(PROJECT tfr911h_v4)    # TODO(v5): rename to tfr911h once parity is proven

set(V5_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(LIB     ${V5_ROOT}/firmware/lib)
set(THIRD   ${V5_ROOT}/third_party)

list(APPEND PICO_BOARD_HEADER_DIRS ${V5_ROOT}/boards)
set(PICO_BOARD tfr911h_rp2350b)

set(PICO_PLATFORM rp2350-arm-s)
set(PICO_RP2350A 0)
set(PICO_RP2350B 1)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)
pico_sdk_init()

add_executable(${PROJECT} main.cpp)

# Generate PIO header from TFR911's PIO program
pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_LIST_DIR}/hamster.pio)

target_sources(${PROJECT} PRIVATE
    main.cpp

    # LittleFS
    ${THIRD}/littlefs/lfs.c
    ${THIRD}/littlefs/lfs_util.c
    ${LIB}/fs/lfs-centipede.c

    # miniz (for compressed VFS operations)
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
    ${THIRD}
    ${THIRD}/littlefs
    ${THIRD}/tcl6.7c
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

# Compile definitions (TCL_MEM_DEBUG stays on for now: spec section 13 Q10)
add_compile_definitions(TCL_MEM_DEBUG=1)
# miniz extraction buffers: 4 KB instead of 64 KB (spec S38; only ~68 KB of heap is free)
add_compile_definitions(MZ_ZIP_IO_BUF_SIZE=4096)
# The SDK's operator new is just malloc(): with exceptions off, a failed `new` returns NULL and the caller then
# writes through it (silent hang).  We supply our own (core/oom_abort.h) that aborts with a message.  Spec S49.
add_compile_definitions(PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1)
# By default the SDK's malloc wrapper calls panic("Out of memory") when malloc fails, so malloc NEVER returns
# NULL and no caller's failure handling can run (miniz, TryAlloc, lfs ...); the board prints ABORT and hangs.
# With 0, malloc returns NULL as in standard C.  Unchecked callers: Tcl's ckalloc and our operator new
# already check and abort with a message.  Spec S49/S51.
add_compile_definitions(PICO_MALLOC_PANIC=0)
add_compile_definitions(TCL_GENERIC_ONLY=1)

### pico_set_binary_type(${PROJECT} copy_to_ram)
