/*
 * switch_input.h
 *
 * The switch/LED-decode subsystem: the GPIO interrupt entry point, the
 * double-press-error check, and the switch-mask -> bed_control.h verb
 * translation. Relocated from app.c on C4 commit 4 (Jul 2026) - pure
 * relocation, no behavior change.
 *
 * switch_pressed/app_ui_interrupt were temporary externs in app.h (via
 * commits 2/3, read-only by bed_actions.c/settings_mode.c); this commit
 * relocates their real definitions here and app.h's declarations move here
 * too. app_reset_double_press_timer()'s implementation also relocates here
 * (it now owns double_press_error_timer); app.h's declaration is removed and
 * bed_actions.c switches its include.
 */

#ifndef SWITCH_INPUT_H_
#define SWITCH_INPUT_H_

#include <stdbool.h>
#include <stdint.h>

extern bool switch_pressed; // Interrupt flag to capture switch up/down state
extern uint16_t app_ui_interrupt; // Packed Port0/1 interrupt source(s)

// Resets the double-press-error timer. Called by bed_actions.c's moved
// action functions, which used to touch double_press_error_timer directly
// via its (then) app.c-private handle.
void app_reset_double_press_timer(void);

// Runs the double-press-error check and (when a fresh switch change is
// latched) the UI interrupt/status read + process_inputs() dispatch. Called
// once per tick from app_process_action(), same relative position as the two
// inline blocks it replaces.
void switch_input_tick(void);

#endif /* SWITCH_INPUT_H_ */
