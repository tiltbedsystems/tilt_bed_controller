/*
 * bed_actions.h
 *
 * The action functions bed_control.c's verbs call into once arbitration has
 * decided a command may proceed normally. Implemented in bed_actions.c,
 * which owns the movement/lights action state (active_move_is_all,
 * bedlights_on) - the rest of the movement/settings state these functions
 * touch (active_switch, is_raising, auto_level_active, first_print[]) stays
 * owned by app.c (see app.h), since it's also read/written extensively by
 * app.c's own tick logic. in_settings_mode is owned by settings_mode.c (see
 * settings_mode.h) since C4 commit 3. bed_control.c itself stays free of
 * this - and of any hardware/SDK dependency - so it remains host-testable in
 * isolation (see DECISIONS.md #12).
 *
 *  Created on: Jul 7, 2026
 *  Relocated from app_actions.h/app.c on C4 (Jul 2026).
 */

#ifndef BED_ACTIONS_H_
#define BED_ACTIONS_H_

#include <stdbool.h>

#include "bed_control.h"
#include "bed_types.h"

void app_move_start(bed_target_t target, bed_direction_t dir);
void app_move_stop(void);
void app_auto_level_start(void);
void app_auto_level_stop(void);
void app_lights_set(bool on);

// Full clean stop: motors, auto-level, and LEDs off, active_switch reset to
// OFF. Used when a non-active source's command is force-stopped.
void app_stop_all(void);

// Full stop of MOVEMENT only - motors, auto-level and the movement LEDs
// (green/blue/red) - leaving the under-bed lighting and its amber indicator
// untouched. Used for two-source conflicts (DECISIONS.md #36).
void app_stop_movement(void);

// Output application for APP-sourced commands (BLE Stage C). The switch path
// gets its outputs from switch_input.c's process_outputs(), driven by a real
// switch edge; an app command has no edge, so ble_bed_service.c calls these
// after the matching bed_control verb succeeds. See the block comment above
// their definitions in bed_actions.c.
void app_apply_move_outputs(bed_target_t target);
void app_apply_auto_level_start_outputs(void);
void app_apply_lights_outputs(void);
void app_apply_stop_outputs(void);

// Returns active_switch to OFF. Required on every terminal app command -
// without it app_bed_is_idle() never goes true and bed_control's source lock
// is never released, locking out the wall switch (DECISIONS.md #12).
void app_release_active_switch(void);

bool app_bed_is_idle(void);
active_switch_t app_active_command(void);
bool app_in_settings_mode(void);
bool app_lights_on(void);

#endif /* BED_ACTIONS_H_ */
