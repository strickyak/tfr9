#pragma once
// io_install.h -- how a device puts its handlers into the 256-entry I/O dispatch tables ($FF00-$FFFF).
//
//   IoReset()                      engine setup starts with a clean table and an empty ownership map
//   IoClaim(first, last, name)     record who owns an address range; an overlap, or $FF7F (Multi-Pak select),
//                                  is a configuration error: say which devices clash and stop (see iomap.h)
//   IoSet(addr, reader, writer)    store the handlers for one address (nullptr = none)
//   IoFinish()                     after setup: reserve user-requested enhancements, check the result against the plan
//
// RULE: handlers are NAMED functions marked IN_RAM -- never lambdas.  A lambda cannot be IN_RAM, so it would
// run from flash and stall the bus (spec 5.1; enforced by scripts/check-no-lambda-io.sh).  Install() itself runs
// once at setup on core 0 and may live in flash; only the handlers it stores must be in RAM.
//
// Requires IOReader/IOWriter/IOReaders/IOWriters (main.cpp), cobs_printf and centipede_abort.
#include "iomap.h"
#include "ports_config.h"   // PortBase, PortsValidate; includes the generated ports.tsv tables

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
  if (r.owner) cobs_printf("[io-map: %s wants 0x%04x-0x%04x but 0x%04x already belongs to %s]\n", name, first, last, r.addr, r.owner);
  else         cobs_printf("[io-map: %s may not use 0x%04x-0x%04x (0x%04x is forbidden or out of range)]\n", name, first, last, r.addr);
  centipede_abort("io-map conflict");
}

static inline void IoSet(uint16_t addr, IOReader r, IOWriter w) {
  IOReaders[addr & 0xFF] = r;
  IOWriters[addr & 0xFF] = w;
}

// Enhancement devices the user asked for (`with e1, becker at 0xff48`) are not implemented
// yet (Phase 5): reserve their ranges so they are checked and shown like any device, and say so once per device.
static void IoClaimEnhancements(const TargetSpec& tgt) {
  const char* wmap = g_port_settings.WithMap();
  for (size_t i = 0; i < kNumMapPorts; i++) {
    const MapPortRange& e = kMapPorts[i];
    if (strcmp(e.map_name, wmap)) continue;
    if (IsCoreDeviceOf(tgt, e.device)) continue;
    const char* dv = g_port_settings.Get(e.device);
    char at_key[PortSettings::kLen];
    snprintf(at_key, sizeof at_key, "%s.at", e.device);
    const char* ov = g_port_settings.Get(at_key);
    bool enabled = (dv && !strcmp(dv, "on")) || (!dv && ov != nullptr);
    if (!enabled) continue;
    uint16_t base = e.first;
    if (ov && !ParsePortAddr(ov, &base)) continue;
    uint16_t last = (uint16_t)(base + (e.last - e.first));
    IoClaim(base, last, e.device);
    cobs_printf("[io-map: %s reserved at 0x%04x-0x%04x for %s/%s: no handler in this firmware yet]\n",
                e.device, base, last, tgt.run_os, tgt.for_hw);
  }
}

// After an engine's devices are installed: reserve the enhancements, then compare the RP2350 ownership map with
// what the configuration says it should be (resolved by the same code that `bye` used to approve it).
static void IoFinish() {
  const TargetSpec* tgt = nullptr;
  PortsCheck trc = ResolveActiveTarget(g_port_settings, &tgt);
  if (!trc.ok || !tgt) { cobs_printf("[io-map: target resolution problem: %s]\n", trc.msg); return; }
  IoClaimEnhancements(*tgt);
  IoMap& expect = g_io_scratch;
  PortsCheck pc = PortsValidate(g_port_settings, expect, /*include_host_reserved=*/false);
  if (!pc.ok) { cobs_printf("[io-map: %s/%s: configuration problem: %s]\n", tgt->run_os, tgt->for_hw, pc.msg); return; }
  int bad = 0, ranges = 0;
  const char* prev = nullptr;
  for (uint32_t a = 0xFF00; a <= 0xFFFF; a++) {
    const char* want = expect.OwnerOf((uint16_t)a);
    const char* have = g_io_map.OwnerOf((uint16_t)a);
    if (have && (!prev || strcmp(prev, have))) ranges++;
    prev = have;
    if ((want == nullptr) == (have == nullptr) && (!want || !strcmp(want, have))) continue;
    cobs_printf("[io-map: %s/%s 0x%04x should be %s but is %s]\n",
                tgt->run_os, tgt->for_hw, (unsigned)a, want ? want : "unclaimed", have ? have : "unclaimed");
    bad++;
  }
  if (!bad)
    cobs_printf("[io-map: %s/%s/%s matches the plan (engine %s, %d ranges, %d settings)]\n",
                tgt->on_host, tgt->run_os, tgt->for_hw, tgt->engine, ranges, g_port_settings.n_);
}
