#ifndef V4_TURBO9SIM_H_
#define V4_TURBO9SIM_H_

// v4_turbo9sim.h — Simulated ACIA (serial port) device for TurbOS9.
//
// This is the memory-mapped terminal device used by NitrOS9/TurbOS9
// on the TFR911. It provides:
//   base+0: TX data register (write: send char; read: last char sent)
//   base+1: RX data register (read: consume received char)
//   base+2: Status register (bit 0 = timer, bit 1 = RX ready)
//   base+3: Control register (interrupt enable mask)
//
// The Centipede does not use this (it uses PIA-based I/O), so it
// inherits DontTurbo9sim<T> instead.

#include "v4_types.h"
#include "v4_ram.h"

// Shared state for the simulated ACIA.
inline bool sim_timer_irq;
inline bool sim_rx_ready_irq;
inline byte sim_status_reg;
inline byte sim_control_reg;
inline byte sim_last_char_rx;
inline byte sim_last_char_tx;

// ── DontTurbo9sim<T> ──
template <typename T>
struct DontTurbo9sim {
  constexpr static bool Does_Turbo9sim() { return false; }
  constexpr static bool Turbo9sim_IrqNeeded() { return false; }
  FORCE_INLINE static void Turbo9sim_SetTimerFired() {}
  constexpr static bool Turbo9sim_CanRx() { return false; }
  FORCE_INLINE static void Turbo9sim_SetRx(byte ch) {}
  static void Turbo9sim_Install(uint base) {}
};

// ── DoTurbo9sim<T> ──
template <typename T>
struct DoTurbo9sim {
  static constexpr byte SIM_TIMER_BIT = 0x01;
  static constexpr byte SIM_RX_BIT    = 0x02;

  constexpr static bool Does_Turbo9sim() { return true; }

  FORCE_INLINE static bool Turbo9sim_IrqNeeded() {
    return (sim_status_reg & sim_control_reg) != 0;
  }

  FORCE_INLINE static void Turbo9sim_SetTimerFired() {
    sim_status_reg |= SIM_TIMER_BIT;
  }

  FORCE_INLINE static bool Turbo9sim_CanRx() {
    return !(sim_status_reg & SIM_RX_BIT);
  }

  FORCE_INLINE static void Turbo9sim_SetRx(byte ch) {
    sim_status_reg |= SIM_RX_BIT;
    sim_last_char_rx = ch;
  }

  // Install the ACIA at the given I/O base address ($FFxx).
  static void Turbo9sim_Install(uint base) {
    base &= 0xFF;

    // Readers
    IOReaders[base + 0] = [](uint) -> byte { return sim_last_char_tx; };
    IOReaders[base + 1] = [](uint) -> byte {
      sim_status_reg &= ~SIM_RX_BIT;  // Consume the char.
      return sim_last_char_rx;
    };
    IOReaders[base + 2] = [](uint) -> byte { return sim_status_reg; };
    IOReaders[base + 3] = [](uint) -> byte { return sim_control_reg; };

    // Writers
    IOWriters[base + 0] = [](uint, byte data) {
      sim_last_char_tx = data;
      // Send to tether via COBS C_PUTCHAR.
      T::ShowChar(data);
    };
    IOWriters[base + 1] = [](uint, byte) {
      // No effect (write to RX register).
    };
    IOWriters[base + 2] = [](uint, byte data) {
      // Writing to status register clears interrupt flags.
      if (data & SIM_TIMER_BIT) {
        sim_timer_irq = false;
        sim_status_reg &= ~SIM_TIMER_BIT;
      }
      if (data & SIM_RX_BIT) {
        sim_rx_ready_irq = false;
        sim_status_reg &= ~SIM_RX_BIT;
      }
    };
    IOWriters[base + 3] = [](uint, byte data) {
      sim_control_reg = data;
    };
  }
};

#endif  // V4_TURBO9SIM_H_
