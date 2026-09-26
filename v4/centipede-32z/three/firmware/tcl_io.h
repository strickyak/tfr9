#ifndef CENTIPEDE_FIRMWARE_TCL_IO_H_
#define CENTIPEDE_FIRMWARE_TCL_IO_H_

// Tcl I/O multiplexer: routes SpoonFeeder input/output between
// USB CDC (tether stdin/stdout) and CoCo2 keyboard/screen.
//
// The I/O channels are "repluggable" — Coco2 I/O is added when
// DriveConsole becomes ready and removed when it exits.
//
// Usage in SpoonFeeder:
//   tcl_io::emit(ch);              // Output to all active channels
//   byte key = tcl_io::poll_key(&iks);  // Poll all active inputs

#include <pico/stdlib.h>

#include "cobs_tx.h"
#include "vfs.h"

namespace tcl_io {

// I/O channel flags — can be combined
enum IoChannel : uint {
  IO_NONE = 0,
  IO_USB = 1,    // USB CDC stdin/stdout (raw getchar/putchar)
  IO_COCO2 = 2,  // CoCo2 keyboard/screen via DriveConsole peek/poke
};

// Currently active I/O channels. Modified by foreground (DriveConsole)
// and read by background (SpoonFeeder).
volatile uint active_io = IO_USB;

// Output a character to all active output channels.
inline void emit(unsigned char ch) {
  if (active_io & IO_USB) {
    cobs_putchar(ch);
  }
  if (active_io & IO_COCO2) {
    console::emit_char(ch);
  }
}

// Output a string to all active output channels.
inline void emit_string(const char* s) {
  for (const char* p = s; *p; p++) {
    emit(*p);
  }
}

// Poll all active input channels for a keypress.
// Returns 0 if no key is available.
// iks is the Coco2 keyboard state (can be null if no Coco2).
inline unsigned char poll_key(console::inkey_state* iks) {
  // Check Coco2 keyboard first (lower latency when physically present)
  if ((active_io & IO_COCO2) && iks) {
    byte key = console::Coco2Inkey(iks);
    if (key) return key;
  }
  // Check USB CDC (non-blocking) — skip if USB is not connected
  if ((active_io & IO_USB) && usb_tether_ok()) {
    std::string* pkt = usb_packet_buf.Yoink([](std::string* s) {
      // Anything that is not an RPC or PicoRPC packet is treated as keyboard input
      return s && s->length() > 0 &&
             (unsigned char)(*s)[0] != T_RPC &&
             (unsigned char)(*s)[0] != T_PICO_RPC;
    });
    
    if (pkt) {
      unsigned char uch = (unsigned char)(*pkt)[0];
      delete pkt;
      return uch;
    }
  }
  return 0;
}

inline void add_coco2() { active_io |= IO_COCO2; }
inline void remove_coco2() { active_io &= ~IO_COCO2; }

}  // namespace tcl_io

#endif  // CENTIPEDE_FIRMWARE_TCL_IO_H_
