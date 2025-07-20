#include "app-t9sim.h"

void app_main() {
  for (unsigned k = 0; TRUE; k++) {
    printf("\n[%d]\n", k);
    for (unsigned i = 0; i < 10000; i++) {
      printf("%u ", i);
    }
  }
}
