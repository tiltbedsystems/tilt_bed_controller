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

// Accel I2C burst is rate-limited to the MC3479's actual output rate rather
// than run unconditionally on every super-loop pass. During auto-level the
// loop free-spins in EM0 (app_is_ok_to_sleep() vetoes sleep while a motor is
// on), so an ungated per-pass read burned ~60% of loop time in blocking I2C
// on sl_i2cspm_on_board - enough to starve the BLE radio past its 1000 ms
// supervision timeout on a sustained hold (connection_closed reason=0x1008).
// Root-caused and fixed by a controlled A/B bench test on this exact code
// path (DECISIONS.md #30) - not inferred, measured.
#define ACCEL_I2C_MIN_INTERVAL_MS 10 // Matches the MC3479's 100 Hz output rate (decimate-by-10, init_accel()) - sampling faster only returns duplicate data

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/
#define PI 3.14159265
#define RAD_TO_DEG (180.0 / PI)
#define PITCH_OFFSET 90

// How long one telemetry sampling burst stays open (DECISIONS #2: milliseconds
// via sleeptimer, never loop passes). At the measured ~2.31 ms/pass on the
// auto-level path that is ~200 passes, comfortably more than the 130-slot
// filter below needs to fill, so a burst always ends on a fully-converged
// average rather than a partly-filled one.
//
// BENCH-TUNABLE. The trade-off is settling quality against EM0 time: longer
// gives the filter more margin to converge and the mechanical structure more
// time to stop rocking after a move, but pins the CPU awake (see
// app_is_ok_to_sleep()) and holds the accelerometer I2C burst running for
// that whole window. Shorter risks the displayed angle freezing before the
// bed has actually settled - which is the entire point of trigger T3. Only
// ever runs while a phone is subscribed.
#define TILT_TELEMETRY_BURST_MS 500

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

// All the state one accel filter chain needs: LPF accumulator, the
// NUM_ACCEL_READINGS-deep circular buffer, its running sums and index, and
// the outputs. There are TWO independent instances - auto-level's and
// telemetry's - and they must stay independent.
//
// DECISIONS.md #31. The algorithm is deliberately shared (one filter, no
// drift between what auto-level acts on and what the app displays); the
// STORAGE is deliberately not. A decimated telemetry sampler writing
// auto-level's buffer would stretch its 130 slots from ~300 ms to seconds,
// and check_level() would then average pre-press history into the first
// direction decision of an auto-level run. That is why this is a struct
// passed by the caller and not another set of file-scope globals.
typedef struct {
  int16_t readings_roll[NUM_ACCEL_READINGS];
  int16_t readings_pitch[NUM_ACCEL_READINGS];
  int32_t sum_roll;
  int32_t sum_pitch;
  uint8_t index;
  int16_t lpf_roll;        // LPF accumulator, SCALED
  int16_t lpf_pitch;
  int16_t avg_roll;        // running average, SCALED, no baseline applied
  int16_t avg_pitch;
  int16_t baseline_roll;   // avg minus the user's stored zero-level baseline
  int16_t baseline_pitch;
} accel_filter_state_t;

// Auto-level's instance. Its outputs are mirrored into the existing globals
// below so every existing reader (check_level, returnAccelValue,
// bed_status.c) is untouched by the refactor.
static accel_filter_state_t auto_level_filter = {0};

static int16_t accel_average_roll_angle = 0; // Average value of roll SCALED VALUE
static int16_t accel_average_pitch_angle = 0; // Average value of pitch SCALED VALUE
int16_t baseline_accel_average_roll_angle = 0; // Average value of roll, deviation from user's level baseline - read by bed_status.c for telemetry
int16_t baseline_accel_average_pitch_angle = 0; // Average value of pitch, deviation from user's level baseline - read by bed_status.c for telemetry
static int16_t abs_baseline_roll_angle = 0;
static int16_t abs_baseline_pitch_angle = 0;



// (the circular-buffer index moved into accel_filter_state_t - each filter
//  chain now owns its own)


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

  // Read raw data from accel, rate-limited to ACCEL_I2C_MIN_INTERVAL_MS
  // (see comment above). Between real reads, reuse the cached accel_vector[]
  // from the last one - the sensor hasn't produced a new sample yet, so this
  // is not stale data, it's the same sample a same-rate poll would have
  // returned. Everything below still runs every call at full loop rate on
  // whichever sample is current.
  static uint32_t last_accel_i2c_tick = 0;
  uint32_t now = sl_sleeptimer_get_tick_count();
  if (sl_sleeptimer_tick_to_ms(now - last_accel_i2c_tick) >= ACCEL_I2C_MIN_INTERVAL_MS) {
    last_accel_i2c_tick = now;
    accel_get_xyz_raw(&accel_vector[0]);
  }
  x_accel_vector = accel_vector[0];
  y_accel_vector = accel_vector[1];
  z_accel_vector = accel_vector[2];

  // Calculate vector magnitude
  accel_vector_mag = sqrt(x_accel_vector * x_accel_vector + y_accel_vector * y_accel_vector + z_accel_vector * z_accel_vector);

  // Calculate roll and pitch angles
  //
  // PITCH SIGN IS DELIBERATE - DO NOT "FIX" IT HERE.
  // This produces NEGATIVE pitch when the HEAD is raised (see the frame
  // diagram below set_auto_level_motor_direction()). That is the internal
  // convention, it is fixed by how the accelerometer is mounted, and two
  // things depend on it:
  //
  //   1. Auto-level's direction logic. latest_pitch_angle feeds the running
  //      average -> check_level(PITCH) -> level_status[PITCH].positive ->
  //      set_auto_level_motor_direction(), which decides whether HEAD or FOOT
  //      drives up. Negating here inverts that - the bed would level away
  //      from level. Safety-relevant.
  //   2. Every zero-level baseline already stored in NVM3, on every bed
  //      built. settings_mode.c stores returnAccelValue(PITCH) raw, in this
  //      convention. Negating here would silently invalidate all of them and
  //      there is no migration path for units already in the field.
  //
  // The product-facing convention (POSITIVE = head raised) is applied ONCE,
  // at the wire boundary, in bed_status.c's bed_status_get(). That is the
  // only place the two conventions meet. Roll is unaffected either way.
  latest_roll_angle = (float)(atan2(x_accel_vector, accel_vector_mag)) * (RAD_TO_DEG);
  latest_pitch_angle = (float)(atan2(y_accel_vector,z_accel_vector)) * (RAD_TO_DEG) - PITCH_OFFSET;

  // Convert to scaled integers
  latest_roll_angle_scaled = (int16_t)(latest_roll_angle * SCALE_FACTOR);
  latest_pitch_angle_scaled = (int16_t)(latest_pitch_angle * SCALE_FACTOR);

  // NOTE: the low-pass filter used to live here, writing the file-scope
  // latest_*_filtered. It moved into accel_filter_update() so that each
  // caller filters into its OWN state (DECISIONS #31) - read_accel() is now
  // stateless with respect to filtering, and does nothing but produce
  // latest_*_scaled for whichever chain called it. Same arithmetic, same
  // order, same per-pass rate; auto-level's numbers are unchanged.
}

// One pass of the shared filter chain: LPF the current sample, push it
// through the caller's circular buffer, recompute the caller's average and
// baseline-adjusted outputs. Reads only latest_*_scaled (whatever
// read_accel() last produced) and the user's stored baseline; writes nothing
// but *st.
//
// This is a line-for-line lift of what accel_running_average() did inline
// before, with each global replaced by the equivalent field of *st. Nothing
// about the arithmetic changed.
static void accel_filter_update(accel_filter_state_t *st)
{
  // Apply low-pass filter to the raw accel reading
  st->lpf_roll  = accel_low_pass_filter(latest_roll_angle_scaled,  st->lpf_roll,  accel_alpha);
  st->lpf_pitch = accel_low_pass_filter(latest_pitch_angle_scaled, st->lpf_pitch, accel_alpha);

  // Subtract value in index position from sum and add latest value
  st->sum_roll  = st->sum_roll  - st->readings_roll[st->index]  + st->lpf_roll;
  st->sum_pitch = st->sum_pitch - st->readings_pitch[st->index] + st->lpf_pitch;

  // Update readings arrays
  st->readings_roll[st->index]  = st->lpf_roll;
  st->readings_pitch[st->index] = st->lpf_pitch;

  // Calculate the running average
  st->avg_roll  = st->sum_roll  / NUM_ACCEL_READINGS;
  st->avg_pitch = st->sum_pitch / NUM_ACCEL_READINGS;

  // Move to next current_index in a circular manner
  st->index = (st->index + 1) % NUM_ACCEL_READINGS;

  // Adjust running average angles against user set baseline
  st->baseline_roll  = st->avg_roll  - (baseline_roll_angle);
  st->baseline_pitch = st->avg_pitch - (baseline_pitch_angle);
}

void accel_running_average(){

//   VALUES ARE SCALED

      // Read latest accel data
      read_accel();

      // Filter into AUTO-LEVEL's own state. Never telemetry's.
      accel_filter_update(&auto_level_filter);

      // Mirror the outputs into the globals every existing reader already
      // uses (check_level(), returnAccelValue(), print_accel_value(),
      // bed_status.c). Keeping these as the published names is what makes
      // the state-struct refactor invisible to the rest of the file.
      latest_roll_angle_filtered  = auto_level_filter.lpf_roll;
      latest_pitch_angle_filtered = auto_level_filter.lpf_pitch;
      accel_average_roll_angle    = auto_level_filter.avg_roll;
      accel_average_pitch_angle   = auto_level_filter.avg_pitch;
      baseline_accel_average_roll_angle  = auto_level_filter.baseline_roll;
      baseline_accel_average_pitch_angle = auto_level_filter.baseline_pitch;
}

/*******************************************************************************
 *** Tilt telemetry sampler (BLE_GATT_DESIGN.md #6, DECISIONS.md #17/#31/#33)
 *
 * Keeps the app's tilt readout live outside auto-level, which is the only
 * thing that sampled the accelerometer before this. Structurally a sibling of
 * exp_board.c's sample_motor_faults_tick(): called once per
 * app_process_action(), main-loop context only, never from a stack callback.
 *
 * ONE sampling window. While it is open the sampler runs every pass, the same
 * rate auto-level's sampler runs at, so the filter above behaves identically
 * in both. The window is open when a motor is energised OR a burst timer is
 * running.
 *
 * Four things arm the burst. Arming is idempotent and EXTENDS a running burst
 * (restart_timer, so bursts never stack):
 *   T1  status_subscriber_count 0 -> 1 (app opens or reconnects)
 *       - armed in ble_bed_service.c's update_status_subscriber_count()
 *   T2  a REFRESH_TILT command arrives - armed from the main-loop command
 *       handler in ble_bed_service.c, never from the stack callback
 *   T3  motors transition any-on -> all-off, below. This is the one that
 *       makes the number the user finally reads a SETTLED one: while motors
 *       run, the accelerometer sees frame vibration and the actuators' own
 *       dynamic acceleration, so the last in-motion sample is the least
 *       trustworthy of the whole move. Auto-level's 1500 ms steady-read
 *       exists for exactly this reason.
 *   T4  auto-level - nothing to add, its own sampler already covers it.
 *
 * Arming COMMANDS NOTHING. It sets a timer. It cannot start, stop, or delay a
 * motor, and a movement starting mid-burst is not a conflict - the window is
 * already open, it simply stays open, and T3 re-arms when the move ends.
 * There is deliberately no arbitration between this and the motion path.
 ******************************************************************************/

static accel_filter_state_t telemetry_filter = {0};

static sl_sleeptimer_timer_handle_t tilt_burst_timer;
static volatile bool tilt_burst_running = false;
static bool tilt_telemetry_valid = false; // has this sampler ever produced a value?
static bool tilt_prev_motors_on = false;  // for the T3 edge

static void tilt_burst_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;
  tilt_burst_running = false;
}

// Idempotent by construction: restart_timer on an already-running burst moves
// its expiry out to a full TILT_TELEMETRY_BURST_MS from now, which is the
// "extend, don't stack" behaviour we want. Safe on a never-started handle -
// same pattern as restart_status_notify_timer() in ble_bed_service.c.
void tilt_telemetry_arm_burst(void)
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(TILT_TELEMETRY_BURST_MS);
  tilt_burst_running = true;
  sl_sleeptimer_restart_timer(&tilt_burst_timer, ticks, tilt_burst_timer_callback, NULL, 0, 0);
}

bool tilt_telemetry_burst_active(void)
{
  return tilt_burst_running;
}

bool tilt_telemetry_value_valid(void)
{
  return tilt_telemetry_valid;
}

int16_t tilt_telemetry_roll_scaled(void)
{
  return telemetry_filter.baseline_roll;
}

int16_t tilt_telemetry_pitch_scaled(void)
{
  return telemetry_filter.baseline_pitch;
}

static bool any_motor_energised(void)
{
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    if (motor_state[i].motor_on) {
      return true;
    }
  }
  return false;
}

// status_subscribed and other_sampler_active are passed in rather than read
// here so this file keeps no dependency on the BLE layer or on app.c's mode
// flags, and so both gates are visible at the call site in
// app_process_action().
void tilt_telemetry_tick(bool status_subscribed, bool other_sampler_active)
{
  bool motors_on = any_motor_energised();

  // T3 edge. Tracked unconditionally so tilt_prev_motors_on is always
  // accurate - otherwise a phone subscribing mid-move would see a spurious
  // stop edge on the first tick after subscribing.
  bool motors_just_stopped = (tilt_prev_motors_on && !motors_on);
  tilt_prev_motors_on = motors_on;

  // Gate 2a. No subscriber, no sampler, and no burst either - a switch-only
  // bed with no phone connected must stay bit-for-bit what it was, including
  // never vetoing sleep. This is what keeps the existing regression baseline
  // valid for that case, so the T3 arm below sits AFTER this return.
  if (!status_subscribed) {
    return;
  }

  if (motors_just_stopped) {
    tilt_telemetry_arm_burst(); // T3
  }

  // Gate 2c. Auto-level and the settings zero-level capture already run
  // accel_running_average() every pass; running this as well would put two
  // samplers on the bus per pass and halve the effective averaging window in
  // time. Auto-level's value is what bed_status.c ships in that state anyway.
  if (other_sampler_active) {
    return;
  }

  // Gate 2b: is the sampling window open?
  if (!motors_on && !tilt_burst_running) {
    return;
  }

  read_accel();
  accel_filter_update(&telemetry_filter);
  tilt_telemetry_valid = true;
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

// NOTE ON SIGN, for the commented-out CSV lines below as well as the live
// prints: every pitch value printed from this file is in the INTERNAL
// convention (head raised => NEGATIVE), not the wire convention the app shows
// (head raised => POSITIVE, applied in bed_status.c). If the CSV-style
// printfs below are ever re-enabled for a tilt capture, the resulting log
// will disagree in sign with the app for the same physical bed position.
// Negate on the analysis side, or note the convention in the capture header -
// do not "correct" it here (see the warning at read_accel()).
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
