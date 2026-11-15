#pragma once
// guts.h -- Guts<T>: the bus-cycle engine (core 1 foreground).
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

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
    if (current_engine == ENGINE_FLAT65280 || current_engine == ENGINE_DEEP65280 || current_engine == ENGINE_GEP9 || current_engine == ENGINE_GEP9BIG || current_engine == ENGINE_MULTICOMP09) {
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
      if (current_engine == ENGINE_MULTICOMP09) {
        irq_needed = mc_irq_needed();
      } else if (current_engine == ENGINE_GEP9 || current_engine == ENGINE_GEP9BIG) {
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
          if (current_engine == ENGINE_MULTICOMP09) {
            if (unlikely(addr >= 0xFFD0 && addr <= 0xFFDF)) {
              value = mc_read_io(addr);
            } else {
              value = ram[mc_slot_base[addr >> 13] | (addr & 0x1FFF)];
            }
          } else if (current_engine == ENGINE_GEP9 || current_engine == ENGINE_GEP9BIG) {
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
                if (gep9_mmu.current_task == 0 || (gep9_mmu.task_flags[gep9_mmu.current_task] & 0x01) != 0 || ((uint16_t)(addr - gep9_embiggen_base) < 8)) {
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

          if (current_engine == ENGINE_MULTICOMP09) {
            if (unlikely(addr >= 0xFFD0 && addr <= 0xFFDF)) {
              mc_write_io(addr, value);
            } else {
              ram[mc_slot_base[addr >> 13] | (addr & 0x1FFF)] = value;
            }
          } else if (current_engine == ENGINE_GEP9 || current_engine == ENGINE_GEP9BIG) {
            if (unlikely(addr >= 0xFF00)) {
              if (addr >= 0xFFF0) {
                *gep9_mmu.TranslateWrite(0, addr) = value;
              } else {
                if (gep9_mmu.current_task == 0 || (gep9_mmu.task_flags[gep9_mmu.current_task] & 0x01) != 0 || ((uint16_t)(addr - gep9_embiggen_base) < 8)) {
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

