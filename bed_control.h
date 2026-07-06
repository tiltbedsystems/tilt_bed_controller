/*
 * bed_control.h
 *
 * Command API for the bed: the verbs a phone app will call (D1) and the
 * ones physical switches call today (C2). Owns the two-source lock
 * (DECISIONS.md #12) - every command carries the calling source, and any
 * input from a source other than the one currently holding the lock is
 * interpreted only as a clean stop.
 *
 * Collision/limit supervision (run_motor_supervision, B5) runs beneath
 * this API in the main tick, so every source inherits it automatically
 * (DECISIONS.md #13). No bed_reset() verb - RESET is hardware-only,
 * deliberately omitted (DECISIONS.md #16).
 *
 *  Created on: Jul 7, 2026
 */

#ifndef BED_CONTROL_H_
#define BED_CONTROL_H_

#include <stdbool.h>

typedef enum {
  BED_SOURCE_NONE,
  BED_SOURCE_SWITCHES,
  BED_SOURCE_APP
} bed_source_t;

typedef enum {
  BED_TARGET_HEAD,
  BED_TARGET_LEFT,
  BED_TARGET_RIGHT,
  BED_TARGET_FOOT,
  BED_TARGET_ALL
} bed_target_t;

typedef enum {
  BED_DIR_DOWN,
  BED_DIR_UP
} bed_direction_t;

void bed_move_start(bed_source_t src, bed_target_t target, bed_direction_t dir);
void bed_move_stop(bed_source_t src);
void bed_auto_level_start(bed_source_t src);
void bed_auto_level_stop(bed_source_t src);
void bed_lights_set(bed_source_t src, bool on);

// Call once per app tick: releases the lock promptly when the bed goes idle.
void bed_control_tick(void);

// For future telemetry / BLE control-holder UI.
bed_source_t bed_active_source(void);

#endif /* BED_CONTROL_H_ */
