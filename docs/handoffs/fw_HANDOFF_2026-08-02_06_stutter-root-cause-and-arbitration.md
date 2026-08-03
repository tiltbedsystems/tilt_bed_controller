# fw_HANDOFF_2026-08-02_06 — root cause of the 10 Hz stutter, plus watchdog and arbitration fixes

The stutter is root-caused, not guessed at, and the root cause is
**falsifiable from a log you already have**. Compiles clean; nothing run on
hardware.

---

## a. Header

| Field | Value |
|---|---|
| Repo | `tilt_bed_controller` |
| Branch | `main` |
| HEAD before session | `570be7a` |
| HEAD after session | `a22406f` (plus this handoff's own commit) |
| Session date | 2026-08-02 |
| Build | Clean, `base` config. Not flashed. |

### Commits this session

```
a22406f Fix the 10 Hz app-move stutter: a panel check running on app moves
```

### `git status` at end of session

```
On branch main
Your branch is ahead of 'origin/main' by 28 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
	modified:   .projectlinkstore

Untracked files:
	.projectchecksums
	docs/handoffs/fw_HANDOFF_2026-08-02_06_stutter-root-cause-and-arbitration.md
	tilt_bed_controller_project.pdf
```

---

## b. ROOT CAUSE

### b.1 The missing piece you asked for

Your hypothesis (a)–(d) was right as far as it went. The thing that re-clears
the state on every cycle is **`switch_input_tick()`'s double-press error
check**, at `switch_input.c:333-381`.

It is a **physical switch panel consistency check**. It compares
`app_ui_status` — the switch-board status word latched at the last *physical*
switch interrupt (`switch_input.c:397`) — against a fresh read taken 50 ms
later, and declares a double press when they **match**:

```c
if (app_ui_status_new == app_ui_status){
    printf("Double Press Error\r\n");
    set_dual_motors_off(HEAD);
    set_dual_motors_off(FOOT);
    active_switch = OFF;
    ...
}
```

`set_dual_motors_off(HEAD)` + `set_dual_motors_off(FOOT)` covers all four
motors. So this is **both** the de-energiser and the recurring
`active_switch = OFF` — the two things your account was missing.

Its only guard was the enclosing `if ((active_switch != OFF) && !any_collision
&& (!auto_level_active) && (!in_settings_mode))`. An app move sets
`active_switch` through the same `motor_button_pressed()` the switch path
uses, so the check ran on app moves — moves the physical panel had nothing to
do with.

### b.2 The complete loop

1. App MOVE_START repeat → `bed_move_start()` → `motor_button_pressed()` sets
   `active_switch` and `initial_switch_press = true` →
   `app_apply_move_outputs()` → **motors on**, LED on.
2. Next tick: `active_switch != OFF`, `initial_switch_press` true →
   `start_double_press_error_timer()` (50 ms, **periodic** —
   `sl_sleeptimer_start_periodic_timer`, `switch_input.c:107`).
3. Timer fires. No physical button is pressed. The fresh status read matches
   the stale latched one → **bogus "Double Press Error"** → **motors off**,
   `active_switch = OFF`, LEDs off.
4. `service_move_watchdog()`'s idle branch sees `app_bed_is_idle()` → cancels
   the watchdog, clears `app_move_active`.
5. Next repeat, within 100 ms → step 1.

The app's 100 ms repeat is the rate limiter. Hence exactly 10 Hz.

### b.3 The asymmetry — the LED bits are the fingerprint

This is the part that proves the mechanism rather than merely fitting it.

The status word is not just switches. From `switch_input.c:62-69`, Port 1
bits 4–7 are the **LED enable lines**:

```
 P1_7  EN_AMBERn      P1_5  EN_GREENn
 P1_6  EN_BLUEn       P1_4  EN_REDn
```

That is what normally makes a legitimate **physical** hold safe. The status is
latched at the interrupt (`switch_input.c:397`), and only *then* does
`process_outputs()` light that move's LED — so the read 50 ms later differs,
and no error fires. The check has always depended on that.

It is also exactly what makes the bug selective. The latched `app_ui_status`
freezes whichever LED was lit at the moment of the last physical interrupt. A
later app move matches it only if it lights the **same** LED:

| App command | LED lit | Matches a latch taken while that LED was on? |
|---|---|---|
| single side | GREEN (`app_apply_move_outputs`, non-ALL) | yes → stutters |
| ALL up/down | BLUE (`app_apply_move_outputs`, ALL) | yes → stutters |
| auto-level | RED | never reaches the check — see below |

That is precisely your observation: **"the stutter attaches to whichever app
command type was in use during the conflict."** Observation 4 (conflict during
a single-side move → single-side stutters, ALL does not) and observation 5
(conflict during ALL → ALL stutters, single-side does not) are the same fact
seen from two sides.

**Independent confirmation:** auto-level was never affected, and the enclosing
condition at `switch_input.c:333` already contains `(!auto_level_active)`. I
did not have to add anything to explain observation 4's "auto-level moves
normally" — the existing code excludes it. A wrong theory would not have
predicted that for free.

### b.4 Why it survived closing and reopening the app

`app_ui_status` is written in exactly one place: `switch_input.c:397`, inside
the branch that runs on a **physical** switch interrupt. Closing the app,
reopening it, or swapping Mock→BLE transport does not touch it. The primed
value persisted, so the stutter resumed as soon as the repeats did. A board
reset reinitialised it.

**This corrects one thing in your brief.** You suggested the missing watchdog
explained the persistence ("with no watchdog, no expiry ever fires
app_stop_all(), so nothing resets app state"). That is not it — the watchdog
was re-armed on every cycle by the next repeat, so it was never absent for
long. The state that actually survived is the latched status word in
`switch_input.c`. The watchdog defect is real and is fixed (§b.6), but it is
not the persistence mechanism.

### b.5 The fix, and why I did **not** separate `active_switch`

**Fix:** gate the double-press check on
`bed_active_source() == BED_SOURCE_SWITCHES`. One condition, sitting
alongside the `!auto_level_active` and `!in_settings_mode` exclusions already
on that line, and semantically exact — a physical-panel consistency check has
no business running on a move the panel did not start. The timer start is
gated the same way.

**`run_motor_supervision()` is deliberately left OUTSIDE the gate.** It lives
in the same block (`switch_input.c:344`), and it is collision and limit
detection. Gating the whole block on `BED_SOURCE_SWITCHES` — the obvious
one-line edit — would have silently removed collision protection from every
app-driven move. That would have been a far worse bug than the one being
fixed, introduced while fixing it.

**On splitting `active_switch`: I recommend against it, and I think the
smaller fix is the correct one, not merely the cheaper one.**

The diagnosis "one variable, two writers" describes the situation but not the
fault. Every other reader of `active_switch` behaves correctly when the app
writes it — that is exactly why Bed Status needed no code last session, and
why the auto-level branch, the supervision path and the collision handler all
work for app moves. Precisely one reader is wrong: a check that is
*intrinsically about the physical panel* and had no way to say so. The defect
is the missing predicate, not the shared variable.

Cost of separating, for completeness: `bed_status.c`, `bed_actions.c`,
`app.c`'s auto-level gate, `settings_mode.c`, `switch_input.c` and the
limit/collision supervision all read `active_switch`. Each would need to learn
which source is active. That is a large change across the safety path to fix
one predicate — and it would lose the Bed Status property you correctly called
a good call.

What I would accept as a reason to revisit: if a *second* panel-specific
reader turns up. One is a missing predicate; two is a missing abstraction.

### b.6 Watchdog silently disarmed (item 7)

Real defect, fixed. The idle branch called `move_watchdog_cancel()` whenever
the bed looked idle — including while the app still believed it held a move
and ownership was in dispute, which is exactly when dead-man protection
matters.

Now:

- **On collision:** still cancels. The collision handler owns the stop and its
  LED blink sequence; letting the watchdog expire into `app_stop_all()`
  mid-sequence would cut across it.
- **On plain idle:** clears `app_move_active` and **leaves the watchdog
  armed.** The watchdog guards the **link**, not the bed. A live app re-arms
  it on the next repeat; a dead link still expires into a full stop.

That is the property acceptance step (f) has to re-prove.

### b.7 Two-source conflict: stop, don't hand over (item 8)

Observation 6 was stop-then-takeover: the app's command force-stopped the
bed, and its next 100 ms repeat then found the lock released and started the
app's move.

`TWO_SOURCE_LOCKOUT_MS` = 500 ms, sleeptimer, named constant, commented
bench-tunable (#2). Armed on:

- any start-class verb returning `false` (force-stopped by the other source);
- the bed going idle underneath a move the app believed it held — which is how
  the **other** direction is covered, since that force-stop happens inside
  `bed_control` when the switch calls it, not in the BLE layer.

While it runs, `MOVE_START` and `AUTO_LEVEL_START` are refused with
`FORCE_STOPPED_OTHER_SOURCE`. **Stops are never locked out** — STOP_ALL,
MOVE_STOP, AUTO_LEVEL_STOP and REFRESH_TILT are unaffected.

One behaviour to be aware of, as specified: after the 500 ms expires, an app
button still being held **will** start the bed, because the app is still
repeating. That is what "lockout" rather than "latch" means, and it is what
you asked for.

---

## c. Complete diff

`a22406f`, source verbatim and in full. Diffstat:

```
ble_bed_service.c |  79 +++++++++++++++++++++++++++++++--
docs/DECISIONS.md | 127 ++++++++++++++++++++++++++++++++++++++++++++++++++++++
switch_input.c    |  41 +++++++++++++++---
3 files changed, 238 insertions(+), 9 deletions(-)
```

### c.1 Source diff

```diff
diff --git a/ble_bed_service.c b/ble_bed_service.c
index b1601ee..5d66f8f 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -181,6 +181,19 @@ void ble_bed_service_encode_status(const bed_status_t *in, bool tilt_fresh, uint
 // stopping the bed.
 #define MOVE_WATCHDOG_MS 300
 
+// After a two-source conflict force-stops the bed, app motion commands are
+// refused for this long (DECISIONS.md #12, #35). Without it the app's 100 ms
+// MOVE_START repeat simply restarts the bed a tick after the wall switch
+// stopped it, which is the "stop then hand over" behaviour the stop-from-
+// anyone rule exists to prevent.
+//
+// BENCH-TUNABLE. Milliseconds via sleeptimer (DECISIONS.md #2), never loop
+// passes. Long enough to outlast several app repeats and to read as a
+// deliberate stop rather than a glitch; short enough that a user who really
+// does want the app to take over only has to re-press. 500 ms is a starting
+// point, not a measured value.
+#define TWO_SOURCE_LOCKOUT_MS 500
+
 static uint8_t advertising_set_handle = 0xff;
 
 static uint8_t open_connections[2] = { SLOT_EMPTY, SLOT_EMPTY };
@@ -235,6 +248,12 @@ static bool            app_move_active = false;
 static bed_target_t    app_move_target = BED_TARGET_HEAD;
 static bed_direction_t app_move_dir    = BED_DIR_UP;
 
+// Two-source conflict lockout (#35). While this is running, app motion
+// commands are refused so the app's repeat cannot restart a bed the other
+// source just stopped.
+static sl_sleeptimer_timer_handle_t two_source_lockout_timer;
+static volatile bool two_source_lockout_active = false;
+
 static void start_advertising(void)
 {
   // Best-effort: harmless if already advertising, and legacy advertising
@@ -362,6 +381,25 @@ static void move_watchdog_cancel(void)
   sl_sleeptimer_stop_timer(&move_watchdog_timer);
 }
 
+static void two_source_lockout_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
+{
+  (void)handle;
+  (void)data;
+  two_source_lockout_active = false;
+}
+
+// Starts (or extends) the post-conflict lockout. Called whenever the app's
+// motion is taken away by the other source - either because bed_control
+// force-stopped an app command, or because the bed went idle underneath a
+// move the app still believed it held.
+static void two_source_lockout_arm(void)
+{
+  two_source_lockout_active = true;
+  sl_sleeptimer_restart_timer(&two_source_lockout_timer,
+                               sl_sleeptimer_ms_to_tick(TWO_SOURCE_LOCKOUT_MS),
+                               two_source_lockout_callback, NULL, 0, 0);
+}
+
 // Main-loop context. Called every tick.
 static void service_move_watchdog(void)
 {
@@ -369,14 +407,31 @@ static void service_move_watchdog(void)
     return;
   }
 
-  // The move ended without the app telling us: a collision trip, or a limit
-  // stop that returned the bed to idle. Nothing left to guard.
-  if (any_collision || app_bed_is_idle()) {
+  if (any_collision) {
+    // The collision handler owns the stop and its LED sequence from here.
+    // Nothing left for the watchdog to guard, and letting it expire into an
+    // app_stop_all() would cut across that sequence.
     move_watchdog_cancel();
     app_move_active = false;
     return;
   }
 
+  if (app_bed_is_idle() && app_move_active) {
+    // The bed stopped underneath a move the app still believed it held - the
+    // wall switch took it (DECISIONS #12), or a limit stop ended it.
+    //
+    // Clear our belief, and lock the app out briefly so its 100 ms repeat
+    // cannot immediately restart a bed the other source just stopped.
+    //
+    // DO NOT cancel the watchdog here. It guards the LINK, not the bed: if
+    // the app is still there it re-arms on the next repeat, and if the link
+    // is gone it must still expire into a full stop. Cancelling it here was
+    // the defect - it disarmed the dead-man protection at exactly the moment
+    // the bed's state was in dispute.
+    app_move_active = false;
+    two_source_lockout_arm();
+  }
+
   if (!move_watchdog_expired) {
     return;
   }
@@ -477,6 +532,16 @@ static void process_pending_command(void)
     return;
   }
 
+  // Post-conflict lockout (#35). Applies to the motion-starting opcodes only:
+  // stops must always work, LIGHTS moves nothing, and REFRESH_TILT returned
+  // above. Reported as FORCE_STOPPED_OTHER_SOURCE because that is what it is
+  // - the tail of the force-stop the other source just caused.
+  if (two_source_lockout_active
+      && (opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)) {
+    send_command_result(conn, seq, CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE);
+    return;
+  }
+
   uint8_t result = CMD_RESULT_REJECTED_INVALID;
 
   switch (opcode) {
@@ -498,8 +563,12 @@ static void process_pending_command(void)
       if (!app_move_active) {
         if (!bed_move_start(BED_SOURCE_APP, target, dir)) {
           // The wall switch holds the lock; bed_control turned this into a
-          // full stop instead (DECISIONS #12).
+          // full stop instead (DECISIONS #12). The app's movement does NOT
+          // start, and the lockout stops its 100 ms repeat from restarting
+          // the bed a tick later - which was the "stop then hand over"
+          // behaviour (#35).
           move_watchdog_cancel();
+          two_source_lockout_arm();
           result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
           break;
         }
@@ -535,6 +604,7 @@ static void process_pending_command(void)
       move_watchdog_cancel();
       app_move_active = false;
       if (!bed_auto_level_start(BED_SOURCE_APP)) {
+        two_source_lockout_arm(); // #35, same reasoning as MOVE_START
         result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
         break;
       }
@@ -557,6 +627,7 @@ static void process_pending_command(void)
       }
       // Latched, no watchdog, no motion.
       if (!bed_lights_set(BED_SOURCE_APP, arg0 == 1)) {
+        two_source_lockout_arm(); // #35 - this one force-stopped the bed too
         result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
         break;
       }
diff --git a/switch_input.c b/switch_input.c
index a40446f..2e968fb 100644
--- a/switch_input.c
+++ b/switch_input.c
@@ -335,15 +335,46 @@ void switch_input_tick(void)
           scan_all_currents();
           initial_switch_press = false;
 
-          // Double press error check timer
-          sl_sleeptimer_is_timer_running(&double_press_error_timer, &is_double_press_error_timer_on);
-          if (!is_double_press_error_timer_on){
-              start_double_press_error_timer();
+          // Double press error check timer. Not started for an app-sourced
+          // move - there is no second physical button to catch, and the
+          // check below is gated on the same condition anyway. Belt and
+          // braces: this also stops the periodic timer being left running
+          // across a source change.
+          if (bed_active_source() == BED_SOURCE_SWITCHES){
+              sl_sleeptimer_is_timer_running(&double_press_error_timer, &is_double_press_error_timer_on);
+              if (!is_double_press_error_timer_on){
+                  start_double_press_error_timer();
+              }
           }
       }
       run_motor_supervision(active_switch, is_raising);
 
-      if (double_press_error_timer_timeout){
+      // The double-press check below is a PHYSICAL SWITCH PANEL consistency
+      // check and must only run on a move the panel actually started.
+      //
+      // It compares the switch-board status latched at the last physical
+      // switch interrupt against a fresh read, and calls them a double press
+      // if they match. That status word includes the LED enable bits
+      // (P1_4-P1_7), which is what normally makes a legitimate physical hold
+      // differ from its own latched value - the LED for that move turns on
+      // after the latch.
+      //
+      // An APP move sets active_switch through the same
+      // motor_button_pressed() the switch path uses, so before this gate the
+      // check ran on app moves too. With no physical button pressed, the
+      // fresh read matched the stale latched value whenever the app's LED
+      // happened to be the one that was lit at the last physical interrupt -
+      // firing a bogus "Double Press Error" that killed the motors and set
+      // active_switch = OFF, which the app's next 100 ms MOVE_START repeat
+      // then undid. That was the 10 Hz stutter, and the LED bits are why it
+      // only affected whichever app command type was in use during the
+      // conflict (green = single side, blue = ALL, red = auto-level).
+      //
+      // run_motor_supervision() above is deliberately OUTSIDE this gate:
+      // collision and limit supervision must cover app moves too.
+      bool switch_sourced_move = (bed_active_source() == BED_SOURCE_SWITCHES);
+
+      if (double_press_error_timer_timeout && switch_sourced_move){
           double_press_error_timer_timeout = false;
 
           // Get source of switch interrupt
```

`docs/DECISIONS.md` (+127) adds #35; docs-only, `git show a22406f -- docs/`
reproduces it, and its substance is §b above.

---

## d. Raw output of every command run

No secrets; no redaction.

### d.1 Finding every writer of `active_switch` — the search that cracked it

```
$ grep -rn "active_switch *=" --include=*.c --include=*.h . | grep -v test/
./app.c:145:active_switch_t active_switch = OFF;
./app.c:573:      active_switch = OFF;
./bed_actions.c:45:  active_switch = switch_value;
./bed_actions.c:134:      active_switch = up ? SWITCH_UP_ALL_VALUE : SWITCH_DOWN_ALL_VALUE;
./bed_actions.c:152:  active_switch = SWITCH_AUTO_LEVEL_VALUE;
./bed_actions.c:197:  active_switch = SWITCH_BED_LIGHT_VALUE;
./bed_actions.c:219:  active_switch = OFF;
./bed_actions.c:307:  active_switch = OFF;
./switch_input.c:162:              active_switch = OFF;
./switch_input.c:173:              active_switch = OFF;
./switch_input.c:184:              active_switch = OFF;
./switch_input.c:198:              active_switch = OFF;
./switch_input.c:211:              active_switch = OFF;
./switch_input.c:365:              active_switch = OFF;
```

`switch_input.c:365` is the one that is not in `process_outputs()` and not in
`bed_actions.c` — the double-press handler. That is what led to §b.

### d.2 The double-press block

`switch_input.c:325-395` read in full. Key facts extracted:
`DOUBLE_PRESS_ERROR_CHECK_TIME_INTERVAL 50` (`switch_input.c:49`);
`sl_sleeptimer_start_periodic_timer` (`:107`) so it repeats;
`app_reset_double_press_timer()` merely stops it (`:112-115`).

### d.3 Ruling out a wrong theory

I initially suspected the comparison was unconditionally true, because
`switch_input.c:353-354` reads:

```c
get_ui_status();
app_ui_status_new = get_ui_status();
```

and the comment block at `switch_input.c:56-58` shows
`app_ui_status = ((uint16_t) (ports[0] << 8)) | ports[1];`, which looks like
`get_ui_status()` writing the global it is about to be compared against.

Checked instead of assumed:

```
$ grep -n -B4 -A14 "get_ui_status" exp_ui.c
exp_ui.c:253:uint16_t get_ui_status()
exp_ui.c-254-{
...
exp_ui.c-258-  uint16_t ui_status;
```

It uses a **local**. The line in `switch_input.c:58` is documentation of the
bit packing inside a comment, not code, and `app_ui_status` is `static` in
`switch_input.c` and not visible to `exp_ui.c` anyway. **Theory falsified**,
and the real mechanism (a stale latch plus the LED bits) is what survived.

### d.4 The LED bits

```
$ sed -n '62,70p' switch_input.c
   P0_7  UP_M4           P1_7  EN_AMBERn
   P0_6  UP_M3           P1_6  EN_BLUEn
   P0_5  UP_M2           P1_5  EN_GREENn
   P0_4  UP_M1           P1_4  EN_REDn
   P0_3  DOWN_M4         P1_3  BED_LIGHTS
   P0_2  DOWN_M3         P1_2  DOWN_ALL
   P0_1  DOWN_M2         P1_1  AUTO_LEVEL
   P0_0  DOWN_M1         P1_0  UP_ALL
```

The status word carries the LED enables. That is §b.3.

### d.5 Include check before using `bed_active_source()`

```
$ grep -n '#include' switch_input.c
...
26:#include "bed_control.h"
```

Already present — no new dependency added.

### d.6 Build

```
$ cmake --build build --config base --target tilt_bed_controller
[1/3] Building C object .../switch_input.c.obj
[2/3] Building C object .../ble_bed_service.c.obj
[3/3] Linking C executable base\tilt_bed_controller.out
```

Clean, zero warnings.

### d.7 A command that failed

Chaining two heredocs in one `bash -c` broke the parser:

```
$ cat >> docs/DECISIONS.md <<'EOF' ... EOF
  git add ... && git commit -F - <<'EOF' ... EOF
/usr/bin/bash: -c: line 151: unexpected EOF while looking for matching `''
[exit code 2]
```

Checked whether it had half-applied before retrying:

```
$ grep -c "^## 35\." docs/DECISIONS.md
0
$ git log --oneline -1
570be7a Handoff for ab12d09: ...
```

Clean failure, nothing written, nothing committed. Redone as two separate
steps.

### d.8 Commit

```
$ git log --oneline -1
a22406f Fix the 10 Hz app-move stutter: a panel check running on app moves
```

---

## e. VERIFIED vs ASSERTED

### VERIFIED — static

| # | Claim | Evidence |
|---|---|---|
| V1 | The double-press handler kills all four motors and clears `active_switch` | `switch_input.c:363-365`; HEAD+FOOT pairs cover motors A–D |
| V2 | Its only guard was `active_switch != OFF` (plus collision/auto-level/settings) | `switch_input.c:333` before the change |
| V3 | An app move sets `active_switch` via the switch path's own helper | `bed_actions.c:45`, reached from `app_move_start()` |
| V4 | The timer is **periodic**, 50 ms | `switch_input.c:49,107` |
| V5 | `app_ui_status` is written only on a physical switch interrupt | `switch_input.c:397`, sole assignment |
| V6 | The status word contains the LED enable bits | `switch_input.c:62-69`; raw output §d.4 |
| V7 | `get_ui_status()` does **not** write the global — the always-true theory is false | `exp_ui.c:253-258`; raw output §d.3 |
| V8 | Auto-level was already excluded from the check | `switch_input.c:333` `(!auto_level_active)` |
| V9 | `run_motor_supervision()` is in the same block and is now outside the new gate | §c diff |
| V10 | Builds clean | raw output §d.6 |

### ASSERTED

| # | Claim | Why |
|---|---|---|
| A1 | **This is the root cause of the observed stutter.** | Derived from the code. It explains all six observations including both halves of the asymmetry and the persistence, and it predicts a log line — but no bench run has confirmed it. **Step 0 below is the confirmation, and it needs no new firmware.** |
| A2 | The gate removes the stutter | Not run. |
| A3 | The watchdog now survives a disputed idle | Not run. Step (f) proves it. |
| A4 | The 500 ms lockout stops the takeover | Not run. |
| A5 | 500 ms is the right value | Picked, not measured. Bench-tunable. |
| A6 | Nothing else regressed | Steps (a)–(g) and the regression run. |

---

## f. QUESTIONS FOR REVIEW

1. **The lockout expires while the app button is still held**, so the bed
   restarts after 500 ms if the user never let go. That is what "lockout"
   means and it is what was specified — but "latch until the app sends
   MOVE_STOP" is the other reasonable reading. Say which you want.
2. **A limit stop also arms the lockout**, because the bed goes idle
   underneath a move the app believes it holds and the code cannot tell that
   apart from losing the lock. Harmless (restarting at a limit achieves
   nothing) but it means a 500 ms dead period after every app move that ends
   at a limit. Acceptable?
3. **The double-press check remains structurally fragile.** It depends on the
   LED bits changing between latch and re-read to avoid false positives on
   legitimate physical holds. That is incidental, not designed. It is not
   this session's bug and I did not touch it — but it is worth its own look.
4. **`FORCE_STOPPED_OTHER_SOURCE` is reused for lockout rejections.** Accurate
   (it is the tail of that force-stop) but the app cannot distinguish "just
   stopped you" from "still locked out". Add a code, or leave it?

---

## g. NOT DONE

| Item | Reason |
|---|---|
| Splitting `active_switch` per source | Assessed and recommended against, with reasons — §b.5. Say the word and I will do it. |
| Reworking the double-press check itself | Out of scope; raised as §f.3. |
| Instrumentation build | Not needed — the root cause is confirmable from a printf already in the firmware (step 0). Two further diagnostic printfs already exist commented out at `switch_input.c:356-357` if you want the status words. |
| Flashing / bench | Cannot; the user drives the bench. |
| Manual-move loop-rate measurement | Still owed, carried from two sessions ago. |
| Release-config pass | Still owed before Stage C ships. |
| Pushing | 28 commits ahead of `origin/main`. Not requested. |

---

## BENCH ACCEPTANCE

**Step 0 costs nothing and confirms the diagnosis before you flash anything.**

Keep the wall switch within reach throughout — it is your abort.

### Step 0 — confirm the root cause from the log you already have

If you still have the PuTTY log from the failing run, open it in Notepad and
search for:

```
Double Press Error
```

**If the root cause is right, that line is repeating rapidly — roughly ten
times a second — throughout the stutter.** It comes from a printf that has
been in `switch_input.c` all along; nothing was added to produce it.

If you do not have the log, reproduce it on the **old** firmware first:

1. Flash the previous commit:
   ```
   cd C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\tilt_bed_controller
   ```
   ```
   git checkout 570be7a
   ```
   Build and flash as normal, then:
   ```
   git checkout main
   ```
2. PuTTY on **COM6**, 115200 8N1, Session → Logging → "All session output" →
   `stutter_before.log`.
3. Reproduce observation 1: hold UP RIGHT in the app, press a physical
   button, release the physical button first.
4. Watch the console while it stutters.

**Expect:** `Double Press Error` scrolling at about 10 Hz. Save the log.

If you want to see the fingerprint itself, uncomment these two lines — they
are already there, just commented — at `switch_input.c:356-357`:

```c
printf("Initial UI Interrupt Source = %d, Status = %d \r\n", app_ui_interrupt, app_ui_status);
printf("New UI Interrupt Source = %d, Status = %d \r\n", app_ui_interrupt_new, app_ui_status_new);
```

The two `Status =` values will be **equal** on every stutter cycle. That is
the match that fires the bogus error. Re-comment them afterwards.

**If `Double Press Error` is NOT in the log, stop and tell me — the diagnosis
is wrong and the fix below is aimed at the wrong thing.**

### Step 1 — flash the fix

```
git log --oneline -1
```

Expect `a22406f Fix the 10 Hz app-move stutter...` or later. Build, flash,
start a fresh PuTTY log called `stutter_after.log`. Connect the app, let it
bond.

### Step a — the exact reproduction from observation 1

1. Hold **UP RIGHT** in the app. The right side rises.
2. Still holding, press any direction on the **physical** panel.
3. **Release the physical button first**, keeping the app button held.

**Expect:** no stutter. The bed is stopped (the physical press force-stopped
it) and stays stopped for about half a second; after that, since you are
still holding the app button, it resumes moving smoothly. **No 10 Hz
buzzing at any point**, and no `Double Press Error` in the log.

### Step b — observation 2's ordering

1. Hold an app direction.
2. While holding, press a physical button. Keep both held briefly, then
   release both.

**Expect:** the bed stops on the physical press and stays stopped. No
stutter.

### Step c — observation 5's ALL variant

1. Hold any **physical** button.
2. While it is held, hold **ALL UP** in the app.
3. Release the physical button, keep the app button held.

**Expect:** no stutter on ALL. Then repeat the whole thing with a
**single-side** app button instead, to confirm neither type stutters now.

### Step d — physical held, app pressed: stop, no takeover

1. Hold a direction on the **physical** panel so the bed is moving.
2. While still holding it, press and hold a movement button **in the app**.

**Expect:** the bed **stops** and does **not** start moving in the app's
direction. The app should show a force-stop result ("Stopped — wall switch is
in control" or similar).

3. Keep both held for a few seconds. **This is the important part:** for the
   first ~500 ms the app is locked out. After that, because you are still
   holding both, behaviour will oscillate between the two sources — that is
   expected with both held, and is not the stutter. Release the physical
   button and confirm the app then drives normally.

### Step e — app held, physical pressed: stop, no handover

1. Hold a movement button **in the app**.
2. While still holding, press a direction on the **physical** panel.

**Expect:** the bed stops. It does **not** start moving in the physical
button's direction.

### Step f — WATCHDOG, re-proven (do not skip)

This is the fix for the silently-disarmed watchdog and it must be re-proven,
not assumed.

1. Hold **ALL UP** in the app so the bed is moving.
2. Still holding, pull down the phone's notification shade with your other
   hand and **tap Bluetooth off**.

**Expect:** the bed stops within about 300 ms.

Do this **three times**. Then do it once more in the harder case:

3. Hold an app movement button, tap the **physical** panel once (which stops
   the bed), keep holding the app button so it resumes after the lockout, and
   **then** kill Bluetooth.

**Expect:** still stops within ~300 ms. This is the case the old code got
wrong — the watchdog had been cancelled by the disputed-idle branch.

**If the bed keeps moving after the link drops, hit the wall switch and stop
the acceptance run.**

### Step g — normal operation unaffected

With no conflict at all:

1. Single-side app moves, each side, up and down. Smooth, no stutter.
2. ALL UP and ALL DOWN from the app. Smooth.
3. AUTO LEVEL from the app on a tilted bed. Levels normally.
4. Single-side and ALL from the **physical** panel. Normal.
5. **Physical double-press still errors:** press two physical motor buttons
   at once. `Double Press Error` should appear and the motors should stop.
   The gate must not have disabled the real check.
6. Collision on an **app** move: raise a side into the rigid block from the
   app. It must still trip — `run_motor_supervision()` was deliberately kept
   outside the new gate, and this confirms it.

### Step h — full regression

All of `docs/REGRESSION.md`, phone bonded and subscribed throughout,
including §6.8. Watch for `reason=0x1008`.

### What to send back

- `stutter_before.log` (or a note that step 0 used an existing log) and
  whether `Double Press Error` was repeating at ~10 Hz.
- `stutter_after.log`.
- Pass/fail for steps a–h.
- For step f: how many of the four link-drop tests stopped the bed.
- For step g.5 and g.6 specifically: does the real double-press check still
  fire, and does collision still trip on an app move? Those two are the
  regressions this fix could plausibly have caused.

Everything stays **ASSERTED** until that comes back.
