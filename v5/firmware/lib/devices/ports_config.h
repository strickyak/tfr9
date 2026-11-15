#pragma once
// ports_config.h -- target selection (`on ... run ... for ... with ...`) and flat key-value I/O port configuration.
// Pure C++17, no Pico SDK, no heap: it compiles on the PC for tests (tests/host/ports_config_test.cpp).
//
// User-facing grammar (spec 7.1, 15.y):
//   on <host> run <os> [for <target>] [with <options...>]
//   run <os> [for <target>] [with <options...>]
// Any uppercase in `on`/`run` commands is converted to lowercase.  It compiles down into flat key-value pairs:
//   on = tfr9 | coco2 | coco3
//   run = turbo9os | nitros9l1 | nitros9l2 | hatvan | fuzix | rsbasic
//   for = turbo9sim | flat65280 | deep65280 | jamesbrown | coco3 | coco2 | gep9 | gep9big | multicomp09
//   engine = hamster_flat | hamster_gime | hamster_gep9 | hamster_multicomp | gerbil_io | ...
//   with = e1
//   <device> = on | off
//   <device>.<param> = <value>    (e.g. becker.at = 0xff41, dw.over = bitbang, dw.baud = 9600)
//
// PortsValidate() is the gatekeeper: it resolves every core device, motherboard reserved range, and enhancement,
// claiming ranges in an IoMap so an overlap or 0xff7f ($FF7F) is caught BEFORE launching core 1.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "iomap.h"
#include "ports_gen.h"      // kTargets, kCorePorts, kHostReserved, kMapPorts (from engines/ports.tsv)

inline bool LowerCopy(char* dst, size_t cap, const char* src) {
  if (!src || cap == 0) return false;
  size_t i = 0;
  for (; src[i]; i++) {
    if (i + 1 >= cap) return false;
    char c = src[i];
    dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  }
  dst[i] = 0;
  return true;
}

struct PortSettings {
  static constexpr int kMax = 32, kLen = 24;
  char key_[kMax][kLen];
  char val_[kMax][kLen];
  int n_ = 0;

  void Clear() { n_ = 0; }
  const char* Get(const char* key) const {
    char kbuf[kLen];
    if (!LowerCopy(kbuf, sizeof kbuf, key)) return nullptr;
    for (int i = 0; i < n_; i++) if (!strcmp(key_[i], kbuf)) return val_[i];
    return nullptr;
  }
  // Stores lowercase key and value; returns false if the table is full or key/value is too long.
  bool Set(const char* key, const char* val) {
    char kbuf[kLen], vbuf[kLen];
    if (!LowerCopy(kbuf, sizeof kbuf, key) || !LowerCopy(vbuf, sizeof vbuf, val)) return false;
    for (int i = 0; i < n_; i++) if (!strcmp(key_[i], kbuf)) { strcpy(val_[i], vbuf); return true; }
    if (n_ >= kMax) return false;
    strcpy(key_[n_], kbuf); strcpy(val_[n_], vbuf); n_++;
    return true;
  }
  bool Unset(const char* key) {
    char kbuf[kLen];
    if (!LowerCopy(kbuf, sizeof kbuf, key)) return false;
    for (int i = 0; i < n_; i++) if (!strcmp(key_[i], kbuf)) {
      for (int j = i; j + 1 < n_; j++) { strcpy(key_[j], key_[j + 1]); strcpy(val_[j], val_[j + 1]); }
      n_--; return true;
    }
    return false;
  }
  // Remove `dev` and every `dev.*` parameter key.
  void UnsetDevice(const char* dev) {
    char dbuf[kLen];
    if (!LowerCopy(dbuf, sizeof dbuf, dev)) return;
    size_t dlen = strlen(dbuf);
    int out = 0;
    for (int i = 0; i < n_; i++) {
      bool match = !strcmp(key_[i], dbuf) ||
                   (!strncmp(key_[i], dbuf, dlen) && key_[i][dlen] == '.');
      if (!match) {
        if (out != i) { strcpy(key_[out], key_[i]); strcpy(val_[out], val_[i]); }
        out++;
      }
    }
    n_ = out;
  }
  const char* Host(const char* dflt = "tfr9") const {
    const char* h = Get("on");
    return h ? h : dflt;
  }
  const char* WithMap() const {
    const char* w = Get("with");
    return w ? w : "e1";
  }
};

inline PortSettings g_port_settings;

// Accepts 0xff48 / 0xFF48 (preferred in Tcl) or $ff48 (hex only: bare numbers are ambiguous).  Must be in 0xff00..0xffff.
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

inline bool ParsePositiveInt(const char* s, uint32_t* out) {
  if (!s || !*s) return false;
  uint64_t v = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') return false;
    v = v * 10 + (unsigned)(*s - '0');
    if (v > 0xFFFFFFFFu) return false;
  }
  if (v == 0) return false;
  *out = (uint32_t)v;
  return true;
}

inline bool IsKnownHost(const char* h) {
  if (!h) return false;
  for (size_t i = 0; i < kNumTargets; i++) if (!strcmp(kTargets[i].on_host, h)) return true;
  return false;
}

inline bool IsKnownEnhancementMap(const char* m) {
  if (!m) return false;
  for (size_t i = 0; i < kNumMapPorts; i++) if (!strcmp(kMapPorts[i].map_name, m)) return true;
  return false;
}

// Map tag syntax in `with` (e.g. `e1`, `e2`): recognized during digestion so `with = e2` is stored cleanly.
inline bool IsMapTagToken(const char* s) {
  if (!s || s[0] != 'e' || s[1] < '0' || s[1] > '9') return false;
  for (size_t i = 2; s[i]; i++) if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

inline bool IsSimpleIdent(const char* s) {
  if (!s || !*s) return false;
  for (size_t i = 0; s[i]; i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

inline bool IsReservedGrammarWord(const char* s) {
  return !strcmp(s, "on") || !strcmp(s, "run") || !strcmp(s, "for") ||
         !strcmp(s, "with") || !strcmp(s, "engine") || !strcmp(s, "at") || !strcmp(s, "off");
}

inline const TargetSpec* FindTarget(const char* on_host, const char* run_os, const char* for_hw) {
  for (size_t i = 0; i < kNumTargets; i++) {
    if (!strcmp(kTargets[i].on_host, on_host) &&
        !strcmp(kTargets[i].run_os, run_os) &&
        !strcmp(kTargets[i].for_hw, for_hw))
      return &kTargets[i];
  }
  return nullptr;
}

inline const MapPortRange* FindMapPort(const char* map_name, const char* dev) {
  for (size_t i = 0; i < kNumMapPorts; i++) {
    if (!strcmp(kMapPorts[i].map_name, map_name) && !strcmp(kMapPorts[i].device, dev)) return &kMapPorts[i];
  }
  return nullptr;
}

inline bool IsCoreDeviceOf(const TargetSpec& tgt, const char* dev) {
  for (size_t i = 0; i < kNumCorePorts; i++) {
    const CorePortRange& p = kCorePorts[i];
    if (!strcmp(p.on_host, tgt.on_host) && !strcmp(p.run_os, tgt.run_os) &&
        !strcmp(p.for_hw, tgt.for_hw) && !strcmp(p.device, dev))
      return true;
  }
  return false;
}

inline bool TargetHasFixedPorts(const TargetSpec& tgt) {
  return !strcmp(tgt.engine, "hamster_multicomp");
}

struct PortsCheck {
  bool ok;
  char msg[220];
};

inline PortsCheck PortsOk() { PortsCheck r; r.ok = true; r.msg[0] = 0; return r; }
inline PortsCheck PortsFail(const char* fmt, const char* a = "", const char* b = "", const char* c = "", const char* d = "") {
  PortsCheck r; r.ok = false; snprintf(r.msg, sizeof r.msg, fmt, a, b, c, d); return r;
}

// Resolve omitted `for <target>` when `run <os>` on `on_host` has a unique target; explain choices if ambiguous.
inline PortsCheck ResolveDefaultFor(const char* on_host, const char* run_os, const char** out_for) {
  const char* found = nullptr;
  char choices[120]; choices[0] = 0;
  int count = 0;
  for (size_t i = 0; i < kNumTargets; i++) {
    if (strcmp(kTargets[i].on_host, on_host) || strcmp(kTargets[i].run_os, run_os)) continue;
    found = kTargets[i].for_hw;
    if (count > 0) strncat(choices, ", ", sizeof(choices) - strlen(choices) - 1);
    strncat(choices, kTargets[i].for_hw, sizeof(choices) - strlen(choices) - 1);
    count++;
  }
  if (count == 1) { *out_for = found; return PortsOk(); }
  if (count > 1) return PortsFail("missing 'for <target>' for '%s' on '%s' (choices: %s)", run_os, on_host, choices);
  return PortsFail("unknown os '%s' on '%s'", run_os, on_host);
}

inline PortsCheck ExplainMissingTarget(const char* on_host, const char* run_os, const char* for_hw) {
  char hosts[64]; hosts[0] = 0;
  for (size_t i = 0; i < kNumTargets; i++) {
    if (!strcmp(kTargets[i].run_os, run_os) && !strcmp(kTargets[i].for_hw, for_hw)) {
      if (hosts[0]) strncat(hosts, ", ", sizeof(hosts) - strlen(hosts) - 1);
      strncat(hosts, kTargets[i].on_host, sizeof(hosts) - strlen(hosts) - 1);
    }
  }
  if (hosts[0]) {
    char pair[64]; snprintf(pair, sizeof pair, "%s for %s", run_os, for_hw);
    return PortsFail("target '%s' is not supported on '%s' (supported on: %s)", pair, on_host, hosts);
  }
  return PortsFail("unknown target '%s for %s' on '%s'", run_os, for_hw, on_host);
}

inline PortsCheck ApplyWithItem(const char words[][PortSettings::kLen], int m, PortSettings& st) {
  if (m <= 0) return PortsOk();
  if (m == 1 && IsMapTagToken(words[0])) {
    if (!st.Set("with", words[0])) return PortsFail("too many settings");
    return PortsOk();
  }
  const char* dev = words[0];
  if (!IsSimpleIdent(dev) || IsReservedGrammarWord(dev))
    return PortsFail("invalid device name '%s' in 'with'", dev);
  if (m == 2 && !strcmp(words[1], "off")) {
    st.UnsetDevice(dev);
    if (!st.Set(dev, "off")) return PortsFail("too many settings");
    return PortsOk();
  }
  if ((m - 1) % 2 != 0)
    return PortsFail("in 'with': device '%s' expects 'off' or '<param> <value>' pairs (e.g. '%s at 0xff41')", dev, dev);
  st.UnsetDevice(dev);
  if (!st.Set(dev, "on")) return PortsFail("too many settings");
  for (int i = 1; i < m; i += 2) {
    const char* param = words[i];
    const char* val = words[i + 1];
    if (!IsSimpleIdent(param) || !strcmp(param, "off"))
      return PortsFail("invalid parameter '%s' for device '%s'", param, dev);
    char kbuf[PortSettings::kLen];
    if (snprintf(kbuf, sizeof kbuf, "%s.%s", dev, param) >= (int)sizeof(kbuf))
      return PortsFail("setting name '%s.%s' is too long", dev, param);
    if (!st.Set(kbuf, val)) return PortsFail("too many settings or value too long for '%s'", kbuf);
  }
  return PortsOk();
}

// Parse `on <host> [run <os> [for <target>] [with ...]]` or `run <os> [for <target>] [with ...]`.
// Transactional: `st` is only updated if parsing succeeds.  `*out_target` receives the resolved TargetSpec
// (or nullptr when `on <host>` was called alone to set the host).
inline PortsCheck ParseOnRunCommand(int argc, const char* const* argv, const char* board_host,
                                    PortSettings& st, const TargetSpec** out_target = nullptr) {
  if (out_target) *out_target = nullptr;
  if (argc < 1) return PortsFail("empty command");
  char cmd[PortSettings::kLen];
  if (!LowerCopy(cmd, sizeof cmd, argv[0])) return PortsFail("command name too long");

  char host_buf[PortSettings::kLen];
  char os_buf[PortSettings::kLen];
  char for_buf[PortSettings::kLen];
  int idx = 1;

  if (!strcmp(cmd, "on")) {
    if (argc < 2) return PortsFail("usage: on <tfr9|coco2|coco3> [run <os> [for <target>] [with ...]]");
    if (!LowerCopy(host_buf, sizeof host_buf, argv[1])) return PortsFail("host name '%s' is too long", argv[1]);
    if (!IsKnownHost(host_buf)) return PortsFail("unknown host '%s' (known: tfr9, coco2, coco3)", host_buf);
    if (argc == 2) {
      st.Clear();
      st.Set("on", host_buf);
      return PortsOk();
    }
    char kw[PortSettings::kLen];
    if (!LowerCopy(kw, sizeof kw, argv[2]) || strcmp(kw, "run") != 0 || argc < 4)
      return PortsFail("usage: on %s run <os> [for <target>] [with ...]", host_buf);
    if (!LowerCopy(os_buf, sizeof os_buf, argv[3])) return PortsFail("os name '%s' is too long", argv[3]);
    idx = 4;
  } else if (!strcmp(cmd, "run")) {
    if (argc < 2) return PortsFail("usage: run <os> [for <target>] [with ...]");
    if (!LowerCopy(host_buf, sizeof host_buf, st.Host(board_host))) return PortsFail("invalid host");
    if (!IsKnownHost(host_buf)) return PortsFail("unknown host '%s' (known: tfr9, coco2, coco3)", host_buf);
    if (!LowerCopy(os_buf, sizeof os_buf, argv[1])) return PortsFail("os name '%s' is too long", argv[1]);
    idx = 2;
  } else {
    return PortsFail("expected 'on' or 'run', got '%s'", cmd);
  }

  const char* for_hw = nullptr;
  if (idx < argc) {
    char kw[PortSettings::kLen];
    if (!LowerCopy(kw, sizeof kw, argv[idx])) return PortsFail("token '%s' is too long", argv[idx]);
    if (!strcmp(kw, "for")) {
      if (idx + 1 >= argc) return PortsFail("missing target after 'for'");
      if (!LowerCopy(for_buf, sizeof for_buf, argv[idx + 1])) return PortsFail("target '%s' is too long", argv[idx + 1]);
      for_hw = for_buf;
      idx += 2;
    }
  }
  if (!for_hw) {
    PortsCheck rc = ResolveDefaultFor(host_buf, os_buf, &for_hw);
    if (!rc.ok) return rc;
  }

  const TargetSpec* tgt = FindTarget(host_buf, os_buf, for_hw);
  if (!tgt) return ExplainMissingTarget(host_buf, os_buf, for_hw);

  PortSettings tmp;
  tmp.Set("on", tgt->on_host);
  tmp.Set("run", tgt->run_os);
  tmp.Set("for", tgt->for_hw);
  tmp.Set("engine", tgt->engine);

  if (idx < argc) {
    char kw[PortSettings::kLen];
    if (!LowerCopy(kw, sizeof kw, argv[idx]) || strcmp(kw, "with") != 0)
      return PortsFail("unexpected '%s': expected 'with'", argv[idx]);
    idx++;
    char words[16][PortSettings::kLen];
    int m = 0, items = 0;
    for (int a = idx; a < argc; a++) {
      const char* p = argv[a];
      while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ',') {
          if (m > 0) {
            PortsCheck rc = ApplyWithItem(words, m, tmp);
            if (!rc.ok) return rc;
            m = 0; items++;
          }
          p++;
          continue;
        }
        if (!*p) break;
        if (m >= 16) return PortsFail("too many tokens in one 'with' item");
        size_t wlen = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != ',') {
          if (wlen + 1 >= PortSettings::kLen) return PortsFail("token in 'with' is too long");
          char c = *p++;
          words[m][wlen++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        words[m][wlen] = 0;
        m++;
      }
    }
    if (m > 0) {
      PortsCheck rc = ApplyWithItem(words, m, tmp);
      if (!rc.ok) return rc;
      items++;
    }
    if (items == 0) return PortsFail("expected options after 'with'");
  }

  st = tmp;
  if (out_target) *out_target = tgt;
  return PortsOk();
}

// Resolve the active TargetSpec of `st` (defaulting to turbo9os on tfr9 when `run` was not set).
inline PortsCheck ResolveActiveTarget(const PortSettings& st, const TargetSpec** out_tgt, const char* default_host = "tfr9") {
  const char* on_h = st.Host(default_host);
  if (!IsKnownHost(on_h)) return PortsFail("unknown host '%s' (known: tfr9, coco2, coco3)", on_h);
  const char* run_o = st.Get("run");
  const char* for_t = st.Get("for");
  if (!run_o) {
    if (!strcmp(on_h, "tfr9")) { run_o = "turbo9os"; if (!for_t) for_t = "turbo9sim"; }
    else return PortsFail("no 'run <os>' configured for host '%s'", on_h);
  } else if (!for_t) {
    PortsCheck rc = ResolveDefaultFor(on_h, run_o, &for_t);
    if (!rc.ok) return rc;
  }
  const TargetSpec* tgt = FindTarget(on_h, run_o, for_t);
  if (!tgt) return ExplainMissingTarget(on_h, run_o, for_t);
  const char* eng = st.Get("engine");
  if (eng && strcmp(eng, tgt->engine) != 0)
    return PortsFail("engine '%s' does not match '%s' implied by '%s/%s'", eng, tgt->engine, run_o, for_t);
  *out_tgt = tgt;
  return PortsOk();
}

// Base address of a core device of the active target under `st`.  Falls back to `dflt` if ports.tsv has no row.
inline uint16_t PortBase(const char* device, uint16_t dflt, const PortSettings& st = g_port_settings) {
  char at_key[PortSettings::kLen];
  snprintf(at_key, sizeof at_key, "%s.at", device);
  const char* ov = st.Get(at_key);
  uint16_t a;
  if (ov && ParsePortAddr(ov, &a)) return a;
  const TargetSpec* tgt = nullptr;
  if (ResolveActiveTarget(st, &tgt).ok && tgt) {
    for (size_t i = 0; i < kNumCorePorts; i++) {
      const CorePortRange& p = kCorePorts[i];
      if (!strcmp(p.on_host, tgt->on_host) && !strcmp(p.run_os, tgt->run_os) &&
          !strcmp(p.for_hw, tgt->for_hw) && !strcmp(p.device, device))
        return p.first;
    }
  }
  return dflt;
}

inline PortsCheck ValidateDeviceParam(const char* dev, const char* param, const char* val) {
  if (!strcmp(dev, "dw")) {
    if (!strcmp(param, "over")) {
      if (!strcmp(val, "bitbang") || !strcmp(val, "uart") || !strcmp(val, "usb")) return PortsOk();
      return PortsFail("dw.over: unknown transport '%s' (known: bitbang, uart, usb)", val);
    }
    if (!strcmp(param, "baud")) {
      uint32_t b = 0;
      if (ParsePositiveInt(val, &b)) return PortsOk();
      return PortsFail("dw.baud: '%s' is not a positive baud rate", val);
    }
    return PortsFail("unknown option '%s' for device 'dw'", param);
  }
  if (!strcmp(dev, "becker") && !strcmp(param, "over")) {
    if (!strcmp(val, "usb") || !strcmp(val, "uart") || !strcmp(val, "hat")) return PortsOk();
    return PortsFail("becker.over: unknown transport '%s' (known: usb, uart, hat)", val);
  }
  return PortsFail("unknown option '%s' for device '%s'", param, dev);
}

inline PortsCheck FormatClaimFailure(const char* dev, uint16_t base, uint16_t last, const IoMap::Result& r) {
  PortsCheck c; c.ok = false;
  if (r.owner) snprintf(c.msg, sizeof c.msg, "%s (0x%04x-0x%04x) conflicts with %s at 0x%04x", dev, base, last, r.owner, r.addr);
  else         snprintf(c.msg, sizeof c.msg, "%s (0x%04x-0x%04x) may not use 0x%04x (forbidden: Multi-Pak select)", dev, base, last, r.addr);
  return c;
}

// Validate `st` and populate `map`.  When `include_host_reserved` is true (default, used by `ports check` and `bye`),
// CoCo motherboard reserved ranges are claimed first so collisions with PIA/SAM/CoCo3-GIME are caught.
// When false (used by IoFinish), `map` contains only the ranges the RP2350 itself installs in IOReaders/IOWriters.
inline PortsCheck PortsValidate(const PortSettings& st, IoMap& map, bool include_host_reserved = true,
                                const char* default_host = "tfr9") {
  map.Reset();
  const TargetSpec* tgt = nullptr;
  PortsCheck rc = ResolveActiveTarget(st, &tgt, default_host);
  if (!rc.ok) return rc;

  const char* wmap = st.WithMap();
  if (!IsKnownEnhancementMap(wmap)) return PortsFail("unknown enhancement map '%s' (known: e1)", wmap);

  // 1. Host motherboard reserved ranges (coco2 PIA/SAM; coco3 PIA/SAM + GIME/vectors under gerbil_io).
  if (include_host_reserved) {
    ClaimReport hr = ClaimHostReserved(map, kHostReserved, kNumHostReserved, tgt->on_host, tgt->engine);
    if (!hr.ok) return PortsFail("internal host table conflict on %s (%s)", tgt->on_host, hr.device);
  }

  // 2. Core RP2350-claimed devices of (on, run, for).
  for (size_t i = 0; i < kNumCorePorts; i++) {
    const CorePortRange& p = kCorePorts[i];
    if (strcmp(p.on_host, tgt->on_host) || strcmp(p.run_os, tgt->run_os) || strcmp(p.for_hw, tgt->for_hw)) continue;
    const char* dv = st.Get(p.device);
    if (dv) {
      if (!strcmp(dv, "off")) return PortsFail("core device %s of %s/%s cannot be turned off", p.device, tgt->run_os, tgt->for_hw);
      if (strcmp(dv, "on") != 0)
        return PortsFail("device '%s' value must be 'on' or 'off' (use '%s.at 0xff48' for an address)", p.device, p.device);
    }
    uint16_t base = p.first;
    unsigned size = (unsigned)p.last - p.first + 1;
    char at_key[PortSettings::kLen];
    snprintf(at_key, sizeof at_key, "%s.at", p.device);
    const char* ov = st.Get(at_key);
    if (ov) {
      if (!strcmp(p.device, "vectors")) return PortsFail("the vectors (0xfff0-0xffff) cannot be moved");
      if (TargetHasFixedPorts(*tgt)) return PortsFail("%s for %s decodes its own ports and cannot be moved (%s)", tgt->run_os, tgt->for_hw, p.device);
      if (!ParsePortAddr(ov, &base)) return PortsFail("%s: '%s' is not an address like 0xff48", at_key, ov);
    }
    if ((uint32_t)base + size - 1 > 0xFFFF) return PortsFail("%s would run past 0xffff", p.device);
    IoMap::Result r = map.Claim(base, (uint16_t)(base + size - 1), p.device);
    if (!r.ok) return FormatClaimFailure(p.device, base, (uint16_t)(base + size - 1), r);
  }

  // 3. Validate every key in `st` so unknown devices, unknown options, or bad values are rejected.
  for (int k = 0; k < st.n_; k++) {
    const char* key = st.key_[k];
    const char* val = st.val_[k];
    if (!strcmp(key, "on") || !strcmp(key, "run") || !strcmp(key, "for") ||
        !strcmp(key, "engine") || !strcmp(key, "with"))
      continue;
    char dev[PortSettings::kLen];
    const char* dot = strchr(key, '.');
    const char* param = nullptr;
    if (dot) {
      size_t dlen = (size_t)(dot - key);
      if (dlen == 0 || dlen >= sizeof(dev) || !dot[1]) return PortsFail("invalid setting key '%s'", key);
      memcpy(dev, key, dlen); dev[dlen] = 0;
      param = dot + 1;
    } else {
      strcpy(dev, key);
    }

    bool is_core = IsCoreDeviceOf(*tgt, dev);
    bool is_dw = !strcmp(dev, "dw");
    const MapPortRange* mp = FindMapPort(wmap, dev);
    if (!is_core && !is_dw && !mp)
      return PortsFail("'%s' is not a device of %s/%s or a known enhancement", dev, tgt->run_os, tgt->for_hw);

    if (!param) {
      if (strcmp(val, "on") != 0 && strcmp(val, "off") != 0)
        return PortsFail("device '%s' value must be 'on' or 'off' (use '%s.at 0xff48' for an address)", dev, dev);
      continue;
    }
    if (!strcmp(param, "at")) {
      if (is_dw) return PortsFail("device 'dw' is a transport, not a memory-mapped port");
      uint16_t dummy;
      if (!ParsePortAddr(val, &dummy)) return PortsFail("%s: '%s' is not an address like 0xff48", key, val);
      continue;
    }
    PortsCheck prc = ValidateDeviceParam(dev, param, val);
    if (!prc.ok) return prc;
  }

  // 4. Claim enabled memory-mapped enhancements from `wmap` (skipping any device that is already core on `tgt`).
  for (size_t i = 0; i < kNumMapPorts; i++) {
    const MapPortRange& e = kMapPorts[i];
    if (strcmp(e.map_name, wmap)) continue;
    if (IsCoreDeviceOf(*tgt, e.device)) continue;
    const char* dv = st.Get(e.device);
    char at_key[PortSettings::kLen];
    snprintf(at_key, sizeof at_key, "%s.at", e.device);
    const char* ov = st.Get(at_key);
    bool enabled = (dv && !strcmp(dv, "on")) || (!dv && ov != nullptr);
    if (!enabled) continue;
    uint16_t base = e.first;
    if (ov && !ParsePortAddr(ov, &base)) return PortsFail("%s: '%s' is not an address like 0xff48", at_key, ov);
    unsigned size = (unsigned)e.last - e.first + 1;
    if ((uint32_t)base + size - 1 > 0xFFFF) return PortsFail("%s would run past 0xffff", e.device);
    IoMap::Result r = map.Claim(base, (uint16_t)(base + size - 1), e.device);
    if (!r.ok) return FormatClaimFailure(e.device, base, (uint16_t)(base + size - 1), r);
  }

  return PortsOk();
}
