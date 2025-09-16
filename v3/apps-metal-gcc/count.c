#include "app-t9sim.h"

#define LED_PORT 0xFF04

void app_main() {
  for (unsigned k = 0; TRUE; k++) {
    printf("\n[%d]\n", k);
    for (unsigned i = 0; i < 10000; i++) {
      POKE1(LED_PORT, i>>8);
      printf("%u ", i);
    }
  }
}
