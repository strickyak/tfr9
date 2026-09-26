#ifndef CENTIPEDE_FIRMWARE_COCO128K_H_
#define CENTIPEDE_FIRMWARE_COCO128K_H_

// GIME MMU & System Registers
inline uint8_t gime_init0 = 0;       // $FF90: Bit 6=MMUEN, Bit 7=COCO, Bit 3=MC3
inline uint8_t gime_init1 = 0;       // $FF91: Bit 0=TR (Task Register)
inline uint8_t gime_irqenr = 0;      // $FF92
inline uint8_t gime_firqenr = 0;     // $FF93
inline uint8_t gime_timer_msb = 0;   // $FF94
inline uint8_t gime_timer_lsb = 0;   // $FF95
inline uint8_t gime_vmode = 0;       // $FF98
inline uint8_t gime_vres = 0;        // $FF99
inline uint8_t gime_brdr = 0;        // $FF9A
inline uint8_t gime_vscroll = 0;     // $FF9B
inline uint8_t gime_hscroll = 0;     // $FF9C
inline uint8_t gime_voff_msb = 0;    // $FF9D
inline uint8_t gime_voff_lsb = 0;    // $FF9E

// MMU Task Tables: 2 tasks x 8 slots of 8KB
// Default Task 0 & Task 1 mappings on CoCo 3 reset:
inline uint8_t mmu_task[2][8] = {
    {0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F},  // Task 0 ($FFA0-$FFA7)
    {0x38, 0x30, 0x31, 0x32, 0x33, 0x3D, 0x35, 0x3F}   // Task 1 ($FFA8-$FFAF)
};

// 16 Palette Registers: $FFB0-$FFBF
inline uint8_t gime_palette[16] = {0};

template <typename T>
struct DoCoco128k {
  static constexpr bool HasCoco128k() { return true; }
  static constexpr bool HasCoco64k() { return true; }

  // FORCE_INLINE ensures these class methods are inlined directly into
  // the standalone IN_RAM function core1_trampoline3(), avoiding Flash access.
  FORCE_INLINE static bool UseCoco64kRam(uint a) {
    if (centipede_config.become_coco3) {
      // In CoCo 3 mode:
      // When SamTyBit is 0 (ROM mode), addresses below 0x8000 read RAM.
      // When SamTyBit is 1 (All-RAM mode), addresses below 0xFF00 read RAM.
      return (a < (SamTyBit ? 0xFF00 : 0x8000));
    } else {
      // CoCo 2 compatibility mode
      return (a < (SamTyBit ? 0xFF00 : 0x8000));
    }
  }

  FORCE_INLINE static uint TranslateCoco64kRamAddress(uint a) {
    if (centipede_config.become_coco3) {
      if (UNLIKELY(a >= 0xFE00 && a < 0xFF00 && (gime_init0 & 0x08))) {
        // $FF90 Bit 3 (MC3): Constant RAM at $FE00-$FEFF from block 15 ($3F)
        return 0x1E000 | (a & 0x1FFF);
      }
      if (LIKELY(gime_init0 & 0x40)) {
        // GIME MMU Enabled (Bit 6 of $FF90 is 1)
        uint task = gime_init1 & 1;
        uint slot = (a >> 13) & 7;
        uint block = mmu_task[task][slot] & 0x0F;
        return (block << 13) | (a & 0x1FFF);
      } else {
        // MMU Disabled: SAM compatibility mapping
        // In 128K CoCo 3, SAM maps to blocks $38..$3F (blocks 8..15)
        uint p1_offset = SamP1Bit ? 0x8000 : 0x0000;
        return 0x10000 | ((p1_offset ^ (a & 0xFFFF)) & 0xFFFF);
      }
    } else {
      // CoCo 2 64K mode
      return (SamP1Bit ? 0x8000 : 0) ^ a;
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

    // Reset GIME registers to defaults
    gime_init0 = 0;
    gime_init1 = 0;
    gime_irqenr = 0;
    gime_firqenr = 0;

    static const uint8_t default_task0[8] = {0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
    static const uint8_t default_task1[8] = {0x38, 0x30, 0x31, 0x32, 0x33, 0x3D, 0x35, 0x3F};
    for (int i = 0; i < 8; i++) {
      mmu_task[0][i] = default_task0[i];
      mmu_task[1][i] = default_task1[i];
    }
    for (int i = 0; i < 16; i++) {
      gime_palette[i] = 0x12; // CoCo 3 default boot palette color (green)
    }

    // Register GIME I/O Handlers
    IOWriters[0x90] = WriteFF90_Init0;
    IOReaders[0x90] = ReadFF90_Init0;
    IOWriters[0x91] = WriteFF91_Init1;
    IOReaders[0x91] = ReadFF91_Init1;
    IOWriters[0x92] = WriteFF92_IrqEnr;
    IOReaders[0x92] = ReadFF92_IrqEnr;
    IOWriters[0x93] = WriteFF93_FirqEnr;
    IOReaders[0x93] = ReadFF93_FirqEnr;
    IOWriters[0x94] = WriteFF94_TimerMsb;
    IOReaders[0x94] = ReadFF94_TimerMsb;
    IOWriters[0x95] = WriteFF95_TimerLsb;
    IOReaders[0x95] = ReadFF95_TimerLsb;

    IOWriters[0x98] = WriteFF98_Vmode;
    IOReaders[0x98] = ReadFF98_Vmode;
    IOWriters[0x99] = WriteFF99_Vres;
    IOReaders[0x99] = ReadFF99_Vres;
    IOWriters[0x9A] = WriteFF9A_Brdr;
    IOReaders[0x9A] = ReadFF9A_Brdr;
    IOWriters[0x9B] = WriteFF9B_Vscroll;
    IOReaders[0x9B] = ReadFF9B_Vscroll;
    IOWriters[0x9C] = WriteFF9C_Hscroll;
    IOReaders[0x9C] = ReadFF9C_Hscroll;
    IOWriters[0x9D] = WriteFF9D_VoffMsb;
    IOReaders[0x9D] = ReadFF9D_VoffMsb;
    IOWriters[0x9E] = WriteFF9E_VoffLsb;
    IOReaders[0x9E] = ReadFF9E_VoffLsb;

    // MMU Registers $FFA0-$FFAF
    for (uint a = 0xA0; a <= 0xAF; a++) {
      IOWriters[a] = WriteFFAx_MMU;
      IOReaders[a] = ReadFFAx_MMU;
    }

    // Palette Registers $FFB0-$FFBF
    for (uint a = 0xB0; a <= 0xBF; a++) {
      IOWriters[a] = WriteFFBx_Palette;
      IOReaders[a] = ReadFFBx_Palette;
    }
  }

  static void WriteOtherSamBit(uint a, byte d) {
    bool odd = a & 1;
    uint bitnum = (a - 0xFFC0) >> 1;
    PUSH_TO_BG(FG2BG_PUTCHAR, 0, (odd ? 'A' : 'a') + bitnum);
  }

  static void WriteFFD4_P1Clear(uint a, byte d) {
    SamP1Bit = false;
  }
  static void WriteFFD5_P1Set(uint a, byte d) {
    SamP1Bit = true;
  }
  static void WriteFFDE_TyClear(uint a, byte d) {
    SamTyBit = false;
  }
  static void WriteFFDF_TySet(uint a, byte d) {
    SamTyBit = true;
  }

  static void WriteFF90_Init0(uint a, byte d) {
    gime_init0 = d;
  }
  static byte ReadFF90_Init0(uint a) {
    return gime_init0;
  }

  static void WriteFF91_Init1(uint a, byte d) {
    gime_init1 = d;
  }
  static byte ReadFF91_Init1(uint a) {
    return gime_init1;
  }

  static void WriteFF92_IrqEnr(uint a, byte d) { gime_irqenr = d; }
  static byte ReadFF92_IrqEnr(uint a) { return gime_irqenr; }

  static void WriteFF93_FirqEnr(uint a, byte d) { gime_firqenr = d; }
  static byte ReadFF93_FirqEnr(uint a) { return gime_firqenr; }

  static void WriteFF94_TimerMsb(uint a, byte d) { gime_timer_msb = d & 0x0F; }
  static byte ReadFF94_TimerMsb(uint a) { return gime_timer_msb; }

  static void WriteFF95_TimerLsb(uint a, byte d) { gime_timer_lsb = d; }
  static byte ReadFF95_TimerLsb(uint a) { return gime_timer_lsb; }

  static void WriteFF98_Vmode(uint a, byte d) { gime_vmode = d; }
  static byte ReadFF98_Vmode(uint a) { return gime_vmode; }

  static void WriteFF99_Vres(uint a, byte d) { gime_vres = d; }
  static byte ReadFF99_Vres(uint a) { return gime_vres; }

  static void WriteFF9A_Brdr(uint a, byte d) { gime_brdr = d; }
  static byte ReadFF9A_Brdr(uint a) { return gime_brdr; }

  static void WriteFF9B_Vscroll(uint a, byte d) { gime_vscroll = d; }
  static byte ReadFF9B_Vscroll(uint a) { return gime_vscroll; }

  static void WriteFF9C_Hscroll(uint a, byte d) { gime_hscroll = d; }
  static byte ReadFF9C_Hscroll(uint a) { return gime_hscroll; }

  static void WriteFF9D_VoffMsb(uint a, byte d) { gime_voff_msb = d; }
  static byte ReadFF9D_VoffMsb(uint a) { return gime_voff_msb; }

  static void WriteFF9E_VoffLsb(uint a, byte d) { gime_voff_lsb = d; }
  static byte ReadFF9E_VoffLsb(uint a) { return gime_voff_lsb; }

  static void WriteFFAx_MMU(uint a, byte d) {
    uint reg = a & 0x0F;
    uint task = (reg >> 3) & 1;
    uint slot = reg & 7;
    mmu_task[task][slot] = d & 0x3F;
  }
  static byte ReadFFAx_MMU(uint a) {
    uint reg = a & 0x0F;
    uint task = (reg >> 3) & 1;
    uint slot = reg & 7;
    return mmu_task[task][slot];
  }

  static void WriteFFBx_Palette(uint a, byte d) {
    gime_palette[a & 0x0F] = d & 0x3F;
  }
  static byte ReadFFBx_Palette(uint a) {
    return gime_palette[a & 0x0F];
  }
};

#endif  // CENTIPEDE_FIRMWARE_COCO128K_H_
