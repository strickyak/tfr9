#ifndef _FIRMWARE_RTC_H_
#define _FIRMWARE_RTC_H_

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"

// Hardware alarm channel (0-3 available)
#define TIMER_ALARM_NUM 0
#define INTERVAL_US     20000ULL // 20ms in microseconds

// Timekeeping structure updated by the ISR
typedef struct {
    volatile uint64_t ticks_20ms; // Total 20ms ticks elapsed
    volatile uint32_t seconds;    // Whole seconds
    volatile uint32_t milliseconds; // Sub-second millisecond remainder
} SystemTime;

static SystemTime g_sys_time = {0};

struct repeating_timer g_timer;

/**
 * @brief Interrupt Service Routine for 20ms tick timekeeping.
 */
bool alarm_irq_handler(struct repeating_timer *t) {
    // Update timekeeping variables
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
void start_20ms_timer(void) {
    add_repeating_timer_ms(-20, alarm_irq_handler, NULL, &g_timer);
}

/**
 * @brief Thread-safe snapshot read of current uptime.
 */
void get_system_time(uint32_t *out_sec, uint32_t *out_ms) {
    // Atomically capture volatile variables
    uint32_t save = save_and_disable_interrupts();
    *out_sec = g_sys_time.seconds;
    *out_ms = g_sys_time.milliseconds;
    restore_interrupts(save);
}

/**
 * @brief Thread-safe snapshot read of 20ms ticks.
 */
uint64_t get_system_ticks_20ms(void) {
    uint32_t save = save_and_disable_interrupts();
    uint64_t ticks = g_sys_time.ticks_20ms;
    restore_interrupts(save);
    return ticks;
}

#endif // _FIRMWARE_RTC_H_
