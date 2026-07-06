/*
 * settings_mode.c
 *
 * Relocated from app.c on C4 commit 3 (Jul 2026) - see settings_mode.h.
 * Pure relocation: no logic or timing change from the pre-move behavior.
 */

#include "settings_mode.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "sl_sleeptimer.h"

#include "app.h"
#include "auto_level.h"
#include "bed_settings.h"
#include "bed_types.h"
#include "debug_capture.h"
#include "exp_ui.h"
#include "motor_current_functions.h"
#include "switch_input.h"

/// Collision detection security margins
#define RAISE_HIGH_SENSITIVITY 350 // Default values - safest
#define RAISE_MED_SENSITIVITY 550
#define RAISE_LOW_SENSITIVITY 750
#define RAISE_OFF_SENSITIVITY 10000
#define LOWER_HIGH_SENSITIVITY 250 // Default values - safest
#define LOWER_MED_SENSITIVITY 275
#define LOWER_LOW_SENSITIVITY 300
#define LOWER_OFF_SENSITIVITY 750

// Collision sensitivity ladder, index 0 = safest (HIGH) ... last = OFF
static const struct {
  int16_t raise;
  int16_t lower;
  bool led_green;
  bool led_blue;
  bool led_red;
} sensitivity_levels[] = {
  { RAISE_HIGH_SENSITIVITY, LOWER_HIGH_SENSITIVITY, true,  false, false },
  { RAISE_MED_SENSITIVITY,  LOWER_MED_SENSITIVITY,  false, true,  false },
  { RAISE_LOW_SENSITIVITY,  LOWER_LOW_SENSITIVITY,  false, false, true  },
  { RAISE_OFF_SENSITIVITY,  LOWER_OFF_SENSITIVITY,  true,  true,  true  },
};
#define NUM_SENSITIVITY_LEVELS (sizeof(sensitivity_levels) / sizeof(sensitivity_levels[0]))

// Definition - extern via settings_mode.h. See switch_input.h for the
// switch_pressed/app_ui_interrupt shared externs this module reads.
bool in_settings_mode = false;

static uint8_t nvm3StatusCounter = 0; // Incremented during successful NVM3 operation, value used to tell user if there were errors

static bool zeroLevelFirstPass = true;
static bool resetZeroLevelFirstPass = true;
static bool writeCollisionDetection = false;

static sl_sleeptimer_timer_handle_t handle_5000ms_timer; // For settings erase NVM3
static sl_sleeptimer_timer_handle_t handle_2000ms_timer; // For settings set NVM3

static bool is_5000ms_timer_running = false;
static bool timeout_5000ms = false;

static bool timeout_2000ms = false;
static bool is_2000ms_timer_running = false;

/*******************************************************************************/
/// For "Settings" NVM3 wipe
/*******************************************************************************/
// Called when the 5 second timer goes off - sets a flag
static void callback_5000ms_timer(sl_sleeptimer_timer_handle_t *handle_5000ms, void *data_5000ms)
{
  (void)handle_5000ms;
  (void)data_5000ms;
  timeout_5000ms = true;
}

// Starts a 5000ms one-shot timer for auto_level
static void start_5000ms_oneshot_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(5000);
  sl_sleeptimer_start_timer(&handle_5000ms_timer, ticks, callback_5000ms_timer, NULL, 0, 0);
}

/*******************************************************************************/
/// For "Settings" NVM3 store
/*******************************************************************************/
// Called when the 1 second timer goes off
static void callback_2000ms_timer(sl_sleeptimer_timer_handle_t *handle_2000ms, void *data_2000ms)
{
  (void)handle_2000ms;
  (void)data_2000ms;
  timeout_2000ms = true;
}

// Starts a 2000ms one-shot timer for auto_level
static void start_2000ms_oneshot_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(2000);
  sl_sleeptimer_start_timer(&handle_2000ms_timer, ticks, callback_2000ms_timer, NULL, 0, 0);
}

void settings_mode_cancel_entry(void)
{
  sl_sleeptimer_stop_timer(&handle_5000ms_timer);
  timeout_5000ms = false;
}

// Finds the ladder rung matching the current raise/lower margins, or -1 if mismatched
static int8_t find_sensitivity_index(void)
{
  for (uint8_t lvl = 0; lvl < NUM_SENSITIVITY_LEVELS; lvl++) {
    if ((raise_collision_current_security_margin == sensitivity_levels[lvl].raise) &&
        (lower_collision_current_security_margin == sensitivity_levels[lvl].lower)) {
      return (int8_t)lvl;
    }
  }
  return -1;
}

// Applies a ladder rung: sets both margins and the matching LED pattern
static void apply_sensitivity_level(uint8_t level)
{
  raise_collision_current_security_margin = sensitivity_levels[level].raise;
  lower_collision_current_security_margin = sensitivity_levels[level].lower;
  if (sensitivity_levels[level].led_green) { set_LED_on(LED_GREEN); } else { set_LED_off(LED_GREEN); }
  if (sensitivity_levels[level].led_blue)  { set_LED_on(LED_BLUE);  } else { set_LED_off(LED_BLUE);  }
  if (sensitivity_levels[level].led_red)   { set_LED_on(LED_RED);   } else { set_LED_off(LED_RED);   }
}

// Settings-mode branch of process_outputs() - executes LED/motor output
// changes for the currently latched switch interrupt while in_settings_mode.
void settings_mode_process_outputs(void)
{
  switch (app_ui_interrupt) // Configure controls based on activated switch
  {
    case SWITCH_UP_ALL:
      if (switch_pressed && (active_switch == OFF)){

        active_switch = SWITCH_UP_ALL_VALUE;
        writeCollisionDetection = false;

      }
      else if (active_switch == SWITCH_UP_ALL_VALUE){

        active_switch = OFF;
        sl_sleeptimer_stop_timer(&handle_2000ms_timer);
        set_LED_off(LED_RED);
        set_LED_off(LED_GREEN);
        set_LED_off(LED_BLUE);
        writeCollisionDetection = false;

      }
      break;

    case SWITCH_AUTO_LEVEL:
      // On press
      if (switch_pressed && ((active_switch == OFF) || (active_switch == SWITCH_AUTO_LEVEL_VALUE))){
        active_switch = SWITCH_AUTO_LEVEL_VALUE;
        zeroLevelFirstPass = true;
        resetZeroLevelFirstPass = true;
      }
      // On release
      else if ((!switch_pressed) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
        active_switch = OFF;
        sl_sleeptimer_stop_timer(&handle_2000ms_timer);
        sl_sleeptimer_stop_timer(&handle_5000ms_timer);
        set_LED_off(LED_RED);
        set_LED_off(LED_GREEN);
        set_LED_off(LED_BLUE);
      }
      break;

    case SWITCH_DOWN_ALL:
      if (switch_pressed && (active_switch == OFF)){

        active_switch = SWITCH_DOWN_ALL_VALUE;
        writeCollisionDetection = false;

      }
      else if (active_switch == SWITCH_DOWN_ALL_VALUE){

        active_switch = OFF;
        sl_sleeptimer_stop_timer(&handle_2000ms_timer);
        set_LED_off(LED_RED);
        set_LED_off(LED_GREEN);
        set_LED_off(LED_BLUE);
        writeCollisionDetection = false;

      }
      break;

    case SWITCH_BED_LIGHTS:
      if (switch_pressed && (active_switch == OFF)){
        active_switch = SWITCH_BED_LIGHT_VALUE;
      }
      else if (active_switch == SWITCH_BED_LIGHT_VALUE){

        active_switch = OFF;

      }
      break;

    default:
      break;
  }
}

// Settings-mode block of app_process_action(): entry check, then (while
// active) the zero-angle store/erase and sensitivity-ladder logic.
void settings_mode_tick(void)
{
  if ((switch_pressed == true) && (active_switch == SWITCH_BED_LIGHT_VALUE) && (!in_settings_mode)){
    sl_sleeptimer_is_timer_running(&handle_5000ms_timer, &is_5000ms_timer_running);

    if (!is_5000ms_timer_running){
      start_5000ms_oneshot_timer();
    }

    if (timeout_5000ms){
        in_settings_mode = true;
        sl_sleeptimer_stop_timer(&handle_5000ms_timer);
        timeout_5000ms = false;
    }
  }

  if (in_settings_mode){

      fast_blink_LED(LED_AMBER);

      /******************************
      Setting Auto Level "Zero" Point

      (1) Start timers to trip timeout after 1 & 5 seconds
              -> during the 1 second timer, accel values are read and averaged
              -> the 5 second timer is used to reset stored values if button is continuously held
      (2) After timeout, store roll and pitch angles and turn on green LED to indicate successful NVM3 writes
      ******************************/

      if (active_switch == SWITCH_AUTO_LEVEL_VALUE){
        sl_sleeptimer_is_timer_running(&handle_2000ms_timer, &is_2000ms_timer_running);

        // Only start 2 second timer to read and store accel values once per button press
        if ((!is_2000ms_timer_running) && (zeroLevelFirstPass)){
            start_2000ms_oneshot_timer();
#if !BED_CAPTURE_MOTOR_CURRENTS
            printf("2 second timer start\r\n");
            if (zeroLevelFirstPass){
                printf("zeroLevelFirstPass is true\r\n");
            }
#endif
            set_LED_on(LED_RED);
        }

        if (zeroLevelFirstPass){
            accel_running_average();
        }

        sl_sleeptimer_is_timer_running(&handle_5000ms_timer, &is_5000ms_timer_running);

        // Only start 5 second timer to reset stored accel values after the store function has processed and the user has not already reset values
        if ((!is_5000ms_timer_running) && (!zeroLevelFirstPass) && (resetZeroLevelFirstPass)){
            start_5000ms_oneshot_timer();
#if !BED_CAPTURE_MOTOR_CURRENTS
            printf("5 second timer start\r\n");
#endif
        }


        // After 1 second of holding the button, write roll and pitch values
        if (timeout_2000ms){

            zeroLevelFirstPass = false;
            sl_sleeptimer_stop_timer(&handle_2000ms_timer);
            timeout_2000ms = false;

#if !BED_CAPTURE_MOTOR_CURRENTS
            printf("Writing values to NVM3...\r\n");
#endif
            nvm3StatusCounter = 0;
            if (!bed_settings_set(BED_SETTING_BASELINE_ROLL_ANGLE, returnAccelValue(ROLL))) {
#if !BED_CAPTURE_MOTOR_CURRENTS
                printf("Failed to store roll angle\r\n");
#endif
            }
            else {
#if !BED_CAPTURE_MOTOR_CURRENTS
                printf("Successfully stored roll angle\r\n");
#endif
                nvm3StatusCounter++;
            }
            if (!bed_settings_set(BED_SETTING_BASELINE_PITCH_ANGLE, returnAccelValue(PITCH))) {
#if !BED_CAPTURE_MOTOR_CURRENTS
                printf("Failed to store pitch angle\r\n");
#endif
            }
            else {
#if !BED_CAPTURE_MOTOR_CURRENTS
                printf("Successfully stored pitch angle\r\n");
#endif
                nvm3StatusCounter++;
            }

            // If all writes are successful turn on green LED
            if (nvm3StatusCounter == 2){
                set_LED_on(LED_GREEN);
#if !BED_CAPTURE_MOTOR_CURRENTS
                printf("Stored ROLL value is: %d\r\n", returnAccelValue(ROLL));
                printf("Stored PITCH value is: %d\r\n", returnAccelValue(PITCH));
#endif
            }
            // If any writes fail, flash red LED
            else {
                fast_blink_LED(LED_RED);
            }
        }

        // After holding button for another 5 continuous seconds, reset roll and pitch values
        if (timeout_5000ms){
            timeout_5000ms = false;
            sl_sleeptimer_stop_timer(&handle_5000ms_timer);
            resetZeroLevelFirstPass = false;
            bed_settings_erase(BED_SETTING_BASELINE_ROLL_ANGLE);
            bed_settings_erase(BED_SETTING_BASELINE_PITCH_ANGLE);
            set_LED_off(LED_GREEN);
            set_LED_on(LED_BLUE);
#if !BED_CAPTURE_MOTOR_CURRENTS
            printf("Erased Angles");
#endif
        }
     }

      /******************************
      Setting Collision Detection Sensitivity

      --> If ALL_UP button held for 2 seconds, increase collision sensitivity and set LED
      --> If ALL_DOWN button held for 2 seconds, decrease collision sensitivity and set LED
      ******************************/
      if (active_switch == SWITCH_UP_ALL_VALUE){
          sl_sleeptimer_is_timer_running(&handle_2000ms_timer, &is_2000ms_timer_running);

          // Only start 2 second timer to read and store accel values once per button press
          if (!is_2000ms_timer_running){
              start_2000ms_oneshot_timer();
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("2 second timer start\r\n");
#endif
          }

          // After 2 seconds of holding the button, increase sensitivity (decrease collision security margin)
          if (timeout_2000ms){
              int8_t level = find_sensitivity_index();
              if (level < 0) {
                  level = 0; // mismatched - snap to safest
              } else if (level > 0) {
                  level--; // increase sensitivity = move toward HIGH
              }
              apply_sensitivity_level((uint8_t)level);
              writeCollisionDetection = true;
          }
       }
      if (active_switch == SWITCH_DOWN_ALL_VALUE){
          sl_sleeptimer_is_timer_running(&handle_2000ms_timer, &is_2000ms_timer_running);

          // Only start 2 second timer to read and store accel values once per button press
          if (!is_2000ms_timer_running){
              start_2000ms_oneshot_timer();
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("2 second timer start\r\n");
#endif
          }

          // After 2 seconds of holding the button, decrease sensitivity (increase collision security margin)
          if (timeout_2000ms){
              int8_t level = find_sensitivity_index();
              if (level < 0) {
                  level = 0; // mismatched - snap to safest, matching original behavior
              } else if (level < (int8_t)(NUM_SENSITIVITY_LEVELS - 1)) {
                  level++; // decrease sensitivity = move toward OFF
              }
              apply_sensitivity_level((uint8_t)level);
              writeCollisionDetection = true;
           }
       }
      if (writeCollisionDetection == true){
          writeCollisionDetection = false;

#if !BED_CAPTURE_MOTOR_CURRENTS
          printf("Writing values to NVM3...\r\n");
#endif
          nvm3StatusCounter = 0;
          if (!bed_settings_set(BED_SETTING_RAISE_COLLISION_MARGIN, raise_collision_current_security_margin)) {
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("Failed to store raise security margin\r\n");
#endif
          }
          else {
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("Successfully stored raise security margin\r\n");
#endif
              nvm3StatusCounter++;
          }
          if (!bed_settings_set(BED_SETTING_LOWER_COLLISION_MARGIN, lower_collision_current_security_margin)) {
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("Failed to store lower security margin\r\n");
#endif
          }
          else {
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("Successfully stored lower security margin\r\n");
#endif
              nvm3StatusCounter++;
          }
          // If all writes are successful turn on green LED
          if (nvm3StatusCounter == 2){
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("Stored Raise Sensitivity is: %d\r\n", raise_collision_current_security_margin);
              printf("Stored Lower Sensitivity is: %d\r\n", lower_collision_current_security_margin);
#endif
          }
          // If any writes fail, flash red LED
          else {
              fast_blink_LED(LED_RED);
          }
          sl_sleeptimer_stop_timer(&handle_2000ms_timer);
          timeout_2000ms = false;
      }
  }
}
