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

static const char* kEnh[] = {"becker", "audio", "rtc", "orchestra90", "info"};

static void TestIoMapBasics() {
  IoMap m;
  CHECK(m.Claim(0xFF80, 0xFF86, "emudsk").ok, "first claim");
  IoMap::Result r = m.Claim(0xFF86, 0xFF88, "acia");
  CHECK(!r.ok && r.addr == 0xFF86 && !strcmp(r.owner, "emudsk"), "overlap names the owner");
  CHECK(m.OwnerOf(0xFF87) == nullptr, "a failed claim claims nothing (all-or-nothing)");
  CHECK(!m.Claim(0xFF7F, 0xFF7F, "x").ok, "0xff7f alone is refused");
  CHECK(!m.Claim(0xFF70, 0xFF80, "x").ok && m.OwnerOf(0xFF70) == nullptr, "a range containing 0xff7f is refused, nothing claimed");
  CHECK(!m.Claim(0xFE00, 0xFE01, "x").ok, "outside the I/O page is refused");
  CHECK(!m.Claim(0xFF90, 0xFF8F, "x").ok, "first > last is refused");
  CHECK(m.Claim(0xFFF0, 0xFFFF, "vectors").ok, "top of the page is claimable");
}

static void TestTableIsSane() {
  for (size_t i = 0; i < kNumCorePorts; i++) {
    CHECK(kCorePorts[i].first >= 0xFF00 && kCorePorts[i].first <= kCorePorts[i].last, "core row %zu range", i);
    CHECK(!(kCorePorts[i].first <= 0xFF7F && kCorePorts[i].last >= 0xFF7F), "core row %zu (%s) touches 0xff7f", i, kCorePorts[i].device);
  }
  for (size_t i = 0; i < kNumHostReserved; i++) {
    CHECK(kHostReserved[i].first >= 0xFF00 && kHostReserved[i].first <= kHostReserved[i].last, "host row %zu range", i);
    CHECK(!(kHostReserved[i].first <= 0xFF7F && kHostReserved[i].last >= 0xFF7F), "host row %zu (%s) touches 0xff7f", i, kHostReserved[i].device);
  }
  for (size_t i = 0; i < kNumMapPorts; i++) {
    CHECK(kMapPorts[i].first >= 0xFF00 && kMapPorts[i].first <= kMapPorts[i].last, "map row %zu range", i);
    CHECK(!(kMapPorts[i].first <= 0xFF7F && kMapPorts[i].last >= 0xFF7F), "map row %zu (%s) touches 0xff7f", i, kMapPorts[i].device);
  }
}

// Every declared target: host motherboard reserved ranges + core RP2350 ports must never overlap.
static void TestCoreAndHostNeverOverlap() {
  for (size_t i = 0; i < kNumTargets; i++) {
    const TargetSpec& t = kTargets[i];
    IoMap m;
    ClaimReport hr = ClaimHostReserved(m, kHostReserved, kNumHostReserved, t.on_host, t.engine);
    CHECK(hr.ok, "%s/%s/%s: host reserved %s overlaps %s at 0x%04x", t.on_host, t.run_os, t.for_hw, hr.device, hr.owner ? hr.owner : "?", hr.addr);
    ClaimReport cr = ClaimCorePorts(m, kCorePorts, kNumCorePorts, t.on_host, t.run_os, t.for_hw);
    CHECK(cr.ok, "%s/%s/%s: core device %s overlaps %s at 0x%04x", t.on_host, t.run_os, t.for_hw, cr.device, cr.owner ? cr.owner : "?", cr.addr);
  }
}

// Enhancements in map e1: skipping devices already built into a target (jamesbrown), the ONLY clashes across all 23
// targets are tfr911h/hatvan/gep9 and tfr911h/hatvan/gep9big (embiggen_mmu 0xff40-47) vs becker (0xff41-42).
static void TestEnhancementsFitEverywhere() {
  int conflicts = 0;
  for (size_t i = 0; i < kNumTargets; i++) {
    const TargetSpec& t = kTargets[i];
    for (const char* enh : kEnh) {
      bool already_core = false;
      for (size_t j = 0; j < kNumCorePorts; j++) {
        if (!strcmp(kCorePorts[j].on_host, t.on_host) && !strcmp(kCorePorts[j].run_os, t.run_os) &&
            !strcmp(kCorePorts[j].for_hw, t.for_hw) && !strcmp(kCorePorts[j].device, enh))
          already_core = true;
      }
      if (already_core) continue;
      IoMap m;
      ClaimHostReserved(m, kHostReserved, kNumHostReserved, t.on_host, t.engine);
      ClaimCorePorts(m, kCorePorts, kNumCorePorts, t.on_host, t.run_os, t.for_hw);
      ClaimReport r = ClaimEnhancement(m, kMapPorts, kNumMapPorts, "e1", enh);
      if (!r.ok) {
        conflicts++;
        bool is_gep = !strcmp(t.on_host, "tfr911h") && !strcmp(t.run_os, "hatvan") &&
                      (!strcmp(t.for_hw, "gep9") || !strcmp(t.for_hw, "gep9big"));
        bool expected = is_gep && !strcmp(enh, "becker");
        CHECK(expected, "unexpected conflict: %s/%s/%s + %s at 0x%04x (owner %s)",
              t.on_host, t.run_os, t.for_hw, enh, r.addr, r.owner ? r.owner : "?");
      }
    }
  }
  CHECK(conflicts == 2, "expected exactly 2 known conflicts (gep9 and gep9big + becker), saw %d", conflicts);
}

// CoCo 2 vs CoCo 3 GIME/MMU rule: `on coco2 run nitros9l2 for coco3` simulates GIME/MMU/vectors (`gerbil_gime`),
// whereas `on coco3 run nitros9l2 for coco3` uses the real CoCo 3 motherboard (`gerbil_io`) and claims ONLY cart I/O.
static void TestCoco2VsCoco3GimeOwnership() {
  IoMap m2, m3;
  ClaimCorePorts(m2, kCorePorts, kNumCorePorts, "coco2", "nitros9l2", "coco3");
  ClaimCorePorts(m3, kCorePorts, kNumCorePorts, "coco3", "nitros9l2", "coco3");
  CHECK(m2.OwnerOf(0xFF90) && !strcmp(m2.OwnerOf(0xFF90), "gime"), "coco2 simulates GIME");
  CHECK(m2.OwnerOf(0xFFA0) && !strcmp(m2.OwnerOf(0xFFA0), "mmu"), "coco2 simulates MMU");
  CHECK(m2.OwnerOf(0xFFF0) && !strcmp(m2.OwnerOf(0xFFF0), "vectors"), "coco2 simulates vectors");
  CHECK(m3.OwnerOf(0xFF90) == nullptr, "coco3 leaves GIME to real CoCo 3 motherboard");
  CHECK(m3.OwnerOf(0xFFA0) == nullptr, "coco3 leaves MMU to real CoCo 3 motherboard");
  CHECK(m3.OwnerOf(0xFFF0) == nullptr, "coco3 leaves vectors to real CoCo 3 motherboard");
  CHECK(m3.OwnerOf(0xFF80) && !strcmp(m3.OwnerOf(0xFF80), "emudsk"), "coco3 still claims emudsk");
  CHECK(m3.OwnerOf(0xFF88) && !strcmp(m3.OwnerOf(0xFF88), "acia"), "coco3 still claims acia");
}

// Golden addresses for existing guest images and Diller's jamesbrown L2.
static void TestGoldenAddresses() {
  struct G { const char* on_h; const char* run_o; const char* for_t; const char* dev; uint16_t first, last; };
  static const G golden[] = {
    {"tfr911h", "turbo9os",  "turbo9sim",   "turbo9sim_acia", 0xFF00, 0xFF03},
    {"tfr911h", "turbolab",  "turbo9sim",   "turbo9sim_acia", 0xFF00, 0xFF03},
    {"tfr911h", "turbolab",  "turbo9sim",   "vectors",        0xFFF0, 0xFFFF},
    {"tfr911h", "nitros9l1", "flat65280",   "emudsk",         0xFF80, 0xFF86},
    {"tfr911h", "nitros9l1", "flat65280",   "clockstop",      0xFF87, 0xFF87},
    {"tfr911h", "nitros9l1", "flat65280",   "acia",           0xFF88, 0xFF89},
    {"tfr911h", "nitros9l2", "deep65280",   "gime",           0xFF90, 0xFF91},
    {"tfr911h", "nitros9l2", "deep65280",   "mmu",            0xFFA0, 0xFFAF},
    {"tfr911h", "nitros9l2", "jamesbrown",  "acia",           0xFF14, 0xFF15},
    {"tfr911h", "nitros9l2", "jamesbrown",  "audio",          0xFF41, 0xFF42},
    {"tfr911h", "nitros9l2", "jamesbrown",  "info",           0xFF43, 0xFF44},
    {"tfr911h", "nitros9l2", "jamesbrown",  "becker",         0xFF45, 0xFF46},
    {"tfr911h", "nitros9l2", "jamesbrown",  "emudsk",         0xFF80, 0xFF86},
    {"tfr911h", "nitros9l2", "jamesbrown",  "gime",           0xFF90, 0xFF93},
    {"tfr911h", "nitros9l2", "jamesbrown",  "mmu",            0xFFA0, 0xFFAF},
    {"tfr911h", "nitros9l2", "jamesbrown",  "rtc",            0xFFC0, 0xFFC7},
    {"tfr911h", "hatvan",    "gep9",        "console",        0xFF00, 0xFF05},
    {"tfr911h", "hatvan",    "gep9",        "disk",           0xFF10, 0xFF1B},
    {"tfr911h", "hatvan",    "gep9",        "fuse_dma",       0xFF20, 0xFF2F},
    {"tfr911h", "hatvan",    "gep9",        "task_sec",       0xFF30, 0xFF36},
    {"tfr911h", "hatvan",    "gep9",        "embiggen_mmu",   0xFF40, 0xFF47},
    {"tfr911h", "hatvan",    "gep9big",     "disk",           0xFF10, 0xFF1B},
    {"tfr911h", "hatvan",    "gep9big",     "task_sec",       0xFF30, 0xFF36},
    {"tfr911h", "hatvan",    "gep9big",     "embiggen_mmu",   0xFF40, 0xFF47},
    {"tfr911h", "hatvan",    "coco2",       "disk",           0xFF50, 0xFF5B},
    {"tfr911h", "hatvan",    "coco2",       "console",        0xFF60, 0xFF65},
    {"tfr911h", "hatvan",    "coco2",       "fuse_dma",       0xFF80, 0xFF8F},
    {"tfr911h", "hatvan",    "coco2",       "embiggen_mmu",   0xFFE0, 0xFFE7},
    {"tfr911h", "hatvan",    "coco2",       "task_sec",       0xFFE8, 0xFFEE},
    {"tfr911h", "fuzix",     "multicomp09", "acia",           0xFFD0, 0xFFD1},
    {"tfr911h", "fuzix",     "multicomp09", "sd",             0xFFD8, 0xFFDC},
    {"tfr911h", "fuzix",     "multicomp09", "mmu",            0xFFDE, 0xFFDF},
  };
  for (const auto& g : golden) {
    bool found = false;
    for (size_t i = 0; i < kNumCorePorts; i++) {
      const CorePortRange& p = kCorePorts[i];
      if (!strcmp(p.on_host, g.on_h) && !strcmp(p.run_os, g.run_o) &&
          !strcmp(p.for_hw, g.for_t) && !strcmp(p.device, g.dev)) {
        found = true;
        CHECK(p.first == g.first && p.last == g.last, "%s/%s/%s %s is 0x%04x-0x%04x, want 0x%04x-0x%04x",
              g.on_h, g.run_o, g.for_t, g.dev, p.first, p.last, g.first, g.last);
      }
    }
    CHECK(found, "%s/%s/%s %s missing", g.on_h, g.run_o, g.for_t, g.dev);
  }
}

int main() {
  TestIoMapBasics();
  TestTableIsSane();
  TestCoreAndHostNeverOverlap();
  TestEnhancementsFitEverywhere();
  TestCoco2VsCoco3GimeOwnership();
  TestGoldenAddresses();
  if (failures) { printf("iomap_test: %d FAILURES\n", failures); return 1; }
  printf("iomap_test: OK (%zu targets, %zu core rows, %zu map rows)\n", kNumTargets, kNumCorePorts, kNumMapPorts);
  return 0;
}
