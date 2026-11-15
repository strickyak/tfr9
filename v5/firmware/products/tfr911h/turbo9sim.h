#ifndef _TURBO9SIM_H_
#define _TURBO9SIM_H_

#define Printf if(false)printf

bool sim_timer_irq;
bool sim_rx_ready_irq;
byte sim_status_reg;
byte sim_control_reg;
byte sim_last_char_rx;
byte sim_last_char_tx;

template <typename T>
struct DontTurbo9sim {
  constexpr static bool Does_Turbo9sim() { return false; }
  constexpr static bool Turbo9sim_IrqNeeded() { return false; }
  force_inline static void Turbo9sim_SetTimerFired() {}
  constexpr static bool Turbo9sim_CanRx() { return false; }
  force_inline static void Turbo9sim_SetRx(byte ch) {}
};

template <typename T>
struct DoTurbo9sim {
  constexpr static byte SIM_TIMER_BIT = 0x01;
  constexpr static byte SIM_RX_BIT = 0x02;
  constexpr static bool Does_Turbo9sim() { return true; }

  force_inline static bool Turbo9sim_IrqNeeded() {
    bool z = sim_status_reg & sim_control_reg;
    Printf("XXX sim IrqNeeded? s=%02x c=%02x %x\n", sim_status_reg,
           sim_control_reg, z);
    return z;
  }

  force_inline static void Turbo9sim_SetTimerFired() {
    sim_status_reg |= SIM_TIMER_BIT;
    Printf("XXX sim timer fired: s=%02x\n", sim_status_reg);
  }

  force_inline static bool Turbo9sim_CanRx() {
    bool z = !(sim_status_reg & SIM_RX_BIT);
    Printf("XXX sim CanRx? %x\n", z);
    return z;
  }
  force_inline static void Turbo9sim_SetRx(byte ch) {
    sim_status_reg |= SIM_RX_BIT;
    sim_last_char_rx = ch;
    Printf("XXX sim SetRx(%02x) s=%02x\n", ch, sim_status_reg);
  }

  // (Turbo9sim_Install, which installed this ACIA's handlers as lambdas, is gone: lambdas cannot be IN_RAM.
  //  The handlers are now the named IN_RAM functions in lib/devices/acia_tfr.h, installed by Turbo9SimAcia.)
};

#undef Printf

#endif  // _TURBO9SIM_H_
