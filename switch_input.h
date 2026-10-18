/*
 * switch_input.h
 *
 * The switch/LED-decode subsystem: the GPIO interrupt entry point, the switch
 * sampler (the one reader of the UI expanders' input ports - DECISIONS.md #41),
 * the double-press check, and the switch-mask -> bed_control.h verb
 * translation. Relocated from app.c on C4 commit 4 (Jul 2026).
 *
 * switch_pressed/app_ui_interrupt were temporary externs in app.h (via
 * commits 2/3, read-only by bed_actions.c/settings_mode.c); their real
 * definitions live here and app.h's declarations moved here too. Since #41 each
 * process_inputs() call sees exactly one single-bit app_ui_interrupt mask.
 */

#ifndef SWITCH_INPUT_H_
#define SWITCH_INPUT_H_

#include <stdbool.h>
#include <stdint.h>

extern bool switch_pressed; // Interrupt flag to capture switch up/down state
extern uint16_t app_ui_interrupt; // Packed Port0/1 interrupt source(s)

// Baseline read of both UI boards, taking a re-driver hold if a button is
// found down. Call once from app_init(), right after init_ui_expander(). No
// events are generated for what it finds.
void switch_sampler_init(void);

// Runs the double-press check, then the switch sampler, then delivers the edges
// the sampler accepted to process_inputs() (one call per bit, releases first).
// Called once per tick from app_process_action().
void switch_input_tick(void);

// Sampler state for other modules (DECISIONS.md #41):
//  - switch_sampler_active(): a sampling session is running; the main loop must
//    keep running, so app_is_ok_to_sleep() vetoes sleep.
//  - switch_sampler_down(): debounced mask of switches physically down (same
//    bit layout as app_ui_interrupt). 0 whenever the sampler is idle.
//  - switch_state_fresh(): false if a session is running but has had no good
//    read for SWITCH_STATE_STALE_MS; callers must then treat it as "no switch
//    down" (fail toward OFF).
bool switch_sampler_active(void);
uint16_t switch_sampler_down(void);
bool switch_state_fresh(void);

// Unconditional motion fail-safe (DECISIONS.md #38). Call once per
// app_process_action(). De-energises the motors whenever the sampler's
// debounced state says no switch is down (stale counts as none), the app is not
// commanding motion, auto-level is not running and settings mode is inactive -
// regardless of what active_switch, switch_held, switch_driving or any table
// match believe.
void motion_failsafe_tick(void);

#endif /* SWITCH_INPUT_H_ */
