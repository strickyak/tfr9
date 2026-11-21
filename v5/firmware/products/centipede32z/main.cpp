// Centipede 32z Firmware (v5) — RP2350B CoCo 1/2/3 Cartridge Supervisor
//
// Shares `firmware/lib/` (core, fs, tcl, devices, mem, cpu_tracker) and `firmware/engines/`
// with `tfr911h`, driving the CoCo 40-pin cartridge bus via `gerbil.pio` and four dedicated
// IN_RAM Gerbil foreground engines (`gerbil_sam64k`, `gerbil_gime`, `gerbil_gep9`, `gerbil_io`).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#define FORCE_INLINE inline __attribute__((always_inline))
#define force_inline FORCE_INLINE
#define IN_RAM __not_in_flash("centipede")
#define IN_FLASH

#include "gerbil.pio.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "pico/time.h"

extern "C" {
int putchar_raw(int c);
}

#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define likely(x)   LIKELY(x)
#define unlikely(x) UNLIKELY(x)

#define MHz 250
#define DEEP_256K 1
#define DEFAULT_BOARD_HOST "coco2"
#define SAM_BIT_16K 1
#define SAM_BIT_64K 0
#define USE_PMODE4 1
#define GREEN_PMODE 0
#define INVERSE_PMODE 0
#define FLOPPY_OVER_VFS 1
#define AUTO_GLOB 1
#define BUG_SPLASH_MILLIS 1500

using byte = unsigned char;
using addr16 = uint16_t;

// Board pin aliases used by shared device headers (`clock_stop.h`, `gep9_io.h`, `audio_sn76489.h`):
constexpr uint RESET = G_RESET;
constexpr uint HALT  = G_HALT;
constexpr uint NMI   = G_NMI;
constexpr uint LED   = G_LED;

// Tcl interpreter
#include "tcl6.7c/tcl.h"
Tcl_Interp* global_tcl_interp = nullptr;

// COBS + Cross-Core FIFOs
#include "cobs.h"
#include "cross-core.h"

enum : byte {
  BG2FG_PEEK = 1,
  BG2FG_POKE = 2,
  BG2FG_EXIT_CONSOLE = 3,
};

enum : byte {
  FG2BG_READ = 1,
  FG2BG_WRITE = 3,
  FG0BG_FIC = 5,
  FG2BG_PUTCHAR = 6,
  FG2BG_FLOPPY_LATCH = 10,
  FG2BG_FLOPPY_COMMAND = 11,
  FG2BG_W_256 = 12,
  FG2BG_PEEK_REPLY = 13,
  FG2BG_SPOON_ON_RESET = 14,
  FG2BG_START_KEYBOARD_INJECTOR = 15,
  FG2BG_EMUDSK = 16,
  FG2BG_GEP9_DISK = 17,
  FG2BG_FUZIX_SD_READ = 18,
  FG2BG_FUZIX_SD_WRITE = 19,
  FG2BG_HOST_RESET = 20,
};

enum : byte {
  C_LOGGING = 130,
  T_DISK_READ = 173,
  C_DISK_WRITE = 174,
  C_COMPRESSED_CYCLES = 175,
  C_CORE_DUMP = 189,
  C_PRE_PUTCHAR = 191,
  C_PUTCHAR = 193,
  C_WRITE_CYCLE = 195,
  C_SHUTDOWN = 255,
};

// Cross-core queues (power-of-2 element counts)
CrossCoreFIFO<uint, 512> bg2fg;
CrossCoreFIFO<uint, 2048> fg2bg;
CrossCoreFIFO<byte, 4096> fg2bg_chars;  // 4096-byte dedicated console TX FIFO

FORCE_INLINE void IN_RAM Say(byte c) {
  (void)fg2bg_chars.push(c);
}
#define SAY(c) Say((byte)(c))
#define PUSH_TO_BG(cmd, addr, data) \
  ((void)fg2bg.push(((uint)(cmd) << 24) | (((uint)(addr) & 0xFFFFu) << 8) | ((uint)(data) & 0xFFu)))

extern volatile uint nmi_fuse;
#define ASSERT_NMI() do { gpio_set_dir(G_NMI, GPIO_OUT); nmi_fuse = 6; } while (0)

// Reboot persistence (`boot_mode`)
#define BOOT_MODE_CHECKER 0x5A3C96E1u
uint32_t __uninitialized_ram(boot_mode);
uint32_t __uninitialized_ram(boot_mode_check);

volatile bool startup_e_clock_detected = false;
volatile bool coco_running = false;
volatile bool spoon_has_work = false;

// Core + VFS + Console + Tcl libraries (order: vfs_rpc -> console -> tcl_io -> vfs -> gspoon fwd -> littlefs)
#include "config.h"
#include "rtc.h"
#include "restart.h"
#include "flash-label.h"
#include "cobs_tx.h"
#include "abort.h"
#include "heap.h"
#include "oom_abort.h"
#include "pcb.h"
#include "usb_pipeline.h"
#include "circbuf.h"
CircBuf<unsigned char, 1024> usb_raw_buf;
CircBuf<std::string*, 64> usb_packet_buf;
UsbReceiver usb_receiver(usb_raw_buf);
CobsDecoder<1024, 64> cobs_decoder(usb_raw_buf, usb_packet_buf);
#include "coro.h"
#include "vfs_rpc.h"

#ifndef T_RPC
#define T_RPC 180
#endif
#ifndef T_PICO_RPC
#define T_PICO_RPC 181
#endif
#ifndef T_WALLTIME
#define T_WALLTIME 182
#endif

#include "bug.h"
#include "console.h"
#include "keyboard_injector.h"
#include "tcl_io.h"
extern "C" {
#include "lfs-centipede.h"
}
#include "vfs.h"

namespace gspoon {
extern Coro* g_spoon_coro;
Coro* g_spoon_coro = nullptr;
void SleepMillis(Coro* c, uint64_t ms);
}  // namespace gspoon

#include "littlefs.h"

static bool g_term_acia_arrow_keys = false;

// Physical 256 KB RAM + 16-byte vector RAM + I/O dispatch tables
byte ram[256 * 1024];
byte vector_ram[16];
bool is_an_os9 = false;
CircBuf<byte, 256> term_input;

using IOReader = byte (*)(uint addr);
using IOWriter = void (*)(uint addr, byte data);
IOReader IOReaders[256];
IOWriter IOWriters[256];

byte IN_RAM vector_ram_reader(uint a) { return vector_ram[a & 15u]; }

volatile byte sim_status_reg = 0;
volatile byte sim_control_reg = 0;

// Floppy disk emulation (`floppy.h`)
#include "floppy.h"

struct CentipedeFloppyTraits {
  static void Fatal(const char* s, int x) {
    cobs_printf("FATAL(%s, %d)\n", s, x);
    centipede_abort(s);
  }
};
using FloppyImpl = DoFloppy<CentipedeFloppyTraits>;

// PicoRPC + Tcl commands
#include "pico_rpc.h"
#include "tcl_commands.h"

// Memory & Device Subsystems (`firmware/lib/mem/` and `firmware/lib/devices/`)
#include "deep_mmu.h"
#include "gep9_mmu.h"
#include "io_install.h"
#include "audio_sn76489.h"
#include "becker.h"
#include "rtc_port.h"
#include "orchestra90.h"
#include "info_port.h"
#include "emudsk.h"
#include "acia_term.h"
#include "gime.h"
#include "clock_stop.h"
#include "gep9_io.h"
#include "enhancements.h"

// ROM flash images + Shared & Gerbil Engines
#include "disk11_rom.h"
#include "coco3_rom.h"
#include "flat65280.h"
#include "deep65280.h"
#include "gep9.h"
static void ResetCycleStreamForNewSession();

#include "gerbil_guts.h"
#include "centipede_targets.h"
#include "ports_cmd.h"
#include "gspoon.h"

// Cycle compression & 6809 instruction boundary tracker (Core 0 background)
#include "compress.h"
#include "cpu_tracker.h"

static cputracker::CpuTracker g_cpu_tracker;
static constexpr uint kCompressionThreshold = 60;
static uint32_t g_cycle_buffer[kCompressionThreshold + 4];
static uint8_t g_compression_buffer[4 * (kCompressionThreshold + 4) + 16];
static uint g_cycle_buf_count = 0;
static uint8_t g_pkt_write_counter_lsb = 0;
static bool g_compress_needs_reset = true;
static bool g_was_usb_tether_ok = false;

static void ResetCycleStreamForNewSession() {
  g_cycle_buf_count = 0;
  g_pkt_write_counter_lsb = 0;
  g_compress_needs_reset = true;
}

static bool IsRomPredicateForCompression(addr16 addr) {
  if (!SamTyBit) {
    if (g_gime_rom_mode) {
      return 0x8000u <= addr && addr < 0xFE00u;
    }
    return (0x8000u <= addr && addr < 0xC000u) ||
           (g_disk11_rom_enabled && 0xC000u <= addr && addr < 0xE000u);
  }
  return false;
}

static void FlushCyclesWithCompression() {
  if (g_cycle_buf_count == 0) return;
  if (!usb_tether_ok()) {
    g_cycle_buf_count = 0;
    g_compress_needs_reset = true;
    return;
  }
  bool reset_now = g_compress_needs_reset;
  if (reset_now) {
    ResetCompressCycles();
    g_pkt_write_counter_lsb = 0;
    g_compress_needs_reset = false;
  }
  uint writes_in_pkt = 0;
  for (uint i = 0; i < g_cycle_buf_count; i++) {
    if ((g_cycle_buffer[i] >> 24) == FG2BG_WRITE) {
      writes_in_pkt++;
    }
  }
  g_compression_buffer[0] = C_COMPRESSED_CYCLES;
  g_compression_buffer[1] = (uint8_t)((g_cycle_buf_count & 0x7Fu) | (reset_now ? 0x80u : 0x00u));
  g_compression_buffer[2] = g_pkt_write_counter_lsb;
  g_compression_buffer[3] = (uint8_t)(g_fg2bg_push_fail & 0xFFu);
  uint n = CompressCycles(g_compression_buffer + 4, g_cycle_buffer, g_cycle_buf_count,
                          IsRomPredicateForCompression);
  CobsEncodeAndTransmit(g_compression_buffer, n + 4, putchar_raw);
  g_pkt_write_counter_lsb = (uint8_t)(g_pkt_write_counter_lsb + writes_in_pkt);
  g_cycle_buf_count = 0;
}

static void InsertCycleWithCompression(uint32_t chore) {
  g_cycle_buffer[g_cycle_buf_count++] = chore;
  if (g_cycle_buf_count >= kCompressionThreshold) {
    FlushCyclesWithCompression();
  }
}

// Product-specific PicoRPC handler (`get-status`, `get-cycles`, `get-video-state`)
static bool centipede_rpc_hook(const pcb::RpcRequest& req, pcb::RpcResponse& resp) {
  if (req.method == "get-video-state") {
    FlushCyclesWithCompression();
    g_compress_needs_reset = true;
    resp.status = 0;
    resp.data.resize(54);
    uint8_t flags = 0;
    if (g_gime_rom_mode) flags |= 0x01u;
    if (SamTyBit) flags |= 0x02u;
    if (SamP1Bit) flags |= 0x04u;
    if (g_gerbil_loop_fn == gerbil_gime_foreground) flags |= 0x08u;
    if (g_deep_mmu_mask == 0x1Fu) flags |= 0x10u;
    if (coco_running) flags |= 0x20u;
    uint16_t sam = g_sam_bits;
    resp.data[0] = (char)flags;
    resp.data[1] = (char)g_pia1_ff22;
    resp.data[2] = (char)g_pia1_crb;
    resp.data[3] = (char)(sam & 0xFFu);
    resp.data[4] = (char)((sam >> 8) & 0xFFu);
    resp.data[5] = 0;
    resp.data[6] = (char)g_gime_init0;
    resp.data[7] = (char)g_gime_init1;
    resp.data[8] = (char)g_gime_irqenr;
    resp.data[9] = (char)g_gime_firqenr;
    for (int i = 4; i < 16; i++) {
      resp.data[6 + i] = (char)g_gime_regs[i];
    }
    for (int s = 0; s < 8; s++) {
      resp.data[22 + s] = (char)deep65280_mmu[0][s];
      resp.data[30 + s] = (char)deep65280_mmu[1][s];
    }
    for (int i = 0; i < 16; i++) {
      resp.data[38 + i] = (char)g_gime_palette[i];
    }
    return true;
  }
  if (req.method == "get-status") {
    resp.status = 0;
    uint e_hi = 0, e_lo = 0, e_trans = 0;
    bool last_e = gpio_get(G_E);
    for (uint i = 0; i < 2000; i++) {
      bool cur_e = gpio_get(G_E);
      if (cur_e) e_hi++;
      else       e_lo++;
      if (cur_e != last_e) {
        e_trans++;
        last_e = cur_e;
      }
    }
    char buf[256];
    snprintf(buf, sizeof(buf),
             "halt=%d rst=%d e_trans=%u e_hi=%u e_lo=%u f2b_sz=%u active=%d pc=%u/%u cy_idx=%u frozen=%d mmu0=%02x base0=%05x Ty=%d P1=%d",
             gpio_get(G_HALT), gpio_get(G_RESET), e_trans, e_hi, e_lo,
             (unsigned)fg2bg.size(),
             keyboard_injector::active ? 1 : 0,
             (unsigned)keyboard_injector::script_pc,
             (unsigned)keyboard_injector::script_len,
             (unsigned)g_cycle_history_idx,
             g_freeze_cycles ? 1 : 0,
             (unsigned)deep65280_mmu[0][0],
             (unsigned)task_mmu_base[0],
             SamTyBit ? 1 : 0,
             SamP1Bit ? 1 : 0);
    resp.message = buf;
    return true;
  }
  if (req.method == "get-cycles") {
    resp.status = 0;
    uint8_t cur = g_cycle_history_idx;
    resp.data.resize(256 * 4);
    for (uint i = 0; i < 256; i++) {
      uint8_t slot = (uint8_t)((cur + i) & 0xFFu);
      resp.data[i * 4 + 0] = (char)(g_cycle_history[slot].abus >> 8);
      resp.data[i * 4 + 1] = (char)(g_cycle_history[slot].abus & 0xFFu);
      resp.data[i * 4 + 2] = (char)g_cycle_history[slot].dbus;
      resp.data[i * 4 + 3] = (char)g_cycle_history[slot].flags;
    }
    if (req.flags & 1) {
      g_freeze_cycles = false;
    }
    return true;
  }
  return false;
}

// ═══════════════════════════════════════════════════════════════════
// Core 0 Background Scheduler & Coroutines
// ═══════════════════════════════════════════════════════════════════

static inline void putbyte(byte x) {
  unsigned char pkt[2] = {C_PUTCHAR, x};
  CobsEncodeAndTransmit(pkt, 2, putchar_raw);
}

static inline void drain_chars() {
  if (!usb_tether_ok()) {
    byte discard;
    while (fg2bg_chars.pop(discard)) {}
    return;
  }
  byte c;
  while (fg2bg_chars.pop(c)) {
    putbyte(c);
  }
}

// Poll USB packets for terminal input once `coco_running` is true (OS consoles + keyboard injector).
static inline void PollRuntimeUsbInput() {
  if (!coco_running || !usb_tether_ok()) return;
  while (true) {
    std::string* pkt = usb_packet_buf.Yoink([](std::string* s) {
      return s && s->length() > 0 &&
             (unsigned char)(*s)[0] != T_RPC &&
             (unsigned char)(*s)[0] != T_PICO_RPC &&
             (unsigned char)(*s)[0] != T_DISK_READ &&
             (unsigned char)(*s)[0] != T_WALLTIME;
    });
    if (!pkt) break;
    byte cmd = (byte)(*pkt)[0];
    if (cmd == T_HELLO) {
      // Discard tether's 3-byte startup handshake {T_HELLO=178, 129, 0} so 129/0
      // are never misread as keystrokes on /Term or the CoCo keyboard injector,
      // and reset the stateful cycle compressor for the newly attached tether session.
      ResetCycleStreamForNewSession();
      delete pkt;
      continue;
    }
    if (g_rx_poll_fn) {
      if ((cmd == C_PUTCHAR || cmd == 193) && pkt->size() >= 2) {
        byte c = (byte)(*pkt)[1];
        if (c == 10) c = 13;
        term_input.Put(c);
      } else if (cmd >= 1 && cmd <= 127) {
        for (size_t i = 0; i < pkt->length(); i++) {
          byte ch = (byte)(*pkt)[i];
          if (ch == 10) ch = 13;
          if (ch >= 1 && ch <= 127) term_input.Put(ch);
        }
      } else if (g_term_acia_arrow_keys && cmd >= 128 && cmd <= 133 && pkt->size() == 1) {
        term_input.Put(cmd);
      }
    } else {
      // BASIC / CoCo keyboard mode: feed incoming tether keystrokes into the PIA keyboard injector!
      if ((cmd == C_PUTCHAR || cmd == 193) && pkt->size() >= 2) {
        keyboard_injector::queue_char((*pkt)[1]);
      } else if (cmd >= 1 && cmd <= 131) {
        for (size_t i = 0; i < pkt->length(); i++) {
          keyboard_injector::queue_char((*pkt)[i]);
        }
      }
    }
    delete pkt;
  }
  if (g_rx_poll_fn) {
    g_rx_poll_fn();
  }
}

static volatile bool floppy_has_work = false;
static volatile uint floppy_pending_chore = 0;

static uint8_t drain_stack[4096]   __attribute__((aligned(8)));
static uint8_t floppy_stack[6144]  __attribute__((aligned(8)));
static uint8_t spoon_stack[16384]  __attribute__((aligned(8)));

static void drain_task(Coro& self) {
  uint idle_polls = 0;
  while (true) {
    bool usb_ok = usb_tether_ok();
    if (usb_ok != g_was_usb_tether_ok) {
      g_was_usb_tether_ok = usb_ok;
      if (usb_ok) {
        ResetCycleStreamForNewSession();
      }
    }

    uint popped = 0;
    while (popped < 64) {
      uint chore = 0;
      if (!fg2bg.pop(chore)) {
        break;
      }
      popped++;
      idle_polls = 0;
      uint kind = chore >> 24;
      uint addr = (chore >> 8) & 0xFFFFu;
      byte data = (byte)(chore & 0xFFu);

      if (kind == FG2BG_WRITE) {
        if (usb_ok) {
          g_cpu_tracker.ProcessCycle(cputracker::CycleType::WRITE, (uint16_t)addr, data);
          InsertCycleWithCompression(chore);
        }
        continue;
      }
      if (kind == FG2BG_READ) {
        if (usb_ok) {
          bool is_fic = g_cpu_tracker.ProcessCycle(cputracker::CycleType::READ, (uint16_t)addr, data);
          uint32_t tracked = is_fic ? (((uint32_t)FG0BG_FIC << 24) | (chore & 0x00FFFFFFu)) : chore;
          InsertCycleWithCompression(tracked);
        }
        continue;
      }

      switch (kind) {
        case FG2BG_PUTCHAR:
          if (usb_ok) putbyte(data);
          break;

        case FG2BG_FLOPPY_LATCH:
          FloppyImpl::BackgroundFifoFloppyLatch(data);
          break;

        case FG2BG_FLOPPY_COMMAND:
        case FG2BG_W_256:
        case FG2BG_FUZIX_SD_READ:
        case FG2BG_FUZIX_SD_WRITE:
        case FG2BG_EMUDSK:
          while (floppy_has_work) {
            coro_yield(&self);
          }
          floppy_pending_chore = chore;
          floppy_has_work = true;
          break;

        case FG2BG_GEP9_DISK: {
          Coro* saved = rpc::g_vfs_coro;
          rpc::g_vfs_coro = &self;
          uint8_t drv = g_gep9_disk_req.drive;
          if (drv > 3 || !g_drive_nodes[drv]) {
            g_gep9_disk_req.status.store(2, std::memory_order_release);
          } else {
            auto node = g_drive_nodes[drv];
            lfs_soff_t off = (lfs_soff_t)g_gep9_disk_req.sector * 256;
            if (node->seek(off, LFS_SEEK_SET, &self) != off) {
              g_gep9_disk_req.status.store(3, std::memory_order_release);
            } else {
              uint8_t task = g_gep9_disk_req.mem_task;
              uint32_t maddr24 = g_gep9_disk_req.mem_addr24;
              byte sec_buf[256];
              if (g_gep9_disk_req.command == 1) {
                if (node->read(sec_buf, 256) == 256) {
                  for (uint32_t i = 0; i < 256; i++) *gep9_mmu.TranslateWrite24(task, maddr24 + i) = sec_buf[i];
                  g_gep9_disk_req.status.store(1, std::memory_order_release);
                } else {
                  g_gep9_disk_req.status.store(4, std::memory_order_release);
                }
              } else if (g_gep9_disk_req.command == 2) {
                for (uint32_t i = 0; i < 256; i++) sec_buf[i] = *gep9_mmu.TranslateRead24(task, maddr24 + i);
                g_gep9_disk_req.status.store(node->write(sec_buf, 256) == 256 ? 1 : 4, std::memory_order_release);
              } else {
                g_gep9_disk_req.status.store(4, std::memory_order_release);
              }
            }
          }
          rpc::g_vfs_coro = saved;
          break;
        }

        case FG2BG_SPOON_ON_RESET:
          spoon_has_work = true;
          break;

        case FG2BG_START_KEYBOARD_INJECTOR:
          keyboard_injector::start_if_queued();
          break;

        case FG2BG_HOST_RESET:
          cobs_printf("\n[CoCo RESET detected -> restarting Centipede]\n");
          stdio_flush();
          sleep_ms(20);
          rp2350_reset_standard();
          break;

        default:
          break;
      }
      // Yield right away after any non-cycle chore so floppy/spoon/VFS tasks run immediately.
      break;
    }

    if (popped == 0) {
      if (++idle_polls >= 16) {
        idle_polls = 0;
        if (g_cycle_buf_count > 0 && usb_ok) {
          FlushCyclesWithCompression();
        }
      }
    }
    coro_yield(&self);
  }
}

static void floppy_task(Coro& self) {
  while (true) {
    if (!floppy_has_work) {
      coro_yield(&self);
      continue;
    }
    uint chore = floppy_pending_chore;
    uint kind = chore >> 24;
    byte data = (byte)(chore & 0xFFu);

    switch (kind) {
      case FG2BG_FLOPPY_COMMAND:
        FloppyImpl::BackgroundFifoFloppyCommand(self, chore, data);
        break;
      case FG2BG_W_256:
        FloppyImpl::BackgroundFifoFloppyW256(self, data);
        break;
      case FG2BG_FUZIX_SD_READ: {
        Coro* saved = rpc::g_vfs_coro;
        rpc::g_vfs_coro = &self;
        uint32_t lba = chore & 0x00FFFFFFu;
        if (!g_fuzix_disk_opened && vfs_file_open(&g_fuzix_disk_file, "/pc/disk2.img", LFS_O_RDWR) >= 0) {
          g_fuzix_disk_opened = true;
        }
        if (g_fuzix_disk_opened) {
          vfs_file_seek(&g_fuzix_disk_file, (lfs_soff_t)lba * 512, LFS_SEEK_SET, &self);
          vfs_file_read(&g_fuzix_disk_file, fuzix_sd_buf, 512);
        }
        __dmb();
        fuzix_sd_ctl = 0xE0u;
        rpc::g_vfs_coro = saved;
        break;
      }
      case FG2BG_FUZIX_SD_WRITE: {
        Coro* saved = rpc::g_vfs_coro;
        rpc::g_vfs_coro = &self;
        uint32_t lba = chore & 0x00FFFFFFu;
        if (!g_fuzix_disk_opened && vfs_file_open(&g_fuzix_disk_file, "/pc/disk2.img", LFS_O_RDWR) >= 0) {
          g_fuzix_disk_opened = true;
        }
        if (g_fuzix_disk_opened) {
          vfs_file_seek(&g_fuzix_disk_file, (lfs_soff_t)lba * 512, LFS_SEEK_SET, &self);
          vfs_file_write(&g_fuzix_disk_file, fuzix_sd_buf, 512);
        }
        __dmb();
        fuzix_sd_ctl = 0x80u;
        rpc::g_vfs_coro = saved;
        break;
      }
      case FG2BG_EMUDSK:
        emudsk_service_bg(&self);
        break;
      default:
        break;
    }
    floppy_has_work = false;
  }
}

static void spoon_task(Coro& self) {
  while (true) {
    if (!spoon_has_work) {
      coro_yield(&self);
      continue;
    }
    GerbilReleaseHalt();
    gspoon::BackgroundSpoonFeeder(&self);
    spoon_has_work = false;
  }
}

static bool timer_60hz_callback(struct repeating_timer*) {
  if (g_timer_60hz_fn) {
    g_timer_60hz_fn();
  }
  return true;
}

static void centipede_core0_background() {
  struct repeating_timer timer60;
  add_repeating_timer_us(-16667, timer_60hz_callback, nullptr, &timer60);

  Coro drain, floppy, spoon;
  coro_create(&drain, drain_task, drain_stack, sizeof(drain_stack));
  coro_create(&floppy, floppy_task, floppy_stack, sizeof(floppy_stack));
  coro_create(&spoon, spoon_task, spoon_stack, sizeof(spoon_stack));

  cobs_printf("Centipede v5: background coroutines initialized.\n");

  uint64_t last_tick_20ms = 0;
  while (true) {
    drain_chars();
    coro_resume(&drain);
    if (PumpUsbCobsHasWork()) PumpUsbCobs();

    drain_chars();
    coro_resume(&floppy);
    if (PumpUsbCobsHasWork()) PumpUsbCobs();

    drain_chars();
    coro_resume(&spoon);
    if (PumpUsbCobsHasWork()) PumpUsbCobs();

    PollRuntimeUsbInput();
    Phase2PollEnhancements();

    uint64_t cur_tick = get_system_ticks_20ms();
    if (cur_tick != last_tick_20ms) {
      last_tick_20ms = cur_tick;
      keyboard_injector::tick();
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// Core 1 Foreground Entry (strictly IN_RAM, zero flash calls!)
// ═══════════════════════════════════════════════════════════════════

static FORCE_INLINE bool detect_e_clock_sram() {
  uint transitions = 0;
  bool last_e = (sio_hw->gpio_in & (1u << G_E)) != 0;
  for (uint i = 0; i < 100000; i++) {
    bool cur_e = (sio_hw->gpio_in & (1u << G_E)) != 0;
    if (cur_e != last_e) {
      transitions++;
      last_e = cur_e;
      if (transitions >= 16) {
        startup_e_clock_detected = true;
        return true;
      }
    }
  }
  return false;
}

void IN_RAM centipede_core1_entry() {
  save_and_disable_interrupts();

  if (!detect_e_clock_sram()) {
    // No CoCo E clock yet — start USB-only Tcl REPL on Core 0 and poll until CoCo powers on.
    spoon_has_work = true;
    while (!detect_e_clock_sram()) {
      for (volatile uint i = 0; i < 2500000; i++) {}
    }
  }

  // CoCo E/Q clocks are running: spoonfeed PMODE4 Tcl console until `bye` (`BG2FG_EXIT_CONSOLE`)!
  gspoon::SpoonfeedConsoleOnReset();

  PUSH_TO_BG(FG2BG_START_KEYBOARD_INJECTOR, 0, 0);
  coco_running = true;

  // Tail-call the specialized IN_RAM Gerbil foreground engine selected by Core 0 at `bye`!
  GerbilLoopFn fn = g_gerbil_loop_fn;
  if (fn) {
    fn();
  } else {
    gerbil_sam64k_foreground();
  }
}

// ═══════════════════════════════════════════════════════════════════
// Hardware Pin Init, QMI Flash Timing, and `main()`
// ═══════════════════════════════════════════════════════════════════

static void __not_in_flash_func(safe_adjust_flash_speed)() {
  uint32_t ints = save_and_disable_interrupts();
  constexpr uint32_t clkdiv = 4;   // 250 MHz / 4 = 62.5 MHz QSPI
  constexpr uint32_t rxdelay = 4;
  hw_write_masked(
      &qmi_hw->m[0].timing,
      ((clkdiv << QMI_M0_TIMING_CLKDIV_LSB) & QMI_M0_TIMING_CLKDIV_BITS) |
          ((rxdelay << QMI_M0_TIMING_RXDELAY_LSB) & QMI_M0_TIMING_RXDELAY_BITS),
      QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
  restore_interrupts(ints);
}

static void InitializeCentipedePins() {
  auto set_in = [](uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_set_pulls(pin, false, false);
  };
  auto set_od = [](uint pin) {
    gpio_init(pin);
    gpio_put(pin, 0);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
  };

  for (uint pin = 0; pin < 8; pin++) set_in(pin);       // D0..D7
  set_in(G_CTS);
  set_in(G_SCS);
  set_in(G_RW);
  set_in(G_E);
  set_in(G_Q);

  gpio_init(G_LED);
  gpio_set_dir(G_LED, GPIO_OUT);
  gpio_put(G_LED, 0);

  gpio_init(G_SND);
  gpio_set_dir(G_SND, GPIO_IN);

  set_od(G_CART);
  set_od(G_SLENB);
  set_od(G_HALT);
  set_od(G_NMI);
  set_od(G_RESET);

  for (uint pin = 32; pin < 48; pin++) set_in(pin);     // A0..A15
}

int main() {
  InitializeCentipedePins();
  FlashLabel::InitLabel();
  set_sys_clock_khz(MHz * 1000, true);
  stdio_usb_init();
  safe_adjust_flash_speed();

  // Hold CoCo HALT# and RESET# low across startup LED blink so 6809E + PIAs reset cleanly.
  gpio_put(G_HALT, 0);  gpio_set_dir(G_HALT, GPIO_OUT);
  gpio_put(G_RESET, 0); gpio_set_dir(G_RESET, GPIO_OUT);
  for (uint i = 0; i < 3; i++) {
    gpio_put(G_LED, 1);
    sleep_ms(120);
    gpio_put(G_LED, 0);
    sleep_ms(120);
  }
  gpio_set_dir(G_RESET, GPIO_IN);
  gpio_set_dir(G_HALT, GPIO_IN);

  FlashLabel::PrintLabel();
  init_lfs();
  start_20ms_timer();

  g_product_rpc_hook = centipede_rpc_hook;
  g_pre_bye_rpc_hook = centipede_prepare_launch_on_core0;

  global_tcl_interp = Tcl_CreateInterp();
  register_tcl_commands(global_tcl_interp);
  register_ports_commands(global_tcl_interp);
  RegisterEnhancementTclCommands(global_tcl_interp);

  g_config.SetStandard();
  if (boot_mode_check == boot_mode + BOOT_MODE_CHECKER &&
      (boot_mode == 3 || boot_mode == 89 || boot_mode == 90)) {
    g_config.become_coco3 = true;
  }
  g_config.trace_reads = false;
  g_config.floppy_pc = false;
  set_floppy_names();

  g_cpu_tracker.Reset();
  g_cpu_tracker.SetMode(cputracker::TrackerMode::PREDICTING);
  g_cpu_tracker.EnableRegisterSnarfing(true);
  ResetCompressCycles();

  // Start Gerbil PIO state machine on pio0 sm0, launch Core 1, then run Core 0 background!
  pio_clear_instruction_memory(pio0);
  pio_add_program_at_offset(pio0, &gerbil_program, 0);
  gerbil_program_init(pio0, 0, 0);
  cobs_printf("#gerbil_program.length=%d\n", gerbil_program.length);

  multicore_launch_core1(centipede_core1_entry);
  centipede_core0_background();
  return 0;
}
