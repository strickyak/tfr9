// Host test for firmware/lib/devices/ports_config.h: `on ... run ... for ... with ...` digestion,
// repeated `WITH`, `,` rejection, `key = value`, and `turbolab` options.
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include "ports_config.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static bool Has(const PortsCheck& c, const char* s) { return strstr(c.msg, s) != nullptr; }

static PortsCheck ParseLine(const char* line, PortSettings& st, const char* board_host = "tfr911h") {
  std::istringstream iss(line);
  std::vector<std::string> toks;
  std::string w;
  while (iss >> w) toks.push_back(w);
  std::vector<const char*> argv;
  for (auto& t : toks) argv.push_back(t.c_str());
  return ParseOnRunCommand((int)argv.size(), argv.data(), board_host, st);
}

static PortsCheck ParseAndValidate(const char* line, const char* board_host = "tfr911h") {
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

  // 1b. Retired `tfr9` host name and `,` separator are both cleanly rejected.
  {
    PortsCheck old_h = ParseAndValidate("on tfr9 run turbo9os");
    CHECK(!old_h.ok && Has(old_h, "tfr911h"), "old host tfr9 refused: '%s'", old_h.msg);

    PortsCheck comma = ParseAndValidate("ON coco2 RUN nitros9l2 FOR coco3 WITH e1, becker");
    CHECK(!comma.ok && Has(comma, "repeat 'with'"), "comma separator refused: '%s'", comma.msg);

    PortsCheck wempty1 = ParseAndValidate("ON tfr911h RUN turbo9os WITH");
    CHECK(!wempty1.ok, "trailing WITH refused: '%s'", wempty1.msg);

    PortsCheck wempty2 = ParseAndValidate("ON tfr911h RUN turbo9os WITH WITH becker");
    CHECK(!wempty2.ok, "WITH WITH refused: '%s'", wempty2.msg);
  }

  // 2. Repeated `WITH` grammar: uppercase converted to lowercase, 0x... hex barewords, multi-arg `dw`, flat KV digestion.
  {
    PortSettings st;
    PortsCheck p = ParseLine("ON coco2 RUN nitros9l2 FOR coco3 WITH E2 WITH becker at 0xFF41 WITH dw over bitbang baud 9600 WITH rtc at 0xFFE0", st);
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

    PortsCheck v1 = ParseAndValidate("ON coco2 RUN nitros9l2 FOR coco3 WITH E1 WITH becker at 0xFF41 WITH dw over bitbang baud 9600 WITH rtc at 0xFFE0");
    CHECK(v1.ok, "validates with E1: %s", v1.msg);

    // Same guest on real CoCo 3 implies gerbil_io instead of gerbil_gime!
    PortSettings st3;
    CHECK(ParseLine("ON coco3 RUN nitros9l2 FOR coco3 WITH E1 WITH becker at 0xFF41 WITH dw over bitbang baud 9600 WITH rtc at 0xFFE0", st3).ok, "parse coco3");
    CHECK(!strcmp(st3.Get("engine"), "gerbil_io"), "coco3 implies gerbil_io");
    CHECK(PortsValidate(st3, m).ok, "coco3 validates");
  }

  // 3. `on <host>` standalone + portable `run ...` shorthand (e.g. inside a .pak).
  {
    PortSettings st;
    CHECK(ParseLine("ON COCO3", st).ok && !strcmp(st.Host(), "coco3"), "on coco3 sets host");
    CHECK(ParseLine("RUN nitros9l2 FOR coco3 WITH e1 WITH becker WITH audio", st).ok, "run inherits host coco3");
    CHECK(!strcmp(st.Get("on"), "coco3") && !strcmp(st.Get("engine"), "gerbil_io"), "inherited coco3 -> gerbil_io");
    IoMap m;
    CHECK(PortsValidate(st, m).ok, "run shorthand validates");
  }

  // 4. Omitted `for` works when unique, and lists choices when ambiguous.
  {
    CHECK(ParseAndValidate("on tfr911h run turbo9os").ok, "turbo9os unique for");
    CHECK(ParseAndValidate("on tfr911h run turbolab").ok, "turbolab unique for");
    CHECK(ParseAndValidate("on tfr911h run nitros9l1").ok, "nitros9l1 unique for");
    CHECK(ParseAndValidate("on tfr911h run fuzix").ok, "fuzix on tfr911h unique for");
    PortsCheck amb = ParseAndValidate("on tfr911h run nitros9l2");
    CHECK(!amb.ok && Has(amb, "deep65280") && Has(amb, "jamesbrown") && Has(amb, "coco3"), "ambiguous nitros9l2 lists choices: '%s'", amb.msg);
  }

  // 5. Known conflict: gep9 / gep9big on tfr911h have embiggen_mmu at 0xff40-47 vs Becker's e1 default 0xff41-42.
  for (const char* f : {"gep9", "gep9big"}) {
    char c1[112], c2[112];
    snprintf(c1, sizeof c1, "on tfr911h run hatvan for %s with e1 with becker", f);
    snprintf(c2, sizeof c2, "on tfr911h run hatvan for %s with e1 with becker at 0xff48", f);
    PortsCheck bad = ParseAndValidate(c1);
    CHECK(!bad.ok && Has(bad, "becker") && Has(bad, "embiggen_mmu"), "%s + becker conflict: '%s'", f, bad.msg);
    CHECK(ParseAndValidate(c2).ok, "%s + becker at 0xff48 ok", f);
  }
  CHECK(ParseAndValidate("on tfr911h run hatvan for coco2 with e1 with becker").ok, "hatvan for coco2 has no clash at 0xff41");

  // 6. Motherboard hardware collisions on coco2 and coco3 are caught!
  {
    PortsCheck pia = ParseAndValidate("on coco2 run nitros9l2 for coco3 with becker at 0xff14");
    CHECK(!pia.ok && Has(pia, "coco_pia"), "coco2 PIA collision caught: '%s'", pia.msg);
    PortsCheck sam = ParseAndValidate("on coco2 run nitros9l2 for coco3 with rtc at 0xffc0");
    CHECK(!sam.ok && Has(sam, "coco_sam"), "coco2 SAM collision caught: '%s'", sam.msg);
    PortsCheck gime = ParseAndValidate("on coco3 run nitros9l2 for coco3 with rtc at 0xff90");
    CHECK(!gime.ok && Has(gime, "coco3_gime"), "coco3 real GIME collision caught: '%s'", gime.msg);
    PortsCheck jb = ParseAndValidate("on coco2 run nitros9l2 for jamesbrown");
    CHECK(!jb.ok && Has(jb, "supported on: tfr911h"), "jamesbrown refused on coco2: '%s'", jb.msg);
  }

  // 7. 0xff7f ($FF7F), core device rules, fixed ports, and invalid options.
  {
    PortsCheck mp1 = ParseAndValidate("on tfr911h run nitros9l1 for flat65280 with becker at 0xff7f");
    CHECK(!mp1.ok && Has(mp1, "Multi-Pak"), "0xff7f refused: '%s'", mp1.msg);
    PortsCheck mp2 = ParseAndValidate("on tfr911h run nitros9l1 for flat65280 with becker at 0xff7e");
    CHECK(!mp2.ok && Has(mp2, "Multi-Pak"), "0xff7e-7f refused: '%s'", mp2.msg);
    PortsCheck ov = ParseAndValidate("on tfr911h run nitros9l2 for deep65280 with emudsk at 0xff90");
    CHECK(!ov.ok && Has(ov, "emudsk") && Has(ov, "gime"), "core onto core: '%s'", ov.msg);
    CHECK(ParseAndValidate("on tfr911h run nitros9l1 for flat65280 with emudsk at 0xff20").ok, "movable core device");
    PortsCheck vec = ParseAndValidate("on tfr911h run nitros9l1 for flat65280 with vectors at 0xffe0");
    CHECK(!vec.ok && Has(vec, "vectors"), "vectors fixed: '%s'", vec.msg);
    PortsCheck coff = ParseAndValidate("on tfr911h run nitros9l1 for flat65280 with emudsk off");
    CHECK(!coff.ok && Has(coff, "cannot be turned off"), "core off: '%s'", coff.msg);
    PortsCheck mc = ParseAndValidate("on tfr911h run fuzix for multicomp09 with acia at 0xff20");
    CHECK(!mc.ok && Has(mc, "cannot be moved"), "multicomp fixed: '%s'", mc.msg);
    PortsCheck unk = ParseAndValidate("on tfr911h run nitros9l1 for flat65280 with gizmo at 0xff48");
    CHECK(!unk.ok && Has(unk, "gizmo"), "unknown device: '%s'", unk.msg);
    PortsCheck bare = ParseAndValidate("on tfr911h run nitros9l1 for flat65280 with becker at 48");
    CHECK(!bare.ok && Has(bare, "not an address"), "bare number: '%s'", bare.msg);
    PortsCheck dwbad = ParseAndValidate("on coco2 run nitros9l2 for coco3 with dw over telepathy");
    CHECK(!dwbad.ok && Has(dwbad, "dw.over"), "bad dw.over: '%s'", dwbad.msg);
    CHECK(ParseAndValidate("on tfr911h run nitros9l2 for jamesbrown with dw baud 57600 polarity dragon tx 16 rx 17").ok, "dw polarity/tx/rx ok");
    PortsCheck dwpol = ParseAndValidate("on tfr911h run nitros9l2 for jamesbrown with dw polarity sideways");
    CHECK(!dwpol.ok && Has(dwpol, "dw.polarity"), "bad dw.polarity: '%s'", dwpol.msg);
    PortsCheck dwtx = ParseAndValidate("on tfr911h run nitros9l2 for jamesbrown with dw tx 13");
    CHECK(!dwtx.ok && Has(dwtx, "dw.tx"), "bad dw.tx: '%s'", dwtx.msg);
    PortsCheck dwrx = ParseAndValidate("on tfr911h run nitros9l2 for jamesbrown with dw rx 12");
    CHECK(!dwrx.ok && Has(dwrx, "dw.rx"), "bad dw.rx: '%s'", dwrx.msg);
    CHECK(ParseAndValidate("on tfr911h run nitros9l1 for flat65280 with becker off").ok, "enhancement off");
  }

  // 8. PortBase follows (on, run, for) and `.at` overrides.
  {
    g_port_settings.Clear();
    CHECK(ParseLine("on tfr911h run hatvan for gep9", g_port_settings).ok, "gep9");
    CHECK(PortBase("console", 0) == 0xFF00, "gep9 console 0xff00");
    CHECK(ParseLine("on tfr911h run hatvan for coco2", g_port_settings).ok, "hatvan coco2");
    CHECK(PortBase("console", 0) == 0xFF60, "coco2 console 0xff60");
    CHECK(PortBase("embiggen_mmu", 0) == 0xFFE0, "coco2 embiggen 0xffe0");
    CHECK(ParseLine("on tfr911h run hatvan for gep9 with console at 0xff50", g_port_settings).ok, "override");
    CHECK(PortBase("console", 0) == 0xFF50, "override wins");
    IoMap mv;
    CHECK(PortsValidate(g_port_settings, mv).ok, "override validates");
    CHECK(PortBase("nonesuch", 0x1234) == 0x1234, "fallback");
    g_port_settings.Clear();
  }

  // 9. TurboLab `ON tfr911h RUN turbolab WITH ...` grammar, options, SI/binary counts, and OS-9 module resolver!
  {
    // Strick's exact prompt example:
    PortSettings st;
    PortsCheck p = ParseLine("ON tfr911h RUN turbolab WITH trigger = r/0xFFE8 WITH trace_count = 10000", st);
    CHECK(p.ok, "parse Strick turbolab command: %s", p.msg);
    CHECK(!strcmp(st.Get("on"), "tfr911h") && !strcmp(st.Get("run"), "turbolab") &&
          !strcmp(st.Get("for"), "turbo9sim") && !strcmp(st.Get("engine"), "hamster_turbolab"),
          "turbolab target keys");
    CHECK(!strcmp(st.Get("trigger"), "r/0xFFE8"), "trigger=r/0xFFE8");
    CHECK(!strcmp(st.Get("trace_count"), "10000"), "trace_count=10000");

    IoMap m;
    CHECK(PortsValidate(st, m).ok, "Strick turbolab command validates");

    turbolab::TurbolabPlan plan;
    CHECK(CompileTurbolabPlan(st, plan).ok, "compile turbolab plan");
    CHECK(plan.has_trigger_wp && plan.trigger_wp.type == 'r' && plan.trigger_wp.addr == 0xFFE8 && plan.trigger_wp.count == 1,
          "trigger_wp r/0xFFE8/1");
    CHECK(plan.trace_count == 10000, "trace_count 10000");
    CHECK(plan.trace_flags == turbolab::kTraceAllNoIdle, "default trace_flags when trigger/trace_count set");

    // Compact `key=value`, case-preserved image path, SI `10k` vs binary `16K`, multiple `WITH watch = ...`, and enhancements!
    PortSettings st2;
    PortsCheck p2 = ParseLine(
        "ON tfr911h RUN turbolab WITH image=/pc/MyDir/Pi.decb WITH trigger=x/0x1019/3 "
        "WITH stop=c/16K WITH trace_count=10k WITH trace=x+w WITH watch=0x1000 WITH watch=w/0x0020/5 WITH rtc", st2);
    CHECK(p2.ok, "parse complex turbolab: %s", p2.msg);
    CHECK(PortsValidate(st2, m).ok, "validate complex turbolab");
    turbolab::TurbolabPlan plan2;
    CHECK(CompileTurbolabPlan(st2, plan2).ok, "compile plan2");
    CHECK(!strcmp(plan2.image, "/pc/MyDir/Pi.decb"), "case-preserved image path: '%s'", plan2.image);
    CHECK(plan2.has_trigger_wp && plan2.trigger_wp.type == 'x' && plan2.trigger_wp.addr == 0x1019 && plan2.trigger_wp.count == 3,
          "trigger x/0x1019/3");
    CHECK(plan2.max_cycles == 16384, "binary 16K == 16384 (got %llu)", (unsigned long long)plan2.max_cycles);
    CHECK(plan2.trace_count == 10000, "decimal 10k == 10000 (got %llu)", (unsigned long long)plan2.trace_count);
    CHECK(plan2.trace_flags == (turbolab::kTraceX | turbolab::kTracePlus | turbolab::kTraceW), "trace x+w");
    CHECK(plan2.num_watches == 2, "2 watchpoints");
    CHECK(plan2.watches[0].type == 0 && plan2.watches[0].addr == 0x1000 && plan2.watches[0].count == 0, "watch.0");
    CHECK(plan2.watches[1].type == 'w' && plan2.watches[1].addr == 0x0020 && plan2.watches[1].count == 5, "watch.1");
    CHECK(!strcmp(st2.Get("rtc"), "on") && m.OwnerOf(0xFF72) && !strcmp(m.OwnerOf(0xFF72), "rtc"), "turbolab + rtc enhancement");

    // Silent watchpoint-only run (no trigger, no trace_count, no trace) keeps trace_flags == 0!
    PortSettings st3;
    CHECK(ParseLine("ON tfr911h RUN turbolab WITH watch = 0x1000 WITH stop = 500ms", st3).ok, "silent watch");
    turbolab::TurbolabPlan plan3;
    CHECK(CompileTurbolabPlan(st3, plan3).ok && plan3.trace_flags == 0 && plan3.max_time_us == 500000, "silent watch plan");

    // Symbolic OS-9 module watchpoint resolution (`@kernel+0x1B`):
    PortSettings st4;
    CHECK(ParseLine("ON tfr911h RUN turbolab WITH stop = @kernel+0x1B", st4).ok, "parse @kernel+0x1B");
    turbolab::TurbolabPlan plan4;
    CHECK(CompileTurbolabPlan(st4, plan4).ok && plan4.has_stop_wp && plan4.stop_wp.symbolic &&
          plan4.stop_wp.type == 'x' && !strcmp(plan4.stop_wp.mod_name, "kernel") && plan4.stop_wp.mod_offset == 0x1B,
          "parsed symbolic @kernel+0x1B");
    uint8_t fake_ram[65536] = {};
    // Build a valid OS-9 module header at $E3B1 named "Kernel" (size $0040, namoff $000D)
    const uint16_t kBase = 0xE3B1;
    fake_ram[kBase + 0] = 0x87; fake_ram[kBase + 1] = 0xCD;
    fake_ram[kBase + 2] = 0x00; fake_ram[kBase + 3] = 0x40;
    fake_ram[kBase + 4] = 0x00; fake_ram[kBase + 5] = 0x0D;
    fake_ram[kBase + 6] = 0xF1; fake_ram[kBase + 7] = 0x81;
    uint8_t parity = 0xFF;
    for (int j = 0; j < 8; j++) parity ^= fake_ram[kBase + j];
    fake_ram[kBase + 8] = parity;
    const char* nm = "Kerne";
    memcpy(&fake_ram[kBase + 0x0D], nm, 5);
    fake_ram[kBase + 0x0D + 5] = 'l' | 0x80;
    char err_buf[96] = {};
    CHECK(turbolab::ResolveModuleWatchpoint(fake_ram, plan4.stop_wp, err_buf, sizeof(err_buf)), "resolve @kernel: %s", err_buf);
    CHECK(!plan4.stop_wp.symbolic && plan4.stop_wp.addr == (uint16_t)(kBase + 0x1B), "resolved to $E3CC");

    // Reject turbolab options on non-turbolab targets, and reject bad option values!
    PortsCheck wrong_tgt = ParseAndValidate("ON tfr911h RUN turbo9os WITH trigger = r/0xFFE8");
    CHECK(!wrong_tgt.ok && Has(wrong_tgt, "run turbolab"), "trigger refused on turbo9os: '%s'", wrong_tgt.msg);
    PortsCheck no_eq = ParseAndValidate("ON tfr911h RUN turbolab WITH trigger r/0xFFE8");
    CHECK(!no_eq.ok && Has(no_eq, "expects '= <value>'"), "missing '=' caught: '%s'", no_eq.msg);
    PortsCheck bad_tr = ParseAndValidate("ON tfr911h RUN turbolab WITH trace = zzz");
    CHECK(!bad_tr.ok && Has(bad_tr, "invalid trace"), "bad trace flag caught: '%s'", bad_tr.msg);
  }

  // 10. Address parsing & table limits.
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
