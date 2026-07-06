/*
 * bed_control.c
 *
 *  Created on: Jul 7, 2026
 */

#include "bed_control.h"
#include "bed_actions.h"

static bed_source_t active_source = BED_SOURCE_NONE;

typedef enum {
  ARBITRATE_NORMAL,     // src holds (or acquires) the lock - proceed normally
  ARBITRATE_FORCE_STOP  // a different source holds the lock - force a clean stop instead
} arbitrate_result_t;

/*
 * Releases the lock lazily (if the bed is already idle), then either grants
 * the lock to an idle bed, confirms the calling source already holds it, or
 * signals that a different source holds it. Every verb - including the
 * stop-class ones - runs through this uniformly: a stop request from the
 * source that already holds the lock is just that source's own natural
 * stop; a stop (or any other command) from the other source becomes a full
 * app_stop_all() via ARBITRATE_FORCE_STOP, matching DECISIONS.md #12's "any
 * input whatsoever from the other source is interpreted ONLY as an
 * immediate clean STOP" - not a partial, verb-specific stop.
 */
static arbitrate_result_t arbitrate(bed_source_t src)
{
  if (app_bed_is_idle()) {
    active_source = BED_SOURCE_NONE;
  }

  if (active_source == BED_SOURCE_NONE) {
    active_source = src;
    return ARBITRATE_NORMAL;
  }

  if (active_source == src) {
    return ARBITRATE_NORMAL;
  }

  return ARBITRATE_FORCE_STOP;
}

void bed_move_start(bed_source_t src, bed_target_t target, bed_direction_t dir)
{
  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    app_stop_all();
    return;
  }
  app_move_start(target, dir);
}

void bed_move_stop(bed_source_t src)
{
  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    app_stop_all();
    return;
  }
  app_move_stop();
}

void bed_auto_level_start(bed_source_t src)
{
  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    app_stop_all();
    return;
  }
  app_auto_level_start();
}

void bed_auto_level_stop(bed_source_t src)
{
  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    app_stop_all();
    return;
  }
  app_auto_level_stop();
}

void bed_lights_set(bed_source_t src, bool on)
{
  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    app_stop_all();
    return;
  }
  app_lights_set(on);
}

void bed_control_tick(void)
{
  if (app_bed_is_idle()) {
    active_source = BED_SOURCE_NONE;
  }
}

bed_source_t bed_active_source(void)
{
  return active_source;
}
