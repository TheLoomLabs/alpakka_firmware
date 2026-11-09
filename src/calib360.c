// SPDX-License-Identifier: GPL-2.0-only
// Real-world calibration, in-game 360 test (fork extension). See calib360.h.

#include <math.h>
#include "calib360.h"
#include "bus.h"
#include "button.h"
#include "config.h"
#include "hid.h"
#include "logging.h"
#include "pin.h"

#define CALIB360_SPIN_MS 1000

static bool active = false;
static bool reset_pending = false;
static uint16_t counts = 0;
static double spin_remaining = 0;
static double spin_step = 0;

static Button btn_spin;
static Button btn_up;
static Button btn_down;
static Button btn_right;
static Button btn_left;
static Button btn_home;
static Button btn_plus_one;
static Button btn_minus_one;
static bool prev_spin, prev_up, prev_down, prev_right, prev_left, prev_home;
static bool prev_plus_one, prev_minus_one;

static uint16_t clamp_counts(double value) {
    if (value < CALIB360_COUNTS_MIN) return CALIB360_COUNTS_MIN;
    if (value > CALIB360_COUNTS_MAX) return CALIB360_COUNTS_MAX;
    return (uint16_t)lround(value);
}

static void adjust_counts(int delta) {
    counts = clamp_counts((double)counts + delta);
    info("Calib360: counts=%i\n", counts);
}

// Change counts by a fraction, always by at least one count.
static void adjust(double fraction) {
    double delta = counts * fraction;
    if (fabs(delta) < 1) delta = (fraction > 0) ? 1 : -1;
    counts = clamp_counts(counts + delta);
    info("Calib360: counts=%i\n", counts);
}

// Rising edge: true only on the tick the button goes down.
static bool pressed(Button *button, bool *prev) {
    bool now = button->is_pressed(button);
    bool edge = now && !*prev;
    *prev = now;
    return edge;
}

void calib360_start(uint16_t initial) {
    Actions none = {0,};
    btn_spin = Button_(PIN_R2, NORMAL, none, none, none);
    btn_up = Button_(PIN_DPAD_UP, NORMAL, none, none, none);
    btn_down = Button_(PIN_DPAD_DOWN, NORMAL, none, none, none);
    btn_right = Button_(PIN_DPAD_RIGHT, NORMAL, none, none, none);
    btn_left = Button_(PIN_DPAD_LEFT, NORMAL, none, none, none);
    btn_home = Button_(PIN_HOME, NORMAL, none, none, none);
    btn_plus_one = Button_(PIN_R1, NORMAL, none, none, none);
    btn_minus_one = Button_(PIN_L1, NORMAL, none, none, none);
    // Buttons held when entering (e.g. still on R2) must not trigger.
    prev_spin = prev_up = prev_down = prev_right = prev_left = prev_home = true;
    prev_plus_one = prev_minus_one = true;
    counts = clamp_counts(initial ? initial : 8192);
    spin_remaining = 0;
    active = true;
    reset_pending = true;
    info("Calib360: started, counts=%i\n", counts);
}

void calib360_stop() {
    if (!active) return;
    active = false;
    reset_pending = true;
    spin_remaining = 0;
    info("Calib360: stopped, counts=%i\n", counts);
}

bool calib360_is_active() {
    return active;
}

bool calib360_is_spinning() {
    return active && spin_remaining > 0;
}

uint16_t calib360_get_counts() {
    return counts;
}

bool calib360_take_reset() {
    bool value = reset_pending;
    reset_pending = false;
    return value;
}

void calib360_report() {
    bus_i2c_io_cache_update();
    if (pressed(&btn_home, &prev_home)) {
        calib360_stop();
        return;
    }
    // Adjustments are ignored while a spin is running, so a test is never
    // a mix of two values.
    if (spin_remaining <= 0) {
        if (pressed(&btn_right, &prev_right)) adjust(0.01);
        if (pressed(&btn_left, &prev_left)) adjust(-0.01);
        if (pressed(&btn_up, &prev_up)) adjust(0.001);
        if (pressed(&btn_down, &prev_down)) adjust(-0.001);
        if (pressed(&btn_plus_one, &prev_plus_one)) adjust_counts(1);
        if (pressed(&btn_minus_one, &prev_minus_one)) adjust_counts(-1);
        if (pressed(&btn_spin, &prev_spin)) {
            spin_remaining = counts;
            spin_step = (double)counts * 1000 / CALIB360_SPIN_MS / CFG_TICK_FREQUENCY;
            info("Calib360: spin %i\n", counts);
        }
    }
    if (spin_remaining > 0) {
        double step = (spin_remaining < spin_step) ? spin_remaining : spin_step;
        hid_mouse_move(step, 0);
        spin_remaining -= step;
        if (spin_remaining < 1e-6) spin_remaining = 0;
    }
}
