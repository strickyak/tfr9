#ifndef V4_RAM_H_
#define V4_RAM_H_

// v4_ram.h — Shared RAM array, I/O device tables, and inline accessors.

#include "v4_types.h"

// ── The 64K RAM array ──
// Both TFR911 and Centipede have 64K of RAM managed by the RP2350.
extern byte ram[64 * 1024];

// ── I/O Device Tables ──
// Readers and writers for the $FF00–$FFFF I/O page.
// Indexed by (addr & 0xFF). nullptr means no device at that address.
extern IOReader IOReaders[256];
extern IOWriter IOWriters[256];

// ── Inline accessors ──
// These are used by CRTP mixins (e.g., OS loaders) to poke/peek RAM
// without knowing which platform they're on.

FORCE_INLINE void Poke(uint a, byte b) { ram[a & 0xFFFF] = b; }
FORCE_INLINE byte Peek(uint a) { return ram[a & 0xFFFF]; }
FORCE_INLINE uint Peek2(uint a) {
  return (static_cast<uint>(Peek(a)) << 8) | Peek(a + 1);
}

// ── Vector installation ──
// Writes a 16-bit vector to the 6809 vector area at FFF0–FFFF.
// Index 0 = $FFF0, index 7 = $FFFE (RESET).
// The IOReaders for these addresses are installed so the CPU reads them.
extern byte vector_ram[16];

inline void InstallVector(uint i, uint addr) {
  vector_ram[2 * i + 0] = static_cast<byte>(addr >> 8);
  vector_ram[2 * i + 1] = static_cast<byte>(addr);

  IOReaders[0xF0 + 2 * i + 0] = [](uint a) -> byte {
    return vector_ram[a & 15];
  };
  IOReaders[0xF0 + 2 * i + 1] = [](uint a) -> byte {
    return vector_ram[a & 15];
  };
}

#endif  // V4_RAM_H_
