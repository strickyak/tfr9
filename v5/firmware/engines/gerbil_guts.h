#pragma once
// gerbil_guts.h -- Gerbil PIO bus-cycle engines for Centipede 32z (core 1 foreground, IN_RAM).
//
// Spec section 5.5 / 7 / Appendix C:
// Mirrors `firmware/engines/guts.h` (Hamster) for the CoCo 40-pin cartridge bus (`gerbil.pio`),
// instantiated into four dedicated IN_RAM core-1 loops with zero per-cycle engine branches:
//   - gerbil_sam64k_foreground  (CoCo 2 64 KB RAM substitution + Disk 1.1 ROM + floppy + IODriveMask)
//   - gerbil_gime_foreground    (CoCo 3 128K/256K MMU + GIME + Palette + ROM/OS + IODriveMask)
//   - gerbil_gep9_foreground    (Hatvan 256-byte paged Gep9MMU + RTI TaskFuse + capability-gated IODriveMask)
//   - gerbil_io_foreground      (Pure I/O cartridge engine for real CoCo 3 + IODriveMask)
//
// Core 1 runs strictly out of SRAM with zero calls into XIP flash (`scripts/check-core1-sram.sh -s`).

#ifndef GERBIL_HIGH_WATERMARK
#define GERBIL_HIGH_WATERMARK 220
#endif
#ifndef GERBIL_LOW_WATERMARK
#define GERBIL_LOW_WATERMARK 100
#endif

// Staging slices in the top 40 KB of the 256 KB `ram[]` array for 64K/128K BASIC modes
// so core 1 reads Disk 1.1 ROM and CoCo 3 ROM from zero-wait-state SRAM without wasting 40 KB of static .data!
static inline byte* const sram_coco3_rom  = ram + (216 * 1024);  // 32 KB: 216 KB .. 248 KB
static inline byte* const sram_disk11_rom = ram + (248 * 1024);  //  8 KB: 248 KB .. 256 KB

// Shared foreground state configured on core 0 before `BG2FG_EXIT_CONSOLE`:
volatile bool SamTyBit = false;
volatile bool SamP1Bit = false;
volatile byte g_pia0_ff02 = 0xFF;
bool g_sam64k_ram_enabled = true;
bool g_disk11_rom_enabled = false;
bool g_floppy_enabled = false;
bool g_gime_rom_mode = false;
bool fg_halt_for_flow_control = false;
volatile uint nmi_fuse = 0;
volatile uint cart_fuse = 0;

struct CycleRecord {
  uint16_t abus;
  uint8_t dbus;
  uint8_t flags;  // bit 0: reading
};
volatile CycleRecord g_cycle_history[256];
volatile uint8_t g_cycle_history_idx = 0;
volatile bool g_freeze_cycles = false;

FORCE_INLINE void IN_RAM GerbilAssertHalt() {
  gpio_set_dir(G_HALT, GPIO_OUT);
  gpio_put(G_LED, 1);
}

FORCE_INLINE void IN_RAM GerbilReleaseHalt() {
  gpio_set_dir(G_HALT, GPIO_IN);
  gpio_put(G_LED, 0);
}

FORCE_INLINE void IN_RAM GerbilAssertCart() {
  gpio_set_dir(G_CART, GPIO_OUT);
  cart_fuse = 6;
}

FORCE_INLINE void IN_RAM GerbilFlowControlCheck() {
  uint sz = fg2bg.size();
  if (!fg_halt_for_flow_control) {
    if (sz >= GERBIL_HIGH_WATERMARK) {
      fg_halt_for_flow_control = true;
      GerbilAssertHalt();
    }
  } else {
    if (sz <= GERBIL_LOW_WATERMARK) {
      fg_halt_for_flow_control = false;
      GerbilReleaseHalt();
    }
  }
}

FORCE_INLINE void IN_RAM GerbilPushRead(uint abus, byte dbus) {
  if (abus != 0xFFFF) {
    (void)fg2bg.push(((uint)FG2BG_READ << 24) | (abus << 8) | dbus);
  }
}

FORCE_INLINE void IN_RAM GerbilPushWrite(uint abus, byte dbus) {
  (void)fg2bg.push(((uint)FG2BG_WRITE << 24) | (abus << 8) | dbus);
}

FORCE_INLINE void IN_RAM ShadowSamAndPiaWrite(uint abus, byte dbus) {
  if ((abus & 0xFFE3u) == 0xFF02u) {
    g_pia0_ff02 = dbus;
    ram[0xFF02] = dbus;
  } else if (abus >= 0xFFC0u && abus <= 0xFFDFu) {
    ram[abus] = dbus;
    switch (abus & 0xFFu) {
      case 0xD4: SamP1Bit = false; break;
      case 0xD5: SamP1Bit = true;  break;
      case 0xDE: SamTyBit = false; break;
      case 0xDF: SamTyBit = true;  break;
      default: break;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// Four Gerbil Bus Policies
// ═══════════════════════════════════════════════════════════════════

// 1. gerbil_sam64k: CoCo 2 64 KB RAM substitution + Disk 1.1 ROM + floppy + keyboard injector + IODriveMask.
//    Serves `on coco2 run rsbasic for coco2`, DECB preload, and `on coco2 run nitros9l1 for flat65280`.
struct GerbilSam64kPolicy {
  static constexpr bool kHasGimeTimer = false;

  static FORCE_INLINE bool Read(uint, uint abus, byte& dbus) {
    if (likely(abus < 0x8000u)) {
      if (likely(g_sam64k_ram_enabled)) {
        dbus = ram[abus + (SamP1Bit ? 0x8000u : 0u)];
        return true;
      }
      return false;
    }

    if (likely(abus < 0xFF00u)) {
      if (SamTyBit && g_sam64k_ram_enabled) {
        dbus = ram[abus];
        return true;
      }
      // Decode $C000..$DFFF directly from `abus` (on CoCo 2, SAM gates CTS# with E, after rising Q).
      if (g_disk11_rom_enabled && 0xC000u <= abus && abus < 0xE000u) {
        dbus = sram_disk11_rom[abus - 0xC000u];
        return true;
      }
      return false;
    }

    uint8_t lo = (uint8_t)(abus & 0xFFu);
    if (IoIsDriven(lo)) {
      IOReader fn = IOReaders[lo];
      dbus = fn ? fn(abus) : 0xFF;
      return true;
    }
    if (unlikely(keyboard_injector::active && (abus & 0xFFE3u) == 0xFF00u)) {
      dbus = keyboard_injector::probe_table[keyboard_injector::active_table_idx][g_pia0_ff02];
      return true;
    }
    if (unlikely(g_floppy_enabled && 0x40u <= lo && lo < 0x60u)) {
      FloppyImpl::ReadScsFloppy(abus, dbus);
      return true;
    }
    return false;
  }

  static FORCE_INLINE void Write(uint, uint abus, byte dbus) {
    if (likely(abus < 0x8000u)) {
      ram[abus + (SamP1Bit ? 0x8000u : 0u)] = dbus;
      return;
    }
    if (likely(abus < 0xFF00u)) {
      ram[abus] = dbus;
      return;
    }

    uint8_t lo = (uint8_t)(abus & 0xFFu);
    if (IoIsDriven(lo)) {
      IOWriter fn = IOWriters[lo];
      if (fn) fn(abus, dbus);
      return;
    }
    if (unlikely(g_floppy_enabled && 0x40u <= lo && lo < 0x60u)) {
      ram[abus] = dbus;
      FloppyImpl::WriteScsFloppy(abus, dbus);
      return;
    }
    ShadowSamAndPiaWrite(abus, dbus);
  }
};

// 2. gerbil_gime: CoCo 3 128 KB / 256 KB MMU + GIME + Palette + ROM/OS + floppy + keyboard injector + IODriveMask.
//    Serves `on coco2 run rsbasic for coco3` ("Become CoCo 3"), `nitros9l2` (`deep65280`, `coco3`), and `fuzix`.
struct GerbilGimePolicy {
  static constexpr bool kHasGimeTimer = true;

  static FORCE_INLINE bool IsProtectedCoco3Rom(uint abus) {
    if (!g_gime_rom_mode) return false;
    if (0xE000u <= abus && abus < 0xFE00u) {
      return (deep65280_active_task == 0) && (deep65280_mmu[0][7] >= 0x3Cu);
    }
    return !SamTyBit && (0x8000u <= abus && abus < 0xE000u);
  }

  static FORCE_INLINE uint32_t TranslateAddr(uint abus) {
    if (unlikely(abus >= 0xFE00u)) {
      // Constant $FE00..$FEFF page when MC3 (g_gime_init0 bit 3) is set or in 256K OS mode.
      if (!g_gime_rom_mode || (g_gime_init0 & 0x08u) != 0) {
        return g_deep_fe_base | (abus & 0x1FFFu);
      }
    }
    return task_mmu_base[(abus >> 13) & 7u] | (abus & 0x1FFFu);
  }

  static FORCE_INLINE bool Read(uint, uint abus, byte& dbus) {
    if (likely(abus < 0xFF00u)) {
      if (IsProtectedCoco3Rom(abus)) {
        // CoCo 3 GIME INIT0 ($FF90) bit 1 selects internal 32K ROM (1) vs 16K internal + 16K cart (0).
        if (g_disk11_rom_enabled && (g_gime_init0 & 0x02u) == 0 && 0xC000u <= abus && abus < 0xE000u) {
          dbus = sram_disk11_rom[abus - 0xC000u];
        } else {
          dbus = sram_coco3_rom[abus - 0x8000u];
        }
        return true;
      }
      dbus = ram[TranslateAddr(abus)];
      return true;
    }

    uint8_t lo = (uint8_t)(abus & 0xFFu);
    if (IoIsDriven(lo)) {
      IOReader fn = IOReaders[lo];
      dbus = fn ? fn(abus) : 0xFF;
      return true;
    }
    if (unlikely(keyboard_injector::active && (abus & 0xFFE3u) == 0xFF00u)) {
      dbus = keyboard_injector::probe_table[keyboard_injector::active_table_idx][g_pia0_ff02];
      return true;
    }
    if (unlikely(g_floppy_enabled && 0x40u <= lo && lo < 0x60u)) {
      FloppyImpl::ReadScsFloppy(abus, dbus);
      return true;
    }
    return false;
  }

  static FORCE_INLINE void Write(uint, uint abus, byte dbus) {
    if (likely(abus < 0xFF00u)) {
      uint32_t atrans = TranslateAddr(abus);
      if (!IsProtectedCoco3Rom(abus)) {
        ram[atrans] = dbus;
      }
      // Mirror CoCo 3 text writes in Block 8 ($10400..$105FF) to $0400..$05FF when not in bitplane graphics (BP=0)
      // so `tether --quick-get-text` works without overwriting HSCREEN pixels in Block 0!
      if (g_gime_rom_mode && (g_gime_regs[8] & 0x80u) == 0 &&
          0x0400u <= abus && abus < 0x0600u && (atrans >> 13) == 8u) {
        ram[abus] = dbus;
      }
      return;
    }

    uint8_t lo = (uint8_t)(abus & 0xFFu);
    if (IoIsDriven(lo)) {
      IOWriter fn = IOWriters[lo];
      if (fn) fn(abus, dbus);
      return;
    }
    if (unlikely(g_floppy_enabled && 0x40u <= lo && lo < 0x60u)) {
      ram[abus] = dbus;
      FloppyImpl::WriteScsFloppy(abus, dbus);
      return;
    }
    ShadowSamAndPiaWrite(abus, dbus);
  }
};

// 3. gerbil_gep9: Hatvan 256-byte paged Gep9MMU + RTI TaskFuse + capability-gated IODriveMask.
//    Serves `on coco2 run hatvan for coco2` and `on coco3 run hatvan for coco2`.
struct GerbilGep9Policy {
  static constexpr bool kHasGimeTimer = false;

  static FORCE_INLINE bool Read(uint, uint abus, byte& dbus) {
    if (unlikely(gep9_mmu.task_fuse_step == 1)) {
      gep9_mmu.current_task = gep9_mmu.task_fuse_target;
      gep9_mmu.task_fuse_armed = false;
      gep9_mmu.task_fuse_step = 0;
    }

    if (likely(abus < 0xFF00u)) {
      dbus = *gep9_mmu.TranslateRead(gep9_mmu.current_task, (uint16_t)abus);
      if (unlikely(gep9_mmu.task_fuse_armed && gep9_mmu.task_fuse_step == 0 && dbus == 0x3Bu)) {
        gep9_mmu.task_fuse_step = 1;
      }
      return true;
    }

    uint8_t lo = (uint8_t)(abus & 0xFFu);
    if (lo >= 0xF0u) {
      if (abus != 0xFFFFu && (abus & 1u) == 0) {
        gep9_mmu.current_task = 0;
      }
      dbus = *gep9_mmu.TranslateRead(0, (uint16_t)abus);
      return true;
    }
    if (IoIsDriven(lo)) {
      if (gep9_mmu.current_task == 0 ||
          (gep9_mmu.task_flags[gep9_mmu.current_task] & 0x01u) != 0 ||
          ((uint16_t)(abus - gep9_embiggen_base) < 8u)) {
        IOReader fn = IOReaders[lo];
        dbus = fn ? fn(abus) : 0;
      } else {
        dbus = 0;
      }
      return true;
    }
    // Pass unclaimed $FF00..$FF3F (PIA) and $FFC0..$FFDF (SAM) to CoCo motherboard.
    return false;
  }

  static FORCE_INLINE void Write(uint, uint abus, byte dbus) {
    if (likely(abus < 0xFF00u)) {
      *gep9_mmu.TranslateWrite(gep9_mmu.current_task, (uint16_t)abus) = dbus;
      return;
    }
    uint8_t lo = (uint8_t)(abus & 0xFFu);
    if (lo >= 0xF0u) {
      if (gep9_mmu.current_task == 0) {
        *gep9_mmu.TranslateWrite(0, (uint16_t)abus) = dbus;
      }
      return;
    }
    if (IoIsDriven(lo)) {
      if (gep9_mmu.current_task == 0 ||
          (gep9_mmu.task_flags[gep9_mmu.current_task] & 0x01u) != 0 ||
          ((uint16_t)(abus - gep9_embiggen_base) < 8u)) {
        IOWriter fn = IOWriters[lo];
        if (fn) fn(abus, dbus);
      }
      return;
    }
    ShadowSamAndPiaWrite(abus, dbus);
  }
};

// 4. gerbil_io: Pure I/O cartridge engine for a real CoCo 3 (`on coco3 ...`).
//    Motherboard owns RAM, ROM, GIME, MMU, Palette, PIA, SAM, and Vectors; RP2350 drives only IODriveMask + floppy.
struct GerbilIoPolicy {
  static constexpr bool kHasGimeTimer = false;

  static FORCE_INLINE bool Read(uint, uint abus, byte& dbus) {
    if (likely(abus < 0xFF00u)) {
      return false;
    }
    uint8_t lo = (uint8_t)(abus & 0xFFu);
    if (IoIsDriven(lo)) {
      IOReader fn = IOReaders[lo];
      dbus = fn ? fn(abus) : 0xFF;
      return true;
    }
    if (unlikely(keyboard_injector::active && (abus & 0xFFE3u) == 0xFF00u)) {
      dbus = keyboard_injector::probe_table[keyboard_injector::active_table_idx][g_pia0_ff02];
      return true;
    }
    if (unlikely(g_floppy_enabled && 0x40u <= lo && lo < 0x60u)) {
      FloppyImpl::ReadScsFloppy(abus, dbus);
      return true;
    }
    return false;
  }

  static FORCE_INLINE void Write(uint, uint abus, byte dbus) {
    if (likely(abus < 0xFF00u)) {
      // Passively shadow low 64K writes so `tether --quick-get-text` can inspect screen memory.
      ram[abus] = dbus;
      return;
    }
    uint8_t lo = (uint8_t)(abus & 0xFFu);
    if (IoIsDriven(lo)) {
      IOWriter fn = IOWriters[lo];
      if (fn) fn(abus, dbus);
      return;
    }
    if (unlikely(g_floppy_enabled && 0x40u <= lo && lo < 0x60u)) {
      ram[abus] = dbus;
      FloppyImpl::WriteScsFloppy(abus, dbus);
      return;
    }
    ShadowSamAndPiaWrite(abus, dbus);
  }
};

// ═══════════════════════════════════════════════════════════════════
// Shared Gerbil PIO Handshake Template + 4 Instantiated IN_RAM Loops
// ═══════════════════════════════════════════════════════════════════

template <typename Policy>
FORCE_INLINE void GerbilForegroundLoop() {
  const PIO pio = pio0;
  constexpr uint sm = 0;

  while (true) {
    const uint signals = gerbil_program_get_word(pio, sm);
    const bool reading = ((signals & (1u << G_RW)) != 0);
    const uint abus = sio_hw->gpio_hi_in & 0xFFFFu;
    byte dbus = 0;

    if (likely(reading)) {
      if (Policy::Read(signals, abus, dbus)) {
        gerbil_program_put_word(pio, sm, 0x100u | (uint32_t)dbus);
      } else {
        gerbil_program_put_word(pio, sm, 0x000u);
        dbus = (byte)gerbil_program_get_word(pio, sm);
      }
      if (unlikely(g_config.trace_reads)) {
        GerbilPushRead(abus, dbus);
      }
    } else {
      dbus = (byte)gerbil_program_get_word(pio, sm);
      Policy::Write(signals, abus, dbus);
      if (unlikely(g_config.trace_writes)) {
        GerbilPushWrite(abus, dbus);
      }
    }

    if (!g_freeze_cycles) {
      uint8_t h_idx = g_cycle_history_idx++;
      g_cycle_history[h_idx].abus = (uint16_t)abus;
      g_cycle_history[h_idx].dbus = dbus;
      g_cycle_history[h_idx].flags = reading ? 1u : 0u;
    }

    GerbilFlowControlCheck();

    // CoCo physical RESET button pressed -> notify Core 0 to reboot cleanly!
    if (unlikely((signals & (1u << G_RESET)) == 0)) {
      coco_running = false;
      (void)fg2bg.push((uint)FG2BG_HOST_RESET << 24);
      while (true) {
        tight_loop_contents();
      }
    }

    if (unlikely(nmi_fuse != 0)) {
      if (--nmi_fuse == 0) {
        gpio_set_dir(G_NMI, GPIO_IN);
      }
    }

    if constexpr (Policy::kHasGimeTimer) {
      if (unlikely(cart_fuse != 0)) {
        if (--cart_fuse == 0) {
          gpio_set_dir(G_CART, GPIO_IN);
        }
      }
      if (unlikely(g_gime_timer_reload != 0)) {
        if (--g_gime_timer_count == 0) {
          g_gime_timer_count = g_gime_timer_reload;
          if ((g_gime_init0 & 0x20u) && (g_gime_firqenr & 0x20u)) {
            GerbilAssertCart();
          }
        }
      }
    }
  }
}

void IN_RAM gerbil_sam64k_foreground() { GerbilForegroundLoop<GerbilSam64kPolicy>(); }
void IN_RAM gerbil_gime_foreground()   { GerbilForegroundLoop<GerbilGimePolicy>(); }
void IN_RAM gerbil_gep9_foreground()   { GerbilForegroundLoop<GerbilGep9Policy>(); }
void IN_RAM gerbil_io_foreground()     { GerbilForegroundLoop<GerbilIoPolicy>(); }

using GerbilLoopFn = void (*)();

struct GerbilEngineEntry {
  const char* name;
  GerbilLoopFn loop_fn;
};

static const GerbilEngineEntry kGerbilEngines[] = {
  {"gerbil_sam64k", gerbil_sam64k_foreground},
  {"gerbil_gime",   gerbil_gime_foreground},
  {"gerbil_gep9",   gerbil_gep9_foreground},
  {"gerbil_io",     gerbil_io_foreground},
};

static inline GerbilLoopFn FindGerbilEngine(const char* name) {
  if (!name) return nullptr;
  for (const GerbilEngineEntry& e : kGerbilEngines) {
    if (!strcmp(name, e.name)) return e.loop_fn;
  }
  return nullptr;
}
