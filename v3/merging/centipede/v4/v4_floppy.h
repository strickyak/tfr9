#ifndef V4_FLOPPY_H_
#define V4_FLOPPY_H_

// v4_floppy.h — CRTP floppy disk emulation mixins (STUB).
//
// DoFloppy<T> — Full floppy disk emulation via SCS/CTS select lines.
//   Handles DSKCON-style sector reads and writes, NMI pulsing,
//   and 256-byte data transfers. Only meaningful on Centipede
//   (which has the SCS/CTS lines from the cartridge port).
//
// DontFloppy<T> — No-ops. Used when floppy emulation is not needed
//   (e.g., TFR911 which uses tethered disk I/O instead).

#include "v4_types.h"

template <typename T>
struct DontFloppy {
  static void BackgroundFifoFloppyLatch(byte chore_byte) {}
  static void BackgroundFifoFloppyCommand(/* Coro& self, */ uint chore, byte chore_byte) {}
  static void BackgroundFifoFloppyW256(/* Coro& self */) {}
  static void ReadScsFloppy(const uint& abus, byte& dbus) { dbus = 0xFF; }
  static void WriteScsFloppy(const uint& abus, byte& dbus) {}
};

template <typename T>
struct DoFloppy {
  // STUB: Full implementation will be ported from v1/firmware/floppy.h.
  static void BackgroundFifoFloppyLatch(byte chore_byte) {}
  static void BackgroundFifoFloppyCommand(/* Coro& self, */ uint chore, byte chore_byte) {}
  static void BackgroundFifoFloppyW256(/* Coro& self */) {}
  static void ReadScsFloppy(const uint& abus, byte& dbus) { dbus = 0xFF; }
  static void WriteScsFloppy(const uint& abus, byte& dbus) {}
};

#endif  // V4_FLOPPY_H_
