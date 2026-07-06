/*
 * motor_current_functions.h
 *
 *  Created on: Dec 31, 2023
 *      Author: Andrew Duncan
 */

#ifndef MOTOR_CURRENT_FUNCTIONS_H_
#define MOTOR_CURRENT_FUNCTIONS_H_

#include "dual_motor_control.h"

extern bool motor_is_moving_stable[NUM_MOTORS];
extern bool collision[NUM_MOTORS];
extern bool any_collision;

extern uint32_t filtered_current[NUM_MOTORS]; // Low-pass-filtered per-motor current (mA) - read by bed_status.c for telemetry

extern bool dynamic_collision_detection_enabled; // Boolean to tell if dynamic collision detection is enabled.

extern uint16_t dynamic_reading_count[NUM_MOTORS]; // Counter to determine how many readings have been made since switch was pressed
extern uint16_t dynamic_max_average_calibration_current[NUM_MOTORS];
extern uint16_t dynamic_collision_current_threshold[NUM_MOTORS];

extern int16_t raise_collision_current_security_margin; // To allow app.c to write to NVM3
extern int16_t lower_collision_current_security_margin; // To allow app.c to write to NVM3
extern bool motor_current_running_average_on; // To allow app.c to control when to use the "check_motor_movement" function
extern uint8_t reading_count;
extern bool initial_switch_press;

/*******************************************************************************
 * Motor Current Supervision and Safety Functions
 ******************************************************************************/
void scan_all_currents();
void check_motor_movement();
uint32_t low_pass_filter(uint32_t new_value, uint32_t old_value, float alpha);
void motor_current_running_average(uint8_t motor);
void set_dynamic_collosion_threshold(uint8_t motor);
void read_motor_movement(uint8_t motor);
void reset_ignore_data_points_count();

void max_load_check(uint8_t motor);
void collision_check(bool is_raising);
void run_motor_supervision(active_switch_t sw, bool is_raising);

void print_current_value(uint8_t motor);

void reset_motor_current_data();

#endif /* MOTOR_CURRENT_FUNCTIONS_H_ */
