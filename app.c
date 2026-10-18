/***************************************************************************//**
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/
/*
 * Bed application logic below is ported from totem_controller (Gecko SDK
 * 4.2.2), originally: Alan Jones & Andrew Duncan. The SoC Empty sample's
 * sl_bt_on_event() boot/advertising handling (below) is untouched and stays
 * dormant until BLE work begins post-D1 regression (DECISIONS.md #20).
 */
#include "sl_bt_api.h"
#include "sl_main_init.h"
#include "app_assert.h"
#include "app.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "sl_component_catalog.h"
#include "em_gpio.h"
#include "pin_config.h"
#include "sl_atomic.h"
#include "sl_pwm.h"
#include "sl_pwm_instances.h"
#include "sl_simple_led.h"
#include "sl_simple_led_instances.h"
#include "sl_sleeptimer.h"

#include "adc_currents.h"
#include "auto_level.h"
#include "bed_actions.h"
#include "bed_settings.h"
#include "bed_types.h"
#include "ble_bed_service.h"
#include "debug_capture.h"
#include "exp_board.h"
#include "exp_ui.h"
#include "mc3479.h"
#include "settings_mode.h"
#include "switch_input.h"
#include "version.h"
#include "dual_motor_control.h"
#include "motor_current_functions.h"
#include "nvm3_functions.h"

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/

// Series of #defines to use as switch input masks with packed variables.
// The single-side SWITCH_* masks and the double-press timing constant moved
// to switch_input.c (C4 commit 4) - only consumer was the switch decode/
// dispatch logic now living there. SWITCH_UP_ALL/AUTO_LEVEL/DOWN_ALL/
// BED_LIGHTS stay in app.h - shared with settings_mode.c's
// settings_mode_process_outputs(), which switches on the same
// app_ui_interrupt masks.

#define COLLISION_BLINK_CYCLES 3 // Number of times to blink LEDs for a collision
#define ACCEL_TIMER_DURATION 1000 // Time in ms

// Collision sensitivity ladder defines/table moved to settings_mode.c - only
// consumer was the settings-mode sensitivity logic now living there.


/*******************************************************************************
 ******************************   STRUCTURES   *********************************
 ******************************************************************************/

/*******************************************************************************
 ***************************   PRIVATE VARIABLES   *****************************
 ******************************************************************************/

// Flags
static bool heartbeat = false; // Interrupt flag for LED heartbeat - use for debug only
// switch_pressed moved to switch_input.c
bool auto_level_active = false; // Tracks whether autolevel is running - extern via app.h, see bed_actions.c
bool is_raising; // Used to set specific collision threshold based on whether motors are raising or lowering - extern via app.h, see bed_actions.c

// Counters
static uint8_t LED_blink_counter = 0; // LED blink counter

// NVM3 Status
Ecode_t nvm3Status;

// Settings Mode variables moved to settings_mode.c (in_settings_mode,
// zeroLevelFirstPass, resetZeroLevelFirstPass, writeCollisionDetection,
// nvm3StatusCounter) - see settings_mode.h.

// app_ui_interrupt moved to switch_input.c

static sl_sleeptimer_timer_handle_t heartbeat_delay_timer;
static sl_sleeptimer_timer_handle_t LED_500ms_timer; // For collision detected LED blinks

static sl_sleeptimer_timer_handle_t auto_level_LED_blink_timer; // For red LED blink during auto level
static bool auto_level_LED_blink_timer_timeout = false;
static bool is_auto_level_LED_blink_timer_on = false;

static sl_sleeptimer_timer_handle_t LED_200ms_timer; // For rapid LED blink for settings or for failed auto-level
static sl_sleeptimer_timer_handle_t auto_level_timer;
// handle_5000ms_timer/handle_2000ms_timer moved to settings_mode.c

// Used to determine if all LEDs are on or off for turning all off and on in LED_timer_callback
static bool all_LEDs_on = false;
static bool single_LED_on[NUM_LEDS] = {false};
static bool is_LED_timer_running = false;
static bool LED_timer_timeout = false;

const int rotating_LED_sequence[] = {LED_AMBER, LED_BLUE, LED_RED, LED_GREEN}; // Desired sequence of LEDs

// Used for auto_level flags & timing
static bool auto_level_timer_timeout = false;
static bool is_auto_level_timer_running = false;
// is_5000ms_timer_running/timeout_5000ms/timeout_2000ms/is_2000ms_timer_running moved to settings_mode.c


// Determines which switch is active - extern via app.h, see bed_actions.c
active_switch_t active_switch = OFF;
// Stores what switch was active when a collision occurred to use to reverse direction
static active_switch_t active_switch_during_collision = OFF;

bool first_print[10] = {true, true, true, true, true, true, true, true, true, true}; // extern via app.h, see bed_actions.c


/*******************************************************************************
 *********************   PRIVATE FUNCTION DECLARATIONS   ***********************
 ******************************************************************************/

/*******************************************************************************/
// Called when the heartbeat periodic timer goes off - sets a flag
static void heartbeat_timer_callback(sl_sleeptimer_timer_handle_t *heartbeat_handle, void *heartbeat_data)
{
  (void)heartbeat_handle;
  (void)heartbeat_data;
  bool local_heartbeat = true;
  sl_atomic_store(heartbeat, local_heartbeat);
}

// Starts a periodic timer which regulates heartbeat timing aspects of the system
static void start_heartbeat_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(1000);
  sl_sleeptimer_start_periodic_timer(&heartbeat_delay_timer, ticks, heartbeat_timer_callback, NULL, 0, 0);
}




/*******************************************************************************/

/*******************************************************************************/
// Called when the LED timer goes off - sets a flag - inverts LED on/off
static void LED_timer_callback(sl_sleeptimer_timer_handle_t *LED_handle, void *LED_data)
{
  (void)LED_handle;
  (void)LED_data;
  LED_timer_timeout = true;
}

// Starts a 500ms periodic timer for LED blink
static void start_500ms_LED_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(500);
  sl_sleeptimer_start_periodic_timer(&LED_500ms_timer, ticks, LED_timer_callback, NULL, 0, 0);
}

// Starts a 200ms periodic timer for LED blink
static void start_200ms_LED_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(200);
  sl_sleeptimer_start_periodic_timer(&LED_200ms_timer, ticks, LED_timer_callback, NULL, 0, 0);
}




// "Settings" NVM3 wipe/store timer callbacks + starters moved to settings_mode.c

/*******************************************************************************/
/// For auto-level delays
/*******************************************************************************/
// Called when the auto_level timer goes off
static void callback_auto_level_timer(sl_sleeptimer_timer_handle_t *handle_auto_level, void *data_auto_level)
{
  (void)handle_auto_level;
  (void)data_auto_level;
  auto_level_timer_timeout = true;
}

// Starts a one-shot timer for auto_level
static void start_auto_level_oneshot_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(ACCEL_TIMER_DURATION);
  sl_sleeptimer_start_timer(&auto_level_timer, ticks, callback_auto_level_timer, NULL, 0, 0);
}




/*******************************************************************************/
/// For auto-level LED blink delays
/*******************************************************************************/
// Called when the auto_level timer goes off
static void callback_auto_level_LED_blink_timer(sl_sleeptimer_timer_handle_t *handle_auto_level_LED_blink, void *data_auto_level_LED_blink)
{
  (void)handle_auto_level_LED_blink;
  (void)data_auto_level_LED_blink;
  auto_level_LED_blink_timer_timeout = true;
}

// Starts a one-shot timer for auto_level
static void start_auto_level_LED_blink_oneshot_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(500);
  sl_sleeptimer_start_periodic_timer(&auto_level_LED_blink_timer, ticks, callback_auto_level_LED_blink_timer, NULL, 0, 0);
}

/*******************************************************************************/
/*******************************************************************************/

/*******************************************************************************/
/*******************************************************************************/

// Turn on all LEDs
static void set_all_LED_on(){

  // Set all LEDs on to start
  for (uint8_t i = 0; i < NUM_LEDS; i++){
    set_LED_on(i);
    all_LEDs_on = true;
  }
}

// Turn off all LEDs - non-static: called by bed_actions.c, see app.h
void set_all_LED_off(){

  // Set all LEDs on to start
  for (uint8_t i = 0; i < NUM_LEDS; i++){
    set_LED_off(i);
    all_LEDs_on = false;
  }
}

// Toggle all LEDs
static void toggle_all_LED(){

  if (all_LEDs_on){
  // Set all LEDs off
  for (uint8_t i = 0; i < NUM_LEDS; i++){
    set_LED_off(i);
    all_LEDs_on = false;
  }
  } else if (!all_LEDs_on){
    // Set all LEDs on
    for (uint8_t i = 0; i < NUM_LEDS; i++){
      set_LED_on(i);
      all_LEDs_on = true;
    }
  }
}

// Toggle single LED
static void toggle_single_LED(led_color_t LED_color){

  if (single_LED_on[LED_color]){
  // Set LED off
  set_LED_off(LED_color);
  single_LED_on[LED_color] = false;
  } else if (!single_LED_on[LED_color]){
    // Set LED on
    set_LED_on(LED_color);
    single_LED_on[LED_color] = true;
    }

}

// Single LED blink start (can only be used on one LED at a time)
static void auto_level_blink_LED(led_color_t LED_color){

  sl_sleeptimer_is_timer_running(&auto_level_LED_blink_timer, &is_auto_level_LED_blink_timer_on);

  if (!is_auto_level_LED_blink_timer_on){
    start_auto_level_LED_blink_oneshot_timer();
    set_LED_on(LED_color);
    single_LED_on[LED_color] = true;
  }
  if (auto_level_LED_blink_timer_timeout){
    toggle_single_LED(LED_color);
    auto_level_LED_blink_timer_timeout = false;
  }
}

// Single LED blink stop - non-static: called by bed_actions.c, see app.h
void auto_level_stop_blink_LED(led_color_t LED_color){

  sl_sleeptimer_stop_timer(&auto_level_LED_blink_timer);
  set_LED_off(LED_color);
  auto_level_LED_blink_timer_timeout = false;
  single_LED_on[LED_color] = false;
}

// Single LED blink FAST start (can only be used on one LED at a time) -
// non-static: called by settings_mode.c, see app.h
void fast_blink_LED(led_color_t LED_color){

  sl_sleeptimer_is_timer_running(&LED_200ms_timer, &is_LED_timer_running);

  if (!is_LED_timer_running){
    start_200ms_LED_timer();
    set_LED_on(LED_color);
    single_LED_on[LED_color] = true;
  }
  if (LED_timer_timeout){
    toggle_single_LED(LED_color);
    LED_timer_timeout = false;
  }
}

// Single LED blink FAST stop - mirrors auto_level_stop_blink_LED(), but for
// LED_200ms_timer. LED_timer_timeout is shared with the 500 ms collision blink
// timer. This is only called on settings-mode exit, when nothing should be
// driving motors (settings mode never energises them and BLE motion commands
// are refused in it), so no collision blink should be running; if one were,
// clearing the flag would only delay its next toggle by one 500 ms period.
// Non-static: called by settings_mode.c, see app.h
void fast_stop_blink_LED(led_color_t LED_color){

  sl_sleeptimer_stop_timer(&LED_200ms_timer);
  set_LED_off(LED_color);
  LED_timer_timeout = false;
  single_LED_on[LED_color] = false;
}

// sl_button_on_change() moved to switch_input.c

// Resets both the on-board and switch board I2C GPIO expanders simultaneously
static void reset_expanders()
{
  GPIO_PinOutClear(RST_EXP_N_PORT, RST_EXP_N_PIN);

  // Wait for expanders to reset correctly - needs less than 1 usec low
  // RC time constant with reset line caps could be as long as 30-45 msec
  sl_sleeptimer_delay_millisecond(100); // ~ 3 time constants
  GPIO_PinOutSet(RST_EXP_N_PORT, RST_EXP_N_PIN);
  sl_sleeptimer_delay_millisecond(100);
}

// motor_switch_table[]/side_to_target()/process_outputs()/process_inputs()
// moved to switch_input.c

// Resets the auto-level timer - bed_actions.c's action functions used to
// touch it directly via its app.c-private handle - see app.h.
void app_reset_auto_level_timer(void)
{
  sl_sleeptimer_stop_timer(&auto_level_timer);
}

// find_sensitivity_index()/apply_sensitivity_level() moved to settings_mode.c

/*******************************************************************************
 *****************************   PUBLIC FUNCTIONS   ****************************
 ******************************************************************************/

/***************************************************************************//**
 * Initialize the Tilt Bed application
 ******************************************************************************/
void app_init(void)
{

  // stdout is redirected to VCOM in project configuration
#if !BED_CAPTURE_MOTOR_CURRENTS
  printf("Welcome to the Tilt Bed application\r\n");
  printf("Application Version %d.%d.%d \r\n", VERSION_MAJOR, VERSION_MINOR, VERSION_BUILD);
#endif

  // Initialize the Incremental Analog to Digital Converter (IADC)
  initIADC ();

  // Configure GPIO expander reset pin - resets both the board and UI expanders
  GPIO_PinModeSet(RST_EXP_N_PORT,
                  RST_EXP_N_PIN,
                  gpioModePushPull,
                  0);

  // Put I2C GPIO expanders in known reset state and initialize
  reset_expanders();
  init_board_expander();
  init_ui_expander();
  switch_sampler_init(); // baseline read of the UI boards; DECISIONS.md #41

  int PWM_duty = 100;

  sl_pwm_set_duty_cycle(&sl_pwm_motor1, PWM_duty);
  sl_pwm_set_duty_cycle(&sl_pwm_motor2, PWM_duty);
  sl_pwm_set_duty_cycle(&sl_pwm_motor3, PWM_duty);
  sl_pwm_set_duty_cycle(&sl_pwm_motor4, PWM_duty);

  // // Start motor PWM's - only for initial testing, then remove
  sl_pwm_start(&sl_pwm_motor1);
  sl_pwm_start(&sl_pwm_motor2);
  sl_pwm_start(&sl_pwm_motor3);
  sl_pwm_start(&sl_pwm_motor4);


  // Initialize the accelerometer
  init_accel();

  // Enable to test accel
  accel_wake();
  // accel_standby();

  // Start accel timer
  // start_accel_check_timer();

#if !BED_CAPTURE_MOTOR_CURRENTS
  if (accel_get_wake_state() == MC3479_STATE_WAKE)
    printf("Accelerometer is sampling ...\r\n");
  else
    printf("Accelerometer in standby ...\r\n");
#endif

  // Initialize (and start) the heartbeat periodic timer
  start_heartbeat_timer();


  // Initialize NVM3
  nvm3Status = nvm3_initDefault();
  if (nvm3Status != ECODE_NVM3_OK) {
    // Handle initialization error
#if !BED_CAPTURE_MOTOR_CURRENTS
      printf("NVM3 initialization error!");
#endif
  }
  else{
#if !BED_CAPTURE_MOTOR_CURRENTS
      printf("NVM3 initialized successfully\r\n");
#endif
  }

  // Read all user inputs, which will use default values if not stored
  bed_settings_init();

#if !BED_CAPTURE_MOTOR_CURRENTS
  // Print the loaded or default values
  printf("Loaded Roll Angle: %d\r\n", baseline_roll_angle);
  printf("Loaded Pitch Angle: %d\r\n", baseline_pitch_angle);
  printf("Loaded Raise Collision Sensitivity: %d\r\n", raise_collision_current_security_margin);
  printf("Loaded Lower Collision Sensitivity: %d\r\n", lower_collision_current_security_margin);
#endif



  // Reset level status to not interfere with collision detection
  reset_level_data();
}



/*******************************************************************************
********************************************************************************
 *** App ticking function
 *******************************************************************************
*******************************************************************************/
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
/***************************************************************************//**
 * Power Manager sleep veto (SiSDK 2026.6 migration).
 *
 * The stock "Bluetooth - SoC Empty" base project pulls in the Power Manager
 * component, whose sl_power_manager_sleep() call in main()'s super loop puts
 * the CPU to sleep between iterations. The old Gecko SDK 4.2.2 project had no
 * Power Manager, so its super loop free-spun and run_motor_supervision() ran
 * back-to-back at the ~1 ms/tick cadence the current supervision is calibrated
 * around (100-sample warmup; 350-sample dynamic threshold "occurs within
 * 303ms"). Under Power Manager that cadence collapsed, slowing every
 * current-based safety function equally: limit-switch inference (is_motor_
 * moving), dynamic collision-threshold calibration, and collision response.
 *
 * Returning false here vetoes sleep - sl_power_manager_sleep() then returns
 * without entering WFI, so the CPU stays in EM0 and the super loop free-spins,
 * exactly as the pre-migration build did. Gated on a motor being energized so
 * we only hold EM0 during a move; idle sleep is unaffected. Stateless (reads
 * motor_state each call) so there is no add/remove requirement to keep paired.
 * Overrides the SL_WEAK stub in autogen/sl_power_manager_handler.c.
 ******************************************************************************/
bool app_is_ok_to_sleep(void)
{
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    if (motor_state[i].motor_on) {
      return false;  // A motor is running - keep the supervision loop fast.
    }
  }

  // The switch sampler is polled from the main loop (10 ms cadence), so a
  // sampling session needs the loop awake (DECISIONS.md #41).
  if (switch_sampler_active()) {
    return false;
  }

  // The motor-enable shadow, not just motor_state[]: set_all_motors_on()
  // energises the outputs without setting motor_state[].motor_on (#40), so a
  // runaway could previously let the CPU sleep between fail-safe samples - the
  // suspected cause of the ALL MOTORS fail-safe taking 271-307 ms against
  // 214-223 ms for the HEAD pair. The shadow is conservative (true until a
  // successful write leaves all four enables low), and needs no I2C.
  if (motor_outputs_maybe_on()) {
    return false;
  }

  // A tilt telemetry burst also needs the loop (DECISIONS.md #33). The
  // sampler runs from app_process_action(), so without this a standalone
  // refresh - app connects, or REFRESH_TILT arrives, with the bed sitting
  // still - would arm a burst and then have no loop to run on: the CPU
  // sleeps, no passes happen, the filter never fills, nothing updates.
  //
  // This pins EM0 for TILT_TELEMETRY_BURST_MS. It is bounded (the burst
  // always expires), and it can only ever happen while a phone is subscribed
  // - tilt_telemetry_tick() will not arm a burst otherwise - so an idle bed
  // with no app connected sleeps exactly as it does today.
  if (tilt_telemetry_burst_active()) {
    return false;
  }

  return true;
}
#endif // SL_CATALOG_POWER_MANAGER_PRESENT

void app_process_action(void)
{
  bool local_heartbeat;

  // If there is a collision --> stop all motors, then flash all LEDs a few times
  // Once this sequence is done (which will take a few seconds), then collision can be
  // set back to false and motor operation can resume

/*******************************************************************************
 *** Collision Detection
 ******************************************************************************/
  // If "collision_check" function in "motor_current_functions.c" detects a collision...
  if (any_collision){
    sl_sleeptimer_is_timer_running(&LED_500ms_timer, &is_LED_timer_running);

    // Note: motors are shut off in "collision_check" function. All functions that are supervised with the "any_collision" flag are suspended until the flag is cleared.
    // The code below does the following:
    // (1) Start LED blink sequence to notify user of a collision
    // (2) Reverse motor direction to alleviate pressure
    // (3) Turn off motors, LEDs, and reset active switch and "any_collision" flags
    if (!is_LED_timer_running){
      active_switch_during_collision = active_switch;
      auto_level_active = false;
      auto_level_stop_blink_LED(LED_RED);
      start_500ms_LED_timer();
      LED_blink_counter = 0;
#if !BED_CAPTURE_MOTOR_CURRENTS
      printf("Once\r\n");
#endif

      set_all_LED_on();
    }
    if (LED_timer_timeout){
      toggle_all_LED();
      LED_timer_timeout = false;
      LED_blink_counter++;
      if (LED_blink_counter == 2){
        reverse_motor_direction_and_start(active_switch_during_collision);
      }
    }
    if (LED_blink_counter >= COLLISION_BLINK_CYCLES){
      set_all_motors_off();
      sl_sleeptimer_stop_timer(&LED_500ms_timer);
      set_all_LED_off();
      if (app_lights_on()){
        set_LED_on(LED_AMBER);
      }
      active_switch = OFF;
      any_collision = false;

    }
  }

/*******************************************************************************
*** Auto Level
*******************************************************************************/

  if ((auto_level_active) && (!any_collision) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
      accel_running_average(); // Accel data is always being read, filtered, and averaged during auto leveling
      auto_level_blink_LED(LED_RED);

      if (first_check){ // Check level on both axes
          steady_accel_read_both_axes();
          if (first_print[0]){
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("Step 1: ROLL and PITCH check\r\n");
#endif
              first_print[0] = false;
          }
          if (axis_disabled[ROLL]){
              auto_level_current_axis = PITCH;
          }
      }
      else if (((axis_disabled[ROLL]) && (axis_disabled[PITCH]))){ // No leveling required, exit immediately
          auto_level_active = false;
          auto_level_stop_blink_LED(LED_RED);
          set_LED_on(LED_GREEN);
          if (first_print[3]){
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("Bed is disabled, no leveling required\r\n");
#endif
              first_print[3] = false;
          }
      }
      else if ((!level_status[auto_level_current_axis].level) && (!axis_disabled[auto_level_current_axis])){ // Steady accel read is obtained, so if current axis is not level continue with leveling
          if (first_print[1]){
#if !BED_CAPTURE_MOTOR_CURRENTS
              if (auto_level_current_axis == ROLL){
                    printf("Step 2: ROLL Not Level\r\n");
              } else {
                    printf("Step 2: PITCH Not Level\r\n");
              }
#endif
              first_print[1] = false;
          }

          auto_level_single_axis(auto_level_current_axis); // Checks level based on accel read, sets motor directions, and levels current axis

      } else if (!any_collision) { // Current axis is now level, switch to next axis after brief pause
          // If level or disabled or a combination of the two, exit auto-level
          if ((level_status[ROLL].level && level_status[PITCH].level) ||
              (axis_disabled[ROLL] && level_status[PITCH].level) ||
              (axis_disabled[PITCH] && level_status[ROLL].level)) {
              // Turn off all motors and reset all motors status
              set_all_motors_off();
              for (int8_t i=0; i < NUM_MOTORS; i++){
                motor_state[i].motor_on = false;
              }
              auto_level_active = false;
              auto_level_stop_blink_LED(LED_RED);
              set_LED_on(LED_GREEN);
#if !BED_CAPTURE_MOTOR_CURRENTS
              printf("Exit auto-level\r\n");
              if (bed_level){
                  printf("Bed is level\r\n");
              }
              else if (axis_disabled[ROLL] && level_status[PITCH].level){
                  printf("ROLL is disabled and Pitch is level\r\n");
              }
              else if (axis_disabled[PITCH] && level_status[ROLL].level){
                  printf("PITCH is disabled and ROLL is level\r\n");
              }
#endif
          } else {

            // Otherwise, switch axis and level
            sl_sleeptimer_is_timer_running(&auto_level_timer, &is_auto_level_timer_running);
            if (!is_auto_level_timer_running){
                start_auto_level_oneshot_timer();
            }
            if (auto_level_timer_timeout){
                // If timer ends and bed is level, bed is disabled, or a combination of one axis being disabled and the other is level --> exit auto_level

                if (auto_level_current_axis == ROLL){
                    auto_level_current_axis = PITCH;
#if !BED_CAPTURE_MOTOR_CURRENTS
                    printf("Axis set to PITCH\r\n");
#endif
                }
                else if (auto_level_current_axis == PITCH){
                    auto_level_current_axis = ROLL;
#if !BED_CAPTURE_MOTOR_CURRENTS
                    printf("Axis set to ROLL\r\n");
#endif
                }
            }
         }
      }
  }


/*******************************************************************************
*** "Settings" Mode
*** For adjusting Custom Level Angle and Collision Sensitivity
*******************************************************************************/

  settings_mode_tick();

  /*******************************************************************************
  *** Switch Input
  *** Double Press & Button Release Check, then UI interrupt/status dispatch
  *******************************************************************************/

  switch_input_tick();

  sl_atomic_load(local_heartbeat, heartbeat);

  if (local_heartbeat) {

    // Toggle LED for heartbeat
    //sl_led_toggle(&sl_led_local_led);

    // Reset the flag
    local_heartbeat = false;
    sl_atomic_store(heartbeat, local_heartbeat);
  }

  // Unconditional runaway guard (DECISIONS.md #38). Trusts a direct hardware
  // read and nothing else - deliberately not active_switch, switch_held,
  // switch_driving or any table match, because those are what desync.
  motion_failsafe_tick();

  sample_motor_faults_tick();

  // Live tilt for the app (BLE_GATT_DESIGN.md #6, DECISIONS.md #33). Both
  // gates are passed in explicitly rather than read inside the sampler:
  //   - nothing sampled unless a phone is actually subscribed, so a
  //     switch-only bed is bit-for-bit unchanged;
  //   - skipped while auto-level or settings-mode's zero-level capture is
  //     running, because those already call accel_running_average() every
  //     pass and bed_status.c ships their value in those states.
  tilt_telemetry_tick(ble_bed_service_status_subscribed(),
                      auto_level_active || in_settings_mode);

  bed_control_tick();
  ble_bed_service_tick();
}

/**************************************************************************//**
 * Bluetooth stack event handler.
 * This overrides the default weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt)
{
  // All BLE stack event handling and GATT glue for the Tilt Bed Service
  // lives in ble_bed_service.c (BLE_GATT_DESIGN.md #11) - this stays a
  // thin forwarder so the bed application code above is untouched by BLE.
  ble_bed_service_on_event(evt);
}
