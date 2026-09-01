#define MHz 250  // 250

#define ALWAYS_TRACE_READS_IF_ADDR_GE 0xFF00
#define FIFO_INDICATOR_0500 1
#define RPC_VERBOSE 0
#define FLOPPY_OVER_VFS 1

#define BUG_SPLASH_MILLIS 400
#define AUTO_GLOB 1
#define DEFANG 1
#define USE_PMODE4 1
#define INVERSE_PMODE 1
#define GREEN_PMODE 0

#define ON_RESET_DO_SPOONFEED_CONSOLE 1
#define GSPOON_POC_DEMO 0
#define ECHO_PUTCHAR_ON_CONSOLE 1
#define USE_ORCHESTRA90 1
#define STACK_SIZE   (20 * 1024) // was 10K

// #define TRIGGER_ON_READ     0xCA71
// #define TRIGGER_ON_WRITE 0xFE7F

#define PICO_LFS_TRACE_CHAR 1 // uses `char centipede_trace_char(char c);`

// #define CENTIPEDE_REV 3204 // 32d
// #define CENTIPEDE_REV 3205 // 32e
#define CENTIPEDE_REV 3226  // 32z

#define DBUS_HOLD_CYCLES 0

// --- Tuning constants for cycle logging and flow control ---
#define COMPRESSION_MAX 100        // Cycles per compressed packet
#define FG2BG_HIGH_WATERMARK 1000  // Assert HALT when FIFO exceeds this
#define FG2BG_LOW_WATERMARK 500    // Release HALT when FIFO drains below this

#define CENTIPEDE_INVERT_EQ 1

#define IN_FLASH __in_flash("FLASH")
#define IN_RAM __not_in_flash("centipede")

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define FORCE_INLINE inline __attribute__((always_inline))

#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/regs/pads_qspi.h>
#include <hardware/structs/qmi.h>
#include <hardware/sync.h>
#include <pico/multicore.h>
#include <pico/platform.h>
#include <pico/stdlib.h>
#include <pico/time.h>

#include "pico/rand.h"

extern "C" {
#include <arm_acle.h>
#include <cmsis_gcc.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

#include "littlefs/lfs-centipede.h"
#include "littlefs/lfs.h"
#include "littlefs/lfs_util.h"
#include "config.h"

int _getentropy(void* buffer, size_t length) {
  char* ptr = (char*)buffer;
  while (length >= 4) {
    uint32_t r = get_rand_32();
    memcpy(ptr, &r, 4);
    ptr += 4;
    length -= 4;
  }
  if (length > 0) {
    uint32_t r = get_rand_32();
    memcpy(ptr, &r, length);
  }
  return 0;
}
int getentropy(void* buffer, size_t length) {
  return _getentropy(buffer, length);
}
}

#include <cstring>
#include <functional>
#include <atomic>

#include "tcl6.7c/tcl.h"

Tcl_Interp* global_tcl_interp = nullptr;

const char HexAlphabet[] =
    "0123456789ABCDEFXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
    "XXXXXXX";

#define G_RW 20
#define G_E 21
#define G_Q 22

#if CENTIPEDE_REV == 3205  // 32e

#define G_LED 25
#define G_SCS 26
#define G_CART 27
#define G_SLENB 28
#define G_HALT 29
#define G_NMI 30
#define G_CTS 31

#elif CENTIPEDE_REV == 3204  // 32d

#define G_CTS 18  // bodged
#define G_SCS 19  // bodged

#define G_LED 25
#define G_SND 26
#define G_CART 27
#define G_SLENB 28
#define G_HALT 29
#define G_NMI 30
#define G_RESET 31

#elif CENTIPEDE_REV == 3226  // 32z

#define G_CTS 8
#define G_SCS 9

#define G_LED 25
#define G_SND 26
#define G_CART 27
#define G_SLENB 28
#define G_HALT 29
#define G_NMI 30
#define G_RESET 31

#else  // Original Centipede experiment (all wire-wrapped)

#define G_LED 25
#define G_NMI 26
#define G_RESET 27
#define G_HALT 28
#define G_SLENB 29

#endif

#define G_D0 0
#define G_A0 32

#include <array>
#include <atomic>
#include <cstdint>

using byte = unsigned char;
using addr16 = uint16_t;

#define SET_LED(X) gpio_put(G_LED, (X))
#define volatile_sio_hw ((volatile sio_hw_t*)SIO_BASE)

void INPUT(int i) {
  gpio_init(i);
  gpio_set_dir(i, GPIO_IN);
  gpio_set_pulls(i, false, false);
}
void OUTPUT(int i, int x) {
  gpio_init(i);
  gpio_set_dir(i, GPIO_OUT);
  gpio_put(i, x);
}

#if 0
void ResetOn() {
  gpio_set_dir(G_RESET, GPIO_OUT);
}
void ResetOff() {
  gpio_set_dir(G_RESET, GPIO_IN);
}
#endif

void HaltOn() {
  gpio_set_dir(G_HALT, GPIO_OUT);
  // SET_LED(1);
}
void HaltOff() {
  // SET_LED(0);
  gpio_set_dir(G_HALT, GPIO_IN);
}

#define BOOT_MODE_CHECKER 0x56781234u
uint32_t  __uninitialized_ram(boot_mode);
uint32_t  __uninitialized_ram(boot_mode_check);
std::atomic<bool> startup_e_clock_detected{false};

// Detect whether the CoCo2's E clock is running by sampling GPIO.
// Returns true only if we see several hundred high AND low samples,
// confirming a real oscillating clock (not a floating/noisy pin).
bool IN_RAM detect_e_clock() {
  uint count_high = 0, count_low = 0, transitions = 0;
  bool last_state = gpio_get(G_E);
  for (uint i = 0; i < 10000; i++) {
    bool current_state = gpio_get(G_E);
    if (current_state)
      count_high++;
    else
      count_low++;
      
    if (current_state != last_state) {
      transitions++;
      last_state = current_state;
    }
  }
  // Require at least 200 samples of each state and 10 transitions to confirm a clock.
  // At 0.9 MHz E clock and ~250 MHz CPU, we expect ~5000 of each and many transitions.
  bool ok = count_high > 200 && count_low > 200 && transitions >= 10;
  if (ok) startup_e_clock_detected = true;
  return ok;
}

#include "cross-core.h"
#include "usb_pipeline.h"

CircBuf<unsigned char, 1024> usb_raw_buf;
CircBuf<std::string*, 64> usb_packet_buf;

UsbReceiver usb_receiver(usb_raw_buf);
CobsDecoder<1024, 64> cobs_decoder(usb_raw_buf, usb_packet_buf);

CrossCoreFIFO<uint, 8192> fg2bg;
CrossCoreFIFO<uint, 8192> bg2fg;

// Foreground flow control: smooth HALT-based throttling to prevent fg2bg
// overflow. High watermark: assert HALT when FIFO exceeds this (stop pushing,
// slow CPU). Low watermark: release HALT when FIFO drains below this (resume
// pushing).
// FG2BG_HIGH_WATERMARK and FG2BG_LOW_WATERMARK defined at top of file.
inline volatile bool fg_halt_for_flow_control = false;

#if FIFO_INDICATOR_0500
// Pointer to ram[] for FIFO pressure indicators in FlowControlCheck.
// Set to ram[] before the foreground bus loop starts.
byte* fifo_indicator_ram = nullptr;
#endif

// Called every bus cycle in the foreground to manage flow control.
FORCE_INLINE void IN_RAM FlowControlCheck() {
  uint sz = fg2bg.size();

#if FIFO_INDICATOR_0500
  // Visual FIFO pressure indicators — poke 0xFF into VDG screen RAM.
  // These are visible as bright blocks on the CoCo2 display.
  //   $0500 = 80% full (6554 of 8192)
  //   $0502 = 99% full (8110 of 8192)
  //   $0504 = 100% full (push failed) — set by PUSH_TO_BG on failure
  if (fifo_indicator_ram) {
    if (sz > (8192 * 80 / 100)) fifo_indicator_ram[0x0500] = 0xFF;
    if (sz > (8192 * 99 / 100)) fifo_indicator_ram[0x0502] = 0xFF;
  }
#endif

  if (fg_halt_for_flow_control) {
    if (sz < FG2BG_LOW_WATERMARK) {
      HaltOff();
      fg_halt_for_flow_control = false;
    }
  } else {
    if (sz > FG2BG_HIGH_WATERMARK) {
      HaltOn();
      fg_halt_for_flow_control = true;
    }
  }
}


#if FIFO_INDICATOR_0500
// push_fail_counter: incremented on the foreground when fg2bg.push() fails.
volatile uint32_t push_fail_counter = 0;
#endif

#define SAY(C) PUSH_TO_BG(FG2BG_PUTCHAR, 0, (C) & 255)
#if FIFO_INDICATOR_0500
#define PUSH_TO_BG(T, A, D) do { \
    if (!fg2bg.push(((T) << 24) | ((A) << 8) | (D))) { \
      push_fail_counter++; \
      if (fifo_indicator_ram) fifo_indicator_ram[0x0504] = 0xFF; \
    } \
  } while(0)
#else
#define PUSH_TO_BG(T, A, D) fg2bg.push(((T) << 24) | ((A) << 8) | (D))
#endif

#define INCLUDING
#include "cobs_tx.h"

#include "bug.h"
#include "disk11_rom.h"  // byte disk11_rom[8192]...
#include "egg.h"

// Called from littlefs to indicate disk read/writes on tether console,
// but only if PICO_LFS_TRACE_CHAR
extern "C" void centipede_trace_char(char c) {
  cobs_putchar(c);
}

using IOReader = byte (*)(uint addr);
using IOWriter = void (*)(uint addr, byte data);

IOReader IOReaders[256];
IOWriter IOWriters[256];

byte ram[64 * 1024];

// Code to tethered PC.
//
// Length is explicit:
#define C_LOGGING 130
#define C_DISK_READ 173
#define C_DISK_WRITE 174
#define C_COMPRESSED_CYCLES 175
//
// Length is implicit:
#define C_PUTCHAR 193     // 0xC1
#define C_RAM2_WRITE 195  // 0xC3
#define C_RAM2_READ 211   // 0xD3
#define C_CYCLE_RD3 211   // 0xD3

// Commands into the FIFO to the slow core

enum FifoNumbers {
  FG2BG_PUTCHAR,         // 0
  FG2BG_READ,            // 1
  FG2BG_SPOON_ON_RESET,  // 2
  FG2BG_WRITE,           // 3
  FG2BG_SYNC_NEEDED,     // a boundary, not an event.
  UNUSED__FG2BG_NMI,     // now handled entirely in foreground.
  FG2BG_FLOPPY_COMMAND,
  FG2BG_FLOPPY_LATCH,
  FG2BG_W_256,
  FG2BG_PEEK_REPLY,
  FG2BG_START_KEYBOARD_INJECTOR,
};

enum Bg2FgNumbers {
  BG2FG_PEEK = 1,
  BG2FG_POKE = 2,
  BG2FG_EXIT_CONSOLE = 3,
};

void IN_RAM fg_say(char c) {
    SAY(c);
}
void IN_RAM fg_say_hex(byte b) {
    const char* hex = "0123456789ABCDEF";
    SAY(hex[b >> 4]);
    SAY(hex[b & 15]);
}

// {
#define COMPRESS_CYCLES 1  // Seems safe by now.
#if COMPRESS_CYCLES

// ResetCompressCycles();          // call once at session start
// uint n = CompressCycles(buf, words, len, pred);  // call per block, state
// persists
#include "compress.h"

// COMPRESSION_MAX defined at top of file.
byte compression_buffer[5 * COMPRESSION_MAX];
uint32_t cycle_buffer[COMPRESSION_MAX];
uint cycle_i;

// Diagnostic counters for debugging lost write cycle records.
// write_counter: incremented on the background for each FG2BG_WRITE processed.
volatile uint32_t write_counter = 0;

bool IsRomPredicateForCompression(addr16 addr) {
  return 0x8000 <= addr && addr < 0xFF00;
}

FORCE_INLINE void SendSizePrefix(uint sz) {
  if (!usb_tether_ok()) return;
  if (sz >= 64) {
    unsigned char pkt[2] = {(unsigned char)(0xC0 + (sz >> 6)),
                            (unsigned char)(0x80 + (sz & 63))};
    CobsEncodeAndTransmit(pkt, 2, putchar_raw);
  } else {
    unsigned char pkt[1] = {(unsigned char)(0x80 + sz)};
    CobsEncodeAndTransmit(pkt, 1, putchar_raw);
  }
}

#define ASSERT_HALT() gpio_set_dir(G_HALT, GPIO_OUT)
#define RELEASE_HALT() gpio_set_dir(G_HALT, GPIO_IN)

// NMI is edge-triggered on the 6809. Assert it on the foreground and set
// a volatile flag. nmi_fuse, if set, gets decremented at the end of
// the foreground cycle, and NMI is released when the fuse hits zero.
volatile int nmi_fuse;
#define ASSERT_NMI() do { \
    gpio_set_dir(G_NMI, GPIO_OUT); \
    nmi_fuse = 2; \
  } while(0)
#define RELEASE_NMI() gpio_set_dir(G_NMI, GPIO_IN)

#define GERBIL_GET() gerbil_program_get_word(pio, sm)
#define GERBIL_DRIVE(X) gerbil_program_put_word(pio, sm, 0x100 | (X))
#define GERBIL_PASS() gerbil_program_put_word(pio, sm, 0)

#include "console.h"
#include "keyboard_injector.h"
#include "coro.h"
#include "flash-label.h"
#include "floppy.h"
#include "gerbil.pio.h"
#include "rtc.h"

// Declared here (before gspoon.h) so both gspoon.h functions
// and foreground tasks can access it.
volatile bool spoon_has_work = false;

#include "gspoon.h"
#include "tcl_io.h"
#include "vfs.h"

void IN_RAM SendCycleBuffer(uint count) {
  if (count == 0) return;
  if (usb_tether_ok()) {
    uint n = CompressCycles(compression_buffer, cycle_buffer, count,
                            IsRomPredicateForCompression);
    // Packet: [cmd, numCycles, write_counter_lsb, push_fail_lsb, compressed...]
    unsigned char pkt[5 * COMPRESSION_MAX + 4];
    pkt[0] = C_COMPRESSED_CYCLES;
    pkt[1] = (unsigned char)count;
    pkt[2] = (unsigned char)(write_counter & 0xFF);
#if FIFO_INDICATOR_0500
    pkt[3] = (unsigned char)(push_fail_counter & 0xFF);
#else
    pkt[3] = 0;
#endif
    for (uint i = 0; i < n; i++) {
      pkt[i + 4] = compression_buffer[i];
    }
    CobsEncodeAndTransmit(pkt, n + 4, putchar_raw);
  }
}

void IN_RAM InsertCycleWithCompression(uint32_t chore) {
  cycle_buffer[cycle_i] = chore;
  cycle_i++;
  if (cycle_i == COMPRESSION_MAX) {
    SendCycleBuffer(cycle_i);
    cycle_i = 0;
  }
}

// Flush any partial batch of cycles that haven't been sent yet.
void IN_RAM FlushPartialCycleBuffer() {
  if (cycle_i > 0) {
    SendCycleBuffer(cycle_i);
    cycle_i = 0;
  }
}

#endif  // COMPRESS_CYCLES
// }

uint trigger;
volatile uint idling;

FORCE_INLINE bool inline_volatile_gpio_get(uint pin) {
#if NUM_BANK0_GPIOS <= 32
  return volatile_sio_hw->gpio_in & (1u << pin);
#else
  if (pin < 32) {
    return volatile_sio_hw->gpio_in & (1u << pin);
  } else {
    return volatile_sio_hw->gpio_hi_in & (1u << (pin - 32));
  }
#endif
}

FORCE_INLINE void force_inline_multicore_fifo_push_blocking(uint32_t data) {
  // We wait for the fifo to have some space
  while (!multicore_fifo_wready()) tight_loop_contents();

  sio_hw->fifo_wr = data;

  // Fire off an event to the other core
  __sev();
}

bool MmuEnabled;
byte MmuTask;
bool StickyRamFFEx;
byte MmuMap[2][8];

bool SamP1Bit;
bool SamTyBit;

#include "coco64k.h"
#include "littlefs.h"
#include "orchestra90.h"
#include "tcl_commands.h"
#include "pico_rpc.h"

/////////////////////////////////////////////////////////////

#ifdef AUTO_TYPE
const char auto_type[] = "10 REM Hello World";
uint auto_i;
uint auto_skip = 100;
uint auto_hold;
byte auto_value;

const char normal_keyboard[] =
    "@ABCDEFGHIJKLMNOPQRSTUVWXYZ~~~~ 0123456789:;,-./\n";
const char shift_keyboard[] =
    "@abcdefghijklmnopqrstuvwxyz~~~~ ~!\"#$%'()*+<=>?\n";

constexpr int SHIFTED = 0x100;

int find_keycode(char c) {
  for (uint i = 0; normal_keyboard[i]; i++) {
    if (normal_keyboard[i] == c) {
      return i;
    }
  }
  for (uint i = 0; shift_keyboard[i]; i++) {
    if (shift_keyboard[i] == c) {
      return i + SHIFTED;
    }
  }
  return -1;
}

byte keyboard_response(char c) {
  int code = find_keycode(c);
  if (code < 0) return 0xFF;

  byte col = code & 7;
  byte row = (code >> 3) & 7;

  byte probe = ram[0xFF02];
  byte z = 0xff;
  if ((probe & (1u << col)) == 0) {
    z &= 0xFF ^ (1u << row);
  }
  if (code & SHIFTED) {
    if ((probe & 0x80) == 0) {
      z &= 0xFF ^ (1u << 6);
    }
  }
  PUSH_TO_BG(FG2BG_PUTCHAR, 0, '0' + (15 & (z >> 4)));
  PUSH_TO_BG(FG2BG_PUTCHAR, 0, '0' + (15 & (z >> 0)));
  return z;
}

#endif

////////////////////////////////////////////////////////
#if 0
void ResetCocoOnStartup() {
    OUTPUT(G_HALT, 0);
    sleep_ms(100);
    OUTPUT(G_RESET, 0);
    sleep_ms(500);
    OUTPUT(G_RESET, 1);
    sleep_ms(100);
    OUTPUT(G_HALT, 1);
}
#endif
////////////////////////////////////////////////////////

template <class T>
class CoreEngine {
 public:
  static void IN_RAM Fatal(const char* s, int x) {
    cobs_printf("\nFATAL(%d.): %s\n", x, s);
    while (1) continue;
  }

  static void IN_FLASH InitializePins() {
    for (uint i = 0; i <= 22; i++) {
      gpio_init(i);
      gpio_set_dir(i, GPIO_IN);
      gpio_set_pulls(i, false, false);
    }
    OUTPUT(G_LED, 1);
    INPUT(G_SND);
    INPUT(G_CTS);
    INPUT(G_SCS);
    INPUT(G_RESET);
    INPUT(G_SLENB);  // gerbil.pio will overtake

#define OPEN_DRAIN(PIN)        \
  gpio_init(PIN);              \
  gpio_set_dir(PIN, GPIO_OUT); \
  gpio_put(PIN, 0);            \
  gpio_set_dir(PIN, GPIO_IN);  \
  gpio_set_pulls(PIN, true, false);

    // OPEN_DRAIN(G_RESET);
    OPEN_DRAIN(G_HALT);
    OPEN_DRAIN(G_NMI);
    OPEN_DRAIN(G_CART);

    for (uint i = 32; i <= 47; i++) {
      gpio_init(i);
      gpio_set_dir(i, GPIO_IN);
      gpio_set_pulls(i, false, false);
    }
    // LED off.
    gpio_init(G_LED);
    gpio_set_dir(G_LED, GPIO_OUT);
    SET_LED(0);
  }  // end InitializePins

  // =========================================================================
  // Cooperative Multitasking — Background Core
  // =========================================================================
  //
  // Three coroutines run in round-robin on the background core:
  //   drain_task  — pops from fg2bg, handles cycle logs/NMI/putchar inline,
  //                 dispatches floppy and spoon chores to other tasks.
  //   floppy_task — handles floppy commands; yields while waiting for USB data.
  //   spoon_task  — runs BackgroundSpoonFeeder console; blocks internally
  //   (acceptable
  //                 because foreground doesn't push cycle logs during console).
  //
  // The scheduler pumps USB between every task switch.

  // Coroutine stacks (4KB each, 8-byte aligned for ARM AAPCS)
  static inline uint8_t drain_stack[STACK_SIZE] __attribute__((aligned(8)));
  static inline uint8_t floppy_stack[STACK_SIZE] __attribute__((aligned(8)));
  static inline uint8_t spoon_stack[STACK_SIZE] __attribute__((aligned(8)));

  // Simple flag-based channel: drain_task sets these, other tasks check them.
  // Only one chore can be pending per task at a time.
  // (inline volatile avoids the in-class-init restriction for template statics)
  static inline volatile uint floppy_pending_chore;
  static inline volatile bool floppy_has_work;

  // --- Drain Task ---
  // The primary fg2bg consumer. Handles fast chores inline and dispatches
  // slow ones to floppy_task/spoon_task.
  static void drain_task(Coro& self) {
    while (true) {
      // NOTE: Do NOT call HaltOff() here. Flow-control HALT is managed
      // exclusively by FlowControlCheck() on the foreground core.
      // Calling HaltOff() here was defeating the watermark-based throttling,
      // causing fg2bg FIFO overflow and lost write cycle records.

      // Advance keyboard injector timing (low overhead check)
      keyboard_injector::tick();

      // Note: drain_task handles background chores and USB polling.
      // BackgroundSpoonFeeder cooperatively yields when waiting for input,
      // allowing drain_task to run and decode keyboard COBS packets.

      uint chore = 0;
      if (!fg2bg.pop(chore)) {
        // FIFO empty — flush any partial cycle compression batch.
#if COMPRESS_CYCLES
        FlushPartialCycleBuffer();
#endif
        // Yield to let other tasks run. The scheduler calls PumpUsbCobs
        // on its main stack between every coro_resume.
        coro_yield(&self);
        continue;
      }

      const uint chore_num = chore >> 24;
      const byte chore_byte = 0xFF & chore;

      switch (chore_num) {
        case FG2BG_PUTCHAR:
          if (chore_byte) {
            cobs_putchar(chore_byte);
          }
          break;

        case FG2BG_START_KEYBOARD_INJECTOR:
          keyboard_injector::start_if_queued();
          break;

        case FG2BG_READ:  // read cycle
          {
#if COMPRESS_CYCLES
            InsertCycleWithCompression(chore);
#else
            if (chore_byte) {
              if (usb_tether_ok()) {
                unsigned char pkt[4] = {C_RAM2_READ, (unsigned char)(chore >> 16),
                                        (unsigned char)(chore >> 8),
                                        (unsigned char)chore};
                CobsEncodeAndTransmit(pkt, 4, putchar_raw);
              }
            }
#endif
          }
          break;

        case FG2BG_WRITE:  // write cycle
          write_counter++;
          {
#if COMPRESS_CYCLES
            InsertCycleWithCompression(chore);
#else
            if (usb_tether_ok()) {
              unsigned char pkt[4] = {C_RAM2_WRITE, (unsigned char)(chore >> 16),
                                      (unsigned char)(chore >> 8),
                                      (unsigned char)chore};
              CobsEncodeAndTransmit(pkt, 4, putchar_raw);
            }
#endif

#if TRIGGER_ON_WRITE
            if (((chore >> 8) & 0xFFFF) == TRIGGER_ON_WRITE) {
              Speed = SLOW_SPEED;
            }
#endif
          }
          break;

        case FG2BG_FLOPPY_LATCH:
        case FG2BG_FLOPPY_COMMAND:
        case FG2BG_W_256:
          // Dispatch to floppy task — spin briefly if it's still busy.
          while (floppy_has_work) coro_yield(&self);
          floppy_pending_chore = chore;
          floppy_has_work = true;
          break;

        case FG2BG_SPOON_ON_RESET:
          // Dispatch to spoon task.
          spoon_has_work = true;
          break;

        case FG2BG_PEEK_REPLY:
          // During console mode, peek replies go directly to console::peek()
          // via fg2bg. This shouldn't arrive here, but handle gracefully.
          break;

        default:
          cobs_printf("\nWUT? CHORE=%x\n", chore);
      }

      // Yield after every chore so the scheduler can pump USB and
      // resume floppy/spoon tasks. Without this, at SLOW_SPEED the
      // FIFO is never empty and drain_task would run forever,
      // starving ReceiveSectorData's USB polling.
      coro_yield(&self);
    }
  }

  // --- Floppy Task ---
  // Handles floppy commands. Yields while waiting for USB sector data,
  // allowing drain_task to continue processing cycle logs.
  static void floppy_task(Coro& self) {
    while (true) {
      if (!floppy_has_work) {
        coro_yield(&self);
        continue;
      }

      uint chore = floppy_pending_chore;
      uint chore_num = chore >> 24;
      byte chore_byte = chore & 0xFF;

      switch (chore_num) {
        case FG2BG_FLOPPY_LATCH:
          T::BackgroundFifoFloppyLatch(chore_byte);
          break;
        case FG2BG_FLOPPY_COMMAND:
          T::BackgroundFifoFloppyCommand(self, chore, chore_byte);
          break;
        case FG2BG_W_256:
          T::BackgroundFifoFloppyW256(self, chore_byte);
          break;
      }

      floppy_has_work = false;
    }
  }

  // --- Spoon Task ---
  // Runs the BackgroundSpoonFeeder Tcl console.
  // During console mode, foreground is in DriveConsole and does NOT push
  // cycle logs to fg2bg. So the drain_task is idle — no starvation risk.
  // BackgroundSpoonFeeder reads fg2bg directly for PEEK_REPLY during console
  // mode (via console::peek), so drain_task must not consume those entries.
  // This works because drain_task yields when fg2bg is empty.
  static void spoon_task(Coro& self) {
    while (true) {
      if (!spoon_has_work) {
        coro_yield(&self);
        continue;
      }

      HaltOff();
      gspoon::BackgroundSpoonFeeder(&self);
      spoon_has_work = false;
    }
  }

  FORCE_INLINE static void background() {
    // Create coroutines
    Coro drain, floppy, spoon;
    coro_create(&drain, drain_task, drain_stack, sizeof(drain_stack));
    coro_create(&floppy, floppy_task, floppy_stack, sizeof(floppy_stack));
    coro_create(&spoon, spoon_task, spoon_stack, sizeof(spoon_stack));

    cobs_printf("Background: coroutines initialized.\n");

    // Round-robin scheduler
    while (true) {
      coro_resume(&drain);
      if (PumpUsbCobsHasWork()) PumpUsbCobs();
      coro_resume(&floppy);
      if (PumpUsbCobsHasWork()) PumpUsbCobs();
      coro_resume(&spoon);
      if (PumpUsbCobsHasWork()) PumpUsbCobs();
    }
  }  // end background

  FORCE_INLINE static void foreground() {
    // Disable interrupts in this "fast" core.
    save_and_disable_interrupts();

    const PIO pio = pio0;
    constexpr uint sm = 0;

    // Detect whether a Coco2 is connected and powered on.
    if (!detect_e_clock()) {
      // No Coco2 clock — start USB-only Tcl session.
      // cobs_printf("No Coco2 E clock detected. Starting USB-only mode.\n");
      cobs_printf(" [-E] "); // Indicate clock not detected.
      spoon_has_work = true;  // Start BackgroundSpoonFeeder on background

      // Poll for Coco2 power-on. Check E clock periodically.
      // Interrupts are disabled, so we use a busy-wait delay.
      while (!detect_e_clock()) {
        // ~10ms delay at 250MHz ≈ 2.5M cycles
        for (volatile uint i = 0; i < 2500000; i++) {
        }
      }
      // cobs_printf("Coco2 E clock detected! Entering bus cycle loop.\n");
      cobs_printf(" [+E] "); // Indicate clock detected.
    }

    // Coco2 is running — enter normal PIO bus cycle loop.
      // ON RESET, GO INTO SPOONFEEDING.
      gspoon::SpoonfeedConsoleOnReset();

      // IF KEYBOARD INJECTION QUEUED, START IT (in background task).
      PUSH_TO_BG(FG2BG_START_KEYBOARD_INJECTOR, 0, 0);

      // AFTER SPOONFEEDING, START NORMAL CYCLES.
#if FIFO_INDICATOR_0500
      fifo_indicator_ram = ram;  // Enable FIFO pressure indicators on VDG screen
#endif
      uint cycle = 0;
      bool floppy_emulation = centipede_config.floppy_fd || centipede_config.floppy_pc;
      while (true) {
        const uint signals = GERBIL_GET();
        FlowControlCheck();  // Check watermark every cycle

        const bool reading = ((signals & (1u << G_RW)) != 0);
        const uint abus = volatile_sio_hw->gpio_hi_in & 0xFFFF;
        byte dbus = 0x00;

        constexpr uint NEG_CTS = (1 << G_CTS);
        constexpr uint NEG_SCS = (1 << G_SCS);
        constexpr uint NEG_SELECTS = NEG_CTS | NEG_SCS;

        if (LIKELY(!floppy_emulation || (signals & NEG_SELECTS) == NEG_SELECTS)) {
          // CASE normal

          if (LIKELY(reading)) {
            // CASE normal read
            if (0xFF00 <= abus) {
              // CASE device read
              if (UNLIKELY(abus == 0xFF00 && keyboard_injector::active)) {
                // KEYBOARD INJECTOR: ultra-fast inline path.
                // Determine which column is probed from ram[$FF02].
                byte probe = ram[0xFF02];
                byte sense = 0x7F; // default: no key
                for (int col = 0; col < 8; col++) {
                  if ((probe & (1 << col)) == 0) {
                    sense &= keyboard_injector::row_response[col];
                  }
                }
                dbus = sense;
                GERBIL_DRIVE(dbus);
              } else {
                auto r = IOReaders[abus & 0xFF];
                if (r) {
                  dbus = r(abus);
                  GERBIL_DRIVE(dbus);
                } else {
                  GERBIL_PASS();
                  dbus = (byte)(GERBIL_GET());  // log & debug
                }
              }
            } else if (
                    centipede_config.rom_disk11
                    && not T::UseCoco64kRam(abus)
                    && 0xC000 <= abus
                    && abus < 0xE000) {
              // I DONT KNOW WHY, but we're not seeing CTS drop for Disk Basic
              // ROM.
              //--SAY('c');
              dbus = disk11_rom[abus & 0x1FFF];
              GERBIL_DRIVE(dbus);
            } else if (
                    centipede_config.ram_64k
                    && T::UseCoco64kRam(abus)) {
              uint atrans = T::TranslateCoco64kRamAddress(abus);
              dbus = ram[atrans];
              GERBIL_DRIVE(dbus);
            } else {
              GERBIL_PASS();
              dbus = (byte)(GERBIL_GET());  // log & debug
            }
            if (centipede_config.trace_reads
#if ALWAYS_TRACE_READS_IF_ADDR_GE
                    || (ALWAYS_TRACE_READS_IF_ADDR_GE <= abus)
#endif
               ) {
                T::PushFifoRead(abus, dbus);
            }
#if TRIGGER_ON_READ
            if (abus == TRIGGER_ON_READ) {
              centipede_config.trace_reads = true;
            }
#endif
          } else {
            // CASE normal write
            dbus = (byte)(GERBIL_GET());

            if (0xFF00 <= abus) {
              auto w = IOWriters[abus & 0xFF];
              if (w) {
                w(abus, dbus);
              }
              ram[abus] = dbus;

              // Currently, always trace non-special I/O writes.
              T::PushFifoWrite(abus, dbus);

            } else {
              uint atrans = T::UseCoco64kRam(abus)
                                ? T::TranslateCoco64kRamAddress(abus)
                                : abus;
              ram[atrans] = dbus;

              // Optionally, always trace all non-special writes.
              if (centipede_config.trace_writes) {
                T::PushFifoWrite(atrans, dbus);
              }
            }
          }
        } else {  // Is Special Select
          // CASE special
          if (LIKELY(reading)) {  // Special CPU READING -- we TX
            // CASE special read
            if ((signals & NEG_SCS) == 0) {  // READ CTS
              T::ReadScsFloppy(abus, dbus);
            }
            // JOIN special read
            GERBIL_DRIVE(dbus);
            if (true) {
              T::PushFifoRead(abus, dbus);
            }
          } else {  // Special CPU WRITING -- we RX
            // SAY('W');
            // CASE special write
            dbus = (byte)(GERBIL_GET());
            ram[abus] = dbus;

            if (LIKELY((signals & NEG_SCS) == 0)) {
              T::WriteScsFloppy(abus, dbus);
            }  // end special write SCS
            T::PushFifoWrite(abus, dbus);
          }  // end special read or write
        }  // end if special

        ++cycle;
#if GSPOON_POC_DEMO
        if (cycle == 2000000) {
          gspoon::SpoonNMI();  // Hijack for PoC demo
        }
#endif

#if ON_RESET_DO_SPOONFEED_CONSOLE
        if ((signals & (1 << G_RESET)) == 0) {
          break;
        }
#endif

          if (nmi_fuse) {
            if (--nmi_fuse == 0) {
              gpio_set_dir(G_NMI, GPIO_IN);  // Release NMI
            }
          }

      }  // end while true (until RESET)

      // IF NORMAL RUN EXITS, it's because of RESET, so RESTART.
      rp2350_reset_standard();

  }  // end foreground


// // #define PushFifoRead_CRITERIA (Speed <= SLOW_SPEED && !fg_halt_for_flow_control)
// #define PushFifoRead_CRITERIA (Speed <= SLOW_SPEED)

  FORCE_INLINE static void PushFifoRead(uint abus, byte dbus) {
    // if (PushFifoRead_CRITERIA) {
      if (abus != 0xFFFF) {
        PUSH_TO_BG(FG2BG_READ, abus, dbus);
      }
    // }
  }
  FORCE_INLINE static void PushFifoWrite(uint abus, byte dbus) {
    // Always push writes — they're rare and critical for the virtual screen.
    // Only reads are throttled by flow control.
      bool ok = fg2bg.push(((FG2BG_WRITE) << 24) | ((abus) << 8) | (dbus));
#if FIFO_INDICATOR_0500
      if (!ok) { push_fail_counter++; if (fifo_indicator_ram) fifo_indicator_ram[0x0504] = 0xFF; }
#endif
  }

  FORCE_INLINE static void RunCores(void (*core1_func)(void),
                                    void (*core0_func)(void)) {
    const PIO pio = pio0;
    constexpr uint sm = 0;

    pio_clear_instruction_memory(pio);
    pio_add_program_at_offset(pio, &gerbil_program, 0);
    gerbil_program_init(pio, sm, 0);
    cobs_printf("#gerbil_program.length=%d\n", gerbil_program.length);

    // foreground must be fast.
    multicore_launch_core1(core1_func);

    // background on core 0 handles interrupts.
    core0_func();
  }
};  // end CoreEngine

void IN_RAM core1_trampoline();
void IN_RAM core0_trampoline();

class Engine0 : public DoFloppy<Engine0>,
                // public DoCoco3Mmu<Engine0>,
                // public SmallRam<Engine0>,
                // public ReadWriteSpyEngine<Engine0>
                // public WriteSpyEngine<Engine0>
                public DoCoco64k<Engine0>,
                public CoreEngine<Engine0> {
 public:
  static void RunEngine() {
    // T::InitCoco3Mmu();
    InitCoco64k();
#if USE_ORCHESTRA90
    orchestra90::Init();
#endif
    ResetCompressCycles();  // call once at session start
    RunCores(core1_trampoline, core0_trampoline);
  }
};

void IN_RAM core1_trampoline() { Engine0::foreground(); }

void IN_RAM core0_trampoline() { Engine0::background(); }

void IN_RAM restart_core1(void (*func)(void)) {
  // 1. Force Core 1 into reset
  multicore_reset_core1();

  // 2. Small delay to ensure hardware lines settle (often optional, but safe)
  sleep_us(10);

  // 3. Launch it again with the desired entry point
  multicore_launch_core1(func);
}

void IN_RAM safe_adjust_flash_speed() {
#if MHz > 150
  // 1. Critical: Disable interrupts while making the adjustment
  uint32_t ints = save_and_disable_interrupts();

  const uint32_t HASTY = 2;
  const uint32_t SAFE = 4;  // 250 MHz / 4 = 62.5 MHz (Perfect for safety)
  uint32_t clkdiv = SAFE;
  uint32_t rxdelay =
      4;  // On RP2350, for QSPI frequencies, match RXDELAY to CLKDIV

  // 2. Mask and update the CLKDIV and RXDELAY fields in the QMI timing register
  hw_write_masked(
      &qmi_hw->m[0].timing,
      ((clkdiv << QMI_M0_TIMING_CLKDIV_LSB) & QMI_M0_TIMING_CLKDIV_BITS) |
          ((rxdelay << QMI_M0_TIMING_RXDELAY_LSB) & QMI_M0_TIMING_RXDELAY_BITS),
      QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);

  // 3. Re-enable interrupts
  restore_interrupts(ints);
#endif
}

int IN_RAM main() {
  // ResetCocoOnStartup();

  Engine0::InitializePins();
  FlashLabel::InitLabel();
#if MHz != 150
  set_sys_clock_khz(MHz * 1000, true);
#endif
  stdio_usb_init();
  safe_adjust_flash_speed();

  OUTPUT(G_HALT, 0);
  OUTPUT(G_RESET, 0);
  for (uint i = 0; i < 5; i++) {
    SET_LED(1);
    sleep_ms(200);

    SET_LED(0);
    sleep_ms(200);
  }
  INPUT(G_RESET);
  INPUT(G_HALT);

  FlashLabel::PrintLabel();
  init_lfs();
  start_20ms_timer();
  global_tcl_interp = Tcl_CreateInterp();
  register_tcl_commands(global_tcl_interp);
  centipede_config.SetAll(true);  // enable everything
  centipede_config.trace_reads = false;
  centipede_config.floppy_pc = false;
  set_floppy_names();

  Engine0::RunEngine();
}
