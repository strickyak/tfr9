#ifndef FIRMWARE_CONSOLE_H_
#define FIRMWARE_CONSOLE_H_

// Keycode Convention -- Aug 2, 2026
//
// ASCII control codes:
//   8    BS          CoCo Left Arrow (destructive backspace)
//   9    TAB         CoCo Right Arrow (insert tab)
//   13   CR          Enter
//   27   ESC         CoCo Break (ANSI prefix with half-second timeout)
//   127  DEL         CoCo Shift-Break
//
// Extended keycodes (128+), used by both CoCo keyboard and tether:
//   128  Up          CoCo Up Arrow / tether Up
//   129  Down        CoCo Down Arrow / tether Down
//   130  Cursor Left CoCo Shift-Left / tether Left (non-destructive)
//   131  Cursor Right CoCo Shift-Right / tether Right (non-destructive)
//   132  Page Up     CoCo Shift-Up / tether PgUp or Shift-Up
//   133  Page Down   CoCo Shift-Down / tether PgDn or Shift-Down
//
// Tether notes:
// - The tether has dedicated Backspace and Tab keys, so Left/Right
//   arrows always produce 130/131 (non-destructive cursor motion)
//   regardless of shift state.
// - The tether parses ANSI escape sequences locally and sends these
//   single-byte keycodes over COBS.  The firmware never sees ESC
//   sequences from the tether.

#include <stdio.h>
#include <string.h>

#include "cobs_tx.h"
#include "pico/stdlib.h"

#if USE_PMODE4
#include "../util/font5x7.h"
#endif

void draw_bug_pmode4_splash_screen() {
  for (uint i = 0; i < sizeof bug_pmode4; i++) {
    uint addr = 0x0800 + i;
    byte val = bug_pmode4[i];

    uint cmd = ((uint)BG2FG_POKE << 24) | ((uint)addr << 8) | val;
    while (!bg2fg.push(cmd)) {
    }
  }
}

namespace console {

// Poke a byte to CoCo memory via cross-core FIFO to the foreground.
// Fire-and-forget: no reply expected.
void poke(unsigned short addr, unsigned char val) {
  uint cmd = ((uint)BG2FG_POKE << 24) | ((uint)addr << 8) | val;
  while (!bg2fg.push(cmd)) {
    // FIFO full, spin. Foreground DriveConsole will drain it.
    // TODO: Future cooperative multitasking - pump USB here.
  }
}

// Peek a byte from CoCo memory via cross-core FIFO round-trip.
// Blocks until the foreground services the request and returns the reply.
byte peek(unsigned short addr) {
  uint cmd = ((uint)BG2FG_PEEK << 24) | ((uint)addr << 8);
  while (!bg2fg.push(cmd)) {
    // FIFO full, spin.
  }
  // Wait for FG2BG_PEEK_REPLY from foreground.
  // Note: any FG2BG_WRITE or other items encountered here are dropped.
  // This is acceptable because peek() completes in microseconds, and
  // drain_task processes writes during the (much longer) intervals
  // between peek() calls.
  uint peek_polls = 0;
  while (true) {
    uint z = 0;
    if (fg2bg.pop(z)) {
      uint chore_num = z >> 24;
      if (chore_num == FG2BG_PEEK_REPLY) {
        return (byte)(z & 0xFF);
      }
      if (chore_num == FG2BG_PUTCHAR) {
        cobs_putchar(z & 0xFF);
      }
      // Other items (WRITE, READ, etc.) are dropped here.
      // drain_task handles the vast majority of these between peek() calls.
    }
    if (peek_polls <= 10000000 + 1) {
      peek_polls++;
      if (peek_polls == 10000000) {
        cobs_printf("peek(%04x) STUCK after 10M polls! fg2bg.size=%d\n", addr,
                    fg2bg.size());
      }
    }
  }
}

#define PIA0_PORT_A 0xFF00
#define PIA0_PORT_B 0xFF02

const unsigned char unshifted_map[8][7] = {
    // PA0  PA1  PA2  PA3  PA4  PA5  PA6
    {'@', 'h', 'p', 'x', '0', '8', 13},  // PB0 (13 = Enter)
    {'a', 'i', 'q', 'y', '1', '9', 12},  // PB1 (12 = Clear)
    {'b', 'j', 'r', 'z', '2', ':', 27},  // PB2 (27 = Break)
    {'c', 'k', 's', 128, '3', ';', 0},   // PB3 (128 = Up)
    {'d', 'l', 't', 129, '4', ',', 0},   // PB4 (129 = Down)
    {'e', 'm', 'u', 8, '5', '-', 0},     // PB5 (8 = Left/BS)
    {'f', 'n', 'v', 9, '6', '.', 0},     // PB6 (9 = Right/TAB)
    {'g', 'o', 'w', ' ', '7', '/', 0}  // PB7 (PA6 is Shift, handled separately)
};

const unsigned char shifted_map[8][7] = {
    // PA0  PA1  PA2  PA3  PA4  PA5  PA6
    {'@', 'H', 'P', 'X', '_', '(', 13},  // PB0 (Shift+Enter)
    {'A', 'I', 'Q', 'Y', '!', ')', 12},  // PB1 (Shift+Clear)
    {'B', 'J', 'R', 'Z', '"', '*', 127},  // PB2 (Shift+Break = DEL)
    {'C', 'K', 'S', 132, '#', '+', 0},    // PB3 (132 = Page Up)
    {'D', 'L', 'T', 133, '$', '<', 0},    // PB4 (133 = Page Down)
    {'E', 'M', 'U', 130, '%', '=', 0},    // PB5 (130 = Cursor Left)
    {'F', 'N', 'V', 131, '&', '>', 0},    // PB6 (131 = Cursor Right)
    {'G', 'O', 'W', ' ', '\'', '?', 0}   // PB7 (Shift+Space)
};

// CLEAR modifier: union of our mappings + NitrOS-9 conventions
const unsigned char clear_map[8][7] = {
    // PA0  PA1  PA2  PA3  PA4  PA5  PA6
    {   '`', 31&'h', 31&'p', 31&'x', '0', '[', 13},  // PB0: @→`, 8→[
    {31&'a', 31&'i', 31&'q', 31&'y', '|', ']', 12},  // PB1: 1→|, 9→]
    {31&'b', 31&'j', 31&'r', 31&'z', '2', ':', 27},  // PB2
    {31&'c', 31&'k', 31&'s',    128, '~', ';', 0},    // PB3: 3→~, Up
    {31&'d', 31&'l', 31&'t',    129, '4', '{', 0},    // PB4: ,→{, Down
    {31&'e', 31&'m', 31&'u',      8, '5', '_', 0},     // PB5: -→_, Left/BS
    {31&'f', 31&'n', 31&'v',      9, '6', '}', 0},     // PB6: .→}, Right/TAB
    {31&'g', 31&'o', 31&'w',    ' ', '^', '\\', 0}   // PB7: 7→^, /→backslash
};


// State structure to remember previous scans for debouncing
struct inkey_state {
  unsigned char prev_pressed[8];
  unsigned char debounced_pressed[8];
};

unsigned char Coco2Inkey(struct inkey_state* state) {
  unsigned char curr_pressed_all[8];

  // Scan the matrix
  for (int col = 0; col < 8; col++) {
    // Drive one column low, rest high
    poke(PIA0_PORT_B, (unsigned char)~(1 << col));

    // Read Port A and invert so 1 = pressed, 0 = released. Mask 7 rows.
    curr_pressed_all[col] = ~peek(PIA0_PORT_A) & 0x7F;
  }

  // Sanity check: if ALL keys appear pressed in every column,
  // the PIA is not initialized yet (0xFF00 reads DDR=0x00 instead of port
  // data). This is physically impossible on a real keyboard.
  bool all_pressed = true;
  for (int col = 0; col < 8; col++) {
    if (curr_pressed_all[col] != 0x7F) {
      all_pressed = false;
      break;
    }
  }
  if (all_pressed) return 0;  // PIA not ready

  // Check for shift key (PB7, PA6)
  int shift_pressed = (curr_pressed_all[7] & (1 << 6)) != 0;
  // Check for clear key (PB1, PA6) used as a modifier
  int clear_pressed = (curr_pressed_all[1] & (1 << 6)) != 0;

  // Select keyboard map based on modifiers.
  // Clear takes priority over Shift (Shift is ignored when Clear is held).
  const unsigned char(*active_map)[7];
  if (clear_pressed) {
    active_map = clear_map;
  } else if (shift_pressed) {
    active_map = shifted_map;
  } else {
    active_map = unshifted_map;
  }

  unsigned char returned_char = 0;

  // Debounce and detect newly pressed keys
  for (int col = 0; col < 8; col++) {
    unsigned char curr = curr_pressed_all[col];
    unsigned char prev = state->prev_pressed[col];

    // Key is debounced if it is pressed in both current and previous scan
    unsigned char debounced = curr & prev;

    // Find which keys transitioned to debounced state
    unsigned char triggered = debounced & ~state->debounced_pressed[col];

    // Update the debounced matrix:
    // Set newly debounced keys, clear keys that are physically released.
    state->debounced_pressed[col] |= debounced;
    state->debounced_pressed[col] &= curr;

    // Remember current physical state for next scan's debounce
    state->prev_pressed[col] = curr;

    // If we haven't found a character to return yet, check triggered keys
    if (returned_char == 0 && triggered != 0) {
      for (int row = 0; row < 7; row++) {
        if (triggered & (1 << row)) {
          // Ignore modifier keys themselves as characters
          if (col == 7 && row == 6) continue;  // Shift
          if (col == 1 && row == 6) continue;  // Clear

          returned_char = active_map[col][row];
          break;  // Return first triggered key, not last
        }
      }
    }
  }

  // De-select all columns when done scanning to leave matrix in a clean state
  poke(PIA0_PORT_B, 0xFF);

  return returned_char;
}

enum AnsiState { NORMAL, ESCAPE, CSI };
AnsiState ansi_state = NORMAL;
int csi_params[4];
int csi_param_count = 0;
unsigned short saved_cursor = 0x400;
bool inverse_video = false;

#if USE_PMODE4
unsigned char shadow_fb[6144];
int cursor_row = 0;
int cursor_col = 0;
int saved_cursor_row = 0;
int saved_cursor_col = 0;

void render_char(int row, int col, unsigned char ascii, bool inverse) {
  if (row < 0 || row >= 24 || col < 0 || col >= 40) return;
  if (ascii < 32 || ascii > 127) ascii = 32;

  unsigned char* font_data = Font5x7 + ((ascii - 32) * 5);

  int pixel_x = col * 6;
  int pixel_y = row * 8;

  for (int y = 0; y < 8; y++) {
    for (int px = 0; px < 6; px++) {
      int screen_x = pixel_x + px;
      int b_idx = (pixel_y + y) * 32 + (screen_x / 8);
      int bit_idx = 7 - (screen_x % 8);

      bool is_set = false;
      if (y < 7 && px < 5) {
        is_set = (font_data[px] & (1 << y)) != 0;
      }
#if INVERSE_PMODE
      is_set = !is_set;
#endif
      if (inverse) is_set = !is_set;

      if (is_set) {
        shadow_fb[b_idx] |= (1 << bit_idx);
      } else {
        shadow_fb[b_idx] &= ~(1 << bit_idx);
      }
    }

    int b_start = (pixel_y + y) * 32 + (pixel_x / 8);
    int b_end = (pixel_y + y) * 32 + ((pixel_x + 5) / 8);
    for (int b = b_start; b <= b_end; b++) {
      poke(0x800 + b, shadow_fb[b]);
    }
  }
}

void scroll_up() {
  memmove(shadow_fb, shadow_fb + 256, 6144 - 256);
#if INVERSE_PMODE
  memset(shadow_fb + (6144 - 256), 0xFF, 256);
#else
  memset(shadow_fb + (6144 - 256), 0, 256);
#endif
  for (int i = 0; i < 6144; i++) {
    poke(0x800 + i, shadow_fb[i]);
  }
}

void emit_char(unsigned char ascii) {
  if (ansi_state == ESCAPE) {
    if (ascii == '[') {
      ansi_state = CSI;
      csi_param_count = 0;
      for (int i = 0; i < 4; i++) csi_params[i] = 0;
    } else if (ascii == '7') {
      saved_cursor_row = cursor_row;
      saved_cursor_col = cursor_col;
      ansi_state = NORMAL;
    } else if (ascii == '8') {
      cursor_row = saved_cursor_row;
      cursor_col = saved_cursor_col;
      ansi_state = NORMAL;
    } else {
      cobs_printf("Unsupported ESC sequence: ESC %c\n", ascii);
      ansi_state = NORMAL;
    }
    return;
  }

  if (ansi_state == CSI) {
    if (ascii >= '0' && ascii <= '9') {
      csi_params[csi_param_count] =
          csi_params[csi_param_count] * 10 + (ascii - '0');
    } else if (ascii == ';') {
      if (csi_param_count < 3) csi_param_count++;
    } else {
      int n = csi_params[0] == 0 ? 1 : csi_params[0];

      switch (ascii) {
        case 'H':
        case 'f': {
          int r = csi_params[0] == 0 ? 1 : csi_params[0];
          int c =
              (csi_param_count > 0 && csi_params[1] > 0) ? csi_params[1] : 1;
          if (r > 24) r = 24;
          if (c > 40) c = 40;
          cursor_row = r - 1;
          cursor_col = c - 1;
          break;
        }
        case 'A':  // Up
          cursor_row -= n;
          if (cursor_row < 0) cursor_row = 0;
          break;
        case 'B':  // Down
          cursor_row += n;
          if (cursor_row > 23) cursor_row = 23;
          break;
        case 'C':  // Right
          cursor_col += n;
          if (cursor_col > 39) cursor_col = 39;
          break;
        case 'D':  // Left
          cursor_col -= n;
          if (cursor_col < 0) cursor_col = 0;
          break;
        case 's':
          saved_cursor_row = cursor_row;
          saved_cursor_col = cursor_col;
          break;
        case 'u':
          cursor_row = saved_cursor_row;
          cursor_col = saved_cursor_col;
          break;
        case 'm':
          for (int i = 0; i <= csi_param_count; i++) {
            if (csi_params[i] == 0)
              inverse_video = false;
            else if (csi_params[i] == 7)
              inverse_video = true;
          }
          break;
        case 'J':
          if (csi_params[0] == 2) {
#if INVERSE_PMODE
            memset(shadow_fb, 0xFF, 6144);
            for (int i = 0; i < 6144; i++) poke(0x800 + i, 0xFF);
#else
            memset(shadow_fb, 0, 6144);
            for (int i = 0; i < 6144; i++) poke(0x800 + i, 0);
#endif
            cursor_row = 0;
            cursor_col = 0;
          }
          break;
        case 'K':
          if (csi_params[0] == 0) {  // Clear to end of line
            for (int c = cursor_col; c < 40; c++)
              render_char(cursor_row, c, ' ', false);
          } else if (csi_params[0] == 2) {  // Clear entire line
            for (int c = 0; c < 40; c++) render_char(cursor_row, c, ' ', false);
          }
          break;
        default:
          cobs_printf("Unsupported CSI sequence: CSI ... %c\n", ascii);
          break;
      }
      ansi_state = NORMAL;
    }
    return;
  }

  if (ascii == 0x1B) {
    ansi_state = ESCAPE;
    return;
  }

  if (ascii == '\r') {
    cursor_col = 0;
  } else if (ascii == '\n') {
    cursor_col = 0;
    cursor_row++;
  } else if (ascii == 8 || ascii == 127) {  // backspace
    if (cursor_col > 0) {
      cursor_col--;
    } else if (cursor_row > 0) {
      cursor_row--;
      cursor_col = 39;
    }
  } else if (ascii >= 0x20) {
    render_char(cursor_row, cursor_col, ascii, inverse_video);
    cursor_col++;
    if (cursor_col >= 40) {
      cursor_col = 0;
      cursor_row++;
    }
  }

  // Handle scrolling
  if (cursor_row >= 24) {
    scroll_up();
    cursor_row = 23;
  }
}

#else
unsigned short cursor = 0x400;

void emit_char(unsigned char ascii) {
  if (ansi_state == ESCAPE) {
    if (ascii == '[') {
      ansi_state = CSI;
      csi_param_count = 0;
      for (int i = 0; i < 4; i++) csi_params[i] = 0;
    } else if (ascii == '7') {
      saved_cursor = cursor;
      ansi_state = NORMAL;
    } else if (ascii == '8') {
      cursor = saved_cursor;
      ansi_state = NORMAL;
    } else {
      cobs_printf("Unsupported ESC sequence: ESC %c\n", ascii);
      ansi_state = NORMAL;
    }
    return;
  }

  if (ansi_state == CSI) {
    if (ascii >= '0' && ascii <= '9') {
      csi_params[csi_param_count] =
          csi_params[csi_param_count] * 10 + (ascii - '0');
    } else if (ascii == ';') {
      if (csi_param_count < 3) csi_param_count++;
    } else {
      int n = csi_params[0] == 0 ? 1 : csi_params[0];

      switch (ascii) {
        case 'H':
        case 'f': {
          int r = csi_params[0] == 0 ? 1 : csi_params[0];
          int c =
              (csi_param_count > 0 && csi_params[1] > 0) ? csi_params[1] : 1;
          if (r > 16) r = 16;
          if (c > 32) c = 32;
          cursor = 0x400 + (r - 1) * 32 + (c - 1);
          break;
        }
        case 'A':  // Up
          cursor = (cursor >= 0x400 + n * 32) ? cursor - n * 32
                                              : (cursor & 0x1F) + 0x400;
          break;
        case 'B':  // Down
          cursor = (cursor + n * 32 < 0x600) ? cursor + n * 32
                                             : (cursor & 0x1F) + 0x5E0;
          break;
        case 'C':  // Right
        {
          unsigned short current_row_start = cursor & ~0x1F;
          unsigned short max_c = current_row_start + 31;
          cursor += n;
          if (cursor > max_c) cursor = max_c;
        } break;
        case 'D':  // Left
        {
          unsigned short current_row_start = cursor & ~0x1F;
          if (cursor >= current_row_start + n) {
            cursor -= n;
          } else {
            cursor = current_row_start;
          }
        } break;
        case 's':
          saved_cursor = cursor;
          break;
        case 'u':
          cursor = saved_cursor;
          break;
        case 'm':
          for (int i = 0; i <= csi_param_count; i++) {
            if (csi_params[i] == 0)
              inverse_video = false;
            else if (csi_params[i] == 7)
              inverse_video = true;
          }
          break;
        case 'J':
          if (csi_params[0] == 2) {
            for (unsigned short i = 0x400; i < 0x600; i++) poke(i, 0x20);
            cursor = 0x400;
          }
          break;
        case 'K':
          if (csi_params[0] == 0) {  // Clear to end of line
            unsigned short eol = (cursor & ~0x1F) + 0x20;
            for (unsigned short i = cursor; i < eol; i++) poke(i, 0x20);
          } else if (csi_params[0] == 2) {  // Clear entire line
            unsigned short sol = cursor & ~0x1F;
            unsigned short eol = sol + 0x20;
            for (unsigned short i = sol; i < eol; i++) poke(i, 0x20);
          }
          break;
        default:
          cobs_printf("Unsupported CSI sequence: CSI ... %c\n", ascii);
          break;
      }
      ansi_state = NORMAL;
    }
    return;
  }

  if (ascii == 0x1B) {
    ansi_state = ESCAPE;
    return;
  }

  if (ascii == '\r') {
    cursor = cursor & ~0x1F;
  } else if (ascii == '\n') {
    cursor = (cursor & ~0x1F) + 0x20;
  } else if (ascii == 8 || ascii == 127) {  // backspace
    if (cursor > 0x400) {
      cursor--;
    }
  } else if (ascii >= 0x20 && ascii <= 0x7F) {
    // Map ASCII to CoCo display bytes:
    //   Lowercase a-z → $01-$1A (normal video, common in Tcl)
    //   Uppercase A-Z → $41-$5A (inverse video, less common)
    //   @ and [\]^_  → $00,$1B-$1F (normal video)
    //   ` and {|}~   → $40,$5B-$5E (inverse video)
    //   Space-?      → $20-$3F (unchanged)
    unsigned char mapped;
    if (ascii >= 'a' && ascii <= 'z') {
      mapped = ascii - 0x60;  // $01-$1A normal
    } else if (ascii >= 'A' && ascii <= 'Z') {
      mapped = ascii;  // $41-$5A inverse
    } else if (ascii >= 0x20 && ascii <= 0x3F) {
      mapped = ascii;  // $20-$3F unchanged
    } else if (ascii >= 0x40 && ascii <= 0x5F) {
      mapped = ascii - 0x40;  // @[\]^_ → $00-$1F normal
    } else {
      mapped = ascii - 0x20;  // `{|}~ → $40-$5F inverse
    }

    if (inverse_video) {
      mapped ^= 0x40;  // Toggle CoCo inverse video for ANSI ESC[7m
    }

    poke(cursor, mapped);
    cursor++;
  } else if (ascii >= 0x80) {
    poke(cursor, ascii);  // semigraphics — pass through
    cursor++;
  }

  // Handle scrolling
  if (cursor >= 0x600) {
    for (unsigned short i = 0x400; i < 0x5E0; i++) {
      poke(i, peek(i + 0x20));
    }
    for (unsigned short i = 0x5E0; i < 0x600; i++) {
      poke(i, 0x20);
    }
    cursor = 0x5E0;
  }
}
#endif

// Send a string to the CoCo2 screen only (not USB).
void emit_char_string(const char* s) {
  for (const char* p = s; *p; p++) {
    emit_char(*p);
  }
}

}  // namespace console

#endif  // FIRMWARE_CONSOLE_H_
