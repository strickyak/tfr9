#pragma once
// io_install.h -- how a device puts its handlers into the 256-entry I/O dispatch tables ($FF00-$FFFF).
//
//   IoReset()                      engine setup starts with a clean table and an empty ownership map
//   IoClaim(first, last, name)     record who owns an address range; an overlap, or $FF7F (Multi-Pak select),
//                                  is a configuration error: say which devices clash and stop (see iomap.h)
//   IoSet(addr, reader, writer)    store the handlers for one address (nullptr = none)
//   IoVerifyPlan(engine)           after setup, check what was installed against firmware/engines/ports.tsv
//
// RULE: handlers are NAMED functions marked IN_RAM -- never lambdas.  A lambda cannot be IN_RAM, so it would
// run from flash and stall the bus (spec 5.1; enforced by scripts/check-no-lambda-io.sh).  Install() itself runs
// once at setup on core 0 and may live in flash; only the handlers it stores must be in RAM.
//
// Requires IOReader/IOWriter/IOReaders/IOWriters (main.cpp), cobs_printf and centipede_abort.
#include "iomap.h"
#include "ports_gen.h"      // generated from engines/ports.tsv by scripts/gen-ports.sh

static IoMap g_io_map;      // who owns what, for the engine being set up

static inline void IoReset() {
  g_io_map.Reset();
  for (int i = 0; i < 256; i++) { IOReaders[i] = nullptr; IOWriters[i] = nullptr; }
}

static void IoClaim(uint16_t first, uint16_t last, const char* name) {
  IoMap::Result r = g_io_map.Claim(first, last, name);
  if (r.ok) return;
  if (r.owner) cobs_printf("[io-map: %s wants $%04X-$%04X but $%04X already belongs to %s]\n", name, first, last, r.addr, r.owner);
  else         cobs_printf("[io-map: %s may not use $%04X-$%04X ($%04X is forbidden or out of range)]\n", name, first, last, r.addr);
  centipede_abort("io-map conflict");
}

static inline void IoSet(uint16_t addr, IOReader r, IOWriter w) {
  IOReaders[addr & 0xFF] = r;
  IOWriters[addr & 0xFF] = w;
}

// Compare the installed ownership map with the plan in ports.tsv (profile TFR4, the v4 addresses).  A mismatch
// is printed, not fatal: it means the code and the plan disagree and one of them needs fixing.
static void IoVerifyPlan(const char* engine, const char* profile = "TFR4") {
  int rows = 0, bad = 0;
  uint8_t covered[256] = {0};
  for (size_t i = 0; i < kNumPorts; i++) {
    const PortRange& p = kPorts[i];
    if (strcmp(p.profile, profile) || strcmp(p.engine, engine)) continue;
    rows++;
    for (uint32_t a = p.first; a <= p.last; a++) {
      covered[a & 0xFF] = 1;
      const char* o = g_io_map.OwnerOf((uint16_t)a);
      if (!o || strcmp(o, p.device)) {
        cobs_printf("[io-map: %s $%04X should be %s (ports.tsv) but is %s]\n", engine, (unsigned)a, p.device, o ? o : "unclaimed");
        bad++;
      }
    }
  }
  for (uint32_t a = 0xFF00; a <= 0xFFFF; a++) {
    if (g_io_map.OwnerOf((uint16_t)a) && !covered[a & 0xFF]) {
      cobs_printf("[io-map: %s $%04X is %s but ports.tsv has no row for it]\n", engine, (unsigned)a, g_io_map.OwnerOf((uint16_t)a));
      bad++;
    }
  }
  if (!rows) cobs_printf("[io-map: %s has no rows for profile %s in ports.tsv]\n", engine, profile);
  else if (!bad) cobs_printf("[io-map: %s matches ports.tsv (%d ranges)]\n", engine, rows);
}
