#ifndef CENTIPEDE_FIRMWARE_COCO128K_H_
#define CENTIPEDE_FIRMWARE_COCO128K_H_

template <typename T>
struct DoCoco128k {
  static constexpr bool HasCoco128k() { return true; }
  static constexpr bool HasCoco64k() { return true; }

  // FORCE_INLINE ensures these class methods are inlined directly into
  // the standalone IN_RAM function core1_trampoline3(), avoiding Flash access.
  FORCE_INLINE static bool UseCoco64kRam(uint a) {
    if (centipede_config.become_coco3) {
      // CoCo 3 mode: start out behaving like Coco2 with 128K of RAM,
      // with upper half unused, ready to be converted into CoCo 3.
      return (a < (SamTyBit ? 0xFF00 : 0x8000));
    } else {
      // CoCo 2 compatibility mode
      return (a < (SamTyBit ? 0xFF00 : 0x8000));
    }
  }

  FORCE_INLINE static uint TranslateCoco64kRamAddress(uint a) {
    if (centipede_config.become_coco3) {
      // CoCo 3 mode address translation:
      // Uses the lower 64K of the 128K RAM array, leaving the upper half unused for now.
      return SamP1Bit ? (0x8000 ^ a) : a;
    } else {
      // CoCo 2 compatibility mode
      return SamP1Bit ? (0x8000 ^ a) : a;
    }
  }

  static void InitCoco128k() {
    for (uint a = 0xFFD4; a < 0xFFE0; a++) {
      IOWriters[255 & a] = WriteOtherSamBit;
    }

    SamP1Bit = false;
    SamTyBit = false;
    IOWriters[0xD4] = WriteFFD4_P1Clear;
    IOWriters[0xD5] = WriteFFD5_P1Set;
    IOWriters[0xDE] = WriteFFDE_TyClear;
    IOWriters[0xDF] = WriteFFDF_TySet;
  }

  static void WriteOtherSamBit(uint a, byte d) {
    bool odd = a & 1;
    uint bitnum = (a - 0xFFC0) >> 1;
    PUSH_TO_BG(FG2BG_PUTCHAR, 0, (odd ? 'A' : 'a') + bitnum);
  }

  static void WriteFFD4_P1Clear(uint a, byte d) { SamP1Bit = false; }
  static void WriteFFD5_P1Set(uint a, byte d) { SamP1Bit = true; }
  static void WriteFFDE_TyClear(uint a, byte d) { SamTyBit = false; }
  static void WriteFFDF_TySet(uint a, byte d) { SamTyBit = true; }
};

#endif  // CENTIPEDE_FIRMWARE_COCO128K_H_
