// Host test for firmware/lib/devices/ports_config.h: `on ... run ... for ... with ...` digestion and port validation.
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include "ports_config.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static bool Has(const PortsCheck& c, const char* s) { return strstr(c.msg, s) != nullptr; }

static PortsCheck ParseLine(const char* line, PortSettings& st, const char* board_host = "tfr9") {
  std::istringstream iss(line);
  std::vector<std::string> toks;
  std::string w;
  while (iss >> w) toks.push_back(w);
  std::vector<const char*> argv;
  for (auto& t : toks) argv.push_back(t.c_str());
  return ParseOnRunCommand((int)argv.size(), argv.data(), board_host, st);
}

static PortsCheck ParseAndValidate(const char* line, const char* board_host = "tfr9") {
  PortSettings st;
  PortsCheck pc = ParseLine(line, st, board_host);
  if (!pc.ok) return pc;
  IoMap m;
  return PortsValidate(st, m, /*include_host_reserved=*/true, board_host);
}

int main() {
  // 1. Every declared (on, run, for) target in ports.tsv parses and validates cleanly.
  for (size_t i = 0; i < kNumTargets; i++) {
    const TargetSpec& t = kTargets[i];
    char cmd[128];
    snprintf(cmd, sizeof cmd, "on %s run %s for %s", t.on_host, t.run_os, t.for_hw);
    PortSettings st;
    PortsCheck p = ParseLine(cmd, st);
    CHECK(p.ok, "parse '%s': %s", cmd, p.msg);
    CHECK(st.Get("engine") && !strcmp(st.Get("engine"), t.engine), "%s implied engine %s", cmd, t.engine);
    IoMap m;
    PortsCheck v = PortsValidate(st, m);
    CHECK(v.ok, "validate '%s': %s", cmd, v.msg);
  }

  // 2. Strick's full grammar example: uppercase converted to lowercase, 0x... hex barewords, multi-arg `dw`, flat KV digestion.
  {
    PortSettings st;
    PortsCheck p = ParseLine("ON coco2 RUN nitros9l2 FOR coco3 WITH E2, becker at 0xFF41, dw over bitbang baud 9600, rtc at 0xFFE0", st);
    CHECK(p.ok, "parse full ON/RUN/FOR/WITH: %s", p.msg);
    CHECK(!strcmp(st.Get("on"), "coco2"), "on=coco2");
    CHECK(!strcmp(st.Get("run"), "nitros9l2"), "run=nitros9l2");
    CHECK(!strcmp(st.Get("for"), "coco3"), "for=coco3");
    CHECK(!strcmp(st.Get("engine"), "gerbil_gime"), "engine=gerbil_gime");
    CHECK(!strcmp(st.Get("with"), "e2"), "with=e2");
    CHECK(!strcmp(st.Get("becker"), "on") && !strcmp(st.Get("becker.at"), "0xff41"), "becker.at=0xff41");
    CHECK(!strcmp(st.Get("dw"), "on") && !strcmp(st.Get("dw.over"), "bitbang") && !strcmp(st.Get("dw.baud"), "9600"), "dw transport");
    CHECK(!strcmp(st.Get("rtc"), "on") && !strcmp(st.Get("rtc.at"), "0xffe0"), "rtc.at=0xffe0");

    // Validation catches unknown map `e2` right now, and passes with `E1`.
    IoMap m;
    PortsCheck v2 = PortsValidate(st, m);
    CHECK(!v2.ok && Has(v2, "e2"), "unknown enhancement map e2 caught: '%s'", v2.msg);

    PortsCheck v1 = ParseAndValidate("ON coco2 RUN nitros9l2 FOR coco3 WITH E1, becker at 0xFF41, dw over bitbang baud 9600, rtc at 0xFFE0");
    CHECK(v1.ok, "validates with E1: %s", v1.msg);

    // Same guest on real CoCo 3 implies gerbil_io instead of gerbil_gime!
    PortSettings st3;
    CHECK(ParseLine("ON coco3 RUN nitros9l2 FOR coco3 WITH E1, becker at 0xFF41, dw over bitbang baud 9600, rtc at 0xFFE0", st3).ok, "parse coco3");
    CHECK(!strcmp(st3.Get("engine"), "gerbil_io"), "coco3 implies gerbil_io");
    CHECK(PortsValidate(st3, m).ok, "coco3 validates");
  }

  // 3. `on <host>` standalone + portable `run ...` shorthand (e.g. inside a .pak).
  {
    PortSettings st;
    CHECK(ParseLine("ON COCO3", st).ok && !strcmp(st.Host(), "coco3"), "on coco3 sets host");
    CHECK(ParseLine("RUN nitros9l2 FOR coco3 WITH e1, becker, audio", st).ok, "run inherits host coco3");
    CHECK(!strcmp(st.Get("on"), "coco3") && !strcmp(st.Get("engine"), "gerbil_io"), "inherited coco3 -> gerbil_io");
    IoMap m;
    CHECK(PortsValidate(st, m).ok, "run shorthand validates");
  }

  // 4. Omitted `for` works when unique, and lists choices when ambiguous.
  {
    CHECK(ParseAndValidate("on tfr9 run turbo9os").ok, "turbo9os unique for");
    CHECK(ParseAndValidate("on tfr9 run nitros9l1").ok, "nitros9l1 unique for");
    CHECK(ParseAndValidate("on tfr9 run fuzix").ok, "fuzix on tfr9 unique for");
    PortsCheck amb = ParseAndValidate("on tfr9 run nitros9l2");
    CHECK(!amb.ok && Has(amb, "deep65280") && Has(amb, "jamesbrown") && Has(amb, "coco3"), "ambiguous nitros9l2 lists choices: '%s'", amb.msg);
  }

  // 5. Known conflict: gep9 / gep9big on tfr9 have embiggen_mmu at 0xff40-47 vs Becker's e1 default 0xff41-42.
  for (const char* f : {"gep9", "gep9big"}) {
    char c1[96], c2[96];
    snprintf(c1, sizeof c1, "on tfr9 run hatvan for %s with e1, becker", f);
    snprintf(c2, sizeof c2, "on tfr9 run hatvan for %s with e1, becker at 0xff48", f);
    PortsCheck bad = ParseAndValidate(c1);
    CHECK(!bad.ok && Has(bad, "becker") && Has(bad, "embiggen_mmu"), "%s + becker conflict: '%s'", f, bad.msg);
    CHECK(ParseAndValidate(c2).ok, "%s + becker at 0xff48 ok", f);
  }
  CHECK(ParseAndValidate("on tfr9 run hatvan for coco2 with e1, becker").ok, "hatvan for coco2 has no clash at 0xff41");

  // 6. Motherboard hardware collisions on coco2 and coco3 are caught!
  {
    PortsCheck pia = ParseAndValidate("on coco2 run nitros9l2 for coco3 with becker at 0xff14");
    CHECK(!pia.ok && Has(pia, "coco_pia"), "coco2 PIA collision caught: '%s'", pia.msg);
    PortsCheck sam = ParseAndValidate("on coco2 run nitros9l2 for coco3 with rtc at 0xffc0");
    CHECK(!sam.ok && Has(sam, "coco_sam"), "coco2 SAM collision caught: '%s'", sam.msg);
    PortsCheck gime = ParseAndValidate("on coco3 run nitros9l2 for coco3 with rtc at 0xff90");
    CHECK(!gime.ok && Has(gime, "coco3_gime"), "coco3 real GIME collision caught: '%s'", gime.msg);
    PortsCheck jb = ParseAndValidate("on coco2 run nitros9l2 for jamesbrown");
    CHECK(!jb.ok && Has(jb, "supported on: tfr9"), "jamesbrown refused on coco2: '%s'", jb.msg);
  }

  // 7. 0xff7f ($FF7F), core device rules, fixed ports, and invalid options.
  {
    PortsCheck mp1 = ParseAndValidate("on tfr9 run nitros9l1 for flat65280 with becker at 0xff7f");
    CHECK(!mp1.ok && Has(mp1, "Multi-Pak"), "0xff7f refused: '%s'", mp1.msg);
    PortsCheck mp2 = ParseAndValidate("on tfr9 run nitros9l1 for flat65280 with becker at 0xff7e");
    CHECK(!mp2.ok && Has(mp2, "Multi-Pak"), "0xff7e-7f refused: '%s'", mp2.msg);
    PortsCheck ov = ParseAndValidate("on tfr9 run nitros9l2 for deep65280 with emudsk at 0xff90");
    CHECK(!ov.ok && Has(ov, "emudsk") && Has(ov, "gime"), "core onto core: '%s'", ov.msg);
    CHECK(ParseAndValidate("on tfr9 run nitros9l1 for flat65280 with emudsk at 0xff20").ok, "movable core device");
    PortsCheck vec = ParseAndValidate("on tfr9 run nitros9l1 for flat65280 with vectors at 0xffe0");
    CHECK(!vec.ok && Has(vec, "vectors"), "vectors fixed: '%s'", vec.msg);
    PortsCheck coff = ParseAndValidate("on tfr9 run nitros9l1 for flat65280 with emudsk off");
    CHECK(!coff.ok && Has(coff, "cannot be turned off"), "core off: '%s'", coff.msg);
    PortsCheck mc = ParseAndValidate("on tfr9 run fuzix for multicomp09 with acia at 0xff20");
    CHECK(!mc.ok && Has(mc, "cannot be moved"), "multicomp fixed: '%s'", mc.msg);
    PortsCheck unk = ParseAndValidate("on tfr9 run nitros9l1 for flat65280 with gizmo at 0xff48");
    CHECK(!unk.ok && Has(unk, "gizmo"), "unknown device: '%s'", unk.msg);
    PortsCheck bare = ParseAndValidate("on tfr9 run nitros9l1 for flat65280 with becker at 48");
    CHECK(!bare.ok && Has(bare, "not an address"), "bare number: '%s'", bare.msg);
    PortsCheck dwbad = ParseAndValidate("on coco2 run nitros9l2 for coco3 with dw over telepathy");
    CHECK(!dwbad.ok && Has(dwbad, "dw.over"), "bad dw.over: '%s'", dwbad.msg);
    CHECK(ParseAndValidate("on tfr9 run nitros9l1 for flat65280 with becker off").ok, "enhancement off");
  }

  // 8. PortBase follows (on, run, for) and `.at` overrides.
  {
    g_port_settings.Clear();
    CHECK(ParseLine("on tfr9 run hatvan for gep9", g_port_settings).ok, "gep9");
    CHECK(PortBase("console", 0) == 0xFF00, "gep9 console 0xff00");
    CHECK(ParseLine("on tfr9 run hatvan for coco2", g_port_settings).ok, "hatvan coco2");
    CHECK(PortBase("console", 0) == 0xFF60, "coco2 console 0xff60");
    CHECK(PortBase("embiggen_mmu", 0) == 0xFFE0, "coco2 embiggen 0xffe0");
    CHECK(ParseLine("on tfr9 run hatvan for gep9 with console at 0xff30", g_port_settings).ok, "override");
    CHECK(PortBase("console", 0) == 0xFF30, "override wins");
    CHECK(PortBase("nonesuch", 0x1234) == 0x1234, "fallback");
    g_port_settings.Clear();
  }

  // 9. Address parsing & table limits.
  uint16_t a = 0;
  CHECK(ParsePortAddr("0xFF48", &a) && a == 0xFF48, "0xFF48");
  CHECK(ParsePortAddr("0xff48", &a) && a == 0xFF48, "0xff48");
  CHECK(ParsePortAddr("$ff48", &a) && a == 0xFF48, "$ff48");
  CHECK(!ParsePortAddr("FF48", &a), "bare hex is ambiguous");
  CHECK(!ParsePortAddr("0xFE00", &a), "below I/O page");
  CHECK(!ParsePortAddr("0x10000", &a), "above 16 bits");

  PortSettings st;
  CHECK(st.Set("A", "B") && !strcmp(st.Get("a"), "b") && !strcmp(st.Get("A"), "b"), "lowercase normalization");
  st.Clear();
  for (int i = 0; i < PortSettings::kMax; i++) { char k[16]; snprintf(k, sizeof k, "k%d", i); CHECK(st.Set(k, "1"), "fill %d", i); }
  CHECK(!st.Set("one_more", "1"), "full table");

  if (failures) { printf("ports_config_test: %d FAILURES\n", failures); return 1; }
  printf("ports_config_test: OK\n");
  return 0;
}
