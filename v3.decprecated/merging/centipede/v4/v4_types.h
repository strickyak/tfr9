#ifndef V4_TYPES_H_
#define V4_TYPES_H_

// v4_types.h — Common types, macros, and protocol constants for the
// unified TFR911/Centipede v4 firmware framework.

#include <cstdint>
#include <cstring>
#include <functional>

// ── Type aliases ──

using byte = unsigned char;
using uint = unsigned int;
using addr16 = uint16_t;

// ── Compiler hints ──

#ifndef FORCE_INLINE
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

#ifndef IN_RAM
#define IN_RAM __attribute__((section(".time_critical")))
#endif

#ifndef IN_FLASH
#define IN_FLASH
#endif

#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// ── I/O Device function types ──
// These are the function pointer types stored in the IOReaders/IOWriters
// arrays. Using raw function pointers for zero-overhead in the inner loop.

using IOReader = byte (*)(uint addr);
using IOWriter = void (*)(uint addr, byte data);

// ── COBS Protocol Constants ──
// Command bytes for Pico → Tether direction.

enum PicoToTether : byte {
  C_NOP      = 0,
  C_SHUTDOWN = 255,

  // Long-form codes (128–191). Followed by a size prefix.
  C_LOGGING      = 130,  // Ten levels: 130–139.
  C_PRE_LOAD     = 163,  // Tether pokes to Pico: [cmd, addr_hi, addr_lo, data...]
  C_RAM_CONFIG   = 164,  // Pico tells tether its RAM config.
  C_DUMP_RAM     = 167,
  C_DUMP_LINE    = 168,
  C_DUMP_STOP    = 169,
  C_DUMP_PHYS    = 170,
  C_EVENT        = 172,
  C_DISK_READ    = 173,
  C_DISK_WRITE   = 174,
  C_COMPRESSED_CYCLES = 175,

  // Short-form codes (192–255). Length is implicit in the low nybble.
  C_REBOOT       = 192,  // n=0. No payload.
  C_PUTCHAR      = 193,  // n=1. Payload is "Data".
  C_RAM2_WRITE   = 195,  // n=3. Payload is "AHi ALo Data".
  C_RAM3_WRITE   = 196,  // n=4. Payload is "AHighest AHi ALo Data".
  C_RAM5_WRITE   = 198,  // n=6. Payload is "PHighest PHi PLo AHi ALo Data".
  C_CYCLE        = 200,  // TFR911 uncompressed cycle: 8-byte payload.
  C_CYCLE_RD3    = 211,  // Centipede read cycle: A A D.
};

// Command bytes for Tether → Pico direction.
enum TetherToPico : byte {
  T_DISK_READ = 173,
  T_HELLO     = 178,
  T_COMMAND   = 179,  // Tcl command string (null-terminated).
  T_RPC       = 180,
  T_PICO_RPC  = 181,
};

// ── Foreground → Background inter-core message tags ──
// Packed into a uint32: (tag << 24) | (addr << 8) | data.

enum FG2BG : uint {
  FG2BG_PUTCHAR = 0,
  FG2BG_READ    = 1,
  FG2BG_SPOON_ON_RESET = 2,
  FG2BG_WRITE   = 3,
  FG2BG_SYNC_NEEDED = 4,  // Boundary marker, not an event.
  FG2BG_NMI     = 5,
  FG2BG_FLOPPY_COMMAND = 6,
  FG2BG_FLOPPY_LATCH   = 7,
  FG2BG_W_256   = 8,
  FG2BG_PEEK_REPLY = 9,
  FG2BG_START_KEYBOARD_INJECTOR = 10,
};

// ── Background → Foreground inter-core message tags ──
enum BG2FG : uint {
  BG2FG_PEEK = 1,
  BG2FG_POKE = 2,
  BG2FG_EXIT_CONSOLE = 3,
};

#endif  // V4_TYPES_H_
