#ifdef TRACKING
#else
  #define TRACKING 0
#endif

#define SHOW_IRQS TRACKING

#define INCLUDED_DISK 0

constexpr unsigned BLINKS = 5;  // Initial LED blink countdown.

#if OS_LEVEL == 90
#define BANNER "Level 90 Turbo9SIM"
#define FOR_COCO 0  // SAM & VDG & PIAs
#define FOR_TURBO9SIM 1
#define IRQS 1
#endif

#if OS_LEVEL == 100
#define FOR_COCO 1  // SAM & VDG & PIAs
#define FOR_TURBO9SIM 0
#define IRQS 1
#define BANNER "Level 1 NitrOS-9"
#endif

#if OS_LEVEL == 200
#define FOR_COCO 1  // SAM & VDG & PIAs
#define FOR_TURBO9SIM 0
#define IRQS 1
#define BANNER "Level 2 NitrOS-9"
#endif

// #define TRACKING TRACE_CYCLES
// #define TRACKING_CYCLE 1124000 // 566750 // 940000
// #define STOP_CYCLE 2000000 // 652689 // 1139300

#define SEEN TRACKING
#define RECORD 0  // Fix me later.
#define HEURISTICS TRACKING
#define OPCODES TRACKING
#define TRACE_RTI TRACKING

#define ALLOW_DUMP_PHYS 0
#define ALLOW_DUMP_RAM 0

#define RAPID_BURST_CYCLES 256  // Without checking IRQs, etc.

// tmanager.cpp -- for the TFR/901 -- strick
//
// SPDX-License-Identifier: MIT

#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/structs/systick.h>
#include <hardware/timer.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <pico/unique_id.h>
#include <stdio.h>

#include <cstring>
#include <functional>
#include <vector>

#define LED(X) gpio_put(25, (X))

//////////////////////////////////////// #include "pico/cyw43_arch.h"

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define force_inline inline __attribute__((always_inline))

typedef unsigned char byte;
typedef unsigned int word;
typedef unsigned char T_byte;
typedef unsigned int T_word;
typedef unsigned char T_16[16];

extern void putbyte(byte x);
extern void SendIn_byte(byte x);
extern void SendIn_word(word x);
extern void SendIn_16(byte* x);
extern void RecvOut_byte(byte* x);

volatile bool TimerFired;

uint quiet_ram;
inline void Quiet() { quiet_ram++; }
inline void Noisy() { quiet_ram--; }

extern uint interest;

bool V2;

struct DontLog {
  force_inline int Logf(const char* fmt, ...) { return 0; }
};
struct DoLog {
  int Logf(const char* fmt, ...) {
    if (!interest) return 0;
    if (quiet_ram > 0) return 0;

    va_list va;
    va_start(va, fmt);
    int z = printf(fmt, va);
    va_end(va);
    return z;
  }
};

// Configuration
#include "tfr9ports.gen.h"

// PIO code
#include "latch.pio.h"
#include "tpio.pio.h"

///////////////////////  // LED_W for Pico W:   LED_W(1) for on, LED_W(0) for
/// off.
///////////////////////  #define LED_W(X)
/// cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (X))

#define ATTENTION_SPAN 25  // was 250

#if TRACKING
#define VSYNC_TICK_MASK \
  0xFFFF  // 0x3FF  // 0xFFFF   // one less than a power of two
#define ACIA_TICK_MASK 0xFF  // 0xFFF     // one less than a power of two
#else
#define VSYNC_TICK_MASK 0x3FFF  // 0xFFFF   // one less than a power of two
#define ACIA_TICK_MASK 0xFF     // 0xFFF     // one less than a power of two
#endif

#define TFR_RTC_BASE 0xFF50

#if TRACKING

#define SHOW_TICKS 1
#define PICO_USE_TIMER 0
#define D \
  if (interest) printf
#define Q \
  if (interest) printf

#else

#define SHOW_TICKS 0
#define PICO_USE_TIMER 1
#define D \
  if (0) printf
#define Q \
  if (0) printf

#endif

#define HL_JOIN(H, L) (((255 & (H)) << 8) | ((255 & (L)) << 0))

#define HL_SPLIT(H, L, X) (H = (byte)((X) >> 8), L = (byte)((X) >> 0))

#define QUAD_JOIN(A, B, C, D)                                       \
  (((255 & (A)) << 24) | ((255 & (B)) << 16) | ((255 & (C)) << 8) | \
   ((255 & (D)) << 0))

#define QUAD_SPLIT(A, B, C, D, X)                                      \
  (A = (byte)((X) >> 24), B = (byte)((X) >> 16), C = (byte)((X) >> 8), \
   D = (byte)((X) >> 0))

char Buf64[64];  // General use Buffer.

uint interest;
uint btbug;

uint current_opcode_cy;  // what was the CY of the current opcode?
uint current_opcode_pc;  // what was the PC of the current opcode?
uint current_opcode;     // what was the current opcode?
uint sdc_disk_pending;
byte sdc_disk_read_data[256];
byte* sdc_disk_read_ptr;

constexpr uint RTI_SZ = 12;
constexpr uint SWI2_SZ = 17;
static byte hist_data[24];
static uint hist_addr[24];

#define MAX_INTEREST 999999999
#define getchar(X) NeverUseGetChar

/////////// #include "../generated/rpc.h"

const byte Rom[] = {
#if FOR_TURBO9SIM
#include "turbo9sim.rom.h"
#endif

#if FOR_COCO

#if OS_LEVEL <= 199
#include "../generated/level1.rom.h"
#endif
#if OS_LEVEL >= 200
#include "../generated/level2.rom.h"
#endif

#endif
};

#if INCLUDED_DISK
byte Disk[] = {
#if OS_LEVEL == 100
#include "level1.disk.h"
#endif
#if OS_LEVEL == 200
#include "level2.disk.h"
#endif
};
#endif

#define LEVEL1_LAUNCHER_START 0x2500
#define LEVEL2_LAUNCHER_START 0x2500

uint FFFxVectors[] = {
#if OS_LEVEL == 100
    0,  //  6309 TRAP
    // From ~/coco-shelf/toolshed/cocoroms/bas13.rom :
    0x0100,
    0x0103,
    0x010f,
    0x010c,
    0x0106,
    0x0109,
    LEVEL1_LAUNCHER_START,  //  RESET
#endif
#if OS_LEVEL == 200
    // From ~/coco-shelf/toolshed/cocoroms/coco3.rom :
    0x0000,  // 6309 traps
    0xFEEE,
    0xFEF1,
    0xFEF4,
    0xFEF7,
    0xFEFA,
    0xFEFD,
    LEVEL2_LAUNCHER_START,
#endif
};

enum {
  // C_NOCHAR=160,
  C_PUTCHAR = 161,
  // C_GETCHAR=162,
  C_STOP = 163,
  C_ABORT = 164,
  C_KEY = 165,
  C_NOKEY = 166,
  C_DUMP_RAM = 167,
  C_DUMP_LINE = 168,
  C_DUMP_STOP = 169,
  C_DUMP_PHYS = 170,
  C_POKE = 171,
  C_EVENT = 172,
  C_DISK_READ = 173,
  C_DISK_WRITE = 174,
  C_CONFIG = 175,
  //
  // EVENT_PC_M8 = 238,
  // EVENT_GIME = 239,
  EVENT_RTI = 240,
  EVENT_SWI2 = 241,
  EVENT_CC = 242,
  EVENT_D = 243,
  EVENT_DP = 244,
  EVENT_X = 245,
  EVENT_Y = 246,
  EVENT_U = 247,
  EVENT_PC = 248,
  EVENT_SP = 249,
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

using IOReader = std::function<byte(uint addr, byte data)>;
using IOWriter = std::function<void(uint addr, byte data)>;

extern "C" {
extern int stdio_usb_in_chars(char* buf, int length);
}

void ShowChar(byte ch) {
  putchar(C_PUTCHAR);
  putchar(ch);
}
void ShowStr(const char* s) {
  while (*s) {
    ShowChar(*s++);
  }
}

// putbyte does CR/LF escaping for Binary Data
void putbyte(byte x) { putchar_raw(x); }

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

#include "ram.h"
#include "turbo9sim.h"

#if SEEN
class Bitmap64K {
  const static uint SZ = (1u << 16);

 public:
  byte guts[SZ >> 3];

  bool operator[](uint i) const {
    i &= (SZ - 1);
    byte mask = 1 << (i & 7);
    uint sub = i >> 3;
    return (0 != (mask & guts[sub]));
  }
  void Insert(uint i) {
    i &= (SZ - 1);
    byte mask = 1 << (i & 7);
    uint sub = i >> 3;
    guts[sub] |= mask;
  }

} Seen;
#endif

void DumpPhys() {
#if ALLOW_DUMP_PHYS
  Quiet();
  putbyte(C_DUMP_PHYS);
  uint sz = the_ram.PhysSize();
  for (uint i = 0; i < sz; i += 16) {
    for (uint j = 0; j < 16; j++) {
      if (the_ram.ReadPhys(i + j)) goto yes;
    }
    continue;
  yes:
    putbyte(C_DUMP_LINE);
    putbyte(i >> 16);
    putbyte(i >> 8);
    putbyte(i);
    for (uint j = 0; j < 16; j++) {
      putbyte(the_ram.ReadPhys(i + j));
    }
  }
  putbyte(C_DUMP_STOP);
  Noisy();
#endif
}

void DumpRam() {
#if ALLOW_DUMP_RAM
  Quiet();
  putbyte(C_DUMP_RAM);
  for (uint i = 0; i < 0x10000; i += 16) {
    for (uint j = 0; j < 16; j++) {
      if (Peek(i + j)) goto yes;
    }
    continue;
  yes:
    putbyte(C_DUMP_LINE);
    putbyte(i >> 16);
    putbyte(i >> 8);
    putbyte(i);
    for (uint j = 0; j < 16; j++) {
      putbyte(Peek(i + j));
    }
  }
  putbyte(C_DUMP_STOP);
  Noisy();
#endif
}

void GET_STUCK() {
  while (1) {
    putbyte(255);
    sleep_ms(1000);
  }
}

void DumpRamAndGetStuck(const char* why, uint what) {
  interest = MAX_INTEREST;
  printf("\n(((((((((([[[[[[[[[[{{{{{{{{{{\n");
  printf("DumpRamAndGetStuck: %s ($%x = %d.)\n", why, what, what);
#if RECORD
  DumpTrace();
#endif  // RECORD
  DumpPhys();
  DumpRam();
  printf("\n}}}}}}}}}}]]]]]]]]]]))))))))))\n");
  GET_STUCK();
}

uint AddressOfRom() {
#if FOR_TURBO9SIM
  return 0;
#endif
#if FOR_COCO
  return 0x2500;
#endif
}

void ViewAt(const char* label, uint hi, uint lo) {
#if 0
#if TRACKING
    Quiet();
    uint addr = (hi << 8) | lo;
    VIEWF("=== %s: @%04x: ", label, addr);
    for (uint i = 0; i < 8; i++) {
        uint x = Peek2(addr+i+i);
        VIEWF("%04x ", x);
    }
    VIEWF("|");
    for (uint i = 0; i < 16; i++) {
        byte ch = 0x7f & Peek(addr+i);
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
#endif
}

void StrobePin(uint pin) {
  gpio_put(pin, 0);
  DELAY;
  gpio_put(pin, 1);
  DELAY;
}

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

  const uint EnoughCyclesToReset = 12;
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

static inline bool hasty_pio_sm_is_rx_fifo_empty(PIO pio, uint sm) {
  return (pio->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + sm))) != 0;
}

static inline uint32_t hasty_pio_sm_get(PIO pio, uint sm) {
  return pio->rxf[sm];
}

static inline void hasty_pio_sm_put(PIO pio, uint sm, uint32_t data) {
  pio->txf[sm] = data;
}

static inline uint WAIT_GET() {
  const PIO pio = pio0;
  constexpr uint sm = 0;
  // My own loop is fastere than calling get_blocking:
  // return pio_sm_get_blocking(pio, sm);
  while (hasty_pio_sm_is_rx_fifo_empty(pio, sm)) continue;
  return hasty_pio_sm_get(pio, sm);
}

static inline void PUT(uint x) {
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

#if IRQS
bool FinishChangeIrq(bool irq_needed) {
  const PIO pio = pio0;
  constexpr uint sm = 0;

  // Assume tpio_program is already stuck at beginning of loop.
  // Disable it.
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

  if (SHOW_IRQS) ShowChar(irq_needed ? ';' : ',');
  return true;
}
bool NeoIrq(bool irq_needed) {
  const PIO pio = pio0;
  constexpr uint sm = 0;

  int attempt = 100;
  if (SHOW_IRQS) ShowChar('>');
  while (pio_sm_get_pc(pio, sm) != 2) {
    if (SHOW_IRQS) ShowChar('^');
    attempt--;
    if (!attempt) {
      // We failed to hit the loop at PC=1.
      return false;
    }
  }
  return FinishChangeIrq(irq_needed);
}
#endif

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

bool gime_irq_enabled;
bool gime_vsync_irq_enabled;
bool gime_vsync_irq_firing;

bool vsync_irq_enabled;
bool vsync_irq_firing;

bool acia_irq_enabled;
bool acia_irq_firing;
bool acia_char_in_ready;
int acia_char;

template <uint N>
class CircBuf {
 private:
  byte buf[N];
  uint nextIn, nextOut;

  uint NumBytesAvailable() {
    if (nextOut <= nextIn)
      return nextIn - nextOut;
    else
      return nextIn + N - nextOut;
  }

 public:
  CircBuf() : nextIn(0), nextOut(0) {}

  bool HasAtLeast(uint n) {
    uint ba = NumBytesAvailable();
    return ba >= n;
  }
  byte Peek() { return buf[nextOut]; }
  byte Take() {
    byte z = buf[nextOut];
    ++nextOut;
    if (nextOut >= N) nextOut = 0;
    return z;
  }
  void Put(byte x) {
    // ShowChar('`');
    buf[nextIn] = x;
    ++nextIn;
    if (nextIn >= N) nextIn = 0;
  }
};

#if 0
TODO
void PutIrq(bool activate) {
  gpio_put(IRQ_BAR_PIN, not activate);  // negative logic
}
#endif

#if PICO_USE_TIMER
struct repeating_timer TimerData;

bool TimerCallback(repeating_timer_t* rt) {
  TimerFired = true;
  return true;
}
#endif

const uint kDiskReadSize = 1 + 4 + 256;

bool TryGetUsbByte(char* ptr) {
  int rc = stdio_usb_in_chars(ptr, 1);
  return (rc != PICO_ERROR_NO_DATA);
}

  CircBuf<1200> usb_input;
  CircBuf<1200> term_input;
  CircBuf<1200> disk_input;

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
    switch (peek) {
      case C_DISK_READ:
        if (usb_input.HasAtLeast(kDiskReadSize)) {
          for (uint i = 0; i < kDiskReadSize; i++) {
            byte t = usb_input.Take();
            disk_input.Put(t);
          }
        }
        break;
    }
    return;
  }

// yak1

struct EngineBase {
  virtual void Run() {
    printf("EngineBase::Run : subclass responsibility");
  }
};

template <class ToLog, class RamT, class ToTurbo9sim>
struct Engine 
              : public EngineBase
              , public ToTurbo9sim
              , public ToLog {
  RamT the_ram;

  void SendEventHist(byte event, byte sz) {
#if 1
    Quiet();
    putbyte(C_EVENT);
    putbyte(event);
    putbyte(sz);
    putbyte(current_opcode_pc >> 8);
    putbyte(current_opcode_pc);
    putbyte(current_opcode_cy >> 24);
    putbyte(current_opcode_cy >> 16);
    putbyte(current_opcode_cy >> 8);
    putbyte(current_opcode_cy);
    for (byte i = 0; i < sz; i++) {
      putbyte(hist_data[i]);
      putbyte(hist_addr[i] >> 8);
      putbyte(hist_addr[i]);
    }
    Noisy();
#endif  // TRACKING
  }

  void SendEventRam(byte event, byte sz, word base_addr) {
#if TRACKING
    Quiet();
    putbyte(C_EVENT);
    putbyte(event);
    putbyte(sz);
    putbyte(base_addr >> 8);
    putbyte(base_addr);
    for (byte i = 0; i < sz; i++) {
      putbyte(Peek(base_addr + i));
    }
    Noisy();
#endif  // TRACKING
  }

  force_inline byte FastPeek(uint addr) { return the_ram.FastRead(addr); }
  force_inline byte Peek(uint addr) { return the_ram.Read(addr); }

  force_inline void Poke(uint addr, byte data) { the_ram.Write(addr, data); }
#if 0
force_inline void PokeQuietly(uint addr, byte data) { the_ram.WriteQuietly(addr, data); }
#endif
  force_inline void FastPoke(uint addr, byte data) {
    the_ram.FastWrite(addr, data);
  }

  force_inline void Poke(uint addr, byte data, byte block) {
    the_ram.Write(addr, data, block);
  }

  force_inline uint Peek2(uint addr) {
    uint hi = Peek(addr);
    uint lo = Peek(addr + 1);
    return (hi << 8) | lo;
  }
  force_inline void Poke2(uint addr, uint data) {
    byte hi = (byte)(data >> 8);
    byte lo = (byte)data;
    Poke(addr, hi);
    Poke(addr + 1, lo);
  }

  void InitRamFromRom() {
    Quiet();
    uint start = AddressOfRom();
    for (uint i = 0; i < sizeof Rom; i++) {
      uint addr = start + i;
      Poke(addr, Rom[i]);

#if 0
        putbyte(C_POKE);
        putbyte(the_ram.Block(addr));
        putbyte(addr >> 8);
        putbyte(addr);
        putbyte(Rom[i]);
#endif
    }
    Noisy();
  }

  void PreRoll() {
    const PIO pio = pio0;
    constexpr uint sm = 0;

    while (1) {
      constexpr uint GO_AHEAD = 0x12345678;
      pio_sm_put(pio, sm, GO_AHEAD);

      const uint got32 = WAIT_GET();

      byte junk, alo, ahi, flags;
      QUAD_SPLIT(junk, alo, ahi, flags, got32);
      const uint addr = HL_JOIN(ahi, alo);

      // const uint addr = WAIT_GET();
      // const uint flags = WAIT_GET();
      const bool reading = (flags & F_READ);
      const byte x = Peek(0xFFFE);

      printf(":Preroll: got %08x addr %x flags %x reading %x x %x\n", got32,
             addr, flags, reading, x);

      if (reading) {
        PUT(QUAD_JOIN(0xAA /*=unused*/, 0x00 /*=inputs*/, x,
                      0xFF /*=outputs*/));
        // PUT(0x00FF);  // pindirs: outputs
        // PUT(x);       // pins
        // PUT(0x0000);  // pindirs
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

#if FOR_COCO
  uint sdc_lsn;
  uint emu_disk_buffer;
  byte rtc_value;
#endif

  void HandleIOWrites(uint addr, byte data) {
    const bool reading = false;

    byte dev = addr & 0xFF;  // TODO -- handle f256 with 2 IO pages
    IOWriter writer = IOWriters[dev];
    if (writer) {
      // New style, pluggable, not all is converted yet:
      ///// data = (*reader)(addr, data);
      writer(addr, data);
    } else
      switch (255 & addr) {
#if FOR_COCO
        case 0x4B:
          sdc_lsn = (Peek(0xFF49) << 16) | (Peek(0xFF4A) << 8) | (0xFF & data);
          printf("SET SDC SECTOR sdc_lsn=%x\n", sdc_lsn);
          break;

        case 0x48:             // WD Floppy or CocoSDC command reg.
          if (data == 0xC0) {  // Special CocoSDC Command Mode
            if (Peek(0xFF40) == 0x43) {
              byte special_cmd = Peek(0xFF49);
              printf("- yak - Special CocoSDC Command Mode: %x %x %x\n",
                     special_cmd, Peek(0xFF4A), Peek(0xFF4B));
              switch (special_cmd) {
                case 'Q':
                  Poke(0xFF49, 0x00);
                  Poke(0xFF4A, 0x02);
                  Poke(0xFF4B, 0x10);  // Say size $000210 sectors.
                  break;
                case 'g':  // Set global flags.
                  // llcocosdc.0250230904: [Secondary command to "Set Global
                  // Flags"] [Disable Floppy Emulation capability in SDC
                  // controller] uses param $FF80
                  Poke(0xFF49, 0x00);
                  Poke(0xFF4A, 0x00);
                  Poke(0xFF4B, 0x00);
                  break;
                default:
                  Poke(0xFF49, 0x00);
                  Poke(0xFF4A, 0x00);
                  Poke(0xFF4B, 0x00);
                  break;
              }  // end switch special_cmd
            }  // if data

          } else if (0x84 <= data && data <= 0x87) {
            // Read Sector
            ReadDisk(data & 3, sdc_lsn, sdc_disk_read_data);
            sdc_disk_read_ptr = sdc_disk_read_data;
            sdc_disk_pending = data;
          }

          break;  // case 0x48

        case 0x00:
        case 0x01:
        case 0x02:
          ToLog::Logf("-PIA PIA0: %04x %c\n", addr, reading ? 'r' : 'w');
          break;
        case 0x03:
          // Read PIA0
          ToLog::Logf("-PIA PIA0: %04x %c\n", addr, reading ? 'r' : 'w');
          if (not reading) {
            vsync_irq_enabled = bool((data & 1) != 0);
          }
          break;

        case 0x90:  // GIME INIT0
          if (not reading) {
            gime_irq_enabled = bool((data & 0x20) != 0);
          }
          break;

        case 0x92:  // GIME IRQEN
          if (not reading) {
            gime_vsync_irq_enabled = bool((data & 0x08) != 0);
          }
          break;

        case 255 & (ACIA_PORT + 0):  // control port
          if (reading) {             // reading control port
            {
            }
          } else {  // writing control port:
            if ((data & 0x03) != 0) {
              acia_irq_enabled = false;
            }

            if ((data & 0x80) != 0) {
              acia_irq_enabled = true;
            } else {
              acia_irq_enabled = false;
            }
          }
          break;

        case 255 & (ACIA_PORT + 1):  // data port
          if (reading) {             // reading data port
          } else {                   // writing data port:
            putbyte(C_PUTCHAR);
            putbyte(data);
          }
          break;

        case 255 & (EMUDSK_PORT + 0):  // LSN(hi)
        case 255 & (EMUDSK_PORT + 1):  // LSN(mid)
        case 255 & (EMUDSK_PORT + 2):  // LSN(lo)
        case 255 & (EMUDSK_PORT + 4):  // buffer addr
        case 255 & (EMUDSK_PORT + 5):
        case 255 & (EMUDSK_PORT + 6):  // drive number
          ToLog::Logf("-EMUDSK %x %x %x\n", addr, data, Peek(addr));
          break;
        case 255 & (EMUDSK_PORT + 3):  // Run EMUDSK Command.
          if (!reading) {
            byte command = data;

            ToLog::Logf("-EMUDSK device %x sector $%02x.%04x bufffer $%04x diskop %x\n",
              Peek(EMUDSK_PORT + 6), Peek(EMUDSK_PORT + 0),
              Peek2(EMUDSK_PORT + 1), Peek2(EMUDSK_PORT + 4), command);

            uint lsn = Peek2(EMUDSK_PORT + 1);
            emu_disk_buffer = Peek2(EMUDSK_PORT + 4);
#if INCLUDED_DISK
            byte* dp = Disk + 256 * lsn;
#endif

            ToLog::Logf("-EMUDSK VARS sector $%04x buffer $%04x diskop %x\n", lsn,
              emu_disk_buffer, command);

            switch (command) {
              case 0:  // Disk Read
#if INCLUDED_DISK
                for (uint k = 0; k < 256; k++) {
                  Poke(emu_disk_buffer + k, dp[k]);
                }
#else
                putbyte(C_DISK_READ);
                putbyte(Peek(EMUDSK_PORT + 6));  // device
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
                      Poke(emu_disk_buffer + k, disk_input.Take());
                    }
                    data = 0;  // Ready
                    break;
                  }
                }
#endif
                break;

              case 1:  // Disk Write
#if INCLUDED_DISK
                for (uint k = 0; k < 256; k++) {
                  dp[k] = Peek(emu_disk_buffer + k);
                }
#else
                putbyte(C_DISK_WRITE);
                putbyte(Peek(EMUDSK_PORT + 6));  // device
                putbyte(lsn >> 16);
                putbyte(lsn >> 8);
                putbyte(lsn >> 0);
                for (uint k = 0; k < 256; k++) {
                  putbyte(Peek(emu_disk_buffer + k));
                }
                break;

              default:
                printf("\nwut emudsk command %d.\n", command);
                DumpRamAndGetStuck("wut emudsk", command);
#endif
            }
          }
          break;

        case 0xFF & (TFR_RTC_BASE + 1):
          switch (data) {
            case 0:
              rtc_value = 0;
              break;  // Sec
            case 1:
              rtc_value = 3;
              break;  // Sec (10)
            case 2:
              rtc_value = 5;
              break;  // Min
            case 3:
              rtc_value = 1;
              break;  // Min (10)

            case 4:
              rtc_value = 2;
              break;  // Hour
            case 5:
              rtc_value = 1;
              break;  // Hour (10)

            case 6:
              rtc_value = 5;
              break;  // Day of month
            case 7:
              rtc_value = 2;
              break;  // Day of month (10)

            case 8:
              rtc_value = 2;
              break;  // Month 1-12
            case 9:
              rtc_value = 1;
              break;  // Month (10)

            case 10:
              rtc_value = 4;
              break;  // Year - 1900
            case 11:
              rtc_value = 2;
              break;  // (10)
            case 12:
              rtc_value = 1;
              break;  // (100)

            default:
              rtc_value = data;
              break;
          }  // switch data
          break;
#endif  // FOR_COCO

      }  // switch addr & 255
  }  // HandleIOWrites

  uint data;
  uint num_resets;
  uint event;
  uint when;
  uint num_swi2s;
#ifdef TRACKING
  bool vma;      // Valid Memory Address ( = delayed AVMA )
  bool fic;      // First Instruction Cycle ( = delayed LIC )
  uint next_pc;  // for multibyte ops.

#endif

  IOReader IOReaders[256];
  IOWriter IOWriters[256];

#if FOR_COCO
  byte Reader43(uint addr, byte data) {
    if ((Peek(0xFF7F) & 3) == 3) {
      // Respond to MPI slot 3.
      // Return what was saved at 0x42?
      // I don't know why, just guessing, but it works,
      // for this line:
      // "llcocosdc.0250230904"+01e0   lda -5,x ; get value from Flash Ctrl Reg
      data = 0x60 & Peek(addr - 1);
    } else {
      data = 0;
    }
    return data;
  }
  byte Reader48(uint addr, byte data) {
    if (sdc_disk_pending) {
      data = 3;  // BUSY and READY
      sdc_disk_pending = 0;
    } else {
      data = 0;  // NOT BUSY
    }
    return data;
  }
  byte Reader4b(uint addr, byte data) {
    if (sdc_disk_read_ptr) {
      data = *sdc_disk_read_ptr++;
      if (sdc_disk_read_ptr == (sdc_disk_read_data + 256)) {
        sdc_disk_read_ptr = nullptr;
      }
    }
    return data;
  }
  byte ReaderAcia0(uint addr, byte data) {
    data = 0x02;  // Transmit buffer always considered empty.
    data |= (acia_irq_firing) ? 0x80 : 0x00;
    data |= (acia_char_in_ready) ? 0x01 : 0x00;

    acia_irq_firing = false;  // Side effect of reading status.
    return data;
  }

  byte ReaderAcia1(uint addr, byte data) {
    if (acia_char_in_ready) {
      data = acia_char;
      acia_char_in_ready = false;
    } else {
      data = 0;
    }
    return data;
  }
#endif  // FOR_COCO

  void ReaderInit() {
#if FOR_COCO
    IOReaders[0x43] = [this](uint addr, byte data) {
      return this->Reader43(addr, data);
    };
    IOReaders[0x48] = [this](uint addr, byte data) {
      return this->Reader48(addr, data);
    };
    IOReaders[0x4b] = [this](uint addr, byte data) {
      return this->Reader4b(addr, data);
    };
    IOReaders[(byte)(ACIA_PORT + 0)] = [this](uint addr, byte data) {
      return this->ReaderAcia0(addr, data);
    };
    IOReaders[(byte)(ACIA_PORT + 1)] = [this](uint addr, byte data) {
      return this->ReaderAcia1(addr, data);
    };
#endif  // FOR_COCO
  }

  void HandleIOReads(uint addr) {
    data = Peek(addr);  // default behavior

    byte dev = addr & 0xFF;
    // IOReader reader = IOReaders[dev];
    IOReader xxreader = IOReaders[dev];
    if (false) {
      // New style, pluggable, not all is converted yet:
      ///// data = (*reader)(addr, data);
      // data = (reader)(addr, data);
    } else if (xxreader) {
      // New style, pluggable, not all is converted yet:
      ///// data = (*reader)(addr, data);
      data = (xxreader)(addr, data);
    } else
      switch (dev) {
#if FOR_TURBO9SIM

#endif

#if FOR_COCO
          // yak
        case 0x42:  // CocoSDC Flash Data?
          // We need to emulate just enough of the Flash register behavior
          // so the initial scan thinks it has found a CocoSDC.
          // Cycles I see in the scan:
          // r FF42 <- $64
          // w FF43 -> 0
          // clr FF42
          // r FF43 -> should differ by XOR $60
          break;

        case 0x43:  // CocoSDC Flash Control
          assert(0);
          if ((Peek(0xFF7F) & 3) == 3) {
            // Respond to MPI slot 3.
            // Return what was saved at 0x42?
            // I don't know why, just guessing, but it works,
            // for this line:
            // "llcocosdc.0250230904"+01e0   lda -5,x ; get value from Flash
            // Ctrl Reg
            data = 0x60 & Peek(addr - 1);
          } else {
            data = 0;
          }
          break;

        case 0x48:  // Read SDC Status
          assert(0);
          if (sdc_disk_pending) {
            data = 3;  // BUSY and READY
            sdc_disk_pending = 0;
          } else {
            data = 0;  // NOT BUSY
          }
          break;

        case 0x4b:  // Read SDC Data
          assert(0);
          if (sdc_disk_read_ptr) {
            data = *sdc_disk_read_ptr++;
            if (sdc_disk_read_ptr == (sdc_disk_read_data + 256)) {
              sdc_disk_read_ptr = nullptr;
            }
          }
          break;

        // Read PIA0
        case 0x00:
          ToLog::Logf("-PIA PIA0 Read not Impl: %x\n", addr);
          data = 0xFF;  // say like, no key pressed
          break;
        case 0x01:
          ToLog::Logf("-PIA PIA0 Read not Impl: %x\n", addr);
          DumpRamAndGetStuck("pia0", addr);
          break;
        case 0x02:
          Poke(
              0xFF03,
              Peek(0xFF03) & 0x7F);  // Clear the bit indicating VSYNC occurred.
          vsync_irq_firing = false;
          data = 0xFF;
          break;
        case 0x03:
          // OK to read, for the HIGH bit, which tells if VSYNC ocurred.
          break;

        case 0x92:  // GIME IRQEN register
        {
          if (gime_irq_enabled && gime_vsync_irq_enabled &&
              gime_vsync_irq_firing) {
            data = 0x08;
            gime_vsync_irq_firing =
                false;  // Reading this register clears the IRQ.
          } else {
            data = 0;
          }
        } break;

        case 255 & (ACIA_PORT + 0):  // read ACIA control/status port
          assert(0);
          {
            data = 0x02;  // Transmit buffer always considered empty.
            data |= (acia_irq_firing) ? 0x80 : 0x00;
            data |= (acia_char_in_ready) ? 0x01 : 0x00;

            acia_irq_firing = false;  // Side effect of reading status.
          }
          break;

        case 255 & (ACIA_PORT + 1):  // read ACIA data port
          assert(0);
          if (acia_char_in_ready) {
            data = acia_char;
            acia_char_in_ready = false;
          } else {
            data = 0;
          }
          break;

        case 0xFF & (BLOCK_PORT + 7):  // read disk status
          // TODO
          break;

        case 0xFF & (EMUDSK_PORT + 3):  // read disk status
          data = 0;                     // always a good status.
          break;

        case 0xFF & (TFR_RTC_BASE + 0):
          data = rtc_value;
          break;
#endif  // FOR_COCO

        default:
          break;
      }  // switch

    PUT(QUAD_JOIN(0xAA /*=unused*/, 0x00 /*=inputs*/, data, 0xFF /*=outputs*/));

    // PUT(0x00FF);  // pindirs: outputs
    // PUT(data);    // pins
    // PUT(0x0000);  // pindirs
  }

  void Run() override {
    the_ram.Reset();
#if OS_LEVEL == 200
    printf("COCO3: Prepare for Level 2\n");
    // Poke(0x5E, 0x39); // RTS for D.BtBug // TODO
    Poke(0x5E, 0x7E);    // RTS for D.BtBug // TODO
    Poke(0x5F, 0xFF);    // RTS for D.BtBug // TODO
    Poke(0x60, 0xEC);    // RTS for D.BtBug // TODO
    Poke(0xFFEC, 0x39);  // RTS for D.BtBug // TODO
#endif

    ShowChar('X');
    printf("stage-X\n");
    InitRamFromRom();
    ShowChar('Y');
    printf("stage-Y\n");
    ResetCpu();
    ShowChar('Z');
    printf("stage-Z\n");

#if FOR_COCO
    // Set interrupt vectors
    for (uint j = 0; j < 8; j++) {
      Poke2(0xFFF0 + j + j, FFFxVectors[j]);
    }
#endif

    ShowChar('+');
    LED(0);
    printf("\nStartPio()\n");
    ShowChar('P');
    StartPio();
    ShowChar('I');
    printf("\nEND StartPio()\n");
    ShowChar('O');

#if PICO_USE_TIMER
    // thanks https://forums.raspberrypi.com/viewtopic.php?t=349809
    alarm_pool_init_default();

    add_repeating_timer_us(16666 /* 60 Hz */, TimerCallback, nullptr,
                           &TimerData);
#endif

    ShowChar('\n');
    auto e = this;
    e->ReaderInit();
    e->RunMachineCycles();

    sleep_ms(100);
    printf("\nEngine Finished.\n");
    sleep_ms(100);
    GET_STUCK();

  }  // end Run

  bool PeekDiskInput() {
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

  void ReadDisk(uint device, uint lsn, byte* buffer) {
#if INCLUDED_DISK
    for (uint k = 0; k < 256; k++) {
      Poke(buffer + k, TODO dp[k]);
    }
#else
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
          buffer[k] = disk_input.Take();
        }
        break;
      }
    }
#endif
  }

  void RunMachineCycles() {
    uint cy = 0;  // This is faster if local.

    // TOP
    const PIO pio = pio0;
    constexpr uint sm = 0;

    ShowStr("* PreRoll\n");
    printf("* PreRoll\n");
    PreRoll();

    const byte value_FFFF = Peek(0xFFFF);
    printf("value_FFFF = %x\n", value_FFFF);
    const uint value_FFFF_shift_8_plus_FF = (value_FFFF << 8) + 0xFF;

    bool prev_irq_needed = false;

    ShowStr("============================\n");
    printf("============================\n");

    while (true) { ///////////////////////////////// Outer Machine Loop
#if IRQS
      bool irq_needed = false;
#if FOR_TURBO9SIM
      irq_needed |= ToTurbo9sim::IrqNeeded();  // either Timer or RX
#endif
#if FOR_COCO
      irq_needed |=
          (vsync_irq_enabled && vsync_irq_firing) ||
          (acia_irq_enabled && acia_irq_firing) ||
          (gime_irq_enabled && gime_vsync_irq_enabled && gime_vsync_irq_firing);
#endif  // FOR_COCO
      if (SHOW_IRQS)
        if (vsync_irq_enabled && vsync_irq_firing) ShowChar('H');
      if (SHOW_IRQS)
        if (acia_irq_enabled && acia_irq_firing) ShowChar('A');
      if (SHOW_IRQS)
        if (gime_irq_enabled && gime_vsync_irq_enabled && gime_vsync_irq_firing)
          ShowChar('G');

      if (irq_needed != prev_irq_needed) {
        bool ok = NeoIrq(irq_needed);
        if (ok) {
          prev_irq_needed = irq_needed;
          LED(irq_needed);
        }
      }
#endif  // IRQS

      PollUsbInput();

#if FOR_TURBO9SIM
      if (ToTurbo9sim::CanRx()) {
        if (term_input.HasAtLeast(1)) {
          byte ch = term_input.Take();
          ToTurbo9sim::SetRx(ch);
        }
      }
#endif
#if FOR_COCO
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
#endif

#if !PICO_USE_TIMER
      // Simulate timer firing every so-many cycles,
      // but not with realtime timer,
      // because we are running slowly.
      if ((cy & VSYNC_TICK_MASK) == 0) TimerFired = true;
#endif

      if (TimerFired) {
        TimerFired = false;

#if FOR_TURBO9SIM
        ToTurbo9sim::SetTimerFired();
#endif
#if FOR_COCO
        Poke(0xFF03,
             Peek(0xFF03) | 0x80);  // Set the bit indicating VSYNC occurred.
        if (vsync_irq_enabled) {
          vsync_irq_firing = true;
        }
        if (gime_irq_enabled && gime_vsync_irq_enabled) {
          gime_vsync_irq_firing = true;
        }
#endif
      }  // end Timer

      for (uint loop = 0; loop < RAPID_BURST_CYCLES; loop++) { /////// Inner Machine Loop
#if TRACKING
#if TRACKING_CYCLE
        interest = (cy > TRACKING_CYCLE) ? 99999 : 0;
#else
        interest = 99999;
#endif
#endif

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
            PUT((FastPeek(addr) << 8) + 0xFF);
#if TRACKING || OPCODES || HEURISTICS
            data = FastPeek(addr);
#endif
          } else {
            const uint data_and_more = WAIT_GET();
            FastPoke(addr, (byte)data_and_more);
#if TRACKING || OPCODES || HEURISTICS
            data = (byte)data_and_more;
#endif
          }  // end if reading

          // =============================================================
        } else if (addr == 0xFFFF) {
          if (reading) {
            PUT(value_FFFF_shift_8_plus_FF);
#if TRACKING || OPCODES || HEURISTICS
            data = value_FFFF;
#endif
          } else {
            const byte foo = WAIT_GET();
            (void)foo;
#if TRACKING || OPCODES || HEURISTICS
            data = foo;
#endif
          }

          // =============================================================
        } else if (addr < 0xFF00) {
          if (reading) {  // if reading FExx
            PUT((Peek(addr) << 8) + 0xFF);
#if TRACKING || OPCODES || HEURISTICS
            data = Peek(addr);
#endif
          } else {  // if writing FExx
            const byte foo = WAIT_GET();
            Poke(addr, foo, 0x3F);
#if TRACKING || OPCODES || HEURISTICS
            data = foo;
#endif
          }  // end if reading

          // =============================================================
        } else {
          if (reading) {  // CPU reads, Pico Tx
            HandleIOReads(addr);
          } else {
            // CPU writes, Pico Rx
            data = WAIT_GET();
            Poke(addr, data, 0x3F);
            HandleIOWrites(addr, data);
          }  // end if read / write
        }  // addr type

        // =============================================================
        // =============================================================

#if OPCODES
        if (reading and addr != 0xFFFF and fic) {
          // ShowChar('1');
          current_opcode_cy = cy;
          current_opcode_pc = addr;
          current_opcode = data;
          // printf("1<%d,%d,%d>\n", cy, addr, data);

#if HEURISTICS
          if (addr < 0x0010) {
            DumpRamAndGetStuck("PC too low", addr);
          }
          if (addr > 0xFF00 && addr < 0xFFF0) {
            // fic can be asserted during interrupt vector fetch.
            DumpRamAndGetStuck("PC too high", addr);
          }

#endif
        }

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
#if HEURISTICS
          if (current_opcode == 0x20 /*BRA*/ && current_opcode_cy + 1 == cy) {
            if (data == 0xFE) {
              DumpRamAndGetStuck("Infinite BRA loop", addr);
            }
          }
#endif
#if TRACE_RTI
          if (current_opcode == 0x3B) {  // RTI
            uint age = cy - current_opcode_cy -
                       2 /*one byte opcode, one extra cycle */;
            if (0)
              printf("~RTI~R<%d,ccy=%d,cpc=%d,cop=%x,a=%d> %04x:%02x\n", cy,
                     current_opcode_cy, current_opcode_pc, current_opcode, age,
                     addr, data);
            interest += 50;
            // TODO -- recognize E==0 for FIRQ
            if (age < RTI_SZ) {
              hist_data[age] = data;
              hist_addr[age] = addr;
              if (age == RTI_SZ - 1) {
                SendEventHist(EVENT_RTI, RTI_SZ);
              }
            }
          }  // end RTI
          if (current_opcode == 0x103F) {  // SWI2/OS9
            uint age =
                cy - current_opcode_cy -
                2 /*two byte opcode.  extra cycle contains OS9 call number. */;
            if (0)
              printf("~OS9~R<%d,ccy=%d,cpc=%d,cop=%x,a=%d> %04x:%02x\n", cy,
                     current_opcode_cy, current_opcode_pc, current_opcode, age,
                     addr, data);
            interest += 50;
            if (age < SWI2_SZ) {
              hist_data[age] = data;
              hist_addr[age] = addr;
              if (age == SWI2_SZ - 1) {
                SendEventHist(EVENT_SWI2, SWI2_SZ);
              }
            }
          }
#endif
        }
        if (!reading) {
#if TRACE_RTI
          if (current_opcode == 0x103F) {  // SWI2/OS9
            uint age = cy - current_opcode_cy - 2 /*two byte opcode*/;
            if (0)
              printf("~OS9~W<%d,ccy=%d,cpc=%d,cop=%x,a=%d> %04x:%02x\n", cy,
                     current_opcode_cy, current_opcode_pc, current_opcode, age,
                     addr, data);
            interest += 50;
            if (age < SWI2_SZ) {
              hist_data[age] = data;
              hist_addr[age] = addr;
            }
          }
#endif
        }
#endif  // OPCODES

        uint high = flags & F_HIGH;

#if HEURISTICS
        if (loop < 4) {
          if (0)
            printf("$%d: #%d$ %04x %02x %02x $$ [ #%d. pc=%04x op=%02x ]\n",
                   loop, cy, addr, (high >> 8), (255 & data), current_opcode_cy,
                   current_opcode_pc, current_opcode);
        }
#endif

#if TRACKING
        if (reading and (not vma) and (addr == 0xFFFF)) {
          ToLog::Logf("- ---- --  =%s #%d\n", HighFlags(high), cy);
        } else {
          const char* label = reading ? (vma ? "r" : "-") : "w";
          if (reading) {
            if (fic) {
              label = "@";
#if SEEN
              if (not Seen[addr]) {
                label = "@@";
              }
#endif
              next_pc = addr + 1;
            } else {
              // case: Reading but not FIC
              if (next_pc == addr) {
                label = "&";
                next_pc++;
              }

            }  // end case Reading but not FIC

          }  // end if reading
          ToLog::Logf("%s %04x %02x  =%s #%d\n", label, addr, 0xFF & data,
            HighFlags(high), cy);
        }  // end if valid cycle

#if SEEN
        if (fic) {
          Seen.Insert(addr);
        }
#endif
#endif  // TRACKING

#if OPCODES
        vma = (0 != (flags & F_AVMA));
        fic = (0 != (flags & F_LIC));
#endif

#if STOP_CYCLE
        if (cy >= STOP_CYCLE) {
          printf("=== STOPPING BECAUSE CYCLE %d REACHED MAXIMIUM\n", cy);
          goto bottom;
        }
#endif
        cy++;
      }  // next loop

    }  // while true

  bottom: {}
  }  // end RunMachineCycles
};  // end struct Engine

void InitialBanners() {
  for (uint i = BLINKS; i > 0; i--) {
    LED(1);
    sleep_ms(500);
    ShowChar(' ');
    ShowChar('0' + i);
    ShowChar('!');
    printf(" %d!\n", i);
    LED(0);
    sleep_ms(500);
  }

  ShowStr("\nAbout: https://github.com/strickyak/tfr9 <strick@yak.net>");
  memset(Buf64, 0, sizeof Buf64);
  pico_get_unique_board_id_string(Buf64, sizeof Buf64);
  ShowStr("\nBoard-ID: ");
  ShowStr(Buf64);
#ifdef PICO_BOARD
  ShowStr("\nBoard-Type: " PICO_BOARD);
#endif
#ifdef BANNER
  ShowStr("\nFirmware: " BANNER);
#endif
#ifdef PICO_PROGRAM_NAME
  ShowStr("\nProgram-Name: " PICO_PROGRAM_NAME);
#endif
#ifdef PICO_PROGRAM_BUILD_DATE
  ShowStr("\nBuild-Date: " PICO_PROGRAM_BUILD_DATE);
#endif
#ifdef __DATE__
  ShowStr("\nBuild-DATE: " __DATE__);
#endif
  ShowStr("\n");
  printf("OS_LEVEL=%d\n", OS_LEVEL);
}

// std::vector<EngineBase*> engines;
// std::vector<EngineBase*> fast_engines;
struct harness {
    EngineBase* engines[5];
    EngineBase* fast_engines[5];

    Engine <DoLog, SmallRam<DoLogMmu, DoTracePokes>, Turbo9sim> t9slow;
    Engine <DontLog, SmallRam<DontLogMmu, DontTracePokes>, Turbo9sim> t9fast;

    Engine<DoLog, SmallRam<DoLogMmu, DoTracePokes>, NoTurbo9sim> l1slow;
    Engine<DontLog, SmallRam<DontLogMmu, DontTracePokes>, NoTurbo9sim> l1fast;

    Engine<DoLog, BigRam<DoLogMmu, DoTracePokes>, NoTurbo9sim> l2slow;
    Engine<DontLog, BigRam<DontLogMmu, DontTracePokes>, NoTurbo9sim> l2fast;

    explicit harness() {
        t9slow.Install(PRIMARY_TURBO9SIM, t9slow.IOReaders, t9slow.IOWriters);
        t9slow.Install(SECONDARY_TURBO9SIM, t9slow.IOReaders, t9slow.IOWriters);
        
        t9fast.Install(PRIMARY_TURBO9SIM, t9fast.IOReaders, t9fast.IOWriters);
        t9fast.Install(SECONDARY_TURBO9SIM, t9fast.IOReaders, t9fast.IOWriters);

        engines[0] = &t9slow;
        engines[1] = &l1slow;
        engines[2] = &l2slow;
        fast_engines[0] = &t9fast;
        fast_engines[1] = &l1fast;
        fast_engines[2] = &l2fast;
    }
} Harness;

void CreateEngines() {
#if 0
        ShowChar('a');
    auto* t9_engine = new Engine<DoLog, SmallRam<DoLogMmu, DoTracePokes>, Turbo9sim>();
  t9_engine->Install(PRIMARY_TURBO9SIM, t9_engine->IOReaders, t9_engine->IOWriters);
  t9_engine->Install(SECONDARY_TURBO9SIM, t9_engine->IOReaders, t9_engine->IOWriters);

        ShowChar('a');
    auto* fast_t9_engine = new Engine<DoLog, SmallRam<DoLogMmu, DoTracePokes>, Turbo9sim>();
  t9_engine->Install(PRIMARY_TURBO9SIM, t9_engine->IOReaders, t9_engine->IOWriters);
        ShowChar('a');
  t9_engine->Install(SECONDARY_TURBO9SIM, t9_engine->IOReaders, t9_engine->IOWriters);
        ShowChar('a');

    engines[0] = t9_engine;
    fast_engines[0] = fast_t9_engine;


        ShowChar('b');
    auto* l1_engine = new Engine<DoLog, SmallRam<DoLogMmu, DoTracePokes>, NoTurbo9sim>();
    auto* fast_l1_engine = new Engine<DontLog, SmallRam<DoLogMmu, DoTracePokes>, NoTurbo9sim>();

        ShowChar('b');
    engines[1] = l1_engine;
        ShowChar('b');
    fast_engines[1] = fast_l1_engine;
        ShowChar('b');

        ShowChar('c');
    auto* l2_engine = new Engine<DoLog, BigRam<DontLogMmu, DontTracePokes>, NoTurbo9sim>();
        ShowChar('c');
    auto* fast_l2_engine = new Engine<DontLog, BigRam<DontLogMmu, DontTracePokes>, NoTurbo9sim>();
        ShowChar('c');

    engines[2] = l2_engine;
    fast_engines[2] = fast_l2_engine;

        ShowChar('q');
        ShowChar('\n');

  // t9_engine->Run();
  // l1_engine->Run();
  // l2_engine->Run();

 // fast_t9_engine->Run();
 // fast_l1_engine->Run();
 // fast_l2_engine->Run();
 #endif
}

void Shell() {
    ShowChar('r');
    while (true) {
        ShowChar('^');
        PollUsbInput();

        if (term_input.HasAtLeast(1)) {
          byte ch = term_input.Take();
          if ('0' <= ch && ch <= '4') {
            uint num = ch - '0';
            if (Harness.engines[num]) {
                Harness.engines[num]->Run();
            } else {
                ShowChar('?');
            }

          } else if ('5' <= ch && ch <= '9') {
            uint num = ch - '5';
            if (Harness.fast_engines[num]) {
                Harness.fast_engines[num]->Run();
            } else {
                ShowChar('?');
            }
            
          } else {
                ShowChar('?');
          }
        } // term_input
        sleep_ms(500);
    }
    // Shell never returns.
}

int main() {
  // set_sys_clock_khz(250000, true);
  // set_sys_clock_khz(250000, true); // 0.559099
  // set_sys_clock_khz(260000, true); // 0.516071  0.531793
  // set_sys_clock_khz(270000, true); // NO? YES.
  // up to 270(0.509053, 0.517735) with divisor 3.
  stdio_usb_init();

  gpio_init(25);
  gpio_set_dir(25, GPIO_OUT);

  InitializePinsForGpio();

  interest = MAX_INTEREST;  /// XXX

  quiet_ram = 0;

  InitialBanners();

  CreateEngines();
  Shell();
}
