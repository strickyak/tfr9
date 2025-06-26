#ifndef _NITROS9LEVEL1_H_
#define _NITROS9LEVEL1_H_

#define LEVEL1_LAUNCHER_START 0x2500

const byte Nitros9level1_Rom[] = {
#include "../generated/level1.rom.h"
};

uint const Coco2Vectors[] = {
    // From ~/coco-shelf/toolshed/cocoroms/bas13.rom :
    0,  //  6309 TRAP
    0x0100,
    0x0103,
    0x010f,
    0x010c,
    0x0106,
    0x0109,
    LEVEL1_LAUNCHER_START,  //  RESET
};

template <class T>
struct DoNitros9level1 {

  static void Install_OS() {
ShowChar('p');
    T::ResetRam();
ShowChar('q');
    // Copy ROM to RAM
    for (uint a = 0; a < sizeof Nitros9level1_Rom; a++) {
        T::Poke(LEVEL1_LAUNCHER_START+a, Nitros9level1_Rom[a]);
    }
ShowChar('r');
    // Fix Vectors
    for (uint i = 0; i < 8; i++) {
        T::Poke2(0xFFF0 + 2*i, Coco2Vectors[i]);
    }
ShowChar('s');
  }
};

#endif  // _NITROS9LEVEL1_H_
