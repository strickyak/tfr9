#ifndef _TURBO9SIM_H_
#define _TURBO9SIM_H_

#define PRIMARY_TURBO9SIM 0xE0
#define SECONDARY_TURBO9SIM 0x00

struct NoTurbo9sim {
  force_inline void Install(uint base, IOReader readers[], IOWriter writers[]) {
  }
  force_inline bool IrqNeeded() { return false; }
  force_inline void SetTimerFired() {}
  force_inline bool CanRx() { return false; }
  force_inline void SetRx(byte ch) {}
};

struct Turbo9sim {
  bool sim_timer_irq;
  bool sim_rx_ready_irq;
  byte sim_status_reg;
  byte sim_control_reg;
  byte sim_last_char_rx;
  byte sim_last_char_tx;

  constexpr static byte SIM_TIMER_BIT = 0x01;
  constexpr static byte SIM_RX_BIT = 0x02;

  force_inline bool IrqNeeded() {
    bool z = sim_status_reg & sim_control_reg;
    printf("yak IrqNeeded? s=%02x c=%02x %x\n", sim_status_reg, sim_control_reg,
           z);
    return z;
  }

  force_inline void SetTimerFired() {
    sim_status_reg |= SIM_TIMER_BIT;
    printf("yak timer fired: s=%02x\n", sim_status_reg);
  }

  force_inline bool CanRx() {
    bool z = !(sim_status_reg & SIM_RX_BIT);
    printf("yak CanRx? %x\n", z);
    return z;
  }
  force_inline void SetRx(byte ch) {
    sim_status_reg |= SIM_RX_BIT;
    sim_last_char_rx = ch;
    printf("yak SetRx(%02x) s=%02x\n", ch, sim_status_reg);
  }

  void Install(uint base, IOReader readers[], IOWriter writers[]) {
    // four readers:
    readers[base + 0] = [this](uint addr, byte data) {
      byte z = this->simTxReader(addr, data);
      printf("yak read 0 => %02x\n", z);
      return z;
    };
    readers[base + 1] = [this](uint addr, byte data) {
      byte z = this->simRxReader(addr, data);
      printf("yak read 1 => %02x\n", z);
      return z;
    };
    readers[base + 2] = [this](uint addr, byte data) {
      byte z = this->simStatusReader(addr, data);
      printf("yak read 2 => %02x\n", z);
      return z;
    };
    readers[base + 3] = [this](uint addr, byte data) {
      byte z = this->simControlReader(addr, data);
      printf("yak read 3 => %02x\n", z);
      return z;
    };

    // four writers:
    writers[base + 0] = [this](uint addr, byte data) {
      printf("yak write 0 <= %02x\n", data);
      this->simTxWriter(addr, data);
    };
    writers[base + 1] = [this](uint addr, byte data) {
      printf("yak write 1 <= %02x\n", data);
      this->simRxWriter(addr, data);
    };
    writers[base + 2] = [this](uint addr, byte data) {
      printf("yak write 2 <= %02x\n", data);
      this->simStatusWriter(addr, data);
    };
    writers[base + 3] = [this](uint addr, byte data) {
      printf("yak write 3 <= %02x\n", data);
      this->simControlWriter(addr, data);
    };
  }

  byte simTxReader(uint addr, byte data) { return sim_last_char_tx; }
  byte simRxReader(uint addr, byte data) {
    sim_status_reg &= ~SIM_RX_BIT;  // Consume the char, if any.
    return sim_last_char_rx;
  }
  byte simStatusReader(uint addr, byte data) { return sim_status_reg; }
  byte simControlReader(uint addr, byte data) { return sim_control_reg; }

  void simTxWriter(uint addr, byte data) {
    this->sim_last_char_tx = data;

    // ShowChar('-');
    ShowChar(data);
  }

  void simRxWriter(uint addr, byte data) {
    // no effect
  }

  void simStatusWriter(uint addr, byte data) {
    if (data & SIM_TIMER_BIT) {  // Clear Timer Interrupt
      this->sim_timer_irq = false;
      this->sim_status_reg &= ~SIM_TIMER_BIT;
    }

    if (data & SIM_RX_BIT) {  // Clear Rx Interrupt
      this->sim_rx_ready_irq = false;
      this->sim_status_reg &= ~SIM_RX_BIT;
    }
  }

  void simControlWriter(uint addr, byte data) { this->sim_control_reg = data; }
};

#endif  // _TURBO9SIM_H_
