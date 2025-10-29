#ifndef _SAMVDG_H_
#define _SAMVDG_H_

uint SamBits;

template <typename T>
struct DontCocoSamVdg {
  constexpr static bool Does_CocoSamVdg() { return false; }
  constexpr static bool Sam_Upper32kIsRom() { return false; }
  constexpr static bool Sam_Swap32kRams() { return false; }

  constexpr static bool Sam_DoesTextScreen() { return false; }
  static uint Sam_ModeV() { return 0; }
  static uint Sam_ModeF() { return 0; }
  static uint Sam_ModeP() { return 0; }
  static uint Sam_ModeR() { return 0; }
  static uint Sam_ModeM() { return 0; }
  static uint Sam_ModeTY() { return 0; }
  static uint Sam_ScreenAddress() { return 0; }

  static void Reset_CocoSamVdg() {}
};

template <typename T>
struct DoCocoSamVdg {
  constexpr static bool Does_CocoSamVdg() { return true; }
  static bool Sam_Upper32kIsRom() { return 0 == (0x8000 & SamBits); }
  static bool Sam_Swap32kRams() { return 0 != ((1U << 10) & SamBits); }

  static bool Sam_DoesTextScreen() { return 0 == (Pia1.outB & 0x80); }  // Pia1.PB7 is the A/G bit to VDG.
  static uint Sam_ModeV() { return 7 & SamBits; }
  static uint Sam_ModeF() { return 127 & (SamBits >> 3); }
  static uint Sam_ModeP() { return 1 & (SamBits >> 10); }
  static uint Sam_ModeR() { return 3 & (SamBits >> 11); }
  static uint Sam_ModeM() { return 3 & (SamBits >> 13); }
  static uint Sam_ModeTY() { return 1 & (SamBits >> 15); }
  static uint Sam_ScreenAddress() { return Sam_ModeF() << 9; }

  static void Reset_CocoSamVdg() { SamBits = 0; }

  static void Install_CocoSamVdg() {
    for (uint i = 0; i < 16; i++) {
      IOWriters[255 & (0xC0 + 2 * i) + 0] = [i](uint a, byte value) {
        SamBits &= ~(1U << i);
      };
      IOWriters[255 & (0xC0 + 2 * i + 1)] = [i](uint a, byte value) {
        SamBits |= (1U << i);
      };
    }
  }
};

#endif  //  _SAMVDG_H_
