#pragma once
// tcl_stream.h -- evaluate a Tcl script that arrives in pieces, ONE COMPLETE COMMAND AT A TIME.
//
// v4's `source` read the whole file into a ckalloc'd buffer and called Tcl_Eval once (spec S38): memory grew
// with the file (twice, for a file inside a .zip/.pak).  This evaluator keeps only the command being
// assembled, so memory is O(longest command), not O(file).  It depends only on tcl.h (no Pico SDK), so it is
// unit-tested on the PC (tests/host/tcl_stream_test.cpp), differentially against Tcl_Eval of the whole text.
//
// Semantics match "Tcl_Eval the whole file": commands run in order in the same interpreter; the first command
// that returns a code other than TCL_OK stops the script and that code (TCL_ERROR, TCL_RETURN, TCL_BYE=9, ...)
// is returned with the interpreter's result intact.  A command still incomplete at EOF is passed to Tcl_Eval
// as is, so Tcl itself reports "missing close-brace" etc.
#include <cstddef>
#include <string>
#include "tcl6.7c/tcl.h"

// read(ctx, buf, n): returns bytes placed in buf (0 = end of file, < 0 = I/O error).
typedef long (*TclStreamReader)(void* ctx, char* buf, size_t n);

struct TclStreamStats {
  size_t max_buffered = 0;   // largest command text held at once (bytes)
  size_t commands = 0;       // commands evaluated
};

constexpr size_t kTclStreamMaxCommand = 64 * 1024;   // a single command may not be larger than this

// Returns the Tcl result code.  On an I/O error or an over-long command, returns TCL_ERROR with a message.
inline int TclEvalStream(Tcl_Interp* interp, TclStreamReader read, void* ctx, TclStreamStats* stats = nullptr,
                         size_t max_command = kTclStreamMaxCommand) {
  std::string cmd;      // the command being assembled (whole lines)
  std::string line;     // the current partial line
  char buf[256];     // 256 = one tether-VFS read RPC (vfs.h chunks at 256)
  TclStreamStats local;
  TclStreamStats& st = stats ? *stats : local;

  auto run = [&](std::string& text) -> int {
    st.commands++;
    int rc = Tcl_Eval(interp, const_cast<char*>(text.c_str()), 0, nullptr);
    text.clear();
    return rc;
  };
  auto line_done = [&]() -> int {   // `line` is a full line (with its '\n'); add it to cmd, run cmd if complete
    cmd += line;
    line.clear();
    if (cmd.size() > st.max_buffered) st.max_buffered = cmd.size();
    if (cmd.size() > max_command) {
      Tcl_SetResult(interp, const_cast<char*>("source: command too long"), TCL_STATIC);
      return TCL_ERROR;
    }
    if (Tcl_CommandComplete(const_cast<char*>(cmd.c_str()))) return run(cmd);
    return TCL_OK;
  };

  for (;;) {
    long n = read(ctx, buf, sizeof buf);
    if (n < 0) {
      Tcl_SetResult(interp, const_cast<char*>("source: error reading file"), TCL_STATIC);
      return TCL_ERROR;
    }
    if (n == 0) break;
    for (long i = 0; i < n; i++) {
      line += buf[i];
      if (buf[i] == '\n') {
        int rc = line_done();
        if (rc != TCL_OK) return rc;
      } else if (line.size() + cmd.size() > max_command) {
        Tcl_SetResult(interp, const_cast<char*>("source: command too long"), TCL_STATIC);
        return TCL_ERROR;
      }
    }
  }
  cmd += line;                      // last line without a trailing newline
  if (cmd.size() > st.max_buffered) st.max_buffered = cmd.size();
  if (cmd.empty()) return TCL_OK;
  return run(cmd);                  // complete, or incomplete (Tcl reports the error)
}
