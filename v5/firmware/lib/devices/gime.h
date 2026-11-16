#pragma once
// gime.h -- CoCo 3 GIME control/IRQ/timer/shadow registers ($FF90..$FF9F) and palette ($FFB0..$FFBF).
//
// Shared by TFR/911H (`hamster_gime`) and Centipede 32z (`gerbil_gime`).
// Shares `deep65280_mmu`, `deep65280_active_task`, `g_gime_mmu_enabled`, and `update_task_mmu()`
// from `lib/mem/deep_mmu.h` so both Hamster and Gerbil GIME policies translate addresses with zero extra branches.

static volatile byte g_gime_init0 = 0x40;       // $FF90: bit 6 MMUEN, bit 5 IEN/FEN, bit 3 MC3
static volatile byte g_gime_init1 = 0x00;       // $FF91: bit 0 TR (active MMU task)
static volatile byte g_gime_irqenr = 0x00;      // $FF92: bit 3 VBORD (60 Hz vertical border IRQ)
static volatile byte g_gime_firqenr = 0x00;     // $FF93: FIRQ enable (bit 5 TMR)
static volatile uint16_t g_gime_timer_reload = 0;
static volatile uint16_t g_gime_timer_count = 0;
static volatile byte g_gime_regs[16] = {0};     // shadow registers for $FF94..$FF9F
static volatile byte g_gime_palette[16] = {0};  // 6-bit palette registers $FFB0..$FFBF
static volatile bool g_gime_irq_enabled = false;
static volatile bool g_gime_vbord_enabled = false;
static volatile bool g_gime_vbord_pending = false;
static uint16_t g_gime_base = 0xFF90;
static uint16_t g_palette_base = 0xFFB0;

FORCE_INLINE bool IN_RAM gime_irq_needed() {
  return g_gime_irq_enabled && g_gime_vbord_enabled && g_gime_vbord_pending;
}

void IN_RAM gime_tick_60hz() {
  if (g_gime_irq_enabled && g_gime_vbord_enabled) {
    g_gime_vbord_pending = true;
  }
}

byte IN_RAM coco3_gime_read_init0(uint) { return g_gime_init0; }
void IN_RAM coco3_gime_write_init0(uint, byte d) {
  g_gime_init0 = d;
  g_gime_mmu_enabled = (d & 0x40) != 0;
  g_gime_irq_enabled = (d & 0x20) != 0;
  update_task_mmu();
}

byte IN_RAM coco3_gime_read_init1(uint) { return g_gime_init1; }
void IN_RAM coco3_gime_write_init1(uint, byte d) {
  g_gime_init1 = d;
  deep65280_active_task = d & 1;
  update_task_mmu();
}

// Reading $FF92 reports currently pending & enabled IRQ bits and clears the pending latch.
byte IN_RAM coco3_gime_read_irqenr(uint) {
  byte z = (g_gime_irq_enabled && g_gime_vbord_enabled && g_gime_vbord_pending) ? 0x08 : 0x00;
  g_gime_vbord_pending = false;
  return z;
}
void IN_RAM coco3_gime_write_irqenr(uint, byte d) {
  g_gime_irqenr = d;
  g_gime_vbord_enabled = (d & 0x08) != 0;
  if (!g_gime_vbord_enabled) g_gime_vbord_pending = false;
}

byte IN_RAM coco3_gime_read_firqenr(uint) { return g_gime_firqenr; }
void IN_RAM coco3_gime_write_firqenr(uint, byte d) { g_gime_firqenr = d; }

byte IN_RAM coco3_gime_read_timer_msb(uint) { return g_gime_regs[4]; }
void IN_RAM coco3_gime_write_timer_msb(uint, byte d) {
  g_gime_regs[4] = d & 0x0F;
  g_gime_timer_reload = ((uint16_t)g_gime_regs[4] << 8) | g_gime_regs[5];
  g_gime_timer_count = g_gime_timer_reload;
}

byte IN_RAM coco3_gime_read_timer_lsb(uint) { return g_gime_regs[5]; }
void IN_RAM coco3_gime_write_timer_lsb(uint, byte d) {
  g_gime_regs[5] = d;
  g_gime_timer_reload = ((uint16_t)g_gime_regs[4] << 8) | g_gime_regs[5];
  g_gime_timer_count = g_gime_timer_reload;
}

byte IN_RAM coco3_gime_read_shadow(uint addr) {
  return g_gime_regs[(addr - g_gime_base) & 0x0F];
}
void IN_RAM coco3_gime_write_shadow(uint addr, byte d) {
  g_gime_regs[(addr - g_gime_base) & 0x0F] = d;
}

byte IN_RAM coco3_palette_read(uint addr) {
  return g_gime_palette[(addr - g_palette_base) & 0x0F];
}
void IN_RAM coco3_palette_write(uint addr, byte d) {
  uint idx = (addr - g_palette_base) & 0x0F;
  byte c = d & 0x3F;
  g_gime_palette[idx] = c;
  ram[0xFFB0 + idx] = c;  // Shadow at $FFB0..$FFBF so tether --quick-get-hscreen reads the active palette
}

struct Coco3Gime {
  static constexpr uint16_t kDefaultBase = 0xFF90;
  static void Install(uint16_t base = kDefaultBase, uint16_t len = 16, byte init0 = 0x40, byte init1 = 0x00) {
    g_gime_base = base;
    g_gime_init0 = init0;
    g_gime_init1 = init1;
    g_gime_irqenr = 0;
    g_gime_firqenr = 0;
    g_gime_timer_reload = 0;
    g_gime_timer_count = 0;
    for (int i = 0; i < 16; i++) g_gime_regs[i] = 0;
    g_gime_mmu_enabled = (init0 & 0x40) != 0;
    g_gime_irq_enabled = (init0 & 0x20) != 0;
    g_gime_vbord_enabled = false;
    g_gime_vbord_pending = false;
    deep65280_active_task = init1 & 1;
    update_task_mmu();

    IoClaim(base, (uint16_t)(base + len - 1), "gime");
    IoSet(base + 0, coco3_gime_read_init0,   coco3_gime_write_init0);
    IoSet(base + 1, coco3_gime_read_init1,   coco3_gime_write_init1);
    if (len >= 3) IoSet(base + 2, coco3_gime_read_irqenr,    coco3_gime_write_irqenr);
    if (len >= 4) IoSet(base + 3, coco3_gime_read_firqenr,   coco3_gime_write_firqenr);
    if (len >= 5) IoSet(base + 4, coco3_gime_read_timer_msb, coco3_gime_write_timer_msb);
    if (len >= 6) IoSet(base + 5, coco3_gime_read_timer_lsb, coco3_gime_write_timer_lsb);
    for (uint16_t i = 6; i < len; i++) {
      IoSet(base + i, coco3_gime_read_shadow, coco3_gime_write_shadow);
    }
    if (!g_timer_60hz_fn) {
      g_timer_60hz_fn = gime_tick_60hz;
    }
  }
};

struct Coco3Palette {
  static constexpr uint16_t kDefaultBase = 0xFFB0;
  static void Install(uint16_t base = kDefaultBase) {
    g_palette_base = base;
    for (int i = 0; i < 16; i++) {
      g_gime_palette[i] = 0x12;  // CoCo 3 default green
      ram[0xFFB0 + i] = 0x12;
    }
    IoClaim(base, base + 15, "palette");
    for (int i = 0; i < 16; i++) {
      IoSet(base + i, coco3_palette_read, coco3_palette_write);
    }
  }
};
