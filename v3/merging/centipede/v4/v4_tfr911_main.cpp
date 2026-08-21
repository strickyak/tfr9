// v4_tfr911_main.cpp — Top-level wiring for TFR911H v4 build (STUB).
//
// This file shows how to compose the final Engine struct from CRTP mixins.
// It is not yet buildable — it requires the Pico SDK and PIO programs.
//
// Build: cmake with PLATFORM=TFR911.

// ── Pico SDK headers (would be included in a real build) ──
// #include <pico/stdlib.h>
// #include <pico/multicore.h>
// #include <hardware/pio.h>
// #include "pio_veryturbos.pio.h"

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
#include "v4_engine_tfr911.h"

// ── Global storage ──
byte ram[64 * 1024];
IOReader IOReaders[256];
IOWriter IOWriters[256];
byte vector_ram[16];

// ── The Engine ──
// Compose the TFR911 Engine from CRTP mixins.
struct Engine : public DoTurbo9os<Engine>,
                public DoTurbo9sim<Engine>,
                public DoInterrupts<Engine>,
                public DoTrace<Engine>,
                public DontFloppy<Engine>,
                public DoConsole<Engine>,
                public TFR911Engine<Engine> {

  // LoadRomData is called by DoTurbo9os::Install_OS().
  // It copies the ROM image into ram[] and sets the RESET vector.
  static void LoadRomData() {
    // STUB: Will load from compiled-in ROM arrays.
    ShowString("LoadRomData: STUB\n");
  }
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

  // 3. LED blink to indicate boot.
  // for (int i = 0; i < 3; i++) { ... }

  // 4. Flash label.
  // FlashLabel::InitLabel();
  // FlashLabel::PrintLabel();

  // 5. Tcl console (blocks until "bye").
  Engine::RunConsole();

  // 6. Install turbo9sim ACIA.
  Engine::Turbo9sim_Install(0xFF00);

  // 7. Launch foreground on core1, background on core0.
  Engine::RunCores(core1_trampoline, core0_trampoline);

  // Never reached.
  while (true) sleep_ms(1000);
}
#endif
