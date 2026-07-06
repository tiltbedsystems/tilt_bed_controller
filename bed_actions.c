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
