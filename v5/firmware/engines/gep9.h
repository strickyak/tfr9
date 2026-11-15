#pragma once
// gep9.h -- Engines gep9 / gep9big: Hatvan 6309 support.
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// ═══════════════════════════════════════════════════════════════════
// gep9 (Hatvan 6309) Engine Support
// ═══════════════════════════════════════════════════════════════════

void IN_RAM gep9_write_disk_cmdst(uint, byte d) {
  g_gep9_disk_req.drive = gep9_disk.drive;
  g_gep9_disk_req.sector = gep9_disk.sector;
  g_gep9_disk_req.mem_task = gep9_disk.mem_task;
  g_gep9_disk_req.mem_addr = gep9_disk.mem_addr;
  g_gep9_disk_req.command = d;
  g_gep9_disk_req.status.store(-1, std::memory_order_release);

  uint pb_w = ((uint)FG2BG_GEP9_DISK << 24);
  while (!fg2bg.push(pb_w)) { tight_loop_contents(); }

  while (g_gep9_disk_req.status.load(std::memory_order_acquire) < 0) {
    tight_loop_contents();
  }
  gep9_disk.status = (byte)g_gep9_disk_req.status.load(std::memory_order_acquire);
}

static bool gep9_load_decb_into_task(uint8_t task, const char* path) {
  auto img_node = vfs_resolve(path);
  if (!img_node) {
    cobs_printf("[gep9: could not resolve %s]\n", path);
    return false;
  }
  int err = img_node->open_file(LFS_O_RDONLY);
  if (err < 0) {
    cobs_printf("[gep9: failed to open %s (err %d)]\n", path, err);
    return false;
  }
  cobs_printf("[gep9: loading %s into Task %d...]\n", path, (int)task);
  byte hdr[5];
  lfs_ssize_t hn = img_node->read(hdr, 5);
  if (hn == 5 && (hdr[0] == 0x00 || hdr[0] == 0xFD || hdr[0] == 0xFE || hdr[0] == 0xFF)) {
    // Extended DECB stream
    img_node->seek(0, LFS_SEEK_SET, nullptr);
    uint16_t high16 = 0;
    while (true) {
      lfs_ssize_t n = img_node->read(hdr, 5);
      if (n < 5) break;
      byte chunk_type = hdr[0];
      uint16_t length = ((uint16_t)hdr[1] << 8) | hdr[2];
      uint16_t addr = ((uint16_t)hdr[3] << 8) | hdr[4];

      if (chunk_type == 0xFE) { // Extended: Upper 16-bit address extension (ChunkHigh16)
        high16 = addr;
        if (length > 0) {
          img_node->seek(length, LFS_SEEK_CUR, nullptr);
        }
      } else if (chunk_type == 0x00) { // Data chunk
        uint32_t full_addr = ((uint32_t)high16 << 16) | addr;
        byte chunk_buf[256];
        uint16_t remaining = length;
        uint32_t cur_addr = full_addr;
        while (remaining > 0) {
          uint16_t to_read = remaining > 256 ? 256 : remaining;
          lfs_ssize_t rn = img_node->read(chunk_buf, to_read);
          if (rn <= 0) break;
          for (int i = 0; i < rn; i++) {
            if (gep9_mmu.embiggen && (cur_addr + i) >= 0x10000) {
              uint32_t phys = cur_addr + i;
              uint8_t blk = phys / 8192;
              uint16_t off = phys % 8192;
              *gep9_mmu.TranslateBlockWrite(task, blk, off) = chunk_buf[i];
            } else {
              *gep9_mmu.TranslateWrite(task, (uint16_t)(cur_addr + i)) = chunk_buf[i];
            }
          }
          cur_addr += rn;
          remaining -= rn;
        }
      } else if (chunk_type == 0xFF) { // Exec trailer
        if (task == 0) {
          *gep9_mmu.TranslateWrite(0, 0xFFFE) = (addr >> 8) & 0xFF;
          *gep9_mmu.TranslateWrite(0, 0xFFFF) = addr & 0xFF;
          cobs_printf("[gep9: DECB execution address $%04X set in reset vector]\n", addr);
        }
        break;
      } else {
        // Non-data chunk (magic, symbols, debug info)
        if (length > 0) {
          img_node->seek(length, LFS_SEEK_CUR, nullptr);
        }
      }
    }
  } else {
    // Raw image fallback
    cobs_printf("[gep9: loading raw image into Task %d...]\n", (int)task);
    img_node->seek(0, LFS_SEEK_SET, nullptr);
    byte buf[256];
    uint32_t total = 0;
    while (total < 65536) {
      lfs_ssize_t rn = img_node->read(buf, 256);
      if (rn <= 0) break;
      for (int i = 0; i < rn; i++) {
        *gep9_mmu.TranslateWrite(task, total + i) = buf[i];
      }
      total += rn;
    }
  }
  img_node->close_file(nullptr);
  cobs_printf("[gep9: Task %d image load complete]\n", (int)task);
  return true;
}

void setup_gep9() {
  current_engine = ENGINE_GEP9;

  // 1. Initialize gep9 MMU & memory pool
  gep9_mmu.Init();

  // 2. Open up to 4 drive nodes if configured (default drive 0: /pc/disk0.dsk)
  for (int d = 0; d < 4; d++) {
    const char* path = "";
    if (d == 0) {
      if (centipede_config.drive0[0]) {
        path = centipede_config.drive0;
      } else {
        auto hnode = vfs_resolve("/pc/hatvan0.dsk");
        path = hnode ? "/pc/hatvan0.dsk" : "/pc/disk0.dsk";
      }
    }
    else if (d == 1) path = centipede_config.drive1;
    else if (d == 2) path = centipede_config.drive2;
    else if (d == 3) path = centipede_config.drive3;

    if (path && path[0]) {
      g_drive_nodes[d] = vfs_resolve(path);
      if (g_drive_nodes[d]) {
        int err = g_drive_nodes[d]->open_file(LFS_O_RDWR);
        if (err < 0) {
          cobs_printf("[gep9: drive %d open RDWR failed (err %d), trying RDONLY: %s]\n", d, err, path);
          err = g_drive_nodes[d]->open_file(LFS_O_RDONLY);
          if (err >= 0) {
            cobs_printf("[gep9: opened drive %d read-only: %s]\n", d, path);
          } else {
            cobs_printf("[gep9: drive %d open failed: %s]\n", d, path);
            g_drive_nodes[d] = nullptr;
          }
        } else {
          cobs_printf("[gep9: opened drive %d: %s]\n", d, path);
        }
      } else {
        cobs_printf("[gep9: could not resolve drive %d path: %s]\n", d, path);
      }
    } else {
      g_drive_nodes[d] = nullptr;
    }
  }

  // 3. Load tasks into memory:
  // Task 0: Kernel (flat_img or /pc/kernel_6809.decb)
  const char* t0_path = centipede_config.flat_img[0] ? centipede_config.flat_img : "/pc/kernel_6809.decb";
  gep9_load_decb_into_task(0, t0_path);

  // Task 1: RBF disk driver (task1 or /pc/rbf_6809.decb)
  const char* t1_path = centipede_config.task1[0] ? centipede_config.task1 : "/pc/rbf_6809.decb";
  gep9_load_decb_into_task(1, t1_path);

  // Task 2: ProcFS driver (task2 or /pc/procfs_6809.decb)
  const char* t2_path = centipede_config.task2[0] ? centipede_config.task2 : "/pc/procfs_6809.decb";
  gep9_load_decb_into_task(2, t2_path);

  // 4. Install IO handlers
  Install_Gep9_IO();
  cobs_printf("[gep9: IO and MMU initialized | preloaded %d / 1024 pages (Task 0: %d, Task 1: %d, Task 2: %d)]\n",
              (int)gep9_mmu.allocated_count,
              gep9_mmu.CountTaskFrames(0),
              gep9_mmu.CountTaskFrames(1),
              gep9_mmu.CountTaskFrames(2));
}

void setup_gep9big() {
  current_engine = ENGINE_GEP9BIG;

  // 1. Initialize gep9 MMU with EMBIGGEN active
  gep9_mmu.Init();
  gep9_mmu.embiggen = true;

  // 2. Open up to 4 drive nodes if configured
  for (int d = 0; d < 4; d++) {
    const char* path = "";
    if (d == 0) {
      if (centipede_config.drive0[0]) {
        path = centipede_config.drive0;
      } else {
        auto hnode = vfs_resolve("/pc/hatvan0.dsk");
        path = hnode ? "/pc/hatvan0.dsk" : "/pc/disk0.dsk";
      }
    }
    else if (d == 1) path = centipede_config.drive1;
    else if (d == 2) path = centipede_config.drive2;
    else if (d == 3) path = centipede_config.drive3;

    if (path && path[0]) {
      g_drive_nodes[d] = vfs_resolve(path);
      if (g_drive_nodes[d]) {
        int err = g_drive_nodes[d]->open_file(LFS_O_RDWR);
        if (err < 0) {
          cobs_printf("[gep9big: drive %d open RDWR failed (err %d), trying RDONLY: %s]\n", d, err, path);
          err = g_drive_nodes[d]->open_file(LFS_O_RDONLY);
          if (err >= 0) {
            cobs_printf("[gep9big: opened drive %d read-only: %s]\n", d, path);
          } else {
            cobs_printf("[gep9big: drive %d open failed: %s]\n", d, path);
            g_drive_nodes[d] = nullptr;
          }
        } else {
          cobs_printf("[gep9big: opened drive %d: %s]\n", d, path);
        }
      } else {
        cobs_printf("[gep9big: could not resolve drive %d path: %s]\n", d, path);
      }
    } else {
      g_drive_nodes[d] = nullptr;
    }
  }

  // 3. Load tasks into memory:
  const char* t0_path = centipede_config.flat_img[0] ? centipede_config.flat_img : "/pc/kernel_6809.decb";
  gep9_load_decb_into_task(0, t0_path);

  if (centipede_config.task1[0]) {
    gep9_load_decb_into_task(1, centipede_config.task1);
  }
  if (centipede_config.task2[0]) {
    gep9_load_decb_into_task(2, centipede_config.task2);
  }

  // 4. Install IO handlers (including 0xff40..0xff47 or 0xffe0..0xffe7 MMAP vector)
  Install_Gep9_IO();
  cobs_printf("[gep9big: EMBIGGEN IO and MMU initialized | preloaded %d / 1024 pages (Task 0: %d, Task 1: %d, Task 2: %d)]\n",
              (int)gep9_mmu.allocated_count,
              gep9_mmu.CountTaskFrames(0),
              gep9_mmu.CountTaskFrames(1),
              gep9_mmu.CountTaskFrames(2));
}

void setup_hatvan_coco2() {
  setup_gep9big();
}

