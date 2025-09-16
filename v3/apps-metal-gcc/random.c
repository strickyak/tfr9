#include "app-t9sim.h"

#define LED_PORT 0xFF04
#define RAND_PORT 0xFF05

void app_main() {
  for (unsigned k = 0; TRUE; k++) {
    printf("\n[%d]\n", k);
    for (unsigned i = 0; i < 10000; i++) {
      POKE1(LED_PORT, i>>8);
      byte r = PEEK1(RAND_PORT);
      printf("%u ", r);
    }
  }
}
