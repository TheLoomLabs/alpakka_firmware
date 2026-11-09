// SPDX-License-Identifier: GPL-2.0-only
// Real-world calibration, in-game 360 test (fork extension).

#pragma once
#include <stdint.h>
#include <stdbool.h>

/*
Calibration mode is started and stopped by the app (config key CALIBRATION_360)
and is not stored. While active, normal profile input is paused and only these
buttons are read:

- R2: turn by exactly `counts` mouse counts (spread over about one second).
- D-pad right / left: counts +1% / -1%.
- D-pad up / down: counts +0.1% / -0.1% (at least one count).
- Home: cancel calibration mode.

The user adjusts `counts` until the game turns exactly 360 degrees. The app
then reads the value and stores it in the profile (CtrlGyro rw_counts_360).
*/

#define CALIB360_COUNTS_MIN 100
#define CALIB360_COUNTS_MAX 65535

void calib360_start(uint16_t counts);
void calib360_stop();
bool calib360_is_active();
uint16_t calib360_get_counts();
// Returns true once after a start or stop: profile inputs must be released.
bool calib360_take_reset();
// Called every tick instead of the profile report while active.
void calib360_report();
