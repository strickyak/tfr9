#pragma once
// drain.h -- USB drain coroutines (characters and events).
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// ═══════════════════════════════════════════════════════════════════
// drain_chars — drain high-priority character FIFO (inline, not a coroutine)
// Runs on the scheduler's own stack. Processes ALL pending chars each call.
// ═══════════════════════════════════════════════════════════════════
static uint32_t bg_chars_drained = 0;

inline void drain_chars() {
  if (!usb_tether_ok()) {
    // USB disconnected — discard to prevent blocking
    byte discard;
    while (fg2bg_chars.pop(discard)) {}
    return;
  }
  byte c;
  while (fg2bg_chars.pop(c)) {
    putbyte(c);
    bg_chars_drained++;
  }
}

// ═══════════════════════════════════════════════════════════════════
// drain_task — drain fg2bg events FIFO (coroutine, one event per resume)
// Follows the Centipede pattern exactly: pop one chore, process it, yield.
// The scheduler pumps USB between every coro_resume.
// ═══════════════════════════════════════════════════════════════════
static uint8_t drain_stack[4096] __attribute__((aligned(8)));
static uint32_t bg_events_drained = 0;

static void drain_task(Coro& self) {
#if SPEED_STATS
  int bg_mega_cycles = 0;
#endif
  while (true) {
    uint chore = 0;
    if (fg2bg.pop(chore)) {
      bg_events_drained++;
      uint chore_num = chore >> 24;
      switch (chore_num) {
#if SPEED_STATS
        case FG2BG_MEGA_CYCLE:
          bg_mega_cycles++;
          if (bg_mega_cycles >= 20) {
            if (milliseconds > 0) {
              cobs_printf("[Mc=%g  s=%g  Mcps=%g]",
                          double(cycles) / double(1000 * 1000),
                          double(milliseconds) / double(1000),
                          (double)cycles / double(1000 * milliseconds));
            }
            bg_mega_cycles = 0;
          }
          break;
#endif
#if TRACE
        case FG2BG_READ: {
          unsigned char pkt[4] = {C_RAM2_READ,
              (unsigned char)(chore >> 16),
              (unsigned char)(chore >> 8),
              (unsigned char)chore};
          CobsEncodeAndTransmit(pkt, 4, [](int ch) { putchar_raw(ch); });
          break;
        }
        case FG2BG_WRITE: {
          unsigned char pkt[4] = {C_RAM2_WRITE,
              (unsigned char)(chore >> 16),
              (unsigned char)(chore >> 8),
              (unsigned char)chore};
          CobsEncodeAndTransmit(pkt, 4, [](int ch) { putchar_raw(ch); });
          break;
        }
        case FG2BG_FIC: {
          unsigned char pkt[4] = {C_FIC_CYCLE,
              (unsigned char)(chore >> 16),
              (unsigned char)(chore >> 8),
              (unsigned char)chore};
          CobsEncodeAndTransmit(pkt, 4, [](int ch) { putchar_raw(ch); });
          break;
        }
#endif
        case FG2BG_EMUDSK:
          emudsk_service_bg(&self);
          break;
        case FG2BG_GEP9_DISK: {
          rpc::g_vfs_coro = &self;
          uint8_t drv = g_gep9_disk_req.drive;
          if (drv > 3 || !g_drive_nodes[drv]) {
            g_gep9_disk_req.status.store(2, std::memory_order_release); // Drive not ready / enabled
            break;
          }
          auto node = g_drive_nodes[drv];
          lfs_soff_t offset = (lfs_soff_t)g_gep9_disk_req.sector * 256;
          lfs_soff_t actual = node->seek(offset, LFS_SEEK_SET, &self);
          if (actual != offset) {
            g_gep9_disk_req.status.store(3, std::memory_order_release); // Seek error / out of range
            break;
          }
          uint8_t task = g_gep9_disk_req.mem_task;
          uint32_t addr24 = g_gep9_disk_req.mem_addr24;
          byte sec_buf[256];
          if (g_gep9_disk_req.command == 1) {
            // Read sector
            lfs_ssize_t n = node->read(sec_buf, 256);
            if (n == 256) {
              for (uint32_t i = 0; i < 256; i++) {
                *gep9_mmu.TranslateWrite24(task, addr24 + i) = sec_buf[i];
              }
              g_gep9_disk_req.status.store(1, std::memory_order_release); // 1 = OKAY
            } else {
              g_gep9_disk_req.status.store(4, std::memory_order_release);
            }
          } else if (g_gep9_disk_req.command == 2) {
            // Write sector
            for (uint32_t i = 0; i < 256; i++) {
              sec_buf[i] = *gep9_mmu.TranslateRead24(task, addr24 + i);
            }
            lfs_ssize_t n = node->write(sec_buf, 256);
            if (n == 256) {
              g_gep9_disk_req.status.store(1, std::memory_order_release); // 1 = OKAY
            } else {
              g_gep9_disk_req.status.store(4, std::memory_order_release);
            }
          } else {
            g_gep9_disk_req.status.store(4, std::memory_order_release);
          }
          break;
        }
        case FG2BG_MC_SD: {
          rpc::g_vfs_coro = &self;
          if (!g_drive_nodes[0]) {
            g_mc_sd_req.status.store(1, std::memory_order_release);
            break;
          }
          auto node = g_drive_nodes[0];
          lfs_soff_t offset = (lfs_soff_t)g_mc_sd_req.lba * 512;
          lfs_soff_t actual = node->seek(offset, LFS_SEEK_SET, &self);
          if (actual != offset) {
            g_mc_sd_req.status.store(2, std::memory_order_release);
            break;
          }
          if (g_mc_sd_req.command == 0) {
            // Read 512-byte sector
            lfs_ssize_t n = node->read(mc_sd_buf, 512);
            if (n == 512) {
              g_mc_sd_req.status.store(0, std::memory_order_release);
            } else {
              g_mc_sd_req.status.store(3, std::memory_order_release);
            }
          } else if (g_mc_sd_req.command == 1) {
            // Write 512-byte sector
            lfs_ssize_t n = node->write(mc_sd_buf, 512);
            if (n == 512) {
              g_mc_sd_req.status.store(0, std::memory_order_release);
            } else {
              g_mc_sd_req.status.store(3, std::memory_order_release);
            }
          } else {
            g_mc_sd_req.status.store(4, std::memory_order_release);
          }
          break;
        }
        default:
          break;
      }
    }
    // Yield after every chore (or empty pop), exactly like Centipede.
    // The scheduler pumps USB between every coro_resume.
    coro_yield(&self);
  }
}

