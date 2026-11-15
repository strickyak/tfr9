#ifndef V4_GEP9_IO_H_
#define V4_GEP9_IO_H_

#include <atomic>
#include "v4_gep9_mmu.h"

// ═══════════════════════════════════════════════════════════════════
// gep9 Hardware I/O Registers ($FF00..$FFFF)
//
// Register Map:
//   $FF00: Term.Out (putchar)
//   $FF01: Term.In  (getchar, non-blocking)
//   $FF02: Reg.Stat (Bit 0: Timer.Ready, Bit 1: Term.RxReady, W1C)
//   $FF03: Reg.Ctrl (Bit 0: TimrIRQ, Bit 1: TermIRQ)
//   $FF04: logchar  (diagnostic trace log)
//   $FF05: Exit.Code
//
//   $FF10..$FF17: Hatvan Disk Controller (/d0../d3)
//   $FF20: TaskFuse
//   $FF21..$FF2A: Fast DMA Block Copy Engine
//   $FF2B..$FF2C: Shared Memory Curtain (default $E000)
//   $FF2D: TaskFlagsTarget
//   $FF2E: TaskFlagsRegister (Bit 0 = TASK_FLAG_IO)
//   $FF2F: PurgeTaskMem (Reclamation Port)
//   $FFF0..$FFFF: 6309 System Vectors
// ═══════════════════════════════════════════════════════════════════

struct Gep9IOState {
  volatile byte reg_stat; // $FF02: Bit 0 = Timer.Ready, Bit 1 = Term.RxReady
  volatile byte reg_ctrl; // $FF03: Bit 0 = TimrIRQ, Bit 1 = TermIRQ
};

struct Gep9DMAState {
  uint8_t  src_task;  // $FF21
  uint8_t  src_page;  // $FF22
  uint16_t src_addr;  // $FF23..$FF24
  uint8_t  dst_task;  // $FF25
  uint8_t  dst_page;  // $FF26
  uint16_t dst_addr;  // $FF27..$FF28
  uint8_t  count;     // $FF29
  uint8_t  status;    // $FF2A: 0=busy, 1=OK, >1=error
};

struct Gep9DiskState {
  uint8_t  drive;       // $FF10 (0..3)
  uint32_t sector;      // $FF11..$FF13 (24-bit LSN)
  uint8_t  mem_task;    // $FF14
  uint16_t mem_addr;    // $FF15..$FF16
  uint8_t  status;      // $FF17: 0=busy, 1=OK, >1=error
};

struct Gep9DiskRequest {
  uint8_t  drive;
  uint32_t sector;
  uint8_t  mem_task;
  uint16_t mem_addr;
  uint8_t  command;     // 1=read sector, 2=write sector
  std::atomic<int8_t> status; // -1=pending, 0/1=OK, >1=error
};

inline Gep9IOState     gep9_io;
inline Gep9DMAState    gep9_dma;
inline Gep9DiskState   gep9_disk;
inline Gep9DiskRequest g_gep9_disk_req;

inline volatile byte   gep9_rx_char = 0;
inline volatile bool   gep9_rx_has_char = false;

// Forward-declared utilities from main
void Say(byte c);
void cobs_printf(const char* fmt, ...);
void rp2350_reset_standard();

// ── IRQ Evaluation ──
FORCE_INLINE bool IN_RAM gep9_irq_needed() {
  return (gep9_io.reg_stat & gep9_io.reg_ctrl & 0x03) != 0;
}

FORCE_INLINE void IN_RAM gep9_update_irq_pin() {
  if (gep9_irq_needed()) {
    gpio_set_dir(22, GPIO_OUT);
  } else {
    gpio_set_dir(22, GPIO_IN);
  }
}

// ── Console & Diagnostic Callbacks ($FF00..$FF05) ──
byte IN_RAM gep9_read_term_out(uint) { return 0; }
void IN_RAM gep9_write_term_out(uint, byte d) { Say(d); }

byte IN_RAM gep9_read_term_in(uint) {
  static bool reported_shell_stats = false;
  if (!reported_shell_stats && gep9_mmu.task_tables[3] != nullptr && gep9_mmu.CountTaskFrames(3) > 0) {
    reported_shell_stats = true;
    cobs_printf("\n[gep9 shell running: %d / 1024 physical pages allocated (%.1f KB / 256 KB)]\n",
                (int)gep9_mmu.allocated_count,
                (float)(gep9_mmu.allocated_count * 256) / 1024.0f);
    for (int t = 0; t < 16; t++) {
      int cnt = gep9_mmu.CountTaskFrames(t);
      if (cnt > 0) {
        cobs_printf("  Task %2d: %3d pages (%5d bytes / %4.1f KB)\n",
                    t, cnt, cnt * 256, (float)(cnt * 256) / 1024.0f);
      }
    }
  }

  if (gep9_rx_has_char) {
    byte c = gep9_rx_char;
    gep9_rx_has_char = false;
    gep9_io.reg_stat &= ~0x02; // Clear Term.RxReady
    gep9_update_irq_pin();
    return c;
  }
  return 0; // Non-blocking: 0 when empty
}
void IN_RAM gep9_write_term_in(uint, byte) {}

byte IN_RAM gep9_read_reg_stat(uint) { return gep9_io.reg_stat; }
void IN_RAM gep9_write_reg_stat(uint, byte d) {
  gep9_io.reg_stat &= ~(d & 0x03); // W1C
  if (gep9_rx_has_char) {
    gep9_io.reg_stat |= 0x02;
  }
  gep9_update_irq_pin();
}

byte IN_RAM gep9_read_reg_ctrl(uint) { return gep9_io.reg_ctrl; }
void IN_RAM gep9_write_reg_ctrl(uint, byte d) {
  gep9_io.reg_ctrl = d & 0x03;
  gep9_update_irq_pin();
}

byte IN_RAM gep9_read_logchar(uint) { return 0; }
void IN_RAM gep9_write_logchar(uint, byte d) { Say(d); }

byte IN_RAM gep9_read_exit(uint) { return 0; }
void IN_RAM gep9_write_exit(uint, byte d) {
  // Immediately assert RESET and HALT to stop 6309 bus activity
  gpio_put(20, 0); gpio_set_dir(20, GPIO_OUT);
  gpio_put(24, 0); gpio_set_dir(24, GPIO_OUT);

  // Wait for Core 0 to drain pending console characters
  while (fg2bg_chars.size() > 0) {
    sleep_ms(2);
  }
  sleep_ms(30);

  cobs_printf("\n *** GEP9 Exiting with status %d\n", (int)d);
  cobs_printf("[gep9 on exit: %d / 1024 physical pages allocated (%.1f KB / 256 KB)]\n",
              (int)gep9_mmu.allocated_count,
              (float)(gep9_mmu.allocated_count * 256) / 1024.0f);
  for (int t = 0; t < 16; t++) {
    int cnt = gep9_mmu.CountTaskFrames(t);
    if (cnt > 0) {
      cobs_printf("  Task %2d: %3d pages (%5d bytes / %4.1f KB)\n",
                  t, cnt, cnt * 256, (float)(cnt * 256) / 1024.0f);
    }
  }
  cobs_shutdown(d);
  stdio_flush();
  sleep_ms(100);
  rp2350_reset_standard();
}

// ── Disk Controller Callbacks ($FF10..$FF17) ──
byte IN_RAM gep9_read_disk_drive(uint) { return gep9_disk.drive; }
void IN_RAM gep9_write_disk_drive(uint, byte d) { gep9_disk.drive = d & 0x03; }

byte IN_RAM gep9_read_disk_sec0(uint) { return (gep9_disk.sector >> 16) & 0xFF; }
void IN_RAM gep9_write_disk_sec0(uint, byte d) {
  gep9_disk.sector = (gep9_disk.sector & 0x0000FFFF) | ((uint32_t)d << 16);
}
byte IN_RAM gep9_read_disk_sec1(uint) { return (gep9_disk.sector >> 8) & 0xFF; }
void IN_RAM gep9_write_disk_sec1(uint, byte d) {
  gep9_disk.sector = (gep9_disk.sector & 0x00FF00FF) | ((uint32_t)d << 8);
}
byte IN_RAM gep9_read_disk_sec2(uint) { return gep9_disk.sector & 0xFF; }
void IN_RAM gep9_write_disk_sec2(uint, byte d) {
  gep9_disk.sector = (gep9_disk.sector & 0x00FFFF00) | d;
}

byte IN_RAM gep9_read_disk_task(uint) { return gep9_disk.mem_task; }
void IN_RAM gep9_write_disk_task(uint, byte d) { gep9_disk.mem_task = d; }

byte IN_RAM gep9_read_disk_addr_hi(uint) { return (gep9_disk.mem_addr >> 8) & 0xFF; }
void IN_RAM gep9_write_disk_addr_hi(uint, byte d) {
  gep9_disk.mem_addr = (gep9_disk.mem_addr & 0x00FF) | ((uint16_t)d << 8);
}
byte IN_RAM gep9_read_disk_addr_lo(uint) { return gep9_disk.mem_addr & 0xFF; }
void IN_RAM gep9_write_disk_addr_lo(uint, byte d) {
  gep9_disk.mem_addr = (gep9_disk.mem_addr & 0xFF00) | d;
}

byte IN_RAM gep9_read_disk_cmdst(uint) {
  byte st = gep9_disk.status;
  gep9_disk.status = 0; // Reset on read
  return st;
}

void IN_RAM gep9_write_disk_cmdst(uint, byte d); // Defined in main where fg2bg is visible

// ── TaskFuse ($FF20) ──
byte IN_RAM gep9_read_task_fuse(uint) {
  return gep9_mmu.task_fuse_armed ? gep9_mmu.task_fuse_target : 0;
}
void IN_RAM gep9_write_task_fuse(uint, byte d) {
  gep9_mmu.task_fuse_armed = true;
  gep9_mmu.task_fuse_target = d;
  gep9_mmu.task_fuse_step = 0;
}

// ── Fast DMA Block Copy Engine ($FF21..$FF2A) ──
byte IN_RAM gep9_read_dma_src_task(uint) { return gep9_dma.src_task; }
void IN_RAM gep9_write_dma_src_task(uint, byte d) { gep9_dma.src_task = d; }

byte IN_RAM gep9_read_dma_src_page(uint) { return gep9_dma.src_page; }
void IN_RAM gep9_write_dma_src_page(uint, byte d) { gep9_dma.src_page = d; }

byte IN_RAM gep9_read_dma_src_hi(uint) { return (gep9_dma.src_addr >> 8) & 0xFF; }
void IN_RAM gep9_write_dma_src_hi(uint, byte d) {
  gep9_dma.src_addr = (gep9_dma.src_addr & 0x00FF) | ((uint16_t)d << 8);
}
byte IN_RAM gep9_read_dma_src_lo(uint) { return gep9_dma.src_addr & 0xFF; }
void IN_RAM gep9_write_dma_src_lo(uint, byte d) {
  gep9_dma.src_addr = (gep9_dma.src_addr & 0xFF00) | d;
}

byte IN_RAM gep9_read_dma_dst_task(uint) { return gep9_dma.dst_task; }
void IN_RAM gep9_write_dma_dst_task(uint, byte d) { gep9_dma.dst_task = d; }

byte IN_RAM gep9_read_dma_dst_page(uint) { return gep9_dma.dst_page; }
void IN_RAM gep9_write_dma_dst_page(uint, byte d) { gep9_dma.dst_page = d; }

byte IN_RAM gep9_read_dma_dst_hi(uint) { return (gep9_dma.dst_addr >> 8) & 0xFF; }
void IN_RAM gep9_write_dma_dst_hi(uint, byte d) {
  gep9_dma.dst_addr = (gep9_dma.dst_addr & 0x00FF) | ((uint16_t)d << 8);
}
byte IN_RAM gep9_read_dma_dst_lo(uint) { return gep9_dma.dst_addr & 0xFF; }
void IN_RAM gep9_write_dma_dst_lo(uint, byte d) {
  gep9_dma.dst_addr = (gep9_dma.dst_addr & 0xFF00) | d;
}

byte IN_RAM gep9_read_dma_count(uint) {
  byte st = gep9_dma.status;
  gep9_dma.status = 0; // Reset on read
  return st;
}
void IN_RAM gep9_write_dma_count(uint, byte d) {
  gep9_dma.count = d;
  uint16_t count = (d == 0) ? 256 : d;
  for (uint16_t i = 0; i < count; i++) {
    byte val = *gep9_mmu.TranslateReadPhys(gep9_dma.src_task, gep9_dma.src_page, gep9_dma.src_addr + i);
    *gep9_mmu.TranslateWritePhys(gep9_dma.dst_task, gep9_dma.dst_page, gep9_dma.dst_addr + i) = val;
  }
  gep9_dma.status = 1; // 1 = OKAY
}

byte IN_RAM gep9_read_dma_status(uint) {
  byte st = gep9_dma.status;
  gep9_dma.status = 0; // Reset on read
  return st;
}
void IN_RAM gep9_write_dma_status(uint, byte d) {
  gep9_dma.status = d;
}

// ── Shared Memory Curtain ($FF2B..$FF2C) ──
byte IN_RAM gep9_read_curtain_hi(uint) { return (gep9_mmu.shared_curtain >> 8) & 0xFF; }
void IN_RAM gep9_write_curtain_hi(uint, byte d) {
  gep9_mmu.shared_curtain = (gep9_mmu.shared_curtain & 0x00FF) | ((uint16_t)d << 8);
}
byte IN_RAM gep9_read_curtain_lo(uint) { return gep9_mmu.shared_curtain & 0xFF; }
void IN_RAM gep9_write_curtain_lo(uint, byte d) {
  gep9_mmu.shared_curtain = (gep9_mmu.shared_curtain & 0xFF00) | d;
}

// ── Task Capabilities & Security Flags ($FF2D..$FF2E) ──
byte IN_RAM gep9_read_flags_target(uint) { return gep9_mmu.task_flags_target; }
void IN_RAM gep9_write_flags_target(uint, byte d) { gep9_mmu.task_flags_target = d; }

byte IN_RAM gep9_read_flags_reg(uint) { return gep9_mmu.task_flags[gep9_mmu.task_flags_target]; }
void IN_RAM gep9_write_flags_reg(uint, byte d) {
  gep9_mmu.task_flags[gep9_mmu.task_flags_target] = d;
}

// ── Task Memory Purge ($FF2F) ──
byte IN_RAM gep9_read_purge(uint) { return 0; }
void IN_RAM gep9_write_purge(uint, byte d) {
  if (d != 0) {
    gep9_mmu.PurgeTask(d);
  }
}

// ── EMBIGGEN MMAP Vector ($FF40..$FF47 by default; see gep9_embiggen_base) ──
extern volatile uint16_t gep9_embiggen_base;
byte IN_RAM gep9_read_mmap(uint addr) {
  uint8_t slot = (addr - gep9_embiggen_base) & 0x07;
  return gep9_mmu.task_mmap[gep9_mmu.current_task][slot];
}
void IN_RAM gep9_write_mmap(uint addr, byte d) {
  uint8_t slot = (addr - gep9_embiggen_base) & 0x07;
  gep9_mmu.task_mmap[gep9_mmu.current_task][slot] = d;
}

// ── System Vectors ($FFF0..$FFFF) ──
byte IN_RAM gep9_read_vector(uint addr) {
  return *gep9_mmu.TranslateRead(0, addr);
}
void IN_RAM gep9_write_vector(uint addr, byte d) {
  *gep9_mmu.TranslateWrite(0, addr) = d;
}

// ── Devices and the register table installer ──
// Each device claims its range in the IoMap and stores NAMED IN_RAM handlers (see lib/devices/io_install.h).

struct GepConsole {     // console and diagnostics, $FF00..$FF05
  static void Install(uint16_t base = 0xFF00) {
    IoClaim(base, base + 5, "console");
    IoSet(base + 0, gep9_read_term_out, gep9_write_term_out);
    IoSet(base + 1, gep9_read_term_in,  gep9_write_term_in);
    IoSet(base + 2, gep9_read_reg_stat, gep9_write_reg_stat);
    IoSet(base + 3, gep9_read_reg_ctrl, gep9_write_reg_ctrl);
    IoSet(base + 4, gep9_read_logchar,  gep9_write_logchar);
    IoSet(base + 5, gep9_read_exit,     gep9_write_exit);
  }
};

struct GepDisk {        // disk controller, $FF10..$FF17
  static void Install(uint16_t base = 0xFF10) {
    IoClaim(base, base + 7, "disk");
    IoSet(base + 0, gep9_read_disk_drive,   gep9_write_disk_drive);
    IoSet(base + 1, gep9_read_disk_sec0,    gep9_write_disk_sec0);
    IoSet(base + 2, gep9_read_disk_sec1,    gep9_write_disk_sec1);
    IoSet(base + 3, gep9_read_disk_sec2,    gep9_write_disk_sec2);
    IoSet(base + 4, gep9_read_disk_task,    gep9_write_disk_task);
    IoSet(base + 5, gep9_read_disk_addr_hi, gep9_write_disk_addr_hi);
    IoSet(base + 6, gep9_read_disk_addr_lo, gep9_write_disk_addr_lo);
    IoSet(base + 7, gep9_read_disk_cmdst,   gep9_write_disk_cmdst);
  }
};

struct GepFuseDma {     // TaskFuse, fast DMA, shared-memory curtain, task flags, purge: $FF20..$FF2F
  static void Install(uint16_t base = 0xFF20) {
    IoClaim(base, base + 15, "fuse_dma");
    IoSet(base + 0x0, gep9_read_task_fuse,    gep9_write_task_fuse);
    IoSet(base + 0x1, gep9_read_dma_src_task, gep9_write_dma_src_task);
    IoSet(base + 0x2, gep9_read_dma_src_page, gep9_write_dma_src_page);
    IoSet(base + 0x3, gep9_read_dma_src_hi,   gep9_write_dma_src_hi);
    IoSet(base + 0x4, gep9_read_dma_src_lo,   gep9_write_dma_src_lo);
    IoSet(base + 0x5, gep9_read_dma_dst_task, gep9_write_dma_dst_task);
    IoSet(base + 0x6, gep9_read_dma_dst_page, gep9_write_dma_dst_page);
    IoSet(base + 0x7, gep9_read_dma_dst_hi,   gep9_write_dma_dst_hi);
    IoSet(base + 0x8, gep9_read_dma_dst_lo,   gep9_write_dma_dst_lo);
    IoSet(base + 0x9, gep9_read_dma_count,    gep9_write_dma_count);
    IoSet(base + 0xA, gep9_read_dma_status,   gep9_write_dma_status);
    IoSet(base + 0xB, gep9_read_curtain_hi,   gep9_write_curtain_hi);
    IoSet(base + 0xC, gep9_read_curtain_lo,   gep9_write_curtain_lo);
    IoSet(base + 0xD, gep9_read_flags_target, gep9_write_flags_target);
    IoSet(base + 0xE, gep9_read_flags_reg,    gep9_write_flags_reg);
    IoSet(base + 0xF, gep9_read_purge,        gep9_write_purge);
  }
};

// Where the EMBIGGEN MMAP vector is: the bus loop (guts.h) lets every task reach it without a trap.
volatile uint16_t gep9_embiggen_base = 0xFF40;

struct GepEmbiggen {    // EMBIGGEN MMAP vector, eight registers (default $FF40..$FF47)
  static void Install(uint16_t base = 0xFF40) {
    gep9_embiggen_base = base;
    IoClaim(base, base + 7, "embiggen_mmu");
    for (int i = 0; i < 8; i++) IoSet(base + i, gep9_read_mmap, gep9_write_mmap);
  }
};

struct GepVectors {     // system vectors $FFF0..$FFFF, through the paged memory
  static void Install(uint16_t base = 0xFFF0) {
    IoClaim(base, base + 15, "vectors");
    for (int i = 0; i < 16; i++) IoSet(base + i, gep9_read_vector, gep9_write_vector);
  }
};

// All gep9/hatvan targets install the EMBIGGEN range (0xff40..0xff47 on gep9/gep9big, 0xffe0..0xffe7 on coco2).
void Install_Gep9_IO() {
  IoReset();
  GepConsole::Install(PortBase("console", 0xFF00));
  GepDisk::Install(PortBase("disk", 0xFF10));
  GepFuseDma::Install(PortBase("fuse_dma", 0xFF20));
  GepEmbiggen::Install(PortBase("embiggen_mmu", 0xFF40));
  GepVectors::Install();
  IoFinish();
}

#endif  // V4_GEP9_IO_H_
