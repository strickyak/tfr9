#ifndef FIRMWARE_KEYBOARD_INJECTOR_H_
#define FIRMWARE_KEYBOARD_INJECTOR_H_

#include <string>
#include "console.h"
#include "rtc.h"
#include "cobs_tx.h"

namespace keyboard_injector {

struct KeystrokeAction {
    uint16_t wait_ticks; // Number of 20ms ticks to wait
    int8_t target_col;   // -1 if idle/gap
    int8_t target_row;
    bool needs_shift;
    bool needs_clear;
};

#define MAX_SCRIPT_ACTIONS 128
#define TYPING_DELAY_MS 250  // Total ms per keystroke (half down, half up)
#define TYPING_HALF_TICKS ((TYPING_DELAY_MS / 2) / 20)  // Convert ms to 20ms ticks
#define PAUSE_TICKS (1000 / 20)  // ~ always 1 second = 50 ticks

// Pre-compiled script array (filled by background)
inline KeystrokeAction script[MAX_SCRIPT_ACTIONS];
inline size_t script_len = 0;
inline size_t script_pc = 0;
inline uint32_t next_transition_tick = 0;

inline std::string queued_string = "";

// ---- Foreground-visible state ----
// The foreground loop checks 'active' and reads 'row_response[col]'.
// These are written by the background's tick-advance logic.
// 'active' is set true when injection starts, false when done.
volatile inline bool active = false;

// row_response[col]: the byte to return for $FF00 when column 'col' 
// is being probed (bit col is 0 in the probe mask).
// 0x7F means "no key in this column".
volatile inline byte row_response[8] = {0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F};

// ---- End foreground state ----

inline void set_responses_for_action(const KeystrokeAction& act) {
    // First, clear all responses to "no key"
    for (int i = 0; i < 8; i++) row_response[i] = 0x7F;
    
    if (act.target_col >= 0) {
        // Set the target key's row bit low in the target column
        row_response[act.target_col] &= ~(1 << act.target_row);
        
        // If shift needed, set row 6 low in column 7
        if (act.needs_shift) {
            row_response[7] &= ~(1 << 6);
        }
        // If clear needed, set row 6 low in column 1
        if (act.needs_clear) {
            row_response[1] &= ~(1 << 6);
        }
    }
}

inline void lookup_char(char c, int8_t* col_out, int8_t* row_out, bool* shift_out, bool* clear_out) {
    *col_out = -1;
    *row_out = -1;
    *shift_out = false;
    *clear_out = false;
    
    for (int col = 0; col < 8; ++col)
        for (int row = 0; row < 7; ++row)
            if (console::unshifted_map[col][row] == (unsigned char)c) {
                *col_out = col; *row_out = row; return;
            }
    for (int col = 0; col < 8; ++col)
        for (int row = 0; row < 7; ++row)
            if (console::shifted_map[col][row] == (unsigned char)c) {
                *col_out = col; *row_out = row; *shift_out = true; return;
            }
    for (int col = 0; col < 8; ++col)
        for (int row = 0; row < 7; ++row)
            if (console::clear_map[col][row] == (unsigned char)c) {
                *col_out = col; *row_out = row; *clear_out = true; return;
            }
}

inline void queue_string(const std::string& str) {
    queued_string = str;
}

inline void start_if_queued() {
    if (queued_string.empty()) return;
    
    cobs_printf("[keyboard_injector] Compiling sequence: \"%s\"\n", queued_string.c_str());
    
    script_len = 0;
    for (char c : queued_string) {
        if (script_len + 2 > MAX_SCRIPT_ACTIONS) break;
        
        if (c == '~') {
            script[script_len++] = {PAUSE_TICKS, -1, -1, false, false};
        } else {
            int8_t c_col, c_row;
            bool c_shift, c_clear;
            lookup_char(c, &c_col, &c_row, &c_shift, &c_clear);
            script[script_len++] = {TYPING_HALF_TICKS, c_col, c_row, c_shift, c_clear};
            script[script_len++] = {TYPING_HALF_TICKS, -1, -1, false, false};
        }
    }
    
    queued_string = "";
    script_pc = 0;
    
    uint32_t current_ticks = (uint32_t)g_sys_time.ticks_20ms;
    if (script_len > 0) {
        next_transition_tick = current_ticks + script[0].wait_ticks;
        set_responses_for_action(script[0]);
    }
    
    // Make the foreground see it
    active = true;
    cobs_printf("[keyboard_injector] Active, %d actions\n", (int)script_len);
}

// Called periodically from background to advance the script based on ticks.
inline void tick() {
    if (!active) return;
    
    uint32_t current_ticks = (uint32_t)g_sys_time.ticks_20ms;
    if ((int32_t)(current_ticks - next_transition_tick) >= 0) {
        script_pc++;
        if (script_pc >= script_len) {
            active = false;
            for (int i = 0; i < 8; i++) row_response[i] = 0x7F;
            cobs_printf("[keyboard_injector] Sequence complete.\n");
            return;
        }
        set_responses_for_action(script[script_pc]);
        next_transition_tick = current_ticks + script[script_pc].wait_ticks;
    }
}

} // namespace keyboard_injector

#endif // FIRMWARE_KEYBOARD_INJECTOR_H_
