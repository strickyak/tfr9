#ifndef _SAMVDG_H_
#define _SAMVDG_H_

uint SamBits;

template <typename T>
struct DontCocoSamVdg {
  constexpr static bool Does_CocoSamVdg() { return false; }
  constexpr static bool Sam_Upper32kIsRom() { return false; }
  constexpr static bool Sam_Swap32kRams() { return false; }

  static void Reset_CocoSamVdg() { }
};

template <typename T>
struct DoCocoSamVdg {
  constexpr static bool Does_CocoSamVdg() { return true; }
  static bool Sam_Upper32kIsRom() {
      return 0 == (0x8000 & SamBits);
  }
  static bool Sam_Swap32kRams() {
      return 0 != ((1U << 10) & SamBits);
  }

  static void Reset_CocoSamVdg() { SamBits = 0; }

  static void Install_CocoSamVdg() {
      for ( uint i = 0; i < 16; i++) {
          IOWriters[255&(0xC0 + 2*i) + 0] = [i](uint a, byte value) {
              SamBits &= ~(1U << i);
          };
          IOWriters[255&(0xC0 + 2*i + 1)] = [i](uint a, byte value) {
              SamBits |= (1U << i);
          };
      }
  }
};


#endif  //  _SAMVDG_H_
