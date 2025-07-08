#ifndef _TURBO9SIM_H_
#define _TURBO9SIM_H_

bool sim_timer_irq;
bool sim_rx_ready_irq;
byte sim_status_reg;
byte sim_control_reg;
byte sim_last_char_rx;
byte sim_last_char_tx;

template <class T>
struct DontTurbo9sim {
  constexpr static bool Turbo9sim_IrqNeeded() { return false; }
  force_inline static void Turbo9sim_SetTimerFired() {}
  constexpr static bool Turbo9sim_CanRx() { return false; }
  force_inline static void Turbo9sim_SetRx(byte ch) {}
};

template <class T>
struct DoTurbo9sim {
  constexpr static byte SIM_TIMER_BIT = 0x01;
  constexpr static byte SIM_RX_BIT = 0x02;

  force_inline static bool Turbo9sim_IrqNeeded() {
    bool z = sim_status_reg & sim_control_reg;
    printf("yak IrqNeeded? s=%02x c=%02x %x\n", sim_status_reg, sim_control_reg,
           z);
    return z;
  }

  force_inline static void Turbo9sim_SetTimerFired() {
    sim_status_reg |= SIM_TIMER_BIT;
    printf("yak timer fired: s=%02x\n", sim_status_reg);
  }

  force_inline static bool Turbo9sim_CanRx() {
    bool z = !(sim_status_reg & SIM_RX_BIT);
    printf("yak CanRx? %x\n", z);
    return z;
  }
  force_inline static void Turbo9sim_SetRx(byte ch) {
    sim_status_reg |= SIM_RX_BIT;
    sim_last_char_rx = ch;
    printf("yak SetRx(%02x) s=%02x\n", ch, sim_status_reg);
  }

  void static Turbo9sim_Install(uint base) {
    base &= 0xFF;
    ShowChar('1');
    // four readers:
    IOReaders[base + 0] = [](uint addr, byte data) {
      byte z = simTxReader(addr, data);
      printf("turbo read 0 => %02x\n", z);
      return z;
    };
    ShowChar('1');
    IOReaders[base + 1] = [](uint addr, byte data) {
      byte z = simRxReader(addr, data);
      printf("turbo read 1 => %02x\n", z);
      return z;
    };
    ShowChar('1');
    IOReaders[base + 2] = [](uint addr, byte data) {
      byte z = simStatusReader(addr, data);
      printf("turbo read 2 => %02x\n", z);
      return z;
    };
    ShowChar('1');
    IOReaders[base + 3] = [](uint addr, byte data) {
      byte z = simControlReader(addr, data);
      printf("turbo read 3 => %02x\n", z);
      return z;
    };

    ShowChar('2');
    // four writers:
    IOWriters[base + 0] = [](uint addr, byte data) {
      printf("turbo write 0 <= %02x\n", data);
      simTxWriter(addr, data);
    };
    ShowChar('2');
    IOWriters[base + 1] = [](uint addr, byte data) {
      printf("turbo write 1 <= %02x\n", data);
      simRxWriter(addr, data);
    };
    ShowChar('2');
    IOWriters[base + 2] = [](uint addr, byte data) {
      printf("turbo write 2 <= %02x\n", data);
      simStatusWriter(addr, data);
    };
    ShowChar('2');
    IOWriters[base + 3] = [](uint addr, byte data) {
      printf("turbo write 3 <= %02x\n", data);
      simControlWriter(addr, data);
    };
    ShowChar('3');
  }

  byte static simTxReader(uint addr, byte data) { return sim_last_char_tx; }
  byte static simRxReader(uint addr, byte data) {
    sim_status_reg &= ~SIM_RX_BIT;  // Consume the char, if any.
    return sim_last_char_rx;
  }
  byte static simStatusReader(uint addr, byte data) { return sim_status_reg; }
  byte static simControlReader(uint addr, byte data) { return sim_control_reg; }

  void static simTxWriter(uint addr, byte data) {
    sim_last_char_tx = data;

    // ShowChar('+');
    ShowChar(data);
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

#endif  // _TURBO9SIM_H_
