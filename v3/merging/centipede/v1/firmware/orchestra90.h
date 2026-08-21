#ifndef FIRMWARE_ORCHESTRA90_H_
#define FIRMWARE_ORCHESTRA90_H_

#include "hardware/pwm.h"

namespace orchestra90 {

void WriteLeft(uint a, byte d) { pwm_set_chan_level(5, PWM_CHAN_A, d); }

void WriteRight(uint a, byte d) { pwm_set_chan_level(5, PWM_CHAN_B, d); }

void initPorts() {
  IOWriters[0x7A] = WriteLeft;
  IOWriters[0x7B] = WriteRight;
}

void initHardware() {
  // Initialize the GPIOs for PWM function
  gpio_set_function(10, GPIO_FUNC_PWM);
  gpio_set_function(11, GPIO_FUNC_PWM);

  // Get the slice number (we know it's 5, but this is best practice)
  uint slice_num = pwm_gpio_to_slice_num(10);

  // Set the wrap value to 255 for perfect 8-bit scaling
  pwm_set_wrap(slice_num, 255);

  // Start with silence (0% duty cycle)
  pwm_set_chan_level(slice_num, PWM_CHAN_A, 0);  // GPIO 10
  pwm_set_chan_level(slice_num, PWM_CHAN_B, 0);  // GPIO 11

  // Turn on the PWM slice
  pwm_set_enabled(slice_num, true);
}

void Init() {
  initHardware();
  initPorts();
}
}  // namespace orchestra90

#endif  // FIRMWARE_ORCHESTRA90_H_
