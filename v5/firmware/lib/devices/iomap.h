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

struct ClaimReport {
  bool ok;
  uint16_t addr;
  const char* device;  // the row that failed
  const char* owner;   // the earlier owner, or nullptr (forbidden address)
};

// Claim host motherboard reserved ranges (e.g. coco_pia, coco_sam, coco3_gime).
template <typename HostRow>
inline ClaimReport ClaimHostReserved(IoMap& m, const HostRow* t, size_t n, const char* on_host, const char* engine) {
  for (size_t i = 0; i < n; i++) {
    if (strcmp(t[i].on_host, on_host)) continue;
    if (strcmp(t[i].engine, "*") && strcmp(t[i].engine, engine)) continue;
    IoMap::Result r = m.Claim(t[i].first, t[i].last, t[i].device);
    if (!r.ok) return {false, r.addr, t[i].device, r.owner};
  }
  return {true, 0, nullptr, nullptr};
}

// Claim every core port row of (on_host, run_os, for_hw).
template <typename PortRow>
inline ClaimReport ClaimCorePorts(IoMap& m, const PortRow* t, size_t n,
                                  const char* on_host, const char* run_os, const char* for_hw) {
  for (size_t i = 0; i < n; i++) {
    if (strcmp(t[i].on_host, on_host) || strcmp(t[i].run_os, run_os) || strcmp(t[i].for_hw, for_hw)) continue;
    IoMap::Result r = m.Claim(t[i].first, t[i].last, t[i].device);
    if (!r.ok) return {false, r.addr, t[i].device, r.owner};
  }
  return {true, 0, nullptr, nullptr};
}

// Claim one enhancement from `map_name` (e.g. "e1") by device name.
template <typename MapRow>
inline ClaimReport ClaimEnhancement(IoMap& m, const MapRow* t, size_t n, const char* map_name, const char* device) {
  for (size_t i = 0; i < n; i++) {
    if (strcmp(t[i].map_name, map_name) || strcmp(t[i].device, device)) continue;
    IoMap::Result r = m.Claim(t[i].first, t[i].last, t[i].device);
    return {r.ok, r.addr, t[i].device, r.owner};
  }
  return {false, 0, device, nullptr};  // no such enhancement
}
