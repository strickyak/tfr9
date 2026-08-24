// v4_tfr911h_main.cpp — TFR911H v4 build with Tcl Shell + LittleFS + VFS.
//
// Self-contained compilation unit that produces tfr911h_v4.uf2.
// Includes TFR911 headers from tmanager911/very-turbos/ directly.
// Boots into Tcl REPL on background (core 0).  When the user says
// `bye`, the 6309 is reset and the foreground PIO bus loop starts.
//
// Based on: v3/tmanager911/very-turbos/veryturbos.cpp (510 lines)

#define TRACE 0
#define SPEED_STATS 1
#define DEBUG_TCL_REPL 0
// TransmitWrite now routes through fg2bg FIFO — safe from core 1.
#define DUMP_FIRST_CYCLES 16    // Log the first N bus cycles after boot for debugging
#define MHz 250  // clock speed

#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/structs/qmi.h>
#include <hardware/structs/systick.h>
#include <hardware/timer.h>
#include <hardware/watchdog.h>
#include <pico/bootrom.h>
#include <pico/multicore.h>
#include <pico/rand.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <pico/unique_id.h>
#include <stdint.h>
#include <stdio.h>

#include <cstring>
#include <atomic>
#include <array>
#include <string>
#include <functional>

// LittleFS (must come before firmware headers that use lfs types)
// Wrap in extern "C" — these are C headers compiled as C in lfs.c/lfs-centipede.c
extern "C" {
#include "../v1/littlefs/lfs-centipede.h"
#include "../v1/littlefs/lfs.h"
#include "../v1/littlefs/lfs_util.h"
}

extern "C" {
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

#define FORCE_INLINE inline __attribute__((always_inline))
#define force_inline FORCE_INLINE
#define IN_RAM __not_in_flash("tfr911")

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define likely(x) LIKELY(x)
#define unlikely(x) UNLIKELY(x)

// COBS encoder/decoder (shared with Centipede)
#include "cobs.h"
#include "../v1/firmware/cross-core.h"

// Console stubs for TFR911 (must come before tcl_commands.h / tcl_io.h)
// Block the real console.h from loading by defining its include guard.
#define FIRMWARE_CONSOLE_H_
// Block the real gspoon.h — editor.h includes it, but TFR911 doesn't
// need the full Centipede spoon feeder.  We provide gspoon::g_spoon_coro
// and gspoon::SleepMillis as stubs below.
#define _GSPOON_H_
#include "v4_console_tfr911_stub.h"

// Tcl interpreter
#include "../v1/tcl6.7c/tcl.h"
Tcl_Interp* global_tcl_interp = nullptr;

const char HexAlphabet[] =
    "0123456789ABCDEFXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
    "XXXXXXX";

using byte = unsigned char;

// ═══════════════════════════════════════════════════════════════════
// TFR911H GPIO Pin Assignments
// ═══════════════════════════════════════════════════════════════════
constexpr uint RESET = 20;
constexpr uint NMI = 21;
constexpr uint IRQ = 22;
constexpr uint FIRQ = 23;
constexpr uint HALT = 24;
constexpr uint LED = 25;
constexpr uint LIC = 26;
constexpr uint AVMA = 27;
constexpr uint BS = 28;
constexpr uint E = 29;
constexpr uint Q = 30;
constexpr uint R_W = 31;

// ═══════════════════════════════════════════════════════════════════
// IO function pointer types and arrays
// ═══════════════════════════════════════════════════════════════════
bool is_an_os9;
using IOReader = byte (*)(uint addr);
using IOWriter = void (*)(uint addr, byte data);
IOReader IOReaders[256];
IOWriter IOWriters[256];

// ═══════════════════════════════════════════════════════════════════
// Cross-core FIFO: foreground (core 1) → background (core 0)
// ═══════════════════════════════════════════════════════════════════
CrossCoreFIFO<uint, 8192> fg2bg;

// Same event tags as Centipede — shared for future background code reuse.
enum FG2BG_Tags {
  FG2BG_PUTCHAR = 0,
  FG2BG_READ    = 1,
  FG2BG_SPOON_ON_RESET = 2,
  FG2BG_WRITE   = 3,
  FG2BG_SYNC_NEEDED = 4,
  FG2BG_NMI     = 5,
  FG2BG_FLOPPY_COMMAND = 6,
  FG2BG_FLOPPY_LATCH   = 7,
  FG2BG_W_256   = 8,
  FG2BG_PEEK_REPLY = 9,
  FG2BG_START_KEYBOARD_INJECTOR = 10,
#if SPEED_STATS
  FG2BG_MEGA_CYCLE = 11,
#endif
  FG2BG_FIC   = 12,   // First Instruction Cycle (read with LIC from previous cycle)
};

// SAY: blocking push — console output must never be dropped.
// Stalls the foreground briefly if the FIFO is full (background drains it).
#define SAY(C) do { \
  uint _say_w = ((uint)FG2BG_PUTCHAR << 24) | ((C) & 255); \
  while (!fg2bg.push(_say_w)) { tight_loop_contents(); } \
} while(0)
// PUSH_TO_BG: blocking push for cycle events — trace must be complete.
// HALT flow control keeps the FIFO near LOW_WATERMARK in steady state;
// blocking only kicks in on transient bursts (e.g., IRQ register stacking).
#define PUSH_TO_BG(TAG, A, D) do { \
  uint _pb_w = ((uint)(TAG) << 24) | (((A) & 0xFFFF) << 8) | ((D) & 0xFF); \
  while (!fg2bg.push(_pb_w)) { tight_loop_contents(); } \
} while(0)

// ═══════════════════════════════════════════════════════════════════
// Protocol constants
// ═══════════════════════════════════════════════════════════════════
enum message_type : byte {
  C_LOGGING = 130,
  C_PRE_LOAD = 163,
  C_RAM_CONFIG = 164,
  C_DUMP_RAM = 167,
  C_DUMP_LINE = 168,
  C_DUMP_STOP = 169,
  C_DUMP_PHYS = 170,
  C_EVENT = 172,
  C_DISK_READ = 173,
  C_DISK_WRITE = 174,
  EVENT_RTI = 176,
  EVENT_SWI2 = 177,
  T_HELLO = 178,
  T_COMMAND = 179,
  C_REBOOT = 192,
  C_PUTCHAR = 193,
  C_RAM2_WRITE = 195,
  C_RAM3_WRITE = 196,
  C_RAM5_WRITE = 198,
  C_CYCLE = 200,
  C_RAM2_READ = 211,
  C_FIC_CYCLE = 227,   // 0xE3: First Instruction Cycle, 3-byte payload: AHi ALo Data
};

enum cycle_kind : byte {
  CY_UNUSED = 0,
  CY_SEEN = 1,
  CY_UNSEEN = 2,
  CY_MORE = 3,
  CY_READ = 4,
  CY_WRITE = 5,
  CY_IDLE = 6,
  CY_FIC = 7,
};

// ═══════════════════════════════════════════════════════════════════
// Globals
// ═══════════════════════════════════════════════════════════════════
byte ram[64 * 1024];

byte vector_ram[16];

// IN_RAM IO reader for vector_ram — must not stall in flash
// during the critical PIO read cycle (E=Q=1).
byte IN_RAM vector_ram_reader(uint _a) { return vector_ram[_a & 15]; }

void InstallVector(uint i, uint addr) {
  vector_ram[2 * i + 0] = (byte)(addr >> 8);
  vector_ram[2 * i + 1] = (byte)addr;

  IOReaders[255 & (0xFFF0 + 2 * i + 0)] =
  IOReaders[255 & (0xFFF0 + 2 * i + 1)] = vector_ram_reader;
}

// ═══════════════════════════════════════════════════════════════════
// COBS output helpers (via shared cobs_tx.h) + USB pipeline
// ═══════════════════════════════════════════════════════════════════
#include "../v1/firmware/cobs_tx.h"
#include "../v1/firmware/usb_pipeline.h"

// ShowChar — called by the old turbo9sim.h (unqualified) from simTxWriter.
// Pushes to fg2bg FIFO; background drains and sends over USB.
// (CoreEngine::ShowChar in v4_core_engine.h is a separate CRTP method
// used only by Centipede — TFR911 Engine does not inherit from CoreEngine.)
void ShowChar(char c) {
  SAY(c);
}

void putbyte(byte x) {
  unsigned char pkt[2] = {C_PUTCHAR, x};
  CobsEncodeAndTransmit(pkt, 2, [](int ch) { putchar_raw(ch); });
}

void TransmitMessage(byte messtype, uint sz, const byte* buf) {
  byte pkt[sz + 1];
  pkt[0] = messtype;
  memcpy(pkt + 1, buf, sz);
  CobsEncodeAndTransmit(pkt, sz + 1, [](int ch) { putchar_raw(ch); });
}

void TransmitCycle(uint cy, byte flags, byte kind, byte data, uint addr) {
  byte r[8];
  r[0] = cy >> 24;
  r[1] = cy >> 16;
  r[2] = cy >> 8;
  r[3] = cy >> 0;
  r[4] = flags + (kind << 5);
  r[5] = data;
  r[6] = addr >> 8;
  r[7] = addr >> 0;
  TransmitMessage(C_CYCLE, 8, r);
}

void TransmitWrite(uint addr, byte data) {
  // Push through fg2bg FIFO instead of calling putchar_raw directly.
  // This is safe from core 1 (Install_OS) because the background
  // drains the FIFO while waiting for foreground_running.
  uint word = ((uint)FG2BG_WRITE << 24) | ((addr & 0xFFFF) << 8) | data;
  while (!fg2bg.push(word)) {
    tight_loop_contents();  // Spin until space available
  }
}

// ═══════════════════════════════════════════════════════════════════
// CircBuf + USB/COBS pipeline (PumpUsbCobs compatible)
// ═══════════════════════════════════════════════════════════════════
#include "circbuf.h"

// PIO header (generated by cmake)
#include "pio_veryturbos.pio.h"

// Flash label
#include "../../../tmanager911/very-turbos/flash-label.h"

// USB pipeline buffers — same names as Centipede for usb_pipeline.h compat
CircBuf<unsigned char, 1024> usb_raw_buf;
CircBuf<std::string*, 64> usb_packet_buf;
UsbReceiver usb_receiver(usb_raw_buf);
CobsDecoder<1024, 64> cobs_decoder(usb_raw_buf, usb_packet_buf);

// Terminal input buffer (chars from USB → turbo9sim RX)
CircBuf<unsigned char, 1024> term_input;

volatile uint delay_busy;
void Delay(uint n) {
  for (uint i = 0; i < n * 10; i++) {
    delay_busy += i;
  }
}

// ═══════════════════════════════════════════════════════════════════
// ACIA simulation state
// ═══════════════════════════════════════════════════════════════════
bool acia_irq_enabled;
bool acia_irq_firing;
bool acia_char_in_ready;
int acia_char;
bool irq_needed;
bool prev_irq_needed;

#define Printf if(false)printf



// ═══════════════════════════════════════════════════════════════════
// Include TFR911 headers from v1
// ═══════════════════════════════════════════════════════════════════
#include "../../../tmanager911/very-turbos/turbo9sim.h"
#include "../../../tmanager911/very-turbos/romlist.h"

// ═══════════════════════════════════════════════════════════════════
// Coroutines, VFS, Tcl commands, PicoRPC
// ═══════════════════════════════════════════════════════════════════
#include "../v1/firmware/coro.h"

// Abort handler
#include "../v1/firmware/abort.h"

// PCB (protobuf-like encoding for RPC)
#include "../v1/firmware/pcb.h"

// VFS RPC (must come before tcl_io/vfs/tcl_commands)
#include "../v1/firmware/vfs_rpc.h"

// Forward-define T_RPC and T_PICO_RPC before tcl_io.h needs them
#ifndef T_RPC
#define T_RPC 180
#endif
#ifndef T_PICO_RPC
#define T_PICO_RPC 181
#endif

// Tcl I/O — TFR911 uses USB only (no CoCo2 screen)
#include "../v1/firmware/tcl_io.h"

// VFS (file system overlay)
#include "../v1/firmware/vfs.h"

// RTC — provides get_system_time() needed by SleepMillis
#include "../v1/firmware/rtc.h"

// gspoon namespace stub — only g_spoon_coro and SleepMillis are needed.
// TFR911 doesn't use the full gspoon (no CoCo2 screen/keyboard).
namespace gspoon {
  Coro* g_spoon_coro = nullptr;

  void SleepMillis(Coro* c, uint64_t ms) {
    uint32_t start_sec, start_ms;
    get_system_time(&start_sec, &start_ms);
    double start_time = (double)start_sec + ((double)start_ms / 1000.0);
    double seconds = (double)ms / 1000.0;
    coro_yield(c);
    while (true) {
      uint32_t current_sec, current_ms;
      get_system_time(&current_sec, &current_ms);
      double current_time = (double)current_sec + ((double)current_ms / 1000.0);
      if (current_time - start_time >= seconds) break;
      coro_yield(c);
    }
  }
}  // namespace gspoon

// LittleFS Tcl commands (ls, cp, cat, etc.)
#include "../v1/firmware/littlefs.h"

// Globals needed by pico_rpc.h and tcl_commands.h
uint32_t boot_mode = 0;
uint32_t boot_mode_check = 0;
#define BOOT_MODE_CHECKER 0x56781234u
bool startup_e_clock_detected = false;

// Stub for menu.h — TFR911 has no floppy config.
inline void set_floppy_names() {}

// Tcl commands (all built-in shell commands)
#include "../v1/firmware/tcl_commands.h"

// PicoRPC (inject commands from PC)
#include "../v1/firmware/pico_rpc.h"

#define TCL_BYE 9

// ═══════════════════════════════════════════════════════════════════
// USB packet dispatch — delivers typed chars to term_input
// ═══════════════════════════════════════════════════════════════════
void PollTermInput() {
  // Take non-RPC, non-PicoRPC packets from usb_packet_buf as terminal input
  while (true) {
    std::string* pkt = usb_packet_buf.Yoink([](std::string* s) {
      return s && s->length() > 0 &&
             (unsigned char)(*s)[0] != T_RPC &&
             (unsigned char)(*s)[0] != T_PICO_RPC;
    });
    if (!pkt) break;
    byte cmd = (byte)(*pkt)[0];
    switch (cmd) {
      case C_PUTCHAR:
        if (pkt->size() >= 2) {
          byte c = (byte)(*pkt)[1];
          if (c == 10) c = 13;
          term_input.Put(c);
        }
        break;
      case C_REBOOT:
        reset_usb_boot(0, 0);
        break;
      default:
        if (cmd >= 1 && cmd <= 127) {
          byte c = cmd;
          if (c == 10) c = 13;
          term_input.Put(c);
        }
        break;
    }
    delete pkt;
  }
}

// ═══════════════════════════════════════════════════════════════════
// IN_RAM IO readers/writers for turbo9sim ACIA
// The lambda IOReaders/IOWriters installed by Turbo9sim_Install live
// in flash. During bus cycles, the PIO holds at pull block side 3
// (E=Q=1) waiting for data. An XIP cache miss on the lambda body
// stalls the CPU, and the 6809 latches garbage from the data bus.
// These IN_RAM wrappers eliminate that stall.
// ═══════════════════════════════════════════════════════════════════

// Readers
byte IN_RAM acia_read_tx(uint addr)      { return sim_last_char_tx; }
byte IN_RAM acia_read_rx(uint addr)      { sim_status_reg &= ~0x02; return sim_last_char_rx; }
byte IN_RAM acia_read_status(uint addr)  { return sim_status_reg; }
byte IN_RAM acia_read_control(uint addr) { return sim_control_reg; }

// Writers
void IN_RAM acia_write_tx(uint addr, byte data) {
  sim_last_char_tx = data;
  SAY(data);  // Push to background — no USB call from foreground!
}
void IN_RAM acia_write_rx(uint addr, byte data) {
  // No effect (write to RX register).
}
void IN_RAM acia_write_status(uint addr, byte data) {
  if (data & 0x01) { sim_timer_irq = false; sim_status_reg &= ~0x01; }
  if (data & 0x02) { sim_rx_ready_irq = false; sim_status_reg &= ~0x02; }
}
void IN_RAM acia_write_control(uint addr, byte data) {
  sim_control_reg = data;
}
#include "../../../tmanager911/very-turbos/turbo9os.h"

// ═══════════════════════════════════════════════════════════════════
// GPIO Initialization — Open-drain for slow control pins
// ═══════════════════════════════════════════════════════════════════
uint InitializePinsReturnDirections() {
  uint directions = 0;
  for (int i = 0; i < 48; i++) {
    gpio_init(i);
    switch (i) {
      // E, Q, LED: push-pull outputs
      case E:
      case Q:
      case LED:
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 1);
        directions |= (1 << i);
        break;

      // Slow 6809E control: open-drain
      // Output latch = 0, direction = input (released, pulled high).
      // Assert by setting dir to output. Release by setting dir to input.
      case RESET:
      case NMI:
      case IRQ:
      case FIRQ:
      case HALT:
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);             // Latch = 0 (active low)
        gpio_set_dir(i, GPIO_IN);   // Released (not driving)
        gpio_set_pulls(i, true, false);  // Internal pull-up
        break;

      default:
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_up(i);
        break;
    }
  }
  return directions;
}


// ═══════════════════════════════════════════════════════════════════
// Timer (1ms tick on core 0)
// ═══════════════════════════════════════════════════════════════════
constexpr uint GROUP_SIZE = 100; // 10000;
#if SPEED_STATS
uint milliseconds;
volatile uint64_t cycles;  // Updated by foreground, read by background for stats
struct repeating_timer TimerData;
bool TimerCallback(repeating_timer_t* rt) {
  milliseconds++;
  return true;
}
#endif

// ═══════════════════════════════════════════════════════════════════
// Guts<T> — the bus cycle engine (CRTP)
// ═══════════════════════════════════════════════════════════════════
// HALT-based flow control for cycle logging.
// When the fg2bg FIFO fills up, the foreground asserts HALT to throttle
// the 6309.  During HALT the 6309 emits idle cycles at addr=0xFFFF which
// we skip (no FIFO push), letting the background drain the FIFO.
#define FG2BG_HIGH_WATERMARK 2100  // 6000  // Assert HALT when FIFO exceeds this
#define FG2BG_LOW_WATERMARK  2000  // Release HALT when FIFO drains below this
volatile bool fg_halt_for_flow_control = false;
volatile int fg_inner_step = 0;  // Diagnostic: where in the inner loop is the foreground?

volatile bool foreground_running = false;  // Set by core 1 when inner loop starts
template <typename T>
struct Guts {
  force_inline static void Poke(uint a, byte b) { ram[a & 0xFFFF] = b; }
  force_inline static byte Peek(uint a) { return ram[a & 0xFFFF]; }
  force_inline static uint Peek2(uint a) {
    return (((uint)Peek(a)) << 8) | Peek(a + 1);
  }

  static bool ChangeInterruptPin(bool irq_needed) {
    // Open-drain: assert by setting dir to output, release by input
    if (irq_needed) {
      gpio_set_dir(IRQ, GPIO_OUT);
    } else {
      gpio_set_dir(IRQ, GPIO_IN);
    }
    return true;
  }

  static void FORCE_INLINE RunCPU() {
    T::Install_OS();

    volatile sio_hw_t* hw = (volatile sio_hw_t*)sio_hw;

#if SPEED_STATS
    cycles = 0;
#endif

#if DUMP_FIRST_CYCLES
    // Diagnostic: capture the first N bus cycles starting from the
    // reset vector fetch (addr=0xFFFE). Skips HALT-induced idle
    // cycles (addr=0xFFFF) that occur before HALT is released.
    struct CycleDump { uint16_t addr; uint8_t value; uint8_t rw; };
    static CycleDump dump_buf[DUMP_FIRST_CYCLES];
    int dump_count = 0;
    bool dump_triggered = false;  // Start recording on first addr==0xFFFE
#endif

    // Signal background that we've reached the inner loop.
    // Background waits for this before releasing RESET/HALT.
    foreground_running = true;

    // OUTER LOOP — foreground only handles IRQ pin + PIO bus cycles.
    // USB I/O and terminal RX are handled by background on core 0.
    while (true) {
      irq_needed = T::Turbo9sim_IrqNeeded();
      if (irq_needed != prev_irq_needed) {
        bool ok = ChangeInterruptPin(irq_needed);
        if (ok) {
          prev_irq_needed = irq_needed;
          gpio_put(LED, irq_needed);
        }
      }

#if TRACE
      // Flow control: throttle 6309 via HALT when FIFO is filling up.
      // During HALT, the 6309 puts 0xFFFF on the address bus; we skip
      // those reads (no push) so the FIFO drains.
      if (fg_halt_for_flow_control) {
        if (fg2bg.size() < FG2BG_LOW_WATERMARK) {
          gpio_set_dir(HALT, GPIO_IN);   // Release HALT
          fg_halt_for_flow_control = false;
        }
      } else {
        if (fg2bg.size() > FG2BG_HIGH_WATERMARK) {
          gpio_set_dir(HALT, GPIO_OUT);  // Assert HALT (open-drain)
          fg_halt_for_flow_control = true;
        }
      }
#endif

      uint prev_late_pins = 0;

      // INNER LOOP
      for (int i = 0; i < GROUP_SIZE; i++) {
        fg_inner_step = 1;
        pio_sm_put(pio0, 0, 0);  // put sync word

        byte value = 0;
        fg_inner_step = 2;
        uint pins = pio_sm_get_blocking(pio0, 0);  // get early pins
        uint prev_addr = 0xFFFF & hw->gpio_hi_in;
        uint addr;
        while (true) {
          addr = 0xFFFF & hw->gpio_hi_in;
          if (addr == prev_addr) break;
          prev_addr = addr;
        }

        // Read R/W from GPIO AFTER address stabilization, not from
        // the PIO's early-pins sample.  The PIO decides read/write
        // via `jmp pin` (Phase 3), but `in pins, 32` samples at
        // Phase 2 — 8ns earlier.  During HALT transitions, R/W can
        // change in that window, causing firmware/PIO desync → deadlock.
        const bool reading = gpio_get(R_W);
        byte kind = 0;
        uint late_pins = 0;

        if (likely(reading)) {
          // READ CYCLES
          if (likely(addr < 0xFF00)) {
            value = ram[addr];
          } else {
            IOReader fn = IOReaders[addr & 0xFF];
            if (fn) {
              value = fn(addr);
            } else {
              value = 0;
            }
          }
          kind = CY_READ;
        } else {
          // WRITE CYCLES
          fg_inner_step = 4;
          late_pins = pio_sm_get_blocking(pio0, 0);
          value = (byte)late_pins;

          if (likely(addr < 0xFF00)) {
            ram[addr] = value;
          } else {
            IOWriter fn = IOWriters[addr & 0xFF];
            if (fn) {
              fn(addr, value);
            }
          }
          kind = CY_WRITE;
        }

        if (likely(reading)) {
          fg_inner_step = 5;
          pio_sm_put(pio0, 0, value);
          fg_inner_step = 6;
          late_pins = pio_sm_get_blocking(pio0, 0);  // LATE PINS
        }

        // ── Trace push: AFTER the PIO handshake is complete ──────
        // All bus timing is done; the 6309 has latched its data.
        // Pushing here avoids SRAM bus contention during the critical
        // window between computing the read value and pio_sm_put.
        bool is_lic = ((late_pins & (1<<LIC)) != 0);
        bool is_fic = ((prev_late_pins & (1<<LIC)) != 0);

        fg_inner_step = 7;
#if TRACE
        if (reading) {
          if (addr != 0xFFFF) {
            if (is_fic) {
              PUSH_TO_BG(FG2BG_FIC, addr, value);
            } else {
              PUSH_TO_BG(FG2BG_READ, addr, value);
            }
          }
        } else {
          PUSH_TO_BG(FG2BG_WRITE, addr, value);
        }
#endif

#if DUMP_FIRST_CYCLES
        if (!dump_triggered && addr == 0xFFFE) {
          dump_triggered = true;  // Start recording from the reset vector fetch
        }
        if (dump_triggered && dump_count < DUMP_FIRST_CYCLES) {
          dump_buf[dump_count] = { (uint16_t)addr, value, (uint8_t)(reading ? 'r' : 'W') };
          dump_count++;
          if (dump_count == DUMP_FIRST_CYCLES) {
            // Format: "D:AAAA=VV:R\n" for each cycle
            for (int d = 0; d < DUMP_FIRST_CYCLES; d++) {
              SAY('D'); SAY(':');
              SAY(HexAlphabet[(dump_buf[d].addr >> 12) & 0xF]);
              SAY(HexAlphabet[(dump_buf[d].addr >> 8) & 0xF]);
              SAY(HexAlphabet[(dump_buf[d].addr >> 4) & 0xF]);
              SAY(HexAlphabet[dump_buf[d].addr & 0xF]);
              SAY('=');
              SAY(HexAlphabet[(dump_buf[d].value >> 4) & 0xF]);
              SAY(HexAlphabet[dump_buf[d].value & 0xF]);
              SAY(':');
              SAY(dump_buf[d].rw);
              SAY('\n');
            }
          }
        }
#endif

        prev_late_pins = late_pins;
      }  // next i

#if SPEED_STATS
      cycles += GROUP_SIZE;
      static int cycle_counter = 0;
      cycle_counter += GROUP_SIZE;
      if (cycle_counter >= 1000000) {
        PUSH_TO_BG(FG2BG_MEGA_CYCLE, 0, 0);
        cycle_counter -= 1000000;
      }
#endif
    }  // true
  }    // func RunCPU
};     // Guts

// ═══════════════════════════════════════════════════════════════════
// Engine — CRTP composition
// ═══════════════════════════════════════════════════════════════════
struct Engine : public DoTurbo9os<Engine,
                    RomList<Turbo9os_Rom, Basic09_Rom>>,
                public DoTurbo9sim<Engine>,
                public Guts<Engine> {};

// This IN_RAM Engine Launcher will contain the inlined Engine::RunCPU,
// so that method will effectively be IN_RAM as well.
void IN_RAM Engine__RunCPU() {
    Engine::RunCPU();
}

// ═══════════════════════════════════════════════════════════════════
// 60Hz Timer for turbo9sim
// ═══════════════════════════════════════════════════════════════════
struct repeating_timer Timer60HzData;
bool IN_RAM Timer60HzCallback(repeating_timer_t* rt) {
  Engine::Turbo9sim_SetTimerFired();  // Sets sim_status_reg bit; foreground polls it
  return true;
}

// ═══════════════════════════════════════════════════════════════════
// Flash speed adjustment (for overclocking > 150 MHz)
// ═══════════════════════════════════════════════════════════════════
void IN_RAM safe_adjust_flash_speed() {
#if MHz > 150
  uint32_t ints = save_and_disable_interrupts();
  const uint32_t SAFE = 4;
  uint32_t clkdiv = SAFE;
  uint32_t rxdelay = 4;
  hw_write_masked(
      &qmi_hw->m[0].timing,
      ((clkdiv << QMI_M0_TIMING_CLKDIV_LSB) & QMI_M0_TIMING_CLKDIV_BITS) |
          ((rxdelay << QMI_M0_TIMING_RXDELAY_LSB) & QMI_M0_TIMING_RXDELAY_BITS),
      QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
  restore_interrupts(ints);
#endif
}

// ═══════════════════════════════════════════════════════════════════
// Tcl REPL coroutine (runs on core 0 before 6309 starts)
// ═══════════════════════════════════════════════════════════════════
static uint8_t tcl_stack[20 * 1024];  // 20K for Tcl REPL + VFS RPC
volatile bool tcl_repl_done = false;

static void tcl_repl_task(Coro& self) {
  rpc::g_vfs_coro = &self;  // VFS RPC yields back to scheduler
  gspoon::g_spoon_coro = &self;  // ls_cmd etc. call coro_yield(g_spoon_coro)
  cobs_printf("TFR911 TCL SHELL\n");

  char line[256];
  while (true) {
    cobs_printf("> ");
    int pos = 0;
    // Read a line from USB (yield while waiting for keys)
    while (true) {
      // Check for injected commands from PicoRPC
      if (!g_pending_injections.empty()) {
        pcb::RpcRequest req = g_pending_injections.front();
        g_pending_injections.erase(g_pending_injections.begin());
        std::string cmd = req.data;
        cobs_printf("[Injecting: %s]\n", cmd.c_str());
        int rc = Tcl_Eval(global_tcl_interp, (char*)cmd.c_str(), 0, (char**)0);
        pcb::RpcResponse resp;
        resp.serial = req.serial;
        resp.status = rc;
        const char* out = global_tcl_interp->result;
        if (out && out[0]) resp.message = out;
        pico_rpc::send_response(resp);
        if (rc == TCL_BYE) goto BYE;
        cobs_printf("> ");
        continue;
      }

      // Poll for keyboard input from USB packets
      std::string* pkt = usb_packet_buf.Yoink([](std::string* s) {
        return s && s->length() > 0 &&
               (unsigned char)(*s)[0] != T_RPC &&
               (unsigned char)(*s)[0] != T_PICO_RPC;
      });
      if (pkt) {
        byte ch = (byte)(*pkt)[0];
#if DEBUG_TCL_REPL
        cobs_printf("[key: cmd=%d len=%d", ch, (int)pkt->size());
        if (ch == C_PUTCHAR && pkt->size() >= 2)
          cobs_printf(" val=%d", (byte)(*pkt)[1]);
        cobs_printf("]\n");
#endif
        if (ch == C_PUTCHAR && pkt->size() >= 2) ch = (byte)(*pkt)[1];
        delete pkt;
        if (ch == 13 || ch == 10) break;  // End of line
        if (ch == 8 || ch == 127) {  // Backspace
          if (pos > 0) { pos--; cobs_printf("\x08 \x08"); }
          continue;
        }
        if (ch > 127) {
#if DEBUG_TCL_REPL
          cobs_printf("[skip non-ASCII %d]\n", ch);
#endif
          continue;  // Skip non-ASCII (like the ² superscript)
        }
        if (pos < 255) {
          line[pos++] = ch;
          cobs_putchar(ch);
        }
        continue;
      }
      coro_yield(&self);  // Nothing to do — yield to let PumpUsbCobs run
    }
    line[pos] = 0;
    cobs_putchar('\n');

    if (pos == 0) continue;

    int result = Tcl_Eval(global_tcl_interp, line, 0, (char**)0);
    const char* output = global_tcl_interp->result;
    if (output && output[0]) {
      if (result == TCL_ERROR) cobs_putchar('?');
      tcl_io::emit_string(output);
      cobs_putchar('\n');
    }
    if (result == TCL_BYE) break;
  }

BYE:
  cobs_printf("[BYE — starting 6309]\n");
  tcl_repl_done = true;
}

// ═══════════════════════════════════════════════════════════════════
// Background loop (core 0) — two-phase lifecycle
// Phase 1: Tcl REPL (6309 idle, core 1 not launched)
// Phase 2: Bus operation (6309 running, FIFO drain + USB I/O)
// ═══════════════════════════════════════════════════════════════════
void IN_RAM tfr911_background() {
  // ── Phase 1: Tcl REPL ──────────────────────────────────────────
  // 6309 is not running.  We can write ram[] directly from Tcl.
  init_lfs();
  global_tcl_interp = Tcl_CreateInterp();
  register_tcl_commands(global_tcl_interp);

  Coro tcl_repl;
  coro_create(&tcl_repl, tcl_repl_task, tcl_stack, sizeof(tcl_stack));

  while (!tcl_repl_done) {
    coro_resume(&tcl_repl);
    // Always pump USB — not just when HasWork — to ensure bytes flow
    // from USB → raw_buf → cobs_decoder → packet_buf.
    usb_receiver.Tick();
    cobs_decoder.Tick();
    RpcEvaluator::Tick();
    PicoRpcEvaluator::Tick();
#if DEBUG_TCL_REPL
    {
      static int pump_count = 0;
      pump_count++;
      if ((pump_count & 0xFFFFF) == 0) {
        cobs_printf("[repl: raw=%d pkt=%d pump=%d]\n",
                    usb_raw_buf.NumBuffered(),
                    usb_packet_buf.NumBuffered(),
                    pump_count >> 20);
      }
    }
#endif
  }

  // ── Transition: Reset 6309, launch foreground on core 1 ──────
  //
  // The 6309 is CMOS but not fully static — it needs continuous E/Q
  // clocks or it loses state.  During the Tcl REPL, no clocks were
  // generated (PIO was idle at pull-block).  So we must do the reset
  // WITH the PIO/foreground running:
  //
  //   1. Assert RESET + HALT (open-drain) from background
  //   2. Re-init PIO and launch foreground on core 1
  //   3. Foreground generates E/Q via PIO; 6309 sees IDLE cycles
  //   4. Background waits for foreground to be running
  //   5. Hold RESET for ~10ms (~28K E cycles, well above the 8-cycle minimum)
  //   6. Release RESET (still HALTed)
  //   7. Wait ~5ms for 6309 to see RESET de-assert
  //   8. Release HALT → 6309 fetches reset vector (0xFFFE/0xFFFF)
  //
  cobs_printf("[bye: asserting RESET+HALT...]\n");
  gpio_set_dir(RESET, GPIO_OUT);  // Assert RESET (open-drain, latch=0)
  gpio_set_dir(HALT, GPIO_OUT);   // Assert HALT  (open-drain, latch=0)

  // Re-init PIO — the SM was sitting at pull-block since main().
  pio_sm_set_enabled(pio0, 0, false);
  pio_sm_restart(pio0, 0);
  {
    pio_clear_instruction_memory(pio0);
    const uint offset = pio_add_program(pio0, &t911veryfast_program);
    t911veryfast_program_init(pio0, 0, offset);
  }

  // Launch foreground on core 1.
  // Install_OS loads ROM + vectors into ram[], then enters the inner loop.
  // The inner loop feeds the PIO → E/Q start cycling → 6309 gets clocked.
  // During HALT+RESET, the 6309 just runs IDLE cycles (addr=0xFFFF).
  foreground_running = false;
  cobs_printf("[bye: launching core1...]\n");
  multicore_launch_core1(Engine__RunCPU);

  // Wait for foreground to reach the inner loop (E/Q now cycling).
  // Drain fg2bg FIFO during the wait — Install_OS pushes ~34K ROM
  // writes through the FIFO, and we need to drain them as
  // C_RAM2_WRITE packets so the tether sees the ROM contents.
  while (!foreground_running) {
    uint chore = 0;
    while (fg2bg.pop(chore)) {
      uint chore_num = chore >> 24;
      byte chore_byte = chore & 0xFF;
      switch (chore_num) {
        case FG2BG_PUTCHAR:
          if (chore_byte) putbyte(chore_byte);
          break;
        case FG2BG_WRITE: {
          unsigned char pkt[4] = {C_RAM2_WRITE,
              (unsigned char)(chore >> 16),
              (unsigned char)(chore >> 8),
              (unsigned char)chore};
          CobsEncodeAndTransmit(pkt, 4, [](int ch) { putchar_raw(ch); });
          break;
        }
        default:
          break;
      }
    }
    sleep_ms(1);
  }
  cobs_printf("[bye: foreground running, holding RESET...]\n");

  // Hold RESET for ~10ms (~28K E cycles at ~2.88 Mcps).
  // 6309 datasheet requires ≥8 E cycles; this is very conservative.
  sleep_ms(10);

  // Release RESET (6309 still HALTed)
  gpio_set_dir(RESET, GPIO_IN);  // Release RESET (floats high via pull-up)
  cobs_printf("[bye: RESET released, holding HALT...]\n");

  // Wait for 6309 to see RESET de-assert and prepare internally
  sleep_ms(5);

  // Release HALT → 6309 fetches reset vector and starts executing
  gpio_set_dir(HALT, GPIO_IN);  // Release HALT (floats high via pull-up)
  cobs_printf("[bye: HALT released, 6309 booting]\n");

  alarm_pool_init_default();
#if SPEED_STATS
  add_repeating_timer_us(1000, TimerCallback, nullptr, &TimerData);
#endif

#if TRACE
  // Once very 5 seconds, when we are slowed down for TRACE.
  add_repeating_timer_ms(5000, Timer60HzCallback, nullptr, &Timer60HzData);
#else
  // 60Hz otherwise.
  add_repeating_timer_us(16667, Timer60HzCallback, nullptr, &Timer60HzData);
#endif
  cobs_printf("[bye: Phase 2 started]\n");

  // ── Phase 2: Normal bus operation ──────────────────────────────
#if SPEED_STATS
  int bg_mega_cycles = 0;
#endif
#if DEBUG_TCL_REPL
  int phase2_loops = 0;
  int phase2_chores = 0;
#endif

  while (true) {
#if DEBUG_TCL_REPL
    phase2_loops++;
    if ((phase2_loops & 0xFFFFF) == 0) {
      cobs_printf("[ph2: loops=%dM chores=%d fifo=%d cy=%u halt=%d step=%d]\n",
                  phase2_loops >> 20, phase2_chores, fg2bg.size(),
                  (unsigned)cycles, (int)fg_halt_for_flow_control,
                  (int)fg_inner_step);
    }
#endif
    // Drain fg2bg FIFO — handle events pushed by foreground.
    // Interleave PumpUsbCobs and PollTermInput every 256 drained
    // events to prevent USB input starvation while still draining
    // as fast as possible (foreground blocking pushes need prompt drain).
    uint chore = 0;
    int drain_count = 0;
    while (fg2bg.pop(chore)) {
      uint chore_num = chore >> 24;
      byte chore_byte = chore & 0xFF;
#if DEBUG_TCL_REPL
      phase2_chores++;
#endif
      switch (chore_num) {
        case FG2BG_PUTCHAR:
          if (chore_byte) putbyte(chore_byte);
          break;
#if SPEED_STATS
        case FG2BG_MEGA_CYCLE:
          bg_mega_cycles++;
          if (bg_mega_cycles >= 20) {
            if (milliseconds > 0) {
              cobs_printf("[Mc=%g  s=%g  Mcps=%g]",
                          double(cycles) / double(1000 * 1000),
                          double(milliseconds) / double(1000),
                          (double)cycles / double(1000 * milliseconds));
            }
            bg_mega_cycles = 0;
          }
          break;
#endif
#if TRACE
        case FG2BG_READ: {
          unsigned char pkt[4] = {C_RAM2_READ,
              (unsigned char)(chore >> 16),
              (unsigned char)(chore >> 8),
              (unsigned char)chore};
          CobsEncodeAndTransmit(pkt, 4, [](int ch) { putchar_raw(ch); });
          break;
        }
        case FG2BG_WRITE: {
          unsigned char pkt[4] = {C_RAM2_WRITE,
              (unsigned char)(chore >> 16),
              (unsigned char)(chore >> 8),
              (unsigned char)chore};
          CobsEncodeAndTransmit(pkt, 4, [](int ch) { putchar_raw(ch); });
          break;
        }
        case FG2BG_FIC: {
          unsigned char pkt[4] = {C_FIC_CYCLE,
              (unsigned char)(chore >> 16),
              (unsigned char)(chore >> 8),
              (unsigned char)chore};
          CobsEncodeAndTransmit(pkt, 4, [](int ch) { putchar_raw(ch); });
          break;
        }
#endif
        default:
          break;
      }

      // Every 256 drained events, service USB and terminal input
      // so the 6309 can receive keystrokes even during heavy tracing.
      drain_count++;
      if ((drain_count & 0xFF) == 0) {
        if (PumpUsbCobsHasWork()) PumpUsbCobs();
        PollTermInput();
        if (Engine::Turbo9sim_CanRx()) {
          if (term_input.NumBuffered() > 0) {
            Engine::Turbo9sim_SetRx(term_input.Take());
          }
        }
      }
    }

    // Also service USB/terminal when FIFO is empty (idle periods)
    if (PumpUsbCobsHasWork()) PumpUsbCobs();
    PollTermInput();
    if (Engine::Turbo9sim_CanRx()) {
      if (term_input.NumBuffered() > 0) {
        byte ch = term_input.Take();
        Engine::Turbo9sim_SetRx(ch);
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// main()
// ═══════════════════════════════════════════════════════════════════
int main() {
#if MHz != 150
  set_sys_clock_khz(MHz * 1000, true);
#endif
  safe_adjust_flash_speed();
  stdio_usb_init();
  uint directions = InitializePinsReturnDirections();

  for (int i = 0; i < 3; i++) {
    gpio_put(LED, 1);
    sleep_ms(200);
    gpio_put(LED, 0);
    sleep_ms(200);
    cobs_printf(":%d:\n", i);
  }

  FlashLabel::InitLabel();
  FlashLabel::PrintLabel();

  // Set up PIO for the 6309 bus engine (but don't start the 6309 yet)
  pio_clear_instruction_memory(pio0);
  const uint offset_t911 = pio_add_program(pio0, &t911veryfast_program);
  t911veryfast_program_init(pio0, 0, offset_t911);

  cobs_printf(":g:\n");
  Engine::Turbo9sim_Install(0xFF00);

  // Override turbo9sim's flash-resident lambda IOReaders/IOWriters with
  // IN_RAM versions. This prevents XIP stalls during PIO bus cycles.
  IOReaders[0x00] = acia_read_tx;
  IOReaders[0x01] = acia_read_rx;
  IOReaders[0x02] = acia_read_status;
  IOReaders[0x03] = acia_read_control;
  IOWriters[0x00] = acia_write_tx;
  IOWriters[0x01] = acia_write_rx;
  IOWriters[0x02] = acia_write_status;
  IOWriters[0x03] = acia_write_control;

  // DON'T launch core 1 here — the Tcl REPL runs first on core 0.
  // After Tcl says "bye", tfr911_background() resets the 6309 and
  // launches the foreground on core 1.
  tfr911_background();  // Never returns
}
