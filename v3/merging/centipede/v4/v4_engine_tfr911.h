#ifndef V4_ENGINE_TFR911_H_
#define V4_ENGINE_TFR911_H_

// v4_engine_tfr911.h — TFR911H platform engine (STUB).
//
// Provides TFR911-specific GPIO initialization, PIO setup, interrupt
// pin control, and FORCE_INLINE helpers called from the foreground loop.
//
// The foreground inner loop itself is a free templated IN_RAM function
// (tfr911_foreground_loop<T>) defined at the bottom of this file.
// GCC does not support __attribute__((section)) on class methods, so
// the loop must be a free function to get placed in RAM. All T::methods
// it calls are FORCE_INLINE and will be inlined into the IN_RAM body.
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
// Contains only FORCE_INLINE helpers and non-hot-path setup functions.
// The foreground loop is the free function tfr911_foreground_loop<T>() below.
template <class T>
class TFR911Engine : public CoreEngine<T> {
 public:

  // ── GPIO Initialization (IN_FLASH, called once at boot) ──
  static void IN_FLASH InitializePins() {
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

  // ── PIO Initialization (IN_FLASH, called once) ──
  static void IN_FLASH InitPIO() {
    // STUB: Load the t911veryfast PIO program.
    // pio_clear_instruction_memory(pio0);
    // const uint offset = pio_add_program(pio0, &t911veryfast_program);
    // t911veryfast_program_init(pio0, 0, offset);
  }

  // ── Interrupt Pin Control (FORCE_INLINE, called from inner loop) ──
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

  // ── HALT control (FORCE_INLINE) ──
  // TFR911 has a direct HALT pin. Future: for flow control or
  // waiting on disk I/O.
  FORCE_INLINE static void HaltOn() {
    gpio_put(tfr911_pins::HALT, 0);
  }
  FORCE_INLINE static void HaltOff() {
    gpio_put(tfr911_pins::HALT, 1);
  }

  // ── Reset sequence (IN_FLASH, called once before CPU starts) ──
  // The TFR911 generates E/Q clock phases via PIO; during reset,
  // we drive the clocks directly via GPIO.
  static void IN_FLASH RunReset() {
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

  // ── USB/COBS (FORCE_INLINE for hot path) ──
  FORCE_INLINE static void PutCharRaw(byte ch) {
    putchar_raw(ch);  // Pico SDK stdio USB.
  }

  static void PollUsbInput() {
    // STUB: Read USB bytes into COBS decoder, dispatch packets.
  }

  // ── Inner loop helpers (FORCE_INLINE) ──
  // These will be called from tfr911_foreground_loop<T>() and
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
    return 0;
  }

  FORCE_INLINE static void WriteIO(uint addr, byte value) {
    IOWriter fn = IOWriters[addr & 0xFF];
    if (fn) fn(addr, value);
  }
};

// ══════════════════════════════════════════════════════════════════
// tfr911_foreground_loop<T>() — The TFR911 foreground inner loop.
//
// This is a FREE FUNCTION marked IN_RAM so GCC places it in the
// .time_critical section. All T::methods it calls are FORCE_INLINE
// and will be compiled directly into this function body, keeping
// the entire hot path in RAM with zero FLASH fetch stalls.
//
// Called from an IN_RAM trampoline:
//   void IN_RAM core1_trampoline() {
//       tfr911_foreground_loop<Engine>();
//   }
// ══════════════════════════════════════════════════════════════════
template <typename T>
void IN_RAM tfr911_foreground_loop() {
  // One-time setup (these may call IN_FLASH functions, which is fine
  // because they run before the real-time inner loop starts).
  T::Install_OS();
  T::RunReset();
  T::InitPIO();

  // volatile sio_hw_t* hw = (volatile sio_hw_t*)sio_hw;

  // STUB: The real inner loop will be:
  //
  //   while (true) {
  //     // OUTER LOOP: poll interrupts, USB, terminal input.
  //     irq_needed |= T::Turbo9sim_IrqNeeded();
  //     if (irq_needed != prev_irq_needed) {
  //       if (irq_needed) T::AssertIRQPin(); else T::ReleaseIRQPin();
  //       prev_irq_needed = irq_needed;
  //     }
  //     T::PollUsbInput();  // Note: not FORCE_INLINE, but called rarely.
  //
  //     // INNER LOOP: bus cycles.
  //     for (int i = 0; i < GROUP_SIZE; i++) {
  //       pio_sm_put(pio0, 0, 0);          // sync
  //       uint pins = pio_sm_get_blocking(pio0, 0);  // early pins
  //       uint addr = 0xFFFF & hw->gpio_hi_in;       // address bus
  //       // ... settle loop ...
  //
  //       bool reading = (pins & (1 << R_W));
  //       if (LIKELY(reading)) {
  //         byte value;
  //         if (LIKELY(addr < 0xFF00))
  //           value = T::ReadRam(addr);     // FORCE_INLINE
  //         else
  //           value = T::ReadIO(addr);      // FORCE_INLINE
  //         pio_sm_put(pio0, 0, value);     // drive data bus
  //         uint late_pins = pio_sm_get_blocking(pio0, 0);
  //         // ... FIC/LIC check ...
  //       } else {
  //         uint late_pins = pio_sm_get_blocking(pio0, 0);
  //         byte value = (byte)late_pins;
  //         if (LIKELY(addr < 0xFF00)) {
  //           T::WriteRam(addr, value);     // FORCE_INLINE
  //           T::OnWriteTrace(addr, value); // FORCE_INLINE (or no-op)
  //         } else {
  //           T::WriteIO(addr, value);      // FORCE_INLINE
  //         }
  //       }
  //     }  // end inner loop
  //   }  // end outer loop

  while (true) {
    // STUB: sleep to prevent busy-spin in stub builds.
    sleep_ms(100);
  }
}

#endif  // V4_ENGINE_TFR911_H_
