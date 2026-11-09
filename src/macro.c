// SPDX-License-Identifier: GPL-2.0-only
// Macro player (fork extension). Driven by the main loop, see macro.h.

#include <string.h>
#include <pico/time.h>
#include "macro.h"
#include "config.h"
#include "ctrl.h"
#include "hid.h"
#include "profile.h"
#include "logging.h"

typedef struct _MacroState {
    bool running;
    bool held;            // Trigger button currently held.
    uint8_t step;
    uint64_t next_us;     // When the next step (or tap release) is due.
    uint8_t tap_key;      // Key pending release from a tap (0 = none).
    uint8_t down[MACRO_STEPS_MAX];  // Keys held down by DOWN steps.
    uint8_t ndown;
} MacroState;

static MacroState states[MACRO_SLOTS];

uint16_t macro_delay_ms(uint8_t code) {
    // 0-32: 10ms steps (0-320ms). 33-63: 80ms steps (400-2800ms).
    code &= 0x3F;
    if (code <= 32) return code * 10;
    return 320 + (code - 32) * 80;
}

static bool is_playable(uint8_t key) {
    // Keys, modifiers, mouse buttons, scroll and gamepad buttons. No procedures
    // (avoid recursion) and no analog axes.
    if (key == KEY_NONE) return false;
    if (key >= PROC_INDEX) return false;
    if (hid_is_mouse_axis(key)) return false;
    if (hid_is_gamepad_axis(key)) return false;
    return true;
}

static uint8_t *macro_data(uint8_t index, uint8_t *format) {
    uint8_t section = SECTION_MACRO_1 + ((index - 1) / 2);
    uint8_t subindex = (index - 1) % 2;
    CtrlProfile *profile = config_profile_read(profile_get_active_index(false));
    *format = profile->sections[section].macro.format[subindex];
    return profile->sections[section].macro.macro[subindex];
}

// Decode step `n`. Returns false at the end of the macro.
static bool macro_step(
    uint8_t *data, uint8_t format, uint8_t n,
    uint8_t *action, MacroOp *op, uint16_t *delay
) {
    if (format & MACRO_FORMAT_STEPS) {
        if (n >= MACRO_STEPS_MAX) return false;
        uint8_t a = data[n*2];
        uint8_t b = data[n*2 + 1];
        if (a == 0 && b == 0) return false;
        *action = a;
        *op = (MacroOp)(b >> 6);
        *delay = macro_delay_ms(b);
        return true;
    } else {
        // Legacy: one key per byte.
        if (n >= 28 || data[n] == 0) return false;
        *action = data[n];
        *op = MACRO_OP_TAP;
        *delay = 10;
        return true;
    }
}

static uint16_t macro_tap_ms(uint8_t format) {
    if (!(format & MACRO_FORMAT_STEPS)) return 10;
    return (((format >> 2) & 0x1F) + 1) * 10;
}

static MacroMode macro_mode(uint8_t format) {
    if (!(format & MACRO_FORMAT_STEPS)) return MACRO_MODE_ONCE;
    return (MacroMode)(format & 0x03);
}

// Release everything this macro is holding and stop it.
static void macro_stop(uint8_t slot) {
    MacroState *s = &states[slot];
    if (s->tap_key) hid_release(s->tap_key);
    for (uint8_t i=0; i<s->ndown; i++) hid_release(s->down[i]);
    bool held = s->held;
    memset(s, 0, sizeof(MacroState));
    s->held = held;
}

static void macro_start(uint8_t slot) {
    MacroState *s = &states[slot];
    macro_stop(slot);
    s->running = true;
    s->step = 0;
    s->next_us = time_us_64();
}

void macro_trigger_press(uint8_t index) {
    if (index < 1 || index > MACRO_SLOTS) return;
    uint8_t slot = index - 1;
    uint8_t format;
    macro_data(index, &format);
    MacroState *s = &states[slot];
    s->held = true;
    MacroMode mode = macro_mode(format);
    if (mode == MACRO_MODE_TOGGLE && s->running) {
        macro_stop(slot);
        return;
    }
    if (mode == MACRO_MODE_ONCE && s->running) return;  // Ignore re-trigger while playing.
    macro_start(slot);
}

void macro_trigger_release(uint8_t index) {
    if (index < 1 || index > MACRO_SLOTS) return;
    uint8_t slot = index - 1;
    uint8_t format;
    macro_data(index, &format);
    states[slot].held = false;
    if (macro_mode(format) == MACRO_MODE_REPEAT_HELD) macro_stop(slot);
}

static void macro_run(uint8_t slot, uint64_t now) {
    MacroState *s = &states[slot];
    uint8_t format;
    uint8_t *data = macro_data(slot + 1, &format);
    // Bound the work per tick, so zero-delay macros cannot lock the loop.
    for (uint8_t guard=0; guard < MACRO_STEPS_MAX + 1; guard++) {
        if (!s->running || now < s->next_us) return;
        // Finish a pending tap.
        if (s->tap_key) {
            hid_release(s->tap_key);
            s->tap_key = 0;
            uint8_t action = 0; MacroOp op = MACRO_OP_TAP; uint16_t delay = 0;
            macro_step(data, format, s->step - 1, &action, &op, &delay);
            s->next_us = now + (uint64_t)delay * 1000;
            continue;
        }
        uint8_t action = 0; MacroOp op = MACRO_OP_TAP; uint16_t delay = 0;
        if (!macro_step(data, format, s->step, &action, &op, &delay)) {
            // End of the sequence.
            MacroMode mode = macro_mode(format);
            bool loop = (mode == MACRO_MODE_TOGGLE) ||
                        (mode == MACRO_MODE_REPEAT_HELD && s->held);
            // Release keys left held by DOWN steps at the end of every pass.
            for (uint8_t i=0; i<s->ndown; i++) hid_release(s->down[i]);
            s->ndown = 0;
            if (loop && s->step > 0) {
                s->step = 0;
                // At least one tick between passes.
                s->next_us = now + CFG_TICK_INTERVAL_IN_US;
            } else {
                macro_stop(slot);
            }
            return;
        }
        s->step++;
        if (op == MACRO_OP_WAIT || !is_playable(action)) {
            s->next_us = now + (uint64_t)delay * 1000;
        } else if (op == MACRO_OP_TAP) {
            hid_press(action);
            profile_set_reported_inputs(true);
            s->tap_key = action;
            s->next_us = now + (uint64_t)macro_tap_ms(format) * 1000;
        } else if (op == MACRO_OP_DOWN) {
            hid_press(action);
            profile_set_reported_inputs(true);
            if (s->ndown < MACRO_STEPS_MAX) s->down[s->ndown++] = action;
            s->next_us = now + (uint64_t)delay * 1000;
        } else if (op == MACRO_OP_UP) {
            // Only release keys this macro pressed, to keep the state matrix sane.
            for (uint8_t i=0; i<s->ndown; i++) {
                if (s->down[i] == action) {
                    hid_release(action);
                    s->down[i] = s->down[--s->ndown];
                    break;
                }
            }
            s->next_us = now + (uint64_t)delay * 1000;
        }
    }
}

void macro_tick() {
    uint64_t now = time_us_64();
    for (uint8_t slot=0; slot<MACRO_SLOTS; slot++) {
        if (states[slot].running) macro_run(slot, now);
    }
}

void macro_reset() {
    // The HID state matrix is being cleared (profile change): drop all state
    // without releasing, since the matrix is zeroed anyway.
    memset(states, 0, sizeof(states));
}
