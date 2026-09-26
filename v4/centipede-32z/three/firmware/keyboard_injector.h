#ifndef FIRMWARE_KEYBOARD_INJECTOR_H_
#define FIRMWARE_KEYBOARD_INJECTOR_H_

#include <string>
#include "console.h"
#include "rtc.h"
#include "cobs_tx.h"
#include "hardware/sync.h"

namespace keyboard_injector {

struct KeystrokeAction {
    uint16_t wait_ticks; // Number of 20ms ticks to wait
    int8_t target_col;   // -1 if idle/gap
    int8_t target_row;
    bool needs_shift;
    bool needs_clear;
};

#define MAX_SCRIPT_ACTIONS 256
#define TYPING_DOWN_MS 500  // 0.5s key down
#define TYPING_UP_MS   500  // 0.5s key up between keystrokes
#define TYPING_DOWN_TICKS (TYPING_DOWN_MS / 20)  // 25 ticks
#define TYPING_UP_TICKS   (TYPING_UP_MS / 20)    // 25 ticks
#define PAUSE_TICKS (1000 / 20)  // ~ always 1 second = 50 ticks

// Pre-compiled key_script array (filled by background)
inline KeystrokeAction key_script[MAX_SCRIPT_ACTIONS];
inline size_t script_len = 0;
inline size_t script_pc = 0;
inline uint32_t next_transition_tick = 0;

inline std::string queued_string = "";

// ---- Foreground-visible state ----
// The foreground loop checks 'active' and indexes 'probe_table[active_table_idx][ram[0xFF02]]'.
// 'active' is set true when injection starts, false when done.
volatile inline bool active = false;

// Double-buffered 256-entry lookup table mapping probe byte ($FF02) -> sense byte ($FF00).
// Pre-calculates the response for all possible 256 probe combinations.
volatile inline uint8_t probe_table[2][256];
volatile inline uint8_t active_table_idx = 0;

// ---- End foreground state ----

inline void update_probe_table_for_action(const KeystrokeAction& act) {
    uint8_t next_idx = 1 - active_table_idx;
    byte col_resp[8] = {0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F};

    if (act.target_col >= 0) {
        col_resp[act.target_col] &= ~(1 << act.target_row);
        if (act.needs_shift) {
            col_resp[7] &= ~(1 << 6);
        }
        if (act.needs_clear) {
            col_resp[1] &= ~(1 << 6);
        }
        cobs_printf("[keyboard_injector] Key DOWN (col %d, row %d, shift %d) for %d ms\n",
                    act.target_col, act.target_row, act.needs_shift, act.wait_ticks * 20);
    } else {
        cobs_printf("[keyboard_injector] Key UP for %d ms\n", act.wait_ticks * 20);
    }

    // For any probe written to $FF02:
    // Bits that are 0 indicate selected column(s).
    for (int probe = 0; probe < 256; probe++) {
        byte sense = 0x7F;
        for (int col = 0; col < 8; col++) {
            if ((probe & (1 << col)) == 0) {
                sense &= col_resp[col];
            }
        }
        probe_table[next_idx][probe] = sense;
    }

    __dmb();
    active_table_idx = next_idx;
}

inline void lookup_char(char c, int8_t* col_out, int8_t* row_out, bool* shift_out, bool* clear_out) {
    *col_out = -1;
    *row_out = -1;
    *shift_out = false;
    *clear_out = false;

    if (c == '\n') c = '\r';

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

inline uint16_t g_down_ticks = TYPING_DOWN_TICKS;
inline uint16_t g_up_ticks = TYPING_UP_TICKS;

inline void queue_string(const std::string& str, uint16_t down_ticks = TYPING_DOWN_TICKS, uint16_t up_ticks = TYPING_UP_TICKS) {
    queued_string = str;
    g_down_ticks = (down_ticks > 0) ? down_ticks : 1;
    g_up_ticks = (up_ticks > 0) ? up_ticks : 1;
}

inline void start_if_queued() {
    if (active) return;
    if (queued_string.empty()) return;

    cobs_printf("[keyboard_injector] Compiling sequence: \"%s\"\n", queued_string.c_str());

    script_len = 0;
    for (char c : queued_string) {
        if (script_len + 2 > MAX_SCRIPT_ACTIONS) break;

        if (c == '~') {
            key_script[script_len++] = {PAUSE_TICKS, -1, -1, false, false};
        } else {
            int8_t c_col, c_row;
            bool c_shift, c_clear;
            lookup_char(c, &c_col, &c_row, &c_shift, &c_clear);
            key_script[script_len++] = {g_down_ticks, c_col, c_row, c_shift, c_clear};
            key_script[script_len++] = {g_up_ticks, -1, -1, false, false};
        }
    }

    queued_string = "";
    script_pc = 0;

    uint32_t current_ticks = (uint32_t)g_sys_time.ticks_20ms;
    if (script_len > 0) {
        next_transition_tick = current_ticks + key_script[0].wait_ticks;
        update_probe_table_for_action(key_script[0]);
    }

    active = true;
    cobs_printf("[keyboard_injector] Active, %d actions\n", (int)script_len);
}

// Called periodically from background to advance the key_script based on ticks.
inline void tick() {
    if (!active) return;

    uint32_t current_ticks = (uint32_t)g_sys_time.ticks_20ms;
    if ((int32_t)(current_ticks - next_transition_tick) >= 0) {
        script_pc++;
        if (script_pc >= script_len) {
            active = false;
            cobs_printf("[keyboard_injector] Sequence complete.\n");
            start_if_queued();
            return;
        }
        update_probe_table_for_action(key_script[script_pc]);
        next_transition_tick = current_ticks + key_script[script_pc].wait_ticks;
    }
}

} // namespace keyboard_injector

#endif // FIRMWARE_KEYBOARD_INJECTOR_H_
