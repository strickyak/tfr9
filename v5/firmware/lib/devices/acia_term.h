#pragma once
// acia_term.h -- the terminal ACIA 6850 ($FF88/$FF89) used by the NitrOS-9 engines (flat65280, deep65280).
// Handlers are NAMED functions marked IN_RAM, never lambdas: a lambda cannot be IN_RAM, and flash-resident
// code stalls the bus (spec 5.1 / scripts/check-no-lambda-io.sh).  State lives in emudsk.h (to be tidied).
// (Handler names keep their v4 spelling for now.)

// IN_RAM ACIA 6850 callbacks
byte IN_RAM flat65280_acia_read_status(uint) {
  byte s = 0x02; // TDRE is always 1 (transmit empty)
  bool rx_irq = flat65280_rx_has_char && ((flat65280_acia_ctrl & 0x80) != 0);
  bool tx_irq = ((flat65280_acia_ctrl & 0x60) == 0x20);
  if (flat65280_rx_has_char) {
    s |= 0x01; // RDRF = 1 (receive full)
  }
  if (rx_irq || tx_irq) {
    s |= 0x80; // IRQ
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
  if (d == 7) audio::Bell();
  Say(d);
}


inline void term_acia_poll_rx() {
  while (!flat65280_rx_has_char && term_input.NumBuffered() > 0) {
    byte ch = term_input.Take();
    if (ch >= 128) {
      // Tether encodes Up/Down/Left/Right as 128..131 and PgUp/PgDn as 132..133.
      // Translate to CoCo 3 arrow codes unless bit 7 of audio::control requests raw codes (e.g. vi).
      static const byte kCocoArrows[4] = {0x0C, 0x0A, 0x08, 0x09};
      if (ch > 133) continue;
      bool raw_arrows = audio::installed && ((audio::control & 0x80) != 0);
      if (!raw_arrows) {
        if (ch > 131) continue;
        ch = kCocoArrows[ch - 128];
      }
    }
    flat65280_rx_char = ch;
    __dmb();
    flat65280_rx_has_char = true;
    audio::Click();
  }
}

struct TermAcia {
  static constexpr uint16_t kDefaultBase = 0xFF88;
  static void Install(uint16_t base = kDefaultBase) {
    flat65280_acia_ctrl = 0;
    flat65280_rx_has_char = false;
    g_term_acia_arrow_keys = true;
    IoClaim(base, base + 1, "acia");
    IoSet(base + 0, flat65280_acia_read_status, flat65280_acia_write_ctrl);
    IoSet(base + 1, flat65280_acia_read_rx,     flat65280_acia_write_tx);
    g_rx_poll_fn = term_acia_poll_rx;
  }
};
