/*
 * auto_level.h
 *
 *  Created on: May 7, 2023
 *      Author: Andrew Duncan
 */

#ifndef AUTO_LEVEL_H_
#define AUTO_LEVEL_H_

#include <stdbool.h>
#include <stdint.h>

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

/***************************************************************************//**
 * Tilt telemetry sampler (BLE_GATT_DESIGN.md #6, DECISIONS.md #17/#31/#33)
 *
 * Keeps the app's tilt readout live outside auto-level. Entirely separate
 * storage from auto-level's filter chain - see the big comment block in
 * auto_level.c and DECISIONS #31 for why that separation is mandatory and
 * must not be "cleaned up" into one shared buffer.
 ******************************************************************************/

// Call once per app_process_action(), main-loop context only. Never from a
// Bluetooth stack callback (DECISIONS #27 - a blocking accel read inside a
// callback is the mechanism behind the reason=0x1008 drops).
//   status_subscribed     - at least one connection has Bed Status notifies on
//   other_sampler_active  - auto-level or settings zero-level capture running
void tilt_telemetry_tick(bool status_subscribed, bool other_sampler_active);

// Opens (or extends) the sampling burst. Idempotent; never stacks. Commands
// no motion and cancels none - it only sets a timer.
void tilt_telemetry_arm_burst(void);

// True while a burst is open. app_is_ok_to_sleep() must veto sleep on this,
// or a standalone refresh has no main loop to run on.
bool tilt_telemetry_burst_active(void);

// True once the telemetry sampler has produced at least one value since boot.
// Until then bed_status.c falls back to auto-level's last-known value.
bool tilt_telemetry_value_valid(void);

// Latest telemetry tilt, SCALED, deviation from the user's baseline, in
// auto_level.c's INTERNAL sign convention - bed_status.c applies the wire
// convention (see its comment; pitch is negated exactly once, there).
int16_t tilt_telemetry_roll_scaled(void);
int16_t tilt_telemetry_pitch_scaled(void);


#endif /* AUTO_LEVEL_H_ */
