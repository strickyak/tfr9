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
      if (current_engine == ENGINE_DEEP65280) {
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

