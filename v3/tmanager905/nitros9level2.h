#ifndef _NITROS9LEVEL2_H_
#define _NITROS9LEVEL2_H_

const byte Nitros9level2_Rom[] = {
#include "../generated/level2.rom.h"
};

template <class T>
struct DoNitros9level2 {

  void Install_OS() {
    for (uint a = 0; a < sizeof Nitros9level2_Rom; a++) {
        T::Poke(0x2500+a, Nitros9level2_Rom[a]);
    }
  }
};

#endif  // _NITROS9LEVEL2_H_
