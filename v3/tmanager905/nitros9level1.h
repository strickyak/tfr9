#ifndef _NITROS9LEVEL1_H_
#define _NITROS9LEVEL1_H_

const byte Nitros9level1_Rom[] = {
#include "../generated/level1.rom.h"
};

template <class T>
struct DoNitros9level1 {

  void Install_OS() {
    for (uint a = 0; a < sizeof Nitros9level1_Rom; a++) {
        T::Poke(0x2500+a, Nitros9level1_Rom[a]);
    }
  }
};

#endif  // _NITROS9LEVEL1_H_
