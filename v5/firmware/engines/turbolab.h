#pragma once
// turbolab.h -- Engine `turbolab` (`on tfr911h run turbolab [with ...]`).
//
// Brings v4 TurboLab's 4-phase cycle-accurate bus tracer, hardware watchpoints, Red-Page / Zero-Vector /
// BRA-self fault traps, and Phase-4 SWI 6809/6309 register capture into v5's unified TFR911H firmware:
//   - Phase 1: Wait for 6309 reset vector fetch ($FFFE + $FFFF with BS=1), suppressing pre-reset writes.
//   - Phase 2: Full-speed (~1.6 MHz) untraced execution until trigger fires (still checking faults & watches).
//   - Phase 3: Lossless cycle-accurate tracing (FIC `x`, `+`, `r`, `w`, `-`, plus SWI2/RTI fuses for `i`).
//   - Phase 4: Inject SWI ($3F) on LIC (waking CWAI/SYNC via NMI if needed), capture 12B (6809) or 14B
//              (6309 native) register frame, halt CPU, stream traces + 31B C_FAULT + 64KB C_CORE_DUMP,
//              and cleanly shut down / reset back to `TCL>`.
//
// Zero static `.bss` bloat: the 8,192-entry (128 KB) lock-free TraceRecord ring buffer lives inside the
// unused upper 192 KB of `ram[]` (`ram + 65536`), since `turbolab` uses flat 64 KB `ram[0..65535]`.
// Core 1 runs 100% in SRAM (`IN_RAM`) with zero calls to XIP flash (`make check-core1`).

#include <atomic>
#include <cstdint>
#include <cstring>
#include "turbolab_config.h"

namespace turbolab_rt {

// Wire protocol command IDs (shared with host/cmd/tether/turbolab.go)
constexpr byte C_TURBOLAB_START = 186;
constexpr byte C_TRACE_CYCLES   = 187;
constexpr byte C_FAULT          = 188;
constexpr byte C_CORE_DUMP_PKT  = 189;
constexpr byte T_SIGINT         = 183;

// Fault / stop reason codes
constexpr byte FAULT_RED_PAGE       = 1;
constexpr byte FAULT_ZERO_VECTOR    = 2;
constexpr byte FAULT_MAX_CYCLES     = 3;
constexpr byte FAULT_MAX_TIME       = 4;
constexpr byte FAULT_BRA_SELF       = 5;
constexpr byte FAULT_MAX_WATCHPOINT = 6;
constexpr byte FAULT_SIGINT         = 7;
constexpr byte FAULT_TRACE_COUNT    = 8;

// Low-nybble cycle kinds in TraceRecord.kind
constexpr byte KIND_IDLE        = 0;  // '-'
constexpr byte KIND_FIC         = 1;  // 'x'
constexpr byte KIND_OPCODE_CONT = 2;  // '+'
constexpr byte KIND_READ        = 3;  // 'r'
constexpr byte KIND_WRITE       = 4;  // 'w'

// High-nybble live CPU bus status signals in TraceRecord.kind
constexpr uint8_t FLAG_BA   = 0x10;  // 'a'
constexpr uint8_t FLAG_BS   = 0x20;  // 's' (GPIO 28)
constexpr uint8_t FLAG_LIC  = 0x40;  // '_' (GPIO 26)
constexpr uint8_t FLAG_BUSY = 0x80;  // 'y'

struct TraceRecord {
  uint64_t cycle;
  uint16_t addr;
  uint8_t data;
  uint8_t kind;
};
static_assert(sizeof(TraceRecord) == 16, "TraceRecord expected 16 bytes");

// Lock-free SPSC ring buffer backed by `ram + 65536` (8,192 entries = 128 KB inside unused `ram[64K..192K]`).
struct UpperRamTraceFIFO {
  static constexpr uint32_t kCap = 8192;
  static constexpr uint32_t kMask = kCap - 1;
  std::atomic<uint32_t> write_idx{0};
  std::atomic<uint32_t> read_idx{0};

  FORCE_INLINE TraceRecord* IN_RAM base() {
    return reinterpret_cast<TraceRecord*>(&ram[65536]);
  }
  void clear() {
    write_idx.store(0, std::memory_order_relaxed);
    read_idx.store(0, std::memory_order_relaxed);
  }
  FORCE_INLINE bool IN_RAM empty() const {
    return write_idx.load(std::memory_order_acquire) == read_idx.load(std::memory_order_relaxed);
  }
  FORCE_INLINE bool IN_RAM push(uint64_t cy, uint16_t addr, uint8_t data, uint8_t kind) {
    uint32_t w = write_idx.load(std::memory_order_relaxed);
    uint32_t next = (w + 1) & kMask;
    if (UNLIKELY(next == read_idx.load(std::memory_order_acquire))) return false;
    TraceRecord* slot = &base()[w];
    slot->cycle = cy;
    slot->addr = addr;
    slot->data = data;
    slot->kind = kind;
    write_idx.store(next, std::memory_order_release);
    return true;
  }
  bool pop(TraceRecord& out) {
    uint32_t r = read_idx.load(std::memory_order_relaxed);
    if (r == write_idx.load(std::memory_order_acquire)) return false;
    const TraceRecord* slot = &base()[r];
    out.cycle = slot->cycle;
    out.addr = slot->addr;
    out.data = slot->data;
    out.kind = slot->kind;
    read_idx.store((r + 1) & kMask, std::memory_order_release);
    return true;
  }
};

inline UpperRamTraceFIFO g_trace_fifo;

struct CpuRegisterDump {
  bool valid = false;
  bool is_6309_native = false;
  uint16_t pc = 0;
  uint16_t s = 0;
  uint16_t u = 0;
  uint16_t y = 0;
  uint16_t x = 0;
  uint8_t dp = 0;
  uint8_t a = 0;
  uint8_t b = 0;
  uint8_t e = 0;
  uint8_t f = 0;
  uint8_t cc = 0;
  uint8_t streak_len = 0;
};

// Runtime state shared between Core 0 and Core 1 (32-bit/8-bit atomics only: zero libatomic flash calls!)
inline bool g_active = false;
inline std::atomic<bool> g_cpu_started{false};
inline std::atomic<uint8_t> g_current_phase{0};
inline std::atomic<bool> g_fault_triggered{false};
inline std::atomic<uint8_t> g_fault_reason{0};
inline uint64_t g_fault_cycle = 0;
inline uint16_t g_fault_addr = 0;
inline byte g_fault_data = 0;
inline CpuRegisterDump g_cpu_regs;

// Configured execution parameters (written by Core 0 before launching Core 1)
inline uint8_t g_trace_flags = 0;
inline uint64_t g_trigger_cycle = 0;
inline uint64_t g_trigger_time_us = 0;
inline uint64_t g_trace_count_limit = 0;
inline uint64_t g_max_cycles = 0;
inline uint64_t g_max_time_us = 0;
inline uint64_t g_emitted_traces = 0;

// Trace fuse state (Core 1 only)
inline uint8_t g_swi2_write_fuse = 0;
inline bool g_swi2_num_needed = false;
inline uint8_t g_rti_read_fuse = 0;
inline bool g_prefix10_seen = false;
inline uint16_t g_prefix10_addr = 0;

// Pure-SRAM 64-bit hardware microsecond read (never calls SDK `time_us_64()` in flash!)
FORCE_INLINE uint64_t IN_RAM fast_time_us_64() {
  uint32_t hi = timer_hw->timerawh;
  uint32_t lo;
  do {
    lo = timer_hw->timerawl;
    uint32_t next_hi = timer_hw->timerawh;
    if (LIKELY(hi == next_hi)) break;
    hi = next_hi;
  } while (true);
  return ((uint64_t)hi << 32) | lo;
}

FORCE_INLINE void IN_RAM AssertNMI()  { gpio_set_dir(NMI, GPIO_OUT); }
FORCE_INLINE void IN_RAM ReleaseNMI() { gpio_set_dir(NMI, GPIO_IN);  }
FORCE_INLINE void IN_RAM HaltPinOn()  { gpio_set_dir(HALT, GPIO_OUT); }
FORCE_INLINE void IN_RAM HaltPinOff() { gpio_set_dir(HALT, GPIO_IN);  }

// ── Hardware Watchpoint Chain (all callbacks IN_RAM) ──────────────
struct Watchpoint;
using WatchCheckFn = bool (*)(Watchpoint* wp, uint addr, byte kind, bool reading);
using WatchActionFn = void (*)(Watchpoint* wp, uint64_t cycles, uint addr, byte value, byte kind,
                               uint early_pins, uint prev_late_pins);

struct Watchpoint {
  uint16_t addr = 0;
  uint32_t count = 0;
  WatchCheckFn check = nullptr;
  WatchActionFn action = nullptr;
  Watchpoint* next = nullptr;
};

FORCE_INLINE bool IN_RAM wp_match_count(Watchpoint* wp) {
  if (wp->count > 0) {
    if (--wp->count == 0) {
      wp->count = 0xFFFFFFFFu;
      return true;
    }
    return false;
  }
  return true;
}

bool IN_RAM wp_check_read(Watchpoint* wp, uint addr, byte kind, bool reading) {
  if (reading && kind != KIND_IDLE && UNLIKELY(addr == wp->addr)) return wp_match_count(wp);
  return false;
}
bool IN_RAM wp_check_write(Watchpoint* wp, uint addr, byte kind, bool) {
  if (kind == KIND_WRITE && UNLIKELY(addr == wp->addr)) return wp_match_count(wp);
  return false;
}
bool IN_RAM wp_check_exec(Watchpoint* wp, uint addr, byte kind, bool) {
  if (kind == KIND_FIC && UNLIKELY(addr == wp->addr)) return wp_match_count(wp);
  return false;
}
bool IN_RAM wp_check_any(Watchpoint* wp, uint addr, byte kind, bool) {
  if (kind != KIND_IDLE && UNLIKELY(addr == wp->addr)) return wp_match_count(wp);
  return false;
}

inline bool g_cycle_logged_by_wp = false;
inline bool g_trigger_wp_hit = false;

FORCE_INLINE uint8_t IN_RAM pack_cpu_flags(uint early_pins, uint prev_late_pins) {
  uint8_t f = 0;
  if ((early_pins & (1u << BS)) != 0)      f |= FLAG_BS;
  if ((prev_late_pins & (1u << LIC)) != 0) f |= FLAG_LIC;
  return f;
}

void IN_RAM wp_action_log(Watchpoint*, uint64_t cycles, uint addr, byte value, byte kind,
                          uint early_pins, uint prev_late_pins) {
  if (!g_cycle_logged_by_wp) {
    uint8_t packed = (uint8_t)((kind & 0x0F) | pack_cpu_flags(early_pins, prev_late_pins));
    while (!g_trace_fifo.push(cycles, (uint16_t)addr, value, packed)) {
      if (UNLIKELY(g_fault_triggered.load(std::memory_order_relaxed))) break;
      tight_loop_contents();
    }
    g_cycle_logged_by_wp = true;
  }
}

void IN_RAM wp_action_trigger(Watchpoint*, uint64_t, uint, byte, byte, uint, uint) {
  g_trigger_wp_hit = true;
}

void IN_RAM wp_action_max(Watchpoint*, uint64_t cycles, uint addr, byte value, byte, uint, uint) {
  g_fault_reason.store(FAULT_MAX_WATCHPOINT, std::memory_order_relaxed);
  g_fault_cycle = cycles;
  g_fault_addr = (uint16_t)addr;
  g_fault_data = value;
  g_fault_triggered.store(true, std::memory_order_release);
}

constexpr int kMaxWpPool = 20;
inline Watchpoint g_wp_pool[kMaxWpPool];
inline int g_wp_pool_used = 0;
inline Watchpoint* g_wp_head = nullptr;
inline Watchpoint* g_wp_trig_node = nullptr;
inline Watchpoint* g_wp_trig_prev = nullptr;

using WatchHookFn = void (*)(uint64_t cycles, uint addr, byte value, byte kind, uint early_pins, uint prev_late_pins);
inline WatchHookFn g_wp_hook = nullptr;

FORCE_INLINE void IN_RAM wp_detach_trigger() {
  if (g_wp_trig_node != nullptr) {
    if (g_wp_trig_prev != nullptr) g_wp_trig_prev->next = g_wp_trig_node->next;
    else                           g_wp_head = g_wp_trig_node->next;
    g_wp_trig_node = nullptr;
    if (g_wp_head == nullptr) g_wp_hook = nullptr;
  }
}

void IN_RAM run_watchpoint_chain(uint64_t cycles, uint addr, byte value, byte kind,
                                 uint early_pins, uint prev_late_pins) {
  g_cycle_logged_by_wp = false;
  for (Watchpoint* wp = g_wp_head; wp != nullptr; wp = wp->next) {
    if (wp->check(wp, addr, kind, kind != KIND_WRITE)) {
      wp->action(wp, cycles, addr, value, kind, early_pins, prev_late_pins);
      if (UNLIKELY(g_fault_triggered.load(std::memory_order_relaxed))) return;
    }
  }
}

// Writable RAM-backed vectors ($FFF0..$FFFF) for TurboLab (post-reset software can update vectors in RAM!)
byte IN_RAM turbolab_ram_vector_reader(uint addr) { return ram[addr & 0xFFFF]; }
void IN_RAM turbolab_ram_vector_writer(uint addr, byte data) { ram[addr & 0xFFFF] = data; }

FORCE_INLINE void IN_RAM record_fault(byte reason, uint64_t cy, uint addr, byte data) {
  g_fault_reason.store(reason, std::memory_order_relaxed);
  g_fault_cycle = cy;
  g_fault_addr = (uint16_t)addr;
  g_fault_data = data;
  g_fault_triggered.store(true, std::memory_order_release);
}

FORCE_INLINE byte IN_RAM classify_read_cycle(uint addr, bool is_idle, uint prev_late_pins,
                                             byte prev_kind, uint16_t prev_addr) {
  if (UNLIKELY(is_idle)) return KIND_IDLE;
  if ((prev_late_pins & (1u << LIC)) != 0) return KIND_FIC;
  if ((prev_kind == KIND_FIC || prev_kind == KIND_OPCODE_CONT) && addr == (uint16_t)(prev_addr + 1))
    return KIND_OPCODE_CONT;
  return KIND_READ;
}

// Emit trace record if `kind` / interrupt fuses match `g_trace_flags`.
// Returns true if `g_trace_count_limit` was reached on this record.
FORCE_INLINE bool IN_RAM trace_cycle(uint64_t cycles, uint addr, byte value, byte kind, bool reading,
                                     bool is_bs, uint early_pins, uint prev_late_pins, bool already_logged) {
  bool is_rti = (reading && kind == KIND_FIC && value == 0x3B && addr < 0xFFF0);
  if (is_rti) LedOff();

  if (reading) {
    if (kind == KIND_FIC && value == 0x10 && addr < 0xFFF0) {
      g_prefix10_seen = true;
      g_prefix10_addr = (uint16_t)addr;
    } else if (g_prefix10_seen) {
      g_prefix10_seen = false;
      if (value == 0x3F && addr == (uint16_t)(g_prefix10_addr + 1)) {
        g_swi2_num_needed = true;
        g_swi2_write_fuse = 12;
      }
    }
    if (is_rti) {
      g_rti_read_fuse = 13;  // 1 intermediate read + up to 12 stack reads
    }
  }

  if (cycles < g_trigger_cycle) return false;

  bool emit = false;
  switch (kind) {
    case KIND_IDLE:        emit = (g_trace_flags & turbolab::kTraceIdle) != 0; break;
    case KIND_FIC:         emit = (g_trace_flags & turbolab::kTraceX)    != 0; break;
    case KIND_OPCODE_CONT: emit = (g_trace_flags & turbolab::kTracePlus) != 0; break;
    case KIND_READ:        emit = (g_trace_flags & turbolab::kTraceR)    != 0; break;
    case KIND_WRITE:       emit = (g_trace_flags & turbolab::kTraceW)    != 0; break;
  }

  if ((g_trace_flags & turbolab::kTraceI) != 0) {
    if (is_bs || is_rti || g_prefix10_seen) emit = true;
    if (g_swi2_write_fuse > 0) {
      if (!reading && kind == KIND_WRITE) {
        emit = true;
        g_swi2_write_fuse--;
      } else if (reading && g_swi2_num_needed) {
        emit = true;
        if (kind != KIND_OPCODE_CONT || value != 0x3F) g_swi2_num_needed = false;
      }
    }
    if (g_rti_read_fuse > 0 && !is_rti && reading) {
      emit = true;
      if (g_rti_read_fuse == 12 && (value & 0x80) == 0) g_rti_read_fuse = 2;  // FIRQ frame
      else                                              g_rti_read_fuse--;
    }
  }

  if (emit) {
    if (!already_logged) {
      uint8_t packed = (uint8_t)((kind & 0x0F) | pack_cpu_flags(early_pins, prev_late_pins));
      while (!g_trace_fifo.push(cycles, (uint16_t)addr, value, packed)) {
        if (UNLIKELY(g_fault_triggered.load(std::memory_order_relaxed))) break;
        tight_loop_contents();
      }
    }
    g_emitted_traces++;
    if (UNLIKELY(g_trace_count_limit > 0 && g_emitted_traces >= g_trace_count_limit)) {
      record_fault(FAULT_TRACE_COUNT, cycles, addr, value);
      return true;
    }
  }
  return false;
}

FORCE_INLINE bool IN_RAM handle_reset_cycle(uint64_t cy, uint a, byte v, uint ep, uint plp) {
  g_cycle_logged_by_wp = false;
  if (UNLIKELY(g_wp_hook != nullptr)) {
    g_trigger_wp_hit = false;
    g_wp_hook(cy, a, v, KIND_READ, ep, plp);
    if (UNLIKELY(g_fault_triggered.load(std::memory_order_relaxed))) return true;
    if (UNLIKELY(g_trigger_wp_hit)) {
      g_trigger_cycle = cy;
      wp_detach_trigger();
    }
  }
  if (g_trace_flags != 0 && cy >= g_trigger_cycle) {
    if (trace_cycle(cy, a, v, KIND_READ, true, true, ep, plp, g_cycle_logged_by_wp)) return true;
  }
  if (UNLIKELY(g_max_cycles > 0 && cy >= g_max_cycles)) {
    record_fault(FAULT_MAX_CYCLES, cy, a, v);
    return true;
  }
  return false;
}

}  // namespace turbolab_rt

// ═══════════════════════════════════════════════════════════════════
// Core 1 Foreground Engine (`hamster_turbolab`, 100% IN_RAM)
// ═══════════════════════════════════════════════════════════════════
void IN_RAM hamster_turbolab_foreground() {
  using namespace turbolab_rt;
  volatile sio_hw_t* hw = (volatile sio_hw_t*)sio_hw;

  uint64_t cycles = 0;
  uint64_t pre_reset_cycles = 0;
  uint64_t start_us = 0;
  uint prev_late_pins = 0;
  uint16_t prev_addr = 0;
  byte prev_kind = KIND_IDLE;
  bool local_prev_irq = false;
  bool native_6309_seen = false;
  uint16_t bra_fe_addr = 0xFFFFu;

  bool saw_fffe = false;
  byte fffe_val = 0;
  uint fffe_early = 0;
  uint fffe_prev_late = 0;

  LedOff();
  g_swi2_write_fuse = 0;
  g_swi2_num_needed = false;
  g_rti_read_fuse = 0;
  g_prefix10_seen = false;
  g_prefix10_addr = 0;
  g_emitted_traces = 0;

  g_current_phase.store(1, std::memory_order_relaxed);
  foreground_running.store(true, std::memory_order_release);

  // ── Phase 1: Synchronize on Reset Vector Fetch ($FFFE then $FFFF with BS=1) ──
  while (true) {
    if (UNLIKELY(g_fault_triggered.load(std::memory_order_relaxed))) goto phase4;

    for (int i = 0; i < 50; i++) {
      pio_sm_put(pio0, 0, 0);
      uint early_pins = pio_sm_get_blocking(pio0, 0);
      uint addr = 0xFFFFu & hw->gpio_hi_in;
      const bool reading = 0 != (early_pins & (1u << R_W));
      const bool is_bs   = 0 != (early_pins & (1u << BS));

      if (UNLIKELY(!g_cpu_started.load(std::memory_order_acquire))) {
        if (LIKELY(reading)) pio_sm_put(pio0, 0, ram[addr]);
        prev_late_pins = pio_sm_get_blocking(pio0, 0);
        saw_fffe = false;
        pre_reset_cycles = 0;
        continue;
      }

      if (!saw_fffe) {
        if (reading && is_bs && addr == 0xFFFE) {
          saw_fffe = true;
          fffe_val = ram[0xFFFE];
          fffe_early = early_pins;
          fffe_prev_late = prev_late_pins;
        } else {
          pre_reset_cycles++;
          if (UNLIKELY(pre_reset_cycles >= 100000)) {
            record_fault(FAULT_ZERO_VECTOR, 0, addr, 0);
            HaltPinOn();
            goto phase4;
          }
        }
      } else {
        if (reading && is_bs && addr == 0xFFFF) {
          saw_fffe = false;
          cycles = 2;
          start_us = fast_time_us_64();
          byte ffff_val = ram[0xFFFF];
          pio_sm_put(pio0, 0, ffff_val);
          uint ffff_late = pio_sm_get_blocking(pio0, 0);

          // Check Zero Reset Vector ($0000)
          if (UNLIKELY(fffe_val == 0 && ffff_val == 0)) {
            prev_late_pins = ffff_late;
            record_fault(FAULT_ZERO_VECTOR, 2, 0xFFFF, 0);
            goto phase4;
          }

          // Process Cycle #1 ($FFFE) and Cycle #2 ($FFFF) through watchpoints & tracer!
          if (handle_reset_cycle(1, 0xFFFE, fffe_val, fffe_early, fffe_prev_late)) {
            prev_late_pins = ffff_late;
            goto phase4;
          }
          if (handle_reset_cycle(2, 0xFFFF, ffff_val, early_pins, prev_late_pins)) {
            prev_late_pins = ffff_late;
            goto phase4;
          }
          prev_late_pins = ffff_late;
          prev_addr = 0xFFFF;
          prev_kind = KIND_READ;
          goto phase2;
        } else if (reading && is_bs && addr == 0xFFFE) {
          // Stretched $FFFE read
        } else {
          saw_fffe = false;
          pre_reset_cycles++;
        }
      }

      if (LIKELY(reading)) pio_sm_put(pio0, 0, ram[addr]);
      prev_late_pins = pio_sm_get_blocking(pio0, 0);
    }
  }

  // ── Phase 2: Fast Untraced Execution Before Trigger ──
phase2:
  g_current_phase.store(2, std::memory_order_relaxed);
  if (g_trace_flags != 0 && cycles >= g_trigger_cycle && g_trigger_time_us == 0) {
    wp_detach_trigger();
    goto phase3;
  }

  while (true) {
    if (UNLIKELY(g_fault_triggered.load(std::memory_order_relaxed))) {
      if (g_fault_cycle == 0) { g_fault_cycle = cycles; g_fault_addr = prev_addr; }
      goto phase4;
    }

    uint64_t elapsed_us = fast_time_us_64() - start_us;
    if (UNLIKELY(g_max_time_us > 0 && elapsed_us >= g_max_time_us)) {
      record_fault(FAULT_MAX_TIME, cycles, prev_addr, 0);
      goto phase4;
    }
    if (UNLIKELY(g_trace_flags != 0 && g_trigger_time_us > 0 && elapsed_us >= g_trigger_time_us)) {
      g_trigger_cycle = cycles + 1;
      g_trigger_time_us = 0;
      wp_detach_trigger();
      goto phase3;
    }

    bool irq_now = turbo9sim_irq_needed();
    if (irq_now != local_prev_irq) {
      ChangeInterruptPin(irq_now);
      local_prev_irq = irq_now;
    }

    for (int i = 0; i < 50; i++) {
      if (UNLIKELY(g_trace_flags != 0 && g_trigger_time_us == 0 && (cycles + 1) >= g_trigger_cycle)) {
        wp_detach_trigger();
        goto phase3;
      }

      pio_sm_put(pio0, 0, 0);
      uint early_pins = pio_sm_get_blocking(pio0, 0);
      uint addr = 0xFFFFu & hw->gpio_hi_in;
      const bool reading = 0 != (early_pins & (1u << R_W));
      const bool is_bs   = 0 != (early_pins & (1u << BS));
      const bool vma     = 0 != (prev_late_pins & (1u << AVMA));
      const bool is_idle = !vma && !is_bs;
      const bool is_fic  = !is_idle && (0 != (prev_late_pins & (1u << LIC)));
      cycles++;

      bool red_page = (!is_idle && UNLIKELY(addr >= 0xFF00) && UNLIKELY(!IoIsDriven(addr & 0xFF)));
      byte value = 0;
      uint late_pins = 0;

      if (LIKELY(reading)) {
        if (UNLIKELY(is_idle)) {
          value = 0;
        } else if (LIKELY(addr < 0xFF00)) {
          value = ram[addr];
        } else if (UNLIKELY(red_page)) {
          value = ram[addr];
        } else {
          value = IOReaders[addr & 0xFF](addr);
          if (UNLIKELY(is_bs)) {
            LedOn();
            if (UNLIKELY(value == 0)) record_fault(FAULT_ZERO_VECTOR, cycles, addr, 0);
          }
        }
        if (is_fic && value == 0x3B && addr < 0xFFF0) LedOff();
        if (UNLIKELY(value == 0x3D) && !is_idle && addr >= 1 && addr < 0xFFEF && ram[addr - 1] == 0x11) {
          native_6309_seen = (ram[addr + 1] & 0x01) != 0;
        }
        pio_sm_put(pio0, 0, value);
        late_pins = pio_sm_get_blocking(pio0, 0);
      } else {
        late_pins = pio_sm_get_blocking(pio0, 0);
        value = (byte)late_pins;
        if (!is_idle && !red_page) {
          if (LIKELY(addr < 0xFF00)) ram[addr] = value;
          else                       IOWriters[addr & 0xFF](addr, value);
        }
      }

      byte kind = is_idle ? KIND_IDLE : (is_fic ? KIND_FIC : (reading ? KIND_READ : KIND_WRITE));

      if (UNLIKELY(g_wp_hook != nullptr)) {
        g_trigger_wp_hit = false;
        g_wp_hook(cycles, addr, value, kind, early_pins, prev_late_pins);
        if (UNLIKELY(g_fault_triggered.load(std::memory_order_relaxed))) {
          prev_late_pins = late_pins;
          goto phase4;
        }
        if (UNLIKELY(g_trigger_wp_hit)) {
          g_trigger_cycle = cycles;
          bool hit_limit = false;
          if (g_trace_flags != 0) {
            hit_limit = trace_cycle(cycles, addr, value, kind, reading, is_bs, early_pins, prev_late_pins, g_cycle_logged_by_wp);
          }
          prev_late_pins = late_pins;
          prev_addr = (uint16_t)addr;
          prev_kind = kind;
          wp_detach_trigger();
          if (UNLIKELY(red_page)) {
            record_fault(FAULT_RED_PAGE, cycles, addr, value);
            goto phase4;
          }
          if (UNLIKELY(hit_limit)) goto phase4;
          goto phase3;
        }
      }

      prev_late_pins = late_pins;
      prev_addr = (uint16_t)addr;
      prev_kind = kind;

      if (UNLIKELY(red_page)) {
        record_fault(FAULT_RED_PAGE, cycles, addr, value);
        goto phase4;
      }
      if (!is_idle && (addr < bra_fe_addr || addr > (uint)(bra_fe_addr + 1))) {
        bra_fe_addr = 0xFFFFu;
      }
      if (UNLIKELY(value == 0x20) && reading && !is_idle && addr < 0xFFEF && ram[addr + 1] == 0xFE) {
        if (is_fic || bra_fe_addr == (uint16_t)addr) {
          record_fault(FAULT_BRA_SELF, cycles, addr, value);
          goto phase4;
        }
        bra_fe_addr = (uint16_t)addr;
      }
      if (UNLIKELY(g_max_cycles > 0 && cycles >= g_max_cycles)) {
        record_fault(FAULT_MAX_CYCLES, cycles, addr, value);
        goto phase4;
      }
      if (UNLIKELY(g_fault_triggered.load(std::memory_order_relaxed))) {
        if (g_fault_cycle == 0) { g_fault_cycle = cycles; g_fault_addr = (uint16_t)addr; g_fault_data = value; }
        goto phase4;
      }
    }
  }

  // ── Phase 3: Cycle-Accurate Tracing After Trigger ──
phase3:
  g_current_phase.store(3, std::memory_order_relaxed);
  while (true) {
    if (UNLIKELY(g_fault_triggered.load(std::memory_order_relaxed))) {
      if (g_fault_cycle == 0) { g_fault_cycle = cycles; g_fault_addr = prev_addr; }
      goto phase4;
    }

    if (UNLIKELY(g_max_time_us > 0 && (fast_time_us_64() - start_us) >= g_max_time_us)) {
      record_fault(FAULT_MAX_TIME, cycles, prev_addr, 0);
      goto phase4;
    }

    bool irq_now = turbo9sim_irq_needed();
    if (irq_now != local_prev_irq) {
      ChangeInterruptPin(irq_now);
      local_prev_irq = irq_now;
    }

    for (int i = 0; i < 50; i++) {
      pio_sm_put(pio0, 0, 0);
      uint early_pins = pio_sm_get_blocking(pio0, 0);
      uint addr = 0xFFFFu & hw->gpio_hi_in;
      const bool reading = 0 != (early_pins & (1u << R_W));
      const bool is_bs   = 0 != (early_pins & (1u << BS));
      const bool vma     = 0 != (prev_late_pins & (1u << AVMA));
      const bool is_idle = !vma && !is_bs;
      cycles++;

      bool red_page = (!is_idle && UNLIKELY(addr >= 0xFF00) && UNLIKELY(!IoIsDriven(addr & 0xFF)));
      byte value = 0;
      byte kind = KIND_IDLE;
      uint late_pins = 0;

      if (LIKELY(reading)) {
        if (UNLIKELY(is_idle)) {
          value = 0;
        } else if (LIKELY(addr < 0xFF00)) {
          value = ram[addr];
        } else if (UNLIKELY(red_page)) {
          value = ram[addr];
        } else {
          value = IOReaders[addr & 0xFF](addr);
          if (UNLIKELY(is_bs)) {
            LedOn();
            if (UNLIKELY(value == 0)) record_fault(FAULT_ZERO_VECTOR, cycles, addr, 0);
          }
        }
        if (UNLIKELY(value == 0x3D) && !is_idle && addr >= 1 && addr < 0xFFEF && ram[addr - 1] == 0x11) {
          native_6309_seen = (ram[addr + 1] & 0x01) != 0;
        }
        pio_sm_put(pio0, 0, value);
        late_pins = pio_sm_get_blocking(pio0, 0);
        kind = classify_read_cycle(addr, is_idle, prev_late_pins, prev_kind, prev_addr);
      } else {
        late_pins = pio_sm_get_blocking(pio0, 0);
        value = (byte)late_pins;
        if (!is_idle && !red_page) {
          if (LIKELY(addr < 0xFF00)) ram[addr] = value;
          else                       IOWriters[addr & 0xFF](addr, value);
        }
        kind = is_idle ? KIND_IDLE : KIND_WRITE;
      }

      g_cycle_logged_by_wp = false;
      if (UNLIKELY(g_wp_hook != nullptr)) {
        g_wp_hook(cycles, addr, value, kind, early_pins, prev_late_pins);
      }
      bool hit_trace_count = trace_cycle(cycles, addr, value, kind, reading, is_bs,
                                         early_pins, prev_late_pins, g_cycle_logged_by_wp);

      prev_late_pins = late_pins;
      prev_addr = (uint16_t)addr;
      prev_kind = kind;

      if (UNLIKELY(red_page)) {
        record_fault(FAULT_RED_PAGE, cycles, addr, value);
        goto phase4;
      }
      if (UNLIKELY(hit_trace_count)) {
        goto phase4;
      }
      if (!is_idle && (addr < bra_fe_addr || addr > (uint)(bra_fe_addr + 1))) {
        bra_fe_addr = 0xFFFFu;
      }
      if (UNLIKELY(value == 0x20) && reading && !is_idle && addr < 0xFFEF && ram[addr + 1] == 0xFE) {
        if (kind == KIND_FIC || bra_fe_addr == (uint16_t)addr) {
          record_fault(FAULT_BRA_SELF, cycles, addr, value);
          goto phase4;
        }
        bra_fe_addr = (uint16_t)addr;
      }
      if (UNLIKELY(g_max_cycles > 0 && cycles >= g_max_cycles)) {
        record_fault(FAULT_MAX_CYCLES, cycles, addr, value);
        goto phase4;
      }
      if (UNLIKELY(g_fault_triggered.load(std::memory_order_relaxed))) {
        if (g_fault_cycle == 0) { g_fault_cycle = cycles; g_fault_addr = (uint16_t)addr; g_fault_data = value; }
        goto phase4;
      }
    }
  }

  // ── Phase 4: LIC-Aligned SWI ($3F) / NMI Register Snarfing & CPU Halt ──
  // S63: In 6309 native mode (`LDMD #1`), the HD63C09E prefetches the next opcode *during* the LIC=1
  // cycle itself, so injecting $3F after LIC=1 would replace an operand rather than an opcode!
  // Asserting hardware NMI# in 6309 native mode finishes the current instruction cleanly, discards the
  // 1-byte prefetch, pushes the exact 14-byte native register frame onto S, and halts at $FFFC (BS=1).
phase4:
  g_current_phase.store(4, std::memory_order_relaxed);
  enum P4State { P4_AWAIT_LIC, P4_INJECT_SWI, P4_AWAIT_STACK_WRITES, P4_DONE };
  bool p4_via_nmi = native_6309_seen;
  P4State p4_state = p4_via_nmi ? P4_AWAIT_STACK_WRITES
                                : (((prev_late_pins & (1u << LIC)) != 0) ? P4_INJECT_SWI : P4_AWAIT_LIC);
  if (p4_via_nmi) {
    AssertNMI();
  }

  uint16_t swi_injected_pc = 0;
  uint16_t write_start = 0;
  uint8_t  write_streak_count = 0;
  uint8_t  swi_stack_bytes[16];
  for (int k = 0; k < 16; k++) swi_stack_bytes[k] = 0;

  g_cpu_regs.valid = false;
  g_cpu_regs.is_6309_native = false;
  g_cpu_regs.streak_len = 0;

  for (int p4_cycle = 0; p4_cycle < 256; p4_cycle++) {
    if (p4_state == P4_AWAIT_LIC && p4_cycle >= 16) {
      AssertNMI();  // Wake CPU if sleeping in CWAI / SYNC
    }
    // Fallback if S was never loaded before LDMD #1 (so NMI was disarmed): switch to $3F SWI feed
    if (p4_via_nmi && p4_cycle == 32 && write_streak_count == 0) {
      ReleaseNMI();
      p4_via_nmi = false;
      p4_state = P4_INJECT_SWI;
    }

    pio_sm_put(pio0, 0, 0);
    uint early_pins = pio_sm_get_blocking(pio0, 0);
    uint addr = 0xFFFFu & hw->gpio_hi_in;
    const bool reading = 0 != (early_pins & (1u << R_W));
    const bool is_bs   = 0 != (early_pins & (1u << BS));
    cycles++;

    if (LIKELY(reading)) {
      if ((addr == 0xFFFA || addr == 0xFFFB || (is_bs && addr >= 0xFFF8 && addr <= 0xFFFD)) &&
          (write_streak_count == 12 || write_streak_count == 14)) {
        HaltPinOn();
        p4_state = P4_DONE;
        pio_sm_put(pio0, 0, ram[addr]);
        prev_late_pins = pio_sm_get_blocking(pio0, 0);
        break;
      }

      if (is_bs && (addr == 0xFFF6 || addr == 0xFFF7) && write_streak_count == 3) {
        write_streak_count = 0;
        p4_state = p4_via_nmi ? P4_AWAIT_STACK_WRITES : P4_AWAIT_LIC;
      } else if (write_streak_count > 0 && write_streak_count != 12 && write_streak_count != 14) {
        write_streak_count = 0;
      }

      byte value;
      if (p4_state == P4_INJECT_SWI && !is_bs && addr < 0xFFF0) {
        if (swi_injected_pc == 0) swi_injected_pc = (uint16_t)addr;
        value = 0x3F;  // SWI opcode
        if (!native_6309_seen) p4_state = P4_AWAIT_STACK_WRITES;
      } else {
        value = ram[addr];
      }

      pio_sm_put(pio0, 0, value);
      uint late_pins = pio_sm_get_blocking(pio0, 0);
      if (p4_state == P4_AWAIT_LIC && (late_pins & (1u << LIC)) != 0) {
        p4_state = P4_INJECT_SWI;
      }
      prev_late_pins = late_pins;
    } else {
      uint late_pins = pio_sm_get_blocking(pio0, 0);
      byte value = (byte)late_pins;
      ram[addr] = value;

      if (p4_state == P4_INJECT_SWI) {
        p4_state = P4_AWAIT_STACK_WRITES;
      }
      if (p4_state == P4_AWAIT_STACK_WRITES) {
        if (write_streak_count == 0) write_start = (uint16_t)addr;
        if (write_streak_count < 16) swi_stack_bytes[write_streak_count] = value;
        write_streak_count++;
      }
      if (p4_state == P4_AWAIT_LIC && (late_pins & (1u << LIC)) != 0) {
        p4_state = P4_INJECT_SWI;
      }
      prev_late_pins = late_pins;
    }
  }

  ReleaseNMI();
  ChangeInterruptPin(false);
  HaltPinOn();

  if (write_streak_count == 12) {
    g_cpu_regs.valid = true;
    g_cpu_regs.is_6309_native = false;
    uint16_t pushed_pc = ((uint16_t)swi_stack_bytes[1] << 8) | swi_stack_bytes[0];
    g_cpu_regs.pc = p4_via_nmi ? pushed_pc : (swi_injected_pc ? swi_injected_pc : (uint16_t)(pushed_pc ? pushed_pc - 1 : 0));
    g_cpu_regs.s = (uint16_t)(write_start + 1);
    g_cpu_regs.u = ((uint16_t)swi_stack_bytes[3] << 8) | swi_stack_bytes[2];
    g_cpu_regs.y = ((uint16_t)swi_stack_bytes[5] << 8) | swi_stack_bytes[4];
    g_cpu_regs.x = ((uint16_t)swi_stack_bytes[7] << 8) | swi_stack_bytes[6];
    g_cpu_regs.dp = swi_stack_bytes[8];
    g_cpu_regs.b = swi_stack_bytes[9];
    g_cpu_regs.a = swi_stack_bytes[10];
    g_cpu_regs.e = 0;
    g_cpu_regs.f = 0;
    g_cpu_regs.cc = swi_stack_bytes[11];
    g_cpu_regs.streak_len = 12;
  } else if (write_streak_count == 14) {
    g_cpu_regs.valid = true;
    g_cpu_regs.is_6309_native = true;
    uint16_t pushed_pc = ((uint16_t)swi_stack_bytes[1] << 8) | swi_stack_bytes[0];
    g_cpu_regs.pc = p4_via_nmi ? pushed_pc : (swi_injected_pc ? swi_injected_pc : (uint16_t)(pushed_pc ? pushed_pc - 1 : 0));
    g_cpu_regs.s = (uint16_t)(write_start + 1);
    g_cpu_regs.u = ((uint16_t)swi_stack_bytes[3] << 8) | swi_stack_bytes[2];
    g_cpu_regs.y = ((uint16_t)swi_stack_bytes[5] << 8) | swi_stack_bytes[4];
    g_cpu_regs.x = ((uint16_t)swi_stack_bytes[7] << 8) | swi_stack_bytes[6];
    g_cpu_regs.dp = swi_stack_bytes[8];
    g_cpu_regs.f = swi_stack_bytes[9];
    g_cpu_regs.e = swi_stack_bytes[10];
    g_cpu_regs.b = swi_stack_bytes[11];
    g_cpu_regs.a = swi_stack_bytes[12];
    g_cpu_regs.cc = swi_stack_bytes[13];
    g_cpu_regs.streak_len = 14;
  }

  foreground_running.store(false, std::memory_order_relaxed);
  g_current_phase.store(0, std::memory_order_release);
}

// ═══════════════════════════════════════════════════════════════════
// Core 0 Setup, Streaming Loader, Trace Drain & Fault Dump
// ═══════════════════════════════════════════════════════════════════

// Stream `.img` (64 KB), `.decb`, or OS-9 module file into `ram[0..65535]` using <=256B chunks (zero heap!).
inline bool turbolab_load_vfs_image(const char* path, char* err_buf, size_t err_cap) {
  struct vfs_info st;
  if (vfs_stat(path, &st) != 0 || st.type == LFS_TYPE_DIR) {
    snprintf(err_buf, err_cap, "cannot open image '%s'", path);
    return false;
  }
  auto f = vfs_resolve(path);
  if (!f || f->open_file(LFS_O_RDONLY) < 0) {
    snprintf(err_buf, err_cap, "cannot open image '%s'", path);
    return false;
  }

  memset(ram, 0, 65536);
  byte buf[256];

  // Case 1: 65536-byte raw `.img` / `.rom`
  if (st.size == 65536) {
    for (uint32_t off = 0; off < 65536; off += 256) {
      if (f->read(&ram[off], 256) != 256) {
        f->close_file(nullptr);
        snprintf(err_buf, err_cap, "short read at offset %u in '%s'", (unsigned)off, path);
        return false;
      }
    }
    f->close_file(nullptr);
    return true;
  }

  // Peek header bytes to distinguish DECB ($00 ...) vs OS-9 module ($87 $CD ...)
  if (st.size < 9 || st.size > 65536) {
    f->close_file(nullptr);
    snprintf(err_buf, err_cap, "unsupported image size %u in '%s'", (unsigned)st.size, path);
    return false;
  }
  byte hdr[5];
  if (f->read(hdr, 5) < 5) {
    f->close_file(nullptr);
    snprintf(err_buf, err_cap, "short header read in '%s'", path);
    return false;
  }
  f->seek(0, LFS_SEEK_SET, nullptr);

  // Case 2: DECB binary ($00 len_hi len_lo addr_hi addr_lo ... $FF $00 $00 exec_hi exec_lo)
  if (hdr[0] == 0x00) {
    bool found_exec = false;
    while (true) {
      int n = (int)f->read(hdr, 5);
      if (n == 0) break;
      if (n != 5) {
        f->close_file(nullptr);
        snprintf(err_buf, err_cap, "truncated DECB block header in '%s'", path);
        return false;
      }
      uint16_t sz = ((uint16_t)hdr[1] << 8) | hdr[2];
      uint16_t addr = ((uint16_t)hdr[3] << 8) | hdr[4];
      if (hdr[0] == 0x00) {
        if ((uint32_t)addr + sz > 65536) {
          f->close_file(nullptr);
          snprintf(err_buf, err_cap, "DECB block ($%04X + $%04X) exceeds 64KB in '%s'", addr, sz, path);
          return false;
        }
        uint16_t rem = sz;
        uint32_t dst = addr;
        while (rem > 0) {
          uint16_t chunk = (rem > 256) ? 256 : rem;
          if (f->read(buf, chunk) != chunk) {
            f->close_file(nullptr);
            snprintf(err_buf, err_cap, "truncated DECB payload in '%s'", path);
            return false;
          }
          memcpy(&ram[dst], buf, chunk);
          dst += chunk;
          rem -= chunk;
        }
      } else if (hdr[0] == 0xFF) {
        ram[0xFFFE] = (byte)(addr >> 8);
        ram[0xFFFF] = (byte)(addr & 0xFF);
        found_exec = true;
        break;
      } else {
        f->close_file(nullptr);
        snprintf(err_buf, err_cap, "unknown DECB block tag $%02X in '%s'", hdr[0], path);
        return false;
      }
    }
    f->close_file(nullptr);
    if (!found_exec) {
      snprintf(err_buf, err_cap, "DECB file '%s' missing $FF exec record", path);
      return false;
    }
    return true;
  }

  // Case 3: Raw OS-9 module file ($87 $CD ...), loaded upward ending at $FF00
  if (hdr[0] == 0x87 && hdr[1] == 0xCD && st.size <= 0xFF00) {
    uint32_t begin = 0xFF00u - (uint32_t)st.size;
    uint32_t rem = (uint32_t)st.size;
    uint32_t dst = begin;
    while (rem > 0) {
      uint32_t chunk = (rem > 256) ? 256 : rem;
      if ((uint32_t)f->read(&ram[dst], chunk) != chunk) {
        f->close_file(nullptr);
        snprintf(err_buf, err_cap, "short module read in '%s'", path);
        return false;
      }
      dst += chunk;
      rem -= chunk;
    }
    f->close_file(nullptr);
    for (uint i = 0; i < 7; i++) {
      ram[0xFFF0 + 2 * i + 0] = (byte)(Turbo9os_Vectors[i] >> 8);
      ram[0xFFF0 + 2 * i + 1] = (byte)(Turbo9os_Vectors[i] & 0xFF);
    }
    // Locate `kernel` or `krn` module, else fall back to `begin`
    turbolab::WatchSpec kw{};
    kw.symbolic = true;
    strcpy(kw.mod_name, "kernel");
    uint16_t mod_base = (uint16_t)begin;
    if (turbolab::ResolveModuleWatchpoint(ram, kw, nullptr, 0)) {
      mod_base = kw.addr;
    } else {
      kw.symbolic = true;
      strcpy(kw.mod_name, "krn");
      if (turbolab::ResolveModuleWatchpoint(ram, kw, nullptr, 0)) mod_base = kw.addr;
    }
    uint16_t entry_off = ((uint16_t)ram[mod_base + 9] << 8) | ram[mod_base + 10];
    uint16_t reset_vec = (uint16_t)(mod_base + entry_off);
    ram[0xFFFE] = (byte)(reset_vec >> 8);
    ram[0xFFFF] = (byte)(reset_vec & 0xFF);
    return true;
  }

  f->close_file(nullptr);
  snprintf(err_buf, err_cap, "unrecognized image format in '%s' (expected 64KB .img, .decb, or OS-9 module)", path);
  return false;
}

inline void turbolab_drain_batches(int max_batches) {
  using namespace turbolab_rt;
  constexpr size_t kMaxBatch = 20;
  TraceRecord batch[kMaxBatch];
  int sent = 0;
  while (!g_trace_fifo.empty() && (max_batches <= 0 || sent < max_batches)) {
    size_t count = 0;
    while (count < kMaxBatch && g_trace_fifo.pop(batch[count])) count++;
    if (count == 0) break;
    if (usb_tether_ok()) {
      uint8_t pkt[2 + kMaxBatch * 12];
      pkt[0] = C_TRACE_CYCLES;
      pkt[1] = (uint8_t)count;
      for (size_t i = 0; i < count; i++) {
        size_t off = 2 + i * 12;
        memcpy(&pkt[off + 0], &batch[i].cycle, 8);
        pkt[off + 8]  = (uint8_t)(batch[i].addr >> 8);
        pkt[off + 9]  = (uint8_t)(batch[i].addr & 0xFF);
        pkt[off + 10] = batch[i].data;
        pkt[off + 11] = batch[i].kind;
      }
      CobsEncodeAndTransmit(pkt, 2 + count * 12, [](int ch) { putchar_raw(ch); });
      if (max_batches <= 0) {
        stdio_flush();
        sleep_us(400);
      }
    }
    sent++;
  }
  if (sent > 0 && usb_tether_ok()) stdio_flush();
}

inline void turbolab_stream_ram64k() {
  using namespace turbolab_rt;
  if (!usb_tether_ok()) return;
  uint8_t dump_pkt[258];
  dump_pkt[0] = C_CORE_DUMP_PKT;
  for (int chunk = 0; chunk < 256; chunk++) {
    dump_pkt[1] = (uint8_t)chunk;
    memcpy(&dump_pkt[2], &ram[chunk * 256], 256);
    CobsEncodeAndTransmit(dump_pkt, sizeof(dump_pkt), [](int ch) { putchar_raw(ch); });
    if ((chunk & 7) == 7) {
      stdio_flush();
      sleep_us(400);
    }
  }
  stdio_flush();
}

inline void turbolab_poll_core0() {
  using namespace turbolab_rt;
  if (!g_active) return;

  // 1. Drain pending trace records (up to 10 packets of 20 records per scheduler turn)
  turbolab_drain_batches(10);

  // 2. Check if Core 1 hit a fault or execution limit
  if (!g_fault_triggered.load(std::memory_order_acquire)) return;

  // Wait up to 50 ms for Core 1 to finish Phase-4 SWI register capture and halt the 6309
  uint64_t t0 = time_us_64();
  while (g_current_phase.load(std::memory_order_acquire) != 0 && (time_us_64() - t0) < 50000) {
    tight_loop_contents();
  }

  // Drain all remaining console chars and trace records in exact order before fault header
  drain_chars();
  turbolab_drain_batches(0);
  sleep_ms(5);
  drain_chars();

  byte reason = g_fault_reason.load(std::memory_order_relaxed);
  if (usb_tether_ok()) {
    uint8_t fault_pkt[31];
    fault_pkt[0]  = C_FAULT;
    fault_pkt[1]  = reason;
    memcpy(&fault_pkt[2], &g_fault_cycle, 8);
    fault_pkt[10] = (uint8_t)(g_fault_addr >> 8);
    fault_pkt[11] = (uint8_t)(g_fault_addr & 0xFF);
    fault_pkt[12] = g_fault_data;
    fault_pkt[13] = g_cpu_regs.valid ? 1 : 0;
    fault_pkt[14] = g_cpu_regs.is_6309_native ? 1 : 0;
    fault_pkt[15] = (uint8_t)(g_cpu_regs.pc >> 8);
    fault_pkt[16] = (uint8_t)(g_cpu_regs.pc & 0xFF);
    fault_pkt[17] = (uint8_t)(g_cpu_regs.s >> 8);
    fault_pkt[18] = (uint8_t)(g_cpu_regs.s & 0xFF);
    fault_pkt[19] = (uint8_t)(g_cpu_regs.u >> 8);
    fault_pkt[20] = (uint8_t)(g_cpu_regs.u & 0xFF);
    fault_pkt[21] = (uint8_t)(g_cpu_regs.y >> 8);
    fault_pkt[22] = (uint8_t)(g_cpu_regs.y & 0xFF);
    fault_pkt[23] = (uint8_t)(g_cpu_regs.x >> 8);
    fault_pkt[24] = (uint8_t)(g_cpu_regs.x & 0xFF);
    fault_pkt[25] = g_cpu_regs.dp;
    fault_pkt[26] = g_cpu_regs.a;
    fault_pkt[27] = g_cpu_regs.b;
    fault_pkt[28] = g_cpu_regs.e;
    fault_pkt[29] = g_cpu_regs.f;
    fault_pkt[30] = g_cpu_regs.cc;

    CobsEncodeAndTransmit(fault_pkt, sizeof(fault_pkt), [](int ch) { putchar_raw(ch); });
    stdio_flush();
    sleep_ms(2);

    turbolab_stream_ram64k();

    byte exit_code = 1;
    if (reason == FAULT_MAX_CYCLES || reason == FAULT_MAX_TIME ||
        reason == FAULT_MAX_WATCHPOINT || reason == FAULT_TRACE_COUNT) {
      exit_code = 0;
    } else if (reason == FAULT_SIGINT) {
      exit_code = 130;
    }
    cobs_shutdown(exit_code);
    stdio_flush();
  }

  sleep_ms(100);
  rp2350_reset_standard();
}

inline void setup_turbolab();

inline void turbolab_on_sigint() {
  using namespace turbolab_rt;
  if (!g_active) return;
  uint8_t ph = g_current_phase.load(std::memory_order_acquire);
  if (g_cpu_started.load(std::memory_order_acquire) && (ph == 2 || ph == 3)) {
    g_fault_reason.store(FAULT_SIGINT, std::memory_order_relaxed);
    g_fault_cycle = 0;
    g_fault_addr = 0;
    g_fault_data = 0;
    g_fault_triggered.store(true, std::memory_order_release);
  }
}

inline void setup_turbolab() {
  using namespace turbolab_rt;

  turbolab::TurbolabPlan plan;
  PortsCheck pc = CompileTurbolabPlan(g_port_settings, plan);
  if (!pc.ok) {
    cobs_printf("turbolab: %s\n", pc.msg);
    centipede_abort("turbolab: invalid plan");
  }

  // 1. Load guest image into `ram[0..65535]`: `WITH image = ...`, else `Config(flat_img)`, else built-in Turbo9OS!
  const char* img_path = plan.image[0] ? plan.image : (g_config.flat_img[0] ? g_config.flat_img : nullptr);
  if (img_path && img_path[0]) {
    char err_buf[128] = {};
    cobs_printf("[turbolab: loading image '%s']\n", img_path);
    if (!turbolab_load_vfs_image(img_path, err_buf, sizeof(err_buf))) {
      cobs_printf("turbolab: %s\n", err_buf);
      centipede_abort(err_buf);
    }
  } else {
    memset(ram, 0, 65536);
    constexpr uint n = Turbo9osActiveRoms::total_size;
    constexpr uint begin = 0xFF00 - n;
    cobs_printf("[turbolab: installing built-in Turbo9OS (%u bytes at $%04X)]\n", n, begin);
    for (uint i = 0; i < n; i++) {
      ram[(begin + i) & 0xFFFF] = Turbo9osActiveRoms::data[i];
    }
    for (uint i = 0; i < 7; i++) {
      ram[0xFFF0 + 2 * i + 0] = (byte)(Turbo9os_Vectors[i] >> 8);
      ram[0xFFF0 + 2 * i + 1] = (byte)(Turbo9os_Vectors[i] & 0xFF);
    }
    uint entry = (((uint)ram[begin + 9]) << 8) | ram[begin + 10];
    uint reset_vec = begin + entry;
    ram[0xFFFE] = (byte)(reset_vec >> 8);
    ram[0xFFFF] = (byte)(reset_vec & 0xFF);
  }

  // 2. Resolve any symbolic `@module+offset` watchpoints against loaded `ram[0..65535]`
  char sym_err[128] = {};
  if (plan.has_trigger_wp && plan.trigger_wp.symbolic) {
    if (!turbolab::ResolveModuleWatchpoint(ram, plan.trigger_wp, sym_err, sizeof(sym_err))) {
      cobs_printf("turbolab: %s\n", sym_err);
      centipede_abort(sym_err);
    }
    cobs_printf("[turbolab: resolved trigger @%s%+d -> $%04X]\n",
                plan.trigger_wp.mod_name, (int)plan.trigger_wp.mod_offset, plan.trigger_wp.addr);
  }
  if (plan.has_stop_wp && plan.stop_wp.symbolic) {
    if (!turbolab::ResolveModuleWatchpoint(ram, plan.stop_wp, sym_err, sizeof(sym_err))) {
      cobs_printf("turbolab: %s\n", sym_err);
      centipede_abort(sym_err);
    }
    cobs_printf("[turbolab: resolved stop @%s%+d -> $%04X]\n",
                plan.stop_wp.mod_name, (int)plan.stop_wp.mod_offset, plan.stop_wp.addr);
  }
  for (int i = 0; i < plan.num_watches; i++) {
    if (plan.watches[i].symbolic) {
      if (!turbolab::ResolveModuleWatchpoint(ram, plan.watches[i], sym_err, sizeof(sym_err))) {
        cobs_printf("turbolab: %s\n", sym_err);
        centipede_abort(sym_err);
      }
      cobs_printf("[turbolab: resolved watch @%s%+d -> $%04X]\n",
                  plan.watches[i].mod_name, (int)plan.watches[i].mod_offset, plan.watches[i].addr);
    }
  }

  // 3. Install core I/O ($FF00..$FF03 Turbo9Sim ACIA + $FFF0..$FFFF RAM vectors) + any enabled enhancements!
  IoReset();
  Turbo9SimAcia::Install(PortBase("turbo9sim_acia", Turbo9SimAcia::kDefaultBase));
  IoClaim(0xFFF0, 0xFFFF, "vectors");
  for (uint a = 0xFFF0; a <= 0xFFFF; a++) {
    IoSet((uint16_t)a, turbolab_ram_vector_reader, turbolab_ram_vector_writer);
  }
  IoFinish();  // Also installs any enabled `with ...` enhancements and populates g_io_drive_mask!

  // 4. Configure runtime state & watchpoint chain for Core 1
  g_trace_fifo.clear();
  {
    byte discard;
    while (fg2bg_chars.pop(discard)) {}
  }
  term_input.Reset();

  g_cpu_started.store(false, std::memory_order_relaxed);
  g_current_phase.store(0, std::memory_order_relaxed);
  g_fault_triggered.store(false, std::memory_order_relaxed);
  g_fault_reason.store(0, std::memory_order_relaxed);
  g_fault_cycle = 0;
  g_fault_addr = 0;
  g_fault_data = 0;
  g_cpu_regs = CpuRegisterDump{};

  g_trace_flags = plan.trace_flags;
  g_trigger_cycle = plan.trigger_cycle;
  g_trigger_time_us = plan.trigger_time_us;
  g_trace_count_limit = plan.trace_count;
  g_max_cycles = plan.max_cycles;
  g_max_time_us = plan.max_time_us;

  g_wp_pool_used = 0;
  g_wp_head = nullptr;
  g_wp_trig_node = nullptr;
  g_wp_trig_prev = nullptr;
  Watchpoint* last_wp = nullptr;

  auto get_check = [](char t) -> WatchCheckFn {
    if (t == 'r') return wp_check_read;
    if (t == 'w') return wp_check_write;
    if (t == 'x') return wp_check_exec;
    return wp_check_any;
  };
  auto append_wp = [&](WatchCheckFn chk, WatchActionFn act, uint16_t addr, uint32_t cnt) -> Watchpoint* {
    if (g_wp_pool_used >= kMaxWpPool) return nullptr;
    Watchpoint* node = &g_wp_pool[g_wp_pool_used++];
    node->addr = addr;
    node->count = cnt;
    node->check = chk;
    node->action = act;
    node->next = nullptr;
    if (last_wp) last_wp->next = node;
    else         g_wp_head = node;
    last_wp = node;
    return node;
  };

  for (int i = 0; i < plan.num_watches; i++) {
    append_wp(get_check(plan.watches[i].type), wp_action_log, plan.watches[i].addr, plan.watches[i].count);
  }
  if (plan.has_trigger_wp) {
    g_wp_trig_prev = last_wp;
    g_wp_trig_node = append_wp(get_check(plan.trigger_wp.type), wp_action_trigger,
                               plan.trigger_wp.addr, plan.trigger_wp.count);
    g_trigger_cycle = UINT64_MAX;
  }
  if (plan.has_stop_wp) {
    append_wp(get_check(plan.stop_wp.type), wp_action_max, plan.stop_wp.addr, plan.stop_wp.count);
  }
  g_wp_hook = (g_wp_head != nullptr) ? run_watchpoint_chain : nullptr;

  // 5. Notify Tether (`C_TURBOLAB_START` + initial 64KB RAM image for OS-9 module scan / listings / SWI2 strings)
  if (usb_tether_ok()) {
    uint8_t start_pkt[3 + turbolab::kMaxWatches * 3];
    start_pkt[0] = C_TURBOLAB_START;
    start_pkt[1] = g_trace_flags;
    start_pkt[2] = (uint8_t)plan.num_watches;
    for (int i = 0; i < plan.num_watches; i++) {
      start_pkt[3 + i * 3 + 0] = (uint8_t)plan.watches[i].type;
      start_pkt[3 + i * 3 + 1] = (uint8_t)(plan.watches[i].addr >> 8);
      start_pkt[3 + i * 3 + 2] = (uint8_t)(plan.watches[i].addr & 0xFF);
    }
    CobsEncodeAndTransmit(start_pkt, 3 + (size_t)plan.num_watches * 3, [](int ch) { putchar_raw(ch); });
    stdio_flush();
    turbolab_stream_ram64k();
  }

  g_active = true;
}
