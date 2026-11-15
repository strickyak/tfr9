#pragma once
// timers.h -- 60 Hz timer callback and flash-speed adjustment.
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// ═══════════════════════════════════════════════════════════════════
// 60Hz Timer for turbo9sim
// ═══════════════════════════════════════════════════════════════════
struct repeating_timer Timer60HzData;
volatile bool halt_suppress_timer = false;
bool IN_RAM Timer60HzCallback(repeating_timer_t* rt) {
  if (!halt_suppress_timer) {
    if (current_engine == ENGINE_MULTICOMP09) {
      if (mc_timer_en) {
        mc_timer_pending = true;
      }
    } else if (current_engine == ENGINE_GEP9 || current_engine == ENGINE_GEP9BIG) {
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

