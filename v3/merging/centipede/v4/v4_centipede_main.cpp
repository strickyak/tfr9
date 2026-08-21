// v4_centipede_main.cpp — Top-level wiring for Centipede v4 build (STUB).
//
// This file shows how to compose the final Engine struct from CRTP mixins.
// It is not yet buildable — it requires the Pico SDK and PIO programs.
//
// Build: cmake with PLATFORM=CENTIPEDE.

// ── Pico SDK headers (would be included in a real build) ──
// #include <pico/stdlib.h>
// #include <pico/multicore.h>
// #include <hardware/pio.h>
// #include "gerbil.pio.h"

// ── v4 framework headers ──
#include "v4_types.h"
#include "v4_ram.h"
#include "v4_interrupts.h"
#include "v4_turbo9sim.h"
#include "v4_trace.h"
#include "v4_floppy.h"
#include "v4_os_loader.h"
#include "v4_console.h"
#include "v4_core_engine.h"
#include "v4_engine_centipede.h"

// ── Global storage ──
byte ram[64 * 1024];
IOReader IOReaders[256];
IOWriter IOWriters[256];
byte vector_ram[16];

// ── The Engine ──
// Compose the Centipede Engine from CRTP mixins.
struct Engine : public DontOS<Engine>,         // OS loaded by Tcl, not compiled-in ROM.
                public DontTurbo9sim<Engine>,   // Uses PIA-based I/O, not sim ACIA.
                public DoInterrupts<Engine>,
                public DontTrace<Engine>,       // Use DoTrace if trace is desired.
                public DoFloppy<Engine>,
                public DoConsole<Engine>,
                public CentipedeEngine<Engine> {
};

// ── Trampoline functions ──
void core1_trampoline() { Engine::foreground(); }
void core0_trampoline() { Engine::background(); }

#if 0  // Not buildable yet without Pico SDK.
int main() {
  // 1. Clock setup.
  // set_sys_clock_khz(250 * 1000, true);
  // stdio_usb_init();

  // 2. GPIO.
  Engine::InitializePins();

  // 3. LED blink.
  // ...

  // 4. Flash label.
  // FlashLabel::InitLabel();
  // FlashLabel::PrintLabel();

  // 5. Tcl console (blocks until "bye").
  Engine::RunConsole();

  // 6. Launch foreground on core1, background on core0.
  Engine::RunCores(core1_trampoline, core0_trampoline);

  // Never reached.
  while (true) sleep_ms(1000);
}
#endif
