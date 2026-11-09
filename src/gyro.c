// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022, Input Labs Oy.

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "button.h"
#include "config.h"
#include "gyro.h"
#include "common.h"
#include "hid.h"
#include "imu.h"
#include "pin.h"
#include "touch.h"
#include "vector.h"

double sensitivity_multiplier;

uint8_t world_init = 0;
Vector world_top;
Vector world_fw;
Vector world_right;
Vector accel_smooth;

void gyro_update_sensitivity() {
    uint8_t preset = config_get_mouse_sens_preset();
    sensitivity_multiplier = config_get_mouse_sens_value(preset);
}

void gyro_accel_correction() {
    Vector accel = imu_read_accel();
    // Convert to inverted unit value.
    accel.x /= -BIT_14;
    accel.y /= -BIT_14;
    accel.z /= -BIT_14;
    // Get a smoothed gravity vector.
    accel_smooth = vector_smooth(accel_smooth, accel, CFG_ACCEL_CORRECTION_SMOOTH);
    if (world_init < CFG_ACCEL_CORRECTION_SMOOTH) {
        // It the world space orientation is not fully initialized.
        world_top = vector_normalize(vector_invert(accel_smooth));
        world_fw = vector_cross_product(world_top, (Vector){1, 0, 0});
        world_right = vector_cross_product(world_fw, world_top);
        world_init++;
    } else {
        // Correction.
        float rate_fw = (world_right.z - accel_smooth.x) * CFG_ACCEL_CORRECTION_RATE;
        float rate_r = (world_fw.z - accel_smooth.y) * CFG_ACCEL_CORRECTION_RATE;
        Vector4 correction_fw = quaternion(world_fw, rate_fw);
        Vector4 correction_r = quaternion(world_right, -rate_r);
        Vector4 correction = qmultiply(correction_fw, correction_r);
        world_top = qrotate(correction, world_top);
        world_right = qrotate(correction, world_right);
        world_fw = vector_cross_product(world_top, world_right);
    }
}

void gyro_absolute_output(float value, uint8_t *actions, bool *pressed) {
    for(uint8_t i=0; i<4; i++) {
        uint8_t action = actions[i];
        if (hid_is_gamepad_axis(action)) {
            value = fabs(value);
            if      (action == GAMEPAD_AXIS_LX)     hid_gamepad_axis(LX,  value);
            else if (action == GAMEPAD_AXIS_LY)     hid_gamepad_axis(LY,  value);
            else if (action == GAMEPAD_AXIS_LZ)     hid_gamepad_axis(LZ,  value);
            else if (action == GAMEPAD_AXIS_RX)     hid_gamepad_axis(RX,  value);
            else if (action == GAMEPAD_AXIS_RY)     hid_gamepad_axis(RY,  value);
            else if (action == GAMEPAD_AXIS_RZ)     hid_gamepad_axis(RZ,  value);
            else if (action == GAMEPAD_AXIS_LX_NEG) hid_gamepad_axis(LX, -value);
            else if (action == GAMEPAD_AXIS_LY_NEG) hid_gamepad_axis(LY, -value);
            else if (action == GAMEPAD_AXIS_LZ_NEG) hid_gamepad_axis(LZ, -value);
            else if (action == GAMEPAD_AXIS_RX_NEG) hid_gamepad_axis(RX, -value);
            else if (action == GAMEPAD_AXIS_RY_NEG) hid_gamepad_axis(RY, -value);
            else if (action == GAMEPAD_AXIS_RZ_NEG) hid_gamepad_axis(RZ, -value);
        } else {
            if (!(*pressed) && value >= 0.5) {
                hid_press(action);
                if (i==3) *pressed = true;
            }
            else if (*pressed && value < 0.5) {
                hid_release(action);
                if (i==3) *pressed = false;
            }
        }
    }
}

void gyro_incremental_output(double value, uint8_t *actions) {
    for(uint8_t i=0; i<4; i++) {
        uint8_t action = actions[i];
        if      (action == MOUSE_X)     hid_mouse_move(value, 0);
        else if (action == MOUSE_Y)     hid_mouse_move(0, value);
        else if (action == MOUSE_X_NEG) hid_mouse_move(-value, 0);
        else if (action == MOUSE_Y_NEG) hid_mouse_move(0, -value);
    }
}

double hssnf(double t, double k, double x) {
    double a = x - (x * k);
    double b = 1 - (x * k * (1/t));
    return a / b;
}

// Normalized curve shape: maps t in [0, 1] to [0, 1].
// Mirrored in the ctrl-app (lib/gyro_accel.ts) for plotting, keep in sync.
static float gyro_accel_shape(GyroAccelCurve curve, float t, float p) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    if (curve == GYRO_ACCEL_LINEAR) {
        return t;
    }
    if (curve == GYRO_ACCEL_POWER) {
        if (p <= 0) p = 2;
        return powf(t, p);
    }
    if (curve == GYRO_ACCEL_NATURAL) {
        if (p <= 0) p = 3;
        return (1 - expf(-p * t)) / (1 - expf(-p));
    }
    if (curve == GYRO_ACCEL_SIGMOID) {
        if (p <= 0) p = 10;
        float l0 = 1 / (1 + expf(p * 0.5f));
        float l1 = 1 / (1 + expf(-p * 0.5f));
        float lt = 1 / (1 + expf(-p * (t - 0.5f)));
        return (lt - l0) / (l1 - l0);
    }
    return 0;
}

// Real-world calibration: output scale so that one degree of controller
// rotation gives `ratio` degrees in game, where a 360 turn takes `counts_360`
// mouse counts. Scale is per raw gyro unit per base tick (250 Hz), the same
// unit as CFG_GYRO_SENSITIVITY * sensitivity_multiplier. 0 = disabled.
float gyro_real_world_scale(uint16_t counts_360, uint8_t ratio_tenths) {
    if (counts_360 == 0) return 0;
    float ratio = (ratio_tenths ?: 10) / 10.0f;
    float counts_per_degree = counts_360 / 360.0f * ratio;
    return GYRO_DPS_PER_LSB * counts_per_degree / CFG_TICK_FREQUENCY_BASE;
}

float gyro_accel_gain(GyroAccel *accel, float speed) {
    if (accel->curve == GYRO_ACCEL_OFF) return 1;
    float range = accel->speed_max - accel->threshold;
    float t = (range > 0) ? (speed - accel->threshold) / range : (speed > accel->threshold);
    float shape = gyro_accel_shape(accel->curve, t, accel->exponent);
    return accel->gain_min + (accel->gain_max - accel->gain_min) * shape;
}

// Update the smoothed angular speed (deg/s) from the axes that are in use.
static void gyro_accel_update_speed(Gyro *self, Vector raw) {
    bool use_x = self->actions_x_pos[0] || self->actions_x_neg[0];
    bool use_y = self->actions_y_pos[0] || self->actions_y_neg[0];
    bool use_z = self->actions_z_pos[0] || self->actions_z_neg[0];
    double x = use_x ? raw.x * self->sens_x : 0;
    double y = use_y ? raw.y * self->sens_y : 0;
    double z = use_z ? raw.z * self->sens_z : 0;
    float speed = sqrt(x*x + y*y + z*z) * GYRO_DPS_PER_LSB;
    // Exponential moving average with a time constant in milliseconds, so the
    // smoothing feels the same at any polling rate.
    float dt = 1000.0f / CFG_TICK_FREQUENCY;
    float alpha = dt / (self->accel.smoothing + dt);
    self->accel_speed += (speed - self->accel_speed) * alpha;
}

// Player space: replace local yaw with yaw around the real-world vertical axis.
// Based on the "player space" technique from GyroWiki / JoyShockMapper.
static Vector gyro_player_space(Gyro *self, Vector g) {
    Vector a = imu_read_accel();
    // Accelerometer remapped into the gyro axes (yaw=x, pitch=y, roll=z), see imu.c.
    Vector up = {-a.z, a.x, a.y};
    if (!self->gravity_init) {
        self->gravity = up;
        self->gravity_init = true;
    } else {
        float dt = 1000.0f / CFG_TICK_FREQUENCY;
        float alpha = dt / (100.0f + dt);  // ~100ms time constant.
        self->gravity.x += (up.x - self->gravity.x) * alpha;
        self->gravity.y += (up.y - self->gravity.y) * alpha;
        self->gravity.z += (up.z - self->gravity.z) * alpha;
    }
    Vector n = vector_normalize(self->gravity);
    if (n.x < 0) n = vector_invert(n);  // Same sign convention as local yaw.
    double world_yaw = g.x * n.x + g.z * n.z;  // Yaw and roll contributions only.
    double local = sqrt(g.x * g.x + g.z * g.z);
    double yaw = fmin(fabs(world_yaw) * 1.41, local);
    yaw = world_yaw < 0 ? -yaw : yaw;
    return (Vector){yaw, g.y, g.z};
}

// Tiered smoothing (only slow movements are smoothed) and soft tightening
// (very slow movements are scaled down), to hide hand tremor.
static Vector gyro_stabilize(Gyro *self, Vector g) {
    GyroExtras *e = &(self->extras);
    double speed = sqrt(g.x*g.x + g.y*g.y + g.z*g.z) * GYRO_DPS_PER_LSB;
    if (e->smooth_threshold > 0 && e->smooth_time > 0) {
        double t2 = e->smooth_threshold;
        double t1 = t2 / 2;
        double w = constrain((speed - t1) / (t2 - t1), 0, 1);
        float dt = 1000.0f / CFG_TICK_FREQUENCY;
        float alpha = dt / (e->smooth_time + dt);
        self->smooth_state.x += (g.x * (1-w) - self->smooth_state.x) * alpha;
        self->smooth_state.y += (g.y * (1-w) - self->smooth_state.y) * alpha;
        self->smooth_state.z += (g.z * (1-w) - self->smooth_state.z) * alpha;
        g.x = g.x * w + self->smooth_state.x;
        g.y = g.y * w + self->smooth_state.y;
        g.z = g.z * w + self->smooth_state.z;
        speed = sqrt(g.x*g.x + g.y*g.y + g.z*g.z) * GYRO_DPS_PER_LSB;
    }
    if (e->tighten > 0 && speed < e->tighten) {
        double k = speed / e->tighten;
        g.x *= k;
        g.y *= k;
        g.z *= k;
    }
    return g;
}

static void gyro_reset_filters(Gyro *self) {
    self->accel_speed = 0;
    self->smooth_state = (Vector){0, 0, 0};
}

void Gyro__report_absolute(Gyro *self) {
    // Accel-based correction.
    gyro_accel_correction();
    // Get data from gyros.
    Vector gyro = imu_read_gyro();
    static float sens = -BIT_18 * M_PI;
    // Rotate world space orientation.
    Vector4 rx = quaternion(world_right, gyro.y / sens);
    Vector4 ry = quaternion(world_fw, gyro.z / sens);
    Vector4 rz = quaternion(world_top, gyro.x / sens);
    static uint8_t i = 0;
    Vector4 r;
    if      (i==0) r = qmultiply(qmultiply(rx, ry), rz);
    else if (i==1) r = qmultiply(qmultiply(rz, rx), ry);
    else if (i==2) r = qmultiply(qmultiply(ry, rz), rx);
    else if (i==3) r = qmultiply(qmultiply(rx, rz), ry);
    else if (i==4) r = qmultiply(qmultiply(ry, rx), rz);
    else if (i==5) r = qmultiply(qmultiply(rz, ry), rx);
    i++;
    if (i>5) i = 0;
    world_top = qrotate(r, world_top);
    world_fw = qrotate(r, world_fw);
    world_right = vector_cross_product(world_fw, world_top);
    // Debug.
    bool debug = 0;
    if (debug) {
        hid_gamepad_axis(LX, world_top.x);
        hid_gamepad_axis(LY, -world_top.y);
        hid_gamepad_axis(RX, world_fw.x);
        hid_gamepad_axis(RY, -world_fw.y);
        return;
    }
    // Output calculation.
    float x = degrees(asin(-world_right.z)) / 90;
    float y = degrees(asin(-world_top.z)) / 90;
    float z = degrees(asin(world_fw.z)) / 90;
    if (fabs(x) > 0.5 && z < 0) x += -z * 2 * sign(x); // Steering lock.
    x = constrain(x * 1.1, -1, 1); // Additional saturation.
    x = ramp(x, self->absolute_x_min/90, self->absolute_x_max/90); // Adjust range.
    y = ramp(y, self->absolute_y_min/90, self->absolute_y_max/90); // Adjust range.
    // Output mapping.
    if (x >= 0) gyro_absolute_output( x, self->actions_x_pos, &(self->pressed_x_pos));
    else        gyro_absolute_output(-x, self->actions_x_neg, &(self->pressed_x_neg));
    if (y >= 0) gyro_absolute_output( y, self->actions_y_pos, &(self->pressed_y_pos));
    else        gyro_absolute_output(-y, self->actions_y_neg, &(self->pressed_y_neg));
    // printf("\r%6.1f %6.1f %6.1f", x*100, y*100, z*100);
}

void Gyro__report_incremental(Gyro *self) {
     // Read gyro values.
    Vector imu_gyro = imu_read_gyro();
    // Space and stabilization.
    if (self->extras.space == GYRO_SPACE_PLAYER) imu_gyro = gyro_player_space(self, imu_gyro);
    imu_gyro = gyro_stabilize(self, imu_gyro);
    // Aim-down-sights modifier.
    bool ads = (
        self->extras.ads_engage != PIN_NONE &&
        self->ads_button.is_pressed(&(self->ads_button))
    );
    double x, y, z;
    if (self->extras.rw_scale > 0) {
        // Real-world calibration replaces the global mouse sensitivity.
        x = imu_gyro.x * self->extras.rw_scale;
        y = imu_gyro.y * self->extras.rw_scale;
        z = imu_gyro.z * self->extras.rw_scale;
    } else {
        x = imu_gyro.x * CFG_GYRO_SENSITIVITY_X * sensitivity_multiplier;
        y = imu_gyro.y * CFG_GYRO_SENSITIVITY_Y * sensitivity_multiplier;
        z = imu_gyro.z * CFG_GYRO_SENSITIVITY_Z * sensitivity_multiplier;
    }
    // Additional processing.
    double t = 1.0;
    double k = 0.5;
    if      (x > 0 && x <  t) x =  hssnf(t, k,  x);
    else if (x < 0 && x > -t) x = -hssnf(t, k, -x);
    if      (y > 0 && y <  t) y =  hssnf(t, k,  y);
    else if (y < 0 && y > -t) y = -hssnf(t, k, -y);
    if      (z > 0 && z <  t) z =  hssnf(t, k,  z);
    else if (z < 0 && z > -t) z = -hssnf(t, k, -z);
    // Acceleration (the ADS curve replaces the normal one while aiming).
    if (self->accel.curve != GYRO_ACCEL_OFF || self->extras.ads_accel.curve != GYRO_ACCEL_OFF) {
        gyro_accel_update_speed(self, imu_gyro);
        GyroAccel *accel = ads ? &(self->extras.ads_accel) : &(self->accel);
        double gain = gyro_accel_gain(accel, self->accel_speed);
        // Vertical (pitch) gets only part of the boost above the low gain.
        double gain_y = accel->gain_min + (gain - accel->gain_min) * self->extras.accel_vertical;
        x *= gain;
        y *= gain_y;
        z *= gain;
    }
    if (ads) {
        x *= self->extras.ads_sens;
        y *= self->extras.ads_sens;
        z *= self->extras.ads_sens;
    }
    // Normalize per-tick output so sensitivity does not depend on polling rate.
    double rate_factor = (double)CFG_TICK_FREQUENCY_BASE / CFG_TICK_FREQUENCY;
    x *= rate_factor;
    y *= rate_factor;
    z *= rate_factor;
    // Report.
    if (x >= 0) gyro_incremental_output( x * self->sens_x, self->actions_x_pos);
    else        gyro_incremental_output(-x * self->sens_x, self->actions_x_neg);
    if (y >= 0) gyro_incremental_output( y * self->sens_y, self->actions_y_pos);
    else        gyro_incremental_output(-y * self->sens_y, self->actions_y_neg);
    if (z >= 0) gyro_incremental_output( z * self->sens_z, self->actions_z_pos);
    else        gyro_incremental_output(-z * self->sens_z, self->actions_z_neg);
}

bool Gyro__is_engaged(Gyro *self) {
    if (self->engage == PIN_NONE) return false;
    if (self->engage == PIN_TOUCH_IN) return touch_status();
    return self->engage_button.is_pressed(&(self->engage_button));
}

void Gyro__report(Gyro *self) {
    if (self->mode == GYRO_MODE_TOUCH_ON) {
        if (self->is_engaged(self)) self->report_incremental(self);
        else gyro_reset_filters(self);  // Avoid stale state bursts on re-engage.
    }
    else if (self->mode == GYRO_MODE_TOUCH_OFF) {
        if (!self->is_engaged(self)) self->report_incremental(self);
        else gyro_reset_filters(self);
    }
    else if (self->mode == GYRO_MODE_ALWAYS_ON) {
        self->report_incremental(self);
    }
    else if (self->mode == GYRO_MODE_AXIS_ABSOLUTE) {
        self->report_absolute(self);
    }
    else if (self->mode == GYRO_MODE_OFF) {
        return;
    }
}

void Gyro__reset(Gyro *self) {
    world_init = 0;
    self->pressed_x_pos = false;
    self->pressed_y_pos = false;
    self->pressed_z_pos = false;
    self->pressed_x_neg = false;
    self->pressed_y_neg = false;
    self->pressed_z_neg = false;
}

void Gyro__config_x(Gyro *self, double min, double max, Actions neg, Actions pos, float sens) {
    self->absolute_x_min = min;
    self->absolute_x_max = max;
    memcpy(self->actions_x_neg, neg, ACTIONS_LEN);
    memcpy(self->actions_x_pos, pos, ACTIONS_LEN);
    self->sens_x = sens;
}

void Gyro__config_y(Gyro *self, double min, double max, Actions neg, Actions pos, float sens) {
    self->absolute_y_min = min;
    self->absolute_y_max = max;
    memcpy(self->actions_y_neg, neg, ACTIONS_LEN);
    memcpy(self->actions_y_pos, pos, ACTIONS_LEN);
    self->sens_y = sens;
}

void Gyro__config_z(Gyro *self, double min, double max, Actions neg, Actions pos, float sens) {
    self->absolute_z_min = min;
    self->absolute_z_max = max;
    memcpy(self->actions_z_neg, neg, ACTIONS_LEN);
    memcpy(self->actions_z_pos, pos, ACTIONS_LEN);
    self->sens_z = sens;
}

void Gyro__config_accel(Gyro *self, GyroAccel accel) {
    self->accel = accel;
    self->accel_speed = 0;
}

void Gyro__config_extras(Gyro *self, GyroExtras extras) {
    self->extras = extras;
    if (extras.ads_engage != PIN_NONE) {
        Actions none = {0,};
        self->ads_button = Button_(extras.ads_engage, NORMAL, none, none, none);
    }
    self->gravity_init = false;
    gyro_reset_filters(self);
}

Gyro Gyro_ (
    GyroMode mode,
    uint8_t engage
) {
    Gyro gyro;
    gyro.is_engaged = Gyro__is_engaged;
    gyro.report = Gyro__report;
    gyro.report_incremental = Gyro__report_incremental;
    gyro.report_absolute = Gyro__report_absolute;
    gyro.reset = Gyro__reset;
    gyro.config_x = Gyro__config_x;
    gyro.config_y = Gyro__config_y;
    gyro.config_z = Gyro__config_z;
    gyro.config_accel = Gyro__config_accel;
    gyro.config_extras = Gyro__config_extras;
    gyro.extras = (GyroExtras){
        .ads_engage = PIN_NONE,
        .ads_sens = 1,
        .ads_accel = {.curve = GYRO_ACCEL_OFF, .gain_min = 1, .gain_max = 1},
        .space = GYRO_SPACE_LOCAL,
        .accel_vertical = 1,
    };
    gyro.smooth_state = (Vector){0, 0, 0};
    gyro.gravity_init = false;
    gyro.accel = (GyroAccel){.curve = GYRO_ACCEL_OFF, .gain_min = 1, .gain_max = 1};
    gyro.accel_speed = 0;
    gyro.mode = mode;
    gyro.engage = engage;
    if (engage != PIN_NONE && engage != PIN_TOUCH_IN) {
        Actions none = {0,};
        gyro.engage_button = Button_(engage, NORMAL, none, none, none);
    }
    memset(gyro.actions_x_pos, 0, ACTIONS_LEN);
    memset(gyro.actions_y_pos, 0, ACTIONS_LEN);
    memset(gyro.actions_z_pos, 0, ACTIONS_LEN);
    memset(gyro.actions_x_neg, 0, ACTIONS_LEN);
    memset(gyro.actions_y_neg, 0, ACTIONS_LEN);
    memset(gyro.actions_z_neg, 0, ACTIONS_LEN);
    gyro_update_sensitivity();
    gyro.reset(&gyro);
    return gyro;
}
