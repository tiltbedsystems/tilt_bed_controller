/*
 * auto_level.c
 *
 *  Created on: May 7, 2023
 *      Author: Andrew Duncan
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "sl_atomic.h"
#include "sl_simple_button_instances.h"
#include "sl_sleeptimer.h"

#include "pin_config.h"
#include "em_gpio.h"
#include "bed_types.h"
#include "exp_board.h"
#include "mc3479.h"
#include "dual_motor_control.h"
#include "motor_current_functions.h"
#include "auto_level.h"
#include "debug_capture.h"

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/
#define PI 3.14159265
#define RAD_TO_DEG (180.0 / PI)
#define PITCH_OFFSET 90

#define NUM_ACCEL_READINGS 130 // Number of accelerometer samples to average
#define TOLERANCE 0.01 // Tolerance auto-level code reaches before determining bed is level
#define CLOSE_TOLERANCE 0.1 // Auto-level won't begin leveling if within this tolerance
#define PULSE_START_TOLERANCE 0.3 // Auto-level will begin pulsing at this tolerance to not overshoot

#define STEADY_ACCEL_READ_TIMER_TIME 1500 // Time in ms
#define SNEAK_MOTOR_OFF_TIME 350  // Time in ms
#define SNEAK_MOTOR_ON_TIME 75 // Time in ms

/*******************************************************************************
 ******************************   STRUCTURES   *********************************
 ******************************************************************************/


/*******************************************************************************
 ***************************   PRIVATE VARIABLES   *****************************
 ******************************************************************************/

int16_t accel_vector[3]; // XYZ g vector in raw ADC counts
int16_t x_accel_vector; // X g vector in raw ADC counts
int16_t y_accel_vector; // Y g vector in raw ADC counts
int16_t z_accel_vector; // Z g vector in raw ADC counts

int32_t accel_vector_mag = 0; // Magnitude of acceleration vector
int16_t tolerance = TOLERANCE * SCALE_FACTOR;  // Tolerance in degrees * SCALE FACTOR for determining levelness
int16_t close_tolerance = CLOSE_TOLERANCE * SCALE_FACTOR; // Tolerance of beginning sneak leveling to adjust slowly
int16_t pulse_start_tolerance = PULSE_START_TOLERANCE * SCALE_FACTOR;

float latest_roll_angle = 0.0; // X-Z angle, deviation from the baseline_roll_angle
float latest_pitch_angle = 0.0; // Y-Z angle, deviation from the baseline_pitch_angle
int16_t latest_roll_angle_scaled = 0; // Scaled value
int16_t latest_pitch_angle_scaled = 0; // Scaled value
int16_t latest_roll_angle_filtered = 0; // LPF scaled value
int16_t latest_pitch_angle_filtered = 0; // LPF scaled value
static float accel_alpha = 0.1; // LPF adjustment value

static int16_t accel_readings_roll[NUM_ACCEL_READINGS] = {0}; // Array to hold the last NUM_ACCEL_READINGS number of accel roll readings
static int16_t accel_readings_pitch[NUM_ACCEL_READINGS] = {0}; // Array to hold the last NUM_ACCEL_READINGS number of accel pitch readings
static int32_t accel_running_sum_roll = 0; // Running sum of accel values for roll to be used to average
static int32_t accel_running_sum_pitch = 0; // Running sum of accel values for pitch to be used to average
static int16_t accel_average_roll_angle = 0; // Average value of roll SCALED VALUE
static int16_t accel_average_pitch_angle = 0; // Average value of pitch SCALED VALUE
int16_t baseline_accel_average_roll_angle = 0; // Average value of roll, deviation from user's level baseline - read by bed_status.c for telemetry
int16_t baseline_accel_average_pitch_angle = 0; // Average value of pitch, deviation from user's level baseline - read by bed_status.c for telemetry
static int16_t abs_baseline_roll_angle = 0;
static int16_t abs_baseline_pitch_angle = 0;



static uint8_t index = 0; // Index


side_level_status_t level_status[NUM_AXES] = {false};


static sl_sleeptimer_timer_handle_t steady_accel_read_oneshot_timer; // Timer to indicate accel has had enough time to get accurate reading
static bool steady_accel_read_timer_timeout = false;
static bool is_steady_accel_read_timer_running = false;

static sl_sleeptimer_timer_handle_t sneak_oneshot_timer; // Timer to indicate how long to turn motors on for when "sneaking" up on level
static bool sneak_motors_on = false;
static bool is_sneak_timer_running = false;

static sl_sleeptimer_timer_handle_t stop_sneak_oneshot_timer; // Timer to indicate how long to turn motors on for when "sneaking" up on level
static bool is_stop_sneak_timer_running = false;

static sl_sleeptimer_timer_handle_t pulse_latch_oneshot_timer; // Timer to indicate how long to turn motors on for when "sneaking" up on level
static bool pulse_latch = false;

/*******************************************************************************
 ***************************   EXTERN VARIABLES   ******************************
 ******************************************************************************/

int16_t baseline_roll_angle; // User inputted baseline xz angle (roll) in degrees for surface levelness SCALED VALUE
int16_t baseline_pitch_angle; // User inputted baseline yz angle (pitch) in degrees for surface levelness SCALED VALUE
int8_t auto_level_current_axis = ROLL;
bool first_check = false;
bool leveling_success[NUM_AXES] = {false};
bool bed_level = false;
bool bed_leveling_disabled = false;
bool axis_disabled[NUM_AXES] = {false};


/*******************************************************************************
 *********************   TIMER FUNCTION DECLARATIONS   ***********************
 ******************************************************************************/


/*******************************************************************************/
// Called when the start_steady_accel_read_oneshot_timer() goes off
static void steady_accel_read_oneshot_callback(sl_sleeptimer_timer_handle_t *steady_accel_read_handle, void *steady_accel_read_data)
{
  (void)steady_accel_read_handle;
  (void)steady_accel_read_data;
  steady_accel_read_timer_timeout = true;
}

// Starts a one-shot timer for ensuring accurate level reading
static void start_steady_accel_read_oneshot_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(STEADY_ACCEL_READ_TIMER_TIME);
  sl_sleeptimer_start_timer(&steady_accel_read_oneshot_timer, ticks, steady_accel_read_oneshot_callback, NULL, 0, 0);
}
/*******************************************************************************/

/*******************************************************************************/
// Called when the start_sneak_oneshot_timer() goes off
static void sneak_oneshot_callback(sl_sleeptimer_timer_handle_t *sneak_oneshot_read_handle, void *sneak_oneshot_read_data)
{
  (void)sneak_oneshot_read_handle;
  (void)sneak_oneshot_read_data;

  sneak_motors_on = true;

}

// Starts a one-shot timer for motor on time when sneaking up on level
static void start_sneak_oneshot_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(SNEAK_MOTOR_OFF_TIME);
  sl_sleeptimer_start_timer(&sneak_oneshot_timer, ticks, sneak_oneshot_callback, NULL, 0, 0);
}
/*******************************************************************************/

/*******************************************************************************/
// Called when the start_stop_sneak_oneshot_timer() goes off
static void stop_sneak_oneshot_callback(sl_sleeptimer_timer_handle_t *stop_sneak_oneshot_read_handle, void *stop_sneak_oneshot_read_data)
{
  (void)stop_sneak_oneshot_read_handle;
  (void)stop_sneak_oneshot_read_data;

  sneak_motors_on = false;

}

// Starts a one-shot timer for motor on time when sneaking up on level
static void start_stop_sneak_oneshot_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(SNEAK_MOTOR_ON_TIME);
  sl_sleeptimer_start_timer(&stop_sneak_oneshot_timer, ticks, stop_sneak_oneshot_callback, NULL, 0, 0);
}
/*******************************************************************************/

/*******************************************************************************
 *********************   PRIVATE FUNCTION DECLARATIONS   ***********************
 ******************************************************************************/


int32_t accel_low_pass_filter(int32_t new_value, int32_t old_value, float accel_alpha) {
    return accel_alpha * new_value + (1 - accel_alpha) * old_value;
}

// Read data and convert to usable values
void read_accel(){

  // Read raw data from accel
  accel_get_xyz_raw(&accel_vector[0]);
  x_accel_vector = accel_vector[0];
  y_accel_vector = accel_vector[1];
  z_accel_vector = accel_vector[2];

  // Calculate vector magnitude
  accel_vector_mag = sqrt(x_accel_vector * x_accel_vector + y_accel_vector * y_accel_vector + z_accel_vector * z_accel_vector);

  // Calculate roll and pitch angles
  latest_roll_angle = (float)(atan2(x_accel_vector, accel_vector_mag)) * (RAD_TO_DEG);
  latest_pitch_angle = (float)(atan2(y_accel_vector,z_accel_vector)) * (RAD_TO_DEG) - PITCH_OFFSET;

  // Convert to scaled integers
  latest_roll_angle_scaled = (int16_t)(latest_roll_angle * SCALE_FACTOR);
  latest_pitch_angle_scaled = (int16_t)(latest_pitch_angle * SCALE_FACTOR);

  // Apply low-pass filter to the raw accel reading
  latest_roll_angle_filtered = accel_low_pass_filter(latest_roll_angle_scaled, latest_roll_angle_filtered, accel_alpha);
  latest_pitch_angle_filtered = accel_low_pass_filter(latest_pitch_angle_scaled, latest_pitch_angle_filtered, accel_alpha);
}

void accel_running_average(){

//   VALUES ARE SCALED

      // Read latest accel data
      read_accel();

      // Subtract value in index position from sum and add latest value
      accel_running_sum_roll = accel_running_sum_roll - accel_readings_roll[index] + latest_roll_angle_filtered;
      accel_running_sum_pitch = accel_running_sum_pitch - accel_readings_pitch[index] + latest_pitch_angle_filtered;

      // Update readings arrays
      accel_readings_roll[index] = latest_roll_angle_filtered;
      accel_readings_pitch[index] = latest_pitch_angle_filtered;

      // Calculate the running average
      accel_average_roll_angle = accel_running_sum_roll / NUM_ACCEL_READINGS;
      accel_average_pitch_angle = accel_running_sum_pitch / NUM_ACCEL_READINGS;

      // Move to next current_index in a circular manner
      index = (index + 1) % NUM_ACCEL_READINGS;

      // Adjust running average angles against user set baseline
      baseline_accel_average_roll_angle = accel_average_roll_angle - (baseline_roll_angle);
      baseline_accel_average_pitch_angle = accel_average_pitch_angle - (baseline_pitch_angle);
}

int16_t returnAccelValue(axis_t axis) {
  switch (axis) {
    case ROLL:
      return accel_average_roll_angle;
    case PITCH:
      return accel_average_pitch_angle;
    default:
      // Handle unexpected input
      return 0; // Or some error value
  }
}

void print_accel_value(axis_t axis){
  if (axis == ROLL){
    printf("ROLL Angle is: %d\r\n", accel_average_roll_angle);
  }
  else if (axis == PITCH){
    printf("PITCH Angle is: %d\r\n", accel_average_pitch_angle);
  }
//
//  if (axis == ROLL){
//    printf("%d,", accel_average_roll_angle);
//  }
//  else if (axis == PITCH){
//    printf("%d,", accel_average_pitch_angle);
//  }

//  if (axis == ROLL){
//    printf("%d,", latest_roll_angle_scaled);
//  }
//  else if (axis == PITCH){
//    printf("%d,", latest_pitch_angle_scaled);
//  }

//  if (axis == ROLL){
//    printf("%d,", latest_roll_angle_filtered);
//  }
//  else if (axis == PITCH){
//    printf("%d,", latest_pitch_angle_filtered);
//  }
}
void check_level(axis_t axis){

  if (axis == ROLL){
      abs_baseline_roll_angle = abs(baseline_accel_average_roll_angle);

      /// Pulse Tolerance
      if (abs_baseline_roll_angle <= pulse_start_tolerance){
          level_status[ROLL].pulse = true;
      } else {
          level_status[ROLL].pulse = false;
      }
      /// Level & Close to level checks
      if (abs_baseline_roll_angle <= tolerance){
          level_status[ROLL].level = true;
          level_status[ROLL].close_to_level = false;
    //      printf("ROLL is level\r\n");
    //      print_accel_value(ROLL);
      } else if (abs_baseline_roll_angle <= close_tolerance){
          level_status[ROLL].level = false;
          level_status[ROLL].close_to_level = true;
    //      printf("ROLL is CLOSE to level\r\n");
    //      print_accel_value(ROLL);
      } else {
          level_status[ROLL].level = false;
          level_status[ROLL].close_to_level = false;
      }
      /// Positive or negative tilt check
      if (baseline_accel_average_roll_angle > tolerance){
          level_status[ROLL].positive = true;
      } else if (baseline_accel_average_roll_angle < -tolerance){
          level_status[ROLL].positive = false;
      }
  }
  if (axis == PITCH){
      abs_baseline_pitch_angle = abs(baseline_accel_average_pitch_angle);

      /// Pulse Tolerance
      if (abs_baseline_pitch_angle <= pulse_start_tolerance){
          level_status[PITCH].pulse = true;
      } else {
          level_status[PITCH].pulse = false;
      }
      /// Level & Close to level checks
      if (abs_baseline_pitch_angle <= tolerance){
          level_status[PITCH].level = true;
          level_status[PITCH].close_to_level = false;
    //      printf("PITCH is level\r\n");
    //      print_accel_value(PITCH);
      } else if (abs_baseline_pitch_angle <= close_tolerance){
          level_status[PITCH].level = false;
          level_status[PITCH].close_to_level = true;
    //      printf("PITCH is CLOSE to level\r\n");
    //      print_accel_value(PITCH);
      } else {
          level_status[PITCH].level = false;
          level_status[PITCH].close_to_level = false;
      }
      /// Level & Close to level checks
      if (baseline_accel_average_pitch_angle > tolerance){
          level_status[PITCH].positive = true;
      } else if (baseline_accel_average_pitch_angle < -tolerance){
          level_status[PITCH].positive = false;
      }
  }
}

void reset_level_data(){
  bed_level = false;
  bed_leveling_disabled = false;
  sneak_motors_on = false;
  sl_sleeptimer_stop_timer(&sneak_oneshot_timer);

  steady_accel_read_timer_timeout = false;
  sl_sleeptimer_stop_timer(&steady_accel_read_oneshot_timer);

  pulse_latch = false;
  sl_sleeptimer_stop_timer(&pulse_latch_oneshot_timer);

  for (uint8_t i=0; i < NUM_AXES; i++){
      level_status[i].close_to_level = false;
      level_status[i].level = false;
      level_status[i].positive = false;
      axis_disabled[i] = false;
  }
}

/*
  *         NOTE: AUTO LEVEL FUNCTION USES HARDWIRED MOTORS 0 -> 3, NOT A -> D
  *      This is because the motors will always be connected in the same order with relation
  *      to how the POWER BOARD is installed from wiring restraints. Regardless of how the user wants to 
  *      set their switch arrangment, the auto_level sequence will use default configuration.
  *     
  * 
  * 
  *                                                     If HEAD is high,
  *                                                     PITCH is NEGATIVE
  * 
  *                                                     AUTO_LEVEL_HEAD
  *                                     MOTOR 0    ____________________________   MOTOR 3
  *                                              |  [====]             [====]  |  
  *                                              | - - - - - - - - - - - - - - |
  *                                              |                             |
  *    If LEFT is high,                          |   ^                     ^   |                         If RIGHT is high,
  *    ROLL is POSITIVE      AUTO_LEVEL_LEFT     |   |                     |   |  AUTO_LEVEL_RIGHT       ROLL is NEGATIVE
  *                                              |   |  POWER BOARD FACES  |   |
  *                                              |          THIS WAY           |
  *                                              | ___________________________ | 
  *                                      MOTOR 1                                   MOTOR 2
  *                                                       AUTO_LEVEL_FOOT      
  * 
  *                                                       If FOOT is high,
  *                                                       PITCH is POSITIVE
  * 
  */

void set_auto_level_motor_direction(axis_t axis){

  // If checking roll and roll is not level, set motors as necessary
  if (axis == ROLL){
      if (level_status[ROLL].positive == true){
        set_dual_motors_direction_up(RIGHT);
        set_dual_motors_direction_down(LEFT);
      }
      if (level_status[ROLL].positive == false){
        set_dual_motors_direction_up(LEFT);
        set_dual_motors_direction_down(RIGHT);
      }
    return;
  }
  // If checking pitch and pitch is not level set motors as necessary
  if (axis == PITCH){
      if (level_status[PITCH].positive == true){
        set_dual_motors_direction_up(HEAD);
        set_dual_motors_direction_down(FOOT);
      }
      if (level_status[PITCH].positive == false){
        set_dual_motors_direction_up(FOOT);
        set_dual_motors_direction_down(HEAD);
      }
    return;
  }
}

void level_axis_pulse (axis_t axis){
  if (level_status[axis].level){
          set_all_motors_off();
          auto_level_motors_on = false;
          for (uint8_t i=0; i < NUM_MOTORS; i++){
              motor_state[i].motor_on = false;
          };
          return;
    }
    else {
        // Depending on state of sneak motors, start on or off timer
        if (sneak_motors_on){
            sl_sleeptimer_is_timer_running(&stop_sneak_oneshot_timer, &is_stop_sneak_timer_running);
            if (!is_stop_sneak_timer_running){
                start_stop_sneak_oneshot_timer();
            }
        } else {
            sl_sleeptimer_is_timer_running(&sneak_oneshot_timer, &is_sneak_timer_running);
            if (!is_sneak_timer_running){
                start_sneak_oneshot_timer();
            }
        }

        // If sneak motors are supposed to be on, but aren't, then turn on
        if (sneak_motors_on && (!auto_level_motors_on)){

            sl_sleeptimer_stop_timer(&sneak_oneshot_timer);
#if !BED_CAPTURE_MOTOR_CURRENTS
            printf("Pulse on...\r\n");
#endif
            set_auto_level_motor_direction(axis);
            reset_motor_current_data();
            reset_ignore_data_points_count();
            if (axis == ROLL){
                set_dual_motors_on(LEFT);
                set_dual_motors_on(RIGHT);
            }
            else if (axis == PITCH){
                set_dual_motors_on(HEAD);
                set_dual_motors_on(FOOT);
            }
            // Set auto-level motors on flag
            auto_level_motors_on = true;
            scan_all_currents();

      }
      // If sneak motors are supposed to be off, but are on, then turn off
      else if ((!sneak_motors_on) && auto_level_motors_on){
            sl_sleeptimer_stop_timer(&stop_sneak_oneshot_timer);
#if !BED_CAPTURE_MOTOR_CURRENTS
            printf("Pulse off...\r\n");
#endif
            set_all_motors_off();
            auto_level_motors_on = false;
            for (uint8_t i=0; i < NUM_MOTORS; i++){
                motor_state[i].motor_on = false;
            };
        }
    }
}

void level_axis (axis_t axis){

  if (level_status[axis].level){
      set_all_motors_off();
      auto_level_motors_on = false;
      for (uint8_t i=0; i < NUM_MOTORS; i++){
          motor_state[i].motor_on = false;
      };
      return;
  }
  else {
      if (!auto_level_motors_on){
#if !BED_CAPTURE_MOTOR_CURRENTS
          printf("Leveling...\r\n");
#endif
          set_auto_level_motor_direction(axis);
          reset_motor_current_data();
          reset_ignore_data_points_count();
          if (axis == ROLL){
              set_dual_motors_on(LEFT);
              set_dual_motors_on(RIGHT);
          }
          else if (axis == PITCH){
              set_dual_motors_on(HEAD);
              set_dual_motors_on(FOOT);
          }
          // Set auto-level motors on flag
          auto_level_motors_on = true;
          scan_all_currents();
      }
      else if (auto_level_motors_on && !pulse_latch){
          run_motor_supervision(SWITCH_AUTO_LEVEL_VALUE, true);
          if (any_collision){
              set_all_motors_off();
              auto_level_motors_on = false;
              for (uint8_t i=0; i < NUM_MOTORS; i++){
                  motor_state[i].motor_on = false;
              };
              return;

          }
      }
  }
}

void steady_accel_read_both_axes(){
  //  accel_running_average();
    sl_sleeptimer_is_timer_running(&steady_accel_read_oneshot_timer, &is_steady_accel_read_timer_running);
    if (!is_steady_accel_read_timer_running){
        start_steady_accel_read_oneshot_timer();
    }
    if (steady_accel_read_timer_timeout){
        steady_accel_read_timer_timeout = false;
        first_check = false;
        for(int8_t i=0; i < NUM_AXES; i++){
            check_level(i);
        }
        for(int8_t i=0; i < NUM_AXES; i++){
            axis_disabled[i] = ((level_status[i].close_to_level) || (level_status[i].level));
        }
    }
    reset_motor_current_data();
}

void auto_level_single_axis(axis_t axis){
  check_level(axis);
  if (!level_status[axis].level){
      if (level_status[axis].pulse){
          // Latch the pulsing function for a certain time to prevent code from flipping back and forth between continuous and pulsing motion
//          sl_sleeptimer_is_timer_running(&pulse_latch_oneshot_timer, &is_pulse_latch_timer_running);
//          if (!is_pulse_latch_timer_running){
//              start_pulse_latch_oneshot_timer();
//          }
          if (!pulse_latch){
              pulse_latch = true;
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("Latch\r\n");
#endif
          }

          level_axis_pulse(axis);
      } else if (!pulse_latch){
          level_axis(axis);
      }

      /******************************
      ADD CODE SUPERVISING MOTOR MOVEMENT, IF ALL MOTORS ARE NOT MOVING - THIS AXIS DID NOT LEVEL PROPERLY
      ******************************/
  }
  if (level_status[axis].level){
#if !BED_CAPTURE_MOTOR_CURRENTS
      if (axis == ROLL){
          printf("ROLL Leveled successfully...\r\n");
      } else if (axis == PITCH){
          printf("PITCH Leveled successfully...\r\n");
      }
#endif
      leveling_success[axis] = true; // Leveled successfully, no error
      auto_level_motors_on = false;
      pulse_latch = false;
      // Turn off all motors and reset all motors status
      set_all_motors_off();
      for (int8_t i=0; i < NUM_MOTORS; i++){
        motor_state[i].motor_on = false;
      }
  }
}
