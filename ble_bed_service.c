/*
 * ble_bed_service.c
 *
 * See ble_bed_service.h. Advertising bring-up, bonding, per-connection
 * CONTROL/VIEWER role tracking, Bed Status telemetry, and - as of Stage C
 * (DECISIONS.md #34) - the full Command characteristic write path: all seven
 * opcodes of BLE_GATT_DESIGN.md #4, the #5 dead-man watchdog, and #14 role
 * arbitration.
 *
 * This module decodes and arbitrates; it does not reimplement bed logic.
 * Every opcode routes through the existing bed_control.c verbs with
 * BED_SOURCE_APP, the same verbs a physical switch press calls, so the
 * two-source lock (DECISIONS #12) and the collision/limit supervision
 * beneath it (#13) apply to app commands automatically.
 *
 * TWO RULES THAT MUST SURVIVE ANY EDIT HERE:
 *   1. The stack callback does NO work - it copies bytes, sets a flag, and
 *      returns. Everything else happens in main-loop context. Every opcode
 *      touches I2C (motors and LEDs are behind an expander), and blocking
 *      I2C inside a Bluetooth callback is the confirmed mechanism behind
 *      this project's reason=0x1008 supervision-timeout drops
 *      (DECISIONS #27/#30).
 *   2. STOP_ALL never shares the deferred-command slot. See its flag below.
 *
 *  Created on: Jul 12, 2026
 */

#include "ble_bed_service.h"

#include <stdio.h>
#include <string.h>

#include "app_assert.h"
#include "gatt_db.h"
#include "sl_sleeptimer.h"

#include "auto_level.h"              // tilt_telemetry_arm_burst() - triggers T1 and T2
#include "bed_actions.h"             // app_stop_all(), the app-path output helpers
#include "bed_control.h"             // the command verbs every opcode routes through
#include "motor_current_functions.h" // any_collision - cancels the move watchdog

// Security-lifecycle diagnostic logging (bonding/connection state prints
// below, at sl_bt_evt_sm_bonded/sm_bonding_failed/sm_confirm_bonding/
// connection_opened/connection_closed). Originally added to diagnose the
// fresh-pairing bring-up bug; kept on rather than removed now that pairing
// is bench-confirmed working (62773f6 / this session), because each print
// fires once per connection/bonding event - not per tick, so it carries
// none of the timing-loop cost this session's other findings are about -
// and reason/status codes here (e.g. connection_closed's 0x1008) are the
// most useful diagnostic for exactly the class of BLE-drop bug that has
// already bitten this project once. Set to 0 for a quiet production log.
#define BLE_DEBUG_SECURITY_LOG 1

// Bench-only, gated OFF by default (matches debug_capture.h's
// BED_CAPTURE_MOTOR_CURRENTS convention - a hardcoded, source-visible flag
// that must be hand-flipped for a bench session, never left on by accident).
// When 1, wipes the BLE bonding table on every boot so bring-up testing
// always starts from an empty bond table, regardless of what accumulated in
// NVM3 across reflashes (NVM3 survives a reflash; nothing before this
// normally called sl_bt_sm_delete_bondings(), so stale firmware-side bonds
// were accumulating while the phone side kept getting cleared - see the
// AUTH FAIL bench findings). sl_bt_sm_delete_bondings() only touches the
// Bluetooth stack's own bonding database - a distinct NVM3 object from the
// application's settings storage (nvm3_default_config.h / bed_settings.c /
// nvm3_functions.c use their own separate keys) - so this cannot disturb
// calibration/settings data.
// NEVER leave enabled (1) in a normal build - a production unit must keep
// customer bonds across a reset, exactly like it keeps their settings.
#define BLE_BENCH_WIPE_BONDS 0

/*******************************************************************************
 *** Bed Status wire encoding (BLE_GATT_DESIGN.md #6)
 ******************************************************************************/

void ble_bed_service_encode_status(const bed_status_t *in, bool tilt_fresh, uint8_t *out)
{
  memset(out, 0, gattdb_bed_status_len);

  uint8_t flags = 0;
  if (in->any_collision)    flags |= 0x01;
  if (in->in_settings_mode) flags |= 0x02;
  if (in->lights_on)        flags |= 0x04;
  if (tilt_fresh)            flags |= 0x08;
  out[0] = flags;

  // bed_source_t / bed_command_t / bed_target_t / bed_direction_t enum
  // values already match the wire encoding 1:1 (BLE_GATT_DESIGN.md #6) - no
  // translation table needed, just a direct cast.
  out[1] = (uint8_t)in->active_source;
  out[2] = (uint8_t)in->command;

  uint8_t detail = (uint8_t)in->command_target & 0x07;
  if (in->command_direction == BED_DIR_UP) {
    detail |= 0x08;
  }
  out[3] = detail;

  out[4] = (uint8_t)((uint16_t)in->roll_angle_scaled & 0xFF);
  out[5] = (uint8_t)(((uint16_t)in->roll_angle_scaled >> 8) & 0xFF);
  out[6] = (uint8_t)((uint16_t)in->pitch_angle_scaled & 0xFF);
  out[7] = (uint8_t)(((uint16_t)in->pitch_angle_scaled >> 8) & 0xFF);

  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    uint8_t status = 0;
    if (in->motors[i].on)             status |= 0x01;
    if (in->motors[i].moving_up)      status |= 0x02;
    if (in->motors[i].at_upper_limit) status |= 0x04;
    if (in->motors[i].at_lower_limit) status |= 0x08;
    if (in->motors[i].fault)          status |= 0x10;

    uint16_t current_ma = (in->motors[i].filtered_current_ma > 0xFFFF)
                             ? 0xFFFF : (uint16_t)in->motors[i].filtered_current_ma;

    uint8_t base = 8 + (i * 3);
    out[base]     = status;
    out[base + 1] = (uint8_t)(current_ma & 0xFF);
    out[base + 2] = (uint8_t)((current_ma >> 8) & 0xFF);
  }
}

/*******************************************************************************
 *** Connection / role tracking
 *
 * Two logical slots (CONTROL, VIEWER - BLE_GATT_DESIGN.md #8/#14), assigned
 * once a connection finishes bonding (sl_bt_evt_sm_bonded), not at raw
 * connection-open: every characteristic already requires Bonded, so nothing
 * is reachable pre-bond regardless, and waiting for bonded confirmation
 * keeps a connection that opens but never bonds from occupying a role slot.
 ******************************************************************************/

#define ROLE_CONTROL 0
#define ROLE_VIEWER  1
#define SLOT_EMPTY   0xff

#define BLE_BED_SERVICE_MAX_BONDS 8 // DECISIONS.md #19

// 30 ms, in 0.625 ms units (BLE_GATT_DESIGN.md #8/#22).
#define ADV_INTERVAL_UNITS 48

// 15-30 ms interval (1.25 ms units), latency 0, 1 s supervision timeout
// (BLE_GATT_DESIGN.md #8/#22) - link-loss safety is the Stage C watchdog,
// not this timeout.
#define CONN_MIN_INTERVAL        12
#define CONN_MAX_INTERVAL        24
#define CONN_LATENCY              0
#define CONN_SUPERVISION_TIMEOUT 100

// Adaptive Bed Status notify cadence (BLE_GATT_DESIGN.md #6).
#define STATUS_NOTIFY_IDLE_MS   1000
#define STATUS_NOTIFY_MOVING_MS  100

// Command characteristic wire format (BLE_GATT_DESIGN.md #4): 4 bytes,
// { opcode, arg0, arg1, seq }.
#define CMD_LEN            4
#define CMD_OFS_OPCODE     0
#define CMD_OFS_ARG0       1
#define CMD_OFS_ARG1       2
#define CMD_OFS_SEQ        3

// Opcodes (BLE_GATT_DESIGN.md #4). All implemented as of Stage C.
#define CMD_OPCODE_MOVE_START        0x01
#define CMD_OPCODE_MOVE_STOP         0x02
#define CMD_OPCODE_AUTO_LEVEL_START  0x03
#define CMD_OPCODE_AUTO_LEVEL_STOP   0x04
#define CMD_OPCODE_LIGHTS_SET        0x05
#define CMD_OPCODE_STOP_ALL          0x06
#define CMD_OPCODE_REFRESH_TILT      0x07

// Command Result codes (BLE_GATT_DESIGN.md #7).
#define CMD_RESULT_ACCEPTED                   0x00
#define CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE 0x01
#define CMD_RESULT_REJECTED_VIEWER            0x02
#define CMD_RESULT_REJECTED_SETTINGS_MODE     0x03
#define CMD_RESULT_REJECTED_INVALID           0x04
#define CMD_RESULT_WATCHDOG_STOP              0x05
#define CMD_RESULT_LEN                        2

// Dead-man watchdog (BLE_GATT_DESIGN.md #5). Milliseconds via sleeptimer,
// never loop passes (DECISIONS.md #2). The app repeats MOVE_START - and, as
// of DECISIONS #36, AUTO_LEVEL_START - every 100 ms while its button is held,
// so 300 ms tolerates two lost writes before stopping the bed.
//
// This same period does double duty as the app's "has stopped commanding"
// fallback for the two-source inhibit (#36): if no motion repeat arrives for
// MOVE_WATCHDOG_MS, the app is treated as having released, which covers a
// lost MOVE_STOP write.
#define MOVE_WATCHDOG_MS 300

static uint8_t advertising_set_handle = 0xff;

static uint8_t open_connections[2] = { SLOT_EMPTY, SLOT_EMPTY };
static uint8_t control_connection = SLOT_EMPTY;
static uint8_t viewer_connection  = SLOT_EMPTY;

static bool control_status_subscribed = false;
static bool viewer_status_subscribed  = false;
static uint8_t status_subscriber_count = 0;

static sl_sleeptimer_timer_handle_t status_notify_timer;
static volatile bool status_notify_due = false;
static uint8_t last_sent_status[gattdb_bed_status_len];

// Deferred Command write. The stack callback ONLY fills these in and returns;
// every decision and every side effect happens later, in main-loop context,
// from process_pending_command(). See the handler for why (DECISIONS #27).
//
// This slot is last-write-wins: a second write arriving before the main loop
// drains it overwrites the first. That is acceptable for every opcode EXCEPT
// STOP_ALL, because the rest are self-correcting - the app repeats MOVE_START
// every 100 ms, and a lost MOVE_STOP is caught by the watchdog within 300 ms.
static volatile bool    command_pending = false;
static volatile uint8_t pending_command[CMD_LEN];
static volatile uint8_t pending_command_len = 0;   // as written, before clamping
static volatile uint8_t pending_command_conn = SLOT_EMPTY;

// STOP_ALL gets its own flag and is NEVER routed through the shared slot.
// It is sent once, it is the app's big red button, and nothing corrects for
// it being lost. Set only in the Bluetooth handler, cleared only by the main
// loop after the stop has actually been performed - no other path may clear
// it, so no other command can displace a pending stop.
static volatile bool    stop_all_pending = false;
static volatile uint8_t stop_all_seq  = 0;
static volatile uint8_t stop_all_conn = SLOT_EMPTY;

// Control connection dropped while APP held the bed. Handled in the main
// loop, not in the disconnect callback: app_stop_all() drives LEDs over I2C
// on sl_i2cspm_on_board, and blocking I2C inside a stack callback is the
// confirmed cause of this project's reason=0x1008 drops (DECISIONS #27/#30).
static volatile bool disconnect_stop_pending = false;

// Dead-man watchdog state (BLE_GATT_DESIGN.md #5).
static sl_sleeptimer_timer_handle_t move_watchdog_timer;
static volatile bool move_watchdog_expired = false;  // set in timer ISR context
static bool    move_watchdog_armed = false;
static uint8_t last_move_seq = 0;                    // echoed on WATCHDOG_STOP

// The app-sourced move currently believed to be running, so a repeat with a
// DIFFERENT target/dir can be turned into MOVE_STOP + MOVE_START (#4).
static bool            app_move_active = false;
static bed_target_t    app_move_target = BED_TARGET_HEAD;
static bed_direction_t app_move_dir    = BED_DIR_UP;

// Auto-level is hold-to-run as of DECISIONS #36, matching the physical
// button. The app repeats AUTO_LEVEL_START every 100 ms while held, so this
// flag stops a repeat from restarting the levelling state machine -
// app_auto_level_start() resets first_check, leveling_success[] and the axis,
// so calling it ten times a second would mean levelling never progressed.
static bool app_auto_level_active = false;

// Whether the app is still asking for motion, for the two-source inhibit
// (#36). Refreshed by every MOVE_START/AUTO_LEVEL_START, INCLUDING refused
// ones: a refused repeat still means the user is holding the button, so the
// latch must stay. Cleared by an explicit stop, or by MOVE_WATCHDOG_MS of
// silence if a stop write is lost.
static bool     app_commanding = false;
static uint32_t app_command_deadline_tick = 0;

static void start_advertising(void)
{
  // Best-effort: harmless if already advertising, and legacy advertising
  // naturally refuses to start once both role slots are occupied.
  sl_status_t sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                                  sl_bt_legacy_advertiser_connectable);
  (void)sc;
}

static uint8_t open_connection_count(void)
{
  uint8_t count = 0;
  for (uint8_t i = 0; i < 2; i++) {
    if (open_connections[i] != SLOT_EMPTY) {
      count++;
    }
  }
  return count;
}

static void track_connection_opened(uint8_t connection)
{
  for (uint8_t i = 0; i < 2; i++) {
    if (open_connections[i] == SLOT_EMPTY) {
      open_connections[i] = connection;
      return;
    }
  }
}

static void track_connection_closed(uint8_t connection)
{
  for (uint8_t i = 0; i < 2; i++) {
    if (open_connections[i] == connection) {
      open_connections[i] = SLOT_EMPTY;
    }
  }
}

// Shared by sl_bt_evt_sm_bonded (fresh pairing) and sl_bt_evt_connection_
// parameters (covers a RESUMED bond too - sm_bonded's own doc says it fires
// for a pairing/bonding PROCEDURE completing, not for simply re-establishing
// encryption from an already-stored bond on reconnect, which does not repeat
// the procedure). Idempotent: a connection that already holds a role hits
// neither branch on a later call. Returns true only when this call is what
// just assigned the role (either branch taken) - callers use this to seed
// CCCD state exactly once per connection, right after the role that seed
// depends on (control vs. viewer) actually exists.
static bool assign_role_if_needed(uint8_t connection)
{
  if (control_connection == SLOT_EMPTY) {
    control_connection = connection;
    return true;
  } else if (viewer_connection == SLOT_EMPTY && connection != control_connection) {
    viewer_connection = connection;
    return true;
  }
  return false;
}

static void update_status_subscriber_count(void)
{
  uint8_t previous = status_subscriber_count;
  uint8_t count = (control_status_subscribed ? 1 : 0) + (viewer_status_subscribed ? 1 : 0);
  if (count == 0 && status_subscriber_count != 0) {
    // #24: nobody subscribed - stop the notify timer, no more radio traffic.
    sl_sleeptimer_stop_timer(&status_notify_timer);
  }
  status_subscriber_count = count;

  // T1 (DECISIONS.md #33): first subscriber arriving. Covers both a fresh
  // app open and a reconnect, because the resumed-bond CCCD seeding path
  // (af369f2) lands here too. Without this the app would show whatever tilt
  // was last computed - typically from the previous auto-level run, possibly
  // hours old - until something else happened to open the sampling window.
  // Arming only sets a timer; it commands no motion.
  if (previous == 0 && count > 0) {
    tilt_telemetry_arm_burst();
  }
}

// Notifies the Command Result characteristic (BLE_GATT_DESIGN.md #7):
// { seq, result }, echoed to the connection that issued the write.
// Main-loop context only.
static void send_command_result(uint8_t connection, uint8_t seq, uint8_t result)
{
  if (connection == SLOT_EMPTY) {
    return;
  }
  uint8_t payload[CMD_RESULT_LEN] = { seq, result };
  sl_status_t sc = sl_bt_gatt_server_send_notification(connection, gattdb_command_result,
                                                        CMD_RESULT_LEN, payload);
  (void)sc; // best-effort: no-op if the client never subscribed to results
}

/*******************************************************************************
 *** Dead-man watchdog (BLE_GATT_DESIGN.md #5)
 *
 * Armed and refreshed only by MOVE_START from the control connection. AUTO_
 * LEVEL and LIGHTS are latched commands and never arm it (#4). Cancelled by
 * MOVE_STOP, STOP_ALL, a collision, or the bed going idle on its own (a limit
 * stop). Expiry is a clean full stop, not a partial one.
 ******************************************************************************/

static void move_watchdog_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;
  move_watchdog_expired = true; // ISR context - flag only, the tick does the work
}

static void move_watchdog_arm(void)
{
  move_watchdog_armed = true;
  move_watchdog_expired = false;
  sl_sleeptimer_restart_timer(&move_watchdog_timer,
                               sl_sleeptimer_ms_to_tick(MOVE_WATCHDOG_MS),
                               move_watchdog_callback, NULL, 0, 0);
}

static void move_watchdog_cancel(void)
{
  move_watchdog_armed = false;
  move_watchdog_expired = false;
  sl_sleeptimer_stop_timer(&move_watchdog_timer);
}

// The app has issued a motion command. Called for every MOVE_START and
// AUTO_LEVEL_START, accepted or refused (#36 - see app_commanding).
static void note_app_commanding(void)
{
  app_commanding = true;
  app_command_deadline_tick = sl_sleeptimer_get_tick_count()
                              + sl_sleeptimer_ms_to_tick(MOVE_WATCHDOG_MS);
}

// The app has stopped asking for motion - an explicit stop, a disconnect, or
// the silence timeout below. Releases its half of the two-source inhibit.
static void note_app_stopped_commanding(void)
{
  app_commanding = false;
  bed_note_source_released(BED_SOURCE_APP);
}

// Main-loop context. The dead-man period doing its second job: no motion
// repeat for MOVE_WATCHDOG_MS means the app has released, even if its
// MOVE_STOP write was lost.
static void service_app_commanding(void)
{
  if (!app_commanding) {
    return;
  }
  // Signed difference so this is correct across tick-counter wraparound.
  if ((int32_t)(sl_sleeptimer_get_tick_count() - app_command_deadline_tick) >= 0) {
    note_app_stopped_commanding();
  }
}

// Main-loop context. Called every tick.
static void service_move_watchdog(void)
{
  if (!move_watchdog_armed) {
    return;
  }

  if (any_collision) {
    // The collision handler owns the stop and its LED sequence from here.
    // Nothing left for the watchdog to guard, and letting it expire into an
    // app_stop_all() would cut across that sequence.
    move_watchdog_cancel();
    app_move_active = false;
    return;
  }

  if (app_bed_is_idle() && app_move_active) {
    // The bed stopped underneath a move the app still believed it held - the
    // wall switch took it (DECISIONS #12), or a limit stop ended it. Clear
    // our belief; the two-source latch (#36) is bed_control's job now, set at
    // the moment of the conflict rather than inferred here.
    //
    // DO NOT cancel the watchdog here. It guards the LINK, not the bed: if
    // the app is still there it re-arms on the next repeat, and if the link
    // is gone it must still expire into a full stop. Cancelling it here was
    // the defect fixed in #35 - it disarmed the dead-man protection at
    // exactly the moment the bed's state was in dispute.
    app_move_active = false;
  }

  if (!move_watchdog_expired) {
    return;
  }

  // No MOVE_START repeat for MOVE_WATCHDOG_MS - the app is gone, the phone
  // slept, or the link stalled. Full clean stop, then tell the app why if it
  // is still there to hear it.
  move_watchdog_cancel();
  app_move_active = false;
  app_auto_level_active = false;   // hold-to-run auto-level is watchdogged too (#36)
  note_app_stopped_commanding();
  app_stop_all();
  send_command_result(control_connection, last_move_seq, CMD_RESULT_WATCHDOG_STOP);
}

/*******************************************************************************
 *** Command dispatch
 ******************************************************************************/

// Acts on a Command write, in MAIN-LOOP context, from ble_bed_service_tick().
//
// Everything this function does was deliberately kept out of the stack
// callback. That is not stylistic: DECISIONS.md #27/#30. Blocking work
// reached from inside a Bluetooth callback - an accel I2C read, an expander
// write to drive a motor or an LED - is precisely the mechanism that starved
// the radio past its 1000 ms supervision timeout and produced the
// reason=0x1008 drops this project has already chased twice. Every opcode
// here touches I2C, so every one of them has to be handled from the loop.
static void process_pending_command(void)
{
  // STOP_ALL first, unconditionally, before anything else can be considered.
  // Its own flag, so a later command in the shared slot cannot displace it.
  if (stop_all_pending) {
    uint8_t seq  = stop_all_seq;
    uint8_t conn = stop_all_conn;
    stop_all_pending = false;

    move_watchdog_cancel();
    app_move_active = false;
    app_auto_level_active = false;
    note_app_stopped_commanding(); // never inhibited, and releases the latch
    app_stop_all();                // STOP_ALL keeps its full everything-off behaviour

    // Accepted from ANY role - control or viewer. DECISIONS #12's
    // stop-from-anyone rule, and #4's explicit carve-out.
    send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
  }

  if (!command_pending) {
    return;
  }

  // Snapshot before clearing the flag. Both this and the callback that sets
  // it run in main-loop context on this part - sl_bt_on_event() is dispatched
  // from the same super loop, not from an ISR - so no critical section is
  // needed; the volatile qualifiers are belt-and-braces.
  uint8_t cmd[CMD_LEN];
  for (uint8_t i = 0; i < CMD_LEN; i++) {
    cmd[i] = pending_command[i];
  }
  uint8_t len  = pending_command_len;
  uint8_t conn = pending_command_conn;
  command_pending = false;

  // A short write has no trustworthy seq field. Echo 0 rather than a byte
  // that was never sent; the app sees REJECTED_INVALID either way.
  uint8_t seq = (len == CMD_LEN) ? cmd[CMD_OFS_SEQ] : 0;

  if (len != CMD_LEN) {
    send_command_result(conn, seq, CMD_RESULT_REJECTED_INVALID);
    return;
  }

  uint8_t opcode = cmd[CMD_OFS_OPCODE];
  uint8_t arg0   = cmd[CMD_OFS_ARG0];
  uint8_t arg1   = cmd[CMD_OFS_ARG1];

  // REFRESH_TILT is not a motion command: accepted from either role and in
  // settings mode, where the sampler stands down and it is harmlessly
  // absorbed. Handled before the arbitration gates for that reason (#4).
  if (opcode == CMD_OPCODE_REFRESH_TILT) {
    if (arg0 != 0 || arg1 != 0) {
      send_command_result(conn, seq, CMD_RESULT_REJECTED_INVALID);
      return;
    }
    tilt_telemetry_arm_burst();
    send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
    return;
  }

  // Everything below is motion-class and subject to arbitration.

  // A viewer is a window, not a second source (#14). STOP_ALL never reaches
  // here - it is handled above, from any role.
  if (conn != control_connection) {
    send_command_result(conn, seq, CMD_RESULT_REJECTED_VIEWER);
    return;
  }

  if (app_in_settings_mode()) {
    send_command_result(conn, seq, CMD_RESULT_REJECTED_SETTINGS_MODE);
    return;
  }

  // Two-source conflict inhibit (#36). Motion-starting opcodes only: stops
  // are never inhibited, LIGHTS is not motion, and REFRESH_TILT returned
  // above.
  //
  // Note note_app_commanding() BEFORE the refusal. A refused repeat still
  // means the user is holding the button, so it must keep the app's half of
  // the latch alive - otherwise the silence timeout would expire mid-hold,
  // the latch would clear, and the next repeat would restart the bed. That
  // is exactly the "release both, then press again" rule this implements.
  // It refreshes the latch; it never clears or extends anything else.
  if (bed_conflict_inhibited()
      && (opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)) {
    note_app_commanding();
    send_command_result(conn, seq, CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE);
    return;
  }

  uint8_t result = CMD_RESULT_REJECTED_INVALID;

  switch (opcode) {
    case CMD_OPCODE_MOVE_START: {
      if (arg0 > (uint8_t)BED_TARGET_ALL || arg1 > (uint8_t)BED_DIR_UP) {
        break; // out-of-range target or direction
      }
      bed_target_t    target = (bed_target_t)arg0;
      bed_direction_t dir    = (bed_direction_t)arg1;

      note_app_commanding();

      // A repeat with a DIFFERENT target/dir is MOVE_STOP then MOVE_START
      // (#4) - not a direction change under a running move.
      if (app_move_active && (target != app_move_target || dir != app_move_dir)) {
        bed_move_stop(BED_SOURCE_APP);
        app_apply_stop_outputs();
        app_move_active = false;
      }

      if (!app_move_active) {
        if (!bed_move_start(BED_SOURCE_APP, target, dir)) {
          // The wall switch holds the bed; bed_control stopped movement and
          // latched the inhibit (#36). The app's movement does NOT start, and
          // will not start until both sources release.
          move_watchdog_cancel();
          result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
          break;
        }
        app_apply_move_outputs(target);
        app_move_active = true;
        app_move_target = target;
        app_move_dir    = dir;
      }

      // Both a fresh start and an identical repeat refresh the watchdog.
      last_move_seq = seq;
      move_watchdog_arm();
      result = CMD_RESULT_ACCEPTED;
      break;
    }

    case CMD_OPCODE_MOVE_STOP:
      if (arg0 != 0 || arg1 != 0) {
        break;
      }
      move_watchdog_cancel();
      app_move_active = false;
      note_app_stopped_commanding();
      bed_move_stop(BED_SOURCE_APP); // stop-class: always reports true
      app_apply_stop_outputs();
      result = CMD_RESULT_ACCEPTED;
      break;

    case CMD_OPCODE_AUTO_LEVEL_START:
      if (arg0 != 0 || arg1 != 0) {
        break;
      }
      // HOLD-TO-RUN as of #36, matching the physical auto-level button. The
      // app repeats this every 100 ms while held and it arms the same
      // dead-man watchdog a move does.
      note_app_commanding();

      if (!app_auto_level_active) {
        move_watchdog_cancel();
        app_move_active = false;
        if (!bed_auto_level_start(BED_SOURCE_APP)) {
          result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
          break;
        }
        app_apply_auto_level_start_outputs();
        app_auto_level_active = true;
      }
      // A repeat must NOT re-enter bed_auto_level_start() - that resets the
      // levelling state machine, so at 10 Hz it would never make progress.
      // It only refreshes the watchdog, exactly like a MOVE_START repeat.
      last_move_seq = seq;
      move_watchdog_arm();
      result = CMD_RESULT_ACCEPTED;
      break;

    case CMD_OPCODE_AUTO_LEVEL_STOP:
      if (arg0 != 0 || arg1 != 0) {
        break;
      }
      move_watchdog_cancel();
      app_auto_level_active = false;
      note_app_stopped_commanding();
      bed_auto_level_stop(BED_SOURCE_APP); // stop-class: always reports true
      app_apply_stop_outputs();            // clears the red/green auto-level LEDs
      result = CMD_RESULT_ACCEPTED;
      break;

    case CMD_OPCODE_LIGHTS_SET:
      if (arg0 > 1 || arg1 != 0) {
        break;
      }
      // Latched, no watchdog, no motion.
      if (!bed_lights_set(BED_SOURCE_APP, arg0 == 1)) {
        result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
        break;
      }
      app_apply_lights_outputs();
      result = CMD_RESULT_ACCEPTED;
      break;

    default:
      // Unknown opcode. STOP_ALL cannot land here (own flag), REFRESH_TILT
      // returned above.
      break;
  }

  send_command_result(conn, seq, result);
}

// Seeds control_status_subscribed/viewer_status_subscribed from the client's
// actual CCCD, read directly from the stack, instead of relying solely on
// sl_bt_evt_gatt_server_characteristic_status - which per its own doc fires
// only on a CCCD CHANGE, not as a current-state event. On a resumed bond the
// CCCD may already be set (restored by the stack) with no change event to
// catch it, leaving status_subscriber_count stuck at 0 while the client
// believes it's subscribed and ble_bed_service_tick() early-returns forever.
// Called once, immediately after a connection is freshly assigned a role
// (assign_role_if_needed() returning true) - reads ground truth so it's
// correct whether or not the stack restores the CCCD, and closes the
// ordering race where a characteristic_status event could otherwise arrive
// before the role exists to seed against.
static void seed_status_subscription_from_cccd(uint8_t connection)
{
  uint16_t flags = 0;
  sl_status_t sc = sl_bt_gatt_server_read_client_configuration(connection, gattdb_bed_status, &flags);

  // Permanent probe, not bisection scaffolding: this is the one piece of
  // evidence that tells the bench whether the stack actually restores the
  // CCCD across a resumed-bond reconnect, which is the fact this fix
  // depends on. Same justification as the existing security-lifecycle
  // prints - keep it.
#if BLE_DEBUG_SECURITY_LOG
  printf("BLE: cccd_seed conn=%u bed_status=0x%04x sc=0x%04x\r\n",
         connection, flags, (unsigned)sc);
#endif

  if (sc == SL_STATUS_OK && (flags & sl_bt_gatt_server_notification)) {
    if (connection == control_connection) {
      control_status_subscribed = true;
    } else if (connection == viewer_connection) {
      viewer_status_subscribed = true;
    }
    update_status_subscriber_count();
    // Required, not optional: update_status_subscriber_count() only ever
    // STOPS the notify timer (#24), it never starts one. Without this, a
    // seeded-subscribed idle bed would send nothing until the next
    // flags-change edge - exactly the bug this function exists to close.
    status_notify_due = true;
  }
}

static void handle_connection_closed(uint8_t connection)
{
  track_connection_closed(connection);

  if (connection == control_connection) {
    // BLE_GATT_DESIGN.md #5, implemented exactly as written: the control
    // connection dropping while APP holds the bed is a full clean stop, NOT
    // bed_move_stop(). bed_move_stop() would leave a latched auto-level run
    // going with nobody connected to stop it.
    //
    // Flagged, not done here: this is the disconnect callback, and
    // app_stop_all() drives LEDs over I2C. Blocking I2C in a stack callback
    // is DECISIONS #27/#30's confirmed radio-starvation mechanism.
    if (bed_active_source() == BED_SOURCE_APP) {
      disconnect_stop_pending = true;
    }

    control_connection = SLOT_EMPTY;
    control_status_subscribed = false;

    if (viewer_connection != SLOT_EMPTY) {
      // Promote the surviving viewer to control (DECISIONS.md #21) - a
      // reconnect should not be required to regain control. The physical
      // GATT subscription state travels with the connection, not the role
      // label, so it moves along with the promotion.
      control_connection = viewer_connection;
      control_status_subscribed = viewer_status_subscribed;
      viewer_connection = SLOT_EMPTY;
      viewer_status_subscribed = false;

      uint8_t role_value = ROLE_CONTROL;
      sl_status_t sc = sl_bt_gatt_server_send_notification(control_connection, gattdb_client_role,
                                                             1, &role_value);
      (void)sc; // best-effort: no-op if the promoted connection hadn't subscribed
    }
  } else if (connection == viewer_connection) {
    viewer_connection = SLOT_EMPTY;
    viewer_status_subscribed = false;
  }

  update_status_subscriber_count();
  start_advertising(); // a slot just freed
}

/*******************************************************************************
 *** Bed Status notify cadence (BLE_GATT_DESIGN.md #6/#24)
 ******************************************************************************/

static void status_notify_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;
  status_notify_due = true;
}

static void restart_status_notify_timer(bool moving)
{
  uint32_t interval_ms = moving ? STATUS_NOTIFY_MOVING_MS : STATUS_NOTIFY_IDLE_MS;
  uint32_t ticks = sl_sleeptimer_ms_to_tick((uint16_t)interval_ms);
  sl_sleeptimer_restart_timer(&status_notify_timer, ticks, status_notify_timer_callback, NULL, 0, 0);
}

// True if the packed status differs from the last-sent one in any field the
// #6 cadence table calls "state" (flags/source/command/motor status bits) -
// deliberately NOT current_ma or the tilt angles, which wander continuously
// and would otherwise defeat the adaptive cadence by looking "changed" every
// tick.
static bool status_flags_changed(const uint8_t *new_packed, const uint8_t *old_packed)
{
  if (memcmp(new_packed, old_packed, 4) != 0) { // flags, active_source, command, command_detail
    return true;
  }
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    if (new_packed[8 + i * 3] != old_packed[8 + i * 3]) { // motor status byte, not current_ma
      return true;
    }
  }
  return false;
}

static void publish_status(const uint8_t *packed, bool moving)
{
  sl_bt_gatt_server_write_attribute_value(gattdb_bed_status, 0, gattdb_bed_status_len, packed);
  sl_status_t sc = sl_bt_gatt_server_notify_all(gattdb_bed_status, gattdb_bed_status_len, packed);
  (void)sc;

  memcpy(last_sent_status, packed, gattdb_bed_status_len);
  status_notify_due = false;
  restart_status_notify_timer(moving);
}

bool ble_bed_service_status_subscribed(void)
{
  return status_subscriber_count > 0;
}

bool ble_bed_service_app_motion_active(void)
{
  return app_move_active || app_auto_level_active;
}

void ble_bed_service_tick(void)
{
  // Control link dropped mid-command (BLE_GATT_DESIGN.md #5). Deferred out
  // of the disconnect callback; done here, first, before anything else.
  if (disconnect_stop_pending) {
    disconnect_stop_pending = false;
    move_watchdog_cancel();
    app_move_active = false;
    app_auto_level_active = false;
    note_app_stopped_commanding(); // the app is gone - release its half of the latch
    app_stop_all();
  }

  // Drained BEFORE the subscriber check: a Command write is valid from a
  // bonded connection whether or not it subscribed to Bed Status, and its
  // Command Result must still be sent. Only the telemetry notify below is
  // gated on having a subscriber.
  process_pending_command();
  service_move_watchdog();
  service_app_commanding();

  if (status_subscriber_count == 0) {
    return; // #24: no subscribers - no notify traffic, no accel-sampling gate to feed yet
  }

  bed_status_t status;
  bed_status_get(&status);

  uint8_t packed[gattdb_bed_status_len];
  ble_bed_service_encode_status(&status, false, packed); // tilt_fresh: Stage B5

  if (status_notify_due || status_flags_changed(packed, last_sent_status)) {
    bool moving = (status.command == BED_COMMAND_MOVE) || (status.command == BED_COMMAND_AUTO_LEVEL);
    publish_status(packed, moving);
  }
}

/*******************************************************************************
 *** sl_bt event handling
 ******************************************************************************/

void ble_bed_service_on_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;

  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id:
      // Bonded + encrypted, Just Works (no MITM claim - no display/passkey
      // entry hardware), LE Secure Connections only (BLE_GATT_DESIGN.md #8).
      // Bit 3 (BONDING_REQUEST_REQUIRED) is set so sl_bt_evt_sm_confirm_bonding
      // fires for every bonding request and the app decides it, instead of
      // the stack auto-rejecting a re-pair against an existing bond (bench-
      // confirmed 0x1205 SMP_PAIRING_NOT_SUPPORTED with this bit clear -
      // DECISIONS.md #28). That handler is the pairing window (DECISIONS.md
      // #42): new bonds and replacements alike are accepted only while
      // settings mode is active.
      sc = sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_BONDING_REQUIRED
                               | SL_BT_SM_CONFIGURATION_SC_ONLY
                               | SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED,
                               sl_bt_sm_io_capability_noinputnooutput);
      app_assert_status(sc);
      printf("BLE SM: configure -> 0x%04x\r\n", (unsigned)sc);

      // Reject new bonds once the 8-bond store is full, rather than
      // silently evicting an existing one.
      sc = sl_bt_sm_store_bonding_configuration(BLE_BED_SERVICE_MAX_BONDS, 0);
      app_assert_status(sc);
      printf("BLE SM: store_bonding_configuration -> 0x%04x\r\n", (unsigned)sc);

      // Bondable at all times at the stack level. The physical pairing
      // window (settings-mode-only bonding, DECISIONS.md #19/#42) is enforced
      // per request in the sl_bt_evt_sm_confirm_bonding handler below - one
      // decision point, not a second mechanism here.
      sc = sl_bt_sm_set_bondable_mode(1);
      app_assert_status(sc);
      printf("BLE SM: set_bondable_mode(1) -> 0x%04x\r\n", (unsigned)sc);

#if BLE_BENCH_WIPE_BONDS
      sc = sl_bt_sm_delete_bondings();
      printf("BLE SM: BENCH-ONLY delete_bondings() -> 0x%04x\r\n", (unsigned)sc);
#endif

      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert_status(sc);

      // "Advertise" is checked on the Tilt Bed Service in the GATT
      // Configurator, so this call packs the 128-bit service UUID into the
      // advertising payload automatically (BLE_GATT_DESIGN.md #8).
      sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                  sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);

      sc = sl_bt_advertiser_set_timing(advertising_set_handle,
                                        ADV_INTERVAL_UNITS, ADV_INTERVAL_UNITS, 0, 0);
      app_assert_status(sc);

      start_advertising();
      break;

    case sl_bt_evt_connection_opened_id: {
      uint8_t connection = evt->data.evt_connection_opened.connection;
      track_connection_opened(connection);

#if BLE_DEBUG_SECURITY_LOG
      // bonding != 0xff here means the stack recognized this as a RESUMING
      // known bond at connect time, before any pairing happens - direct
      // evidence of a stale firmware-side bond if the phone reports itself
      // as not bonded.
      printf("BLE: connection_opened conn=%u bonding=0x%02x\r\n",
             connection, evt->data.evt_connection_opened.bonding);
#endif

      sc = sl_bt_connection_set_parameters(connection,
                                            CONN_MIN_INTERVAL, CONN_MAX_INTERVAL,
                                            CONN_LATENCY, CONN_SUPERVISION_TIMEOUT,
                                            0, 0xffff);
      (void)sc; // best-effort: peer may reject, link still usable at default params

      if (open_connection_count() < 2) {
        start_advertising(); // keep the second slot discoverable (#22)
      }
      break;
    }

    case sl_bt_evt_connection_closed_id:
#if BLE_DEBUG_SECURITY_LOG
      // An sl_status_t code - see sl_status.h SL_STATUS_BT_CTRL_* (e.g.
      // 0x1008 = CONNECTION_TIMEOUT / supervision timeout) and
      // SL_STATUS_BT_SMP_* for pairing-specific failures.
      printf("BLE: connection_closed conn=%u reason=0x%04x\r\n",
             evt->data.evt_connection_closed.connection,
             evt->data.evt_connection_closed.reason);
#endif
      handle_connection_closed(evt->data.evt_connection_closed.connection);
      break;

    case sl_bt_evt_sm_bonded_id:
#if BLE_DEBUG_SECURITY_LOG
      printf("BLE SM: bonded conn=%u bonding=0x%02x security_mode=%u\r\n",
             evt->data.evt_sm_bonded.connection,
             evt->data.evt_sm_bonded.bonding,
             evt->data.evt_sm_bonded.security_mode);
#endif
      if (assign_role_if_needed(evt->data.evt_sm_bonded.connection)) {
        // Fresh pairing - the CCCD read will normally return 0 here (the
        // client hasn't subscribed yet), a harmless no-op. Seeded anyway for
        // symmetry with the resumed-bond call site below and so role
        // assignment and CCCD seeding always happen together, not
        // conditionally on which event assigned the role.
        seed_status_subscription_from_cccd(evt->data.evt_sm_bonded.connection);
      }
      break;

    // An sl_status_t code - see sl_status.h SL_STATUS_BT_SMP_* for
    // pairing-specific failures (e.g. 0x1203 = AUTHENTICATION_REQUIREMENTS,
    // IO-capability mismatch).
    case sl_bt_evt_sm_bonding_failed_id:
#if BLE_DEBUG_SECURITY_LOG
      printf("BLE SM: bonding_failed conn=%u reason=0x%04x\r\n",
             evt->data.evt_sm_bonding_failed.connection,
             evt->data.evt_sm_bonding_failed.reason);
#endif
      break;

    // Fires for every bonding attempt now that sl_bt_sm_configure()'s bit 3
    // (BONDING_REQUEST_REQUIRED) is set (DECISIONS.md #28). bonding_handle
    // == 0xff (SL_BT_INVALID_BONDING_HANDLE) is a fresh pairing with no
    // existing bond; anything else means the peer already resolved to an
    // existing bond and confirming here REPLACES it (per this event's own
    // SDK doc: "a bonding already exists ... overwriting is a potential
    // security risk"). Logged so a replace vs. a new bond is visible on the
    // bench.
    //
    // THE PAIRING WINDOW (DECISIONS.md #42, implementing #19): confirm only
    // while settings mode is active on the switch panel, reject otherwise.
    // New bonds and replacements of an existing bond go through the same
    // window, so #28's auto-replace now happens only inside it. An
    // already-bonded phone reconnecting resumes encryption from its stored
    // bond and never raises this event, so it is not affected. The window
    // closes on settings_mode_exit() and on reset. Rejecting (0) rather than
    // ignoring the event gives the phone a clean pairing failure, not a hang.
    case sl_bt_evt_sm_confirm_bonding_id: {
      const sl_bt_evt_sm_confirm_bonding_t *cb = &evt->data.evt_sm_confirm_bonding;
      uint8_t confirm = app_in_settings_mode() ? 1 : 0;
#if BLE_DEBUG_SECURITY_LOG
      printf("BLE SM: confirm_bonding conn=%u (%s) -> %s\r\n",
             cb->connection,
             (cb->bonding_handle == SL_BT_INVALID_BONDING_HANDLE) ? "new bond" : "REPLACING existing bond",
             confirm ? "ACCEPTED (settings mode)" : "REJECTED (not in settings mode)");
#endif
      sl_bt_sm_bonding_confirm(cb->connection, confirm);
      break;
    }

    // Covers a RESUMED bond's encryption completing, which sl_bt_evt_sm_
    // bonded does not fire for (see assign_role_if_needed's comment). This
    // event also fires once at raw connection-open with security_mode ==
    // no-security, which the check below correctly ignores.
    case sl_bt_evt_connection_parameters_id: {
      const sl_bt_evt_connection_parameters_t *params = &evt->data.evt_connection_parameters;
      printf("BLE: connection_parameters conn=%u interval=%u security_mode=%u\r\n",
             params->connection, params->interval, params->security_mode);
      if (params->security_mode != sl_bt_connection_mode1_level1) {
        if (assign_role_if_needed(params->connection)) {
          // The resumed-bond path: sm_bonded does not fire here (see the
          // comment above this case), so this is the only place a resumed
          // connection's CCCD ever gets seeded.
          seed_status_subscription_from_cccd(params->connection);
        }
      }
      break;
    }

    case sl_bt_evt_gatt_server_user_read_request_id: {
      const sl_bt_evt_gatt_server_user_read_request_t *req = &evt->data.evt_gatt_server_user_read_request;
      if (req->characteristic == gattdb_client_role) {
        // Defaults to VIEWER if asked before role assignment lands (should
        // not happen - bonding completes, hence a role, before any GATT
        // operation is reachable - but never default-grant CONTROL).
        uint8_t role_value = (req->connection == control_connection) ? ROLE_CONTROL : ROLE_VIEWER;
        sl_bt_gatt_server_send_user_read_response(req->connection, gattdb_client_role, 0,
                                                    1, &role_value, NULL);
      }
      break;
    }

    // Command characteristic write (BLE_GATT_DESIGN.md #4). The Command
    // characteristic is a VALUE-type characteristic with write_no_response,
    // so writes surface here rather than as a user-write request.
    //
    // THIS HANDLER DOES NO WORK. It copies four bytes, sets a flag, and
    // returns. It does not read the accelerometer, touch I2C, drive a motor,
    // or notify anything. process_pending_command() does all of that one
    // tick later from the main loop.
    //
    // DECISIONS.md #27/#30: blocking work inside a Bluetooth stack callback
    // is the confirmed mechanism behind this project's reason=0x1008
    // supervision-timeout drops. REFRESH_TILT exists to trigger accelerometer
    // reads, so doing it here would recreate that bug exactly. Any future
    // opcode added to this switch must obey the same rule.
    case sl_bt_evt_gatt_server_attribute_value_id: {
      const sl_bt_evt_gatt_server_attribute_value_t *av =
          &evt->data.evt_gatt_server_attribute_value;

      if (av->attribute != gattdb_command) {
        break;
      }

      // STOP_ALL never goes in the shared slot. It is sent once, nothing
      // corrects for losing it, and a MOVE_START repeat arriving in the same
      // tick would otherwise overwrite it. Own flag, cleared only by the main
      // loop once the stop has actually happened.
      if (av->value.len == CMD_LEN
          && av->value.data[CMD_OFS_OPCODE] == CMD_OPCODE_STOP_ALL) {
        stop_all_seq  = av->value.data[CMD_OFS_SEQ];
        stop_all_conn = av->connection;
        stop_all_pending = true;
        break;
      }

      uint8_t copy_len = av->value.len;
      if (copy_len > CMD_LEN) {
        copy_len = CMD_LEN;
      }
      for (uint8_t i = 0; i < CMD_LEN; i++) {
        pending_command[i] = (i < copy_len) ? av->value.data[i] : 0;
      }
      // Keep the length AS WRITTEN, not the clamped copy length, so
      // process_pending_command() can reject a wrong-length write instead of
      // silently accepting a truncated or padded one.
      pending_command_len  = av->value.len;
      pending_command_conn = av->connection;
      command_pending = true;
      break;
    }

    case sl_bt_evt_gatt_server_characteristic_status_id: {
      const sl_bt_evt_gatt_server_characteristic_status_t *cs = &evt->data.evt_gatt_server_characteristic_status;
      if (cs->characteristic == gattdb_bed_status
          && cs->status_flags == sl_bt_gatt_server_client_config) {
        bool subscribed = (cs->client_config_flags & sl_bt_gatt_server_notification) != 0;

        if (cs->connection == control_connection) {
          control_status_subscribed = subscribed;
        } else if (cs->connection == viewer_connection) {
          viewer_status_subscribed = subscribed;
        } else {
          // Matches neither role slot - dropped silently before this log
          // line existed. Not re-architected to be keyed by connection
          // handle here (that would touch #21's viewer->control promotion
          // logic in handle_connection_closed(), scoped out of this task);
          // this is visibility only, so a real occurrence isn't invisible.
#if BLE_DEBUG_SECURITY_LOG
          printf("BLE: characteristic_status DROPPED conn=%u client_config_flags=0x%04x "
                 "(matches neither control nor viewer connection)\r\n",
                 cs->connection, cs->client_config_flags);
#endif
        }
        update_status_subscriber_count();

        if (subscribed) {
          // Do NOT publish synchronously here - this runs in the stack
          // event callback, not coordinated main-loop context, and
          // bed_status_get() can block on I2C (bench-confirmed: this was
          // stalling the super loop long enough to drop the BLE link).
          // Flagging due lets the next ble_bed_service_tick() publish from
          // the main loop instead.
          status_notify_due = true;
        }
      }
      break;
    }

    default:
      break;
  }
}
