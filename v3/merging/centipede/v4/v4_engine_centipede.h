#ifndef V4_ENGINE_CENTIPEDE_H_
#define V4_ENGINE_CENTIPEDE_H_

// v4_engine_centipede.h — Centipede platform engine (STUB).
//
// Provides the Centipede-specific foreground inner loop, GPIO initialization,
// PIO setup, and interrupt pin control.
//
// The Centipede plugs into the CoCo's cartridge port. The RP2350B
// passively monitors E/Q clocks and selectively drives the data bus
// via PIO (Gerbil). It has:
//   - NMI (open-drain, edge-triggered)
//   - HALT (open-drain)
//   - CART (directly triggers FIRQ via SAM/GIME)
//   - SCS, CTS (active-low select signals from cartridge port)
//   - SLENB (active-low, tells CoCo we're driving data bus)
//   - No direct IRQ or FIRQ pin access.
// Address bus A0-A15 on GPIO 32-47 (high pins).
// Data bus D0-D7 on GPIO 0-7.
// R/W on GPIO 20, E(BAR) on GPIO 21, Q(BAR) on GPIO 22.

#include "v4_types.h"
#include "v4_ram.h"
#include "v4_core_engine.h"

// ── Centipede GPIO pin assignments ──
// These vary by hardware revision; this is for rev 32e.
namespace centipede_pins {
  constexpr uint RW     = 20;
  constexpr uint EBAR   = 21;
  constexpr uint QBAR   = 22;
  constexpr uint SND    = 23;
  constexpr uint LED    = 25;
  constexpr uint SCS    = 26;
  constexpr uint CART   = 27;
  constexpr uint SLENB  = 28;
  constexpr uint HALT   = 29;
  constexpr uint NMI    = 30;
  constexpr uint CTS    = 31;
  constexpr uint RESET  = 24;  // Active-low, directly from CoCo bus.
}  // namespace centipede_pins

// ── CentipedeEngine<T> ──
template <class T>
class CentipedeEngine : public CoreEngine<T> {
 public:

  // ── GPIO Initialization ──
  static void InitializePins() {
    using namespace centipede_pins;
    // Lower GPIO 0-22: data bus (0-7) and control signals (20-22).
    for (uint i = 0; i <= 22; i++) {
      gpio_init(i);
      gpio_set_dir(i, GPIO_IN);
      gpio_set_pulls(i, false, false);
    }
    // LED
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    gpio_put(LED, 1);

    // Open-drain pins: NMI, HALT, CART.
    // Set output value to 0, then control assertion via direction.
    auto open_drain = [](uint pin) {
      gpio_init(pin);
      gpio_set_dir(pin, GPIO_OUT);
      gpio_put(pin, 0);
      gpio_set_dir(pin, GPIO_IN);  // Released (pulled high externally).
      gpio_set_pulls(pin, true, false);
    };
    open_drain(HALT);
    open_drain(NMI);
    open_drain(CART);

    // High GPIO 32-47: address bus (active input).
    for (uint i = 32; i <= 47; i++) {
      gpio_init(i);
      gpio_set_dir(i, GPIO_IN);
      gpio_set_pulls(i, false, false);
    }
  }

  // ── PIO Initialization ──
  static void InitPIO() {
    // STUB: Load the Gerbil PIO program.
    // pio_set_gpio_base(pio0, 0);
    // const uint offset = pio_add_program(pio0, &gerbil_program);
    // gerbil_program_init(pio0, 0, offset);
  }

  // ── Interrupt Pin Control ──
  // Centipede has no direct IRQ/FIRQ pins — those are no-ops.
  // NMI is open-drain: assert by setting direction to output (pulls low).
  FORCE_INLINE static void AssertIRQPin() {
    // No direct IRQ pin on cartridge port.
  }
  FORCE_INLINE static void ReleaseIRQPin() {
    // No direct IRQ pin.
  }
  FORCE_INLINE static void AssertFIRQPin() {
    // Could potentially use CART pin to trigger FIRQ via SAM.
    // STUB: not implemented yet.
  }
  FORCE_INLINE static void ReleaseFIRQPin() {
    // STUB.
  }
  FORCE_INLINE static void AssertNMIPin() {
    gpio_set_dir(centipede_pins::NMI, GPIO_OUT);  // Pulls low (asserted).
  }
  FORCE_INLINE static void ReleaseNMIPin() {
    gpio_set_dir(centipede_pins::NMI, GPIO_IN);   // Released (pulled high).
  }

  // ── HALT control ──
  FORCE_INLINE static void HaltOn() {
    gpio_set_dir(centipede_pins::HALT, GPIO_OUT);
  }
  FORCE_INLINE static void HaltOff() {
    gpio_set_dir(centipede_pins::HALT, GPIO_IN);
  }

  // ── USB/COBS ──
  FORCE_INLINE static void PutCharRaw(byte ch) {
    putchar_raw(ch);
  }

  static void PollUsbInput() {
    // STUB: Read USB bytes into COBS decoder, dispatch packets.
  }

  // ── Foreground Inner Loop ──
  // STUB: The Centipede foreground loop that monitors E/Q via Gerbil PIO,
  // reads address from gpio_hi_in, handles reads (GERBIL_DRIVE/PASS)
  // and writes (GERBIL_GET), pushes to fg2bg FIFO.
  static void foreground() {
    T::InitializePins();
    T::InitPIO();

    // Main cycle loop would go here.
    // For each cycle:
    //   1. signals = GERBIL_GET()  — wait for PIO
    //   2. addr = volatile_sio_hw->gpio_hi_in & 0xFFFF
    //   3. FlowControlCheck()
    //   4. if reading: compute response, GERBIL_DRIVE(data) or GERBIL_PASS()
    //      if writing: data = GERBIL_GET(), store to ram[]
    //   5. PUSH_TO_BG for tracing
    while (true) {
      sleep_ms(100);
    }
  }
};

#endif  // V4_ENGINE_CENTIPEDE_H_
