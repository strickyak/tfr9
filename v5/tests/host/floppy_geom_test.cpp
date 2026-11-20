#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "floppy_geom.h"

static void make_os9_lsn0(uint8_t lsn0[19], uint32_t tot, uint8_t tks, uint8_t fmt, uint16_t spt) {
  memset(lsn0, 0, 19);
  lsn0[0] = (uint8_t)((tot >> 16) & 0xFFu);
  lsn0[1] = (uint8_t)((tot >> 8) & 0xFFu);
  lsn0[2] = (uint8_t)(tot & 0xFFu);
  lsn0[3] = tks;
  lsn0[16] = fmt;
  lsn0[17] = (uint8_t)((spt >> 8) & 0xFFu);
  lsn0[18] = (uint8_t)(spt & 0xFFu);
}

int main() {
  uint8_t blank[19] = {};

  // 1. Double-density (18 spt x 256B) raw / DECB sizes across 35, 40, 80 tracks, 1S & 2S
  {
    FloppyGeometry g = FloppyDetectGeometry(161280, blank, true);  // 35T SS DD
    assert(g.sides == 1 && g.tracks == 35 && g.spt == 18 && g.sector_bytes == 256);

    g = FloppyDetectGeometry(184320, blank, true);  // 40T SS DD
    assert(g.sides == 1 && g.tracks == 40 && g.spt == 18 && g.sector_bytes == 256);

    g = FloppyDetectGeometry(322560, blank, true);  // 35T DS DD
    assert(g.sides == 2 && g.tracks == 35 && g.spt == 18 && g.sector_bytes == 256);

    g = FloppyDetectGeometry(368640, blank, true);  // 40T DS DD default
    assert(g.sides == 2 && g.tracks == 40 && g.spt == 18 && g.sector_bytes == 256);

    g = FloppyDetectGeometry(737280, blank, true);  // 80T DS DD
    assert(g.sides == 2 && g.tracks == 80 && g.spt == 18 && g.sector_bytes == 256);
  }

  // 2. Single-density (10 spt x 256B & 18 spt x 128B) sizes across 35, 40, 80 tracks
  {
    FloppyGeometry g = FloppyDetectGeometry(89600, blank, true);   // 35T SS SD (256B)
    assert(g.sides == 1 && g.tracks == 35 && g.spt == 10 && g.sector_bytes == 256);

    g = FloppyDetectGeometry(102400, blank, true);  // 40T SS SD (256B)
    assert(g.sides == 1 && g.tracks == 40 && g.spt == 10 && g.sector_bytes == 256);

    g = FloppyDetectGeometry(179200, blank, true);  // 35T DS SD (256B)
    assert(g.sides == 2 && g.tracks == 35 && g.spt == 10 && g.sector_bytes == 256);

    g = FloppyDetectGeometry(204800, blank, true);  // 40T DS SD (256B)
    assert(g.sides == 2 && g.tracks == 40 && g.spt == 10 && g.sector_bytes == 256);

    g = FloppyDetectGeometry(409600, blank, true);  // 80T DS SD (256B)
    assert(g.sides == 2 && g.tracks == 80 && g.spt == 10 && g.sector_bytes == 256);

    g = FloppyDetectGeometry(80640, blank, true);   // 35T SS SD (128B)
    assert(g.sides == 1 && g.tracks == 35 && g.spt == 18 && g.sector_bytes == 128);

    g = FloppyDetectGeometry(92160, blank, true);   // 40T SS SD (128B)
    assert(g.sides == 1 && g.tracks == 40 && g.spt == 18 && g.sector_bytes == 128);
  }

  // 3. OS-9 LSN0 disambiguation: 40T DS vs 80T SS at 368,640 B (DD) and 204,800 B (SD),
  //    plus mixed-density Track 0 (DD.SPT=10, DD.TKS=18).
  {
    uint8_t lsn0[19];
    make_os9_lsn0(lsn0, 1440, 18, 0x03, 18);  // 40T DS DD (l1_coco.dsk)
    FloppyGeometry g = FloppyDetectGeometry(368640, lsn0, true);
    assert(g.sides == 2 && g.tracks == 40 && g.spt == 18 && g.t0s0_spt == 18);

    make_os9_lsn0(lsn0, 1440, 18, 0x06, 18);  // 80T SS DD (DD.FMT bit 0 == 0)
    g = FloppyDetectGeometry(368640, lsn0, true);
    assert(g.sides == 1 && g.tracks == 80 && g.spt == 18 && g.t0s0_spt == 18);

    make_os9_lsn0(lsn0, 800, 10, 0x04, 10);   // 80T SS SD (10 spt, DD.FMT bit 0 == 0)
    g = FloppyDetectGeometry(204800, lsn0, true);
    assert(g.sides == 1 && g.tracks == 80 && g.spt == 10 && g.t0s0_spt == 10);

    // Mixed-density OS-9 40T DS (Track 0 Side 0 = 10 spt FM, remaining 79 track-sides = 18 spt MFM)
    make_os9_lsn0(lsn0, 10 + 79 * 18, 18, 0x03, 10);
    g = FloppyDetectGeometry((10 + 79 * 18) * 256, lsn0, true);
    assert(g.sides == 2 && g.tracks == 40 && g.spt == 18 && g.t0s0_spt == 10);
  }

  // 4. Drive select & LSN mapping (including Track 34 DOS boot track on 40T DS DD)
  {
    assert(FloppyDriveFromLatch(0x29) == 0);
    assert(FloppyDriveFromLatch(0x2A) == 1);
    assert(FloppyDriveFromLatch(0x2C) == 2);
    assert(FloppyDriveFromLatch(0x68) == 3);  // Drive 3 via bit 6 when bits 0..2 are 0
    assert(FloppyDriveFromLatch(0x69) == 0);  // Drive 0 Side 1 when bit 0 + bit 6 are set
    assert(FloppyDriveFromLatch(0x08) == -1); // Motor on alone selects no drive

    FloppyGeometry g40ds = {2, 40, 18, 18, 256};
    uint32_t lsn = 0;
    // Track 34 Side 0 Sector 1 -> LSN 1224 (byte offset 313,344)
    assert(FloppyComputeLSN(g40ds, 0, 0x39, 34, 1, lsn) && lsn == 1224);
    // Track 34 Side 1 Sector 1 -> LSN 1242
    assert(FloppyComputeLSN(g40ds, 0, 0x79, 34, 1, lsn) && lsn == 1242);
    // Out-of-bounds track / sector rejected
    assert(!FloppyComputeLSN(g40ds, 0, 0x39, 40, 1, lsn));
    assert(!FloppyComputeLSN(g40ds, 0, 0x39, 0, 19, lsn));
    assert(!FloppyComputeLSN(g40ds, 0, 0x39, 0, 0, lsn));

    // Single-sided disk rejects Side 1 (0x40) on drives 0..2
    FloppyGeometry g35ss = {1, 35, 18, 18, 256};
    assert(FloppyComputeLSN(g35ss, 0, 0x39, 17, 1, lsn) && lsn == 306);
    assert(!FloppyComputeLSN(g35ss, 0, 0x79, 17, 1, lsn));
    // Drive 3 uses bit 6 for drive select, so Side 0 works on Drive 3
    assert(FloppyComputeLSN(g35ss, 3, 0x68, 17, 1, lsn) && lsn == 306);
  }

  printf("floppy_geom_test: OK\n");
  return 0;
}
