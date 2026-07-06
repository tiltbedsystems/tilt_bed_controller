/*
 * bed_status.h
 *
 * Read-only telemetry snapshot (refactor step C3). A single
 * bed_status_get() call gathers everything a BLE notify characteristic or a
 * bench debug print would want, from data the firmware already tracks - no
 * new sampling. Hardware-neutral: no internal enums (active_switch_t etc.)
 * are exposed here, matching bed_control.h's approach.
 *
 *  Created on: Jul 4, 2026
 */

#ifndef BED_STATUS_H_
#define BED_STATUS_H_

#include <stdint.h>
#include <stdbool.h>

#include "bed_control.h"
#include "bed_types.h"

typedef struct {
  bool on;
  bool moving_up;           // Meaningful only while 'on' is true - retains the
                             // last-commanded direction after the motor stops
  bool at_upper_limit;
  bool at_lower_limit;
  bool fault;
  uint32_t filtered_current_ma;
} bed_motor_status_t;

typedef enum {
  BED_COMMAND_NONE,
  BED_COMMAND_MOVE,
  BED_COMMAND_AUTO_LEVEL,
  BED_COMMAND_LIGHTS
} bed_command_t;

typedef struct {
  // Indexed by physical motor position (A-D / 0-3), matching motor_state[]'s
  // own convention - NOT by logical side (a side is two motors sharing a
  // corner pair; see dual_motor_control.c).
  bed_motor_status_t motors[NUM_MOTORS];

  // Current tilt, deviation from the user's level baseline (scaled). Only
  // continuously fresh while auto-level or the settings zero-level capture
  // is running - stale (last-known, possibly 0) otherwise. See
  // DECISIONS.md #17.
  int16_t roll_angle_scaled;
  int16_t pitch_angle_scaled;

  bool any_collision;
  bool in_settings_mode;
  bool lights_on;

  bed_command_t command;
  bed_target_t command_target;       // valid only when command == BED_COMMAND_MOVE
  bed_direction_t command_direction; // valid only when command == BED_COMMAND_MOVE

  bed_source_t active_source;
} bed_status_t;

void bed_status_get(bed_status_t *out);

#endif /* BED_STATUS_H_ */
