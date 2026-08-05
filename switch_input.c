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
#include "ble_bed_service.h"
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

// Every SWITCH bit of the packed Port0/Port1 status word, and nothing else.
// Port 0 is all eight single-side switches; Port 1 bits 0-3 are UP_ALL,
// AUTO_LEVEL, DOWN_ALL and BED_LIGHTS. Bits 4-7 of Port 1 are the LED enable
// OUTPUTS and are deliberately excluded - see the fail-safe below.
#define SWITCH_INPUT_MASK 0xFF0F

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

/*
 * PHYSICAL PANEL STATE - owned exclusively by this file (DECISIONS.md #37).
 *
 * These exist because active_switch could not answer "is a physical button
 * currently held?". It is the COMMAND variable, written by whichever source
 * is driving the bed, so an app move set it through the same
 * motor_button_pressed() the panel uses. Two readers here needed the panel
 * question and asked the command variable instead:
 *
 *   - process_inputs()'s press guard, which skipped every physical press
 *     while the app was moving, so the press never reached bed_control and
 *     no conflict was ever detected;
 *   - process_outputs()'s press/release pairing, which could be energised by
 *     an app-written value and left unpaired when the app changed it.
 *
 *   switch_held    which physical button is physically down, set on press and
 *                  cleared on release REGARDLESS of whether the command was
 *                  accepted - so the release always reaches bed_control and
 *                  releases this source's half of the #36 latch.
 *   switch_driving the same value, but only when bed_control ACCEPTED the
 *                  press. process_outputs energises and de-energises on this
 *                  one, so a refused press never drives a motor and an
 *                  accepted one is always paired with its own release.
 */
static active_switch_t switch_held = OFF;
static active_switch_t switch_driving = OFF;

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
  // Discard any expiry that already fired. Stopping a timer must also drop
  // its pending result, or the flag survives to be consumed at an arbitrary
  // later moment (DECISIONS.md #38).
  double_press_error_timer_timeout = false;
}

// Clears ALL of the panel's own state together. The double-press handler used
// to clear active_switch alone, which was correct before the #37 split when
// active_switch WAS all three - afterwards it left switch_held and
// switch_driving stale, which is bench observations 1 and 2 (#38).
static void clear_switch_panel_state(void)
{
  switch_held = OFF;
  switch_driving = OFF;
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
          if ((switch_pressed) && (switch_driving == SWITCH_UP_ALL_VALUE)){
              set_LED_on(LED_BLUE);
              set_all_motors_on();
          } else if ((!switch_pressed) && (switch_driving == SWITCH_UP_ALL_VALUE)){
              set_LED_off(LED_BLUE);
              set_all_motors_off();
              switch_driving = OFF;
              active_switch = OFF;
          }
          break;

      case SWITCH_AUTO_LEVEL:
          if ((switch_pressed) && (switch_driving == SWITCH_AUTO_LEVEL_VALUE)){
              set_LED_on(LED_RED);
              set_all_motors_off();
          } else if ((!switch_pressed) && (switch_driving == SWITCH_AUTO_LEVEL_VALUE)){
              set_all_motors_off();
              auto_level_stop_blink_LED(LED_RED);
              switch_driving = OFF;
              active_switch = OFF;
          }
          break;

      case SWITCH_DOWN_ALL:
          if ((switch_pressed) && (switch_driving == SWITCH_DOWN_ALL_VALUE)){
              set_LED_on(LED_BLUE);
              set_all_motors_on();
          } else if ((!switch_pressed) && (switch_driving == SWITCH_DOWN_ALL_VALUE)){
              set_LED_off(LED_BLUE);
              set_all_motors_off();
              switch_driving = OFF;
              active_switch = OFF;
          }
          break;

      case SWITCH_BED_LIGHTS:
          if ((switch_pressed) && (switch_driving == SWITCH_BED_LIGHT_VALUE)){
              if (app_lights_on()){
                  set_LED_on(LED_AMBER);
                  set_underbed_lighting_on();}
              else {
                  set_LED_off(LED_AMBER);
                  set_underbed_lighting_off();
              }
          } else if ((!switch_pressed) && (switch_driving == SWITCH_BED_LIGHT_VALUE)){
              switch_driving = OFF;
              active_switch = OFF;
          }
          break;

    default: {
        for (uint8_t t = 0; t < NUM_MOTOR_SWITCH_TABLE_ENTRIES; t++) {
            if (app_ui_interrupt == motor_switch_table[t].mask) {
                if ((switch_pressed) && (switch_driving == motor_switch_table[t].value)){
                    set_LED_on(LED_GREEN);
                    set_dual_motors_on(motor_switch_table[t].side); // Motor statuses are set in "set_dual_motors_on/off()" function
                } else if ((!switch_pressed) && (switch_driving == motor_switch_table[t].value)){
                    set_LED_off(LED_GREEN);
                    set_all_motors_off();
                    switch_driving = OFF;
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

      /*
       * A press may only drive a motor if bed_control accepts THIS press
       * (DECISIONS.md #38). switch_driving is cleared at the start of every
       * press event and re-set below only when the verb returns true, so a
       * stale value can never survive into process_outputs' press arm.
       *
       * Without this, a desynced switch_driving left the panel able to
       * energise motors for a button whose bed_move_start() was never even
       * called - motion with arbitration bypassed entirely, which is what
       * bench observation 2 was: every other button dead because switch_held
       * was stale, and the SAME button still moving the bed because
       * switch_driving was.
       */
      if (switch_pressed) {
          switch_driving = OFF;
      }

      switch (app_ui_interrupt) // Configure controls based on activated switch
      {
        // Every press below is gated on switch_held, NOT active_switch
        // (DECISIONS.md #37). The old guard asked "is the bed idle?" when it
        // meant "is a physical button already down?", so while the app was
        // moving the bed every physical press was silently discarded and no
        // conflict was ever reported.
        case SWITCH_UP_ALL:
            if (switch_pressed && (switch_held == OFF)){
                switch_held = SWITCH_UP_ALL_VALUE;
                if (bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_ALL, BED_DIR_UP)) {
                    switch_driving = SWITCH_UP_ALL_VALUE;
                }
            } else if ((!switch_pressed) && (switch_held == SWITCH_UP_ALL_VALUE)){
                switch_held = OFF;
                bed_move_stop(BED_SOURCE_SWITCHES);
            }
            break;

        case SWITCH_AUTO_LEVEL:
            if (switch_pressed && (switch_held == OFF)){
                switch_held = SWITCH_AUTO_LEVEL_VALUE;
                if (bed_auto_level_start(BED_SOURCE_SWITCHES)) {
                    switch_driving = SWITCH_AUTO_LEVEL_VALUE;
                }
            } else if ((!switch_pressed) && (switch_held == SWITCH_AUTO_LEVEL_VALUE)){
                switch_held = OFF;
                bed_auto_level_stop(BED_SOURCE_SWITCHES);
            }
            break;

        case SWITCH_DOWN_ALL:
            if (switch_pressed && (switch_held == OFF)){
                switch_held = SWITCH_DOWN_ALL_VALUE;
                if (bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_ALL, BED_DIR_DOWN)) {
                    switch_driving = SWITCH_DOWN_ALL_VALUE;
                }
            } else if ((!switch_pressed) && (switch_held == SWITCH_DOWN_ALL_VALUE)){
                switch_held = OFF;
                bed_move_stop(BED_SOURCE_SWITCHES);
            }
            break;

        case SWITCH_BED_LIGHTS:
            if (switch_pressed && (switch_held == OFF)){
                switch_held = SWITCH_BED_LIGHT_VALUE;
                if (bed_lights_set(BED_SOURCE_SWITCHES, !app_lights_on())) {
                    switch_driving = SWITCH_BED_LIGHT_VALUE;
                }
            } else if ((!switch_pressed) && (switch_held == SWITCH_BED_LIGHT_VALUE)){
                switch_held = OFF;
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
                    if (switch_pressed && (switch_held == OFF)) {
                        switch_held = motor_switch_table[t].value;
                        if (bed_move_start(BED_SOURCE_SWITCHES,
                                           side_to_target(motor_switch_table[t].side),
                                           motor_switch_table[t].up ? BED_DIR_UP : BED_DIR_DOWN)) {
                            switch_driving = motor_switch_table[t].value;
                        }
                    } else if ((!switch_pressed) && (switch_held == motor_switch_table[t].value)) {
                        switch_held = OFF;
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

/*******************************************************************************
 *** Motion fail-safe (DECISIONS.md #38)
 *
 * THE BED MUST NEVER MOVE WHILE NOTHING IS COMMANDING IT. Every other check in
 * this file trusts some piece of bookkeeping - active_switch, switch_held,
 * switch_driving, app_ui_interrupt, a table match. This one trusts none of
 * them, because they are precisely the things that desync: bench observation 1
 * was motors energised with active_switch == OFF, which also switches off
 * run_motor_supervision() and leaves nothing that would ever stop them.
 *
 * It asks the hardware directly and nothing else: is any switch physically
 * down? Is the app commanding? Is auto-level running? Is settings mode active?
 * If all four are no and a motor is energised, that is a runaway by
 * definition - de-energise.
 *
 * Rate-limited to FAILSAFE_SAMPLE_MS, matching the sample_motor_faults_tick()
 * precedent: a direct read is blocking I2C on the UI expander, and per-pass
 * blocking I2C on a shared bus is DECISIONS #27/#30's radio-starvation
 * mechanism. Milliseconds via sleeptimer, never loop passes (#2).
 *
 * It requires FAILSAFE_CONFIRM_SAMPLES consecutive quiet samples before
 * acting. That is what stops it fighting the normal paths: the window between
 * a press being registered and its outputs applied is a single tick, far
 * shorter than the confirm window, and in that window the switch IS physically
 * down so the first test already fails. The confirm count only guards against
 * a torn read at the exact moment of a release.
 ******************************************************************************/

#define FAILSAFE_SAMPLE_MS        50
#define FAILSAFE_CONFIRM_SAMPLES  3   // ~150 ms of agreement before acting

static uint32_t failsafe_last_sample_tick = 0;
static uint8_t  failsafe_quiet_samples = 0;
static bool     failsafe_tripped = false; // print once, not every pass

static bool any_motor_energised_now(void)
{
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    if (motor_state[i].motor_on) {
      return true;
    }
  }
  return false;
}

void motion_failsafe_tick(void)
{
  uint32_t now = sl_sleeptimer_get_tick_count();
  if (sl_sleeptimer_tick_to_ms(now - failsafe_last_sample_tick) < FAILSAFE_SAMPLE_MS) {
    return;
  }
  failsafe_last_sample_tick = now;

  // Auto-level legitimately drives motors with no button held in the panel
  // sense, and settings mode drives them from its own logic. Neither is a
  // runaway, so neither is counted as quiet.
  if (auto_level_active || in_settings_mode || ble_bed_service_app_motion_active()) {
    failsafe_quiet_samples = 0;
    failsafe_tripped = false;
    return;
  }

  // DIRECT hardware read - deliberately not any cached flag. Switches are
  // active low with pull-ups, so a switch bit reading 0 means that button is
  // physically down. The LED bits (P1_4-P1_7) are masked out: they are
  // outputs, and they are exactly what made the double-press check depend on
  // something other than the switches (#35/#38).
  uint16_t status = get_ui_status();
  bool any_switch_down = ((status & SWITCH_INPUT_MASK) != SWITCH_INPUT_MASK);

  if (any_switch_down) {
    failsafe_quiet_samples = 0;
    failsafe_tripped = false;
    return;
  }

  if (!any_motor_energised_now()) {
    failsafe_quiet_samples = 0;
    failsafe_tripped = false;
    return;
  }

  if (failsafe_quiet_samples < FAILSAFE_CONFIRM_SAMPLES) {
    failsafe_quiet_samples++;
    return;
  }

  // Nothing is commanding motion and a motor is on. De-energise.
  set_all_motors_off();
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    motor_state[i].motor_on = false;
  }
  set_LED_off(LED_GREEN);
  set_LED_off(LED_BLUE);
  set_LED_off(LED_RED);
  clear_switch_panel_state();
  active_switch = OFF;

  if (!failsafe_tripped) {
    failsafe_tripped = true;
#if !BED_CAPTURE_MOTOR_CURRENTS
    printf("FAILSAFE: motors energised with nothing commanding - stopped\r\n");
#endif
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

          // Double press error check timer. Not started for an app-sourced
          // move - there is no second physical button to catch, and the
          // check below is gated on the same condition anyway. Belt and
          // braces: this also stops the periodic timer being left running
          // across a source change.
          if (bed_active_source() == BED_SOURCE_SWITCHES){
              sl_sleeptimer_is_timer_running(&double_press_error_timer, &is_double_press_error_timer_on);
              if (!is_double_press_error_timer_on){
                  start_double_press_error_timer();
              }
          }
      }
      run_motor_supervision(active_switch, is_raising);

      // The double-press check below is a PHYSICAL SWITCH PANEL consistency
      // check and must only run on a move the panel actually started.
      //
      // It compares the switch-board status latched at the last physical
      // switch interrupt against a fresh read, and calls them a double press
      // if they match. That status word includes the LED enable bits
      // (P1_4-P1_7), which is what normally makes a legitimate physical hold
      // differ from its own latched value - the LED for that move turns on
      // after the latch.
      //
      // An APP move sets active_switch through the same
      // motor_button_pressed() the switch path uses, so before this gate the
      // check ran on app moves too. With no physical button pressed, the
      // fresh read matched the stale latched value whenever the app's LED
      // happened to be the one that was lit at the last physical interrupt -
      // firing a bogus "Double Press Error" that killed the motors and set
      // active_switch = OFF, which the app's next 100 ms MOVE_START repeat
      // then undid. That was the 10 Hz stutter, and the LED bits are why it
      // only affected whichever app command type was in use during the
      // conflict (green = single side, blue = ALL, red = auto-level).
      //
      // run_motor_supervision() above is deliberately OUTSIDE this gate:
      // collision and limit supervision must cover app moves too.
      bool switch_sourced_move = (bed_active_source() == BED_SOURCE_SWITCHES);

      // The flag is consumed UNCONDITIONALLY; only the check itself is gated
      // (DECISIONS.md #38). Putting switch_sourced_move in the same condition
      // as the clear - which is what #35 did - meant the flag went stale
      // whenever the source was not SWITCHES, and a stale flag is then
      // consumed at an arbitrary later moment instead of 50 ms after the
      // latch. This check is only sound at that 50 ms point, because what
      // makes a legitimate single press differ from its own latched status is
      // the LED bits changing in between. Run it at any other time and it
      // compares a steady state against an edge, and matches whenever the LED
      // happens to line up. That is the misfire.
      if (double_press_error_timer_timeout){
          double_press_error_timer_timeout = false;

        if (switch_sourced_move){

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
              // Clear the panel's state TOGETHER with active_switch (#38).
              // Clearing active_switch alone left switch_held naming a button
              // nobody was holding - so every other button failed its
              // `switch_held == OFF` press test and went dead - and left
              // switch_driving naming a button process_outputs would still
              // energise on, which is motion with bed_control never consulted.
              clear_switch_panel_state();
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
