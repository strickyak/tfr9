#pragma once
// multicomp09.h -- Engine multicomp09fuzix: Fuzix 6809 support.
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// ═══════════════════════════════════════════════════════════════════
// Multicomp09 (Fuzix 6809) Engine Support
// ═══════════════════════════════════════════════════════════════════

void IN_RAM mc_dispatch_sd_cmd(uint8_t cmd) {
  switch (cmd) {
    case 0x00: { // Read 512-byte sector
      mc_sd_idx = 0;
      g_mc_sd_req.lba = (uint32_t)mc_sd_lba0 |
                        ((uint32_t)mc_sd_lba1 << 8) |
                        ((uint32_t)mc_sd_lba2 << 16);
      g_mc_sd_req.command = 0;
      g_mc_sd_req.status.store(-1, std::memory_order_release);

      uint pb_w = ((uint)FG2BG_MC_SD << 24);
      while (!fg2bg.push(pb_w)) { tight_loop_contents(); }

      while (g_mc_sd_req.status.load(std::memory_order_acquire) < 0) {
        tight_loop_contents();
      }

      mc_sd_ctl = 0xE0; // Data byte ready
      break;
    }
    case 0x01: { // Write 512-byte sector
      if (mc_sd_ctl == 0xA0) {
        g_mc_sd_req.lba = (uint32_t)mc_sd_lba0 |
                          ((uint32_t)mc_sd_lba1 << 8) |
                          ((uint32_t)mc_sd_lba2 << 16);
        g_mc_sd_req.command = 1;
        g_mc_sd_req.status.store(-1, std::memory_order_release);

        uint pb_w = ((uint)FG2BG_MC_SD << 24);
        while (!fg2bg.push(pb_w)) { tight_loop_contents(); }

        while (g_mc_sd_req.status.load(std::memory_order_acquire) < 0) {
          tight_loop_contents();
        }
        mc_sd_ctl = 0x80; // Idle
      } else {
        mc_sd_idx = 0;
        mc_sd_ctl = 0xA0; // Ready for write data
      }
      break;
    }
    default:
      mc_sd_ctl = 0x80; // Idle
      break;
  }
}

// multicomp09fuzix decodes $FFD0..$FFDF itself (mc_read_io / mc_write_io in the bus loop), not through the dispatch
// tables, so here we only record the claims and check them against ports.tsv.  Note: the tables are NOT cleared,
// so the Turbo9Sim ACIA handlers installed at startup at $FF00..$FF03 are still in them (v4 behavior).
struct MulticompIo {
  static void Claim() {
    IoResetOwners();
    IoClaim(0xFFD0, 0xFFD1, "acia");
    IoClaim(0xFFD2, 0xFFD5, "uart_stubs");
    IoClaim(0xFFD8, 0xFFDC, "sd");
    IoClaim(0xFFDD, 0xFFDD, "timer");
    IoClaim(0xFFDE, 0xFFDF, "mmu");
    IoFinish();
  }
};

void setup_multicomp09() {
  current_engine = ENGINE_MULTICOMP09;
  MulticompIo::Claim();

  // 1. Initialize RAM and MMU
  memset(ram, 0, sizeof(ram)); // Zero physical RAM (256 KB)
  for (int s = 0; s < 8; s++) {
    mc_map0[s] = s;
    mc_map1[s] = s;
  }
  mc_tr = 0;
  mc_slot = 0;
  mc_update_mmu();

  mc_acia_ctrl = 0;
  mc_rx_has_char = false;
  mc_timer_en = false;
  mc_timer_pending = false;
  mc_sd_ctl = 0x80;
  mc_sd_idx = 0;

  // 2. Open drive 0 (SD card image)
  const char* disk_path = centipede_config.drive0[0] ? centipede_config.drive0 : "/pc/disk.img";
  g_drive_nodes[0] = vfs_resolve(disk_path);
  if (g_drive_nodes[0]) {
    int err = g_drive_nodes[0]->open_file(LFS_O_RDWR);
    if (err < 0) {
      cobs_printf("[multicomp09fuzix: disk open RDWR failed (err %d), trying RDONLY: %s]\n", err, disk_path);
      err = g_drive_nodes[0]->open_file(LFS_O_RDONLY);
      if (err >= 0) {
        cobs_printf("[multicomp09fuzix: opened disk read-only: %s]\n", disk_path);
      } else {
        cobs_printf("[multicomp09fuzix: disk open failed: %s]\n", disk_path);
        g_drive_nodes[0] = nullptr;
      }
    } else {
      cobs_printf("[multicomp09fuzix: opened disk: %s]\n", disk_path);
    }
  } else {
    cobs_printf("[multicomp09fuzix: could not resolve disk path: %s]\n", disk_path);
  }

  // 3. Load fuzix kernel (Extended DECB) into physical RAM
  const char* kernel_path = centipede_config.flat_img[0] ? centipede_config.flat_img : "/pc/fuzix.bin";
  auto img_node = vfs_resolve(kernel_path);
  if (!img_node) {
    cobs_printf("[multicomp09fuzix: could not resolve kernel %s]\n", kernel_path);
    return;
  }
  int err = img_node->open_file(LFS_O_RDONLY);
  if (err < 0) {
    cobs_printf("[multicomp09fuzix: failed to open kernel %s (err %d)]\n", kernel_path, err);
    return;
  }
  cobs_printf("[multicomp09fuzix: loading kernel %s into RAM...]\n", kernel_path);

  byte hdr[5];
  uint16_t high16 = 0;
  uint16_t exec_addr = 0x0200; // default Fuzix entry
  while (true) {
    lfs_ssize_t n = img_node->read(hdr, 5);
    if (n < 5) break;
    byte chunk_type = hdr[0];
    uint16_t length = ((uint16_t)hdr[1] << 8) | hdr[2];
    uint16_t addr = ((uint16_t)hdr[3] << 8) | hdr[4];

    if (chunk_type == 0xFE) { // Upper 16-bit address
      high16 = addr;
      if (length > 0) img_node->seek(length, LFS_SEEK_CUR, nullptr);
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
          uint32_t p = (cur_addr + i) & (sizeof(ram) - 1);
          ram[p] = chunk_buf[i];
        }
        cur_addr += rn;
        remaining -= rn;
      }
    } else if (chunk_type == 0xFF) { // Exec trailer
      exec_addr = addr;
      cobs_printf("[multicomp09fuzix: DECB execution address $%04X]\n", exec_addr);
      break;
    } else {
      if (length > 0) img_node->seek(length, LFS_SEEK_CUR, nullptr);
    }
  }
  img_node->close_file(nullptr);

  // Set reset vector at $FFFE..$FFFF in RAM
  ram[0xFFFE] = (exec_addr >> 8) & 0xFF;
  ram[0xFFFF] = exec_addr & 0xFF;
  memcpy(vector_ram, &ram[0xFFF0], 16);

  cobs_printf("[multicomp09fuzix: loaded kernel, reset vector $%02X%02X]\n", ram[0xFFFE], ram[0xFFFF]);
}

