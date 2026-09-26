#ifndef CENTIPEDE_FIRMWARE_ABORT_H_
#define CENTIPEDE_FIRMWARE_ABORT_H_

// Centipede abort/panic — malloc-free, safe to call from any context.
//
// centipede_abort(msg): Print msg to USB tether, then hang forever
// waiting for physical reset.
//
// panic(): Provides the definition for the Pico SDK's panic() declaration.
// Tcl also calls panic().  Our definition calls centipede_abort().
//
// These functions never return.  They do not call malloc, free, new,
// delete, printf, or any STL functions.  They use only putchar_raw
// for USB CDC output.
//
// TODO: Also print to CoCo screen via poke, once we figure out how
// to do it safely in a panic context (foreground core may be stuck).

#include "../util/cobs.h"

extern "C" {
extern int putchar_raw(int c);

// RP2350B address space validation.
// SRAM: 0x20000000 – 0x20082000 (520 KB)
// Flash (XIP): 0x10000000 – 0x11000000 (16 MB)
static inline bool _abort_ptr_valid(const void* p) {
  unsigned int a = (unsigned int)(uintptr_t)p;
  if (a >= 0x20000000u && a < 0x20082000u) return true;   // SRAM
  if (a >= 0x10000000u && a < 0x11000000u) return true;   // Flash
  return false;
}

static inline bool _abort_is_printable(char c) {
  return (c >= 0x20 && c <= 0x7E) || c == '\n';
}

// Send one character to USB CDC as a C_PUTCHAR COBS packet.
// Does not check usb_tether_ok() — we're dying anyway.
// Does not use malloc — uses a small stack buffer.
static inline void _abort_usb_char(char c) {
  unsigned char pkt[2] = {193 /* C_PUTCHAR */, (unsigned char)c};
  CobsEncodeAndTransmit(pkt, 2, [](int ch) { putchar_raw(ch); });
}

// Send a string to USB CDC, one character at a time via COBS.
static inline void _abort_usb_string(const char* s, int maxlen) {
  for (int i = 0; i < maxlen && s[i]; i++) {
    if (_abort_is_printable(s[i])) {
      _abort_usb_char(s[i]);
    } else {
      _abort_usb_char('?');
    }
  }
}

}  // extern "C"

// ---------------------------------------------------------------
// centipede_abort — the main entry point.
// Prints the first 32 characters of msg to USB tether, then hangs.
// Safe to call with a garbage pointer (validated before use).
// ---------------------------------------------------------------
extern "C" __attribute__((noreturn))
void centipede_abort(const char* msg) {
  _abort_usb_string("\n*** ABORT: ", 12);

  if (msg && _abort_ptr_valid(msg)) {
    _abort_usb_string(msg, 32);
  } else {
    _abort_usb_string("(bad ptr)", 10);
  }

  _abort_usb_string(" ***\n", 5);

  // Hang forever.  Physical reset required.
  while (true) {
    __asm volatile("wfi");  // Save power while waiting
  }
}

// ---------------------------------------------------------------
// CENTIPEDE_ASSERT — aborts with a message if condition is false.
// Usage: CENTIPEDE_ASSERT(ptr != nullptr, "g_vfs_coro is null");
// ---------------------------------------------------------------
#define CENTIPEDE_ASSERT(cond, msg) \
  do { if (!(cond)) centipede_abort(msg); } while (0)

#endif  // CENTIPEDE_FIRMWARE_ABORT_H_
