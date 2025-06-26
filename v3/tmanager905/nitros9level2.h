#ifndef _NITROS9LEVEL2_H_
#define _NITROS9LEVEL2_H_

#define LEVEL2_LAUNCHER_START 0x2500

const byte Nitros9level2_Rom[] = {
#include "../generated/level2.rom.h"
};

uint const Coco3Vectors[] = {
    // From ~/coco-shelf/toolshed/cocoroms/coco3.rom :
    0x0000,  // 6309 traps
    0xFEEE,
    0xFEF1,
    0xFEF4,
    0xFEF7,
    0xFEFA,
    0xFEFD,
    LEVEL2_LAUNCHER_START,
};

template <class T>
struct DoNitros9level2 {

  static void Install_OS() {
    T::ResetRam();
    for (uint a = 0; a < sizeof Nitros9level2_Rom; a++) {
        T::Poke(LEVEL2_LAUNCHER_START+a, Nitros9level2_Rom[a]);
    }
    for (uint i = 0; i < 8; i++) {
        T::Poke2(0xFFF0 + 2*i, Coco3Vectors[i]);
    }
  }
};

#endif  // _NITROS9LEVEL2_H_
