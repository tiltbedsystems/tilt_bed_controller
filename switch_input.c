/*
 * switch_input.c
 *
 * Relocated from app.c on C4 commit 4 (Jul 2026) - see switch_input.h. Pure
 * relocation: no logic or timing change from the pre-move behavior. The one
 * observable-free reordering: the heartbeat block used to sit between the
 * double-press-check block and the UI-dispatch block inline in
 * app_process_action(); combining those two into switch_input_tick() means
 * heartbeat now runs after both instead of between them. heartbeat's own
 * block only sets/clears an atomic flag (its LED toggle line is already
 * commented out), so this has no reachable behavioral effect.
 */

#include "switch_input.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "sl_atomic.h"
#include "sl_simple_button_instances.h"
#include "sl_sleeptimer.h"

#include "app.h"
#include "bed_actions.h"
#include "bed_control.h"
#include "bed_types.h"
#include "debug_capture.h"
#include "dual_motor_control.h"
#include "exp_board.h"
#include "exp_ui.h"
#include "motor_current_functions.h"
#include "settings_mode.h"

// Series of #defines to use as switch input masks with packed variables.
// SWITCH_UP_ALL/AUTO_LEVEL/DOWN_ALL/BED_LIGHTS live in app.h - shared with
// settings_mode.c's settings_mode_process_outputs(), which switches on the
// same app_ui_interrupt masks.
#define SWITCH_DOWN_FOOT   0x0100
#define SWITCH_DOWN_RIGHT  0x0200
#define SWITCH_UP_RIGHT    0x0400
#define SWITCH_DOWN_HEAD   0x0800
#define SWITCH_UP_FOOT     0x1000
#define SWITCH_DOWN_LEFT   0x2000
#define SWITCH_UP_LEFT     0x4000
#define SWITCH_UP_HEAD     0x8000

/// Double press error check time interval
#define DOUBLE_PRESS_ERROR_CHECK_TIME_INTERVAL 50 // time in ms

// Flags
static bool read_ui_data = false; // Interrupt flag to read the UI status
static bool process_switch_change = true; // Indicates now clear to deal with another switch press
bool switch_pressed = false; // Interrupt flag to capture switch up/down state - extern via switch_input.h

/*
  I2C GPIO Expander Port 0 and Port 1 packed status variables
  app_ui_status = ((uint16_t) (ports[0] << 8)) | ports[1];

  app_ui_interrupt shares same bit mapping

   P0_7  UP_M4           P1_7  EN_AMBERn
   P0_6  UP_M3           P1_6  EN_BLUEn
   P0_5  UP_M2           P1_5  EN_GREENn
   P0_4  UP_M1           P1_4  EN_REDn
   P0_3  DOWN_M4         P1_3  BED_LIGHTS
   P0_2  DOWN_M3         P1_2  DOWN_ALL
   P0_1  DOWN_M2         P1_1  AUTO_LEVEL
   P0_0  DOWN_M1         P1_0  UP_ALL

  Port register bit ordering is:
  MSB   Px.7 Px.6 Px.5 Px.4 Px.3 Px.2 Px.1 Px.0   LSB

  e.g. 0xF0 sets upper 4 IO's to 1, and lower 4 to 0 in that register

  All switches have pull-ups and all LED's are active low.
 */

static uint16_t app_ui_status;
static uint16_t app_ui_status_new; // Used for double press error check
uint16_t app_ui_interrupt; // Packed Port0/1 interrupt source(s) - extern via switch_input.h
static uint16_t app_ui_interrupt_new; // Used for double press error check

// Timer for reading switch values during operation to ensure motors are off if no buttons are pressed
// Important for pressing two switches at the same time
static sl_sleeptimer_timer_handle_t double_press_error_timer;
static bool double_press_error_timer_timeout = false;
static bool is_double_press_error_timer_on = false;

/*******************************************************************************/
/// For double press error timer - ONESHOT
/*******************************************************************************/
// Called when the auto_level timer goes off
static void callback_double_press_error_timer(sl_sleeptimer_timer_handle_t *handle_double_press_error, void *data_double_press_error)
{
  (void)handle_double_press_error;
  (void)data_double_press_error;

  double_press_error_timer_timeout = true;

}

// Starts a one-shot timer for auto_level
static void start_double_press_error_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(DOUBLE_PRESS_ERROR_CHECK_TIME_INTERVAL);
  sl_sleeptimer_start_periodic_timer(&double_press_error_timer, ticks, callback_double_press_error_timer, NULL, 0, 0);
}

// Resets the double-press-error timer - non-static: called by bed_actions.c
// via switch_input.h.
void app_reset_double_press_timer(void)
{
  sl_sleeptimer_stop_timer(&double_press_error_timer);
}

// Maps each single-side switch mask to its active_switch_t value, motor side, and direction
static const struct {
  uint16_t mask;
  active_switch_t value;
  side_of_bed_t side;
  bool up;
} motor_switch_table[] = {
  { SWITCH_DOWN_HEAD,  SWITCH_DOWN_HEAD_VALUE,  HEAD,  false },
  { SWITCH_DOWN_LEFT,  SWITCH_DOWN_LEFT_VALUE,  LEFT,  false },
  { SWITCH_DOWN_RIGHT, SWITCH_DOWN_RIGHT_VALUE, RIGHT, false },
  { SWITCH_DOWN_FOOT,  SWITCH_DOWN_FOOT_VALUE,  FOOT,  false },
  { SWITCH_UP_HEAD,    SWITCH_UP_HEAD_VALUE,    HEAD,  true  },
  { SWITCH_UP_LEFT,    SWITCH_UP_LEFT_VALUE,    LEFT,  true  },
  { SWITCH_UP_RIGHT,   SWITCH_UP_RIGHT_VALUE,   RIGHT, true  },
  { SWITCH_UP_FOOT,    SWITCH_UP_FOOT_VALUE,    FOOT,  true  },
};
#define NUM_MOTOR_SWITCH_TABLE_ENTRIES (sizeof(motor_switch_table) / sizeof(motor_switch_table[0]))

// Maps a side_of_bed_t to its bed_control.h bed_target_t equivalent (explicit,
// not a cast, so the two enums can be reordered independently without risk)
static bed_target_t side_to_target(side_of_bed_t side)
{
  switch (side) {
    case HEAD:  return BED_TARGET_HEAD;
    case LEFT:  return BED_TARGET_LEFT;
    case RIGHT: return BED_TARGET_RIGHT;
    case FOOT:  return BED_TARGET_FOOT;
  }
  return BED_TARGET_HEAD; // unreachable - side_of_bed_t has exactly these 4 values
}

// Executes changes in motor states to turn various power outputs ON/OFF
static void process_outputs()
{
  if((!any_collision) && (!in_settings_mode)){

    switch (app_ui_interrupt) // Configure controls based on activated switch
    {
      case SWITCH_UP_ALL:
          if ((switch_pressed) && (active_switch == SWITCH_UP_ALL_VALUE)){
              set_LED_on(LED_BLUE);
              set_all_motors_on();
          } else if ((!switch_pressed) && (active_switch == SWITCH_UP_ALL_VALUE)){
              set_LED_off(LED_BLUE);
              set_all_motors_off();
              active_switch = OFF;
          }
          break;

      case SWITCH_AUTO_LEVEL:
          if ((switch_pressed) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
              set_LED_on(LED_RED);
              set_all_motors_off();
          } else if ((!switch_pressed) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
              set_all_motors_off();
              auto_level_stop_blink_LED(LED_RED);
              active_switch = OFF;
          }
          break;

      case SWITCH_DOWN_ALL:
          if ((switch_pressed) && (active_switch == SWITCH_DOWN_ALL_VALUE)){
              set_LED_on(LED_BLUE);
              set_all_motors_on();
          } else if ((!switch_pressed) && (active_switch == SWITCH_DOWN_ALL_VALUE)){
              set_LED_off(LED_BLUE);
              set_all_motors_off();
              active_switch = OFF;
          }
          break;

      case SWITCH_BED_LIGHTS:
          if ((switch_pressed) && (active_switch == SWITCH_BED_LIGHT_VALUE)){
              if (app_lights_on()){
                  set_LED_on(LED_AMBER);
                  set_underbed_lighting_on();}
              else {
                  set_LED_off(LED_AMBER);
                  set_underbed_lighting_off();
              }
          } else if ((!switch_pressed) && (active_switch == SWITCH_BED_LIGHT_VALUE)){
              active_switch = OFF;
          }
          break;

    default: {
        for (uint8_t t = 0; t < NUM_MOTOR_SWITCH_TABLE_ENTRIES; t++) {
            if (app_ui_interrupt == motor_switch_table[t].mask) {
                if ((switch_pressed) && (active_switch == motor_switch_table[t].value)){
                    set_LED_on(LED_GREEN);
                    set_dual_motors_on(motor_switch_table[t].side); // Motor statuses are set in "set_dual_motors_on/off()" function
                } else if ((!switch_pressed) && (active_switch == motor_switch_table[t].value)){
                    set_LED_off(LED_GREEN);
                    set_all_motors_off();
                    active_switch = OFF;
                }
                break;
            }
        }
        break;
    }
    }
  }

  else if (in_settings_mode){
      settings_mode_process_outputs();
  }

  // Ready to look at another switch input, if any
  process_switch_change = true;
}

// Configures motor control elements based on response to change in switch state
static void process_inputs()
{
  if (!in_settings_mode){

      switch (app_ui_interrupt) // Configure controls based on activated switch
      {
        case SWITCH_UP_ALL:
            if (switch_pressed && (active_switch == OFF)){
                bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_ALL, BED_DIR_UP);
            } else if ((!switch_pressed) && (active_switch == SWITCH_UP_ALL_VALUE || active_switch == OFF)){
                bed_move_stop(BED_SOURCE_SWITCHES);
            }
            break;

        case SWITCH_AUTO_LEVEL:
            if (switch_pressed && (active_switch == OFF)){
                bed_auto_level_start(BED_SOURCE_SWITCHES);
            } else if ((!switch_pressed) && (active_switch == SWITCH_AUTO_LEVEL_VALUE || active_switch == OFF)){
                bed_auto_level_stop(BED_SOURCE_SWITCHES);
            }
            break;

        case SWITCH_DOWN_ALL:
            if (switch_pressed && (active_switch == OFF)){
                bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_ALL, BED_DIR_DOWN);
            } else if ((!switch_pressed) && (active_switch == SWITCH_DOWN_ALL_VALUE || active_switch == OFF)){
                bed_move_stop(BED_SOURCE_SWITCHES);
            }
            break;

        case SWITCH_BED_LIGHTS:
            if (switch_pressed && (active_switch == OFF)){
                bed_lights_set(BED_SOURCE_SWITCHES, !app_lights_on());

            } else if ((!switch_pressed) && (active_switch == SWITCH_BED_LIGHT_VALUE || active_switch == OFF)){
                sl_sleeptimer_stop_timer(&double_press_error_timer);
                /*** For "Settings Mode" ***/
                settings_mode_cancel_entry();

            }
            break;

        default: {
            bool matched = false;
            for (uint8_t t = 0; t < NUM_MOTOR_SWITCH_TABLE_ENTRIES; t++) {
                if (app_ui_interrupt == motor_switch_table[t].mask) {
                    matched = true;
                    if (switch_pressed && (active_switch == OFF)) {
                        bed_move_start(BED_SOURCE_SWITCHES,
                                       side_to_target(motor_switch_table[t].side),
                                       motor_switch_table[t].up ? BED_DIR_UP : BED_DIR_DOWN);
                    } else if ((!switch_pressed) && (active_switch == motor_switch_table[t].value || active_switch == OFF)) {
                        bed_move_stop(BED_SOURCE_SWITCHES);
                    }
                    break;
                }
            }
            if (!matched && (app_ui_interrupt != 0)) {
#if !BED_CAPTURE_MOTOR_CURRENTS
                printf("Unrecognized switch mask: 0x%04X\r\n", app_ui_interrupt);
#endif
            }
            break;
        }
      }

      // Switch changes now implemented in control structures, so set outputs if no input hold
      process_outputs();

   }

   else if (in_settings_mode){

        process_outputs();

   }
}

// Called whenever there is a change in switch or expander interrupt state
void sl_button_on_change(const sl_button_t *handle)
{
  bool local_read_ui_data;

  // Will need to expand logic to handle motor fault as well

  if (sl_button_get_state(handle) == SL_SIMPLE_BUTTON_PRESSED) {
    if (&sl_button_ui_int == handle) { // Test for UI expander switch change
        local_read_ui_data = true;
        sl_atomic_store(read_ui_data, local_read_ui_data);
    }
  }
}

// Runs the double-press-error check and (when a fresh switch change is
// latched) the UI interrupt/status read + process_inputs() dispatch. Called
// once per tick from app_process_action(), same relative position as the two
// inline blocks it replaces.
void switch_input_tick(void)
{
  /****
   * Double Press check
   ****/

  if ((active_switch != OFF) && !any_collision && (!auto_level_active) && (!in_settings_mode)){
      if (initial_switch_press){
          scan_all_currents();
          initial_switch_press = false;

          // Double press error check timer
          sl_sleeptimer_is_timer_running(&double_press_error_timer, &is_double_press_error_timer_on);
          if (!is_double_press_error_timer_on){
              start_double_press_error_timer();
          }
      }
      run_motor_supervision(active_switch, is_raising);

      if (double_press_error_timer_timeout){
          double_press_error_timer_timeout = false;

          // Get source of switch interrupt
          app_ui_interrupt_new = get_ui_interrupt();

          // Read the switch board input ports and process accordingly
          get_ui_status();
          app_ui_status_new = get_ui_status();

//          printf("Initial UI Interrupt Source = %d, Status = %d \r\n", app_ui_interrupt, app_ui_status);
//          printf("New UI Interrupt Source = %d, Status = %d \r\n", app_ui_interrupt_new, app_ui_status_new);

          if (app_ui_status_new == app_ui_status){
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("Double Press Error\r\n");
#endif
              set_dual_motors_off(HEAD);
              set_dual_motors_off(FOOT);
              active_switch = OFF;
              // Reset motor readings
              reset_ignore_data_points_count();
              reset_motor_current_data();
              // Set all LEDs off
              set_LED_off(LED_GREEN);
              set_LED_off(LED_BLUE);
              set_LED_off(LED_RED);
//              set_LED_off(LED_AMBER);
              /*** For "Settings Mode" ***/
              settings_mode_cancel_entry();

          } else {
//              printf("No Error\r\n");
          }
      }
  }

  bool local_read_ui_data;
  sl_atomic_load(local_read_ui_data, read_ui_data);

  if (local_read_ui_data && process_switch_change) {

      local_read_ui_data = false;
      sl_atomic_store(read_ui_data, local_read_ui_data);
      process_switch_change = false;

      // Get source of switch interrupt
      app_ui_interrupt = get_ui_interrupt();

      // Read the switch board input ports and process accordingly
      get_ui_status();
      app_ui_status = get_ui_status();
//       printf("\r\n");
//       printf("UI Interrupt Source = %d, Status = %d \r\n", app_ui_interrupt, app_ui_status);

      /*
       Mask interrupt variable in bitwise AND with switch state variable
       to determine the state of the switch - pressed or released.

       Switches are active low with pull-ups, so if status bit is 1 when
       state change was latched into register, switch was released.
       */

      if (app_ui_interrupt & app_ui_status){ // Switch was released
        switch_pressed = false;
      }
      else {
        switch_pressed = true;
      }

      process_inputs();
  }
}
