#define MHz 250  // clock speed, 150 is "normal".

#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/structs/systick.h>
#include <hardware/timer.h>
#include <hardware/i2c.h>
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

using IOReader = std::function<byte(uint addr)>;
using IOWriter = std::function<void(uint addr, byte data)>;

IOReader IOReaders[256];
IOWriter IOWriters[256];

void PollUsbInput();

void InstallVector(uint i, uint target_addr) {
  IOReaders[255 & (0xFFF0 + 2 * i + 0)] = [target_addr](uint _a) {
    return (byte)(target_addr >> 8);
  };
  IOReaders[255 & (0xFFF0 + 2 * i + 1)] = [target_addr](uint _a) {
    return (byte)(target_addr >> 0);
  };
}

// #include "acia.h"
// #include "turbo9sim.h"
// #include "ram.h"

byte ram[0x10000];
bool is_an_os9;

// These vectors show up at 0xFFF0.
// They do not include the final RESET vector at 0xFFFE;
// that will be computed by InstallTurbo9OS.
const uint Turbo9os_Vectors[] = {
    0x0000, 0x0100, 0x0103, 0x010F, 0x010C, 0x0106, 0x0109,
};

const byte Turbo9os_Rom[] = {
#include "turbo9os.rom.h"
};

void DoWrite(uint addr, byte data);
byte DoRead(uint addr);

void Poke(uint addr, byte data) { ram[addr] = data; }
byte Peek(uint addr) { return ram[addr]; }
uint Peek2(uint addr) {
    return ((uint)Peek(addr) << 8) | (uint)Peek(addr+1);
}

void InstallTurbo9OS() {
    is_an_os9 = true;
    // Copy 7 Vectors;  Reset vector comes later.
    for (uint i = 0; i < 7; i++) {
      InstallVector(i, Turbo9os_Vectors[i]);
    }
    // Copy ROM to RAM, ending just before 0xFF00.
    constexpr uint n = sizeof Turbo9os_Rom;
    constexpr uint begin = 0xFF00 - n;  // beginning addr of ROM
    for (uint i = 0; i < n; i++) {
      Poke(begin + i, Turbo9os_Rom[i]);
    }

    assert(Peek2(begin) == 0x87CD);  // OS9 module magic number

    uint name = Peek2(begin + 4);  // OS9 module name offset is 4
    char expect[7] = "kernel";
    expect[5] |= 0x80;  // OS9 string termination bit overlays final 'l'
    assert(0 == memcmp(Turbo9os_Rom + name, expect, 6));

    uint entry = Peek2(begin + 9);  // OS9 module entry offset is 9
    InstallVector(7, begin + entry);
}

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

volatile uint busy;
void Delay(uint n) {
    for (uint i = 0; i < n*10; i++) {
        busy += i;
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

void DoWrite(uint addr, byte data) {
    if (addr < 0xFF00) {
        Poke(addr, data);
    } else {
        auto w = IOWriters[addr & 0xFF];
        if (w) {
            w(addr, data);
        }
    }
}
byte DoRead(uint addr) {
    if (addr < 0xFF00) {
        return Peek(addr);
    } else {
        auto r = IOReaders[addr & 0xFF];
        if (r) {
            return r(addr);
        }
        return 0;
    }
}

void RunCPU(uint directions) {
    volatile sio_hw_t* hw = (volatile sio_hw_t*) sio_hw;
    bool valid_memory_cycle = true;
    while (true) {
        Delay(1); // phase 1
        gpio_put(Q, 1);

        const uint lo = hw->gpio_in;
        const uint hi = hw->gpio_hi_in;
        const bool reading = (lo & (1<<R_W)) != 0;
        const uint addr = hi & 0xFFFF;

        Delay(1); // phase 2
        gpio_put(E, 1);
        const uint re_lo = hw->gpio_in;
        // phase 3

        if (!valid_memory_cycle) { // Not a valid cycle
            Delay(1); // phase 3
            gpio_put(Q, 0);
            Delay(1); // phase 4
            gpio_put(E, 0);
            printf("cy\n");

        } else if (reading) { // CPU reads, pico outputs.

            byte data = DoRead(addr);
            gpio_set_dir_all_bits(directions | 0xFF);  // Data bus outputs
            gpio_put_all(directions | data);
            Delay(1); // phase 3
            gpio_put(Q, 0);
            Delay(1); // phase 4
            gpio_put(E, 0);
            Delay(1); // hold time
            gpio_set_dir_all_bits(directions | 0x00);  // Data bus inputs

            printf("cy r .. %04x %02x\n", addr, data);

        } else { // CPU writes, pico inputs.

            Delay(1); // phase 3
            gpio_put(Q, 0);
            Delay(1); // phase 4
            byte data = hw->gpio_in & 0xFF;
            DoWrite(addr, data);
            gpio_put(E, 0);

            printf("cy w >> %04x %02x\n", addr, data);
        }
        valid_memory_cycle = (re_lo & (1<<AVMA)) != 0;
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
  }

  InstallTurbo9OS();
  RunReset();
  RunCPU(directions);
}
