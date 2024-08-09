// tfr901main.cpp -- for the TFR/901 -- strick
//
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "yksi.pio.h"

typedef unsigned char byte;

const uint BootStart = 0xC000;

const byte Rom[] = {
#include "boot.rom.h"
};

byte Ram[0x10000];
byte Seen[0x10000];

uint Vectors[] = {
     0,  //  6309 TRAP
     // From ~/coco-shelf/toolshed/cocoroms/bas13.rom :
     0x0100, 0x0103, 0x010f, 0x010c, 0x0106, 0x0109,
     0,  //  RESET
};

#define PEEK2(addr) (((uint)Ram[0xFFFF&(addr)] << 8) | (uint)Ram[0xFFFF&((addr)+1)])

#define POKE2(addr,value) do { Ram[0xFFFF&(addr)] = ((value)>>8) ; Ram[0xFFFF&((addr)+1)] = (value) ; } while (0)

#define DELAY sleep_us(1);

#if 0
volatile uint delay_tactic;

void Delay(uint n) {
    for (uint i = 0; i < n; i++) {
        delay_tactic += i*i;
    }
}
#endif

void ViewAt(const char* label, uint hi, uint lo) {
    uint addr = (hi << 8) | lo;
    printf("=== %s: @%04x: ", label, addr);
    for (uint i = 0; i < 8; i++) {
        uint x = PEEK2(addr+i+i);
        printf("%04x ", x);
    }
    printf("|");
    for (uint i = 0; i < 16; i++) {
        byte ch = 0x7f & Ram[0xFFFF&(addr+i)];
        if (32 <= ch && ch <= 127) printf("%c", ch);
        else if (ch==0) printf("-");
        else printf(".");
    }
    printf("|\n");
}

void SetMode(byte x) {
    printf("mode %x\n", x);
    for (uint i = 18; i < 21; i++) {
        gpio_set_dir(i, true/*out*/);
        gpio_put(i, (1&x));
        x = x >> 1;
    }
}

void OutLowDataByte(byte x) {
    printf("out %x\n", x);
    for (uint i = 0; i < 8; i++) {
        gpio_set_dir(i, true/*out*/);
        gpio_put(i, (1&x));
        x = x >> 1;
    }
}

void SetLatch(byte x) {
    printf("latch %x\n", x);
    SetMode(0);
    OutLowDataByte(x); DELAY;
    SetMode(4); DELAY;
    SetMode(0);
}

void ResetCpu() {
    for (uint i = 0; i < 21; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
    }

    const uint PIN_EBAR = 16; // clock output pin
    const uint PIN_QBAR = 17; // clock output pin

    printf("begin reset cpu ========\n");
    SetLatch(0x1C); // Reset & Halt.
    for (uint i = 0; i < 1000; i++) {
        gpio_put(PIN_QBAR, 0); DELAY;
        gpio_put(PIN_EBAR, 0); DELAY;
        gpio_put(PIN_QBAR, 1); DELAY;
        gpio_put(PIN_EBAR, 1); DELAY;
    }
    printf("begin halt cpu ========\n");
    SetLatch(0x1D); // Halt.
    for (uint i = 0; i < 1000; i++) {
        gpio_put(PIN_QBAR, 0); DELAY;
        gpio_put(PIN_EBAR, 0); DELAY;
        gpio_put(PIN_QBAR, 1); DELAY;
        gpio_put(PIN_EBAR, 1); DELAY;
    }
    printf("run ========\n");
    SetLatch(0x1F); // Run.
}

uint WAIT_GET() {
    const PIO pio = pio0;
    const uint sm = 0;
    const uint LED_PIN = PICO_DEFAULT_LED_PIN;

    // gpio_put(LED_PIN, 1);
    while (pio_sm_is_rx_fifo_empty(pio, sm)) {
        // printf(",");
    }
    // DELAY;
    uint z = pio_sm_get(pio, sm);
    if(0) printf("< %x\n", z);
    // gpio_put(LED_PIN, 0);
    return z;
}

void PUT(uint x) {
    const PIO pio = pio0;
    const uint sm = 0;

    while (pio_sm_is_tx_fifo_full (pio, sm)) {
        printf("ERROR: tx fifo full\n");
        while (true) DELAY;
    }
    pio_sm_put(pio, sm, x);
    if(0) printf("> %x\n", x);
}

void StartYksi() {
    const PIO pio = pio0;
    const uint sm = 0;

    pio_clear_instruction_memory(pio);
    const uint offset = pio_add_program(pio, &yksi_program);
    yksi_program_init(pio, sm, offset);
}

void HandleYksi(uint num_cycles, uint krn_entry) {
    const PIO pio = pio0;
    const uint sm = 0;

    const uint FULL = 1000000;

    uint vma = 0;
    uint start = 0;
    uint trigger = 0;
    uint active = 0;
    uint tank = FULL;
    uint hidden = 0;
    uint num_resets = 0;
    uint event = 0;
    uint when = 0;
    uint arg = 0;
    uint prev = 0; // previous data byte

    PUT(0x1F0000);  // 0:15 inputs; 16:21 outputs.
    PUT(0x000000);  // Data to put.
    for (uint i = 0; i < num_cycles; i++) {
        uint twenty_three = WAIT_GET();
        if (twenty_three != 23) {
            sleep_ms(1000);
            printf("ERROR: NOT twenty_three: %d.\n", twenty_three);
            sleep_ms(3000);
            while (true) DELAY;
        }

        uint addr = WAIT_GET();
        uint flags = WAIT_GET();

        if (active && 0xFF04 <= addr && addr < 0xFFEC) {
            trigger = 1;
            printf("IOPAGE: addr %x flags %x -- Stopping.\n", addr, flags);
            return;
        }

        if ((i & 0xFFFF) == 0) {
            printf("--- cycle %d. ---\n", i);
        }

        bool reading = (flags & 0x100);
        if (reading) { // CPU reads, Pico Tx
            byte data = Ram[addr];

            if (start) {
                if (!Seen[addr]) {
                    Seen[addr] = 1;
                    printf("---------- (%d. hidden)\n", hidden);
                    tank = FULL;
                    hidden = 0;
                } else if (tank) {
                    --tank;
                    hidden = 0;
                } else {
                    ++hidden;
                }

                if (addr == krn_entry) {
                    num_resets++;
                    if (num_resets >= 2) {
                        printf("---- Second Reset -- Stopping.\n");
                        return;
                    }
                }

                switch (data) {
                case 0x10:
                case 0x20:
                case 0x2B:
                    event = data;
                    when = i;
                    break;
                default:
                    event = 0;
                    when = 0;
                }
            }

            PUT(0x00FF);  // pindirs: outputs
            PUT(data);    // pins
            PUT(0x0000);  // pindirs

            if (tank) printf("%c %x %x   #%d\n", (start ? '@' : vma ? 'r' : '-'), addr, data, i);

            if (addr == 0xFFFE) active = 1;

            switch (event) {
            case 0x10: // prefix $10
                if (i - when == 1) {
                    switch (data) {
                    case 0x3f: // SWI2
                        event = (event << 8) | data;
                    }
                }
                break;
            case 0x20:  // BRA: branch always
                if (i - when == 1) {
                    switch (data) {
                    case 0xFE: // Infinite Loop
                        printf("------- Infinite Loop.  Stopping.\n");
                        return;
                        break;
                    }
                }
                break;
            case 0x3B:  // RTI
                switch (i - when) {
                case 2:
                    arg = 0x80 & data;  // entire bit of condition codes
                    printf("= CC %02x", data);
                    break;
                case 3:
                    break;
                case 4: ViewAt("D", prev, data);
                    break;
                case 5: printf("= DP %02x", data);
                    break;
                case 6:
                    break;
                case 7: ViewAt("X", prev, data);
                    break;
                case 8:
                    break;
                case 9: ViewAt("Y", prev, data);
                    break;
                case 10:
                    break;
                case 11: ViewAt("U", prev, data);
                    break;
                case 12:
                    break;
                case 13: ViewAt("PC", prev, data);
                    break;
                case 14: ViewAt("SP", addr>>8, addr);
                    break;
                }
            }
            prev = data;
        } else {  // CPU writes, Pico Rx
            byte data = WAIT_GET();
            if (active) {
                Ram[addr] = data;
                if (tank) printf("w %x %x\n", addr, data);

                switch (event) {
                case 0x103f:  // RTI
                    switch (i - when) {
                    case 5: ViewAt("PC", data, prev);
                        break;
                    case 7: ViewAt("U", data, prev);
                        break;
                    case 9: ViewAt("Y", data, prev);
                        break;
                    case 11: ViewAt("X", data, prev);
                        break;
                    case 12: printf("= DP: %02x\n", data);
                        break;
                    case 14: ViewAt("D", data, prev);
                        break;
                    case 15: printf("= CC: %02x\n", data);
                            ViewAt("SP", addr>>8, addr);
                        break;
                    }
                    break;
                }

                prev = data;
            }
        }

        // printf("##### tank %d. event $%x when %d.\n", tank, event, when);

        vma = flags & 0x200;   // AVMA bit means next cycle is Valid
        start = flags & 0x400; // LIC bit means next cycle is Start
    }
    if (hidden) {
        printf("---------- (%d. hidden)\n", hidden);
    }
}

uint InitRamFromRomReturnReset() {
    for (uint i = 0; i < sizeof Rom; i++) {
        Ram[BootStart + i] = Rom[i];
    }

    for (uint i = 0; i < 7; i++) {
        Ram[0xFFF0 + i + i + 0] = Vectors[i] >> 8;
        Ram[0xFFF0 + i + i + 1] = Vectors[i];
    }

    // uint krn_size = PEEK2(BootStart+2);
    uint krn_entry = BootStart + PEEK2(BootStart+9);
    POKE2(0xFFFE, krn_entry);

    return krn_entry;
}

int main() {
    stdio_init_all();

#ifndef PICO_DEFAULT_LED_PIN
#warning blink example requires a board with a regular LED
#endif
    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    const uint BLINKS = 5;
    for (uint i = 0; i <= BLINKS; i++) {
        gpio_put(LED_PIN, 1);
        sleep_ms(300);
        gpio_put(LED_PIN, 0);
        sleep_ms(700);
        printf("+%d.\n", i);
    }

    for (uint i = 0xFFF0; i < 0x10000; i++) {
        printf("V %04x = %02x\n", i, Ram[i]);
    }

    gpio_put(LED_PIN, 1);
    ResetCpu();
    gpio_put(LED_PIN, 0);
    sleep_us(20);
    uint krn_entry = InitRamFromRomReturnReset();
    printf("krn_entry on reset is %x", krn_entry);

    StartYksi();
    HandleYksi(100 * 1000 * 1000, krn_entry);
    sleep_ms(1000);
    printf("\nFinished.\n");
    sleep_ms(3000);
    while (true) DELAY;

}
