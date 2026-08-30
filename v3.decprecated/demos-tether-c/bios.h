#ifndef _DEMOS_TETHER_C__BIOS_H_
#define _DEMOS_TETHER_C__BIOS_H_

#include "demos-metal-gcc/t9sim.h"
#include "demos-metal-gcc/types.h"

char BiosBuffer[300];

void BiosPutByte(byte c) { POKE1(T9SIM_TX_ADDR, c); }

void BiosPutChar(char c) { BiosPutByte((byte)c); }

void BiosPutStr(const char* s) {
  while (*s) {
    BiosPutChar(*s++);
  }
}

bool TryBiosGetChar(char* out) {
  if (PEEK1(T9SIM_STATUS_ADDR) & T9SIM_RX_BIT) {
    *out = PEEK1(T9SIM_RX_ADDR);
    return TRUE;
  }
  return FALSE;
}
char BiosGetChar() {
  char c;
  bool b;
  do {
    b = TryBiosGetChar(&c);
  } while (!b);
  return c;
}

char* BiosGetStr() {
  char* p = BiosBuffer;
  while (TRUE) {
    char c = BiosGetChar();

    switch (c) {
      case 1:
      case 27:
        return NULL;
      case '\n':
      case '\r':
        *p = 0;
        BiosPutChar('\r');
        BiosPutChar('\n');
        return BiosBuffer;
        break;
      case 8:
      case 127:
        if (p > BiosBuffer) {
          p--;
          BiosPutStr("\b \b");
          // BiosPutChar('[');
          // BiosPutChar(*p);
          // BiosPutChar(']');
        }
        break;
      default:
        if (32 <= c && c <= 126) {
          *p++ = c;
          *p = 0;
          BiosPutChar(c);
        } else {
          BiosPutChar('[');
          BiosPutChar('?');
          BiosPutChar(']');
        }
        break;
    }  // switch (c)
  }  // while TRUE
}

#endif  // _DEMOS_TETHER_C__BIOS_H_
