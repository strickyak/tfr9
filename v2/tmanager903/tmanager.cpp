#define FOR_BASIC 0

// tmanager.cpp -- for the TFR/901 -- strick
//
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

// PIO code for the primary pico
#include "tpio.pio.h"

// Port Assignments
#include "tfr9ports.gen.h"

#define ATTENTION_SPAN 25 // was 250
#define TICK_MASK 0xFFFF   // one less than a power of two

//#define D if(1) if(1)printf
//#define P if(1) if(1 || 0)printf
//#define V if(1) if(1 || interest)printf
//#define Q if(1) if(1 || interest)printf

#define D if(1)printf
#define P if(1)printf
#define V if(1)printf
#define Q if(1)printf

typedef unsigned char byte;
typedef unsigned int word;
typedef unsigned char T_byte;
typedef unsigned int T_word;
typedef unsigned char T_16 [16];

extern byte getbyte();
extern void putbyte(byte x);
extern void SendIn_byte(byte x);
extern void SendIn_word(word x);
extern void SendIn_16(byte* x);
extern void RecvOut_byte(byte* x);

/////////// #include "../generated/rpc.h"

const byte Rom[] = {
  #include "level1.rom.h"
};

byte Disk[] = {
  #include "level1.disk.h"
};

byte Ram[0x10000];
byte Seen[0x10000];

uint Vectors[] = {
     0,  //  6309 TRAP
     // From ~/coco-shelf/toolshed/cocoroms/bas13.rom :
     0x0100, 0x0103, 0x010f, 0x010c, 0x0106, 0x0109,
     0,  //  RESET
};

enum {
    C_NOCHAR=160,
    C_PUTCHAR=161,
    C_GETCHAR=162,
    C_STOP=163,
    C_ABORT=164,
    C_KEY=165,
    C_NOKEY=166,
    C_DUMP_RAM=167,
    C_DUMP_LINE=168,
    C_DUMP_STOP=169,
};

#define PEEK(addr) (Ram[0xFFFF&(addr)])
#define PEEK2(addr) (((uint)Ram[0xFFFF&(addr)] << 8) | (uint)Ram[0xFFFF&((addr)+1)])

#define POKE(addr,value) do { Ram[0xFFFF&(addr)] = (byte)(value); } while (0)
#define POKE2(addr,value) do { Ram[0xFFFF&(addr)] = (byte)((value)>>8) ; Ram[0xFFFF&((addr)+1)] = (byte)(value) ; } while (0)

#define DELAY sleep_us(1);

#define IO_START 0xFF00
#define PRELUDE_START (IO_START - 32)

#define GET_STUCK()   { while (1) {putchar(255); sleep_ms(1000);} }

#define F_READ 0x0100
#define F_AVMA 0x0200
#define F_LIC  0x0400
#define F_BA   0x0800
#define F_BS   0x1000
#define F_BUSY 0x2000

#define F_HIGH (F_BA|F_BS|F_BUSY)

const byte RESET_BAR_PIN = 21;  // negative logic
const byte IRQ_BAR_PIN = 22;    // negative logic

uint interest;

// putbyte does CR/LF escaping for Binary Data
void putbyte(byte x) {
    switch (x) {
    case 0:  // NUL
    case 1:  // the escape
    case 10: // LF
    case 13: // CR
        putchar(1u);  // 1 is the escape char
        putchar(x | 32u);
        break;
    default:
        putchar(x);
    }
}

// Does this need escaping yet?
byte getbyte() {
    byte x = getchar();
    if (x == 1) {
        // Escaped.
        x = getchar();
        return x - 32u;
    } else {
        // Not escaped.
        return (x==10u) ? 13u : x;  // TODO
    }
}

const char* HighFlags(uint high) {
    if (!high) {
        return "";
    }
    static char high_buf[8];
    char* p = high_buf;
    if (high & F_BA) *p++ = 'A';
    if (high & F_BS) *p++ = 'S';
    if (high & F_BUSY) *p++ = 'Y';
    *p = '\0';
    return high_buf;
}

void DumpRam() {
    putchar(C_DUMP_RAM);
    for (uint i = 0; i < 0x10000; i += 16) {
        for (uint j = 0; j < 16; j++) {
            if (Ram[i+j]) goto yes;
        }
        continue;
yes:
        putchar(C_DUMP_LINE);
        putbyte(i>>8);
        putbyte(i);
        for (uint j = 0; j < 16; j++) {
            putbyte(Ram[i+j]);
        }
    }
    putchar(C_DUMP_STOP);
}
void DumpRamAndGetStuck() {
    DumpRam();
    GET_STUCK();
}

uint AddressOfRom() {
    return IO_START - sizeof(Rom);
}

bool CheckHeader(uint p) {
    uint z = 0;
    for (uint i = 0; i < 9; i++) {
        z ^= Rom[i+p];
    }
    // P("CheckHeader: %x\n", z);
    return (z == 255); // will be 255 if good.
}

void PrintName(uint p) {
    D(" ");
    D("\"");
    while (1) {
        byte ch = Rom[p];
        if ((127 & ch) < 32) break;
        D("%c", 127 & ch);
        if (ch & 128) break;
        ++p;
    }
    D("\"");
    D(" ");
}

void FindKernelEntry(uint *krn_start, uint *krn_entry, uint *krn_end) {
    for (uint i = 0; i + 10 < sizeof Rom; i++) {
        if (Rom[i]==0x87 && Rom[i+1]==0xCD) {
            if (!(CheckHeader(i))) {
                D("(Bad header at $%x)\n", i);
                continue;
            }
            uint size = (((uint)Rom[i+2])<<8) | Rom[i+3];
            uint name = (((uint)Rom[i+4])<<8) | Rom[i+5];
            uint entry = (((uint)Rom[i+9])<<8) | Rom[i+10];
            D("Offset %x Addr %x Size %x Entry %x", i, i + AddressOfRom(), size, entry);
            PrintName(i + name);
            D("\n");
            if (Rom[i+name]=='K' && Rom[i+name+1]=='r' && Rom[i+name+2]==('n'|0x80)) {
                *krn_start = i + AddressOfRom();
                *krn_entry = i + entry + AddressOfRom();
                *krn_end = i + size + AddressOfRom();
            }
            i += size - 1; // Skip over the module.  Less 1 because "i++" will still execute.
        }
    }
}

void ViewAt(const char* label, uint hi, uint lo) {
    uint addr = (hi << 8) | lo;
    V("=== %s: @%04x: ", label, addr);
    for (uint i = 0; i < 8; i++) {
        uint x = PEEK2(addr+i+i);
        V("%04x ", x);
    }
    V("|");
    for (uint i = 0; i < 16; i++) {
        byte ch = 0x7f & Ram[0xFFFF&(addr+i)];
        if (32 <= ch && ch <= 127) {
            V("%c", ch);
        } else if (ch==0) {
            V("-");
        } else {
            V(".");
        }
    }
    V("|\n");
}

void ResetCpu() {
    for (uint i = 0; i <= 20; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
    }
    // 903:
    uint pins[] = {21, 22, 26, 27, 28};
    for (uint p : pins) {
        gpio_init(p);
        gpio_put(p, 1);
        gpio_set_dir(p, GPIO_OUT);
        gpio_put(p, 1);
    }

    const uint PIN_E = 16; // clock output pin
    const uint PIN_Q = 17; // clock output pin

    D("begin reset cpu ========\n");
    gpio_put(RESET_BAR_PIN, not true);  // negative logic
    for (uint i = 0; i < 60; i++) {
        gpio_put(PIN_Q, 0); DELAY;
        gpio_put(PIN_E, 0); DELAY;
        gpio_put(PIN_Q, 1); DELAY;
        gpio_put(PIN_E, 1); DELAY;
    }
    D("run ========\n");
    gpio_put(RESET_BAR_PIN, not false);  // negative logic
}

uint WAIT_GET() {
    const PIO pio = pio0;
    const uint sm = 0;

    while (pio_sm_is_rx_fifo_empty(pio, sm)) continue;

    uint z = pio_sm_get(pio, sm);
    return z;
}

void PUT(uint x) {
    const PIO pio = pio0;
    const uint sm = 0;

    while (pio_sm_is_tx_fifo_full (pio, sm)) {
        P("ERROR: tx fifo full\n");
        while (true) DELAY;
    }
    pio_sm_put(pio, sm, x);
    if(0) P("> %x\n", x);
}

void StartPio() {
    const PIO pio = pio0;
    const uint sm = 0;

    pio_clear_instruction_memory(pio);
    const uint offset = pio_add_program(pio, &tpio_program);
    tpio_program_init(pio, sm, offset);
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

byte GetCharFromConsole() {
    putbyte(C_GETCHAR);
    byte one = getbyte();
    if (one == C_NOCHAR) {
        return 0;
    } else if (one == C_GETCHAR) {
        byte two = getbyte();
        return two;
    } else {
        printf("\nGetCharFromConsole did not expect %d\n", one);
        DumpRamAndGetStuck();
        return 0;
    }
}

int strikes;
void Strike(const char* why) {
    ++strikes;
    if (strikes >= 3) {
        D("------ Third Strike -------- %d\n", strikes);
        DumpRamAndGetStuck();
    }
}

void ShowChar(byte ch) {
    putchar(C_PUTCHAR);
    putchar(ch);
}

bool vsync_irq_enabled;

void EnableVSyncIrq(bool enable) {
    vsync_irq_enabled = not not enable;
}
void VSyncIrq(bool activate) {
    if (vsync_irq_enabled or not activate) {
        gpio_put(IRQ_BAR_PIN, not activate);  // negative logic
        printf("\n<IRQ=%d>\n", activate);
        ShowChar(activate ? '<' : '>');
    }
}

void HandlePio(uint num_cycles, uint krn_entry) {
    const PIO pio = pio0;
    const uint sm = 0;

    byte data;
    uint vma = 0; // Valid Memory Address ( = delayed AVMA )
    uint fic = 0; // First Instruction Cycle ( = delayed LIC )
    uint active = 0;
    uint num_resets = 0;
    uint event = 0;
    uint when = 0;
    uint arg = 0;
    uint prev = 0; // previous data byte
    interest = ATTENTION_SPAN;
    bool watch = 0; // May be useful again.

    PUT(0x1F0000);  // 0:15 inputs; 16:21 outputs.
    PUT(0x000000);  // Data to put.

    for (uint cy = 0; !num_cycles || cy < num_cycles; cy++) {
        if ((cy & TICK_MASK) == TICK_MASK) {
            ShowChar('`');
            Ram[0xFF03] |= 0x80;  // Set the bit indicating VSYNC occurred.
            if (vsync_irq_enabled) {
                VSyncIrq(true);
            }
        }

        uint twenty_three = WAIT_GET();
        if (twenty_three != 23) {
            D("ERROR: NOT twenty_three: %d.\n", twenty_three);
            while (true) DELAY;
        }

        const uint addr = WAIT_GET();
        const uint flags = WAIT_GET();
        // printf("--frodo-- & %04x %04x & %s\n", addr, flags, fic? "S" : "-");

        bool io = (active && 0xFF00 <= addr && addr <= /*0xFFEE*/ 0xFFFD);
        const bool reading = (flags & F_READ);

        if (watch) {
            // ...
        }

        if (reading) { // CPU reads, Pico Tx
            // q1: Reading.
            if (addr == 0x002E) {
                watch = true;
            }

            data = Ram[addr];  // default behavior

            if (fic && addr<0x0080) {
                    D("----- PC IN 00[0-7]x. Stopping addr=%x flags=%x\n", addr, flags);
                    DumpRamAndGetStuck();
                    return;
            }
            if (0xFF00 <= addr && addr < 0xFFF0) { // READ (GET) IO
                if (fic) {
                        D("----- PC IN FFxx. Stopping addr=%x flags=%x\n", addr, flags);
                        DumpRamAndGetStuck();
                        return;
                }

                switch (0xFF & addr) {
                case 0xFF & CONSOLE_PORT: // getchar from console
                    data = GetCharFromConsole();

                    D("= READY CHAR $%x\n", data);
                    switch (data) {
                    case C_ABORT:
                        D("----- GOT ABORT FROM CONSOLE.  Aborting.\n");
                        DumpRamAndGetStuck();
                        return;
                    case C_STOP:
                        D("----- GOT STOP FROM CONSOLE.  Stopping.\n");
                        DumpRamAndGetStuck();
                        return;
                    default:
                        break;
                    }
                    break;

                // Read PIA0
                case 0x00:
                case 0x01:
                    D("----- PIA0 Read not Impl: %x\n", addr);
                    DumpRamAndGetStuck();
                    break;
                case 0x02:
                    Ram[0xFF03] &= 0x7F;  // Clear the bit indicating VSYNC occurred.
                    VSyncIrq(false);  // Reading this register clears the vertical IRQ.
                    data = 0xFF;
                    break;
                case 0x03:
                    // OK to read, for the HIGH bit, which tells if VSYNC ocurred.
                    break;

                case 0xFF & (BLOCK_PORT+7): // read disk status
                    data = 1;  // 1 is OKAY
                    break;

                case 0xFF & 0xFFF0: // 6309 trap
                case 0xFF & 0xFFF1: // 6309 trap
                    D("----- 6309 ERROR TRAP.  Stopping.\n");
                    DumpRamAndGetStuck();
                    break;

                default: // other reads from Ram.
                    data = Ram[addr];
                    break;
                }
            }

            if (fic) {
                if (addr == krn_entry) {
                    num_resets++;
                    if (num_resets >= 2) {
                        D("---- Second Reset -- Stopping.\n");
                        DumpRamAndGetStuck();
                        return;
                    }
                }

                switch (data) {
                case 0x10:
                case 0x20:
                case 0x3B:
                    event = data;
                    when = cy;
                    break;
                default:
                    event = 0;
                    when = 0;
                }
            }
            PUT(0x00FF);  // pindirs: outputs
            PUT(data);    // pins
            PUT(0x0000);  // pindirs

            uint high = flags & F_HIGH;
            if (true || vma || high) {
                Q("%s %x %x  =%s #%d\n", (fic ? (Seen[addr] ? "@" : "@@") : vma ? "r" : "-"), addr, data, HighFlags(high), cy);
            } else {
                Q("|\n");
            }

            if (addr == 0xFFFE) active = 1;

            if (fic) {
                if (!Seen[addr]) {
                    interest = ATTENTION_SPAN;  // Interesting when at a new PC value.
                    Seen[addr] = 1;
                }
            }

            switch (event) {
            case 0x10: // prefix $10
                if (cy - when == 1) {
                    switch (data) {
                    case 0x3f: // SWI2
                        event = (event << 8) | data;
                    }
                }
                break;
            case 0x20:  // BRA: branch always
                if (cy - when == 1) {
                    switch (data) {
                    case 0xFE: // Infinite Loop
                        D("------- Infinite Loop.  Stopping.\n");
                        DumpRamAndGetStuck();
                        return;
                        break;
                    }
                }
                break;
            case 0x3B:  // RTI
                switch (cy - when) {
                case 2:
                    arg = 0x80 & data;  // entire bit of condition codes
                    Q("= CC: %02x (%s)\n", data, DecodeCC(data));
                    break;
                case 3:
                    break;
                case 4: ViewAt("D", prev, data);
                    break;
                case 5: P("= DP: %02x\n", data);
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
            // q2: Writing
            data = WAIT_GET();
            if (active) {
                if (addr==0 && data==0) {
                    Strike("writing 0 to 0");
                }

                if (FOR_BASIC && (0x8000 <= addr && addr < 0xFF00)) {
                    // Writing to ROM space
                    Q("wROM %x %x\n", addr, data);
                } else {
                    // Writing to RAM or IO space
                    Ram[addr] = data;
                    Q("w %x %x\n", addr, data);
                }

                switch (event) {
                case 0x103f:  // SWI2 (i.e. OS9 kernel call)
                    switch (cy - when) {
                    case 5: ViewAt("PC", data, prev);
                        break;
                    case 7: ViewAt("U", data, prev);
                        break;
                    case 9: ViewAt("Y", data, prev);
                        break;
                    case 11: ViewAt("X", data, prev);
                        break;
                    case 12: P("= DP: %02x\n", data);
                        break;
                    case 14: ViewAt("D", data, prev);
                        break;
                    case 15: P("= CC: %02x (%s)\n", data, DecodeCC(data));
                            ViewAt("SP", addr>>8, addr);
                        break;
                    }
                    break;
                }
            } // end if active
        }
        prev = data;

        // q3: Side Effects after Reading or Writing.
        if (io) {
            interest = ATTENTION_SPAN;  // Interesting when it is I/O.
            switch (255&addr) {
            case 0x00:
            case 0x01:
            case 0x02:
                D("= PIA0: %04x %c\n", addr, reading? 'r': 'w');
                goto OKAY;
            case 0x03:
                // Read PIA0
                D("= PIA0: %04x %c\n", addr, reading? 'r': 'w');
                EnableVSyncIrq(data&1); // Low bit is the IRQ enable on the PIA control.
                goto OKAY;

            case 255&CONSOLE_PORT:
                if (reading) {
                    if (32 <= data && data <= 126) {
                        Q("= GETCHAR: $%02x = '%c'\n", data, data);
                    } else {
                        Q("= GETCHAR: $%02x\n", data);
                    }
                } else {
                    if (32 <= data && data <= 126) {
                        Q("= PUTCHAR: $%02x = '%c'\n", data, data);
                    } else {
                        Q("= PUTCHAR: $%02x\n", data);
                    }
                    putchar(C_PUTCHAR);
                    putchar(data);
                }
                goto OKAY;

            case 255&(BLOCK_PORT+0): // Save params for Disk.
            case 255&(BLOCK_PORT+1):
            case 255&(BLOCK_PORT+2):
            case 255&(BLOCK_PORT+3):
            case 255&(BLOCK_PORT+4):
            case 255&(BLOCK_PORT+5):
            case 255&(BLOCK_PORT+6):
                Q("-BLOCK %x %x %x\n", addr, data, Ram[addr]);
                goto OKAY;
            case 255&(BLOCK_PORT+7): // Run Disk Command.
                if (!reading) {
                    byte command = Ram[BLOCK_PORT + 0];

                    Q("-NANDO %x %x %x\n", addr, data, Ram[data]);
                    Q("- sector $%02x.%04x bufffer $%04x diskop %x\n",
                        Ram[BLOCK_PORT + 1],
                        PEEK2(BLOCK_PORT + 2),
                        PEEK2(BLOCK_PORT + 4),
                        command);

                    uint lsn = PEEK2(BLOCK_PORT + 2);
                    byte* dp = Disk + 256*lsn;
                    uint buffer = PEEK2(BLOCK_PORT + 4);
                    byte* rp = Ram + buffer;

                    Q("- VARS sector $%04x bufffer $%04x diskop %x\n", lsn, buffer, command);

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
                    default: {
                        printf("\nwut command %d.\n", command);
                        DumpRamAndGetStuck();
                        }
                    }
                }
                goto OKAY;

            default: {}
            }
            D("--- IOPAGE %s: addr %x flags %x data %x\n", reading? "READ": "WRITE", addr, flags, data);
        }
OKAY:

        vma = 0 != (flags & F_AVMA);   // AVMA bit means next cycle is Valid
        fic = 0 != (flags & F_LIC); // LIC bit means next cycle is First Instruction Cycle

#if 0
        if (interest > 0) {
            interest--;
            if (!interest) {
                D("...\n");
                D("...\n");
                D("...\n");
            }
        }
#endif
    }
}

void InitRamFromRom() {
    uint addr = AddressOfRom();
    for (uint i = 0; i < sizeof Rom; i++) {
        Ram[addr + i] = Rom[i];
    }
}

// LED for Pico W:   LED(1) for on, LED(0) for off.
#define LED(X) cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (X))

int main() {
    stdio_init_all();
    cyw43_arch_init();

    const uint BLINKS = 5;
    for (uint i = 0; i <= BLINKS; i++) {
        LED(1);
        sleep_ms(300);
        LED(0);
        sleep_ms(700);

        char pbuf[10];
        sprintf(pbuf, "+%d+ ", BLINKS - i);
        printf("%s", pbuf);
        for (const char*p = pbuf; *p; p++) {
            putchar(C_PUTCHAR);
            putchar(*p);
        }
    }
    printf("\n");
    putchar(C_PUTCHAR);
    putchar('\n');

    LED(1);
    sleep_us(200);
    InitRamFromRom();
    ResetCpu();
    LED(0);

    uint a = AddressOfRom();
    D("AddressOfRom = $%x\n", a);
    uint krn_start, krn_entry, krn_end;
    FindKernelEntry(&krn_start, &krn_entry, &krn_end);
    D("KernelEntry = $%x\n", krn_entry);

    // Set interrupt vectors
    for (uint j = 1; j < 7; j++) {
        POKE2(0xFFF0+j+j, Vectors[j]);
    }
    POKE2(0xFFFE, PRELUDE_START);       // Reset Vector.

    StartPio();
    HandlePio(0, krn_entry);
    sleep_ms(100);
    D("\nFinished.\n");
    sleep_ms(100);
    GET_STUCK();
}
