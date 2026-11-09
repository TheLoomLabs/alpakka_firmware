// SPDX-License-Identifier: GPL-2.0-only
// Macro player (fork extension).

#pragma once
#include <stdint.h>
#include <stdbool.h>

/*
Macro storage (CtrlMacro section, 2 macros per section, 28 bytes each).

The per-macro `format` byte selects the encoding:
- 0x00: Legacy. Each byte is a key, typed one after another (10ms press, 10ms gap).
- bit 7 set: Steps format.
    bits 0-1: Trigger mode (MacroMode).
    bits 2-6: Tap hold time, (value + 1) * 10 ms.

Steps format: up to 14 steps of 2 bytes [action, op_delay].
- op_delay bits 6-7: operation (MacroOp).
- op_delay bits 0-5: delay code, see macro_delay_ms(). The delay is waited after
  the operation (after the release, for taps).
- A step with both bytes zero ends the macro.
*/

#define MACRO_FORMAT_STEPS 0x80
#define MACRO_STEPS_MAX 14
#define MACRO_SLOTS 8

typedef enum _MacroMode {
    MACRO_MODE_ONCE = 0,         // Play once per press.
    MACRO_MODE_REPEAT_HELD = 1,  // Loop while the button is held.
    MACRO_MODE_TOGGLE = 2,       // Press to start looping, press again to stop.
} MacroMode;

typedef enum _MacroOp {
    MACRO_OP_TAP = 0,    // Press, hold for the tap time, release.
    MACRO_OP_DOWN = 1,   // Press and keep held.
    MACRO_OP_UP = 2,     // Release.
    MACRO_OP_WAIT = 3,   // Only wait (action ignored).
} MacroOp;

uint16_t macro_delay_ms(uint8_t code);
void macro_trigger_press(uint8_t index);    // Index 1-8.
void macro_trigger_release(uint8_t index);  // Index 1-8.
void macro_tick();
void macro_reset();
