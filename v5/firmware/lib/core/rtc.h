#ifndef CENTIPEDE_FIRMWARE_RTC_H_
#define CENTIPEDE_FIRMWARE_RTC_H_

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

/**
 * @brief Structure holding system uptime and tick counters.
 */
typedef struct {
    volatile uint64_t ticks_20ms;   // Total 20ms ticks since boot
    volatile uint32_t seconds;      // Whole seconds since boot
    volatile uint32_t milliseconds; // Sub-second millisecond remainder
} SystemTime;

inline SystemTime g_sys_time = {0};

inline struct repeating_timer g_timer;

/**
 * @brief Interrupt Service Routine for 20ms tick timekeeping.
 */
inline bool alarm_irq_handler(struct repeating_timer *t) {
    (void)t;
    g_sys_time.ticks_20ms++;
    g_sys_time.milliseconds += 20;

    if (g_sys_time.milliseconds >= 1000) {
        g_sys_time.milliseconds -= 1000;
        g_sys_time.seconds++;
    }
    return true;
}

/**
 * @brief Initializes Alarm 0 to fire every 20ms and enables the IRQ handler.
 */
inline void start_20ms_timer(void) {
    add_repeating_timer_ms(-20, alarm_irq_handler, NULL, &g_timer);
}

/**
 * @brief Thread-safe snapshot read of current uptime.
 */
inline void get_system_time(uint32_t *out_sec, uint32_t *out_ms) {
    uint32_t save = save_and_disable_interrupts();
    *out_sec = g_sys_time.seconds;
    *out_ms = g_sys_time.milliseconds;
    restore_interrupts(save);
}

/**
 * @brief Thread-safe snapshot read of 20ms ticks.
 */
inline uint64_t get_system_ticks_20ms(void) {
    uint32_t save = save_and_disable_interrupts();
    uint64_t ticks = g_sys_time.ticks_20ms;
    restore_interrupts(save);
    return ticks;
}

#endif  // CENTIPEDE_FIRMWARE_RTC_H_
