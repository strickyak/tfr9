#ifndef _TURBO9OS_H_
#define _TURBO9OS_H_

// These vectors get copied to 0xFFF0.
// They do not include the final RESET vector at 0xFFFE;
// that will be computed.
const byte Turbo9os_Vectors[] = {
    0x00, 0x00, 0x01, 0x00, 0x01, 0x03, 0x01,
    0x0F, 0x01, 0x0C, 0x01, 0x06, 0x01, 0x09,
};

const byte Turbo9os_Rom[] = {
#include "turbo9os.rom.h"
};

template <typename T>
struct DoTurbo9os {
  static void Install_OS() {
    // Copy Vectors.
    for (uint i = 0; i < sizeof Turbo9os_Vectors; i++) {
      T::Poke(0xFFF0 + i, Turbo9os_Vectors[i]);
    }
    // Copy ROM to RAM, ending just before 0xFF00.
    constexpr uint n = sizeof Turbo9os_Rom;
    constexpr uint begin = 0xFF00 - n;  // beginning addr of ROM
    for (uint i = 0; i < n; i++) {
      T::Poke(begin + i, Turbo9os_Rom[i]);
    }

    assert(T::Peek2(begin) == 0x87CD);  // OS9 module magic number

    uint name = T::Peek2(begin + 4);  // OS9 module name offset is 4
    char expect[7] = "kernel";
    expect[5] |= 0x80;  // OS9 string termination bit overlays final 'l'
    assert(0 == memcmp(Turbo9os_Rom + name, expect, 6));

    uint entry = T::Peek2(begin + 9);  // OS9 module entry offset is 9
    T::Poke2(0xFFFE,
             begin + entry);  // Set RESET vector at 0xFFFE to the entry.

    T::DumpRam();
  }
};

#endif  // _TURBO9OS_H_
