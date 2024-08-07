// tfr901main.cpp -- for the TFR/901 -- strick
//
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "yksi.pio.h"

typedef unsigned char byte;

const byte Rom[] = {
#include "boot.rom.h"
};

byte Ram[0x10000];

#define RESET_VECTOR 0xF068

uint Vectors[] = {
     0xFFF0,  //  6309 TRAP
     0x00DF,  //  SWI3
     0x00E5,  //  SWI2
     0x00EF,  //  FIRQ
     0x00F0,  //  IRQ
     0x00F4,  //  SWI
     0x00EB,  //  NMI
     RESET_VECTOR,  //  RESET
};

#define DELAY sleep_us(1);

#if 0
volatile uint delay_tactic;

void Delay(uint n) {
    for (uint i = 0; i < n; i++) {
        delay_tactic += i*i;
    }
}
#endif

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
        printf(",");
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
    if(0) printf("s0 ");
    pio_clear_instruction_memory(pio);
    if(0) printf("s00 ");
    const uint offset = pio_add_program(pio, &yksi_program);

    yksi_program_init(pio, sm, offset);

    // pio_sm_set_enabled(pio, sm, true);
    // pio_sm_restart(pio, sm);
}

void HandleYksi(uint num_cycles) {
    const PIO pio = pio0;
    const uint sm = 0;

    uint vma = 0;
    uint start = 0;
    uint trigger = 0;
    uint active = 0;

    PUT(0x1F0000);  // 0:15 inputs; 16:21 outputs.
    PUT(0x000000);  // Data to put.
    for (uint i = 0; i < num_cycles; i++) {
        uint twenty_three = WAIT_GET();
        if (twenty_three != 23) {
            printf("ERROR: NOT twenty_three: %d.\n", twenty_three);
            while (true) DELAY;
        }

        uint addr = WAIT_GET();
        uint flags = WAIT_GET();

        if (0xFF00 <= addr && addr < 0xFFF0) {
            trigger = 1;
        }

        bool reading = (flags & 0x100);
        if (reading) { // CPU reads, Pico Tx
            byte data = Ram[addr];
            PUT(0x00FF);  // pindirs: outputs
            PUT(data);    // pins
            PUT(0x0000);  // pindirs
            if (start) printf("%c %x %x\n",
                (start ? '@' : vma ? 'r' : '-'),
                addr, data);
            if (addr == 0xFFFE) active = 1;
        } else {  // CPU writes, Pico Rx
            byte data = WAIT_GET();
            if (active) Ram[addr] = data;
            if (1) printf("w %x %x\n", addr, data);
        }

        vma = flags & 0x200;   // AVMA bit means next cycle is Valid
        start = flags & 0x400; // LIC bit means next cycle is Start
    }
}

int main() {
#ifndef PICO_DEFAULT_LED_PIN
#warning blink example requires a board with a regular LED
#else
    stdio_init_all();
    for (uint i = 0; i < sizeof Rom; i++) {
        Ram[0xC000 + i] = Rom[i];
    }
    for (uint i = 0; i < 8; i++) {
        Ram[0xFFF0 + i + i + 0] = Vectors[i] >> 8;
        Ram[0xFFF0 + i + i + 1] = Vectors[i];
    }

    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    const uint N = 3;
    for (uint i = 0; i < N; i++) {
        gpio_put(LED_PIN, 1);
        sleep_ms(300);
        gpio_put(LED_PIN, 0);
        sleep_ms(700);
        printf("+%d.\n", i);
    }

    for (uint i = 0xFFF0; i < 0x10000; i++) {
        printf("V %04x = %02x\n", i, Ram[i]);
    }

    if (1) {
        gpio_put(LED_PIN, 1);
        ResetCpu();
        gpio_put(LED_PIN, 0);
        sleep_us(20);

        StartYksi();
        HandleYksi(10000000);
    }
#endif
}
