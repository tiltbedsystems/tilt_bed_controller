/*
 * bed_status.c
 *
 *  Created on: Jul 4, 2026
 */

#include "bed_status.h"

#include "app.h"          // auto_level_active - see the tilt source selection below
#include "bed_actions.h"
#include "dual_motor_control.h"
#include "motor_current_functions.h"
#include "auto_level.h"
#include "exp_board.h"
#include "settings_mode.h" // in_settings_mode - same

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
  // Non-blocking: reads a value sampled in main-loop context by
  // sample_motor_faults_tick() (exp_board.c), not a live I2C read. Calling
  // get_motor_faults() directly here would block on I2CSPM_Transfer on
  // sl_i2cspm_on_board (the same bus the MC3479 accel driver uses) from
  // whatever context calls bed_status_get() - this previously included the
  // BLE stack's gatt_server_characteristic_status callback and stalled the
  // super loop long enough to drop the BLE link (bench-confirmed:
  // connection_closed reason 0x1008, supervision timeout).
  uint8_t faults = get_cached_motor_faults();

  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    out->motors[i].on = motor_state[i].motor_on;
    out->motors[i].moving_up = motor_state[i].motor_up;
    out->motors[i].at_upper_limit = at_limit_switch[i].upper;
    out->motors[i].at_lower_limit = at_limit_switch[i].lower;
    out->motors[i].fault = (faults >> i) & 0x01;
    out->motors[i].filtered_current_ma = filtered_current[i];
  }

  // TILT SOURCE SELECTION - which of the two filter chains goes on the wire.
  // Two exist by design (DECISIONS.md #31); this is the one place that picks
  // between them, and it is explicit so nobody has to infer it.
  //
  //   auto-level / settings zero-level running -> AUTO-LEVEL's value.
  //       Those paths call accel_running_average() every pass and the
  //       telemetry sampler stands down (its gate 2c), so auto-level's chain
  //       is the only one being fed. It is also the value auto-level is
  //       actually steering on, which is what the user wants to watch.
  //   otherwise, once telemetry has produced anything -> TELEMETRY's value.
  //       Live during a manual move and during a post-move settle burst.
  //   otherwise -> auto-level's last-known value.
  //       Only before the telemetry sampler has ever run: no phone had
  //       subscribed yet, or nothing has triggered a burst since boot. Same
  //       stale-last-known behaviour as before this change, which is why
  //       tilt_fresh stays hardcoded 0 (DECISIONS.md #17/#33).
  //
  // Both chains produce auto_level.c's INTERNAL sign convention, so the wire
  // negation below applies to whichever was chosen.
  int16_t roll_scaled;
  int16_t pitch_scaled;

  if (auto_level_active || in_settings_mode) {
    roll_scaled  = baseline_accel_average_roll_angle;
    pitch_scaled = baseline_accel_average_pitch_angle;
  } else if (tilt_telemetry_value_valid()) {
    roll_scaled  = tilt_telemetry_roll_scaled();
    pitch_scaled = tilt_telemetry_pitch_scaled();
  } else {
    roll_scaled  = baseline_accel_average_roll_angle;
    pitch_scaled = baseline_accel_average_pitch_angle;
  }

  out->roll_angle_scaled = roll_scaled;

  // Product convention on the wire is POSITIVE = head raised. The internal
  // value is the opposite sign (see auto_level.c's frame diagram: "If HEAD is
  // high, PITCH is NEGATIVE"), and it must stay that way - latest_pitch_angle
  // feeds check_level()/set_auto_level_motor_direction(), so flipping it
  // upstream would invert which pair auto-level drives. The wire encoding is
  // the correct boundary for the convention change. Roll is unaffected.
  //
  // This negation is the ONLY place the two conventions meet. Do not add a
  // second one anywhere downstream - ble_bed_service.c just packs the int16.
  // Negation cannot overflow: the source range is about -28500..+10500
  // (atan2 minus PITCH_OFFSET, scaled, less a baseline clamped to +/-1500 by
  // bed_settings.c), against INT16_MIN -32768.
  out->pitch_angle_scaled = -pitch_scaled;

  out->any_collision = any_collision;
  out->in_settings_mode = app_in_settings_mode();
  out->lights_on = app_lights_on();

  translate_command(app_active_command(), &out->command, &out->command_target, &out->command_direction);

  out->active_source = bed_active_source();
}
