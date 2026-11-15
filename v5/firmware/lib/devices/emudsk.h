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
byte IN_RAM emudsk_read_cmd(uint)  { return emudsk_status; }
byte IN_RAM emudsk_read_buf0(uint) { return emudsk_buffer[0]; }
byte IN_RAM emudsk_read_buf1(uint) { return emudsk_buffer[1]; }
byte IN_RAM emudsk_read_drv(uint)  { return emudsk_drive; }

void IN_RAM emudsk_write_lsn0(uint, byte d) { emudsk_lsn[0] = d; }
void IN_RAM emudsk_write_lsn1(uint, byte d) { emudsk_lsn[1] = d; }
void IN_RAM emudsk_write_lsn2(uint, byte d) { emudsk_lsn[2] = d; }
void IN_RAM emudsk_write_buf0(uint, byte d) { emudsk_buffer[0] = d; }
void IN_RAM emudsk_write_buf1(uint, byte d) { emudsk_buffer[1] = d; }
void IN_RAM emudsk_write_drv(uint, byte d)  { emudsk_drive = d; }

void IN_RAM emudsk_write_command(uint, byte data) {
  g_emudsk_req.drive = emudsk_drive;
  g_emudsk_req.lsn = ((uint32_t)emudsk_lsn[0] << 16) |
                     ((uint32_t)emudsk_lsn[1] << 8) |
                     (uint32_t)emudsk_lsn[2];
  g_emudsk_req.buffer_addr = ((uint16_t)emudsk_buffer[0] << 8) |
                             (uint16_t)emudsk_buffer[1];
  g_emudsk_req.command = data;
  g_emudsk_req.status.store(-1, std::memory_order_release);

  uint pb_w = ((uint)FG2BG_EMUDSK << 24);
  while (!fg2bg.push(pb_w)) { tight_loop_contents(); }

  while (g_emudsk_req.status.load(std::memory_order_acquire) < 0) {
    tight_loop_contents();
  }
  emudsk_status = (byte)g_emudsk_req.status.load(std::memory_order_acquire);
}

