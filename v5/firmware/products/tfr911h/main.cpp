// v4_tfr911h_main.cpp — TFR911H v4 build with Tcl Shell + LittleFS + VFS.
//
// Self-contained compilation unit that produces tfr911h_v4.uf2.
// Includes TFR911 headers from tmanager911/very-turbos/ directly.
// Boots into Tcl REPL on background (core 0).  When the user says
// `bye`, the 6309 is reset and the foreground PIO bus loop starts.
//
// Based on: v3/tmanager911/very-turbos/veryturbos.cpp (510 lines)

#define TRY_AX_ROM 0
#define JUST_WAIT_DONT_HALT 1
#define OCCASIONAL_HALTING 0
#define TRACE 0
#define SPEED_STATS 0
// TransmitWrite now routes through fg2bg FIFO — safe from core 1.
#define HALT_IS_OPEN_DRAIN 1
#define CLOCK_IRQ 1             // 0 to disable 60Hz timer IRQ during debugging
#define DUMP_FIRST_CYCLES 0    // Log the first N bus cycles after boot for debugging
#define MHz 250  // clock speed

constexpr unsigned GROUP_SIZE = 50; // 100; // 10000;

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
#include "lfs-centipede.h"
#include "littlefs/lfs.h"
#include "littlefs/lfs_util.h"
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

#ifndef DEEP_256K
#define DEEP_256K 1
#endif

// COBS encoder/decoder (shared with Centipede)
#include "cobs.h"
#include "cross-core.h"

// Console stubs for TFR911 (must come before tcl_commands.h / tcl_io.h)
// Block the real console.h from loading by defining its include guard.
#define FIRMWARE_CONSOLE_H_
// Block the real gspoon.h — editor.h includes it, but TFR911 doesn't
// need the full Centipede spoon feeder.  We provide gspoon::g_spoon_coro
// and gspoon::SleepMillis as stubs below.
#define _GSPOON_H_
#include "v4_console_tfr911_stub.h"

// Tcl interpreter
#include "tcl6.7c/tcl.h"
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
CrossCoreFIFO<uint, 1024> fg2bg;        // Events: trace logs, MEGA_CYCLE, etc. (watermark is 50)
CrossCoreFIFO<byte, 8192> fg2bg_chars;  // Characters: SAY output (high priority)

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
  FG2BG_EMUDSK = 13,  // EmuDsk sector transfer request to background
  FG2BG_GEP9_DISK = 14, // gep9 Hatvan disk sector transfer request
  FG2BG_MC_SD = 15,   // multicomp09 SPI SD sector transfer request
};

// Say: push a console character to the high-priority chars FIFO.
// Spin-waits if full (8192 slots — should never fill in practice).
FORCE_INLINE void IN_RAM Say(byte c) {
  while (!fg2bg_chars.push(c)) { tight_loop_contents(); }
}
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
#if DEEP_256K
byte ram[256 * 1024]; // 256 KB physical RAM (32 blocks of 8 KB each)
#else
byte ram[128 * 1024]; // 128 KB physical RAM (16 blocks of 8 KB each)
#endif

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
#include "cobs_tx.h"
#include "usb_pipeline.h"

// ShowChar — called by the old turbo9sim.h (unqualified) from simTxWriter.
// Pushes to fg2bg FIFO; background drains and sends over USB.
// (CoreEngine::ShowChar in v4_core_engine.h is a separate CRTP method
// used only by Centipede — TFR911 Engine does not inherit from CoreEngine.)
FORCE_INLINE void IN_RAM ShowChar(char c) {
  Say((byte)c);
}

FORCE_INLINE void IN_RAM putbyte(byte x) {
  unsigned char pkt[2] = {C_PUTCHAR, x};
  CobsEncodeAndTransmit(pkt, 2, [](int ch) { putchar_raw(ch); });
}

FORCE_INLINE void IN_RAM TransmitMessage(byte messtype, uint sz, const byte* buf) {
  byte pkt[sz + 1];
  pkt[0] = messtype;
  memcpy(pkt + 1, buf, sz);
  CobsEncodeAndTransmit(pkt, sz + 1, [](int ch) { putchar_raw(ch); });
}

FORCE_INLINE void IN_RAM TransmitCycle(uint cy, byte flags, byte kind, byte data, uint addr) {
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

// ═══════════════════════════════════════════════════════════════════
// CircBuf + USB/COBS pipeline (PumpUsbCobs compatible)
// ═══════════════════════════════════════════════════════════════════
#include "circbuf.h"

// PIO header (generated by cmake)
#include "hamster.pio.h"

// Flash label
#include "flash-label.h"

// USB pipeline buffers — same names as Centipede for usb_pipeline.h compat
CircBuf<unsigned char, 1024> usb_raw_buf;
CircBuf<std::string*, 64> usb_packet_buf;
UsbReceiver usb_receiver(usb_raw_buf);
CobsDecoder<1024, 64> cobs_decoder(usb_raw_buf, usb_packet_buf);

// Terminal input buffer (chars from USB → turbo9sim RX)
CircBuf<unsigned char, 1024> term_input;

// ═══════════════════════════════════════════════════════════════════
// ACIA simulation state
// ═══════════════════════════════════════════════════════════════════
bool acia_irq_enabled;
bool acia_irq_firing;
bool acia_char_in_ready;
int acia_char;
bool irq_needed;
bool prev_irq_needed;

// ═══════════════════════════════════════════════════════════════════
// Include TFR911 headers from v1
// ═══════════════════════════════════════════════════════════════════
#include "turbo9sim.h"
#include "romlist.h"

// ═══════════════════════════════════════════════════════════════════
// Coroutines, VFS, Tcl commands, PicoRPC
// ═══════════════════════════════════════════════════════════════════
#include "coro.h"

// Abort handler
#include "abort.h"
#include "oom_abort.h"   // new/abort() failures print "*** ABORT" (spec S49)

// PCB (protobuf-like encoding for RPC)
#include "pcb.h"

// VFS RPC (must come before tcl_io/vfs/tcl_commands)
#include "vfs_rpc.h"

// Forward-define T_RPC, T_PICO_RPC, and T_WALLTIME before tcl_io.h needs them
#ifndef T_RPC
#define T_RPC 180
#endif
#ifndef T_PICO_RPC
#define T_PICO_RPC 181
#endif
#ifndef T_WALLTIME
#define T_WALLTIME 182
#endif

// Tcl I/O — TFR911 uses USB only (no CoCo2 screen)
#include "tcl_io.h"

// VFS (file system overlay)
#include "vfs.h"

// RTC — provides get_system_time() needed by SleepMillis
#include "rtc.h"

// gspoon namespace stub — only g_spoon_coro and SleepMillis are needed.
// TFR911 doesn't use the full gspoon (no CoCo2 screen/keyboard).
namespace gspoon {
  Coro* g_spoon_coro = nullptr;

  void SleepMillis(Coro* c, uint64_t ms) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    coro_yield(c);
    while ((uint32_t)(to_ms_since_boot(get_absolute_time()) - start) < ms) {
      coro_yield(c);
    }
  }
}  // namespace gspoon

// LittleFS Tcl commands (ls, cp, cat, etc.)
#include "littlefs.h"

// Globals needed by pico_rpc.h and tcl_commands.h
uint32_t __uninitialized_ram(boot_mode);
uint32_t __uninitialized_ram(boot_mode_check);
#define BOOT_MODE_CHECKER 0x56781234u
bool startup_e_clock_detected = false;

// Stub for menu.h — TFR911 has no floppy config.
inline void set_floppy_names() {}

// Tcl commands (all built-in shell commands)
#include "tcl_commands.h"

// PicoRPC (inject commands from PC)
#include "pico_rpc.h"

#define TCL_BYE 9

// ═══════════════════════════════════════════════════════════════════
// USB packet dispatch — delivers typed chars to term_input
// ═══════════════════════════════════════════════════════════════════
static bool g_term_acia_arrow_keys = false;
inline void turbolab_on_sigint();

void PollTermInput() {
  // Take non-RPC, non-PicoRPC, non-WallTime packets from usb_packet_buf as terminal input
  while (true) {
    std::string* pkt = usb_packet_buf.Yoink([](std::string* s) {
      return s && s->length() > 0 &&
             (unsigned char)(*s)[0] != T_RPC &&
             (unsigned char)(*s)[0] != T_PICO_RPC &&
             (unsigned char)(*s)[0] != T_WALLTIME;
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
      case 183:  // T_SIGINT (host Ctrl-C interrupt for turbolab)
        turbolab_on_sigint();
        break;
      default:
        if (cmd >= 1 && cmd <= 127) {
          byte c = cmd;
          if (c == 10) c = 13;
          term_input.Put(c);
        } else if (g_term_acia_arrow_keys && cmd >= 128 && cmd <= 133) {
          term_input.Put(cmd);
        }
        break;
    }
    delete pkt;
  }
}

#include "io_install.h"   // IoReset/IoClaim/IoSet: how devices fill the I/O dispatch tables
#include "acia_tfr.h"   // TFR911 ACIA 6850 IN_RAM callbacks (term port)
#include "gep9_mmu.h"
#include "gep9_io.h"
#include "v4_multicomp09.h"

#include "deep_mmu.h"   // Deep65280 MMU state and address translation
#include "emudsk.h"   // EMUDSK block-device register model ($FF80..$FF86)
#include "gime.h"   // CoCo 3 GIME control/IRQ/shadow registers ($FF90..$FF9F) & palette ($FFB0..$FFBF)
#include "rtc_port.h"   // RTC 8-register clock port + T_WALLTIME receiver
#include "audio_sn76489.h"   // SN76489 sound generator + keyclick/bell/beep PWM mixer
#include "orchestra90.h"   // Orchestra-90 stereo PWM DAC
#include "becker.h"   // DriveWire / FujiNet Becker port over UART0
#include "info_port.h"   // Firmware diagnostic report port
#include "enhancements.h"   // Enhancement installer, reset, background service, Tcl commands
#include "clock_stop.h"   // CLOCK_AND_STOP register (60 Hz clock enable, stop/exit command)
#include "acia_term.h"   // terminal ACIA 6850 ($FF88/$FF89 or $FF14/$FF15) for NitrOS-9 engines
#include "turbo9os.h"   // Engine turbo9os: built-in Turbo9OS ROM + setup
#include "flat65280.h"   // Engine flat65280: NitrOS-9 Level 1 I/O and setup
#include "deep65280.h"   // Engine deep65280: NitrOS-9 Level 2 I/O and setup
#include "gep9.h"   // Engine gep9: Hatvan 6309 support
#include "multicomp09.h"   // Engine multicomp09: Fuzix 6809 support
#include "pins.h"   // TFR911H GPIO initialization
// ═══════════════════════════════════════════════════════════════════
// Timer (1ms tick on core 0)
// ═══════════════════════════════════════════════════════════════════
#if SPEED_STATS
uint milliseconds;
volatile uint64_t cycles;  // Updated by foreground, read by background for stats
volatile uint64_t fg_idle_cycles;   // Cycles where addr==0xFFFF (HALT-induced)
volatile uint64_t fg_active_cycles; // Cycles where addr!=0xFFFF (real execution)
struct repeating_timer TimerData;
bool TimerCallback(repeating_timer_t* rt) {
  milliseconds++;
  return true;
}
#endif

#include "guts.h"   // Hamster policy-template engines (4 IN_RAM core-1 foreground loops)
#include "timers.h"   // 60 Hz timer callback and flash-speed adjustment
#include "repl.h"   // Tcl REPL coroutine (core 0, before the 6309 starts)
#include "drain.h"   // USB drain coroutines (characters and events)
#include "turbolab.h"   // Engine turbolab: 4-phase cycle tracer, watchpoints & SWI register dump
#include "registry.h"   // (run_os, for_hw) target launchers
#include "ports_cmd.h"   // `on` / `run`, `ports list|check`, and the checked `bye`
#include "background.h"   // periodic_status and the background loop (core 0 scheduler)
// ═══════════════════════════════════════════════════════════════════
// main()
// ═══════════════════════════════════════════════════════════════════
int main() {
#if MHz != 150
  set_sys_clock_khz(MHz * 1000, true);
#endif
  safe_adjust_flash_speed();
  stdio_usb_init();
  InitializePinsReturnDirections();

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
  const uint offset_t911 = pio_add_program(pio0, &hamster_program);
  hamster_program_init(pio0, 0, offset_t911);

  cobs_printf(":g:\n");
  Install_Turbo9os_IO();   // (installed again, and checked against the plan, when `bye` launches turbo9os)

  // DON'T launch core 1 here — the Tcl REPL runs first on core 0.
  // After Tcl says "bye", tfr911_background() resets the 6309 and
  // launches the foreground on core 1.
  tfr911_background();  // Never returns
}
