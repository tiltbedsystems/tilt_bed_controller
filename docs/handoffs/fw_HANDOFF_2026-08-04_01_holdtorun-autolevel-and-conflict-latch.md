> **FILENAME CORRECTION — read this first.**
>
> This session's handoff was originally written as
> `fw_HANDOFF_2026-08-02_07_holdtorun-autolevel-and-conflict-latch.md`. **That
> date was wrong.** This session ran on **2026-08-04**; I carried 2026-08-02
> forward from the previous sessions in the same conversation without checking
> the clock. The Stop hook caught it, which is the first time that hook has
> earned its keep.
>
> The misdated file has been deleted and replaced by this one. Nothing else
> about it changed — the body below is byte-identical to what was committed in
> `fef5323`, apart from this note and the corrected header fields.
>
> **The other handoff files are NOT affected.** `_01` through `_06` were all
> committed on 2026-08-02 (`git log --date=short` confirms: `140c37b` through
> `e0d764b` are all 2026-08-02), so their names are correct. Only this one was
> wrong, and only because it was written two days after the batch it followed.
>
> **Corrected header values**, superseding §a below where they differ:
>
> | Field | Correct value |
> |---|---|
> | Session date | **2026-08-04** |
> | HEAD before session | `e0d764b` |
> | HEAD after the work commit | `5351aef` |
> | HEAD after the (misdated) handoff commit | `fef5323` |
> | HEAD after this correction | see the commit that adds this file |
>
> `git status` at the end of this correction:
>
> ```
> On branch main
> Your branch is ahead of 'origin/main' by 31 commits.
>
> Changes not staged for commit:
> 	modified:   .projectlinkstore
>
> Untracked files:
> 	.projectchecksums
> 	tilt_bed_controller_project.pdf
> ```
>
> The `git status` block inside §a was captured before the handoff commit and
> is left as it was, since it was accurate at the moment it was taken.

---

# fw_HANDOFF_2026-08-04_01 — auto-level hold-to-run; conflict becomes a latched inhibit

Three behaviour changes from the Stage C bench. The #35 stutter fix is
bench-confirmed and untouched.

**The arbitration logic is host-VERIFIED** — the unit test was extended and
all 30 checks pass. Everything above it is ASSERTED until the bench run.

---

## a. Header

| Field | Value |
|---|---|
| Repo | `tilt_bed_controller` |
| Branch | `main` |
| HEAD before session | `e0d764b` |
| HEAD after session | `5351aef` (plus this handoff's own commit) |
| Session date | 2026-08-04 (see the correction note above) |
| Build | Firmware clean, `base` config. Host unit test: 30/30 pass. Not flashed. |

### Commits this session

```
5351aef Auto-level hold-to-run; two-source conflict becomes a latched inhibit
```

### `git status` at end of session

```
On branch main
Your branch is ahead of 'origin/main' by 30 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
	modified:   .projectlinkstore

Untracked files:
	.projectchecksums
	docs/handoffs/fw_HANDOFF_2026-08-02_07_holdtorun-autolevel-and-conflict-latch.md
	tilt_bed_controller_project.pdf
```

---

## b. Design as built

### b.1 Auto-level hold-to-run

`app_auto_level_active` guards re-entry exactly as `app_move_active` does for
MOVE_START. This is the load-bearing detail: `app_auto_level_start()` resets
`first_check`, `leveling_success[]`, `auto_level_current_axis` and the
`first_print[]` array, so calling it on every 100 ms repeat would restart the
levelling state machine ten times a second and levelling would never progress.

**Holding through completion needed no special handling**, and I checked
rather than assumed. On completion `app.c:630-636` turns the motors off, sets
`auto_level_active = false` and lights the green LED — but leaves
`active_switch` at `SWITCH_AUTO_LEVEL_VALUE`. So `app_bed_is_idle()` stays
false, the watchdog's idle branch does not fire, repeats keep refreshing the
watchdog, and release cleans up. That is also exactly what the physical button
does, which is what acceptance (b) asks you to confirm.

Watchdog: `AUTO_LEVEL_START` now arms and refreshes it; expiry is the same
`app_stop_all()` + `WATCHDOG_STOP`. The comments and `BLE_GATT_DESIGN.md` §5
saying AUTO_LEVEL never arms it were correct for a latched command and are
updated.

### b.2 The latched inhibit

`TWO_SOURCE_LOCKOUT_MS` is gone — removed, not left dormant.

**Placement: `bed_control.c`.** That is the one place both sources already
meet, so a single check covers them symmetrically. This matters more than it
sounds: the requirement is that *nothing* moves until both release, so a
**physical** press while latched must also be refused. Putting the check only
in the BLE layer would have covered the app and silently missed the panel.
Host test scenario 5c is specifically that case.

**State:** `conflict_inhibit` plus `source_commanding[]` per source. Set on
any `ARBITRATE_FORCE_STOP`. Cleared only inside `note_commanding(src, false)`
when neither source is commanding.

**Release detection:**

| Source | Signal |
|---|---|
| Physical | the panel's existing release interrupt, reaching `bed_control` through `bed_move_stop()` / `bed_auto_level_stop()` |
| App | explicit `MOVE_STOP`/`AUTO_LEVEL_STOP`, or `MOVE_WATCHDOG_MS` (300 ms) with no motion repeat |

The app's silence timeout uses a tick deadline compared with a **signed**
difference so it is correct across tick-counter wraparound.

**The refresh-on-refusal rule, and why it is not optional.** A refused
MOVE_START calls `note_app_commanding()` *before* the refusal returns. Without
that, the 300 ms silence timeout would expire while the user was still
holding, the latch would clear, and the next repeat would restart the bed —
which is the exact behaviour being removed. Acceptance step (e) is the test
for this: physical released, app still held, must stay dead.

This is what your "a refused command must not extend or reset anything" means
in practice: it must not clear the latch and must not restart movement. It
*does* keep the latch alive, because the user is demonstrably still holding.

### b.3 Which LEDs go out

`app_stop_movement()` in `bed_actions.c`: `app_stop_all()` then restore amber
if the lights are on. Every force-stop path in `bed_control.c` uses it;
`STOP_ALL` still calls `app_stop_all()` directly.

**On the reviewer's decision — I think it is right, with one caveat.** The
reasoning holds: the lights are a separate latched choice and a movement
conflict should not darken the van. Worth knowing, though: `app_stop_all()`
never turned the under-bed lighting *output* off in the first place — it only
cleared the amber indicator, leaving `bedlights_on` true and the lighting on.
So before this change a conflict left the lights **on with the indicator off**,
which was simply an inconsistency. The fix removes it. It also means "STOP_ALL
remains available for turning everything off" is not quite true today: STOP_ALL
clears the amber LED but does not call `set_underbed_lighting_off()` either.
Flagged in §f.1 rather than changed, since nobody asked for it.

---

## c. Complete diff

`5351aef`, source verbatim and in full. Diffstat:

```
bed_actions.c           |  19 ++++++
bed_actions.h           |   5 ++
bed_control.c           |  78 +++++++++++++++++++++--
bed_control.h           |  17 +++++
ble_bed_service.c       | 164 +++++++++++++++++++++++++++++-------------------
docs/BLE_GATT_DESIGN.md |  47 +++++++++++---
docs/DECISIONS.md       | 107 +++++++++++++++++++++++++++++++
test/bed_control_test.c |  83 ++++++++++++++++++++++--
8 files changed, 438 insertions(+), 82 deletions(-)
```

### c.1 Source diff (including the host test)

```diff
diff --git a/bed_actions.c b/bed_actions.c
index 53722cd..d378f37 100644
--- a/bed_actions.c
+++ b/bed_actions.c
@@ -307,6 +307,25 @@ void app_release_active_switch(void)
   active_switch = OFF;
 }
 
+// Full stop of MOVEMENT only, leaving the under-bed lighting and its amber
+// indicator alone (DECISIONS.md #36).
+//
+// This is what a two-source conflict uses. app_stop_all() clears every panel
+// LED including amber, which for a conflict is wrong: the under-bed lights
+// are a latched setting the user chose separately, and a movement conflict
+// plunging the bed into darkness is a surprise, not a feature. The lighting
+// output itself was never touched by app_stop_all() - only its indicator -
+// so restoring the indicator is all that is needed to keep the two agreeing.
+//
+// STOP_ALL still calls app_stop_all() directly and keeps its full behaviour.
+void app_stop_movement(void)
+{
+  app_stop_all();
+  if (bedlights_on) {
+    set_LED_on(LED_AMBER);
+  }
+}
+
 bool app_bed_is_idle(void)
 {
   return (active_switch == OFF) && (!in_settings_mode);
diff --git a/bed_actions.h b/bed_actions.h
index fc59ead..5ffb9bb 100644
--- a/bed_actions.h
+++ b/bed_actions.h
@@ -34,6 +34,11 @@ void app_lights_set(bool on);
 // OFF. Used when a non-active source's command is force-stopped.
 void app_stop_all(void);
 
+// Full stop of MOVEMENT only - motors, auto-level and the movement LEDs
+// (green/blue/red) - leaving the under-bed lighting and its amber indicator
+// untouched. Used for two-source conflicts (DECISIONS.md #36).
+void app_stop_movement(void);
+
 // Output application for APP-sourced commands (BLE Stage C). The switch path
 // gets its outputs from switch_input.c's process_outputs(), driven by a real
 // switch edge; an app command has no edge, so ble_bed_service.c calls these
diff --git a/bed_control.c b/bed_control.c
index 150a816..ba2210c 100644
--- a/bed_control.c
+++ b/bed_control.c
@@ -9,6 +9,55 @@
 
 static bed_source_t active_source = BED_SOURCE_NONE;
 
+/*
+ * Two-source conflict inhibit (DECISIONS.md #36).
+ *
+ * A conflict - either source touching the bed while the other is moving it -
+ * stops movement and LATCHES. Nothing moves again until BOTH sources have
+ * released, at which point a fresh press on either one starts normally. This
+ * replaced a 500 ms timed lockout, which expired while a button was still
+ * held and let whichever source was still holding win - the stop-then-hand-
+ * over behaviour #12 exists to prevent.
+ *
+ * This lives here because bed_control is the one place both sources already
+ * meet, so a single check covers the app and the wall switch symmetrically.
+ *
+ * "Commanding" means a source is currently asking for motion. The switch path
+ * sets it on press and clears it on release, both through the verbs below.
+ * The app path sets it on every MOVE_START/AUTO_LEVEL_START and clears it on
+ * an explicit stop or after MOVE_WATCHDOG_MS of silence - see
+ * ble_bed_service.c, which calls bed_note_source_released() for the timeout.
+ */
+static bool conflict_inhibit = false;
+static bool source_commanding[3] = { false, false, false }; // indexed by bed_source_t
+
+static void note_commanding(bed_source_t src, bool commanding)
+{
+  if (src == BED_SOURCE_NONE) {
+    return;
+  }
+  source_commanding[src] = commanding;
+
+  // Cessation is the ONLY thing that clears the latch. A refused command must
+  // never clear it - an app repeating MOVE_START because the user is still
+  // holding is still commanding, and must keep being refused.
+  if (conflict_inhibit
+      && !source_commanding[BED_SOURCE_SWITCHES]
+      && !source_commanding[BED_SOURCE_APP]) {
+    conflict_inhibit = false;
+  }
+}
+
+void bed_note_source_released(bed_source_t src)
+{
+  note_commanding(src, false);
+}
+
+bool bed_conflict_inhibited(void)
+{
+  return conflict_inhibit;
+}
+
 typedef enum {
   ARBITRATE_NORMAL,     // src holds (or acquires) the lock - proceed normally
   ARBITRATE_FORCE_STOP  // a different source holds the lock - force a clean stop instead
@@ -45,8 +94,14 @@ static arbitrate_result_t arbitrate(bed_source_t src)
 
 bool bed_move_start(bed_source_t src, bed_target_t target, bed_direction_t dir)
 {
+  note_commanding(src, true);
+
+  if (conflict_inhibit) {
+    return false; // latched - both sources must release first (#36)
+  }
   if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
-    app_stop_all();
+    conflict_inhibit = true;
+    app_stop_movement();
     return false;
   }
   app_move_start(target, dir);
@@ -55,8 +110,10 @@ bool bed_move_start(bed_source_t src, bed_target_t target, bed_direction_t dir)
 
 bool bed_move_stop(bed_source_t src)
 {
+  note_commanding(src, false); // stops are never inhibited
+
   if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
-    app_stop_all();
+    app_stop_movement();
     return true;
   }
   app_move_stop();
@@ -65,8 +122,14 @@ bool bed_move_stop(bed_source_t src)
 
 bool bed_auto_level_start(bed_source_t src)
 {
+  note_commanding(src, true);
+
+  if (conflict_inhibit) {
+    return false; // latched (#36)
+  }
   if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
-    app_stop_all();
+    conflict_inhibit = true;
+    app_stop_movement();
     return false;
   }
   app_auto_level_start();
@@ -75,18 +138,23 @@ bool bed_auto_level_start(bed_source_t src)
 
 bool bed_auto_level_stop(bed_source_t src)
 {
+  note_commanding(src, false); // stops are never inhibited
+
   if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
-    app_stop_all();
+    app_stop_movement();
     return true;
   }
   app_auto_level_stop();
   return true;
 }
 
+// Lights are not motion: never inhibited, and never latch the inhibit. A
+// lights command from the non-holding source still force-stops the bed, which
+// is #12's existing stop-from-anyone behaviour and is left unchanged.
 bool bed_lights_set(bed_source_t src, bool on)
 {
   if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
-    app_stop_all();
+    app_stop_movement();
     return false;
   }
   app_lights_set(on);
diff --git a/bed_control.h b/bed_control.h
index e50fd4a..c288823 100644
--- a/bed_control.h
+++ b/bed_control.h
@@ -63,4 +63,21 @@ void bed_control_tick(void);
 // For future telemetry / BLE control-holder UI.
 bed_source_t bed_active_source(void);
 
+/*
+ * Two-source conflict inhibit (DECISIONS.md #36).
+ *
+ * A conflict - either source touching the bed while the other is moving it -
+ * stops movement and latches. While latched, the motion-starting verbs above
+ * refuse (bed_move_start / bed_auto_level_start return false); the stop-class
+ * verbs and bed_lights_set are never inhibited. The latch clears only when
+ * BOTH sources have released - by cessation, never by a refused command.
+ */
+bool bed_conflict_inhibited(void);
+
+// Declares that a source has stopped asking for motion. The verbs above do
+// this themselves for press/release; this is for the app's dead-man fallback,
+// where a lost MOVE_STOP is covered by MOVE_WATCHDOG_MS of silence instead
+// (see ble_bed_service.c).
+void bed_note_source_released(bed_source_t src);
+
 #endif /* BED_CONTROL_H_ */
diff --git a/ble_bed_service.c b/ble_bed_service.c
index 5d66f8f..68cfb55 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -176,23 +176,15 @@ void ble_bed_service_encode_status(const bed_status_t *in, bool tilt_fresh, uint
 #define CMD_RESULT_LEN                        2
 
 // Dead-man watchdog (BLE_GATT_DESIGN.md #5). Milliseconds via sleeptimer,
-// never loop passes (DECISIONS.md #2). The app repeats MOVE_START every
-// 100 ms while its button is held, so 300 ms tolerates two lost writes before
-// stopping the bed.
-#define MOVE_WATCHDOG_MS 300
-
-// After a two-source conflict force-stops the bed, app motion commands are
-// refused for this long (DECISIONS.md #12, #35). Without it the app's 100 ms
-// MOVE_START repeat simply restarts the bed a tick after the wall switch
-// stopped it, which is the "stop then hand over" behaviour the stop-from-
-// anyone rule exists to prevent.
+// never loop passes (DECISIONS.md #2). The app repeats MOVE_START - and, as
+// of DECISIONS #36, AUTO_LEVEL_START - every 100 ms while its button is held,
+// so 300 ms tolerates two lost writes before stopping the bed.
 //
-// BENCH-TUNABLE. Milliseconds via sleeptimer (DECISIONS.md #2), never loop
-// passes. Long enough to outlast several app repeats and to read as a
-// deliberate stop rather than a glitch; short enough that a user who really
-// does want the app to take over only has to re-press. 500 ms is a starting
-// point, not a measured value.
-#define TWO_SOURCE_LOCKOUT_MS 500
+// This same period does double duty as the app's "has stopped commanding"
+// fallback for the two-source inhibit (#36): if no motion repeat arrives for
+// MOVE_WATCHDOG_MS, the app is treated as having released, which covers a
+// lost MOVE_STOP write.
+#define MOVE_WATCHDOG_MS 300
 
 static uint8_t advertising_set_handle = 0xff;
 
@@ -248,11 +240,20 @@ static bool            app_move_active = false;
 static bed_target_t    app_move_target = BED_TARGET_HEAD;
 static bed_direction_t app_move_dir    = BED_DIR_UP;
 
-// Two-source conflict lockout (#35). While this is running, app motion
-// commands are refused so the app's repeat cannot restart a bed the other
-// source just stopped.
-static sl_sleeptimer_timer_handle_t two_source_lockout_timer;
-static volatile bool two_source_lockout_active = false;
+// Auto-level is hold-to-run as of DECISIONS #36, matching the physical
+// button. The app repeats AUTO_LEVEL_START every 100 ms while held, so this
+// flag stops a repeat from restarting the levelling state machine -
+// app_auto_level_start() resets first_check, leveling_success[] and the axis,
+// so calling it ten times a second would mean levelling never progressed.
+static bool app_auto_level_active = false;
+
+// Whether the app is still asking for motion, for the two-source inhibit
+// (#36). Refreshed by every MOVE_START/AUTO_LEVEL_START, INCLUDING refused
+// ones: a refused repeat still means the user is holding the button, so the
+// latch must stay. Cleared by an explicit stop, or by MOVE_WATCHDOG_MS of
+// silence if a stop write is lost.
+static bool     app_commanding = false;
+static uint32_t app_command_deadline_tick = 0;
 
 static void start_advertising(void)
 {
@@ -381,23 +382,35 @@ static void move_watchdog_cancel(void)
   sl_sleeptimer_stop_timer(&move_watchdog_timer);
 }
 
-static void two_source_lockout_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
+// The app has issued a motion command. Called for every MOVE_START and
+// AUTO_LEVEL_START, accepted or refused (#36 - see app_commanding).
+static void note_app_commanding(void)
 {
-  (void)handle;
-  (void)data;
-  two_source_lockout_active = false;
+  app_commanding = true;
+  app_command_deadline_tick = sl_sleeptimer_get_tick_count()
+                              + sl_sleeptimer_ms_to_tick(MOVE_WATCHDOG_MS);
+}
+
+// The app has stopped asking for motion - an explicit stop, a disconnect, or
+// the silence timeout below. Releases its half of the two-source inhibit.
+static void note_app_stopped_commanding(void)
+{
+  app_commanding = false;
+  bed_note_source_released(BED_SOURCE_APP);
 }
 
-// Starts (or extends) the post-conflict lockout. Called whenever the app's
-// motion is taken away by the other source - either because bed_control
-// force-stopped an app command, or because the bed went idle underneath a
-// move the app still believed it held.
-static void two_source_lockout_arm(void)
+// Main-loop context. The dead-man period doing its second job: no motion
+// repeat for MOVE_WATCHDOG_MS means the app has released, even if its
+// MOVE_STOP write was lost.
+static void service_app_commanding(void)
 {
-  two_source_lockout_active = true;
-  sl_sleeptimer_restart_timer(&two_source_lockout_timer,
-                               sl_sleeptimer_ms_to_tick(TWO_SOURCE_LOCKOUT_MS),
-                               two_source_lockout_callback, NULL, 0, 0);
+  if (!app_commanding) {
+    return;
+  }
+  // Signed difference so this is correct across tick-counter wraparound.
+  if ((int32_t)(sl_sleeptimer_get_tick_count() - app_command_deadline_tick) >= 0) {
+    note_app_stopped_commanding();
+  }
 }
 
 // Main-loop context. Called every tick.
@@ -418,18 +431,16 @@ static void service_move_watchdog(void)
 
   if (app_bed_is_idle() && app_move_active) {
     // The bed stopped underneath a move the app still believed it held - the
-    // wall switch took it (DECISIONS #12), or a limit stop ended it.
-    //
-    // Clear our belief, and lock the app out briefly so its 100 ms repeat
-    // cannot immediately restart a bed the other source just stopped.
+    // wall switch took it (DECISIONS #12), or a limit stop ended it. Clear
+    // our belief; the two-source latch (#36) is bed_control's job now, set at
+    // the moment of the conflict rather than inferred here.
     //
     // DO NOT cancel the watchdog here. It guards the LINK, not the bed: if
     // the app is still there it re-arms on the next repeat, and if the link
     // is gone it must still expire into a full stop. Cancelling it here was
-    // the defect - it disarmed the dead-man protection at exactly the moment
-    // the bed's state was in dispute.
+    // the defect fixed in #35 - it disarmed the dead-man protection at
+    // exactly the moment the bed's state was in dispute.
     app_move_active = false;
-    two_source_lockout_arm();
   }
 
   if (!move_watchdog_expired) {
@@ -441,6 +452,8 @@ static void service_move_watchdog(void)
   // is still there to hear it.
   move_watchdog_cancel();
   app_move_active = false;
+  app_auto_level_active = false;   // hold-to-run auto-level is watchdogged too (#36)
+  note_app_stopped_commanding();
   app_stop_all();
   send_command_result(control_connection, last_move_seq, CMD_RESULT_WATCHDOG_STOP);
 }
@@ -469,7 +482,9 @@ static void process_pending_command(void)
 
     move_watchdog_cancel();
     app_move_active = false;
-    app_stop_all();
+    app_auto_level_active = false;
+    note_app_stopped_commanding(); // never inhibited, and releases the latch
+    app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
 
     // Accepted from ANY role - control or viewer. DECISIONS #12's
     // stop-from-anyone rule, and #4's explicit carve-out.
@@ -532,12 +547,19 @@ static void process_pending_command(void)
     return;
   }
 
-  // Post-conflict lockout (#35). Applies to the motion-starting opcodes only:
-  // stops must always work, LIGHTS moves nothing, and REFRESH_TILT returned
-  // above. Reported as FORCE_STOPPED_OTHER_SOURCE because that is what it is
-  // - the tail of the force-stop the other source just caused.
-  if (two_source_lockout_active
+  // Two-source conflict inhibit (#36). Motion-starting opcodes only: stops
+  // are never inhibited, LIGHTS is not motion, and REFRESH_TILT returned
+  // above.
+  //
+  // Note note_app_commanding() BEFORE the refusal. A refused repeat still
+  // means the user is holding the button, so it must keep the app's half of
+  // the latch alive - otherwise the silence timeout would expire mid-hold,
+  // the latch would clear, and the next repeat would restart the bed. That
+  // is exactly the "release both, then press again" rule this implements.
+  // It refreshes the latch; it never clears or extends anything else.
+  if (bed_conflict_inhibited()
       && (opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)) {
+    note_app_commanding();
     send_command_result(conn, seq, CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE);
     return;
   }
@@ -552,6 +574,8 @@ static void process_pending_command(void)
       bed_target_t    target = (bed_target_t)arg0;
       bed_direction_t dir    = (bed_direction_t)arg1;
 
+      note_app_commanding();
+
       // A repeat with a DIFFERENT target/dir is MOVE_STOP then MOVE_START
       // (#4) - not a direction change under a running move.
       if (app_move_active && (target != app_move_target || dir != app_move_dir)) {
@@ -562,13 +586,10 @@ static void process_pending_command(void)
 
       if (!app_move_active) {
         if (!bed_move_start(BED_SOURCE_APP, target, dir)) {
-          // The wall switch holds the lock; bed_control turned this into a
-          // full stop instead (DECISIONS #12). The app's movement does NOT
-          // start, and the lockout stops its 100 ms repeat from restarting
-          // the bed a tick later - which was the "stop then hand over"
-          // behaviour (#35).
+          // The wall switch holds the bed; bed_control stopped movement and
+          // latched the inhibit (#36). The app's movement does NOT start, and
+          // will not start until both sources release.
           move_watchdog_cancel();
-          two_source_lockout_arm();
           result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
           break;
         }
@@ -591,6 +612,7 @@ static void process_pending_command(void)
       }
       move_watchdog_cancel();
       app_move_active = false;
+      note_app_stopped_commanding();
       bed_move_stop(BED_SOURCE_APP); // stop-class: always reports true
       app_apply_stop_outputs();
       result = CMD_RESULT_ACCEPTED;
@@ -600,15 +622,26 @@ static void process_pending_command(void)
       if (arg0 != 0 || arg1 != 0) {
         break;
       }
-      // Latched (#12): no watchdog. Drop any move watchdog still armed.
-      move_watchdog_cancel();
-      app_move_active = false;
-      if (!bed_auto_level_start(BED_SOURCE_APP)) {
-        two_source_lockout_arm(); // #35, same reasoning as MOVE_START
-        result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
-        break;
+      // HOLD-TO-RUN as of #36, matching the physical auto-level button. The
+      // app repeats this every 100 ms while held and it arms the same
+      // dead-man watchdog a move does.
+      note_app_commanding();
+
+      if (!app_auto_level_active) {
+        move_watchdog_cancel();
+        app_move_active = false;
+        if (!bed_auto_level_start(BED_SOURCE_APP)) {
+          result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
+          break;
+        }
+        app_apply_auto_level_start_outputs();
+        app_auto_level_active = true;
       }
-      app_apply_auto_level_start_outputs();
+      // A repeat must NOT re-enter bed_auto_level_start() - that resets the
+      // levelling state machine, so at 10 Hz it would never make progress.
+      // It only refreshes the watchdog, exactly like a MOVE_START repeat.
+      last_move_seq = seq;
+      move_watchdog_arm();
       result = CMD_RESULT_ACCEPTED;
       break;
 
@@ -616,8 +649,11 @@ static void process_pending_command(void)
       if (arg0 != 0 || arg1 != 0) {
         break;
       }
+      move_watchdog_cancel();
+      app_auto_level_active = false;
+      note_app_stopped_commanding();
       bed_auto_level_stop(BED_SOURCE_APP); // stop-class: always reports true
-      app_apply_stop_outputs();
+      app_apply_stop_outputs();            // clears the red/green auto-level LEDs
       result = CMD_RESULT_ACCEPTED;
       break;
 
@@ -627,7 +663,6 @@ static void process_pending_command(void)
       }
       // Latched, no watchdog, no motion.
       if (!bed_lights_set(BED_SOURCE_APP, arg0 == 1)) {
-        two_source_lockout_arm(); // #35 - this one force-stopped the bed too
         result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
         break;
       }
@@ -790,6 +825,8 @@ void ble_bed_service_tick(void)
     disconnect_stop_pending = false;
     move_watchdog_cancel();
     app_move_active = false;
+    app_auto_level_active = false;
+    note_app_stopped_commanding(); // the app is gone - release its half of the latch
     app_stop_all();
   }
 
@@ -799,6 +836,7 @@ void ble_bed_service_tick(void)
   // gated on having a subscriber.
   process_pending_command();
   service_move_watchdog();
+  service_app_commanding();
 
   if (status_subscriber_count == 0) {
     return; // #24: no subscribers - no notify traffic, no accel-sampling gate to feed yet
diff --git a/test/bed_control_test.c b/test/bed_control_test.c
index 9a46484..7d3bfa6 100644
--- a/test/bed_control_test.c
+++ b/test/bed_control_test.c
@@ -36,6 +36,7 @@ static int auto_level_start_calls;
 static int auto_level_stop_calls;
 static int lights_set_calls;
 static int stop_all_calls;
+static int stop_movement_calls; // force-stops go through app_stop_movement() (#36)
 static bool stub_idle;
 
 static void reset_stub(bool idle)
@@ -46,7 +47,14 @@ static void reset_stub(bool idle)
   auto_level_stop_calls = 0;
   lights_set_calls = 0;
   stop_all_calls = 0;
+  stop_movement_calls = 0;
   stub_idle = idle;
+
+  // Clear any latched two-source inhibit (#36) left by the previous scenario,
+  // using the real mechanism: both sources releasing. bed_control's inhibit
+  // is file-static and deliberately has no test back door.
+  bed_note_source_released(BED_SOURCE_SWITCHES);
+  bed_note_source_released(BED_SOURCE_APP);
 }
 
 void app_move_start(bed_target_t target, bed_direction_t dir)
@@ -87,6 +95,12 @@ void app_stop_all(void)
   stub_idle = true;
 }
 
+void app_stop_movement(void)
+{
+  stop_movement_calls++;
+  stub_idle = true;
+}
+
 bool app_bed_is_idle(void)
 {
   return stub_idle;
@@ -118,7 +132,7 @@ int main(void)
         "scenario 1a-ret: bed_move_start returns true when it performs the normal action");
 
   ok = bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_HEAD, BED_DIR_UP);
-  CHECK(stop_all_calls == 1 && move_start_calls == 1,
+  CHECK(stop_movement_calls == 1 && move_start_calls == 1,
         "scenario 1b: switch move while APP holds the lock forces a stop, not a switch move");
   CHECK(ok == false,
         "scenario 1b-ret: bed_move_start returns false when force-stopped instead of performing the move");
@@ -137,7 +151,7 @@ int main(void)
         "scenario 2a-ret: bed_move_start returns true when it performs the normal action");
 
   ok = bed_auto_level_start(BED_SOURCE_APP);
-  CHECK(stop_all_calls == 1 && auto_level_start_calls == 0,
+  CHECK(stop_movement_calls == 1 && auto_level_start_calls == 0,
         "scenario 2b: app auto-level while SWITCHES holds the lock forces a stop, not an app auto-level start");
   CHECK(ok == false,
         "scenario 2b-ret: bed_auto_level_start returns false when force-stopped instead of starting");
@@ -147,7 +161,7 @@ int main(void)
   reset_stub(true);
   bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_ALL, BED_DIR_DOWN);
   ok = bed_lights_set(BED_SOURCE_APP, true);
-  CHECK(stop_all_calls == 1 && lights_set_calls == 0,
+  CHECK(stop_movement_calls == 1 && lights_set_calls == 0,
         "scenario 2c: app lights-set while SWITCHES holds the lock forces a stop, not a lights change");
   CHECK(ok == false,
         "scenario 2c-ret: bed_lights_set returns false when force-stopped instead of setting lights");
@@ -162,7 +176,7 @@ int main(void)
   stub_idle = false;
 
   ok = bed_move_stop(BED_SOURCE_APP); // non-active source requests a stop
-  CHECK(stop_all_calls == 1,
+  CHECK(stop_movement_calls == 1,
         "scenario 3a: non-active-source stop request still results in a full stop (never refused)");
   CHECK(ok == true,
         "scenario 3a-ret: bed_move_stop returns true even when the stop was a forced full stop");
@@ -173,7 +187,7 @@ int main(void)
   stub_idle = false;
 
   ok = bed_move_stop(BED_SOURCE_SWITCHES); // active source's own stop
-  CHECK(move_stop_calls == 1 && stop_all_calls == 0,
+  CHECK(move_stop_calls == 1 && stop_movement_calls == 0,
         "scenario 3b: active source's own stop calls its natural stop action, not a forced stop");
   CHECK(ok == true,
         "scenario 3b-ret: bed_move_stop returns true for its own normal stop");
@@ -185,7 +199,7 @@ int main(void)
   stub_idle = false;
 
   ok = bed_auto_level_stop(BED_SOURCE_APP); // non-active source requests a stop
-  CHECK(stop_all_calls == 1 && ok == true,
+  CHECK(stop_movement_calls == 1 && ok == true,
         "scenario 3c: non-active-source auto-level stop forces a full stop and still returns true");
 
   // Scenario 4: re-acquire after idle - the lock releases once the bed goes
@@ -201,6 +215,63 @@ int main(void)
   CHECK(ok == true,
         "scenario 4b-ret: bed_move_start returns true acquiring the lock after release");
 
+  // Scenario 5: the two-source conflict LATCH (DECISIONS.md #36). After a
+  // conflict, neither source may start until BOTH have released. This is the
+  // rule that replaced the old 500 ms timed lockout, which expired while a
+  // button was still held and let the surviving source win.
+  reset_stub(true);
+  bed_move_start(BED_SOURCE_APP, BED_TARGET_HEAD, BED_DIR_UP); // APP holds
+  ok = bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_HEAD, BED_DIR_UP);
+  CHECK(ok == false && bed_conflict_inhibited(),
+        "scenario 5a: a conflict latches the inhibit");
+
+  move_start_calls = 0;
+  ok = bed_move_start(BED_SOURCE_APP, BED_TARGET_HEAD, BED_DIR_UP);
+  CHECK(ok == false && move_start_calls == 0,
+        "scenario 5b: app repeat while inhibited is refused and starts nothing");
+
+  ok = bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_HEAD, BED_DIR_UP);
+  CHECK(ok == false && move_start_calls == 0,
+        "scenario 5c: switch press while inhibited is refused too - both sources, not just the app");
+
+  ok = bed_auto_level_start(BED_SOURCE_APP);
+  CHECK(ok == false && auto_level_start_calls == 0,
+        "scenario 5d: auto-level start is inhibited as well - it is motion");
+
+  // A refused repeat must NOT clear the latch: only cessation does.
+  bed_note_source_released(BED_SOURCE_SWITCHES); // physical button released
+  CHECK(bed_conflict_inhibited(),
+        "scenario 5e: latch survives one source releasing while the other still commands");
+
+  ok = bed_move_start(BED_SOURCE_APP, BED_TARGET_HEAD, BED_DIR_UP);
+  CHECK(ok == false && move_start_calls == 0,
+        "scenario 5f: app still refused while it alone is still holding");
+
+  bed_note_source_released(BED_SOURCE_APP); // app released too
+  CHECK(!bed_conflict_inhibited(),
+        "scenario 5g: latch clears once BOTH sources have released");
+
+  ok = bed_move_start(BED_SOURCE_APP, BED_TARGET_HEAD, BED_DIR_UP);
+  CHECK(ok == true && move_start_calls == 1,
+        "scenario 5h: a fresh press after both released starts normally");
+
+  // Scenario 6: stops are never inhibited (#36 scope).
+  reset_stub(true);
+  bed_move_start(BED_SOURCE_APP, BED_TARGET_HEAD, BED_DIR_UP);
+  bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_HEAD, BED_DIR_UP); // conflict -> latched
+  CHECK(bed_conflict_inhibited(), "scenario 6a: inhibited");
+
+  ok = bed_move_stop(BED_SOURCE_APP);
+  CHECK(ok == true, "scenario 6b: bed_move_stop still works while inhibited");
+
+  reset_stub(true);
+  bed_move_start(BED_SOURCE_APP, BED_TARGET_HEAD, BED_DIR_UP);
+  bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_HEAD, BED_DIR_UP);
+  lights_set_calls = 0;
+  ok = bed_lights_set(BED_SOURCE_APP, true);
+  CHECK(lights_set_calls == 1,
+        "scenario 6c: lights are not motion and are not inhibited");
+
   printf("\n%d check(s) failed\n", failures);
   return failures == 0 ? 0 : 1;
 }
```

`docs/BLE_GATT_DESIGN.md` (§4 opcode row, §4 arbitration note, §5 watchdog
scope) and `docs/DECISIONS.md` (+#36) are docs-only; `git show 5351aef -- docs/`
reproduces them and their substance is §b.

---

## d. Raw output of every command run

No secrets; no redaction.

### d.1 Checking that hold-to-run cannot loop on completion

```
$ (Read app.c:609-668)
624	      } else if (!any_collision) {
...
630	              set_all_motors_off();
634	              auto_level_active = false;
635	              auto_level_stop_blink_LED(LED_RED);
636	              set_LED_on(LED_GREEN);
```

No `active_switch = OFF` on the completion path, so the bed is not idle after
levelling finishes. That is what makes §b.1 safe.

### d.2 Confirming the lockout was fully removed

```
$ grep -rn "two_source_lockout\|TWO_SOURCE_LOCKOUT" --include=*.c --include=*.h .
none
```

### d.3 Finding the host-test breakage before the compiler did

```
$ grep -n "app_stop_all\|app_stop_movement\|bed_note_source_released\|bed_conflict_inhibited" test/bed_control_test.c
84:void app_stop_all(void)
```

The test stubs every `bed_actions.h` function so `bed_control.c` can link
standalone. Adding `app_stop_movement()` to the real module would have broken
that link, and the force-stop assertions all counted `app_stop_all()` calls
that now go elsewhere.

### d.4 Host unit test — extended and run

```
$ gcc -std=c99 -Wall -I.. -o bed_control_test.exe bed_control_test.c ../bed_control.c
=== BUILD OK, running ===
PASS: scenario 1a: app bed_move_start acquires the lock as APP
PASS: scenario 1a-ret: bed_move_start returns true when it performs the normal action
PASS: scenario 1b: switch move while APP holds the lock forces a stop, not a switch move
PASS: scenario 1b-ret: bed_move_start returns false when force-stopped instead of performing the move
PASS: scenario 1c: lock still shows APP immediately after the forced stop (lazy release happens on the next call/tick)
PASS: scenario 2a: switch bed_move_start acquires the lock as SWITCHES
PASS: scenario 2a-ret: bed_move_start returns true when it performs the normal action
PASS: scenario 2b: app auto-level while SWITCHES holds the lock forces a stop, not an app auto-level start
PASS: scenario 2b-ret: bed_auto_level_start returns false when force-stopped instead of starting
PASS: scenario 2c: app lights-set while SWITCHES holds the lock forces a stop, not a lights change
PASS: scenario 2c-ret: bed_lights_set returns false when force-stopped instead of setting lights
PASS: scenario 3a: non-active-source stop request still results in a full stop (never refused)
PASS: scenario 3a-ret: bed_move_stop returns true even when the stop was a forced full stop
PASS: scenario 3b: active source's own stop calls its natural stop action, not a forced stop
PASS: scenario 3b-ret: bed_move_stop returns true for its own normal stop
PASS: scenario 3c: non-active-source auto-level stop forces a full stop and still returns true
PASS: scenario 4a: tick releases the lock once idle
PASS: scenario 4b: next source's start acquires the lock after release
PASS: scenario 4b-ret: bed_move_start returns true acquiring the lock after release
PASS: scenario 5a: a conflict latches the inhibit
PASS: scenario 5b: app repeat while inhibited is refused and starts nothing
PASS: scenario 5c: switch press while inhibited is refused too - both sources, not just the app
PASS: scenario 5d: auto-level start is inhibited as well - it is motion
PASS: scenario 5e: latch survives one source releasing while the other still commands
PASS: scenario 5f: app still refused while it alone is still holding
PASS: scenario 5g: latch clears once BOTH sources have released
PASS: scenario 5h: a fresh press after both released starts normally
PASS: scenario 6a: inhibited
PASS: scenario 6b: bed_move_stop still works while inhibited
PASS: scenario 6c: lights are not motion and are not inhibited

0 check(s) failed
exit=0
```

Zero warnings from `-Wall`. Scenarios 5 and 6 are new.

### d.5 Firmware build

```
$ cmake --build build --config base --target tilt_bed_controller
[1/7] Building C object .../bed_status.c.obj
[2/7] Building C object .../bed_control.c.obj
[3/7] Building C object .../bed_actions.c.obj
[4/7] Building C object .../switch_input.c.obj
[5/7] Building C object .../ble_bed_service.c.obj
[6/7] Building C object .../app.c.obj
[7/7] Linking C executable base\tilt_bed_controller.out
```

Clean, zero warnings.

### d.6 Commit

```
$ git log --oneline -1
5351aef Auto-level hold-to-run; two-source conflict becomes a latched inhibit
```

### d.7 Failures

None this session. (The previous session's chained-heredoc failure was avoided
by writing the DECISIONS text to a file and `cat`-ing it.)

---

## e. VERIFIED vs ASSERTED

### VERIFIED — executed, not just compiled

| # | Claim | Evidence |
|---|---|---|
| V1 | A conflict latches the inhibit | host test 5a, §d.4 |
| V2 | **Both** sources are refused while latched, not just the app | host test 5b + **5c** |
| V3 | Auto-level start is inhibited too | host test 5d |
| V4 | One source releasing does not clear it | host test 5e, 5f |
| V5 | It clears only when both have released, and a fresh press then works | host test 5g, 5h |
| V6 | Stops are never inhibited | host test 6b |
| V7 | Lights are not inhibited | host test 6c |
| V8 | Every pre-existing arbitration behaviour still holds | host test 1a–4b, all still passing |
| V9 | Firmware builds clean | §d.5 |
| V10 | Host test builds clean under `-Wall` | §d.4 |
| V11 | `TWO_SOURCE_LOCKOUT_MS` is fully removed | §d.2 |
| V12 | Auto-level completion leaves `active_switch` set, so hold-to-run cannot loop | `app.c:630-636`, §d.1 |

### ASSERTED

| # | Claim | Why |
|---|---|---|
| A1 | Hold-to-run auto-level feels right and stops promptly on release | Never run. |
| A2 | The watchdog covers a stalled-but-not-dropped link during auto-level | Never run — and note the bench cannot easily produce a *stall*; switching Bluetooth off produces a *drop*, which the disconnect handler already covered. Step (c) tests the watchdog path, not the stall itself. |
| A3 | Movement LEDs go out and amber stays on during a conflict | Logic is a two-line change; no hardware has confirmed it. |
| A4 | The physical→app conflict direction behaves on real hardware | The host test proves the arbitration; the interrupt timing and `process_outputs()` interaction are not modelled. |
| A5 | The stutter fix is still intact | Untouched by inspection, and step (j) re-tests it. |
| A6 | No new `reason=0x1008` | Unmeasured. |

---

## f. QUESTIONS FOR REVIEW

1. **"STOP_ALL remains available for turning everything off" is not quite
   true today.** `app_stop_all()` clears the amber LED but never calls
   `set_underbed_lighting_off()`, so STOP_ALL leaves the under-bed lights
   physically on with the indicator off. That inconsistency predates this
   change and I did not touch it. Should STOP_ALL actually kill the lights?
2. **The LED decision — I agree with it**, and §b.3 notes that it also
   removes a pre-existing inconsistency rather than only adding a nicety. No
   objection.
3. **A limit stop no longer arms anything.** In #35 the idle branch armed the
   500 ms lockout; now it only clears `app_move_active`. So after an app move
   ends at a limit, a still-held app button restarts it on the next repeat —
   it will just hit the limit again. Previously there was a 500 ms pause.
   Acceptable, or should a limit stop also latch until release?
4. **`app_commanding` is only reset by the BLE layer.** If the app disconnects
   mid-hold, the disconnect handler clears it; if the *stack* never reports a
   disconnect, the 300 ms timeout does. I believe that is complete, but it is
   the one path with two owners.

---

## g. NOT DONE

| Item | Reason |
|---|---|
| Touching the #35 double-press gate | Explicitly forbidden; bench-confirmed. `run_motor_supervision()` still outside it. |
| Pairing window, `tilt_fresh`, #29 loop-pass constants | Out of scope, untouched. |
| Release-config build | Not run. Still owed before Stage C ships. |
| Flashing / bench | User drives the bench. |
| Manual-move loop-rate measurement | Still owed, now three sessions old. |
| Making STOP_ALL kill the under-bed lighting | Raised as §f.1, not done unasked. |
| Pushing | 30 commits ahead of `origin/main`. |

---

## BENCH ACCEPTANCE

Keep the wall switch within reach — it is your abort throughout.

### Step 0 — build and flash

```
cd C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\tilt_bed_controller
```
```
git log --oneline -1
```

Expect `5351aef Auto-level hold-to-run...` or later. Build and flash as
normal. Open PuTTY on **COM6**, 115200 8N1, Session → Logging → "All session
output" → `conflict_latch.log`. Connect the app and let it bond.

### Step a — auto-level is hold-to-run

Tilt the bed a few degrees with the wall switch first.

1. Press and **hold** AUTO LEVEL in the app. Levelling runs — red LED blinks,
   roll then pitch.
2. **Release** part-way through.

**Expect:** motors stop immediately on release, and the LEDs clear — no red
blink left running, no green left lit.

### Step b — hold through completion

1. Tilt the bed again. Hold AUTO LEVEL in the app and **keep holding**.
2. Let levelling finish. Note what the LEDs do.
3. Now release.

**Expect:** exactly what the physical auto-level button does when you hold it
through completion — motors stop, green LED comes on and stays on while you
keep holding, and the LEDs clear when you release. If it behaves differently
from the physical button, note precisely how.

*(For reference, hold the physical AUTO_LEVEL button through a completion too
and compare. That comparison is the actual acceptance criterion.)*

### Step c — watchdog during auto-level

1. Tilt the bed. Hold AUTO LEVEL in the app so levelling is running.
2. Still holding, pull down the phone's notification shade and **tap
   Bluetooth off**.

**Expect:** the bed stops within about 300 ms.

Do it **twice**. Note: this exercises the watchdog path, but switching
Bluetooth off produces a clean *disconnect*, which the disconnect handler
already covered before this change. A link that stalls *without* disconnecting
is the case the watchdog uniquely covers and it is hard to produce
deliberately — so treat this as "the watchdog is wired in and does not
misfire" rather than proof of the stall case.

### Step d — conflict: app holding, physical pressed

1. Hold a direction in the app — say HEAD UP. The bed moves.
2. While still holding, press **any** button on the physical panel.

**Expect, immediately:** motors stop and the movement LEDs (green/blue/red) go
out.

3. **Keep holding both.** Wait several seconds.

**Expect:** nothing moves. Not after 500 ms, not at all. If the bed twitches
or restarts at any point, that is a failure — note how long after the
conflict it happened.

### Step e — release the physical only

From step d, **release the physical button but keep holding the app button.**

**Expect:** still nothing moves. This is the specific case the old 500 ms
lockout got wrong, and the one the refresh-on-refusal rule exists for. Wait at
least five seconds to be sure.

### Step f — release both, then re-press

From step e:

1. Release the app button too.
2. Press the app button again.

**Expect:** the bed moves normally.

### Step g — the mirror: physical holding, app pressed

1. Hold a direction on the **physical** panel. The bed moves.
2. While still holding, press a movement button **in the app**.

**Expect:** motors and movement LEDs out immediately, and they stay out while
either is held. Release both, then either source works normally again.

### Step h — the lights survive a conflict

1. Turn the under-bed lights **ON** from the app (or the physical BED_LIGHTS
   button). Confirm the lights are on and the amber LED is lit.
2. Now trigger a conflict exactly as in step d.

**Expect:** motors and the green/blue/red LEDs go out, and **the under-bed
lights and the amber LED stay ON**. If the van goes dark, that is the failure
this step is looking for.

### Step i — STOP ALL is never inhibited

1. Trigger a conflict as in step d and stay in the inhibited state (both
   buttons still held).
2. Press **STOP ALL** in the app.

**Expect:** it is accepted and works. The app should not report it as
rejected.

### Step j — the stutter must still be gone

Re-run the previous session's reproduction:

1. Hold **UP RIGHT** in the app.
2. Press a physical button, then **release the physical button first**,
   keeping the app button held.

**Expect:** no stutter, and — importantly — **no stream of `Double Press
Error` on the serial console.** Search the log afterwards:

open `conflict_latch.log` in Notepad and search for `Double Press Error`. A
few isolated occurrences from genuine two-button presses are fine; a rapid
repeating stream is not.

### Step k — normal operation unaffected

1. Single-side app moves, each side, up and down.
2. ALL UP / ALL DOWN from the app.
3. Auto-level from the app, held to completion.
4. Single-side and ALL from the physical panel.
5. Physical double-press still errors (press two physical motor buttons at
   once — `Double Press Error` should appear and motors stop).
6. Collision on an app move: raise a side into the rigid block from the app.
   It must still trip.

### Step l — full regression

All of `docs/REGRESSION.md`, phone bonded and subscribed throughout, including
§6.8. Watch for `reason=0x1008`.

### What to send back

- `conflict_latch.log`.
- Pass/fail for steps a–l.
- For step b: how the app's hold-through-completion compares to the physical
  button's, in your words.
- For steps d/e/g: whether the bed ever moved again before both buttons were
  released, and if so how long after.
- For step h: did the under-bed lights stay on.
- For step j: whether `Double Press Error` appeared as a stream.

Everything except the arbitration logic stays **ASSERTED** until this comes
back.
