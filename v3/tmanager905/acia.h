#ifndef _ACIA_H_
#define _ACIA_H_

// Motorola 6850 Asynchronous Commuication Interface Adapter (UART)

template <typename T>
struct DontAcia {
  constexpr static bool DoesAcia() { return false; }
};
template <typename T>
struct DoAcia {
  constexpr static bool DoesAcia() { return true; }

  static void Acia_Install(uint port) {
    T::Logf(LHello, "bilbo $%x install", port);
    uint sub = 255 & port;
    // Readers

    IOReaders[sub + 0] = [](uint addr, byte data) {
        // MC6850 Status Read
      data = 0x02;  // Transmit buffer always considered empty.
      data |= (acia_irq_firing) ? 0x80 : 0x00;
      data |= (acia_char_in_ready) ? 0x01 : 0x00;

      acia_irq_firing = false;  // Side effect of reading status.
      return data;
    };
    IOReaders[sub + 1] = [](uint addr, byte data) {
        // MC6850 Data Read
      if (acia_char_in_ready) {
        data = acia_char;
        acia_char_in_ready = false;
      } else {
        data = 0;
      }
      return data;
    };

    // Writers

    IOWriters[sub + 0] = [](uint addr, byte data) {
        // MC6850 Command Write
#if 0
    ----- this is having no effect, because we set acia_irq_enabled again
        in the next clause.  so why was this here? -----
      if ((data & 0x03) != 0) {
        acia_irq_enabled = false;
      }
#endif
      if ((data & 0x80) != 0) {
        acia_irq_enabled = true;
      } else {
        acia_irq_enabled = false;
      }
    };

    IOWriters[sub + 1] = [](uint addr, byte data) {
        // MC6850 Data Write
      T::Logf(LHello, "bilbo %d putchar", data);
      if (data == 0 || data >= 128) {
        putbyte(C_PUTCHAR);
      }  // otherwise, normal 7-bit chars don't need the prefix.
      putbyte(data);
    };
  }
};

#endif  // _ACIA_H_
