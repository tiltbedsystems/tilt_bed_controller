/*
 * dual_motor_control.c
 *
 *  Created on: Dec 30, 2023
 *      Author: Andrew Duncan
 */

#include <stdio.h>
#include <stdbool.h>

#include "bed_types.h"
#include "dual_motor_control.h"
#include "motor_current_functions.h"
#include "exp_board.h"
#include "sl_sleeptimer.h"
#include "auto_level.h"

#define TEMP_MOTOR_OFF_TIME 1500 // Time in ms

/*******************************************************************************
 *******************************   Extern Variables   ***************************
 ******************************************************************************/

// Motor statuses

individual_motor_state_t motor_state[NUM_MOTORS] = {false};

// Power board output assignments
const uint8_t OUTPUT0 = 0;
const uint8_t OUTPUT1 = 1;
const uint8_t OUTPUT2 = 2;
const uint8_t OUTPUT3 = 3;

// Motor assignment variables
uint8_t MOTORA = OUTPUT0;
uint8_t MOTORB = OUTPUT1;
uint8_t MOTORC = OUTPUT2;
uint8_t MOTORD = OUTPUT3;

// Auto_level motors on or off status
bool auto_level_motors_on = false;

// Temp motor pause
bool set_motors_off_once = false;
bool motors_temp_off = false;

/*******************************************************************************
 ***************************   PRIVATE VARIABLES   *****************************
 ******************************************************************************/

// Limit switch statuses (limit_switch_t declared in dual_motor_control.h)

limit_switch_t at_limit_switch[NUM_MOTORS] = {false};

/*******************************************************************************
 *********************   TIMER FUNCTION DECLARATIONS   ***********************
 ******************************************************************************/



static sl_sleeptimer_timer_handle_t temp_motor_off_timer; // Timer for the temporary all-motor pause when one actuator hits a limit switch
bool temp_motor_off_timer_timeout = false;
static bool is_temp_motor_off_timer_running = false;


/*******************************************************************************/
// Called when the start_steady_accel_read_oneshot_timer() goes off
static void temp_motor_off_callback(sl_sleeptimer_timer_handle_t *temp_motor_off_handle, void *temp_motor_off_data)
{
  (void)temp_motor_off_handle;
  (void)temp_motor_off_data;
  temp_motor_off_timer_timeout = true;
}

// Starts a one-shot timer for ensuring accurate level reading
static void start_temp_motor_off_oneshot_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(TEMP_MOTOR_OFF_TIME);
  sl_sleeptimer_start_timer(&temp_motor_off_timer, ticks, temp_motor_off_callback, NULL, 0, 0);
}

/*******************************************************************************
 *********************   PRIVATE FUNCTION DECLARATIONS   ***********************
 ******************************************************************************/

/*
  * 
  *                          HEAD (0)
  *         MOTOR A     ________________    MOTOR D
  *                   |  [====]  [====]  |  
  *                   | - - - - - - - -  |
  *                   |                  |
  *                   |                  |
  *         LEFT (1)  |                  |  RIGHT (2)
  *                   |                  |
  *                   |                  |
  *                   | ________________ | 
  *          MOTOR B                        MOTOR C
  *                         FOOT (3)        
  * 
  * 
  * 
  *              ^                                                                           ^
  *             | |                                                                         | | 
  *    MOTOR A  | |_                                                                       _| |  MOTOR D
  *             |   |                                                                     |   |
  *             |___|__                                                                 __|___|
  *                    |                     _____________________                     |
  *                    |________ [OUTPUT 0] |                     | [OUTPUT 3] ________|
  *                                         |  TOTEM POWER BOARD  |
  *                     ________ [OUTPUT 1] |_____________________| [OUTPUT 2] ________
  *              ^     |                                                               |     ^
  *             | |    |                                                               |    | |
  *             | |_   |                                                               |   _| |
  *    MOTOR B  |   |  |                                                               |  |   |  MOTOR C
  *             |___|__|                                                               |__|___|
  * 
  * 
  * 
  */


void limit_switch_motor_off(uint8_t motor){
  if (motor == MOTORA){
    set_dual_motors_off(HEAD);
    set_dual_motors_off(LEFT);
  }
  else if (motor == MOTORB){
    set_dual_motors_off(LEFT);
    set_dual_motors_off(FOOT);
  }
  else if (motor == MOTORC){
    set_dual_motors_off(RIGHT);
    set_dual_motors_off(FOOT);
  }
  else if (motor == MOTORD){
    set_dual_motors_off(HEAD);
    set_dual_motors_off(RIGHT);
  }
}

void all_motor_limit_switch_temp_off(){
  sl_sleeptimer_is_timer_running(&temp_motor_off_timer, &is_temp_motor_off_timer_running);

  // Only start 2 second timer to read and store accel values once per button press
  if ((!is_temp_motor_off_timer_running) && (!temp_motor_off_timer_timeout)){
      start_temp_motor_off_oneshot_timer();
//      printf("Temp motor pause timer start\r\n");

      // Set all motors off
      set_all_motors_off();
      for (uint8_t i=0; i < NUM_MOTORS; i++){
          motor_state[i].motor_on = false;
      }
  }
  // Turn all motors back on after timeout and DO NOT RESET temp_motor_off_timer_timeout here, that must be reset on button release to only have pause occur once
  if ((temp_motor_off_timer_timeout) && (!set_motors_off_once)){
      set_motors_off_once = true;
      set_all_motors_on();
      for (uint8_t i=0; i < NUM_MOTORS; i++){
          motor_state[i].motor_on = true;
      }
      motors_temp_off = false;
      if (dynamic_collision_detection_enabled){
          reset_ignore_data_points_count();
          reset_motor_current_data();
      }
  }

}



void auto_level_limit_switch_motor_off(uint8_t motor, int8_t auto_level_current_axis){
  if (auto_level_current_axis == ROLL){
      if (motor == MOTORA){
        set_dual_motors_off(LEFT);
      }
      if (motor == MOTORB){
        set_dual_motors_off(LEFT);
      }
      if (motor == MOTORC){
        set_dual_motors_off(RIGHT);
      }
      if (motor == MOTORD){
        set_dual_motors_off(RIGHT);
      }
  } else if (auto_level_current_axis == PITCH){
      if (motor == MOTORA){
        set_dual_motors_off(HEAD);
      }
      else if (motor == MOTORB){
        set_dual_motors_off(FOOT);
      }
      else if (motor == MOTORC){
        set_dual_motors_off(FOOT);
      }
      else if (motor == MOTORD){
        set_dual_motors_off(HEAD);
      }
  }

}

void limit_switch_statuses(uint8_t motor, active_switch_t switch_value, int8_t auto_level_current_axis){

  // Determine if motor is moving or not
    read_motor_movement(motor);

    // If motor is supposed to be on and moving up...
    if ((motor_state[motor].motor_on) & (motor_state[motor].motor_up)){
    
        // ...and motor is not moving, it is at the upper limit switch
        if (!motor_is_moving_stable[motor]){
            // If limit switch status is false, set to true and turn off associated motors
            if (at_limit_switch[motor].upper == false){
                at_limit_switch[motor].upper = true;
//                printf("Motor %d is at upper limit switch\r\n", motor);
                // Turn off motors if button pressed is NOT all up or all down or auto-level
                if (!((switch_value == SWITCH_UP_ALL_VALUE) || (switch_value == SWITCH_DOWN_ALL_VALUE)
                    || (switch_value == SWITCH_AUTO_LEVEL_VALUE))){
                    limit_switch_motor_off(motor);
                } else if (switch_value == SWITCH_AUTO_LEVEL_VALUE){
                    auto_level_limit_switch_motor_off(motor, auto_level_current_axis);
                } else if ((switch_value == SWITCH_UP_ALL_VALUE) || (switch_value == SWITCH_DOWN_ALL_VALUE)){
                    motors_temp_off = true;
                }
            }
          // ...else motor is moving, and is not at the upper limit switch
        } else if (motor_is_moving_stable[motor]) {
            // If limit switch status is true, set to false and allow motors to continue running
            if (at_limit_switch[motor].lower == true){
                at_limit_switch[motor].lower = false;
//                printf("Motor %d is NOT at lower limit switch\r\n", motor);
            }
            if (at_limit_switch[motor].upper == true){
                at_limit_switch[motor].upper = false;
//                printf("Motor %d is NOT at upper limit switch\r\n", motor);
            }
        }
    }
    // If motor is supposed to be on and moving down...
    else if ((motor_state[motor].motor_on) & (!motor_state[motor].motor_up)){
    
        // ...and motor is not moving, it is at the lower limit switch
        if (!motor_is_moving_stable[motor]){
            // If limit switch status is false, set to true and turn off associated motors
            if (at_limit_switch[motor].lower == false){
                at_limit_switch[motor].lower = true;
//                printf("Motor %d is at lower limit switch\r\n", motor);
                // Turn off motors if button pressed is NOT all up or all down or auto-level
                if (!((switch_value == SWITCH_UP_ALL_VALUE) || (switch_value == SWITCH_DOWN_ALL_VALUE)
                    || (switch_value == SWITCH_AUTO_LEVEL_VALUE))){
                    limit_switch_motor_off(motor);
                } else if (switch_value == SWITCH_AUTO_LEVEL_VALUE){
                    auto_level_limit_switch_motor_off(motor, auto_level_current_axis);
                } else if ((switch_value == SWITCH_UP_ALL_VALUE) || (switch_value == SWITCH_DOWN_ALL_VALUE)){
                    motors_temp_off = true;
                }
            }
          // ...else motor is moving, and is not at the lower limit switch
        } else if (motor_is_moving_stable[motor]){
            // If limit switch status is true, set to false and allow motors to continue running
            if (at_limit_switch[motor].lower == true){
                at_limit_switch[motor].lower = false;
//                printf("Motor %d is NOT at lower limit switch\r\n", motor);
            }
            if (at_limit_switch[motor].upper == true){
                at_limit_switch[motor].upper = false;
//                printf("Motor %d is NOT at upper limit switch\r\n", motor);
            }
        }
    }
}

// Set motor direction to opposite of input switch direction
void reverse_motor_direction_and_start(active_switch_t switch_value){ ///NEED TO UPDATE BECAUSE AUTO LEVEL WONT WORK HERE
  switch(switch_value){
    case SWITCH_UP_ALL_VALUE:
      set_all_motors_direction_down();
      set_all_motors_on();
      break;
    case SWITCH_DOWN_ALL_VALUE:
      set_all_motors_direction_up();
      set_all_motors_on();
      break;
    case SWITCH_DOWN_HEAD_VALUE:
      set_dual_motors_direction_up(HEAD);
      set_dual_motors_on(HEAD);
      break;
    case SWITCH_DOWN_LEFT_VALUE:
      set_dual_motors_direction_up(LEFT);
      set_dual_motors_on(LEFT);
      break;
    case SWITCH_DOWN_RIGHT_VALUE:
      set_dual_motors_direction_up(RIGHT);
      set_dual_motors_on(RIGHT);
      break;
    case SWITCH_DOWN_FOOT_VALUE:
      set_dual_motors_direction_up(FOOT);
      set_dual_motors_on(FOOT);
      break;
    case SWITCH_UP_HEAD_VALUE:
      set_dual_motors_direction_down(HEAD);
      set_dual_motors_on(HEAD);
      break;
    case SWITCH_UP_LEFT_VALUE:
      set_dual_motors_direction_down(LEFT);
      set_dual_motors_on(LEFT);
      break;
    case SWITCH_UP_RIGHT_VALUE:
      set_dual_motors_direction_down(RIGHT);
      set_dual_motors_on(RIGHT);
      break;
    case SWITCH_UP_FOOT_VALUE:
      set_dual_motors_direction_down(FOOT);
      set_dual_motors_on(FOOT);
      break;
    default:
      break;
  }
}

void set_dual_motors_on(side_of_bed_t side)

{
  switch (side) {
    case HEAD:
      // If trying to raise HEAD and BOTH Motor A and Motor D (HEAD) are NOT at UPPER limit switch, move UP
      if ((motor_state[MOTORA].motor_up && motor_state[MOTORD].motor_up) && !(at_limit_switch[MOTORA].upper || at_limit_switch[MOTORD].upper)){
          set_motor_on(MOTORA);
          motor_state[MOTORA].motor_on = true;

          set_motor_on(MOTORD);
          motor_state[MOTORD].motor_on = true;
      }
      // If trying to lower HEAD and BOTH Motor A and Motor D (HEAD) are NOT at LOWER limit switch, move DOWN
      else if ((!motor_state[MOTORA].motor_up && !motor_state[MOTORD].motor_up) && !(at_limit_switch[MOTORA].lower || at_limit_switch[MOTORD].lower)){
          set_motor_on(MOTORA);
          motor_state[MOTORA].motor_on = true;

          set_motor_on(MOTORD);
          motor_state[MOTORD].motor_on = true;
      }

      break;
    case LEFT:
      // If trying to raise LEFT and BOTH Motor A and Motor B (LEFT) are NOT at UPPER limit switch, move UP
      if ((motor_state[MOTORA].motor_up && motor_state[MOTORB].motor_up) && !(at_limit_switch[MOTORA].upper || at_limit_switch[MOTORB].upper)){
          set_motor_on(MOTORA);
          motor_state[MOTORA].motor_on = true;

          set_motor_on(MOTORB);
          motor_state[MOTORB].motor_on = true;
      }
      // If trying to lower LEFT and BOTH Motor A and Motor B (LEFT) are NOT at LOWER limit switch, move DOWN
      else if ((!motor_state[MOTORA].motor_up && !motor_state[MOTORB].motor_up) && !(at_limit_switch[MOTORA].lower || at_limit_switch[MOTORB].lower)){
          set_motor_on(MOTORA);
          motor_state[MOTORA].motor_on = true;

          set_motor_on(MOTORB);
          motor_state[MOTORB].motor_on = true;
      }

      break;
    case RIGHT:
      // If trying to raise RIGHT and BOTH Motor C and Motor D (RIGHT) are NOT at UPPER limit switch, move UP
      if ((motor_state[MOTORC].motor_up && motor_state[MOTORD].motor_up) && !(at_limit_switch[MOTORC].upper || at_limit_switch[MOTORD].upper)){
          set_motor_on(MOTORC);
          motor_state[MOTORC].motor_on = true;

          set_motor_on(MOTORD);
          motor_state[MOTORD].motor_on = true;
      }
      // If trying to lower RIGHT and BOTH Motor C and Motor D (RIGHT) are NOT at LOWER limit switch, move DOWN
      else if ((!motor_state[MOTORC].motor_up && !motor_state[MOTORD].motor_up) && !(at_limit_switch[MOTORC].lower || at_limit_switch[MOTORD].lower)){
          set_motor_on(MOTORC);
          motor_state[MOTORC].motor_on = true;

          set_motor_on(MOTORD);
          motor_state[MOTORD].motor_on = true;
      }

      break;
    case FOOT:
      // If trying to raise FOOT and BOTH Motor B and Motor C (FOOT) are NOT at UPPER limit switch, move UP
      if ((motor_state[MOTORB].motor_up && motor_state[MOTORC].motor_up) && !(at_limit_switch[MOTORB].upper || at_limit_switch[MOTORC].upper)){
          set_motor_on(MOTORB);
          motor_state[MOTORB].motor_on = true;

          set_motor_on(MOTORC);
          motor_state[MOTORC].motor_on = true;
      }
      // If trying to lower FOOT and BOTH Motor B and Motor C (FOOT) are NOT at LOWER limit switch, move DOWN
      else if ((!motor_state[MOTORB].motor_up && !motor_state[MOTORC].motor_up) && !(at_limit_switch[MOTORB].lower || at_limit_switch[MOTORC].lower)){
          set_motor_on(MOTORB);
          motor_state[MOTORB].motor_on = true;

          set_motor_on(MOTORC);
          motor_state[MOTORC].motor_on = true;
      }

      break;
  }
}

void set_dual_motors_off (side_of_bed_t side)
{
  switch (side) {
    case HEAD:
      set_motor_off(MOTORA);
      motor_state[MOTORA].motor_on = false;

      set_motor_off(MOTORD);
      motor_state[MOTORD].motor_on = false;
      break;
    case LEFT:
      set_motor_off(MOTORA);
      motor_state[MOTORA].motor_on = false;

      set_motor_off(MOTORB);
      motor_state[MOTORB].motor_on = false;
      break;
    case RIGHT:
      set_motor_off(MOTORC);
      motor_state[MOTORC].motor_on = false;

      set_motor_off(MOTORD);
      motor_state[MOTORD].motor_on = false;
      break;
    case FOOT:
      set_motor_off(MOTORB);
      motor_state[MOTORB].motor_on = false;

      set_motor_off(MOTORC);
      motor_state[MOTORC].motor_on = false;
      break;
  }
}

void set_dual_motors_direction_up (side_of_bed_t side)
{
  switch (side) {
    case HEAD:
      set_motor_direction_up(MOTORA);
      motor_state[MOTORA].motor_up = true;

      set_motor_direction_up(MOTORD);
      motor_state[MOTORD].motor_up = true;
      break;
    case LEFT:
      set_motor_direction_up(MOTORA);
      motor_state[MOTORA].motor_up = true;

      set_motor_direction_up(MOTORB);
      motor_state[MOTORB].motor_up = true;
      break;
    case RIGHT:
      set_motor_direction_up(MOTORC);
      motor_state[MOTORC].motor_up = true;

      set_motor_direction_up(MOTORD);
      motor_state[MOTORD].motor_up = true;
      break;
    case FOOT:
      set_motor_direction_up(MOTORB);
      motor_state[MOTORB].motor_up = true;

      set_motor_direction_up(MOTORC);
      motor_state[MOTORC].motor_up = true;
      break;
  }
}

void set_dual_motors_direction_down (side_of_bed_t side)
{
  switch (side) {
    case HEAD:
      set_motor_direction_down(MOTORA);
      motor_state[MOTORA].motor_up = false;

      set_motor_direction_down(MOTORD);
      motor_state[MOTORD].motor_up = false;
      break;
    case LEFT:
      set_motor_direction_down(MOTORA);
      motor_state[MOTORA].motor_up = false;

      set_motor_direction_down(MOTORB);
      motor_state[MOTORB].motor_up = false;
      break;
    case RIGHT:
      set_motor_direction_down(MOTORC);
      motor_state[MOTORC].motor_up = false;

      set_motor_direction_down(MOTORD);
      motor_state[MOTORD].motor_up = false;
      break;
    case FOOT:
      set_motor_direction_down(MOTORB);
      motor_state[MOTORB].motor_up = false;
      
      set_motor_direction_down(MOTORC);
      motor_state[MOTORC].motor_up = false;
      break;
  }
}
