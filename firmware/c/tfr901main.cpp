// tfr901main.cpp -- for the TFR/901 -- strick
//
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "yksi.pio.h"

#define CONSOLE_PORT 0xFF10

typedef unsigned char byte;

const byte Rom[] = {
  #include "boot.rom.h"
};

byte Disk[] = {
  #include "boot.disk.h"
};

byte Ram[0x10000];
byte Seen[0x10000];

uint Vectors[] = {
     0,  //  6309 TRAP
     // From ~/coco-shelf/toolshed/cocoroms/bas13.rom :
     0x0100, 0x0103, 0x010f, 0x010c, 0x0106, 0x0109,
     0,  //  RESET
};

const char ConsoleCommands[] =
    "   echo Hello World\r"
    "   mdir\r"
    "   procs\r"
    "   free\r"
    "   dir\r"
    "   nando\r\0";

#define PEEK2(addr) (((uint)Ram[0xFFFF&(addr)] << 8) | (uint)Ram[0xFFFF&((addr)+1)])

#define POKE2(addr,value) do { Ram[0xFFFF&(addr)] = ((value)>>8) ; Ram[0xFFFF&((addr)+1)] = (value) ; } while (0)

#define DELAY sleep_us(1);

#define IO_START 0xFF00

void DumpRamAndGetStuck() {
    printf("\n:DumpRam\n");
    for (uint i = 0; i < 0x10000; i += 16) {
      for (uint j = 0; j < 16; j++) {
        if (Ram[i+j]) goto yes;
      }
      continue;

yes:
      printf(":%04x: ", i);
      for (uint j = 0; j < 16; j++) {
        printf("%c%02x", (Seen[i+j]? '-' : ' '), Ram[i+j]);
        if ((j&3)==3) putchar(' ');
      }
      printf("\n");
    }
    printf("\n:DumpRam\n");
    while (1) {} // Stuck
}

uint AddressOfRom() {
    return IO_START - sizeof(Rom);
}

bool CheckHeader(uint p) {
    uint z = 0;
    for (uint i = 0; i < 9; i++) {
        z ^= Rom[i+p];
    }
    // printf("CheckHeader: %x\n", z);
    return (z == 255); // will be 255 if good.
}

void PrintName(uint p) {
    putchar(' ');
    putchar('"');
    while (1) {
        byte ch = Rom[p];
        if ((127 & ch) < 32) break;
        putchar(127 & ch);
        if (ch & 128) break;
        ++p;
    }
    putchar('"');
    putchar(' ');
}

void FindKernelEntry(uint *krn_start, uint *krn_entry, uint *krn_end) {
    for (uint i = 0; i + 10 < sizeof Rom; i++) {
        if (Rom[i]==0x87 && Rom[i+1]==0xCD) {
            if (!(CheckHeader(i))) {
                printf("(Bad header at $%x)\n", i);
                continue;
            }
            uint size = (((uint)Rom[i+2])<<8) | Rom[i+3];
            uint name = (((uint)Rom[i+4])<<8) | Rom[i+5];
            uint entry = (((uint)Rom[i+9])<<8) | Rom[i+10];
            printf("Offset %x Addr %x Size %x Entry %x", i, i + AddressOfRom(), size, entry);
            PrintName(i + name);
            putchar('\n');
            if (Rom[i+name]=='K' && Rom[i+name+1]=='r' && Rom[i+name+2]==('n'|0x80)) {
                *krn_start = i + AddressOfRom();
                *krn_entry = i + entry + AddressOfRom();
                *krn_end = i + size + AddressOfRom();
            }
        }
    }
}

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

const char* DecodeCC(byte cc) {
    static char buf[9];
    buf[0] = 0x80 & cc ? 'E' : 'e';
    buf[1] = 0x40 & cc ? 'F' : 'f';
    buf[2] = 0x20 & cc ? 'H' : 'h';
    buf[3] = 0x10 & cc ? 'I' : 'i';
    buf[4] = 0x08 & cc ? 'N' : 'n';
    buf[5] = 0x04 & cc ? 'Z' : 'z';
    buf[6] = 0x02 & cc ? 'V' : 'v';
    buf[7] = 0x01 & cc ? 'C' : 'c';
    buf[8] = '\0';
    return buf;
}

void HandleYksi(uint num_cycles, uint krn_entry) {
    const PIO pio = pio0;
    const uint sm = 0;

    const uint FULL = 1000 * 1000 * 1000;

    byte data;
    uint vma = 0;
    uint start = 0;
    uint active = 0;
    uint num_resets = 0;
    uint event = 0;
    uint when = 0;
    uint arg = 0;
    uint prev = 0; // previous data byte
    uint console_command_index = 0;

    Ram[0xFF10] = ConsoleCommands[console_command_index++];
    printf("= READY CHAR $%x\n", Ram[0xFF10]);

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

        if ((i & 0xFFFF) == 0) {
            printf("--- cycle %d. ---\n", i);
        }

        bool reading = (flags & 0x100);
        if (reading) { // CPU reads, Pico Tx

            switch (addr) {
            case 0xFF10: // getchar from console
                data = ConsoleCommands[console_command_index++];
                printf("= READY CHAR $%x\n", data);
                if (!data) {
                    printf("----- END OF CONSOLE COMMANDS.  Stopping.\n");
                    DumpRamAndGetStuck();
                    return;
                }
                break;

            case 0xFF3F: // read disk status
                data = 1;  // 1 is OKAY
                break;
            
            default: // other reads from Ram.
                data = Ram[addr];
                break;
            }

            if (start) {
                if (addr == krn_entry) {
                    num_resets++;
                    if (num_resets >= 2) {
                        printf("---- Second Reset -- Stopping.\n");
                        DumpRamAndGetStuck();
                        return;
                    }
                }

                switch (data) {
                case 0x10:
                case 0x20:
                case 0x3B:
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

            printf("%s %x %x   #%d\n", (start ? (Seen[addr] ? "@" : "@@") : vma ? "r" : "-"), addr, data, i);

            if (addr == 0xFFFE) active = 1;

            if (start) {
                Seen[addr] = 1;
            }

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
                        DumpRamAndGetStuck();
                        return;
                        break;
                    }
                }
                break;
            case 0x3B:  // RTI
                switch (i - when) {
                case 2:
                    arg = 0x80 & data;  // entire bit of condition codes
                    printf("= CC: %02x (%s)\n", data, DecodeCC(data));
                    break;
                case 3:
                    break;
                case 4: ViewAt("D", prev, data);
                    break;
                case 5: printf("= DP: %02x\n", data);
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
        } else {  // CPU writes, Pico Rx
            data = WAIT_GET();
            if (active) {
                Ram[addr] = data;
                printf("w %x %x\n", addr, data);

                switch (event) {
                case 0x103f:  // SWI2 (i.e. OS9 kernel call)
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
                    case 15: printf("= CC: %02x (%s)\n", data, DecodeCC(data));
                            ViewAt("SP", addr>>8, addr);
                        break;
                    }
                    break;
                }
            }
        }
        prev = data;

        if (active && 0xFF00 <= addr && addr < 0xFFEE) {
            switch (255&addr) {
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03:
                printf("= PIA0: %04x %c\n", addr, reading? 'r': 'w');
                goto OKAY;

            case 255&CONSOLE_PORT:
                if (reading) {
                    printf("= GETCHAR: $%02x = < %c >\n", data, (32 <= data && data <= 126) ? data : '#');
                } else {
                    printf("= PUTCHAR: $%02x = < %c >\n", data, (32 <= data && data <= 126) ? data : '#');
                }
                goto OKAY;

            case 0x30:
            case 0x31:
            case 0x32:
            case 0x33:
            case 0x34:
            case 0x35:
            case 0x36:
            case 0x37:
            case 0x38:
            case 0x39:
            case 0x3A:
            case 0x3B:
            case 0x3C:
            case 0x3D:
            case 0x3E:
                printf("-NANDO %x %x %x\n", addr, data, Ram[data]);
                goto OKAY;

            case 0x3F:
                if (!reading) {
                    byte command = Ram[0xFF30];

                    printf("-NANDO %x %x %x\n", addr, data, Ram[data]);
                    printf("- sector $%02x.%04x bufffer $%04x command %x\n",
                        Ram[0xFF31],
                        PEEK2(0xFF32),
                        PEEK2(0xFF38),
                        command);

                    uint lsn = PEEK2(0xFF32);
                    byte* dp = Disk + 256*lsn;
                    uint buffer = PEEK2(0xFF38);
                    byte* rp = Ram + buffer;
                    switch (command) {
                    case 0: // Read
                        for (uint k = 0; k < 256; k++) {
                            rp[k] = dp[k];
                        }
                        goto OKAY;
                    case 1: // Write
                        for (uint k = 0; k < 256; k++) {
                            dp[k] = rp[k];
                        }
                        goto OKAY;
                    default: {}
                    }
                }
                goto OKAY;

            default: {}
            }
            printf("\n--- IOPAGE: addr %x flags %x data %x -- Stopping.\n", addr, flags, data);
            DumpRamAndGetStuck();
            return;
        }
OKAY:

        vma = flags & 0x200;   // AVMA bit means next cycle is Valid
        start = flags & 0x400; // LIC bit means next cycle is Start
    }
}

void InitRamFromRom() {
    uint addr = AddressOfRom();
    for (uint i = 0; i < sizeof Rom; i++) {
        Ram[addr + i] = Rom[i];
    }
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

    gpio_put(LED_PIN, 1);
    ResetCpu();
    gpio_put(LED_PIN, 0);
    sleep_us(20);
    InitRamFromRom();

    uint a = AddressOfRom();
    printf("AddressOfRom = $%x\n", a);
    uint krn_start, krn_entry, krn_end;
    FindKernelEntry(&krn_start, &krn_entry, &krn_end);
    printf("KernelEntry = $%x\n", krn_entry);

#if 1
    POKE2(0xFFFE, krn_entry);       // Reset Vector.
#else
    POKE2(0xFFFE, a);       // Reset Vector.
    POKE2(a+5, krn_entry);  // into the JMP statement.
#endif

    for (uint j = 1; j < 7; j++) {
        //NO// POKE2(0xFFF0+j+j, krn_start + PEEK2(krn_end+j+j-2));  // Six interrupt Vectors.
        POKE2(0xFFF0+j+j, Vectors[j]);
    }

    StartYksi();
    HandleYksi(1000 * 1000 * 1000, krn_entry);
    sleep_ms(1000);
    printf("\nFinished.\n");
    sleep_ms(3000);
    while (true) DELAY;
}
