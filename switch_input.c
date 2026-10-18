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

#include "em_gpio.h"
#include "sl_atomic.h"
#include "sl_simple_button_instances.h"
#include "sl_simple_button_ui_int_config.h"
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

// Switch sampler timing (DECISIONS.md #41). Milliseconds via sleeptimer, never
// loop passes (#2).
#define SWITCH_SAMPLE_MS            10
#define SWITCH_DEBOUNCE_SAMPLES     3   // 3 identical reads = >=20 ms stable
#define SWITCH_READ_FAIL_LIMIT      5   // 50 ms of failed reads -> all released
#define SWITCH_STATE_STALE_MS       100 // fail-safe distrusts older data
#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0

// Every SWITCH bit of the packed Port0/Port1 status word, and nothing else.
// Port 0 is all eight single-side switches; Port 1 bits 0-3 are UP_ALL,
// AUTO_LEVEL, DOWN_ALL and BED_LIGHTS. Bits 4-7 of Port 1 are the LED enable
// OUTPUTS and are deliberately excluded - see the fail-safe below.
#define SWITCH_INPUT_MASK 0xFF0F

/*
 * BENCH-ONLY RUNAWAY PROVOCATION - MUST NEVER BE 1 IN A NORMAL BUILD.
 *
 * Same convention as BLE_BENCH_WIPE_BONDS in ble_bed_service.c: a hardcoded,
 * source-visible flag that has to be hand-flipped for a bench session and
 * hand-flipped back afterwards. Default 0.
 *
 * When 1, tapping and RELEASING a physical button deliberately energises a
 * motor pair ~300 ms later, with no button held and no source commanding -
 * i.e. it manufactures exactly the runaway the motion fail-safe exists to
 * catch, so the fail-safe can be tested in the only direction that matters.
 * Until this has been run, "the fail-safe would catch a runaway" is an
 * assumption with no evidence behind it.
 *
 *   tap UP HEAD  -> provokes the HEAD pair   (set_dual_motors_on)
 *   tap UP ALL   -> provokes all four motors (set_all_motors_on)
 *
 * Two different calls on purpose: the single-side and all-motor paths energise
 * differently, so both need proving.
 *
 * It energises through the SAME calls the real paths use, so the fail-safe is
 * tested against a genuinely energised bed rather than a simulation of one.
 *
 * MUST be defined here, above process_inputs() - the trigger sites are in it.
 * Defining it further down (next to the fail-safe, where it reads more
 * naturally) silently compiles the trigger sites out, and the provocation
 * does nothing at all. That happened; -Wunused-function caught it.
 *
 * WHY IT MUST NEVER SHIP: a production unit with this enabled would drive a
 * motor pair with nothing held, every time a button was tapped, relying on the
 * fail-safe as its only stop. That is a bed that moves on its own - the exact
 * behaviour this whole subsystem exists to prevent.
 */
#define BED_BENCH_PROVOKE_RUNAWAY 0

// How long after the button release to energise. Long enough that the release
// has been fully processed and every variable is back to idle, so the
// fail-safe faces a real "nothing is commanding" state rather than a race with
// the release path. Milliseconds via sleeptimer (DECISIONS #2).
#define PROVOKE_DELAY_MS 300

// Flags
bool switch_pressed = false; // Interrupt flag to capture switch up/down state - extern via switch_input.h

/*
  I2C GPIO Expander Port 0 and Port 1 packed status variables
  status = ((uint16_t) (ports[0] << 8)) | ports[1]; (see ui_read_inputs())

  app_ui_interrupt and the sampler's "down" masks share the same bit mapping

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

uint16_t app_ui_interrupt; // The ONE switch bit of the edge being delivered - extern via switch_input.h

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

// Number of switches in a "down" mask (1 = physically down). Anything outside
// SWITCH_INPUT_MASK is ignored.
static uint8_t count_switches_down(uint16_t down)
{
  uint8_t n = 0;
  down &= SWITCH_INPUT_MASK;
  while (down != 0) {
    n += (uint8_t)(down & 1u);
    down >>= 1;
  }
  return n;
}

/*******************************************************************************
 *** Switch sampler (DECISIONS.md #41) - the ONE reader of the UI input ports
 *
 * Reading an expander's input port clears its interrupt flag, so every extra
 * reader races the others for the edge. Three code paths used to read it (the
 * dispatch, the double-press check and the fail-safe) and a two-button release
 * was dropped as a result. Now only this sampler reads (ui_read_inputs()), and
 * everything else - the event dispatch, the double-press check, the fail-safe -
 * uses the debounced state it keeps.
 *
 * A "session" starts when the UI_INT line asserts (edge flag, or a level check
 * so a missed edge still starts one) and ends when everything is released and
 * stable. During a session the off-board re-drivers are held on, and both UI
 * boards are read every SWITCH_SAMPLE_MS and debounced per bit. Edges are
 * delivered to process_inputs() one bit at a time, releases first.
 *
 * Everything is in "down" polarity: down = ~status & SWITCH_INPUT_MASK.
 ******************************************************************************/

#if SWITCH_SAMPLER_DIAG && !BED_CAPTURE_MOTOR_CURRENTS
#define SW_DIAG(...) printf(__VA_ARGS__)
#else
#define SW_DIAG(...) ((void)0)
#endif

static bool     sampler_active = false;       // session running, re-drivers held
static volatile bool ui_wake_request = false; // set by sl_button_on_change
static bool     aux_present = false;
static bool     switch_present = false;
static uint16_t deb_down = 0;                 // debounced down-mask
static uint8_t  pending_count[16];            // per-bit consecutive-differ counter
static uint8_t  bounce_count[16];             // per-bit bounce tally for diag
static uint8_t  read_fail_run = 0;
static uint32_t next_sample_tick = 0;
static uint32_t last_good_sample_tick = 0;
static uint16_t pending_release = 0;          // accepted edges to deliver
static uint16_t pending_press = 0;

// UI_INT is the expanders' active-low interrupt output. Level check only, no I2C.
static bool ui_int_asserted(void)
{
  return GPIO_PinInGet(SL_SIMPLE_BUTTON_UI_INT_PORT, SL_SIMPLE_BUTTON_UI_INT_PIN) == 0;
}

static uint16_t status_to_down(uint16_t status)
{
  return (uint16_t)(~status) & SWITCH_INPUT_MASK;
}

// One transaction per board that is not currently believed present. The result
// is only used to decide presence; the next sample reads the real state.
static void sampler_probe_missing_boards(void)
{
  uint16_t status;
  if (!aux_present) {
    aux_present = ui_read_inputs(UI_AUX_I2C_BUS_ADDRESS, &status);
  }
  if (!switch_present) {
    switch_present = ui_read_inputs(UI_SWITCH_I2C_BUS_ADDRESS, &status);
  }
}

// Baseline read at boot. Call from app_init() right after init_ui_expander().
void switch_sampler_init(void)
{
  uint16_t status;
  uint16_t down = 0;
  bool no_wake = false;

  sl_atomic_store(ui_wake_request, no_wake);
  ui_redriver_acquire();

  aux_present = ui_read_inputs(UI_AUX_I2C_BUS_ADDRESS, &status);
  if (aux_present) {
    down |= status_to_down(status);
  }
  switch_present = ui_read_inputs(UI_SWITCH_I2C_BUS_ADDRESS, &status);
  if (switch_present) {
    down |= status_to_down(status);
  }

  // No events are generated for what is found here. A button held at power-up
  // is therefore never acted on: its press is never delivered, so switch_held
  // is never set to its value, and when it is later released the release finds
  // switch_held != its value and does nothing (process_inputs() only acts on a
  // release whose switch_held matches).
  deb_down = down;
  last_good_sample_tick = sl_sleeptimer_get_tick_count();

  // This baseline read also cleared any interrupt pending from boot.
  if (deb_down != 0) {
    sampler_active = true; // keep the re-driver hold
    next_sample_tick = sl_sleeptimer_get_tick_count();
  } else {
    ui_redriver_release();
  }
}

static void switch_sampler_tick(void)
{
  uint32_t now = sl_sleeptimer_get_tick_count();
  bool wake;

  if (!sampler_active) {
    sl_atomic_load(wake, ui_wake_request);
    if (!wake && !ui_int_asserted()) {
      return;
    }
    ui_redriver_acquire();
    sampler_probe_missing_boards();
    sampler_active = true;
    next_sample_tick = now;
  }

  if ((int32_t)(now - next_sample_tick) < 0) {
    return;
  }
  uint32_t period = sl_sleeptimer_ms_to_tick(SWITCH_SAMPLE_MS);
  next_sample_tick += period;
  if ((int32_t)(now - next_sample_tick) >= 0) {
    next_sample_tick = now + period; // more than one period behind: resync
  }

  // Clear BEFORE the read: an interrupt after the read sets it again and keeps
  // the session alive.
  bool no_wake = false;
  sl_atomic_store(ui_wake_request, no_wake);

  uint16_t raw = 0;
  uint16_t status;
  bool any_ok = false;
  bool skip_debounce = false;
  bool any_present = aux_present || switch_present;

  if (aux_present) {
    if (ui_read_inputs(UI_AUX_I2C_BUS_ADDRESS, &status)) {
      raw |= status_to_down(status);
      any_ok = true;
    } else {
      SW_DIAG("SW: aux board read failed\r\n");
    }
  }
  if (switch_present) {
    if (ui_read_inputs(UI_SWITCH_I2C_BUS_ADDRESS, &status)) {
      raw |= status_to_down(status);
      any_ok = true;
    } else {
      SW_DIAG("SW: switch board read failed\r\n");
    }
  }

  if (any_ok) {
    last_good_sample_tick = now;
    read_fail_run = 0;
  } else if (any_present) {
    // Every present board failed this sample. Hold the debounced state until
    // the limit, then fail toward OFF: all released, boards re-probed at the
    // next session start.
    if (read_fail_run < 255) {
      read_fail_run++;
    }
    if (read_fail_run >= SWITCH_READ_FAIL_LIMIT) {
      raw = 0;
      aux_present = false;
      switch_present = false;
      SW_DIAG("SW: %u failed reads - treating all switches as released\r\n",
              (unsigned)read_fail_run);
    } else {
      skip_debounce = true;
    }
  }
  // else: no board present at all -> raw stays 0 (fail toward OFF)

  if (!skip_debounce) {
    for (uint8_t b = 0; b < 16; b++) {
      uint16_t bit = (uint16_t)(1u << b);
      if ((SWITCH_INPUT_MASK & bit) == 0) {
        continue;
      }
      bool raw_b = ((raw & bit) != 0);
      bool deb_b = ((deb_down & bit) != 0);

      if (raw_b == deb_b) {
        if (pending_count[b] > 0) {
          if (bounce_count[b] < 255) {
            bounce_count[b]++;
          }
          pending_count[b] = 0;
        }
      } else {
        pending_count[b]++;
        if (pending_count[b] >= SWITCH_DEBOUNCE_SAMPLES) {
          deb_down ^= bit;
          if (raw_b) {
            pending_press |= bit;
          } else {
            pending_release |= bit;
          }
          SW_DIAG("SW: 0x%04X %s (bounces %u)\r\n", (unsigned)bit,
                  raw_b ? "down" : "up", (unsigned)bounce_count[b]);
          pending_count[b] = 0;
          bounce_count[b] = 0;
        }
      }
    }
  }

  // End the session only when everything is released AND stable AND no
  // interrupt arrived since the read.
  sl_atomic_load(wake, ui_wake_request);
  if ((deb_down == 0) && !wake) {
    bool settled = true;
    for (uint8_t b = 0; b < 16; b++) {
      if (pending_count[b] != 0) {
        settled = false;
      }
    }
    if (settled) {
      sampler_active = false;
      ui_redriver_release();
    }
  }
}

// True while a session is running (the sampler needs the main loop to run).
bool switch_sampler_active(void)
{
  return sampler_active;
}

// Debounced down-mask. Idle => 0 by construction: a session only ends with
// everything released, so idle state is valid without a read.
uint16_t switch_sampler_down(void)
{
  return deb_down;
}

// False if the sampler is running but has had no good read for
// SWITCH_STATE_STALE_MS. Consumers must then treat it as "no switch down".
bool switch_state_fresh(void)
{
  return !sampler_active ||
         (sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count() - last_good_sample_tick)
          <= SWITCH_STATE_STALE_MS);
}

#if BED_BENCH_PROVOKE_RUNAWAY
static bool     provoke_armed = false;
static bool     provoke_all = false;
static uint32_t provoke_at_tick = 0;
static uint32_t provoke_energised_tick = 0;
static bool     provoke_running = false;

// Called from process_inputs() on the release of the trigger buttons.
static void provoke_arm(bool all)
{
  provoke_armed = true;
  provoke_all = all;
  provoke_at_tick = sl_sleeptimer_get_tick_count()
                    + sl_sleeptimer_ms_to_tick(PROVOKE_DELAY_MS);
}

// Called once per tick, BEFORE motion_failsafe_tick(), so the fail-safe sees
// the energised state on one of its own sample points rather than in the same
// statement.
static void provoke_tick(void)
{
  if (!provoke_armed) {
    return;
  }
  if ((int32_t)(sl_sleeptimer_get_tick_count() - provoke_at_tick) < 0) {
    return;
  }
  provoke_armed = false;

  // The same calls the real paths use - not a simulation.
  if (provoke_all) {
    set_all_motors_on();
  } else {
    set_dual_motors_on(HEAD);
  }

  provoke_energised_tick = sl_sleeptimer_get_tick_count();
  provoke_running = true;
  printf("PROVOKE: energised %s with nothing commanding - fail-safe should stop this\r\n",
         provoke_all ? "ALL MOTORS" : "HEAD pair");
}
#endif // BED_BENCH_PROVOKE_RUNAWAY

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
#if BED_BENCH_PROVOKE_RUNAWAY
                provoke_arm(true); // bench only - all four motors
#endif
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
#if BED_BENCH_PROVOKE_RUNAWAY
                        if (motor_switch_table[t].value == SWITCH_UP_HEAD_VALUE) {
                            provoke_arm(false); // bench only - HEAD pair
                        }
#endif
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
 * It asks for the switch state and the motor hardware and nothing else: is any
 * switch down, per the sampler's debounced state (#41 - the sampler is the only
 * reader of the UI input ports, so this no longer reads them itself, and stale
 * or unknown counts as "no switch down", i.e. it fails toward OFF)? Is any
 * motor-enable line actually high (a direct read, not motor_state[] - #40)?
 * And, separately: Is the app commanding? Is auto-level running? Is settings mode active?
 * If all four are no and a motor is energised, that is a runaway by
 * definition - de-energise.
 *
 * Rate-limited to FAILSAFE_SAMPLE_MS, matching the sample_motor_faults_tick()
 * precedent: the motor-enable read is blocking I2C on the on-board expander,
 * and per-pass blocking I2C on a shared bus is DECISIONS #27/#30's
 * radio-starvation mechanism. Milliseconds via sleeptimer, never loop passes (#2).
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

// Asks the HARDWARE: is any of the four motor-enable lines high? Not
// motor_state[].motor_on - that is bookkeeping, and set_all_motors_on() sets
// the outputs without touching it, so "hardware on, flags off" was invisible
// here (DECISIONS.md #40). That is exactly the state the #38 bug class
// produces.
//
// Cost: one blocking I2C transaction on sl_i2cspm_on_board per sample, i.e.
// per FAILSAFE_SAMPLE_MS = 50 ms, and ONLY once the switch check below has
// passed (so never while any button is down). Same order as
// sample_motor_faults_tick() (5/s, same bus, same context), and it is
// main-loop context, not a Bluetooth stack callback, so it is not the
// #27/#30 mechanism.
//
// A FAILED read counts as "energised": this function cannot see the motors,
// and a blind fail-safe should fail toward OFF. That cannot fight normal
// motion on one transient error, because the caller already requires
// FAILSAFE_CONFIRM_SAMPLES consecutive samples and has already established
// that nothing is commanding - so the worst case of acting on a bad read is
// de-energising motors that were meant to be off.
static bool any_motor_energised_now(void)
{
  uint8_t enables = 0;
  if (!get_motor_enables(&enables)) {
    return true;
  }
  return (enables != 0);
}

void motion_failsafe_tick(void)
{
#if BED_BENCH_PROVOKE_RUNAWAY
  provoke_tick(); // bench only - manufactures the runaway this must catch
#endif

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

  // The sampler's debounced switch state - deliberately not active_switch,
  // switch_held or any other bookkeeping, and no I2C read of its own (#41).
  // Only SWITCH_INPUT_MASK bits are ever in it: the LED bits (P1_4-P1_7) are
  // outputs, and they are exactly what made the double-press check depend on
  // something other than the switches (#35/#38). Stale or unknown state counts
  // as no switch down: this function must fail toward OFF.
  bool any_switch_down = switch_state_fresh() &&
                         (switch_sampler_down() != 0);

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

  // Leave NO desynced bookkeeping behind. A fail-safe that stops the motors
  // and strands the state has converted a runaway into the stuck state this
  // project has already hit twice (#38 additions).
  //
  //  - The switches' half of the #36 latch: this function has just
  //    established by direct read that no switch is down, so the panel is
  //    definitively not commanding. Without this the latch could stay set
  //    forever, because clearing switch_held above means a later release
  //    event finds no match and never reaches bed_move_stop().
  //  - Inferred limits: every other stop path clears these (#37). A runaway
  //    is precisely the case where motors ran unsupervised, so any inference
  //    made during it is worthless.
  //
  // app_move_active / app_auto_level_active need no clearing - the fail-safe
  // only runs when ble_bed_service_app_motion_active() is already false.
  // bed_control's active_source self-releases on the next bed_control_tick()
  // now that app_bed_is_idle() is true.
  bed_note_source_released(BED_SOURCE_SWITCHES);
  reset_limit_switch_flags();

  if (!failsafe_tripped) {
    failsafe_tripped = true;
#if !BED_CAPTURE_MOTOR_CURRENTS
    printf("FAILSAFE: motors energised with nothing commanding - stopped\r\n");
#endif
#if BED_BENCH_PROVOKE_RUNAWAY
    // Measured, not assumed: energise -> de-energise, in milliseconds.
    if (provoke_running) {
      provoke_running = false;
      uint32_t elapsed_ms = sl_sleeptimer_tick_to_ms(
                              sl_sleeptimer_get_tick_count() - provoke_energised_tick);
      printf("PROVOKE: fail-safe caught it after %lu ms\r\n", (unsigned long)elapsed_ms);
    }
#endif
  }
}

// Called whenever there is a change in switch or expander interrupt state.
// Flag only: no I2C, nothing else. The sampler (switch_sampler_tick()) starts a
// session on it. Only the asserting (PRESSED = UI_INT low) edge matters; the
// release edge is the expander clearing its own flag after the sampler's read.
void sl_button_on_change(const sl_button_t *handle)
{
  bool local_wake;

  // Will need to expand logic to handle motor fault as well

  if (sl_button_get_state(handle) == SL_SIMPLE_BUTTON_PRESSED) {
    if (&sl_button_ui_int == handle) { // Test for UI expander switch change
        local_wake = true;
        sl_atomic_store(ui_wake_request, local_wake);
    }
  }
}

// Hands one accepted edge to the existing single-bit dispatch.
static void deliver_switch_edge(uint16_t bit, bool pressed)
{
  app_ui_interrupt = bit;
  switch_pressed = pressed;
  process_inputs();
}

// Runs the double-press check, then the switch sampler, then delivers the
// debounced edges the sampler accepted to process_inputs(). Called once per
// tick from app_process_action().
void switch_input_tick(void)
{
  /****
   * Double Press check
   ****/

  if ((active_switch != OFF) && !any_collision && (!auto_level_active) && (!in_settings_mode)){
      if (initial_switch_press){
          scan_all_currents();
          initial_switch_press = false;
      }
      run_motor_supervision(active_switch, is_raising);

      // The double-press check below is a PHYSICAL SWITCH PANEL consistency
      // check and must only run on a move the panel actually started.
      //
      // An APP move sets active_switch through the same
      // motor_button_pressed() the switch path uses, and no physical button is
      // down during one, so the gate keeps this check off app moves (it
      // misfired on them before - the 10 Hz stutter, #35/#38).
      //
      // run_motor_supervision() above is deliberately OUTSIDE this gate:
      // collision and limit supervision must cover app moves too.
      bool switch_sourced_move = (bed_active_source() == BED_SOURCE_SWITCHES);

      // A double press is TWO PHYSICAL SWITCHES DOWN (DECISIONS.md #40), read
      // from the sampler's debounced state (#41) - no I2C here, so it can be
      // checked every pass instead of on a timer. Two switches down cannot be
      // produced by one button, however it bounces.
      if (switch_sourced_move && (count_switches_down(switch_sampler_down()) >= 2)){
#if !BED_CAPTURE_MOTOR_CURRENTS
          printf("Double Press Error\r\n");
          printf("  down mask 0x%04X, %u switches down\r\n",
                 (unsigned)switch_sampler_down(),
                 (unsigned)count_switches_down(switch_sampler_down()));
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
//          set_LED_off(LED_AMBER);
          /*** For "Settings Mode" ***/
          settings_mode_cancel_entry();
      }
  }

  /****
   * Switch sampler + edge delivery (DECISIONS.md #41)
   ****/

  switch_sampler_tick();

  // One process_inputs() call per bit, so process_inputs()/process_outputs()
  // and settings_mode_process_outputs() keep seeing exactly one single-bit
  // mask per call, as they always have.
  uint16_t releases = pending_release;
  uint16_t presses = pending_press;
  pending_release = 0;
  pending_press = 0;

  // Releases first, so a release always reaches bed_move_stop() before
  // anything new can start.
  for (uint8_t b = 0; b < 16; b++) {
    uint16_t bit = (uint16_t)(1u << b);
    if (releases & bit) {
      deliver_switch_edge(bit, false);
    }
  }

  if (presses != 0) {
    if (count_switches_down(deb_down) >= 2) {
      // Two or more down: no press may start anything (a second press during a
      // move is caught by the double-press check above on the next pass).
      SW_DIAG("SW: %u switches down - presses ignored\r\n",
              (unsigned)count_switches_down(deb_down));
    } else {
      for (uint8_t b = 0; b < 16; b++) {
        uint16_t bit = (uint16_t)(1u << b);
        if (presses & bit) {
          deliver_switch_edge(bit, true);
        }
      }
    }
  }
}
