#ifndef V4_OS_LOADER_H_
#define V4_OS_LOADER_H_

// v4_os_loader.h — CRTP mixins for loading an OS into ram[].
//
// DoTurbo9os<T, ROMLIST> — Loads a linked ROM list into ram[] and
//   installs 6809 vectors. Used by TFR911 when booting TurbOS9.
//
// DontOS<T> — No-op. OS loading is handled by Tcl commands instead.
//
// The romlist.h template from the existing codebase is reused here.

#include "v4_types.h"
#include "v4_ram.h"

// ── RomList ──
// Compile-time linked list of ROM images, concatenated in order.
// Usage: RomList<rom1, rom2, rom3>

template <const byte* First, size_t FirstSize>
struct SingleRom {
  static constexpr size_t total_size = FirstSize;
  static const byte* data() { return First; }
};

// Forward declaration — the actual romlist.h from the codebase provides this.
// We include a simplified stub here for the framework API.
template <const byte* ... Roms>
struct RomList;

// ── DontOS<T> ──
template <typename T>
struct DontOS {
  static void Install_OS() {
    // No-op: OS loading is handled by Tcl commands.
  }
};

// ── DoTurbo9os<T> ──
// Loads a ROM image into ram[] ending just before $FF00,
// installs the OS9 vectors, and sets the RESET vector to the
// kernel entry point.
template <typename T>
struct DoTurbo9os {
  static void Install_OS() {
    T::ShowString("DoTurbo9os Install_OS...\n");

    // Install standard OS9 vectors at $FFF0–$FFFC.
    // Index 0=$FFF0, 1=$FFF2, ..., 6=$FFFC, 7=$FFFE(RESET).
    static const uint vectors[] = {
        0x0000, 0x0100, 0x0103, 0x010F, 0x010C, 0x0106, 0x0109,
    };
    for (uint i = 0; i < 7; i++) {
      InstallVector(i, vectors[i]);
    }

    // The actual ROM data and entry point setup is done by the
    // platform-specific build, which calls T::LoadRomData().
    T::LoadRomData();
  }
};

#endif  // V4_OS_LOADER_H_
