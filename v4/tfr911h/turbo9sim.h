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

  void static Turbo9sim_Install(uint base) {
    base &= 0xFF;
    ShowChar('I');
    // four readers:
    IOReaders[base + 0] = [](uint addr) {
      byte z = simTxReader(addr);
      Printf("turbo read 0 => %02x\n", z);
      return z;
    };
    IOReaders[base + 1] = [](uint addr) {
      byte z = simRxReader(addr);
      Printf("turbo read 1 => %02x\n", z);
#define TSIM_ECHO_RX 0
#if TSIM_ECHO_RX
      ShowChar(z);
#endif
      return z;
    };
    IOReaders[base + 2] = [](uint addr) {
      byte z = simStatusReader(addr);
      Printf("turbo read 2 => %02x\n", z);
      return z;
    };
    IOReaders[base + 3] = [](uint addr) {
      byte z = simControlReader(addr);
      Printf("turbo read 3 => %02x\n", z);
      return z;
    };

    // four writers:
    IOWriters[base + 0] = [](uint addr, byte data) {
      Printf("turbo write 0 <= %02x\n", data);
      simTxWriter(addr, data);
    };
    IOWriters[base + 1] = [](uint addr, byte data) {
      Printf("turbo write 1 <= %02x\n", data);
      simRxWriter(addr, data);
    };
    IOWriters[base + 2] = [](uint addr, byte data) {
      Printf("turbo write 2 <= %02x\n", data);
      simStatusWriter(addr, data);
    };
    IOWriters[base + 3] = [](uint addr, byte data) {
      Printf("turbo write 3 <= %02x\n", data);
      simControlWriter(addr, data);
    };
  }

  byte static simTxReader(uint addr) { return sim_last_char_tx; }
  byte static simRxReader(uint addr) {
    sim_status_reg &= ~SIM_RX_BIT;  // Consume the char, if any.
#define PREFER_LINEFEED 0
#if PREFER_LINEFEED
    if (sim_last_char_rx == 13) return 10;
#endif
    return sim_last_char_rx;
  }
  byte static simStatusReader(uint addr) { return sim_status_reg; }
  byte static simControlReader(uint addr) { return sim_control_reg; }

  void static simTxWriter(uint addr, byte data) {
    sim_last_char_tx = data;

    // ShowChar('(');
    ShowChar(data);
    // ShowChar(')');
  }

  void static simRxWriter(uint addr, byte data) {
    // no effect
  }

  void static simStatusWriter(uint addr, byte data) {
    if (data & SIM_TIMER_BIT) {  // Clear Timer Interrupt
      sim_timer_irq = false;
      sim_status_reg &= ~SIM_TIMER_BIT;
    }

    if (data & SIM_RX_BIT) {  // Clear Rx Interrupt
      sim_rx_ready_irq = false;
      sim_status_reg &= ~SIM_RX_BIT;
    }
  }

  void static simControlWriter(uint addr, byte data) { sim_control_reg = data; }
};

#undef Printf

#endif  // _TURBO9SIM_H_
