#define MHz 250  // clock speed, 150 is "normal".

#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/structs/systick.h>
#include <hardware/timer.h>
// #include <hardware/i2c.h>
#include <pico/bootrom.h>
#include <pico/multicore.h>
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
unsigned long long rounds = 0;
uint milliseconds;
uint errors;

#define ERR            if (errors++ < 32) printf

void RunCPU() {
    volatile sio_hw_t* hw = (volatile sio_hw_t*) sio_hw;

    while (true) {
        memory_index = 0;
        uint step = 0;
        uint value = 0;
        uint16_t countdown = 0;

        while (memory_index < NW) {
            uint pins = pio_sm_get_blocking(pio0, 0);
            uint addr = 0xFFFF & hw->gpio_hi_in;  // Read addr from high pins [32:47]

            // uint check = hw->gpio_in;
            // if (pins != check) printf("@ check %08x %08x xor %08x\n", pins, check, (pins^check));

            char rw = (0 != (pins & (1<<R_W))) ? 'r' : 'W';

            if (0 == (pins & (1<<R_W))) {
                // on Write cycle, Receive the value that was Written.
                value = pio_sm_get_blocking(pio0, 0);
            }

            // pio_sm_put(pio0, 0, 0x39);  // $39=RTS
            pio_sm_put(pio0, 0, 0xBD);  // $BD=JSR
            // pio_sm_put(pio0, 0, 0x20);  // $20=BRA
            // pio_sm_put(pio0, 0, 0x21);  // $21=BRN
// printf("      ");

            uint pins2 = hw->gpio_in;
            char lic = (0 != (pins2 & (1<<LIC))) ? '-' : ' ';
            char avma = (0 != (pins2 & (1<<AVMA))) ? '#' : ' ';
//printf("=%08x ", pins2);
//printf("\n");

            if (rounds > 3) switch (step) {
                case 0:
                        if (addr != 0xBDBD) ERR("@ s0 addr %04x\n", addr);
                        if (avma != '#') ERR("@ s0 no avma %08x\n", pins);
                        if (rw == 'W') ERR("@ s0 oops write\n");
                        break;
                case 1:
                        if (addr != 0xBDBE) ERR("@ s1 addr %04x\n", addr);
                        if (avma != '#') ERR("@ s1 no avma %08x\n", pins);
                        if (rw == 'W') ERR("@ s1 oops write\n");
                        break;
                case 2:
                        if (addr != 0xBDBF) ERR("@ s2 addr %04x\n", addr);
                        if (avma != ' ') ERR("@ s2 oops avma %08x\n", pins);
                        if (rw == 'W') ERR("@ s2 oops write\n");
                        break;
                case 3:
                        if (rw == 'W') ERR("@ s3 oops write\n");
                        break;
                case 4:
                        if (rw == 'W') ERR("@ s4 oops write\n");
                        break;
                case 5:
                        if (rw == 'W') ERR("@ s5 oops write\n");
                        if (avma != '#') ERR("@ s5 no avma %08x\n", pins);
                        break;
                case 6:
                        if (avma != '#') ERR("@ s6 no avma %08x\n", pins);
                        if (rw != 'W') ERR("@ s6 not write\n");
                        if (countdown == 0) {
                            countdown = addr;
                        } else {
                            if (countdown != addr) ERR("@ s6 got %04x want %04x xor %04x\n", addr, countdown, (addr ^ countdown));
                        }
                        countdown--;
                        break;
                case 7:
                        if (rw != 'W') ERR("@ s7 not write\n");
                        if (avma != '#') ERR("@ s0 oops avma %08x\n", pins);
                        if (countdown == 0) {
                            countdown = addr;
                        } else {
                            if (countdown != addr) ERR("@ s7 got %04x want %04x xor %04x\n", addr, countdown, (addr ^ countdown));
                        }
                        countdown--;
                        break;
                default:
                        ERR("@ bad case r%d. s%d.\n", rounds, step);
            }

            step++;
            if (0 != (pins2 & (1<<LIC))) {
                uint ms = milliseconds;
                step = 0;
                rounds++;
                if ((rounds & 0xFFFFF) == 0) {
                    printf("= %gMc secs=%10.3f rate=%g  errors= %u  eps= %g\n", 8*double(rounds)/1000000.0, double(ms)/1000.0, 8*double(rounds)*1000/double(ms), errors, double(errors)*1000/double(ms));
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

struct repeating_timer TimerData;

bool TimerCallback(repeating_timer_t* rt) {
    milliseconds++;
    return true;
}


int main() {
#if MHz != 150
  set_sys_clock_khz(MHz * 1000, true);
#endif
  stdio_usb_init();
  uint directions = InitializePinsReturnDirections();

  for (int i = 0;  i < 3; i++) {
    gpio_put(LED, 1);
    sleep_ms(200);
    gpio_put(LED, 0);
    sleep_ms(800);
    printf(":%d:\n", i);
  }

  printf(":r:\n");
  RunReset();

  printf(":t:\n");
  alarm_pool_init_default();
  add_repeating_timer_us(1000, TimerCallback, nullptr, &TimerData);     

  printf(":p:\n");
  pio_clear_instruction_memory(pio0);
  const uint offset_t911 = pio_add_program(pio0, &t911veryfast_program);
  t911veryfast_program_init(pio0, 0, offset_t911);

  printf(":g:\n");
  multicore_launch_core1(RunCPU);
  while (true) sleep_ms(1);
}
