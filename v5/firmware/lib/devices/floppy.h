#ifndef CENTIPEDE_FIRMWARE_FLOPPY_H_
#define CENTIPEDE_FIRMWARE_FLOPPY_H_

#include <string.h>

#include "cobs_tx.h"
#include "floppy_geom.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#ifndef FORCE_INLINE
#define FORCE_INLINE inline __attribute__((always_inline))
#endif
#ifndef IN_RAM
#define IN_RAM __not_in_flash("centipede")
#endif

// ============================================================================
// Floppy Disk Controller Emulation
//
// CONCURRENCY MODEL:
//   Foreground (Core 1) runs the bus cycle loop in lockstep with the Gerbil
//   PIO state machine. It MUST NOT block, busy-wait, or execute from Flash!
//   All foreground methods must be in SRAM (FORCE_INLINE / IN_RAM). It handles:
//     - WriteCommand, WriteTrack, WriteSector, WriteLatch (via WriteScsFloppy)
//     - ReadStatus, ReadData (via ReadScsFloppy)
//     - WriteData: collects 256 bytes, snapshots, asserts NMI
//
//   Background (Core 0) processes coroutines and the fg2bg software FIFO.
//   It may execute from Flash. It handles:
//     - Read sector: sends request to tether, receives data, sets DRQ
//     - Write sector: transmits snapshot buffer to tether
//     - NMI release: via high-priority volatile flag (nmi_pending)
//
// KEY TIMING CONSTRAINTS:
//   1. Disk BASIC writes $FF40 (latch) AFTER $FF48 (command). So floppy_latch
//      is stale at command time. Snapshot latch in WriteData at byte 256.
//   2. The fg2bg FIFO may have thousands of trace entries. Background
//      handlers run long after the foreground event. Never touch foreground-
//      owned state (like floppy_ptr) from a background handler during writes.
//   3. NMI is edge-triggered on the 6809. Pin must return high before the
//      next NMI. Use nmi_pending flag, not the FIFO, for NMI release.
//   4. ReadStatus clears DRQ — DSKCON relies on this behavior.
// ============================================================================

volatile byte floppy_latch;     // FG-written, BG-read.  Drive select, density, HALT enable
byte floppy_command;            // Currently unused

// Write-sector snapshot: captured on foreground at sector end, consumed by background.
byte floppy_write_chore;   // Snapshot of command byte (set by BG at command time)
byte floppy_write_latch;   // Snapshot of latch  (set by FG at sector end)
byte floppy_write_track;   // Snapshot of track  (set by FG at sector end)
byte floppy_write_sector;  // Snapshot of sector (set by FG at sector end)

// Double-buffered write buffers so foreground can snapshot instantly without memcpy.
byte floppy_write_bufs[2][256];
volatile uint8_t floppy_write_active_idx = 0;

// Atomic FDC status register. Written by both cores:
//   FG: WriteCommand sets initial value; ReadData/WriteData clear BUSY+DRQ at sector end.
//   BG: Read sector sets DRQ (or RNF/Lost Data) after VFS transfer finishes.
// FG reads with acquire ordering to ensure floppy_buf visibility.
std::atomic<byte> floppy_status{0};

volatile byte floppy_track;     // FG-written (WriteTrack), BG-read at command time
volatile byte floppy_sector;    // FG-written (WriteSector), BG-read at command time
volatile byte floppy_data;      // FG-written (WriteData / data reg for seek)
volatile byte *floppy_ptr;      // FG-owned during transfers, BG sets after read load

byte floppy_buf[256];  // Shared sector buffer; BG writes (read), FG writes (write)
volatile byte *floppy_read_end = floppy_buf + 256;
volatile uint16_t floppy_cur_sector_bytes = 256;
volatile bool floppy_in_read = false;
volatile bool floppy_in_write = false;
std::atomic<bool> floppy_latch_written{false};

#if FLOPPY_OVER_VFS
#include "vfs.h"
inline vfs_file_t floppy_vfs_files[4];
inline bool floppy_vfs_opened[4] = {false, false, false, false};
inline FloppyGeometry floppy_geom[4] = {
    {1, 35, 18, 18, 256},
    {1, 35, 18, 18, 256},
    {1, 35, 18, 18, 256},
    {1, 35, 18, 18, 256},
};
char floppy_vfs_paths[4][136];

void set_floppy_names() {
  for (uint i = 0; i < 4; i++) {
    if (floppy_vfs_opened[i]) {
      vfs_file_close(&floppy_vfs_files[i]);
      floppy_vfs_opened[i] = false;
    }
    floppy_geom[i] = FloppyGeometry{1, 35, 18, 18, 256};
    snprintf(floppy_vfs_paths[i], sizeof(floppy_vfs_paths[i]), "/%s/f%u",
             (g_config.floppy_fd ? "fd" : "pc"), i);
  }
  floppy_read_end = floppy_buf + 256;
  floppy_cur_sector_bytes = 256;
  floppy_in_read = false;
  floppy_in_write = false;
  floppy_latch_written.store(false, std::memory_order_relaxed);
}

// Open drive hnum if not yet opened, and infer single/double density,
// single/double sided, and 35/40/80 tracks from image byte size + LSN 0.
inline bool FloppyEnsureOpenedAndDetectGeometry(Coro& self, int hnum, const char* tag) {
  if (hnum < 0 || hnum >= 4) return false;
  if (floppy_vfs_opened[hnum]) return true;

  int res = vfs_file_open(&floppy_vfs_files[hnum], floppy_vfs_paths[hnum], LFS_O_RDWR | LFS_O_CREAT);
  if (res < 0) {
    cobs_printf("[%s h%d=%d=%s]", tag, hnum, res, floppy_vfs_paths[hnum]);
    return false;
  }
  floppy_vfs_opened[hnum] = true;

  lfs_soff_t file_size = vfs_file_seek(&floppy_vfs_files[hnum], 0, LFS_SEEK_END, &self);
  uint8_t lsn0[19] = {};
  bool has_lsn0 = false;
  if (file_size >= 256 &&
      vfs_file_seek(&floppy_vfs_files[hnum], 0, LFS_SEEK_SET, &self) >= 0 &&
      vfs_file_read(&floppy_vfs_files[hnum], lsn0, sizeof(lsn0)) == (lfs_ssize_t)sizeof(lsn0)) {
    has_lsn0 = true;
  }
  FloppyGeometry g = FloppyDetectGeometry((int64_t)file_size, lsn0, has_lsn0);
  floppy_geom[hnum] = g;
  cobs_printf("[%s h%d=%d=%s sz=%ld %uT/%uS/%uspt/%uB]", tag, hnum, res, floppy_vfs_paths[hnum],
              (long)file_size, g.tracks, g.sides, g.spt, g.sector_bytes);
  return true;
}
#endif

template <typename T>
struct DontFloppy {
  static void BackgroundFifoFloppyLatch(byte chore_byte) {
    cobs_printf("# Floppy Latch not installed\n");
  }
  static void BackgroundFifoFloppyCommand(Coro &self, uint chore,
                                          byte chore_byte) {
    cobs_printf("# Floppy Command not installed\n");
  }
  static void BackgroundFifoFloppyW256(Coro& self, byte chore_byte) {
    cobs_printf("# Floppy W256 not installed\n");
  }
  FORCE_INLINE static bool IN_RAM WantsHalt() { return false; }
  FORCE_INLINE static void IN_RAM ReadScsFloppy(const uint &abus, byte &dbus) {
    dbus = 0xFF;
  }
  FORCE_INLINE static void IN_RAM WriteScsFloppy(const uint &abus, byte &dbus) {
  }
};

template <typename T>
struct DoFloppy {
  static void SendSectorData() {
    CobsEncodeAndTransmit(floppy_buf, 256, putchar_raw);
  }

  static void ReceiveSectorData(Coro &self) {
    std::string *pkt = nullptr;
    while (!pkt) {
      // Don't call PumpUsbCobs here — the scheduler pumps on its main stack.
      pkt = usb_packet_buf.Yoink([](std::string *s) {
        return s && s->length() > 0 && (byte)(*s)[0] == 173;  // T_DISK_READ
      });
      if (!pkt) coro_yield(&self);  // Let drain task run while we wait
    }

    // Cleverly avoiding the initial metadata
    // (which really should be checked, but our
    // read requests and consumption are currently
    // synchronous)
    // and using just the last 256 bytes.
    if (pkt->length() >= 256) {
      for (int i = 0; i < 256; i++) {
        floppy_buf[i] = (*pkt)[pkt->length() - 256 + i];
      }
    } else {
      T::Fatal("pkt too short", pkt->length());
    }
    delete pkt;
  }

  static void BackgroundFifoFloppyLatch(byte chore_byte) {
    static byte last_latch;
    if (chore_byte != last_latch) {
      cobs_printf(" _%02x ", chore_byte);
      last_latch = chore_byte;
    }
  }

  static void BackgroundFifoFloppyCommand(Coro &self, uint chore,
                                          byte chore_byte) {
    (void)chore;
    cobs_printf(" f!%02x ", chore_byte);
    if ((chore_byte & 0xF0) == 0x80) {  // read sector (0x80..0x8F)
      // Wait up to 100 us for 6809 code (DECB DSKCON, NitrOS-9 boot_1773 / rb1773)
      // to write $FF40 (drive select + side select 0x40) after writing $FF48.
      // All three write $FF40 within ~15..22 us of $FF48 with interrupts masked;
      // callers that pre-set $FF40 before $FF48 fall through with floppy_latch intact.
      uint64_t t_cmd = time_us_64();
      while (!floppy_latch_written.load(std::memory_order_acquire) &&
             (time_us_64() - t_cmd) < 100) {
        coro_yield(&self);
      }
      byte latch_snap = floppy_latch;
      byte track_snap = floppy_track;
      byte sector_snap = floppy_sector;
      cobs_printf(" %dr%d/%x", track_snap, sector_snap, chore_byte);
#if FLOPPY_OVER_VFS
      {
        // Temporarily redirect VFS RPCs to yield via our coroutine,
        // not the spoon_task's coroutine (which is what g_vfs_coro
        // normally points to).  The vfs_rpc_busy lock prevents
        // concurrent RPCs, so this swap is safe.
        Coro* saved_vfs_coro = rpc::g_vfs_coro;
        rpc::g_vfs_coro = &self;

        int hnum = FloppyDriveFromLatch(latch_snap);
        cobs_printf(":%d", hnum);

        bool read_ok = false;
        byte err_status = 0x10;  // Record Not Found by default
        uint32_t lsn = 0;
        if (FloppyEnsureOpenedAndDetectGeometry(self, hnum, "open")) {
          const FloppyGeometry& g = floppy_geom[hnum];
          floppy_cur_sector_bytes = g.sector_bytes;
          floppy_read_end = floppy_buf + g.sector_bytes;
          if (FloppyComputeLSN(g, hnum, latch_snap, track_snap, sector_snap, lsn)) {
            lfs_soff_t seeked = vfs_file_seek(
                &floppy_vfs_files[hnum], (lfs_soff_t)lsn * g.sector_bytes, LFS_SEEK_SET, &self);
            lfs_ssize_t bytes_read = vfs_file_read(
                &floppy_vfs_files[hnum], floppy_buf, g.sector_bytes);
            (void)seeked;
            if (bytes_read == (lfs_ssize_t)g.sector_bytes) {
              read_ok = true;
            } else {
              err_status = 0x10;
            }
          }
        } else {
          err_status = 0x04;
        }
        if (!read_ok) {
          cobs_printf("[R FAIL h%d]", hnum);
          memset(floppy_buf, 0xFF, 256);
          floppy_ptr = floppy_buf;
          floppy_status.store(err_status, std::memory_order_release);
        } else {
          floppy_ptr = floppy_buf;
          floppy_status.store(0x02, std::memory_order_release);
        }

        rpc::g_vfs_coro = saved_vfs_coro;
      }
#else
      if (!usb_tether_ok()) {
        memset(floppy_buf, 0xFF, 256);
        floppy_ptr = floppy_buf;
        floppy_status.store(0x04, std::memory_order_release);  // Lost Data
        return;
      }
      {
        unsigned char pkt[6] = {C_DISK_READ, 'f',        chore_byte,
                                latch_snap,  track_snap, sector_snap};
        CobsEncodeAndTransmit(pkt, 6, putchar_raw);
      }

      ReceiveSectorData(self);
      floppy_ptr = floppy_buf;
      floppy_status.store(0x02, std::memory_order_release);
#endif
      cobs_printf(" ");
    } else if ((chore_byte & 0xF0) == 0xA0) {  // write sector (0xA0..0xAF)
      cobs_printf(" %dw%d/%x", floppy_track, floppy_sector, chore_byte);
      floppy_write_chore = chore_byte;
    }
  }
  static void BackgroundFifoFloppyW256(Coro& self, byte chore_byte) {
    byte* write_buf = floppy_write_bufs[chore_byte & 1];
#if FLOPPY_OVER_VFS
    // Temporarily redirect VFS RPCs to yield via our coroutine.
    Coro* saved_vfs_coro = rpc::g_vfs_coro;
    rpc::g_vfs_coro = &self;

    int hnum = FloppyDriveFromLatch(floppy_write_latch);
    cobs_printf(":%d", hnum);

    bool write_ok = false;
    uint32_t lsn = 0;
    if (FloppyEnsureOpenedAndDetectGeometry(self, hnum, "Wopen")) {
      const FloppyGeometry& g = floppy_geom[hnum];
      floppy_cur_sector_bytes = g.sector_bytes;
      floppy_read_end = floppy_buf + g.sector_bytes;
      if (FloppyComputeLSN(g, hnum, floppy_write_latch, floppy_write_track,
                           floppy_write_sector, lsn)) {
        lfs_soff_t seeked = vfs_file_seek(
            &floppy_vfs_files[hnum], (lfs_soff_t)lsn * g.sector_bytes, LFS_SEEK_SET, &self);
        lfs_ssize_t bytes_written = vfs_file_write(
            &floppy_vfs_files[hnum], write_buf, g.sector_bytes);
        cobs_printf("[W h%d lsn%u sk%d wr%d]", hnum, (unsigned)lsn, (int)seeked, (int)bytes_written);
        if (bytes_written == (lfs_ssize_t)g.sector_bytes) write_ok = true;
      }
    }
    if (!write_ok) {
      cobs_printf("[W FAIL h%d]", hnum);
      floppy_status.store(0x04, std::memory_order_release);  // Lost Data
      rpc::g_vfs_coro = saved_vfs_coro;
      return;
    }
    cobs_printf(" [sent] ");

    rpc::g_vfs_coro = saved_vfs_coro;
#else
    if (!usb_tether_ok()) {
      // No USB tether: cannot transmit sector data to PC.
      // Signal Lost Data error (bit 2) so DSKCON reports I/O error to BASIC.
      floppy_status.store(0x04, std::memory_order_release);  // Lost Data
      return;
    }
    // Send metadata + sector data as one combined packet.
    unsigned char pkt[256 + 6];
    pkt[0] = C_DISK_WRITE;
    pkt[1] = 'f';
    pkt[2] = floppy_write_chore;
    pkt[3] = floppy_write_latch;
    pkt[4] = floppy_write_track;
    pkt[5] = floppy_write_sector;
    memcpy(pkt + 6, write_buf, 256);
    CobsEncodeAndTransmit(pkt, 256 + 6, putchar_raw);

    cobs_printf(" [sent] ");
#endif
  }

  // Assert CoCo HALT# while HALT enable ($FF40 bit 7) is set and a Read Sector
  // command is still waiting on background VFS I/O (BUSY=1, DRQ=0 -> status 0x01).
  FORCE_INLINE static bool IN_RAM WantsHalt() {
    return floppy_in_read &&
           ((floppy_latch & 0x80u) != 0) &&
           (floppy_status.load(std::memory_order_acquire) == 0x01u);
  }

  FORCE_INLINE static void IN_RAM ReadScsFloppy(const uint &abus, byte &dbus) {
    // SAY('S');
    // CASE special read SCS
    switch (abus & 15) {
      case 0x8:  // ReadStatus ($FF48 read)
        // acquire: ensures floppy_buf writes (by BG) are visible when DRQ is set.
        dbus = floppy_status.load(std::memory_order_acquire);
        // Clear DRQ on read, keep BUSY. DSKCON relies on this behavior.
        // Without it, reads freeze (DRQ stays asserted between sectors).
        floppy_status.store(
            dbus & 1, std::memory_order_relaxed);
        break;
      case 0x9:  // ReadTrack ( read)
        dbus = floppy_track;
        break;
      case 0xA:  // ReadSector ( read)
        dbus = floppy_sector;
        break;
      case 0xB:  // ReadData ($FF4B read) — FOREGROUND
        if (floppy_in_read) {
          dbus = *floppy_ptr++;
          floppy_data = dbus;
          if (floppy_ptr >= floppy_read_end) {
            // All sector bytes delivered. Clear BUSY+DRQ (bits 0..1) while keeping
            // any error bits (0x10 RNF / 0x04 Lost Data) so the NMI handler sees them.
            floppy_in_read = false;
            byte st = floppy_status.load(std::memory_order_relaxed);
            floppy_status.store((byte)(st & ~0x03u), std::memory_order_relaxed);
            floppy_ptr = floppy_buf;
            if ((floppy_latch & 0x80) != 0) {
              // On real Tandy FD-502 hardware, INTRQ/NMI resets bit 7 (HALTENA)
              // of the $FF40 latch flip-flop so the next $FF48 command does not
              // immediately re-halt the 6809 before it updates $FF40 (SIDESEL).
              floppy_latch &= ~0x80u;
              ASSERT_NMI();
            }
          }
        } else {
          // Idle WD1773 Data Register readback (used by rb1773 hardware-presence probe).
          dbus = floppy_data;
        }
        break;
      default:
        dbus = ram[abus];
        break;
    }
  }

  FORCE_INLINE static void IN_RAM WriteScsFloppy(const uint &abus, byte &dbus) {
    // WRITE SCS
    switch (abus & 15) {
      case 0x0:  // WriteLatch
        floppy_latch = dbus;
        floppy_latch_written.store(true, std::memory_order_release);
        PUSH_TO_BG(FG2BG_FLOPPY_LATCH, 0, dbus);
        break;
      case 0x8:  // WriteCommand
        floppy_latch_written.store(false, std::memory_order_relaxed);
        if ((dbus & 0xF0) == 0xA0) {
          // WRITE: BUSY + DRQ (0x03) immediately — CoCo must start feeding data.
          floppy_in_read = false;
          floppy_in_write = true;
          floppy_status.store(0x03, std::memory_order_relaxed);
          floppy_ptr = floppy_write_bufs[floppy_write_active_idx];
        } else if ((dbus & 0xF0) == 0x80) {
          // READ: BUSY only (0x01). Background sets DRQ after loading data.
          floppy_in_write = false;
          floppy_in_read = true;
          floppy_status.store(0x01, std::memory_order_relaxed);
          floppy_ptr = floppy_buf;
        } else {
          floppy_in_read = false;
          floppy_in_write = false;
          floppy_latch &= ~0x80u;  // Type I / IV completion clears HALTENA
          floppy_status.store(0x00, std::memory_order_relaxed);
          floppy_ptr = floppy_buf;
        }

        // Type I commands: Restore, Seek, Step, Step-In, Step-Out
        if ((dbus & 0xF0) == 0x00) {
          floppy_track = 0;  // RESTORE to track 0
        } else if ((dbus & 0xF0) == 0x10) {
          floppy_track = floppy_data;  // SEEK to track in Data Register
        } else if ((dbus & 0xE0) == 0x40) {
          if (dbus & 0x10) floppy_track++;
        } else if ((dbus & 0xE0) == 0x60) {
          if ((dbus & 0x10) && floppy_track > 0) floppy_track--;
        }

        PUSH_TO_BG(FG2BG_FLOPPY_COMMAND, 0, dbus);
        break;
      case 0x9:  // WriteTrack
        floppy_track = dbus;
        break;
      case 0xA:  // WriteSector
        floppy_sector = dbus;
        break;
      case 0xB:  // WriteData ( write) — FOREGROUND
        floppy_data = dbus;
        if (floppy_in_write) {
          *floppy_ptr++ = dbus;
          if (floppy_ptr >= floppy_write_bufs[floppy_write_active_idx] + floppy_cur_sector_bytes) {
            // All sector bytes received. Clear status unconditionally.
            floppy_in_write = false;
            floppy_status.store(0x00, std::memory_order_relaxed);
            if ((floppy_latch & 0x80) != 0) {
              floppy_write_latch = floppy_latch;
              floppy_write_track = floppy_track;
              floppy_write_sector = floppy_sector;
              floppy_latch &= ~0x80u;
              uint8_t completed_idx = floppy_write_active_idx;
              floppy_write_active_idx ^= 1;
              floppy_ptr = floppy_write_bufs[floppy_write_active_idx];
              PUSH_TO_BG(FG2BG_W_256, 0, completed_idx);
              ASSERT_NMI();
            } else {
              floppy_ptr = floppy_write_bufs[floppy_write_active_idx];  // Reset to prevent overrun
            }
          }
        }
        break;
      default:
        break;
    }
  }
};

#endif  // CENTIPEDE_FIRMWARE_FLOPPY_H_
