#ifndef _TURBO9OS_H_
#define _TURBO9OS_H_

const byte Turbo9os_Rom[] = {
#include "turbo9os.rom.h"
};

template <typename T>
struct DoTurbo9os {
  static void Install_OS() {
    // Copy ROM to RAM.
    for (uint a = 0; a < sizeof Turbo9os_Rom; a++) {
      T::Poke(a, Turbo9os_Rom[a]);
    }
  }
};

#endif  // _TURBO9OS_H_
