/*
 * settings_mode.h
 *
 * The "settings mode" subsystem: holding BED_LIGHTS 5 s enters it; while in
 * it, AUTO_LEVEL-hold stores/erases the custom zero angle and UP_ALL/
 * DOWN_ALL-hold steps the collision-sensitivity ladder, writing to NVM3.
 * A later, separate press of BED_LIGHTS exits it (settings_mode_exit()).
 * Relocated from app.c on C4 commit 3 (Jul 2026) - pure relocation, no
 * behavior change. in_settings_mode is defined here now (was app.c); app.c's
 * own tick/dispatch logic still reads it via this header, same as
 * bed_actions.c did through app.h before this move.
 *
 * settings_mode_tick()/settings_mode_process_outputs() read (never write)
 * switch_pressed and app_ui_interrupt, defined in switch_input.c and shared
 * via switch_input.h (relocated there from app.c on commit 4).
 */

#ifndef SETTINGS_MODE_H_
#define SETTINGS_MODE_H_

#include <stdbool.h>

extern bool in_settings_mode;

// Runs the settings-mode entry check and (while active) the zero-angle and
// sensitivity-ladder logic. Called once per tick from app_process_action(),
// same relative position as the inline block it replaces.
void settings_mode_tick(void);

// Settings-mode LED/motor output handling for the currently latched switch
// interrupt (app_ui_interrupt). Called from process_outputs() when
// in_settings_mode is true.
void settings_mode_process_outputs(void);

// Cancels the entry/erase 5-second timer and clears its timeout flag. Called
// by app.c from two sites that abort settings-mode entry from the outside:
// BED_LIGHTS released early (process_inputs()) and a double-press error
// during the entry hold (the double-press-check block).
void settings_mode_cancel_entry(void);

// Abandons any settings-mode hold in progress (UP_ALL/DOWN_ALL sensitivity
// step, AUTO_LEVEL zero-angle store/erase): stops both settings timers and
// clears both timeout flags. No NVM3 write; does NOT leave settings mode.
// Called by the hold-release branches, by settings_mode_exit(), and by
// app_stop_all() (bed_actions.c) when STOP arrives in settings mode.
void settings_mode_cancel_holds(void);

// Leaves settings mode: stops both settings timers (no NVM3 write), turns the
// motors off, stops the amber fast blink and restores amber to the under-bed
// lights state, turns red/green/blue off, and clears the panel bookkeeping
// (switch_held, switch_driving, active_switch, the switches' half of the #36
// latch). Called on a new BED_LIGHTS press while in settings mode.
void settings_mode_exit(void);

#endif /* SETTINGS_MODE_H_ */
