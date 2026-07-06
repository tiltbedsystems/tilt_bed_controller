/*
 * bed_status.c
 *
 *  Created on: Jul 4, 2026
 */

#include "bed_status.h"

#include "bed_actions.h"
#include "dual_motor_control.h"
#include "motor_current_functions.h"
#include "auto_level.h"
#include "exp_board.h"

// Translates the internal active_switch_t (app.c's command decode) into the
// hardware-neutral command/target/direction fields - mirrors bed_control.h's
// own translation, so no internal enum leaks into bed_status_t.
static void translate_command(active_switch_t sw, bed_command_t *command,
                               bed_target_t *target, bed_direction_t *direction)
{
  *command = BED_COMMAND_NONE;
  *target = BED_TARGET_HEAD;   // don't-care unless command == BED_COMMAND_MOVE
  *direction = BED_DIR_UP;     // don't-care unless command == BED_COMMAND_MOVE

  switch (sw) {
    case OFF:
      *command = BED_COMMAND_NONE;
      break;
    case SWITCH_AUTO_LEVEL_VALUE:
      *command = BED_COMMAND_AUTO_LEVEL;
      break;
    case SWITCH_BED_LIGHT_VALUE:
      *command = BED_COMMAND_LIGHTS;
      break;
    case SWITCH_UP_ALL_VALUE:
      *command = BED_COMMAND_MOVE; *target = BED_TARGET_ALL; *direction = BED_DIR_UP;
      break;
    case SWITCH_DOWN_ALL_VALUE:
      *command = BED_COMMAND_MOVE; *target = BED_TARGET_ALL; *direction = BED_DIR_DOWN;
      break;
    case SWITCH_UP_HEAD_VALUE:
      *command = BED_COMMAND_MOVE; *target = BED_TARGET_HEAD; *direction = BED_DIR_UP;
      break;
    case SWITCH_DOWN_HEAD_VALUE:
      *command = BED_COMMAND_MOVE; *target = BED_TARGET_HEAD; *direction = BED_DIR_DOWN;
      break;
    case SWITCH_UP_LEFT_VALUE:
      *command = BED_COMMAND_MOVE; *target = BED_TARGET_LEFT; *direction = BED_DIR_UP;
      break;
    case SWITCH_DOWN_LEFT_VALUE:
      *command = BED_COMMAND_MOVE; *target = BED_TARGET_LEFT; *direction = BED_DIR_DOWN;
      break;
    case SWITCH_UP_RIGHT_VALUE:
      *command = BED_COMMAND_MOVE; *target = BED_TARGET_RIGHT; *direction = BED_DIR_UP;
      break;
    case SWITCH_DOWN_RIGHT_VALUE:
      *command = BED_COMMAND_MOVE; *target = BED_TARGET_RIGHT; *direction = BED_DIR_DOWN;
      break;
    case SWITCH_UP_FOOT_VALUE:
      *command = BED_COMMAND_MOVE; *target = BED_TARGET_FOOT; *direction = BED_DIR_UP;
      break;
    case SWITCH_DOWN_FOOT_VALUE:
      *command = BED_COMMAND_MOVE; *target = BED_TARGET_FOOT; *direction = BED_DIR_DOWN;
      break;
    case NUM_MOTOR_SWITCHES:
      // Sentinel value, never a real active_switch_t state.
      break;
  }
}

void bed_status_get(bed_status_t *out)
{
  uint8_t faults = get_motor_faults();

  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    out->motors[i].on = motor_state[i].motor_on;
    out->motors[i].moving_up = motor_state[i].motor_up;
    out->motors[i].at_upper_limit = at_limit_switch[i].upper;
    out->motors[i].at_lower_limit = at_limit_switch[i].lower;
    out->motors[i].fault = (faults >> i) & 0x01;
    out->motors[i].filtered_current_ma = filtered_current[i];
  }

  out->roll_angle_scaled = baseline_accel_average_roll_angle;
  out->pitch_angle_scaled = baseline_accel_average_pitch_angle;

  out->any_collision = any_collision;
  out->in_settings_mode = app_in_settings_mode();
  out->lights_on = app_lights_on();

  translate_command(app_active_command(), &out->command, &out->command_target, &out->command_direction);

  out->active_source = bed_active_source();
}
