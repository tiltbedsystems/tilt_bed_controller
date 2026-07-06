/*
 * test/bed_control_test.c
 *
 * Host-compiled unit test for bed_control.c's source-lock arbitration
 * (DECISIONS.md #12). Compiles standalone on the PC with any C99 compiler -
 * bed_control.c, bed_control.h, bed_actions.h, and bed_types.h have no
 * Simplicity Studio SDK or hardware includes.
 *
 * IMPORTANT: this file provides its own stub definitions of every
 * bed_actions.h function (app_move_start, app_move_stop, etc.) so it can
 * exercise bed_control.c's arbitration in isolation from real motor/LED/
 * timer state. Those symbols are ALSO defined for real in bed_actions.c
 * (C4 relocated them out of app.c - see DECISIONS.md #12). This file
 * must NEVER be part of the embedded (Simplicity Studio / arm-none-eabi)
 * build - linking it alongside bed_actions.c would be a duplicate-symbol
 * error, and it isn't meant to run on target anyway. Build and run on the
 * host only:
 *
 *   gcc -std=c99 -Wall -I.. -o bed_control_test bed_control_test.c ../bed_control.c
 *   ./bed_control_test
 *
 * This is the only way to exercise the "other source forces a stop" path
 * before BLE (D1) exists to provide a real second source - see C2's plan
 * for the four scenarios covered here.
 */

#include <stdbool.h>
#include <stdio.h>

#include "../bed_control.h"
#include "../bed_actions.h"

static int move_start_calls;
static int move_stop_calls;
static int auto_level_start_calls;
static int auto_level_stop_calls;
static int lights_set_calls;
static int stop_all_calls;
static bool stub_idle;

static void reset_stub(bool idle)
{
  move_start_calls = 0;
  move_stop_calls = 0;
  auto_level_start_calls = 0;
  auto_level_stop_calls = 0;
  lights_set_calls = 0;
  stop_all_calls = 0;
  stub_idle = idle;
}

void app_move_start(bed_target_t target, bed_direction_t dir)
{
  (void)target;
  (void)dir;
  move_start_calls++;
  stub_idle = false;
}

void app_move_stop(void)
{
  move_stop_calls++;
  stub_idle = true;
}

void app_auto_level_start(void)
{
  auto_level_start_calls++;
  stub_idle = false;
}

void app_auto_level_stop(void)
{
  auto_level_stop_calls++;
  stub_idle = true;
}

void app_lights_set(bool on)
{
  (void)on;
  lights_set_calls++;
}

void app_stop_all(void)
{
  stop_all_calls++;
  stub_idle = true;
}

bool app_bed_is_idle(void)
{
  return stub_idle;
}

active_switch_t app_active_command(void)
{
  return OFF;
}

static int failures = 0;

#define CHECK(cond, msg) do { \
  if (cond) { printf("PASS: %s\n", msg); } \
  else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

int main(void)
{
  // Scenario 1: app move-start acquires the lock; a switch command while
  // APP holds it is force-stopped, not executed as a switch move.
  reset_stub(true);
  bed_move_start(BED_SOURCE_APP, BED_TARGET_HEAD, BED_DIR_UP);
  CHECK(move_start_calls == 1 && bed_active_source() == BED_SOURCE_APP,
        "scenario 1a: app bed_move_start acquires the lock as APP");

  bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_HEAD, BED_DIR_UP);
  CHECK(stop_all_calls == 1 && move_start_calls == 1,
        "scenario 1b: switch move while APP holds the lock forces a stop, not a switch move");
  CHECK(bed_active_source() == BED_SOURCE_APP,
        "scenario 1c: lock still shows APP immediately after the forced stop (lazy release happens on the next call/tick)");

  // Scenario 2: switch move-start acquires the lock; an app command while
  // SWITCHES holds it is force-stopped, not executed as an app command -
  // including a non-movement verb (auto-level), matching "any input
  // whatsoever from the other source is interpreted ONLY as a stop".
  reset_stub(true);
  bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_ALL, BED_DIR_DOWN);
  CHECK(move_start_calls == 1 && bed_active_source() == BED_SOURCE_SWITCHES,
        "scenario 2a: switch bed_move_start acquires the lock as SWITCHES");

  bed_auto_level_start(BED_SOURCE_APP);
  CHECK(stop_all_calls == 1 && auto_level_start_calls == 0,
        "scenario 2b: app auto-level while SWITCHES holds the lock forces a stop, not an app auto-level start");

  // Scenario 3: stop always succeeds from either source - both the
  // non-active source's stop request AND the active source's own stop.
  reset_stub(false); // bed is "moving" under SWITCHES
  bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_HEAD, BED_DIR_UP);
  move_start_calls = 0; // ignore the acquire call itself for this scenario's counts
  stub_idle = false;

  bed_move_stop(BED_SOURCE_APP); // non-active source requests a stop
  CHECK(stop_all_calls == 1,
        "scenario 3a: non-active-source stop request still results in a full stop (never refused)");

  reset_stub(true);
  bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_HEAD, BED_DIR_UP); // re-acquire as SWITCHES
  move_start_calls = 0;
  stub_idle = false;

  bed_move_stop(BED_SOURCE_SWITCHES); // active source's own stop
  CHECK(move_stop_calls == 1 && stop_all_calls == 0,
        "scenario 3b: active source's own stop calls its natural stop action, not a forced stop");

  // Scenario 4: re-acquire after idle - the lock releases once the bed goes
  // idle (via bed_control_tick), and the next source to command acquires it.
  reset_stub(true);
  bed_control_tick();
  CHECK(bed_active_source() == BED_SOURCE_NONE,
        "scenario 4a: tick releases the lock once idle");

  bed_move_start(BED_SOURCE_APP, BED_TARGET_FOOT, BED_DIR_DOWN);
  CHECK(move_start_calls == 1 && bed_active_source() == BED_SOURCE_APP,
        "scenario 4b: next source's start acquires the lock after release");

  printf("\n%d check(s) failed\n", failures);
  return failures == 0 ? 0 : 1;
}
