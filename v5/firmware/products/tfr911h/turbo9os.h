#ifndef _TURBO9OS_H_
#define _TURBO9OS_H_

#include "romlist.h"

// These vectors get copied to 0xFFF0.
// They do not include the final RESET vector at 0xFFFE;
// that will be computed.
const uint Turbo9os_Vectors[] = {
    0x0000, 0x0100, 0x0103, 0x010F, 0x010C, 0x0106, 0x0109,
};

const byte Turbo9os_Rom[] = {
#include "turbo9os.rom.h"
};
const byte Basic09_Rom[] = {
#include "basic09.mod.h"
};
const byte Ncl_Rom[] = {
#include "ncl.mod.h"
};
const byte Ax_Rom[] = {
#include "ax.mod.h"
};

#if TRY_AX_ROM
using Turbo9osActiveRoms = RomList<Turbo9os_Rom, Ax_Rom>;
#else
using Turbo9osActiveRoms = RomList<Turbo9os_Rom, Basic09_Rom>;
#endif

// Core-0 setup for built-in Turbo9OS (`on tfr9 run turbo9os for turbo9sim`).
// Runs before core 1 launches, so core 1 never calls cobs_printf or touches flash ROM tables.
inline void setup_turbo9os() {
  clock_and_stop_reg = 0;
  flat65280_acia_ctrl = 0;
  flat65280_rx_has_char = false;

  Install_Turbo9os_IO();

  cobs_printf("DoTurbo9os Install_OS...\n");
  is_an_os9 = true;
  for (uint i = 0; i < 7; i++) {
    InstallVector(i, Turbo9os_Vectors[i]);
  }

  constexpr uint n = Turbo9osActiveRoms::total_size;
  constexpr uint begin = 0xFF00 - n;
  cobs_printf("DoTurbo9os Install_OS... n=%04x begin=%04x\n", n, begin);
  for (uint i = 0; i < n; i++) {
    uint addr = begin + i;
    byte b = Turbo9osActiveRoms::data[i];
    ram[addr & 0xFFFF] = b;
    if (usb_tether_ok()) {
      unsigned char pkt[4] = {
          C_RAM2_WRITE,
          (unsigned char)((addr >> 8) & 0xFF),
          (unsigned char)(addr & 0xFF),
          b,
      };
      CobsEncodeAndTransmit(pkt, 4, [](int ch) { putchar_raw(ch); });
    }
  }

  uint magic = (((uint)ram[begin]) << 8) | ram[begin + 1];
  assert(magic == 0x87CD);  // OS9 module magic number

  uint name = (((uint)ram[begin + 4]) << 8) | ram[begin + 5];
  char expect[7] = "kernel";
  expect[5] |= 0x80;  // OS9 string termination bit overlays final 'l'
  assert(0 == memcmp(Turbo9osActiveRoms::data + name, expect, 6));

  uint entry = (((uint)ram[begin + 9]) << 8) | ram[begin + 10];
  cobs_printf("DoTurbo9os Install_OS... begin=%04x entry=%04x\n", begin, begin + entry);
  InstallVector(7, begin + entry);

  IoFinish();
}

#endif  // _TURBO9OS_H_
