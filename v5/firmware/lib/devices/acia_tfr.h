#pragma once
// acia_tfr.h -- TFR911 ACIA 6850 IN_RAM callbacks (term port).
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// ═══════════════════════════════════════════════════════════════════
// IN_RAM IO readers/writers for turbo9sim ACIA
// The lambda IOReaders/IOWriters installed by Turbo9sim_Install live
// in flash. During bus cycles, the PIO holds at pull block side 3
// (E=Q=1) waiting for data. An XIP cache miss on the lambda body
// stalls the CPU, and the 6809 latches garbage from the data bus.
// These IN_RAM wrappers eliminate that stall.
// ═══════════════════════════════════════════════════════════════════

// Readers
byte IN_RAM acia_read_tx(uint addr)      { return sim_last_char_tx; }
byte IN_RAM acia_read_rx(uint addr)      { sim_status_reg &= ~0x02; return sim_last_char_rx; }
byte IN_RAM acia_read_status(uint addr)  { return sim_status_reg; }
byte IN_RAM acia_read_control(uint addr) { return sim_control_reg; }

// Writers
void IN_RAM acia_write_tx(uint addr, byte data) {
  sim_last_char_tx = data;
  Say(data);  // Push to high-priority chars FIFO
}
void IN_RAM acia_write_rx(uint addr, byte data) {
  // No effect (write to RX register).
}
void IN_RAM acia_write_status(uint addr, byte data) {
  if (data & 0x01) { sim_timer_irq = false; sim_status_reg &= ~0x01; }
  if (data & 0x02) { sim_rx_ready_irq = false; sim_status_reg &= ~0x02; }
}
void IN_RAM acia_write_control(uint addr, byte data) {
  sim_control_reg = data;
}

struct Turbo9SimAcia {   // Turbo9Sim's ACIA: tx data, rx data, status, control at $FF00..$FF03
  static constexpr uint16_t kDefaultBase = 0xFF00;
  static void Install(uint16_t base = kDefaultBase) {
    sim_timer_irq = false;
    sim_rx_ready_irq = false;
    sim_status_reg = 0;
    sim_control_reg = 0;
    IoClaim(base, base + 3, "turbo9sim_acia");
    IoSet(base + 0, acia_read_tx,      acia_write_tx);
    IoSet(base + 1, acia_read_rx,      acia_write_rx);
    IoSet(base + 2, acia_read_status,  acia_write_status);
    IoSet(base + 3, acia_read_control, acia_write_control);
    g_rx_poll_fn = turbo9sim_poll_rx;
    g_timer_60hz_fn = turbo9sim_tick_60hz;
  }
};

// (Install_Turbo9os_IO below, after RamVectors)
struct RamVectors {      // $FFF0..$FFFF belong to the engine's RAM; nothing in the tables, only the claim
  static void Claim(uint16_t base = 0xFFF0) { IoClaim(base, base + 15, "vectors"); }
};

// Turbo9OS's I/O: the Turbo9Sim ACIA, handlers IN_RAM so the bus is never stalled by flash.  Run at startup and
// again when `bye` launches Turbo9OS, so that port settings made in the meantime take effect.
void Install_Turbo9os_IO() {
  IoReset();
  Turbo9SimAcia::Install(PortBase("turbo9sim_acia", Turbo9SimAcia::kDefaultBase));
  RamVectors::Claim();
}
