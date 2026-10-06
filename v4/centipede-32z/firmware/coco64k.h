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

// I/O register handlers, called from the foreground bus loop through
// IOReaders[]/IOWriters[].  They are plain functions, not members of the
// template below, because GCC ignores IN_RAM's section attribute on
// template members and leaves them in flash, where an XIP cache miss
// stalls past the end of the 6809 bus cycle.
namespace coco64k_io {

static void IN_RAM WriteOtherSamBit(uint a, byte d) {
  bool odd = a & 1;
  uint bitnum = (a - 0xFFC0) >> 1;
}

static void IN_RAM WriteFFD4_P1Clear(uint a, byte d) { SamP1Bit = false; }
static void IN_RAM WriteFFD5_P1Set(uint a, byte d) { SamP1Bit = true; }
static void IN_RAM WriteFFDE_TyClear(uint a, byte d) { SamTyBit = false; }
static void IN_RAM WriteFFDF_TySet(uint a, byte d) { SamTyBit = true; }

}  // namespace coco64k_io

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

  static void InitCoco64k() {
    for (uint a = 0xFFD4; a < 0xFFE0; a++) {
      IOWriters[255 & a] = coco64k_io::WriteOtherSamBit;
    }

    SamP1Bit = false;
    SamTyBit = false;
    IOWriters[0xD4] = coco64k_io::WriteFFD4_P1Clear;
    IOWriters[0xD5] = coco64k_io::WriteFFD5_P1Set;
    IOWriters[0xDE] = coco64k_io::WriteFFDE_TyClear;
    IOWriters[0xDF] = coco64k_io::WriteFFDF_TySet;
  }
};

#endif  // CENTIPEDE_FIRMWARE_COCO64K_H_
