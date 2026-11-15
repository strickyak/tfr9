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
#if DEEP_256K
    task_mmu_base[slot] = (uint32_t)(d & 0x1F) << 13;
#else
    task_mmu_base[slot] = (uint32_t)(d & 0x0F) << 13;
#endif
  }
}

byte IN_RAM deep65280_vector_reader(uint addr) {
  return vector_ram[addr & 0x0F];
}

void IN_RAM deep65280_vector_writer(uint addr, byte d) {
  vector_ram[addr & 0x0F] = d;
}

void Install_Deep65280_IO() {
  for (int i = 0; i < 256; i++) {
    IOReaders[i] = nullptr;
    IOWriters[i] = nullptr;
  }

  // EMUDSK ($FF80..$FF86)
  IOReaders[0x80] = emudsk_read_lsn0;
  IOReaders[0x81] = emudsk_read_lsn1;
  IOReaders[0x82] = emudsk_read_lsn2;
  IOReaders[0x83] = emudsk_read_cmd;
  IOReaders[0x84] = emudsk_read_buf0;
  IOReaders[0x85] = emudsk_read_buf1;
  IOReaders[0x86] = emudsk_read_drv;

  IOWriters[0x80] = emudsk_write_lsn0;
  IOWriters[0x81] = emudsk_write_lsn1;
  IOWriters[0x82] = emudsk_write_lsn2;
  IOWriters[0x83] = emudsk_write_command;
  IOWriters[0x84] = emudsk_write_buf0;
  IOWriters[0x85] = emudsk_write_buf1;
  IOWriters[0x86] = emudsk_write_drv;

  // CLOCK_AND_STOP ($FF87)
  IOReaders[0x87] = clock_and_stop_reader;
  IOWriters[0x87] = clock_and_stop_writer;

  // ACIA 6850 ($FF88..$FF89)
  IOReaders[0x88] = flat65280_acia_read_status;
  IOReaders[0x89] = flat65280_acia_read_rx;
  IOWriters[0x88] = flat65280_acia_write_ctrl;
  IOWriters[0x89] = flat65280_acia_write_tx;

  // GIME Init0 ($FF90)
  IOReaders[0x90] = deep65280_read_gime_init0;

  // Task Switch Register ($FF91)
  IOReaders[0x91] = deep65280_read_task;
  IOWriters[0x91] = deep65280_write_task;

  // MMU Task 0 & 1 Registers ($FFA0..$FFAF)
  for (int i = 0xA0; i <= 0xAF; i++) {
    IOReaders[i] = deep65280_read_mmu;
    IOWriters[i] = deep65280_write_mmu;
  }

  // System Vectors ($FFF0..$FFFF)
  for (int i = 0xF0; i <= 0xFF; i++) {
    IOReaders[i] = deep65280_vector_reader;
    IOWriters[i] = deep65280_vector_writer;
  }
}

void setup_deep65280() {
  current_engine = ENGINE_DEEP65280;

  // 1. Open drive nodes if configured
  for (int d = 0; d < 2; d++) {
    const char* path = (d == 0) ? centipede_config.drive0 : centipede_config.drive1;
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

  const char* img_path = centipede_config.flat_img[0] ? centipede_config.flat_img : "/pc/deep65280.img";
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

  const char* img_path = centipede_config.flat_img[0] ? centipede_config.flat_img : "/pc/deep65280.img";
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

