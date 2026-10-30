#ifndef _BOARDS_TFR911H_RP2350B_H
#define _BOARDS_TFR911H_RP2350B_H

// For C++ compatibility
#ifdef __cplusplus
extern "C" {
#endif

// 1. Define your board's core features
#define PICO_DEFAULT_LED_PIN 25

// Notice we are INTENTIONALLY NOT defining PICO_VBUS_PIN.
// This leaves GPIO24 completely free for your own use.

#ifdef __cplusplus
}
#endif

#endif // _BOARDS_TFR911H_RP2350B_H
