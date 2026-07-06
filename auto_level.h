/*
 * auto_level.h
 *
 *  Created on: May 7, 2023
 *      Author: Andrew Duncan
 */

#ifndef AUTO_LEVEL_H_
#define AUTO_LEVEL_H_

#include "bed_types.h"

typedef struct {
  bool level;
  bool positive;
  bool close_to_level;
  bool pulse;
} side_level_status_t; // Flags to indicate which sides are level, too high, or too low


extern side_level_status_t level_status[NUM_AXES];

extern int16_t baseline_roll_angle; // To allow app.c to write to NVM3
extern int16_t baseline_pitch_angle; // To allow app.c to write to NVM3

// Current tilt, deviation from the user's level baseline (scaled). Only
// updated while accel_running_average() runs - i.e. while auto-level or the
// settings zero-level capture is active. Stale (last-known) otherwise; see
// DECISIONS.md #17. Read by bed_status.c for telemetry.
extern int16_t baseline_accel_average_roll_angle;
extern int16_t baseline_accel_average_pitch_angle;

extern int8_t auto_level_current_axis; // Used for input to limit switch supervision in app.c
extern bool first_check; // Set in app.c interrupt inputs
extern bool leveling_success[NUM_AXES]; // Used to stop auto-leveling process in app.c
extern bool bed_level;
extern bool bed_leveling_disabled;
extern bool axis_disabled[NUM_AXES];
/***************************************************************************//**
 * Auto-level code functions
 ******************************************************************************/

void accel_running_average();
void print_accel_value(axis_t axis);
void read_accel();
void set_auto_level_motor_direction(axis_t axis);
void check_level(axis_t axis);
void level_axis (axis_t axis);

void auto_level();
void auto_level_single_axis(axis_t axis);
void steady_accel_read_both_axes();

void reset_level_data();
int16_t returnAccelValue(axis_t axis);


#endif /* AUTO_LEVEL_H_ */
