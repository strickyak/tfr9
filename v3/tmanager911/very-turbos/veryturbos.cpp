#define TRACE 1
#define MHz 200  // clock speed, 150 is "normal".

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

#define force_inline inline __attribute__((always_inline))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

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

bool is_an_os9;  // unused
using IOReader = std::function<byte(uint addr)>;
using IOWriter = std::function<void(uint addr, byte data)>;
IOReader IOReaders[256];
IOWriter IOWriters[256];

enum cycle_kind : byte {
  CY_UNUSED = 0,
  CY_SEEN = 1,
  CY_UNSEEN = 2,
  CY_MORE = 3,
  CY_READ = 4,
  CY_WRITE = 5,
  CY_IDLE = 6,
  CY_FIC = 7,  // first instruction cycle
};

enum message_type : byte {
  // Long form codes, 128 to 191.
  // Followed by a 1-byte or 2-byte Size value.
  // If following byte in 128 to 191, it is 1-byte, use low 6 bits for size.
  // If following byte in 192 to 255, it is 2-byte, use low 6 bits times 64,
  // plus low 6 bits of next byte.
  C_LOGGING = 130,  // Ten levels: 130 to 139.

  C_PRE_LOAD = 163,    // Console pokes to Manager
  C_RAM_CONFIG = 164,  // Pico tells tconsole.
  C_DUMP_RAM = 167,
  C_DUMP_LINE = 168,
  C_DUMP_STOP = 169,
  C_DUMP_PHYS = 170,
  C_EVENT = 172,  // event.h
  C_DISK_READ = 173,
  C_DISK_WRITE = 174,

  EVENT_RTI = 176,
  EVENT_SWI2 = 177,

  // Short form codes, 192 to 255.
  // The packet length does not follow,
  // but is in the low nybble.
  C_REBOOT = 192,      // n=0
  C_PUTCHAR = 193,     // n=1
  C_RAM2_WRITE = 195,  // n=3
  C_RAM3_WRITE = 196,  // n=4
  C_RAM5_WRITE = 198,  // n=6
  C_CYCLE = 200,       // tracing one cycle
};

void InstallVector(uint i, uint addr) {
  IOReaders[255 & (0xFFF0 + 2 * i + 0)] = [addr](uint _a) {
    return (byte)(addr >> 8);
  };
  IOReaders[255 & (0xFFF0 + 2 * i + 1)] = [addr](uint _a) {
    return (byte)(addr >> 0);
  };
}

extern "C" {
extern int stdio_usb_in_chars(char* buf, int length);
}

void ShowChar(char c) { putchar(c); }

// putbyte does CR/LF escaping for Binary Data
void putbyte(byte x) { putchar_raw(x); }

// Put Size with 1-byte / 2-byte encoding
void putsz(uint n) {
  assert(n < 4096);
  if (n < 64) {
    putbyte(0x80 + n);
  } else {
    putbyte(0xC0 + (n >> 6));  // div 64
    putbyte(0x80 + (n & 63));  // mod 64
  }
}

void TransmitHeader(byte messtype, uint sz) {
  putbyte(messtype);
  if (sz < 64) {
    putbyte(128 + sz);
  } else {
    assert(sz <= 1024);             // really could go up to 4095
    putbyte(128 + 64 + (sz >> 6));  // send sz mod 64
    putbyte(128 + (sz & 63));       // send sz div 64
  }
}

void TransmitMessage(byte messtype, uint sz, char* buf) {
  if (messtype < 0xC0) {
    TransmitHeader(messtype, sz);
  } else {
    putbyte(messtype);
    assert((messtype & 15) == sz);
  }
  for (uint i = 0; i < sz; i++) {
    putbyte(buf[i]);
  }
}

void TransmitCycle(uint cy, byte flags, byte kind, byte data, uint addr) {
  byte r[8];
  r[0] = cy >> 24;
  r[1] = cy >> 16;
  r[2] = cy >> 8;
  r[3] = cy >> 0;
  r[4] = flags + (kind << 5);
  r[5] = data;
  r[6] = addr >> 8;
  r[7] = addr >> 0;
  TransmitMessage(C_CYCLE, 8, (char*)r);
}

void TransmitWrite(uint addr, byte data) {
  putbyte(C_RAM2_WRITE);
  putbyte(addr >> 8);
  putbyte(addr);
  putbyte(data);
}

#include "circbuf.h"
#include "pio_veryturbos.pio.h"
#include "turbo9os.h"
#include "turbo9sim.h"

CircBuf<1024> usb_input;
CircBuf<1024> term_input;

volatile uint delay_busy;
void Delay(uint n) {
  for (uint i = 0; i < n * 10; i++) {
    delay_busy += i;
  }
}

bool acia_irq_enabled;
bool acia_irq_firing;
bool acia_char_in_ready;
int acia_char;
bool irq_needed;
bool prev_irq_needed;

bool TryGetUsbByte(char* ptr) {
  int rc = stdio_usb_in_chars(ptr, 1);
  return (rc != PICO_ERROR_NO_DATA);
}

#define Printf if(false)printf

void PollUsbInput() {
  // Try from USB to `usb_input` object.
  while (1) {
    char x = 0;
    bool ok = TryGetUsbByte(&x);
    if (ok) {
        Printf("usb_input.Put(%u) ", x&0xFF);
      usb_input.Put(x);
    } else {
      break;
    }
  }

  // Try from `usb_input` object to `term_input`, if it Peeks as ASCII
  while (1) {
    int peek = usb_input.HasAtLeast(1) ? (int)usb_input.Peek() : -1;
    if (peek == -1) {
        Printf("~");
        break;
    }

    Printf("peekI(%u) ", peek);
    peek &= 0xFF;
    Printf("peekB(%u) ", peek);
    if (peek == 0) {
      break;
    } else if (1 <= peek && peek <= 126) {
      byte c = usb_input.Take();
      Printf("took(%u) ", c);
      assert((int)c == peek);
      if (c == 10) {
        c = 13;
      }
      Printf("term_put(%u) ", c);
      term_input.Put(c);
    } else {
      // Non-ASCII
      byte c = usb_input.Take();
      Printf("ignore(%d)", c);
    }
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
      case LED:
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
    Delay(10);  // phase 1
    gpio_put(Q, 1);
    Delay(10);  // phase 2
    gpio_put(E, 1);
    Delay(10);  // phase 3
    gpio_put(Q, 0);
    Delay(10);  // phase 4
    gpio_put(E, 0);
  }
  gpio_put(RESET, 1);
}

byte ram[64 * 1024];

constexpr uint GROUP_SIZE = 10000;
#ifdef HISTORY
uint history[GROUP_SIZE + 64];
#endif

uint milliseconds;
uint errors;

#define ERR \
  if (errors++ < 32) printf

template <typename T>
struct Guts {
  force_inline static void Poke(uint a, byte b) { ram[a & 0xFFFF] = b; }
  force_inline static byte Peek(uint a) { return ram[a & 0xFFFF]; }
  force_inline static uint Peek2(uint a) {
    return (((uint)Peek(a)) << 8) | Peek(a + 1);
  }

  static bool ChangeInterruptPin(bool irq_needed) {
    gpio_put(IRQ, !irq_needed);
    return true;
  }

  static void RunCPU() {
    T::Install_OS();

    volatile sio_hw_t* hw = (volatile sio_hw_t*)sio_hw;

    int cycles = 0;
    int epochs = 0;
    while (true) {
      irq_needed |= T::Turbo9sim_IrqNeeded();  // either Timer or RX
      if (irq_needed != prev_irq_needed) {
        bool ok = ChangeInterruptPin(irq_needed);
        if (ok) {
          prev_irq_needed = irq_needed;
          gpio_put(LED, irq_needed);
        }
      }
      PollUsbInput();
      if (T::Turbo9sim_CanRx()) {
        if (term_input.HasAtLeast(1)) {
          byte ch = term_input.Take();
          Printf("set_rx(%u) ", ch);
          T::Turbo9sim_SetRx(ch);
        }
      }

      uint prev_late_pins = 0;

      for (int i = 0; i < GROUP_SIZE; i++) {
        pio_sm_put(pio0, 0, 0); // put sync word

        byte value = 0;
        uint pins = pio_sm_get_blocking(pio0, 0); // get early pins
        uint prev_addr =
            0xFFFF & hw->gpio_hi_in;  // Read addr from high pins [32:47]
        uint addr;
        while (true) {
            addr = 0xFFFF & hw->gpio_hi_in;
            if (addr == prev_addr) break;
            prev_addr = addr;
        }

        const bool reading = (pins & (1 << R_W));
        const char rw = (reading) ? 'r' : 'W';
        byte kind = 0;
        uint late_pins = 0;

        //////////////////
        if (likely(reading)) {
          // READ CYCLES
          if (likely(addr < 0xFF00)) {
            value = ram[addr];
          } else {
            IOReader fn = IOReaders[addr & 0xFF];
            if (fn) {
              value = fn(addr);
            } else {
              value = 0;
            }
          }
          kind = CY_READ;
        } else {  
          // WRITE CYCLES
          // on Write cycle, Receive the value that was Written.
          late_pins = pio_sm_get_blocking(pio0, 0);
          value = (byte)late_pins;

          if (likely(addr < 0xFF00)) {
            ram[addr] = value;
#ifdef TRACE
            TransmitWrite(addr, value);
#endif
          } else {
            IOWriter fn = IOWriters[addr & 0xFF];
            if (fn) {
              fn(addr, value);
            } else {
              // do nothing
            }
          }
          kind = CY_WRITE;
        }
        /////////////////

        if (likely(reading)) {
            pio_sm_put(pio0, 0, value);
            late_pins = pio_sm_get_blocking(pio0, 0); // LATE PINS
        }
        bool is_lic = ((late_pins & (1<<LIC)) != 0);
        bool is_fic = ((prev_late_pins & (1<<LIC)) != 0);

#ifdef TRACE
        // uint late_pins = hw->gpio_in;  // Read addr from lower pins [0..31]
        if (is_fic) {
#if 1
                kind = CY_FIC;
#else
            if (seen[addr]) {
                kind = CY_SEEN;
            } else {
                kind = CY_UNSEEN;
                seen[addr] = 1;
            }
#endif
        }

        // printf("+%c %04x %02x\n", rw, addr, value);
        // TransmitCycle(uint cy, byte flags, byte kind, byte data, uint addr);
        TransmitCycle(cycles+i, (byte)is_lic, kind, value, addr);
#endif

#ifdef HISTORY
        history[i] = (pins & 0xF000) | (uint(addr) << 8) | (0xFF & value);
#endif

        prev_late_pins = late_pins;
      }  // next i

      cycles += GROUP_SIZE;
      epochs++;
      if (epochs == 5000) {
          printf("[Mc=%g  s=%g  Mcps=%g]",
                  double(cycles)/double(1000*1000),
                  double(milliseconds)/double(1000),
                  (double)cycles / double(1000*milliseconds) );
          epochs = 0;
      }
    }  // true
  }  // func RunCPU
}; // Guts

struct Engine : public DoTurbo9os<Engine,
                    RomList<Turbo9os_Rom, Ncl_Rom>>,
                public DoTurbo9sim<Engine>,
                public Guts<Engine> {};

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

  for (int i = 0; i < 3; i++) {
    gpio_put(LED, 1);
    sleep_ms(200);
    gpio_put(LED, 0);
    sleep_ms(200);
    printf(":%d:\n", i);
  }

  printf(":r:\n");
  RunReset();

  printf(":p:\n");
  pio_clear_instruction_memory(pio0);
  const uint offset_t911 = pio_add_program(pio0, &t911veryfast_program);
  t911veryfast_program_init(pio0, 0, offset_t911);

  printf(":g:\n");
  Engine::Turbo9sim_Install(0xFF00);
  multicore_launch_core1(Engine::RunCPU);

  alarm_pool_init_default();
  add_repeating_timer_us(1000, TimerCallback, nullptr, &TimerData);
  while (true) sleep_ms(1234);
}
