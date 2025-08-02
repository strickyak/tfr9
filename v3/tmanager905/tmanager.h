// tmanager.cpp -- for the TFR/901 -- strick
//
// SPDX-License-Identifier: MIT

#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/structs/systick.h>
#include <hardware/timer.h>
#include <pico/bootrom.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <pico/unique_id.h>
#include <stdio.h>

#include <cstring>
#include <functional>
#include <vector>

constexpr uint NumberOfLivenessTildes = 8;

#define printf T::Logf

#define LED(X) gpio_put(25, (X))

// If we use the "W" version of a pico,
// it requires pico/cyw43_arch.h:
// #include "pico/cyw43_arch.h"

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define force_inline inline __attribute__((always_inline))
#define MUMBLE(X)              \
  {                            \
    ShowStr(X " ");            \
    printf("MUMBLE: " X "\n"); \
  }

typedef unsigned char byte;
typedef unsigned int word;
typedef unsigned char T_byte;
typedef unsigned int T_word;
typedef unsigned char T_16[16];

// RAPID_BURST_CYCLES is how many cycles
// to run quickly without checking for IRQs
// and other stuff that slows us down.
// For a slower but more accurate simulation,
// reduce the scale.
// In order for simulated clock ticks to work,
// RAPID_BURST_CYCLES must be a power of two,
// so RAPID_BURST_SCALE is that power.
constexpr uint RAPID_BURST_SCALE = 8;
constexpr uint RAPID_BURST_CYCLES = (1u << RAPID_BURST_SCALE);

// POKE EVENTS in Ram can cause USB traffic.
// Sometimes we don't want that, in the middle of
// some other USB operations.   So Quiet()
// and Noisy() can squelch that.
uint quiet_ram;
inline void Quiet() { quiet_ram++; }
inline void Noisy() { quiet_ram--; }

enum message_type : byte {
  // Long form codes, 128 to 191.
  // Followed by a 1-byte or 2-byte Size value.
  // If following byte in 128 to 191, it is 1-byte, use low 6 bits for size.
  // If following byte in 192 to 255, it is 2-byte, use low 6 bits times 64,
  // plus low 6 bits of next byte.
  C_LOGGING = 130,  // Ten levels: 130 to 139.

  C_PRE_LOAD = 163,    // Console pokes to Manager
  C_RAM_CONFIG = 164,  // Pico tells tconsole.
  C_DUMP_RAM = 167,
  C_DUMP_LINE = 168,
  C_DUMP_STOP = 169,
  C_DUMP_PHYS = 170,
  C_EVENT = 172,  // event.h
  C_DISK_READ = 173,
  C_DISK_WRITE = 174,

  EVENT_RTI = 176,
  EVENT_SWI2 = 177,

  // Short form codes, 192 to 255.
  // The packet length does not follow,
  // but is in the low nybble.
  C_REBOOT = 192,      // n=0
  C_PUTCHAR = 193,     // n=1
  C_RAM2_WRITE = 195,  // n=3
  C_RAM3_WRITE = 196,  // n=4
  C_RAM5_WRITE = 198,  // n=6
  C_CYCLE = 200,       // tracing one cycle
};

extern void putbyte(byte x);
extern void PreLoadPacket();

uint current_opcode_cy;  // what was the CY of the current opcode?
uint current_opcode_pc;  // what was the PC of the current opcode?
uint current_opcode;     // what was the current opcode?

bool enable_show_irqs;
bool enable_trace = true;
uint trace_at_what_cycle;
uint interest;
uint stop_at_what_cycle;

bool gime_irq_enabled;
bool gime_vsync_irq_enabled;
bool gime_vsync_irq_firing;

bool acia_irq_enabled;
bool acia_irq_firing;
bool acia_char_in_ready;
int acia_char;

void ShowChar(byte ch) {
  if (ch < 1 || ch > 127) {
    putchar(C_PUTCHAR);
  }
  putchar(ch);
}
void ShowStr(const char* s) {
  while (*s) {
    ShowChar(*s++);
  }
}

// putbyte does CR/LF escaping for Binary Data
void putbyte(byte x) { putchar_raw(x); }

// Put Size with 1-byte / 2-byte encoding
void putsz(uint n) {
  assert(n < 4096);
  if (n < 64) {
    putbyte(0x80 + n);
  } else {
    putbyte(0xC0 + (n >> 6));  // div 64
    putbyte(0x80 + (n & 63));  // mod 64
  }
}

// Include logging first.
#include "benchmark-cycles.h"
#include "logging.h"
#include "pcrange.h"
#include "picotimer.h"
#include "seen.h"
#include "trace.h"

template <typename T>
struct DontShowIrqs {
  force_inline static void ShowIrqs(char ch) {}
};
template <typename T>
struct DoShowIrqs {
  force_inline static void ShowIrqs(char ch) {
    if (enable_show_irqs) ShowChar(ch);
  }
};

using IOReader = std::function<byte(uint addr, byte data)>;
using IOWriter = std::function<void(uint addr, byte data)>;
IOReader IOReaders[256];
IOWriter IOWriters[256];
void PollUsbInput();

#include "acia.h"
#include "gime.h"

// Configuration
#include "tfr9ports.gen.h"

// PIO code
#include "latch.pio.h"
#include "tpio.pio.h"

/*
  LED_W for Pico W:   LED_W(1) for on, LED_W(0) for off.
  // #define LED_W(X) cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (X))
*/

#define HL_JOIN(H, L) (((255 & (H)) << 8) | ((255 & (L)) << 0))

#define HL_SPLIT(H, L, X) (H = (byte)((X) >> 8), L = (byte)((X) >> 0))

#define QUAD_JOIN(A, B, C, D)                                       \
  (((255 & (A)) << 24) | ((255 & (B)) << 16) | ((255 & (C)) << 8) | \
   ((255 & (D)) << 0))

#define QUAD_SPLIT(A, B, C, D, X)                                      \
  (A = (byte)((X) >> 24), B = (byte)((X) >> 16), C = (byte)((X) >> 8), \
   D = (byte)((X) >> 0))

constexpr uint RTI_SZ = 12;
constexpr uint SWI2_SZ = 17;
static byte hist_data[24];
static uint hist_addr[24];

#define MAX_INTEREST 0x7FFFffff
#define getchar(X) NeverUseGetChar

const byte Level1_Rom[] = {
#include "../generated/level1.rom.h"
};

const byte Level2_Rom[] = {
#include "../generated/level2.rom.h"
};

#define DELAY sleep_us(1)

#define F_READ 0x01
#define F_AVMA 0x02
#define F_LIC 0x04
#define F_BA 0x08
#define F_BS 0x10
#define F_BUSY 0x20

#define F_HIGH (F_BA | F_BS | F_BUSY)

#define PIN_E 8           // clock output pin
#define PIN_Q 9           // clock output pin
#define COUNTER_CLOCK 10  // 74hc161 counter control output pin
#define COUNTER_RESET 11  // 74hc161 counter control output pin

#define STATE_Y5_RESET_PIN 0
#define STATE_Y5_IRQ_PIN 2

extern "C" {
extern int stdio_usb_in_chars(char* buf, int length);
}

const char* HighFlags(uint high) {
  if (!high) {
    return "";
  }
  static char high_buf[8];
  char* p = high_buf;
  if (high & F_BA) *p++ = 'A';
  if (high & F_BS) *p++ = 'S';
  if (high & F_BUSY) *p++ = 'Y';
  *p = '\0';
  return high_buf;
}

#include "circbuf.h"

// SmallRam & BigRam
#include "ram.h"

// Debugging
#include "event.h"
#include "hyper.h"
#include "reboot.h"

// I/O devices
// dont include "cocosdc.h"; use emudsk instead.
#include "emudsk.h"
#include "samvdg.h"
#include "turbo9sim.h"

// Operating Systems
#include "nitros9level1.h"
#include "nitros9level2.h"
#include "turbo9os.h"

void ViewAt(const char* label, uint hi, uint lo) {
#if 0
    Quiet();
    uint addr = (hi << 8) | lo;
    VIEWF("=== %s: @%04x: ", label, addr);
    for (uint i = 0; i < 8; i++) {
        uint x = T::Peek2(addr+i+i);
        VIEWF("%04x ", x);
    }
    VIEWF("|");
    for (uint i = 0; i < 16; i++) {
        byte ch = 0x7f & T::Peek(addr+i);
        if (32 <= ch && ch <= 126) {
            VIEWF("%c", ch);
        } else if (ch==0) {
            VIEWF("-");
        } else {
            VIEWF(".");
        }
    }
    VIEWF("|\n");
    Noisy();
#endif
}

void StrobePin(uint pin) {
  gpio_put(pin, 0);
  DELAY;
  gpio_put(pin, 1);
  DELAY;
}

// Use SetY(uint) to manually change the Multiplex Counter
// to the specified state, from outisde the PIO state machines.
// GPIO will have to "own" the COUNTER_RESET and COUNTER_CLOCK
// pins, instead of PIO owning them (see InitializePinsForGpio()).
// So really this is only used to reset the CPU
// (see ResetCpu()) before PIO begins.
void SetY(uint y) {
  StrobePin(COUNTER_RESET);
  for (uint i = 0; i < y; i++) {
    StrobePin(COUNTER_CLOCK);
  }
}

void InitializePinsForGpio() {
  for (uint i = 0; i < 8; i++) {
    gpio_init(i);
    gpio_set_dir(i, GPIO_IN);
  }
  for (uint i = 8; i < 12; i++) {
    gpio_init(i);
    gpio_set_dir(i, GPIO_OUT);
    gpio_put(i, 1);
  }
}

#if 0
void ResetCpu() {
  printf("Resetting CPU ... ");
  InitializePinsForGpio();

  // Activate the 6309 RESET line
  SetY(4);
  for (uint i = 0; i < 8; i++) {
    gpio_put(i, 1);
    gpio_set_dir(i, GPIO_OUT);
    gpio_put(i, 1);
  }
  gpio_put(STATE_Y5_RESET_PIN, 0);  // 0 is active
  StrobePin(COUNTER_CLOCK);         // Y5
  StrobePin(COUNTER_CLOCK);         // Y6
  for (uint i = 0; i < 8; i++) {
    gpio_set_dir(i, GPIO_IN);
  }
  SetY(0);

  const uint EnoughCyclesToReset = 32;
  gpio_put(PIN_Q, 0);
  DELAY;
  gpio_put(PIN_E, 0);
  DELAY;
  for (uint i = 0; i < EnoughCyclesToReset; i++) {
    gpio_put(PIN_Q, 1);
    DELAY;
    gpio_put(PIN_E, 1);
    DELAY;
    gpio_put(PIN_Q, 0);
    DELAY;
    gpio_put(PIN_E, 0);
    DELAY;
  }

  // Release the 6309 RESET line
  SetY(4);
  for (uint i = 0; i < 8; i++) {
    gpio_put(i, 1);
    gpio_set_dir(i, GPIO_OUT);
    gpio_put(i, 1);
  }
  gpio_put(STATE_Y5_RESET_PIN, 1);  // 1 is release
  StrobePin(COUNTER_CLOCK);         // Y5
  StrobePin(COUNTER_CLOCK);         // Y6
  for (uint i = 0; i < 8; i++) {
    gpio_set_dir(i, GPIO_IN);
  }
  SetY(0);
  printf("... done.\n");
}
#endif

// These were copied from Pico's headers
// and made "hasty" by removing assertions.
static force_inline bool hasty_pio_sm_is_rx_fifo_empty(PIO pio, uint sm) {
  return (pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + sm))) != 0;
}

static force_inline uint32_t hasty_pio_sm_get(PIO pio, uint sm) {
  return pio->rxf[sm];
}

static force_inline void hasty_pio_sm_put(PIO pio, uint sm, uint32_t data) {
  pio->txf[sm] = data;
}

static force_inline uint WAIT_GET() {
  const PIO pio = pio0;
  constexpr uint sm = 0;
  // My own loop is faster than calling get_blocking:
  // return pio_sm_get_blocking(pio, sm);
  while (hasty_pio_sm_is_rx_fifo_empty(pio, sm)) continue;
  return hasty_pio_sm_get(pio, sm);
}

static force_inline void PUT(uint x) {
  const PIO pio = pio0;
  constexpr uint sm = 0;
  hasty_pio_sm_put(pio, sm, x);
}

void StartPio() {
  const PIO pio = pio0;
  constexpr uint sm = 0;

  pio_clear_instruction_memory(pio);
  pio_add_program_at_offset(pio, &tpio_program, 0);
  tpio_program_init(pio, sm, 0);
}

const char* DecodeCC(byte cc) {
  static char buf[9];
  buf[0] = 0x80 & cc ? 'E' : 'e';
  buf[1] = 0x40 & cc ? 'F' : 'f';
  buf[2] = 0x20 & cc ? 'H' : 'h';
  buf[3] = 0x10 & cc ? 'I' : 'i';
  buf[4] = 0x08 & cc ? 'N' : 'n';
  buf[5] = 0x04 & cc ? 'Z' : 'z';
  buf[6] = 0x02 & cc ? 'V' : 'v';
  buf[7] = 0x01 & cc ? 'C' : 'c';
  buf[8] = '\0';
  return buf;
}

bool TryGetUsbByte(char* ptr) {
  int rc = stdio_usb_in_chars(ptr, 1);
  return (rc != PICO_ERROR_NO_DATA);
}

void PollJustUsbInput() {
  // Try from USB to `usb_input` object.
  while (1) {
    char x = 0;
    bool ok = TryGetUsbByte(&x);
    if (ok) {
      usb_input.Put(x);
    } else {
      break;
    }
  }
}
void Fatal(const char* s) {
  while (*s) {
    putchar(*s);
    s++;
  }
  while (1) {
    putchar('#');
    sleep_ms(2000);
  }
}
void PollUsbInput() {
  // Try from USB to `usb_input` object.
  while (1) {
    char x = 0;
    bool ok = TryGetUsbByte(&x);
    if (ok) {
      usb_input.Put(x);
    } else {
      break;
    }
  }

  // Try from `usb_input` object to `term_input`, if it Peeks as ASCII
  while (1) {
    int peek = usb_input.HasAtLeast(1) ? (int)usb_input.Peek() : -1;
    if (1 <= peek && peek <= 126) {
      byte c = usb_input.Take();
      assert((int)c == peek);
      if (c == 10) {
        c = 13;
      }
      term_input.Put(c);
    } else {
      break;
    }
  }

  // Try from `usb_input` object to `disk_input`, if it Peeks as C_DISK_READ.
  int peek = usb_input.HasAtLeast(1) ? (int)usb_input.Peek() : -1;
  // Do not take, until a full packet is available.
  // This way, the initial C_DISK_READ byte will clog the buffer
  // and prevent any of these from ending up on term_input.
  switch (peek) {
    case C_REBOOT:
      ShowStr("\n*** REBOOTING ***\n");
#if 0
      delay_ms(200);
      rom_REBOOT(REBOOT_TYPE_NORMAL | REBOOT_TO_ARM | NO_RETURN_UNTIL_SUCCESS,
                 200 /* delay_ms */,
                 0 /* p0 */
                 0 /* p1 */);
#else
      Reboot();
#endif
      while (1) {
        sleep_ms(50);
        ShowChar('.');
      }
      break;

    case C_DISK_READ:
      if (usb_input.HasAtLeast(kDiskReadSize)) {
        for (uint i = 0; i < kDiskReadSize; i++) {
          byte t = usb_input.Take();
          disk_input.Put(t);
        }
      }
      break;

    case C_PRE_LOAD:
      while (usb_input.HasAtLeast(2)) {
        byte sz = 63 & usb_input.Peek(1);
        if (usb_input.HasAtLeast(2 + sz)) {
          PreLoadPacket();
        }
      }
      break;

    case -1:
      break;
    case 0:
      (void)usb_input.Take();
      break;

    default:
      Fatal("PollUsbInput -- default");
  }
  return;
}

// yak1

uint data;
uint num_resets;
uint event;
uint when;
uint num_swi2s;

bool vma;      // Valid Memory Address ( = delayed AVMA )
bool fic;      // First Instruction Cycle ( = delayed LIC )
uint next_pc;  // for multibyte ops.

uint TildePowerOf2;
uint OuterLoops;

template <typename T>
struct EngineBase {
  static void DumpPhys() {
    Quiet();
    putbyte(C_DUMP_PHYS);
    uint sz = T::PhysSize();
    for (uint i = 0; i < sz; i += 16) {
      for (uint j = 0; j < 16; j++) {
        if (T::ReadPhys(i + j)) goto yes;
      }
      continue;
    yes:
      putbyte(C_DUMP_LINE);
      putbyte(i >> 16);
      putbyte(i >> 8);
      putbyte(i);
      for (uint j = 0; j < 16; j++) {
        putbyte(T::ReadPhys(i + j));
      }
    }
    putbyte(C_DUMP_STOP);
    Noisy();
  }

  static void DumpRam() {
    Quiet();
    putbyte(C_DUMP_RAM);
    for (uint i = 0; i < 0x10000; i += 16) {
      for (uint j = 0; j < 16; j++) {
        if (T::Peek(i + j)) goto yes;
      }
      continue;
    yes:
      putbyte(C_DUMP_LINE);
      putbyte(i >> 16);
      putbyte(i >> 8);
      putbyte(i);
      for (uint j = 0; j < 16; j++) {
        putbyte(T::Peek(i + j));
      }
    }
    putbyte(C_DUMP_STOP);
    Noisy();
  }

  static void GET_STUCK() {
    while (1) {
      putbyte(255);  // signal for console to hang up.
      sleep_ms(1000);
    }
  }

  static void DumpRamAndGetStuck(const char* why, uint what) {
    interest = MAX_INTEREST;
    printf("\n(((((((((([[[[[[[[[[{{{{{{{{{{\n");
    printf("DumpRamAndGetStuck: %s ($%x = %d.)\n", why, what, what);
    if (T::PhysSize() > 0x10000) DumpPhys();
    DumpRam();
    printf("\n}}}}}}}}}}]]]]]]]]]]))))))))))\n");
    GET_STUCK();
  }

  static bool ChangeInterruptPin(bool irq_needed) {
    constexpr uint PULL_BLOCK_PC = 2;
    const PIO pio = pio0;
    constexpr uint sm = 0;

    int attempt = 200;
    T::ShowIrqs('>');
    while (pio_sm_get_pc(pio, sm) != PULL_BLOCK_PC) {
      T::ShowIrqs('^');
      attempt--;
      if (!attempt) {
        // We failed to hit the PULL_BLOCK_PC
        return false;
      }
    }

    // Disable the running TPIO program.
    pio_sm_set_enabled(pio, sm, false);

    // and switch to the Latch program.
    pio_clear_instruction_memory(pio);
    pio_add_program_at_offset(pio, &latch_program, 0);
    latch_program_init(pio, sm, 0);

    byte unused = 0x00;
    byte inputs = 0x00;
    byte irq_on = irq_needed ? 0xFB : 0xFF;
    byte outputs = 0xFF;
    pio_sm_put(pio, sm, QUAD_JOIN(unused, inputs, irq_on, outputs));

    // Wait for Finished signal on FIFO, then stop pio.
    (void)pio_sm_get_blocking(pio, sm);
    pio_sm_set_enabled(pio, sm, false);

    pio_clear_instruction_memory(pio);
    pio_add_program_at_offset(pio, &tpio_program, 0);
    tpio_program_init(pio, sm, 0);

    T::ShowIrqs(irq_needed ? ';' : ',');
    return true;
  }

  static void PreRoll() {
    const PIO pio = pio0;
    constexpr uint sm = 0;

    while (1) {
      constexpr uint GO_AHEAD = 0x12345678;
      pio_sm_put(pio, sm, GO_AHEAD);

      const uint got32 = WAIT_GET();

      byte junk, alo, ahi, flags;
      QUAD_SPLIT(junk, alo, ahi, flags, got32);
      const uint addr = HL_JOIN(ahi, alo);

      const bool reading = (flags & F_READ);
      const byte x = T::Peek(0xFFFE);

      printf(":Preroll: got %08x addr %x flags %x reading %x x %x\n", got32,
             addr, flags, reading, x);

      if (reading) {
        PUT(QUAD_JOIN(0xAA /*=unused*/, 0x00 /*=inputs*/, x,
                      0xFF /*=outputs*/));
      } else {
        {}  // do nothing.
      }  // end if reading

      if (addr == 0xFFFE) {
        printf(":Preroll: exit\n");
        return;
      }
    }
  }

  /////////////

  static void HandleIOWrite(uint addr, byte data) {
    const bool reading = false;

    byte dev = addr & 0xFF;  // TODO -- handle f256 with 2 IO pages
    IOWriter writer = IOWriters[dev];
    if (writer) {
      // New style, pluggable, not all is converted yet:
      ///// data = (*reader)(addr, data);
      writer(addr, data);
    } else
      switch (255 & addr) {
        case 0x90:  // GIME INIT0
          gime_irq_enabled = bool((data & 0x20) != 0);
          break;

        case 0x92:  // GIME IRQEN
          gime_vsync_irq_enabled = bool((data & 0x08) != 0);
          break;

      }  // switch addr & 255
  }  // HandleIOWrite

  static void HandleIORead(uint addr) {
    data = T::Peek(addr);  // default behavior

    byte dev = addr & 0xFF;
    // IOReader reader = IOReaders[dev];
    IOReader r = IOReaders[dev];
    if (r) {
      // New style, pluggable, not all is converted yet:
      ///// data = (*reader)(addr, data);
      data = (r)(addr, data);
    } else
      switch (dev) {
        case 0x92:  // GIME IRQEN register
          if (gime_irq_enabled && gime_vsync_irq_enabled &&
              gime_vsync_irq_firing) {
            data = 0x08;
            gime_vsync_irq_firing =
                false;  // Reading this register clears the IRQ.
          } else {
            data = 0;
          }
          break;

        default:
          break;
      }  // switch

    PUT(QUAD_JOIN(0xAA /*=unused*/, 0x00 /*=inputs*/, data, 0xFF /*=outputs*/));
  }  // HandleIORead

  static void ResetCpu() {
    printf("Resetting CPU ... ");
    InitializePinsForGpio();

    // Activate the 6309 RESET line
    SetY(4);
    for (uint i = 0; i < 8; i++) {
      gpio_put(i, 1);
      gpio_set_dir(i, GPIO_OUT);
      gpio_put(i, 1);
    }
    gpio_put(STATE_Y5_RESET_PIN, 0);  // 0 is active
    StrobePin(COUNTER_CLOCK);         // Y5
    StrobePin(COUNTER_CLOCK);         // Y6
    for (uint i = 0; i < 8; i++) {
      gpio_set_dir(i, GPIO_IN);
    }
    SetY(0);

    const uint EnoughCyclesToReset = 32;
    gpio_put(PIN_Q, 0);
    DELAY;
    gpio_put(PIN_E, 0);
    DELAY;
    for (uint i = 0; i < EnoughCyclesToReset; i++) {
      gpio_put(PIN_Q, 1);
      DELAY;
      gpio_put(PIN_E, 1);
      DELAY;
      gpio_put(PIN_Q, 0);
      DELAY;
      gpio_put(PIN_E, 0);
      DELAY;
    }

    // Release the 6309 RESET line
    SetY(4);
    for (uint i = 0; i < 8; i++) {
      gpio_put(i, 1);
      gpio_set_dir(i, GPIO_OUT);
      gpio_put(i, 1);
    }
    gpio_put(STATE_Y5_RESET_PIN, 1);  // 1 is release
    StrobePin(COUNTER_CLOCK);         // Y5
    StrobePin(COUNTER_CLOCK);         // Y6
    for (uint i = 0; i < 8; i++) {
      gpio_set_dir(i, GPIO_IN);
    }
    SetY(0);
    printf("... done.\n");
  }

  static void Run() {
    MUMBLE("CF");
    T::SendRamConfigOverUSB();

    MUMBLE("INS");
    T::Install();

    MUMBLE("RC");
    ResetCpu();
    MUMBLE("LZ");
    LED(0);
    MUMBLE("PIO");
    StartPio();

    T::StartTimer(16666 /* 60 Hz */);

    MUMBLE("RMC");
    RunMachineCycles();

    sleep_ms(100);
    printf("\nEngine Finished.\n");
    sleep_ms(100);
    MUMBLE("STUCK");
    GET_STUCK();

  }  // end Run

  static bool PeekDiskInput() {
    int peek = disk_input.HasAtLeast(1) ? (int)disk_input.Peek() : -1;
    switch (peek) {
      case C_DISK_READ:
        if (disk_input.HasAtLeast(kDiskReadSize)) {
          return true;
        }
        break;
    }
    return false;
  }

  static void ReadDisk(uint device, uint lsn, byte* buf) {
    printf("READ SDC SECTOR %x %x\n", device, lsn);
    putbyte(C_DISK_READ);
    putbyte(device);
    putbyte(lsn >> 16);
    putbyte(lsn >> 8);
    putbyte(lsn >> 0);

    while (1) {
      PollUsbInput();
      if (PeekDiskInput()) {
        for (uint k = 0; k < kDiskReadSize - 256; k++) {
          (void)disk_input.Take();  // 4-byte device & LSN.
        }
        for (uint k = 0; k < 256; k++) {
          buf[k] = disk_input.Take();
        }
        break;
      }
    }
  }

  static void RunMachineCycles() {
    uint cy = 0;  // This is faster if local.
    bool prev_irq_needed = false;

    // TOP
    const PIO pio = pio0;
    constexpr uint sm = 0;

    MUMBLE("PRE");
    PreRoll();

    MUMBLE("FFFF");
    const byte value_FFFF = T::Peek(0xFFFF);
    printf("value_FFFF = %x\n", value_FFFF);
    const uint value_FFFF_shift_8_plus_FF = (value_FFFF << 8) + 0xFF;

    MUMBLE("LOOP");
    ShowStr("\n========\n");
    printf("========\n");

    TildePowerOf2 = 1;
    for (OuterLoops = 0; true;
         OuterLoops++) {  ///////////////////////////////// Outer Machine Loop
      // for (OuterLoops= 0; OuterLoops < 30 * 4000; OuterLoops++) {
      // ///////////////////////////////// Outer Machine Loop

      if (OuterLoops <= (1 << (NumberOfLivenessTildes - 1))) {
        if (OuterLoops == TildePowerOf2 - 1) {
          // Draw tildes at cycle 0, 1, 3, 7, 15, ... to show we are up and
          // running.
          ShowChar('~');
          TildePowerOf2 <<= 1;
          if (OuterLoops == (1 << (NumberOfLivenessTildes - 1)) - 1) {
            ShowChar('\n');
          }
        }
      }

      bool irq_needed = false;

      irq_needed |= T::Turbo9sim_IrqNeeded();  // either Timer or RX

      if (T::DoesSamvdg()) {
        irq_needed |= (vsync_irq_enabled && vsync_irq_firing);
        T::ShowIrqs('H');
      }

      if (T::DoesAcia()) {
        irq_needed |= (acia_irq_enabled && acia_irq_firing);
        T::ShowIrqs('A');
      }

      if (T::DoesGime()) {
        irq_needed |= (gime_irq_enabled && gime_vsync_irq_enabled &&
                       gime_vsync_irq_firing);
        T::ShowIrqs('G');
      }

      if (irq_needed != prev_irq_needed) {
        bool ok = ChangeInterruptPin(irq_needed);
        if (ok) {
          prev_irq_needed = irq_needed;
          LED(irq_needed);
        }
      }

      PollUsbInput();

      if (T::Turbo9sim_CanRx()) {
        if (term_input.HasAtLeast(1)) {
          byte ch = term_input.Take();
          T::Turbo9sim_SetRx(ch);
        }
      }

      if (T::DoesAcia()) {
        if (not acia_char_in_ready) {
          if (term_input.HasAtLeast(1)) {
            acia_char = term_input.Take();
            acia_char_in_ready = true;
            acia_irq_firing = true;
          } else {
            acia_char = 0;
            acia_char_in_ready = false;
            acia_irq_firing = false;
          }
        }
      }

      if (!T::DoesPicoTimer()) {
        // Simulate timer firing every so-many cycles,
        // but not with realtime timer,
        // because we are running slowly.
        if ((cy & VSYNC_TICK_MASK) == 0) {
          TimerFired = true;
        }
        // Notice that relies on RAPID_BURST_CYCLES
        // being a power of two.
      }

      if (TimerFired) {
        TimerFired = false;

        T::Turbo9sim_SetTimerFired();

        if (T::DoesSamvdg()) {
          T::Poke(0xFF03,
                  T::Peek(0xFF03) |
                      0x80);  // Set the bit indicating VSYNC occurred.
          if (vsync_irq_enabled) {
            vsync_irq_firing = true;
          }
          if (gime_irq_enabled && gime_vsync_irq_enabled) {
            gime_vsync_irq_firing = true;
          }
        }
      }  // end if TimerFired

      T::BenchmarkCycle(cy);

      for (uint loop = 0; loop < RAPID_BURST_CYCLES;
           loop++) {  /////// Inner Machine Loop

        if (T::DoesLog()) {
          if (cy >= trace_at_what_cycle) interest = 999999;
        }

        constexpr uint GO_AHEAD = 0x12345678;
        pio_sm_put(pio, sm, GO_AHEAD);

        const uint get32 = WAIT_GET();
        const uint addr = (0xFF00 & get32) | (get32 >> 16);
        const uint flags = get32;
        const bool reading = (flags & F_READ) != 0;

        // =============================================================
        // =============================================================

        if (likely(addr < 0xFE00)) {
          if (reading) {
            PUT((T::FastPeek(addr) << 8) + 0xFF);
            if (T::DoesLog()) {
              data = T::FastPeek(addr);
            }
          } else {
            const uint data_and_more = WAIT_GET();
            T::FastPoke(addr, (byte)data_and_more);
            if (T::DoesLog()) {
              data = (byte)data_and_more;
            }
          }  // end if reading

          // =============================================================
        } else if (addr == 0xFFFF) {
          if (reading) {
            PUT(value_FFFF_shift_8_plus_FF);
            if (T::DoesLog()) {
              data = value_FFFF;
            }
          } else {
            const byte foo = WAIT_GET();
            (void)foo;
            if (T::DoesLog()) {
              data = foo;
            }
          }

          // =============================================================
        } else if (addr < 0xFF00) {
          if (reading) {  // if reading FExx
            PUT((T::Peek(addr) << 8) + 0xFF);
            if (T::DoesLog()) {
              data = T::Peek(addr);
            }
          } else {  // if writing FExx
            const byte foo = WAIT_GET();
            T::Poke(addr, foo, 0x3F);
            if (T::DoesLog()) {
              data = foo;
            }
          }  // end if reading

          // =============================================================
        } else {
          if (reading) {  // CPU reads, Pico Tx
            HandleIORead(addr);
          } else {  // if writing
            // CPU writes, Pico Rx
            data = WAIT_GET();
            T::Poke(addr, data, 0x3F);
            HandleIOWrite(addr, data);
          }  // end if reading / writing
        }  // end four addr type cases

        // =============================================================
        // =============================================================

        if (fic and reading and addr < 0xFFF0) {
          current_opcode_cy = cy;
          current_opcode_pc = addr;
          current_opcode = data;

          if (T::BadPc(addr)) {
            DumpRamAndGetStuck("PC out of range", addr);
          }

          T::Hyper(data, addr);
        }

        if (T::DoesLog()) {
          if (reading and addr != 0xFFFF) {
            if (current_opcode == 0x10 /* prefix */ &&
                current_opcode_cy + 1 == cy) {
              current_opcode = 0x1000 | data;
              // printf("change to opcode %x\n", current_opcode);
            }
            if (current_opcode == 0x11 /* prefix */ &&
                current_opcode_cy + 1 == cy) {
              current_opcode = 0x1100 | data;
              // printf("change to opcode %x\n", current_opcode);
            }

            if (T::DoesEvent()) {
              if (current_opcode == 0x3B) {  // RTI
                uint age = cy - current_opcode_cy -
                           2 /*one byte opcode, one extra cycle */;
                if (0)
                  printf("~RTI~R<%d,ccy=%d,cpc=%d,cop=%x,a=%d> %04x:%02x\n", cy,
                         current_opcode_cy, current_opcode_pc, current_opcode,
                         age, addr, data);
                interest += 50;
                // TODO -- recognize E==0 for FIRQ
                if (age < RTI_SZ) {
                  hist_data[age] = data;
                  hist_addr[age] = addr;
                  if (age == RTI_SZ - 1) {
                    T::SendEventHist(EVENT_RTI, RTI_SZ);
                  }
                }
              }  // end RTI
              if (current_opcode == 0x103F) {  // SWI2/OS9
                uint age =
                    cy - current_opcode_cy - 2 /*two byte opcode.  extra cycle
                                                  contains OS9 call number. */
                    ;
                if (0)
                  printf("~OS9~R<%d,ccy=%d,cpc=%d,cop=%x,a=%d> %04x:%02x\n", cy,
                         current_opcode_cy, current_opcode_pc, current_opcode,
                         age, addr, data);
                interest += 50;
                if (age < SWI2_SZ) {
                  hist_data[age] = data;
                  hist_addr[age] = addr;
                  if (age == SWI2_SZ - 1) {
                    T::SendEventHist(EVENT_SWI2, SWI2_SZ);
                  }
                }
              }
            }
          }

          if (!reading) {
            if (T::DoesLog()) {
              if (current_opcode == 0x103F) {  // SWI2/OS9
                uint age = cy - current_opcode_cy - 2 /*two byte opcode*/;
                if (0)
                  printf("~OS9~W<%d,ccy=%d,cpc=%d,cop=%x,a=%d> %04x:%02x\n", cy,
                         current_opcode_cy, current_opcode_pc, current_opcode,
                         age, addr, data);
                interest += 50;
                if (age < SWI2_SZ) {
                  hist_data[age] = data;
                  hist_addr[age] = addr;
                }
              }
            }
          }

          if (T::DoesPcRange()) {
            if (current_opcode == 0x20 /*BRA*/ && current_opcode_cy + 1 == cy) {
              if (data == 0xFE) {
                DumpRamAndGetStuck("Infinite BRA loop", addr);
              }
            }
          }

        }  //  T::DoesLog()

        uint high = flags & F_HIGH;

        if (T::DoesTrace()) {
          if (reading and (not vma) and (addr == 0xFFFF)) {
            T::TransmitCycle(cy, high & 31, CY_IDLE, 0, 0);
          } else {
            const char* label = reading ? (vma ? "r" : "-") : "w";
            byte kind = reading ? (vma ? CY_READ : CY_IDLE) : CY_WRITE;
            if (reading) {
              if (fic) {
                label = "@";
                kind = CY_SEEN;
                if (!T::WasItSeen(addr)) {
                  label = "@@";
                  kind = CY_UNSEEN;
                }
                next_pc = addr + 1;
              } else {
                // case: Reading but not FIC
                if (next_pc == addr) {
                  label = "&";
                  kind = CY_MORE;
                  next_pc++;
                }

              }  // end case Reading but not FIC

            }  // end if reading
            T::TransmitCycle(cy, high & 31, kind, data, addr);
          }  // end if valid cycle

          if (fic) {
            T::SeeIt(addr);
          }
        }

        if (T::DoesLog()) {
          vma = (0 != (flags & F_AVMA));
          fic = (0 != (flags & F_LIC));
        }

        cy++;
      }  // next inner machine loop

      if (stop_at_what_cycle) {
        if (cy >= stop_at_what_cycle) {
          printf("=== TFR9 STOPPING BECAUSE CYCLE %d >= %d\n", cy,
                 stop_at_what_cycle);
          goto exit;
        }
      }

    }  // while true

  exit:
    ShowStr("\n<<< exit: TFR9 STOPPING >>>\n");
    Reboot();
  }  // end RunMachineCycles
};  // end struct EngineBase

template <typename T>
struct Slow_Mixins : DoPcRange<T, 0x0010, 0xFF01>,
                     DoTrace<T>,
                     DoSeen<T>,
                     Logging<T, LIrq>,
                     // DoLogMmu<T>,
                     DoShowIrqs<T>,

                     DoTraceRamWrites<T>,
                     DoHyper<T>,
                     DoEvent<T>,
                     DontDumpRamOnEvent<T>,
                     DontPicoTimer<T> {};

template <typename T>
struct Fast_Mixins : DontPcRange<T>,
                     DontTrace<T>,
                     DontSeen<T>,
                     Logging<T, LHello>,
                     // DontLogMmu<T>,
                     DontShowIrqs<T>,

                     DontTraceRamWrites<T>,
                     DontHyper<T>,
                     DontEvent<T>,
                     DontDumpRamOnEvent<T>,
                     DoPicoTimer<T> {};

template <typename T>
struct Common_Mixins : EngineBase<T>, CommonRam<T> {};

template <typename T>
struct T9_Mixins : Common_Mixins<T>,
                   SmallRam<T>,
                   DontBenchmarkCycles<T>,
                   DontAcia<T>,
                   DontGime<T>,
                   DontSamvdg<T>,
                   DoTurbo9sim<T>,
                   DoTurbo9os<T> {
  static void Install() {
    ShowChar('A');
    T::Install_OS();
    ShowChar('B');
    T::Turbo9sim_Install(0xFF00);
    ShowChar('C');
    ShowChar('\n');
  }
};

struct T9_Slow : T9_Mixins<T9_Slow>, Slow_Mixins<T9_Slow> {};

struct T9_Fast : T9_Mixins<T9_Fast>, Fast_Mixins<T9_Fast> {};

template <typename T>
struct X9_Mixins : Common_Mixins<T>,
                   DontBenchmarkCycles<T>,
                   DontAcia<T>,
                   DontGime<T>,
                   DontSamvdg<T>,
                   DoTurbo9sim<T> {
  static void Install() {
    // Without OS.  Must use PreLoadPacket() or some other way of loading a
    // program.
    ShowChar('X');
    T::Turbo9sim_Install(0xFF00);
    ShowChar('Y');
  }
};

// X1 is a blank machine with no OS, Turbo9Sim-like IO, and a Small Ram.
struct X1_Slow : SmallRam<X1_Slow>, X9_Mixins<X1_Slow>, Slow_Mixins<X1_Slow> {};
struct X1_Fast : SmallRam<X1_Fast>, X9_Mixins<X1_Fast>, Fast_Mixins<X1_Fast> {};

// X2 is a blank machine with no OS, Turbo9Sim-like IO, and a Big Ram.
struct X2_Slow : BigRam<X2_Slow>, X9_Mixins<X2_Slow>, Slow_Mixins<X2_Slow> {};
struct X2_Fast : BigRam<X2_Fast>, X9_Mixins<X2_Fast>, Fast_Mixins<X2_Fast> {};

template <typename T>
struct L1_Mixins : Common_Mixins<T>,
                   SmallRam<T>,
                   DontBenchmarkCycles<T>,
                   DoAcia<T>,
                   DoEmudsk<T>,
                   DontGime<T>,
                   DoSamvdg<T>,
                   DontTurbo9sim<T>,
                   DoNitros9level1<T> {
  static void Install() {
    ShowChar('A');
    T::Install_OS();
    ShowChar('B');
    T::Samvdg_Install();
    ShowChar('C');
    T::Emudsk_Install(0xFF80);
    ShowChar('D');
    T::Acia_Install(0xFF06);
    ShowChar('E');
    ShowChar('\n');
  }
};

struct L1_Slow : L1_Mixins<L1_Slow>, Slow_Mixins<L1_Slow> {};

struct L1_Fast : L1_Mixins<L1_Fast>, Fast_Mixins<L1_Fast> {};

template <typename T>
struct L2_Mixins : Common_Mixins<T>,
                   BigRam<T>,
                   DontBenchmarkCycles<T>,
                   DoAcia<T>,
                   DoEmudsk<T>,
                   DoGime<T>,
                   DoSamvdg<T>,
                   DontTurbo9sim<T>,
                   DoNitros9level2<T> {
  static void Install() {
    ShowChar('A');
    T::Install_OS();
    ShowChar('B');
    T::Samvdg_Install();
    ShowChar('C');
    T::Emudsk_Install(0xFF80);
    ShowChar('D');
    T::Acia_Install(0xFF06);
    ShowChar('E');
    ShowChar('\n');
  }
};

struct L2_Slow : L2_Mixins<L2_Slow>, Slow_Mixins<L2_Slow> {};

struct L2_Fast : L2_Mixins<L2_Fast>, Fast_Mixins<L2_Fast> {};

struct harness {
  std::function<void(void)> engines[5];
  std::function<void(void)> fast_engines[5];

  harness() {
    memset(engines, 0, sizeof engines);
    memset(fast_engines, 0, sizeof fast_engines);

    engines[0] = T9_Slow::Run;
    engines[1] = L1_Slow::Run;
    engines[2] = L2_Slow::Run;
    engines[3] = X1_Slow::Run;
    engines[4] = X2_Slow::Run;

    fast_engines[0] = T9_Fast::Run;
    fast_engines[1] = L1_Fast::Run;
    fast_engines[2] = L2_Fast::Run;
    fast_engines[3] = X1_Fast::Run;
    fast_engines[4] = X2_Fast::Run;
  }
};

void PreLoadPacket() {
  (void)usb_input.Take();  // command byte C_PRE_LOAD
  uint sz = 63 & usb_input.Take();
  assert(sz > 2);  // sz is packet size (number of bytes that follow sz).
  uint hi = usb_input.Take();
  uint lo = usb_input.Take();
  uint addr = (hi << 8) | lo;
  uint n = sz - 2;  // n is number of following bytes to be poked.
  putchar('(');
  for (uint i = 0; i < n; i++) {
    ram[addr] = ram[addr + 0x10000] =
        usb_input.Take();  // set upper and lower bank.
    addr++;
    if ((i & 7) == 0) putchar('.');
  }
  putchar(')');
}

void Shell() {
  struct harness harness;
  Verbosity = 5;
  Traceosity = 5;

  while (true) {
      for (int i = 0; i<200; i++) {
        ShowChar(".:,;"[i & 3]);
        PollUsbInput();

        if (term_input.HasAtLeast(1)) {
          byte ch = term_input.Take();
          ShowChar('<');
          if (32 <= ch && ch <= 126) {
            ShowChar(ch);
          } else {
            ShowChar('#');
          }
          ShowChar('>');

          if ('0' <= ch && ch <= '4') {
            uint num = ch - '0';
            if (harness.fast_engines[num]) {
              harness.fast_engines[num]();
            } else {
              ShowStr("-S?-");
            }

          } else if ('5' <= ch && ch <= '9') {
            uint num = ch - '5';
            if (harness.engines[num]) {
              harness.engines[num]();
            } else {
              ShowStr("-F?-");
            }

          } else if (ch == 'q') {
            Traceosity = 6;
          } else if (ch == 'w') {
            Traceosity = 7;
          } else if (ch == 'e') {
            Traceosity = 8;
          } else if (ch == 'r') {
            Traceosity = 9;

          } else if (ch == 'j') {
            Verbosity = 2;
          } else if (ch == 'k') {
            Verbosity = 3;
          } else if (ch == 'l') {
            Verbosity = 4;
          } else if (ch == 'a') {
            Verbosity = 6;
          } else if (ch == 's') {
            Verbosity = 7;
          } else if (ch == 'd') {
            Verbosity = 8;
          } else if (ch == 'f') {
            Verbosity = 9;

          } else if (ch == 'v') {
            set_sys_clock_khz(200000, true);
          } else if (ch == 'c') {
            set_sys_clock_khz(250000, true);
          } else if (ch == 'x') {
            set_sys_clock_khz(260000, true);
          } else if (ch == 'z') {
            set_sys_clock_khz(270000, true);

          } else {
            ShowStr("-#?-");
          }
        }  // term_input

        if (90 <= i && i <= 100) {
            LED(1);
        } else if (120 < i && i < 130) {
            LED(1);
        } else {
            LED(0);
        }

        sleep_ms(10);
      }
  }
  // Shell never returns.
}

int main() {
  stdio_usb_init();

  gpio_init(25);
  gpio_set_dir(25, GPIO_OUT);
  LED(0);
  InitializePinsForGpio();

  LED(1);
  sleep_ms(100);
  LED(0);
  sleep_ms(150);
  LED(1);
  sleep_ms(100);
  LED(0);
  sleep_ms(150);

  interest = MAX_INTEREST;  /// XXX

  quiet_ram = 0;

  Shell();
}
