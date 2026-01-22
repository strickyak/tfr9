#ifndef _CARDKB_H_
#define _CARDKB_H_

// Unit CardKB v1.1 uses I2C1 on the Pi Pico,
// SDA (yellow) on GPIO pin 18
// SCL (white) on GPIO pin 19.
//
// However we are going to re-use the
// Motorola 6850 Asynchronous Commuication Interface Adapter (UART)
// driver for this device.

template <typename T>
struct DontCardKb {
  constexpr static bool DoesCardKb() { return false; }
};
template <typename T>
struct DoCardKb {
  constexpr static bool DoesCardKb() { return true; }

  static void CardKb_Install(uint port) {
    T::Logf(LHello, "cardkb $%x install", port);
    uint sub = 255 & port;
    // Readers

    IOReaders[sub + 0] = [](uint addr, byte data) {
        // CardKb d.b.a. MC6850 Status Read
      data = 0x02;  // Transmit buffer always considered empty.
      data |= (cardkb_irq_firing) ? 0x80 : 0x00;
      data |= (cardkb_char_in_ready) ? 0x01 : 0x00;

      cardkb_irq_firing = false;  // Side effect of reading status.
      return data;
    };
    IOReaders[sub + 1] = [](uint addr, byte data) {
        // CardKb d.b.a. MC6850 Data Read
      if (cardkb_char_in_ready) {
        data = cardkb_char;
        cardkb_char_in_ready = false;
      } else {
        data = 0;
      }
      return data;
    };

    // Writers

    IOWriters[sub + 0] = [](uint addr, byte data) {
        // CardKb d.b.a. MC6850 Command Write
      if ((data & 0x80) != 0) {
        cardkb_irq_enabled = true;
      } else {
        cardkb_irq_enabled = false;
      }
    };

    IOWriters[sub + 1] = [](uint addr, byte data) {
        // CardKb d.b.a. MC6850 Data Write
      T::Logf(LHello, "cardkb %d putchar", data);
      if (data == 0 || data >= 128) {
        putbyte(C_PUTCHAR);
      }  // otherwise, normal 7-bit chars don't need the prefix.
      putbyte(data);
    };
  }
};

#endif  // _CARDKB_H_
