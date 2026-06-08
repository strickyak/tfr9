#ifndef _TURBO9OS_H_
#define _TURBO9OS_H_

// These vectors get copied to 0xFFF0.
// They do not include the final RESET vector at 0xFFFE;
// that will be computed.
const uint Turbo9os_Vectors[] = {
    0x0000, 0x0100, 0x0103, 0x010F, 0x010C, 0x0106, 0x0109,
};

const byte Turbo9os_Rom[] = {
#include "turbo9os.rom.h"
};

template <typename T>
struct DoTurbo9os {
  static void Install_OS() {
    printf("DoTurbo9os Install_OS...\n");
    is_an_os9 = true;
    // Copy 7 Vectors;  Reset vector comes later.
    for (uint i = 0; i < 7; i++) {
      InstallVector(i, Turbo9os_Vectors[i]);
    }
    // Copy ROM to RAM, ending just before 0xFF00.
    constexpr uint n = sizeof Turbo9os_Rom;
    constexpr uint begin = 0xFF00 - n;  // beginning addr of ROM
    printf("DoTurbo9os Install_OS... n=%04x begin=%04x\n", n, begin);
    for (uint i = 0; i < n; i++) {
      T::Poke(begin + i, Turbo9os_Rom[i]);
      TransmitWrite(begin + i, Turbo9os_Rom[i]);
    }

    assert(T::Peek2(begin) == 0x87CD);  // OS9 module magic number

    uint name = T::Peek2(begin + 4);  // OS9 module name offset is 4
    char expect[7] = "kernel";
    expect[5] |= 0x80;  // OS9 string termination bit overlays final 'l'
    assert(0 == memcmp(Turbo9os_Rom + name, expect, 6));

    uint entry = T::Peek2(begin + 9);  // OS9 module entry offset is 9
    printf("DoTurbo9os Install_OS... begin=%04x entry=%04x\n", begin,
           begin + entry);
    InstallVector(7, begin + entry);

    // T::DumpRam();
  }
};

#endif  // _TURBO9OS_H_
