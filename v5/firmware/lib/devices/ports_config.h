#pragma once
// ports_config.h -- user-chosen I/O port placement: which profile, which devices are moved or added.
// Pure C++17, no Pico SDK, no heap: it compiles on the PC for tests (tests/host/ports_config_test.cpp).
//
// The configuration is FLAT KEY-VALUE PAIRS, one value per key (spec 15.x):
//     profile = TFR4 | COCO          which column of ports.tsv the engine's core devices come from
//     <device> = 0xFF48 | off        move a device to a base address, or (enhancements only) leave it out
// Tcl shortcuts (`ports profile COCO`, `enhance becker 0xFF48`, see products/tfr911h/ports_cmd.h) just set keys.
// It lives in RAM only and is written every boot by Tcl; nothing is stored in flash.
//
// PortsValidate() is the gatekeeper: it resolves the placement of every device of an engine and claims the
// ranges in an IoMap, so an overlap (or $FF7F) is reported BEFORE the engine is launched, as text naming both
// devices.  The engine's Install() functions then use PortBase() for the same addresses.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "iomap.h"
#include "ports_gen.h"      // kPorts: generated from engines/ports.tsv

struct PortSettings {
  static constexpr int kMax = 24, kLen = 24;
  char key_[kMax][kLen];
  char val_[kMax][kLen];
  int n_ = 0;

  void Clear() { n_ = 0; }
  const char* Get(const char* key) const {
    for (int i = 0; i < n_; i++) if (!strcmp(key_[i], key)) return val_[i];
    return nullptr;
  }
  // false if the table is full or key/value is too long
  bool Set(const char* key, const char* val) {
    if (strlen(key) >= (size_t)kLen || strlen(val) >= (size_t)kLen) return false;
    for (int i = 0; i < n_; i++) if (!strcmp(key_[i], key)) { strcpy(val_[i], val); return true; }
    if (n_ >= kMax) return false;
    strcpy(key_[n_], key); strcpy(val_[n_], val); n_++;
    return true;
  }
  bool Unset(const char* key) {
    for (int i = 0; i < n_; i++) if (!strcmp(key_[i], key)) {
      for (int j = i; j + 1 < n_; j++) { strcpy(key_[j], key_[j + 1]); strcpy(val_[j], val_[j + 1]); }
      n_--; return true;
    }
    return false;
  }
  const char* Profile() const { const char* p = Get("profile"); return p ? p : "TFR4"; }
};

inline PortSettings g_port_settings;

// Accepts 0xFF48 or $FF48 (hex only: a bare number is ambiguous).  The address must be in $FF00..$FFFF.
inline bool ParsePortAddr(const char* s, uint16_t* out) {
  if (!s) return false;
  if (s[0] == '$') s++; else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2; else return false;
  if (!*s) return false;
  uint32_t v = 0;
  for (; *s; s++) {
    int d = (*s >= '0' && *s <= '9') ? *s - '0' : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 : (*s >= 'A' && *s <= 'F') ? *s - 'A' + 10 : -1;
    if (d < 0) return false;
    v = v * 16 + d;
    if (v > 0xFFFF) return false;
  }
  if (v < 0xFF00) return false;
  *out = (uint16_t)v;
  return true;
}

inline bool IsKnownProfile(const char* p) {
  if (!strcmp(p, "E1")) return false;                  // E1 is the enhancement map, not a profile
  for (size_t i = 0; i < kNumPorts; i++) if (!strcmp(kPorts[i].profile, p)) return true;
  return false;
}

// Some engines decode their ports inside the bus loop and cannot be moved.
inline bool EngineHasFixedPorts(const char* engine) { return !strcmp(engine, "multicomp09fuzix"); }

// Base address of a core device of `engine` under the current settings.  Falls back to `dflt` if ports.tsv has
// no such row (a code/plan mismatch, reported separately by IoFinish).
inline uint16_t PortBase(const char* engine, const char* device, uint16_t dflt) {
  const char* ov = g_port_settings.Get(device);
  uint16_t a;
  if (ov && ParsePortAddr(ov, &a)) return a;
  const char* profile = g_port_settings.Profile();
  for (size_t i = 0; i < kNumPorts; i++) {
    const PortRange& p = kPorts[i];
    if (!strcmp(p.profile, profile) && !strcmp(p.engine, engine) && !strcmp(p.device, device)) return p.first;
  }
  return dflt;
}

struct PortsCheck {
  bool ok;
  char msg[200];
};

inline PortsCheck PortsFail(const char* fmt, const char* a = "", const char* b = "", const char* c = "") {
  PortsCheck r; r.ok = false; snprintf(r.msg, sizeof r.msg, fmt, a, b, c); return r;
}

// Resolve every device of `engine` under `st`, claiming ranges in `map`.  The first problem is returned.
inline PortsCheck PortsValidate(const char* engine, const PortSettings& st, IoMap& map) {
  map.Reset();
  const char* profile = st.Profile();
  if (!IsKnownProfile(profile)) return PortsFail("unknown profile '%s' (known: TFR4, COCO)", profile);

  // 1. the engine's core devices
  int rows = 0;
  for (size_t i = 0; i < kNumPorts; i++) {
    const PortRange& p = kPorts[i];
    if (strcmp(p.profile, profile) || strcmp(p.engine, engine)) continue;
    rows++;
    uint16_t base = p.first;
    unsigned size = (unsigned)p.last - p.first + 1;
    const char* ov = st.Get(p.device);
    if (ov) {
      if (!strcmp(p.device, "vectors")) return PortsFail("the vectors ($FFF0-$FFFF) cannot be moved");
      if (EngineHasFixedPorts(engine)) return PortsFail("%s decodes its own ports and cannot be moved (%s)", engine, p.device);
      if (!strcmp(ov, "off")) return PortsFail("core device %s of %s cannot be turned off", p.device, engine);
      if (!ParsePortAddr(ov, &base)) return PortsFail("%s: '%s' is not an address like 0xFF48", p.device, ov);
    }
    if ((uint32_t)base + size - 1 > 0xFFFF) return PortsFail("%s would run past $FFFF", p.device);
    IoMap::Result r = map.Claim(base, (uint16_t)(base + size - 1), p.device);
    if (!r.ok) {
      PortsCheck c; c.ok = false;
      if (r.owner) snprintf(c.msg, sizeof c.msg, "%s ($%04X-$%04X) conflicts with %s at $%04X", p.device, base, base + size - 1, r.owner, r.addr);
      else         snprintf(c.msg, sizeof c.msg, "%s ($%04X-$%04X) may not use $%04X (forbidden: Multi-Pak select)", p.device, base, base + size - 1, r.addr);
      return c;
    }
  }
  if (!rows) return PortsFail("engine %s has no ports in profile %s", engine, profile);

  // 2. every other key must name an enhancement (E1 map) or one of this engine's devices
  for (int k = 0; k < st.n_; k++) {
    const char* key = st.key_[k];
    if (!strcmp(key, "profile")) continue;
    bool core = false;
    for (size_t i = 0; i < kNumPorts; i++)
      if (!strcmp(kPorts[i].profile, profile) && !strcmp(kPorts[i].engine, engine) && !strcmp(kPorts[i].device, key)) core = true;
    if (core) continue;                                          // handled above
    const PortRange* e = nullptr;
    for (size_t i = 0; i < kNumPorts; i++)
      if (!strcmp(kPorts[i].profile, "E1") && !strcmp(kPorts[i].device, key)) e = &kPorts[i];
    if (!e) return PortsFail("'%s' is not a device of %s or a known enhancement", key, engine);
    const char* v = st.Get(key);
    if (!strcmp(v, "off")) continue;
    uint16_t base;
    if (!ParsePortAddr(v, &base)) return PortsFail("%s: '%s' is not an address like 0xFF48", key, v);
    unsigned size = (unsigned)e->last - e->first + 1;
    if ((uint32_t)base + size - 1 > 0xFFFF) return PortsFail("%s would run past $FFFF", key);
    IoMap::Result r = map.Claim(base, (uint16_t)(base + size - 1), key);
    if (!r.ok) {
      PortsCheck c; c.ok = false;
      if (r.owner) snprintf(c.msg, sizeof c.msg, "%s ($%04X-$%04X) conflicts with %s at $%04X", key, base, base + size - 1, r.owner, r.addr);
      else         snprintf(c.msg, sizeof c.msg, "%s ($%04X-$%04X) may not use $%04X (forbidden: Multi-Pak select)", key, base, base + size - 1, r.addr);
      return c;
    }
  }
  PortsCheck ok; ok.ok = true; ok.msg[0] = 0;
  return ok;
}
