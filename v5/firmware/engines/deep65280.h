#pragma once
// deep65280.h -- Engine deep65280: NitrOS-9 Level 2 I/O and setup.
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// ═══════════════════════════════════════════════════════════════════
// Deep65280 (NitrOS-9 Level 2) IO & Setup
// ═══════════════════════════════════════════════════════════════════

byte IN_RAM deep65280_read_gime_init0(uint) {
  return 0;
}

byte IN_RAM deep65280_read_task(uint) {
  return deep65280_active_task;
}

void IN_RAM deep65280_write_task(uint, byte d) {
  deep65280_active_task = d & 1;
  update_task_mmu();
}

byte IN_RAM deep65280_read_mmu(uint addr) {
  uint reg = addr & 0x0F;
  uint task = (reg >> 3) & 1;
  uint slot = reg & 7;
  return deep65280_mmu[task][slot];
}

void IN_RAM deep65280_write_mmu(uint addr, byte d) {
  uint reg = addr & 0x0F;
  uint task = (reg >> 3) & 1;
  uint slot = reg & 7;
  deep65280_mmu[task][slot] = d;
  if (task == deep65280_active_task) {
    uint8_t blk = g_gime_mmu_enabled ? d : (uint8_t)(0x38 + slot);
    task_mmu_base[slot] = (uint32_t)(blk & g_deep_mmu_mask) << 13;
  }
}

byte IN_RAM deep65280_vector_reader(uint addr) {
  return vector_ram[addr & 0x0F];
}

void IN_RAM deep65280_vector_writer(uint addr, byte d) {
  vector_ram[addr & 0x0F] = d;
}

struct DeepGime {      // the two GIME registers NitrOS-9 Level 2 touches: init0 ($FF90, read) and the task register ($FF91)
  static constexpr uint16_t kDefaultBase = 0xFF90;
  static void Install(uint16_t base = kDefaultBase) {
    IoClaim(base, base + 1, "gime");
    IoSet(base + 0, deep65280_read_gime_init0, nullptr);
    IoSet(base + 1, deep65280_read_task, deep65280_write_task);
  }
};

struct DeepMmu {       // MMU task 0 and task 1 registers, 8 + 8 at $FFA0..$FFAF
  static constexpr uint16_t kDefaultBase = 0xFFA0;
  static void Install(uint16_t base = kDefaultBase) {
    IoClaim(base, base + 15, "mmu");
    for (int i = 0; i < 16; i++) IoSet(base + i, deep65280_read_mmu, deep65280_write_mmu);
    g_deep_mmu_active = true;
  }
};

struct DeepVectors {   // $FFF0..$FFFF: a small RAM of its own
  static constexpr uint16_t kDefaultBase = 0xFFF0;
  static void Install(uint16_t base = kDefaultBase) {
    IoClaim(base, base + 15, "vectors");
    for (int i = 0; i < 16; i++) IoSet(base + i, deep65280_vector_reader, deep65280_vector_writer);
  }
};

void Install_Deep65280_IO() {
  IoReset();
  EmuDsk::Install(PortBase("emudsk", EmuDsk::kDefaultBase));
  ClockStop::Install(PortBase("clockstop", ClockStop::kDefaultBase));
  TermAcia::Install(PortBase("acia", TermAcia::kDefaultBase));
  DeepGime::Install(PortBase("gime", DeepGime::kDefaultBase));
  DeepMmu::Install(PortBase("mmu", DeepMmu::kDefaultBase));
  DeepVectors::Install();
  IoFinish();
}

void setup_deep65280() {
  is_an_os9 = true;

  // 1. Open drive nodes if configured
  for (int d = 0; d < 2; d++) {
    const char* path = (d == 0) ? g_config.drive0 : g_config.drive1;
    if (path && path[0]) {
      g_drive_nodes[d] = vfs_resolve(path);
      if (g_drive_nodes[d]) {
        int err = g_drive_nodes[d]->open_file(LFS_O_RDWR);
        if (err < 0) {
          cobs_printf("[deep65280: drive %d open RDWR failed (err %d), trying RDONLY: %s]\n", d, err, path);
          err = g_drive_nodes[d]->open_file(LFS_O_RDONLY);
          if (err >= 0) {
            cobs_printf("[deep65280: opened drive %d read-only: %s]\n", d, path);
          } else {
            cobs_printf("[deep65280: drive %d open failed: %s]\n", d, path);
            g_drive_nodes[d] = nullptr;
          }
        } else {
          cobs_printf("[deep65280: opened drive %d: %s]\n", d, path);
        }
      } else {
        cobs_printf("[deep65280: could not resolve drive %d path: %s]\n", d, path);
      }
    } else {
      g_drive_nodes[d] = nullptr;
    }
  }

  // 2. Initialize MMU defaults:
  // Task 0 and Task 1: $00, $39, $3A, $3B, $3C, $3D, $3E, $3F
  for (int t = 0; t < 2; t++) {
    deep65280_mmu[t][0] = 0x00;
    for (int s = 1; s < 8; s++) {
      deep65280_mmu[t][s] = 0x38 + s; // 0x39..0x3F
    }
  }
  deep65280_active_task = 0;
  update_task_mmu();

  // 3. Load deep65280 image into physical RAM
#if DEEP_256K
  memset(ram, 0, sizeof(ram)); // 262144 bytes (256KB) zeroed

  const char* img_path = g_config.flat_img[0] ? g_config.flat_img : "/pc/deep65280.img";
  auto img_node = vfs_resolve(img_path);
  if (img_node) {
    int err = img_node->open_file(LFS_O_RDONLY);
    if (err >= 0) {
      cobs_printf("[deep65280: loading %s into 256KB RAM...]\n", img_path);

      // Slot 0 (8KB) into physical block 0 (0x00000..0x01FFF)
      size_t total0 = 0;
      while (total0 < 8192) {
        size_t chunk = 8192 - total0;
        if (chunk > 256) chunk = 256;
        lfs_ssize_t n = img_node->read(&ram[total0], chunk);
        if (n <= 0) break;
        total0 += n;
      }

      // Slots 1..7 (56KB) into physical blocks 25..31 (0x32000..0x3FFFF)
      // Corresponding to MMU pages $39..$3F (& 0x1F = 25..31)
      size_t total1 = 0;
      while (total1 < 57344) {
        size_t chunk = 57344 - total1;
        if (chunk > 256) chunk = 256;
        lfs_ssize_t n = img_node->read(&ram[0x32000 + total1], chunk);
        if (n <= 0) break;
        total1 += n;
      }
      img_node->close_file(nullptr);
      cobs_printf("[deep65280: loaded %d + %d = %d bytes into RAM (256KB)]\n",
                  (int)total0, (int)total1, (int)(total0 + total1));
    } else {
      cobs_printf("[deep65280: failed to open %s (err %d)]\n", img_path, err);
    }
  } else {
    cobs_printf("[deep65280: could not resolve %s]\n", img_path);
  }

  // Copy vectors from physical block 31 ($3FFF0) to vector_ram
  memcpy(vector_ram, &ram[0x3FFF0], 16);
#else
  memset(ram, 0, sizeof(ram)); // 131072 bytes (128KB) zeroed

  const char* img_path = g_config.flat_img[0] ? g_config.flat_img : "/pc/deep65280.img";
  auto img_node = vfs_resolve(img_path);
  if (img_node) {
    int err = img_node->open_file(LFS_O_RDONLY);
    if (err >= 0) {
      cobs_printf("[deep65280: loading %s into 128KB RAM...]\n", img_path);

      // Slot 0 (8KB) into physical block 0 (0x00000..0x01FFF)
      size_t total0 = 0;
      while (total0 < 8192) {
        size_t chunk = 8192 - total0;
        if (chunk > 256) chunk = 256;
        lfs_ssize_t n = img_node->read(&ram[total0], chunk);
        if (n <= 0) break;
        total0 += n;
      }

      // Slots 1..7 (56KB) into physical blocks 9..15 (0x12000..0x1FFFF)
      size_t total1 = 0;
      while (total1 < 57344) {
        size_t chunk = 57344 - total1;
        if (chunk > 256) chunk = 256;
        lfs_ssize_t n = img_node->read(&ram[0x12000 + total1], chunk);
        if (n <= 0) break;
        total1 += n;
      }
      img_node->close_file(nullptr);
      cobs_printf("[deep65280: loaded %d + %d = %d bytes into RAM (128KB)]\n",
                  (int)total0, (int)total1, (int)(total0 + total1));
    } else {
      cobs_printf("[deep65280: failed to open %s (err %d)]\n", img_path, err);
    }
  } else {
    cobs_printf("[deep65280: could not resolve %s]\n", img_path);
  }

  // Copy vectors from physical block 15 ($1FFF0) to vector_ram
  memcpy(vector_ram, &ram[0x1FFF0], 16);
#endif

  // 4. Install IO handlers
  Install_Deep65280_IO();
}

// ═══════════════════════════════════════════════════════════════════
// CoCo-3 / JamesBrown NitrOS-9 Level 2 Targets (`hamster_gime`)
// ═══════════════════════════════════════════════════════════════════

static void install_coco3_dos_launcher(uint16_t reset_entry = 0x2500, bool write_stub = true) {
  static const uint16_t kCoco3Vecs[8] = {
    0x0000, 0xFEEE, 0xFEF1, 0xFEF4, 0xFEF7, 0xFEFA, 0xFEFD, 0x2500,
  };
  for (int i = 0; i < 7; i++) {
    vector_ram[2 * i + 0] = (byte)(kCoco3Vecs[i] >> 8);
    vector_ram[2 * i + 1] = (byte)(kCoco3Vecs[i] & 0xFF);
  }
  vector_ram[14] = (byte)(reset_entry >> 8);
  vector_ram[15] = (byte)(reset_entry & 0xFF);

  if (write_stub) {
    // Disk BASIC `DOS` stub at $2500: orcc #$50; lds #$0500; jmp $2602
    static const byte kDosStub[9] = {0x1A, 0x50, 0x10, 0xCE, 0x05, 0x00, 0x7E, 0x26, 0x02};
    for (int i = 0; i < 9; i++) {
      ram[translate_deep_addr((uint16_t)(0x2500 + i))] = kDosStub[i];
    }
  }
}

static void load_nitros9l2_common(const char* tag, byte* out_init1) {
  is_an_os9 = true;
  sim_status_reg = 0;
  sim_control_reg = 0;
  clock_and_stop_reg = 0;
  *out_init1 = 0;

  // 1. Open drive 0 & drive 1 (defaulting drive 0 to /pc/level2.dsk or /pc/deep0.dsk when unset)
  for (int d = 0; d < 2; d++) {
    const char* path = (d == 0) ? g_config.drive0 : g_config.drive1;
    if (d == 0 && (!path || !path[0])) {
      struct vfs_info st;
      if (vfs_stat("/pc/level2.dsk", &st) == 0) path = "/pc/level2.dsk";
      else if (vfs_stat("/pc/deep0.dsk", &st) == 0) path = "/pc/deep0.dsk";
    }
    if (path && path[0]) {
      g_drive_nodes[d] = vfs_resolve(path);
      if (g_drive_nodes[d]) {
        int err = g_drive_nodes[d]->open_file(LFS_O_RDWR);
        if (err < 0) {
          err = g_drive_nodes[d]->open_file(LFS_O_RDONLY);
          if (err >= 0) {
            cobs_printf("[%s: opened drive %d read-only: %s]\n", tag, d, path);
          } else {
            cobs_printf("[%s: drive %d open failed: %s]\n", tag, d, path);
            g_drive_nodes[d] = nullptr;
          }
        } else {
          cobs_printf("[%s: opened drive %d: %s]\n", tag, d, path);
        }
      } else {
        cobs_printf("[%s: could not resolve drive %d path: %s]\n", tag, d, path);
      }
    } else {
      g_drive_nodes[d] = nullptr;
    }
  }

  // 2. Initialize MMU defaults ($00, $39..$3F for Task 0 & Task 1) and zero RAM
  for (int t = 0; t < 2; t++) {
    deep65280_mmu[t][0] = 0x00;
    for (int s = 1; s < 8; s++) {
      deep65280_mmu[t][s] = 0x38 + s;
    }
  }
  g_gime_mmu_enabled = true;
  deep65280_active_task = 0;
  update_task_mmu();
  memset(ram, 0, sizeof(ram));

  // 3. Load boot image:
  //    (a) explicit Config(flat_img): supports Track-35 ('OS' at $2600), pre-launched ($1A $50 at $2500),
  //        or 64 KB whole-RAM image (Slot 0 -> block 0, Slots 1..7 -> blocks 25..31 / 9..15);
  //    (b) otherwise, if drive 0 has a Track-34 'OS' kernel (LSN 612..629), boot straight off drive 0;
  //    (c) otherwise, fall back to /pc/deep65280.img if present.
  const char* img_path = g_config.flat_img[0] ? g_config.flat_img : nullptr;
  if (!img_path && g_drive_nodes[0]) {
    byte sec[256];
    if (g_drive_nodes[0]->seek(612 * 256, LFS_SEEK_SET, nullptr) == 612 * 256 &&
        g_drive_nodes[0]->read(sec, 256) == 256 &&
        sec[0] == 'O' && sec[1] == 'S') {
      cobs_printf("[%s: booting Track-34 'OS' kernel from drive 0 (LSN 612..629)]\n", tag);
      *out_init1 = 1;
      deep65280_active_task = 1;
      update_task_mmu();
      install_coco3_dos_launcher(0x2500, /*write_stub=*/true);
      for (int i = 0; i < 256; i++) {
        ram[translate_deep_addr((uint16_t)(0x2600 + i))] = sec[i];
      }
      for (int s = 1; s < 18; s++) {
        if (g_drive_nodes[0]->read(sec, 256) != 256) break;
        uint16_t base_log = (uint16_t)(0x2600 + s * 256);
        for (int i = 0; i < 256; i++) {
          ram[translate_deep_addr((uint16_t)(base_log + i))] = sec[i];
        }
      }
      return;
    }
  }
  if (!img_path) img_path = "/pc/deep65280.img";

  auto img_node = vfs_resolve(img_path);
  if (!img_node) {
    cobs_printf("[%s: could not resolve %s]\n", tag, img_path);
    return;
  }
  int err = img_node->open_file(LFS_O_RDONLY);
  if (err < 0) {
    cobs_printf("[%s: failed to open %s (err %d)]\n", tag, img_path, err);
    return;
  }

  byte first_chunk[256];
  lfs_ssize_t n0 = img_node->read(first_chunk, 256);
  if (n0 <= 0) {
    img_node->close_file(nullptr);
    return;
  }

  if (n0 >= 2 && ((first_chunk[0] == 'O' && first_chunk[1] == 'S') ||
                  (first_chunk[0] == 0x1A && first_chunk[1] == 0x50))) {
    bool is_raw_t35 = (first_chunk[0] == 'O' && first_chunk[1] == 'S');
    uint16_t load_base = is_raw_t35 ? 0x2600 : 0x2500;
    *out_init1 = 1;
    deep65280_active_task = 1;
    update_task_mmu();
    install_coco3_dos_launcher(0x2500, /*write_stub=*/is_raw_t35);
    size_t total = 0;
    for (int i = 0; i < n0; i++) {
      ram[translate_deep_addr((uint16_t)(load_base + total++))] = first_chunk[i];
    }
    while (total < 0xD800) {
      lfs_ssize_t n = img_node->read(first_chunk, 256);
      if (n <= 0) break;
      for (int i = 0; i < n; i++) {
        ram[translate_deep_addr((uint16_t)(load_base + total++))] = first_chunk[i];
      }
    }
    img_node->close_file(nullptr);
    cobs_printf("[%s: loaded %d-byte launcher/track image at $%04X]\n", tag, (int)total, load_base);
    return;
  }

  // Whole 64 KB image (Slot 0 -> block 0, Slots 1..7 -> top 7 blocks)
  memcpy(&ram[0], first_chunk, (size_t)n0);
  size_t total0 = (size_t)n0;
  while (total0 < 8192) {
    size_t chunk = 8192 - total0;
    if (chunk > 256) chunk = 256;
    lfs_ssize_t n = img_node->read(&ram[total0], chunk);
    if (n <= 0) break;
    total0 += (size_t)n;
  }
#if DEEP_256K
  constexpr uint32_t kUpperPhys = 0x32000;
  constexpr uint32_t kVecPhys   = 0x3FFF0;
#else
  constexpr uint32_t kUpperPhys = 0x12000;
  constexpr uint32_t kVecPhys   = 0x1FFF0;
#endif
  size_t total1 = 0;
  while (total1 < 57344) {
    size_t chunk = 57344 - total1;
    if (chunk > 256) chunk = 256;
    lfs_ssize_t n = img_node->read(&ram[kUpperPhys + total1], chunk);
    if (n <= 0) break;
    total1 += (size_t)n;
  }
  img_node->close_file(nullptr);
  memcpy(vector_ram, &ram[kVecPhys], 16);
  cobs_printf("[%s: loaded %d + %d = %d bytes into RAM]\n",
              tag, (int)total0, (int)total1, (int)(total0 + total1));
}

void Install_JamesBrown_IO(byte init1) {
  IoReset();
  TermAcia::Install(PortBase("acia", 0xFF14));
  AudioSn76489::Install(PortBase("audio", 0xFF41));
  InfoPort::Install(PortBase("info", 0xFF43));
  BeckerPort::Install(PortBase("becker", 0xFF45));
  EmuDsk::Install(PortBase("emudsk", 0xFF80));
  Coco3Gime::Install(PortBase("gime", 0xFF90), 4, /*init0=*/0x40, init1);
  DeepMmu::Install(PortBase("mmu", 0xFFA0));
  RtcPort::Install(PortBase("rtc", 0xFFC0));
  DeepVectors::Install();
  IoFinish();
}

void Install_Nitros9L2Coco3_IO(byte init1) {
  IoReset();
  EmuDsk::Install(PortBase("emudsk", 0xFF80));
  TermAcia::Install(PortBase("acia", 0xFF88));
  Coco3Gime::Install(PortBase("gime", 0xFF90), 16, /*init0=*/0x40, init1);
  DeepMmu::Install(PortBase("mmu", 0xFFA0));
  Coco3Palette::Install(PortBase("palette", 0xFFB0));
  DeepVectors::Install();
  IoFinish();
}

void setup_jamesbrown() {
  byte init1 = 0;
  load_nitros9l2_common("jamesbrown", &init1);
  Install_JamesBrown_IO(init1);
}

void setup_nitros9l2_coco3() {
  byte init1 = 0;
  load_nitros9l2_common("nitros9l2/coco3", &init1);
  Install_Nitros9L2Coco3_IO(init1);
}

