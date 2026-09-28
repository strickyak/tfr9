#ifndef CENTIPEDE_FIRMWARE_COCO128K_H_
#define CENTIPEDE_FIRMWARE_COCO128K_H_

extern volatile uint8_t gime_init0;           // $FF90: Bit 6=MMUEN, Bit 7=COCO, Bit 3=MC3 (defined in centipede.cpp)
inline volatile uint8_t gime_init1 = 0;       // $FF91: Bit 0=TR (Task Register)
inline volatile uint8_t gime_irqenr = 0;      // $FF92
inline volatile uint8_t gime_firqenr = 0;     // $FF93
inline volatile uint8_t gime_timer_msb = 0;   // $FF94
inline volatile uint8_t gime_timer_lsb = 0;   // $FF95
inline volatile uint8_t gime_vmode = 0;       // $FF98
inline volatile uint8_t gime_vres = 0;        // $FF99
inline volatile uint8_t gime_brdr = 0;        // $FF9A
inline volatile uint8_t gime_vscroll = 0;     // $FF9B
inline volatile uint8_t gime_hscroll = 0;     // $FF9C
inline volatile uint8_t gime_voff_msb = 0;    // $FF9D
inline volatile uint8_t gime_voff_lsb = 0;    // $FF9E

// MMU Task Tables: 2 tasks x 8 slots of 8KB
// Default Task 0 & Task 1 mappings on CoCo 3 reset:
inline volatile uint8_t mmu_task[2][8] = {
    {0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F},  // Task 0 ($FFA0-$FFA7)
    {0x38, 0x30, 0x31, 0x32, 0x33, 0x3D, 0x35, 0x3F}   // Task 1 ($FFA8-$FFAF)
};

// Pre-shifted physical 8KB block bases for ultra-fast single-cycle address translation:
// mmu_base[task_offset | slot] = (block & 0x0F) << 13
inline volatile uint32_t mmu_base[16] = {
    0x10000, 0x12000, 0x14000, 0x16000, 0x18000, 0x1A000, 0x1C000, 0x1E000,
    0x10000, 0x00000, 0x02000, 0x04000, 0x06000, 0x1A000, 0x0A000, 0x1E000
};
inline volatile uint8_t active_mmu_offset = 0;  // 0 for Task 0 ($FFA0-$FFA7), 8 for Task 1 ($FFA8-$FFAF)

// 16 Palette Registers: $FFB0-$FFBF
inline volatile uint8_t gime_palette[16] = {0};

template <typename T>
struct DoCoco128k {
  static constexpr bool HasCoco128k() { return true; }
  static constexpr bool HasCoco64k() { return true; }

  FORCE_INLINE static bool IsCoco3Rom(uint a) {
    // In CoCo 3:
    // When SamTyBit == 0 (ROM mode at boot), $8000-$FDFF is ROM.
    // When SamTyBit == 1 (all-RAM mode), slots mapped to ROM blocks ($3C..$3F)
    // continue to run the 32KB Super Extended BASIC and Disk BASIC code.
    // Serving these reads from immutable coco3_rom[] / disk11_rom[] protects
    // against spurious bus write glitches corrupting the BASIC interpreter in RAM.
    // The constant $FE page ($FE00-$FEFF) is always RAM (Block 15).
    if (0x8000 <= a && a < 0xFE00) {
      if (!SamTyBit) return true;
      uint task = active_mmu_offset >> 3;
      uint slot = (a >> 13) & 7;
      return mmu_task[task][slot] >= 0x3C;
    }
    return false;
  }

  // FORCE_INLINE ensures these class methods are inlined directly into
  // the standalone IN_RAM function core1_trampoline3(), avoiding Flash access.
  FORCE_INLINE static bool UseCoco64kRam(uint a) {
    return (a < (SamTyBit ? 0xFF00 : 0x8000));
  }

  FORCE_INLINE static uint TranslateCoco64kRamAddress(uint a) {
    if (centipede_config.become_coco3) {
      // FAST COCO3 MMU ADDRESS TRANSLATION:
      //
      // Architectural optimization rationale:
      // 1. We boot straight into MMU mode and assume the MMU is always active.
      //    The hardware default Task 0 mapping (blocks $38..$3F -> physical blocks 8..15)
      //    is 100% bit-for-bit identical to SAM compatibility mode. CoCo 3 Color BASIC
      //    enables the MMU immediately on cold boot (coco3.asm:8793), and NitrOS-9 Level 2
      //    requires MMU mode from start. Treating MMU as always active eliminates testing
      //    gime_init0 Bit 6 (MMUEN) on every bus cycle.
      //
      // 2. We assume MC3 (constant RAM at $FE00-$FEFF from Block 15) is always active.
      //    Both CoCo 3 BASIC and NitrOS-9 Level 2 unconditionally keep MC3=1 in INIT0 ($FF90).
      //    Furthermore, in both OSes Slot 7 ($E000-$FFFF) is already mapped to Block 15,
      //    so standard MMU translation for $FE00-$FEFF naturally translates to Block 15
      //    (0x1E000 | 0x1E00 = 0x1FE00) with zero overhead and no special range checks.
      //
      // 3. Pre-shifted mmu_base[16] table: block bases ((block & 0x0F) << 13) are computed
      //    on I/O writes ($FFA0-$FFAF) rather than at runtime. active_mmu_offset (0 or 8)
      //    is updated on $FF91 writes. Address translation is therefore a single table
      //    lookup and bitwise OR (4 instructions on ARM Cortex-M33).
      //
      // NOTE: If running a different OS/environment than Tandy CoCo 3 Color BASIC or
      // NitrOS-9 Level 2 (e.g. diagnostic software that deliberately runs in SAM mode
      // with non-default banks, disables MC3, or maps a different block into Slot 7
      // while relying on MC3), this code may need to be revisited.
      return mmu_base[active_mmu_offset | ((a >> 13) & 7)] | (a & 0x1FFF);
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
    active_mmu_offset = 0;
    IOWriters[0xD4] = WriteFFD4_P1Clear;
    IOWriters[0xD5] = WriteFFD5_P1Set;
    IOWriters[0xDE] = WriteFFDE_TyClear;
    IOWriters[0xDF] = WriteFFDF_TySet;

    // Ensure CoCo 3 ROM boot sequence selects SAM Page 0 ($FFD4) rather than Page 1 ($FFD5)
    // at $C09D (coco3.asm:8854). On a real CoCo 3 with GIME, $FFD5 is ignored.
    // On Centipede plugged into a CoCo 2 motherboard, writing $FFD5 sets SAM P1=1,
    // which diverts CPU writes away from motherboard DRAM Bank 0 (where VDG reads video text).
#if BECOME_COCO3
    coco3_rom[0xC09D - 0x8000 + 1] = 0x5C; // STA $-04,U (write $FFD4: P1=0) instead of $-03,U ($FFD5: P1=1)

    // Insert CLRA; TFR A, DP into the 5 dummy NOPs at $C041..$C045 (coco3.asm:8807-8811)
    // to guarantee DP = 0 on cold boot:
    coco3_rom[0xC041 - 0x8000] = 0x4F;  // CLRA
    coco3_rom[0xC042 - 0x8000] = 0x1F;  // TFR
    coco3_rom[0xC043 - 0x8000] = 0x8B;  // A, DP

    // Patch at $A05B in coco3_rom:
    // In CoCo 2 Color BASIC, $A05E was TFR B, DP ($1F $9B).
    // Tandy replaced it in CoCo 3 with JSR >$8C2E; JMP >$SC000, and at $A05B jumped over it:
    //   $A05B: 7E A0 72 (JMP >$A072, where $A072 is JMP ,Y)
    // CoCo 3 BASIC thus relied on hardware reset clearing DP to 0, which does not happen
    // when Centipede jumps into BASIC without a CPU hardware reset.
    // Replace $A05B..$A05F with:
    //   $A05B: 4F        CLRA
    //   $A05C: 1F 8B     TFR A, DP
    //   $A05E: 6E A4     JMP ,Y
    coco3_rom[0xA05B - 0x8000]     = 0x4F;  // CLRA
    coco3_rom[0xA05B - 0x8000 + 1] = 0x1F;  // TFR
    coco3_rom[0xA05B - 0x8000 + 2] = 0x8B;  // A, DP
    coco3_rom[0xA05B - 0x8000 + 3] = 0x6E;  // JMP
    coco3_rom[0xA05B - 0x8000 + 4] = 0xA4;  // ,Y

    // Patch at $A087 in coco3_rom (BACDST):
    // Replace BRA $A093 ($20 $0A) with CLRA; TFR A, DP; BRA $A093 ($20 $07)
    // using the 10 dummy NOPs at $A089..$A092:
    coco3_rom[0xA087 - 0x8000]     = 0x4F;  // CLRA
    coco3_rom[0xA087 - 0x8000 + 1] = 0x1F;  // TFR
    coco3_rom[0xA087 - 0x8000 + 2] = 0x8B;  // A, DP
    coco3_rom[0xA087 - 0x8000 + 3] = 0x20;  // BRA
    coco3_rom[0xA087 - 0x8000 + 4] = 0x07;  // relative offset to $A093

    // Pre-populate RAM blocks 12..15 ($18000..$1FFFF) with the 32KB CoCo 3 ROM.
    // In Task 0, slots 4..7 map to blocks 12..15 ($8000..$FFFF).
    for (uint i = 0; i < 0x8000; i++) {
      ram[0x18000 + i] = coco3_rom[i];
    }
    if (centipede_config.rom_disk11) {
      for (uint i = 0; i < 0x2000; i++) {
        ram[0x1C000 + i] = disk11_rom[i];
      }
    }


    // Pre-populate interrupt jump vectors (INTIMAGE) in Block 15 ($FEED..$FEFD).
    // Validity flag at $1FEED remains 0 so BASIC forces cold start,
    // but vectors at $1FEEE..$1FEFD are valid in case of early interrupt.
    for (uint i = 0; i < 19; i++) {
      ram[0x1FEED + i] = coco3_rom[0x4359 + i];
    }
    ram[0x1FEED] = 0;
    ram[0x10071] = 0;
    ram[0x10072] = 0;
    ram[0x10073] = 0;
    // Copy hardware vectors to MMU Block 15 ($FFF0-$FFFF) in RAM
    for (uint i = 0; i < 16; i++) {
      ram[0x1FFF0 + i] = coco3_rom[0x7FF0 + i];
    }
#endif
    ram[0x0071] = 0;
    ram[0x0072] = 0;
    ram[0x0073] = 0;
    ram[0xFEED] = 0;

    // Reset GIME registers to defaults
    gime_init0 = 0x0A;
    gime_init1 = 0;
    gime_irqenr = 0;
    gime_firqenr = 0;

    static const uint8_t default_task0[8] = {0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
    static const uint8_t default_task1[8] = {0x38, 0x30, 0x31, 0x32, 0x33, 0x3D, 0x35, 0x3F};
    for (int i = 0; i < 8; i++) {
      mmu_task[0][i] = default_task0[i];
      mmu_task[1][i] = default_task1[i];
      mmu_base[i] = (uint32_t)(default_task0[i] & 0x0F) << 13;
      mmu_base[8 + i] = (uint32_t)(default_task1[i] & 0x0F) << 13;
    }
    for (int i = 0; i < 16; i++) {
      gime_palette[i] = 0x12; // CoCo 3 default boot palette color (green)
      ram[0xFFB0 + i] = 0x12;
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

  static void IN_RAM WriteOtherSamBit(uint a, byte d) {
    bool odd = a & 1;
    uint bitnum = (a - 0xFFC0) >> 1;
  }

  static void IN_RAM WriteFFD4_P1Clear(uint a, byte d) {
    SamP1Bit = false;
  }
  static void IN_RAM WriteFFD5_P1Set(uint a, byte d) {
    SamP1Bit = true;
  }
  static void IN_RAM WriteFFDE_TyClear(uint a, byte d) {
    SamTyBit = false;
  }
  static void IN_RAM WriteFFDF_TySet(uint a, byte d) {
    SamTyBit = true;
  }

  static void IN_RAM WriteFF90_Init0(uint a, byte d) {
    gime_init0 = d;
  }
  static byte IN_RAM ReadFF90_Init0(uint a) {
    return gime_init0;
  }

  static void IN_RAM WriteFF91_Init1(uint a, byte d) {
    gime_init1 = d;
    active_mmu_offset = (d & 1) ? 8 : 0;
  }
  static byte IN_RAM ReadFF91_Init1(uint a) {
    return gime_init1;
  }

  static void IN_RAM WriteFF92_IrqEnr(uint a, byte d) { gime_irqenr = d; }
  static byte IN_RAM ReadFF92_IrqEnr(uint a) { return gime_irqenr; }

  static void IN_RAM WriteFF93_FirqEnr(uint a, byte d) { gime_firqenr = d; }
  static byte IN_RAM ReadFF93_FirqEnr(uint a) { return gime_firqenr; }

  static void IN_RAM WriteFF94_TimerMsb(uint a, byte d) { gime_timer_msb = d & 0x0F; }
  static byte IN_RAM ReadFF94_TimerMsb(uint a) { return gime_timer_msb; }

  static void IN_RAM WriteFF95_TimerLsb(uint a, byte d) { gime_timer_lsb = d; }
  static byte IN_RAM ReadFF95_TimerLsb(uint a) { return gime_timer_lsb; }

  static void IN_RAM WriteFF98_Vmode(uint a, byte d) { gime_vmode = d; }
  static byte IN_RAM ReadFF98_Vmode(uint a) { return gime_vmode; }

  static void IN_RAM WriteFF99_Vres(uint a, byte d) { gime_vres = d; }
  static byte IN_RAM ReadFF99_Vres(uint a) { return gime_vres; }

  static void IN_RAM WriteFF9A_Brdr(uint a, byte d) { gime_brdr = d; }
  static byte IN_RAM ReadFF9A_Brdr(uint a) { return gime_brdr; }

  static void IN_RAM WriteFF9B_Vscroll(uint a, byte d) { gime_vscroll = d; }
  static byte IN_RAM ReadFF9B_Vscroll(uint a) { return gime_vscroll; }

  static void IN_RAM WriteFF9C_Hscroll(uint a, byte d) { gime_hscroll = d; }
  static byte IN_RAM ReadFF9C_Hscroll(uint a) { return gime_hscroll; }

  static void IN_RAM WriteFF9D_VoffMsb(uint a, byte d) { gime_voff_msb = d; }
  static byte IN_RAM ReadFF9D_VoffMsb(uint a) { return gime_voff_msb; }

  static void IN_RAM WriteFF9E_VoffLsb(uint a, byte d) { gime_voff_lsb = d; }
  static byte IN_RAM ReadFF9E_VoffLsb(uint a) { return gime_voff_lsb; }

  static void IN_RAM WriteFFAx_MMU(uint a, byte d) {
    uint reg = a & 0x0F;
    uint task = (reg >> 3) & 1;
    uint slot = reg & 7;
    mmu_task[task][slot] = d & 0x3F;
    mmu_base[reg] = (uint32_t)(d & 0x0F) << 13;
  }
  static byte IN_RAM ReadFFAx_MMU(uint a) {
    uint reg = a & 0x0F;
    uint task = (reg >> 3) & 1;
    uint slot = reg & 7;
    return mmu_task[task][slot];
  }

  static void IN_RAM WriteFFBx_Palette(uint a, byte d) {
    gime_palette[a & 0x0F] = d & 0x3F;
    ram[a] = d & 0x3F;
  }
  static byte IN_RAM ReadFFBx_Palette(uint a) {
    return gime_palette[a & 0x0F];
  }
};

#endif  // CENTIPEDE_FIRMWARE_COCO128K_H_
