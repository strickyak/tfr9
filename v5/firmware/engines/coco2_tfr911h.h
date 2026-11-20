#pragma once
// coco2_tfr911h.h -- Engine `hamster_coco2` setup & I/O installers for CoCo 2 Disk Extended Color BASIC 1.1 on TFR/911H.
//
// Included by firmware/products/tfr911h/main.cpp after coco2_mb.h, coco2_rom.h, disk11_rom.h, floppy.h, and io_install.h.

static byte IN_RAM coco2_floppy_reader(uint addr) {
  byte data = 0xFFu;
  FloppyImpl::ReadScsFloppy(addr, data);
  return data;
}

static void IN_RAM coco2_floppy_writer(uint addr, byte data) {
  const uint8_t prev_idx = floppy_write_active_idx;
  FloppyImpl::WriteScsFloppy(addr, data);
  if (floppy_write_active_idx != prev_idx) {
    floppy_w256_busy_mask.fetch_or((uint8_t)(1u << prev_idx), std::memory_order_release);
  }
}

FORCE_INLINE bool IN_RAM coco2_floppy_wants_halt() {
  if (FloppyImpl::WantsHalt()) return true;
  if (floppy_in_write &&
      (floppy_w256_busy_mask.load(std::memory_order_acquire) & (1u << floppy_write_active_idx)) != 0) {
    return true;
  }
  return false;
}

static void coco2_poll_rx() {
  while (term_input.NumBuffered() > 0) {
    unsigned char c = term_input.Take();
    if (c != 0) {
      keyboard_injector::queue_char((char)c, /*down_ticks=*/2, /*up_ticks=*/2);
    }
  }
}

struct Coco2Pia {
  static constexpr uint16_t kDefaultBase = 0xFF00;
  static void Install(uint16_t base = kDefaultBase) {
    IoClaim(base, base + 0x3Fu, "coco_pia");
    for (uint16_t i = 0; i < 0x20u; i++) {
      IoSet((uint16_t)(base + i), coco2_pia0_reader, coco2_pia0_writer);
    }
    for (uint16_t i = 0x20u; i < 0x40u; i++) {
      IoSet((uint16_t)(base + i), coco2_pia1_reader, coco2_pia1_writer);
    }
  }
};

struct Coco2FloppyLatch {
  static constexpr uint16_t kDefaultBase = 0xFF40;
  static void Install(uint16_t base = kDefaultBase) {
    IoClaim(base, base, "floppy_latch");
    IoSet(base, coco2_floppy_reader, coco2_floppy_writer);
  }
};

struct Coco2FloppyFdc {
  static constexpr uint16_t kDefaultBase = 0xFF48;
  static void Install(uint16_t base = kDefaultBase) {
    IoClaim(base, base + 3u, "floppy_fdc");
    for (uint16_t i = 0; i < 4u; i++) {
      IoSet((uint16_t)(base + i), coco2_floppy_reader, coco2_floppy_writer);
    }
  }
};

struct Coco2Sam {
  static constexpr uint16_t kDefaultBase = 0xFFC0;
  static void Install(uint16_t base = kDefaultBase) {
    IoClaim(base, base + 0x1Fu, "coco_sam");
    for (uint16_t i = 0; i < 0x20u; i++) {
      IoSet((uint16_t)(base + i), coco2_sam_reader, coco2_sam_writer);
    }
  }
};

struct Coco2Vectors {
  static constexpr uint16_t kDefaultBase = 0xFFF0;
  static void Install(uint16_t base = kDefaultBase) {
    IoClaim(base, base + 0x0Fu, "vectors");
    for (uint16_t i = 0; i < 0x10u; i++) {
      IoSet((uint16_t)(base + i), coco2_vector_reader, coco2_vector_writer);
    }
  }
};

static void Install_Coco2_Basic_IO() {
  IoReset();
  Coco2Pia::Install(PortBase("coco_pia", Coco2Pia::kDefaultBase));
  Coco2FloppyLatch::Install(PortBase("floppy_latch", Coco2FloppyLatch::kDefaultBase));
  Coco2FloppyFdc::Install(PortBase("floppy_fdc", Coco2FloppyFdc::kDefaultBase));
  Coco2Sam::Install(PortBase("coco_sam", Coco2Sam::kDefaultBase));
  Coco2Vectors::Install(PortBase("vectors", Coco2Vectors::kDefaultBase));
  g_timer_60hz_fn = coco2_60hz_tick;
  g_rx_poll_fn = coco2_poll_rx;
  g_term_acia_arrow_keys = true;
  IoFinish();
}

static void setup_coco2_basic() {
  is_an_os9 = false;
  sim_status_reg = 0;
  sim_control_reg = 0;

  // 1. Clear 64 KB CoCo 2 guest RAM and populate 32 KB SRAM ROM window at ram[0x10000..0x17FFF].
  memset(ram, 0, 65536);
  memset(&ram[kCoco2RomSramBase], 0xFF, 32768);
  memcpy(&ram[kCoco2RomSramBase + 0x0000u], kExtBas11RomFlash, sizeof(kExtBas11RomFlash));
  memcpy(&ram[kCoco2RomSramBase + 0x2000u], kBas13RomFlash, sizeof(kBas13RomFlash));
  memcpy(&ram[kCoco2RomSramBase + 0x4000u], kDisk11RomFlash, sizeof(kDisk11RomFlash));

  // 2. Reset PIA0, PIA1, SAM, keyboard injector, NMI pulse fuse, and VFS floppy controller.
  coco2_mb_reset();
  nmi_fuse = 0;
  gpio_set_dir(NMI, GPIO_IN);
  gpio_set_dir(FIRQ, GPIO_IN);
  floppy_latch = 0;
  floppy_track = 0;
  floppy_sector = 1;
  floppy_data = 0;
  floppy_status.store(0, std::memory_order_relaxed);
  floppy_w256_busy_mask.store(0, std::memory_order_relaxed);
  keyboard_injector::active = false;
  keyboard_injector::queued_string.clear();
  set_floppy_names();

  // 3. Install CoCo 2 motherboard + Disk 1.1 I/O ports and user enhancements.
  Install_Coco2_Basic_IO();
  coco2_active = true;

  uint16_t reset_vec = ((uint16_t)coco2_vector_reader(0xFFFEu) << 8) | coco2_vector_reader(0xFFFFu);
  cobs_printf("[coco2: 64KB RAM + ExtBAS 1.1 + ColorBAS 1.3 + DiskBAS 1.1 ready, reset=$%04X]\n",
              (unsigned)reset_vec);
}
