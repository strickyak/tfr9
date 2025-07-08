#ifndef _REBOOT_H_
#define _REBOOT_H_

#include "hardware/watchdog.h"

/*

void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms);

void watchdog_start_tick(uint cycles);

void watchdog_update(void);

void watchdog_enable(uint32_t delay_ms, bool pause_on_debug);

bool watchdog_caused_reboot(void);

uint32_t watchdog_get_time_remaining_ms(void);

*/

void Reboot() {
  constexpr uint XOSC_RATE_MHZ = 12;
  constexpr uint DELAY_MS = 100;
  constexpr bool PAUSE_ON_DEBUG = false;

  watchdog_enable(DELAY_MS, PAUSE_ON_DEBUG);
  watchdog_start_tick(XOSC_RATE_MHZ);

  while (true) {
    uint ms = watchdog_get_time_remaining_ms();
    char buf[20];
    sprintf(buf, " [%d]", ms);
    ShowStr(buf);
    sleep_ms(1);
  }
}

#endif  // _REBOOT_H_
