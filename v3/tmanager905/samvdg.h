#ifndef _SAMVDG_H_
#define _SAMVDG_H_

bool vsync_irq_enabled;
bool vsync_irq_firing;


template <class T>
struct DontSamvdg {
  constexpr static bool DoesSamvdg() { return false; }
};

template <class T>
struct DoSamvdg {

  constexpr static bool DoesSamvdg() { return true; }

  static void Samvdg_Install() {
ShowChar('s');

        // Read PIA0 (0x00 to 0x03)
    IOReaders[0x00] = [](uint addr, byte data) {
          T::Logf("-PIA PIA0 Read not Impl: %x\n", addr);
          data = 0xFF;  // say like, no key pressed
            return data;
};

    IOReaders[0x01] = [](uint addr, byte data) {
          T::Logf("-PIA PIA0 Read not Impl: %x\n", addr);
          DumpRamAndGetStuck("pia0", addr);
            return data;
          };

    IOReaders[0x02] = [](uint addr, byte data) {
          T::Poke(
              0xFF03,
              T::Peek(0xFF03) & 0x7F);  // Clear the bit indicating VSYNC occurred.
          vsync_irq_firing = false;
          data = 0xFF;
            return data;
          };

    IOReaders[0x03] = [](uint addr, byte data) {
          // OK to read, for the HIGH bit, which tells if VSYNC ocurred.
            return data;
          };


    // Wire PIA0 (0x00 to 0x03)

    IOWriters[0x00] = [](uint addr, byte data) {};
    IOWriters[0x01] = [](uint addr, byte data) {};
    IOWriters[0x02] = [](uint addr, byte data) {};

    IOWriters[0x03] = [](uint addr, byte data) {
          // Write PIA0
          T::Logf("-PIA PIA0: %04x w\n", addr);
          vsync_irq_enabled = bool((data & 1) != 0);
    };

ShowChar('t');
  }

};

#endif //  _SAMVDG_H_
