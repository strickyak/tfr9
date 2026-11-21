#pragma once
// emudsk.h -- EMUDSK block-device register model ($FF80..$FF86).
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// EMUDSK state ($FF80..$FF86)
struct EmuDskRequest {
  uint8_t drive;
  uint32_t lsn;
  uint16_t buffer_addr;
  uint8_t command;
  std::atomic<int8_t> status;
};
static EmuDskRequest g_emudsk_req;
static byte emudsk_lsn[3] = {0, 0, 0};
static byte emudsk_buffer[2] = {0, 0};
static byte emudsk_drive = 0;
static byte emudsk_status = 0;
static byte emudsk_fifo_buf[256];
static uint8_t emudsk_fifo_idx = 0;
static bool emudsk_fifo_mode = false;
static volatile bool emudsk_dma_busy = false;
static volatile uint8_t emudsk_bra_phase = 0;

static std::shared_ptr<VfsNode> g_drive_nodes[4] = {nullptr, nullptr, nullptr, nullptr};

// CLOCK_AND_STOP state ($FF87)
static volatile byte clock_and_stop_reg = 0;
static uint32_t flat65280_timer_ticks = 0;

// ACIA 6850 state ($FF88..$FF89)
static volatile byte flat65280_acia_ctrl = 0;
static volatile byte flat65280_rx_char = 0;
static volatile bool flat65280_rx_has_char = false;

// IN_RAM EMUDSK callbacks
byte IN_RAM emudsk_read_lsn0(uint) { return emudsk_lsn[0]; }
byte IN_RAM emudsk_read_lsn1(uint) { return emudsk_lsn[1]; }
byte IN_RAM emudsk_read_lsn2(uint) { return emudsk_lsn[2]; }
byte IN_RAM emudsk_read_cmd(uint) {
  if (emudsk_status == 0x80) {
    int8_t st = g_emudsk_req.status.load(std::memory_order_acquire);
    if (st >= 0) {
      emudsk_status = (byte)st;
    }
  }
  return emudsk_status;
}
byte IN_RAM emudsk_read_buf0(uint) {
  if (emudsk_fifo_mode) {
    return emudsk_fifo_buf[emudsk_fifo_idx++];
  }
  return emudsk_buffer[0];
}
byte IN_RAM emudsk_read_buf1(uint) { return emudsk_buffer[1]; }
byte IN_RAM emudsk_read_drv(uint)  { return emudsk_drive; }

void IN_RAM emudsk_write_lsn0(uint, byte d) { emudsk_lsn[0] = d; }
void IN_RAM emudsk_write_lsn1(uint, byte d) { emudsk_lsn[1] = d; }
void IN_RAM emudsk_write_lsn2(uint, byte d) { emudsk_lsn[2] = d; }
void IN_RAM emudsk_write_buf0(uint, byte d) { emudsk_buffer[0] = d; }
void IN_RAM emudsk_write_buf1(uint, byte d) {
  emudsk_buffer[1] = d;
  if (emudsk_fifo_mode) {
    emudsk_fifo_buf[emudsk_fifo_idx++] = d;
  }
}
void IN_RAM emudsk_write_drv(uint, byte d)  { emudsk_drive = d; }

void IN_RAM emudsk_write_command(uint, byte data) {
  // Non-blocking byte-FIFO commands (0x10 = read sector, 0x11 = reset write FIFO, 0x12 = commit write sector)
  // avoid stalling Core 1 on Centipede 32z (where the CoCo's external E/Q clock runs continuously)
  // and work identically on real CoCo 3 motherboard RAM (`gerbil_io`).
  // Legacy command 0x02 (`park` / close drive) is a fast no-op returning status 0.
  if (data == 0x02) {
    emudsk_status = 0;
    return;
  }
  if (data == 0x11) {
    emudsk_fifo_mode = true;
    emudsk_fifo_idx = 0;
    emudsk_status = 0;
    return;
  }
  if (data == 0x10 || data == 0x12) {
    emudsk_fifo_mode = true;
    emudsk_fifo_idx = 0;
    emudsk_status = 0x80;  // bit 7 = busy until Core 0 finishes VFS I/O
    g_emudsk_req.drive = emudsk_drive;
    g_emudsk_req.lsn = ((uint32_t)emudsk_lsn[0] << 16) |
                       ((uint32_t)emudsk_lsn[1] << 8) |
                       (uint32_t)emudsk_lsn[2];
    g_emudsk_req.buffer_addr = 0;
    g_emudsk_req.command = data;
    g_emudsk_req.status.store(-1, std::memory_order_release);
    (void)fg2bg.push((uint)FG2BG_EMUDSK << 24);
    return;
  }

  emudsk_fifo_mode = false;
  g_emudsk_req.drive = emudsk_drive;
  g_emudsk_req.lsn = ((uint32_t)emudsk_lsn[0] << 16) |
                     ((uint32_t)emudsk_lsn[1] << 8) |
                     (uint32_t)emudsk_lsn[2];
  g_emudsk_req.buffer_addr = ((uint16_t)emudsk_buffer[0] << 8) |
                             (uint16_t)emudsk_buffer[1];
  g_emudsk_req.command = data;
  g_emudsk_req.status.store(-1, std::memory_order_release);

#if defined(G_E)
  // On Centipede 32z (Gerbil), the CoCo's external 0.895 MHz E/Q clock runs
  // continuously, so Core 1 must never spin-wait here. Enqueue the VFS request
  // for Core 0, feed one `BRA -2` ($20 $FE) + assert G_HALT so the 6809E halts
  // cleanly before `LDA $FF83`, and release G_HALT once status >= 0.
  emudsk_status = 0x80;
  emudsk_bra_phase = 0;
  emudsk_dma_busy = true;
  (void)fg2bg.push((uint)FG2BG_EMUDSK << 24);
#else
  uint pb_w = ((uint)FG2BG_EMUDSK << 24);
  while (!fg2bg.push(pb_w)) { tight_loop_contents(); }

  while (g_emudsk_req.status.load(std::memory_order_acquire) < 0) {
    tight_loop_contents();
  }
  emudsk_status = (byte)g_emudsk_req.status.load(std::memory_order_acquire);
#endif
}


struct EmuDsk {   // 7 registers: LSN (3), command/status, buffer/FIFO (2), drive
  static constexpr uint16_t kDefaultBase = 0xFF80;
  static void Install(uint16_t base = kDefaultBase) {
    emudsk_fifo_mode = false;
    emudsk_fifo_idx = 0;
    emudsk_status = 0;
    emudsk_dma_busy = false;
    emudsk_bra_phase = 0;
    IoClaim(base, base + 6, "emudsk");
    IoSet(base + 0, emudsk_read_lsn0, emudsk_write_lsn0);
    IoSet(base + 1, emudsk_read_lsn1, emudsk_write_lsn1);
    IoSet(base + 2, emudsk_read_lsn2, emudsk_write_lsn2);
    IoSet(base + 3, emudsk_read_cmd,  emudsk_write_command);
    IoSet(base + 4, emudsk_read_buf0, emudsk_write_buf0);
    IoSet(base + 5, emudsk_read_buf1, emudsk_write_buf1);
    IoSet(base + 6, emudsk_read_drv,  emudsk_write_drv);
  }
};

// Shared Core-0 background handler for FG2BG_EMUDSK (both DMA commands 0/1 and FIFO commands 0x10/0x12).
// Zero-fills short/EOF reads on sparse grow-on-write `.dsk` images.
inline void emudsk_service_bg(Coro* self) {
  Coro* saved = rpc::g_vfs_coro;
  rpc::g_vfs_coro = self;
  uint8_t drv = g_emudsk_req.drive;
  if (drv > 1 || !g_drive_nodes[drv]) {
    g_emudsk_req.status.store(2, std::memory_order_release);  // Drive not ready / enabled
  } else {
    auto node = g_drive_nodes[drv];
    lfs_soff_t off = (lfs_soff_t)g_emudsk_req.lsn * 256;
    if (node->seek(off, LFS_SEEK_SET, self) != off) {
      g_emudsk_req.status.store(4, std::memory_order_release);
    } else {
      uint16_t buf_addr = g_emudsk_req.buffer_addr;
      byte sec_buf[256];
      if (g_emudsk_req.command == 0) {
        lfs_ssize_t n = node->read(sec_buf, 256);
        if (n >= 0) {
          if (n < 256) memset(sec_buf + n, 0, (size_t)(256 - n));
          if (g_deep_mmu_active) {
            for (int i = 0; i < 256; i++) ram[translate_deep_addr((uint16_t)(buf_addr + i))] = sec_buf[i];
          } else {
            for (int i = 0; i < 256; i++) ram[(uint16_t)(buf_addr + i)] = sec_buf[i];
          }
          g_emudsk_req.status.store(0, std::memory_order_release);
        } else {
          g_emudsk_req.status.store(6, std::memory_order_release);
        }
      } else if (g_emudsk_req.command == 1) {
        if (g_deep_mmu_active) {
          for (int i = 0; i < 256; i++) sec_buf[i] = ram[translate_deep_addr((uint16_t)(buf_addr + i))];
        } else {
          for (int i = 0; i < 256; i++) sec_buf[i] = ram[(uint16_t)(buf_addr + i)];
        }
        g_emudsk_req.status.store(node->write(sec_buf, 256) == 256 ? 0 : 6, std::memory_order_release);
      } else if (g_emudsk_req.command == 0x10) {
        lfs_ssize_t n = node->read(emudsk_fifo_buf, 256);
        if (n >= 0 && n < 256) memset(emudsk_fifo_buf + n, 0, (size_t)(256 - n));
        __dmb();
        g_emudsk_req.status.store(n >= 0 ? 0 : 6, std::memory_order_release);
      } else if (g_emudsk_req.command == 0x12) {
        __dmb();
        lfs_ssize_t n = node->write(emudsk_fifo_buf, 256);
        g_emudsk_req.status.store(n == 256 ? 0 : 6, std::memory_order_release);
      } else {
        g_emudsk_req.status.store(6, std::memory_order_release);
      }
    }
  }
  rpc::g_vfs_coro = saved;
}
