#pragma once
// orchestra90.h -- CoCo Orchestra-90 stereo 8-bit DAC (GPIO 10 = Left, GPIO 11 = Right, PWM slice 5).
//
// Default address in enhancement map `e1`: 0xFF7A (Left), 0xFF7B (Right).
// PWM hardware is initialized lazily on first Install().  Core-1 writes update SRAM shadow bytes
// and write straight to RP2350 PWM CC registers (`pwm_set_chan_level` is static inline MMIO).

#include <hardware/pwm.h>

static volatile byte g_orch90_left = 0;
static volatile byte g_orch90_right = 0;
static bool g_orch90_hw_inited = false;

byte IN_RAM orch90_read_left(uint) { return g_orch90_left; }
void IN_RAM orch90_write_left(uint, byte d) {
  g_orch90_left = d;
  pwm_set_chan_level(5, PWM_CHAN_A, d);
}

byte IN_RAM orch90_read_right(uint) { return g_orch90_right; }
void IN_RAM orch90_write_right(uint, byte d) {
  g_orch90_right = d;
  pwm_set_chan_level(5, PWM_CHAN_B, d);
}

struct Orchestra90 {
  static constexpr uint16_t kDefaultBase = 0xFF7A;
  static void Install(uint16_t base = kDefaultBase) {
    if (!g_orch90_hw_inited) {
      g_orch90_hw_inited = true;
      gpio_set_function(10, GPIO_FUNC_PWM);
      gpio_set_function(11, GPIO_FUNC_PWM);
      uint slice = pwm_gpio_to_slice_num(10);
      pwm_set_wrap(slice, 255);
      pwm_set_chan_level(slice, PWM_CHAN_A, 0);
      pwm_set_chan_level(slice, PWM_CHAN_B, 0);
      pwm_set_enabled(slice, true);
    }
    g_orch90_left = 0;
    g_orch90_right = 0;
    IoClaim(base, base + 1, "orchestra90");
    IoSet(base + 0, orch90_read_left,  orch90_write_left);
    IoSet(base + 1, orch90_read_right, orch90_write_right);
  }
};
