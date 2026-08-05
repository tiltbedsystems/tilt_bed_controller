# fw_HANDOFF_2026-08-04_02 — split active_switch; runaway root-cause; false limits

Fixes the swallowed physical press, the auto-level lockup, the false limit
flags and the indicator blip. **The runaway is root-caused as a mechanism but
NOT as a reproduced sequence** — see §b.1, which is written to be read before
anything else here.

---

## a. Header

| Field | Value |
|---|---|
| Repo | `tilt_bed_controller` |
| Branch | `main` |
| HEAD before session | `c706893` |
| HEAD after session | `d9649a9` (plus this handoff's own commit) |
| Session date | 2026-08-04 |
| Build | Firmware clean, `base` config. Host unit test 30/30. Not flashed. |

### Commits this session

```
d9649a9 Split active_switch: give the physical panel its own held-button state
```

### `git status` at end of session

```
On branch main
Your branch is ahead of 'origin/main' by 32 commits.

Changes not staged for commit:
	modified:   .projectlinkstore

Untracked files:
	.projectchecksums
	docs/handoffs/fw_HANDOFF_2026-08-04_02_split-active-switch-and-runaway.md
	tilt_bed_controller_project.pdf
```

---

## b. Analysis

### b.1 THE RUNAWAY — what is confirmed, and what is not

**Confirmed from the code.** `process_outputs()` is called from the end of
`process_inputs()` (`switch_input.c:297`), so both run per switch event in
that order. Its press arm energised on
`switch_pressed && (active_switch == motor_switch_table[t].value)` — and
`active_switch` could hold that value because the **app** wrote it there
through `motor_button_pressed()`. So a physical press that `process_inputs`
had just discarded (the §b.2 guard) still reached `set_dual_motors_on()`.
**Motors energised by the panel path with `bed_control` never informed.** Your
hypothesis, confirmed.

Two further facts, also confirmed, make that unsafe rather than merely
redundant:

1. **The release conditions are asymmetric.** `process_inputs`' release
   accepted `active_switch == value || active_switch == OFF`
   (`switch_input.c:281`); `process_outputs`' release arm accepted only
   `active_switch == value` (`switch_input.c:208`). The physical path's
   **only** hardware de-energise is that arm — `bed_move_stop()` →
   `app_move_stop()` → `motor_button_released()` clears `motor_state` flags
   and leaves the outputs energised. So there are reachable states where the
   release is logically handled and the motors are left on.
2. **No supervision at all when `active_switch == OFF`.**
   `switch_input_tick()`'s block is gated on `active_switch != OFF`
   (`switch_input.c:333`) and `run_motor_supervision()` is inside it. Motors
   energised while `active_switch == OFF` have no collision detection, no
   limit inference, and no path that will ever turn them off.

**NOT confirmed: a sequence that reproduces your observation.** I traced
several candidates and every one ended with some other path calling
`set_all_motors_off()` — the app's `app_apply_stop_outputs()`, a force-stop,
or the watchdog. I could not construct one I am confident produced *that*
event. **An intermittent runaway that stopped reproducing is not evidence of a
fix, and I am not claiming one.**

**What the split makes impossible.** Not "less likely" — structurally
impossible, because the app cannot write either new variable:

| Failure | Why it cannot happen now |
|---|---|
| `process_outputs` energises from an app-set value | It matches `switch_driving`, which only `process_inputs` writes, and only on a real press |
| A press `bed_control` refused still drives a motor | `switch_driving` is set **only** when the verb returned true |
| An accepted press is left unpaired | `switch_driving` is unaffected by anything the app does, so the release arm always matches its own press |
| A physical press is invisible to arbitration | `process_inputs` gates on `switch_held`, so the press always reaches `bed_control` |

What it does **not** fix: the `active_switch == OFF` supervision gap (item 2
above) still exists for any *other* way motors might be energised. I did not
widen that block, because it also contains the double-press check whose gate
is bench-confirmed and which you told me not to disturb. Raised in §f.1.

### b.2 The swallowed press

`process_inputs()` gated every press on `active_switch == OFF`, which meant
"is a physical button already held?" but asked the command variable. While the
app moved the bed it was non-OFF, so the press never reached `bed_control`, no
conflict was reported, the #36 latch never armed, and nothing stopped.

The "same button" exception falls straight out: with the app holding ALL UP,
`active_switch == SWITCH_UP_ALL_VALUE`, so the press was skipped but the
*release* branch matched and called `bed_move_stop()`, which force-stopped —
a brief blip on release, only for the matching button, after which the app's
100 ms repeat restarted the bed. Exactly as you described.

### b.3 The split

`switch_input.c` gains two file-private variables. `active_switch` keeps its
meaning — the **command** variable, what the bed is doing, whoever asked.

- **`switch_held`** — which physical button is down. Set on press, cleared on
  release, **regardless of whether the command was accepted**, so a release
  always reaches `bed_control` and always releases this source's half of the
  #36 latch. That detail matters: if it were only set on accepted presses, a
  refused press would never clear the latch.
- **`switch_driving`** — the same value, but only when `bed_control` accepted
  the press. `process_outputs()` energises and de-energises on this.

`bed_status.c` needed **no change**: `active_switch` is still the command
variable and still tracks app-sourced activity, so last session's finding
holds. `run_motor_supervision()` is still outside the double-press gate. The
#35 fix is untouched.

### b.4 False limit flags — separate defect, and the cause of the lockup

`limit_switch_statuses()` infers `at_limit_switch[motor].upper = true` from
"commanded on but not moving" (`dual_motor_control.c:214-217`) and only clears
it again while that motor is on **and moving** (`:232-238`). A motor left with
a stale `motor_on` flag on a physically stopped bed therefore latches a limit
that nothing can clear.

**This is not cosmetic.** `set_dual_motors_on()` refuses to energise a pair
whose limit flag is set (`dual_motor_control.c:330,338,349,357,368`). A stale
flag makes the bed stop responding — that is the "physical auto-level flashes
red but never runs" half of the lockup, and it is why tapping a manual button
cleared it: that move re-entered the clearing branch.

Fixed by `reset_limit_switch_flags()`, called from `app_stop_all()` and
`app_stop_movement()`. A stop means nothing is commanded, so every inference
is stale by definition; real limits are re-inferred within about a second of
the next move, which is already how the flags behave after any normal stop.

### b.5 The indicator blip

`app_stop_movement()` called `app_stop_all()`, whose `set_all_LED_off()`
cleared amber, then turned amber back on. It now clears green/blue/red
individually and never touches amber.

**Residual, recorded not hidden:** when the conflict interrupts *auto-level*,
`app_stop_movement()` still routes through `app_auto_level_stop()`, which
calls `set_all_LED_off()`. A blip is still possible on that one path. Fixing
it means changing a function the physical auto-level path also uses, which I
was not willing to do unasked in the same change as a safety fix. §f.2.

---

## c. Complete diff

`d9649a9`, source verbatim and in full. Diffstat:

```
bed_actions.c        |  28 ++++++++++--
docs/DECISIONS.md    | 117 +++++++++++++++++++++++++++++++++++++++++++++++++++
dual_motor_control.c |  23 ++++++++++
dual_motor_control.h |   6 +++
switch_input.c       | 112 ++++++++++++++++++++++++++++++++++++------------
5 files changed, 255 insertions(+), 31 deletions(-)
```

### c.1 Source diff

```diff
diff --git a/bed_actions.c b/bed_actions.c
index d378f37..bf89f47 100644
--- a/bed_actions.c
+++ b/bed_actions.c
@@ -216,6 +216,7 @@ void app_stop_all(void)
       motor_state[i].motor_on = false;
   }
   set_all_LED_off();
+  reset_limit_switch_flags(); // stale inferred limits must not survive a stop (#37)
   active_switch = OFF;
 }
 
@@ -320,10 +321,31 @@ void app_release_active_switch(void)
 // STOP_ALL still calls app_stop_all() directly and keeps its full behaviour.
 void app_stop_movement(void)
 {
-  app_stop_all();
-  if (bedlights_on) {
-    set_LED_on(LED_AMBER);
+  if (auto_level_active) {
+    app_auto_level_stop();
+  } else if (active_switch != OFF) {
+    app_move_stop();
+  }
+  set_all_motors_off();
+  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
+    motor_state[i].motor_on = false;
   }
+
+  // Movement indicators ONLY. This used to call app_stop_all(), whose
+  // set_all_LED_off() cleared amber, and then turn amber back on - which is
+  // exactly the blip the bench saw on the under-bed indicator. Clearing the
+  // three movement LEDs individually means amber is never touched.
+  set_LED_off(LED_GREEN);
+  set_LED_off(LED_BLUE);
+  set_LED_off(LED_RED);
+
+  // A stop invalidates every inferred limit (DECISIONS.md #37). Without this
+  // a stale flag survives the stop and set_dual_motors_on() then refuses to
+  // energise that pair, which is the "bed stops responding" half of the
+  // auto-level lockup.
+  reset_limit_switch_flags();
+
+  active_switch = OFF;
 }
 
 bool app_bed_is_idle(void)
diff --git a/dual_motor_control.c b/dual_motor_control.c
index 25b301e..e962407 100644
--- a/dual_motor_control.c
+++ b/dual_motor_control.c
@@ -202,6 +202,29 @@ void auto_level_limit_switch_motor_off(uint8_t motor, int8_t auto_level_current_
 
 }
 
+/*
+ * Clears the INFERRED limit state for every motor (DECISIONS.md #37).
+ *
+ * at_limit_switch[] is not a sensor reading - limit_switch_statuses() infers
+ * it from "commanded on but not moving", and only ever clears it again while
+ * that motor is on AND moving. So a motor left with a stale motor_on flag
+ * while the bed is physically stopped latches a limit that nothing can clear,
+ * and set_dual_motors_on()'s guards below then refuse to energise that pair -
+ * the bed stops responding until some other move happens to clear it.
+ *
+ * A full stop means nothing is commanded, so every inference is stale by
+ * definition. Called from the stop paths. Real limits are re-inferred within
+ * about a second of the next move, which is already how the flags behave
+ * after any normal stop.
+ */
+void reset_limit_switch_flags(void)
+{
+  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
+    at_limit_switch[i].upper = false;
+    at_limit_switch[i].lower = false;
+  }
+}
+
 void limit_switch_statuses(uint8_t motor, active_switch_t switch_value, int8_t auto_level_current_axis){
 
   // Determine if motor is moving or not
diff --git a/dual_motor_control.h b/dual_motor_control.h
index dcea824..46f2090 100644
--- a/dual_motor_control.h
+++ b/dual_motor_control.h
@@ -62,6 +62,12 @@ void auto_level_limit_switch_motor_off(uint8_t motor, int8_t auto_level_current_
 void all_motor_limit_switch_temp_off();
 
 void limit_switch_statuses(uint8_t motor, active_switch_t switch_value, int8_t auto_level_current_axis);
+
+// Clears the INFERRED at_limit_switch[] state for every motor. Called from the
+// stop paths: a stop means nothing is commanded, so any "commanded on but not
+// moving" inference is stale, and a stale one blocks set_dual_motors_on()
+// permanently. See the comment above the definition (DECISIONS.md #37).
+void reset_limit_switch_flags(void);
 void reverse_motor_direction_and_start(active_switch_t switch_value);
 
 #endif /* DUAL_MOTOR_CONTROL_H_ */
diff --git a/switch_input.c b/switch_input.c
index 2e968fb..39f51f7 100644
--- a/switch_input.c
+++ b/switch_input.c
@@ -76,6 +76,33 @@ bool switch_pressed = false; // Interrupt flag to capture switch up/down state -
   All switches have pull-ups and all LED's are active low.
  */
 
+/*
+ * PHYSICAL PANEL STATE - owned exclusively by this file (DECISIONS.md #37).
+ *
+ * These exist because active_switch could not answer "is a physical button
+ * currently held?". It is the COMMAND variable, written by whichever source
+ * is driving the bed, so an app move set it through the same
+ * motor_button_pressed() the panel uses. Two readers here needed the panel
+ * question and asked the command variable instead:
+ *
+ *   - process_inputs()'s press guard, which skipped every physical press
+ *     while the app was moving, so the press never reached bed_control and
+ *     no conflict was ever detected;
+ *   - process_outputs()'s press/release pairing, which could be energised by
+ *     an app-written value and left unpaired when the app changed it.
+ *
+ *   switch_held    which physical button is physically down, set on press and
+ *                  cleared on release REGARDLESS of whether the command was
+ *                  accepted - so the release always reaches bed_control and
+ *                  releases this source's half of the #36 latch.
+ *   switch_driving the same value, but only when bed_control ACCEPTED the
+ *                  press. process_outputs energises and de-energises on this
+ *                  one, so a refused press never drives a motor and an
+ *                  accepted one is always paired with its own release.
+ */
+static active_switch_t switch_held = OFF;
+static active_switch_t switch_driving = OFF;
+
 static uint16_t app_ui_status;
 static uint16_t app_ui_status_new; // Used for double press error check
 uint16_t app_ui_interrupt; // Packed Port0/1 interrupt source(s) - extern via switch_input.h
@@ -153,40 +180,43 @@ static void process_outputs()
     switch (app_ui_interrupt) // Configure controls based on activated switch
     {
       case SWITCH_UP_ALL:
-          if ((switch_pressed) && (active_switch == SWITCH_UP_ALL_VALUE)){
+          if ((switch_pressed) && (switch_driving == SWITCH_UP_ALL_VALUE)){
               set_LED_on(LED_BLUE);
               set_all_motors_on();
-          } else if ((!switch_pressed) && (active_switch == SWITCH_UP_ALL_VALUE)){
+          } else if ((!switch_pressed) && (switch_driving == SWITCH_UP_ALL_VALUE)){
               set_LED_off(LED_BLUE);
               set_all_motors_off();
+              switch_driving = OFF;
               active_switch = OFF;
           }
           break;
 
       case SWITCH_AUTO_LEVEL:
-          if ((switch_pressed) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
+          if ((switch_pressed) && (switch_driving == SWITCH_AUTO_LEVEL_VALUE)){
               set_LED_on(LED_RED);
               set_all_motors_off();
-          } else if ((!switch_pressed) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
+          } else if ((!switch_pressed) && (switch_driving == SWITCH_AUTO_LEVEL_VALUE)){
               set_all_motors_off();
               auto_level_stop_blink_LED(LED_RED);
+              switch_driving = OFF;
               active_switch = OFF;
           }
           break;
 
       case SWITCH_DOWN_ALL:
-          if ((switch_pressed) && (active_switch == SWITCH_DOWN_ALL_VALUE)){
+          if ((switch_pressed) && (switch_driving == SWITCH_DOWN_ALL_VALUE)){
               set_LED_on(LED_BLUE);
               set_all_motors_on();
-          } else if ((!switch_pressed) && (active_switch == SWITCH_DOWN_ALL_VALUE)){
+          } else if ((!switch_pressed) && (switch_driving == SWITCH_DOWN_ALL_VALUE)){
               set_LED_off(LED_BLUE);
               set_all_motors_off();
+              switch_driving = OFF;
               active_switch = OFF;
           }
           break;
 
       case SWITCH_BED_LIGHTS:
-          if ((switch_pressed) && (active_switch == SWITCH_BED_LIGHT_VALUE)){
+          if ((switch_pressed) && (switch_driving == SWITCH_BED_LIGHT_VALUE)){
               if (app_lights_on()){
                   set_LED_on(LED_AMBER);
                   set_underbed_lighting_on();}
@@ -194,7 +224,8 @@ static void process_outputs()
                   set_LED_off(LED_AMBER);
                   set_underbed_lighting_off();
               }
-          } else if ((!switch_pressed) && (active_switch == SWITCH_BED_LIGHT_VALUE)){
+          } else if ((!switch_pressed) && (switch_driving == SWITCH_BED_LIGHT_VALUE)){
+              switch_driving = OFF;
               active_switch = OFF;
           }
           break;
@@ -202,12 +233,13 @@ static void process_outputs()
     default: {
         for (uint8_t t = 0; t < NUM_MOTOR_SWITCH_TABLE_ENTRIES; t++) {
             if (app_ui_interrupt == motor_switch_table[t].mask) {
-                if ((switch_pressed) && (active_switch == motor_switch_table[t].value)){
+                if ((switch_pressed) && (switch_driving == motor_switch_table[t].value)){
                     set_LED_on(LED_GREEN);
                     set_dual_motors_on(motor_switch_table[t].side); // Motor statuses are set in "set_dual_motors_on/off()" function
-                } else if ((!switch_pressed) && (active_switch == motor_switch_table[t].value)){
+                } else if ((!switch_pressed) && (switch_driving == motor_switch_table[t].value)){
                     set_LED_off(LED_GREEN);
                     set_all_motors_off();
+                    switch_driving = OFF;
                     active_switch = OFF;
                 }
                 break;
@@ -233,35 +265,55 @@ static void process_inputs()
 
       switch (app_ui_interrupt) // Configure controls based on activated switch
       {
+        // Every press below is gated on switch_held, NOT active_switch
+        // (DECISIONS.md #37). The old guard asked "is the bed idle?" when it
+        // meant "is a physical button already down?", so while the app was
+        // moving the bed every physical press was silently discarded and no
+        // conflict was ever reported.
         case SWITCH_UP_ALL:
-            if (switch_pressed && (active_switch == OFF)){
-                bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_ALL, BED_DIR_UP);
-            } else if ((!switch_pressed) && (active_switch == SWITCH_UP_ALL_VALUE || active_switch == OFF)){
+            if (switch_pressed && (switch_held == OFF)){
+                switch_held = SWITCH_UP_ALL_VALUE;
+                if (bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_ALL, BED_DIR_UP)) {
+                    switch_driving = SWITCH_UP_ALL_VALUE;
+                }
+            } else if ((!switch_pressed) && (switch_held == SWITCH_UP_ALL_VALUE)){
+                switch_held = OFF;
                 bed_move_stop(BED_SOURCE_SWITCHES);
             }
             break;
 
         case SWITCH_AUTO_LEVEL:
-            if (switch_pressed && (active_switch == OFF)){
-                bed_auto_level_start(BED_SOURCE_SWITCHES);
-            } else if ((!switch_pressed) && (active_switch == SWITCH_AUTO_LEVEL_VALUE || active_switch == OFF)){
+            if (switch_pressed && (switch_held == OFF)){
+                switch_held = SWITCH_AUTO_LEVEL_VALUE;
+                if (bed_auto_level_start(BED_SOURCE_SWITCHES)) {
+                    switch_driving = SWITCH_AUTO_LEVEL_VALUE;
+                }
+            } else if ((!switch_pressed) && (switch_held == SWITCH_AUTO_LEVEL_VALUE)){
+                switch_held = OFF;
                 bed_auto_level_stop(BED_SOURCE_SWITCHES);
             }
             break;
 
         case SWITCH_DOWN_ALL:
-            if (switch_pressed && (active_switch == OFF)){
-                bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_ALL, BED_DIR_DOWN);
-            } else if ((!switch_pressed) && (active_switch == SWITCH_DOWN_ALL_VALUE || active_switch == OFF)){
+            if (switch_pressed && (switch_held == OFF)){
+                switch_held = SWITCH_DOWN_ALL_VALUE;
+                if (bed_move_start(BED_SOURCE_SWITCHES, BED_TARGET_ALL, BED_DIR_DOWN)) {
+                    switch_driving = SWITCH_DOWN_ALL_VALUE;
+                }
+            } else if ((!switch_pressed) && (switch_held == SWITCH_DOWN_ALL_VALUE)){
+                switch_held = OFF;
                 bed_move_stop(BED_SOURCE_SWITCHES);
             }
             break;
 
         case SWITCH_BED_LIGHTS:
-            if (switch_pressed && (active_switch == OFF)){
-                bed_lights_set(BED_SOURCE_SWITCHES, !app_lights_on());
-
-            } else if ((!switch_pressed) && (active_switch == SWITCH_BED_LIGHT_VALUE || active_switch == OFF)){
+            if (switch_pressed && (switch_held == OFF)){
+                switch_held = SWITCH_BED_LIGHT_VALUE;
+                if (bed_lights_set(BED_SOURCE_SWITCHES, !app_lights_on())) {
+                    switch_driving = SWITCH_BED_LIGHT_VALUE;
+                }
+            } else if ((!switch_pressed) && (switch_held == SWITCH_BED_LIGHT_VALUE)){
+                switch_held = OFF;
                 sl_sleeptimer_stop_timer(&double_press_error_timer);
                 /*** For "Settings Mode" ***/
                 settings_mode_cancel_entry();
@@ -274,11 +326,15 @@ static void process_inputs()
             for (uint8_t t = 0; t < NUM_MOTOR_SWITCH_TABLE_ENTRIES; t++) {
                 if (app_ui_interrupt == motor_switch_table[t].mask) {
                     matched = true;
-                    if (switch_pressed && (active_switch == OFF)) {
-                        bed_move_start(BED_SOURCE_SWITCHES,
-                                       side_to_target(motor_switch_table[t].side),
-                                       motor_switch_table[t].up ? BED_DIR_UP : BED_DIR_DOWN);
-                    } else if ((!switch_pressed) && (active_switch == motor_switch_table[t].value || active_switch == OFF)) {
+                    if (switch_pressed && (switch_held == OFF)) {
+                        switch_held = motor_switch_table[t].value;
+                        if (bed_move_start(BED_SOURCE_SWITCHES,
+                                           side_to_target(motor_switch_table[t].side),
+                                           motor_switch_table[t].up ? BED_DIR_UP : BED_DIR_DOWN)) {
+                            switch_driving = motor_switch_table[t].value;
+                        }
+                    } else if ((!switch_pressed) && (switch_held == motor_switch_table[t].value)) {
+                        switch_held = OFF;
                         bed_move_stop(BED_SOURCE_SWITCHES);
                     }
                     break;
```

`docs/DECISIONS.md` (+#37) is docs-only; `git show d9649a9 -- docs/`
reproduces it and its substance is §b.

---

## d. Raw output of every command run

No secrets; no redaction.

### d.1 Enumerating the writers and readers of `active_switch`

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
...
./switch_input.c:365:              active_switch = OFF;
```

### d.2 The limit-flag defect

```
$ grep -rn "at_limit_switch\[" --include=*.c . | grep -v test/
./bed_status.c:88:    out->motors[i].at_upper_limit = at_limit_switch[i].upper;
./dual_motor_control.c:216:            if (at_limit_switch[motor].upper == false){
./dual_motor_control.c:217:                at_limit_switch[motor].upper = true;
./dual_motor_control.c:232:            if (at_limit_switch[motor].lower == true){
...
./dual_motor_control.c:330:      if ((motor_state[MOTORA].motor_up && motor_state[MOTORD].motor_up) && !(at_limit_switch[MOTORA].upper || at_limit_switch[MOTORD].upper)){
./dual_motor_control.c:349:      if ((motor_state[MOTORA].motor_up && motor_state[MOTORB].motor_up) && !(at_limit_switch[MOTORA].upper || at_limit_switch[MOTORB].upper)){
```

Lines 330+ are the reason a stale flag stops the bed responding: they gate
`set_dual_motors_on()`.

### d.3 Firmware build

```
$ cmake --build build --config base --target tilt_bed_controller
[1/11] Building C object .../bed_status.c.obj
[2/11] Building C object .../bed_settings.c.obj
[3/11] Building C object .../settings_mode.c.obj
[4/11] Building C object .../switch_input.c.obj
[5/11] Building C object .../bed_actions.c.obj
[6/11] Building C object .../dual_motor_control.c.obj
[7/11] Building C object .../motor_current_functions.c.obj
[8/11] Building C object .../ble_bed_service.c.obj
[9/11] Building C object .../auto_level.c.obj
[10/11] Building C object .../app.c.obj
[11/11] Linking C executable base\tilt_bed_controller.out
```

Clean, zero warnings.

### d.4 Host unit test

```
$ gcc -std=c99 -Wall -I.. -o bed_control_test.exe bed_control_test.c ../bed_control.c
$ ./bed_control_test.exe
PASS: scenario 6b: bed_move_stop still works while inhibited
PASS: scenario 6c: lights are not motion and are not inhibited

0 check(s) failed
```

All 30 checks still pass. `bed_control.c` was not modified this session, so
this is a regression check rather than new coverage — the arbitration and #36
latch behaviour is unchanged.

### d.5 Commit

```
$ git log --oneline -1
d9649a9 Split active_switch: give the physical panel its own held-button state
```

### d.6 Failures

None this session.

---

## e. VERIFIED vs ASSERTED

### VERIFIED

| # | Claim | Evidence |
|---|---|---|
| V1 | `process_outputs()` runs from the end of `process_inputs()` | `switch_input.c:297` |
| V2 | Its press arm could energise from an app-written `active_switch` | pre-change `switch_input.c:205`; `bed_actions.c:45` is the app's write |
| V3 | `process_inputs`' press guard discarded physical presses during app moves | pre-change `switch_input.c:277` |
| V4 | The release conditions were asymmetric | pre-change `switch_input.c:281` vs `:208` |
| V5 | The physical path's only hardware de-energise is that release arm | `bed_actions.c` `motor_button_released()` sets flags only |
| V6 | `run_motor_supervision()` is gated on `active_switch != OFF` | `switch_input.c:333` |
| V7 | Stale limit flags block `set_dual_motors_on()` | `dual_motor_control.c:330,338,349,357,368` |
| V8 | The limit clear branch requires the motor to be on AND moving | `dual_motor_control.c:232-238` |
| V9 | Firmware builds clean | §d.3 |
| V10 | Host unit test 30/30 | §d.4 |
| V11 | The app cannot write `switch_held`/`switch_driving` | both `static` in `switch_input.c`; no accessor exported |

### ASSERTED

| # | Claim | Why |
|---|---|---|
| A1 | **The runaway is fixed.** | **Not claimed.** The mechanism is removed; the specific observed sequence was never reproduced. See §b.1. |
| A2 | A physical press now latches #36 in both directions | Never run. Steps (a)/(c). |
| A3 | The auto-level lockup is gone | Never run. Step (d). |
| A4 | False limit flags no longer appear | Never run. Step (e). |
| A5 | No indicator blip on the movement conflict path | Never run. Step (f). |
| A6 | The #35 stutter is still gone | Untouched by inspection; step (h) re-tests. |
| A7 | Normal panel operation is unaffected | The panel path's press/release matching variable changed; this is the largest regression risk in the change. Step (i). |

---

## f. QUESTIONS FOR REVIEW

1. **The `active_switch == OFF` supervision gap is still there.**
   `run_motor_supervision()` sits inside a block gated on
   `active_switch != OFF`, so motors energised while it is OFF get no
   collision or limit supervision. The split removes the way I know of to
   reach that state, but it does not close the gap. Widening the block means
   touching the double-press check's enclosing condition, which you told me
   not to disturb. Worth a separate change?
2. **The auto-level conflict path can still blip amber**, because
   `app_stop_movement()` routes through `app_auto_level_stop()`, which calls
   `set_all_LED_off()`. Fixing it means editing a function the physical
   auto-level path shares. Do it?
3. **Clearing all limit flags on every stop** is deliberately blunt. If the bed
   really is at a limit, the flag is gone until the next move re-infers it
   (~1 s). That matches existing post-stop behaviour, but say if you want it
   narrower.
4. **`switch_held` is cleared on release even when the press was refused.**
   That is required so the #36 latch can clear — but it means a refused press
   still occupies the panel's "held" slot until released, so a *second*
   physical button pressed meanwhile is ignored. That mirrors the panel's
   one-button-at-a-time model and the double-press check, but it is a
   behaviour worth naming.

---

## g. NOT DONE

| Item | Reason |
|---|---|
| Claiming the runaway fixed | Explicitly not claimed — §b.1, A1. |
| Widening the supervision gate | §f.1; would disturb the #35 gate's enclosing condition. |
| The auto-level amber blip | §f.2. |
| Touching the #35 double-press fix | Forbidden; untouched. |
| Release-config build | Still owed before Stage C ships. |
| Manual-move loop-rate measurement | Still owed, four sessions old. |
| Flashing / bench | User drives the bench. |
| Pushing | 32 commits ahead of `origin/main`. |

---

## BENCH ACCEPTANCE

**Keep the wall switch reachable and be ready to cut power.** Step (g) is
deliberately hunting for a runaway.

### Step 0 — build and flash

```
cd C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\tilt_bed_controller
```
```
git log --oneline -1
```

Expect `d9649a9 Split active_switch...` or later. Build and flash as normal.
PuTTY on **COM6**, 115200 8N1, Session → Logging → "All session output" →
`split_acceptance.log`. Connect the app and let it bond.

### Step a — app moving, physical pressed (the (d) failure)

1. Hold a direction in the app. The bed moves.
2. While still holding, press **any** physical button.

**Expect:** motors and the movement LEDs go out **immediately**, and stay out
while either button is held. Wait five seconds holding both.

### Step b — release physical, keep holding app

From (a), release the physical button, keep holding the app button.

**Expect:** still nothing moves. Then release the app button and press it
again — normal movement.

### Step c — mirror

1. Hold a physical direction. The bed moves.
2. Press an app direction.

**Expect:** same — immediate stop, stays stopped until both released.

### Step d — the auto-level lockup

1. Tilt the bed. Hold the app's **Hold to Level** button; levelling runs.
2. Press the **physical** auto-level button. Levelling stops.
3. Release everything.
4. Now try **app** auto-level again. It must run normally.
5. Then try **physical** auto-level. It must run normally.

**Expect:** both work on the next attempt, with **no manual-move button needed
to unstick anything**. No repeating "Connection stalled" in the app.

### Step e — no false limit flags

Immediately after step (d), look at the app's motor status display.

**Expect:** no motor reports at upper or lower limit. Previously MOTOR D
falsely showed upper and MOTOR A lower.

### Step f — the lights must not blink

1. Turn the under-bed lights **on**. Amber indicator lit.
2. Trigger a conflict exactly as in step (a), watching the lights and the
   amber LED closely.

**Expect:** neither blinks at all — not even briefly.

*(Known residual: if the conflict interrupts auto-level rather than a move,
amber may still blip. Note it if you see it; it is §f.2, not a new fault.)*

### Step g — THE RUNAWAY HUNT

This is the important one and it is a numbers game, because the original was
seen once and never reproduced.

Tap each physical button **briefly** — press and release inside about half a
second — and confirm the bed stops the instant you let go.

Do the full set in **four passes**:

1. **App disconnected**, all 11 buttons (4 up, 4 down, ALL up, ALL down,
   auto-level).
2. **App connected but idle**, all 11.
3. **App actively moving** (hold a direction in the app with your other hand),
   all 11.
4. **App holding the SAME button** you are tapping — e.g. app holding LEFT UP
   while you tap physical LEFT UP. Do this for all four sides, both
   directions.

**That is roughly 50 taps.** I would call that a meaningful test for a fault
seen once: it covers every button in every source state, including the
same-button case that the confirmed mechanism required. It is not proof of
absence — nothing at this scale would be — and if you want more confidence,
pass 4 is the one to repeat, since it is the only pass that exercised the
confirmed mechanism.

**At any point, if the bed moves with no button held: cut power, note exactly
which button, which pass, and what the app was doing.** That is the single
most valuable data point you could send back.

### Step h — the stutter must still be gone

Hold app **UP RIGHT**, press and release a physical button, keep holding the
app button.

**Expect:** no stutter. Afterwards search `split_acceptance.log` in Notepad
for `Double Press Error` — isolated entries from genuine two-button presses
are fine, a repeating stream is not.

### Step i — normal operation (biggest regression risk)

The panel's press/release matching variable changed, so exercise it properly:

1. Every single-side button from the **physical** panel, up and down, held for
   several seconds each.
2. Physical ALL UP and ALL DOWN.
3. Physical auto-level, held to completion.
4. Physical BED_LIGHTS on and off.
5. Physical double-press: press two motor buttons at once — `Double Press
   Error` must still appear and motors stop.
6. Settings mode: hold BED_LIGHTS 5 s, confirm entry, and that the
   sensitivity ladder still works.
7. Collision on a **physical** move and on an **app** move — both must trip.
8. Limit stop: drive one side to its physical limit from the panel and confirm
   it stops and that the opposite direction still works immediately.

### Step j — full regression

All of `docs/REGRESSION.md`, phone bonded and subscribed throughout, including
§6.8. Watch for `reason=0x1008`.

### What to send back

- `split_acceptance.log`.
- Pass/fail for steps a–j.
- **For step g: how many taps you did, and whether the bed ever moved with no
  button held.** If it did, everything about that moment.
- For step e: the motor status display after the auto-level conflict.
- For step f: did anything blink.

Everything stays **ASSERTED** until this comes back — and the runaway stays
open regardless, because it was never reproduced in the first place.
