// v4_tfr911_main.cpp — Top-level wiring for TFR911H v4 build (STUB).
//
// This file shows how to compose the final Engine struct from CRTP mixins
// and wire up the IN_RAM trampolines for the foreground and background loops.
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

// ── Global storage (BSS — always in RAM) ──
byte ram[64 * 1024];
IOReader IOReaders[256];
IOWriter IOWriters[256];
byte vector_ram[16];

// ── The Engine ──
// Compose the TFR911 Engine from CRTP mixins.
// All methods that are called from the foreground inner loop
// are FORCE_INLINE static, so they compile into the IN_RAM
// tfr911_foreground_loop<Engine>() function body.
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

// ── IN_RAM Trampoline functions ──
// These are the entry points for each core. They are free functions
// so they can carry the IN_RAM attribute (GCC limitation: section
// attributes don't work on class methods).
//
// The templated foreground/background loops are also free IN_RAM
// functions. All T::method() calls from within them are FORCE_INLINE,
// so the entire hot path compiles into RAM with zero FLASH stalls.

void IN_RAM core1_trampoline() {
  tfr911_foreground_loop<Engine>();
}

void IN_RAM core0_trampoline() {
  v4_background_loop<Engine>();
}

#if 0  // Not buildable yet without Pico SDK.
int main() {
  // 1. Clock setup.
  // set_sys_clock_khz(250 * 1000, true);
  // stdio_usb_init();

  // 2. GPIO (IN_FLASH — called once at boot).
  Engine::InitializePins();

  // 3. LED blink to indicate boot.
  // for (int i = 0; i < 3; i++) { ... }

  // 4. Flash label.
  // FlashLabel::InitLabel();
  // FlashLabel::PrintLabel();

  // 5. Tcl console (blocks until "bye").
  Engine::RunConsole();

  // 6. Install turbo9sim ACIA (IN_FLASH — called once).
  Engine::Turbo9sim_Install(0xFF00);

  // 7. Launch foreground on core1, background on core0.
  // Both trampolines are IN_RAM free functions.
  Engine::RunCores(core1_trampoline, core0_trampoline);

  // Never reached.
  while (true) sleep_ms(1000);
}
#endif
