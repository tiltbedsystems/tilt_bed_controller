/*
 * bed_settings.c
 *
 *  Created on: Jul 6, 2026
 */

#include <stdio.h>

#include "bed_settings.h"
#include "nvm3_functions.h"
#include "auto_level.h"
#include "motor_current_functions.h"
#include "debug_capture.h"

typedef struct {
  uint32_t nvm3_key;
  int16_t default_value;
  int16_t min_value;
  int16_t max_value;
  int16_t *value;       // points at the existing live variable
  const char *name;      // for reject/debug messages only
} bed_setting_entry_t;

/*
 * Raise/lower collision margin bounds are widened beyond
 * docs/SETPOINTS_INVENTORY.md's proposed 150-1200 / 100-700 so that the
 * existing sensitivity ladder's OFF rung (RAISE_OFF_SENSITIVITY = 10000,
 * LOWER_OFF_SENSITIVITY = 750, both in app.c) keeps persisting to NVM3
 * exactly as it does today. The inventory's tighter numbers assume its
 * Note A redesign (OFF becomes a separate enable flag, not a raw margin
 * value) - that redesign is deferred, so this migration keeps today's
 * full ladder range valid instead of silently breaking OFF persistence.
 */
static const bed_setting_entry_t bed_setting_table[BED_SETTING_COUNT] = {
  { CUSTOM_ROLL_ANGLE_KEY,           0,   -1500, 1500,  &baseline_roll_angle,                     "baseline roll angle"    },
  { CUSTOM_PITCH_ANGLE_KEY,          0,   -1500, 1500,  &baseline_pitch_angle,                    "baseline pitch angle"   },
  { RAISE_COLLISION_SENSITIVITY_KEY, 350, 150,   10000, &raise_collision_current_security_margin, "raise collision margin" },
  { LOWER_COLLISION_SENSITIVITY_KEY, 250, 100,   750,   &lower_collision_current_security_margin, "lower collision margin" },
};

void bed_settings_init(void)
{
  for (uint8_t i = 0; i < BED_SETTING_COUNT; i++) {
    const bed_setting_entry_t *e = &bed_setting_table[i];
    int16_t loaded = e->default_value;

    if (isValueStored(e->nvm3_key)) {
      int16_t raw;
      if (readValue(e->nvm3_key, &raw) == ECODE_NVM3_OK) {
        if (raw >= e->min_value && raw <= e->max_value) {
          loaded = raw;
        } else {
#if !BED_CAPTURE_MOTOR_CURRENTS
          printf("Rejected stored %s: %d out of range [%d, %d], using default %d\r\n",
                 e->name, raw, e->min_value, e->max_value, e->default_value);
#endif
        }
      }
    }

    *e->value = loaded;
  }
}

bool bed_settings_set(bed_setting_id_t id, int16_t value)
{
  const bed_setting_entry_t *e = &bed_setting_table[id];

  if (value < e->min_value || value > e->max_value) {
#if !BED_CAPTURE_MOTOR_CURRENTS
    printf("Rejected %s: %d out of range [%d, %d]\r\n", e->name, value, e->min_value, e->max_value);
#endif
    return false;
  }

  *e->value = value;

  Ecode_t status = storeValue(e->nvm3_key, value);
  if (status != ECODE_NVM3_OK) {
#if !BED_CAPTURE_MOTOR_CURRENTS
    printf("Failed to persist %s to NVM3\r\n", e->name);
#endif
    return false;
  }

  return true;
}

bool bed_settings_erase(bed_setting_id_t id)
{
  const bed_setting_entry_t *e = &bed_setting_table[id];
  return (eraseValue(e->nvm3_key) == ECODE_NVM3_OK);
}

int16_t bed_settings_get_default(bed_setting_id_t id)
{
  return bed_setting_table[id].default_value;
}

int16_t bed_settings_get_min(bed_setting_id_t id)
{
  return bed_setting_table[id].min_value;
}

int16_t bed_settings_get_max(bed_setting_id_t id)
{
  return bed_setting_table[id].max_value;
}
