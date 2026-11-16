#pragma once
#include <cstdint>

namespace cputracker {

enum RegMask : uint16_t {
  REG_A   = (1 << 0),
  REG_B   = (1 << 1),
  REG_DP  = (1 << 2),
  REG_X   = (1 << 3),
  REG_Y   = (1 << 4),
  REG_U   = (1 << 5),
  REG_S   = (1 << 6),
  REG_CC  = (1 << 7),
  REG_E   = (1 << 8),
  REG_F   = (1 << 9),
  REG_V   = (1 << 10),
  REG_MD  = (1 << 11),
};

// Condition Code Flags
enum CCFlags : uint8_t {
  CC_C = 0x01, // Carry
  CC_V = 0x02, // Overflow
  CC_Z = 0x04, // Zero
  CC_N = 0x08, // Negative
  CC_I = 0x10, // IRQ mask
  CC_H = 0x20, // Half-carry
  CC_F = 0x40, // FIRQ mask
  CC_E = 0x80, // Entire flag (1 = entire register set stacked)
};

struct RegisterState {
  uint8_t a = 0;
  uint8_t b = 0;
  uint8_t dp = 0;
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t u = 0;
  uint16_t s = 0;
  uint8_t cc = 0;
  uint16_t pc = 0;

  // 6309 Registers
  uint8_t e = 0;
  uint8_t f = 0;
  uint16_t v = 0;
  uint8_t md = 0; // Bit 0: 0 = 6809 Emulation Mode, 1 = 6309 Native Mode

  uint16_t valid_mask = 0;

  void Reset() {
    a = b = dp = cc = 0;
    x = y = u = s = pc = 0;
    e = f = v = md = 0;
    valid_mask = 0;
  }

  bool IsValid(RegMask reg) const {
    return (valid_mask & reg) != 0;
  }

  void SetValid(RegMask reg, bool valid = true) {
    if (valid) valid_mask |= reg;
    else valid_mask &= ~reg;
  }

  bool Is6309Native() const {
    return (md & 0x01) != 0;
  }

  uint16_t GetD() const {
    return (uint16_t(a) << 8) | b;
  }

  void SetD(uint16_t val) {
    a = uint8_t(val >> 8);
    b = uint8_t(val & 0xFF);
    valid_mask |= (REG_A | REG_B);
  }

  uint16_t GetW() const {
    return (uint16_t(e) << 8) | f;
  }

  void SetW(uint16_t val) {
    e = uint8_t(val >> 8);
    f = uint8_t(val & 0xFF);
    valid_mask |= (REG_E | REG_F);
  }

  // Branch condition evaluation if CC is valid
  // Returns: 1 = Branch Taken, 0 = Branch Not Taken, -1 = CC not valid / unknown
  int EvaluateBranch(uint8_t op) const {
    if (!IsValid(REG_CC)) return -1;

    bool c = (cc & CC_C) != 0;
    bool v = (cc & CC_V) != 0;
    bool z = (cc & CC_Z) != 0;
    bool n = (cc & CC_N) != 0;

    switch (op) {
      case 0x20: // BRA
      case 0x16: // LBRA
        return 1;
      case 0x21: // BRN
        return 0;
      case 0x22: // BHI (C | Z == 0)
        return (!c && !z) ? 1 : 0;
      case 0x23: // BLS (C | Z == 1)
        return (c || z) ? 1 : 0;
      case 0x24: // BCC (C == 0)
        return (!c) ? 1 : 0;
      case 0x25: // BCS (C == 1)
        return (c) ? 1 : 0;
      case 0x26: // BNE (Z == 0)
        return (!z) ? 1 : 0;
      case 0x27: // BEQ (Z == 1)
        return (z) ? 1 : 0;
      case 0x28: // BVC (V == 0)
        return (!v) ? 1 : 0;
      case 0x29: // BVS (V == 1)
        return (v) ? 1 : 0;
      case 0x2A: // BPL (N == 0)
        return (!n) ? 1 : 0;
      case 0x2B: // BMI (N == 1)
        return (n) ? 1 : 0;
      case 0x2C: // BGE (N ^ V == 0)
        return (n == v) ? 1 : 0;
      case 0x2D: // BLT (N ^ V == 1)
        return (n != v) ? 1 : 0;
      case 0x2E: // BGT (Z | (N ^ V) == 0)
        return (!z && (n == v)) ? 1 : 0;
      case 0x2F: // BLE (Z | (N ^ V) == 1)
        return (z || (n != v)) ? 1 : 0;
      default:
        return -1;
    }
  }
};

} // namespace cputracker
