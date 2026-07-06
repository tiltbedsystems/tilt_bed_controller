/*
 * bed_types.h
 *
 * Shared value types used across the bed control modules (app, auto_level,
 * dual_motor_control, motor_current_functions), consolidated to a single
 * definition each.
 */

#ifndef BED_TYPES_H_
#define BED_TYPES_H_

#define NUM_MOTORS 4
#define NUM_AXES 2 // Pitch and roll
#define SCALE_FACTOR 100

typedef enum {
    ROLL,
    PITCH
} axis_t;

typedef enum {
    HEAD,
    LEFT,
    RIGHT,
    FOOT
} side_of_bed_t;

typedef enum active_switch {
  SWITCH_UP_ALL_VALUE,
  SWITCH_AUTO_LEVEL_VALUE,
  SWITCH_DOWN_ALL_VALUE,
  SWITCH_DOWN_HEAD_VALUE,
  SWITCH_DOWN_LEFT_VALUE,
  SWITCH_DOWN_RIGHT_VALUE,
  SWITCH_DOWN_FOOT_VALUE,
  SWITCH_UP_HEAD_VALUE,
  SWITCH_UP_LEFT_VALUE,
  SWITCH_UP_RIGHT_VALUE,
  SWITCH_UP_FOOT_VALUE,
  NUM_MOTOR_SWITCHES,
  SWITCH_BED_LIGHT_VALUE,
  OFF = -1
} active_switch_t;

#endif /* BED_TYPES_H_ */
