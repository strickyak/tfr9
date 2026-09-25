// main.cpp — TurboLab firmware for TFR/911H (RP2350B + 6309E)
//
// Self-contained subset firmware:
// - No TCL
// - No LittleFS
// - Pure tethered operation with RPC-based handshake & image upload
// - Red-page ($FF04..$FFEF) protection
// - Zero interrupt vector ($0000) abort/fault
// - Dynamic trace filtering, cycle/time triggers, and max limits
// - Immediate core dump transmission on Fault

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/structs/sio.h>
#include <hardware/timer.h>
#include <pico/bootrom.h>
#include <pico/multicore.h>
#include <pico/rand.h>
#include <pico/stdlib.h>
#include <pico/time.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifndef RUNTIME_PIO_ASSEMBLER
#define RUNTIME_PIO_ASSEMBLER 1
#endif

#if RUNTIME_PIO_ASSEMBLER
#include "hamster.h"
#else
#include "hamster.pio.h"
#endif

#include "circbuf.h"
#include "cobs.h"
#include "cross-core.h"
#include "pcb.h"

#define FORCE_INLINE inline __attribute__((always_inline))
#define IN_RAM __not_in_flash("turbolab")
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

static uint32_t current_mhz = 250;

// Set to 1 to enable per-cycle instruction tracing, opcode classification, and trace emission.
// Set to 0 to disable per-cycle trace overhead for maximum bus cycle speed.
#ifndef ENABLE_TRACING
#define ENABLE_TRACING 1
#endif

// Set to 1 to enable runtime fault checks (Red Page $FF04..$FFEF and Zero Vector $0000).
// Set to 0 to disable per-cycle fault checks for maximum bus cycle speed.
#ifndef ENABLE_FAULT_CHECKS
#define ENABLE_FAULT_CHECKS 1
#endif

using byte = uint8_t;
using uint = unsigned int;

// ═══════════════════════════════════════════════════════════════════
// Protocol Constants
// ═══════════════════════════════════════════════════════════════════
constexpr byte C_NOP          = 0;
constexpr byte C_FAULT        = 188;
constexpr byte C_CORE_DUMP    = 189;
constexpr byte C_PUTCHAR      = 193;
constexpr byte C_RESTARTED    = 194;
constexpr byte C_TRACE_CYCLES = 200;

constexpr byte T_REFLASH_NOW_PLEASE = 175;
constexpr byte T_RESTART_NOW_PLEASE = 177;
constexpr byte T_CONSOLE_LINE       = 179;
constexpr byte T_PICO_RPC           = 181;

// Fault Reason Codes
constexpr byte FAULT_RED_PAGE    = 1;
constexpr byte FAULT_ZERO_VECTOR = 2;
constexpr byte FAULT_MAX_CYCLES  = 3;
constexpr byte FAULT_MAX_TIME    = 4;

// Trace Event Kinds
constexpr byte KIND_IDLE        = 0;  // '-'
constexpr byte KIND_FIC         = 1;  // 'x'
constexpr byte KIND_OPCODE_CONT = 2;  // '+'
constexpr byte KIND_READ        = 3;  // 'r'
constexpr byte KIND_WRITE       = 4;  // 'w'
constexpr byte KIND_IRQ         = 5;  // 'i' IRQ
constexpr byte KIND_FIRQ        = 6;  // 'i' FIRQ
constexpr byte KIND_NMI         = 7;  // 'i' NMI
constexpr byte KIND_RTI         = 8;  // 'i' RTI
constexpr byte KIND_SWI2        = 9;  // 't' SWI2

// Trace Flag Bitmasks
constexpr uint8_t TRACE_FLAG_X    = 1 << 0;
constexpr uint8_t TRACE_FLAG_R    = 1 << 1;
constexpr uint8_t TRACE_FLAG_W    = 1 << 2;
constexpr uint8_t TRACE_FLAG_I    = 1 << 3;
constexpr uint8_t TRACE_FLAG_T    = 1 << 4;
constexpr uint8_t TRACE_FLAG_PLUS = 1 << 5;
constexpr uint8_t TRACE_FLAG_IDLE = 1 << 6;

// CPU Signal Flags in high 4 bits of TraceRecord.kind
constexpr uint8_t FLAG_BA   = 0x10;  // 'a'
constexpr uint8_t FLAG_BS   = 0x20;  // 's'
constexpr uint8_t FLAG_LIC  = 0x40;  // '_'
constexpr uint8_t FLAG_BUSY = 0x80;  // 'y'

// ═══════════════════════════════════════════════════════════════════
// TFR911H Pin Assignments
// ═══════════════════════════════════════════════════════════════════
constexpr uint RESET = 20;
constexpr uint NMI   = 21;
constexpr uint IRQ   = 22;
constexpr uint FIRQ  = 23;
constexpr uint HALT  = 24;
constexpr uint LED   = 25;
constexpr uint LIC   = 26;
constexpr uint AVMA  = 27;
constexpr uint BS    = 28;
constexpr uint E     = 29;
constexpr uint Q     = 30;
constexpr uint R_W   = 31;

// ═══════════════════════════════════════════════════════════════════
// Memory & State
// ═══════════════════════════════════════════════════════════════════
alignas(4) byte ram[65536];

struct TraceRecord {
  uint64_t cycle;
  uint16_t addr;
  uint8_t data;
  uint8_t kind;
};

CrossCoreFIFO<TraceRecord, 8192> fg2bg_trace;
CrossCoreFIFO<byte, 8192> fg2bg_chars;

// Console line input ring buffer for 6309 ACIA
CircBuf<byte, 4096> term_input;

// Configuration set by Tether via RPC
volatile uint8_t trace_flags = 0;
volatile uint64_t trigger_cycle = 0;
volatile uint64_t trigger_time_us = 0;
volatile uint64_t max_cycles = 0;
volatile uint64_t max_time_us = 0;

// Execution state
enum FirmwareState {
  STATE_WAIT_CONFIG,
  STATE_RUNNING,
  STATE_FAULT,
  STATE_HALTED
};

volatile FirmwareState fw_state = STATE_WAIT_CONFIG;
volatile bool foreground_running = false;
volatile bool cpu_started = false;
volatile bool fault_triggered = false;
volatile byte fault_reason = 0;
volatile uint64_t fault_cycle = 0;
volatile uint16_t fault_addr = 0;
volatile byte fault_data = 0;
volatile CpuRegisterDump cpu_registers = {};

volatile uint64_t start_time_us = 0;

// Turbo9Sim emulated ACIA registers ($FF00..$FF03)
constexpr byte SIM_TIMER_BIT = 0x01;
constexpr byte SIM_RX_BIT    = 0x02;

volatile byte sim_status_reg = 0;
volatile byte sim_control_reg = 0;
volatile byte sim_last_char_tx = 0;

struct repeating_timer timer60hz_data;
volatile bool timer60hz_running = false;
volatile bool waiting_for_tether_packet = true;
volatile uint64_t last_beacon_us = 0;

bool IN_RAM Timer60HzCallback(repeating_timer_t* rt) {
  sim_status_reg |= SIM_TIMER_BIT;
  return true;
}

// ═══════════════════════════════════════════════════════════════════
// Pin & Hardware Helpers
// ═══════════════════════════════════════════════════════════════════
FORCE_INLINE void IN_RAM LedOn()  { gpio_put(LED, 1); }
FORCE_INLINE void IN_RAM LedOff() { gpio_put(LED, 0); }

// HALT is open-drain: assert via direction OUT (latch=0), release via IN
FORCE_INLINE void IN_RAM HaltOn()  { gpio_set_dir(HALT, GPIO_OUT); }
FORCE_INLINE void IN_RAM HaltOff() { gpio_set_dir(HALT, GPIO_IN);  }

// RESET is active-low: assert via direction OUT (latch=0), release via IN
FORCE_INLINE void IN_RAM AssertReset()  { gpio_put(RESET, 0); gpio_set_dir(RESET, GPIO_OUT); }
FORCE_INLINE void IN_RAM ReleaseReset() { gpio_set_dir(RESET, GPIO_IN);  }

// IRQ and NMI are open-drain
FORCE_INLINE void IN_RAM AssertIRQ()  { gpio_set_dir(IRQ, GPIO_OUT); }
FORCE_INLINE void IN_RAM ReleaseIRQ() { gpio_set_dir(IRQ, GPIO_IN);  }
FORCE_INLINE void IN_RAM AssertNMI()  { gpio_set_dir(NMI, GPIO_OUT); }
FORCE_INLINE void IN_RAM ReleaseNMI() { gpio_set_dir(NMI, GPIO_IN);  }



void InitializePins() {
  for (int i = 0; i < 48; i++) {
    gpio_init(i);
    switch (i) {
      case E:
      case Q:
      case LED:
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);
        break;

      case RESET:
      case NMI:
      case IRQ:
      case FIRQ:
      case HALT:
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);            // Latch 0
        gpio_set_dir(i, GPIO_IN);  // Input (released via pullup)
        gpio_set_pulls(i, true, false);
        break;

      case R_W:
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_up(i);
        break;

      default:
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_up(i);
        break;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// Factored Foreground Bus Loop Helpers (All FORCE_INLINE IN_RAM)
// ═══════════════════════════════════════════════════════════════════

FORCE_INLINE byte IN_RAM fg_loop_handle_io_read(uint addr) {
  switch (addr & 3) {
    case 0:
      return sim_last_char_tx;
    case 1:
      if (!term_input.Empty()) {
        byte val = term_input.Take();
        if (term_input.Empty()) {
          sim_status_reg &= ~SIM_RX_BIT;
        }
        return val;
      }
      return 0;
    case 2:
      return sim_status_reg;
    case 3:
      return sim_control_reg;
  }
  return 0;
}

FORCE_INLINE void IN_RAM fg_loop_handle_io_write(uint addr, byte value) {
  switch (addr & 3) {
    case 0:
      sim_last_char_tx = value;
      fg2bg_chars.push(value);
      break;
    case 1:
      // RX write has no effect
      break;
    case 2:
      if (value & SIM_TIMER_BIT) sim_status_reg &= ~SIM_TIMER_BIT;
      if (value & SIM_RX_BIT)    sim_status_reg &= ~SIM_RX_BIT;
      break;
    case 3:
      sim_control_reg = value;
      break;
  }
}

FORCE_INLINE bool IN_RAM fg_loop_check_red_page(uint addr, bool is_idle, uint64_t cycles, bool reading) {
#if ENABLE_FAULT_CHECKS
  // Red Page Check ($FF04..$FFEF)
  // Disabled during idle cycles (address lines might float).
  // Note: Only called in Phase 2 and Phase 3 (after CPU reset is achieved).
  if (!is_idle && cpu_started && UNLIKELY(addr >= 0xFF04 && addr <= 0xFFEF)) {
    fault_reason = FAULT_RED_PAGE;
    fault_cycle = cycles;
    fault_addr = addr;
    fault_data = reading ? ram[addr] : 0;
    fault_triggered = true;
    return true;
  }
#else
  (void)addr; (void)is_idle; (void)cycles; (void)reading;
#endif
  return false;
}

FORCE_INLINE bool IN_RAM fg_loop_check_zero_vector(uint addr, byte value, bool is_bs, uint64_t cycles) {
#if ENABLE_FAULT_CHECKS
  // Zero Interrupt Vector Check:
  // If BS=1 (Interrupt Acknowledge) and vector data is 0:
  // Note: Only called in Phase 2 and Phase 3 (after CPU reset is achieved).
  if (cpu_started && UNLIKELY(is_bs && value == 0)) {
    fault_reason = FAULT_ZERO_VECTOR;
    fault_cycle = cycles;
    fault_addr = addr;
    fault_data = value;
    fault_triggered = true;
    return true;
  }
#else
  (void)addr; (void)value; (void)is_bs; (void)cycles;
#endif
  return false;
}

FORCE_INLINE byte IN_RAM fg_loop_handle_read(uint addr, bool is_idle, bool is_bs, uint64_t cycles) {
#if ENABLE_FAULT_CHECKS || ENABLE_TRACING
  if (UNLIKELY(is_idle)) {
    return 0;
  }
#endif
  if (LIKELY(addr < 0xFF00)) {
    return ram[addr];
  } else if (addr <= 0xFF03) {
    return fg_loop_handle_io_read(addr);
  } else {
    // Vectors $FFF0..$FFFF
    byte val = ram[addr];
    fg_loop_check_zero_vector(addr, val, is_bs, cycles);
    return val;
  }
}

FORCE_INLINE void IN_RAM fg_loop_handle_write(uint addr, byte value) {
  // Writes are permitted anywhere in RAM, including the vector table $FFF0..$FFFF.
  if (LIKELY(addr < 0xFF00)) {
    ram[addr] = value;
  } else if (addr <= 0xFF03) {
    fg_loop_handle_io_write(addr, value);
  } else {
    // Vectors $FFF0..$FFFF (and non-red-page >= $FF04)
    ram[addr] = value;
  }
}

FORCE_INLINE byte IN_RAM fg_loop_classify_read_cycle(uint addr, bool is_idle, uint prev_late_pins, byte prev_kind, uint16_t prev_addr) {
#if ENABLE_TRACING
  if (UNLIKELY(is_idle)) {
    return KIND_IDLE;
  }
  bool is_fic = ((prev_late_pins & (1 << LIC)) != 0);
  if (is_fic) {
    return KIND_FIC;
  } else if ((prev_kind == KIND_FIC || prev_kind == KIND_OPCODE_CONT) &&
             addr == (prev_addr + 1)) {
    return KIND_OPCODE_CONT;
  } else {
    return KIND_READ;
  }
#else
  (void)addr; (void)is_idle; (void)prev_late_pins; (void)prev_kind; (void)prev_addr;
  return 0;
#endif
}

FORCE_INLINE void IN_RAM fg_loop_trace_cycle(uint64_t cycles, uint addr, byte value, byte kind, bool reading, bool is_bs, uint early_pins, uint prev_late_pins) {
#if ENABLE_TRACING
  // Check for RTI ($3B)
  bool is_rti = (reading && kind == KIND_FIC && value == 0x3B && addr < 0xFFF0);
  if (is_rti) {
    LedOff();
  }

  // Trace filtering: only emit when trigger conditions are satisfied.
  bool trigger_met = cpu_started && (cycles >= trigger_cycle);

  if (trigger_met) {
    bool emit = false;
    switch (kind) {
      case KIND_IDLE:
        emit = (trace_flags & TRACE_FLAG_IDLE) != 0;
        break;
      case KIND_FIC:
        emit = (trace_flags & TRACE_FLAG_X) != 0;
        break;
      case KIND_OPCODE_CONT:
        emit = (trace_flags & TRACE_FLAG_PLUS) != 0;
        break;
      case KIND_READ:
        emit = (trace_flags & TRACE_FLAG_R) != 0;
        break;
      case KIND_WRITE:
        emit = (trace_flags & TRACE_FLAG_W) != 0;
        break;
    }

    // Trace flag 'i' enables logging of interrupt cycles (vector fetch and RTI)
    // tagged with their usual r/w/x status without overriding kind:
    if ((trace_flags & TRACE_FLAG_I) != 0) {
      if (is_bs || is_rti) {
        emit = true;
      }
    }

    // Extract CPU signals for high 4 bits of kind: a=BA s=BS _=LIC y=BUSY
    uint8_t cpu_flags = 0;
#ifdef PIN_BA
    if ((prev_late_pins & (1 << PIN_BA)) != 0) cpu_flags |= FLAG_BA;
#endif
    if ((early_pins & (1 << BS)) != 0)         cpu_flags |= FLAG_BS;
    if ((prev_late_pins & (1 << LIC)) != 0)    cpu_flags |= FLAG_LIC;
#ifdef PIN_BUSY
    if ((prev_late_pins & (1 << PIN_BUSY)) != 0) cpu_flags |= FLAG_BUSY;
#endif

    if (emit) {
      TraceRecord rec{cycles, (uint16_t)addr, value, (uint8_t)((kind & 0x0F) | cpu_flags)};
      while (!fg2bg_trace.push(rec)) {
        if (UNLIKELY(fault_triggered)) break;
        tight_loop_contents();
      }
    }
  }
#else
  (void)cycles; (void)addr; (void)value; (void)kind; (void)reading; (void)is_bs; (void)early_pins; (void)prev_late_pins;
#endif
}

FORCE_INLINE bool IN_RAM fg_loop_check_limits(uint64_t cycles, uint addr, byte value) {
  if (UNLIKELY(max_cycles > 0 && cycles >= max_cycles)) {
    fault_reason = FAULT_MAX_CYCLES;
    fault_cycle = cycles;
    fault_addr = addr;
    fault_data = value;
    fault_triggered = true;
    return true;
  }
  return false;
}

// ═══════════════════════════════════════════════════════════════════
// Core 1: Foreground Real-Time Bus Engine
// ═══════════════════════════════════════════════════════════════════
void IN_RAM foreground_loop() {
  volatile sio_hw_t* hw = (volatile sio_hw_t*)sio_hw;

  uint64_t cycles = 0;
  uint64_t pre_reset_cycles = 0;
  uint prev_late_pins = 0;
#if ENABLE_TRACING
  uint16_t prev_addr = 0;
  byte prev_kind = KIND_IDLE;
#endif
  bool prev_irq_needed = false;

  bool saw_fffe = false;

  LedOff();
  foreground_running = true;

  // ═════════════════════════════════════════════════════════════════
  // Phase 1: Before the Reset Vector is fetched ($FFFE+$FFFF)
  // ═════════════════════════════════════════════════════════════════
  while (true) {
    if (UNLIKELY(fault_triggered)) {
      goto phase4;
    }

    constexpr int GROUP_SIZE = 50;
    for (int i = 0; i < GROUP_SIZE; i++) {
      pio_sm_put(pio0, 0, 0);
      uint early_pins = pio_sm_get_blocking(pio0, 0);
      uint addr = 0xFFFF & hw->gpio_hi_in;
      const bool reading = 0 != (early_pins & (1 << R_W));
      const bool is_bs   = 0 != (early_pins & (1 << BS));

      // Case A: CPU is still held in RESET / HALT by Core 0
      if (UNLIKELY(!cpu_started)) {
        if (LIKELY(reading)) {
          pio_sm_put(pio0, 0, ram[addr]);
        }
        prev_late_pins = pio_sm_get_blocking(pio0, 0);
        saw_fffe = false;
        pre_reset_cycles = 0;
        continue;
      }

      // Case B: CPU is running out of reset (cpu_started == true)
      if (!saw_fffe) {
        if (reading && is_bs && addr == 0xFFFE) {
          // Reset vector high-byte fetch observed!
          saw_fffe = true;
        } else {
          pre_reset_cycles++;
          if (UNLIKELY(pre_reset_cycles >= 100000)) {
            fault_reason = FAULT_ZERO_VECTOR;
            fault_cycle = 0;
            fault_addr = addr;
            fault_triggered = true;
            HaltOn();
            goto phase4;
          }
        }
      } else {
        // saw_fffe is true: awaiting reset vector low-byte ($FFFF)
        if (reading && is_bs && addr == 0xFFFF) {
          // Reset vector successfully fetched!
          saw_fffe = false;
          cycles = 2; // Cycle 1 = $FFFE, Cycle 2 = $FFFF
          start_time_us = time_us_64();
          byte value = ram[addr];
          pio_sm_put(pio0, 0, value);
          prev_late_pins = pio_sm_get_blocking(pio0, 0);
          goto phase2;
        } else if (reading && is_bs && addr == 0xFFFE) {
          // CPU still holding/stretching $FFFE read, maintain saw_fffe state
        } else {
          // False start or noise on address bus; reset search
          saw_fffe = false;
          pre_reset_cycles++;
        }
      }

      byte value = ram[addr];
      if (LIKELY(reading)) {
        pio_sm_put(pio0, 0, value);
      }
      prev_late_pins = pio_sm_get_blocking(pio0, 0);
    }
  }

  // ═════════════════════════════════════════════════════════════════
  // Phase 2: Before the trigger is met, and tracing is off
  // ═════════════════════════════════════════════════════════════════
phase2:
#if ENABLE_TRACING
  if (trace_flags != 0 && cycles >= trigger_cycle) {
    goto phase3;
  }
#endif

  while (true) {
    if (UNLIKELY(fault_triggered)) {
      goto phase4;
    }

    if (!term_input.Empty()) {
      sim_status_reg |= SIM_RX_BIT;
    }

    bool irq_needed = (sim_status_reg & sim_control_reg) != 0;
    if (irq_needed != prev_irq_needed) {
      if (irq_needed) AssertIRQ(); else ReleaseIRQ();
      prev_irq_needed = irq_needed;
    }

    constexpr int GROUP_SIZE = 50;
    for (int i = 0; i < GROUP_SIZE; i++) {
#if ENABLE_TRACING
      if (UNLIKELY(trace_flags != 0 && (cycles + 1) >= trigger_cycle)) {
        goto phase3;
      }
#endif

      pio_sm_put(pio0, 0, 0);
      uint early_pins = pio_sm_get_blocking(pio0, 0);
      uint addr = 0xFFFF & hw->gpio_hi_in;
      const bool reading = 0 != (early_pins & (1 << R_W));
      const bool is_bs   = 0 != (early_pins & (1 << BS));
#if ENABLE_FAULT_CHECKS
      const bool vma     = 0 != (prev_late_pins & (1 << AVMA));
      const bool is_idle = !vma && !is_bs;
#else
      const bool is_idle = false;
#endif
      cycles++;

#if ENABLE_FAULT_CHECKS
      if (fg_loop_check_red_page(addr, is_idle, cycles, reading)) {
        goto phase4;
      }
#endif

      byte value = 0;
      if (LIKELY(reading)) {
        value = fg_loop_handle_read(addr, is_idle, is_bs, cycles);
        if (UNLIKELY(fault_triggered)) goto phase4;
        if (UNLIKELY(is_bs)) LedOn();

        pio_sm_put(pio0, 0, value);
        prev_late_pins = pio_sm_get_blocking(pio0, 0);
      } else {
        prev_late_pins = pio_sm_get_blocking(pio0, 0);
        value = (byte)prev_late_pins;
        fg_loop_handle_write(addr, value);
      }

      if (fg_loop_check_limits(cycles, addr, value)) {
        goto phase4;
      }
    }
  }

#if ENABLE_TRACING
  // ═════════════════════════════════════════════════════════════════
  // Phase 3: After the trigger is met, and tracing is on
  // ═════════════════════════════════════════════════════════════════
phase3:
  while (true) {
    if (UNLIKELY(fault_triggered)) {
      goto phase4;
    }

    if (!term_input.Empty()) {
      sim_status_reg |= SIM_RX_BIT;
    }

    bool irq_needed = (sim_status_reg & sim_control_reg) != 0;
    if (irq_needed != prev_irq_needed) {
      if (irq_needed) AssertIRQ(); else ReleaseIRQ();
      prev_irq_needed = irq_needed;
    }

    constexpr int GROUP_SIZE = 50;
    for (int i = 0; i < GROUP_SIZE; i++) {
      pio_sm_put(pio0, 0, 0);
      uint early_pins = pio_sm_get_blocking(pio0, 0);
      uint addr = 0xFFFF & hw->gpio_hi_in;
      const bool reading = 0 != (early_pins & (1 << R_W));
      const bool is_bs   = 0 != (early_pins & (1 << BS));
#if ENABLE_FAULT_CHECKS || ENABLE_TRACING
      const bool vma     = 0 != (prev_late_pins & (1 << AVMA));
      const bool is_idle = !vma && !is_bs;
#else
      const bool is_idle = false;
#endif
      cycles++;

      byte value = 0;
      byte kind = KIND_IDLE;

#if ENABLE_FAULT_CHECKS
      if (fg_loop_check_red_page(addr, is_idle, cycles, reading)) {
        goto phase4;
      }
#endif

      if (LIKELY(reading)) {
        value = fg_loop_handle_read(addr, is_idle, is_bs, cycles);
        if (UNLIKELY(fault_triggered)) goto phase4;
        if (UNLIKELY(is_bs)) LedOn();

        pio_sm_put(pio0, 0, value);
        uint late_pins = pio_sm_get_blocking(pio0, 0);

        kind = fg_loop_classify_read_cycle(addr, is_idle, prev_late_pins, prev_kind, prev_addr);
        prev_late_pins = late_pins;
      } else {
        uint late_pins = pio_sm_get_blocking(pio0, 0);
        value = (byte)late_pins;
        fg_loop_handle_write(addr, value);
        kind = is_idle ? KIND_IDLE : KIND_WRITE;
        prev_late_pins = late_pins;
      }

      fg_loop_trace_cycle(cycles, addr, value, kind, reading, is_bs, early_pins, prev_late_pins);
      prev_addr = addr;
      prev_kind = kind;

      if (fg_loop_check_limits(cycles, addr, value)) {
        goto phase4;
      }
    }
  }
#endif

  // ═════════════════════════════════════════════════════════════════
  // Phase 4: After the final condition is met (max cycles, fault, ^C)
  //          Prepares / executes NMI register dump, then halts CPU
  // ═════════════════════════════════════════════════════════════════
phase4:
  enum P4State {
    P4_AWAIT_LIC,
    P4_INJECT_SWI,
    P4_AWAIT_STACK_WRITES,
    P4_DONE
  };

  P4State p4_state = P4_AWAIT_LIC;
  // If the last bus cycle of Phase 2/3 already asserted LIC, we can inject immediately
  if ((prev_late_pins & (1 << LIC)) != 0) {
    p4_state = P4_INJECT_SWI;
  }

  uint16_t swi_injected_pc = 0;
  uint16_t write_start = 0;
  uint16_t write_end = 0;
  uint8_t  write_streak_count = 0;
  uint8_t  swi_stack_bytes[16] = {};

  cpu_registers.valid = false;
  cpu_registers.is_6309_native = false;
  cpu_registers.streak_len = 0;

  constexpr int P4_MAX_CYCLES = 256;
  for (int p4_cycle = 0; p4_cycle < P4_MAX_CYCLES; p4_cycle++) {
    // If waiting for LIC for more than 4 cycles, CPU may be halted in CWAI/SYNC.
    // Assert NMI to wake CPU so it can acknowledge interrupt and execute SWI.
    if (p4_state == P4_AWAIT_LIC && p4_cycle >= 4) {
      AssertNMI();
    }

    // Hamster PIO sync
    pio_sm_put(pio0, 0, 0);
    uint early_pins = pio_sm_get_blocking(pio0, 0);
    uint addr = 0xFFFF & hw->gpio_hi_in;
    const bool reading = 0 != (early_pins & (1 << R_W));
    const bool is_bs   = 0 != (early_pins & (1 << BS));
    cycles++;

    byte value = 0;

    if (LIKELY(reading)) {
      // Check for SWI or interrupt vector read ($FFFA, $FFFB, etc.)
      if ((addr == 0xFFFA || addr == 0xFFFB || (is_bs && addr >= 0xFFF8 && addr <= 0xFFFD)) &&
          (write_streak_count == 12 || write_streak_count == 14)) {
        HaltOn();
        p4_state = P4_DONE;
        value = ram[addr];
        pio_sm_put(pio0, 0, value);
        prev_late_pins = pio_sm_get_blocking(pio0, 0);
        break;
      }

      // Check for FIRQ vector read ($FFF6 or $FFF7) with BS=1
      if (is_bs && (addr == 0xFFF6 || addr == 0xFFF7) && write_streak_count == 3) {
        // Reset streak count for FIRQ, and await LIC of FIRQ sequence
        write_streak_count = 0;
        p4_state = P4_AWAIT_LIC;
      } else if (write_streak_count > 0 && write_streak_count != 12 && write_streak_count != 14) {
        // Write streak was broken before completing a full register frame
        write_streak_count = 0;
      }

      if (p4_state == P4_INJECT_SWI) {
        // Next FIC cycle after LIC: Inject SWI opcode ($3F)
        swi_injected_pc = (uint16_t)addr;
        value = 0x3F;
        p4_state = P4_AWAIT_STACK_WRITES;
      } else {
        // Serve from RAM without ACIA I/O side effects
        value = ram[addr];
      }

      pio_sm_put(pio0, 0, value);
      uint late_pins = pio_sm_get_blocking(pio0, 0);

      // Check if this cycle asserts LIC (ready for next opcode fetch)
      if (p4_state == P4_AWAIT_LIC && (late_pins & (1 << LIC)) != 0) {
        p4_state = P4_INJECT_SWI;
      }

      prev_late_pins = late_pins;
    } else {
      // Write cycle
      uint late_pins = pio_sm_get_blocking(pio0, 0);
      value = (byte)late_pins;

      // Save write to RAM
      ram[addr] = value;

      if (p4_state == P4_AWAIT_STACK_WRITES) {
        if (write_streak_count == 0) {
          write_start = (uint16_t)addr;
        }
        write_end = (uint16_t)addr;
        if (write_streak_count < 16) {
          swi_stack_bytes[write_streak_count] = value;
        }
        write_streak_count++;
      }

      // Check if this write cycle asserts LIC
      if (p4_state == P4_AWAIT_LIC && (late_pins & (1 << LIC)) != 0) {
        p4_state = P4_INJECT_SWI;
      }

      prev_late_pins = late_pins;
    }
  }

  // Safety fallback: ensure CPU is halted and NMI/IRQ released
  ReleaseNMI();
  ReleaseIRQ();
  HaltOn();

  // Unpack register frame if SWI completed successfully
  if (write_streak_count == 12) {
    // 6809 mode full register dump
    cpu_registers.valid = true;
    cpu_registers.is_6309_native = false;
    uint16_t pushed_pc = ((uint16_t)swi_stack_bytes[1] << 8) | swi_stack_bytes[0];
    cpu_registers.pc = swi_injected_pc ? swi_injected_pc : (pushed_pc ? pushed_pc - 1 : 0);
    cpu_registers.s = write_start + 1;
    cpu_registers.u = ((uint16_t)swi_stack_bytes[3] << 8) | swi_stack_bytes[2];
    cpu_registers.y = ((uint16_t)swi_stack_bytes[5] << 8) | swi_stack_bytes[4];
    cpu_registers.x = ((uint16_t)swi_stack_bytes[7] << 8) | swi_stack_bytes[6];
    cpu_registers.dp = swi_stack_bytes[8];
    cpu_registers.b = swi_stack_bytes[9];
    cpu_registers.a = swi_stack_bytes[10];
    cpu_registers.e = 0;
    cpu_registers.f = 0;
    cpu_registers.cc = swi_stack_bytes[11];
    cpu_registers.streak_len = 12;
  } else if (write_streak_count == 14) {
    // 6309 native mode full register dump
    cpu_registers.valid = true;
    cpu_registers.is_6309_native = true;
    uint16_t pushed_pc = ((uint16_t)swi_stack_bytes[1] << 8) | swi_stack_bytes[0];
    cpu_registers.pc = swi_injected_pc ? swi_injected_pc : (pushed_pc ? pushed_pc - 1 : 0);
    cpu_registers.s = write_start + 1;
    cpu_registers.u = ((uint16_t)swi_stack_bytes[3] << 8) | swi_stack_bytes[2];
    cpu_registers.y = ((uint16_t)swi_stack_bytes[5] << 8) | swi_stack_bytes[4];
    cpu_registers.x = ((uint16_t)swi_stack_bytes[7] << 8) | swi_stack_bytes[6];
    cpu_registers.dp = swi_stack_bytes[8];
    cpu_registers.f = swi_stack_bytes[9];
    cpu_registers.e = swi_stack_bytes[10];
    cpu_registers.b = swi_stack_bytes[11];
    cpu_registers.a = swi_stack_bytes[12];
    cpu_registers.cc = swi_stack_bytes[13];
    cpu_registers.streak_len = 14;
  }

  return;
} // foreground_loop

// ═══════════════════════════════════════════════════════════════════
// Core 0: Background Administration & USB Pipeline
// ═══════════════════════════════════════════════════════════════════
CircBuf<unsigned char, 4096> usb_rx_raw;
CircBuf<std::string*, 64> usb_rx_packets;
CobsDecoder<4096, 64> cobs_decoder(usb_rx_raw, usb_rx_packets);

void pump_usb_rx() {
  while (true) {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) break;
    usb_rx_raw.Put((unsigned char)c);
  }
  cobs_decoder.Tick();
}

void send_cobs(const unsigned char* data, size_t len) {
  CobsEncodeAndTransmit(data, len, [](int ch) { putchar_raw(ch); });
}

void send_rpc_response(const pcb::RpcResponse& resp) {
  std::vector<uint8_t> payload = resp.encode();
  std::vector<uint8_t> pkt;
  pkt.reserve(payload.size() + 1);
  pkt.push_back(T_PICO_RPC);
  pkt.insert(pkt.end(), payload.begin(), payload.end());
  send_cobs(pkt.data(), pkt.size());
}

void handle_rpc_request(const std::string& pkt) {
  if (pkt.length() < 2) return;
  std::vector<uint8_t> buf(pkt.begin() + 1, pkt.end());
  pcb::RpcRequest req = pcb::RpcRequest::decode(buf);
  pcb::RpcResponse resp;
  resp.serial = req.serial;

  if (req.method == "ping") {
    resp.status = 0;
    resp.data = req.data;
    send_rpc_response(resp);
  } else if (req.method == "config") {
    trace_flags = (uint8_t)req.flags;
    trigger_cycle = (uint64_t)req.offset;
    trigger_time_us = (uint64_t)req.length;
    max_cycles = (uint64_t)req.whence;
    max_time_us = (uint64_t)req.flags; // or passed via data
    // If data payload contains binary config, unpack:
    if (req.data.size() >= 24) {
      const uint8_t* p = (const uint8_t*)req.data.data();
      trigger_cycle = *(const uint64_t*)(p + 0);
      trigger_time_us = *(const uint64_t*)(p + 8);
      max_cycles = *(const uint64_t*)(p + 16);
      if (req.data.size() >= 32) {
        max_time_us = *(const uint64_t*)(p + 24);
      }
    }
    resp.status = 0;
    send_rpc_response(resp);
#if RUNTIME_PIO_ASSEMBLER
  } else if (req.method == "tuning") {
    if (req.data.size() >= 84) {
      const uint32_t* p = (const uint32_t*)req.data.data();
      tuning_mhz = p[0];
      for (int i = 1; i <= 9; i++) {
        tuning_k[i] = p[i];
      }
      for (int i = 1; i <= 9; i++) {
        tuning_t[i] = p[10 + i];
      }
      printf("\n[Firmware Tuning Configured: MHZ=%lu, K1=%lu, K2=%lu, K3=%lu, K4=%lu, T1=%lu, T2=%lu, T3=%lu, T4=%lu, T5=%lu]\n",
             tuning_mhz, K1, K2, K3, K4, T1, T2, T3, T4, T5);
      resp.status = 0;
    } else {
      resp.status = 1;
      resp.message = "tuning payload too short (expected >= 84 bytes)";
    }
    send_rpc_response(resp);
#endif
  } else if (req.method == "upload") {
    size_t offset = (size_t)req.offset;
    size_t len = req.data.size();
    if (offset + len <= 65536) {
      memcpy(&ram[offset], req.data.data(), len);
      resp.status = 0;
    } else {
      resp.status = 1;
      resp.message = "offset out of range";
    }
    send_rpc_response(resp);
  } else if (req.method == "start") {
    resp.status = 0;
    send_rpc_response(resp);

    // Clear trace and char buffers before starting CPU
    fg2bg_trace.clear();
    fg2bg_chars.clear();

    // Boot CPU
    AssertReset();
    HaltOn();

#if RUNTIME_PIO_ASSEMBLER
    if (tuning_mhz != current_mhz) {
      printf("\n[Reconfiguring system clock from %lu MHz to %lu MHz...]\n", current_mhz, tuning_mhz);
      set_sys_clock_khz(tuning_mhz * 1000, true);
      current_mhz = tuning_mhz;
    }
    pio_sm_set_enabled(pio0, 0, false);
    pio_sm_restart(pio0, 0);
    pio_clear_instruction_memory(pio0);
    PioAssembler hamster = build_hamster_program(/*verbose=*/true);
    uint offset = hamster.pio_add_program(pio0);
    hamster_program_init(pio0, 0, offset, hamster);
#else
    pio_sm_set_enabled(pio0, 0, false);
    pio_sm_restart(pio0, 0);
    pio_clear_instruction_memory(pio0);
    uint offset = pio_add_program(pio0, &hamster_program);
    hamster_program_init(pio0, 0, offset);
#endif
    gpio_pull_up(R_W);

    cpu_started = false;
    foreground_running = false;
    multicore_launch_core1(foreground_loop);

    while (!foreground_running) {
      sleep_ms(1);
    }

    // Hold RESET for ~10ms
    sleep_ms(10);
    ReleaseReset();
    sleep_ms(5);

    // Release HALT -> 6309 starts executing
    start_time_us = time_us_64();
    __dmb();
    cpu_started = true;
    HaltOff();

    alarm_pool_init_default();
    add_repeating_timer_us(16667, Timer60HzCallback, nullptr, &timer60hz_data);
    timer60hz_running = true;

    fw_state = STATE_RUNNING;
  } else {
    resp.status = -1;
    resp.message = "unknown method";
    send_rpc_response(resp);
  }
}

void drain_trace_and_chars() {
  // 1. Drain all console characters
  byte ch;
  while (fg2bg_chars.pop(ch)) {
    unsigned char pkt[] = {C_PUTCHAR, ch};
    send_cobs(pkt, sizeof(pkt));
  }

  // 2. Drain all trace cycles until FIFO is empty
  constexpr size_t MAX_BATCH = 20;
  TraceRecord batch[MAX_BATCH];
  while (!fg2bg_trace.empty()) {
    size_t count = 0;
    while (count < MAX_BATCH && fg2bg_trace.pop(batch[count])) {
      count++;
    }
    if (count > 0) {
      uint8_t pkt[1 + 1 + MAX_BATCH * 12];
      pkt[0] = C_TRACE_CYCLES;
      pkt[1] = (uint8_t)count;
      for (size_t i = 0; i < count; i++) {
        size_t off = 2 + i * 12;
        memcpy(&pkt[off + 0], &batch[i].cycle, 8);
        pkt[off + 8] = (uint8_t)(batch[i].addr >> 8);
        pkt[off + 9] = (uint8_t)(batch[i].addr & 0xFF);
        pkt[off + 10] = batch[i].data;
        pkt[off + 11] = batch[i].kind;
      }
      send_cobs(pkt, 2 + count * 12);
      stdio_flush();
      sleep_us(500);  // Allow USB CDC endpoint to transmit
    }
  }
  stdio_flush();
}

void transmit_core_dump() {
  // Drain all pending trace records and console characters before sending fault header
  drain_trace_and_chars();
  sleep_ms(5);

  // Send C_FAULT header: [C_FAULT, reason, cycle_8B, addr_2B, data_1B]
  // Extended with CPU register dump:
  // [valid_1B, is_native_1B, pc_2B, s_2B, u_2B, y_2B, x_2B, dp_1B, a_1B, b_1B, e_1B, f_1B, cc_1B]
  uint8_t fault_pkt[31];
  fault_pkt[0] = C_FAULT;
  fault_pkt[1] = fault_reason;
  memcpy(&fault_pkt[2], (const void*)&fault_cycle, 8);
  fault_pkt[10] = (uint8_t)(fault_addr >> 8);
  fault_pkt[11] = (uint8_t)(fault_addr & 0xFF);
  fault_pkt[12] = fault_data;

  // Extended register payload
  fault_pkt[13] = cpu_registers.valid ? 1 : 0;
  fault_pkt[14] = cpu_registers.is_6309_native ? 1 : 0;
  fault_pkt[15] = (uint8_t)(cpu_registers.pc >> 8);
  fault_pkt[16] = (uint8_t)(cpu_registers.pc & 0xFF);
  fault_pkt[17] = (uint8_t)(cpu_registers.s >> 8);
  fault_pkt[18] = (uint8_t)(cpu_registers.s & 0xFF);
  fault_pkt[19] = (uint8_t)(cpu_registers.u >> 8);
  fault_pkt[20] = (uint8_t)(cpu_registers.u & 0xFF);
  fault_pkt[21] = (uint8_t)(cpu_registers.y >> 8);
  fault_pkt[22] = (uint8_t)(cpu_registers.y & 0xFF);
  fault_pkt[23] = (uint8_t)(cpu_registers.x >> 8);
  fault_pkt[24] = (uint8_t)(cpu_registers.x & 0xFF);
  fault_pkt[25] = cpu_registers.dp;
  fault_pkt[26] = cpu_registers.a;
  fault_pkt[27] = cpu_registers.b;
  fault_pkt[28] = cpu_registers.e;
  fault_pkt[29] = cpu_registers.f;
  fault_pkt[30] = cpu_registers.cc;

  send_cobs(fault_pkt, sizeof(fault_pkt));
  stdio_flush();
  sleep_ms(2);

  // Send 64KB core dump in 64 chunks of 1024 bytes
  uint8_t dump_pkt[1026];
  dump_pkt[0] = C_CORE_DUMP;
  for (int chunk = 0; chunk < 64; chunk++) {
    dump_pkt[1] = (uint8_t)chunk;
    memcpy(&dump_pkt[2], &ram[chunk * 1024], 1024);
    send_cobs(dump_pkt, sizeof(dump_pkt));
    sleep_ms(2);  // Give USB time to flush
  }
}

void restart_to_restarted_state() {
  memset((void*)&cpu_registers, 0, sizeof(cpu_registers));

  // 1. Assert RESET and HALT on 6309 CPU
  AssertReset();
  HaltOn();
  ReleaseIRQ();
  ReleaseNMI();

  // 2. Stop Core 1 if running
  if (foreground_running) {
    multicore_reset_core1();
    foreground_running = false;
  }

  // 3. Cancel 60Hz timer
  if (timer60hz_running) {
    cancel_repeating_timer(&timer60hz_data);
    timer60hz_running = false;
  }

  // 4. Disable Hamster PIO
  pio_sm_set_enabled(pio0, 0, false);
  pio_sm_restart(pio0, 0);
  pio_clear_instruction_memory(pio0);

  // 5. Reset internal state variables
  cpu_started = false;
  fault_triggered = false;
  fault_reason = 0;
  fault_cycle = 0;
  fault_addr = 0;
  fault_data = 0;
  trace_flags = 0;
  trigger_cycle = 0;
  trigger_time_us = 0;
  max_cycles = 0;
  max_time_us = 0;
  start_time_us = 0;
  sim_status_reg = 0;
  sim_control_reg = 0;
  sim_last_char_tx = 0;

  // 6. Clear buffers
  fg2bg_trace.clear();
  fg2bg_chars.clear();
  term_input.Reset();

  // 7. Transition to STATE_WAIT_CONFIG (RESTARTED state)
  fw_state = STATE_WAIT_CONFIG;
  waiting_for_tether_packet = true;
  last_beacon_us = time_us_64();

  // Send initial C_RESTARTED beacon
  unsigned char pkt[] = {C_RESTARTED, 1, 0};
  send_cobs(pkt, sizeof(pkt));
  LedOn();
}

void reflash_now_please() {
  // 1. Assert RESET and HALT on 6309 CPU
  AssertReset();
  HaltOn();
  ReleaseIRQ();

  // 2. Stop Core 1 if running
  if (foreground_running) {
    multicore_reset_core1();
    foreground_running = false;
  }
  cpu_started = false;

  // 3. Cancel 60Hz timer
  if (timer60hz_running) {
    cancel_repeating_timer(&timer60hz_data);
    timer60hz_running = false;
  }

  // 4. Disable Hamster PIO
  pio_sm_set_enabled(pio0, 0, false);
  pio_sm_restart(pio0, 0);
  pio_clear_instruction_memory(pio0);

  sleep_ms(20);
  reset_usb_boot(0, 0);
}

int main() {
#if RUNTIME_PIO_ASSEMBLER
  current_mhz = tuning_mhz;
  set_sys_clock_khz(tuning_mhz * 1000, true);
#else
  set_sys_clock_khz(250000, true);
#endif
  stdio_usb_init();
  InitializePins();

  restart_to_restarted_state();

  while (true) {
    pump_usb_rx();

    // ── STATE: WAIT CONFIG ──
    if (fw_state == STATE_WAIT_CONFIG) {
      uint64_t now = time_us_64();

      // Flash the LED at 2Hz with a 50% duty cycle when in RESTARTED mode,
      // waiting for any packet from the Tether.
      // 2Hz period = 500,000 us; 50% duty cycle = 250,000 us ON, 250,000 us OFF.
      if (waiting_for_tether_packet) {
        if ((now % 500000) < 250000) {
          LedOn();
        } else {
          LedOff();
        }

        if (now - last_beacon_us >= 500000) {  // Every 0.5s
          unsigned char pkt[] = {C_RESTARTED, 1, 0};  // Version 1
          send_cobs(pkt, sizeof(pkt));
          last_beacon_us = now;
        }
      } else {
        LedOff();
      }

      while (usb_rx_packets.NumBuffered() > 0) {
        std::string* pkt = usb_rx_packets.Take();
        if (pkt && pkt->length() > 0) {
          byte cmd = (byte)(*pkt)[0];
          if (cmd == T_REFLASH_NOW_PLEASE || cmd == 175 || cmd == 176) {
            delete pkt;
            reflash_now_please();
            break;
          }
          if (cmd == T_RESTART_NOW_PLEASE) {
            delete pkt;
            restart_to_restarted_state();
            break;
          }
          waiting_for_tether_packet = false;
          LedOff();
          if (cmd == T_PICO_RPC) {
            handle_rpc_request(*pkt);
          }
        }
        delete pkt;
      }
    }

    // ── STATE: RUNNING ──
    else if (fw_state == STATE_RUNNING) {
      // 1. Drain console characters
      byte ch;
      while (fg2bg_chars.pop(ch)) {
        unsigned char pkt[] = {C_PUTCHAR, ch};
        send_cobs(pkt, sizeof(pkt));
      }

      // 2. Drain trace cycles (bundle up to 20 records per packet, up to 10 batches per iteration)
      constexpr size_t MAX_BATCH = 20;
      TraceRecord batch[MAX_BATCH];
      for (int b = 0; b < 10 && !fg2bg_trace.empty(); b++) {
        size_t count = 0;
        while (count < MAX_BATCH && fg2bg_trace.pop(batch[count])) {
          count++;
        }
        if (count > 0) {
          uint8_t pkt[1 + 1 + MAX_BATCH * 12];
          pkt[0] = C_TRACE_CYCLES;
          pkt[1] = (uint8_t)count;
          for (size_t i = 0; i < count; i++) {
            size_t off = 2 + i * 12;
            memcpy(&pkt[off + 0], &batch[i].cycle, 8);
            pkt[off + 8] = (uint8_t)(batch[i].addr >> 8);
            pkt[off + 9] = (uint8_t)(batch[i].addr & 0xFF);
            pkt[off + 10] = batch[i].data;
            pkt[off + 11] = batch[i].kind;
          }
          send_cobs(pkt, 2 + count * 12);
        }
      }
      stdio_flush();

      // 3. Process USB packets from Tether
      while (usb_rx_packets.NumBuffered() > 0) {
        std::string* pkt = usb_rx_packets.Take();
        if (pkt && pkt->length() > 0) {
          byte cmd = (byte)(*pkt)[0];
          if (cmd == T_REFLASH_NOW_PLEASE || cmd == 175 || cmd == 176) {
            delete pkt;
            reflash_now_please();
            break;
          }
          if (cmd == T_RESTART_NOW_PLEASE) {
            delete pkt;
            restart_to_restarted_state();
            break;
          } else if (cmd == T_CONSOLE_LINE || cmd == 178 || cmd == 179) {
            // Push line into term_input
            for (size_t i = 1; i < pkt->length(); i++) {
              if (!term_input.Full()) {
                term_input.Put((byte)(*pkt)[i]);
              }
            }
          } else if (cmd == T_PICO_RPC) {
            handle_rpc_request(*pkt);
          }
        }
        delete pkt;
      }

      // 4. Check for Fault
      if (UNLIKELY(fault_triggered)) {
        fw_state = STATE_FAULT;
      }
    }

    // ── STATE: FAULT ──
    else if (fw_state == STATE_FAULT) {
      transmit_core_dump();
      fw_state = STATE_HALTED;
    }

    // ── STATE: HALTED ──
    else if (fw_state == STATE_HALTED) {
      sleep_ms(100);
      while (usb_rx_packets.NumBuffered() > 0) {
        std::string* pkt = usb_rx_packets.Take();
        if (pkt && pkt->length() > 0) {
          byte cmd = (byte)(*pkt)[0];
          if (cmd == T_REFLASH_NOW_PLEASE || cmd == 175 || cmd == 176) {
            delete pkt;
            reflash_now_please();
            break;
          }
          if (cmd == T_RESTART_NOW_PLEASE) {
            delete pkt;
            restart_to_restarted_state();
            break;
          } else if (cmd == T_PICO_RPC) {
            handle_rpc_request(*pkt);
          }
        }
        delete pkt;
      }
    }
  }

  return 0;
}
