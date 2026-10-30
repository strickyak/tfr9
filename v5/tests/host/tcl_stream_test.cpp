// Host test for firmware/lib/tcl/tcl_stream.h (spec S38): streaming `source` must behave like Tcl_Eval of the
// whole text, while holding only one command in memory.  Builds against the real Tcl 6.7c sources.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "tcl_stream.h"

// tclGlob.c references this (normally from tclUnixUtil.c, which the firmware does not build); the test never calls it.
extern "C" char* Tcl_UnixError(Tcl_Interp*) { return (char*)"unix error"; }

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

struct MemReader { const std::string* s; size_t pos; size_t chunk; };
static long MemRead(void* c, char* buf, size_t n) {
  MemReader* m = (MemReader*)c;
  if (m->pos >= m->s->size()) return 0;
  size_t k = std::min({n, m->chunk, m->s->size() - m->pos});
  memcpy(buf, m->s->data() + m->pos, k);
  m->pos += k;
  return (long)k;
}

static int ByeCmd(ClientData, Tcl_Interp*, int, char**) { return 9; }   // like the firmware's TCL_BYE
static std::string g_log;
static int LogCmd(ClientData, Tcl_Interp*, int argc, char** argv) {
  for (int i = 1; i < argc; i++) { g_log += argv[i]; g_log += ' '; }
  return TCL_OK;
}
static Tcl_Interp* NewInterp() {
  Tcl_Interp* in = Tcl_CreateInterp();
  Tcl_CreateCommand(in, (char*)"bye", ByeCmd, nullptr, nullptr);
  Tcl_CreateCommand(in, (char*)"log", LogCmd, nullptr, nullptr);
  return in;
}
static std::string Var(Tcl_Interp* in, const char* name) { char* v = Tcl_GetVar(in, (char*)name, 0); return v ? v : "<unset>"; }

// `source` always runs INSIDE another Tcl_Eval (numLevels > 0), where TCL_RETURN is not converted to TCL_OK.
// So the two evaluators are invoked from within Tcl commands, exactly as the firmware's source command does.
static std::string g_script; static size_t g_chunk; static int g_inner_rc; static TclStreamStats g_stats;
static int WholeSrc(ClientData, Tcl_Interp* in, int, char**) {
  g_inner_rc = Tcl_Eval(in, const_cast<char*>(g_script.c_str()), 0, nullptr);
  return g_inner_rc;
}
static int StreamSrc(ClientData, Tcl_Interp* in, int, char**) {
  MemReader mr{&g_script, 0, g_chunk};
  g_stats = TclStreamStats();
  g_inner_rc = TclEvalStream(in, MemRead, &mr, &g_stats);
  return g_inner_rc;
}

// Run `script` both ways; return codes, logs, and variables must agree.
static void Differential(const char* what, const std::string& script, size_t chunk, int expect_rc, bool check_buffer = false) {
  g_script = script; g_chunk = chunk;
  Tcl_Interp* a = NewInterp(); Tcl_CreateCommand(a, (char*)"wholesrc", WholeSrc, nullptr, nullptr);
  g_log.clear();
  Tcl_Eval(a, (char*)"wholesrc", 0, nullptr);
  int ra = g_inner_rc; std::string log_a = g_log; std::string res_a = a->result;
  Tcl_Interp* b = NewInterp(); Tcl_CreateCommand(b, (char*)"streamsrc", StreamSrc, nullptr, nullptr);
  g_log.clear();
  Tcl_Eval(b, (char*)"streamsrc", 0, nullptr);
  int rb = g_inner_rc; std::string log_b = g_log; std::string res_b = b->result;
  TclStreamStats st = g_stats;
  CHECK(ra == rb, "%s: rc whole=%d stream=%d (chunk %zu)", what, ra, rb, chunk);
  CHECK(rb == expect_rc, "%s: rc=%d expected %d", what, rb, expect_rc);
  CHECK(log_a == log_b, "%s: log differs: whole=[%s] stream=[%s]", what, log_a.c_str(), log_b.c_str());
  for (const char* v : {"x", "y", "z", "count", "total"})
    CHECK(Var(a, v) == Var(b, v), "%s: var %s whole=%s stream=%s", what, v, Var(a, v).c_str(), Var(b, v).c_str());
  if (rb == TCL_ERROR || rb == TCL_RETURN) CHECK(res_a == res_b, "%s: result differs: [%s] vs [%s]", what, res_a.c_str(), res_b.c_str());
  if (check_buffer) CHECK(st.max_buffered < 200, "%s: buffered %zu bytes (should be one small command)", what, st.max_buffered);
  Tcl_DeleteInterp(a);
  Tcl_DeleteInterp(b);
}

int main() {
  const size_t chunks[] = {1, 2, 7, 64, 100000};
  for (size_t c : chunks) {
    Differential("simple", "set x 1\nset y 2\nlog a b\nset z [expr {$x + $y}]\n", c, TCL_OK);
    Differential("no trailing newline", "set x 1\nset y 2", c, TCL_OK);
    Differential("empty", "", c, TCL_OK);
    Differential("only comments", "# nothing\n   # here\n\n", c, TCL_OK);
    Differential("multi-line proc and braces", "proc add {a b} {\n  set r [expr {$a + $b}]\n  return $r\n}\nset x [add 3 4]\nif {$x > 5} {\n  set y big\n} else {\n  set y small\n}\n", c, TCL_OK);
    Differential("line continuation", "set x \\\n  42\nlog one \\\n two \\\n three\nset y 7\n", c, TCL_OK);
    Differential("braces in a string and quotes", "set x \"a {b\"\nset y {c \"d}\nlog $x $y\n", c, TCL_OK);
    Differential("error stops the script", "set x 1\nerror boom\nset y 2\n", c, TCL_ERROR);
    Differential("bye stops the script (TCL_BYE)", "set x 1\nbye\nset y 2\n", c, 9);
    Differential("return at top level", "set x 1\nreturn done\nset y 2\n", c, TCL_RETURN);
    Differential("missing close-brace at EOF", "set x 1\nset y {unterminated\nset z 3\n", c, TCL_ERROR);
    Differential("CRLF line ends", "set x 1\r\nset y 2\r\nlog hi\r\n", c, TCL_OK);
    Differential("while loop over lines", "set count 0\nset total 0\nwhile {$count < 5} {\n  incr count\n  incr total $count\n}\n", c, TCL_OK);
  }
  // A comment containing an unbalanced brace (Tcl 6.7's Tcl_CommandComplete may wait for more text; the result must still match)
  for (size_t c : chunks) Differential("brace in comment", "# a { here\nset x 1\n# } there\nset y 2\n", c, TCL_OK);

  // Memory bound: a 3 MB script of many small commands must be evaluated holding one command at a time.
  std::string big;
  big += "set count 0\n";
  for (int i = 0; i < 100000; i++) big += "incr count\nset z line" + std::to_string(i) + "\n";
  big += "set total $count\n";
  CHECK(big.size() > 2000000, "test script should be large (%zu)", big.size());
  Differential("large script", big, 64, TCL_OK, /*check_buffer=*/true);

  // Over-long single command is refused (not silently accepted).
  std::string huge = "set x \"" + std::string(70000, 'a') + "\"\n";
  Tcl_Interp* in = NewInterp(); MemReader mr{&huge, 0, 64}; TclStreamStats st;
  int rc = TclEvalStream(in, MemRead, &mr, &st);
  CHECK(rc == TCL_ERROR && strstr(in->result, "too long"), "over-long command: rc=%d result=%s", rc, in->result);
  Tcl_DeleteInterp(in);

  // Reader error is reported.
  auto bad = [](void*, char*, size_t) -> long { return -1; };
  in = NewInterp();
  rc = TclEvalStream(in, bad, nullptr);
  CHECK(rc == TCL_ERROR && strstr(in->result, "error reading"), "reader error: rc=%d result=%s", rc, in->result);
  Tcl_DeleteInterp(in);

  if (failures) { printf("tcl_stream_test: %d FAILURES\n", failures); return 1; }
  printf("tcl_stream_test: OK\n");
  return 0;
}
