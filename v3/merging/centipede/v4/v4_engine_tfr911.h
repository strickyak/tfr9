#ifndef V4_ENGINE_TFR911_H_
#define V4_ENGINE_TFR911_H_

// v4_engine_tfr911.h — TFR911H platform engine (STUB).
//
// Provides the TFR911-specific foreground inner loop, GPIO initialization,
// PIO setup, and interrupt pin control.
//
// The TFR911H is a full motherboard replacement for the CoCo.
// The RP2350B generates E/Q clocks via PIO side-set and has direct
// access to all 6809E pins: RESET, NMI, IRQ, FIRQ, HALT, LIC, AVMA, BS.
// Address bus A0-A15 on GPIO 32-47 (high pins).
// Data bus D0-D7 on GPIO 0-7.
// R/W on GPIO 31, E on GPIO 29, Q on GPIO 30.

#include "v4_types.h"
#include "v4_ram.h"
#include "v4_core_engine.h"

// ── TFR911 GPIO pin assignments ──
namespace tfr911_pins {
  constexpr uint RESET = 20;
  constexpr uint NMI   = 21;
  constexpr uint IRQ   = 22;
  constexpr uint FIRQ  = 23;
  constexpr uint HALT  = 24;
  constexpr uint LED   = 25;
  constexpr uint LIC   = 26;
  constexpr uint AVMA  = 27;
  constexpr uint BS    = 28;
  constexpr uint E     = 29;
  constexpr uint Q     = 30;
  constexpr uint R_W   = 31;
}  // namespace tfr911_pins

// ── TFR911Engine<T> ──
// CRTP mixin providing TFR911-specific hardware interface.
template <class T>
class TFR911Engine : public CoreEngine<T> {
 public:

  // ── GPIO Initialization ──
  static void InitializePins() {
    using namespace tfr911_pins;
    for (uint i = 0; i < 48; i++) {
      gpio_init(i);
      switch (i) {
        case RESET: case NMI: case IRQ: case FIRQ:
        case HALT: case E: case Q: case LED:
          gpio_set_dir(i, GPIO_OUT);
          gpio_put(i, 1);  // Active-low: deassert all.
          break;
        default:
          gpio_set_dir(i, GPIO_IN);
          gpio_pull_up(i);
          break;
      }
    }
  }

  // ── PIO Initialization ──
  static void InitPIO() {
    // STUB: Load the t911veryfast PIO program.
    // pio_clear_instruction_memory(pio0);
    // const uint offset = pio_add_program(pio0, &t911veryfast_program);
    // t911veryfast_program_init(pio0, 0, offset);
  }

  // ── Interrupt Pin Control ──
  // TFR911 has direct push-pull GPIO connections to all interrupt pins.
  FORCE_INLINE static void AssertIRQPin() {
    gpio_put(tfr911_pins::IRQ, 0);  // Active low.
  }
  FORCE_INLINE static void ReleaseIRQPin() {
    gpio_put(tfr911_pins::IRQ, 1);
  }
  FORCE_INLINE static void AssertFIRQPin() {
    gpio_put(tfr911_pins::FIRQ, 0);
  }
  FORCE_INLINE static void ReleaseFIRQPin() {
    gpio_put(tfr911_pins::FIRQ, 1);
  }
  FORCE_INLINE static void AssertNMIPin() {
    gpio_put(tfr911_pins::NMI, 0);
  }
  FORCE_INLINE static void ReleaseNMIPin() {
    gpio_put(tfr911_pins::NMI, 1);
  }

  // ── Reset sequence ──
  // The TFR911 generates E/Q clock phases via PIO; during reset,
  // we drive the clocks directly.
  static void RunReset() {
    using namespace tfr911_pins;
    gpio_put(RESET, 0);
    for (int i = 0; i < 1000; i++) {
      sleep_us(10); gpio_put(Q, 1);
      sleep_us(10); gpio_put(E, 1);
      sleep_us(10); gpio_put(Q, 0);
      sleep_us(10); gpio_put(E, 0);
    }
    gpio_put(RESET, 1);
  }

  // ── USB/COBS ──
  FORCE_INLINE static void PutCharRaw(byte ch) {
    putchar_raw(ch);  // Pico SDK stdio USB.
  }

  static void PollUsbInput() {
    // STUB: Read USB bytes into COBS decoder, dispatch packets.
  }

  // ── Foreground Inner Loop ──
  // STUB: The TFR911 foreground loop that generates E/Q clocks,
  // reads the address bus from gpio_hi_in, handles reads/writes
  // via PIO FIFO, and calls T::OnReadTrace/OnWriteTrace/OnFICTrace.
  static void foreground() {
    T::Install_OS();
    T::RunReset();
    T::InitPIO();

    // Main cycle loop would go here.
    // For each cycle:
    //   1. pio_sm_put(pio0, 0, 0)  — sync
    //   2. pins = pio_sm_get_blocking()  — early pins
    //   3. addr = hw->gpio_hi_in & 0xFFFF  — address
    //   4. if reading: compute value, pio_sm_put value, get late pins
    //      if writing: value = pio_sm_get_blocking(), store to ram[]
    //   5. Call T::OnReadTrace/OnWriteTrace/OnFICTrace as appropriate
    while (true) {
      // STUB: sleep to prevent busy-spin in stub builds.
      sleep_ms(100);
    }
  }
};

#endif  // V4_ENGINE_TFR911_H_
