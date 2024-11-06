#define FOR_BASIC 0
#define TRACKING 1
#define ALL_POKES 1
#define  BORING_SWI2S  160

// tmanager.cpp -- for the TFR/901 -- strick
//
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"


#include <pico/time.h>
#include <hardware/timer.h>
#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/structs/systick.h>
#include <hardware/exception.h>
volatile uint TimerTicks;
volatile bool TimerFired;

typedef unsigned char byte;
typedef unsigned int word;
typedef unsigned char T_byte;
typedef unsigned int T_word;
typedef unsigned char T_16 [16];

extern void putbyte(byte x);
extern void SendIn_byte(byte x);
extern void SendIn_word(word x);
extern void SendIn_16(byte* x);
extern void RecvOut_byte(byte* x);

// Configuration
#include "tfr9ports.gen.h"

// PIO code for the primary pico
#include "tpio.pio.h"

// LED for Pico W:   LED(1) for on, LED(0) for off.
#define LED(X) cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (X))

#define ATTENTION_SPAN 25 // was 250
#define VSYNC_TICK_MASK 0x3FFF // 0xFFFF   // one less than a power of two
#define ACIA_TICK_MASK 0x3FFF // 0xFFF     // one less than a power of two

#define TFR_RTC_BASE 0xFF50

#if TRACKING

    #define USE_TIMER 0
    #define D if(interest)printf
    #define P if(interest)printf
    #define V if(interest)printf
    #define Q if(interest)printf

#else

    #define USE_TIMER 1
    #define D if(0)printf
    #define P if(0)printf
    #define V if(0)printf
    #define Q if(0)printf

#endif

uint interest;
uint btbug;

#define MAX_INTEREST 999999999
#define getchar(X) NeverUseGetChar

/////////// #include "../generated/rpc.h"

const byte Rom[] = {
#if N9_LEVEL == 1
  #include "level1.rom.h"
#endif
#if N9_LEVEL == 2
  #include "level2.rom.h"
#endif
};

byte Disk[] = {
#if N9_LEVEL == 1
  #include "level1.disk.h"
#endif
#if N9_LEVEL == 2
  #include "level2.disk.h"
#endif
};

byte Seen[0x10000];

#define LEVEL1_PRELUDE_START (0xFF00 - 32)

uint FFFxVectors[] = {
#if N9_LEVEL == 1
     0,  //  6309 TRAP
     // From ~/coco-shelf/toolshed/cocoroms/bas13.rom :
     0x0100, 0x0103, 0x010f, 0x010c, 0x0106, 0x0109,
     LEVEL1_PRELUDE_START ,  //  RESET
#endif
#if N9_LEVEL == 2
     // From ~/coco-shelf/toolshed/cocoroms/coco3.rom :
0x0000, // 6309 traps
0xFEEE,
0xFEF1,
0xFEF4,
0xFEF7,
0xFEFA,
0xFEFD,
0x2602,
#endif
};

enum {
    // C_NOCHAR=160,
    C_PUTCHAR=161,
    // C_GETCHAR=162,
    C_STOP=163,
    C_ABORT=164,
    C_KEY=165,
    C_NOKEY=166,
    C_DUMP_RAM=167,
    C_DUMP_LINE=168,
    C_DUMP_STOP=169,
    C_DUMP_PHYS=170,
    C_POKE=171,
    C_EVENT=172,
    //
    EVENT_PC_M8=238,
    EVENT_GIME=239,
    EVENT_RTI=240,
    EVENT_SWI2=241,
    EVENT_CC=242,
    EVENT_D=243,
    EVENT_DP=244,
    EVENT_X=245,
    EVENT_Y=246,
    EVENT_U=247,
    EVENT_PC=248,
    EVENT_SP=249,
};

#define DELAY sleep_us(1);

#define F_READ 0x0100
#define F_AVMA 0x0200
#define F_LIC  0x0400
#define F_BA   0x0800
#define F_BS   0x1000
#define F_BUSY 0x2000

#define F_HIGH (F_BA|F_BS|F_BUSY)

const byte RESET_BAR_PIN = 21;  // negative logic
const byte IRQ_BAR_PIN = 22;    // negative logic

#include "ram.inc.h"

// putbyte does CR/LF escaping for Binary Data
void putbyte(byte x) {
    putchar_raw(x);
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

constexpr uint TRACE_SIZE = 1024;
constexpr uint TRACE_MASK = TRACE_SIZE - 1;

struct TraceRec {
    word addr;
    byte block;
    byte flags;
    byte data;
} Trace[TRACE_SIZE];

uint NextTraceIndex;
void Record(word addr, byte flags, byte data) {
    auto* p = Trace + NextTraceIndex;
    p->addr = addr;
    p->block = the_ram.Block(addr);
    p->flags = flags;
    p->data = data;
    ++NextTraceIndex;
    NextTraceIndex &= TRACE_MASK;
}
void DumpTrace() {
    bool vma = false; // Valid Memory Address ( = delayed AVMA )
    bool fic = false; // First Instruction Cycle ( = delayed LIC )

    printf("\nDumpTrace(((((\n");
    for (uint i = 0; i < TRACE_SIZE; i++) {
        auto* p = Trace + ((i + NextTraceIndex) & TRACE_MASK);
        if (p->addr || p->flags || p->data) {
            // printf("[%4d] %04d %02d %02d\n", TRACE_SIZE-i, p->addr, p->flags, p->data);
            const char* rw = (p->flags & (F_READ>>8)) ? "r" : "w";
            printf("%s %04x %02x ^%02x #T:%d\n", (fic ? "@" : vma ? rw : "-"), p->addr, p->data, p->flags, i);
        }

        vma = 0 != (p->flags & (F_AVMA>>8));   // AVMA bit means next cycle is Valid
        fic = 0 != (p->flags & (F_LIC>>8)); // LIC bit means next cycle is First Instruction Cycle
    }
    printf("DumpTrace)))))\n");
}

void DumpRamText() {
#if 0
    Quiet();
    printf("DumpRamText(((\n");
    for (uint i = 0; i < 0x10000; i += 16) {
        for (uint j = 0; j < 16; j++) {
            if (Peek(i+j)) goto yes;
        }
        continue;
yes:
        printf(" %06x:", i);
        for (uint j = 0; j < 16; j++) {
            printf(" %02x", Peek(i+j));
        }
        printf("\n");
    }
    printf("DumpRamText)))\n");
    Noisy();
#endif
}

void DumpPhys() {
#if 1
    Quiet();
    putbyte(C_DUMP_PHYS);
    uint sz = the_ram.PhysSize();
    for (uint i = 0; i < sz; i += 16) {
        for (uint j = 0; j < 16; j++) {
            if (the_ram.ReadPhys(i+j)) goto yes;
        }
        continue;
yes:
        putbyte(C_DUMP_LINE);
        putbyte(i>>16);
        putbyte(i>>8);
        putbyte(i);
        for (uint j = 0; j < 16; j++) {
            putbyte(the_ram.ReadPhys(i+j));
        }
    }
    putbyte(C_DUMP_STOP);
    Noisy();
#endif
}

void DumpRam() {
#if 1
    Quiet();
    putbyte(C_DUMP_RAM);
    for (uint i = 0; i < 0x10000; i += 16) {
        for (uint j = 0; j < 16; j++) {
            if (Peek(i+j)) goto yes;
        }
        continue;
yes:
        putbyte(C_DUMP_LINE);
        putbyte(i>>16);
        putbyte(i>>8);
        putbyte(i);
        for (uint j = 0; j < 16; j++) {
            putbyte(Peek(i+j));
        }
    }
    putbyte(C_DUMP_STOP);
    Noisy();
#endif
}

void GET_STUCK() {
    while (1) {
        putbyte(255);
        sleep_ms(1000);
    }
}

void DumpRamAndGetStuck(const char* why) {
    interest = MAX_INTEREST;
    printf("\n(((((((((([[[[[[[[[[{{{{{{{{{{\n");
    printf("DumpRamAndGetStuck: %s\n", why);
    DumpTrace();
    DumpRamText();
    DumpPhys();
    DumpRam();
    printf("\n}}}}}}}}}}]]]]]]]]]]))))))))))\n");
    GET_STUCK();
}

uint AddressOfRom() {
#if N9_LEVEL == 1
  return 0xFF00 - sizeof(Rom);
#endif
#if N9_LEVEL == 2
  return 0x2600;
#endif
}

bool CheckHeader(uint p) {
    uint z = 0;
    for (uint i = 0; i < 9; i++) {
        z ^= Rom[i+p];
    }
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

void Level1FindKernelEntry(uint *krn_start, uint *krn_entry, uint *krn_end) {
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
#if 0
#if TRACKING
    Quiet();
    uint addr = (hi << 8) | lo;
    V("=== %s: @%04x: ", label, addr);
    for (uint i = 0; i < 8; i++) {
        uint x = Peek2(addr+i+i);
        V("%04x ", x);
    }
    V("|");
    for (uint i = 0; i < 16; i++) {
        byte ch = 0x7f & Peek(addr+i);
        if (32 <= ch && ch <= 126) {
            V("%c", ch);
        } else if (ch==0) {
            V("-");
        } else {
            V(".");
        }
    }
    V("|\n");
    Noisy();
#endif
#endif
}

void ResetCpu() {
    for (uint i = 0; i <= 20; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
    }
    // 903:
    constexpr uint control_pins[] = {21, 22, 26, 27, 28};
    for (uint p : control_pins) {
        gpio_init(p);
        gpio_put(p, 1);
        gpio_set_dir(p, GPIO_OUT);
        gpio_put(p, 1);
    }

    const uint PIN_E = 16; // clock output pin
    const uint PIN_Q = 17; // clock output pin

    D("begin reset cpu ========\n");
    gpio_put(RESET_BAR_PIN, not true);  // negative logic
    const uint EnoughCyclesToReset = 60;
    for (uint i = 0; i < EnoughCyclesToReset; i++) {
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

    // TODO: waiting should not be needed.
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

#if 0
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
#endif

void Strike(const char* why) {
    interest += 100;
    static int strikes;
    ++strikes;
    D("------ Strike %d -------- %s\n", strikes, why);
    if (strikes >= 20) {
        D("------ 20th Strike -------- %s\n", why);
        DumpRamAndGetStuck("20th strike");
    }
}

void ShowChar(byte ch) {
    putchar(C_PUTCHAR);
    putchar(ch);
}

bool gime_irq_enabled;
bool gime_vsync_irq_enabled;
bool gime_vsync_irq_firing;

bool vsync_irq_enabled;
bool vsync_irq_firing;

bool acia_irq_enabled;
bool acia_irq_firing;
bool acia_char_in_ready;
int acia_char;

class CircBuf {
    private:
        const static uint N = 1234;
        byte buf[N];
        uint nextIn, nextOut;
    public:
        CircBuf() : nextIn(0), nextOut(0) {}

        uint BytesAvailable() {
            if (nextOut <= nextIn) return nextIn - nextOut;
            else return nextIn + N - nextOut;
        }
        bool Has(uint n) {
            uint ba = BytesAvailable();
            // printf("%u", ba);
            return ba >= n;
        }
        byte Peek() {
            return buf[nextOut];
        }
        byte Take() {
            //ShowChar('~');
            byte z = buf[nextOut];
            ++nextOut;
            if (nextOut >= N) nextOut = 0;
            return z;
        }
        void Put(byte x) {
            //ShowChar('`');
            buf[nextIn] = x;
            ++nextIn;
            if (nextIn >= N) nextOut = 0;
        }
};


void PutIrq(bool activate) {
    // LED(activate);
    gpio_put(IRQ_BAR_PIN, not activate);  // negative logic
}

void SendEventRam(byte event, byte sz, word base_addr) {
    Quiet();
    putbyte(C_EVENT);
    putbyte(event);
    putbyte(sz);
    putbyte(base_addr >> 8);
    putbyte(base_addr);
    for (byte i=0; i < sz; i++) {
        putbyte(Peek(base_addr+i));
    }
    Noisy();
}

extern "C" {
    extern int stdio_usb_in_chars(char *buf, int length);
}

void HandlePio(uint num_cycles, uint krn_entry) {
    interest += 100;
    interest = MAX_INTEREST; /// XXX

    const PIO pio = pio0;
    const uint sm = 0;

    CircBuf usb_input;

    byte data;
    byte rtc_value;
    bool vma = false; // Valid Memory Address ( = delayed AVMA )
    bool fic = false; // First Instruction Cycle ( = delayed LIC )
    uint active = 0;
    uint num_resets = 0;
    uint event = 0;
    uint when = 0;
    uint arg = 0;
    uint prev = 0; // previous data byte
    uint num_swi2s = 0;

    PUT(0x1F0000);  // 0:15 inputs; 16:21 outputs.
    PUT(0x000000);  // Data to put.

    for (uint cy = 0; true; cy++) {
        if ((cy & ACIA_TICK_MASK) == ACIA_TICK_MASK) {
            //ShowChar('[');

            {
                char just_one[1] = {0};
                int rc = stdio_usb_in_chars(just_one, sizeof just_one);
                    (void)rc;

                if (just_one[0]) {
                    usb_input.Put((byte)just_one[0]);
                }
            }

            if (not acia_char_in_ready) {
                        int next = usb_input.Has(1) ? (int)usb_input.Peek() : -1;
                        // printf("xxx next %d\n", next);
                        if (1 <= next && next <= 126) {
                                acia_char = usb_input.Take();
                                if (acia_char == 10) {
                                    acia_char = 13;
                                    //ShowChar('$');
                                }
                                acia_char_in_ready = true;
                                acia_irq_firing = true;
                                ShowChar('+');
                                //ShowChar(acia_char);
                        } else {
                                acia_char = 0;
                                acia_char_in_ready = false;
                                acia_irq_firing = false;
                                //ShowChar('-');
                        }
            }
        }


#if USE_TIMER
        if (TimerFired)
#else
        if ((cy & VSYNC_TICK_MASK) == VSYNC_TICK_MASK)
#endif
        {
            ShowChar('`');

            TimerFired = false;
            Poke(0xFF03, Peek(0xFF03) | 0x80);  // Set the bit indicating VSYNC occurred.
            if (vsync_irq_enabled) {
                vsync_irq_firing = true;
            }
            if (gime_irq_enabled && gime_vsync_irq_enabled) {
                gime_vsync_irq_firing = true;
            }

            // GIME Interrupts:

            // INIT0 = $ff90
            //    Bit 5 of FF90 must be set to enable GIME IRQs.
            //    Bit 4 of FF90 must be set to enable GIME FIRQs.
            // Our clock writes 6C to INIT0 (sets bit 5, not bit 4).

            // IRQEN = $ff92
            //    Bit 5 of FF92 enables TIMER irq.
            //    Bit 3 of FF92 enables VSYNC irq.
            //    Bit 1 of FF92 enables KBD/JOY irq.
            //    Bit 0 of FF92 enables CART irq.
            //    Reading FF92 clears the IRQs.
            // Our clock writes 00, 08, and 09 to IRQEN.
            //    (uses only VSYNC and CART irqs)
        }

        const uint addr = WAIT_GET();
        const uint flags = WAIT_GET();

        bool io = (active && 0xFF00 <= addr && addr <= /*0xFFEE*/ 0xFFFD);
        const bool reading = (flags & F_READ);

        if (not reading && 0xFF00 <= addr && addr < 0xFFF0) { // IO is interesting.
                    interest += 10;
        }

        if (reading) { // CPU reads, Pico Tx
            // q1: Reading.
            data = Peek(addr);  // default behavior

            if (active && fic && addr<0x0050) {
                    interest += 100;
                    D("----- PC IN 00[0-4]x. Stopping addr=%x flags=%x\n", addr, flags);
                    DumpRamAndGetStuck("pc");
                    return;
            }
            if (0xFF00 <= addr && addr < 0xFFF0) { // READ (GET) IO
                if (active && fic) {
                    interest += 100;
                        D("----- PC IN FFxx. Stopping addr=%x flags=%x\n", addr, flags);
                        DumpRamAndGetStuck("pc");
                        return;
                }

                switch (0xFF & addr) {

                // Read PIA0
                case 0x00:
                    D("----- PIA0 Read not Impl: %x\n", addr);
                    data = 0xFF;  // say like, no key pressed
                    break;
                case 0x01:
                    D("----- PIA0 Read not Impl: %x\n", addr);
                    DumpRamAndGetStuck("pia0");
                    break;
                case 0x02:
                    Poke(0xFF03, Peek(0xFF03) & 0x7F);  // Clear the bit indicating VSYNC occurred.
                    vsync_irq_firing = false;
                    data = 0xFF;
                    break;
                case 0x03:
                    // OK to read, for the HIGH bit, which tells if VSYNC ocurred.
                    break;

                case 0x92: // GIME IRQEN register
                    {
                        if (gime_irq_enabled && gime_vsync_irq_enabled && gime_vsync_irq_firing) {
                            data = 0x08;
                            gime_vsync_irq_firing = false; // Reading this register clears the IRQ.
                        } else {
                            data = 0;
                        }
                    }
                    break;

                case 255&(ACIA_PORT+0): // read ACIA control/status port
                    {
                        data = 0x02;  // Transmit buffer always considered empty.
                        data |= (acia_irq_firing) ? 0x80 : 0x00;
                        data |= (acia_char_in_ready) ? 0x01 : 0x00;

                        acia_irq_firing = false;  // Side effect of reading status.
                    }
                    break;

                case 255&(ACIA_PORT+1): // read ACIA data port
                    if (acia_char_in_ready) {
                        data = acia_char;
                        acia_char_in_ready = false;
                    } else {
                        data = 0;
                    }
                    break;

                case 0xFF & (BLOCK_PORT+7): // read disk status
                    data = 1;  // 1 is OKAY
                    break;

                case 0xFF & (EMUDSK_PORT+3): // read disk status
                    data = 0;  // 0 is OKAY
                    break;

                case 0xFF & (TFR_RTC_BASE+0):
                    data = rtc_value;
                    break;

                default:
                    data = Peek(addr);
                    break;
                }
            }

            if (fic) {
                if (addr == krn_entry) {
                    num_resets++;
                    if (num_resets >= 2) {
                        D("---- Second Reset -- Stopping.\n");
                        DumpRamAndGetStuck("resets");
                        return;
                    }
                }

                switch (data) {
                case 0x00:
                    Strike("suspicious opcode 0");
                    break;
                case 0x01:
                    Strike("illegal opcode 1");
                    break;
                case 0x02:
                    // Disable case 0x02 because IRQ causes reads to FFFF which is 02
                    // to be confused as executing opcode 2.
                    // Strike("illegal opcode 2");
                    break;
                case 0x05:
                case 0x0B:
                case 0x14:
                case 0x15:
                case 0x18:
                case 0x1B:
                case 0x38:
                case 0x3E:

                case 0x41:
                case 0x42:
                case 0x45:
                case 0x4B:
                case 0x4E:

                case 0x51:
                case 0x52:
                case 0x55:
                case 0x5B:
                case 0x5E:

                case 0x61:
                case 0x62:
                case 0x65:
                case 0x6B:

                case 0x71:
                case 0x72:
                case 0x75:
                case 0x7B:

                case 0x87:
                case 0x8F:

                case 0xC7:
                case 0xCD:
                case 0xCF:
                    Strike("illegal opcode");
                    break;

                case 0x10:
                case 0x20:
                case 0x3B:
                    event = data;
                    when = cy;
                    break;
                case 0x9D:
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
#if 0
                Q("%s %04x %02x  =%s #%d\n", (fic ? "@" : vma ? "r" : "-"), addr, data, HighFlags(high), cy);
#else
                Q("%s %04x %02x  =%s #%d\n", (fic ? (Seen[addr] ? "@" : "@@") : vma ? "r" : "-"), addr, data, HighFlags(high), cy);
#endif
            } else {
                Q("|\n");
            }

            if (addr == 0xFFFE) {
                active = 1;
            }
#if 1
            if (fic) {
                if (!Seen[addr]) {
                    Seen[addr] = 1;
                }
            }
#endif
            switch (event) {
            case 0x103f:
                if (cy - when == 2) {
                    printf("--- OS9 $%02x =%d. ---\n", data, data);
                }

            case 0x10: // prefix $10
                if (cy - when == 1) {
                    switch (data) {
                    case 0x3f: // SWI2
                        event = (event << 8) | data;
                        interest += 50;
                        printf("--- SWI2 ---\n");

                        ++num_swi2s;
                        if (num_swi2s > BORING_SWI2S) {
                            interest = MAX_INTEREST;
                            printf("--- MAX_INTEREST ---\n");
                        }
                    }
                }
                break;
            case 0x20:  // BRA: branch always
                if (cy - when == 1) {
                    switch (data) {
                    case 0xFE: // Infinite Loop
                        D("------- Infinite Loop.  Stopping.\n");
                        DumpRamAndGetStuck("infinite");
                        return;
                        break;
                    }
                }
                break;
            case 0x9D:  // JMP Direct
                if (cy - when == 1) {
                    switch (data) {
                    case 0x5E: // jsr <D.BtBug
                        interest += 50;
                        ++btbug;
                        D("------- jsr D.BtBug %d.\n", btbug);
                        break;
                    }
                }
                break;
            case 0x3B:  // RTI
                {
                    constexpr uint SZ = 20;
                    static byte hist[SZ];
                    interest += 50;
                    uint age = cy - when;
                    if (age < SZ) {
                        hist[age] = data;
                    }
                    switch (age) {
                    case 2:
                        arg = 0x80 & data;  // "entire" bit of condition codes
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
                        SendEventRam(EVENT_D, 16, Peek2(addr-14+3));
                        SendEventRam(EVENT_X, 16, Peek2(addr-14+6));
                        SendEventRam(EVENT_Y, 16, Peek2(addr-14+8));
                        SendEventRam(EVENT_U, 16, Peek2(addr-14+10));
                        SendEventRam(EVENT_PC, 16, Peek2(addr-14+12));
                        SendEventRam(EVENT_PC_M8, 16, Peek2(addr-14+12)-8);
                        SendEventRam(EVENT_GIME, 32, 0xFF90);
                        SendEventRam(EVENT_RTI, 12, addr-12);
                        break;
                    }
                }
            }

        } else {  // CPU writes, Pico Rx

            // q2: Writing
            data = WAIT_GET();
            if (active) {
#if 0
                putbyte(C_POKE);
                putbyte(the_ram.Block(addr));
                putbyte(addr >> 8);
                putbyte(addr);
                putbyte(data);
#endif

#if 0
                if (addr==0 && data==0) {
                    Strike("writing 0 to 0");
                }
#endif
                if (FOR_BASIC && (0x8000 <= addr && addr < 0xFF00)) {
                    // Writing to ROM space
                    Q("wROM %04x %02x\n", addr, data);
                } else {
                    // Writing to RAM or IO space
                    Poke(addr, data);
                    Q("w %04x %02x\n", addr, data);
                }

                switch (event) {
                case 0x103f:  // SWI2 (i.e. OS9 kernel call)
                    {
                        constexpr uint SZ = 20;
                        static byte hist[SZ];

                        uint age = cy - when;
                        if (age < SZ) {
                            hist[age] = data;
                        }

                        switch (age) {
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
                            {

                                ViewAt("SP", addr>>8, addr);

                        SendEventRam(EVENT_D, 16, Peek2(addr+15-14));
                        SendEventRam(EVENT_X, 16, Peek2(addr+15-11));
                        SendEventRam(EVENT_Y, 16, Peek2(addr+15-9));
                        SendEventRam(EVENT_U, 16, Peek2(addr+15-7));
                        SendEventRam(EVENT_PC, 16, Peek2(addr+15-5));
                        SendEventRam(EVENT_GIME, 32, 0xFF90);
                        SendEventRam(EVENT_SWI2, 12, addr);
                                //putbyte(C_EVENT);
                                //putbyte(EVENT_SWI2);
                                //putbyte(SZ);
                                //for (uint i=0; i < SZ; i++) putbyte(hist[i]);
                            }
                            break;
                        }
                    } // end 0x103f SWI2
                    break;
                } // end switch (event)
            } // end if active
        }  // end if read / write
        prev = data;

        if (vma || addr != 0xFFFF) {
            Record(addr, (byte)(flags>>8), data);
        }

        switch (event) {
/*
        case 0x103f:  // SWI2 (i.e. OS9 kernel call)
            DumpRamAndGetStuck("SWI2 event");
            break;
*/
        }

        // q3: Side Effects after Reading or Writing.
        if (io) {

            switch (255&addr) {
            case 0x00:
            case 0x01:
            case 0x02:
                D("= PIA0: %04x %c\n", addr, reading? 'r': 'w');
                goto OKAY;
            case 0x03:
                // Read PIA0
                D("= PIA0: %04x %c\n", addr, reading? 'r': 'w');
                if (not reading) {
                    vsync_irq_enabled = bool ((data&1) != 0);
                }
                goto OKAY;

            case 0x90: // GIME INIT0
                if (not reading) {
                    gime_irq_enabled = bool((data & 0x20) != 0);
                }
                goto OKAY;

            case 0x92: // GIME IRQEN
                if (not reading) {
                    gime_vsync_irq_enabled = bool((data & 0x08) != 0);
                }
                goto OKAY;

            case 255&(ACIA_PORT+0): // control port
                if (reading) {  // reading control prot
                    {}
                } else { // writing control port:
                    if ((data & 0x03) != 0) {
                      acia_irq_enabled = false;
                    }

                    if ((data & 0x80) != 0) {
                      acia_irq_enabled = true;
                    } else {
                      acia_irq_enabled = false;
                    }
                }
                goto OKAY;

            case 255&(ACIA_PORT+1): // data port
                if (reading) {  // reading data prot
                } else { // writing data port:
                    putbyte(C_PUTCHAR);
                    putbyte(data);
                }
                goto OKAY;

            case 255&(BLOCK_PORT+0): // Save params for Disk.
            case 255&(BLOCK_PORT+1):
            case 255&(BLOCK_PORT+2):
            case 255&(BLOCK_PORT+3):
            case 255&(BLOCK_PORT+4):
            case 255&(BLOCK_PORT+5):
            case 255&(BLOCK_PORT+6):
                Q("-BLOCK %x %x %x\n", addr, data, Peek(addr));
                goto OKAY;
            case 255&(BLOCK_PORT+7): // Run Disk Command.
                if (!reading) {
                    byte command = Peek(BLOCK_PORT + 0);

                    Q("-NANDO %x %x %x\n", addr, data, Peek(data));
                    Q("- sector $%02x.%04x bufffer $%04x diskop %x\n",
                        Peek(BLOCK_PORT + 1),
                        Peek2(BLOCK_PORT + 2),
                        Peek2(BLOCK_PORT + 4),
                        command);

                    uint lsn = Peek2(BLOCK_PORT + 2);
                    byte* dp = Disk + 256*lsn;
                    uint buffer = Peek2(BLOCK_PORT + 4);

                    Q("- VARS sector $%04x bufffer $%04x diskop %x\n", lsn, buffer, command);

                    switch (command) {
                    case 0: // Disk Read
                        for (uint k = 0; k < 256; k++) {
                            Poke(buffer+k, dp[k]);
                        }
                        goto OKAY;
                    case 1: // Disk Write
                        for (uint k = 0; k < 256; k++) {
                            dp[k] = Peek(buffer+k);
                        }
                        goto OKAY;
                    default: {
                        printf("\nwut command %d.\n", command);
                        DumpRamAndGetStuck("wut");
                        }
                    }
                }
                goto OKAY;

            case 255&(EMUDSK_PORT+0): // LSN(hi)
            case 255&(EMUDSK_PORT+1): // LSN(mid)
            case 255&(EMUDSK_PORT+2): // LSN(lo)
            case 255&(EMUDSK_PORT+4): // buffer addr
            case 255&(EMUDSK_PORT+5):
            case 255&(EMUDSK_PORT+6): // drive number
                Q("-EMUDSK %x %x %x\n", addr, data, Peek(addr));
                goto OKAY;
            case 255&(EMUDSK_PORT+3): // Run EMUDSK Command.
                if (!reading) {
                    byte command = data;

                    Q("-NANDO %x %x %x\n", addr, data, Peek(data));
                    Q("- sector $%02x.%04x bufffer $%04x diskop %x\n",
                        Peek(EMUDSK_PORT + 0),
                        Peek2(EMUDSK_PORT + 1),
                        Peek2(EMUDSK_PORT + 4),
                        command);

                    uint lsn = Peek2(EMUDSK_PORT + 1);
                    byte* dp = Disk + 256*lsn;
                    uint buffer = Peek2(EMUDSK_PORT + 4);

                    Q("- VARS sector $%04x bufffer $%04x diskop %x\n", lsn, buffer, command);

                    switch (command) {
                    case 0: // Disk Read
                        for (uint k = 0; k < 256; k++) {
                            Poke(buffer+k, dp[k]);
                        }
                        goto OKAY;
                    case 1: // Disk Write
                        for (uint k = 0; k < 256; k++) {
                            dp[k] = Peek(buffer+k);
                        }
                        goto OKAY;
                    default:
                        printf("\nwut emudsk command %d.\n", command);
                        DumpRamAndGetStuck("wut emudsk");
                    }
                }
                goto OKAY;

            case 0xFF & (TFR_RTC_BASE+1):
                switch (data) {
                  case 0: rtc_value = 0; break; // Sec
                  case 1: rtc_value = 3; break; // Sec (10)
                  case 2: rtc_value = 5; break; // Min
                  case 3: rtc_value = 1; break; // Min (10)

                  case 4: rtc_value = 2; break; // Hour
                  case 5: rtc_value = 1; break; // Hour (10)

                  case 6: rtc_value = 5; break; // Day of month
                  case 7: rtc_value = 2; break; // Day of month (10)

                  case 8: rtc_value = 2; break; // Month 1-12
                  case 9: rtc_value = 1; break; // Month (10)

                  case 10: rtc_value = 4; break;  // Year - 1900
                  case 11: rtc_value = 2; break;  // (10)
                  case 12: rtc_value = 1; break;  // (100)

                  default: rtc_value = data; break;
                }
                goto OKAY;
                
            default: {}
            }
            D("--- IOPAGE %s: addr %x flags %x data %x\n", reading? "READ": "WRITE", addr, flags, data);
        }
OKAY:

        vma = 0 != (flags & F_AVMA);   // AVMA bit means next cycle is Valid
        fic = 0 != (flags & F_LIC); // LIC bit means next cycle is First Instruction Cycle

        {
            static bool prev_irq_needed;
            bool irq_needed =
                (vsync_irq_enabled && vsync_irq_firing)
                || (acia_irq_enabled && acia_irq_firing)
                || (gime_irq_enabled && gime_vsync_irq_enabled && gime_vsync_irq_firing)
                ;

            if (irq_needed != prev_irq_needed) {
                PutIrq(irq_needed);
#if TRACKING
                printf("irq_needed=%d\n", irq_needed);
#endif
                prev_irq_needed = irq_needed;
            }
        }

        if (interest && interest < MAX_INTEREST) {
            --interest;
            if (not interest) {
                printf("\n\n...\n... SKIPPING\n...\n\n");
            }
        }
    } // next cy
}

void InitRamFromRom() {
    Quiet();
    uint start = AddressOfRom();
    for (uint i = 0; i < sizeof Rom; i++) {
        uint addr = start + i;
        Poke(addr, Rom[i]);

#if 0
        putbyte(C_POKE);
        putbyte(the_ram.Block(addr));
        putbyte(addr >> 8);
        putbyte(addr);
        putbyte(Rom[i]);
#endif
    }
    Noisy();
}

struct repeating_timer TimerData;

bool TimerCallback(repeating_timer_t *rt) {
    TimerTicks++;
    TimerFired = true;
    return true;
}

int main() {
    interest = MAX_INTEREST; /// XXX
    stdio_usb_init();
    // stdio_init_all();
    cyw43_arch_init();

    quiet_ram = 888;

    const uint BLINKS = 3; // Initial LED blink countdown.
    for (uint i = BLINKS; i > 0; i--) {
        LED(1);
        sleep_ms(500);
        LED(0);
        sleep_ms(500);
        printf("+%d+ ", i);

        char pbuf[10];
        sprintf(pbuf, "+%d+ ", BLINKS - i);
        printf("%s", pbuf);
        for (const char*p = pbuf; *p; p++) {
            putchar(C_PUTCHAR);
            putchar(*p);
        }
    }
    ShowChar('+');

    LED(1);

    the_ram.Reset();
#if N9_LEVEL == 2
    printf("COCO3: Prepare for Level 2\n");
#if 0
    //the_ram.SetEnableMmu(true);
    //the_ram.SetCurrentTask(1);
    //Poke(0xFF90, 0x40);
    //Poke(0xFF91, 0x01);
#endif
    Poke(0x5E, 0x39); // RTS for D.BtBug // TODO
#endif

    ShowChar('X');
    printf("stage-X\n");
    InitRamFromRom();
    ShowChar('Y');
    printf("stage-Y\n");
    ResetCpu();
    ShowChar('Z');
    printf("stage-Z\n");

#if N9_LEVEL == 1
    uint a = AddressOfRom();
    D("AddressOfRom = $%x\n", a);
    uint krn_start, krn_entry, krn_end;
    Level1FindKernelEntry(&krn_start, &krn_entry, &krn_end);
    D("Level1KernelEntry = $%x\n", krn_entry);
#endif

    // Set interrupt vectors
    for (uint j = 0; j < 8; j++) {
        Poke2(0xFFF0+j+j, FFFxVectors[j]);
    }

    ShowChar('+');
    ShowChar('\n');
    LED(0);
    printf("\nStartPio()\n");
    StartPio();

    //---- thanks https://forums.raspberrypi.com/viewtopic.php?t=349809 ----//
    //-- systick_hw->csr |= 0x00000007;  //Enable timer with interrupt
    //-- systick_hw->rvr = 0x00ffffff;         //Set the max counter value (when the timer reach 0, it's set to this value)
    //-- exception_set_exclusive_handler(SYSTICK_EXCEPTION, SysTickINT);	//Interrupt

    // ( pico-sdk/src/common/pico_time/include/pico/time.h )
    // Note: typedef bool (*repeating_timer_callback_t)(repeating_timer_t *rt);
    // Note: static inline bool add_repeating_timer_ms(int32_t delay_ms, repeating_timer_callback_t callback, void *user_data, repeating_timer_t *out)
    alarm_pool_init_default();
    add_repeating_timer_us(16666 /* 60 Hz */, TimerCallback, nullptr, &TimerData);

#if N9_LEVEL == 1
    HandlePio(0, krn_entry);
#endif
#if N9_LEVEL == 2
    HandlePio(0, 0);
#endif
    sleep_ms(100);
    D("\nFinished.\n");
    sleep_ms(100);
    GET_STUCK();
}
