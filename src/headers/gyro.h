// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022, Input Labs Oy.

#pragma once
#include "vector.h"

typedef enum GyroMode_enum {
    GYRO_MODE_OFF,
    GYRO_MODE_ALWAYS_ON,
    GYRO_MODE_TOUCH_OFF,
    GYRO_MODE_TOUCH_ON,
    GYRO_MODE_AXIS_ABSOLUTE,
} GyroMode;

typedef enum GyroAccelCurve_enum {
    GYRO_ACCEL_OFF,
    GYRO_ACCEL_LINEAR,   // Gain grows linearly with speed.
    GYRO_ACCEL_POWER,    // Gain grows as speed^exponent (classic).
    GYRO_ACCEL_NATURAL,  // Fast ramp that eases into the max gain.
    GYRO_ACCEL_SIGMOID,  // S-curve: precise zone, transition, flick zone.
} GyroAccelCurve;

typedef enum GyroSpace_enum {
    GYRO_SPACE_LOCAL,   // Controller axes (stock behavior).
    GYRO_SPACE_PLAYER,  // Yaw follows the real-world vertical axis (gravity).
} GyroSpace;


// Combined gyro value scale (IMU0 at 500 dps full scale, LSM6DS: 17.5 mdps/LSB).
#define GYRO_DPS_PER_LSB 0.0175

typedef struct _GyroAccel {
    GyroAccelCurve curve;
    float gain_min;
    float gain_max;
    float threshold;  // deg/s.
    float speed_max;  // deg/s.
    float exponent;
    float smoothing;  // ms.
} GyroAccel;

float gyro_accel_gain(GyroAccel *accel, float speed);
float gyro_real_world_scale(uint16_t counts_360, uint8_t ratio_tenths);

typedef struct _GyroExtras {
    uint8_t ads_engage;  // Pin, PIN_NONE if disabled.
    float ads_sens;
    GyroAccel ads_accel;
    float tighten;           // deg/s.
    float smooth_threshold;  // deg/s.
    float smooth_time;       // ms.
    GyroSpace space;
    float accel_vertical;    // Share of the acceleration boost on pitch, 0-1.
    float rw_scale;          // Real-world calibration: counts per base tick per
                             // raw gyro unit. 0 = use global mouse sensitivity.
    float shot_time;         // Shot shake suppression window, ms (0 = off).
    float shot_threshold;    // deg/s.
} GyroExtras;

typedef struct Gyro_struct Gyro;
struct Gyro_struct {
    bool (*is_engaged) (Gyro *self);
    void (*report) (Gyro *self);
    void (*report_incremental) (Gyro *self);
    void (*report_absolute) (Gyro *self);
    void (*reset) (Gyro *self);
    void (*config_x) (Gyro *self, double min, double max, Actions neg, Actions pos, float sens);
    void (*config_y) (Gyro *self, double min, double max, Actions neg, Actions pos, float sens);
    void (*config_z) (Gyro *self, double min, double max, Actions neg, Actions pos, float sens);
    void (*config_accel) (Gyro *self, GyroAccel accel);
    void (*config_extras) (Gyro *self, GyroExtras extras);
    GyroMode mode;
    uint8_t engage;
    Button engage_button;
    double absolute_x_min;
    double absolute_y_min;
    double absolute_z_min;
    double absolute_x_max;
    double absolute_y_max;
    double absolute_z_max;
    bool pressed_x_pos;
    bool pressed_y_pos;
    bool pressed_z_pos;
    bool pressed_x_neg;
    bool pressed_y_neg;
    bool pressed_z_neg;
    Actions actions_x_pos;
    Actions actions_y_pos;
    Actions actions_z_pos;
    Actions actions_x_neg;
    Actions actions_y_neg;
    Actions actions_z_neg;
    float sens_x;
    float sens_y;
    float sens_z;
    GyroAccel accel;
    float accel_speed;  // Smoothed angular speed, deg/s.
    GyroExtras extras;
    Button ads_button;
    Vector smooth_state;  // Tiered smoothing residual.
    Vector gravity;       // Smoothed gravity in gyro axes (player space).
    bool gravity_init;
    uint32_t shot_buttons;  // Last seen IO expander state.
    bool shot_init;
    float shot_left;        // ms left in the suppression window.
};

Gyro Gyro_ (
    GyroMode mode,
    uint8_t engage
);

void gyro_update_sensitivity();
// Aim-down-sights boost on / off at runtime (fork extension, not stored).
void gyro_ads_toggle();
bool gyro_ads_is_enabled();
