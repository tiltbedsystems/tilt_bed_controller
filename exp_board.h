/*
 * exp_board.h
 *
 *  Created on: Apr 16, 2023
 *      Author: Alan Jones
 */

#ifndef EXP_BOARD_H_
#define EXP_BOARD_H_

#include <stdbool.h>
#include <stdint.h>


/***************************************************************************//**
 * Initialize local board GPIO expander
 ******************************************************************************/

void init_board_expander();

/***************************************************************************//**
 * Local board GPIO expander interface functions
 ******************************************************************************/

void set_motor_on(int motor);
void set_motor_off(int motor);

void set_motor_direction_up(int motor);
void set_motor_direction_down(int motor);

void set_all_motors_on();
void set_all_motors_off();

void set_all_motors_direction_up();
void set_all_motors_direction_down();

void set_underbed_lighting_on();
void set_underbed_lighting_off();

void set_I2C_redriver_on();
void set_I2C_redriver_off();

uint8_t get_motor_faults();
// Reads the motor ENABLE bits (P0_0-P0_3) back from the on-board expander's output
// register. Returns false if the I2C transfer failed; *enables is then untouched.
// Deliberately has no EFM_ASSERT: it is called by the motion fail-safe, which must
// survive a bus error rather than hang on it.
bool get_motor_enables(uint8_t *enables);

// Interim polled motor-fault telemetry (main-loop context) - see
// sample_motor_faults_tick()'s comment in exp_board.c for the rate
// justification and the transient-miss limitation.
void sample_motor_faults_tick(void);
uint8_t get_cached_motor_faults(void);

#endif /* EXP_BOARD_H_ */
