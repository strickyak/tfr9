#pragma once
// deep_mmu.h -- CoCo 3 / Deep65280 8 KB task MMU state and address translation.
// Shared by TFR911H (`hamster_gime`) and Centipede 32z (`gerbil_gime`).

static uint8_t deep65280_mmu[2][8];
static uint8_t deep65280_active_task = 0;
static bool g_gime_mmu_enabled = true;
#if DEEP_256K
static uint8_t g_deep_mmu_mask = 0x1F;      // 32 blocks (256 KB) by default; 0x0F (128 KB) in 128K CoCo 3 BASIC mode
static uint32_t g_deep_fe_base = 0x3E000;   // Block $3F physical base (0x3E000 for 256 KB, 0x1E000 for 128 KB)
#else
static uint8_t g_deep_mmu_mask = 0x0F;
static uint32_t g_deep_fe_base = 0x1E000;
#endif
static uint32_t task_mmu_base[8];

static inline void IN_RAM update_task_mmu() {
  uint8_t mask = g_deep_mmu_mask;
  for (int s = 0; s < 8; s++) {
    uint8_t blk = g_gime_mmu_enabled ? deep65280_mmu[deep65280_active_task][s] : (uint8_t)(0x38 + s);
    task_mmu_base[s] = (uint32_t)(blk & mask) << 13;
  }
}

static inline uint32_t IN_RAM translate_deep_addr(uint16_t logical) {
  if (logical >= 0xFE00 && logical < 0xFF00) {
    return g_deep_fe_base | (logical & 0x1FFF);
  }
  return task_mmu_base[logical >> 13] | (logical & 0x1FFF);
}
