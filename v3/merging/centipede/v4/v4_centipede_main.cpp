// v4_centipede_main.cpp — Centipede v4 firmware build.
//
// This is functionally equivalent to v1/firmware/centipede.cpp,
// but structured using the v4 architecture:
//   - V4CoreEngine<T> replaces v1's CoreEngine<T>
//   - v1's CRTP mixins (DoCoco64k, DoFloppy) are reused directly
//   - Globals, macros, and infrastructure match v1's definitions
//
// Build: cmake from v4/ directory.

// ═══════════════════════════════════════════════════════════════════
// Configuration defines (must come BEFORE any includes)
// These match v1/firmware/centipede.cpp exactly.
// ═══════════════════════════════════════════════════════════════════

#ifndef MHz
#define MHz 250
#endif

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
#define USE_ORCHESTRA90 0  // Disabled in v4 for now
#define STACK_SIZE   (20 * 1024)

enum TracingSpeed { NO_SPEED, SLOW_SPEED, MEDIUM_SPEED, FAST_SPEED };
// constexpr TracingSpeed Speed = MEDIUM_SPEED;
constexpr TracingSpeed Speed = SLOW_SPEED;

#ifndef CENTIPEDE_REV
#define CENTIPEDE_REV 3226  // 32z
#endif

#define DBUS_HOLD_CYCLES 0

// Flow control tuning
#define COMPRESSION_MAX 100
#define FG2BG_HIGH_WATERMARK 1000
#define FG2BG_LOW_WATERMARK 500

#define CENTIPEDE_INVERT_EQ 1

// Compiler hints
#define IN_FLASH __in_flash("FLASH")
#define IN_RAM __not_in_flash("centipede")

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define FORCE_INLINE inline __attribute__((always_inline))

// ═══════════════════════════════════════════════════════════════════
// Pico SDK headers
// ═══════════════════════════════════════════════════════════════════
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

#include "../v1/littlefs/lfs-centipede.h"
#include "../v1/littlefs/lfs.h"
#include "../v1/littlefs/lfs_util.h"

// CentipedeConfig is defined in v1/firmware/config.h.
// It will be included transitively by tcl_commands.h -> menu.h -> config.h.

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
#include <array>
#include <atomic>
#include <cstdint>
#include <string>

// ═══════════════════════════════════════════════════════════════════
// Tcl interpreter
// ═══════════════════════════════════════════════════════════════════
#include "../v1/tcl6.7c/tcl.h"
Tcl_Interp* global_tcl_interp = nullptr;

const char HexAlphabet[] =
    "0123456789ABCDEFXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
    "XXXXXXX";

// ═══════════════════════════════════════════════════════════════════
// GPIO pin definitions
// ═══════════════════════════════════════════════════════════════════
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
#define G_CTS 18
#define G_SCS 19
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
#else
#define G_LED 25
#define G_NMI 26
#define G_RESET 27
#define G_HALT 28
#define G_SLENB 29
#endif

#define G_D0 0
#define G_A0 32

// ═══════════════════════════════════════════════════════════════════
// Helper macros and functions
// ═══════════════════════════════════════════════════════════════════
#define SET_LED(X) gpio_put(G_LED, (X))
#define volatile_sio_hw ((volatile sio_hw_t*)SIO_BASE)

using byte = unsigned char;
using addr16 = uint16_t;
using uint = unsigned int;

using IOReader = byte (*)(uint addr);
using IOWriter = void (*)(uint addr, byte data);

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

void HaltOn() { gpio_set_dir(G_HALT, GPIO_OUT); }
void HaltOff() { gpio_set_dir(G_HALT, GPIO_IN); }

#define BOOT_MODE_CHECKER 0x56781234u
uint32_t  __uninitialized_ram(boot_mode);
uint32_t  __uninitialized_ram(boot_mode_check);
std::atomic<bool> startup_e_clock_detected{false};

bool IN_RAM detect_e_clock() {
  uint count_high = 0, count_low = 0, transitions = 0;
  bool last_state = gpio_get(G_E);
  for (uint i = 0; i < 10000; i++) {
    bool current_state = gpio_get(G_E);
    if (current_state) count_high++; else count_low++;
    if (current_state != last_state) { transitions++; last_state = current_state; }
  }
  bool ok = count_high > 200 && count_low > 200 && transitions >= 10;
  if (ok) startup_e_clock_detected = true;
  return ok;
}

// ═══════════════════════════════════════════════════════════════════
// Cross-core FIFO and USB pipeline
// ═══════════════════════════════════════════════════════════════════
#include "../v1/firmware/cross-core.h"
#include "../v1/firmware/usb_pipeline.h"

CircBuf<unsigned char, 1024> usb_raw_buf;
CircBuf<std::string*, 64> usb_packet_buf;

UsbReceiver usb_receiver(usb_raw_buf);
CobsDecoder<1024, 64> cobs_decoder(usb_raw_buf, usb_packet_buf);

CrossCoreFIFO<uint, 8192> fg2bg;
CrossCoreFIFO<uint, 8192> bg2fg;

volatile bool fg_halt_for_flow_control = false;

FORCE_INLINE void IN_RAM FlowControlCheck() {
  if (fg_halt_for_flow_control) {
    if (fg2bg.size() < FG2BG_LOW_WATERMARK) {
      HaltOff();
      fg_halt_for_flow_control = false;
    }
  } else {
    if (fg2bg.size() > FG2BG_HIGH_WATERMARK) {
      HaltOn();
      fg_halt_for_flow_control = true;
    }
  }
}

#define SAY(C) PUSH_TO_BG(FG2BG_PUTCHAR, 0, (C) & 255)
#define PUSH_TO_BG(T, A, D) fg2bg.push(((T) << 24) | ((A) << 8) | (D))

// ═══════════════════════════════════════════════════════════════════
// COBS TX
// ═══════════════════════════════════════════════════════════════════
#define INCLUDING
#include "../v1/firmware/cobs_tx.h"

// ═══════════════════════════════════════════════════════════════════
// ROM data and globals
// ═══════════════════════════════════════════════════════════════════
#include "../v1/firmware/bug.h"
#include "../v1/firmware/disk11_rom.h"
#include "../v1/firmware/egg.h"

IOReader IOReaders[256];
IOWriter IOWriters[256];
byte ram[64 * 1024];

// ═══════════════════════════════════════════════════════════════════
// Protocol constants (matching v1's enum values)
// ═══════════════════════════════════════════════════════════════════
#define C_PUTCHAR 193
#define C_RAM2_WRITE 195  // 0xC3
#define C_RAM2_READ  211  // 0xD3
#define C_DISK_READ  173
#define C_DISK_WRITE 174

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
};

enum BG2FG_Tags {
  BG2FG_PEEK = 1,
  BG2FG_POKE = 2,
  BG2FG_EXIT_CONSOLE = 3,
};

// ═══════════════════════════════════════════════════════════════════
// NMI / HALT macros
// ═══════════════════════════════════════════════════════════════════
#define ASSERT_HALT() gpio_set_dir(G_HALT, GPIO_OUT)
#define RELEASE_HALT() gpio_set_dir(G_HALT, GPIO_IN)

volatile bool nmi_pending = false;
#define ASSERT_NMI() do { \
    gpio_set_dir(G_NMI, GPIO_OUT); \
    nmi_pending = true; \
  } while(0)
#define RELEASE_NMI() gpio_set_dir(G_NMI, GPIO_IN)

// ═══════════════════════════════════════════════════════════════════
// Gerbil PIO
// ═══════════════════════════════════════════════════════════════════
#include "gerbil.pio.h"

#define GERBIL_GET() gerbil_program_get_word(pio, sm)
#define GERBIL_DRIVE(X) gerbil_program_put_word(pio, sm, 0x100 | (X))
#define GERBIL_PASS() gerbil_program_put_word(pio, sm, 0)

// ═══════════════════════════════════════════════════════════════════
// v1 headers — include order matches v1/firmware/centipede.cpp exactly.
// Many of these have include guards. The early explicit includes
// ensure correct ordering; later transitive includes are no-ops.
// ═══════════════════════════════════════════════════════════════════
#include "../v1/firmware/abort.h"
#include "../v1/firmware/console.h"
#include "../v1/firmware/keyboard_injector.h"
#include "../v1/firmware/coro.h"
#include "../v1/firmware/flash-label.h"
#include "../v1/firmware/config.h"
#include "../v1/firmware/floppy.h"
#include "../v1/firmware/rtc.h"

// Spoon task work flag — must be declared before gspoon.h
volatile bool spoon_has_work = false;

#include "../v1/firmware/gspoon.h"
#include "../v1/firmware/tcl_io.h"
#include "../v1/firmware/vfs.h"

// SAM bits (for coco64k.h)
bool SamP1Bit;
bool SamTyBit;

#include "../v1/firmware/coco64k.h"
#include "../v1/firmware/littlefs.h"
#include "../v1/firmware/tcl_commands.h"
#include "../v1/firmware/pcb.h"
#include "../v1/firmware/pico_rpc.h"

// ═══════════════════════════════════════════════════════════════════
// Compressed cycles (disabled for now)
// ═══════════════════════════════════════════════════════════════════
#ifndef COMPRESS_CYCLES
#define COMPRESS_CYCLES 0
#endif
#if COMPRESS_CYCLES
#include "../v1/firmware/compress.h"
#endif
inline void ResetCompressCycles() {}
#if COMPRESS_CYCLES
inline void InsertCycleWithCompression(uint chore) {}
inline void FlushPartialCycleBuffer() {}
#endif

// ═══════════════════════════════════════════════════════════════════
// Counter globals
// ═══════════════════════════════════════════════════════════════════
volatile uint push_fail_counter = 0;
volatile uint write_counter = 0;

// rp2350_reset_standard, flash-label, rtc, restart:
// All provided by v1 headers included via tcl_commands.h.
#include <hardware/watchdog.h>

// ═══════════════════════════════════════════════════════════════════
// Coroutines
// ═══════════════════════════════════════════════════════════════════
#include "../v1/firmware/coro.h"

// ═══════════════════════════════════════════════════════════════════
// OPEN_DRAIN macro
// ═══════════════════════════════════════════════════════════════════
#define OPEN_DRAIN(PIN)        \
  gpio_init(PIN);              \
  gpio_set_dir(PIN, GPIO_OUT); \
  gpio_put(PIN, 0);            \
  gpio_set_dir(PIN, GPIO_IN);  \
  gpio_set_pulls(PIN, true, false);

// ═══════════════════════════════════════════════════════════════════
// V4CoreEngine — the foreground + background engine
// Extracted from v1/firmware/centipede.cpp CoreEngine<T>
// ═══════════════════════════════════════════════════════════════════

void IN_RAM core1_trampoline();
void IN_RAM core0_trampoline();

template <class T>
class V4CoreEngine {
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
#ifdef G_SND
    INPUT(G_SND);
#endif
    INPUT(G_CTS);
    INPUT(G_SCS);
#ifdef G_RESET
    INPUT(G_RESET);
#endif
    INPUT(G_SLENB);

    OPEN_DRAIN(G_HALT);
    OPEN_DRAIN(G_NMI);
#ifdef G_CART
    OPEN_DRAIN(G_CART);
#endif

    for (uint i = 32; i <= 47; i++) {
      gpio_init(i);
      gpio_set_dir(i, GPIO_IN);
      gpio_set_pulls(i, false, false);
    }
    gpio_init(G_LED);
    gpio_set_dir(G_LED, GPIO_OUT);
    SET_LED(0);
  }

  // Coroutine stacks
  static inline uint8_t drain_stack[STACK_SIZE] __attribute__((aligned(8)));
  static inline uint8_t floppy_stack[STACK_SIZE] __attribute__((aligned(8)));
  static inline uint8_t spoon_stack[STACK_SIZE] __attribute__((aligned(8)));

  static inline volatile uint floppy_pending_chore;
  static inline volatile bool floppy_has_work;

  // ── drain_task ──
  static void drain_task(Coro& self) {
    while (true) {
      if (nmi_pending) {
        nmi_pending = false;
        gpio_set_dir(G_NMI, GPIO_IN);
      }
      keyboard_injector::tick();

      uint chore = 0;
      if (!fg2bg.pop(chore)) {
#if COMPRESS_CYCLES
        FlushPartialCycleBuffer();
#endif
        coro_yield(&self);
        continue;
      }

      const uint chore_num = chore >> 24;
      const byte chore_byte = 0xFF & chore;

      switch (chore_num) {
        case FG2BG_PUTCHAR:
          if (chore_byte) cobs_putchar(chore_byte);
          break;
        case FG2BG_START_KEYBOARD_INJECTOR:
          keyboard_injector::start_if_queued();
          break;
        case FG2BG_READ:
#if COMPRESS_CYCLES
          InsertCycleWithCompression(chore);
#else
          if (chore_byte && usb_tether_ok()) {
            unsigned char pkt[4] = {C_RAM2_READ,
                                    (unsigned char)(chore >> 16),
                                    (unsigned char)(chore >> 8),
                                    (unsigned char)chore};
            CobsEncodeAndTransmit(pkt, 4, putchar_raw);
          }
#endif
          break;
        case FG2BG_WRITE:
          write_counter++;
#if COMPRESS_CYCLES
          InsertCycleWithCompression(chore);
#else
          if (usb_tether_ok()) {
            unsigned char pkt[4] = {C_RAM2_WRITE,
                                    (unsigned char)(chore >> 16),
                                    (unsigned char)(chore >> 8),
                                    (unsigned char)chore};
            CobsEncodeAndTransmit(pkt, 4, putchar_raw);
          }
#endif
          break;
        case FG2BG_NMI:
          break;
        case FG2BG_FLOPPY_LATCH:
        case FG2BG_FLOPPY_COMMAND:
        case FG2BG_W_256:
          while (floppy_has_work) coro_yield(&self);
          floppy_pending_chore = chore;
          floppy_has_work = true;
          break;
        case FG2BG_SPOON_ON_RESET:
          spoon_has_work = true;
          break;
        case FG2BG_PEEK_REPLY:
          break;
        default:
          cobs_printf("\nWUT? CHORE=%x\n", chore);
      }
      coro_yield(&self);
    }
  }

  // ── floppy_task ──
  static void floppy_task(Coro& self) {
    while (true) {
      if (!floppy_has_work) { coro_yield(&self); continue; }
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
          T::BackgroundFifoFloppyW256(self);
          break;
      }
      floppy_has_work = false;
    }
  }

  // ── spoon_task ──
  static void spoon_task(Coro& self) {
    while (true) {
      if (!spoon_has_work) { coro_yield(&self); continue; }
      HaltOff();
      gspoon::BackgroundSpoonFeeder(&self);
      spoon_has_work = false;
    }
  }

  // ── background ──
  FORCE_INLINE static void background() {
    Coro drain, floppy, spoon;
    coro_create(&drain, drain_task, drain_stack, sizeof(drain_stack));
    coro_create(&floppy, floppy_task, floppy_stack, sizeof(floppy_stack));
    coro_create(&spoon, spoon_task, spoon_stack, sizeof(spoon_stack));
    cobs_printf("Background: coroutines initialized.\n");

    while (true) {
      coro_resume(&drain);
      if (PumpUsbCobsHasWork()) PumpUsbCobs();
      coro_resume(&floppy);
      if (PumpUsbCobsHasWork()) PumpUsbCobs();
      coro_resume(&spoon);
      if (PumpUsbCobsHasWork()) PumpUsbCobs();
    }
  }

  // ── foreground ──
  FORCE_INLINE static void foreground() {
    save_and_disable_interrupts();
    const PIO pio = pio0;
    constexpr uint sm = 0;

    if (!detect_e_clock()) {
      cobs_printf(" [-E] ");
      spoon_has_work = true;
      while (!detect_e_clock()) {
        for (volatile uint i = 0; i < 2500000; i++) {}
      }
      cobs_printf(" [+E] ");
    }

    gspoon::SpoonfeedConsoleOnReset();
    PUSH_TO_BG(FG2BG_START_KEYBOARD_INJECTOR, 0, 0);

    uint cycle = 0;
    bool floppy_emulation = centipede_config.floppy_fd || centipede_config.floppy_pc;
    while (true) {
      const uint signals = GERBIL_GET();
      FlowControlCheck();

      const bool reading = ((signals & (1u << G_RW)) != 0);
      const uint abus = volatile_sio_hw->gpio_hi_in & 0xFFFF;
      byte dbus = 0x00;

      constexpr uint NEG_CTS = (1 << G_CTS);
      constexpr uint NEG_SCS = (1 << G_SCS);
      constexpr uint NEG_SELECTS = NEG_CTS | NEG_SCS;

      if (LIKELY(!floppy_emulation || (signals & NEG_SELECTS) == NEG_SELECTS)) {
        if (LIKELY(reading)) {
          if (0xFF00 <= abus) {
            if (UNLIKELY(abus == 0xFF00 && keyboard_injector::active)) {
              byte probe = ram[0xFF02];
              byte sense = 0x7F;
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
                dbus = (byte)(GERBIL_GET());
              }
            }
          } else if (centipede_config.rom_disk11
                     && not T::UseCoco64kRam(abus)
                     && 0xC000 <= abus && abus < 0xE000) {
            dbus = disk11_rom[abus & 0x1FFF];
            GERBIL_DRIVE(dbus);
          } else if (centipede_config.ram_64k && T::UseCoco64kRam(abus)) {
            uint atrans = T::TranslateCoco64kRamAddress(abus);
            dbus = ram[atrans];
            GERBIL_DRIVE(dbus);
          } else {
            GERBIL_PASS();
            dbus = (byte)(GERBIL_GET());
          }
          if (centipede_config.trace_reads
              || (centipede_config.trace_writes && 0xFF00 <= abus)) {
            T::PushFifoRead(abus, dbus);
          }
        } else {
          dbus = (byte)(GERBIL_GET());
          if (0xFF00 <= abus) {
            auto w = IOWriters[abus & 0xFF];
            if (w) w(abus, dbus);
            ram[abus] = dbus;
            T::PushFifoWrite(abus, dbus);
          } else {
            uint atrans = T::UseCoco64kRam(abus)
                              ? T::TranslateCoco64kRamAddress(abus)
                              : abus;
            ram[atrans] = dbus;
            if (centipede_config.trace_writes) {
              T::PushFifoWrite(atrans, dbus);
            }
          }
        }
      } else {
        if (LIKELY(reading)) {
          if ((signals & NEG_SCS) == 0) {
            T::ReadScsFloppy(abus, dbus);
          }
          GERBIL_DRIVE(dbus);
          if (true) T::PushFifoRead(abus, dbus);
        } else {
          dbus = (byte)(GERBIL_GET());
          ram[abus] = dbus;
          if (LIKELY((signals & NEG_SCS) == 0)) {
            T::WriteScsFloppy(abus, dbus);
          }
          T::PushFifoWrite(abus, dbus);
        }
      }

      ++cycle;

#if ON_RESET_DO_SPOONFEED_CONSOLE
      if ((signals & (1 << G_RESET)) == 0) break;
#endif
    }  // end while true

    rp2350_reset_standard();
  }  // end foreground

  FORCE_INLINE static void PushFifoRead(uint abus, byte dbus) {
    if (abus != 0xFFFF) {
      PUSH_TO_BG(FG2BG_READ, abus, dbus);
    }
  }

  FORCE_INLINE static void PushFifoWrite(uint abus, byte dbus) {
    if (Speed <= MEDIUM_SPEED) {
      bool ok = fg2bg.push(((FG2BG_WRITE) << 24) | ((abus) << 8) | (dbus));
      if (!ok) push_fail_counter++;
    }
  }

  FORCE_INLINE static void RunCores(void (*core1_func)(void),
                                    void (*core0_func)(void)) {
    const PIO pio = pio0;
    constexpr uint sm = 0;

    pio_clear_instruction_memory(pio);
    pio_add_program_at_offset(pio, &gerbil_program, 0);
    gerbil_program_init(pio, sm, 0);
    cobs_printf("#gerbil_program.length=%d\n", gerbil_program.length);

    multicore_launch_core1(core1_func);
    core0_func();
  }
};

// ═══════════════════════════════════════════════════════════════════
// Engine composition
// ═══════════════════════════════════════════════════════════════════
class Engine : public DoFloppy<Engine>,
               public DoCoco64k<Engine>,
               public V4CoreEngine<Engine> {
 public:
  static void RunEngine() {
    InitCoco64k();
    ResetCompressCycles();
    RunCores(core1_trampoline, core0_trampoline);
  }
};

void IN_RAM core1_trampoline() { Engine::foreground(); }
void IN_RAM core0_trampoline() { Engine::background(); }

// ═══════════════════════════════════════════════════════════════════
// Flash speed adjustment
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
// main
// ═══════════════════════════════════════════════════════════════════
int IN_RAM main() {
  Engine::InitializePins();
  FlashLabel::InitLabel();
#if MHz != 150
  set_sys_clock_khz(MHz * 1000, true);
#endif
  stdio_usb_init();
  safe_adjust_flash_speed();

  OUTPUT(G_HALT, 0);
#ifdef G_RESET
  OUTPUT(G_RESET, 0);
#endif
  for (uint i = 0; i < 5; i++) {
    SET_LED(1); sleep_ms(200);
    SET_LED(0); sleep_ms(200);
  }
#ifdef G_RESET
  INPUT(G_RESET);
#endif
  INPUT(G_HALT);

  FlashLabel::PrintLabel();
  init_lfs();
  start_20ms_timer();
  global_tcl_interp = Tcl_CreateInterp();
  register_tcl_commands(global_tcl_interp);
  centipede_config.SetAll(true);
  centipede_config.trace_reads = false;
  centipede_config.floppy_pc = false;
  set_floppy_names();

  Engine::RunEngine();
}
