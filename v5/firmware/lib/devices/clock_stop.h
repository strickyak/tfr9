#pragma once
// clock_stop.h -- CLOCK_AND_STOP register (60 Hz clock enable, stop/exit command).
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// IN_RAM CLOCK_AND_STOP callbacks
byte IN_RAM clock_and_stop_reader(uint) {
  return clock_and_stop_reg;
}

void IN_RAM clock_and_stop_writer(uint, byte data) {
  if (data == 0xFC || data == 0xFD || data == 0xFE) {
    // Stop 6309 bus activity and wait for core 0 to finish draining fg2bg_chars
    // so core 1's cobs_printf / cobs_shutdown cannot interleave mid-COBS-frame with putbyte() (S57).
    gpio_put(RESET, 0); gpio_set_dir(RESET, GPIO_OUT);
    gpio_put(HALT, 0);  gpio_set_dir(HALT, GPIO_OUT);
    while (fg2bg_chars.size() > 0) {
      sleep_ms(2);
    }
    sleep_ms(30);
  }
  if (data == 0xFC) {
    cobs_printf("\n *** Exiting with status 0 due to CLOCK_AND_STOP command $FC.\n");
    cobs_shutdown(0);
    stdio_flush();
    sleep_ms(100);
    rp2350_reset_standard();
  } else if (data == 0xFD) {
    cobs_printf("\n *** Exiting with status 1 due to CLOCK_AND_STOP command $FD.\n");
    cobs_shutdown(1);
    stdio_flush();
    sleep_ms(100);
    rp2350_reset_standard();
  } else if (data == 0xFE) {
    cobs_printf("\n *** PANIC due to CLOCK_AND_STOP command $FE.\n");
    // Stream 64KB memory dump in 256 chunks of 256 bytes
    uint8_t dump_pkt[258];
    dump_pkt[0] = 189; // C_CORE_DUMP
    for (int chunk = 0; chunk < 256; chunk++) {
      dump_pkt[1] = (uint8_t)chunk;
      if (g_deep_mmu_active) {
        uint32_t phys = translate_deep_addr(chunk * 256);
        memcpy(&dump_pkt[2], &ram[phys], 256);
      } else {
        memcpy(&dump_pkt[2], &ram[chunk * 256], 256);
      }
      CobsEncodeAndTransmit(dump_pkt, sizeof(dump_pkt), [](int ch) { putchar_raw(ch); });
      stdio_flush();
      sleep_us(500);
    }
    cobs_shutdown(255);
    stdio_flush();
    sleep_ms(100);
    rp2350_reset_standard();
  } else {
    if (data & 0x01) {
      clock_and_stop_reg &= ~0x01; // W1C
    }
    clock_and_stop_reg = (clock_and_stop_reg & 0x01) | (data & 0xFE);
  }
}


void IN_RAM clock_stop_tick_60hz() {
  gime_tick_60hz();
  flat65280_timer_ticks++;
  uint8_t rate = (clock_and_stop_reg >> 4) & 3;
  bool fire = false;
  switch (rate) {
    case 0: fire = true; break;                                   // 60 Hz
    case 1: fire = true; break;                                   // 50 Hz (approx)
    case 2: fire = (flat65280_timer_ticks % 10) == 0; break;      // 6 Hz
    case 3: fire = (flat65280_timer_ticks % 600) == 0; break;     // 0.1 Hz
  }
  if (fire) {
    clock_and_stop_reg |= 0x01; // Set TICK flag
  }
}

struct ClockStop {   // one register: bit 0/1 = 60 Hz clock status/enable, writing $FC = stop the target with status 0
  static constexpr uint16_t kDefaultBase = 0xFF87;
  static void Install(uint16_t base = kDefaultBase) {
    clock_and_stop_reg = 0;
    flat65280_timer_ticks = 0;
    IoClaim(base, base, "clockstop");
    IoSet(base, clock_and_stop_reader, clock_and_stop_writer);
    g_timer_60hz_fn = clock_stop_tick_60hz;
  }
};
