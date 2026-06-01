#define MHz 150  // clock speed, 150 is "normal".

#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/structs/systick.h>
#include <hardware/timer.h>
// #include <hardware/i2c.h>
#include <pico/bootrom.h>
#include <pico/rand.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <pico/unique_id.h>
#include <stdio.h>

#include <cstring>
#include <functional>
#include <vector>

using byte = unsigned char;

constexpr uint RESET = 20;
constexpr uint NMI = 21;
constexpr uint IRQ = 22;
constexpr uint FIRQ = 23;
constexpr uint HALT = 24;
constexpr uint LED = 25;
constexpr uint LIC = 26;
constexpr uint AVMA = 27;
constexpr uint BS = 28;
constexpr uint E = 29;
constexpr uint Q = 30;
constexpr uint R_W = 31;

#include "t911veryfast.pio.h"

volatile uint delay_busy;
void Delay(uint n) {
    for (uint i = 0; i < n*10; i++) {
        delay_busy += i;
    }
}

uint InitializePinsReturnDirections() {
    uint directions = 0;
    for (int i = 0; i < 48; i++) {
        gpio_init(i);
        switch (i) {
            case RESET:
            case NMI:
            case IRQ:
            case FIRQ:
            case HALT:
            case E:
            case Q:
                gpio_set_dir(i, GPIO_OUT);
                gpio_put(i, 1);
                directions |= (1 << i);  // Mark an output bit.
                break;
            default:
                gpio_set_dir(i, GPIO_IN);
                gpio_pull_up(i);
                break;
        }
    }
    return directions;
}

void RunReset() {
    gpio_put(RESET, 0);
    for (int i = 0; i < 1000; i++) {
        Delay(10); // phase 1
        gpio_put(Q, 1);
        Delay(10); // phase 2
        gpio_put(E, 1);
        Delay(10); // phase 3
        gpio_put(Q, 0);
        Delay(10); // phase 4
        gpio_put(E, 0);
    }
    gpio_put(RESET, 1);
}

volatile uint vol;
constexpr uint NW = 50000;
uint memory[NW + 64];
uint memory_index = 0;

void RunCPU(uint unused_directions) {
    volatile sio_hw_t* hw = (volatile sio_hw_t*) sio_hw;

    while (true) {
        memory_index = 0;
        uint step = 0;
        uint value = 0;
        uint round = 0;
        uint16_t countdown = 0;

        while (memory_index < NW) {
//printf("<");
            uint pins = pio_sm_get_blocking(pio0, 0);
            uint addr = 0xFFFF & hw->gpio_hi_in;  // Read addr from high pins [32:47]

            // uint pins = hw->gpio_in;
//printf("%08x> ", pins);
            char avma = (0 != (pins & (1<<AVMA))) ? '#' : ' ';
            char lic = (0 != (pins & (1<<LIC))) ? '-' : ' ';
            char rw = (0 != (pins & (1<<R_W))) ? 'r' : 'W';

//printf("@%04x %c%c%c ", addr, avma, lic, rw);

            if (0 == (pins & (1<<R_W))) {
                value = pio_sm_get_blocking(pio0, 0);
                //printf(" W(%02x) ", 255 & value);
            }

            // pio_sm_put(pio0, 0, 0x39);  // $39=RTS
            pio_sm_put(pio0, 0, 0xBD);  // $BD=JSR
            // pio_sm_put(pio0, 0, 0x20);  // $20=BRA
            // pio_sm_put(pio0, 0, 0x21);  // $21=BRN
// printf("      ");

            uint pins2 = hw->gpio_in;
//printf("=%08x ", pins2);
//printf("\n");

            if (round > 10) switch (step) {
                case 0:
                        if (addr != 0xBDBD) printf("* s0 addr %x\n", addr);
                        break;
                case 1:
                        if (addr != 0xBDBE) printf("* s1 addr %x\n", addr);
                        break;
                case 2:
                        if (addr != 0xBDBF) printf("* s2 addr %x\n", addr);
                        break;
                case 3:
                        break;
                case 4:
                        break;
                case 5:
                        break;
                case 6:
                        if (rw != 'W') printf("* s6 not write\n");
                        if (countdown == 0) {
                            countdown = addr;
                        } else {
                            if (countdown != addr) printf("*s6 want %d got %d", countdown, addr);
                        }
                        countdown--;
                        break;
                case 7:
                        if (rw != 'W') printf("* s7 not write\n");
                        if (countdown == 0) {
                            countdown = addr;
                        } else {
                            if (countdown != addr) printf("*s7 want %d got %d", countdown, addr);
                        }
                        countdown--;
                        break;
                default:
                        printf("* default r%d s%d\n", round, step);
            }

            step++;
            if (0 != (pins & (1<<LIC))) {
                step = 0;
                round++;
                if ((round & 0x3FF) == 0) {
                    printf("==== ROUND %d\n", round);
                }
            }
            continue;

#if 0







            if ((pins & (1<<R_W)) != 0) {
printf("r");
                // Read Cycle -- we transmit a byte
                byte data = 0x39;  // RTS
                pio_sm_put(pio0, 0, data);
                memory[memory_index++] = (pins & 0xFF000000) | (addr << 8) | data;

            } else {
printf("w(");
                // Write Cycle -- we receive a byte
                byte data = pio_sm_get_blocking(pio0, 0);
printf(")");

                memory[memory_index++] = (pins & 0xFF000000) | (addr << 8) | data;
                pio_sm_put(pio0, 0, 0);  // for sync
            }
printf(" #%d\n", memory_index);

#endif
        }

        for (uint i = 0; i < NW; i++) {
            char lic = (0 != (memory[i] & (1<<LIC))) ? '-' : ' ';
            char rw = (0 != (memory[i] & (1<<R_W))) ? 'r' : 'W';
            printf("[%7d] %c%c %04x\n", i, lic, rw, memory[i]);
        }
    }
}

int main() {
  set_sys_clock_khz(MHz * 1000, true);
  stdio_usb_init();
  uint directions = InitializePinsReturnDirections();

  for (int i = 0;  i < 3; i++) {
    gpio_put(LED, 1);
    sleep_ms(200);
    gpio_put(LED, 0);
    sleep_ms(800);
    printf("<%d>\n", i);
  }

  printf("Reset:\n");
  RunReset();
  printf("PIO:\n");
  pio_clear_instruction_memory(pio0);
  const uint offset_t911 = pio_add_program(pio0, &t911veryfast_program);
  t911veryfast_program_init(pio0, 0, offset_t911);
  printf("Run:\n");
  RunCPU(directions);
}
