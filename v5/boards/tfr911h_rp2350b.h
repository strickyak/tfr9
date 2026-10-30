#ifndef _BOARDS_TFR911H_RP2350B_H
#define _BOARDS_TFR911H_RP2350B_H

// For C++ compatibility
#ifdef __cplusplus
extern "C" {
#endif

// 1. Define your board's core features
#define PICO_DEFAULT_LED_PIN 25

// The chip is an RP2350B (48 GPIOs, QFN-80).  The SDK leaves this undefined (== 0) by default, but say it
// explicitly like solderparty_rp2350_stamp_xl.h does (spec S35).
#define PICO_RP2350A 0

// All TFR/911H boards carry 16 MB of QSPI flash (confirmed; spec section 13 Q9).  Without this the SDK assumes
// 2 MB, while LittleFS lives at 4 MB..15 MB (lfs-centipede.h) and the flash label at the last 4 KB.
// (The pico_board_cmake_set_default line is read by the SDK's CMake to size the linker's FLASH region;
// a plain #define alone would only reach the C code.)
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// Notice we are INTENTIONALLY NOT defining PICO_VBUS_PIN.
// This leaves GPIO24 completely free for your own use.

#ifdef __cplusplus
}
#endif

#endif // _BOARDS_TFR911H_RP2350B_H
