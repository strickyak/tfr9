#pragma once
// flat65280.h -- Engine flat65280: NitrOS-9 Level 1 I/O and setup.
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// IN_RAM RAM vector reader for FFF0..FFFF
byte IN_RAM ram_vector_reader(uint addr) {
  return ram[0xFF00 | (addr & 0xFF)];
}

// IRQ check for flat65280 / deep65280 / jamesbrown / coco3
FORCE_INLINE bool IN_RAM flat65280_irq_needed() {
  bool clock_irq = (clock_and_stop_reg & 0x01) && (clock_and_stop_reg & 0x02);
  bool acia_rx_irq = ((flat65280_acia_ctrl & 0x80) != 0) && flat65280_rx_has_char;
  bool acia_tx_irq = ((flat65280_acia_ctrl & 0x60) == 0x20);
  return clock_irq || acia_rx_irq || acia_tx_irq;
}

// CoCo 2 PIA0 VSYNC -> ClockStop/TermAcia IRQ shim ($FFE0..$FFEF by default).
// Used when running unmodified `flat65280` / `deep65280` images on a CoCo 2
// cartridge port (which has no `_IRQ` pin): `PIA0` 60 Hz VSYNC drives `_IRQ`,
// and `$FFF8:$FFF9` enters this 16-byte SRAM-backed shim to clear `$FF02` and
// either `RTI` immediately or jump to the guest's `$FFF8:$FFF9` handler when
// `flat65280_irq_needed()` is true.
inline uint16_t g_coco_irq_shim_base = 0xFFE0;
byte __not_in_flash("irq_shim_data") g_coco_irq_shim_code[16] = {
    0xB6, 0xFF, 0x02,  // 0..2: LDA >$FF02 (clear CoCo 2 PIA0 VSYNC latch)
    0xB6, 0xFF, 0xEF,  // 3..5: LDA >$FFEF (bit 7 = flat65280_irq_needed())
    0x2B, 0x01,        // 6..7: BMI +1 (to offset 9)
    0x3B,              // 8:    RTI
    0xA6, 0x61,        // 9..A: LDA 1,S (restore stacked A)
    0x7E, 0x00, 0x00,  // B..D: JMP <vector_ram[8..9]>
    0x3B, 0x00,        // E..F
};

byte IN_RAM coco_irq_shim_vec_hi(uint) { return (byte)(g_coco_irq_shim_base >> 8); }
byte IN_RAM coco_irq_shim_vec_lo(uint) { return (byte)(g_coco_irq_shim_base & 0xFFu); }

byte IN_RAM coco_irq_shim_reader(uint addr) {
  uint off = (addr - g_coco_irq_shim_base) & 0x0Fu;
  if (off == 0x0Fu) return flat65280_irq_needed() ? 0x80u : 0x00u;
  if (off == 0x0Cu) return vector_ram[8];
  if (off == 0x0Du) return vector_ram[9];
  return g_coco_irq_shim_code[off];
}

struct CocoIrqShim {
  static constexpr uint16_t kDefaultBase = 0xFFE0;
  static void Install(uint16_t base = kDefaultBase) {
    g_coco_irq_shim_base = base;
    uint16_t stat_addr = (uint16_t)(base + 0x0Fu);
    g_coco_irq_shim_code[4] = (byte)(stat_addr >> 8);
    g_coco_irq_shim_code[5] = (byte)(stat_addr & 0xFFu);
    IoClaim(base, base + 15, "irq_shim");
    for (int i = 0; i < 16; i++) {
      IoSet(base + i, coco_irq_shim_reader, nullptr);
    }
    IoSet(0xFFF8, coco_irq_shim_vec_hi, IOWriters[0xF8]);
    IoSet(0xFFF9, coco_irq_shim_vec_lo, IOWriters[0xF9]);
    g_coco_irq_shim_active = true;
  }
  static void InstallIfCore() {
    const TargetSpec* tgt = nullptr;
    if (ResolveActiveTarget(g_port_settings, &tgt, DEFAULT_BOARD_HOST).ok && tgt &&
        IsCoreDeviceOf(*tgt, "irq_shim")) {
      Install(PortBase("irq_shim", kDefaultBase));
    }
  }
};

struct FlatVectors {   // $FFF0..$FFFF read from RAM (the 6809 vectors live in the top of RAM)
  static constexpr uint16_t kDefaultBase = 0xFFF0;
  static void Install(uint16_t base = kDefaultBase) {
    IoClaim(base, base + 15, "vectors");
    for (int i = 0; i < 16; i++) IoSet(base + i, ram_vector_reader, nullptr);
  }
};

void Install_Flat65280_IO() {
  IoReset();
  EmuDsk::Install(PortBase("emudsk", EmuDsk::kDefaultBase));
  ClockStop::Install(PortBase("clockstop", ClockStop::kDefaultBase));
  TermAcia::Install(PortBase("acia", TermAcia::kDefaultBase));
  FlatVectors::Install();
  CocoIrqShim::InstallIfCore();
  IoFinish();
}

void setup_flat65280() {
  is_an_os9 = true;
  sim_status_reg = 0;
  sim_control_reg = 0;

  // 1. Open drive nodes if configured
  for (int d = 0; d < 2; d++) {
    const char* path = (d == 0) ? g_config.drive0 : g_config.drive1;
    if (path && path[0]) {
      g_drive_nodes[d] = vfs_resolve(path);
      if (g_drive_nodes[d]) {
        int err = g_drive_nodes[d]->open_file(LFS_O_RDWR);
        if (err < 0) {
          cobs_printf("[flat65280: drive %d open RDWR failed (err %d), trying RDONLY: %s]\n", d, err, path);
          err = g_drive_nodes[d]->open_file(LFS_O_RDONLY);
          if (err >= 0) {
            cobs_printf("[flat65280: opened drive %d read-only: %s]\n", d, path);
          } else {
            cobs_printf("[flat65280: drive %d open failed: %s]\n", d, path);
            g_drive_nodes[d] = nullptr;
          }
        } else {
          cobs_printf("[flat65280: opened drive %d: %s]\n", d, path);
        }
      } else {
        cobs_printf("[flat65280: could not resolve drive %d path: %s]\n", d, path);
      }
    } else {
      g_drive_nodes[d] = nullptr;
    }
  }

  // 2. Load flat65280 image into ram[]
  memset(ram, 0, sizeof(ram));
  const char* img_path = g_config.flat_img[0] ? g_config.flat_img : "/pc/flat65280.img";
  auto img_node = vfs_resolve(img_path);
  if (img_node) {
    int err = img_node->open_file(LFS_O_RDONLY);
    if (err >= 0) {
      cobs_printf("[flat65280: loading %s into RAM...]\n", img_path);
      lfs_ssize_t total = 0;
      while (total < 65536) {
        lfs_size_t chunk = 65536 - total;
        if (chunk > 256) chunk = 256;
        lfs_ssize_t n = img_node->read(&ram[total], chunk);
        if (n <= 0) break;
        total += n;
      }
      img_node->close_file(nullptr);
      cobs_printf("[flat65280: loaded %d bytes into RAM]\n", (int)total);
    } else {
      cobs_printf("[flat65280: failed to open %s (err %d)]\n", img_path, err);
    }
  } else {
    cobs_printf("[flat65280: could not resolve %s]\n", img_path);
  }

  // Copy vectors to vector_ram
  memcpy(vector_ram, &ram[0xFFF0], 16);

  // 3. Install IO handlers
  Install_Flat65280_IO();
}

