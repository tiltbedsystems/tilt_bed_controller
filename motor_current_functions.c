/*
  motor_current_functions.c
 
   Created on: Dec 31, 2023
       Author: Andrew Duncan
 */

#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#include "motor_current_functions.h"
#include "dual_motor_control.h"
#include "adc_currents.h"
#include "exp_board.h"
#include "sl_sleeptimer.h"
#include "auto_level.h"
#include "debug_capture.h"


/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/
#define MAX_LOAD_CURRENT 3000

#define NUM_CURRENT_READINGS 200
#define NUM_INITIAL_CURRENTS_IGNORED 100
#define NUM_CURRENTS_READ_DYNAMIC_THRESHOLD 350
#define NUM_CURRENTS_READ_CONTINUOUS_DYNAMIC_THRESHOLD 300 // Amount of currents read after dynamic collision detection is active to see if threshold needs to be reduced (if a weight is removed during movement)

#define ON_CURRENT_THRESHOLD 80
#define CONITNUOUS_DYNAMIC_THRESHOLD_CHANGE_MARGIN 500 // Amount of current in mA that the dynamic current threshold needs to lower to have a new threshold set

#if BED_CAPTURE_MOTOR_CURRENTS
// Real millisecond timestamp (DECISIONS.md #2), shared by the per-tick CSV
// row and the "#COLLISION" marker so both use the same clock. Rezeroed by
// capture_reset_timer() (called from app.c on button press/auto-level start,
// not release - see debug_capture.h) so each bench test gets its own t=0
// without a rebuild or power cycle. Unsigned subtraction is safe across a
// single tick-counter wraparound.
static uint32_t capture_start_tick = 0;

static uint32_t capture_now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count() - capture_start_tick);
}

void capture_reset_timer(void)
{
  capture_start_tick = sl_sleeptimer_get_tick_count();
}
#endif

/*******************************************************************************
 ***************************   PRIVATE VARIABLES   *****************************
 ******************************************************************************/

static uint32_t latest_motor_current[NUM_MOTORS]; // Array to hold motor currents read directly from the current sensors
static uint16_t readings[NUM_MOTORS][NUM_CURRENT_READINGS] = {0}; // 2D array to hold the past "NUM_CURRENT_READINGS" current values read for each motor
static uint32_t current_running_sum[NUM_MOTORS] = {0}; // The running sum of the last "NUM_CURRENT_READINGS" for each motor
static uint16_t average_current_single_reading[NUM_MOTORS] = {0}; // Array to hold single average value
static uint8_t current_index[NUM_MOTORS] = {0}; // Index used to cycle through arrays in a circular manner




static int32_t average_current_delta_sum = 0; // Difference between average current single read and the dynamic threshold (increase in current during movement)
static int32_t average_current_delta_single[NUM_MOTORS] = {0};
static uint8_t ignore_data_points_count[NUM_MOTORS] = {0}; // Ignoring initial motor spike current data


static bool is_motor_moving[NUM_MOTORS] = {false}; // Flag to indicate immediate motor movement (could contain an error reading)
static uint16_t motor_not_moving_counter[NUM_MOTORS] = {0}; // Index to only declare the motor is not moving after 3 instances in a row of no motor movement (if the button is tapped quickly, it can produce a single instance of no motor movement)
bool motor_is_moving_stable[NUM_MOTORS] = {true, true, true, true}; // Flag to supervise motor movement (needs to count two not immediately moving readings in a row to ensure accuracy)

bool collision[NUM_MOTORS] = {false}; // Boolean to indicate a collision has occurred on a specific motor
bool any_collision = false; // Boolean to indicate a collision has occurred on any motor

/************************ Simple LPF Variables ****************************/
uint32_t filtered_current[NUM_MOTORS] = {0}; // Store filtered values for each motor
static uint32_t check_motor_filtered_current[NUM_MOTORS] = {0}; // Store filtered values for each motor
static float alpha = 0.05; // Smoothing factor (adjust based on noise level)

bool dynamic_collision_detection_enabled = true; // Boolean to tell if dynamic collision detection is enabled
static bool dynamic_collision_detection_active = false; // Boolean to tell if dynamic collision detection is active or if still measuring threshold
uint16_t dynamic_reading_count[NUM_MOTORS] = {0}; // Counter to determine how many readings have been made since switch was pressed


uint16_t dynamic_max_average_calibration_current[NUM_MOTORS] = {0}; // Capture the highest motor current during initial movement
uint16_t max_current_threshold_set[NUM_MOTORS] = {0}; // Max current used to set threshold
uint16_t dynamic_collision_current_threshold[NUM_MOTORS] = {0}; // dynamic_max_average_calibration_current + COLLISION_CURRENT_SECURITY_MARGIN


/*******************************************************************************
 ***************************   EXTERN VARIABLES   ******************************
 ******************************************************************************/

// HIGH Sensitivity = xxx, MEDIUM Sensitivity = xxx, LOW Sensitivity = xxx
int16_t raise_collision_current_security_margin;
int16_t lower_collision_current_security_margin;
static int16_t security_margin; // Not an external variable, placed here for logical reading --> is set to either the raise or lower collision security margin defined above
bool motor_current_running_average_on = false;

uint8_t reading_count = 0;
// Used to scan motor currents twice on initial button press due read motor currents function not reading all motors on first use
// Also used for initial parts of current averages
bool initial_switch_press = false;


/*******************************************************************************
 *********************   PRIVATE FUNCTION DECLARATIONS   ***********************
 ******************************************************************************/
// Used to allow limit switch supervision functionality during auto-level


uint32_t low_pass_filter(uint32_t new_value, uint32_t old_value, float alpha) {
    return alpha * new_value + (1 - alpha) * old_value;
}

void scan_all_currents(){
  
  scan_motor_currents();

  /*
      Give IADC time to warm up and run/average scan sequence
    
      5 usec warmup + (2 usec/conversion x 4 conversions x averaging count)
    
      Use non-blocking delay to allow other interrupts to process
    */

  sl_sleeptimer_delay_millisecond(1);

  // Get present motor currents for all four motors
  get_motor_currents(&latest_motor_current[0]);

//  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
//      printf("Motor %d Raw ADC Current: %ld\r\n", i, latest_motor_current[i]);
//  }
}

void check_motor_movement(){
  for (uint8_t i = 0; i < NUM_MOTORS; i++){
      // Apply low-pass filter to the raw current reading
      check_motor_filtered_current[i] = low_pass_filter(latest_motor_current[i], check_motor_filtered_current[i], alpha);

      // Determine if motor is moving based on latest current reading
      is_motor_moving[i] = (check_motor_filtered_current[i] > ON_CURRENT_THRESHOLD);
  }
}

void motor_current_running_average(uint8_t motor){

  if (ignore_data_points_count[motor] < NUM_INITIAL_CURRENTS_IGNORED){
    ignore_data_points_count[motor]++;
    return;
  }

  /******** Simple LPF Approach ********/
  // Apply low-pass filter to the raw current reading
  filtered_current[motor] = low_pass_filter(latest_motor_current[motor], filtered_current[motor], alpha);


  // Update the running sum with the filtered value
  current_running_sum[motor] -= readings[motor][current_index[motor]];
  current_running_sum[motor] += (uint16_t)filtered_current[motor];
  readings[motor][current_index[motor]] = (uint16_t)filtered_current[motor];

  // Calculate the running average
  average_current_single_reading[motor] = current_running_sum[motor] / NUM_CURRENT_READINGS;

  // Move to next current_index in a circular manner
  current_index[motor] = (current_index[motor] + 1) % NUM_CURRENT_READINGS;

}

  void set_dynamic_collosion_threshold(uint8_t motor){
  // If dynamic collision is not yet active, read currents and determine adequate thresholds for each motor (occurs within 303ms)
    if (dynamic_collision_detection_enabled & !dynamic_collision_detection_active){
      // Determine if latest average is greater than current max - if so, set current max to the latest average
      if (dynamic_reading_count[motor] <= NUM_CURRENTS_READ_DYNAMIC_THRESHOLD){
        if (average_current_single_reading[motor] > dynamic_max_average_calibration_current[motor]){
          dynamic_max_average_calibration_current[motor] = average_current_single_reading[motor];
        }
        // Increment count
        dynamic_reading_count[motor]++;
      }
      else {
        // If enough readings have been taken, set threshold and set the dynamic collision to be active
        for (uint8_t i = 0; i < NUM_MOTORS; i++){
          dynamic_collision_current_threshold[i] = dynamic_max_average_calibration_current[i] + raise_collision_current_security_margin;
          max_current_threshold_set[i] = dynamic_max_average_calibration_current[i]; // Record value used to set threshold to use in collision detection delta values
        }
        dynamic_collision_detection_active = true;

        for (uint8_t i = 0; i < NUM_MOTORS; i++){
          printf("Max Current for Motor %d: %d\r\n", i, dynamic_max_average_calibration_current[i]);
          // Reset reading_count, max_average_calibration_current, and average_current_single_reading for re-checking threshold to see if current has lowered (meaning a weight was removed and therefore collision should become more sensitive)
          dynamic_reading_count[i] = 0;
          dynamic_max_average_calibration_current[i] = 0;
        }
      }
    }

    // If dynamic collision is active, continue to monitor currents and if a new, lower current max is read over a period of time, set new thresholds to that value
    if (dynamic_collision_detection_enabled & dynamic_collision_detection_active){

      // Determine if latest average is greater than current max - if so, set current max to the latest average
      if (dynamic_reading_count[3] <= NUM_CURRENTS_READ_CONTINUOUS_DYNAMIC_THRESHOLD){
        if (average_current_single_reading[motor] >= dynamic_max_average_calibration_current[motor]){
          dynamic_max_average_calibration_current[motor] = average_current_single_reading[motor];
        }
      // Increment count
      dynamic_reading_count[motor]++;
      }

      else {
      // If enough readings have been taken, set threshold based on current max IF the current max is lower than the existing threshold by CONITNUOUS_DYNAMIC_THRESHOLD_CHANGE_MARGIN or more
        for (uint8_t i = 0; i < NUM_MOTORS; i++){
          if ((dynamic_max_average_calibration_current[i] + CONITNUOUS_DYNAMIC_THRESHOLD_CHANGE_MARGIN) < max_current_threshold_set[i]){
            dynamic_collision_current_threshold[i] = dynamic_max_average_calibration_current[i] + raise_collision_current_security_margin;
//            printf("LOWERED Max Current for Motor %d: %d\r\n", i, dynamic_max_average_calibration_current[i]);
//            printf("LOWERED Threshold for Motor %d: %d\r\n", i, dynamic_collision_current_threshold[i]);
          }
          // Reset reading_count and max_average_calibration_current for re-checking threshold to see if current has lowered (meaning a weight was removed and therefore collision should become more sensitive)
          dynamic_reading_count[i] = 0;
          dynamic_max_average_calibration_current[i] = 0;
        }
      }
    }
 }

void reset_motor_current_data(){

  motor_current_running_average_on = false;
  dynamic_collision_detection_active = false;


  for (uint8_t i=0; i < NUM_MOTORS; i++){
    dynamic_max_average_calibration_current[i] = 0;
    dynamic_collision_current_threshold[i] = 0;
    dynamic_reading_count[i] = 0;
    average_current_single_reading[i] = 0;
    current_running_sum[i] = 0;
    filtered_current[i] = 0;
    max_current_threshold_set[i] = 0;

    for (uint8_t t=0; t < NUM_CURRENT_READINGS; t++){
        readings[i][t] = 0;
    }
  };

}

void reset_ignore_data_points_count(){

  for (uint8_t i=0; i < NUM_MOTORS; i++){
    ignore_data_points_count[i] = 0;
  };

}

void print_current_value(uint8_t motor){

//   printf("%d,", latest_motor_current[motor]);
//   printf("%ld,", filtered_current[motor]);
   printf("%d,", average_current_single_reading[motor]);


  
}

void read_motor_movement(uint8_t motor){

  if (is_motor_moving[motor]) {
    motor_not_moving_counter[motor] = 0;
    motor_is_moving_stable[motor] = true;

  } else if (!is_motor_moving[motor])
  {
    motor_not_moving_counter[motor]++;
    if (motor_not_moving_counter[motor] > 2){
      motor_is_moving_stable[motor] = false;

    // To prevent an overflow setting the motor_not_moving_counter to 0 and causing motor_is_moving_stable to erroneously be set to true
    } else if (motor_not_moving_counter[motor] >= 65500)
    {
      motor_not_moving_counter[motor] = 2;
      motor_is_moving_stable[motor] = false;
    }
  }
}

void max_load_check(uint8_t motor){
  if (motor_state[motor].motor_up){
      if (filtered_current[motor] > MAX_LOAD_CURRENT){
#if BED_CAPTURE_MOTOR_CURRENTS
          printf("#COLLISION,%lu\r\n", (unsigned long)capture_now_ms());
#else
          printf("\r\n");
          printf("TRIPPED MAX LOAD\r\n");
          printf("Motor %d Filtered Current:  %ld\r\n", motor, filtered_current[motor]);
#endif
          set_all_motors_off();
          any_collision = true;

          // Reset motor readings
          reset_ignore_data_points_count();
          reset_motor_current_data();
      }
  } else { // Motor is lowering, use lower max load current
      if (filtered_current[motor] > (MAX_LOAD_CURRENT - 500)){
#if BED_CAPTURE_MOTOR_CURRENTS
          printf("#COLLISION,%lu\r\n", (unsigned long)capture_now_ms());
#else
          printf("\r\n");
          printf("TRIPPED MAX LOAD\r\n");
          printf("Motor %d Filtered Current:  %ld\r\n", motor, filtered_current[motor]);
#endif
          set_all_motors_off();
          any_collision = true;

          // Reset motor readings
          reset_ignore_data_points_count();
          reset_motor_current_data();
      }
  }
}

/*** My updated collision check, simply uses sum of motor differentials - viable now with minimal current errors ***/

void collision_check(bool is_raising) {
    if (dynamic_collision_detection_active) {
        average_current_delta_sum = 0;
        if (!is_raising){ // If motor is lowering, use the lowering security margin
            security_margin = lower_collision_current_security_margin;
        } else { // If motor is raising, use the raising security margin
            security_margin = raise_collision_current_security_margin;
        }
        // Step 1: Calculate individual deltas and total delta sum for either steady or fast collision detection
        if (dynamic_collision_detection_active){
            for (uint8_t i = 0; i < NUM_MOTORS; i++) {
                average_current_delta_single[i] = (average_current_single_reading[i] - max_current_threshold_set[i]);
                if (average_current_delta_single[i] > 0) {
                    average_current_delta_sum += average_current_delta_single[i];
                }
            }
        }

        // Step 2: Check for collision based on total of all currents
        if (average_current_delta_sum > (security_margin)) {
#if BED_CAPTURE_MOTOR_CURRENTS
            printf("#COLLISION,%lu\r\n", (unsigned long)capture_now_ms());
#else
            if (dynamic_collision_detection_active){
                printf("Tripped on steady!\r\n");
                printf("Securit margin: %d\r\n", security_margin);
            }
            for (uint8_t i = 0; i < NUM_MOTORS; i++) {
                printf("Motor %d Delta Current:  %ld\r\n", i, average_current_delta_single[i]);
            }
#endif

            set_all_motors_off();
            any_collision = true;

            // Reset motor readings
            reset_ignore_data_points_count();
            reset_motor_current_data();
        }
    }
}

// Runs one tick of motor current supervision: scan currents, detect movement,
// per-motor averaging / max-load / dynamic-threshold / limit-switch checks,
// then the summed collision check. Shared by manual moves (app.c) and
// auto-level (auto_level.c). sw selects limit-switch behavior; is_raising
// selects the raise vs lower collision margin.
void run_motor_supervision(active_switch_t sw, bool is_raising)
{
  scan_all_currents();
  check_motor_movement();
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    motor_current_running_average(i);
    max_load_check(i);
    set_dynamic_collosion_threshold(i);
    limit_switch_statuses(i, sw, auto_level_current_axis);
    if (any_collision) {
      break;
    }
  }
  if (motors_temp_off) {
    all_motor_limit_switch_temp_off();
  }
  collision_check(is_raising);

#if BED_CAPTURE_MOTOR_CURRENTS
  {
    static bool header_printed = false;
    if (!header_printed) {
      printf("time_ms,motor0_mA,motor1_mA,motor2_mA,motor3_mA\r\n");
      header_printed = true;
    }
    printf("%lu,%lu,%lu,%lu,%lu\r\n",
           (unsigned long)capture_now_ms(),
           (unsigned long)filtered_current[0], (unsigned long)filtered_current[1],
           (unsigned long)filtered_current[2], (unsigned long)filtered_current[3]);
  }
#endif
}


