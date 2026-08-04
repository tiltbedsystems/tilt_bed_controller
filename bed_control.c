/*
 * bed_control.c
 *
 *  Created on: Jul 7, 2026
 */

#include "bed_control.h"
#include "bed_actions.h"

static bed_source_t active_source = BED_SOURCE_NONE;

/*
 * Two-source conflict inhibit (DECISIONS.md #36).
 *
 * A conflict - either source touching the bed while the other is moving it -
 * stops movement and LATCHES. Nothing moves again until BOTH sources have
 * released, at which point a fresh press on either one starts normally. This
 * replaced a 500 ms timed lockout, which expired while a button was still
 * held and let whichever source was still holding win - the stop-then-hand-
 * over behaviour #12 exists to prevent.
 *
 * This lives here because bed_control is the one place both sources already
 * meet, so a single check covers the app and the wall switch symmetrically.
 *
 * "Commanding" means a source is currently asking for motion. The switch path
 * sets it on press and clears it on release, both through the verbs below.
 * The app path sets it on every MOVE_START/AUTO_LEVEL_START and clears it on
 * an explicit stop or after MOVE_WATCHDOG_MS of silence - see
 * ble_bed_service.c, which calls bed_note_source_released() for the timeout.
 */
static bool conflict_inhibit = false;
static bool source_commanding[3] = { false, false, false }; // indexed by bed_source_t

static void note_commanding(bed_source_t src, bool commanding)
{
  if (src == BED_SOURCE_NONE) {
    return;
  }
  source_commanding[src] = commanding;

  // Cessation is the ONLY thing that clears the latch. A refused command must
  // never clear it - an app repeating MOVE_START because the user is still
  // holding is still commanding, and must keep being refused.
  if (conflict_inhibit
      && !source_commanding[BED_SOURCE_SWITCHES]
      && !source_commanding[BED_SOURCE_APP]) {
    conflict_inhibit = false;
  }
}

void bed_note_source_released(bed_source_t src)
{
  note_commanding(src, false);
}

bool bed_conflict_inhibited(void)
{
  return conflict_inhibit;
}

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

bool bed_move_start(bed_source_t src, bed_target_t target, bed_direction_t dir)
{
  note_commanding(src, true);

  if (conflict_inhibit) {
    return false; // latched - both sources must release first (#36)
  }
  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    conflict_inhibit = true;
    app_stop_movement();
    return false;
  }
  app_move_start(target, dir);
  return true;
}

bool bed_move_stop(bed_source_t src)
{
  note_commanding(src, false); // stops are never inhibited

  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    app_stop_movement();
    return true;
  }
  app_move_stop();
  return true;
}

bool bed_auto_level_start(bed_source_t src)
{
  note_commanding(src, true);

  if (conflict_inhibit) {
    return false; // latched (#36)
  }
  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    conflict_inhibit = true;
    app_stop_movement();
    return false;
  }
  app_auto_level_start();
  return true;
}

bool bed_auto_level_stop(bed_source_t src)
{
  note_commanding(src, false); // stops are never inhibited

  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    app_stop_movement();
    return true;
  }
  app_auto_level_stop();
  return true;
}

// Lights are not motion: never inhibited, and never latch the inhibit. A
// lights command from the non-holding source still force-stops the bed, which
// is #12's existing stop-from-anyone behaviour and is left unchanged.
bool bed_lights_set(bed_source_t src, bool on)
{
  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    app_stop_movement();
    return false;
  }
  app_lights_set(on);
  return true;
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
