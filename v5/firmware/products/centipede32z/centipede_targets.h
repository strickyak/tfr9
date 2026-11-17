#pragma once
// centipede_targets.h -- Core-0 target launchers for all 15 `(on, run, for)` Centipede 32z rows in `ports.tsv`.
//
// Reuses `flat65280.h`, `deep65280.h`, and `gep9.h` directly from `firmware/engines/`, plus adds:
//   - `rsbasic for coco2` (64 KB RAM substitution + Disk 1.1 ROM + floppy, or DECB preload)
//   - `rsbasic for coco3` on `coco2` ("Become CoCo 3": 128 KB GIME/MMU + patched CoCo 3 ROM + Disk 1.1 + floppy)
//   - `rsbasic for coco3` on `coco3` (`gerbil_io` pass-through + optional floppy / enhancements)
//   - `fuzix for coco2` on `coco2` (`gerbil_gime` 256 KB + Fuzix ACIA/SD at $FF60..$FF6C) and `coco3` (`gerbil_io`)
//   - `gerbil_io` variants of `nitros9l1` and `nitros9l2` on a real `coco3`

enum CentipedeLaunchMode : uint8_t {
  LAUNCH_RSBASIC_COCO2 = 0,
  LAUNCH_RSBASIC_COCO3 = 1,
  LAUNCH_HOST_COCO3    = 2,
  LAUNCH_PRELOAD_DECB  = 3,
  LAUNCH_OS_DIRECT     = 4,
};

inline CentipedeLaunchMode g_launch_mode = LAUNCH_RSBASIC_COCO2;
inline uint16_t g_launch_exec_addr = 0xA027;
inline uint16_t g_launch_initial_sp = 0;
inline GerbilLoopFn g_gerbil_loop_fn = gerbil_sam64k_foreground;

// Put a clean 32x16 VDG status banner into $0400..$05FF so the CoCo monitor shows what OS is running on USB!
static void write_vdg_banner(const char* line1, const char* line2 = "SERIAL CONSOLE ON USB TETHER") {
  memset(ram + 0x0400, 0x20, 0x0200);
  auto put_row = [](int row, const char* s) {
    if (!s) return;
    int col = 0;
    for (; s[col] && col < 32; col++) {
      byte c = (byte)s[col];
      if (c >= 'a' && c <= 'z') c = (byte)(c - 'a' + 'A');
      byte vdg = (c >= 0x40 && c <= 0x5F) ? (byte)(c - 0x40) : (c >= 0x20 && c <= 0x3F) ? c : 0x20;
      ram[0x0400 + row * 32 + col] = vdg;
    }
  };
  put_row(1, "=== COPICO CENTIPEDE 32Z V5 ===");
  put_row(3, line1);
  put_row(5, line2);
}

// ── CoCo 3 BASIC ROM Vector Reader/Writer ($FFF0..$FFFF) ──
byte IN_RAM coco3_basic_vec_reader(uint addr) {
  return sram_coco3_rom[0x7FF0u + (addr & 0x0Fu)];
}
void IN_RAM coco3_basic_vec_writer(uint addr, byte d) {
  ram[0x1FFF0u + (addr & 0x0Fu)] = d;
}

// ── Fuzix CoCo2Centipede Devices ($FF60..$FF6C) ──
inline volatile byte fuzix_rx_buf[256];
inline volatile uint8_t fuzix_rx_head = 0;
inline volatile uint8_t fuzix_rx_tail = 0;
inline byte fuzix_sd_buf[512];
inline volatile uint16_t fuzix_sd_idx = 0;
inline volatile byte fuzix_sd_ctl = 0x80;
inline volatile byte fuzix_sd_lba[3] = {0, 0, 0};
inline vfs_file_t g_fuzix_disk_file;
inline bool g_fuzix_disk_opened = false;

byte IN_RAM fuzix_acia_read_stat(uint) {
  return 0x02u | ((fuzix_rx_head != fuzix_rx_tail) ? 0x01u : 0x00u);
}
void IN_RAM fuzix_acia_write_ctrl(uint, byte) {}

byte IN_RAM fuzix_acia_read_data(uint) {
  uint8_t tail = fuzix_rx_tail;
  if (tail != fuzix_rx_head) {
    byte d = fuzix_rx_buf[tail];
    fuzix_rx_tail = (uint8_t)(tail + 1u);
    return d;
  }
  return 0;
}
void IN_RAM fuzix_acia_write_data(uint, byte d) {
  Say(d);
}

byte IN_RAM fuzix_uart_stub_read_stat(uint) { return 0x02u; }
byte IN_RAM fuzix_uart_stub_read_ff(uint)   { return 0xFFu; }
void IN_RAM fuzix_uart_stub_write(uint, byte) {}

byte IN_RAM fuzix_sd_read_data(uint) {
  uint16_t idx = fuzix_sd_idx;
  byte d = (idx < 512u) ? fuzix_sd_buf[idx] : 0xFFu;
  if (idx < 512u) {
    idx++;
    fuzix_sd_idx = idx;
    if (idx >= 512u) fuzix_sd_ctl = 0x80u;
  } else {
    fuzix_sd_ctl = 0x80u;
  }
  return d;
}
void IN_RAM fuzix_sd_write_data(uint, byte d) {
  if (fuzix_sd_idx < 512u) {
    fuzix_sd_buf[fuzix_sd_idx++] = d;
    if (fuzix_sd_idx == 512u) {
      fuzix_sd_ctl = 0x00u;
      __dmb();
      uint32_t lba = ((uint32_t)fuzix_sd_lba[2] << 16) |
                     ((uint32_t)fuzix_sd_lba[1] << 8) |
                     (uint32_t)fuzix_sd_lba[0];
      (void)fg2bg.push(((uint)FG2BG_FUZIX_SD_WRITE << 24) | (lba & 0x00FFFFFFu));
    }
  }
}

byte IN_RAM fuzix_sd_read_ctl(uint) { return fuzix_sd_ctl; }
void IN_RAM fuzix_sd_write_ctl(uint, byte d) {
  if (d == 0x00u) {
    fuzix_sd_idx = 0;
    fuzix_sd_ctl = 0x00u;
    uint32_t lba = ((uint32_t)fuzix_sd_lba[2] << 16) |
                   ((uint32_t)fuzix_sd_lba[1] << 8) |
                   (uint32_t)fuzix_sd_lba[0];
    (void)fg2bg.push(((uint)FG2BG_FUZIX_SD_READ << 24) | (lba & 0x00FFFFFFu));
  } else if (d == 0x01u) {
    fuzix_sd_idx = 0;
    fuzix_sd_ctl = 0xA0u;
  }
}

static uint16_t g_fuzix_sd_base = 0xFF68;
byte IN_RAM fuzix_sd_read_lba(uint addr) {
  return fuzix_sd_lba[(addr - g_fuzix_sd_base - 2u) & 3u];
}
void IN_RAM fuzix_sd_write_lba(uint addr, byte d) {
  uint idx = (addr - g_fuzix_sd_base - 2u);
  if (idx < 3u) fuzix_sd_lba[idx] = d;
}

static void fuzix_poll_rx() {
  while (term_input.NumBuffered() > 0) {
    uint8_t next = (uint8_t)(fuzix_rx_head + 1u);
    if (next == fuzix_rx_tail) break;
    fuzix_rx_buf[fuzix_rx_head] = term_input.Take();
    fuzix_rx_head = next;
  }
}

static void Install_Fuzix_Ports() {
  uint16_t acia_base = PortBase("acia", 0xFF60);
  IoClaim(acia_base, acia_base + 1, "acia");
  IoSet(acia_base + 0, fuzix_acia_read_stat, fuzix_acia_write_ctrl);
  IoSet(acia_base + 1, fuzix_acia_read_data, fuzix_acia_write_data);

  uint16_t stubs_base = PortBase("uart_stubs", 0xFF62);
  IoClaim(stubs_base, stubs_base + 3, "uart_stubs");
  IoSet(stubs_base + 0, fuzix_uart_stub_read_stat, fuzix_uart_stub_write);
  IoSet(stubs_base + 1, fuzix_uart_stub_read_ff,   fuzix_uart_stub_write);
  IoSet(stubs_base + 2, fuzix_uart_stub_read_stat, fuzix_uart_stub_write);
  IoSet(stubs_base + 3, fuzix_uart_stub_read_ff,   fuzix_uart_stub_write);

  g_fuzix_sd_base = PortBase("sd", 0xFF68);
  IoClaim(g_fuzix_sd_base, g_fuzix_sd_base + 4, "sd");
  IoSet(g_fuzix_sd_base + 0, fuzix_sd_read_data, fuzix_sd_write_data);
  IoSet(g_fuzix_sd_base + 1, fuzix_sd_read_ctl,  fuzix_sd_write_ctl);
  IoSet(g_fuzix_sd_base + 2, fuzix_sd_read_lba,  fuzix_sd_write_lba);
  IoSet(g_fuzix_sd_base + 3, fuzix_sd_read_lba,  fuzix_sd_write_lba);
  IoSet(g_fuzix_sd_base + 4, fuzix_sd_read_lba,  fuzix_sd_write_lba);
  g_rx_poll_fn = fuzix_poll_rx;
}

// Load a DECB binary into `ram[0..65535]` and return true with `*out_exec` set.
static bool centipede_load_decb_flat(const char* path, uint16_t* out_exec, Coro* coro_self) {
  vfs_file_t f;
  int err = vfs_file_open(&f, path, LFS_O_RDONLY);
  if (err < 0) {
    cobs_printf("[centipede: failed to open DECB %s (err %d)]\n", path, err);
    return false;
  }
  cobs_printf("[centipede: loading DECB %s...]\n", path);
  uint32_t total_loaded = 0;
  bool got_exec = false;
  while (true) {
    uint8_t hdr[5];
    if (vfs_file_read(&f, hdr, 5) < 5) break;
    uint8_t tag = hdr[0];
    uint16_t len = ((uint16_t)hdr[1] << 8) | hdr[2];
    uint16_t addr = ((uint16_t)hdr[3] << 8) | hdr[4];
    if (tag == 0x00) {
      uint16_t got = 0;
      while (got < len) {
        uint16_t chunk = (len - got > 512) ? 512 : (len - got);
        lfs_ssize_t r = vfs_file_read(&f, ram + ((addr + got) & 0xFFFFu), chunk);
        if (r <= 0) break;
        got += (uint16_t)r;
      }
      total_loaded += got;
    } else if (tag == 0xFD) {
      uint16_t got = 0;
      uint8_t discard[64];
      while (got < len) {
        uint16_t chunk = (len - got > sizeof(discard)) ? sizeof(discard) : (len - got);
        lfs_ssize_t r = vfs_file_read(&f, discard, chunk);
        if (r <= 0) break;
        got += (uint16_t)r;
      }
    } else if (tag == 0xFF) {
      *out_exec = addr;
      got_exec = true;
      cobs_printf("[centipede: DECB exec=$%04x (%u bytes)]\n", addr, (unsigned)total_loaded);
      break;
    } else {
      break;
    }
  }
  vfs_file_close(&f, coro_self);
  __dmb();
  return got_exec || total_loaded > 0;
}

// ── 1. coco2 / rsbasic / coco2 (`gerbil_sam64k`) ──
static void setup_centipede_rsbasic_coco2() {
  is_an_os9 = false;
  IoReset();
  SamP1Bit = false;
  SamTyBit = false;
  g_gime_rom_mode = false;

  if (g_config.preload_decb[0] != '\0') {
    g_sam64k_ram_enabled = true;
    g_disk11_rom_enabled = false;
    g_floppy_enabled = false;
    memset(ram, 0, 65536);
    memset(ram + 0x0400, 0x20, 0x0200);
    uint16_t exec_addr = 0x2400;
    centipede_load_decb_flat(g_config.preload_decb, &exec_addr, gspoon::g_spoon_coro);
    SamTyBit = true;
    g_launch_mode = LAUNCH_PRELOAD_DECB;
    g_launch_exec_addr = exec_addr;
    g_launch_initial_sp = 0xFE00;
  } else {
    g_sam64k_ram_enabled = g_config.ram_64k;
    g_disk11_rom_enabled = g_config.rom_disk11;
    g_floppy_enabled = g_config.floppy_fd || g_config.floppy_pc;
    memset(ram, 0, 65536);
    if (g_disk11_rom_enabled) {
      memcpy(sram_disk11_rom, kDisk11RomFlash, sizeof(kDisk11RomFlash));
    }
    set_floppy_names();
    g_launch_mode = LAUNCH_RSBASIC_COCO2;
    g_launch_exec_addr = 0xA027;
    g_launch_initial_sp = 0;
  }
  IoFinish();
}

// ── 2. coco2 / rsbasic / coco3 (`gerbil_gime`, "Become CoCo 3") ──
static void setup_centipede_rsbasic_coco3() {
  is_an_os9 = false;
  IoReset();
  SamP1Bit = false;
  SamTyBit = false;
  g_deep_mmu_mask = 0x0F;      // 128 KB CoCo 3 (16 blocks of 8 KB)
  g_deep_fe_base  = 0x1E000;   // Block 15
  g_gime_rom_mode = true;
  g_disk11_rom_enabled = g_config.rom_disk11;
  g_floppy_enabled = g_config.floppy_fd || g_config.floppy_pc;

  memset(ram, 0, 128 * 1024);
  memcpy(sram_coco3_rom, kCoco3RomFlash, sizeof(kCoco3RomFlash));
  if (g_disk11_rom_enabled) {
    memcpy(sram_disk11_rom, kDisk11RomFlash, sizeof(kDisk11RomFlash));
  }

  // Apply proven CoCo 2 host compatibility patches to the SRAM CoCo 3 ROM copy (see v4 coco128k.h):
  // 1) Keep SAM Page 0 ($FFD4) instead of Page 1 ($FFD5) at $C09D so CoCo 2 VDG scans Bank 0 DRAM:
  sram_coco3_rom[0xC09D - 0x8000 + 1] = 0x5C;
  // 2) Initialize DP = 0 explicitly when entering at $C000 without CPU hardware reset:
  sram_coco3_rom[0xC041 - 0x8000 + 0] = 0x4F;  // CLRA
  sram_coco3_rom[0xC041 - 0x8000 + 1] = 0x1F;  // TFR A,DP
  sram_coco3_rom[0xC041 - 0x8000 + 2] = 0x8B;
  sram_coco3_rom[0xA05B - 0x8000 + 0] = 0x4F;  // CLRA
  sram_coco3_rom[0xA05B - 0x8000 + 1] = 0x1F;  // TFR A,DP
  sram_coco3_rom[0xA05B - 0x8000 + 2] = 0x8B;
  sram_coco3_rom[0xA05B - 0x8000 + 3] = 0x6E;  // JMP ,Y
  sram_coco3_rom[0xA05B - 0x8000 + 4] = 0xA4;
  sram_coco3_rom[0xA087 - 0x8000 + 0] = 0x4F;  // CLRA
  sram_coco3_rom[0xA087 - 0x8000 + 1] = 0x1F;  // TFR A,DP
  sram_coco3_rom[0xA087 - 0x8000 + 2] = 0x8B;
  sram_coco3_rom[0xA087 - 0x8000 + 3] = 0x20;  // BRA $A093
  sram_coco3_rom[0xA087 - 0x8000 + 4] = 0x07;

  // Pre-populate RAM blocks 12..15 ($18000..$1FFFF) with patched CoCo 3 ROM (+ Disk 1.1 at $1C000 if enabled)
  memcpy(ram + 0x18000, sram_coco3_rom, 0x8000);
  if (g_disk11_rom_enabled) {
    memcpy(ram + 0x1C000, sram_disk11_rom, 0x2000);
  }
  // Pre-populate INTIMAGE ($1FEED..$1FEFD) with validity flag $1FEED = 0 (forces cold boot) + vectors
  memcpy(ram + 0x1FEED, sram_coco3_rom + 0x4359, 19);
  ram[0x1FEED] = 0;
  memcpy(ram + 0x1FFF0, sram_coco3_rom + 0x7FF0, 16);

  // CoCo 3 power-on MMU task maps
  static const uint8_t kTask0[8] = {0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
  static const uint8_t kTask1[8] = {0x38, 0x30, 0x31, 0x32, 0x33, 0x3D, 0x35, 0x3F};
  for (int s = 0; s < 8; s++) {
    deep65280_mmu[0][s] = kTask0[s];
    deep65280_mmu[1][s] = kTask1[s];
  }
  Coco3Gime::Install(PortBase("gime", 0xFF90), 16, /*init0=*/0x0A, /*init1=*/0x00);
  DeepMmu::Install(PortBase("mmu", 0xFFA0));
  Coco3Palette::Install(PortBase("palette", 0xFFB0));
  uint16_t vec_base = PortBase("vectors", 0xFFF0);
  IoClaim(vec_base, vec_base + 15, "vectors");
  for (int i = 0; i < 16; i++) {
    IoSet(vec_base + i, coco3_basic_vec_reader, coco3_basic_vec_writer);
  }
  set_floppy_names();
  IoFinish();

  g_launch_mode = LAUNCH_RSBASIC_COCO3;
  g_launch_exec_addr = 0xC000;
  g_launch_initial_sp = 0;
}

// ── 3. coco3 / rsbasic / coco3 (`gerbil_io`, real CoCo 3 motherboard) ──
static void setup_centipede_host_coco3_rsbasic() {
  is_an_os9 = false;
  IoReset();
  g_floppy_enabled = g_config.floppy_fd || g_config.floppy_pc;
  set_floppy_names();
  IoFinish();
  g_launch_mode = LAUNCH_HOST_COCO3;
  g_launch_exec_addr = 0;
  g_launch_initial_sp = 0;
}

// ── 4. coco2 / nitros9l1 / flat65280 (`gerbil_sam64k`) ──
static void setup_centipede_nitros9l1_coco2() {
  g_sam64k_ram_enabled = true;
  g_disk11_rom_enabled = false;
  g_floppy_enabled = false;
  g_gime_rom_mode = false;
  SamP1Bit = false;
  SamTyBit = true;
  setup_flat65280();
  write_vdg_banner("NITROS-9 LEVEL 1 (FLAT65280)");
  g_launch_mode = LAUNCH_OS_DIRECT;
  g_launch_exec_addr = ((uint16_t)vector_ram[14] << 8) | vector_ram[15];
  g_launch_initial_sp = 0;
}

// ── 5. coco3 / nitros9l1 / flat65280 (`gerbil_io`) ──
static void setup_centipede_nitros9l1_coco3() {
  is_an_os9 = true;
  g_floppy_enabled = false;
  for (int d = 0; d < 2; d++) {
    const char* path = (d == 0) ? g_config.drive0 : g_config.drive1;
    g_drive_nodes[d] = (path && path[0]) ? vfs_resolve(path) : nullptr;
    if (g_drive_nodes[d] && g_drive_nodes[d]->open_file(LFS_O_RDWR) < 0) {
      if (g_drive_nodes[d]->open_file(LFS_O_RDONLY) < 0) g_drive_nodes[d] = nullptr;
    }
  }
  IoReset();
  EmuDsk::Install(PortBase("emudsk", EmuDsk::kDefaultBase));
  ClockStop::Install(PortBase("clockstop", ClockStop::kDefaultBase));
  TermAcia::Install(PortBase("acia", TermAcia::kDefaultBase));
  IoFinish();
  g_launch_mode = LAUNCH_HOST_COCO3;
  g_launch_exec_addr = 0;
  g_launch_initial_sp = 0;
}

// ── 6. coco2 / nitros9l2 / deep65280 (`gerbil_gime`) ──
static void setup_centipede_nitros9l2_deep_coco2() {
  g_deep_mmu_mask = 0x1F;
  g_deep_fe_base  = 0x3E000;
  g_gime_rom_mode = false;
  g_disk11_rom_enabled = false;
  g_floppy_enabled = false;
  SamP1Bit = false;
  SamTyBit = true;
  setup_deep65280();
  write_vdg_banner("NITROS-9 LEVEL 2 (DEEP65280)");
  g_launch_mode = LAUNCH_OS_DIRECT;
  g_launch_exec_addr = ((uint16_t)vector_ram[14] << 8) | vector_ram[15];
  g_launch_initial_sp = 0;
}

// ── 7. coco3 / nitros9l2 / deep65280 (`gerbil_io`) ──
static void setup_centipede_nitros9l2_deep_coco3() {
  setup_centipede_nitros9l1_coco3();  // Same core I/O ports: emudsk + clockstop + acia
}

// ── 8. coco2 / nitros9l2 / coco3 (`gerbil_gime`) ──
static void setup_centipede_nitros9l2_coco3_on_coco2() {
  g_deep_mmu_mask = 0x1F;
  g_deep_fe_base  = 0x3E000;
  g_gime_rom_mode = false;
  g_disk11_rom_enabled = false;
  g_floppy_enabled = false;
  SamP1Bit = false;
  SamTyBit = true;
  setup_nitros9l2_coco3();
  write_vdg_banner("NITROS-9 LEVEL 2 (COCO3)");
  g_launch_mode = LAUNCH_OS_DIRECT;
  g_launch_exec_addr = ((uint16_t)vector_ram[14] << 8) | vector_ram[15];
  g_launch_initial_sp = 0;
}

// ── 9. coco3 / nitros9l2 / coco3 (`gerbil_io`) ──
static void setup_centipede_nitros9l2_coco3_on_coco3() {
  is_an_os9 = true;
  g_floppy_enabled = false;
  for (int d = 0; d < 2; d++) {
    const char* path = (d == 0) ? g_config.drive0 : g_config.drive1;
    g_drive_nodes[d] = (path && path[0]) ? vfs_resolve(path) : nullptr;
    if (g_drive_nodes[d] && g_drive_nodes[d]->open_file(LFS_O_RDWR) < 0) {
      if (g_drive_nodes[d]->open_file(LFS_O_RDONLY) < 0) g_drive_nodes[d] = nullptr;
    }
  }
  IoReset();
  EmuDsk::Install(PortBase("emudsk", 0xFF80));
  TermAcia::Install(PortBase("acia", 0xFF88));
  IoFinish();
  g_launch_mode = LAUNCH_HOST_COCO3;
  g_launch_exec_addr = 0;
  g_launch_initial_sp = 0;
}

// ── 9b. coco2 / nitros9l2 / jamesbrown (`gerbil_gime`) ──
static void setup_centipede_nitros9l2_jamesbrown_on_coco2() {
  g_deep_mmu_mask = 0x1F;
  g_deep_fe_base  = 0x3E000;
  g_gime_rom_mode = false;
  g_disk11_rom_enabled = false;
  g_floppy_enabled = false;
  SamP1Bit = false;
  SamTyBit = true;
  g_guest_boot_us = time_us_64();
  setup_jamesbrown_coco();
  write_vdg_banner("JAMESBROWN NITROS-9 L2 (256K)");
  g_launch_mode = LAUNCH_OS_DIRECT;
  g_launch_exec_addr = ((uint16_t)vector_ram[14] << 8) | vector_ram[15];
  g_launch_initial_sp = 0;
}

// ── 9c. coco3 / nitros9l2 / jamesbrown (`gerbil_io`) ──
static void setup_centipede_nitros9l2_jamesbrown_on_coco3() {
  is_an_os9 = true;
  g_floppy_enabled = false;
  g_guest_boot_us = time_us_64();
  for (int d = 0; d < 2; d++) {
    const char* path = (d == 0) ? g_config.drive0 : g_config.drive1;
    g_drive_nodes[d] = (path && path[0]) ? vfs_resolve(path) : nullptr;
    if (g_drive_nodes[d] && g_drive_nodes[d]->open_file(LFS_O_RDWR) < 0) {
      if (g_drive_nodes[d]->open_file(LFS_O_RDONLY) < 0) g_drive_nodes[d] = nullptr;
    }
  }
  IoReset();
  BeckerPort::Install(PortBase("becker", 0xFF41));
  AudioSn76489::Install(PortBase("audio", 0xFF70));
  RtcPort::Install(PortBase("rtc", 0xFF72));
  InfoPort::Install(PortBase("info", 0xFF7C));
  EmuDsk::Install(PortBase("emudsk", 0xFF80));
  TermAcia::Install(PortBase("acia", 0xFF88));
  IoFinish();
  g_launch_mode = LAUNCH_HOST_COCO3;
  g_launch_exec_addr = 0;
  g_launch_initial_sp = 0;
}

// ── 10 & 11. coco2|coco3 / hatvan / coco2 (`gerbil_gep9`) ──
static void setup_centipede_hatvan() {
  g_floppy_enabled = false;
  g_disk11_rom_enabled = false;
  g_gime_rom_mode = false;
  SamP1Bit = false;
  SamTyBit = true;
  setup_hatvan_coco2();
  write_vdg_banner("HATVAN OS (GEP9 PAGED MMU)");
  uint16_t rst_hi = *gep9_mmu.TranslateRead(0, 0xFFFE);
  uint16_t rst_lo = *gep9_mmu.TranslateRead(0, 0xFFFF);
  g_launch_mode = LAUNCH_OS_DIRECT;
  g_launch_exec_addr = (rst_hi << 8) | rst_lo;
  g_launch_initial_sp = 0;
}

// ── 12. coco2 / fuzix / coco2 (`gerbil_gime`) ──
static void setup_centipede_fuzix_coco2() {
  is_an_os9 = false;
  g_deep_mmu_mask = 0x1F;
  g_deep_fe_base  = 0x3E000;
  g_gime_rom_mode = false;
  g_disk11_rom_enabled = false;
  g_floppy_enabled = false;
  SamP1Bit = false;
  SamTyBit = true;

  for (int s = 0; s < 8; s++) {
    deep65280_mmu[0][s] = (uint8_t)s;
    deep65280_mmu[1][s] = (uint8_t)s;
  }
  fuzix_rx_head = 0;
  fuzix_rx_tail = 0;
  fuzix_sd_idx = 0;
  fuzix_sd_ctl = 0x80;

  const char* disk_path = g_config.drive0[0] ? g_config.drive0 : "/pc/disk2.img";
  if (!g_fuzix_disk_opened) {
    int res = vfs_file_open(&g_fuzix_disk_file, disk_path, LFS_O_RDWR);
    if (res >= 0) {
      g_fuzix_disk_opened = true;
    } else {
      cobs_printf("[fuzix: failed to open %s (%d)]\n", disk_path, res);
    }
  }

  memset(ram, 0, sizeof(ram));
  uint16_t exec_addr = 0x0200;
  const char* bin_path = g_config.flat_img[0] ? g_config.flat_img : "/pc/fuzix.bin";
  centipede_load_decb_flat(bin_path, &exec_addr, gspoon::g_spoon_coro);
  memcpy(vector_ram, &ram[0xFFF0], 16);

  IoReset();
  Install_Fuzix_Ports();
  Coco3Gime::Install(PortBase("gime", 0xFF90), 16, /*init0=*/0x4C, /*init1=*/0x01);
  DeepMmu::Install(PortBase("mmu", 0xFFA0));
  Coco3Palette::Install(PortBase("palette", 0xFFB0));
  DeepVectors::Install();
  IoFinish();

  write_vdg_banner("FUZIX UNIX (COCO2CENTIPEDE)");
  g_launch_mode = LAUNCH_OS_DIRECT;
  g_launch_exec_addr = exec_addr;
  g_launch_initial_sp = 0xE200;
}

// ── 13. coco3 / fuzix / coco2 (`gerbil_io`) ──
static void setup_centipede_fuzix_coco3() {
  is_an_os9 = false;
  g_floppy_enabled = false;
  fuzix_rx_head = 0;
  fuzix_rx_tail = 0;
  fuzix_sd_idx = 0;
  fuzix_sd_ctl = 0x80;

  const char* disk_path = g_config.drive0[0] ? g_config.drive0 : "/pc/disk2.img";
  if (!g_fuzix_disk_opened && vfs_file_open(&g_fuzix_disk_file, disk_path, LFS_O_RDWR) >= 0) {
    g_fuzix_disk_opened = true;
  }

  IoReset();
  Install_Fuzix_Ports();
  IoFinish();
  g_launch_mode = LAUNCH_HOST_COCO3;
  g_launch_exec_addr = 0;
  g_launch_initial_sp = 0;
}

struct CentipedeTargetLauncher {
  const char* on_host;
  const char* run_os;
  const char* for_hw;
  void (*setup)();
};

static const CentipedeTargetLauncher kCentipedeLaunchers[] = {
  {"coco2", "rsbasic",   "coco2",     setup_centipede_rsbasic_coco2},
  {"coco2", "rsbasic",   "coco3",     setup_centipede_rsbasic_coco3},
  {"coco2", "nitros9l1", "flat65280", setup_centipede_nitros9l1_coco2},
  {"coco2", "nitros9l2", "deep65280",  setup_centipede_nitros9l2_deep_coco2},
  {"coco2", "nitros9l2", "jamesbrown", setup_centipede_nitros9l2_jamesbrown_on_coco2},
  {"coco2", "nitros9l2", "coco3",      setup_centipede_nitros9l2_coco3_on_coco2},
  {"coco2", "hatvan",    "coco2",      setup_centipede_hatvan},
  {"coco2", "fuzix",     "coco2",      setup_centipede_fuzix_coco2},
  {"coco3", "rsbasic",   "coco3",      setup_centipede_host_coco3_rsbasic},
  {"coco3", "nitros9l1", "flat65280",  setup_centipede_nitros9l1_coco3},
  {"coco3", "nitros9l2", "deep65280",  setup_centipede_nitros9l2_deep_coco3},
  {"coco3", "nitros9l2", "jamesbrown", setup_centipede_nitros9l2_jamesbrown_on_coco3},
  {"coco3", "nitros9l2", "coco3",      setup_centipede_nitros9l2_coco3_on_coco3},
  {"coco3", "hatvan",    "coco2",     setup_centipede_hatvan},
  {"coco3", "fuzix",     "coco2",     setup_centipede_fuzix_coco3},
};

static inline const CentipedeTargetLauncher* FindCentipedeLauncher(const char* on_host,
                                                                   const char* run_os,
                                                                   const char* for_hw) {
  for (const CentipedeTargetLauncher& t : kCentipedeLaunchers) {
    if (!strcmp(on_host, t.on_host) && !strcmp(run_os, t.run_os) && !strcmp(for_hw, t.for_hw)) {
      return &t;
    }
  }
  return nullptr;
}
