#ifndef _CYBERTERM_H_
#define _CYBERTERM_H_

#include "ssd1306.h"

// CyberTerm is a "CyberDeck terminal" using Ssd1306 for display
// and CardKb for keyboard, for NitrOS9.
 
template <typename T>
struct DontCyberTerm {
  static bool DoesCyberTerm() { return false; }
  static void CyberTerm_Init(uint base) {}
};

#define CTERM_W 16 /* (128/8) */
#define CTERM_H 8  /* (64/8)  */
#define FONT_W 8
#define FONT_H 8

#define DISP_X(X) IOWriters[0x08](0xFF08, (X))
#define DISP_Y(Y) IOWriters[0x09](0xFF09, (Y))
#define DISP_C(C) IOWriters[0x0A](0xFF0A, (C))

byte ct_screen[CTERM_H * CTERM_W];
byte ct_cursor;

template <typename T>
struct DoCyberTerm {
  static bool DoesCyberTerm() { return true; }

  static void CyberTerm_Init(uint base) {
    T::Logf(LHello, "cyberterm $%x install", base);
    T::Ssd1306_Init(base);
    T::CardKb_Install(base);
  
    memset(ct_screen, '.', sizeof ct_screen);
    NewLine();
    Push();

    uint sub = 255 & base;
    IOWriters[sub + 1] = [](uint addr, byte data) {
      // MC6850 Data Write
      PutChar(data);
    };
  }

  static void NewLine() {
      // Shift everything up one line.
      for  (uint y = 0; y < (CTERM_H-1); y++) {
          memcpy(ct_screen + (y*CTERM_W), ct_screen + ((y+1)*CTERM_W), CTERM_W);
      }
      // Clear the last line with spaces.
      memset(ct_screen + (CTERM_H-1) * CTERM_W, ' ', CTERM_W);
      // Reset the cursor to left column.
      ct_cursor = 0;
  }

  static void PutChar(byte b) {
      if (b==10 || b==13) {
          NewLine();
          Push();
          return;
      }
      if (b==8) {
          ct_cursor--;
          ct_screen[(CTERM_H-1) * CTERM_W + ct_cursor] = ' ';
          Push();
          return;
      }

      if (b < ' ' || b > 127)
          b = '?';

      ct_screen[(CTERM_H-1) * CTERM_W + ct_cursor] = b;
      ct_cursor++;
      if (ct_cursor >= CTERM_W) NewLine();
      Push();
  }

  static void Push() {
      DISP_C(DISPLAY_CLEAR_BUFFER);
      for (uint y = 0; y < CTERM_H; y++) {
          for (uint x = 0; x < CTERM_W; x++) {
              byte b = ct_screen[x + (y*CTERM_W)];
              if (b < ' ' || b > 127)
                    b = '?';
              DISP_X(x * FONT_W);
              DISP_Y(y * FONT_H);
              DISP_C(b);
          }
      }
      DISP_C(DISPLAY_SEND_BUFFER);
  }

};

#endif // _CYBERTERM_H_
