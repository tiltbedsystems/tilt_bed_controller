# fw_HANDOFF_2026-08-05_02 — double-press misfire, arbitration bypass, motion fail-safe

Both bench symptoms root-caused to one trigger. **One of them turned out to be
motion with `bed_control` bypassed entirely** — §b.3, flagged loudly as asked.

---

## a. Header

| Field | Value |
|---|---|
| Repo | `tilt_bed_controller` |
| Branch | `main` |
| HEAD before session | `bf3ac09` |
| HEAD after session | `165fea1` (plus this handoff's own commit) |
| Session date | 2026-08-05 (from `date`, per the rule added in `bf3ac09`) |
| Build | Firmware clean, `base` config. Host unit test 30/30. Not flashed. |

### Commits this session

```
165fea1 Fix the double-press misfire, the state it desynced, and add a motion fail-safe
```

### `git status` at end of session

```
On branch main
Your branch is ahead of 'origin/main' by 34 commits.

Changes not staged for commit:
	modified:   .projectlinkstore

Untracked files:
	.projectchecksums
	docs/handoffs/fw_HANDOFF_2026-08-05_02_double-press-misfire-and-failsafe.md
	tilt_bed_controller_project.pdf
```

**Noted with thanks:** your correction that the runaway reproduction was taken
on the **pre-split** build. That resolves the loose end in the previous
handoff's §b.1 — it corroborates the documented mechanism rather than
indicating a second path, and the posture there (mechanism removed, sequence
never reproduced, fix not claimed) stands as written.

---

## b. Analysis

### b.1 Why the double-press check misfires — and it is mine

The check compares `app_ui_status`, latched at the last physical interrupt,
against a fresh read **50 ms later**, and calls a match a double press.

It is only sound at that 50 ms point. What makes a legitimate single press
differ from its own latched status is the **LED bits changing in between** —
the latch is taken before `process_outputs()` lights that move's LED. Run the
comparison at any other moment and it compares a steady state against an edge,
and matches whenever the LED happens to line up.

`double_press_error_timer_timeout` is set by a **periodic** 50 ms timer
(`switch_input.c:141`). #35 wrote the consume as:

```c
if (double_press_error_timer_timeout && switch_sourced_move){
    double_press_error_timer_timeout = false;
```

**The clear is inside a condition that also requires the source to be
SWITCHES.** Whenever it was not, the flag was never consumed and went stale,
to be picked up at an arbitrary later moment — exactly the decoupling the
check cannot survive. `app_reset_double_press_timer()` compounded it by
stopping the timer without discarding a pending expiry.

That gate was mine, added to fix the stutter. Putting it in the clearing
condition rather than around the check body is the defect. **The gate itself
is untouched** — it is bench-confirmed and is not what misfired; only the flag
lifecycle changed.

### b.2 Observation 1 — confirmed exactly as you described

`switch_input_tick()`'s block is gated on `active_switch != OFF`
(`switch_input.c:333`) and `run_motor_supervision()` is inside it. The handler
sets `active_switch = OFF`, so supervision stops, the current filter is never
fed, and the app faithfully reports 0 mA on all four while the motors run.

**This is #37 §b.1 fact 2 happening on the bench**, and it is the most
important thing in this work.

Your observation that the current display is a live diagnostic for it is now
recorded permanently in DECISIONS #38: **motors visibly running with 0 mA on
all four means supervision is not running.** That is the cheapest possible
indicator of the most dangerous state this system has, and it cost nothing to
write down.

### b.3 Observation 2 — it is BOTH, and (b) is the serious one

**SAYING IT LOUDLY, as instructed: `process_outputs()` was energising the
motors from a stale variable after `bed_control` had never been consulted at
all. That is motion without arbitration's permission.**

Both mechanisms were present and each explains half the symptom:

- **(a) stale `switch_held`** still named LEFT DOWN, so every other button
  failed its `switch_held == OFF` press test and did nothing — which is why
  auto-level and lights went dead too, not just the motor buttons.
- **(b) stale `switch_driving`** still named LEFT DOWN, and `process_outputs()`
  energises purely on `switch_driving == value`. So pressing LEFT DOWN again
  **moved the bed**.

**The evidence that it is (b), from the observation itself:** with
`switch_held` stale and non-OFF, the press guard `switch_held == OFF` fails for
**every** button — LEFT DOWN included. So `bed_move_start()` was never called
for LEFT DOWN either. Yet LEFT DOWN moved the bed. The only remaining energise
path in the physical direction is `process_outputs()`' press arm on
`switch_driving`. (a) alone predicts LEFT DOWN would be as dead as everything
else; it was not.

Fixed on its own terms rather than folded into the handler cleanup:
`switch_driving` is cleared at the **start of every press event** and re-set
only when the verb returns true, so a stale value can never survive into the
press arm. The handler cleanup is still needed — it is what stops the desync
arising — but the guard is what makes the bypass unreachable if it ever does.

The app kept working because the app path uses its own state and consults
neither variable.

### b.4 The fail-safe

Unconditional, every main-loop pass, trusting no bookkeeping. Answers to your
specific requirements:

| Requirement | How |
|---|---|
| Not conditional on the desyncing variables | Reads the switch port directly; touches none of `active_switch`, `switch_held`, `switch_driving`, `app_ui_interrupt`, table matches |
| Must not fight the normal paths / no new stutter | Requires **three consecutive quiet samples** (~150 ms). In the press-to-outputs window the switch is physically **down**, so the first test already fails — it can never fire while any switch is held. The confirm count only guards a torn read at the instant of release |
| Must not interfere with auto-level | `auto_level_active` and `in_settings_mode` are explicit exclusions; both legitimately drive motors with no button held |
| Print once, not every pass | `failsafe_tripped` latch, cleared when the condition clears |

One thing your spec did not mention that I had to decide: **a direct read is
blocking I2C on the UI expander**, and per-pass blocking I2C on that bus is
#27/#30's radio-starvation mechanism. So the read is rate-limited to 50 ms on
the `sample_motor_faults_tick()` precedent. A fail-safe that reintroduced the
BLE-drop bug would be a poor trade.

LED bits are masked out (`SWITCH_INPUT_MASK`, `0xFF0F`) so the fail-safe cannot
inherit the LED dependency that made the double-press check fragile.

### b.5 The de-energise asymmetry — closed

`app_move_stop()` now calls `set_all_motors_off()`. The release helpers only
ever cleared `motor_state[]` flags, so the physical path's **only** hardware
de-energise was `process_outputs()`' release arm; any stop that did not reach
that arm left the outputs live while every flag said stopped.

**Why it was built that way:** the switch path was originally the only path,
and in it `process_inputs()` and `process_outputs()` always ran back to back on
the same event, so the split of responsibility was invisible. It only became
reachable when a second source could stop the bed without a matching panel
release event.

### b.6 Should supervision be gated on `active_switch`? — No, left open

You asked, and the answer is no. A gate that switches off collision detection
is the wrong shape for a safety check, and this one can be cleared by an
**error handler** — which is precisely what happened.

Not changed here, deliberately: that gate also encloses the double-press check
whose #35 fix is bench-confirmed, and untangling them is its own change with
its own regression surface. The fail-safe is a backstop for the *consequence*,
not a fix for the *shape*. Recorded in DECISIONS #38 as the next thing to fix
in this area, and raised in §f.1.

---

## c. Complete diff

`165fea1`, source verbatim and in full. Diffstat:

```
app.c             |   5 ++
bed_actions.c     |   9 +++
ble_bed_service.c |   5 ++
ble_bed_service.h |   5 ++
docs/DECISIONS.md | 130 ++++++++++++++++++++++++++++++++++++++++++
switch_input.c    | 166 +++++++++++++++++++++++++++++++++++++++++++++++++++++-
switch_input.h    |   7 +++
7 files changed, 326 insertions(+), 1 deletion(-)
```

### c.1 Source diff

```diff
diff --git a/app.c b/app.c
index 5ed908c..870ada0 100644
--- a/app.c
+++ b/app.c
@@ -700,6 +700,11 @@ void app_process_action(void)
     sl_atomic_store(heartbeat, local_heartbeat);
   }
 
+  // Unconditional runaway guard (DECISIONS.md #38). Trusts a direct hardware
+  // read and nothing else - deliberately not active_switch, switch_held,
+  // switch_driving or any table match, because those are what desync.
+  motion_failsafe_tick();
+
   sample_motor_faults_tick();
 
   // Live tilt for the app (BLE_GATT_DESIGN.md #6, DECISIONS.md #33). Both
diff --git a/bed_actions.c b/bed_actions.c
index bf89f47..d5db191 100644
--- a/bed_actions.c
+++ b/bed_actions.c
@@ -144,6 +144,15 @@ void app_move_stop(void)
   } else {
     motor_button_released();
   }
+
+  // De-energise the hardware, not just the bookkeeping (DECISIONS.md #38).
+  // The release helpers above only clear motor_state[] flags; before this,
+  // the physical path's ONLY hardware de-energise was process_outputs()'
+  // release arm, so any stop that did not reach that arm - a force-stop, a
+  // desynced switch_driving, an app stop - left the outputs live while every
+  // flag said the bed was stopped. A stop verb must stop the motors.
+  // Idempotent: process_outputs() may call this again in the same tick.
+  set_all_motors_off();
 }
 
 void app_auto_level_start(void)
diff --git a/ble_bed_service.c b/ble_bed_service.c
index 68cfb55..be58c12 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -817,6 +817,11 @@ bool ble_bed_service_status_subscribed(void)
   return status_subscriber_count > 0;
 }
 
+bool ble_bed_service_app_motion_active(void)
+{
+  return app_move_active || app_auto_level_active;
+}
+
 void ble_bed_service_tick(void)
 {
   // Control link dropped mid-command (BLE_GATT_DESIGN.md #5). Deferred out
diff --git a/ble_bed_service.h b/ble_bed_service.h
index 09490ca..7fd1068 100644
--- a/ble_bed_service.h
+++ b/ble_bed_service.h
@@ -52,4 +52,9 @@ void ble_bed_service_tick(void);
 // switch-only bed with no phone is unchanged (DECISIONS.md #33).
 bool ble_bed_service_status_subscribed(void);
 
+// True while the app believes it is holding a move or an auto-level. Used by
+// the motion fail-safe in switch_input.c (DECISIONS.md #38) to tell "the app
+// is commanding motion" from "nobody is, and the motors should be off".
+bool ble_bed_service_app_motion_active(void);
+
 #endif /* BLE_BED_SERVICE_H_ */
diff --git a/switch_input.c b/switch_input.c
index 39f51f7..d724621 100644
--- a/switch_input.c
+++ b/switch_input.c
@@ -30,6 +30,7 @@
 #include "exp_board.h"
 #include "exp_ui.h"
 #include "motor_current_functions.h"
+#include "ble_bed_service.h"
 #include "settings_mode.h"
 
 // Series of #defines to use as switch input masks with packed variables.
@@ -48,6 +49,12 @@
 /// Double press error check time interval
 #define DOUBLE_PRESS_ERROR_CHECK_TIME_INTERVAL 50 // time in ms
 
+// Every SWITCH bit of the packed Port0/Port1 status word, and nothing else.
+// Port 0 is all eight single-side switches; Port 1 bits 0-3 are UP_ALL,
+// AUTO_LEVEL, DOWN_ALL and BED_LIGHTS. Bits 4-7 of Port 1 are the LED enable
+// OUTPUTS and are deliberately excluded - see the fail-safe below.
+#define SWITCH_INPUT_MASK 0xFF0F
+
 // Flags
 static bool read_ui_data = false; // Interrupt flag to read the UI status
 static bool process_switch_change = true; // Indicates now clear to deal with another switch press
@@ -139,6 +146,20 @@ static void start_double_press_error_timer()
 void app_reset_double_press_timer(void)
 {
   sl_sleeptimer_stop_timer(&double_press_error_timer);
+  // Discard any expiry that already fired. Stopping a timer must also drop
+  // its pending result, or the flag survives to be consumed at an arbitrary
+  // later moment (DECISIONS.md #38).
+  double_press_error_timer_timeout = false;
+}
+
+// Clears ALL of the panel's own state together. The double-press handler used
+// to clear active_switch alone, which was correct before the #37 split when
+// active_switch WAS all three - afterwards it left switch_held and
+// switch_driving stale, which is bench observations 1 and 2 (#38).
+static void clear_switch_panel_state(void)
+{
+  switch_held = OFF;
+  switch_driving = OFF;
 }
 
 // Maps each single-side switch mask to its active_switch_t value, motor side, and direction
@@ -263,6 +284,23 @@ static void process_inputs()
 {
   if (!in_settings_mode){
 
+      /*
+       * A press may only drive a motor if bed_control accepts THIS press
+       * (DECISIONS.md #38). switch_driving is cleared at the start of every
+       * press event and re-set below only when the verb returns true, so a
+       * stale value can never survive into process_outputs' press arm.
+       *
+       * Without this, a desynced switch_driving left the panel able to
+       * energise motors for a button whose bed_move_start() was never even
+       * called - motion with arbitration bypassed entirely, which is what
+       * bench observation 2 was: every other button dead because switch_held
+       * was stale, and the SAME button still moving the bed because
+       * switch_driving was.
+       */
+      if (switch_pressed) {
+          switch_driving = OFF;
+      }
+
       switch (app_ui_interrupt) // Configure controls based on activated switch
       {
         // Every press below is gated on switch_held, NOT active_switch
@@ -361,6 +399,112 @@ static void process_inputs()
    }
 }
 
+/*******************************************************************************
+ *** Motion fail-safe (DECISIONS.md #38)
+ *
+ * THE BED MUST NEVER MOVE WHILE NOTHING IS COMMANDING IT. Every other check in
+ * this file trusts some piece of bookkeeping - active_switch, switch_held,
+ * switch_driving, app_ui_interrupt, a table match. This one trusts none of
+ * them, because they are precisely the things that desync: bench observation 1
+ * was motors energised with active_switch == OFF, which also switches off
+ * run_motor_supervision() and leaves nothing that would ever stop them.
+ *
+ * It asks the hardware directly and nothing else: is any switch physically
+ * down? Is the app commanding? Is auto-level running? Is settings mode active?
+ * If all four are no and a motor is energised, that is a runaway by
+ * definition - de-energise.
+ *
+ * Rate-limited to FAILSAFE_SAMPLE_MS, matching the sample_motor_faults_tick()
+ * precedent: a direct read is blocking I2C on the UI expander, and per-pass
+ * blocking I2C on a shared bus is DECISIONS #27/#30's radio-starvation
+ * mechanism. Milliseconds via sleeptimer, never loop passes (#2).
+ *
+ * It requires FAILSAFE_CONFIRM_SAMPLES consecutive quiet samples before
+ * acting. That is what stops it fighting the normal paths: the window between
+ * a press being registered and its outputs applied is a single tick, far
+ * shorter than the confirm window, and in that window the switch IS physically
+ * down so the first test already fails. The confirm count only guards against
+ * a torn read at the exact moment of a release.
+ ******************************************************************************/
+
+#define FAILSAFE_SAMPLE_MS        50
+#define FAILSAFE_CONFIRM_SAMPLES  3   // ~150 ms of agreement before acting
+
+static uint32_t failsafe_last_sample_tick = 0;
+static uint8_t  failsafe_quiet_samples = 0;
+static bool     failsafe_tripped = false; // print once, not every pass
+
+static bool any_motor_energised_now(void)
+{
+  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
+    if (motor_state[i].motor_on) {
+      return true;
+    }
+  }
+  return false;
+}
+
+void motion_failsafe_tick(void)
+{
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  if (sl_sleeptimer_tick_to_ms(now - failsafe_last_sample_tick) < FAILSAFE_SAMPLE_MS) {
+    return;
+  }
+  failsafe_last_sample_tick = now;
+
+  // Auto-level legitimately drives motors with no button held in the panel
+  // sense, and settings mode drives them from its own logic. Neither is a
+  // runaway, so neither is counted as quiet.
+  if (auto_level_active || in_settings_mode || ble_bed_service_app_motion_active()) {
+    failsafe_quiet_samples = 0;
+    failsafe_tripped = false;
+    return;
+  }
+
+  // DIRECT hardware read - deliberately not any cached flag. Switches are
+  // active low with pull-ups, so a switch bit reading 0 means that button is
+  // physically down. The LED bits (P1_4-P1_7) are masked out: they are
+  // outputs, and they are exactly what made the double-press check depend on
+  // something other than the switches (#35/#38).
+  uint16_t status = get_ui_status();
+  bool any_switch_down = ((status & SWITCH_INPUT_MASK) != SWITCH_INPUT_MASK);
+
+  if (any_switch_down) {
+    failsafe_quiet_samples = 0;
+    failsafe_tripped = false;
+    return;
+  }
+
+  if (!any_motor_energised_now()) {
+    failsafe_quiet_samples = 0;
+    failsafe_tripped = false;
+    return;
+  }
+
+  if (failsafe_quiet_samples < FAILSAFE_CONFIRM_SAMPLES) {
+    failsafe_quiet_samples++;
+    return;
+  }
+
+  // Nothing is commanding motion and a motor is on. De-energise.
+  set_all_motors_off();
+  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
+    motor_state[i].motor_on = false;
+  }
+  set_LED_off(LED_GREEN);
+  set_LED_off(LED_BLUE);
+  set_LED_off(LED_RED);
+  clear_switch_panel_state();
+  active_switch = OFF;
+
+  if (!failsafe_tripped) {
+    failsafe_tripped = true;
+#if !BED_CAPTURE_MOTOR_CURRENTS
+    printf("FAILSAFE: motors energised with nothing commanding - stopped\r\n");
+#endif
+  }
+}
+
 // Called whenever there is a change in switch or expander interrupt state
 void sl_button_on_change(const sl_button_t *handle)
 {
@@ -430,9 +574,21 @@ void switch_input_tick(void)
       // collision and limit supervision must cover app moves too.
       bool switch_sourced_move = (bed_active_source() == BED_SOURCE_SWITCHES);
 
-      if (double_press_error_timer_timeout && switch_sourced_move){
+      // The flag is consumed UNCONDITIONALLY; only the check itself is gated
+      // (DECISIONS.md #38). Putting switch_sourced_move in the same condition
+      // as the clear - which is what #35 did - meant the flag went stale
+      // whenever the source was not SWITCHES, and a stale flag is then
+      // consumed at an arbitrary later moment instead of 50 ms after the
+      // latch. This check is only sound at that 50 ms point, because what
+      // makes a legitimate single press differ from its own latched status is
+      // the LED bits changing in between. Run it at any other time and it
+      // compares a steady state against an edge, and matches whenever the LED
+      // happens to line up. That is the misfire.
+      if (double_press_error_timer_timeout){
           double_press_error_timer_timeout = false;
 
+        if (switch_sourced_move){
+
           // Get source of switch interrupt
           app_ui_interrupt_new = get_ui_interrupt();
 
@@ -449,6 +605,13 @@ void switch_input_tick(void)
 #endif
               set_dual_motors_off(HEAD);
               set_dual_motors_off(FOOT);
+              // Clear the panel's state TOGETHER with active_switch (#38).
+              // Clearing active_switch alone left switch_held naming a button
+              // nobody was holding - so every other button failed its
+              // `switch_held == OFF` press test and went dead - and left
+              // switch_driving naming a button process_outputs would still
+              // energise on, which is motion with bed_control never consulted.
+              clear_switch_panel_state();
               active_switch = OFF;
               // Reset motor readings
               reset_ignore_data_points_count();
@@ -464,6 +627,7 @@ void switch_input_tick(void)
           } else {
 //              printf("No Error\r\n");
           }
+        }
       }
   }
 
diff --git a/switch_input.h b/switch_input.h
index 3122f4a..7e58676 100644
--- a/switch_input.h
+++ b/switch_input.h
@@ -34,4 +34,11 @@ void app_reset_double_press_timer(void);
 // inline blocks it replaces.
 void switch_input_tick(void);
 
+// Unconditional motion fail-safe (DECISIONS.md #38). Call once per
+// app_process_action(). De-energises the motors whenever a direct hardware
+// read says no switch is down, the app is not commanding motion, auto-level
+// is not running and settings mode is inactive - regardless of what
+// active_switch, switch_held, switch_driving or any table match believe.
+void motion_failsafe_tick(void);
+
 #endif /* SWITCH_INPUT_H_ */
```

`docs/DECISIONS.md` (+#38) is docs-only; `git show 165fea1 -- docs/`
reproduces it and its substance is §b.

---

## d. Raw output of every command run

No secrets; no redaction.

### d.1 The flag lifecycle — the misfire evidence

```
$ grep -n "double_press_error_timer_timeout" switch_input.c
114:static bool double_press_error_timer_timeout = false;
126:  double_press_error_timer_timeout = true;
433:      if (double_press_error_timer_timeout && switch_sourced_move){
434:          double_press_error_timer_timeout = false;
```

Set in the periodic callback (`:126`), cleared **only** at `:434`, inside a
condition that also requires `switch_sourced_move` (`:433`). That is the
staleness.

```
$ grep -n -A4 "void app_reset_double_press_timer" switch_input.c
139:void app_reset_double_press_timer(void)
140-{
141-  sl_sleeptimer_stop_timer(&double_press_error_timer);
142-}
```

Stopped the timer without discarding a pending expiry.

`start_double_press_error_timer()` uses
`sl_sleeptimer_start_periodic_timer` (`switch_input.c:141` pre-change), so the
flag can be re-set repeatedly while unconsumed.

### d.2 Firmware build (after the misfire/desync/fail-safe changes)

```
$ cmake --build build --config base --target tilt_bed_controller
[1/6] Building C object .../bed_actions.c.obj
[2/6] Building C object .../settings_mode.c.obj
[3/6] Building C object .../switch_input.c.obj
[4/6] Building C object .../ble_bed_service.c.obj
[5/6] Building C object .../app.c.obj
[6/6] Linking C executable base\tilt_bed_controller.out
```

### d.3 Firmware rebuild after the de-energise fix, and the host test

```
$ cmake --build build --config base --target tilt_bed_controller
[1/2] Building C object .../bed_actions.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
=== host test ===

0 check(s) failed
```

Both clean, zero warnings. The host test is a regression check —
`bed_control.c` was not modified this session.

### d.4 Commit

```
$ git log --oneline -1
165fea1 Fix the double-press misfire, the state it desynced, and add a motion fail-safe
```

### d.5 Failures

One, mine, recovered: a `sed` address expression with an unescaped comma while
trying to print the double-press handler.

```
$ sed -n '/Double Press Error/,-14p' switch_input.c | head -20
sed: -e expression #1, char 22: unexpected `,'
```

Replaced with targeted `grep -n` calls, which produced §d.1.

---

## e. VERIFIED vs ASSERTED

### VERIFIED — code facts

| # | Claim | Evidence |
|---|---|---|
| V1 | The timeout flag is cleared only inside a condition requiring `switch_sourced_move` | pre-change `switch_input.c:433-434`; §d.1 |
| V2 | The timer is periodic, so the flag can be re-set while unconsumed | `sl_sleeptimer_start_periodic_timer`, `switch_input.c` |
| V3 | `app_reset_double_press_timer()` did not clear the flag | pre-change `switch_input.c:139-142`; §d.1 |
| V4 | The handler cleared `active_switch` only, not `switch_held`/`switch_driving` | pre-change handler body |
| V5 | `run_motor_supervision()` is inside a block gated on `active_switch != OFF` | `switch_input.c:333` |
| V6 | `process_outputs()`' press arm energises purely on `switch_driving == value` | `switch_input.c` press arms |
| V7 | With `switch_held` stale, the press guard fails for **every** button | `switch_held == OFF` is the guard in all five cases |
| V8 | V6+V7 together mean LEFT DOWN moved the bed with `bed_move_start()` never called — mechanism (b) | the two above, plus the observation that LEFT DOWN still moved while everything else was dead |
| V9 | The release helpers cleared flags only, never de-energised | `bed_actions.c` `motor_button_released()` / `all_motors_button_released()` |
| V10 | Firmware builds clean | §d.2, §d.3 |
| V11 | Host unit test 30/30 | §d.3 |

### ASSERTED

| # | Claim | Why |
|---|---|---|
| A1 | The misfire is fixed | Never run. Step (a) is the test: 20 holds per button with no print. |
| A2 | Observations 1 and 2 are gone | Never run. Steps (b), (c). |
| A3 | The fail-safe never fires in normal operation | **The most likely thing to be wrong.** If the confirm window or the switch mask is off, it will trip during ordinary use. Step (e) is the test, and it is the one I would run first. |
| A4 | The fail-safe would actually catch a runaway | Never triggered deliberately — I did not add a way to provoke one, and I would not want one on the bench. It is untested in the direction that matters. |
| A5 | A real double press still fires and recovers cleanly | Step (d). The flag-lifecycle change alters when the check runs. |
| A6 | `set_all_motors_off()` in `app_move_stop()` causes no regression | It is now called on every stop from both sources; idempotent by inspection, unrun. |
| A7 | Nothing in #35/#36/#37 regressed | Step (g). |

---

## f. QUESTIONS FOR REVIEW

1. **Supervision is still gated on `active_switch`** — §b.6. I think that gate
   should go, but not in the same change as a safety fix, and not without
   untangling it from the bench-confirmed #35 check. Want that next?
2. **The fail-safe's confirm window is 3 × 50 ms ≈ 150 ms.** That is how long a
   genuine runaway could run before it trips. Shorter reacts faster but risks
   fighting a release edge; longer is safer against false trips. I picked
   conservative. Say if you want it tighter.
3. **`SWITCH_INPUT_MASK` is `0xFF0F`** — all of Port 0 plus Port 1 bits 0-3,
   derived from the bit map comment in `switch_input.c`, not from a datasheet
   I read. Worth one confirmation against the schematic, because if it is wrong
   the fail-safe either never fires or fires constantly.
4. **The fail-safe clears the panel state when it trips.** That is right for
   recovery, but it means a trip silently resets `switch_held`, so a button
   still physically down would need releasing and re-pressing. Acceptable?

---

## g. NOT DONE

| Item | Reason |
|---|---|
| Ungating supervision from `active_switch` | §b.6/§f.1 — needs its own change. |
| Removing or redesigning the double-press check | Out of scope; you said §0 is about the misfire, not the gate. |
| Provoking a real runaway to test the fail-safe | Would require deliberately breaking the firmware; A4 records it as untested in that direction. |
| Release-config build | Still owed before Stage C ships. |
| Manual-move loop-rate measurement | Still owed, five sessions. |
| Flashing / bench | User drives the bench. |
| Pushing | 34 commits ahead of `origin/main`. |

---

## BENCH ACCEPTANCE

**Run step (e) first** — if the fail-safe misfires, everything else is noise.

### Step 0 — build and flash

```
cd C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\tilt_bed_controller
```
```
git log --oneline -1
```

Expect `165fea1 Fix the double-press misfire...` or later. Build and flash as
normal. PuTTY on **COM6**, 115200 8N1, Session → Logging → "All session
output" → `misfire_acceptance.log`.

### Step e (FIRST) — the fail-safe must stay silent

Use the bed normally for two or three minutes: several single-side moves from
the panel, an ALL UP and ALL DOWN, an auto-level to completion, lights on and
off, then the same set from the app.

**Expect:** `FAILSAFE: motors energised with nothing commanding` **never
appears.** If it does, note exactly what you were doing and stop — it means
the switch mask or the confirm window is wrong, and the fail-safe is
interfering rather than protecting.

### Step a — the misfire must be gone

Hold each physical button for about a second and release. **Twenty times per
button**, all eleven buttons.

**Expect:** `Double Press Error` does not print **once**. Search the log
afterwards in Notepad to be sure — it is easy to miss on a scrolling console.

### Step b — every other button still works afterwards

After any single press-and-release, try a **different** button — including
**auto-level** and **lights**, which were both dead before.

**Expect:** everything responds normally. Do this after at least five
different "first" buttons, so you are not testing one lucky pairing.

### Step c — current must never read zero while motors run

The specific reproduction:

1. Hold **ALL UP** on the panel for one second. Release.
2. Wait one second.
3. Hold **ALL UP** again.

**Expect:** the app shows **non-zero** current on the motors during **both**
runs. Zero current with the motors visibly running is the exact failure, and
it means supervision is not running.

Repeat three times, and also try it with LEFT DOWN.

### Step d — a REAL double press must still work

Press **two** physical motor buttons at the same time.

**Expect:** `Double Press Error` prints, motors stop.

Then — this is the part that was broken — **check recovery**: release both,
then press each of several other buttons in turn, including auto-level and
lights.

**Expect:** all of them work normally. Nothing is left dead.

### Step f — the runaway hunt, again

Tap each physical button briefly and repeatedly, in every direction, both with
the app disconnected and with the app connected and moving.

**THE BED MUST NEVER MOVE WITH NO BUTTON HELD.** If it does: cut power, note
the button, what the app was doing, and whether `FAILSAFE:` appeared in the
log. **If `FAILSAFE:` appeared, that is the fail-safe working** — still report
it, because something upstream let the bed reach that state.

### Step g — the previous list, unchanged

1. Conflict latching both directions (app moving + physical press, and the
   mirror) — motors and movement LEDs out immediately, stay out until both
   released.
2. Auto-level from both sources: no lockup, both work on the next attempt with
   no manual-move button needed.
3. No false limit flags on any motor afterwards.
4. No stutter: hold app UP RIGHT, press and release a physical button, keep
   holding.
5. No under-bed light blip on a movement conflict.
6. Collision trips on both a physical and an app move.
7. Settings mode entry and the sensitivity ladder.

### Step h — full regression

All of `docs/REGRESSION.md`, phone bonded and subscribed throughout, including
§6.8. Watch for `reason=0x1008`.

### What to send back

- `misfire_acceptance.log`.
- **Step e first:** did `FAILSAFE:` ever appear, and doing what.
- Step a: any `Double Press Error` at all, and after which button.
- Step c: did current ever read zero with motors running.
- Step d: did every button work after a real double press.
- Step f: did the bed ever move with nothing held.

Everything stays **ASSERTED** until this comes back. In particular the
fail-safe is untested in the direction that matters — it has never been shown
to catch a real runaway, only to stay quiet.
