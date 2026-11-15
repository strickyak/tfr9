// Host test: the I/O address plan (firmware/engines/ports.tsv) and the IoMap claim registry.
// Run with `make test-host`.  Encodes the rules of spec section 7.2 as executable checks.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "iomap.h"
#include "ports_gen.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static std::vector<std::string> Engines() {
  std::vector<std::string> v;
  for (size_t i = 0; i < kNumPorts; i++) {
    if (!strcmp(kPorts[i].engine, "*")) continue;
    bool seen = false;
    for (auto& e : v) if (e == kPorts[i].engine) seen = true;
    if (!seen) v.push_back(kPorts[i].engine);
  }
  return v;
}
static const char* kEnh[] = {"becker", "audio", "rtc", "orchestra90", "info"};
static const char* kProfiles[] = {"TFR4", "COCO"};

static void TestIoMapBasics() {
  IoMap m;
  CHECK(m.Claim(0xFF80, 0xFF86, "emudsk").ok, "first claim");
  IoMap::Result r = m.Claim(0xFF86, 0xFF88, "acia");
  CHECK(!r.ok && r.addr == 0xFF86 && !strcmp(r.owner, "emudsk"), "overlap names the owner");
  CHECK(m.OwnerOf(0xFF87) == nullptr, "a failed claim claims nothing (all-or-nothing)");
  CHECK(!m.Claim(0xFF7F, 0xFF7F, "x").ok, "$FF7F alone is refused");
  CHECK(!m.Claim(0xFF70, 0xFF80, "x").ok && m.OwnerOf(0xFF70) == nullptr, "a range containing $FF7F is refused, nothing claimed");
  CHECK(!m.Claim(0xFE00, 0xFE01, "x").ok, "outside the I/O page is refused");
  CHECK(!m.Claim(0xFF90, 0xFF8F, "x").ok, "first > last is refused");
  CHECK(m.Claim(0xFFF0, 0xFFFF, "vectors").ok, "top of the page is claimable");
}

static void TestTableIsSane() {
  for (size_t i = 0; i < kNumPorts; i++) {
    CHECK(kPorts[i].first >= 0xFF00 && kPorts[i].first <= kPorts[i].last, "row %zu range", i);
    CHECK(!(kPorts[i].first <= 0xFF7F && kPorts[i].last >= 0xFF7F), "row %zu (%s) touches $FF7F", i, kPorts[i].device);
  }
}

// Every engine under every profile it supports: core ports must not overlap each other.
static void TestCoreNeverOverlaps() {
  for (auto& e : Engines())
    for (auto p : kProfiles) {
      if (!PortsSupported(kPorts, kNumPorts, e.c_str(), p)) continue;
      IoMap m;
      ClaimReport r = ClaimCorePorts(m, kPorts, kNumPorts, e.c_str(), p);
      CHECK(r.ok, "%s/%s: core device %s overlaps %s at $%04X", e.c_str(), p, r.device, r.owner ? r.owner : "?", r.addr);
    }
}

// Enhancements: the ONLY allowed conflict is gep9big/TFR4 (embiggen MMU $FF40-47) vs becker ($FF41/42).
static void TestEnhancementsFitEverywhere() {
  int conflicts = 0;
  for (auto& e : Engines())
    for (auto p : kProfiles) {
      if (!PortsSupported(kPorts, kNumPorts, e.c_str(), p)) continue;
      for (auto enh : kEnh) {
        IoMap m;
        ClaimCorePorts(m, kPorts, kNumPorts, e.c_str(), p);
        ClaimReport r = ClaimEnhancement(m, kPorts, kNumPorts, "E1", enh);
        if (!r.ok) {
          conflicts++;
          bool expected = !strcmp(e.c_str(), "gep9big") && !strcmp(p, "TFR4") && !strcmp(enh, "becker");
          CHECK(expected, "unexpected conflict: %s/%s + %s at $%04X (owner %s)", e.c_str(), p, enh, r.addr, r.owner ? r.owner : "?");
        }
      }
    }
  CHECK(conflicts == 1, "expected exactly 1 known conflict (gep9big/TFR4 + becker), saw %d", conflicts);
}

// The COCO profile must stay off hardware a real CoCo owns (spec 7.2 rule 3), except for devices that
// emulate that very chip (GIME/MMU) and the vectors.
static void TestCocoProfileAvoidsRealHardware() {
  for (size_t i = 0; i < kNumPorts; i++) {
    if (strcmp(kPorts[i].profile, "COCO")) continue;
    const char* d = kPorts[i].device;
    if (!strcmp(d, "gime") || !strcmp(d, "mmu") || !strcmp(d, "vectors")) continue;
    uint16_t a = kPorts[i].first, b = kPorts[i].last;
    auto overlaps = [&](uint16_t lo, uint16_t hi) { return a <= hi && b >= lo; };
    CHECK(!overlaps(0xFF00, 0xFF3F), "COCO %s/%s sits on the PIAs ($FF00-3F)", kPorts[i].engine, d);
    CHECK(!overlaps(0xFF40, 0xFF5F), "COCO %s/%s sits in the cartridge SCS window ($FF40-5F)", kPorts[i].engine, d);
    CHECK(!overlaps(0xFF90, 0xFFBF), "COCO %s/%s sits on the GIME ($FF90-BF)", kPorts[i].engine, d);
    CHECK(!overlaps(0xFFC0, 0xFFDF), "COCO %s/%s sits on the SAM ($FFC0-DF)", kPorts[i].engine, d);
  }
}

// TFR4 must reproduce v4's addresses exactly (existing images depend on them).
static void TestTfr4MatchesV4() {
  struct G { const char* engine; const char* device; uint16_t first, last; };
  static const G golden[] = {
    {"flat65280", "emudsk", 0xFF80, 0xFF86}, {"flat65280", "clockstop", 0xFF87, 0xFF87}, {"flat65280", "acia", 0xFF88, 0xFF89},
    {"deep65280", "emudsk", 0xFF80, 0xFF86}, {"deep65280", "acia", 0xFF88, 0xFF89}, {"deep65280", "gime", 0xFF90, 0xFF91},
    {"deep65280", "mmu", 0xFFA0, 0xFFAF}, {"turbo9os", "turbo9sim_acia", 0xFF00, 0xFF03},
    {"gep9", "console", 0xFF00, 0xFF05}, {"gep9", "disk", 0xFF10, 0xFF17}, {"gep9", "fuse_dma", 0xFF20, 0xFF2F},
    {"multicomp09fuzix", "acia", 0xFFD0, 0xFFD1}, {"multicomp09fuzix", "sd", 0xFFD8, 0xFFDC}, {"multicomp09fuzix", "mmu", 0xFFDE, 0xFFDF},
  };
  for (auto& g : golden) {
    bool found = false;
    for (size_t i = 0; i < kNumPorts; i++)
      if (!strcmp(kPorts[i].profile, "TFR4") && !strcmp(kPorts[i].engine, g.engine) && !strcmp(kPorts[i].device, g.device)) {
        found = true;
        CHECK(kPorts[i].first == g.first && kPorts[i].last == g.last, "TFR4 %s/%s is $%04X-$%04X, v4 had $%04X-$%04X",
              g.engine, g.device, kPorts[i].first, kPorts[i].last, g.first, g.last);
      }
    CHECK(found, "TFR4 %s/%s missing", g.engine, g.device);
  }
}

int main() {
  TestIoMapBasics();
  TestTableIsSane();
  TestCoreNeverOverlaps();
  TestEnhancementsFitEverywhere();
  TestCocoProfileAvoidsRealHardware();
  TestTfr4MatchesV4();
  if (failures) { printf("iomap_test: %d FAILURES\n", failures); return 1; }
  printf("iomap_test: OK (%zu port rows)\n", kNumPorts);
  return 0;
}
