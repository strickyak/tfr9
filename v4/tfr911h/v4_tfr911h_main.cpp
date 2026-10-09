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
#include "../firmware/one/littlefs/lfs-centipede.h"
#include "../firmware/one/littlefs/lfs.h"
#include "../firmware/one/littlefs/lfs_util.h"
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
#include "../firmware/one/firmware/cross-core.h"

// Console stubs for TFR911 (must come before tcl_commands.h / tcl_io.h)
// Block the real console.h from loading by defining its include guard.
#define FIRMWARE_CONSOLE_H_
// Block the real gspoon.h — editor.h includes it, but TFR911 doesn't
// need the full Centipede spoon feeder.  We provide gspoon::g_spoon_coro
// and gspoon::SleepMillis as stubs below.
#define _GSPOON_H_
#include "v4_console_tfr911_stub.h"

// Tcl interpreter
#include "../firmware/one/tcl6.7c/tcl.h"
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
CrossCoreFIFO<uint, 8192> fg2bg;        // Events: trace logs, MEGA_CYCLE, etc.
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
#include "../firmware/one/firmware/cobs_tx.h"
#include "../firmware/one/firmware/usb_pipeline.h"

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

FORCE_INLINE void IN_RAM TransmitWrite(uint addr, byte data) {
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
#include "../firmware/one/firmware/coro.h"

// Abort handler
#include "../firmware/one/firmware/abort.h"

// PCB (protobuf-like encoding for RPC)
#include "../firmware/one/firmware/pcb.h"

// VFS RPC (must come before tcl_io/vfs/tcl_commands)
#include "../firmware/one/firmware/vfs_rpc.h"

// Forward-define T_RPC and T_PICO_RPC before tcl_io.h needs them
#ifndef T_RPC
#define T_RPC 180
#endif
#ifndef T_PICO_RPC
#define T_PICO_RPC 181
#endif

// Tcl I/O — TFR911 uses USB only (no CoCo2 screen)
#include "../firmware/one/firmware/tcl_io.h"

// VFS (file system overlay)
#include "../firmware/one/firmware/vfs.h"

// RTC — provides get_system_time() needed by SleepMillis
#include "../firmware/one/firmware/rtc.h"

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
#include "../firmware/one/firmware/littlefs.h"

// Globals needed by pico_rpc.h and tcl_commands.h
uint32_t __uninitialized_ram(boot_mode);
uint32_t __uninitialized_ram(boot_mode_check);
#define BOOT_MODE_CHECKER 0x56781234u
bool startup_e_clock_detected = false;

// Stub for menu.h — TFR911 has no floppy config.
inline void set_floppy_names() {}

// Tcl commands (all built-in shell commands)
#include "../firmware/one/firmware/tcl_commands.h"

// PicoRPC (inject commands from PC)
#include "../firmware/one/firmware/pico_rpc.h"

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
  Say(data);  // Push to high-priority chars FIFO
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
#include "turbo9os.h"
#include "v4_gep9_mmu.h"
#include "v4_gep9_io.h"

// ═══════════════════════════════════════════════════════════════════
// Flat65280 Engine Support
// ═══════════════════════════════════════════════════════════════════

const byte flat65280_img_data[65536] = {
#include "flat65280.img.h"
};

enum EngineType {
  ENGINE_TURBO9OS = 0,
  ENGINE_FLAT65280 = 1,
  ENGINE_DEEP65280 = 2,
  ENGINE_GEP9 = 3,
  ENGINE_GEP9BIG = 4,
};
static EngineType current_engine = ENGINE_TURBO9OS;

// EMUDSK state ($FF80..$FF86)
struct EmuDskRequest {
  uint8_t drive;
  uint32_t lsn;
  uint16_t buffer_addr;
  uint8_t command;
  std::atomic<int8_t> status;
};
static EmuDskRequest g_emudsk_req;
static byte emudsk_lsn[3] = {0, 0, 0};
static byte emudsk_buffer[2] = {0, 0};
static byte emudsk_drive = 0;
static byte emudsk_status = 0;

static std::shared_ptr<VfsNode> g_drive_nodes[4] = {nullptr, nullptr, nullptr, nullptr};

// CLOCK_AND_STOP state ($FF87)
static volatile byte clock_and_stop_reg = 0;
static uint32_t flat65280_timer_ticks = 0;

// ACIA 6850 state ($FF88..$FF89)
static volatile byte flat65280_acia_ctrl = 0;
static volatile byte flat65280_rx_char = 0;
static volatile bool flat65280_rx_has_char = false;

// IN_RAM EMUDSK callbacks
byte IN_RAM emudsk_read_lsn0(uint) { return emudsk_lsn[0]; }
byte IN_RAM emudsk_read_lsn1(uint) { return emudsk_lsn[1]; }
byte IN_RAM emudsk_read_lsn2(uint) { return emudsk_lsn[2]; }
byte IN_RAM emudsk_read_cmd(uint)  { return emudsk_status; }
byte IN_RAM emudsk_read_buf0(uint) { return emudsk_buffer[0]; }
byte IN_RAM emudsk_read_buf1(uint) { return emudsk_buffer[1]; }
byte IN_RAM emudsk_read_drv(uint)  { return emudsk_drive; }

void IN_RAM emudsk_write_lsn0(uint, byte d) { emudsk_lsn[0] = d; }
void IN_RAM emudsk_write_lsn1(uint, byte d) { emudsk_lsn[1] = d; }
void IN_RAM emudsk_write_lsn2(uint, byte d) { emudsk_lsn[2] = d; }
void IN_RAM emudsk_write_buf0(uint, byte d) { emudsk_buffer[0] = d; }
void IN_RAM emudsk_write_buf1(uint, byte d) { emudsk_buffer[1] = d; }
void IN_RAM emudsk_write_drv(uint, byte d)  { emudsk_drive = d; }

void IN_RAM emudsk_write_command(uint, byte data) {
  g_emudsk_req.drive = emudsk_drive;
  g_emudsk_req.lsn = ((uint32_t)emudsk_lsn[0] << 16) |
                     ((uint32_t)emudsk_lsn[1] << 8) |
                     (uint32_t)emudsk_lsn[2];
  g_emudsk_req.buffer_addr = ((uint16_t)emudsk_buffer[0] << 8) |
                             (uint16_t)emudsk_buffer[1];
  g_emudsk_req.command = data;
  g_emudsk_req.status.store(-1, std::memory_order_release);

  uint pb_w = ((uint)FG2BG_EMUDSK << 24);
  while (!fg2bg.push(pb_w)) { tight_loop_contents(); }

  while (g_emudsk_req.status.load(std::memory_order_acquire) < 0) {
    tight_loop_contents();
  }
  emudsk_status = (byte)g_emudsk_req.status.load(std::memory_order_acquire);
}

// ═══════════════════════════════════════════════════════════════════
// Deep65280 MMU State & Translation
// ═══════════════════════════════════════════════════════════════════
static uint8_t deep65280_mmu[2][8];
static uint8_t deep65280_active_task = 0;
static uint32_t task_mmu_base[8];

static inline void IN_RAM update_task_mmu() {
  for (int s = 0; s < 8; s++) {
#if DEEP_256K
    // 256 KB physical RAM: 32 blocks ($00..$1F). MMU pages $00..$1F shadow $20..$3F.
    task_mmu_base[s] = (uint32_t)(deep65280_mmu[deep65280_active_task][s] & 0x1F) << 13;
#else
    // 128 KB physical RAM: 16 blocks ($00..$0F).
    task_mmu_base[s] = (uint32_t)(deep65280_mmu[deep65280_active_task][s] & 0x0F) << 13;
#endif
  }
}

static inline uint32_t IN_RAM translate_deep_addr(uint16_t logical) {
  if (logical >= 0xFE00 && logical < 0xFF00) {
    // Locked trampoline page ($FE00..$FEFF) hardwired to physical block $3F:
    // Block 15 (0x1E000) for 128KB, Block 31 (0x3E000) for 256KB
#if DEEP_256K
    return 0x3E000 | (logical & 0x1FFF);
#else
    return 0x1E000 | (logical & 0x1FFF);
#endif
  }
  return task_mmu_base[logical >> 13] | (logical & 0x1FFF);
}

// IN_RAM CLOCK_AND_STOP callbacks
byte IN_RAM clock_and_stop_reader(uint) {
  return clock_and_stop_reg;
}

void IN_RAM clock_and_stop_writer(uint, byte data) {
  if (data == 0xFC) {
    cobs_printf("\n *** Exiting with status 0 due to CLOCK_AND_STOP command $FC.\n");
    cobs_shutdown(0);
    stdio_flush();
    sleep_ms(100);
    rp2350_reset_standard();
  } else if (data == 0xFD) {
    cobs_printf("\n *** Exiting with status 1 due to CLOCK_AND_STOP command $FD.\n");
    cobs_shutdown(1);
    stdio_flush();
    sleep_ms(100);
    rp2350_reset_standard();
  } else if (data == 0xFE) {
    cobs_printf("\n *** PANIC due to CLOCK_AND_STOP command $FE.\n");
    // Stream 64KB memory dump in 256 chunks of 256 bytes
    uint8_t dump_pkt[258];
    dump_pkt[0] = 189; // C_CORE_DUMP
    for (int chunk = 0; chunk < 256; chunk++) {
      dump_pkt[1] = (uint8_t)chunk;
      if (current_engine == ENGINE_DEEP65280) {
        uint32_t phys = translate_deep_addr(chunk * 256);
        memcpy(&dump_pkt[2], &ram[phys], 256);
      } else {
        memcpy(&dump_pkt[2], &ram[chunk * 256], 256);
      }
      CobsEncodeAndTransmit(dump_pkt, sizeof(dump_pkt), [](int ch) { putchar_raw(ch); });
      stdio_flush();
      sleep_us(500);
    }
    cobs_shutdown(255);
    stdio_flush();
    sleep_ms(100);
    rp2350_reset_standard();
  } else {
    if (data & 0x01) {
      clock_and_stop_reg &= ~0x01; // W1C
    }
    clock_and_stop_reg = (clock_and_stop_reg & 0x01) | (data & 0xFE);
  }
}

// IN_RAM ACIA 6850 callbacks
byte IN_RAM flat65280_acia_read_status(uint) {
  byte s = 0x02; // TDRE is always 1 (transmit empty)
  if (flat65280_rx_has_char) {
    s |= 0x01; // RDRF = 1 (receive full)
    if (flat65280_acia_ctrl & 0x80) {
      s |= 0x80; // IRQ
    }
  }
  return s;
}

byte IN_RAM flat65280_acia_read_rx(uint) {
  flat65280_rx_has_char = false;
  return flat65280_rx_char;
}

void IN_RAM flat65280_acia_write_ctrl(uint, byte d) {
  flat65280_acia_ctrl = d;
}

void IN_RAM flat65280_acia_write_tx(uint, byte d) {
  Say(d);
}

// IN_RAM RAM vector reader for FFF0..FFFF
byte IN_RAM ram_vector_reader(uint addr) {
  return ram[0xFF00 | (addr & 0xFF)];
}

// IRQ check for flat65280
bool IN_RAM flat65280_irq_needed() {
  bool clock_irq = (clock_and_stop_reg & 0x01) && (clock_and_stop_reg & 0x02);
  bool acia_irq = ((flat65280_acia_ctrl & 0x80) != 0) && flat65280_rx_has_char;
  return clock_irq || acia_irq;
}

void Install_Flat65280_IO() {
  for (int i = 0; i < 256; i++) {
    IOReaders[i] = nullptr;
    IOWriters[i] = nullptr;
  }
  // EMUDSK
  IOReaders[0x80] = emudsk_read_lsn0;
  IOReaders[0x81] = emudsk_read_lsn1;
  IOReaders[0x82] = emudsk_read_lsn2;
  IOReaders[0x83] = emudsk_read_cmd;
  IOReaders[0x84] = emudsk_read_buf0;
  IOReaders[0x85] = emudsk_read_buf1;
  IOReaders[0x86] = emudsk_read_drv;

  IOWriters[0x80] = emudsk_write_lsn0;
  IOWriters[0x81] = emudsk_write_lsn1;
  IOWriters[0x82] = emudsk_write_lsn2;
  IOWriters[0x83] = emudsk_write_command;
  IOWriters[0x84] = emudsk_write_buf0;
  IOWriters[0x85] = emudsk_write_buf1;
  IOWriters[0x86] = emudsk_write_drv;

  // CLOCK_AND_STOP
  IOReaders[0x87] = clock_and_stop_reader;
  IOWriters[0x87] = clock_and_stop_writer;

  // ACIA 6850
  IOReaders[0x88] = flat65280_acia_read_status;
  IOReaders[0x89] = flat65280_acia_read_rx;
  IOWriters[0x88] = flat65280_acia_write_ctrl;
  IOWriters[0x89] = flat65280_acia_write_tx;

  // System Vectors (0xFFF0..0xFFFF)
  for (int i = 0xF0; i <= 0xFF; i++) {
    IOReaders[i] = ram_vector_reader;
  }
}

void setup_flat65280() {
  current_engine = ENGINE_FLAT65280;

  // 1. Open drive nodes if configured
  for (int d = 0; d < 2; d++) {
    const char* path = (d == 0) ? centipede_config.drive0 : centipede_config.drive1;
    if (path && path[0]) {
      g_drive_nodes[d] = vfs_resolve(path);
      if (g_drive_nodes[d]) {
        int err = g_drive_nodes[d]->open_file(LFS_O_RDWR);
        if (err < 0) {
          cobs_printf("[flat65280: drive %d open RDWR failed (err %d), trying RDONLY: %s]\n", d, err, path);
          err = g_drive_nodes[d]->open_file(LFS_O_RDONLY);
          if (err >= 0) {
            cobs_printf("[flat65280: opened drive %d read-only: %s]\n", d, path);
          } else {
            cobs_printf("[flat65280: drive %d open failed: %s]\n", d, path);
            g_drive_nodes[d] = nullptr;
          }
        } else {
          cobs_printf("[flat65280: opened drive %d: %s]\n", d, path);
        }
      } else {
        cobs_printf("[flat65280: could not resolve drive %d path: %s]\n", d, path);
      }
    } else {
      g_drive_nodes[d] = nullptr;
    }
  }

  // 2. Load flat65280 image into ram[]
  bool loaded_from_file = false;
  if (centipede_config.flat_img[0]) {
    auto img_node = vfs_resolve(centipede_config.flat_img);
    if (img_node) {
      int err = img_node->open_file(LFS_O_RDONLY);
      if (err >= 0) {
        cobs_printf("[flat65280: loading %s into RAM...]\n", centipede_config.flat_img);
        memset(ram, 0, sizeof(ram));
        lfs_ssize_t total = 0;
        while (total < 65536) {
          lfs_size_t chunk = 65536 - total;
          if (chunk > 256) chunk = 256;
          lfs_ssize_t n = img_node->read(&ram[total], chunk);
          if (n <= 0) break;
          total += n;
        }
        img_node->close_file(nullptr);
        cobs_printf("[flat65280: loaded %d bytes into RAM]\n", (int)total);
        loaded_from_file = true;
      } else {
        cobs_printf("[flat65280: failed to open %s (err %d), using embedded fallback]\n",
                    centipede_config.flat_img, err);
      }
    } else {
      cobs_printf("[flat65280: could not resolve %s, using embedded fallback]\n",
                  centipede_config.flat_img);
    }
  }

  if (!loaded_from_file) {
    cobs_printf("[flat65280: loading embedded flat65280.img into RAM...]\n");
    memset(ram, 0, sizeof(ram));
    memcpy(ram, flat65280_img_data, sizeof(flat65280_img_data));
    cobs_printf("[flat65280: embedded image loaded (%d bytes)]\n", (int)sizeof(flat65280_img_data));
  }

  // Copy vectors to vector_ram
  memcpy(vector_ram, &ram[0xFFF0], 16);

  // 3. Install IO handlers
  Install_Flat65280_IO();
}

// ═══════════════════════════════════════════════════════════════════
// Deep65280 (NitrOS-9 Level 2) IO & Setup
// ═══════════════════════════════════════════════════════════════════

byte IN_RAM deep65280_read_gime_init0(uint) {
  return 0;
}

byte IN_RAM deep65280_read_task(uint) {
  return deep65280_active_task;
}

void IN_RAM deep65280_write_task(uint, byte d) {
  deep65280_active_task = d & 1;
  update_task_mmu();
}

byte IN_RAM deep65280_read_mmu(uint addr) {
  uint reg = addr & 0x0F;
  uint task = (reg >> 3) & 1;
  uint slot = reg & 7;
  return deep65280_mmu[task][slot];
}

void IN_RAM deep65280_write_mmu(uint addr, byte d) {
  uint reg = addr & 0x0F;
  uint task = (reg >> 3) & 1;
  uint slot = reg & 7;
  deep65280_mmu[task][slot] = d;
  if (task == deep65280_active_task) {
#if DEEP_256K
    task_mmu_base[slot] = (uint32_t)(d & 0x1F) << 13;
#else
    task_mmu_base[slot] = (uint32_t)(d & 0x0F) << 13;
#endif
  }
}

byte IN_RAM deep65280_vector_reader(uint addr) {
  return vector_ram[addr & 0x0F];
}

void IN_RAM deep65280_vector_writer(uint addr, byte d) {
  vector_ram[addr & 0x0F] = d;
}

void Install_Deep65280_IO() {
  for (int i = 0; i < 256; i++) {
    IOReaders[i] = nullptr;
    IOWriters[i] = nullptr;
  }

  // EMUDSK ($FF80..$FF86)
  IOReaders[0x80] = emudsk_read_lsn0;
  IOReaders[0x81] = emudsk_read_lsn1;
  IOReaders[0x82] = emudsk_read_lsn2;
  IOReaders[0x83] = emudsk_read_cmd;
  IOReaders[0x84] = emudsk_read_buf0;
  IOReaders[0x85] = emudsk_read_buf1;
  IOReaders[0x86] = emudsk_read_drv;

  IOWriters[0x80] = emudsk_write_lsn0;
  IOWriters[0x81] = emudsk_write_lsn1;
  IOWriters[0x82] = emudsk_write_lsn2;
  IOWriters[0x83] = emudsk_write_command;
  IOWriters[0x84] = emudsk_write_buf0;
  IOWriters[0x85] = emudsk_write_buf1;
  IOWriters[0x86] = emudsk_write_drv;

  // CLOCK_AND_STOP ($FF87)
  IOReaders[0x87] = clock_and_stop_reader;
  IOWriters[0x87] = clock_and_stop_writer;

  // ACIA 6850 ($FF88..$FF89)
  IOReaders[0x88] = flat65280_acia_read_status;
  IOReaders[0x89] = flat65280_acia_read_rx;
  IOWriters[0x88] = flat65280_acia_write_ctrl;
  IOWriters[0x89] = flat65280_acia_write_tx;

  // GIME Init0 ($FF90)
  IOReaders[0x90] = deep65280_read_gime_init0;

  // Task Switch Register ($FF91)
  IOReaders[0x91] = deep65280_read_task;
  IOWriters[0x91] = deep65280_write_task;

  // MMU Task 0 & 1 Registers ($FFA0..$FFAF)
  for (int i = 0xA0; i <= 0xAF; i++) {
    IOReaders[i] = deep65280_read_mmu;
    IOWriters[i] = deep65280_write_mmu;
  }

  // System Vectors ($FFF0..$FFFF)
  for (int i = 0xF0; i <= 0xFF; i++) {
    IOReaders[i] = deep65280_vector_reader;
    IOWriters[i] = deep65280_vector_writer;
  }
}

void setup_deep65280() {
  current_engine = ENGINE_DEEP65280;

  // 1. Open drive nodes if configured
  for (int d = 0; d < 2; d++) {
    const char* path = (d == 0) ? centipede_config.drive0 : centipede_config.drive1;
    if (path && path[0]) {
      g_drive_nodes[d] = vfs_resolve(path);
      if (g_drive_nodes[d]) {
        int err = g_drive_nodes[d]->open_file(LFS_O_RDWR);
        if (err < 0) {
          cobs_printf("[deep65280: drive %d open RDWR failed (err %d), trying RDONLY: %s]\n", d, err, path);
          err = g_drive_nodes[d]->open_file(LFS_O_RDONLY);
          if (err >= 0) {
            cobs_printf("[deep65280: opened drive %d read-only: %s]\n", d, path);
          } else {
            cobs_printf("[deep65280: drive %d open failed: %s]\n", d, path);
            g_drive_nodes[d] = nullptr;
          }
        } else {
          cobs_printf("[deep65280: opened drive %d: %s]\n", d, path);
        }
      } else {
        cobs_printf("[deep65280: could not resolve drive %d path: %s]\n", d, path);
      }
    } else {
      g_drive_nodes[d] = nullptr;
    }
  }

  // 2. Initialize MMU defaults:
  // Task 0 and Task 1: $00, $39, $3A, $3B, $3C, $3D, $3E, $3F
  for (int t = 0; t < 2; t++) {
    deep65280_mmu[t][0] = 0x00;
    for (int s = 1; s < 8; s++) {
      deep65280_mmu[t][s] = 0x38 + s; // 0x39..0x3F
    }
  }
  deep65280_active_task = 0;
  update_task_mmu();

  // 3. Load deep65280 image into physical RAM
#if DEEP_256K
  memset(ram, 0, sizeof(ram)); // 262144 bytes (256KB) zeroed

  const char* img_path = centipede_config.flat_img[0] ? centipede_config.flat_img : "/pc/deep65280.img";
  auto img_node = vfs_resolve(img_path);
  if (img_node) {
    int err = img_node->open_file(LFS_O_RDONLY);
    if (err >= 0) {
      cobs_printf("[deep65280: loading %s into 256KB RAM...]\n", img_path);

      // Slot 0 (8KB) into physical block 0 (0x00000..0x01FFF)
      size_t total0 = 0;
      while (total0 < 8192) {
        size_t chunk = 8192 - total0;
        if (chunk > 256) chunk = 256;
        lfs_ssize_t n = img_node->read(&ram[total0], chunk);
        if (n <= 0) break;
        total0 += n;
      }

      // Slots 1..7 (56KB) into physical blocks 25..31 (0x32000..0x3FFFF)
      // Corresponding to MMU pages $39..$3F (& 0x1F = 25..31)
      size_t total1 = 0;
      while (total1 < 57344) {
        size_t chunk = 57344 - total1;
        if (chunk > 256) chunk = 256;
        lfs_ssize_t n = img_node->read(&ram[0x32000 + total1], chunk);
        if (n <= 0) break;
        total1 += n;
      }
      img_node->close_file(nullptr);
      cobs_printf("[deep65280: loaded %d + %d = %d bytes into RAM (256KB)]\n",
                  (int)total0, (int)total1, (int)(total0 + total1));
    } else {
      cobs_printf("[deep65280: failed to open %s (err %d)]\n", img_path, err);
    }
  } else {
    cobs_printf("[deep65280: could not resolve %s]\n", img_path);
  }

  // Copy vectors from physical block 31 ($3FFF0) to vector_ram
  memcpy(vector_ram, &ram[0x3FFF0], 16);
#else
  memset(ram, 0, sizeof(ram)); // 131072 bytes (128KB) zeroed

  const char* img_path = centipede_config.flat_img[0] ? centipede_config.flat_img : "/pc/deep65280.img";
  auto img_node = vfs_resolve(img_path);
  if (img_node) {
    int err = img_node->open_file(LFS_O_RDONLY);
    if (err >= 0) {
      cobs_printf("[deep65280: loading %s into 128KB RAM...]\n", img_path);

      // Slot 0 (8KB) into physical block 0 (0x00000..0x01FFF)
      size_t total0 = 0;
      while (total0 < 8192) {
        size_t chunk = 8192 - total0;
        if (chunk > 256) chunk = 256;
        lfs_ssize_t n = img_node->read(&ram[total0], chunk);
        if (n <= 0) break;
        total0 += n;
      }

      // Slots 1..7 (56KB) into physical blocks 9..15 (0x12000..0x1FFFF)
      size_t total1 = 0;
      while (total1 < 57344) {
        size_t chunk = 57344 - total1;
        if (chunk > 256) chunk = 256;
        lfs_ssize_t n = img_node->read(&ram[0x12000 + total1], chunk);
        if (n <= 0) break;
        total1 += n;
      }
      img_node->close_file(nullptr);
      cobs_printf("[deep65280: loaded %d + %d = %d bytes into RAM (128KB)]\n",
                  (int)total0, (int)total1, (int)(total0 + total1));
    } else {
      cobs_printf("[deep65280: failed to open %s (err %d)]\n", img_path, err);
    }
  } else {
    cobs_printf("[deep65280: could not resolve %s]\n", img_path);
  }

  // Copy vectors from physical block 15 ($1FFF0) to vector_ram
  memcpy(vector_ram, &ram[0x1FFF0], 16);
#endif

  // 4. Install IO handlers
  Install_Deep65280_IO();
}

// ═══════════════════════════════════════════════════════════════════
// gep9 (Hatvan 6309) Engine Support
// ═══════════════════════════════════════════════════════════════════

void IN_RAM gep9_write_disk_cmdst(uint, byte d) {
  g_gep9_disk_req.drive = gep9_disk.drive;
  g_gep9_disk_req.sector = gep9_disk.sector;
  g_gep9_disk_req.mem_task = gep9_disk.mem_task;
  g_gep9_disk_req.mem_addr = gep9_disk.mem_addr;
  g_gep9_disk_req.command = d;
  g_gep9_disk_req.status.store(-1, std::memory_order_release);

  uint pb_w = ((uint)FG2BG_GEP9_DISK << 24);
  while (!fg2bg.push(pb_w)) { tight_loop_contents(); }

  while (g_gep9_disk_req.status.load(std::memory_order_acquire) < 0) {
    tight_loop_contents();
  }
  gep9_disk.status = (byte)g_gep9_disk_req.status.load(std::memory_order_acquire);
}

static bool gep9_load_decb_into_task(uint8_t task, const char* path) {
  auto img_node = vfs_resolve(path);
  if (!img_node) {
    cobs_printf("[gep9: could not resolve %s]\n", path);
    return false;
  }
  int err = img_node->open_file(LFS_O_RDONLY);
  if (err < 0) {
    cobs_printf("[gep9: failed to open %s (err %d)]\n", path, err);
    return false;
  }
  cobs_printf("[gep9: loading %s into Task %d...]\n", path, (int)task);
  byte hdr[5];
  lfs_ssize_t hn = img_node->read(hdr, 5);
  if (hn == 5 && (hdr[0] == 0x00 || hdr[0] == 0xFD || hdr[0] == 0xFE || hdr[0] == 0xFF)) {
    // Extended DECB stream
    img_node->seek(0, LFS_SEEK_SET, nullptr);
    uint16_t high16 = 0;
    while (true) {
      lfs_ssize_t n = img_node->read(hdr, 5);
      if (n < 5) break;
      byte chunk_type = hdr[0];
      uint16_t length = ((uint16_t)hdr[1] << 8) | hdr[2];
      uint16_t addr = ((uint16_t)hdr[3] << 8) | hdr[4];

      if (chunk_type == 0xFE) { // Extended: Upper 16-bit address extension (ChunkHigh16)
        high16 = addr;
        if (length > 0) {
          img_node->seek(length, LFS_SEEK_CUR, nullptr);
        }
      } else if (chunk_type == 0x00) { // Data chunk
        uint32_t full_addr = ((uint32_t)high16 << 16) | addr;
        byte chunk_buf[256];
        uint16_t remaining = length;
        uint32_t cur_addr = full_addr;
        while (remaining > 0) {
          uint16_t to_read = remaining > 256 ? 256 : remaining;
          lfs_ssize_t rn = img_node->read(chunk_buf, to_read);
          if (rn <= 0) break;
          for (int i = 0; i < rn; i++) {
            if (gep9_mmu.embiggen && (cur_addr + i) >= 0x10000) {
              uint32_t phys = cur_addr + i;
              uint8_t blk = phys / 8192;
              uint16_t off = phys % 8192;
              *gep9_mmu.TranslateBlockWrite(task, blk, off) = chunk_buf[i];
            } else {
              *gep9_mmu.TranslateWrite(task, (uint16_t)(cur_addr + i)) = chunk_buf[i];
            }
          }
          cur_addr += rn;
          remaining -= rn;
        }
      } else if (chunk_type == 0xFF) { // Exec trailer
        if (task == 0) {
          *gep9_mmu.TranslateWrite(0, 0xFFFE) = (addr >> 8) & 0xFF;
          *gep9_mmu.TranslateWrite(0, 0xFFFF) = addr & 0xFF;
          cobs_printf("[gep9: DECB execution address $%04X set in reset vector]\n", addr);
        }
        break;
      } else {
        // Non-data chunk (magic, symbols, debug info)
        if (length > 0) {
          img_node->seek(length, LFS_SEEK_CUR, nullptr);
        }
      }
    }
  } else {
    // Raw image fallback
    cobs_printf("[gep9: loading raw image into Task %d...]\n", (int)task);
    img_node->seek(0, LFS_SEEK_SET, nullptr);
    byte buf[256];
    uint32_t total = 0;
    while (total < 65536) {
      lfs_ssize_t rn = img_node->read(buf, 256);
      if (rn <= 0) break;
      for (int i = 0; i < rn; i++) {
        *gep9_mmu.TranslateWrite(task, total + i) = buf[i];
      }
      total += rn;
    }
  }
  img_node->close_file(nullptr);
  cobs_printf("[gep9: Task %d image load complete]\n", (int)task);
  return true;
}

void setup_gep9() {
  current_engine = ENGINE_GEP9;

  // 1. Initialize gep9 MMU & memory pool
  gep9_mmu.Init();

  // 2. Open up to 4 drive nodes if configured (default drive 0: /pc/disk0.dsk)
  for (int d = 0; d < 4; d++) {
    const char* path = "";
    if (d == 0) {
      if (centipede_config.drive0[0]) {
        path = centipede_config.drive0;
      } else {
        auto hnode = vfs_resolve("/pc/hatvan0.dsk");
        path = hnode ? "/pc/hatvan0.dsk" : "/pc/disk0.dsk";
      }
    }
    else if (d == 1) path = centipede_config.drive1;
    else if (d == 2) path = centipede_config.drive2;
    else if (d == 3) path = centipede_config.drive3;

    if (path && path[0]) {
      g_drive_nodes[d] = vfs_resolve(path);
      if (g_drive_nodes[d]) {
        int err = g_drive_nodes[d]->open_file(LFS_O_RDWR);
        if (err < 0) {
          cobs_printf("[gep9: drive %d open RDWR failed (err %d), trying RDONLY: %s]\n", d, err, path);
          err = g_drive_nodes[d]->open_file(LFS_O_RDONLY);
          if (err >= 0) {
            cobs_printf("[gep9: opened drive %d read-only: %s]\n", d, path);
          } else {
            cobs_printf("[gep9: drive %d open failed: %s]\n", d, path);
            g_drive_nodes[d] = nullptr;
          }
        } else {
          cobs_printf("[gep9: opened drive %d: %s]\n", d, path);
        }
      } else {
        cobs_printf("[gep9: could not resolve drive %d path: %s]\n", d, path);
      }
    } else {
      g_drive_nodes[d] = nullptr;
    }
  }

  // 3. Load tasks into memory:
  // Task 0: Kernel (flat_img or /pc/kernel_6809.decb)
  const char* t0_path = centipede_config.flat_img[0] ? centipede_config.flat_img : "/pc/kernel_6809.decb";
  gep9_load_decb_into_task(0, t0_path);

  // Task 1: RBF disk driver (task1 or /pc/rbf_6809.decb)
  const char* t1_path = centipede_config.task1[0] ? centipede_config.task1 : "/pc/rbf_6809.decb";
  gep9_load_decb_into_task(1, t1_path);

  // Task 2: ProcFS driver (task2 or /pc/procfs_6809.decb)
  const char* t2_path = centipede_config.task2[0] ? centipede_config.task2 : "/pc/procfs_6809.decb";
  gep9_load_decb_into_task(2, t2_path);

  // 4. Install IO handlers
  Install_Gep9_IO(IOReaders, IOWriters);
  cobs_printf("[gep9: IO and MMU initialized | preloaded %d / 1024 pages (Task 0: %d, Task 1: %d, Task 2: %d)]\n",
              (int)gep9_mmu.allocated_count,
              gep9_mmu.CountTaskFrames(0),
              gep9_mmu.CountTaskFrames(1),
              gep9_mmu.CountTaskFrames(2));
}

void setup_gep9big() {
  current_engine = ENGINE_GEP9BIG;

  // 1. Initialize gep9 MMU with EMBIGGEN active
  gep9_mmu.Init();
  gep9_mmu.embiggen = true;

  // 2. Open up to 4 drive nodes if configured
  for (int d = 0; d < 4; d++) {
    const char* path = "";
    if (d == 0) {
      if (centipede_config.drive0[0]) {
        path = centipede_config.drive0;
      } else {
        auto hnode = vfs_resolve("/pc/hatvan0.dsk");
        path = hnode ? "/pc/hatvan0.dsk" : "/pc/disk0.dsk";
      }
    }
    else if (d == 1) path = centipede_config.drive1;
    else if (d == 2) path = centipede_config.drive2;
    else if (d == 3) path = centipede_config.drive3;

    if (path && path[0]) {
      g_drive_nodes[d] = vfs_resolve(path);
      if (g_drive_nodes[d]) {
        int err = g_drive_nodes[d]->open_file(LFS_O_RDWR);
        if (err < 0) {
          cobs_printf("[gep9big: drive %d open RDWR failed (err %d), trying RDONLY: %s]\n", d, err, path);
          err = g_drive_nodes[d]->open_file(LFS_O_RDONLY);
          if (err >= 0) {
            cobs_printf("[gep9big: opened drive %d read-only: %s]\n", d, path);
          } else {
            cobs_printf("[gep9big: drive %d open failed: %s]\n", d, path);
            g_drive_nodes[d] = nullptr;
          }
        } else {
          cobs_printf("[gep9big: opened drive %d: %s]\n", d, path);
        }
      } else {
        cobs_printf("[gep9big: could not resolve drive %d path: %s]\n", d, path);
      }
    } else {
      g_drive_nodes[d] = nullptr;
    }
  }

  // 3. Load tasks into memory:
  const char* t0_path = centipede_config.flat_img[0] ? centipede_config.flat_img : "/pc/kernel_6809.decb";
  gep9_load_decb_into_task(0, t0_path);

  if (centipede_config.task1[0]) {
    gep9_load_decb_into_task(1, centipede_config.task1);
  }
  if (centipede_config.task2[0]) {
    gep9_load_decb_into_task(2, centipede_config.task2);
  }

  // 4. Install IO handlers (including $FF40..$FF47 MMAP vector)
  Install_Gep9_IO(IOReaders, IOWriters);
  cobs_printf("[gep9big: EMBIGGEN IO and MMU initialized | preloaded %d / 1024 pages (Task 0: %d, Task 1: %d, Task 2: %d)]\n",
              (int)gep9_mmu.allocated_count,
              gep9_mmu.CountTaskFrames(0),
              gep9_mmu.CountTaskFrames(1),
              gep9_mmu.CountTaskFrames(2));
}

// ═══════════════════════════════════════════════════════════════════
// GPIO Initialization — Open-drain for slow control pins
// ═══════════════════════════════════════════════════════════════════
void InitializePinsReturnDirections() {
  for (int i = 0; i < 48; i++) {
    gpio_init(i);
    switch (i) {
      // E, Q, LED: push-pull outputs
      case E:
      case Q:
      case LED:
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 1);
        break;

      // Slow 6809E control: active-low, active-driven.
      // HALT is push-pull (driven HIGH = released).
      // RESET, NMI, IRQ, FIRQ remain open-drain.
      case HALT:
#if HALT_IS_OPEN_DRAIN
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);             // Latch = 0 (active low)
        gpio_set_dir(i, GPIO_IN);   // Released (not driving)
        gpio_set_pulls(i, true, false);  // Internal pull-up
#else
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 1);             // Released (driven HIGH)
#endif
        break;

      case RESET:
      case NMI:
      case IRQ:
      case FIRQ:
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
}


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

// ═══════════════════════════════════════════════════════════════════
// Guts<T> — the bus cycle engine (CRTP)
// ═══════════════════════════════════════════════════════════════════
// HALT-based flow control for cycle logging.
// When the fg2bg FIFO fills up, the foreground asserts HALT to throttle
// the 6309.  During HALT the 6309 emits idle cycles at addr=0xFFFF which
// we skip (no FIFO push), letting the background drain the FIFO.
#define FG2BG_HIGH_WATERMARK 50  // 6000  // Assert HALT when FIFO exceeds this
// #define FG2BG_LOW_WATERMARK  2000  // Release HALT when FIFO drains below this
//== volatile bool fg_halt_for_flow_control = false;
//== volatile bool fg_wants_halt = false;

std::atomic<bool> bg_wants_halt;

// LED is push-pull: Positive Logic (1 == ON)                                                           //
FORCE_INLINE void IN_RAM LedOn()  { gpio_put(LED, 1); }
FORCE_INLINE void IN_RAM LedOff() { gpio_put(LED, 0); }

// HaltOn/HaltOff: encapsulate HALT pin control.
#if HALT_IS_OPEN_DRAIN
// HALT is open-drain: assert by setting dir to output, release by input. Latch is always 0.
FORCE_INLINE void IN_RAM HaltOn()  { gpio_set_dir(HALT, GPIO_OUT); LedOn(); }  // Assert HALT
FORCE_INLINE void IN_RAM HaltOff() { gpio_set_dir(HALT, GPIO_IN); LedOff(); }  // Release HALT
#else
// HALT is push-pull: driven LOW = 6309 halted, driven HIGH = 6309 running.
FORCE_INLINE void IN_RAM HaltOn()  { gpio_put(HALT, 0); LedOn(); }  // Assert HALT
FORCE_INLINE void IN_RAM HaltOff() { gpio_put(HALT, 1); LedOff(); }  // Release HALT
#endif

volatile bool foreground_running = false;  // Set by core 1 when inner loop starts

template <typename T>
struct Guts {
  force_inline static void Poke(uint a, byte b) { ram[a & 0xFFFF] = b; }
  force_inline static byte Peek(uint a) { return ram[a & 0xFFFF]; }
  force_inline static uint Peek2(uint a) {
    return (((uint)Peek(a)) << 8) | Peek(a + 1);
  }

  static void ChangeInterruptPin(bool irq_needed) {
    // Open-drain: assert by setting dir to output, release by input
    if (irq_needed) {
      gpio_set_dir(IRQ, GPIO_OUT);
    } else {
      gpio_set_dir(IRQ, GPIO_IN);
    }
  }

  static void FORCE_INLINE Foreground() {
    if (current_engine == ENGINE_FLAT65280 || current_engine == ENGINE_DEEP65280 || current_engine == ENGINE_GEP9 || current_engine == ENGINE_GEP9BIG) {
      is_an_os9 = true;
    } else {
      T::Install_OS();
    }

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
    bool actually_halted = false;

    // Signal background that we've reached the inner loop.
    // Background waits for this before releasing RESET/HALT.
    foreground_running = true;

    uint prev_late_pins = 0;  // memory for delaying LIC one cycle => FIC.

    // OUTER LOOP — foreground only handles IRQ pin + PIO bus cycles.
    // USB I/O and terminal RX are handled by background on core 0.
    while (true) {
      if (current_engine == ENGINE_GEP9 || current_engine == ENGINE_GEP9BIG) {
        irq_needed = gep9_irq_needed();
      } else if (current_engine == ENGINE_FLAT65280 || current_engine == ENGINE_DEEP65280) {
        irq_needed = flat65280_irq_needed();
      } else {
        irq_needed = T::Turbo9sim_IrqNeeded();
      }
      if (irq_needed != prev_irq_needed) {
        ChangeInterruptPin(irq_needed);
        prev_irq_needed = irq_needed;
        // gpio_put(LED, irq_needed);
      }

#if !JUST_WAIT_DONT_HALT
#if TRACE
      // Flow control: throttle 6309 via HALT when FIFO is filling up.
      // During HALT, the 6309 puts 0xFFFF on the address bus; we skip
      // those reads (no push) so the FIFO drains.
      if (fg_halt_for_flow_control) {
        if (fg2bg.size() < FG2BG_LOW_WATERMARK) {
          fg_wants_halt = false;
          if (!bg_wants_halt) HaltOff();
          fg_halt_for_flow_control = false;
        }
      } else {
        if (fg2bg.size() > FG2BG_HIGH_WATERMARK) {
          fg_wants_halt = true;
          HaltOn();
          fg_halt_for_flow_control = true;
        }
      }
#endif
#endif

#if JUST_WAIT_DONT_HALT
      // Spin here as long as halt is wanted.
      while (true)  {
          bool halt_wanted = bg_wants_halt.load(std::memory_order_relaxed)
            || (fg2bg.size() > FG2BG_HIGH_WATERMARK);
          if (!halt_wanted) break;
          LedOn();
          sleep_us(1);
      }
      LedOff();
#else
      {
          // NEW HALTING: Only foreground contorls HALT.
          bool halt_wanted = bg_wants_halt.load(std::memory_order_relaxed);

#if OCCASIONAL_HALTING
          // Occasional: 98 times off, 1 time on.
          halt_wanted |= (cycles % 99) == 50;
#endif

          if (halt_wanted && !actually_halted) {
              HaltOn();
              actually_halted = true;
              sleep_us(1);
          } else if (!halt_wanted && actually_halted) {
              HaltOff();
              actually_halted = false;
              sleep_us(1);
          }
          sleep_us(1);
      }
#endif

#if SPEED_STATS
      int idle_in_group = 0;
#endif

      // INNER LOOP
      for (int i = 0; i < GROUP_SIZE; i++) {
        pio_sm_put(pio0, 0, 0);  // put sync word

        byte value = 0;

        // Early pins are used for determing R/W.
        // The SM and this early_pins come from the same read.
        uint early_pins = pio_sm_get_blocking(pio0, 0);  // get early pins

        // If R_W was stable on early pins, then the address bus should be stable as well.
        // The address bus is on GPIO[32..47], so we need gpio_hi_in for them.
#if PARANOID
        uint prev_addr = 0xFFFF & hw->gpio_hi_in;
        uint addr;
        while (true) {
          addr = 0xFFFF & hw->gpio_hi_in;
          if (addr == prev_addr) break;
          prev_addr = addr;
          while (true) { // Stop and blink lights
            gpio_put(LED, 1);
            sleep_ms(200);
            gpio_put(LED, 0);
            sleep_ms(200);
          }
        }
#else
        uint addr = 0xFFFF & hw->gpio_hi_in;
#endif

        // Read R/W from GPIO AFTER address stabilization, not from
        // the PIO's early-pins sample.  The PIO decides read/write
        // via `jmp pin` (Phase 3), but `in pins, 32` samples at
        // Phase 2 — 8ns earlier.  During HALT transitions, R/W can
        // change in that window, causing firmware/PIO desync → deadlock.
#if SPEED_STATS
        if (addr == 0xFFFF) idle_in_group++;
#endif

#if 1
#if 1
        const bool reading = 0 != (early_pins & (1<<R_W));
#else
        // BS=1 means either Interrupt Acknowledge or HALT. Neither state
        // can ever perform a valid write. During HALT, R/W is high-impedance,
        // and the Pico's weak pull-ups take too long to pull it HIGH, resulting
        // in phantom writes that corrupt RAM. Forcing reading=true when BS=1
        // prevents this by safely discarding the cycle as a read.
        const bool reading = (0 != (early_pins & (1<<R_W))) || (0 != (early_pins & (1<<BS)));
#endif
#else
        const bool reading = gpio_get(R_W);
#endif
        byte kind = 0;
        uint late_pins = 0;

        if (likely(reading)) {
          // READ CYCLES
          if (current_engine == ENGINE_GEP9 || current_engine == ENGINE_GEP9BIG) {
            // TaskFuse transition: armed fuse fires on the cycle after RTI opcode fetch
            if (unlikely(gep9_mmu.task_fuse_step == 1)) {
              gep9_mmu.current_task = gep9_mmu.task_fuse_target;
              gep9_mmu.task_fuse_armed = false;
              gep9_mmu.task_fuse_step = 0;
            }

            if (unlikely(addr >= 0xFF00)) {
              if (addr >= 0xFFF0) {
                // Vector fetch: instant transition to Task 0 (Kernel)
                // Real vector fetches begin on even addresses $FFF0..$FFFE.
                // 0xFFFF is an internal idle/dead cycle and must not switch tasks!
                if (addr != 0xFFFF && (addr & 1) == 0) {
                  gep9_mmu.current_task = 0;
                }
                value = *gep9_mmu.TranslateRead(0, addr);
              } else {
                if (gep9_mmu.current_task == 0 || (gep9_mmu.task_flags[gep9_mmu.current_task] & 0x01) != 0 || (addr >= 0xFF40 && addr <= 0xFF47)) {
                  IOReader fn = IOReaders[addr & 0xFF];
                  value = fn ? fn(addr) : 0;
                } else {
                  value = 0; // Unblessed user IO access
                }
              }
            } else {
              value = *gep9_mmu.TranslateRead(gep9_mmu.current_task, addr);
            }

            if (unlikely(gep9_mmu.task_fuse_armed && gep9_mmu.task_fuse_step == 0)) {
              if (value == 0x3B) { // RTI opcode fetched
                gep9_mmu.task_fuse_step = 1;
              }
            }
          } else {
            if (likely(addr < 0xFE00)) {
              if (current_engine == ENGINE_DEEP65280) {
                value = ram[task_mmu_base[addr >> 13] | (addr & 0x1FFF)];
              } else {
                value = ram[addr];
              }
            } else if (addr < 0xFF00) {
              if (current_engine == ENGINE_DEEP65280) {
#if DEEP_256K
                value = ram[0x3E000 | (addr & 0x1FFF)];
#else
                value = ram[0x1E000 | (addr & 0x1FFF)];
#endif
              } else {
                value = ram[addr];
              }
            } else {
              IOReader fn = IOReaders[addr & 0xFF];
              if (fn) {
                value = fn(addr);
              } else {
                value = 0;
              }
            }
          }
          kind = CY_READ;
        } else {
          // WRITE CYCLES
          late_pins = pio_sm_get_blocking(pio0, 0);
          value = (byte)late_pins;

          if (current_engine == ENGINE_GEP9 || current_engine == ENGINE_GEP9BIG) {
            if (unlikely(addr >= 0xFF00)) {
              if (addr >= 0xFFF0) {
                *gep9_mmu.TranslateWrite(0, addr) = value;
              } else {
                if (gep9_mmu.current_task == 0 || (gep9_mmu.task_flags[gep9_mmu.current_task] & 0x01) != 0 || (addr >= 0xFF40 && addr <= 0xFF47)) {
                  IOWriter fn = IOWriters[addr & 0xFF];
                  if (fn) fn(addr, value);
                }
              }
            } else {
              *gep9_mmu.TranslateWrite(gep9_mmu.current_task, addr) = value;
            }
          } else {
            if (likely(addr < 0xFE00)) {
              if (current_engine == ENGINE_DEEP65280) {
                ram[task_mmu_base[addr >> 13] | (addr & 0x1FFF)] = value;
              } else {
                ram[addr] = value;
              }
            } else if (addr < 0xFF00) {
              if (current_engine == ENGINE_DEEP65280) {
#if DEEP_256K
                ram[0x3E000 | (addr & 0x1FFF)] = value;
#else
                ram[0x1E000 | (addr & 0x1FFF)] = value;
#endif
              } else {
                ram[addr] = value;
              }
            } else {
              IOWriter fn = IOWriters[addr & 0xFF];
              if (fn) {
                fn(addr, value);
              }
            }
          }
          kind = CY_WRITE;
        }

        if (likely(reading)) {
          pio_sm_put(pio0, 0, value);
          late_pins = pio_sm_get_blocking(pio0, 0);  // LATE PINS
        }

        // ── Trace push: AFTER the PIO handshake is complete ──────
        // All bus timing is done; the 6309 has latched its data.
        // Pushing here avoids SRAM bus contention during the critical
        // window between computing the read value and pio_sm_put.
        bool is_lic = ((late_pins & (1<<LIC)) != 0);
        bool is_fic = ((prev_late_pins & (1<<LIC)) != 0);

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
              Say('D'); Say(':');
              Say(HexAlphabet[(dump_buf[d].addr >> 12) & 0xF]);
              Say(HexAlphabet[(dump_buf[d].addr >> 8) & 0xF]);
              Say(HexAlphabet[(dump_buf[d].addr >> 4) & 0xF]);
              Say(HexAlphabet[dump_buf[d].addr & 0xF]);
              Say('=');
              Say(HexAlphabet[(dump_buf[d].value >> 4) & 0xF]);
              Say(HexAlphabet[dump_buf[d].value & 0xF]);
              Say(':');
              Say(dump_buf[d].rw);
              Say('\n');
            }
          }
        }
#endif

        prev_late_pins = late_pins;
      }  // next i: END INNER LOOP

#if SPEED_STATS
      cycles += GROUP_SIZE;
      fg_idle_cycles += idle_in_group;
      fg_active_cycles += (GROUP_SIZE - idle_in_group);
      static int cycle_counter = 0;
      cycle_counter += GROUP_SIZE;
      if (cycle_counter >= 1000000) {
        PUSH_TO_BG(FG2BG_MEGA_CYCLE, 0, 0);
        cycle_counter -= 1000000;
      }
#endif
    }  // while (true): END OUTER LOOP
  }    // End func Foreground
};     // End struct Guts

// ═══════════════════════════════════════════════════════════════════
// Engine — CRTP composition
// ═══════════════════════════════════════════════════════════════════
struct Engine : public DoTurbo9os<Engine,
#if TRY_AX_ROM
                    RomList<Turbo9os_Rom, Ax_Rom>>,
#else
                    RomList<Turbo9os_Rom, Basic09_Rom>>,
#endif
                public DoTurbo9sim<Engine>,
                public Guts<Engine> {};

// This IN_RAM Engine Launcher will contain the inlined Engine::Foreground,
// so that method will effectively be IN_RAM as well.
void IN_RAM Engine__Foreground() {
    Engine::Foreground();
}

// ═══════════════════════════════════════════════════════════════════
// 60Hz Timer for turbo9sim
// ═══════════════════════════════════════════════════════════════════
struct repeating_timer Timer60HzData;
volatile bool halt_suppress_timer = false;
bool IN_RAM Timer60HzCallback(repeating_timer_t* rt) {
  if (!halt_suppress_timer) {
    if (current_engine == ENGINE_GEP9 || current_engine == ENGINE_GEP9BIG) {
      static uint8_t gep9_div = 0;
      if (++gep9_div >= 12) { // 60 Hz / 12 = 5 Hz (matches emulator default tick-hz)
        gep9_div = 0;
        gep9_io.reg_stat |= 0x01; // Set Timer.Ready flag
        gep9_update_irq_pin();
      }
    } else if (current_engine == ENGINE_FLAT65280 || current_engine == ENGINE_DEEP65280) {
      flat65280_timer_ticks++;
      uint8_t rate = (clock_and_stop_reg >> 4) & 3;
      bool fire = false;
      switch (rate) {
        case 0: fire = true; break;                          // 60 Hz
        case 1: fire = true; break;                          // 50 Hz (approx)
        case 2: fire = (flat65280_timer_ticks % 10) == 0; break;  // 6 Hz
        case 3: fire = (flat65280_timer_ticks % 600) == 0; break; // 0.1 Hz
      }
      if (fire) {
        clock_and_stop_reg |= 0x01; // Set TICK flag
      }
    } else {
      Engine::Turbo9sim_SetTimerFired();
    }
  }
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

static std::string repl_history[20];
static int repl_history_count = 0;

static void tcl_repl_task(Coro& self) {
  rpc::g_vfs_coro = &self;  // VFS RPC yields back to scheduler
  gspoon::g_spoon_coro = &self;  // ls_cmd etc. call coro_yield(g_spoon_coro)
  cobs_printf("\nTFR911 TCL SHELL.  To exit TCL and launch 6809, type  bye \n");

  bool booted_via_rpc = false;
  if (::boot_mode_check == ::boot_mode + BOOT_MODE_CHECKER) {
    ::boot_mode_check = 0;
    booted_via_rpc = true;
  } else {
    ::boot_mode = 0;
  }
  cobs_printf("boot_mode=%u (booted_via_rpc=%d)\n", (unsigned)::boot_mode, booted_via_rpc ? 1 : 0);

  if (booted_via_rpc) {
    cobs_printf("booted_via_rpc is true: waiting 3 seconds for tether USB...\n");
    gspoon::SleepMillis(&self, 3000);
  }

  if (boot_mode != 27) {  // Source RC files, unless BREAK (27 / ESC)
    // First SOURCE /rc/init.tcl if it exists
    struct vfs_info st;
    if (vfs_stat("/rc/init.tcl", &st) == 0) {
      cobs_printf("[Sourcing /rc/init.tcl]\n");
      int init_result = Tcl_Eval(global_tcl_interp, const_cast<char*>("source /rc/init.tcl"), 0, (char**)0);
      if (init_result == TCL_ERROR) {
        const char* output = global_tcl_interp->result;
        if (output && output[0]) cobs_printf("?%s\n", output);
      } else if (init_result == TCL_BYE) {
        cobs_printf("[BYE]\n");
        goto BYE;
      }
    }

    // Then SOURCE /rc/mode%u.tcl (fallback to /pc/mode%u.tcl if not in /rc)
    if (booted_via_rpc || boot_mode > 0) {
      char path_buf[40];
      snprintf(path_buf, sizeof(path_buf), "/rc/mode%u.tcl", (unsigned)boot_mode);
      if (vfs_stat(path_buf, &st) != 0) {
        snprintf(path_buf, sizeof(path_buf), "/pc/mode%u.tcl", (unsigned)boot_mode);
      }
      if (vfs_stat(path_buf, &st) == 0) {
        cobs_printf("[Sourcing %s]\n", path_buf);
        char cmd_buf[50];
        snprintf(cmd_buf, sizeof(cmd_buf), "source %s", path_buf);
        int mode_result = Tcl_Eval(global_tcl_interp, cmd_buf, 0, (char**)0);
        if (mode_result == TCL_ERROR) {
          const char* output = global_tcl_interp->result;
          if (output && output[0]) cobs_printf("?%s\n", output);
        } else if (mode_result == TCL_BYE) {
          cobs_printf("[BYE]\n");
          goto BYE;
        }
      } else {
        cobs_printf("[Warning: mode%u.tcl not found in /rc or /pc]\n", (unsigned)boot_mode);
      }
    }
  }

  char line[256];
  while (true) {
    cobs_printf("TCL> ");
    int line_len = 0;
    int line_cursor = 0;
    int history_index = repl_history_count;
    std::string current_edit = "";

    auto redraw_line = [&]() {
      cobs_printf("\rTCL> ");
      for (int i = 0; i <= line_len; i++) {
        if (i == line_cursor) cobs_printf("\x1b[7m");
        if (i < line_len) {
          cobs_putchar(line[i]);
        } else {
          cobs_putchar(' ');
        }
        if (i == line_cursor) cobs_printf("\x1b[0m");
      }
      cobs_printf("\x1b[K"); // clear rest of line
      for (int i = 0; i < (line_len - line_cursor + 1); i++) cobs_putchar(8);
    };

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
        if (out && out[0]) {
          resp.message = out;
          resp.data = out;
        }
        pico_rpc::send_response(resp);
        if (rc == TCL_BYE) goto BYE;
        cobs_printf("TCL> ");
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
        if (ch == C_PUTCHAR && pkt->size() >= 2) ch = (byte)(*pkt)[1];
        delete pkt;
        if (ch == 13 || ch == 10) {  // End of line
          cobs_putchar('\r');
          cobs_printf("TCL> ");
          line[line_len] = '\0';
          cobs_printf("%s", line);
          cobs_printf("\x1b[K\n");
          break;
        }
        if (ch == 128) {  // Up — history previous
          if (history_index > 0) {
            if (history_index == repl_history_count) {
               line[line_len] = '\0';
               current_edit = line;
            }
            history_index--;
            strcpy(line, repl_history[history_index].c_str());
            line_len = strlen(line);
            line_cursor = line_len;
            redraw_line();
          }
          continue;
        }
        if (ch == 129) {  // Down — history next
          if (history_index < repl_history_count) {
            history_index++;
            if (history_index == repl_history_count) {
              strcpy(line, current_edit.c_str());
            } else {
              strcpy(line, repl_history[history_index].c_str());
            }
            line_len = strlen(line);
            line_cursor = line_len;
            redraw_line();
          }
          continue;
        }
        if (ch == 130) {  // Cursor Left (non-destructive)
          if (line_cursor > 0) {
            line_cursor--;
            redraw_line();
          }
          continue;
        }
        if (ch == 131) {  // Cursor Right (non-destructive)
          if (line_cursor < line_len) {
            line_cursor++;
            redraw_line();
          }
          continue;
        }
        if (ch == 8 || ch == 127) {  // Backspace
          if (line_cursor > 0) {
            for (int i = line_cursor - 1; i < line_len - 1; i++) {
              line[i] = line[i+1];
            }
            line_len--;
            line_cursor--;
            redraw_line();
          }
          continue;
        }
        if (ch > 127) {
          continue;  // Skip non-ASCII (like the ² superscript)
        }
        if (ch >= 0x20 && line_len < 254) {
          for (int i = line_len; i > line_cursor; i--) {
            line[i] = line[i-1];
          }
          line[line_cursor] = (char)ch;
          line_len++;
          line_cursor++;
          redraw_line();
        }
        continue;
      }
      coro_yield(&self);  // Nothing to do — yield to let PumpUsbCobs run
    }
    line[line_len] = 0;

    if (line_len == 0) continue;

    // Save history
    if (repl_history_count == 0 || repl_history[repl_history_count - 1] != line) {
      if (repl_history_count < 20) {
        repl_history[repl_history_count++] = line;
      } else {
        for (int i = 0; i < 19; i++) repl_history[i] = repl_history[i+1];
        repl_history[19] = line;
      }
    }

    int result;
    if (strchr(line, ';') == NULL && strchr(line, '[') == NULL &&
        strchr(line, ']') == NULL && strchr(line, '{') == NULL &&
        strchr(line, '}') == NULL && strchr(line, '"') == NULL) {
      char new_line[260];
      snprintf(new_line, sizeof(new_line), "fs %s", line);
      result = Tcl_Eval(global_tcl_interp, new_line, 0, (char**)0);
    } else {
      result = Tcl_Eval(global_tcl_interp, line, 0, (char**)0);
    }
    const char* output = global_tcl_interp->result;
    if (output && output[0]) {
      if (result == TCL_ERROR) cobs_putchar('?');
      tcl_io::emit_string(output);
      cobs_putchar('\n');
    }
    if (result == TCL_BYE) break;
  }

BYE:
  rpc::g_vfs_coro = nullptr;
  gspoon::g_spoon_coro = nullptr;
  cobs_printf("[BYE — starting 6309]\n");
  tcl_repl_done = true;
}

// ═══════════════════════════════════════════════════════════════════
// drain_chars — drain high-priority character FIFO (inline, not a coroutine)
// Runs on the scheduler's own stack. Processes ALL pending chars each call.
// ═══════════════════════════════════════════════════════════════════
static uint32_t bg_chars_drained = 0;

inline void drain_chars() {
  if (!usb_tether_ok()) {
    // USB disconnected — discard to prevent blocking
    byte discard;
    while (fg2bg_chars.pop(discard)) {}
    return;
  }
  byte c;
  while (fg2bg_chars.pop(c)) {
    putbyte(c);
    bg_chars_drained++;
  }
}

// ═══════════════════════════════════════════════════════════════════
// drain_task — drain fg2bg events FIFO (coroutine, one event per resume)
// Follows the Centipede pattern exactly: pop one chore, process it, yield.
// The scheduler pumps USB between every coro_resume.
// ═══════════════════════════════════════════════════════════════════
static uint8_t drain_stack[4096] __attribute__((aligned(8)));
static uint32_t bg_events_drained = 0;

static void drain_task(Coro& self) {
#if SPEED_STATS
  int bg_mega_cycles = 0;
#endif
  while (true) {
    uint chore = 0;
    if (fg2bg.pop(chore)) {
      bg_events_drained++;
      uint chore_num = chore >> 24;
      switch (chore_num) {
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
        case FG2BG_EMUDSK: {
          rpc::g_vfs_coro = &self;
          uint8_t drv = g_emudsk_req.drive;
          if (drv > 1 || !g_drive_nodes[drv]) {
            g_emudsk_req.status.store(2, std::memory_order_release); // Drive not ready / enabled
            break;
          }
          auto node = g_drive_nodes[drv];
          lfs_soff_t offset = (lfs_soff_t)g_emudsk_req.lsn * 256;
          lfs_soff_t actual = node->seek(offset, LFS_SEEK_SET, &self);
          if (actual != offset) {
            g_emudsk_req.status.store(6, std::memory_order_release); // Seek error
            break;
          }
          uint16_t buf_addr = g_emudsk_req.buffer_addr;
          uint32_t phys_buf = (current_engine == ENGINE_DEEP65280) ? translate_deep_addr(buf_addr) : buf_addr;
          if (g_emudsk_req.command == 0) {
            // Read sector
            lfs_ssize_t n = node->read(&ram[phys_buf], 256);
            if (n == 256) {
              g_emudsk_req.status.store(0, std::memory_order_release);
            } else {
              g_emudsk_req.status.store(6, std::memory_order_release);
            }
          } else if (g_emudsk_req.command == 1) {
            // Write sector
            lfs_ssize_t n = node->write(&ram[phys_buf], 256);
            if (n == 256) {
              g_emudsk_req.status.store(0, std::memory_order_release);
            } else {
              g_emudsk_req.status.store(6, std::memory_order_release);
            }
          } else {
            g_emudsk_req.status.store(6, std::memory_order_release);
          }
          break;
        }
        case FG2BG_GEP9_DISK: {
          rpc::g_vfs_coro = &self;
          uint8_t drv = g_gep9_disk_req.drive;
          if (drv > 3 || !g_drive_nodes[drv]) {
            g_gep9_disk_req.status.store(2, std::memory_order_release); // Drive not ready / enabled
            break;
          }
          auto node = g_drive_nodes[drv];
          lfs_soff_t offset = (lfs_soff_t)g_gep9_disk_req.sector * 256;
          lfs_soff_t actual = node->seek(offset, LFS_SEEK_SET, &self);
          if (actual != offset) {
            g_gep9_disk_req.status.store(3, std::memory_order_release); // Seek error / out of range
            break;
          }
          uint8_t task = g_gep9_disk_req.mem_task;
          uint16_t addr = g_gep9_disk_req.mem_addr;
          byte sec_buf[256];
          if (g_gep9_disk_req.command == 1) {
            // Read sector
            lfs_ssize_t n = node->read(sec_buf, 256);
            if (n == 256) {
              for (int i = 0; i < 256; i++) {
                *gep9_mmu.TranslateWrite(task, addr + i) = sec_buf[i];
              }
              g_gep9_disk_req.status.store(1, std::memory_order_release); // 1 = OKAY
            } else {
              g_gep9_disk_req.status.store(4, std::memory_order_release);
            }
          } else if (g_gep9_disk_req.command == 2) {
            // Write sector
            for (int i = 0; i < 256; i++) {
              sec_buf[i] = *gep9_mmu.TranslateRead(task, addr + i);
            }
            lfs_ssize_t n = node->write(sec_buf, 256);
            if (n == 256) {
              g_gep9_disk_req.status.store(1, std::memory_order_release); // 1 = OKAY
            } else {
              g_gep9_disk_req.status.store(4, std::memory_order_release);
            }
          } else {
            g_gep9_disk_req.status.store(4, std::memory_order_release);
          }
          break;
        }
        default:
          break;
      }
    }
    // Yield after every chore (or empty pop), exactly like Centipede.
    // The scheduler pumps USB between every coro_resume.
    coro_yield(&self);
  }
}

// ═══════════════════════════════════════════════════════════════════
// periodic_status — print diagnostics every 5 seconds (inline)
// ═══════════════════════════════════════════════════════════════════
static uint64_t last_status_time = 0;
static uint32_t last_chars_drained = 0;
static uint32_t last_events_drained = 0;
#if SPEED_STATS
static uint64_t last_idle = 0;
static uint64_t last_active = 0;
#endif

inline void periodic_status() {
  uint64_t now = time_us_64();
  if (now - last_status_time < 5000000) return;
  last_status_time = now;

  uint32_t chars_delta = bg_chars_drained - last_chars_drained;
  uint32_t events_delta = bg_events_drained - last_events_drained;
  last_chars_drained = bg_chars_drained;
  last_events_drained = bg_events_drained;

#if SPEED_STATS
  uint64_t idle_delta = fg_idle_cycles - last_idle;
  uint64_t active_delta = fg_active_cycles - last_active;
  last_idle = fg_idle_cycles;
  last_active = fg_active_cycles;
  cobs_printf("\n[bg: chars=%d/%d events=%d/%d ch_d=%u ev_d=%u idle=%uM active=%uM halt=%d bg_halt=%d]\n",
              (int)fg2bg_chars.size(), 8192,
              (int)fg2bg.size(), 8192,
              chars_delta, events_delta,
              (unsigned)(idle_delta / 1000000),
              (unsigned)(active_delta / 1000000),
              666, //== (int)fg_wants_halt,
              (int)bg_wants_halt);
#else
  if (0) cobs_printf("\n[bg: chars=%d/%d events=%d/%d ch_d=%u ev_d=%u halt=%d bg_halt=%d]\n",
              (int)fg2bg_chars.size(), 8192,
              (int)fg2bg.size(), 8192,
              chars_delta, events_delta,
              666, //== (int)fg_wants_halt,
              (int)bg_wants_halt);
#endif
}

// ═══════════════════════════════════════════════════════════════════
// Background loop (core 0) — Unified Cooperative Scheduler
//
// Phase 1: Tcl REPL (6309 idle, core 1 not launched)
// Phase 2: Bus operation (6309 running, coroutine-based drain)
//
// The scheduler pumps USB between every coroutine resume, exactly
// like the Centipede.  drain_chars() runs inline at the top of
// every iteration so console output is never delayed.
// ═══════════════════════════════════════════════════════════════════
void IN_RAM tfr911_background() {
  // ── Initialize ──────────────────────────────────────────────────
  init_lfs();
  global_tcl_interp = Tcl_CreateInterp();
  register_tcl_commands(global_tcl_interp);

  // Create all coroutines upfront
  Coro tcl_repl_coro;
  coro_create(&tcl_repl_coro, tcl_repl_task, tcl_stack, sizeof(tcl_stack));

  Coro drain_coro;
  coro_create(&drain_coro, drain_task, drain_stack, sizeof(drain_stack));

  bool phase2 = false;

  // ── Unified Scheduler Loop ─────────────────────────────────────
  while (true) {
    // HIGH PRIORITY: drain console characters first (inline, fast)
    drain_chars();

    if (!phase2) {
      // ── Phase 1: Tcl REPL ──────────────────────────────────────
      coro_resume(&tcl_repl_coro);
      PumpUsbCobs();

      if (tcl_repl_done) {
        // ── Transition to Phase 2 ────────────────────────────────
        //
        // The 6309 is CMOS but not fully static — it needs continuous
        // E/Q clocks or it loses state.  During the Tcl REPL, no
        // clocks were generated.  So we must do the reset WITH the
        // PIO/foreground running:
        //
        //   1. Assert RESET + HALT (open-drain) from background
        //   2. Re-init PIO and launch foreground on core 1
        //   3. Foreground generates E/Q; 6309 sees IDLE cycles
        //   4. Background waits for foreground to be running
        //   5. Hold RESET for ~10ms (~28K E cycles)
        //   6. Release RESET (still HALTed)
        //   7. Wait ~5ms for 6309 to see RESET de-assert
        //   8. Release HALT → 6309 fetches reset vector
        //
        cobs_printf("[bye: asserting RESET+HALT...]\n");
        gpio_set_dir(RESET, GPIO_OUT);
        // bg_wants_halt.store(true, std::memory_order_relaxed); //== HaltOn();
        //== HaltOn();

        pio_sm_set_enabled(pio0, 0, false);
        pio_sm_restart(pio0, 0);
        pio_clear_instruction_memory(pio0);
        uint offset = pio_add_program(pio0, &hamster_program);
        hamster_program_init(pio0, 0, offset);

        if (strcmp(centipede_config.engine, "gep9big") == 0 ||
            strcmp(centipede_config.engine, "gep9+") == 0 ||
            strcmp(centipede_config.engine, "big6809") == 0 ||
            strcmp(centipede_config.engine, "hatvanbig") == 0) {
          setup_gep9big();
        } else if (strcmp(centipede_config.engine, "gep9") == 0 ||
            strcmp(centipede_config.engine, "hatvan") == 0) {
          setup_gep9();
        } else if (strcmp(centipede_config.engine, "flat65280") == 0 ||
                   strcmp(centipede_config.engine, "flat") == 0) {
          setup_flat65280();
        } else if (strcmp(centipede_config.engine, "deep65280") == 0 ||
                   strcmp(centipede_config.engine, "deep64280") == 0 ||
                   strcmp(centipede_config.engine, "deep") == 0) {
          setup_deep65280();
        }

        foreground_running = false;
        cobs_printf("[bye: launching core1...]\n");
        multicore_launch_core1(Engine__Foreground);

        // Drain ROM writes from fg2bg while waiting for foreground.
        // Install_OS pushes ~34K FG2BG_WRITE entries.
        // sleep_ms(1) gives Core 1 time to grab USB mutex for prints.
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
        sleep_ms(10);
        gpio_set_dir(RESET, GPIO_IN);
        cobs_printf("[bye: RESET released, holding HALT...]\n");
        sleep_ms(5);
        // bg_wants_halt.store(false, std::memory_order_relaxed); //== HaltOff();
        //== HaltOff();
        cobs_printf("[bye: HALT released, 6309 booting]\n");

        alarm_pool_init_default();
#if SPEED_STATS
        add_repeating_timer_us(1000, TimerCallback, nullptr, &TimerData);
#endif
#if CLOCK_IRQ
#if TRACE
        add_repeating_timer_ms(5000, Timer60HzCallback, nullptr, &Timer60HzData);
#else
        add_repeating_timer_us(16667, Timer60HzCallback, nullptr, &Timer60HzData);
#endif
#endif
        last_status_time = time_us_64();
        cobs_printf("[bye: Phase 2 started]\n");
        phase2 = true;
      }
    } else {
      // ── Phase 2: 6309 running ──────────────────────────────────
      coro_resume(&drain_coro);
      PumpUsbCobs();

      // Deliver terminal input to 6309 ACIA / GEP9 emulator
      PollTermInput();
      if (current_engine == ENGINE_GEP9 || current_engine == ENGINE_GEP9BIG) {
        if (!gep9_rx_has_char && term_input.NumBuffered() > 0) {
          gep9_rx_char = term_input.Take();
          gep9_rx_has_char = true;
          gep9_io.reg_stat |= 0x02; // Set Term.RxReady
          gep9_update_irq_pin();
        }
      } else if (current_engine == ENGINE_FLAT65280 || current_engine == ENGINE_DEEP65280) {
        if (!flat65280_rx_has_char && term_input.NumBuffered() > 0) {
          flat65280_rx_char = term_input.Take();
          flat65280_rx_has_char = true;
        }
      } else {
        if (Engine::Turbo9sim_CanRx()) {
          if (term_input.NumBuffered() > 0) {
            Engine::Turbo9sim_SetRx(term_input.Take());
          }
        }
      }

      periodic_status();
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
