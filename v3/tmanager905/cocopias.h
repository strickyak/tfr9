#ifndef _COCOPIAS_H_
#define _COCOPIAS_H_

constexpr const char* KB_NORMAL =
    "@ABCDEFGHIJKLMNOPQRSTUVWXYZ\204\205\206\207 "
    "0123456789:;,-./\r\014\033\000\000\201\202\000";
constexpr const char* KB_SHIFT =
    "`abcdefghijklmnopqrstuvwxyz____ 0!\"#$%&'()*+<=>?___..__.";
constexpr const char* KB_CTRL =
    ".................................|.~...^[]..{_}\\........";

enum CKS {
  CKS_Empty,    // ready to receive a char
  CKS_Filled1,  // key pressed
  CKS_Filled2,
  CKS_Filled3,
  CKS_Filled4,
  CKS_Filled5,
  CKS_Wait1,  // key released
  CKS_Wait2,
  CKS_Wait3,
  CKS_Wait4,
  CKS_Wait5,
  CKS_Limit,  // Go back to Empty.
} CocoKeyboardState;
uint CocoKeyboardChar;

static byte Keyboard_ComputeSense(byte probe, byte ch, LOGGER logf) {
  bool shifted = false, controlled = false;
  byte sense = 0;
  probe = ~probe;  // Change to positive logic
                   // Use positive logic here.
  for (uint j = 0; j < 8; j++) {
    for (uint i = 0; i < 7; i++) {
      if (KB_NORMAL[i * 8 + j] == ch) {
        if ((byte(1 << j) & probe) != 0) {
          sense |= 1 << i;
        }
      } else if (KB_SHIFT[i * 8 + j] == ch && ch != '.') {
        if ((byte(1 << j) & probe) != 0) {
          sense |= byte(1 << i);
        }
        shifted = true;
      } else if (KB_CTRL[i * 8 + j] == ch && ch != '.') {
        if ((byte(1 << j) & probe) != 0) {
          sense |= byte(1 << i);
        }
        controlled = true;
      }
    }
  }
  if (shifted && (probe & 0x80) != 0) {
    sense |= 0x40;  // Shift key.
  }
  if (controlled && (probe & 0x10) != 0) {
    sense |= 0x40;  // Ctrl key.
  }
  logf("Keyboard_ComputeSense: probe $%x char $%x sense $%x shifted $%x", probe,
       ch, sense, shifted);
  return ~sense;  // Change to negative logic
}

struct Pia {
  const char* name;
  uint addr;
  uint dirA, dirB;
  uint inA, inB;
  uint outA, outB;
  uint controlA, controlB;

  bool enableIrqA, enableIrqB;
  bool irqA, irqB;

  Pia()
      : name("pia?"),
        addr(0xFFFF),
        dirA(0),
        inA(0xFF),
        outA(0xFF),
        dirB(0),
        inB(0xFF),
        outB(0xFF),
        controlA(0),
        controlB(0) {}

  bool PortADataDirectionSelected() const { return 0 == (controlA & 0x04); }
  bool PortBDataDirectionSelected() const { return 0 == (controlB & 0x04); }

  bool Ca1IrqEnabled() const { return (controlA & 0x01); }
  bool Ca2IrqEnabled() const { return 0x10 == (controlA & 0x30); }

  bool Cb1IrqEnabled() const { return (controlB & 0x01); }
  bool Cb2IrqEnabled() const { return 0x10 == (controlB & 0x30); }

  bool Ca1IrqFiring() const { return (controlA & 0x80); }
  bool Ca2IrqFiring() const { return (controlA & 0x40); }
  bool Cb1IrqFiring() const { return (controlB & 0x80); }
  bool Cb2IrqFiring() const { return (controlB & 0x40); }

  bool Ca2IsIrqMode() const { return 0x08 == (controlA & 0x28); }

  bool Cb2IsIrqMode() const { return 0x08 == (controlB & 0x28); }

  void TriggerCa1() { controlA |= 0x80; }
  void TriggerCa2() { controlA |= 0x40; }
  void TriggerCb1() { controlB |= 0x80; }
  void TriggerCb2() { controlB |= 0x40; }

  void Install(const char* n, uint base, LOGGER logf) {
    name = n;

    IOWriters[255 & (base + 0)] = [this](uint a, byte value) {
      if (PortADataDirectionSelected()) {
        dirA = value;
      } else {
        outA = value;
      }
    };
    IOReaders[255 & (base + 0)] = [this, logf](uint a, byte v) {
      if (a == 0xFF00) {
        if (CocoKeyboardChar) {
          inA = Keyboard_ComputeSense(outB, CocoKeyboardChar, logf);
        } else {
          inA = 0xFF;
        }
      }

      if (PortADataDirectionSelected()) {
        return dirA;
      } else {
        return (dirA & outA) | (~dirA & inA);
      }
    };

    IOWriters[255 & (base + 1)] = [this](uint a, byte value) {
      controlA = value;
    };
    IOReaders[255 & (base + 1)] = [this](uint a, byte v) {
      byte z = controlA;
      controlA &= 0x3F;  // clear top to IRQ-firing bits.
      return z;
    };

    IOWriters[255 & (base + 2)] = [this](uint a, byte value) {
      if (PortBDataDirectionSelected()) {
        dirB = value;
      } else {
        outB = value;
      }
    };
    IOReaders[255 & (base + 2)] = [this](uint a, byte v) {
      if (PortBDataDirectionSelected()) {
        return dirB;
      } else {
        return (dirB & outB) | (~dirB & inB);
      }
    };

    IOWriters[255 & (base + 3)] = [this](uint a, byte value) {
      controlB = value;
    };
    IOReaders[255 & (base + 3)] = [this](uint a, byte v) {
      byte z = controlB;
      controlB &= 0x3F;  // clear top to IRQ-firing bits.
      return z;
    };
  }
};

Pia Pia0, Pia1;

struct CocoKeyboard {
  // TODO
};

template <typename T>
struct DontCocoKeyboard {
  constexpr static bool Does_CocoKeyboard() { return false; }
  constexpr static bool Keyboard_CanRx() { return false; }
  static void Keyboard_SetRx(uint ch) {}
  static void Keyboard_Tick(uint unused_steps) {}
};

template <typename T>
struct DoCocoKeyboard {
  constexpr static bool Does_CocoKeyboard() { return true; }

  static bool Keyboard_CanRx() { return (CocoKeyboardState == CKS_Empty); }
  static void Keyboard_SetRx(uint ch) {
    CocoKeyboardChar = ch;
    CocoKeyboardState = CKS_Filled1;
  }
  static void Keyboard_Tick(uint unused_steps) {
    if (CocoKeyboardState) {
      CocoKeyboardState = (CKS)(CocoKeyboardState + 1);
    }
    if (CocoKeyboardState >= CKS_Wait1) {
      CocoKeyboardChar = 0;
    }
    if (CocoKeyboardState >= CKS_Limit) {
      CocoKeyboardState = CKS_Empty;
    }
  }
};

template <typename T>
struct DontCocoPias {
  constexpr static bool Does_CocoPias() { return false; }

  static bool VsyncIrqEnabled() { return false; }
  // bool vsync_irq_firing;
  static bool VsyncIrqFiring() { return false; }

  static void TriggerVSync() {}
};

template <typename T>
struct DoCocoPias {
  constexpr static bool Does_CocoPias() { return true; }

  // bool vsync_irq_enabled;
  static bool VsyncIrqEnabled() { return Pia0.Cb1IrqEnabled(); }
  // bool vsync_irq_firing;
  static bool VsyncIrqFiring() { return Pia0.Cb1IrqFiring(); }

  static void TriggerVSync() { Pia0.TriggerCb1(); }

  static void CocoPias_Install(LOGGER logf) {
    Pia0.Install("pia0", 0xFF00, logf);
    Pia0.Install("pia0", 0xFF1C, logf);  // an alias for Pia0
    Pia1.Install("pia1", 0xFF20, logf);
  }
};

#endif  //  _COCOPIAS_H_
