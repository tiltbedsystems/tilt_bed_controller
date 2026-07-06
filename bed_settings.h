/*
 * bed_settings.h
 *
 * Single writer for every runtime-adjustable, NVM3-backed setting.
 * Firmware-enforced clamping is the safety mechanism here: every setter
 * validates against the setting's min/max before applying or persisting,
 * and rejects (does not silently clamp) out-of-range values.
 *
 *  Created on: Jul 6, 2026
 */

#ifndef BED_SETTINGS_H_
#define BED_SETTINGS_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  BED_SETTING_BASELINE_ROLL_ANGLE,
  BED_SETTING_BASELINE_PITCH_ANGLE,
  BED_SETTING_RAISE_COLLISION_MARGIN,
  BED_SETTING_LOWER_COLLISION_MARGIN,
  BED_SETTING_COUNT
} bed_setting_id_t;

// Loads every setting from NVM3 at startup. Missing or unreadable values
// fall back to their default; stored values outside the setting's min/max
// are rejected (logged) and the default is used for this session - NVM3
// itself is not rewritten on a rejected load.
void bed_settings_init(void);

// Validates value against the setting's min/max; if in range, applies it to
// the live variable and persists it to NVM3, returning true. If out of
// range, rejects it (logged) and leaves the live value and NVM3 untouched,
// returning false. If NVM3 write itself fails, the live value is still
// applied but false is returned.
bool bed_settings_set(bed_setting_id_t id, int16_t value);

// Deletes the setting's NVM3 object only - matches eraseStoredAngles()'s
// existing behavior: the live value is unchanged until next boot/reload.
bool bed_settings_erase(bed_setting_id_t id);

int16_t bed_settings_get_default(bed_setting_id_t id);
int16_t bed_settings_get_min(bed_setting_id_t id);
int16_t bed_settings_get_max(bed_setting_id_t id);

#endif /* BED_SETTINGS_H_ */
