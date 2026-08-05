# fw_HANDOFF_2026-08-05_03 — bench-only runaway provocation; fail-safe cleanup gaps

Closes assumption A4 from the previous handoff by building the means to test
it. **The fail-safe is still unproven** — the provocation exists and compiles,
it has not been run.

---

## a. Header

| Field | Value |
|---|---|
| Repo | `tilt_bed_controller` |
| Branch | `main` |
| HEAD before session | `7fdea90` |
| HEAD after session | `732dfed` (plus this handoff's own commit) |
| Session date | 2026-08-05 (from `date`) |
| Build | Clean with the flag **OFF** and **ON**. Flag committed OFF. Not flashed. |

### Commits this session

```
732dfed Bench-only runaway provocation to prove the fail-safe, plus two cleanup gaps
```

### `git status` at end of session

```
On branch main
Your branch is ahead of 'origin/main' by 36 commits.

Changes not staged for commit:
	modified:   .projectlinkstore

Untracked files:
	.projectchecksums
	docs/handoffs/fw_HANDOFF_2026-08-05_03_runaway-provocation.md
	tilt_bed_controller_project.pdf
```

**`BED_BENCH_PROVOKE_RUNAWAY` is committed as `0`.** Verified after the last
build — see §d.4.

---

## b. Analysis

### b.1 What state does a fail-safe trip leave behind? (asked; code-only)

**Already correct, no change needed:**

| State | Why it is fine |
|---|---|
| Hardware outputs | `set_all_motors_off()` |
| `motor_state[].motor_on` | cleared in the loop |
| Movement LEDs | green/blue/red cleared individually |
| `switch_held`, `switch_driving` | `clear_switch_panel_state()` |
| `active_switch` | set to `OFF` |
| `app_move_active` / `app_auto_level_active` | **cannot be stale** — the fail-safe only runs when `ble_bed_service_app_motion_active()` is already false. It is a precondition, not an oversight. |
| `bed_control`'s `active_source` | self-releases on the next `bed_control_tick()`, because `app_bed_is_idle()` is now true (`active_switch == OFF`, and the fail-safe requires `!in_settings_mode`) |

**Two real gaps, both closed:**

1. **The switches' half of the #36 latch.** The fail-safe clears
   `switch_held`. If the inhibit was latched, a later physical release finds
   `switch_held == OFF`, the release branch `(!switch_pressed) && (switch_held
   == value)` fails, `bed_move_stop()` is never called, and
   `source_commanding[SWITCHES]` stays true — **the latch could then never
   clear**. That is precisely your concern: a runaway converted into the stuck
   state. Now calls `bed_note_source_released(BED_SOURCE_SWITCHES)`.

   This is justified rather than defensive: the fail-safe has just established
   **by direct hardware read** that no switch is down, so the panel is
   definitively not commanding. It is the one place in the codebase entitled to
   assert that.

2. **Inferred limit flags.** Every other stop path clears these (#37); the
   fail-safe did not. A runaway is exactly the case where motors ran
   unsupervised, so any `at_limit_switch[]` inference made during one is
   worthless — and a stale one blocks `set_dual_motors_on()` permanently. Now
   calls `reset_limit_switch_flags()`.

Both are cleanup on the trip path, not changes to how the fail-safe *decides*.
Its detection logic is untouched.

### b.2 The provocation

`BED_BENCH_PROVOKE_RUNAWAY`, default `0`, following `BLE_BENCH_WIPE_BONDS`.

| | |
|---|---|
| Trigger, single-side | tap and release **UP HEAD** → `set_dual_motors_on(HEAD)` |
| Trigger, all four | tap and release **UP ALL** → `set_all_motors_on()` |
| Delay after release | 300 ms (`PROVOKE_DELAY_MS`, sleeptimer, #2) |
| Energise call | the **same** calls the real paths use — not a simulation |
| Timing | energise tick stamped; fail-safe prints elapsed ms on the trip |

Two triggers because the single-side and all-motor paths energise through
different calls, and your requirement 5 asks for both.

The 300 ms delay matters: it is long enough that the release has been fully
processed and every variable is back to idle, so the fail-safe meets a real
"nothing is commanding" state rather than racing the release path. A shorter
delay would risk testing the race instead of the fail-safe.

**How far the bed will actually move.** The fail-safe samples every 50 ms and
needs 3 consecutive quiet samples, so worst case is roughly **150–200 ms** of
travel from energise to de-energise. At typical actuator speed that is on the
order of **1–3 mm** — you will more likely *hear* the motors start and stop
than see the bed move. Do not expect a dramatic lurch; watch the console, not
the frame.

### b.3 A placement bug the compiler caught — worth reading

I first defined the flag next to the fail-safe, near the end of the file,
where it reads most naturally. **The trigger sites are in `process_inputs()`,
earlier in the file.** So `#if BED_BENCH_PROVOKE_RUNAWAY` there evaluated an
undefined macro to `0` and compiled the triggers out.

**The provocation would have done nothing at all** — and the failure looks
exactly like success: flag on, build clean, flash, tap the button, no runaway,
no `FAILSAFE:` print. The obvious reading is "the fail-safe works". The bench
session would have been spent proving nothing, and would have produced a
false VERIFIED.

Only `-Wunused-function` on `provoke_arm` exposed it — the definition existed
with no caller. The flag now sits at the top of the file with a comment saying
why it must, and **both flag states build warning-free** (§d.2, §d.3).

Generalising, because it will happen again: a bench flag whose only job is to
change behaviour is exactly the kind of thing that can silently not work, and
its failure mode is indistinguishable from the result you were hoping for.
Build it with the flag on and read the warnings before trusting it.

---

## c. Complete diff

`732dfed`, source verbatim and in full. Diffstat:

```
docs/DECISIONS.md |  79 +++++++++++++++++++++++++++++++++
switch_input.c    | 127 +++++++++++++++++++++++++++++++++++++++++++++++++++++-
2 files changed, 205 insertions(+), 1 deletion(-)
```

### c.1 Source diff

```diff
diff --git a/switch_input.c b/switch_input.c
index d724621..e44cc51 100644
--- a/switch_input.c
+++ b/switch_input.c
@@ -1,4 +1,4 @@
-/*
+﻿/*
  * switch_input.c
  *
  * Relocated from app.c on C4 commit 4 (Jul 2026) - see switch_input.h. Pure
@@ -55,6 +55,47 @@
 // OUTPUTS and are deliberately excluded - see the fail-safe below.
 #define SWITCH_INPUT_MASK 0xFF0F
 
+/*
+ * BENCH-ONLY RUNAWAY PROVOCATION - MUST NEVER BE 1 IN A NORMAL BUILD.
+ *
+ * Same convention as BLE_BENCH_WIPE_BONDS in ble_bed_service.c: a hardcoded,
+ * source-visible flag that has to be hand-flipped for a bench session and
+ * hand-flipped back afterwards. Default 0.
+ *
+ * When 1, tapping and RELEASING a physical button deliberately energises a
+ * motor pair ~300 ms later, with no button held and no source commanding -
+ * i.e. it manufactures exactly the runaway the motion fail-safe exists to
+ * catch, so the fail-safe can be tested in the only direction that matters.
+ * Until this has been run, "the fail-safe would catch a runaway" is an
+ * assumption with no evidence behind it.
+ *
+ *   tap UP HEAD  -> provokes the HEAD pair   (set_dual_motors_on)
+ *   tap UP ALL   -> provokes all four motors (set_all_motors_on)
+ *
+ * Two different calls on purpose: the single-side and all-motor paths energise
+ * differently, so both need proving.
+ *
+ * It energises through the SAME calls the real paths use, so the fail-safe is
+ * tested against a genuinely energised bed rather than a simulation of one.
+ *
+ * MUST be defined here, above process_inputs() - the trigger sites are in it.
+ * Defining it further down (next to the fail-safe, where it reads more
+ * naturally) silently compiles the trigger sites out, and the provocation
+ * does nothing at all. That happened; -Wunused-function caught it.
+ *
+ * WHY IT MUST NEVER SHIP: a production unit with this enabled would drive a
+ * motor pair with nothing held, every time a button was tapped, relying on the
+ * fail-safe as its only stop. That is a bed that moves on its own - the exact
+ * behaviour this whole subsystem exists to prevent.
+ */
+#define BED_BENCH_PROVOKE_RUNAWAY 0
+
+// How long after the button release to energise. Long enough that the release
+// has been fully processed and every variable is back to idle, so the
+// fail-safe faces a real "nothing is commanding" state rather than a race with
+// the release path. Milliseconds via sleeptimer (DECISIONS #2).
+#define PROVOKE_DELAY_MS 300
+
 // Flags
 static bool read_ui_data = false; // Interrupt flag to read the UI status
 static bool process_switch_change = true; // Indicates now clear to deal with another switch press
@@ -193,6 +234,49 @@ static bed_target_t side_to_target(side_of_bed_t side)
   return BED_TARGET_HEAD; // unreachable - side_of_bed_t has exactly these 4 values
 }
 
+#if BED_BENCH_PROVOKE_RUNAWAY
+static bool     provoke_armed = false;
+static bool     provoke_all = false;
+static uint32_t provoke_at_tick = 0;
+static uint32_t provoke_energised_tick = 0;
+static bool     provoke_running = false;
+
+// Called from process_inputs() on the release of the trigger buttons.
+static void provoke_arm(bool all)
+{
+  provoke_armed = true;
+  provoke_all = all;
+  provoke_at_tick = sl_sleeptimer_get_tick_count()
+                    + sl_sleeptimer_ms_to_tick(PROVOKE_DELAY_MS);
+}
+
+// Called once per tick, BEFORE motion_failsafe_tick(), so the fail-safe sees
+// the energised state on one of its own sample points rather than in the same
+// statement.
+static void provoke_tick(void)
+{
+  if (!provoke_armed) {
+    return;
+  }
+  if ((int32_t)(sl_sleeptimer_get_tick_count() - provoke_at_tick) < 0) {
+    return;
+  }
+  provoke_armed = false;
+
+  // The same calls the real paths use - not a simulation.
+  if (provoke_all) {
+    set_all_motors_on();
+  } else {
+    set_dual_motors_on(HEAD);
+  }
+
+  provoke_energised_tick = sl_sleeptimer_get_tick_count();
+  provoke_running = true;
+  printf("PROVOKE: energised %s with nothing commanding - fail-safe should stop this\r\n",
+         provoke_all ? "ALL MOTORS" : "HEAD pair");
+}
+#endif // BED_BENCH_PROVOKE_RUNAWAY
+
 // Executes changes in motor states to turn various power outputs ON/OFF
 static void process_outputs()
 {
@@ -317,6 +401,9 @@ static void process_inputs()
             } else if ((!switch_pressed) && (switch_held == SWITCH_UP_ALL_VALUE)){
                 switch_held = OFF;
                 bed_move_stop(BED_SOURCE_SWITCHES);
+#if BED_BENCH_PROVOKE_RUNAWAY
+                provoke_arm(true); // bench only - all four motors
+#endif
             }
             break;
 
@@ -374,6 +461,11 @@ static void process_inputs()
                     } else if ((!switch_pressed) && (switch_held == motor_switch_table[t].value)) {
                         switch_held = OFF;
                         bed_move_stop(BED_SOURCE_SWITCHES);
+#if BED_BENCH_PROVOKE_RUNAWAY
+                        if (motor_switch_table[t].value == SWITCH_UP_HEAD_VALUE) {
+                            provoke_arm(false); // bench only - HEAD pair
+                        }
+#endif
                     }
                     break;
                 }
@@ -446,6 +538,10 @@ static bool any_motor_energised_now(void)
 
 void motion_failsafe_tick(void)
 {
+#if BED_BENCH_PROVOKE_RUNAWAY
+  provoke_tick(); // bench only - manufactures the runaway this must catch
+#endif
+
   uint32_t now = sl_sleeptimer_get_tick_count();
   if (sl_sleeptimer_tick_to_ms(now - failsafe_last_sample_tick) < FAILSAFE_SAMPLE_MS) {
     return;
@@ -497,10 +593,39 @@ void motion_failsafe_tick(void)
   clear_switch_panel_state();
   active_switch = OFF;
 
+  // Leave NO desynced bookkeeping behind. A fail-safe that stops the motors
+  // and strands the state has converted a runaway into the stuck state this
+  // project has already hit twice (#38 additions).
+  //
+  //  - The switches' half of the #36 latch: this function has just
+  //    established by direct read that no switch is down, so the panel is
+  //    definitively not commanding. Without this the latch could stay set
+  //    forever, because clearing switch_held above means a later release
+  //    event finds no match and never reaches bed_move_stop().
+  //  - Inferred limits: every other stop path clears these (#37). A runaway
+  //    is precisely the case where motors ran unsupervised, so any inference
+  //    made during it is worthless.
+  //
+  // app_move_active / app_auto_level_active need no clearing - the fail-safe
+  // only runs when ble_bed_service_app_motion_active() is already false.
+  // bed_control's active_source self-releases on the next bed_control_tick()
+  // now that app_bed_is_idle() is true.
+  bed_note_source_released(BED_SOURCE_SWITCHES);
+  reset_limit_switch_flags();
+
   if (!failsafe_tripped) {
     failsafe_tripped = true;
 #if !BED_CAPTURE_MOTOR_CURRENTS
     printf("FAILSAFE: motors energised with nothing commanding - stopped\r\n");
+#endif
+#if BED_BENCH_PROVOKE_RUNAWAY
+    // Measured, not assumed: energise -> de-energise, in milliseconds.
+    if (provoke_running) {
+      provoke_running = false;
+      uint32_t elapsed_ms = sl_sleeptimer_tick_to_ms(
+                              sl_sleeptimer_get_tick_count() - provoke_energised_tick);
+      printf("PROVOKE: fail-safe caught it after %lu ms\r\n", (unsigned long)elapsed_ms);
+    }
 #endif
   }
 }
```

`docs/DECISIONS.md` (+#39) is docs-only; `git show 732dfed -- docs/`
reproduces it and its substance is §b.

---

## d. Raw output of every command run

No secrets; no redaction.

### d.1 Re-reading the fail-safe cleanup, and the limit guard

```
$ sed -n '/^void motion_failsafe_tick/,/^}/p' switch_input.c | tail -22
    failsafe_quiet_samples++;
    return;
  }

  // Nothing is commanding motion and a motor is on. De-energise.
  set_all_motors_off();
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    motor_state[i].motor_on = false;
  }
  set_LED_off(LED_GREEN);
  set_LED_off(LED_BLUE);
  set_LED_off(LED_RED);
  clear_switch_panel_state();
  active_switch = OFF;

  if (!failsafe_tripped) {
    failsafe_tripped = true;
#if !BED_CAPTURE_MOTOR_CURRENTS
    printf("FAILSAFE: motors energised with nothing commanding - stopped\r\n");
#endif
  }
}
```

That listing is the evidence for §b.1: no `bed_note_source_released()`, no
`reset_limit_switch_flags()`.

### d.2 First build pair — THE FAILURE THAT MATTERED

```
=== BUILD 1: flag OFF (default, ships) ===
[1/2] Building C object .../switch_input.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
=== BUILD 2: flag ON (bench provocation) ===
[1/2] Building C object .../switch_input.c.obj
C:/Users/adunc/.../switch_input.c:489:13: warning: 'provoke_arm' defined but not used [-Wunused-function]
  489 | static void provoke_arm(bool all)
      |             ^~~~~~~~~~~
[2/2] Linking C executable base\tilt_bed_controller.out
=== restore flag OFF ===
...switch_input.c:469:#define BED_BENCH_PROVOKE_RUNAWAY 0
```

`provoke_arm` defined but never called, with the flag ON, means the trigger
sites did not compile in — the `#define` at line 469 sat **after**
`process_inputs()`. §b.3.

### d.3 Second build pair — after relocating the flag and helpers

```
$ grep -n "provoke_arm(\|^static void provoke_arm\|^static void provoke_tick\|^static void process_outputs\|^static void process_inputs\|BED_BENCH_PROVOKE_RUNAWAY 0" switch_input.c
91:#define BED_BENCH_PROVOKE_RUNAWAY 0
245:static void provoke_arm(bool all)
256:static void provoke_tick(void)
281:static void process_outputs()
367:static void process_inputs()
405:                provoke_arm(true); // bench only - all four motors
466:                            provoke_arm(false); // bench only - HEAD pair
```

Flag at 91, helpers at 245/256, call sites at 405/466 — definitions now
precede uses.

```
=== BUILD flag OFF ===
[1/2] Building C object .../switch_input.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
=== BUILD flag ON (must be warning-free now) ===
[1/2] Building C object .../switch_input.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
=== restore OFF + rebuild ===
...switch_input.c:91:#define BED_BENCH_PROVOKE_RUNAWAY 0
[1/2] Building C object .../switch_input.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
```

Both states clean, no warnings.

### d.4 Flag state as committed

```
$ Select-String -Path switch_input.c -Pattern 'define BED_BENCH_PROVOKE_RUNAWAY'
switch_input.c:91:#define BED_BENCH_PROVOKE_RUNAWAY 0
```

### d.5 Commit

```
$ git log --oneline -1
732dfed Bench-only runaway provocation to prove the fail-safe, plus two cleanup gaps
```

### d.6 Failures

Two, both mine, both recovered:

1. The flag placement bug — §d.2, the most useful failure of the session.
2. A `python`/`node` heredoc I used to try to relocate the provoke block did
   not execute (the fallback chain silently did nothing); the block was still
   at its old line number afterwards. Redone with `sed` extract-delete-reinsert
   and verified by the grep in §d.3.

---

## e. VERIFIED vs ASSERTED

### VERIFIED

| # | Claim | Evidence |
|---|---|---|
| V1 | The fail-safe did not release the switches' half of the #36 latch, nor clear limit flags | §d.1 listing |
| V2 | `app_move_active` cannot be stale at a trip | the fail-safe returns early when `ble_bed_service_app_motion_active()` is true |
| V3 | `active_source` self-releases after a trip | `bed_control_tick()` clears it when `app_bed_is_idle()`, which is true once `active_switch == OFF` |
| V4 | The provocation would NOT have worked as first written | `-Wunused-function` on `provoke_arm`, §d.2 |
| V5 | Definitions now precede uses | §d.3 grep: flag 91, helpers 245/256, calls 405/466 |
| V6 | Both flag states build clean and warning-free | §d.3 |
| V7 | The flag is committed OFF | §d.4 |
| V8 | The provocation energises through the same calls the real paths use | `set_dual_motors_on(HEAD)` / `set_all_motors_on()` in `provoke_tick()` |

### ASSERTED

| # | Claim | Why |
|---|---|---|
| A1 | **The fail-safe catches a runaway.** | **Still unproven.** This session built the means to find out and nothing more. This is the whole point of the next bench run. |
| A2 | The provocation actually energises the motors on hardware | Compiles and is wired; never run. `set_dual_motors_on()` refuses at a limit flag, so a bed sitting at its upper limit would produce no runaway and look like a pass — see the instructions. |
| A3 | ~150–200 ms / 1–3 mm of travel | Arithmetic from `FAILSAFE_SAMPLE_MS × FAILSAFE_CONFIRM_SAMPLES` plus an assumed actuator speed. The **measured** number comes from the `PROVOKE:` print. |
| A4 | The two cleanup additions are correct and sufficient | Reasoned from the code; a trip has never been observed. |
| A5 | Nothing else regressed | Flag-OFF build is the only change to shipped behaviour, and it is two extra calls on a path that has never fired in normal use. |

---

## f. QUESTIONS FOR REVIEW

1. **`set_dual_motors_on()` refuses to energise a pair at a limit flag.** So if
   the HEAD pair is at its upper limit, the provocation produces nothing and
   looks exactly like a pass. The instructions say to start mid-stroke, but it
   is a trap worth knowing about.
2. **The provocation triggers on UP HEAD / UP ALL release**, which are also
   normal buttons. With the flag on, those two buttons are unusable for normal
   testing. Acceptable for a dedicated session; say if you would rather it were
   an unused combination.
3. **Should the fail-safe's `bed_note_source_released()` call worry anyone?**
   It asserts the switches are not commanding, on the strength of a direct
   hardware read taken in the same function. I think that is the one place
   entitled to make that assertion, but it is a cross-module write from an
   error path.
4. **If the provocation shows the fail-safe does NOT fire**, the most likely
   causes in order: `SWITCH_INPUT_MASK` wrong (still unverified against a
   schematic — carried from #38), `any_motor_energised_now()` reading flags
   that `set_dual_motors_on()` did not set, or the confirm window never
   completing because something re-arms it. §g of the next handoff will need
   to say which.

---

## g. NOT DONE

| Item | Reason |
|---|---|
| **Running the provocation** | Requires the bench. This is the entire point of the next session. |
| Verifying `SWITCH_INPUT_MASK` against the schematic | Carried from #38; still open, and §f.4 makes it the first suspect if the fail-safe does not fire. |
| Ungating supervision from `active_switch` | Still open from #38. |
| Changing the fail-safe's detection logic | Explicitly out of scope — testing it, not changing it. |
| Release-config build | Still owed before Stage C ships. |
| Manual-move loop-rate measurement | Still owed. |
| Pushing | 36 commits ahead of `origin/main`. |

---

## BENCH ACCEPTANCE — provoking the runaway

**Read this whole section before starting.** The bed will deliberately move
with nothing held. That is the point.

### Before you start

- **Put the bed mid-stroke**, not at either limit. `set_dual_motors_on()`
  refuses to energise a pair that has a limit flag set, so a bed at its upper
  limit would produce no runaway at all and look like a pass (§f.1).
- Bed frame clear of obstructions.
- Keep the wall switch reachable and be ready to cut power.

### Step 1 — turn the flag ON

Open this file in VS Code:

```
C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\tilt_bed_controller\switch_input.c
```

Press **Ctrl+F** and search for:

```
BED_BENCH_PROVOKE_RUNAWAY
```

Near the top of the file (around line 91) you will find:

```c
#define BED_BENCH_PROVOKE_RUNAWAY 0
```

Change the `0` to a `1`:

```c
#define BED_BENCH_PROVOKE_RUNAWAY 1
```

Save the file (**Ctrl+S**).

### Step 2 — build and flash

Build and flash exactly as you normally do from VS Code.

Then open PuTTY on **COM6**, 115200 8N1, and turn on logging:
Session → Logging → "All session output" → filename `provoke_runaway.log`.

### Step 3 — provoke the single-side runaway

1. **Tap UP HEAD** on the physical panel — press and release it normally, like
   any other button. The head will rise while you hold it; that part is normal.
2. **Let go and watch the console.**

**About 300 ms after you release**, you should see:

```
PROVOKE: energised HEAD pair with nothing commanding - fail-safe should stop this
FAILSAFE: motors energised with nothing commanding - stopped
PROVOKE: fail-safe caught it after NNN ms
```

**What to expect physically:** the head pair runs for roughly 150–200 ms —
about 1–3 mm. You will more likely hear the motors click on and off than see
the bed move. Do not expect a lurch.

**Write down the `NNN` number.** That is the measured energise-to-de-energise
time, which is the whole point of the exercise.

Do this **five times** and note all five numbers.

### Step 4 — provoke the all-four runaway

1. **Tap UP ALL** and release.

**Expect:** the same three lines, but with `ALL MOTORS` instead of `HEAD pair`.

This matters because all-four energises through a different call
(`set_all_motors_on()` rather than `set_dual_motors_on()`), so it is a separate
proof.

Do this **five times** and note the numbers.

### Step 5 — the state afterwards must be clean

Immediately after a provocation trips:

1. Press **every** physical button in turn — all four sides up and down, ALL
   UP, ALL DOWN, **auto-level**, and **lights**.
2. Confirm each does what it should, with no board reset needed.
3. Check the app: no motor should report a false limit, and current should
   read non-zero while motors run.
4. Connect the app and do one app move and one app auto-level.

**Expect:** everything normal. If any button is dead, or a limit flag is stuck,
say which — that means the trip left state behind and §b.1's cleanup is
incomplete.

### Step 6 — IF THE FAIL-SAFE DOES NOT FIRE

If you see `PROVOKE: energised ...` but **no** `FAILSAFE:` line, and the bed
keeps running:

**Cut power immediately.**

That is the most valuable possible result and it means the fail-safe does not
work. Send back: the log, whether it was the HEAD or ALL case, and how long
the bed ran before you cut power. Do not continue testing.

If you see **neither** print, the flag did not take effect — check step 1 saved
and that you flashed the new build.

### Step 7 — TURN THE FLAG BACK OFF (do not skip)

Back in `switch_input.c`, change it back:

```c
#define BED_BENCH_PROVOKE_RUNAWAY 0
```

Save. Then confirm it is off — in a terminal:

```
cd C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\tilt_bed_controller
```
```
git diff --stat
```

**Expect: no output at all.** No output means your file matches what is
committed, which has the flag at `0`. If it lists `switch_input.c`, the flag is
still on — go back and fix it.

You can also check directly:

```
git diff switch_input.c
```

Again, no output is correct.

### Step 8 — reflash the normal build

**The provocation build must not be the one left on the bench.** Rebuild and
reflash with the flag off, and confirm from the console that tapping UP HEAD
no longer produces a `PROVOKE:` line.

A unit left running the provocation build drives a motor pair every time a
button is tapped, with the fail-safe as its only stop. Do not leave it on the
bed.

### What to send back

- `provoke_runaway.log`.
- The five `NNN` times for HEAD and the five for ALL.
- Whether every button worked afterwards (step 5).
- Confirmation that `git diff --stat` produced no output and that you reflashed
  with the flag off.
- If the fail-safe did not fire: everything from step 6.

Until this comes back, **the fail-safe remains unproven** — this session built
the test, it did not run it.
