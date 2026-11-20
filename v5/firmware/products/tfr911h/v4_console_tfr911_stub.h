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

// CoCo 2 keyboard matrix maps referenced by keyboard_injector.h (shared with console.h)
inline const unsigned char unshifted_map[8][7] = {
    // PA0  PA1  PA2  PA3  PA4  PA5  PA6
    {'@', 'h', 'p', 'x', '0', '8', 13},  // PB0 (13 = Enter)
    {'a', 'i', 'q', 'y', '1', '9', 12},  // PB1 (12 = Clear)
    {'b', 'j', 'r', 'z', '2', ':', 27},  // PB2 (27 = Break)
    {'c', 'k', 's', 128, '3', ';', 0},   // PB3 (128 = Up)
    {'d', 'l', 't', 129, '4', ',', 0},   // PB4 (129 = Down)
    {'e', 'm', 'u', 8, '5', '-', 0},     // PB5 (8 = Left/BS)
    {'f', 'n', 'v', 9, '6', '.', 0},     // PB6 (9 = Right/TAB)
    {'g', 'o', 'w', ' ', '7', '/', 0}    // PB7 (PA6 is Shift, handled separately)
};

inline const unsigned char shifted_map[8][7] = {
    // PA0  PA1  PA2  PA3  PA4  PA5  PA6
    {'@', 'H', 'P', 'X', '_', '(', 13},   // PB0 (Shift+Enter)
    {'A', 'I', 'Q', 'Y', '!', ')', 12},   // PB1 (Shift+Clear)
    {'B', 'J', 'R', 'Z', '"', '*', 127},  // PB2 (Shift+Break = DEL)
    {'C', 'K', 'S', 132, '#', '+', 0},    // PB3 (132 = Page Up)
    {'D', 'L', 'T', 133, '$', '<', 0},    // PB4 (133 = Page Down)
    {'E', 'M', 'U', 130, '%', '=', 0},    // PB5 (130 = Cursor Left)
    {'F', 'N', 'V', 131, '&', '>', 0},    // PB6 (131 = Cursor Right)
    {'G', 'O', 'W', ' ', '\'', '?', 0}    // PB7 (Shift+Space)
};

inline const unsigned char clear_map[8][7] = {
    // PA0  PA1  PA2  PA3  PA4  PA5  PA6
    {   '`', 31&'h', 31&'p', 31&'x', '0', '[', 13},  // PB0: @->`, 8->[
    {31&'a', 31&'i', 31&'q', 31&'y', '|', ']', 12},  // PB1: 1->|, 9->]
    {31&'b', 31&'j', 31&'r', 31&'z', '2', ':', 27},  // PB2
    {31&'c', 31&'k', 31&'s',    128, '~', ';', 0},   // PB3: 3->~, Up
    {31&'d', 31&'l', 31&'t',    129, '4', '{', 0},   // PB4: ,->{, Down
    {31&'e', 31&'m', 31&'u',      8, '5', '_', 0},   // PB5: -->_, Left/BS
    {31&'f', 31&'n', 31&'v',      9, '6', '}', 0},   // PB6: .->}, Right/TAB
    {31&'g', 31&'o', 31&'w',    ' ', '^', '\\', 0}   // PB7: 7->^, /->backslash
};

// Screen state referenced by gspoon.h (not used by TFR911)
inline unsigned char shadow_fb[1] = {};
inline int cursor_row = 0;
inline int cursor_col = 0;

}  // namespace console

#endif  // V4_CONSOLE_TFR911_STUB_H_
