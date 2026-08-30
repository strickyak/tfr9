#ifndef FIRMWARE_RESTART_H_
#define FIRMWARE_RESTART_H_

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"

/**
 * 1. Standard Hardware Reset
 * Reboots the RP2350 to run your flashed application (equivalent to pulling the RUN pin low).
 */
void rp2350_reset_standard(void) {
    // watchdog_reboot(pc, sp, delay_ms)
    // Setting pc (Program Counter) and sp (Stack Pointer) to 0 triggers a standard cold boot.
    watchdog_reboot(0, 0, 0);
    
    // Hang in a tight loop until the watchdog peripheral triggers the reboot
    while (true) {
        tight_loop_contents();
    }
}

/**
 * 2. Reset into BOOTSEL Mode
 * Halts the application and enumerates the RP2350 as a USB mass storage drive for UF2 flashing.
 */
void rp2350_reset_to_flash_mode(void) {
    // reset_usb_boot(usb_activity_gpio_pin_mask, disable_interface_mask)
    // Passing (0, 0) disables any custom activity LEDs and exposes standard USB interfaces.
    // Note: The Pico SDK handles RP2350-specific errata internally for this function.
    reset_usb_boot(0, 0);
    
    // reset_usb_boot is marked as __attribute__((noreturn)), but it's good practice 
    // to include a trap just in case.
    while (true) {
        tight_loop_contents();
    }
}

#endif // FIRMWARE_RESTART_H_
