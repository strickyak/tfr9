#pragma once
// pins.h -- TFR911H GPIO initialization.
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

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


