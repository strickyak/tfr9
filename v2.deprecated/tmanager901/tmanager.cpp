#define FOR_BASIC 0

// tmanager.cpp -- for the TFR/901 -- strick
//
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

// PIO code for the primary pico
#include "tpio.pio.h"

// Port Assignments
#include "tfr9ports.gen.h"

#define ATTENTION_SPAN 25 // was 250
#define TICK_MASK 0xFF   // one less than a power of two

///// #define CONSOLE_PORT 0xFF50
#define DISK_PORT 0xFF58
#define D if(1) if(1)printf
#define P if(1) if(0)printf
#define V if(1) if(1 || interest)printf
#define Q if(1) if(1 || interest)printf

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

#if FOR_BASIC
const byte BasicRom[] = {
  #include "bas13.rom.h"
};

byte Keystrokes[300];

#else
const byte Rom[] = {
  #include "level1.rom.h"
};

byte Disk[] = {
  #include "level1.disk.h"
};
#endif

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

uint interest;

// putbyte does CR/LF escaping for Binary Data
void putbyte(byte x) {
    switch (x) {
    case 0:  // NUL
    case 1:  // the escape
    case 10: // LF
    case 13: // CR
        putchar(1);  // 1 is the escape char
        putchar(x | 32);
        break;
    default:
        putchar(x);
    }
}
byte getbyte() {
    byte x = getchar();
    return (x==10) ? 13 : x;
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
#if 1
    D("\n:DumpRamX\n");
    for (uint i = 0; i < 0x10000; i += 16) {
      for (uint j = 0; j < 16; j++) {
        if (Ram[i+j]) goto yyes;
      }
      continue;

yyes:
      D(":%04x: ", i);
      for (uint j = 0; j < 16; j++) {
        D("%c%02x", (Seen[i+j]? '-' : ' '), Ram[i+j]);
        if ((j&3)==3) D(" ");
      }
      D("\n");
    }
    D("\n:DumpRamX\n");


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
#else
    D("\n:DumpRam\n");
    for (uint i = 0; i < 0x10000; i += 16) {
      for (uint j = 0; j < 16; j++) {
        if (Ram[i+j]) goto yes;
      }
      continue;

yes:
      D(":%04x: ", i);
      for (uint j = 0; j < 16; j++) {
        D("%c%02x", (Seen[i+j]? '-' : ' '), Ram[i+j]);
        if ((j&3)==3) D(" ");
      }
      D("\n");
    }
    D("\n:DumpRam\n");
#endif
}
void DumpRamAndGetStuck() {
    DumpRam();
    GET_STUCK();
}

uint AddressOfRom() {
#if FOR_BASIC
    return 0x8000;
#else
    return IO_START - sizeof(Rom);
#endif
}

#if FOR_BASIC
#else
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
#endif

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

void SetMode(byte x) {
    D("mode %x\n", x);
    for (uint i = 18; i < 21; i++) {
        gpio_set_dir(i, true/*out*/);
        gpio_put(i, (1&x));
        x = x >> 1;
    }
}

void OutLowDataByte(byte x) {
    D("out %x\n", x);
    for (uint i = 0; i < 8; i++) {
        gpio_set_dir(i, true/*out*/);
        gpio_put(i, (1&x));
        x = x >> 1;
    }
}

void SetLatch(byte x) {
    D("latch %x\n", x);
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

    D("begin reset cpu ========\n");
    SetLatch(0x1C); // Reset & Halt.
    for (uint i = 0; i < 20; i++) {
        gpio_put(PIN_QBAR, 0); DELAY;
        gpio_put(PIN_EBAR, 0); DELAY;
        gpio_put(PIN_QBAR, 1); DELAY;
        gpio_put(PIN_EBAR, 1); DELAY;
    }
    D("begin halt cpu ========\n");
    SetLatch(0x1D); // Halt.
    for (uint i = 0; i < 10; i++) {
        gpio_put(PIN_QBAR, 0); DELAY;
        gpio_put(PIN_EBAR, 0); DELAY;
        gpio_put(PIN_QBAR, 1); DELAY;
        gpio_put(PIN_EBAR, 1); DELAY;
    }
    D("run ========\n");
    SetLatch(0x1F); // Run.
}

uint WAIT_GET() {
    const PIO pio = pio0;
    const uint sm = 0;
    const uint LED_PIN = PICO_DEFAULT_LED_PIN;

    // gpio_put(LED_PIN, 1);
    while (pio_sm_is_rx_fifo_empty(pio, sm)) {
        // P(",");
    }
    // DELAY;
    uint z = pio_sm_get(pio, sm);
    if(0) P("< %x\n", z);
    // gpio_put(LED_PIN, 0);
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

#if FOR_BASIC
byte row, col, plane;
bool GetKeysFromConsole() {
    putchar(C_KEY);

    byte x = xgetchar();
    switch (x) {

    case C_NOKEY:
        printf("C_NOKEY\n");
        row = 0;
        col = 0;
        plane = 0;
        return false;

    case C_KEY:
        row = xgetchar();
        col = xgetchar();
        plane = xgetchar();
        printf("C_KEY(%x, %x, %x)\n", row, col, plane);
        return true;

    default:
        printf("C_ default %x\n", x);
        D("----- EXPECTED C_KEY or C_NOKEY.  GOT %d.\n", x);
        DumpRamAndGetStuck();
        return false;

    }
}
#else

#if 0
#include "buffer.h"
Buffer InBuf;

byte XXXXXXXXXXXXXXXGetCharFromConsole() {
printf("\n{frodo ");
    InBuf.Dump();
printf("}\n");

    if (InBuf.Size() >= 2 && InBuf.Peek(0) == C_GETCHAR) {
        InBuf.Take();
        int x = InBuf.Take();
        D(" (( GOT CHAR %d. ))\n", x);
        return (x==10) ? 13 : x;  // turn LF to CR
    }
    return 0;
}
#endif

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

#if 0
byte OldGetCharFromConsole() {
    putchar(C_GETCHAR);
    int x = xgetchar();
    if (x != C_GETCHAR) {
                        D("----- EXPECTED GETCHAR.  GOT %d.\n", x);
                        DumpRamAndGetStuck();
    }
    x = xgetchar();
    D(" (( GOT CHAR %d. ))\n", x);
    return (x==10) ? 13 : x;  // turn LF to CR
}
#endif

#endif

int strikes;
void Strike(const char* why) {
    ++strikes;
    if (strikes >= 3) {
        D("------ Third Strike -------- %d\n", strikes);
        DumpRamAndGetStuck();
    }
}

void HandlePio(uint num_cycles, uint krn_entry) {
    const PIO pio = pio0;
    const uint sm = 0;

    byte data;
    uint vma = 0;
    uint start = 0;
    uint active = 0;
    uint num_resets = 0;
    uint event = 0;
    uint when = 0;
    uint arg = 0;
    uint prev = 0; // previous data byte
    interest = ATTENTION_SPAN;

    PUT(0x1F0000);  // 0:15 inputs; 16:21 outputs.
    PUT(0x000000);  // Data to put.

    for (uint cy = 0; !num_cycles || cy < num_cycles; cy++) {
#if FOR_BASIC
        if ((cy & TICK_MASK) == TICK_MASK) {
            GetKeysFromConsole();
        }
#else
        //if ((cy & TICK_MASK) == TICK_MASK) {
            //putchar('$');
            //InBuf.Tick();
        //}
#endif
        uint twenty_three = WAIT_GET();
        if (twenty_three != 23) {
            D("ERROR: NOT twenty_three: %d.\n", twenty_three);
            while (true) DELAY;
        }

        const uint addr = WAIT_GET();
        const uint flags = WAIT_GET();
        // printf("-- & %04x %04x & %s\n", addr, flags, start? "S" : "-");

        bool io = (active && 0xFF00 <= addr && addr <= /*0xFFEE*/ 0xFFFD);
        const bool reading = (flags & F_READ);

        if (false && !active && addr == 0xFFFF) { // CPU reads, Pico Tx
            Q("||\n");
        } else if (reading) { // CPU reads, Pico Tx

            data = Ram[addr];  // default behavior

            if (start && addr<0x0080) {
                    D("----- PC IN 00[0-7]x. Stopping addr=%x flags=%x\n", addr, flags);
                    DumpRamAndGetStuck();
                    return;
            }
            if (0xFF00 <= addr && addr < 0xFFEE) { // READ (GET) IO
                if (start) {
                        D("----- PC IN FFxx. Stopping addr=%x flags=%x\n", addr, flags);
                        DumpRamAndGetStuck();
                        return;
                }

                uint maddr = (addr < 0xFF40) ? (addr & 0xFFE3) : addr;  // masked addr for two partially-decoded PIAs
                if (maddr != addr) {
                    data = Ram[maddr];  // default behavior but with maddr
                }
                switch (0xFF & maddr) {
#if FOR_BASIC
                case 0xFF & 0xFF00: // Keyboard PIA
                  {
                    data = 0xFF;
                    Q("PIA0 reading: addr=%x maddr=%x\n", addr, maddr);
                    uint query = ~Ram[0xFF02];
                    if (col & query) {
                        data = ~row;
                        Q("PIA0 row=%x col=%x plane=%x query=%x col&query=%x data=%x\n", row, col, plane, query, col & query, data);
                        DumpRam();
                        data = 0xFF;
                    }
                    Ram[0xFF00] = data;
                  }
                    break;

                case 0xFF & 0xFF20: // Second PIA
                    Q("PIA1 reading: addr=%x maddr=%x data=%x", addr, maddr, data);
                    data = Ram[0xFF20] = 0;
                    break;

#else
                case 0xFF & CONSOLE_PORT: // getchar from console ($FF10)
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

                case 0xFF & (DISK_PORT+7): // read disk status
                    data = 1;  // 1 is OKAY
                    break;
#endif

                case 0xFF & 0xFFF0: // 6309 trap
                case 0xFF & 0xFFF1: // 6309 trap
                    D("----- 6309 ERROR TRAP.  Stopping.\n");
                    DumpRamAndGetStuck();
                    //return;
                    //data = Ram[addr];
                    break;

                default: // other reads from Ram.
                    data = Ram[addr];
                    break;
                }
            }

            if (start) {
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
                Q("%s %x %x  =%s #%d\n", (start ? (Seen[addr] ? "@" : "@@") : vma ? "r" : "-"), addr, data, HighFlags(high), cy);
            } else {
                Q("|\n");
            }

            if (addr == 0xFFFE) active = 1;

            if (start) {
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

        if (io) {
            interest = ATTENTION_SPAN;  // Interesting when it is I/O.
            switch (255&addr) {
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03:
                D("= PIA0: %04x %c\n", addr, reading? 'r': 'w');
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

#if FOR_BASIC
#else
            case 0x51:
            case 0x52:
            case 0x53:
            case 0x54:
            case 0x55:
            case 0x56:
            case 0x57:
            case 0x58:
            case 0x59:
            case 0x5A:
            case 0x5B:
            case 0x5C:
            case 0x5D:
            case 0x5E:
                Q("-NANDO %x %x %x\n", addr, data, Ram[data]);
                goto OKAY;
            case 0x5F: // Run Disk Command
                if (!reading) {
                    byte command = Ram[0xFF58];

                    Q("-NANDO %x %x %x\n", addr, data, Ram[data]);
                    Q("- sector $%02x.%04x bufffer $%04x diskop %x\n",
                        Ram[0xFF59],
                        PEEK2(0xFF5A),
                        PEEK2(0xFF5C),
                        command);

                    uint lsn = PEEK2(0xFF5A);
                    byte* dp = Disk + 256*lsn;
                    uint buffer = PEEK2(0xFF5C);
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
#endif

            default: {}
            }
            D("--- IOPAGE %s: addr %x flags %x data %x\n", reading? "READ": "WRITE", addr, flags, data);
            // DumpRamAndGetStuck();
        }
OKAY:

        vma = 0 != (flags & F_AVMA);   // AVMA bit means next cycle is Valid
        start = 0 != (flags & F_LIC); // LIC bit means next cycle is Start

        if (interest > 0) {
            interest--;
            if (!interest) {
                D("...\n");
                D("...\n");
                D("...\n");
            }
        }
    }
}

void InitRamFromRom() {
#if FOR_BASIC
    uint rom_base = 0x8000;
    for (uint i = 0; i < sizeof BasicRom; i++) {
        Ram[rom_base + i] = BasicRom[i];
    }
#else
    uint addr = AddressOfRom();
    for (uint i = 0; i < sizeof Rom; i++) {
        Ram[addr + i] = Rom[i];
    }
#endif
}

#if 0
int XXXmain() {
    stdio_init_all();
    while (true) {
        int x = getchar_timeout_us(0);
        if (x>0) {
            printf("%d[%c] ", x, x);
        } else {
            putchar('~');
        }
    }
}
#endif

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

    gpio_put(LED_PIN, 1);
    ResetCpu();
    gpio_put(LED_PIN, 0);
    sleep_us(20);
    InitRamFromRom();

    // Set interrupt vectors
#if FOR_BASIC
    for (uint j = 0; j < 16; j++) {
        printf("  to:%04x peek:%04x\n", 0xFFF0+j, PEEK(0xBFF0+j));
        POKE(0xFFF0+j, PEEK(0xBFF0+j));
        printf("  at:%04x peek:%04x\n", 0xFFF0+j, PEEK(0xFFF0+j));
    }
#else
    uint a = AddressOfRom();
    D("AddressOfRom = $%x\n", a);
    uint krn_start, krn_entry, krn_end;
    FindKernelEntry(&krn_start, &krn_entry, &krn_end);
    D("KernelEntry = $%x\n", krn_entry);

    for (uint j = 1; j < 7; j++) {
        POKE2(0xFFF0+j+j, Vectors[j]);
    }
    POKE2(0xFFFE, PRELUDE_START);       // Reset Vector.
#endif

    StartPio();
#if FOR_BASIC
    HandlePio(1000 * 1000 * 1000, 0);
#else
    HandlePio(0, krn_entry);
#endif
    sleep_ms(100);
    D("\nFinished.\n");
    sleep_ms(100);
    GET_STUCK();
}
