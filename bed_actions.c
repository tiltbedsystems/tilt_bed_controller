/*
 * bed_actions.c
 *
 * Relocated from app.c on C4 (Jul 2026) - see bed_actions.h. active_switch,
 * is_raising, auto_level_active, and first_print[] stay defined in app.c
 * (extern via app.h) - process_outputs() and app.c's own tick logic
 * read/write them just as much as these functions do, so moving them here
 * would have meant rewriting that logic instead of just relocating this one.
 * in_settings_mode moved to settings_mode.c on C4 commit 3 (extern via
 * settings_mode.h). Only active_move_is_all and bedlights_on are exclusively
 * this module's own state.
 */

#include "bed_actions.h"

#include "app.h"
#include "auto_level.h"
#include "debug_capture.h"
#include "dual_motor_control.h"
#include "exp_board.h"
#include "motor_current_functions.h"
#include "settings_mode.h"
#include "switch_input.h"

/*
 * Tracks whether the in-progress move is an ALL-type move, independent of
 * active_switch's live value. app_move_stop() must call the same release
 * helper today's per-case dispatch always calls for that move type - but
 * active_switch can race to OFF within the same tick (the double-press-error
 * handler clears it before process_inputs() runs later in the same
 * app_process_action() call), so re-inspecting active_switch at stop time
 * would sometimes call the wrong helper (missing all_motors_button_released()'s
 * temp-off-pause flag reset) in that race window. This flag is set
 * unconditionally on every app_move_start() and isn't subject to that race.
 */
static bool active_move_is_all = false;

static bool bedlights_on = false; // Tracks if bedlights are turned on/off

static bool double_check = false; // Used to double check level only one time

// Shared handling for a single-side motor button press
static void motor_button_pressed(active_switch_t switch_value, side_of_bed_t side, bool up)
{
  active_switch = switch_value;
  is_raising = up;
  initial_switch_press = true;
  if (up) {
    set_dual_motors_direction_up(side);
  } else {
    set_dual_motors_direction_down(side);
  }
  if (dynamic_collision_detection_enabled){
      reset_ignore_data_points_count();
      reset_motor_current_data();
  }
  app_reset_double_press_timer();
}

// Shared handling for a single-side motor button release
static void motor_button_released(void)
{
  for (uint8_t i=0; i < NUM_MOTORS; i++){
      motor_state[i].motor_on = false;
  }
  if (dynamic_collision_detection_enabled){
      reset_ignore_data_points_count();
      reset_motor_current_data();
  }
  app_reset_double_press_timer();
}

// Shared handling for an all-motors (UP_ALL/DOWN_ALL) button press
static void all_motors_button_pressed(bool up)
{
  is_raising = up;
  initial_switch_press = true;
  if (up) {
    set_all_motors_direction_up();
  } else {
    set_all_motors_direction_down();
  }
  for (uint8_t i=0; i < NUM_MOTORS; i++){
      motor_state[i].motor_on = true;
      motor_state[i].motor_up = up;
  }
  if (dynamic_collision_detection_enabled){
      reset_ignore_data_points_count();
      reset_motor_current_data();
  }
  // For temp limit switch pause
  set_motors_off_once = false;
  temp_motor_off_timer_timeout = false;
  motors_temp_off = false;
  app_reset_double_press_timer();
}

// Shared handling for an all-motors (UP_ALL/DOWN_ALL) button release
static void all_motors_button_released(void)
{
  for (uint8_t i=0; i < NUM_MOTORS; i++){
      motor_state[i].motor_on = false;
  }
  if (dynamic_collision_detection_enabled){
      reset_ignore_data_points_count();
      reset_motor_current_data();
  }
  // For temp limit switch pause
  set_motors_off_once = false;
  temp_motor_off_timer_timeout = false;
  motors_temp_off = false;
  app_reset_double_press_timer();
}

void app_move_start(bed_target_t target, bed_direction_t dir)
{
  bool up = (dir == BED_DIR_UP);
  active_move_is_all = (target == BED_TARGET_ALL);
  capture_reset_timer();
  switch (target) {
    case BED_TARGET_HEAD:
      motor_button_pressed(up ? SWITCH_UP_HEAD_VALUE : SWITCH_DOWN_HEAD_VALUE, HEAD, up);
      break;
    case BED_TARGET_LEFT:
      motor_button_pressed(up ? SWITCH_UP_LEFT_VALUE : SWITCH_DOWN_LEFT_VALUE, LEFT, up);
      break;
    case BED_TARGET_RIGHT:
      motor_button_pressed(up ? SWITCH_UP_RIGHT_VALUE : SWITCH_DOWN_RIGHT_VALUE, RIGHT, up);
      break;
    case BED_TARGET_FOOT:
      motor_button_pressed(up ? SWITCH_UP_FOOT_VALUE : SWITCH_DOWN_FOOT_VALUE, FOOT, up);
      break;
    case BED_TARGET_ALL:
      active_switch = up ? SWITCH_UP_ALL_VALUE : SWITCH_DOWN_ALL_VALUE;
      all_motors_button_pressed(up);
      break;
  }
}

void app_move_stop(void)
{
  if (active_move_is_all) {
    all_motors_button_released();
  } else {
    motor_button_released();
  }
}

void app_auto_level_start(void)
{
  capture_reset_timer();
  active_switch = SWITCH_AUTO_LEVEL_VALUE;
  initial_switch_press = true;
  auto_level_active = true;
  first_check = true;
  for (uint8_t i=0; i < 10; i++){
      first_print[i] = true;
  }
  for (uint8_t i=0; i < NUM_AXES; i++){
      leveling_success[i] = false;
  }
  double_check = false;
  auto_level_motors_on = false;
  auto_level_current_axis = ROLL; // Start with ROLL
  app_reset_auto_level_timer();
  is_raising = true; // To prevent accidentally tripping on lower collision security margin immediately following auto-level
  reset_level_data();
  for (uint8_t i=0; i < NUM_MOTORS; i++){
      motor_state[i].motor_on = false;
  };
  if (dynamic_collision_detection_enabled){
      reset_ignore_data_points_count();
      reset_motor_current_data();
  }
  app_reset_double_press_timer();
}

void app_auto_level_stop(void)
{
  auto_level_active = false;
  app_reset_auto_level_timer();
  reset_level_data();
  auto_level_stop_blink_LED(LED_RED);
  set_all_LED_off();
  for (uint8_t i=0; i < NUM_MOTORS; i++){
      motor_state[i].motor_on = false;
  };
  if (dynamic_collision_detection_enabled){
      reset_ignore_data_points_count();
      reset_motor_current_data();
  }
  app_reset_double_press_timer();
}

void app_lights_set(bool on)
{
  active_switch = SWITCH_BED_LIGHT_VALUE;
  initial_switch_press = true;
  bedlights_on = on;
  app_reset_double_press_timer();
}

// Full clean stop: whichever of auto-level or movement was active gets its
// own natural stop, then motors/LEDs are forced off and active_switch reset
// regardless - there is no switch-release event driving process_outputs()
// in this path, so this function must finish the job itself.
void app_stop_all(void)
{
  if (auto_level_active) {
    app_auto_level_stop();
  } else if (active_switch != OFF) {
    app_move_stop();
  }
  set_all_motors_off();
  for (uint8_t i=0; i < NUM_MOTORS; i++){
      motor_state[i].motor_on = false;
  }
  set_all_LED_off();
  active_switch = OFF;
}

/*******************************************************************************
 *** Output application for APP-sourced commands (BLE Stage C)
 *
 * The switch path gets its physical outputs from switch_input.c's
 * process_outputs(), which is driven by app_ui_interrupt + switch_pressed -
 * i.e. a real switch edge. An app command has neither, so bed_move_start()
 * and friends latch direction and active_switch but NOTHING ever energises
 * the motors or lights the LEDs. These helpers do what the matching branch of
 * process_outputs() does, and ble_bed_service.c calls them right after the
 * corresponding bed_control verb reports success.
 *
 * They deliberately live here rather than in the BLE module: this is where
 * the switch path's equivalents live, so the two stay side by side and a
 * change to one is obvious next to the other.
 ******************************************************************************/

static side_of_bed_t target_to_side(bed_target_t target)
{
  switch (target) {
    case BED_TARGET_HEAD:  return HEAD;
    case BED_TARGET_LEFT:  return LEFT;
    case BED_TARGET_RIGHT: return RIGHT;
    case BED_TARGET_FOOT:  return FOOT;
    case BED_TARGET_ALL:   break; // caller handles ALL separately
  }
  return HEAD; // unreachable
}

// Mirrors process_outputs()'s press branches: LED_BLUE + all motors for an
// ALL move, LED_GREEN + the one pair otherwise.
void app_apply_move_outputs(bed_target_t target)
{
  if (target == BED_TARGET_ALL) {
    set_LED_on(LED_BLUE);
    set_all_motors_on();
  } else {
    set_LED_on(LED_GREEN);
    set_dual_motors_on(target_to_side(target));
  }
}

// Mirrors process_outputs()'s SWITCH_AUTO_LEVEL press branch. The auto-level
// loop in app.c drives the motors and the blinking LED from here on.
void app_apply_auto_level_start_outputs(void)
{
  set_LED_on(LED_RED);
  set_all_motors_off();
}

// Mirrors process_outputs()'s SWITCH_BED_LIGHTS branch, then returns the bed
// to idle.
void app_apply_lights_outputs(void)
{
  if (bedlights_on) {
    set_LED_on(LED_AMBER);
    set_underbed_lighting_on();
  } else {
    set_LED_off(LED_AMBER);
    set_underbed_lighting_off();
  }
  app_release_active_switch();
}

// Mirrors process_outputs()'s RELEASE branches: motors and LEDs off, bed
// back to idle. Used for app MOVE_STOP and AUTO_LEVEL_STOP.
void app_apply_stop_outputs(void)
{
  set_all_motors_off();
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    motor_state[i].motor_on = false;
  }
  set_all_LED_off();
  if (bedlights_on) {
    set_LED_on(LED_AMBER); // under-bed lighting survives a move, as it does a collision
  }
  app_release_active_switch();
}

// No switch-release event will ever arrive for an app command, so the app
// path has to return active_switch to OFF itself. This is not cosmetic:
// app_bed_is_idle() is what lets bed_control release the source lock, so
// without it a single app command would leave APP holding the lock forever
// and the wall switch permanently force-stopped (DECISIONS.md #12).
void app_release_active_switch(void)
{
  active_switch = OFF;
}

// Full stop of MOVEMENT only, leaving the under-bed lighting and its amber
// indicator alone (DECISIONS.md #36).
//
// This is what a two-source conflict uses. app_stop_all() clears every panel
// LED including amber, which for a conflict is wrong: the under-bed lights
// are a latched setting the user chose separately, and a movement conflict
// plunging the bed into darkness is a surprise, not a feature. The lighting
// output itself was never touched by app_stop_all() - only its indicator -
// so restoring the indicator is all that is needed to keep the two agreeing.
//
// STOP_ALL still calls app_stop_all() directly and keeps its full behaviour.
void app_stop_movement(void)
{
  app_stop_all();
  if (bedlights_on) {
    set_LED_on(LED_AMBER);
  }
}

bool app_bed_is_idle(void)
{
  return (active_switch == OFF) && (!in_settings_mode);
}

active_switch_t app_active_command(void)
{
  return active_switch;
}

bool app_in_settings_mode(void)
{
  return in_settings_mode;
}

bool app_lights_on(void)
{
  return bedlights_on;
}
