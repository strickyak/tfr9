// Host test for spec S49: the Tcl_LowMemoryHook patch in third_party/tcl6.7c (a runaway script gets a Tcl
// error instead of a panic) and firmware/lib/core/heap.h.  Builds against the real Tcl 6.7c sources.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "heap.h"
#include "tcl6.7c/tcl.h"

extern "C" char* Tcl_UnixError(Tcl_Interp*) { return (char*)"unix error"; }

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static int g_budget = 0;      // commands the hook still allows; <0 = unlimited
static int g_hook_calls = 0;
static int Hook() { g_hook_calls++; if (g_budget < 0) return 0; return g_budget-- <= 0; }

int main() {
  Tcl_Interp* in = Tcl_CreateInterp();

  // 1. No hook: everything runs.
  Tcl_LowMemoryHook = nullptr;
  CHECK(Tcl_Eval(in, (char*)"for {set i 0} {$i < 100} {incr i} {set x $i}", 0, nullptr) == TCL_OK, "no-hook loop: %s", in->result);

  // 2. Hook says "low" after 50 commands: the loop stops with TCL_ERROR "out of memory", not a panic.
  Tcl_LowMemoryHook = Hook; g_budget = 50;
  int rc = Tcl_Eval(in, (char*)"for {set i 0} {$i < 100000} {incr i} {set x $i}", 0, nullptr);
  CHECK(rc == TCL_ERROR, "expected TCL_ERROR, got %d", rc);
  CHECK(strstr(in->result, "out of memory") != nullptr, "result was '%s'", in->result);
  CHECK(g_hook_calls > 0 && g_hook_calls < 200, "hook calls %d", g_hook_calls);

  // 3. The error unwinds out of a proc and a catch can handle it; the interpreter stays usable.
  Tcl_Eval(in, (char*)"proc eat {} { for {set i 0} {1} {incr i} { set v$i $i } }", 0, nullptr);
  g_budget = 30;
  rc = Tcl_Eval(in, (char*)"catch {eat} msg; set msg", 0, nullptr);
  CHECK(rc == TCL_OK && strstr(in->result, "out of memory"), "catch: rc=%d result='%s'", rc, in->result);
  g_budget = -1;                                     // memory is back
  rc = Tcl_Eval(in, (char*)"expr 6*7", 0, nullptr);
  CHECK(rc == TCL_OK && strcmp(in->result, "42") == 0, "after recovery: rc=%d '%s'", rc, in->result);

  // 4. heap.h: TryAlloc refuses a request bigger than free memory; MustAlloc/TryAlloc succeed for small ones.
  void* big = TryAlloc((size_t)-1 / 4);
  CHECK(big == nullptr || HeapFreeBytes() >= (size_t)-1 / 4, "TryAlloc of an impossible size must not succeed silently");
  free(big);
  void* small = TryAlloc(100); CHECK(small != nullptr, "small TryAlloc"); free(small);
  void* m = MustAlloc(100);    CHECK(m != nullptr, "small MustAlloc"); free(m);
  CHECK(!HeapIsLow(), "host heap should never look low");

  Tcl_DeleteInterp(in);
  if (failures) { printf("%d FAILURES\n", failures); return 1; }
  printf("tcl_lowmem_test: OK\n");
  return 0;
}
