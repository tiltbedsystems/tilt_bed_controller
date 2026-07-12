/*
 * ble_bed_service.h
 *
 * BLE GATT glue for the Tilt Bed Service (BLE_GATT_DESIGN.md #11). Owns all
 * sl_bt stack event handling for the custom service: advertising bring-up,
 * bonding/security configuration, per-connection CONTROL/VIEWER role
 * tracking (assigned once a connection finishes bonding, not at raw
 * connection-open), and the adaptive Bed Status notify cadence (#6/#24).
 *
 * This module is glue, not a second source of truth: Stage C (not yet
 * built) will feed decoded Command writes into the existing bed_control.c
 * verbs exactly as a physical switch press does today - no bed logic is
 * reimplemented here. Stage B (current) is read-only: advertising +
 * telemetry only, no command write handling, so there is no way to drive a
 * motor over BLE yet.
 *
 *  Created on: Jul 12, 2026
 */

#ifndef BLE_BED_SERVICE_H_
#define BLE_BED_SERVICE_H_

#include <stdint.h>
#include <stdbool.h>

#include "sl_bt_api.h"
#include "bed_status.h"

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

// Call once per app tick (from app_process_action()). Drives the adaptive
// Bed Status notify cadence (BLE_GATT_DESIGN.md #6/#24) - a no-op whenever
// nothing is subscribed.
void ble_bed_service_tick(void);

#endif /* BLE_BED_SERVICE_H_ */
