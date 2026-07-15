/*
 * ble_bed_service.c
 *
 * See ble_bed_service.h. Stage B: advertising bring-up, bonding, per-
 * connection CONTROL/VIEWER role tracking, and read-only Bed Status
 * telemetry. No Command characteristic handling yet - deliberately absent,
 * not just unimplemented, so there is no code path that can drive a motor
 * over BLE in this stage.
 *
 *  Created on: Jul 12, 2026
 */

#include "ble_bed_service.h"

#include <stdio.h>
#include <string.h>

#include "app_assert.h"
#include "gatt_db.h"
#include "sl_sleeptimer.h"

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
// neither branch on a later call.
static void assign_role_if_needed(uint8_t connection)
{
  if (control_connection == SLOT_EMPTY) {
    control_connection = connection;
  } else if (viewer_connection == SLOT_EMPTY && connection != control_connection) {
    viewer_connection = connection;
  }
}

static void update_status_subscriber_count(void)
{
  uint8_t count = (control_status_subscribed ? 1 : 0) + (viewer_status_subscribed ? 1 : 0);
  if (count == 0 && status_subscriber_count != 0) {
    // #24: nobody subscribed - stop the notify timer, no more radio traffic.
    sl_sleeptimer_stop_timer(&status_notify_timer);
  }
  status_subscriber_count = count;
}

static void handle_connection_closed(uint8_t connection)
{
  track_connection_closed(connection);

  if (connection == control_connection) {
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

void ble_bed_service_tick(void)
{
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
      sc = sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_BONDING_REQUIRED
                               | SL_BT_SM_CONFIGURATION_SC_ONLY,
                               sl_bt_sm_io_capability_noinputnooutput);
      app_assert_status(sc);
      printf("BLE SM: configure -> 0x%04x\r\n", (unsigned)sc);

      // Reject new bonds once the 8-bond store is full, rather than
      // silently evicting an existing one.
      sc = sl_bt_sm_store_bonding_configuration(BLE_BED_SERVICE_MAX_BONDS, 0);
      app_assert_status(sc);
      printf("BLE SM: store_bonding_configuration -> 0x%04x\r\n", (unsigned)sc);

      // Bondable at all times for now - DECISIONS.md #19's physical
      // pairing-window gate (settings-mode-only bonding) is a later,
      // separately-approved step, not part of bring-up.
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
      assign_role_if_needed(evt->data.evt_sm_bonded.connection);
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

    // Per sl_bt_sm_configure()'s bit-3 doc, this should NOT fire in our
    // config (bit 3 is clear - "Bonding request does not need to be
    // confirmed"). Handled and logged anyway as a canary: if this DOES
    // print, that's new information contradicting the documented behavior
    // and worth knowing. Auto-confirms if it ever does fire, matching
    // "Just Works is fine for bring-up."
    case sl_bt_evt_sm_confirm_bonding_id: {
      const sl_bt_evt_sm_confirm_bonding_t *cb = &evt->data.evt_sm_confirm_bonding;
#if BLE_DEBUG_SECURITY_LOG
      printf("BLE SM: confirm_bonding conn=%u bonding_handle=0x%02x (auto-confirming)\r\n",
             cb->connection, cb->bonding_handle);
#endif
      sl_bt_sm_bonding_confirm(cb->connection, 1);
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
        assign_role_if_needed(params->connection);
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

    case sl_bt_evt_gatt_server_characteristic_status_id: {
      const sl_bt_evt_gatt_server_characteristic_status_t *cs = &evt->data.evt_gatt_server_characteristic_status;
      if (cs->characteristic == gattdb_bed_status
          && cs->status_flags == sl_bt_gatt_server_client_config) {
        bool subscribed = (cs->client_config_flags & sl_bt_gatt_server_notification) != 0;

        if (cs->connection == control_connection) {
          control_status_subscribed = subscribed;
        } else if (cs->connection == viewer_connection) {
          viewer_status_subscribed = subscribed;
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
