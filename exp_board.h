/*
 * exp_board.h
 *
 *  Created on: Apr 16, 2023
 *      Author: Alan Jones
 */

#ifndef EXP_BOARD_H_
#define EXP_BOARD_H_




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

#endif /* EXP_BOARD_H_ */
