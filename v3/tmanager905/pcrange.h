#ifndef _PCRANGE_H_
#define _PCRANGE_H_

// Heuristic to stop the CPU if it goes crazy.

// DontPcRange: BadPc always returns false.

template <typename T>
struct DontPcRange {
  static constexpr bool DoesPcRange() { return false; }

  static constexpr bool BadPc(uint addr) { return false; }
};

// DoPcRange: BadPc returns true if the PC has wondered out of the
// range of MIN to MAX (inclusive).

template <typename T, uint MIN, uint MAX>
struct DoPcRange {
  static constexpr bool DoesPcRange() { return true; }

  static force_inline bool BadPc(uint addr) {
    // LIC can be asserted before interrupt vector fetch.
    if (addr >= 0xFFF0) return false;

    return addr < MIN || MAX < addr;
  }
};

#endif  // _PCRANGE_H_
