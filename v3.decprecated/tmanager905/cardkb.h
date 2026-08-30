#ifndef _CARDKB_H_
#define _CARDKB_H_


// --- Hardware / I2C pins
#define CARDKB_I2C_PORT i2c1
#define PIN_SDA  18   // change as needed
#define PIN_SCL  19   // change as needed

// CardKBUnit I2C parameters
#define CARDKB_I2C_ADDR 0x5F  // 7-bit I2C address of the CardKBUnit (change to match your device)
#define CARDKB_I2C_BAUDRATE 100000  // 100 kHz // was 400 kHz

uint8_t i2c_raw[10];

byte CardKbRead() {
    memset(i2c_raw, 0, sizeof i2c_raw);

    // Really i2c_read_blocking does NOT block.
    // It returns 1 always.
    // If no char, i2c_raw[0] is 0.

    int read_r = i2c_read_blocking(CARDKB_I2C_PORT, CARDKB_I2C_ADDR, i2c_raw, /*size*/1, /*nostop=*/false);
    return (read_r == 1) ? i2c_raw[0] : 0;
}

void CardKbInit() {
    i2c_init(CARDKB_I2C_PORT, CARDKB_I2C_BAUDRATE);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    // pull-ups for I2C lines (external pull-ups are preferred, but enable internal as fallback)
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);
}

/////////////////////


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
    CardKbInit();

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
  }
};

#endif  // _CARDKB_H_
