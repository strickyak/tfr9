#pragma once
// flat65280.h -- Engine flat65280: NitrOS-9 Level 1 I/O and setup.
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// IN_RAM RAM vector reader for FFF0..FFFF
byte IN_RAM ram_vector_reader(uint addr) {
  return ram[0xFF00 | (addr & 0xFF)];
}

// IRQ check for flat65280 / deep65280
FORCE_INLINE bool IN_RAM flat65280_irq_needed() {
  bool clock_irq = (clock_and_stop_reg & 0x01) && (clock_and_stop_reg & 0x02);
  bool acia_irq = ((flat65280_acia_ctrl & 0x80) != 0) && flat65280_rx_has_char;
  return clock_irq || acia_irq;
}

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

