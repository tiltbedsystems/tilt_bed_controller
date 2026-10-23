/*
 * ble_bed_service.h
 *
 * BLE GATT glue for the Tilt Bed Service (BLE_GATT_DESIGN.md #11). Owns all
 * sl_bt stack event handling for the custom service: advertising bring-up,
 * bonding/security configuration, per-connection CONTROL/VIEWER role
 * tracking (assigned once a connection finishes bonding, not at raw
 * connection-open), and the adaptive Bed Status notify cadence (#6/#24).
 *
 * This module is glue, not a second source of truth: Stage C feeds decoded
 * Command writes into the existing bed_control.c verbs exactly as a physical
 * switch press does - no bed logic is reimplemented here.
 *
 * All seven opcodes of BLE_GATT_DESIGN.md #4 are implemented, along with the
 * #5 dead-man watchdog and #14 role arbitration. The app can drive the bed.
 *
 *  Created on: Jul 12, 2026
 */

#ifndef BLE_BED_SERVICE_H_
#define BLE_BED_SERVICE_H_

#include <stdint.h>
#include <stdbool.h>

#include "sl_bt_api.h"
#include "bed_status.h"

// Bench diagnostics for dead-man watchdog stops (the app's "Connection
// stalled" message, Command Result WATCHDOG_STOP). When 1, ble_bed_service.c
// records refresh and Command-write ticks and app.c's app_process_action()
// records its own pass times; one short block is printed on each watchdog
// stop, and the connection_parameters print adds latency and supervision
// timeout. Measures only: nothing blocks and nothing prints per pass. Set to
// 0 before shipping.
#define BLE_DEBUG_WATCHDOG_LOG 1

#if BLE_DEBUG_WATCHDOG_LOG
// Called first and last in app_process_action(), to time each main-loop pass.
void ble_bed_service_debug_pass_begin(void);
void ble_bed_service_debug_pass_end(void);
#endif

// Packs a bed_status_t snapshot into the 20-byte Bed Status characteristic
// wire format (BLE_GATT_DESIGN.md #6), little-endian, matching gattdb_
// bed_status_len exactly. tilt_fresh is passed in separately rather than
// read from bed_status_t - the continuous accel sampling that makes it
// meaningful is a later, separately-flagged change (DECISIONS.md #17/#24);
// until then callers pass false.
void ble_bed_service_encode_status(const bed_status_t *in, bool tilt_fresh, uint8_t *out);

// Forwards every sl_bt stack event the Tilt Bed Service cares about: boot,
// connection open/close, bonding, characteristic subscribe/unsubscribe.
// Called from app.c's sl_bt_on_event(), which stays a thin forwarder.
void ble_bed_service_on_event(sl_bt_msg_t *evt);

// Call once per app tick (from app_process_action()). Drains any pending
// Command write in main-loop context (never in the stack callback - see
// process_pending_command()) and drives the adaptive Bed Status notify
// cadence (BLE_GATT_DESIGN.md #6/#24), which is a no-op whenever nothing is
// subscribed.
void ble_bed_service_tick(void);

// True while at least one connection has Bed Status notifications enabled.
// app_process_action() passes this to tilt_telemetry_tick() as its master
// gate: no subscriber, no accelerometer sampling and no sleep veto, so a
// switch-only bed with no phone is unchanged (DECISIONS.md #33).
bool ble_bed_service_status_subscribed(void);

// True while the app believes it is holding a move or an auto-level. Used by
// the motion fail-safe in switch_input.c (DECISIONS.md #38) to tell "the app
// is commanding motion" from "nobody is, and the motors should be off".
bool ble_bed_service_app_motion_active(void);

#endif /* BLE_BED_SERVICE_H_ */
