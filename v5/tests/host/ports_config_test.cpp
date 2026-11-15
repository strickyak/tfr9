// Host test for firmware/lib/devices/ports_config.h: profiles, moved and added devices, and conflict reporting.
#include <cstdio>
#include <cstring>
#include <string>
#include "ports_config.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static PortsCheck Run(const char* engine, std::initializer_list<std::pair<const char*, const char*>> kv) {
  PortSettings st; st.Clear();
  for (auto& p : kv) st.Set(p.first, p.second);
  IoMap m;
  return PortsValidate(engine, st, m);
}
static bool Has(const PortsCheck& c, const char* s) { return strstr(c.msg, s) != nullptr; }

int main() {
  // 1. The built-in plan is self-consistent for every (engine, profile) it lists.
  for (size_t i = 0; i < kNumPorts; i++) {
    if (!strcmp(kPorts[i].profile, "E1")) continue;
    PortsCheck c = Run(kPorts[i].engine, {{"profile", kPorts[i].profile}});
    CHECK(c.ok, "default %s/%s should validate: %s", kPorts[i].engine, kPorts[i].profile, c.msg);
  }

  // 2. The known conflict: gep9 and gep9big on TFR4 have embiggen_mmu at $FF40-47; Becker's default is $FF41-42.
  for (const char* eng : {"gep9", "gep9big"}) {
    { PortsCheck c = Run(eng, {{"becker", "0xFF41"}});
      CHECK(!c.ok && Has(c, "becker") && Has(c, "embiggen_mmu"), "%s: expected a becker/embiggen_mmu conflict, got ok=%d '%s'", eng, c.ok, c.msg); }
    // ...and the fix chosen in spec 15.x: move Becker (or use COCO).
    { PortsCheck c = Run(eng, {{"becker", "0xFF48"}}); CHECK(c.ok, "%s: becker at $FF48 should be fine: %s", eng, c.msg); }
    { PortsCheck c = Run(eng, {{"profile", "COCO"}, {"becker", "0xFF41"}}); CHECK(c.ok, "%s: COCO profile has no clash at $FF41: %s", eng, c.msg); }
  }

  // 3. $FF7F is never allowed.
  { PortsCheck c = Run("flat65280", {{"becker", "0xFF7F"}}); CHECK(!c.ok && Has(c, "Multi-Pak"), "$FF7F must be refused: '%s'", c.msg); }
  { PortsCheck c = Run("flat65280", {{"becker", "0xFF7E"}}); CHECK(!c.ok && Has(c, "Multi-Pak"), "$FF7E-7F spans $FF7F: '%s'", c.msg); }

  // 4. Moving a core device onto another one names both.
  { PortsCheck c = Run("deep65280", {{"emudsk", "0xFF90"}}); CHECK(!c.ok && Has(c, "emudsk") && Has(c, "gime"), "emudsk onto gime: '%s'", c.msg); }
  { PortsCheck c = Run("flat65280", {{"emudsk", "0xFF20"}}); CHECK(c.ok, "emudsk moved to free space: %s", c.msg); }

  // 5. Things that are refused outright.
  { PortsCheck c = Run("flat65280", {{"vectors", "0xFFE0"}}); CHECK(!c.ok && Has(c, "vectors"), "vectors fixed: '%s'", c.msg); }
  { PortsCheck c = Run("flat65280", {{"emudsk", "off"}}); CHECK(!c.ok && Has(c, "cannot be turned off"), "core off: '%s'", c.msg); }
  { PortsCheck c = Run("multicomp09fuzix", {{"acia", "0xFF20"}}); CHECK(!c.ok && Has(c, "cannot be moved"), "multicomp fixed: '%s'", c.msg); }
  { PortsCheck c = Run("multicomp09fuzix", {{"profile", "COCO"}}); CHECK(!c.ok && Has(c, "no ports in profile COCO"), "multicomp has no COCO: '%s'", c.msg); }
  { PortsCheck c = Run("flat65280", {{"profile", "Bogus"}}); CHECK(!c.ok && Has(c, "unknown profile"), "bad profile: '%s'", c.msg); }
  { PortsCheck c = Run("flat65280", {{"profile", "E1"}}); CHECK(!c.ok && Has(c, "unknown profile"), "E1 is not a profile: '%s'", c.msg); }
  { PortsCheck c = Run("flat65280", {{"gizmo", "0xFF48"}}); CHECK(!c.ok && Has(c, "gizmo"), "unknown device: '%s'", c.msg); }
  { PortsCheck c = Run("flat65280", {{"becker", "48"}}); CHECK(!c.ok && Has(c, "not an address"), "bare number: '%s'", c.msg); }
  { PortsCheck c = Run("flat65280", {{"gime", "0xFF48"}}); CHECK(!c.ok, "gime is not a flat65280 device: '%s'", c.msg); }

  // 6. Enhancements: off is fine; an unimplemented-but-known one claims its range like any other.
  { PortsCheck c = Run("flat65280", {{"becker", "off"}}); CHECK(c.ok, "becker off: %s", c.msg); }
  { PortsCheck c = Run("flat65280", {{"becker", "0xFF41"}, {"audio", "0xFF70"}}); CHECK(c.ok, "two enhancements: %s", c.msg); }
  { PortsCheck c = Run("flat65280", {{"becker", "0xFF70"}, {"audio", "0xFF70"}}); CHECK(!c.ok && Has(c, "becker") && Has(c, "audio"), "enhancements clash: '%s'", c.msg); }

  // 7. Address parsing.
  uint16_t a = 0;
  CHECK(ParsePortAddr("0xFF48", &a) && a == 0xFF48, "0xFF48");
  CHECK(ParsePortAddr("$ff48", &a) && a == 0xFF48, "$ff48");
  CHECK(!ParsePortAddr("FF48", &a), "bare hex is ambiguous");
  CHECK(!ParsePortAddr("0xFE00", &a), "below the I/O page");
  CHECK(!ParsePortAddr("0x10000", &a), "above 16 bits");
  CHECK(!ParsePortAddr("0xFF4G", &a), "junk");
  CHECK(!ParsePortAddr("", &a) && !ParsePortAddr("0x", &a) && !ParsePortAddr(nullptr, &a), "empty");

  // 8. PortBase follows the profile and the overrides.
  g_port_settings.Clear();
  CHECK(PortBase("gep9", "console", 0) == 0xFF00, "TFR4 console");
  g_port_settings.Set("profile", "COCO");
  CHECK(PortBase("gep9", "console", 0) == 0xFF60, "COCO console");
  g_port_settings.Set("console", "0xFF30");
  CHECK(PortBase("gep9", "console", 0) == 0xFF30, "override wins");
  CHECK(PortBase("gep9", "nonesuch", 0x1234) == 0x1234, "fallback");
  g_port_settings.Clear();

  // 9. Settings table behavior.
  PortSettings st;
  CHECK(st.Set("a", "1") && st.Set("a", "2") && !strcmp(st.Get("a"), "2") && st.n_ == 1, "replace");
  CHECK(st.Unset("a") && !st.Get("a") && st.n_ == 0 && !st.Unset("a"), "unset");
  CHECK(!st.Set("this_key_is_far_too_long_for_the_table", "1"), "long key rejected");
  for (int i = 0; i < PortSettings::kMax; i++) { char k[16]; snprintf(k, sizeof k, "k%d", i); CHECK(st.Set(k, "1"), "fill %d", i); }
  CHECK(!st.Set("one_more", "1"), "full table");

  if (failures) { printf("%d FAILURES\n", failures); return 1; }
  printf("ports_config_test: OK\n");
  return 0;
}
