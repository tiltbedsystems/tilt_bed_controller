/*
 * dual_motor_control.h
 *
 *  Created on: Dec 30, 2023
 *      Author: Andrew Duncan
 */

#ifndef DUAL_MOTOR_CONTROL_H_
#define DUAL_MOTOR_CONTROL_H_

#include <stdint.h>

#include "bed_types.h"

// Power board output assignments
extern const uint8_t OUTPUT0;
extern const uint8_t OUTPUT1;
extern const uint8_t OUTPUT2;
extern const uint8_t OUTPUT3;

// Motor assignment variables
extern uint8_t MOTORA;
extern uint8_t MOTORB;
extern uint8_t MOTORC;
extern uint8_t MOTORD;

// Auto_level motors on or off status
extern bool auto_level_motors_on;

// For temp all up/down limit switch pause
extern bool set_motors_off_once;
extern bool temp_motor_off_timer_timeout;
extern bool motors_temp_off;

// Motor statuses

typedef struct {
  bool    motor_on;              // Motor on/off status
  bool    motor_up;              // Motor direction
} individual_motor_state_t; 

// Motor state
extern individual_motor_state_t motor_state[NUM_MOTORS];

typedef struct {
  bool upper;
  bool lower;
} limit_switch_t; // Flags to determine if at upper or lower limit switch

extern limit_switch_t at_limit_switch[NUM_MOTORS]; // Read by bed_status.c for telemetry

/***************************************************************************//**
 * Bed side dual motor control functions
 ******************************************************************************/
void set_dual_motors_on (side_of_bed_t side);
void set_dual_motors_off (side_of_bed_t side);

void set_dual_motors_direction_up (side_of_bed_t side);
void set_dual_motors_direction_down (side_of_bed_t side);

void auto_level_limit_switch_motor_off(uint8_t motor, int8_t auto_level_current_axis);
void all_motor_limit_switch_temp_off();

void limit_switch_statuses(uint8_t motor, active_switch_t switch_value, int8_t auto_level_current_axis);
void reverse_motor_direction_and_start(active_switch_t switch_value);

#endif /* DUAL_MOTOR_CONTROL_H_ */
