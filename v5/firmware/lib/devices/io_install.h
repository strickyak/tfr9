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

// 256-bit bitmap of $FF00..$FFFF offsets owned by the RP2350 (built in IoFinish from g_io_map).
// On Gerbil (Centipede), core 1 drives D0..D7 + SLENB# on $FFxx ONLY when the bit in g_io_drive_mask is 1,
// letting the CoCo motherboard's real PIA/SAM/GIME drive unclaimed $FFxx ports without bus contention.
static uint32_t g_io_drive_mask[8];

FORCE_INLINE bool IN_RAM IoIsDriven(uint8_t lo) {
  return (g_io_drive_mask[lo >> 5] & (1u << (lo & 31))) != 0;
}

// Core-0 runtime hooks installed alongside devices (replaces the old `current_engine` switch chains):
static void (*g_rx_poll_fn)() = nullptr;      // core-0 Phase 2 terminal RX delivery
static void (*g_timer_60hz_fn)() = nullptr;   // core-0 60 Hz timer callback (targets MUST be IN_RAM)
static bool g_deep_mmu_active = false;        // core-0 EmuDsk / $FE panic dump translates via deep_mmu

inline void IoResetEnhancementState();
inline void IoInstallEnhancementDevice(const char* dev, uint16_t base, uint16_t last, const TargetSpec& tgt);

static inline void IoReset() {
  g_io_map.Reset();
  for (int i = 0; i < 8; i++) g_io_drive_mask[i] = 0;
  for (int i = 0; i < 256; i++) { IOReaders[i] = nullptr; IOWriters[i] = nullptr; }
  g_rx_poll_fn = nullptr;
  g_timer_60hz_fn = nullptr;
  g_deep_mmu_active = false;
  g_term_acia_arrow_keys = false;
  IoResetEnhancementState();
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

// Install user-requested `with ...` enhancement devices (`becker`, `audio`, `rtc`, `orchestra90`, `info`).
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
    IoInstallEnhancementDevice(e.device, base, last, tgt);
  }
}

// After an engine's devices are installed: reserve the enhancements, then compare the RP2350 ownership map with
// what the configuration says it should be (resolved by the same code that `bye` used to approve it).
static void IoFinish() {
  const TargetSpec* tgt = nullptr;
  PortsCheck trc = ResolveActiveTarget(g_port_settings, &tgt, DEFAULT_BOARD_HOST);
  if (!trc.ok || !tgt) { cobs_printf("[io-map: target resolution problem: %s]\n", trc.msg); return; }
  IoClaimEnhancements(*tgt);
  for (int i = 0; i < 8; i++) g_io_drive_mask[i] = 0;
  for (uint32_t a = 0xFF00; a <= 0xFFFF; a++) {
    if (g_io_map.OwnerOf((uint16_t)a) != nullptr) {
      uint8_t lo = (uint8_t)(a & 0xFF);
      g_io_drive_mask[lo >> 5] |= (1u << (lo & 31));
    }
  }
  IoMap& expect = g_io_scratch;
  PortsCheck pc = PortsValidate(g_port_settings, expect, /*include_host_reserved=*/false, DEFAULT_BOARD_HOST);
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
