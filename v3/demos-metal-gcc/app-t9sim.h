#ifndef _TFR9_V3_APPS_APP_T9SIM_H_
#define _TFR9_V3_APPS_APP_T9SIM_H_

#include "v3/apps-metal-gcc/t9sim.h"
#include "v3/apps-metal-gcc/types.h"

void abort(void);
int getchar();
int try_getchar();
void putchar(int x);

#define TEXT_STR_MAX 100

/////////////////////////////////////////
/////////////////////////////////////////

void abort(void) {
  for (const char* s = "\n*** ABORT ***\n"; *s; s++) {
    *(volatile char*)T9SIM_TX_ADDR = *s;
  }
  while (1) {
    POKE1(0, 0);
  }
}

int try_getchar() {
  // NOTE -- this obliterates the TIMER bit.
  if ((PEEK1(T9SIM_STATUS_ADDR) & T9SIM_RX_BIT) != 0) {
    return PEEK1(T9SIM_RX_ADDR);
  };
  return -1;
}

int getchar() {
  // NOTE -- this obliterates the TIMER bit.
  while ((PEEK1(T9SIM_STATUS_ADDR) & T9SIM_RX_BIT) == 0) {
  };
  return PEEK1(T9SIM_RX_ADDR);
}

void putchar(int x) { POKE1(T9SIM_TX_ADDR, x); }

void FnPutStr(int_consumer_fn fn, const char* s) {
  int max = TEXT_STR_MAX;
  for (; *s; s++) {
    fn(*s);
    if (max-- <= 0) {
      fn('\\');
      return;
    }
  }
}

const char PHexAlphabet[] = "0123456789ABCDEF";

void FnPutHex(int_consumer_fn fn, word x) {
  if (x > 15u) {
    FnPutHex(fn, x >> 4u);
  }
  fn(PHexAlphabet[15u & x]);
}

byte DivMod10(word x, word* out_div) {  // returns mod
  word div = 0;
  while (x >= 10000) x -= 10000, div += 1000;
  while (x >= 1000) x -= 1000, div += 100;
  while (x >= 100) x -= 100, div += 10;
  while (x >= 10) x -= 10, div++;
  *out_div = div;
  return (byte)x;
}
void FnPutDec(int_consumer_fn fn, word x) {
  word div;
  if (x > 9u) {
    // eschew div // PPutDec(x / 10u);
    DivMod10(x, &div);
    FnPutDec(fn, div);
  }
  // eschew mod // PutChar('0' + (byte)(x % 10u));
  fn('0' + DivMod10(x, &div));
}

void FnPutSigned(int_consumer_fn fn, int x) {
  if (x < 0) {
    x = -x;
    fn('-');
  }
  FnPutDec(fn, x);
}

// typedef void (*int_consumer_fn)(int);
void FnFormat(int_consumer_fn fn, const char* format, va_list ap) {
  int max = TEXT_STR_MAX;

  for (const char* s = format; *s; s++) {
    if (max-- <= 0) {
      fn('\\');
      break;
    }

    if (*s < ' ') {
      fn('\n');
    } else if (*s != '%') {
      fn(*s);
    } else {
      s++;
      switch (*s) {
        case 'd': {
          int x = va_arg(ap, int);
          FnPutSigned(fn, x);
        } break;
        case 'u': {
          word x = va_arg(ap, word);
          FnPutDec(fn, x);
        } break;
        case 'x': {
          word x = va_arg(ap, word);
          FnPutHex(fn, x);
        } break;
        case 's': {
          char* x = va_arg(ap, char*);
          FnPutStr(fn, x);
        } break;
        default:
          fn(*s);
      };  // end switch
    }  // end if
  }
}

void printf(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  FnFormat(putchar, format, ap);
  va_end(ap);
}

volatile int _dummy_int;
void dispose_int(int x) { _dummy_int = x; }

void silent_printf(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  FnFormat(dispose_int, format, ap);
  va_end(ap);
}

// entry

void app_main();

volatile runnable_fn _dummy_fn;
void main() {
  _dummy_fn = abort;
  _dummy_fn = app_main;

  asm volatile(
      "\n"
      "  .globl entry \n"
      "entry:         \n"
      "  orcc #$50    \n"  // No IRQs, FIRQs, for now.
      "  lds #$1000   \n"  // Reset the stack
      "  jsr _app_main    \n"
      "  jsr _abort   \n");
}

#endif  // _TFR9_V3_APPS_APP_T9SIM_H_
