#pragma once
// flat65280.h -- Engine flat65280: NitrOS-9 Level 1 I/O and setup.
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// IN_RAM ACIA 6850 callbacks
byte IN_RAM flat65280_acia_read_status(uint) {
  byte s = 0x02; // TDRE is always 1 (transmit empty)
  if (flat65280_rx_has_char) {
    s |= 0x01; // RDRF = 1 (receive full)
    if (flat65280_acia_ctrl & 0x80) {
      s |= 0x80; // IRQ
    }
  }
  return s;
}

byte IN_RAM flat65280_acia_read_rx(uint) {
  flat65280_rx_has_char = false;
  return flat65280_rx_char;
}

void IN_RAM flat65280_acia_write_ctrl(uint, byte d) {
  flat65280_acia_ctrl = d;
}

void IN_RAM flat65280_acia_write_tx(uint, byte d) {
  Say(d);
}

// IN_RAM RAM vector reader for FFF0..FFFF
byte IN_RAM ram_vector_reader(uint addr) {
  return ram[0xFF00 | (addr & 0xFF)];
}

// IRQ check for flat65280
bool IN_RAM flat65280_irq_needed() {
  bool clock_irq = (clock_and_stop_reg & 0x01) && (clock_and_stop_reg & 0x02);
  bool acia_irq = ((flat65280_acia_ctrl & 0x80) != 0) && flat65280_rx_has_char;
  return clock_irq || acia_irq;
}

void Install_Flat65280_IO() {
  for (int i = 0; i < 256; i++) {
    IOReaders[i] = nullptr;
    IOWriters[i] = nullptr;
  }
  // EMUDSK
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

  // CLOCK_AND_STOP
  IOReaders[0x87] = clock_and_stop_reader;
  IOWriters[0x87] = clock_and_stop_writer;

  // ACIA 6850
  IOReaders[0x88] = flat65280_acia_read_status;
  IOReaders[0x89] = flat65280_acia_read_rx;
  IOWriters[0x88] = flat65280_acia_write_ctrl;
  IOWriters[0x89] = flat65280_acia_write_tx;

  // System Vectors (0xFFF0..0xFFFF)
  for (int i = 0xF0; i <= 0xFF; i++) {
    IOReaders[i] = ram_vector_reader;
  }
}

void setup_flat65280() {
  current_engine = ENGINE_FLAT65280;

  // 1. Open drive nodes if configured
  for (int d = 0; d < 2; d++) {
    const char* path = (d == 0) ? centipede_config.drive0 : centipede_config.drive1;
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
  const char* img_path = centipede_config.flat_img[0] ? centipede_config.flat_img : "/pc/flat65280.img";
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

