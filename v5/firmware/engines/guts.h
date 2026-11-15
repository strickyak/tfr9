#pragma once
// guts.h -- Hamster PIO bus-cycle engines (core 1 foreground, IN_RAM).
//
// Spec S5 / S6 / section 5.5 / Appendix C:
// Instead of one monolithic loop branching on `current_engine` on every 6309 bus cycle, the shared
// Hamster PIO handshake (`HamsterForegroundLoop<Policy>`) is instantiated into four dedicated IN_RAM
// core-1 entry points:
//   - hamster_flat_foreground       (64 KB flat RAM + IOReaders/IOWriters)
//   - hamster_gime_foreground       (8 KB task MMU + $FE00 trampoline + IOReaders/IOWriters)
//   - hamster_gep9_foreground       (256-byte paged Gep9MMU + TaskFuse + blessed/embiggen I/O)
//   - hamster_multicomp_foreground  (8 KB banked RAM + $FFD0..$FFDF Multicomp I/O + optional IOReaders)
//
// Core 1 runs strictly out of SRAM with zero calls into XIP flash (`scripts/check-core1-sram.sh -s`).

#define FG2BG_HIGH_WATERMARK 50  // Throttle when fg2bg event FIFO exceeds this

std::atomic<bool> bg_wants_halt;
std::atomic<bool> foreground_running{false};  // Set by core 1 when inner loop starts

// LED is push-pull: Positive Logic (1 == ON)
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

FORCE_INLINE void IN_RAM ChangeInterruptPin(bool needed) {
  // Open-drain: assert by setting dir to output, release by input.
  if (needed) {
    gpio_set_dir(IRQ, GPIO_OUT);
  } else {
    gpio_set_dir(IRQ, GPIO_IN);
  }
}

// Pure-SRAM >= 1 us delay using RP2350's side-effect-free raw microsecond counter (TIMERAWL).
// Replaces SDK `sleep_us(1)`, which lives in flash and touches alarm pools.
FORCE_INLINE void IN_RAM sram_delay_1us() {
  uint32_t t0 = timer_hw->timerawl;
  while ((uint32_t)(timer_hw->timerawl - t0) < 2u) {
    tight_loop_contents();
  }
}

// ═══════════════════════════════════════════════════════════════════
// Four Hamster Bus Policies
// ═══════════════════════════════════════════════════════════════════

// 1. hamster_flat: unbanked 64 KB RAM ($0000..$FEFF) + dispatch tables ($FF00..$FFFF).
//    Serves `turbo9os for turbo9sim` and `nitros9l1 for flat65280`.
struct HamsterFlatPolicy {
  static FORCE_INLINE bool IrqNeeded() {
    return turbo9sim_irq_needed() || flat65280_irq_needed();
  }
  static FORCE_INLINE byte Read(uint addr) {
    if (likely(addr < 0xFF00)) {
      return ram[addr];
    }
    IOReader fn = IOReaders[addr & 0xFF];
    return fn ? fn(addr) : 0;
  }
  static FORCE_INLINE void Write(uint addr, byte value) {
    if (likely(addr < 0xFF00)) {
      ram[addr] = value;
      return;
    }
    IOWriter fn = IOWriters[addr & 0xFF];
    if (fn) fn(addr, value);
  }
};

// 2. hamster_gime: 8 KB task MMU ($0000..$FDFF) + fixed $FE00..$FEFF trampoline + dispatch tables ($FF00..$FFFF).
//    Serves `nitros9l2 for deep65280` (and future `jamesbrown` / `coco3` targets on TFR/911H).
struct HamsterGimePolicy {
  static FORCE_INLINE bool IrqNeeded() {
    return flat65280_irq_needed();
  }
  static FORCE_INLINE byte Read(uint addr) {
    if (likely(addr < 0xFE00)) {
      return ram[task_mmu_base[addr >> 13] | (addr & 0x1FFF)];
    }
    if (addr < 0xFF00) {
#if DEEP_256K
      return ram[0x3E000 | (addr & 0x1FFF)];
#else
      return ram[0x1E000 | (addr & 0x1FFF)];
#endif
    }
    IOReader fn = IOReaders[addr & 0xFF];
    return fn ? fn(addr) : 0;
  }
  static FORCE_INLINE void Write(uint addr, byte value) {
    if (likely(addr < 0xFE00)) {
      ram[task_mmu_base[addr >> 13] | (addr & 0x1FFF)] = value;
      return;
    }
    if (addr < 0xFF00) {
#if DEEP_256K
      ram[0x3E000 | (addr & 0x1FFF)] = value;
#else
      ram[0x1E000 | (addr & 0x1FFF)] = value;
#endif
      return;
    }
    IOWriter fn = IOWriters[addr & 0xFF];
    if (fn) fn(addr, value);
  }
};

// 3. hamster_gep9: 256-byte paged Gep9MMU + RTI TaskFuse + capability-gated I/O + EMBIGGEN window.
//    Serves `hatvan` for `gep9`, `gep9big`, and `coco2`.
struct HamsterGep9Policy {
  static FORCE_INLINE bool IrqNeeded() {
    return gep9_irq_needed();
  }
  static FORCE_INLINE byte Read(uint addr) {
    // TaskFuse transition: armed fuse fires on the cycle after RTI opcode fetch.
    if (unlikely(gep9_mmu.task_fuse_step == 1)) {
      gep9_mmu.current_task = gep9_mmu.task_fuse_target;
      gep9_mmu.task_fuse_armed = false;
      gep9_mmu.task_fuse_step = 0;
    }

    byte value;
    if (unlikely(addr >= 0xFF00)) {
      if (addr >= 0xFFF0) {
        // Vector fetch: instant transition to Task 0 (Kernel).
        // Real vector fetches begin on even addresses $FFF0..$FFFE.
        // 0xFFFF is an internal idle/dead cycle and must not switch tasks!
        if (addr != 0xFFFF && (addr & 1) == 0) {
          gep9_mmu.current_task = 0;
        }
        value = *gep9_mmu.TranslateRead(0, addr);
      } else if (gep9_mmu.current_task == 0 ||
                 (gep9_mmu.task_flags[gep9_mmu.current_task] & 0x01) != 0 ||
                 ((uint16_t)(addr - gep9_embiggen_base) < 8)) {
        IOReader fn = IOReaders[addr & 0xFF];
        value = fn ? fn(addr) : 0;
      } else {
        value = 0;  // Unblessed user I/O access
      }
    } else {
      value = *gep9_mmu.TranslateRead(gep9_mmu.current_task, addr);
    }

    if (unlikely(gep9_mmu.task_fuse_armed && gep9_mmu.task_fuse_step == 0)) {
      if (value == 0x3B) {  // RTI opcode fetched
        gep9_mmu.task_fuse_step = 1;
      }
    }
    return value;
  }
  static FORCE_INLINE void Write(uint addr, byte value) {
    if (unlikely(addr >= 0xFF00)) {
      if (addr >= 0xFFF0) {
        *gep9_mmu.TranslateWrite(0, addr) = value;
      } else if (gep9_mmu.current_task == 0 ||
                 (gep9_mmu.task_flags[gep9_mmu.current_task] & 0x01) != 0 ||
                 ((uint16_t)(addr - gep9_embiggen_base) < 8)) {
        IOWriter fn = IOWriters[addr & 0xFF];
        if (fn) fn(addr, value);
      }
    } else {
      *gep9_mmu.TranslateWrite(gep9_mmu.current_task, addr) = value;
    }
  }
};

// 4. hamster_multicomp: eight 8 KB banked slots + $FFD0..$FFDF Multicomp I/O + optional enhancement ports.
//    Serves `fuzix for multicomp09` ($FFF0..$FFFF vectors live in banked slot 7 RAM).
struct HamsterMulticompPolicy {
  static FORCE_INLINE bool IrqNeeded() {
    return mc_irq_needed();
  }
  static FORCE_INLINE byte Read(uint addr) {
    if (likely(addr < 0xFF00)) {
      return ram[mc_slot_base[addr >> 13] | (addr & 0x1FFF)];
    }
    if ((addr & 0xFFF0) == 0xFFD0) {
      return mc_read_io((uint16_t)addr);
    }
    IOReader fn = IOReaders[addr & 0xFF];
    if (fn) return fn(addr);
    return ram[mc_slot_base[7] | (addr & 0x1FFF)];
  }
  static FORCE_INLINE void Write(uint addr, byte value) {
    if (likely(addr < 0xFF00)) {
      ram[mc_slot_base[addr >> 13] | (addr & 0x1FFF)] = value;
      return;
    }
    if ((addr & 0xFFF0) == 0xFFD0) {
      mc_write_io((uint16_t)addr, value);
      return;
    }
    IOWriter fn = IOWriters[addr & 0xFF];
    if (fn) {
      fn(addr, value);
      return;
    }
    ram[mc_slot_base[7] | (addr & 0x1FFF)] = value;
  }
};

// ═══════════════════════════════════════════════════════════════════
// Shared Hamster PIO Bus Loop Template
// ═══════════════════════════════════════════════════════════════════

template <class Policy>
FORCE_INLINE void HamsterForegroundLoop() {
  volatile sio_hw_t* hw = (volatile sio_hw_t*)sio_hw;

#if SPEED_STATS
  cycles = 0;
#endif

#if DUMP_FIRST_CYCLES
  struct CycleDump { uint16_t addr; uint8_t value; uint8_t rw; };
  static CycleDump dump_buf[DUMP_FIRST_CYCLES];
  int dump_count = 0;
  bool dump_triggered = false;  // Start recording on first addr==0xFFFE
#endif

#if !JUST_WAIT_DONT_HALT
  bool actually_halted = false;
#endif

  // Signal background that we've reached the bus loop before it releases RESET/HALT.
  foreground_running.store(true, std::memory_order_release);

  uint prev_late_pins = 0;  // memory for delaying LIC one cycle => FIC.

  // OUTER LOOP — foreground only handles IRQ pin + PIO bus cycles.
  // USB I/O, disk VFS RPC, and terminal RX run on core 0.
  while (true) {
    irq_needed = Policy::IrqNeeded();
    if (irq_needed != prev_irq_needed) {
      ChangeInterruptPin(irq_needed);
      prev_irq_needed = irq_needed;
    }

#if JUST_WAIT_DONT_HALT
    while (true) {
      bool halt_wanted = bg_wants_halt.load(std::memory_order_relaxed)
        || (fg2bg.size() > FG2BG_HIGH_WATERMARK);
      if (!halt_wanted) break;
      LedOn();
      sram_delay_1us();
    }
    LedOff();
#else
    {
      bool halt_wanted = bg_wants_halt.load(std::memory_order_relaxed);
#if OCCASIONAL_HALTING
      halt_wanted |= (cycles % 99) == 50;
#endif
      if (halt_wanted && !actually_halted) {
        HaltOn();
        actually_halted = true;
        sram_delay_1us();
      } else if (!halt_wanted && actually_halted) {
        HaltOff();
        actually_halted = false;
        sram_delay_1us();
      }
      sram_delay_1us();
    }
#endif

#if SPEED_STATS
    int idle_in_group = 0;
#endif

    // INNER LOOP
    for (int i = 0; i < GROUP_SIZE; i++) {
      pio_sm_put(pio0, 0, 0);  // put sync word

      uint early_pins = pio_sm_get_blocking(pio0, 0);  // get early pins
      uint addr = 0xFFFF & hw->gpio_hi_in;

#if SPEED_STATS
      if (addr == 0xFFFF) idle_in_group++;
#endif

      const bool reading = 0 != (early_pins & (1 << R_W));
      byte value = 0;
      uint late_pins = 0;

      if (likely(reading)) {
        value = Policy::Read(addr);
        pio_sm_put(pio0, 0, value);
        late_pins = pio_sm_get_blocking(pio0, 0);  // LATE PINS
      } else {
        late_pins = pio_sm_get_blocking(pio0, 0);
        value = (byte)late_pins;
        Policy::Write(addr, value);
      }

#if TRACE
      // Trace push: AFTER the PIO handshake is complete so the 6309 has already latched data.
      bool is_fic = ((prev_late_pins & (1 << LIC)) != 0);
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
        dump_triggered = true;
      }
      if (dump_triggered && dump_count < DUMP_FIRST_CYCLES) {
        dump_buf[dump_count] = { (uint16_t)addr, value, (uint8_t)(reading ? 'r' : 'W') };
        dump_count++;
        if (dump_count == DUMP_FIRST_CYCLES) {
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
    }  // END INNER LOOP

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
  }  // END OUTER LOOP
}

// ═══════════════════════════════════════════════════════════════════
// Dedicated IN_RAM Core-1 Entry Points & Lookup Table
// ═══════════════════════════════════════════════════════════════════

void IN_RAM hamster_flat_foreground()      { HamsterForegroundLoop<HamsterFlatPolicy>(); }
void IN_RAM hamster_gime_foreground()      { HamsterForegroundLoop<HamsterGimePolicy>(); }
void IN_RAM hamster_gep9_foreground()      { HamsterForegroundLoop<HamsterGep9Policy>(); }
void IN_RAM hamster_multicomp_foreground() { HamsterForegroundLoop<HamsterMulticompPolicy>(); }

using ForegroundFn = void (*)();

struct HamsterEngineEntry {
  const char* name;
  ForegroundFn foreground;
};

static const HamsterEngineEntry kHamsterEngines[] = {
  {"hamster_flat",      hamster_flat_foreground},
  {"hamster_gime",      hamster_gime_foreground},
  {"hamster_gep9",      hamster_gep9_foreground},
  {"hamster_multicomp", hamster_multicomp_foreground},
};

static inline ForegroundFn FindHamsterEngine(const char* name) {
  if (!name) return nullptr;
  for (const HamsterEngineEntry& e : kHamsterEngines) {
    if (!strcmp(name, e.name)) return e.foreground;
  }
  return nullptr;
}
