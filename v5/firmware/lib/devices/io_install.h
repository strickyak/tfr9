#pragma once
// io_install.h -- how a device puts its handlers into the 256-entry I/O dispatch tables ($FF00-$FFFF).
//
//   IoReset()                      engine setup starts with a clean table and an empty ownership map
//   IoClaim(first, last, name)     record who owns an address range; an overlap, or $FF7F (Multi-Pak select),
//                                  is a configuration error: say which devices clash and stop (see iomap.h)
//   IoSet(addr, reader, writer)    store the handlers for one address (nullptr = none)
//   IoFinish(engine)               after setup: reserve user-requested enhancements, check the result against the plan
//
// RULE: handlers are NAMED functions marked IN_RAM -- never lambdas.  A lambda cannot be IN_RAM, so it would
// run from flash and stall the bus (spec 5.1; enforced by scripts/check-no-lambda-io.sh).  Install() itself runs
// once at setup on core 0 and may live in flash; only the handlers it stores must be in RAM.
//
// Requires IOReader/IOWriter/IOReaders/IOWriters (main.cpp), cobs_printf and centipede_abort.
#include "iomap.h"
#include "ports_config.h"   // profile/overrides (PortBase, PortsValidate); includes the generated ports.tsv table

static IoMap g_io_map;      // who owns what, for the engine being set up
static IoMap g_io_scratch;  // working copy for checks (`ports check`, IoFinish): one shared map, RAM is scarce

// Forget who owns what (not the handlers).  For engines that decode some addresses themselves, outside the tables.
static inline void IoResetOwners() { g_io_map.Reset(); }

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

// Enhancement devices the user asked for (`enhance becker 0xFF48`) are not implemented yet: reserve their ranges
// (so they are checked and shown like any device) and say so, once per device.
static void IoClaimEnhancements(const char* engine) {
  const char* profile = g_port_settings.Profile(); (void)profile;
  for (int k = 0; k < g_port_settings.n_; k++) {
    const char* key = g_port_settings.key_[k];
    const char* val = g_port_settings.val_[k];
    if (!strcmp(val, "off")) continue;
    const PortRange* e = nullptr;
    for (size_t i = 0; i < kNumPorts; i++)
      if (!strcmp(kPorts[i].profile, "E1") && !strcmp(kPorts[i].device, key)) e = &kPorts[i];
    if (!e) continue;                       // not an enhancement (a core device override, or "profile")
    uint16_t base;
    if (!ParsePortAddr(val, &base)) continue;
    uint16_t last = (uint16_t)(base + (e->last - e->first));
    IoClaim(base, last, key);
    cobs_printf("[io-map: %s reserved at $%04X-$%04X for %s: no handler in this firmware yet]\n", key, base, last, engine);
  }
}

// After an engine's devices are installed: reserve the enhancements, then compare the ownership map with what the
// configuration says it should be (profile + overrides, resolved by the same code that `bye` used to approve it).
// A mismatch is printed, not fatal: it means the code and the plan disagree and one of them needs fixing.
// `tolerated`: a device the code installs although the plan has no row for it in this engine (v4 behavior kept);
// it is reported as a note instead of a mismatch.
static void IoFinish(const char* engine, const char* tolerated = nullptr) {
  IoClaimEnhancements(engine);
  IoMap& expect = g_io_scratch;
  PortsCheck pc = PortsValidate(engine, g_port_settings, expect);
  if (!pc.ok) { cobs_printf("[io-map: %s: configuration problem: %s]\n", engine, pc.msg); return; }
  int bad = 0, ranges = 0;
  const char* prev = nullptr;
  bool noted = false;
  for (uint32_t a = 0xFF00; a <= 0xFFFF; a++) {
    const char* want = expect.OwnerOf((uint16_t)a);
    const char* have = g_io_map.OwnerOf((uint16_t)a);
    if (have && (!prev || strcmp(prev, have))) ranges++;
    prev = have;
    if ((want == nullptr) == (have == nullptr) && (!want || !strcmp(want, have))) continue;
    if (!want && have && tolerated && !strcmp(have, tolerated)) {
      if (!noted) { cobs_printf("[io-map: note: %s also installs %s (v4 behavior; not in ports.tsv)]\n", engine, tolerated); noted = true; }
      continue;
    }
    cobs_printf("[io-map: %s $%04X should be %s but is %s]\n", engine, (unsigned)a, want ? want : "unclaimed", have ? have : "unclaimed");
    bad++;
  }
  if (!bad) cobs_printf("[io-map: %s matches the plan (profile %s, %d ranges, %d settings)]\n", engine, g_port_settings.Profile(), ranges, g_port_settings.n_);
}
