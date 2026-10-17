#ifndef CENTIPEDE_FIRMWARE_COCO64K_H_
#define CENTIPEDE_FIRMWARE_COCO64K_H_

template <typename T>
struct DontCoco64k {
  static constexpr bool HasCoco64k() { return false; }
  static void InitCoco64k() {}
  FORCE_INLINE static bool IsCoco3Rom(uint a) { return false; }
  static constexpr bool UseCoco64kRam(uint a) { return false; }
  FORCE_INLINE static uint TranslateCoco64kRamAddress(uint a) { return a; }
};

template <typename T>
struct DoCoco64k {
  static constexpr bool HasCoco64k() { return true; }
  FORCE_INLINE static bool IsCoco3Rom(uint a) { return false; }
  FORCE_INLINE static bool UseCoco64kRam(uint a) {
    return (a < (SamTyBit ? 0xFF00 : 0x8000));
  }
  FORCE_INLINE static uint TranslateCoco64kRamAddress(uint a) {
    return SamP1Bit ? (0x8000 ^ a) : a;
  }

  FORCE_INLINE static void DoWriteOtherSamBit(uint a, byte d) {
    bool odd = a & 1;
    uint bitnum = (a - 0xFFC0) >> 1;
    (void)odd;
    (void)bitnum;
  }
  FORCE_INLINE static void DoWriteFFD4_P1Clear(uint a, byte d) { SamP1Bit = false; }
  FORCE_INLINE static void DoWriteFFD5_P1Set(uint a, byte d) { SamP1Bit = true; }
  FORCE_INLINE static void DoWriteFFDE_TyClear(uint a, byte d) { SamTyBit = false; }
  FORCE_INLINE static void DoWriteFFDF_TySet(uint a, byte d) { SamTyBit = true; }

  static void InitCoco64k();
};

void IN_RAM Coco64k_WriteOtherSamBit(uint a, byte d) {
  DoCoco64k<void>::DoWriteOtherSamBit(a, d);
}
void IN_RAM Coco64k_WriteFFD4_P1Clear(uint a, byte d) {
  DoCoco64k<void>::DoWriteFFD4_P1Clear(a, d);
}
void IN_RAM Coco64k_WriteFFD5_P1Set(uint a, byte d) {
  DoCoco64k<void>::DoWriteFFD5_P1Set(a, d);
}
void IN_RAM Coco64k_WriteFFDE_TyClear(uint a, byte d) {
  DoCoco64k<void>::DoWriteFFDE_TyClear(a, d);
}
void IN_RAM Coco64k_WriteFFDF_TySet(uint a, byte d) {
  DoCoco64k<void>::DoWriteFFDF_TySet(a, d);
}

template <typename T>
void DoCoco64k<T>::InitCoco64k() {
  for (uint a = 0xFFD4; a < 0xFFE0; a++) {
    IOWriters[255 & a] = Coco64k_WriteOtherSamBit;
  }

  SamP1Bit = false;
  SamTyBit = false;
  IOWriters[0xD4] = Coco64k_WriteFFD4_P1Clear;
  IOWriters[0xD5] = Coco64k_WriteFFD5_P1Set;
  IOWriters[0xDE] = Coco64k_WriteFFDE_TyClear;
  IOWriters[0xDF] = Coco64k_WriteFFDF_TySet;
}

#endif  // CENTIPEDE_FIRMWARE_COCO64K_H_
