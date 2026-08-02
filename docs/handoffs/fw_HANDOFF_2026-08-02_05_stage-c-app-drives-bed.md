# fw_HANDOFF_2026-08-02_05 — Stage C: the app drives the bed

All six motion opcodes, the dead-man watchdog, and source arbitration.
**Compiles clean. Nothing has been run on hardware.**

---

## a. Header

| Field | Value |
|---|---|
| Repo | `tilt_bed_controller` |
| Branch | `main` |
| HEAD before session | `49935e0` |
| HEAD after session | `ab12d09` (plus this handoff's own commit) |
| Session date | 2026-08-02 |
| Build | **Builds clean**, `base` config. Not flashed, not run. |

### Commits this session

```
ab12d09 Stage C: the app drives the bed
```

### `git status` at end of session

```
On branch main
Your branch is ahead of 'origin/main' by 26 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	.projectchecksums
	docs/handoffs/fw_HANDOFF_2026-08-02_05_stage-c-app-drives-bed.md
	tilt_bed_controller_project.pdf
```

Captured before the final commit, which adds this file and the
`docs/SESSION_LOG.md` entry. The three other entries were already
dirty/untracked before this session.

---

## b. Design as built

The design came from the spec (`BLE_GATT_DESIGN.md` §4/§5/§8) plus the
prompt. What follows is what was actually built, concentrating on the places
the spec was silent and on one thing the spec assumed was already true and
was not.

### b.1 The gap: routing through `bed_control` was not enough to move the bed

This is the finding that turned Stage C from plumbing into real work, and it
is worth reading before the diff.

`bed_move_start(BED_SOURCE_APP, …)` → `app_move_start()` →
`motor_button_pressed()`, which sets the motor **direction**, `is_raising`,
and `active_switch`. It does **not** energise anything. The thing that
actually turns a motor on is `switch_input.c`'s `process_outputs()`:

```c
static void process_outputs()
{
  ...
  switch (app_ui_interrupt) {
    ...
    if ((switch_pressed) && (active_switch == motor_switch_table[t].value)){
        set_LED_on(LED_GREEN);
        set_dual_motors_on(motor_switch_table[t].side);
    }
```

`app_ui_interrupt` and `switch_pressed` come from the physical switch board.
An app command produces neither. So had the opcodes been routed through the
verbs and nothing else — which is what "route through the existing
bed_control API" reads like — every piece of state would have been set
correctly and **the bed would not have moved.**

The fix: the app path applies its own outputs, through four helpers added to
`bed_actions.c` immediately next to the switch path's equivalents, each a
mirror of the matching `process_outputs()` branch:

| Helper | Mirrors |
|---|---|
| `app_apply_move_outputs(target)` | press branch: `LED_BLUE` + all motors for ALL, `LED_GREEN` + one pair otherwise |
| `app_apply_auto_level_start_outputs()` | `SWITCH_AUTO_LEVEL` press: `LED_RED`, motors off (the auto-level loop takes over) |
| `app_apply_lights_outputs()` | `SWITCH_BED_LIGHTS`: amber LED + under-bed lighting |
| `app_apply_stop_outputs()` | release branches: motors off, LEDs off, amber restored if lights are on |

They live in `bed_actions.c` rather than the BLE module deliberately — beside
the switch path's versions, so a change to one is visible next to the other.

### b.2 The subtle half of that gap: the source lock would have jammed

`app_bed_is_idle()` is `active_switch == OFF && !in_settings_mode`, and it is
what `bed_control` uses to release the source lock. On the switch path,
`process_outputs()`'s **release** branch is what sets `active_switch = OFF`.

**No release event ever arrives for an app command.** So without an explicit
reset, a single app LIGHTS_SET — which cannot move anything and looks
entirely harmless — would leave `active_switch = SWITCH_BED_LIGHT_VALUE`
forever, `app_bed_is_idle()` false forever, APP holding the lock forever, and
**every subsequent wall switch press force-stopped** (DECISIONS #12). The bed
would appear to have stopped responding to its own switches.

`app_release_active_switch()` exists for exactly this, and is called from the
lights and stop helpers. It is one line and it is the difference between
Stage C working and the bed bricking its own switch panel.

### b.3 STOP_ALL's dedicated flag

Implemented as specified. The shared slot stays last-write-wins for
everything else.

- Set in the Bluetooth handler, **before** the shared-slot copy, so a
  STOP_ALL write never lands in the slot at all.
- Cleared **only** in the main loop, and only after `app_stop_all()` has
  actually run. No other path touches it.
- Processed first in `process_pending_command()`, before the shared slot is
  even examined — so a MOVE_START sitting in the slot cannot be acted on
  ahead of a pending stop.
- Accepted from any role.

### b.4 Dead-man watchdog

300 ms, `sl_sleeptimer`, milliseconds not loop passes (#2). Armed and
refreshed only by MOVE_START from the control connection; at the app's 100 ms
repeat that tolerates two consecutive lost writes.

Cancelled by MOVE_STOP, STOP_ALL, `any_collision`, or `app_bed_is_idle()` —
the last two covering a collision trip and a limit stop, where the move ends
without the app ever saying so. AUTO_LEVEL and LIGHTS never arm it.

The sleeptimer callback sets a flag and nothing else; `service_move_watchdog()`
does the work from the tick. Expiry is `app_stop_all()` plus a
`WATCHDOG_STOP` result echoing the last MOVE_START's seq.

### b.5 Disconnect stop — the one thing to implement exactly as written

Implemented exactly as written: control-connection disconnect while APP is the
active source is a full `app_stop_all()`, not `bed_move_stop()`. The
distinction is not pedantic — `bed_move_stop()` does not end a latched
auto-level run, so a phone dropping mid-auto-level would leave the bed
levelling itself with nobody connected who could stop it.

**Deferred out of the callback.** `handle_connection_closed()` runs in the
Bluetooth stack callback, and `app_stop_all()` drives LEDs through the I2C
expander. Blocking I2C inside a stack callback is #27/#30's confirmed
radio-starvation mechanism. The callback sets `disconnect_stop_pending`; the
tick performs the stop, first thing, before anything else.

There is a real trade-off here and it is worth naming: deferring costs up to
one main-loop pass (~2.3 ms measured on the auto-level path) before the motors
actually stop. Doing it inline would have removed that delay and risked the
exact bug class this project has already chased twice. ~2.3 ms of extra travel
is nothing; another 0x1008 hunt is not.

### b.6 Arbitration

Order in `process_pending_command()`: STOP_ALL → length check → REFRESH_TILT →
viewer check → settings-mode check → opcode switch.

REFRESH_TILT is deliberately handled **before** the viewer and settings-mode
gates, because it is accepted from any role and in settings mode (#33, and
§4's arbitration note). STOP_ALL never reaches those gates at all.

Wall-switch-wins is not decided in the BLE layer — `bed_control` already
does it. The start-class verbs return `false` when arbitration converted the
command into a force-stop, which maps straight onto
`FORCE_STOPPED_OTHER_SOURCE`. The stop-class verbs always return `true` by
design (a stop always succeeds at stopping), so they always report
`ACCEPTED`.

### b.7 Bed Status — verified, not written

§5 of the prompt asked for `active_source` and `command`/`command_detail` to
reflect app movement, on the premise that "today they only ever report the
switch path".

**No code was needed.** `bed_status.c` already reads `bed_active_source()`
and `app_active_command()`; `bed_move_start()` sets the source to
`BED_SOURCE_APP`; and `app_move_start()` sets `active_switch` through the
same `motor_button_pressed()`/`all_motors_button_pressed()` the switch path
uses. Those fields only ever showed the switch path because **nothing had
ever called the verbs with `BED_SOURCE_APP`** — there was no write path. Now
there is, and they populate themselves.

Traced rather than assumed: `bed_status.c:99` (`translate_command(app_active_command(), …)`),
`bed_status.c:101` (`bed_active_source()`), `bed_actions.c:45` (`active_switch = switch_value`),
`bed_control.c:35` (`active_source = src`). Adding a second mechanism would
have created a second source of truth for no gain.

### b.8 What was deliberately not added

No re-validation of arguments the verbs already handle, no BLE-side motion
interlocks, no retry logic, no command queue. The prompt asked for none of it
and this is a test bed. The one limitation left standing — single-slot
buffering for non-STOP_ALL opcodes — is now correct by design rather than by
omission: everything in that slot is self-correcting.

---

## c. Complete diff

`ab12d09`, verbatim, source in full — never truncated. Diffstat:
```
 bed_actions.c           |  88 ++++++++++++
 bed_actions.h           |  15 ++
 ble_bed_service.c       | 365 ++++++++++++++++++++++++++++++++++++++++++------
 ble_bed_service.h       |  12 +-
 docs/BLE_GATT_DESIGN.md |  21 +--
 docs/DECISIONS.md       | 101 ++++++++++++++
 6 files changed, 543 insertions(+), 59 deletions(-)
```

### c.1 Source diff

```diff
diff --git a/bed_actions.c b/bed_actions.c
index 0683da4..53722cd 100644
--- a/bed_actions.c
+++ b/bed_actions.c
@@ -219,6 +219,94 @@ void app_stop_all(void)
   active_switch = OFF;
 }
 
+/*******************************************************************************
+ *** Output application for APP-sourced commands (BLE Stage C)
+ *
+ * The switch path gets its physical outputs from switch_input.c's
+ * process_outputs(), which is driven by app_ui_interrupt + switch_pressed -
+ * i.e. a real switch edge. An app command has neither, so bed_move_start()
+ * and friends latch direction and active_switch but NOTHING ever energises
+ * the motors or lights the LEDs. These helpers do what the matching branch of
+ * process_outputs() does, and ble_bed_service.c calls them right after the
+ * corresponding bed_control verb reports success.
+ *
+ * They deliberately live here rather than in the BLE module: this is where
+ * the switch path's equivalents live, so the two stay side by side and a
+ * change to one is obvious next to the other.
+ ******************************************************************************/
+
+static side_of_bed_t target_to_side(bed_target_t target)
+{
+  switch (target) {
+    case BED_TARGET_HEAD:  return HEAD;
+    case BED_TARGET_LEFT:  return LEFT;
+    case BED_TARGET_RIGHT: return RIGHT;
+    case BED_TARGET_FOOT:  return FOOT;
+    case BED_TARGET_ALL:   break; // caller handles ALL separately
+  }
+  return HEAD; // unreachable
+}
+
+// Mirrors process_outputs()'s press branches: LED_BLUE + all motors for an
+// ALL move, LED_GREEN + the one pair otherwise.
+void app_apply_move_outputs(bed_target_t target)
+{
+  if (target == BED_TARGET_ALL) {
+    set_LED_on(LED_BLUE);
+    set_all_motors_on();
+  } else {
+    set_LED_on(LED_GREEN);
+    set_dual_motors_on(target_to_side(target));
+  }
+}
+
+// Mirrors process_outputs()'s SWITCH_AUTO_LEVEL press branch. The auto-level
+// loop in app.c drives the motors and the blinking LED from here on.
+void app_apply_auto_level_start_outputs(void)
+{
+  set_LED_on(LED_RED);
+  set_all_motors_off();
+}
+
+// Mirrors process_outputs()'s SWITCH_BED_LIGHTS branch, then returns the bed
+// to idle.
+void app_apply_lights_outputs(void)
+{
+  if (bedlights_on) {
+    set_LED_on(LED_AMBER);
+    set_underbed_lighting_on();
+  } else {
+    set_LED_off(LED_AMBER);
+    set_underbed_lighting_off();
+  }
+  app_release_active_switch();
+}
+
+// Mirrors process_outputs()'s RELEASE branches: motors and LEDs off, bed
+// back to idle. Used for app MOVE_STOP and AUTO_LEVEL_STOP.
+void app_apply_stop_outputs(void)
+{
+  set_all_motors_off();
+  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
+    motor_state[i].motor_on = false;
+  }
+  set_all_LED_off();
+  if (bedlights_on) {
+    set_LED_on(LED_AMBER); // under-bed lighting survives a move, as it does a collision
+  }
+  app_release_active_switch();
+}
+
+// No switch-release event will ever arrive for an app command, so the app
+// path has to return active_switch to OFF itself. This is not cosmetic:
+// app_bed_is_idle() is what lets bed_control release the source lock, so
+// without it a single app command would leave APP holding the lock forever
+// and the wall switch permanently force-stopped (DECISIONS.md #12).
+void app_release_active_switch(void)
+{
+  active_switch = OFF;
+}
+
 bool app_bed_is_idle(void)
 {
   return (active_switch == OFF) && (!in_settings_mode);
diff --git a/bed_actions.h b/bed_actions.h
index a4d5e30..fc59ead 100644
--- a/bed_actions.h
+++ b/bed_actions.h
@@ -34,6 +34,21 @@ void app_lights_set(bool on);
 // OFF. Used when a non-active source's command is force-stopped.
 void app_stop_all(void);
 
+// Output application for APP-sourced commands (BLE Stage C). The switch path
+// gets its outputs from switch_input.c's process_outputs(), driven by a real
+// switch edge; an app command has no edge, so ble_bed_service.c calls these
+// after the matching bed_control verb succeeds. See the block comment above
+// their definitions in bed_actions.c.
+void app_apply_move_outputs(bed_target_t target);
+void app_apply_auto_level_start_outputs(void);
+void app_apply_lights_outputs(void);
+void app_apply_stop_outputs(void);
+
+// Returns active_switch to OFF. Required on every terminal app command -
+// without it app_bed_is_idle() never goes true and bed_control's source lock
+// is never released, locking out the wall switch (DECISIONS.md #12).
+void app_release_active_switch(void);
+
 bool app_bed_is_idle(void);
 active_switch_t app_active_command(void);
 bool app_in_settings_mode(void);
diff --git a/ble_bed_service.c b/ble_bed_service.c
index c46c727..b1601ee 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -2,18 +2,25 @@
  * ble_bed_service.c
  *
  * See ble_bed_service.h. Advertising bring-up, bonding, per-connection
- * CONTROL/VIEWER role tracking, Bed Status telemetry, and - as of the tilt
- * telemetry work (DECISIONS.md #33) - the first Command characteristic write
- * handler.
+ * CONTROL/VIEWER role tracking, Bed Status telemetry, and - as of Stage C
+ * (DECISIONS.md #34) - the full Command characteristic write path: all seven
+ * opcodes of BLE_GATT_DESIGN.md #4, the #5 dead-man watchdog, and #14 role
+ * arbitration.
  *
- * That write path handles exactly ONE opcode, REFRESH_TILT (0x07), which
- * commands no motion: it opens a tilt sampling burst and nothing else. Every
- * motion opcode in BLE_GATT_DESIGN.md #4 (0x01-0x06) is answered
- * REJECTED_INVALID. There is still no code path that can drive a motor over
- * BLE, and that remains deliberate - the write path was opened with a
- * harmless opcode specifically so the plumbing (decode, validate, result
- * notify, main-loop deferral) could be proven on the bench before anything
- * that moves the bed is wired to it.
+ * This module decodes and arbitrates; it does not reimplement bed logic.
+ * Every opcode routes through the existing bed_control.c verbs with
+ * BED_SOURCE_APP, the same verbs a physical switch press calls, so the
+ * two-source lock (DECISIONS #12) and the collision/limit supervision
+ * beneath it (#13) apply to app commands automatically.
+ *
+ * TWO RULES THAT MUST SURVIVE ANY EDIT HERE:
+ *   1. The stack callback does NO work - it copies bytes, sets a flag, and
+ *      returns. Everything else happens in main-loop context. Every opcode
+ *      touches I2C (motors and LEDs are behind an expander), and blocking
+ *      I2C inside a Bluetooth callback is the confirmed mechanism behind
+ *      this project's reason=0x1008 supervision-timeout drops
+ *      (DECISIONS #27/#30).
+ *   2. STOP_ALL never shares the deferred-command slot. See its flag below.
  *
  *  Created on: Jul 12, 2026
  */
@@ -27,7 +34,10 @@
 #include "gatt_db.h"
 #include "sl_sleeptimer.h"
 
-#include "auto_level.h" // tilt_telemetry_arm_burst() - triggers T1 and T2
+#include "auto_level.h"              // tilt_telemetry_arm_burst() - triggers T1 and T2
+#include "bed_actions.h"             // app_stop_all(), the app-path output helpers
+#include "bed_control.h"             // the command verbs every opcode routes through
+#include "motor_current_functions.h" // any_collision - cancels the move watchdog
 
 // Security-lifecycle diagnostic logging (bonding/connection state prints
 // below, at sl_bt_evt_sm_bonded/sm_bonding_failed/sm_confirm_bonding/
@@ -147,15 +157,29 @@ void ble_bed_service_encode_status(const bed_status_t *in, bool tilt_fresh, uint
 #define CMD_OFS_ARG1       2
 #define CMD_OFS_SEQ        3
 
-// Only opcode implemented so far. 0x01-0x06 are specified in #4 but not
-// built; they are REJECTED_INVALID rather than silently swallowed, so the
-// app can tell "not implemented yet" from "lost".
-#define CMD_OPCODE_REFRESH_TILT 0x07
+// Opcodes (BLE_GATT_DESIGN.md #4). All implemented as of Stage C.
+#define CMD_OPCODE_MOVE_START        0x01
+#define CMD_OPCODE_MOVE_STOP         0x02
+#define CMD_OPCODE_AUTO_LEVEL_START  0x03
+#define CMD_OPCODE_AUTO_LEVEL_STOP   0x04
+#define CMD_OPCODE_LIGHTS_SET        0x05
+#define CMD_OPCODE_STOP_ALL          0x06
+#define CMD_OPCODE_REFRESH_TILT      0x07
 
 // Command Result codes (BLE_GATT_DESIGN.md #7).
-#define CMD_RESULT_ACCEPTED         0x00
-#define CMD_RESULT_REJECTED_INVALID 0x04
-#define CMD_RESULT_LEN              2
+#define CMD_RESULT_ACCEPTED                   0x00
+#define CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE 0x01
+#define CMD_RESULT_REJECTED_VIEWER            0x02
+#define CMD_RESULT_REJECTED_SETTINGS_MODE     0x03
+#define CMD_RESULT_REJECTED_INVALID           0x04
+#define CMD_RESULT_WATCHDOG_STOP              0x05
+#define CMD_RESULT_LEN                        2
+
+// Dead-man watchdog (BLE_GATT_DESIGN.md #5). Milliseconds via sleeptimer,
+// never loop passes (DECISIONS.md #2). The app repeats MOVE_START every
+// 100 ms while its button is held, so 300 ms tolerates two lost writes before
+// stopping the bed.
+#define MOVE_WATCHDOG_MS 300
 
 static uint8_t advertising_set_handle = 0xff;
 
@@ -174,11 +198,43 @@ static uint8_t last_sent_status[gattdb_bed_status_len];
 // Deferred Command write. The stack callback ONLY fills these in and returns;
 // every decision and every side effect happens later, in main-loop context,
 // from process_pending_command(). See the handler for why (DECISIONS #27).
+//
+// This slot is last-write-wins: a second write arriving before the main loop
+// drains it overwrites the first. That is acceptable for every opcode EXCEPT
+// STOP_ALL, because the rest are self-correcting - the app repeats MOVE_START
+// every 100 ms, and a lost MOVE_STOP is caught by the watchdog within 300 ms.
 static volatile bool    command_pending = false;
 static volatile uint8_t pending_command[CMD_LEN];
 static volatile uint8_t pending_command_len = 0;   // as written, before clamping
 static volatile uint8_t pending_command_conn = SLOT_EMPTY;
 
+// STOP_ALL gets its own flag and is NEVER routed through the shared slot.
+// It is sent once, it is the app's big red button, and nothing corrects for
+// it being lost. Set only in the Bluetooth handler, cleared only by the main
+// loop after the stop has actually been performed - no other path may clear
+// it, so no other command can displace a pending stop.
+static volatile bool    stop_all_pending = false;
+static volatile uint8_t stop_all_seq  = 0;
+static volatile uint8_t stop_all_conn = SLOT_EMPTY;
+
+// Control connection dropped while APP held the bed. Handled in the main
+// loop, not in the disconnect callback: app_stop_all() drives LEDs over I2C
+// on sl_i2cspm_on_board, and blocking I2C inside a stack callback is the
+// confirmed cause of this project's reason=0x1008 drops (DECISIONS #27/#30).
+static volatile bool disconnect_stop_pending = false;
+
+// Dead-man watchdog state (BLE_GATT_DESIGN.md #5).
+static sl_sleeptimer_timer_handle_t move_watchdog_timer;
+static volatile bool move_watchdog_expired = false;  // set in timer ISR context
+static bool    move_watchdog_armed = false;
+static uint8_t last_move_seq = 0;                    // echoed on WATCHDOG_STOP
+
+// The app-sourced move currently believed to be running, so a repeat with a
+// DIFFERENT target/dir can be turned into MOVE_STOP + MOVE_START (#4).
+static bool            app_move_active = false;
+static bed_target_t    app_move_target = BED_TARGET_HEAD;
+static bed_direction_t app_move_dir    = BED_DIR_UP;
+
 static void start_advertising(void)
 {
   // Best-effort: harmless if already advertising, and legacy advertising
@@ -274,23 +330,97 @@ static void send_command_result(uint8_t connection, uint8_t seq, uint8_t result)
   (void)sc; // best-effort: no-op if the client never subscribed to results
 }
 
+/*******************************************************************************
+ *** Dead-man watchdog (BLE_GATT_DESIGN.md #5)
+ *
+ * Armed and refreshed only by MOVE_START from the control connection. AUTO_
+ * LEVEL and LIGHTS are latched commands and never arm it (#4). Cancelled by
+ * MOVE_STOP, STOP_ALL, a collision, or the bed going idle on its own (a limit
+ * stop). Expiry is a clean full stop, not a partial one.
+ ******************************************************************************/
+
+static void move_watchdog_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
+{
+  (void)handle;
+  (void)data;
+  move_watchdog_expired = true; // ISR context - flag only, the tick does the work
+}
+
+static void move_watchdog_arm(void)
+{
+  move_watchdog_armed = true;
+  move_watchdog_expired = false;
+  sl_sleeptimer_restart_timer(&move_watchdog_timer,
+                               sl_sleeptimer_ms_to_tick(MOVE_WATCHDOG_MS),
+                               move_watchdog_callback, NULL, 0, 0);
+}
+
+static void move_watchdog_cancel(void)
+{
+  move_watchdog_armed = false;
+  move_watchdog_expired = false;
+  sl_sleeptimer_stop_timer(&move_watchdog_timer);
+}
+
+// Main-loop context. Called every tick.
+static void service_move_watchdog(void)
+{
+  if (!move_watchdog_armed) {
+    return;
+  }
+
+  // The move ended without the app telling us: a collision trip, or a limit
+  // stop that returned the bed to idle. Nothing left to guard.
+  if (any_collision || app_bed_is_idle()) {
+    move_watchdog_cancel();
+    app_move_active = false;
+    return;
+  }
+
+  if (!move_watchdog_expired) {
+    return;
+  }
+
+  // No MOVE_START repeat for MOVE_WATCHDOG_MS - the app is gone, the phone
+  // slept, or the link stalled. Full clean stop, then tell the app why if it
+  // is still there to hear it.
+  move_watchdog_cancel();
+  app_move_active = false;
+  app_stop_all();
+  send_command_result(control_connection, last_move_seq, CMD_RESULT_WATCHDOG_STOP);
+}
+
+/*******************************************************************************
+ *** Command dispatch
+ ******************************************************************************/
+
 // Acts on a Command write, in MAIN-LOOP context, from ble_bed_service_tick().
 //
 // Everything this function does was deliberately kept out of the stack
-// callback. That is not stylistic: DECISIONS.md #27/#30. A blocking accel
-// I2C read reached from inside a Bluetooth callback is precisely the
-// mechanism that starved the radio past its 1000 ms supervision timeout and
-// produced the reason=0x1008 drops this project has already chased twice.
-// REFRESH_TILT's whole job is to cause accelerometer reads, so handling it
-// inline in the callback would have rebuilt that bug on purpose.
-//
-// Only one write is buffered. A second arriving before the tick drains the
-// first overwrites it - acceptable while REFRESH_TILT is the only opcode
-// (two refreshes and one refresh are the same thing, and the burst extends
-// rather than stacks), but it must be revisited before MOVE_START, whose
-// 100 ms repeat cadence and dead-man semantics make a dropped write matter.
+// callback. That is not stylistic: DECISIONS.md #27/#30. Blocking work
+// reached from inside a Bluetooth callback - an accel I2C read, an expander
+// write to drive a motor or an LED - is precisely the mechanism that starved
+// the radio past its 1000 ms supervision timeout and produced the
+// reason=0x1008 drops this project has already chased twice. Every opcode
+// here touches I2C, so every one of them has to be handled from the loop.
 static void process_pending_command(void)
 {
+  // STOP_ALL first, unconditionally, before anything else can be considered.
+  // Its own flag, so a later command in the shared slot cannot displace it.
+  if (stop_all_pending) {
+    uint8_t seq  = stop_all_seq;
+    uint8_t conn = stop_all_conn;
+    stop_all_pending = false;
+
+    move_watchdog_cancel();
+    app_move_active = false;
+    app_stop_all();
+
+    // Accepted from ANY role - control or viewer. DECISIONS #12's
+    // stop-from-anyone rule, and #4's explicit carve-out.
+    send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
+  }
+
   if (!command_pending) {
     return;
   }
@@ -310,21 +440,134 @@ static void process_pending_command(void)
   // A short write has no trustworthy seq field. Echo 0 rather than a byte
   // that was never sent; the app sees REJECTED_INVALID either way.
   uint8_t seq = (len == CMD_LEN) ? cmd[CMD_OFS_SEQ] : 0;
-  uint8_t result = CMD_RESULT_REJECTED_INVALID;
 
-  if (len == CMD_LEN
-      && cmd[CMD_OFS_OPCODE] == CMD_OPCODE_REFRESH_TILT
-      && cmd[CMD_OFS_ARG0] == 0
-      && cmd[CMD_OFS_ARG1] == 0) {
-    // T2. Accepted from EITHER role, and accepted in settings mode - see the
-    // arbitration note in BLE_GATT_DESIGN.md #4. It commands no motion, and
-    // the fresher telemetry it produces is notified to both connections
-    // anyway, so rejecting a viewer would only deny it a better reading of
-    // data it is already being sent. In settings mode the sampler's gate 2c
-    // stands down while the zero-level capture runs, so the refresh is
-    // harmlessly absorbed.
+  if (len != CMD_LEN) {
+    send_command_result(conn, seq, CMD_RESULT_REJECTED_INVALID);
+    return;
+  }
+
+  uint8_t opcode = cmd[CMD_OFS_OPCODE];
+  uint8_t arg0   = cmd[CMD_OFS_ARG0];
+  uint8_t arg1   = cmd[CMD_OFS_ARG1];
+
+  // REFRESH_TILT is not a motion command: accepted from either role and in
+  // settings mode, where the sampler stands down and it is harmlessly
+  // absorbed. Handled before the arbitration gates for that reason (#4).
+  if (opcode == CMD_OPCODE_REFRESH_TILT) {
+    if (arg0 != 0 || arg1 != 0) {
+      send_command_result(conn, seq, CMD_RESULT_REJECTED_INVALID);
+      return;
+    }
     tilt_telemetry_arm_burst();
-    result = CMD_RESULT_ACCEPTED;
+    send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
+    return;
+  }
+
+  // Everything below is motion-class and subject to arbitration.
+
+  // A viewer is a window, not a second source (#14). STOP_ALL never reaches
+  // here - it is handled above, from any role.
+  if (conn != control_connection) {
+    send_command_result(conn, seq, CMD_RESULT_REJECTED_VIEWER);
+    return;
+  }
+
+  if (app_in_settings_mode()) {
+    send_command_result(conn, seq, CMD_RESULT_REJECTED_SETTINGS_MODE);
+    return;
+  }
+
+  uint8_t result = CMD_RESULT_REJECTED_INVALID;
+
+  switch (opcode) {
+    case CMD_OPCODE_MOVE_START: {
+      if (arg0 > (uint8_t)BED_TARGET_ALL || arg1 > (uint8_t)BED_DIR_UP) {
+        break; // out-of-range target or direction
+      }
+      bed_target_t    target = (bed_target_t)arg0;
+      bed_direction_t dir    = (bed_direction_t)arg1;
+
+      // A repeat with a DIFFERENT target/dir is MOVE_STOP then MOVE_START
+      // (#4) - not a direction change under a running move.
+      if (app_move_active && (target != app_move_target || dir != app_move_dir)) {
+        bed_move_stop(BED_SOURCE_APP);
+        app_apply_stop_outputs();
+        app_move_active = false;
+      }
+
+      if (!app_move_active) {
+        if (!bed_move_start(BED_SOURCE_APP, target, dir)) {
+          // The wall switch holds the lock; bed_control turned this into a
+          // full stop instead (DECISIONS #12).
+          move_watchdog_cancel();
+          result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
+          break;
+        }
+        app_apply_move_outputs(target);
+        app_move_active = true;
+        app_move_target = target;
+        app_move_dir    = dir;
+      }
+
+      // Both a fresh start and an identical repeat refresh the watchdog.
+      last_move_seq = seq;
+      move_watchdog_arm();
+      result = CMD_RESULT_ACCEPTED;
+      break;
+    }
+
+    case CMD_OPCODE_MOVE_STOP:
+      if (arg0 != 0 || arg1 != 0) {
+        break;
+      }
+      move_watchdog_cancel();
+      app_move_active = false;
+      bed_move_stop(BED_SOURCE_APP); // stop-class: always reports true
+      app_apply_stop_outputs();
+      result = CMD_RESULT_ACCEPTED;
+      break;
+
+    case CMD_OPCODE_AUTO_LEVEL_START:
+      if (arg0 != 0 || arg1 != 0) {
+        break;
+      }
+      // Latched (#12): no watchdog. Drop any move watchdog still armed.
+      move_watchdog_cancel();
+      app_move_active = false;
+      if (!bed_auto_level_start(BED_SOURCE_APP)) {
+        result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
+        break;
+      }
+      app_apply_auto_level_start_outputs();
+      result = CMD_RESULT_ACCEPTED;
+      break;
+
+    case CMD_OPCODE_AUTO_LEVEL_STOP:
+      if (arg0 != 0 || arg1 != 0) {
+        break;
+      }
+      bed_auto_level_stop(BED_SOURCE_APP); // stop-class: always reports true
+      app_apply_stop_outputs();
+      result = CMD_RESULT_ACCEPTED;
+      break;
+
+    case CMD_OPCODE_LIGHTS_SET:
+      if (arg0 > 1 || arg1 != 0) {
+        break;
+      }
+      // Latched, no watchdog, no motion.
+      if (!bed_lights_set(BED_SOURCE_APP, arg0 == 1)) {
+        result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
+        break;
+      }
+      app_apply_lights_outputs();
+      result = CMD_RESULT_ACCEPTED;
+      break;
+
+    default:
+      // Unknown opcode. STOP_ALL cannot land here (own flag), REFRESH_TILT
+      // returned above.
+      break;
   }
 
   send_command_result(conn, seq, result);
@@ -377,6 +620,18 @@ static void handle_connection_closed(uint8_t connection)
   track_connection_closed(connection);
 
   if (connection == control_connection) {
+    // BLE_GATT_DESIGN.md #5, implemented exactly as written: the control
+    // connection dropping while APP holds the bed is a full clean stop, NOT
+    // bed_move_stop(). bed_move_stop() would leave a latched auto-level run
+    // going with nobody connected to stop it.
+    //
+    // Flagged, not done here: this is the disconnect callback, and
+    // app_stop_all() drives LEDs over I2C. Blocking I2C in a stack callback
+    // is DECISIONS #27/#30's confirmed radio-starvation mechanism.
+    if (bed_active_source() == BED_SOURCE_APP) {
+      disconnect_stop_pending = true;
+    }
+
     control_connection = SLOT_EMPTY;
     control_status_subscribed = false;
 
@@ -458,11 +713,21 @@ bool ble_bed_service_status_subscribed(void)
 
 void ble_bed_service_tick(void)
 {
+  // Control link dropped mid-command (BLE_GATT_DESIGN.md #5). Deferred out
+  // of the disconnect callback; done here, first, before anything else.
+  if (disconnect_stop_pending) {
+    disconnect_stop_pending = false;
+    move_watchdog_cancel();
+    app_move_active = false;
+    app_stop_all();
+  }
+
   // Drained BEFORE the subscriber check: a Command write is valid from a
   // bonded connection whether or not it subscribed to Bed Status, and its
   // Command Result must still be sent. Only the telemetry notify below is
   // gated on having a subscriber.
   process_pending_command();
+  service_move_watchdog();
 
   if (status_subscriber_count == 0) {
     return; // #24: no subscribers - no notify traffic, no accel-sampling gate to feed yet
@@ -683,6 +948,18 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
         break;
       }
 
+      // STOP_ALL never goes in the shared slot. It is sent once, nothing
+      // corrects for losing it, and a MOVE_START repeat arriving in the same
+      // tick would otherwise overwrite it. Own flag, cleared only by the main
+      // loop once the stop has actually happened.
+      if (av->value.len == CMD_LEN
+          && av->value.data[CMD_OFS_OPCODE] == CMD_OPCODE_STOP_ALL) {
+        stop_all_seq  = av->value.data[CMD_OFS_SEQ];
+        stop_all_conn = av->connection;
+        stop_all_pending = true;
+        break;
+      }
+
       uint8_t copy_len = av->value.len;
       if (copy_len > CMD_LEN) {
         copy_len = CMD_LEN;
diff --git a/ble_bed_service.h b/ble_bed_service.h
index 887ab61..09490ca 100644
--- a/ble_bed_service.h
+++ b/ble_bed_service.h
@@ -7,14 +7,12 @@
  * tracking (assigned once a connection finishes bonding, not at raw
  * connection-open), and the adaptive Bed Status notify cadence (#6/#24).
  *
- * This module is glue, not a second source of truth: Stage C will feed
- * decoded Command writes into the existing bed_control.c verbs exactly as a
- * physical switch press does today - no bed logic is reimplemented here.
+ * This module is glue, not a second source of truth: Stage C feeds decoded
+ * Command writes into the existing bed_control.c verbs exactly as a physical
+ * switch press does - no bed logic is reimplemented here.
  *
- * The Command write path now exists but carries exactly one opcode,
- * REFRESH_TILT (0x07), which commands no motion. Motion opcodes 0x01-0x06
- * are specified in BLE_GATT_DESIGN.md #4 and answered REJECTED_INVALID, so
- * there is still no way to drive a motor over BLE.
+ * All seven opcodes of BLE_GATT_DESIGN.md #4 are implemented, along with the
+ * #5 dead-man watchdog and #14 role arbitration. The app can drive the bed.
  *
  *  Created on: Jul 12, 2026
  */
```

### c.2 Docs diff

`docs/BLE_GATT_DESIGN.md` (+20/-8) marks all seven opcodes implemented and
documents the STOP_ALL slot carve-out. `docs/DECISIONS.md` (+101) adds #34.
Both docs-only; `git show ab12d09 -- docs/` reproduces them, and #34's
substance is §b above.

---

## d. Raw output of every command run

No secrets in any output; no redaction applied.

### d.1 Reading the existing control API

```
$ cat bed_actions.h; grep -rn "app_stop_all" --include=*.c --include=*.h .
[bed_actions.h contents]
./bed_actions.c:207:void app_stop_all(void)
./bed_actions.h:35:void app_stop_all(void);
./bed_control.c:24: * app_stop_all() via ARBITRATE_FORCE_STOP, matching DECISIONS.md #12's "any
./bed_control.c:49:    app_stop_all();
./bed_control.c:59:    app_stop_all();
./bed_control.c:69:    app_stop_all();
./bed_control.c:79:    app_stop_all();
./bed_control.c:89:    app_stop_all();
./test/bed_control_test.c:84:void app_stop_all(void)
```

### d.2 Tracing how motors actually get energised — the §b.1 finding

```
$ grep -rn "process_outputs\|initial_switch_press" --include=*.c --include=*.h .
./app.c:81:// settings_mode_process_outputs(), which switches on the same
./app.h:96: * (C4, DECISIONS.md #12): process_outputs() and app_process_action()'s own
./bed_actions.c:47:  initial_switch_press = true;
./bed_actions.c:205:// regardless - there is no switch-release event driving process_outputs()
./motor_current_functions.c:108:bool initial_switch_press = false;
./settings_mode.c:135:void settings_mode_process_outputs(void)
./switch_input.c:149:static void process_outputs()
./switch_input.c:222:      settings_mode_process_outputs();
```

`switch_input.c:149-218` then showed `process_outputs()` switching on
`app_ui_interrupt` with `switch_pressed`, neither of which an app command
produces. That is the whole of §b.1.

### d.3 Confirming Bed Status needed no change

```
$ grep -rn "motor_button_pressed\|all_motors_button_pressed" --include=*.c --include=*.h . | grep -v test/
./bed_actions.c:43:static void motor_button_pressed(active_switch_t switch_value, side_of_bed_t side, bool up)
./bed_actions.c:74:static void all_motors_button_pressed(bool up)
./bed_actions.c:122:      motor_button_pressed(up ? SWITCH_UP_HEAD_VALUE : SWITCH_DOWN_HEAD_VALUE, HEAD, up);
...
```

`bed_actions.c:45` is `active_switch = switch_value`, which is what
`app_active_command()` returns and `bed_status.c` reports. §b.7.

### d.4 Type check for the target→side mapping

```
$ cat bed_types.h
...
typedef enum {
    HEAD,
    LEFT,
    RIGHT,
    FOOT
} side_of_bed_t;
```

`bed_target_t` is `HEAD, LEFT, RIGHT, FOOT, ALL`, so 0-3 align — but
`target_to_side()` was written as an explicit switch rather than a cast, so
the mapping does not silently depend on two enums staying in the same order.

### d.5 Locating the toolchain — three failed attempts

```
$ cmake --build build --config base --target tilt_bed_controller
/usr/bin/bash: line 1: cmake: command not found
/usr/bin/bash: line 1: cmake: command not found
```

```
$ Get-Command cmake / ninja / arm-none-eabi-gcc
cmake not on PATH
found: C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
--- ninja / arm-gcc ---
ninja : not on PATH
arm-none-eabi-gcc : not on PATH
```

`autogen_toolchain.cmake` resolves all three by shelling out to `slt where`,
and `slt` is not on PATH either:

```
$ if (Get-Command slt) {"yes"} else {"no"}
no
```

First attempt at the fallback paths failed because I passed the literal
CMake `${...}` strings to `Test-Path`:

```
dir: ${TOOLCHAIN_SLT_PATH}/bin/ ${USER_DIR}/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/
False
False
```

Resolved by globbing the conan store directly:

```
$ Get-ChildItem "$env:USERPROFILE\.silabs\slt\installs\conan\p" -Filter "gcc*" -Directory
C:\Users\adunc\.silabs\slt\installs\conan\p\gcc-a1a188b577d6ca\p\bin\arm-none-eabi-gcc.exe -> False
C:\Users\adunc\.silabs\slt\installs\conan\p\gcc-a999d2e027337f\p\bin\arm-none-eabi-gcc.exe -> True
```

Two GCC installs are present; only the second has the binaries, and it is the
one `autogen_toolchain.cmake` names as its fallback.

### d.6 The build

`base` config — the only configuration this project defines. **Not Release.**

```
$ $env:PATH = "<gcc-a999d2e027337f\p\bin>;<ninja1a38fc85adcf7\p>;<VS cmake bin>;$env:PATH"
$ cmake --build build --config base --target tilt_bed_controller
[1/7] Building C object .../bed_control.c.obj
[2/7] Building C object .../bed_status.c.obj
[3/7] Building C object .../bed_actions.c.obj
[4/7] Building C object .../switch_input.c.obj
[5/7] Building C object .../ble_bed_service.c.obj
[6/7] Building C object .../app.c.obj
[7/7] Linking C executable base\tilt_bed_controller.out
```

`auto_level.c` did **not** appear in that list, which I did not trust — it
was modified in the previous commit, so a stale object could have hidden a
compile error in the tilt telemetry work. Forced it and the rest of the app
layer to rebuild:

```
$ foreach ($f in @('auto_level.c','bed_status.c','ble_bed_service.c','bed_actions.c','app.c')) { (Get-Item $f).LastWriteTime = Get-Date }
$ cmake --build build --config base --target tilt_bed_controller
[1/6] Building C object .../bed_status.c.obj
[2/6] Building C object .../bed_actions.c.obj
[3/6] Building C object .../app.c.obj
[4/6] Building C object .../ble_bed_service.c.obj
[5/6] Building C object .../auto_level.c.obj
[6/6] Linking C executable base\tilt_bed_controller.out
```

Clean, zero warnings, both times.

```
$ arm-none-eabi-size build/base/tilt_bed_controller.out
   text	   data	    bss	    dec	    hex	filename
 240956	    780	  72480	 314216	  4cb68	...\tilt_bed_controller.out
```

Touching mtimes changed no content:

```
$ git status --short
 M .projectlinkstore
 M bed_actions.c
 M bed_actions.h
 M ble_bed_service.c
 M ble_bed_service.h
 M docs/BLE_GATT_DESIGN.md
```

(only the files actually edited).

### d.7 Commit

```
$ git commit ... && git log --oneline -1
ab12d09 Stage C: the app drives the bed
```

### d.8 Failures

Four, all mine, all recovered: two `cmake: command not found` (§d.5), one
`Test-Path` on unexpanded CMake variables (§d.5), and two empty greps hunting
for the SDK path. No build error at any point.

---

## e. VERIFIED vs ASSERTED

### VERIFIED

| # | Claim | Evidence |
|---|---|---|
| V1 | **The whole app layer compiles clean**, zero warnings | raw output §d.6, both builds |
| V2 | It links; binary is 240956 text / 780 data / 72480 bss | `arm-none-eabi-size`, §d.6 |
| V3 | The previous commit's tilt telemetry compiles | `auto_level.c` forced to rebuild, §d.6 — this retires `fw_HANDOFF_..._04`'s ASSERTED A1 |
| V4 | The build was the `base` config, not Release | `CMakePresets.json` defines exactly one configuration, `base` |
| V5 | `process_outputs()` is switch-edge driven, so the app path needed its own outputs | `switch_input.c:149-218`; §d.2 |
| V6 | `active_switch` is what releases the source lock | `bed_actions.c:222` `app_bed_is_idle()`; `bed_control.c:30-36` |
| V7 | Bed Status already reports app source/command | `bed_status.c:99,101`; `bed_actions.c:45`; `bed_control.c:35`; §b.7 |
| V8 | STOP_ALL cannot be displaced | own flag set before the slot copy in the handler; cleared only after `app_stop_all()`; processed first — §c.1 |
| V9 | No work happens in a stack callback | the `attribute_value` case only copies/sets flags; `handle_connection_closed()` only sets `disconnect_stop_pending`; the watchdog callback only sets `move_watchdog_expired` — §c.1 |
| V10 | Watchdog timing is ms via sleeptimer, not loop passes | `MOVE_WATCHDOG_MS 300` + `sl_sleeptimer_ms_to_tick()`, §c.1 |
| V11 | AUTO_LEVEL and LIGHTS never arm the watchdog | no `move_watchdog_arm()` in those cases; §c.1 |
| V12 | Disconnect uses `app_stop_all()`, not `bed_move_stop()` | `ble_bed_service_tick()`'s disconnect block, §c.1 |
| V13 | `tilt_fresh` and the flags byte still untouched | `ble_bed_service_encode_status()` unchanged; call site still passes `false` |

### ASSERTED — everything about behaviour

| # | Claim | Why only ASSERTED |
|---|---|---|
| A1 | **The bed moves from the app at all.** | Never flashed. A clean build proves the types line up, nothing more. |
| A2 | The output helpers energise the right motors with the right LEDs | Transcribed from `process_outputs()` by eye. Never executed. |
| A3 | The source lock releases after an app command | §b.2's fix is reasoned, not observed. If it is wrong the wall switch stops working, which is why acceptance step (h) exists. |
| A4 | The watchdog stops the bed within ~300 ms of link loss | Never triggered. |
| A5 | The disconnect stop fires and ends a latched auto-level | Never triggered. |
| A6 | Wall-switch-wins arbitration behaves against a live app move | `bed_control`'s logic is host-tested, but never with APP as a real second source. |
| A7 | Viewer rejection works | Needs a second phone; may not even be testable. |
| A8 | Live tilt keeps updating during app-driven movement | The sampler gates on "a motor is energised", which should not care who started it — but that is the assumption the prompt flagged, and it is still an assumption. |
| A9 | No new `reason=0x1008` drops | This change adds I2C work on the command path. Unmeasured. |
| A10 | Loop rate is unaffected | Still no manual-move loop-rate figure — carried over from the previous handoff and still open. |

---

## f. QUESTIONS FOR REVIEW

1. **The deferred disconnect stop costs up to one loop pass (~2.3 ms) before
   motors stop.** Deliberate — inline would mean blocking I2C in a stack
   callback, the #27/#30 mechanism. Confirm you want it that way; the
   alternative is a targeted inline `set_all_motors_off()` (GPIO/PWM only, no
   expander) followed by the full stop from the loop, which would cut the
   delay while keeping I2C out of the callback.
2. **`app_apply_*_outputs()` duplicates `process_outputs()`'s branches by
   transcription.** Two places now know how to light an LED and energise a
   pair, and they can drift. The alternative is refactoring
   `process_outputs()` into source-neutral helpers both paths call — a bigger
   change in a path the switch panel depends on, which is why I did not do it
   unasked. Worth doing before this grows further?
3. **A collision cancels the watchdog** via `any_collision`, so the app can
   keep sending MOVE_START repeats during the collision blink sequence with
   no watchdog armed. `bed_control`/`app.c` still suppress motion while
   `any_collision` is set, so nothing moves — but the app gets `ACCEPTED`
   replies for commands that do nothing. Should those report something else?
4. **Settings-mode rejection uses `app_in_settings_mode()`**, which covers all
   of settings mode, not just the zero-level capture. Matches the spec, but it
   does mean the app is fully locked out for as long as the user is in
   settings mode with no indication beyond the result code.
5. **`last_move_seq` echoes the most recent MOVE_START's seq on
   WATCHDOG_STOP.** The spec does not say which seq a watchdog stop should
   carry — it is not a reply to a command. Reasonable, but confirm.

---

## g. NOT DONE

| Item | Reason |
|---|---|
| Pairing window | Explicitly excluded — separate session. |
| `tilt_fresh` | Explicitly excluded. Still hardcoded 0, flags byte untouched. |
| DECISIONS #29 loop-pass constants | Explicitly excluded. |
| Release-config build | Explicitly excluded. The `base` config is the only one defined and is what was built. |
| Flashing / any bench work | Cannot: the user drives the bench. Everything behavioural is ASSERTED. |
| Manual-move loop-rate measurement | Carried over from the previous handoff, still owed, and now more relevant — this change adds work to the command path. |
| Command queueing for non-STOP_ALL opcodes | Not needed: everything else is self-correcting. STOP_ALL got the dedicated flag instead. |
| Refactoring `process_outputs()` to be source-neutral | Bigger change in the switch path; raised as §f.2 rather than taken unasked. |
| `REGRESSION.md` edits | Not asked. Acceptance steps are below. |
| Pushing | 26 commits ahead of `origin/main`. Not requested. |

---

## BENCH ACCEPTANCE — step by step

Sequenced easy to hard, exactly as specified. Nothing below has been run.
Every command is spelled out. `>` lines are what you type.

**Before anything: have the wall switch within reach.** Steps (e) onward move
the bed under app control for the first time. The wall switch force-stops the
app by design (that is step h), so it is your manual abort throughout.

### Step 0 — build and flash

Open a terminal in the project:

```
cd C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\tilt_bed_controller
```

Confirm you are on the right commit:

```
git log --oneline -1
```

Expect `ab12d09 Stage C: the app drives the bed` (or later). Build and flash
the way you normally do from VS Code. Then open PuTTY on **COM6** and turn on
logging: Session → Logging → "All session output" → filename
`stagec_acceptance.log`.

Watch the boot banner appear, then connect the app and let it bond.

### Step a — LIGHTS

Tap LIGHT ON in the app, then LIGHT OFF.

**Expect:** under-bed lighting turns on and off, amber LED follows, **no motor
moves at all**. This is the smallest possible proof that a write reaches
`bed_control` and back out to hardware.

**Then immediately press a wall switch button.** It must work normally. If the
wall switch is dead after an app lights command, that is §b.2's lock-release
bug and everything after this step is invalid — stop and report it.

### Step b — STOP_ALL with nothing running

With the bed idle, tap the app's red STOP button.

**Expect:** nothing happens. No motion, no LED change, no reset, no error.
Boring is correct. This proves the dedicated STOP_ALL flag path runs without
tripping over an idle bed.

### Step c — AUTO_LEVEL from the app

Tilt the bed a few degrees in both roll and pitch using the wall switch, then
tap AUTO LEVEL in the app.

**Expect:** identical behaviour to holding AUTO_LEVEL on the wall switch — red
LED blinks, ROLL levels first, then PITCH, motors stop, green LED, "Bed is
level" on the UART. It is latched: you do not hold anything.

### Step d — STOP_ALL during auto-level

Start auto-level from the app again on a tilted bed. **While it is running**,
tap STOP.

**Expect:** everything stops promptly — motors off, LED sequence ends. The bed
does not resume.

### Step e — MOVE, one side (the big one)

Press and **hold** HEAD UP in the app.

**Expect:** the head rises while you hold, and stops when you release. This is
hold-to-run over BLE working end to end for the first time.

Repeat for HEAD DOWN, LEFT UP/DOWN, RIGHT UP/DOWN, FOOT UP/DOWN.

**If a motor runs on after you release**, the watchdog should stop it within
about 300 ms. If it does not, hit the wall switch and report it.

### Step f — MOVE, ALL

Hold ALL UP, then ALL DOWN.

**Expect:** all four actuators move together, stop on release.

### Step g — WATCHDOG

This is the one that matters most. Two methods; do the second, it is cleaner.

*Method 1 (walk out of range):* awkward indoors, hard to time.

*Method 2 — use this one:*

1. Hold ALL UP in the app so the bed is moving.
2. **While still holding, pull down the phone's notification shade with your
   other hand and tap the Bluetooth toggle off.**

**Expect:** the bed stops within about 300 ms — call it "immediately, but you
can just about notice it". It must not keep running.

On the UART you should see a `connection_closed` line. Note its `reason=`
code.

Do this **three times**, and once more while holding a single side rather than
ALL.

**If the bed keeps moving after the link drops, hit the wall switch
immediately and stop the acceptance run.** That is the one genuinely bad
outcome in this change.

### Step h — WALL SWITCH WINS

1. Hold a movement button in the app so the bed is moving.
2. **While still holding it in the app**, press and hold UP HEAD on the wall
   switch.

**Expect:** the bed stops. The wall switch press is interpreted as a stop, not
as a new movement (DECISIONS #12). The app should show a
`FORCE_STOPPED_OTHER_SOURCE` result — in the app that may surface as
"Stopped — wall switch is in control".

3. Release both. Press the wall switch again on its own.

**Expect:** now it moves normally. The lock has released.

4. Now the reverse: hold the wall switch to start a move, then press a
   movement button in the app.

**Expect:** the bed stops, and the app reports it was force-stopped.

### Step i — VIEWER REJECTION (skip if you only have one phone)

**Only if a second phone with the app is available.** If not, **skip this
step and write "skipped — one phone only" in the record.** It is not a
blocker.

With both phones connected — the first is CONTROL, the second is VIEWER:

1. From the **second** phone, try a movement command. Expect it to be
   rejected ("Another phone has control").
2. From the **second** phone, tap STOP. Expect it to be **accepted** — stop
   works from anyone.

### Step j — live tilt during app movement

The prompt flagged this as an assumption worth one look, so look at it
explicitly rather than in passing.

1. Watch the tilt readout on the phone.
2. Hold a movement button **in the app** and keep watching.

**Expect:** roll and pitch update continuously while the bed moves, exactly as
they do for a wall-switch move. The sampler keys off "a motor is energised"
and should not care who started it — this step is checking that belief.

3. Release, and confirm the value settles over the next half second rather
   than freezing.

### Step k — full regression

Run all of `docs/REGRESSION.md` with the phone bonded and subscribed
throughout, into a fresh copy of the record. Do not skip §6.8 (sustained
auto-level with the phone subscribed) — that is the check that caught
DECISIONS #26 and #30.

Watch the UART for `reason=0x1008` throughout. Any occurrence: stop, save the
log, report it with the surrounding ten lines.

### What to send back

- `stagec_acceptance.log`.
- Pass/fail for steps a through k, with step (i) marked skipped if you have
  one phone.
- For step g: how many of the four link-drop tests stopped the bed, and the
  `reason=` codes.
- For step h: whether the app showed a force-stop result, and whether the
  wall switch worked normally afterwards.
- Anything that moved when you did not expect it to.

Everything in this change stays **ASSERTED** until that comes back. A clean
build is not a working bed.
