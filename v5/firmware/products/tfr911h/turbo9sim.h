#ifndef _TURBO9SIM_H_
#define _TURBO9SIM_H_

// Turbo9Sim console & 60 Hz timer register state ($FF00..$FF03 by default).
bool sim_timer_irq;
bool sim_rx_ready_irq;
byte sim_status_reg;
byte sim_control_reg;
byte sim_last_char_rx;
byte sim_last_char_tx;

constexpr byte SIM_TIMER_BIT = 0x01;
constexpr byte SIM_RX_BIT = 0x02;

FORCE_INLINE bool IN_RAM turbo9sim_irq_needed() {
  return (sim_status_reg & sim_control_reg) != 0;
}

void IN_RAM turbo9sim_tick_60hz() {
  sim_status_reg |= SIM_TIMER_BIT;
}

inline void turbo9sim_poll_rx() {
  if (!(sim_status_reg & SIM_RX_BIT) && term_input.NumBuffered() > 0) {
    sim_last_char_rx = term_input.Take();
    sim_status_reg |= SIM_RX_BIT;
  }
}

#endif  // _TURBO9SIM_H_
