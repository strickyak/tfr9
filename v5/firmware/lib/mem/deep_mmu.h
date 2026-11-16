#pragma once
// deep_mmu.h -- Deep65280 MMU state and address translation.
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// ═══════════════════════════════════════════════════════════════════
// Deep65280 MMU State & Translation
// ═══════════════════════════════════════════════════════════════════
static uint8_t deep65280_mmu[2][8];
static uint8_t deep65280_active_task = 0;
static bool g_gime_mmu_enabled = true;
static uint32_t task_mmu_base[8];

static inline void IN_RAM update_task_mmu() {
  for (int s = 0; s < 8; s++) {
    uint8_t blk = g_gime_mmu_enabled ? deep65280_mmu[deep65280_active_task][s] : (uint8_t)(0x38 + s);
#if DEEP_256K
    // 256 KB physical RAM: 32 blocks ($00..$1F). MMU pages $00..$1F shadow $20..$3F.
    task_mmu_base[s] = (uint32_t)(blk & 0x1F) << 13;
#else
    // 128 KB physical RAM: 16 blocks ($00..$0F).
    task_mmu_base[s] = (uint32_t)(blk & 0x0F) << 13;
#endif
  }
}

static inline uint32_t IN_RAM translate_deep_addr(uint16_t logical) {
  if (logical >= 0xFE00 && logical < 0xFF00) {
    // Locked trampoline page ($FE00..$FEFF) hardwired to physical block $3F:
    // Block 15 (0x1E000) for 128KB, Block 31 (0x3E000) for 256KB
#if DEEP_256K
    return 0x3E000 | (logical & 0x1FFF);
#else
    return 0x1E000 | (logical & 0x1FFF);
#endif
  }
  return task_mmu_base[logical >> 13] | (logical & 0x1FFF);
}

