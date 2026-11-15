#pragma once
// acia_term.h -- the terminal ACIA 6850 ($FF88/$FF89) used by the NitrOS-9 engines (flat65280, deep65280).
// Handlers are NAMED functions marked IN_RAM, never lambdas: a lambda cannot be IN_RAM, and flash-resident
// code stalls the bus (spec 5.1 / scripts/check-no-lambda-io.sh).  State lives in emudsk.h (to be tidied).
// (Handler names keep their v4 spelling for now.)

// IN_RAM ACIA 6850 callbacks
byte IN_RAM flat65280_acia_read_status(uint) {
  byte s = 0x02; // TDRE is always 1 (transmit empty)
  if (flat65280_rx_has_char) {
    s |= 0x01; // RDRF = 1 (receive full)
    if (flat65280_acia_ctrl & 0x80) {
      s |= 0x80; // IRQ
    }
  }
  return s;
}

byte IN_RAM flat65280_acia_read_rx(uint) {
  flat65280_rx_has_char = false;
  return flat65280_rx_char;
}

void IN_RAM flat65280_acia_write_ctrl(uint, byte d) {
  flat65280_acia_ctrl = d;
}

void IN_RAM flat65280_acia_write_tx(uint, byte d) {
  Say(d);
}


struct TermAcia {
  static constexpr uint16_t kDefaultBase = 0xFF88;
  static void Install(uint16_t base = kDefaultBase) {
    IoClaim(base, base + 1, "acia");
    IoSet(base + 0, flat65280_acia_read_status, flat65280_acia_write_ctrl);
    IoSet(base + 1, flat65280_acia_read_rx,     flat65280_acia_write_tx);
  }
};
