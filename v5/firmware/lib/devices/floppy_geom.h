#ifndef CENTIPEDE_FIRMWARE_FLOPPY_GEOM_H_
#define CENTIPEDE_FIRMWARE_FLOPPY_GEOM_H_

#include <stdint.h>

struct FloppyGeometry {
  uint8_t sides;         // 1 (single-sided) or 2 (double-sided)
  uint8_t tracks;        // 35, 40, or 80 (or exact count from OS-9 DD.TOT)
  uint8_t spt;           // sectors per track (18 for DD / 128B SD, 10 for 256B SD)
  uint8_t t0s0_spt;      // sectors on track 0 side 0 (usually == spt; 10 on mixed OS-9)
  uint16_t sector_bytes; // 256 (standard CoCo SD/DD) or 128 (FM 18x128B)
};

// Infer floppy geometry (single/double density, single/double sided, 35/40/80 tracks)
// from image byte size and optional first 19 bytes of LSN 0 (for OS-9 RBF images).
inline FloppyGeometry FloppyDetectGeometry(int64_t file_size,
                                           const uint8_t* lsn0,
                                           bool has_lsn0) {
  // 1. Check for a valid OS-9 RBF LSN 0 descriptor matching the exact image size.
  //    Disambiguates 40T DS vs 80T SS at 368,640 B (DD) and 204,800 B (SD),
  //    and handles mixed-density Track 0 (DD.SPT != DD.TKS).
  if (has_lsn0 && lsn0 != nullptr && file_size >= 256 && (file_size % 256) == 0) {
    uint32_t dd_tot = ((uint32_t)lsn0[0] << 16) | ((uint32_t)lsn0[1] << 8) | lsn0[2];
    uint8_t dd_tks = lsn0[3];
    uint8_t dd_fmt = lsn0[16];
    uint16_t dd_spt = ((uint16_t)lsn0[17] << 8) | lsn0[18];
    if (dd_tot > 0 && (int64_t)dd_tot * 256 == file_size &&
        (dd_tks == 10 || dd_tks == 16 || dd_tks == 18 || dd_tks == 20)) {
      uint8_t sides = (dd_fmt & 0x01) ? 2 : 1;
      uint8_t t0s0 = (dd_spt > 0 && dd_spt <= 36) ? (uint8_t)dd_spt : dd_tks;
      if (dd_tot >= t0s0 && ((dd_tot - t0s0) % dd_tks) == 0) {
        uint32_t total_track_sides = 1u + (dd_tot - t0s0) / dd_tks;
        if ((total_track_sides % sides) == 0) {
          uint32_t tracks = total_track_sides / sides;
          if (tracks >= 1 && tracks <= 255) {
            return FloppyGeometry{sides, (uint8_t)tracks, dd_tks, t0s0, 256};
          }
        }
      }
    }
  }

  // 2. Exact byte-size matches for 35 / 40 / 80 tracks across single & double density
  //    and single & double sided virtual disk images.
  switch (file_size) {
    // Single-density FM, 18 sectors/track x 128 bytes/sector:
    case 35 * 1 * 18 * 128:  //  80,640 B: 35T SS SD (128B)
      return FloppyGeometry{1, 35, 18, 18, 128};
    case 40 * 1 * 18 * 128:  //  92,160 B: 40T SS SD (128B)
      return FloppyGeometry{1, 40, 18, 18, 128};

    // Single-density FM, 10 sectors/track x 256 bytes/sector:
    case 35 * 1 * 10 * 256:  //  89,600 B: 35T SS SD (256B)
      return FloppyGeometry{1, 35, 10, 10, 256};
    case 40 * 1 * 10 * 256:  // 102,400 B: 40T SS SD (256B)
      return FloppyGeometry{1, 40, 10, 10, 256};
    case 35 * 2 * 10 * 256:  // 179,200 B: 35T DS SD (256B)
      return FloppyGeometry{2, 35, 10, 10, 256};
    case 40 * 2 * 10 * 256:  // 204,800 B: 40T DS SD (80T SS SD handled via OS-9 LSN0 above)
      return FloppyGeometry{2, 40, 10, 10, 256};
    case 80 * 2 * 10 * 256:  // 409,600 B: 80T DS SD (256B)
      return FloppyGeometry{2, 80, 10, 10, 256};

    // Double-density MFM, 18 sectors/track x 256 bytes/sector:
    case 35 * 1 * 18 * 256:  // 161,280 B: 35T SS DD (standard CoCo Disk BASIC)
      return FloppyGeometry{1, 35, 18, 18, 256};
    case 40 * 1 * 18 * 256:  // 184,320 B: 40T SS DD
      return FloppyGeometry{1, 40, 18, 18, 256};
    case 35 * 2 * 18 * 256:  // 322,560 B: 35T DS DD
      return FloppyGeometry{2, 35, 18, 18, 256};
    case 40 * 2 * 18 * 256:  // 368,640 B: 40T DS DD (80T SS DD handled via OS-9 LSN0 above)
      return FloppyGeometry{2, 40, 18, 18, 256};
    case 80 * 2 * 18 * 256:  // 737,280 B: 80T DS DD
      return FloppyGeometry{2, 80, 18, 18, 256};
    default:
      break;
  }

  // 3. Fallback for partial/trimmed/newly-created images (256B DD 18 spt):
  if (file_size > 368640) {
    return FloppyGeometry{2, 80, 18, 18, 256};
  } else if (file_size > 322560) {
    return FloppyGeometry{2, 40, 18, 18, 256};
  } else if (file_size > 184320) {
    return FloppyGeometry{2, 35, 18, 18, 256};
  } else if (file_size > 161280) {
    return FloppyGeometry{1, 40, 18, 18, 256};
  }
  return FloppyGeometry{1, 35, 18, 18, 256};
}

// Decode CoCo $FF40 drive select latch:
//   bits 0..2 select drives 0..2; bit 6 (0x40) selects drive 3 when bits 0..2 are 0
//   (or side 1 when drives 0..2 are selected).
inline int FloppyDriveFromLatch(uint8_t latch) {
  if (latch & 0x01) return 0;
  if (latch & 0x02) return 1;
  if (latch & 0x04) return 2;
  if (latch & 0x40) return 3;
  return -1;
}

// Convert (drive, latch, track, 1-based sector) into a 0-based sector index.
// Returns false if drive, side, track, or sector is out of range for `g`.
inline bool FloppyComputeLSN(const FloppyGeometry& g, int hnum, uint8_t latch,
                             uint8_t track, uint8_t sector, uint32_t& out_lsn) {
  if (hnum < 0 || hnum >= 4) return false;
  if (track >= g.tracks) return false;
  uint32_t side = (hnum < 3 && (latch & 0x40)) ? 1u : 0u;
  if (side >= g.sides) return false;
  uint32_t max_sec = (track == 0 && side == 0) ? g.t0s0_spt : g.spt;
  if (sector == 0 || sector > max_sec) return false;
  uint32_t track_side = (uint32_t)track * g.sides + side;
  if (track_side == 0) {
    out_lsn = (uint32_t)(sector - 1);
  } else {
    out_lsn = (uint32_t)g.t0s0_spt + (track_side - 1u) * g.spt + (uint32_t)(sector - 1);
  }
  return true;
}

#endif  // CENTIPEDE_FIRMWARE_FLOPPY_GEOM_H_
