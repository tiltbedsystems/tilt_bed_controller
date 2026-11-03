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

  // Current tilt, deviation from the user's level baseline (scaled), from
  // whichever filter chain bed_status_get() picks. tilt_fresh says whether it
  // is live (Prompt AI C4); see bed_status_get().
  //
  // SIGN CONVENTION - these are wire-facing values, and pitch does NOT match
  // auto_level.c's internal sign. bed_status_get() negates pitch exactly once
  // on the way out; see the comment there.
  //   roll_angle_scaled  POSITIVE = LEFT side raised
  //                      (derived from auto_level.c's frame diagram, NOT yet
  //                      bench-confirmed - BLE_GATT_DESIGN.md #6)
  //   pitch_angle_scaled POSITIVE = HEAD raised
  int16_t roll_angle_scaled;
  int16_t pitch_angle_scaled;
  bool tilt_fresh;           // Bed Status flag bit 3: the angles above are live (Prompt AI C4)

  bool any_collision;
  bool in_settings_mode;
  bool lights_on;
  bool leveled;              // last auto-level run succeeded, nothing moved since (#46)
  bool auto_level_settling;  // an auto-level run is in its Step 1 settling read (Prompt AI)

  bed_command_t command;
  bed_target_t command_target;       // valid only when command == BED_COMMAND_MOVE
  bed_direction_t command_direction; // valid only when command == BED_COMMAND_MOVE

  bed_source_t active_source;
} bed_status_t;

void bed_status_get(bed_status_t *out);

#endif /* BED_STATUS_H_ */
