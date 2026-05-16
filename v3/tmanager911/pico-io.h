#ifndef _PICO_IO_H_
#define _PICO_IO_H_

bool non_organic_led;
constexpr uint LED_PIN = 25;

enum {
  LED_PORT = 4,
  RAND_PORT = 5,
  GPIO_DIRECTION12_PORT = 6,
  GPIO_DATA12_PORT = 7,
};

template <typename T>
struct DontPicoIO {
  constexpr static bool DoesPicoIO() { return false; }

  static void OrganicLED(bool on) {}
  static void SetLED(bool on) {}
  static void PicoIO_Install(uint base) {}
};

template <typename T>
struct DoPicoIO {
  constexpr static bool DoesPicoIO() { return true; }

  static void OrganicLED(bool value) {
    if (!non_organic_led) {
      gpio_put(LED_PIN, value);
    }
  }

  static void SetLED(bool value) {
    non_organic_led = true;  // Stop organic usage
    gpio_put(LED_PIN, value);
  }

  static void PicoIO_Install(uint base) {
    base &= 0xFF;

    // Initialize the LED organically off.
    non_organic_led = false;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, false);

    ShowChar('p');

    IOReaders[base + RAND_PORT] = [](uint addr, byte data) {
      uint32_t r = get_rand_32();
      return static_cast<byte>(r);
    };

    IOWriters[base + LED_PORT] = [](uint addr, byte data) {
      bool value = (data & 1) != 0;  // Use lowest bit for value.
      SetLED(value);
    };

    // GPIO[12:19] set direction
    IOWriters[base + GPIO_DIRECTION12_PORT] = [](uint addr, byte data) {
      constexpr uint shift = 12;  // Starting GPIO pin

      uint b =
          1;  // choose one bit at a time, starting from Least Significant Bit.
      for (int i = 12; i < 20; i++) {
        gpio_init(i);
        gpio_pull_up(i);
        gpio_set_dir(i, (data & b) != 0);
        b <<= 1;
      }
    };

    // GPIO[12:19] set outputs
    IOWriters[base + GPIO_DATA12_PORT] = [](uint addr, byte data) {
      constexpr uint shift = 12;  // Starting GPIO pin
      uint shifted = static_cast<uint>(data) << shift;

      gpio_put_masked(255u << shift, shifted);
    };

    // GPIO[12:19] get inputs
    IOReaders[base + GPIO_DATA12_PORT] = [](uint addr, byte data) {
      constexpr uint shift = 12;  // Starting GPIO pin

      uint32_t all_bits = gpio_get_all();
      printf("gpio_get_all %x => %x", all_bits, (all_bits >> shift));
      return static_cast<byte>(all_bits >> shift);
    };
  }
};

#endif  // _PICO_IO_H_
