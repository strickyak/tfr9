// v4_console_tfr911_stub.h — Empty stubs for the console:: namespace.
// TFR911 has no CoCo2 screen or keyboard, but tcl_commands.h and
// tcl_io.h reference console:: symbols.  These stubs satisfy the
// linker without pulling in the full CoCo2 console driver.
#ifndef V4_CONSOLE_TFR911_STUB_H_
#define V4_CONSOLE_TFR911_STUB_H_

#include <cstdint>

// Forward-declare the global ram[] outside any namespace
extern unsigned char ram[];

namespace console {

struct inkey_state {};

inline void poke(uint16_t addr, uint8_t val) {
  // TFR911 has no CoCo2 bus to poke — write to our global ram[] instead.
  ram[addr & 0xFFFF] = val;
}
inline uint8_t peek(uint16_t addr) {
  return ram[addr & 0xFFFF];
}
inline void emit_char(unsigned char ch) {
  // No CoCo2 screen — silently discard.
}
inline void emit_char_string(const char* s) {
  // No CoCo2 screen — silently discard.
}
inline unsigned char Coco2Inkey(inkey_state* iks) {
  return 0;  // No CoCo2 keyboard.
}

// Keyboard maps referenced by keyboard_injector.h
inline unsigned char unshifted_map[8][7] = {};
inline unsigned char shifted_map[8][7] = {};
inline unsigned char clear_map[8][7] = {};

// Screen state referenced by gspoon.h (not used by TFR911)
inline unsigned char shadow_fb[1] = {};
inline int cursor_row = 0;
inline int cursor_col = 0;

}  // namespace console

#endif  // V4_CONSOLE_TFR911_STUB_H_
