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

void IN_RAM Coco128k_WriteOtherSamBit(uint a, byte d);
void IN_RAM Coco128k_WriteFFD4_P1Clear(uint a, byte d);
void IN_RAM Coco128k_WriteFFD5_P1Set(uint a, byte d);
void IN_RAM Coco128k_WriteFFDE_TyClear(uint a, byte d);
void IN_RAM Coco128k_WriteFFDF_TySet(uint a, byte d);
void IN_RAM Coco128k_WriteFF90_Init0(uint a, byte d);
byte IN_RAM Coco128k_ReadFF90_Init0(uint a);
void IN_RAM Coco128k_WriteFF91_Init1(uint a, byte d);
byte IN_RAM Coco128k_ReadFF91_Init1(uint a);
void IN_RAM Coco128k_WriteFF92_IrqEnr(uint a, byte d);
byte IN_RAM Coco128k_ReadFF92_IrqEnr(uint a);
void IN_RAM Coco128k_WriteFF93_FirqEnr(uint a, byte d);
byte IN_RAM Coco128k_ReadFF93_FirqEnr(uint a);
void IN_RAM Coco128k_WriteFF94_TimerMsb(uint a, byte d);
byte IN_RAM Coco128k_ReadFF94_TimerMsb(uint a);
void IN_RAM Coco128k_WriteFF95_TimerLsb(uint a, byte d);
byte IN_RAM Coco128k_ReadFF95_TimerLsb(uint a);
void IN_RAM Coco128k_WriteFF98_Vmode(uint a, byte d);
byte IN_RAM Coco128k_ReadFF98_Vmode(uint a);
void IN_RAM Coco128k_WriteFF99_Vres(uint a, byte d);
byte IN_RAM Coco128k_ReadFF99_Vres(uint a);
void IN_RAM Coco128k_WriteFF9A_Brdr(uint a, byte d);
byte IN_RAM Coco128k_ReadFF9A_Brdr(uint a);
void IN_RAM Coco128k_WriteFF9B_Vscroll(uint a, byte d);
byte IN_RAM Coco128k_ReadFF9B_Vscroll(uint a);
void IN_RAM Coco128k_WriteFF9C_Hscroll(uint a, byte d);
byte IN_RAM Coco128k_ReadFF9C_Hscroll(uint a);
void IN_RAM Coco128k_WriteFF9D_VoffMsb(uint a, byte d);
byte IN_RAM Coco128k_ReadFF9D_VoffMsb(uint a);
void IN_RAM Coco128k_WriteFF9E_VoffLsb(uint a, byte d);
byte IN_RAM Coco128k_ReadFF9E_VoffLsb(uint a);
void IN_RAM Coco128k_WriteFFAx_MMU(uint a, byte d);
byte IN_RAM Coco128k_ReadFFAx_MMU(uint a);
void IN_RAM Coco128k_WriteFFBx_Palette(uint a, byte d);
byte IN_RAM Coco128k_ReadFFBx_Palette(uint a);

template <typename T>
struct DoCoco128k {
  static constexpr bool HasCoco128k() { return true; }
  static constexpr bool HasCoco64k() { return true; }

  FORCE_INLINE static bool IsCoco3Rom(uint a) {
    // In CoCo 3:
    // $E000-$FDFF is the Super Extended Color BASIC ROM (Block 15 / Slot 7).
    // It contains NO RAM patches and must always be served from immutable coco3_rom[]
    // when Task 0 has Slot 7 mapped to ROM (Block >= $3C). This prevents spurious bus
    // write glitches from corrupting routines like HPRINT ($EF7F) in RAM.
    if (0xE000 <= a && a < 0xFE00) {
      return (active_mmu_offset == 0) && (mmu_task[0][7] >= 0x3C);
    }
    // For $8000-$DFFF:
    // When SamTyBit == 0 (during boot before RAM copy), reads come from ROM.
    // When SamTyBit == 1 (all-RAM mode), reads MUST come from RAM (ram[atrans])
    // because CoCo 3 cold boot applies 27 essential patches into RAM at $8000-$BFFF
    // (such as Patch 4 at $8150 hooking HSCREEN/WIDTH/PALETTE into Super Extended BASIC).
    // The constant $FE page ($FE00-$FEFF) is always RAM (Block 15).
    return !SamTyBit && (0x8000 <= a) && (a < 0xE000);
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
      IOWriters[255 & a] = Coco128k_WriteOtherSamBit;
    }

    SamP1Bit = false;
    SamTyBit = false;
    active_mmu_offset = 0;
    IOWriters[0xD4] = Coco128k_WriteFFD4_P1Clear;
    IOWriters[0xD5] = Coco128k_WriteFFD5_P1Set;
    IOWriters[0xDE] = Coco128k_WriteFFDE_TyClear;
    IOWriters[0xDF] = Coco128k_WriteFFDF_TySet;

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
    IOWriters[0x90] = Coco128k_WriteFF90_Init0;
    IOReaders[0x90] = Coco128k_ReadFF90_Init0;
    IOWriters[0x91] = Coco128k_WriteFF91_Init1;
    IOReaders[0x91] = Coco128k_ReadFF91_Init1;
    IOWriters[0x92] = Coco128k_WriteFF92_IrqEnr;
    IOReaders[0x92] = Coco128k_ReadFF92_IrqEnr;
    IOWriters[0x93] = Coco128k_WriteFF93_FirqEnr;
    IOReaders[0x93] = Coco128k_ReadFF93_FirqEnr;
    IOWriters[0x94] = Coco128k_WriteFF94_TimerMsb;
    IOReaders[0x94] = Coco128k_ReadFF94_TimerMsb;
    IOWriters[0x95] = Coco128k_WriteFF95_TimerLsb;
    IOReaders[0x95] = Coco128k_ReadFF95_TimerLsb;

    IOWriters[0x98] = Coco128k_WriteFF98_Vmode;
    IOReaders[0x98] = Coco128k_ReadFF98_Vmode;
    IOWriters[0x99] = Coco128k_WriteFF99_Vres;
    IOReaders[0x99] = Coco128k_ReadFF99_Vres;
    IOWriters[0x9A] = Coco128k_WriteFF9A_Brdr;
    IOReaders[0x9A] = Coco128k_ReadFF9A_Brdr;
    IOWriters[0x9B] = Coco128k_WriteFF9B_Vscroll;
    IOReaders[0x9B] = Coco128k_ReadFF9B_Vscroll;
    IOWriters[0x9C] = Coco128k_WriteFF9C_Hscroll;
    IOReaders[0x9C] = Coco128k_ReadFF9C_Hscroll;
    IOWriters[0x9D] = Coco128k_WriteFF9D_VoffMsb;
    IOReaders[0x9D] = Coco128k_ReadFF9D_VoffMsb;
    IOWriters[0x9E] = Coco128k_WriteFF9E_VoffLsb;
    IOReaders[0x9E] = Coco128k_ReadFF9E_VoffLsb;

    // MMU Registers $FFA0-$FFAF
    for (uint a = 0xA0; a <= 0xAF; a++) {
      IOWriters[a] = Coco128k_WriteFFAx_MMU;
      IOReaders[a] = Coco128k_ReadFFAx_MMU;
    }

    // Palette Registers $FFB0-$FFBF
    for (uint a = 0xB0; a <= 0xBF; a++) {
      IOWriters[a] = Coco128k_WriteFFBx_Palette;
      IOReaders[a] = Coco128k_ReadFFBx_Palette;
    }
  }

  FORCE_INLINE static void DoWriteOtherSamBit(uint a, byte d) {
    bool odd = a & 1;
    uint bitnum = (a - 0xFFC0) >> 1;
    (void)odd;
    (void)bitnum;
  }

  FORCE_INLINE static void DoWriteFFD4_P1Clear(uint a, byte d) {
    SamP1Bit = false;
  }
  FORCE_INLINE static void DoWriteFFD5_P1Set(uint a, byte d) {
    SamP1Bit = true;
  }
  FORCE_INLINE static void DoWriteFFDE_TyClear(uint a, byte d) {
    SamTyBit = false;
  }
  FORCE_INLINE static void DoWriteFFDF_TySet(uint a, byte d) {
    SamTyBit = true;
  }

  FORCE_INLINE static void DoWriteFF90_Init0(uint a, byte d) {
    gime_init0 = d;
  }
  FORCE_INLINE static byte DoReadFF90_Init0(uint a) {
    return gime_init0;
  }

  FORCE_INLINE static void DoWriteFF91_Init1(uint a, byte d) {
    gime_init1 = d;
    active_mmu_offset = (d & 1) ? 8 : 0;
  }
  FORCE_INLINE static byte DoReadFF91_Init1(uint a) {
    return gime_init1;
  }

  FORCE_INLINE static void DoWriteFF92_IrqEnr(uint a, byte d) { gime_irqenr = d; }
  FORCE_INLINE static byte DoReadFF92_IrqEnr(uint a) { return gime_irqenr; }

  FORCE_INLINE static void DoWriteFF93_FirqEnr(uint a, byte d) { gime_firqenr = d; }
  FORCE_INLINE static byte DoReadFF93_FirqEnr(uint a) { return gime_firqenr; }

  FORCE_INLINE static void DoWriteFF94_TimerMsb(uint a, byte d) { gime_timer_msb = d & 0x0F; }
  FORCE_INLINE static byte DoReadFF94_TimerMsb(uint a) { return gime_timer_msb; }

  FORCE_INLINE static void DoWriteFF95_TimerLsb(uint a, byte d) { gime_timer_lsb = d; }
  FORCE_INLINE static byte DoReadFF95_TimerLsb(uint a) { return gime_timer_lsb; }

  FORCE_INLINE static void DoWriteFF98_Vmode(uint a, byte d) { gime_vmode = d; }
  FORCE_INLINE static byte DoReadFF98_Vmode(uint a) { return gime_vmode; }

  FORCE_INLINE static void DoWriteFF99_Vres(uint a, byte d) { gime_vres = d; }
  FORCE_INLINE static byte DoReadFF99_Vres(uint a) { return gime_vres; }

  FORCE_INLINE static void DoWriteFF9A_Brdr(uint a, byte d) { gime_brdr = d; }
  FORCE_INLINE static byte DoReadFF9A_Brdr(uint a) { return gime_brdr; }

  FORCE_INLINE static void DoWriteFF9B_Vscroll(uint a, byte d) { gime_vscroll = d; }
  FORCE_INLINE static byte DoReadFF9B_Vscroll(uint a) { return gime_vscroll; }

  FORCE_INLINE static void DoWriteFF9C_Hscroll(uint a, byte d) { gime_hscroll = d; }
  FORCE_INLINE static byte DoReadFF9C_Hscroll(uint a) { return gime_hscroll; }

  FORCE_INLINE static void DoWriteFF9D_VoffMsb(uint a, byte d) { gime_voff_msb = d; }
  FORCE_INLINE static byte DoReadFF9D_VoffMsb(uint a) { return gime_voff_msb; }

  FORCE_INLINE static void DoWriteFF9E_VoffLsb(uint a, byte d) { gime_voff_lsb = d; }
  FORCE_INLINE static byte DoReadFF9E_VoffLsb(uint a) { return gime_voff_lsb; }

  FORCE_INLINE static void DoWriteFFAx_MMU(uint a, byte d) {
    uint reg = a & 0x0F;
    uint task = (reg >> 3) & 1;
    uint slot = reg & 7;
    mmu_task[task][slot] = d & 0x3F;
#if FUZIX_COCO2CENTIPEDE
    mmu_base[reg] = (uint32_t)(d & (centipede_config.fuzix ? 0x1F : 0x0F)) << 13;
#else
    mmu_base[reg] = (uint32_t)(d & 0x0F) << 13;
#endif
  }
  FORCE_INLINE static byte DoReadFFAx_MMU(uint a) {
    uint reg = a & 0x0F;
    uint task = (reg >> 3) & 1;
    uint slot = reg & 7;
    return mmu_task[task][slot];
  }

  FORCE_INLINE static void DoWriteFFBx_Palette(uint a, byte d) {
    gime_palette[a & 0x0F] = d & 0x3F;
    ram[a] = d & 0x3F;
  }
  FORCE_INLINE static byte DoReadFFBx_Palette(uint a) {
    return gime_palette[a & 0x0F];
  }
};

void IN_RAM Coco128k_WriteOtherSamBit(uint a, byte d) { DoCoco128k<void>::DoWriteOtherSamBit(a, d); }
void IN_RAM Coco128k_WriteFFD4_P1Clear(uint a, byte d) { DoCoco128k<void>::DoWriteFFD4_P1Clear(a, d); }
void IN_RAM Coco128k_WriteFFD5_P1Set(uint a, byte d) { DoCoco128k<void>::DoWriteFFD5_P1Set(a, d); }
void IN_RAM Coco128k_WriteFFDE_TyClear(uint a, byte d) { DoCoco128k<void>::DoWriteFFDE_TyClear(a, d); }
void IN_RAM Coco128k_WriteFFDF_TySet(uint a, byte d) { DoCoco128k<void>::DoWriteFFDF_TySet(a, d); }
void IN_RAM Coco128k_WriteFF90_Init0(uint a, byte d) { DoCoco128k<void>::DoWriteFF90_Init0(a, d); }
byte IN_RAM Coco128k_ReadFF90_Init0(uint a) { return DoCoco128k<void>::DoReadFF90_Init0(a); }
void IN_RAM Coco128k_WriteFF91_Init1(uint a, byte d) { DoCoco128k<void>::DoWriteFF91_Init1(a, d); }
byte IN_RAM Coco128k_ReadFF91_Init1(uint a) { return DoCoco128k<void>::DoReadFF91_Init1(a); }
void IN_RAM Coco128k_WriteFF92_IrqEnr(uint a, byte d) { DoCoco128k<void>::DoWriteFF92_IrqEnr(a, d); }
byte IN_RAM Coco128k_ReadFF92_IrqEnr(uint a) { return DoCoco128k<void>::DoReadFF92_IrqEnr(a); }
void IN_RAM Coco128k_WriteFF93_FirqEnr(uint a, byte d) { DoCoco128k<void>::DoWriteFF93_FirqEnr(a, d); }
byte IN_RAM Coco128k_ReadFF93_FirqEnr(uint a) { return DoCoco128k<void>::DoReadFF93_FirqEnr(a); }
void IN_RAM Coco128k_WriteFF94_TimerMsb(uint a, byte d) { DoCoco128k<void>::DoWriteFF94_TimerMsb(a, d); }
byte IN_RAM Coco128k_ReadFF94_TimerMsb(uint a) { return DoCoco128k<void>::DoReadFF94_TimerMsb(a); }
void IN_RAM Coco128k_WriteFF95_TimerLsb(uint a, byte d) { DoCoco128k<void>::DoWriteFF95_TimerLsb(a, d); }
byte IN_RAM Coco128k_ReadFF95_TimerLsb(uint a) { return DoCoco128k<void>::DoReadFF95_TimerLsb(a); }
void IN_RAM Coco128k_WriteFF98_Vmode(uint a, byte d) { DoCoco128k<void>::DoWriteFF98_Vmode(a, d); }
byte IN_RAM Coco128k_ReadFF98_Vmode(uint a) { return DoCoco128k<void>::DoReadFF98_Vmode(a); }
void IN_RAM Coco128k_WriteFF99_Vres(uint a, byte d) { DoCoco128k<void>::DoWriteFF99_Vres(a, d); }
byte IN_RAM Coco128k_ReadFF99_Vres(uint a) { return DoCoco128k<void>::DoReadFF99_Vres(a); }
void IN_RAM Coco128k_WriteFF9A_Brdr(uint a, byte d) { DoCoco128k<void>::DoWriteFF9A_Brdr(a, d); }
byte IN_RAM Coco128k_ReadFF9A_Brdr(uint a) { return DoCoco128k<void>::DoReadFF9A_Brdr(a); }
void IN_RAM Coco128k_WriteFF9B_Vscroll(uint a, byte d) { DoCoco128k<void>::DoWriteFF9B_Vscroll(a, d); }
byte IN_RAM Coco128k_ReadFF9B_Vscroll(uint a) { return DoCoco128k<void>::DoReadFF9B_Vscroll(a); }
void IN_RAM Coco128k_WriteFF9C_Hscroll(uint a, byte d) { DoCoco128k<void>::DoWriteFF9C_Hscroll(a, d); }
byte IN_RAM Coco128k_ReadFF9C_Hscroll(uint a) { return DoCoco128k<void>::DoReadFF9C_Hscroll(a); }
void IN_RAM Coco128k_WriteFF9D_VoffMsb(uint a, byte d) { DoCoco128k<void>::DoWriteFF9D_VoffMsb(a, d); }
byte IN_RAM Coco128k_ReadFF9D_VoffMsb(uint a) { return DoCoco128k<void>::DoReadFF9D_VoffMsb(a); }
void IN_RAM Coco128k_WriteFF9E_VoffLsb(uint a, byte d) { DoCoco128k<void>::DoWriteFF9E_VoffLsb(a, d); }
byte IN_RAM Coco128k_ReadFF9E_VoffLsb(uint a) { return DoCoco128k<void>::DoReadFF9E_VoffLsb(a); }
void IN_RAM Coco128k_WriteFFAx_MMU(uint a, byte d) { DoCoco128k<void>::DoWriteFFAx_MMU(a, d); }
byte IN_RAM Coco128k_ReadFFAx_MMU(uint a) { return DoCoco128k<void>::DoReadFFAx_MMU(a); }
void IN_RAM Coco128k_WriteFFBx_Palette(uint a, byte d) { DoCoco128k<void>::DoWriteFFBx_Palette(a, d); }
byte IN_RAM Coco128k_ReadFFBx_Palette(uint a) { return DoCoco128k<void>::DoReadFFBx_Palette(a); }

#endif  // CENTIPEDE_FIRMWARE_COCO128K_H_
