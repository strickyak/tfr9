#ifndef V4_CONSOLE_H_
#define V4_CONSOLE_H_

// v4_console.h — Tcl console and spoonfeeder (STUB).
//
// On both TFR911 and Centipede, when the firmware boots, the user
// sees a "TCL>" prompt (displayed via C_PUTCHAR over USB to the tether).
// The Tcl interpreter runs on the RP2350 (background core).
//
// The user can:
//   - Poke bytes into ram[]: "poke $FF00 $34"
//   - Load files from littlefs: "source /boot.tcl"
//   - Configure devices: "turbo9sim install $FF00"
//   - Start the 6809 CPU: "bye"
//
// When "bye" is executed, the background signals the foreground to
// release RESET and begin running the 6809.
//
// Quick tether commands (T_COMMAND packets) also feed into this console.

#include "v4_types.h"

// ── DoConsole<T> ──
// Runs the Tcl interpreter on the background core.
template <typename T>
struct DoConsole {
  // Called after boot to display prompt and accept commands.
  static void RunConsole() {
    T::ShowString("TCL> ");
    // STUB: Initialize tcl6.7c interpreter, install built-in commands
    // (poke, peek, load, bye, restart, etc.), then enter REPL loop.
    //
    // The REPL reads characters from the tether (via COBS C_PUTCHAR
    // or bare-byte keystrokes) and feeds them to the interpreter.
    //
    // When "bye" is executed, set a flag to exit console mode and
    // begin running the 6809.
  }

  // Called when a T_COMMAND packet arrives from the tether.
  static void HandleTclCommand(const char* cmd, size_t len) {
    // STUB: Feed cmd to the Tcl interpreter and return the result
    // via COBS C_PUTCHAR.
  }

  // Called when a T_PICO_RPC packet arrives (remote procedure call).
  static void HandlePicoRpc(const byte* data, size_t len) {
    // STUB: Decode PCB-encoded RPC, dispatch to handler, send response.
  }
};

// ── DontConsole<T> ──
// No Tcl console. Boot straight into the 6809.
template <typename T>
struct DontConsole {
  static void RunConsole() {}
  static void HandleTclCommand(const char*, size_t) {}
  static void HandlePicoRpc(const byte*, size_t) {}
};

#endif  // V4_CONSOLE_H_
