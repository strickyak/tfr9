#pragma once
// iomap.h -- who owns each address in the 6809 I/O page $FF00-$FFFF (spec sections 5.3, 7).
// Pure C++17, no Pico SDK, no heap: it compiles on the PC for tests and runs at engine Setup() on core 0.
// Claim() refuses overlap (naming both owners) and ALWAYS refuses $FF7F (Multi-Pak select).
#include <cstddef>
#include <cstdint>
#include <cstring>

class IoMap {
 public:
  static constexpr uint16_t kPageBase = 0xFF00;
  static constexpr uint16_t kForbidden = 0xFF7F;
  struct Result {
    bool ok;
    uint16_t addr;       // the offending address when !ok
    const char* owner;   // who already holds it (nullptr if the address is forbidden/out of range)
  };

  IoMap() { Reset(); }
  void Reset() { for (size_t i = 0; i < 256; i++) owner_[i] = nullptr; }

  // Claim first..last (inclusive) for `name`.  All-or-nothing.
  Result Claim(uint16_t first, uint16_t last, const char* name) {
    if (first < kPageBase || last < first) return {false, first, nullptr};
    for (uint32_t a = first; a <= last; a++) {
      if (a == kForbidden) return {false, static_cast<uint16_t>(a), nullptr};
      if (owner_[a & 0xFF]) return {false, static_cast<uint16_t>(a), owner_[a & 0xFF]};
    }
    for (uint32_t a = first; a <= last; a++) owner_[a & 0xFF] = name;
    return {true, first, nullptr};
  }
  const char* OwnerOf(uint16_t a) const { return a >= kPageBase ? owner_[a & 0xFF] : nullptr; }

 private:
  const char* owner_[256];
};

// ---- helpers over the generated table (build/generated/ports_gen.h, from firmware/engines/ports.tsv) ----
// These take the table as parameters so tests can pass their own.
template <typename Row>
inline bool PortsSupported(const Row* t, size_t n, const char* engine, const char* profile) {
  for (size_t i = 0; i < n; i++)
    if (!strcmp(t[i].engine, engine) && !strcmp(t[i].profile, profile)) return true;
  return false;
}

struct ClaimReport {
  bool ok;
  uint16_t addr;
  const char* device;  // the row that failed
  const char* owner;   // the earlier owner, or nullptr (forbidden address)
};

// Claim every row of (engine, profile) -- the engine's core devices.
template <typename Row>
inline ClaimReport ClaimCorePorts(IoMap& m, const Row* t, size_t n, const char* engine, const char* profile) {
  for (size_t i = 0; i < n; i++) {
    if (strcmp(t[i].engine, engine) || strcmp(t[i].profile, profile)) continue;
    IoMap::Result r = m.Claim(t[i].first, t[i].last, t[i].device);
    if (!r.ok) return {false, r.addr, t[i].device, r.owner};
  }
  return {true, 0, nullptr, nullptr};
}

// Claim one enhancement (a row of profile `emap`, engine "*") by device name.
template <typename Row>
inline ClaimReport ClaimEnhancement(IoMap& m, const Row* t, size_t n, const char* emap, const char* device) {
  for (size_t i = 0; i < n; i++) {
    if (strcmp(t[i].profile, emap) || strcmp(t[i].engine, "*") || strcmp(t[i].device, device)) continue;
    IoMap::Result r = m.Claim(t[i].first, t[i].last, t[i].device);
    return {r.ok, r.addr, t[i].device, r.owner};
  }
  return {false, 0, device, nullptr};  // no such enhancement
}
