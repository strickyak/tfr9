#ifndef V4_ENGINE_CENTIPEDE_H_
#define V4_ENGINE_CENTIPEDE_H_

// v4_engine_centipede.h — Centipede platform engine (STUB).
//
// Provides Centipede-specific GPIO initialization, PIO setup, interrupt
// pin control, and FORCE_INLINE helpers called from the foreground loop.
//
// The foreground inner loop itself is a free templated IN_RAM function
// (centipede_foreground_loop<T>) defined at the bottom of this file.
// GCC does not support __attribute__((section)) on class methods, so
// the loop must be a free function to get placed in RAM.
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
// CRTP mixin providing Centipede-specific hardware interface.
// Contains only FORCE_INLINE helpers and non-hot-path setup functions.
// The foreground loop is the free function centipede_foreground_loop<T>() below.
template <class T>
class CentipedeEngine : public CoreEngine<T> {
 public:

  // ── GPIO Initialization (IN_FLASH, called once at boot) ──
  static void IN_FLASH InitializePins() {
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

  // ── PIO Initialization (IN_FLASH, called once) ──
  static void IN_FLASH InitPIO() {
    // STUB: Load the Gerbil PIO program.
    // pio_set_gpio_base(pio0, 0);
    // const uint offset = pio_add_program(pio0, &gerbil_program);
    // gerbil_program_init(pio0, 0, offset);
  }

  // ── Interrupt Pin Control (FORCE_INLINE, called from inner loop) ──
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

  // ── HALT control (FORCE_INLINE, called from inner loop) ──
  FORCE_INLINE static void HaltOn() {
    gpio_set_dir(centipede_pins::HALT, GPIO_OUT);
  }
  FORCE_INLINE static void HaltOff() {
    gpio_set_dir(centipede_pins::HALT, GPIO_IN);
  }

  // ── Flow control (FORCE_INLINE, called every bus cycle) ──
  FORCE_INLINE static void FlowControlCheck() {
    if (fg_halt_for_flow_control) {
      if (fg2bg.size() < FG2BG_LOW_WATERMARK) {
        T::HaltOff();
        fg_halt_for_flow_control = false;
      }
    } else {
      if (fg2bg.size() > FG2BG_HIGH_WATERMARK) {
        T::HaltOn();
        fg_halt_for_flow_control = true;
      }
    }
  }

  // ── USB/COBS ──
  FORCE_INLINE static void PutCharRaw(byte ch) {
    putchar_raw(ch);
  }

  static void PollUsbInput() {
    // STUB: Read USB bytes into COBS decoder, dispatch packets.
  }

  // ── Inner loop helpers (FORCE_INLINE) ──
  // These will be called from centipede_foreground_loop<T>() and
  // will be inlined into the IN_RAM function body.

  FORCE_INLINE static byte ReadRam(uint addr) {
    return ram[addr];
  }

  FORCE_INLINE static void WriteRam(uint addr, byte value) {
    ram[addr] = value;
  }

  FORCE_INLINE static byte ReadIO(uint addr) {
    IOReader fn = IOReaders[addr & 0xFF];
    if (fn) return fn(addr);
    return 0xFF;  // Centipede returns 0xFF for unhandled reads.
  }

  FORCE_INLINE static void WriteIO(uint addr, byte value) {
    IOWriter fn = IOWriters[addr & 0xFF];
    if (fn) fn(addr, value);
    ram[addr] = value;  // Centipede mirrors I/O writes to RAM.
  }

  // ── Gerbil PIO helpers (FORCE_INLINE) ──
  // These wrap the PIO FIFO get/put operations.
  // In the real build, pio and sm will be globals or constexpr.
  // FORCE_INLINE static uint GerbilGet() {
  //   return gerbil_program_get_word(pio, sm);
  // }
  // FORCE_INLINE static void GerbilDrive(byte data) {
  //   gerbil_program_put_word(pio, sm, 0x100 | data);
  // }
  // FORCE_INLINE static void GerbilPass() {
  //   gerbil_program_put_word(pio, sm, 0);
  // }
};

// ══════════════════════════════════════════════════════════════════
// centipede_foreground_loop<T>() — The Centipede foreground inner loop.
//
// This is a FREE FUNCTION marked IN_RAM so GCC places it in the
// .time_critical section. All T::methods it calls are FORCE_INLINE
// and will be compiled directly into this function body, keeping
// the entire hot path in RAM with zero FLASH fetch stalls.
//
// This is CRITICAL for the Centipede: if any code in this loop
// causes a FLASH cache miss, the Pico will stall and miss bus
// cycles on the Gerbil PIO wheel, causing data corruption.
//
// Called from an IN_RAM trampoline:
//   void IN_RAM core1_trampoline() {
//       centipede_foreground_loop<Engine>();
//   }
// ══════════════════════════════════════════════════════════════════
template <typename T>
void IN_RAM centipede_foreground_loop() {
  T::InitializePins();
  T::InitPIO();

  // STUB: The real inner loop will be:
  //
  //   while (true) {
  //     const uint signals = T::GerbilGet();     // FORCE_INLINE
  //     T::FlowControlCheck();                   // FORCE_INLINE
  //
  //     const bool reading = ((signals & (1u << G_RW)) != 0);
  //     const uint abus = volatile_sio_hw->gpio_hi_in & 0xFFFF;
  //     byte dbus = 0x00;
  //
  //     if (LIKELY(reading)) {
  //       if (0xFF00 <= abus) {
  //         dbus = T::ReadIO(abus);              // FORCE_INLINE
  //         T::GerbilDrive(dbus);                // FORCE_INLINE
  //       } else {
  //         dbus = T::ReadRam(abus);             // FORCE_INLINE
  //         T::GerbilDrive(dbus);                // FORCE_INLINE
  //       }
  //       T::PushFifoRead(abus, dbus);           // FORCE_INLINE
  //     } else {
  //       dbus = (byte)(T::GerbilGet());         // FORCE_INLINE
  //       if (0xFF00 <= abus) {
  //         T::WriteIO(abus, dbus);              // FORCE_INLINE
  //       } else {
  //         T::WriteRam(abus, dbus);             // FORCE_INLINE
  //       }
  //       T::PushFifoWrite(abus, dbus);          // FORCE_INLINE
  //     }
  //     // All called methods are FORCE_INLINE → zero call overhead.
  //     // IOReaders[]/IOWriters[] function pointers are the only
  //     // indirect calls, and those are unavoidable.
  //   }

  while (true) {
    sleep_ms(100);
  }
}

#endif  // V4_ENGINE_CENTIPEDE_H_
