#ifndef CENTIPEDE_FIRMWARE_FLOPPY_H_
#define CENTIPEDE_FIRMWARE_FLOPPY_H_

#include <string.h>

#include "cobs_tx.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

// ============================================================================
// Floppy Disk Controller Emulation
//
// CONCURRENCY MODEL:
//   Foreground (core0) runs the bus cycle loop in lockstep with the Gerbil
//   PIO state machine. It MUST NOT block or busy-wait. It handles:
//     - WriteCommand, WriteTrack, WriteSector, WriteLatch (via WriteScsFloppy)
//     - ReadStatus, ReadData (via ReadScsFloppy)
//     - WriteData: collects 256 bytes, snapshots, asserts NMI
//
//   Background (core1) processes the fg2bg software FIFO. It handles:
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

byte floppy_latch;     // FG-written, BG-read.  Drive select, density, HALT enable
byte floppy_command;   // Currently unused

// Write-sector snapshot: captured on foreground at byte 256, consumed by background.
byte floppy_write_chore;   // Snapshot of command byte (set by BG at command time)
byte floppy_write_latch;   // Snapshot of latch  (set by FG at byte 256)
byte floppy_write_track;   // Snapshot of track  (set by FG at byte 256)
byte floppy_write_sector;  // Snapshot of sector (set by FG at byte 256)
byte floppy_write_buf[256];  // Snapshot of sector data (memcpy'd by FG at byte 256)

// Atomic FDC status register. Written by both cores:
//   FG: WriteCommand sets initial value; ReadData/WriteData clear at byte 256.
//   BG: Read sector sets DRQ after data is loaded.
// FG reads with acquire ordering to ensure floppy_buf visibility.
std::atomic<byte> floppy_status{0};

byte floppy_track;     // FG-written (WriteTrack), BG-read at command time
byte floppy_sector;    // FG-written (WriteSector), BG-read at command time
volatile byte *floppy_ptr;  // FG-owned during transfers, BG sets after read load

byte floppy_buf[256];  // Shared sector buffer; BG writes (read), FG writes (write)
#define floppy_limit (256 + floppy_buf)

#if FLOPPY_OVER_VFS
#include "vfs.h"
inline vfs_file_t floppy_vfs_files[4];
inline bool floppy_vfs_opened[4] = {false, false, false, false};
char floppy_vfs_paths[4][136];

void set_floppy_names() {
        for (uint i = 0; i < 4; i++) {
            sprintf(floppy_vfs_paths[i], "/%s/floppy%d.dsk",
                    (centipede_config.floppy_fd ? "fd" : "pc"), i);
        }
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
  static void BackgroundFifoFloppyW256(Coro& self) {
    cobs_printf("# Floppy W256 not installed\n");
  }
  static void ReadScsFloppy(const uint &abus, byte &dbus) {
    cobs_printf("# Floppy (ReadScs) not installed\n");
    dbus = 0xFF;
  }
  static void WriteScsFloppy(const uint &abus, byte &dbus) {
    cobs_printf("# Floppy (WriteScs) not installed\n");
    dbus = 0xFF;
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
    cobs_printf(" f!%02x ", chore_byte);
    switch (chore_byte) {
      case 0x17:  // seek track
        floppy_track = floppy_buf[0];
        break;

      case 0x80:  // read sector
        cobs_printf(" %dr%d/%x", floppy_track, floppy_sector, chore_byte);
#if FLOPPY_OVER_VFS
        {
          // Temporarily redirect VFS RPCs to yield via our coroutine,
          // not the spoon_task's coroutine (which is what g_vfs_coro
          // normally points to).  The vfs_rpc_busy lock prevents
          // concurrent RPCs, so this swap is safe.
          Coro* saved_vfs_coro = rpc::g_vfs_coro;
          rpc::g_vfs_coro = &self;

          int hnum = -1;
          if (floppy_latch & 1) hnum = 0;
          else if (floppy_latch & 2) hnum = 1;
          else if (floppy_latch & 4) hnum = 2;
          else if (floppy_latch & 8) hnum = 3;

          cobs_printf(":%d", hnum);

          bool read_ok = false;
          if (hnum >= 0 && hnum < 4) {
            if (!floppy_vfs_opened[hnum]) {
              int res = vfs_file_open(&floppy_vfs_files[hnum], floppy_vfs_paths[hnum], LFS_O_RDWR | LFS_O_CREAT);
              cobs_printf("[open h%d=%d=%s]", hnum, res, floppy_vfs_paths[hnum]);
              if (res >= 0) floppy_vfs_opened[hnum] = true;
            }
            if (floppy_vfs_opened[hnum]) {
              uint dden_offset = (floppy_latch & 0x40) ? 18 : 0;
              uint lsn = dden_offset + 18 * floppy_track + floppy_sector - 1;
              lfs_soff_t seeked = vfs_file_seek(&floppy_vfs_files[hnum], lsn * 256, LFS_SEEK_SET, &self);
              lfs_ssize_t bytes_read = vfs_file_read(&floppy_vfs_files[hnum], floppy_buf, 256);
#if 0
              cobs_printf("[R h%d lsn%d sk%d rd%d %02x%02x%02x%02x stk=%d/%d]",
                hnum, lsn, (int)seeked, (int)bytes_read,
                floppy_buf[0], floppy_buf[1], floppy_buf[2], floppy_buf[3],
                coro_stack_used(&self), coro_stack_free(&self));
#endif
              if (bytes_read == 256) read_ok = true;
            }
          }
          if (!read_ok) {
            cobs_printf("[R FAIL h%d]", hnum);
            memset(floppy_buf, 0xFF, 256);
            floppy_ptr = floppy_buf;
            floppy_status.store(0x04, std::memory_order_release);
          } else {
            floppy_ptr = floppy_buf;
            floppy_status.store(0x02, std::memory_order_release);
          }

          rpc::g_vfs_coro = saved_vfs_coro;
        }
#else
        if (!usb_tether_ok()) {
          // No USB tether: cannot fetch sector data from PC.
          // Fill buffer with 0xFF and signal CRC/Lost Data error (bit 2)
          // so DSKCON reports an I/O error to BASIC.
          memset(floppy_buf, 0xFF, 256);
          floppy_ptr = floppy_buf;
          floppy_status.store(0x04, std::memory_order_release);  // Lost Data
          break;
        }
        {
          unsigned char pkt[6] = {C_DISK_READ,  'f',          chore_byte,
                                  floppy_latch, floppy_track, floppy_sector};
          CobsEncodeAndTransmit(pkt, 6, putchar_raw);
        }

        ReceiveSectorData(self);
        floppy_ptr = floppy_buf;
        // release: ensures all floppy_buf[] writes are visible to
        // foreground before it sees DRQ via acquire load.
        floppy_status.store(0x02, std::memory_order_release);
#endif

        cobs_printf(" ");
        break;

      case 0xA0:  // write sector (BACKGROUND HANDLER)
        cobs_printf(" %dw%d/%x", floppy_track, floppy_sector, chore_byte);
        floppy_write_chore = chore_byte;
        // CAUTION: Do NOT read floppy_latch here — Disk BASIC writes $FF40
        // AFTER $FF48. The latch is snapshotted later in WriteData (byte 256).
        //
        // CAUTION: Do NOT reset floppy_ptr here. The foreground's WriteCommand
        // handler already reset it, and by the time this background handler
        // runs, the CoCo may have already written data bytes into floppy_buf.
        // Resetting floppy_ptr here would clobber data and cause all-zero
        // sector writes.

        break;
    }
  }
  static void BackgroundFifoFloppyW256(Coro& self) {
#if FLOPPY_OVER_VFS
    // Temporarily redirect VFS RPCs to yield via our coroutine.
    Coro* saved_vfs_coro = rpc::g_vfs_coro;
    rpc::g_vfs_coro = &self;

    int hnum = -1;
    if (floppy_write_latch & 1) hnum = 0;
    else if (floppy_write_latch & 2) hnum = 1;
    else if (floppy_write_latch & 4) hnum = 2;
    else if (floppy_write_latch & 8) hnum = 3;

    cobs_printf(":%d", hnum);

    bool write_ok = false;
    if (hnum >= 0 && hnum < 4) {
      if (!floppy_vfs_opened[hnum]) {
        int res = vfs_file_open(&floppy_vfs_files[hnum], floppy_vfs_paths[hnum], LFS_O_RDWR | LFS_O_CREAT);
        cobs_printf("[Wopen h%d=%d=%s]", hnum, res, floppy_vfs_paths[hnum]);
        if (res >= 0) floppy_vfs_opened[hnum] = true;
      }
      if (floppy_vfs_opened[hnum]) {
        uint dden_offset = (floppy_write_latch & 0x40) ? 18 : 0;
        uint lsn = dden_offset + 18 * floppy_write_track + floppy_write_sector - 1;
        lfs_soff_t seeked = vfs_file_seek(&floppy_vfs_files[hnum], lsn * 256, LFS_SEEK_SET, &self);
        lfs_ssize_t bytes_written = vfs_file_write(&floppy_vfs_files[hnum], floppy_write_buf, 256);
        cobs_printf("[W h%d lsn%d sk%d wr%d]", hnum, lsn, (int)seeked, (int)bytes_written);
        if (bytes_written == 256) write_ok = true;
      }
    }
    if (!write_ok) {
      cobs_printf("[W FAIL h%d]", hnum);
      floppy_ptr = floppy_buf;
      floppy_status.store(0x04, std::memory_order_release);  // Lost Data
      rpc::g_vfs_coro = saved_vfs_coro;
      return;
    }
    floppy_ptr = floppy_buf;
    cobs_printf(" [sent] ");

    rpc::g_vfs_coro = saved_vfs_coro;
#else
    if (!usb_tether_ok()) {
      // No USB tether: cannot transmit sector data to PC.
      // Signal Lost Data error (bit 2) so DSKCON reports I/O error to BASIC.
      floppy_ptr = floppy_buf;
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
    memcpy(pkt + 6, floppy_write_buf, 256);
    CobsEncodeAndTransmit(pkt, 256 + 6, putchar_raw);

    floppy_ptr = floppy_buf;

    cobs_printf(" [sent] ");
#endif
  }

  static void ReadScsFloppy(const uint &abus, byte &dbus) {
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
      case 0xB:  // ReadData ($FF4B read) — FOREGROUND
        dbus = *floppy_ptr++;
        if (floppy_ptr >= floppy_limit) {
          // All 256 bytes delivered. Clear status and reset pointer
          // unconditionally to prevent overrun into uninitialized memory.
          floppy_status.store(0x00, std::memory_order_relaxed);
          floppy_ptr = floppy_buf;
          if ((floppy_latch & 0x80) != 0) {
            // HALT/NMI enabled: pulse NMI to signal transfer complete.
            // NMI is edge-triggered — release is via nmi_pending flag
            // in the background drain_task (not the FIFO, which is too slow).
            ASSERT_NMI();
            PUSH_TO_BG(FG2BG_NMI, 0, 0);  // For background logging
          }
        }
        break;
      default:
        dbus = ram[abus];
        break;
    }
  }

  static void WriteScsFloppy(const uint &abus, byte &dbus) {
    // WRITE SCS
    switch (abus & 15) {
      case 0x0:  // WriteLatch
        floppy_latch = dbus;
        PUSH_TO_BG(FG2BG_FLOPPY_LATCH, 0, dbus);
        break;
      case 0x8:  // WriteCommand
        if ((dbus & 0xF0) == 0xA0) {
          // WRITE: BUSY + DRQ (0x03) immediately — CoCo must start feeding data.
          floppy_status.store(0x03, std::memory_order_relaxed);
        } else if ((dbus & 0xF0) == 0x80) {
          // READ: BUSY only (0x01). Background sets DRQ after loading data.
          floppy_status.store(0x01, std::memory_order_relaxed);
        } else {
          floppy_status.store(0x00, std::memory_order_relaxed);
        }

        floppy_ptr = floppy_buf;  // Reset pointer.
        if (dbus == 0x17)
          floppy_track = floppy_buf[0];  // was losing critical race

        PUSH_TO_BG(FG2BG_FLOPPY_COMMAND, 0, dbus);
        break;
      case 0x9:  // WriteTrack
        floppy_track = dbus;
        break;
      case 0xA:  // WriteSector
        floppy_sector = dbus;
        break;
      case 0xB:  // WriteData ($FF4B write) — FOREGROUND
        *floppy_ptr++ = dbus;
        if (floppy_ptr >= floppy_limit) {
          // All 256 bytes received. Clear status unconditionally.
          floppy_status.store(0x00, std::memory_order_relaxed);
          if ((floppy_latch & 0x80) != 0) {
            // HALT/NMI enabled. Snapshot ALL metadata and data NOW, before
            // NMI fires and the CoCo immediately starts the next write.
            //
            // WHY snapshot latch/track/sector here (not at command time):
            //   Disk BASIC writes $FF40 (latch) AFTER $FF48 (command).
            //   At command time, floppy_latch still has the OLD value.
            //   By byte 256, the correct latch value has been written.
            //
            // WHY memcpy floppy_buf into floppy_write_buf:
            //   After NMI, the CoCo's NMI handler returns to DSKCON which
            //   immediately starts writing the next sector's data into
            //   floppy_buf. The background hasn't transmitted yet.
            floppy_write_latch = floppy_latch;
            floppy_write_track = floppy_track;
            floppy_write_sector = floppy_sector;
            memcpy(floppy_write_buf, floppy_buf, 256);
            // Reset pointer BEFORE NMI to prevent duplicate triggers if
            // stray WriteData bytes arrive before the next command.
            floppy_ptr = floppy_buf;
            PUSH_TO_BG(FG2BG_W_256, 0, 0);
            ASSERT_NMI();
            PUSH_TO_BG(FG2BG_NMI, 0, 0);  // For background logging
          } else {
            floppy_ptr = floppy_buf;  // Reset to prevent overrun
          }
        }
        break;
      default:
        break;
    }
  }
};

#endif  // CENTIPEDE_FIRMWARE_FLOPPY_H_
