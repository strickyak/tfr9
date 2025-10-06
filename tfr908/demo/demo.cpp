#include <hardware/clocks.h>
// #include <hardware/dma.h>
// #include <hardware/pio.h>
// #include <hardware/structs/systick.h>
// #include <hardware/timer.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <setjmp.h>
#include <stdio.h>

#define G_RW 31
#define G_E  30
#define G_Q  29

#define G_D0 0
#define G_A0 32

#define G_RESET 28
#define G_NMI 27
#define G_LIC 26
#define G_AVMA 22
#define G_IRQ 21
#define G_FIRQ 20
#define G_HALT 11

#define G_LED 25
#define SET_LED(X) gpio_put(G_LED, (X))

#define DELAY sleep_us(1)

void InitInPin(int pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
}
void OutPin(int pin, bool value) {
  gpio_init(pin);  // GPIO needs to own the output pin.
  gpio_put(pin, value);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, value);
}

void InitializePins() {
    for (uint i = 0; i <= 22; i++) {
        InitInPin(i);
    }
    for (uint i = 26; i <= 47; i++) {
        InitInPin(i);
    }
    OutPin(G_E, false);
    OutPin(G_Q, false);
    OutPin(G_RESET, true);
    OutPin(G_HALT, true);
    OutPin(G_NMI, true);
    OutPin(G_IRQ, true);
    OutPin(G_FIRQ, true);
    OutPin(G_LED, false);
}

void Reset() {
    OutPin(G_HALT, 0);
    OutPin(G_RESET, 0);
    DELAY;
    for (uint i = 0; i < 100000; i++) {
        OutPin(G_Q, 1); DELAY;
        OutPin(G_E, 1); DELAY;
        OutPin(G_Q, 0); DELAY;
        OutPin(G_E, 0); DELAY;
    }
    OutPin(G_HALT, 0);
    OutPin(G_RESET, 1);
    DELAY;
    for (uint i = 0; i < 100000; i++) {
        OutPin(G_Q, 1); DELAY;
        OutPin(G_E, 1); DELAY;
        OutPin(G_Q, 0); DELAY;
        OutPin(G_E, 0); DELAY;
    }
    OutPin(G_HALT, 1);
    OutPin(G_RESET, 1);
    DELAY;
}

void Step(uint cycle) {
        OutPin(G_Q, 1); DELAY;
        OutPin(G_E, 1); DELAY;

        uint64_t pins = gpio_get_all64();
        uint addr = (pins >> 32) & 0xFFFFu;
#if 0
        bool write = false;
#else
        bool write = (pins & (1lu << G_RW)) == 0;
        //uint data = pins & 0xFFu;
#endif

        if (!write) {
            for (uint j=0; j<8; j++) {
                OutPin(j, (j==5) ? 1 : 0);
            }
        }

        OutPin(G_Q, 0); DELAY;

#if 0
        printf("%04d: %08x %02x @ %04x\n", cycle, 0xFFFFFFFFu & (uint)pins, 0xFFu & (uint)gpio_get_all64(), addr);
#else
        printf("%04d: %c %08x %02x @ %04x\n", cycle, (write?'W':'r'), 0xFFFFFFFFu & (uint)pins, 0xFFu & (uint)gpio_get_all64(), addr);
#endif

        OutPin(G_E, 0); DELAY;
        if (!write) {
            for (uint j=0; j<8; j++) {
                InitInPin(j);
            }
        }
}

int main() {
  stdio_usb_init();
  // LED
  gpio_init(G_LED);
  gpio_set_dir(G_LED, GPIO_OUT);
  SET_LED(0);

  for (uint i = 5; i>0; i--) {
    printf("*** This is TFR/908 Demo: %d\n", i);
    sleep_ms(500);
    SET_LED(1);
    sleep_ms(500);
    SET_LED(0);
  }

  InitializePins();
  for (uint cycle = 0; cycle < 100000000; cycle++) {
    Step(cycle);
  }
  while (1) SET_LED(1);
}

/*
*** This is TFR/908 Demo: 4
*** This is TFR/908 Demo: 3
*** This is TFR/908 Demo: 2
*** This is TFR/908 Demo: 1
0000: W 78300b00 00 @ 0000
0001: W 78700800 00 @ 0000
0002: r f8700800 20 @ ffff
0003: r f8700e00 20 @ fffe
0004: r f8300a00 20 @ ffff
0005: r f8700800 20 @ ffff
0006: r f8700800 20 @ 2020
0007: r f8300800 20 @ 2021
0008: r f8700800 20 @ ffff
0009: r f8700800 20 @ 2042
0010: r f8300800 20 @ 2043
0011: r f8700800 20 @ ffff
0012: r f8700800 20 @ 2064
0013: r f8300800 20 @ 2065
0014: r f8700800 20 @ ffff
0015: r f8700800 20 @ 2086
0016: r f8300800 20 @ 2087
0017: r f8700800 20 @ ffff
0018: r f8700800 20 @ 20a8
0019: r f8300800 20 @ 20a9
0020: r f8700800 20 @ ffff
0021: r f8700800 20 @ 20ca
0022: r f8300800 20 @ 20cb
0023: r f8700800 20 @ ffff
*/
