// v4_tfr911h_main.cpp — TFR911H v4 build (no Tcl, no LittleFS).
//
// Self-contained compilation unit that produces tfr911h_v4.uf2.
// Includes TFR911 headers from tmanager911/very-turbos/ directly.
// Boots OS9 from compiled-in RomList, ACIA console via USB.
//
// Based on: v3/tmanager911/very-turbos/veryturbos.cpp (510 lines)

#define TRACE 0
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
#include <stdio.h>

#include <cstring>
#include <atomic>
#include <array>

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
};

#define SAY(C) PUSH_TO_BG(FG2BG_PUTCHAR, 0, (C) & 255)
#define PUSH_TO_BG(TAG, A, D) fg2bg.push(((TAG) << 24) | ((A) << 8) | (D))

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
// COBS output helpers
// ═══════════════════════════════════════════════════════════════════
extern "C" {
extern int stdio_usb_in_chars(char* buf, int length);
}

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

void cobs_printf(const char* fmt, ...) {
  char buf[256];
  buf[0] = C_PUTCHAR;
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buf + 1, sizeof(buf) - 1, fmt, args);
  va_end(args);
  if (len > 0) {
    if (len >= (int)sizeof(buf) - 1) len = sizeof(buf) - 2;
    CobsEncodeAndTransmit((const unsigned char*)buf, len + 1,
                          [](int ch) { putchar_raw(ch); });
  }
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
  byte pkt[4] = {C_RAM2_WRITE, (byte)(addr >> 8), (byte)addr, data};
  CobsEncodeAndTransmit(pkt, 4, [](int ch) { putchar_raw(ch); });
}

// ═══════════════════════════════════════════════════════════════════
// CircBuf + USB/COBS input pipeline
// ═══════════════════════════════════════════════════════════════════
#include "circbuf.h"

// PIO header (generated by cmake)
#include "pio_veryturbos.pio.h"

// Flash label
#include "../../../tmanager911/very-turbos/flash-label.h"

CircBuf<unsigned char, 1024> usb_input;
CircBuf<std::string*, 16> usb_cobs_output;
CobsDecoder<1024, 16> cobs_decoder(usb_input, usb_cobs_output);
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

bool TryGetUsbByte(char* ptr) {
  int rc = stdio_usb_in_chars(ptr, 1);
  return (rc != PICO_ERROR_NO_DATA);
}

#define Printf if(false)printf

// ═══════════════════════════════════════════════════════════════════
// USB polling (called from foreground outer loop)
// ═══════════════════════════════════════════════════════════════════
void PollUsbInput() {
  while (1) {
    char x = 0;
    bool ok = TryGetUsbByte(&x);
    if (ok) {
      usb_input.Put(x);
    } else {
      break;
    }
  }

  cobs_decoder.Tick();

  while (usb_cobs_output.NumBuffered() > 0) {
    std::string* pkt_ptr = usb_cobs_output.Take();
    std::string pkt = *pkt_ptr;
    delete pkt_ptr;

    if (pkt.size() == 0) continue;

    byte cmd = pkt[0];
    switch (cmd) {
      case C_PUTCHAR:
        if (pkt.size() >= 2) {
          byte c = pkt[1];
          if (c == 10) c = 13;
          term_input.Put(c);
        }
        break;

      case C_PRE_LOAD:
        if (pkt.size() >= 3) {
          uint addr = ((uint)(byte)pkt[1] << 8) | (byte)pkt[2];
          for (uint i = 3; i < pkt.size(); i++) {
            ram[addr & 0xFFFF] = (byte)pkt[i];
            addr++;
          }
        }
        break;

      case C_REBOOT:
        reset_usb_boot(0, 0);
        break;

      case C_DISK_READ:
        break;

      case T_HELLO:
        break;

      case T_COMMAND:
        break;

      default:
        if (cmd >= 1 && cmd <= 127) {
          byte c = cmd;
          if (c == 10) c = 13;
          term_input.Put(c);
        }
        break;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// Include TFR911 headers from v1
// ═══════════════════════════════════════════════════════════════════
#include "../../../tmanager911/very-turbos/turbo9sim.h"
#include "../../../tmanager911/very-turbos/romlist.h"

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
// RunReset — bit-bang E/Q clocks during RESET (open-drain RESET)
// ═══════════════════════════════════════════════════════════════════
void RunReset() {
  gpio_set_dir(RESET, GPIO_OUT);  // Assert RESET (pulls low via latch=0)
  for (int i = 0; i < 1000; i++) {
    Delay(10);
    gpio_put(Q, 1);
    Delay(10);
    gpio_put(E, 1);
    Delay(10);
    gpio_put(Q, 0);
    Delay(10);
    gpio_put(E, 0);
  }
  gpio_set_dir(RESET, GPIO_IN);  // Release RESET (pulled high)
}

// ═══════════════════════════════════════════════════════════════════
// Timer (1ms tick on core 0)
// ═══════════════════════════════════════════════════════════════════
constexpr uint GROUP_SIZE = 10000;
uint milliseconds;
volatile int cycles;  // Updated by foreground, read by background for stats
struct repeating_timer TimerData;
bool TimerCallback(repeating_timer_t* rt) {
  milliseconds++;
  return true;
}

// ═══════════════════════════════════════════════════════════════════
// Guts<T> — the bus cycle engine (CRTP)
// ═══════════════════════════════════════════════════════════════════
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

    cycles = 0;

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

      // TODO: When TRACE is enabled and FG2BG_READ/WRITE events flood
      // the FIFO, add watermark-based HALT flow control here:
      //   if (fg2bg.size() > FG2BG_HIGH_WATERMARK) {
      //     gpio_set_dir(HALT, GPIO_OUT);  // Assert HALT (open-drain)
      //     fg_halt_for_flow_control = true;
      //   }
      //   if (fg_halt_for_flow_control && fg2bg.size() < FG2BG_LOW_WATERMARK) {
      //     gpio_set_dir(HALT, GPIO_IN);   // Release HALT
      //     fg_halt_for_flow_control = false;
      //   }

      uint prev_late_pins = 0;

      // INNER LOOP
      for (int i = 0; i < GROUP_SIZE; i++) {
        pio_sm_put(pio0, 0, 0);  // put sync word

        byte value = 0;
        uint pins = pio_sm_get_blocking(pio0, 0);  // get early pins
        uint prev_addr = 0xFFFF & hw->gpio_hi_in;
        uint addr;
        while (true) {
          addr = 0xFFFF & hw->gpio_hi_in;
          if (addr == prev_addr) break;
          prev_addr = addr;
        }

        const bool reading = (pins & (1 << R_W));
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
          late_pins = pio_sm_get_blocking(pio0, 0);
          value = (byte)late_pins;

          if (likely(addr < 0xFF00)) {
            ram[addr] = value;
#if TRACE
            PUSH_TO_BG(FG2BG_WRITE, addr, value);
#endif
          } else {
            IOWriter fn = IOWriters[addr & 0xFF];
            if (fn) {
              fn(addr, value);
            }
          }
          kind = CY_WRITE;
        }

        if (likely(reading)) {
          pio_sm_put(pio0, 0, value);
          late_pins = pio_sm_get_blocking(pio0, 0);  // LATE PINS
        }
        bool is_lic = ((late_pins & (1<<LIC)) != 0);
        bool is_fic = ((prev_late_pins & (1<<LIC)) != 0);

        prev_late_pins = late_pins;
      }  // next i

      cycles += GROUP_SIZE;
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
// Background loop (core 0) — FIFO drain + USB I/O + terminal RX
// ═══════════════════════════════════════════════════════════════════
void IN_RAM tfr911_background() {
  uint bg_epochs = 0;

  while (true) {
    // Drain fg2bg FIFO — handle events pushed by foreground.
    uint chore = 0;
    while (fg2bg.pop(chore)) {
      uint chore_num = chore >> 24;
      byte chore_byte = chore & 0xFF;
      switch (chore_num) {
        case FG2BG_PUTCHAR:
          if (chore_byte) putbyte(chore_byte);
          break;
        // Future: FG2BG_READ, FG2BG_WRITE for trace logging
        default:
          break;
      }
    }

    // Poll USB input (read chars from host → term_input CircBuf)
    PollUsbInput();

    // Deliver RX chars to turbo9sim.
    // Sets sim_status_reg bits, which the foreground's
    // Turbo9sim_IrqNeeded() polls each outer loop iteration.
    if (Engine::Turbo9sim_CanRx()) {
      if (term_input.NumBuffered() > 0) {
        byte ch = term_input.Take();
        Engine::Turbo9sim_SetRx(ch);
      }
    }

#if 1
    // Periodic stats (every ~5 seconds at typical iteration rate).
    bg_epochs++;
    if (bg_epochs >= 50000) {
      if (milliseconds > 0) {
        cobs_printf("[Mc=%g  s=%g  Mcps=%g]",
                    double(cycles) / double(1000 * 1000),
                    double(milliseconds) / double(1000),
                    (double)cycles / double(1000 * milliseconds));
      }
      bg_epochs = 0;
    }
#endif
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
  RunReset();

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

  multicore_launch_core1(Engine__RunCPU);  // Core 1 = foreground (PIO bus cycles)

  alarm_pool_init_default();
  add_repeating_timer_us(1000, TimerCallback, nullptr, &TimerData);
  add_repeating_timer_us(16667, Timer60HzCallback, nullptr, &Timer60HzData);

  tfr911_background();  // Core 0 = background (FIFO drain + USB) — never returns
}
