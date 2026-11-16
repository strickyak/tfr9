#ifndef V4_GEP9_IO_H_
#define V4_GEP9_IO_H_

#include <atomic>
#include "gep9_mmu.h"

// ═══════════════════════════════════════════════════════════════════
// gep9 Hardware I/O Registers — Hatvan Unified Hardware I/O Spec v2.0
//
// Master Register Map ($FF00..$FFFF on gep9 / gep9big):
//   $FF00: Term.Out (putchar)
//   $FF01: Term.In  (getchar, non-blocking, 0 = empty)
//   $FF02: Reg.Stat (Bit 0: Timer.Ready, Bit 1: Term.RxReady, W1C)
//   $FF03: Reg.Ctrl (Bit 0: TimrIRQ, Bit 1: TermIRQ)
//   $FF04: logchar  (diagnostic trace log)
//   $FF05: Exit.Code
//
//   $FF10:       Disk.Drive  (0..3 for /d0../d3)
//   $FF11:       Disk.CmdSt  (W: 1=Read, 2=Write; R: 0=Busy, 1=OK, >1=Err, reset on read)
//   $FF12..$FF13: Reserved   (4-byte alignment padding)
//   $FF14..$FF17: Disk.Sector (32-bit Big-Endian LSN)
//   $FF18..$FF1B: Disk.Target (32-bit Big-Endian TAddr32: [Task:8][Addr:24])
//
//   $FF20:       TaskFuse / Task.Active (W: arm target task; R: armed target or current_task)
//   $FF21:       DMA.CmdSt   (W: 1=Start transfer; R: 0=Busy, 1=OK, >1=Err, reset on read)
//   $FF22..$FF23: Reserved   (4-byte alignment padding)
//   $FF24..$FF27: DMA.Src    (32-bit Big-Endian TAddr32: [SrcTask:8][SrcAddr:24])
//   $FF28..$FF2B: DMA.Dst    (32-bit Big-Endian TAddr32: [DstTask:8][DstAddr:24])
//   $FF2C..$FF2F: DMA.Count  (32-bit Big-Endian transfer byte count)
//
//   $FF30..$FF33: SharedMemoryCurtain (32-bit Big-Endian, default $0000E000)
//   $FF34:       TaskFlagsTarget      (target task ID 0..255, default 1)
//   $FF35:       TaskFlagsRegister    (Bit 0 = TASK_FLAG_IO)
//   $FF36:       PurgeTaskMem         (W: non-zero task ID purges RAM & flags)
//
//   $FF40..$FF47: Embiggen MMAP       (8-byte vector mapping 8KB slots 0..7, user accessible)
//   $FFF0..$FFFF: 6309 System Vectors (pinned to Task 0 vector page)
// ═══════════════════════════════════════════════════════════════════

struct Gep9IOState {
  volatile byte reg_stat; // +$02: Bit 0 = Timer.Ready, Bit 1 = Term.RxReady
  volatile byte reg_ctrl; // +$03: Bit 0 = TimrIRQ, Bit 1 = TermIRQ
  volatile byte exit_code;
};

struct Gep9DMAState {
  uint8_t  status;      // +$01 ($FF21): 0=busy, 1=OK, >1=error
  uint8_t  src_task;    // +$04 ($FF24)
  uint32_t src_addr24;  // +$05..+$07 ($FF25..$FF27, 24-bit Big-Endian)
  uint8_t  dst_task;    // +$08 ($FF28)
  uint32_t dst_addr24;  // +$09..+$0B ($FF29..$FF2B, 24-bit Big-Endian)
  uint32_t count;       // +$0C..+$0F ($FF2C..$FF2F, 32-bit Big-Endian)
};

struct Gep9DiskState {
  uint8_t  drive;       // +$00 ($FF10, 0..3)
  uint8_t  status;      // +$01 ($FF11: 0=busy, 1=OK, >1=error)
  uint32_t sector;      // +$04..+$07 ($FF14..$FF17, 32-bit Big-Endian LSN)
  uint8_t  mem_task;    // +$08 ($FF18)
  uint32_t mem_addr24;  // +$09..+$0B ($FF19..$FF1B, 24-bit Big-Endian)
};

struct Gep9DiskRequest {
  uint8_t  drive;
  uint32_t sector;
  uint8_t  mem_task;
  uint32_t mem_addr24;
  uint8_t  command;           // 1=read sector, 2=write sector
  std::atomic<int8_t> status; // -1=pending, 0/1=OK, >1=error
};

inline Gep9IOState     gep9_io;
inline Gep9DMAState    gep9_dma;
inline Gep9DiskState   gep9_disk;
inline Gep9DiskRequest g_gep9_disk_req;

inline volatile byte     gep9_rx_char = 0;
inline std::atomic<bool> gep9_rx_has_char{false};

// Forward-declared utilities from main
void Say(byte c);
void cobs_printf(const char* fmt, ...);
void rp2350_reset_standard();

// ── IRQ Evaluation ──
FORCE_INLINE bool IN_RAM gep9_irq_needed() {
  return (gep9_io.reg_stat & gep9_io.reg_ctrl & 0x03) != 0;
}

FORCE_INLINE void IN_RAM gep9_update_irq_pin() {
#ifndef G_CART
  if (gep9_irq_needed()) {
    gpio_set_dir(IRQ, GPIO_OUT);
  } else {
    gpio_set_dir(IRQ, GPIO_IN);
  }
#endif
}

byte IN_RAM gep9_read_reserved(uint) { return 0; }
void IN_RAM gep9_write_reserved(uint, byte) {}

// ── Console & Diagnostic Callbacks ($FF00..$FF05) ──
byte IN_RAM gep9_read_term_out(uint) { return 0; }
void IN_RAM gep9_write_term_out(uint, byte d) { Say(d); }

byte IN_RAM gep9_read_term_in(uint) {
  if (gep9_rx_has_char.load(std::memory_order_acquire)) {
    byte c = gep9_rx_char;
    gep9_rx_has_char.store(false, std::memory_order_release);
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
  if (gep9_rx_has_char.load(std::memory_order_relaxed)) {
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

byte IN_RAM gep9_read_exit(uint) { return gep9_io.exit_code; }
void IN_RAM gep9_write_exit(uint, byte d) {
  gep9_io.exit_code = d;
  // Immediately assert RESET and HALT to stop 6809/6309 bus activity
  gpio_put(RESET, 0); gpio_set_dir(RESET, GPIO_OUT);
  gpio_put(HALT, 0);  gpio_set_dir(HALT, GPIO_OUT);

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

// ── Disk Controller Callbacks ($FF10..$FF1B, Hatvan Spec v2.0) ──
byte IN_RAM gep9_read_disk_drive(uint) { return gep9_disk.drive; }
void IN_RAM gep9_write_disk_drive(uint, byte d) { gep9_disk.drive = d & 0x03; }

byte IN_RAM gep9_read_disk_cmdst(uint) {
  byte st = gep9_disk.status;
  gep9_disk.status = 0; // Reset on read
  return st;
}
void IN_RAM gep9_write_disk_cmdst(uint, byte d); // Defined in gep9.h where fg2bg is visible

byte IN_RAM gep9_read_disk_sec0(uint) { return (gep9_disk.sector >> 24) & 0xFF; }
void IN_RAM gep9_write_disk_sec0(uint, byte d) {
  gep9_disk.sector = (gep9_disk.sector & 0x00FFFFFFu) | ((uint32_t)d << 24);
}
byte IN_RAM gep9_read_disk_sec1(uint) { return (gep9_disk.sector >> 16) & 0xFF; }
void IN_RAM gep9_write_disk_sec1(uint, byte d) {
  gep9_disk.sector = (gep9_disk.sector & 0xFF00FFFFu) | ((uint32_t)d << 16);
}
byte IN_RAM gep9_read_disk_sec2(uint) { return (gep9_disk.sector >> 8) & 0xFF; }
void IN_RAM gep9_write_disk_sec2(uint, byte d) {
  gep9_disk.sector = (gep9_disk.sector & 0xFFFF00FFu) | ((uint32_t)d << 8);
}
byte IN_RAM gep9_read_disk_sec3(uint) { return gep9_disk.sector & 0xFF; }
void IN_RAM gep9_write_disk_sec3(uint, byte d) {
  gep9_disk.sector = (gep9_disk.sector & 0xFFFFFF00u) | d;
}

byte IN_RAM gep9_read_disk_task(uint) { return gep9_disk.mem_task; }
void IN_RAM gep9_write_disk_task(uint, byte d) { gep9_disk.mem_task = d; }

byte IN_RAM gep9_read_disk_addr0(uint) { return (gep9_disk.mem_addr24 >> 16) & 0xFF; }
void IN_RAM gep9_write_disk_addr0(uint, byte d) {
  gep9_disk.mem_addr24 = (gep9_disk.mem_addr24 & 0x00FFFFu) | ((uint32_t)d << 16);
}
byte IN_RAM gep9_read_disk_addr1(uint) { return (gep9_disk.mem_addr24 >> 8) & 0xFF; }
void IN_RAM gep9_write_disk_addr1(uint, byte d) {
  gep9_disk.mem_addr24 = (gep9_disk.mem_addr24 & 0xFF00FFu) | ((uint32_t)d << 8);
}
byte IN_RAM gep9_read_disk_addr2(uint) { return gep9_disk.mem_addr24 & 0xFF; }
void IN_RAM gep9_write_disk_addr2(uint, byte d) {
  gep9_disk.mem_addr24 = (gep9_disk.mem_addr24 & 0xFFFF00u) | d;
}

// ── TaskFuse ($FF20) & Fast DMA Block Copy Engine ($FF21..$FF2F, Spec v2.0) ──
byte IN_RAM gep9_read_task_fuse(uint) {
  return gep9_mmu.task_fuse_armed ? gep9_mmu.task_fuse_target : gep9_mmu.current_task;
}
void IN_RAM gep9_write_task_fuse(uint, byte d) {
  gep9_mmu.task_fuse_armed = true;
  gep9_mmu.task_fuse_target = d;
  gep9_mmu.task_fuse_step = 0;
}

byte IN_RAM gep9_read_dma_cmdst(uint) {
  byte st = gep9_dma.status;
  gep9_dma.status = 0; // Reset on read
  return st;
}
void IN_RAM gep9_write_dma_cmdst(uint, byte d) {
  if (d != 1) return;
  uint32_t count = gep9_dma.count;
  uint8_t src_task = gep9_dma.src_task;
  uint32_t src_addr = gep9_dma.src_addr24;
  uint8_t dst_task = gep9_dma.dst_task;
  uint32_t dst_addr = gep9_dma.dst_addr24;
  for (uint32_t i = 0; i < count; i++) {
    byte val = *gep9_mmu.TranslateRead24(src_task, src_addr + i);
    *gep9_mmu.TranslateWrite24(dst_task, dst_addr + i) = val;
  }
  gep9_dma.status = 1; // 1 = OKAY
}

byte IN_RAM gep9_read_dma_src_task(uint) { return gep9_dma.src_task; }
void IN_RAM gep9_write_dma_src_task(uint, byte d) { gep9_dma.src_task = d; }

byte IN_RAM gep9_read_dma_src0(uint) { return (gep9_dma.src_addr24 >> 16) & 0xFF; }
void IN_RAM gep9_write_dma_src0(uint, byte d) {
  gep9_dma.src_addr24 = (gep9_dma.src_addr24 & 0x00FFFFu) | ((uint32_t)d << 16);
}
byte IN_RAM gep9_read_dma_src1(uint) { return (gep9_dma.src_addr24 >> 8) & 0xFF; }
void IN_RAM gep9_write_dma_src1(uint, byte d) {
  gep9_dma.src_addr24 = (gep9_dma.src_addr24 & 0xFF00FFu) | ((uint32_t)d << 8);
}
byte IN_RAM gep9_read_dma_src2(uint) { return gep9_dma.src_addr24 & 0xFF; }
void IN_RAM gep9_write_dma_src2(uint, byte d) {
  gep9_dma.src_addr24 = (gep9_dma.src_addr24 & 0xFFFF00u) | d;
}

byte IN_RAM gep9_read_dma_dst_task(uint) { return gep9_dma.dst_task; }
void IN_RAM gep9_write_dma_dst_task(uint, byte d) { gep9_dma.dst_task = d; }

byte IN_RAM gep9_read_dma_dst0(uint) { return (gep9_dma.dst_addr24 >> 16) & 0xFF; }
void IN_RAM gep9_write_dma_dst0(uint, byte d) {
  gep9_dma.dst_addr24 = (gep9_dma.dst_addr24 & 0x00FFFFu) | ((uint32_t)d << 16);
}
byte IN_RAM gep9_read_dma_dst1(uint) { return (gep9_dma.dst_addr24 >> 8) & 0xFF; }
void IN_RAM gep9_write_dma_dst1(uint, byte d) {
  gep9_dma.dst_addr24 = (gep9_dma.dst_addr24 & 0xFF00FFu) | ((uint32_t)d << 8);
}
byte IN_RAM gep9_read_dma_dst2(uint) { return gep9_dma.dst_addr24 & 0xFF; }
void IN_RAM gep9_write_dma_dst2(uint, byte d) {
  gep9_dma.dst_addr24 = (gep9_dma.dst_addr24 & 0xFFFF00u) | d;
}

byte IN_RAM gep9_read_dma_cnt0(uint) { return (gep9_dma.count >> 24) & 0xFF; }
void IN_RAM gep9_write_dma_cnt0(uint, byte d) {
  gep9_dma.count = (gep9_dma.count & 0x00FFFFFFu) | ((uint32_t)d << 24);
}
byte IN_RAM gep9_read_dma_cnt1(uint) { return (gep9_dma.count >> 16) & 0xFF; }
void IN_RAM gep9_write_dma_cnt1(uint, byte d) {
  gep9_dma.count = (gep9_dma.count & 0xFF00FFFFu) | ((uint32_t)d << 16);
}
byte IN_RAM gep9_read_dma_cnt2(uint) { return (gep9_dma.count >> 8) & 0xFF; }
void IN_RAM gep9_write_dma_cnt2(uint, byte d) {
  gep9_dma.count = (gep9_dma.count & 0xFFFF00FFu) | ((uint32_t)d << 8);
}
byte IN_RAM gep9_read_dma_cnt3(uint) { return gep9_dma.count & 0xFF; }
void IN_RAM gep9_write_dma_cnt3(uint, byte d) {
  gep9_dma.count = (gep9_dma.count & 0xFFFFFF00u) | d;
}

// ── Shared Memory Curtain & Task Security ($FF30..$FF36, Spec v2.0) ──
byte IN_RAM gep9_read_curtain0(uint) { return (gep9_mmu.shared_curtain >> 24) & 0xFF; }
void IN_RAM gep9_write_curtain0(uint, byte d) {
  gep9_mmu.shared_curtain = (gep9_mmu.shared_curtain & 0x00FFFFFFu) | ((uint32_t)d << 24);
}
byte IN_RAM gep9_read_curtain1(uint) { return (gep9_mmu.shared_curtain >> 16) & 0xFF; }
void IN_RAM gep9_write_curtain1(uint, byte d) {
  gep9_mmu.shared_curtain = (gep9_mmu.shared_curtain & 0xFF00FFFFu) | ((uint32_t)d << 16);
}
byte IN_RAM gep9_read_curtain2(uint) { return (gep9_mmu.shared_curtain >> 8) & 0xFF; }
void IN_RAM gep9_write_curtain2(uint, byte d) {
  gep9_mmu.shared_curtain = (gep9_mmu.shared_curtain & 0xFFFF00FFu) | ((uint32_t)d << 8);
}
byte IN_RAM gep9_read_curtain3(uint) { return gep9_mmu.shared_curtain & 0xFF; }
void IN_RAM gep9_write_curtain3(uint, byte d) {
  gep9_mmu.shared_curtain = (gep9_mmu.shared_curtain & 0xFFFFFF00u) | d;
}

byte IN_RAM gep9_read_flags_target(uint) { return gep9_mmu.task_flags_target; }
void IN_RAM gep9_write_flags_target(uint, byte d) { gep9_mmu.task_flags_target = d; }

byte IN_RAM gep9_read_flags_reg(uint) { return gep9_mmu.task_flags[gep9_mmu.task_flags_target]; }
void IN_RAM gep9_write_flags_reg(uint, byte d) {
  gep9_mmu.task_flags[gep9_mmu.task_flags_target] = d;
}

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
  return *gep9_mmu.TranslateVectorRead((uint16_t)addr);
}
void IN_RAM gep9_write_vector(uint addr, byte d) {
  *gep9_mmu.TranslateVectorWrite((uint16_t)addr) = d;
}

// ── Devices and the register table installer ──
// Each device claims its range in the IoMap and stores NAMED IN_RAM handlers (see lib/devices/io_install.h).

inline bool gep9_reported_shell_stats = false;

inline void gep9_poll_rx() {
  if (!gep9_reported_shell_stats && gep9_mmu.task_tables[3] != nullptr && gep9_mmu.CountTaskFrames(3) > 0) {
    gep9_reported_shell_stats = true;
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
  if (!gep9_rx_has_char.load(std::memory_order_acquire) && term_input.NumBuffered() > 0) {
    gep9_rx_char = term_input.Take();
    gep9_rx_has_char.store(true, std::memory_order_release);
    gep9_io.reg_stat |= 0x02; // Set Term.RxReady
    gep9_update_irq_pin();
  }
}

void IN_RAM gep9_tick_60hz() {
  static uint8_t gep9_div = 0;
  if (++gep9_div >= 12) { // 60 Hz / 12 = 5 Hz (matches emulator default tick-hz)
    gep9_div = 0;
    gep9_io.reg_stat |= 0x01; // Set Timer.Ready flag
    gep9_update_irq_pin();
  }
}

struct GepConsole {     // console and diagnostics, $FF00..$FF05
  static void Install(uint16_t base = 0xFF00) {
    gep9_io.reg_stat = 0;
    gep9_io.reg_ctrl = 0;
    gep9_io.exit_code = 0;
    gep9_rx_has_char.store(false, std::memory_order_relaxed);
    gep9_reported_shell_stats = false;
    IoClaim(base, base + 5, "console");
    IoSet(base + 0, gep9_read_term_out, gep9_write_term_out);
    IoSet(base + 1, gep9_read_term_in,  gep9_write_term_in);
    IoSet(base + 2, gep9_read_reg_stat, gep9_write_reg_stat);
    IoSet(base + 3, gep9_read_reg_ctrl, gep9_write_reg_ctrl);
    IoSet(base + 4, gep9_read_logchar,  gep9_write_logchar);
    IoSet(base + 5, gep9_read_exit,     gep9_write_exit);
    g_rx_poll_fn = gep9_poll_rx;
    g_timer_60hz_fn = gep9_tick_60hz;
  }
};

struct GepDisk {        // disk controller, $FF10..$FF1B (Spec v2.0, 12 bytes)
  static void Install(uint16_t base = 0xFF10) {
    gep9_disk = {};
    g_gep9_disk_req.status.store(0, std::memory_order_relaxed);
    IoClaim(base, base + 0xB, "disk");
    IoSet(base + 0x0, gep9_read_disk_drive, gep9_write_disk_drive);
    IoSet(base + 0x1, gep9_read_disk_cmdst, gep9_write_disk_cmdst);
    IoSet(base + 0x2, gep9_read_reserved,   gep9_write_reserved);
    IoSet(base + 0x3, gep9_read_reserved,   gep9_write_reserved);
    IoSet(base + 0x4, gep9_read_disk_sec0,  gep9_write_disk_sec0);
    IoSet(base + 0x5, gep9_read_disk_sec1,  gep9_write_disk_sec1);
    IoSet(base + 0x6, gep9_read_disk_sec2,  gep9_write_disk_sec2);
    IoSet(base + 0x7, gep9_read_disk_sec3,  gep9_write_disk_sec3);
    IoSet(base + 0x8, gep9_read_disk_task,  gep9_write_disk_task);
    IoSet(base + 0x9, gep9_read_disk_addr0, gep9_write_disk_addr0);
    IoSet(base + 0xA, gep9_read_disk_addr1, gep9_write_disk_addr1);
    IoSet(base + 0xB, gep9_read_disk_addr2, gep9_write_disk_addr2);
  }
};

struct GepFuseDma {     // TaskFuse & fast DMA, $FF20..$FF2F (Spec v2.0, 16 bytes)
  static void Install(uint16_t base = 0xFF20) {
    gep9_dma = {};
    IoClaim(base, base + 0xF, "fuse_dma");
    IoSet(base + 0x0, gep9_read_task_fuse,    gep9_write_task_fuse);
    IoSet(base + 0x1, gep9_read_dma_cmdst,    gep9_write_dma_cmdst);
    IoSet(base + 0x2, gep9_read_reserved,     gep9_write_reserved);
    IoSet(base + 0x3, gep9_read_reserved,     gep9_write_reserved);
    IoSet(base + 0x4, gep9_read_dma_src_task, gep9_write_dma_src_task);
    IoSet(base + 0x5, gep9_read_dma_src0,     gep9_write_dma_src0);
    IoSet(base + 0x6, gep9_read_dma_src1,     gep9_write_dma_src1);
    IoSet(base + 0x7, gep9_read_dma_src2,     gep9_write_dma_src2);
    IoSet(base + 0x8, gep9_read_dma_dst_task, gep9_write_dma_dst_task);
    IoSet(base + 0x9, gep9_read_dma_dst0,     gep9_write_dma_dst0);
    IoSet(base + 0xA, gep9_read_dma_dst1,     gep9_write_dma_dst1);
    IoSet(base + 0xB, gep9_read_dma_dst2,     gep9_write_dma_dst2);
    IoSet(base + 0xC, gep9_read_dma_cnt0,     gep9_write_dma_cnt0);
    IoSet(base + 0xD, gep9_read_dma_cnt1,     gep9_write_dma_cnt1);
    IoSet(base + 0xE, gep9_read_dma_cnt2,     gep9_write_dma_cnt2);
    IoSet(base + 0xF, gep9_read_dma_cnt3,     gep9_write_dma_cnt3);
  }
};

struct GepTaskSec {     // SharedMemoryCurtain, TaskFlags, PurgeTaskMem: $FF30..$FF36 (Spec v2.0, 7 bytes)
  static void Install(uint16_t base = 0xFF30) {
    IoClaim(base, base + 6, "task_sec");
    IoSet(base + 0, gep9_read_curtain0,     gep9_write_curtain0);
    IoSet(base + 1, gep9_read_curtain1,     gep9_write_curtain1);
    IoSet(base + 2, gep9_read_curtain2,     gep9_write_curtain2);
    IoSet(base + 3, gep9_read_curtain3,     gep9_write_curtain3);
    IoSet(base + 4, gep9_read_flags_target, gep9_write_flags_target);
    IoSet(base + 5, gep9_read_flags_reg,    gep9_write_flags_reg);
    IoSet(base + 6, gep9_read_purge,        gep9_write_purge);
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

struct GepVectors {     // system vectors $FFF0..$FFFF, through Task 0 vector page
  static void Install(uint16_t base = 0xFFF0) {
    IoClaim(base, base + 15, "vectors");
    for (int i = 0; i < 16; i++) IoSet(base + i, gep9_read_vector, gep9_write_vector);
  }
};

// All gep9/hatvan targets install the Spec v2.0 groups + EMBIGGEN range.
void Install_Gep9_IO() {
  IoReset();
  GepConsole::Install(PortBase("console", 0xFF00));
  GepDisk::Install(PortBase("disk", 0xFF10));
  GepFuseDma::Install(PortBase("fuse_dma", 0xFF20));
  GepTaskSec::Install(PortBase("task_sec", 0xFF30));
  GepEmbiggen::Install(PortBase("embiggen_mmu", 0xFF40));
  GepVectors::Install();
  IoFinish();
}

#endif  // V4_GEP9_IO_H_
