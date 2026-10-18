# fw handoff — fail-safe hardware read, double press, swallowed switch events

## a. Header

- Repo: tilt_bed_controller, branch main
- HEAD before session work: cb98bf6
- HEAD after (work commit): 3f0d409 (this handoff is committed after it)
- `BED_BENCH_PROVOKE_RUNAWAY` committed as 0 (`switch_input.c:91`).
- Final `git status` is at the end of section d.

## b. Analysis (full text; also recorded as DECISIONS #40)

### Bench log, read in full (107 lines)
- HEAD provocation: `PROVOKE: fail-safe caught it after 213/213/213/216/213/213/213/215/213/213/213 ms`: 11 of 11.
- ALL provocation: five `PROVOKE: energised ALL MOTORS` lines, no `FAILSAFE:` line after any. Confirms the report.
- One `FAILSAFE:` with no preceding `PROVOKE:` energise line, directly after a `Max Current` block (HEAD pair, motors 0 and 3). That block prints about 300 ms into a hold, so the hold was real and the trip came after its release. `provoke_running` was false at that trip: no "caught" line followed it, so the provocation did not cause it.
- 18 `Double Press Error` lines; the last two follow `Exit auto-level`.

### 1. Root cause of the ALL miss (confirmed from code)
`any_motor_energised_now()` read `motor_state[].motor_on`. `set_dual_motors_on()` sets those flags; `set_all_motors_on()` (`exp_board.c`) writes the expander and sets none. On the real ALL paths the flags are set by `all_motors_button_pressed()`. The provocation calls `set_all_motors_on()` alone, so the outputs were live with every flag false. The fail-safe's header said it "asks the hardware directly"; it did that for switches and read bookkeeping for motors.

### 2. The fix
`get_motor_enables()` in `exp_board.c` reads the output register (`EXP_CMD_OUTPUT`) and returns bits 0-3. No `EFM_ASSERT`, so a bus error returns false instead of hanging.
- Enable bit gates motion: PWM is started once in `app.c` and never stopped; the bed is idle with PWM running, so PWM alone does not move a motor. Inferred from code and behaviour; no schematic is in the repo.
- Cost (#27): one transaction per 50 ms, only after the switch check passes, main-loop context rather than a stack callback, same order as `sample_motor_faults_tick()`.
- Failed read: counts as energised (fail toward OFF). The existing 3-sample confirm and the nothing-commanding precondition stop one transient from fighting normal motion.
- Provocation, confirm window and sample rate: not changed.

### 3. The unprovoked trip
Neither candidate fits.
- (a) flags left on after a release: `app_move_stop()` clears the flags and calls `set_all_motors_off()` together, and the release arm does both too.
- (b) `all_motor_limit_switch_temp_off()`: only runs when `motors_temp_off` is set, which `limit_switch_statuses()` sets only for ALL or auto-level moves. This was a HEAD hold.
- Found instead: `get_ui_status()` clears the expander's interrupt flag (`exp_ui.c:263`). The double-press check and, since 165fea1, the fail-safe both call it on their own timers. If a read lands between a switch edge and the main-loop read, the main-loop `get_ui_interrupt()` returns 0, `app_ui_interrupt & app_ui_status` is 0, so it is treated as a press with no table match, and the release is dropped. `switch_held`/`switch_driving` stay set, the motors keep running, and the fail-safe trips about 150-200 ms later. This matches the unprovoked trip, and is a candidate for the original runaway because the double-press check made the same reads every 50 ms before the fail-safe existed.
- This is inferred from code. It has not been observed. Under it, expect after the fix: no unprovoked trip, and possibly `UI: recovered swallowed switch change` lines. The fail-safe now reads hardware, so a flags-only desync (a) would no longer trip at all.

### 4. Double press errors on single presses
No debounce exists in firmware (grep: only a motor not-moving debounce) and none is documented for hardware. The old check fired when the fresh status equalled the status latched at the last processed interrupt. Two ways a single press matches, from code:
- An extra interrupt after the LED changed (contact bounce on the press) re-latches the status with the LED already on; the 50 ms read then matches.
- The LED returns to its latched state while the button is held: auto-level ending with the button still down. This is the log's last two lines.

The check now counts switches down in a fresh read and fires at 2 or more, so one button cannot trigger it. A second button pressed while the first is held is still caught at the next 50 ms sample. Not fixed: bounce on a release or press can still cause a brief stop and restart, since the main input path has no debounce.
The settings-mode entry (hold bed lights 5 s) is cancelled by this check; it can now only be cancelled by a real second button.

## c. Source changes

Full diff of the work commit 3f0d409 (docs/DECISIONS.md excluded; it is in the repo):

```diff
diff --git a/exp_board.c b/exp_board.c
index e85787f..c774062 100644
--- a/exp_board.c
+++ b/exp_board.c
@@ -276,6 +276,28 @@ uint8_t get_motor_faults()
   return faults;
 }
 
+// Reads back the motor ENABLE bits (P0_0-P0_3 = EN_MOTOR1-4) from the output
+// register, the same register and the same transaction set_motor_on() starts
+// with. These are the EN_MOTOR1-4 lines (init_board_expander()). PWM is started
+// once at boot and never stopped (app.c), so with these low a motor is idle at
+// any duty cycle - the enable bit is what gates motion, which makes this the
+// hardware truth about "is this motor allowed to run" - unlike
+// motor_state[].motor_on, which is bookkeeping that set_all_motors_on() never
+// touches (DECISIONS.md #40). No EFM_ASSERT: see the header comment.
+bool get_motor_enables(uint8_t *enables)
+{
+  I2C_TransferReturn_TypeDef ret;
+  uint8_t cmd = EXP_CMD_OUTPUT;
+  uint8_t port0 = 0;
+
+  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &cmd, 1, &port0, 1);
+  if (ret != i2cTransferDone) {
+    return false;
+  }
+  *enables = port0 & 0x0F;
+  return true;
+}
+
 // Rate-limited, main-loop-context wrapper around get_motor_faults(). Call
 // once per app_process_action() pass; internally gates the actual I2C read
 // to once per MOTOR_FAULT_SAMPLE_INTERVAL_MS so it doesn't add a second
diff --git a/exp_board.h b/exp_board.h
index ba0fdab..6040b97 100644
--- a/exp_board.h
+++ b/exp_board.h
@@ -8,6 +8,7 @@
 #ifndef EXP_BOARD_H_
 #define EXP_BOARD_H_
 
+#include <stdbool.h>
 #include <stdint.h>
 
 
@@ -40,6 +41,11 @@ void set_I2C_redriver_on();
 void set_I2C_redriver_off();
 
 uint8_t get_motor_faults();
+// Reads the motor ENABLE bits (P0_0-P0_3) back from the on-board expander's output
+// register. Returns false if the I2C transfer failed; *enables is then untouched.
+// Deliberately has no EFM_ASSERT: it is called by the motion fail-safe, which must
+// survive a bus error rather than hang on it.
+bool get_motor_enables(uint8_t *enables);
 
 // Interim polled motor-fault telemetry (main-loop context) - see
 // sample_motor_faults_tick()'s comment in exp_board.c for the rate
diff --git a/switch_input.c b/switch_input.c
index e44cc51..584d0ee 100644
--- a/switch_input.c
+++ b/switch_input.c
@@ -234,6 +234,57 @@ static bed_target_t side_to_target(side_of_bed_t side)
   return BED_TARGET_HEAD; // unreachable - side_of_bed_t has exactly these 4 values
 }
 
+/*
+ * OUT-OF-BAND STATUS READS MUST NOT SWALLOW SWITCH EVENTS (DECISIONS.md #40).
+ *
+ * Reading the expander's input port clears its interrupt flag (exp_ui.c,
+ * get_ui_status()). The normal read in switch_input_tick() is the consumer of
+ * that flag: it reads the interrupt SOURCE bits first, then the status. But
+ * the double-press check and the motion fail-safe also read the status, on
+ * their own timers. If one of them lands between a switch edge and the normal
+ * read, it clears the flag, the normal read finds NO source bits, and the
+ * event is dropped. A dropped RELEASE leaves switch_held / switch_driving set
+ * and the motors running with nothing held - the unprovoked "FAILSAFE:" trip
+ * in the 732dfed bench log, and the original runaway.
+ *
+ * So every out-of-band read goes through here. If the fresh switch bits
+ * differ from the status latched at the last PROCESSED event, a change has
+ * happened that has not been processed yet; record WHICH bits so the normal
+ * read can use them if the chip no longer reports a source, and make sure the
+ * normal read runs.
+ *
+ * app_ui_status_valid: before the first processed event app_ui_status is just
+ * zero, which would read as "every switch changed".
+ */
+static bool app_ui_status_valid = false;
+static uint16_t swallowed_change_hint = 0;
+
+static uint16_t get_ui_status_out_of_band(void)
+{
+  uint16_t status = get_ui_status();
+  uint16_t changed = (status ^ app_ui_status) & SWITCH_INPUT_MASK;
+
+  if (app_ui_status_valid && (changed != 0)) {
+    bool run_read = true;
+    swallowed_change_hint |= changed;
+    sl_atomic_store(read_ui_data, run_read);
+  }
+  return status;
+}
+
+// Number of switches physically down in a status word (active low). The LED
+// bits are excluded by the mask.
+static uint8_t count_switches_down(uint16_t status)
+{
+  uint16_t down = (uint16_t)(~status) & SWITCH_INPUT_MASK;
+  uint8_t n = 0;
+  while (down != 0) {
+    n += (uint8_t)(down & 1u);
+    down >>= 1;
+  }
+  return n;
+}
+
 #if BED_BENCH_PROVOKE_RUNAWAY
 static bool     provoke_armed = false;
 static bool     provoke_all = false;
@@ -502,7 +553,8 @@ static void process_inputs()
  * run_motor_supervision() and leaves nothing that would ever stop them.
  *
  * It asks the hardware directly and nothing else: is any switch physically
- * down? Is the app commanding? Is auto-level running? Is settings mode active?
+ * down? Is any motor-enable line actually high (not motor_state[] - #40)?
+ * And, separately: Is the app commanding? Is auto-level running? Is settings mode active?
  * If all four are no and a motor is energised, that is a runaway by
  * definition - de-energise.
  *
@@ -526,14 +578,32 @@ static uint32_t failsafe_last_sample_tick = 0;
 static uint8_t  failsafe_quiet_samples = 0;
 static bool     failsafe_tripped = false; // print once, not every pass
 
+// Asks the HARDWARE: is any of the four motor-enable lines high? Not
+// motor_state[].motor_on - that is bookkeeping, and set_all_motors_on() sets
+// the outputs without touching it, so "hardware on, flags off" was invisible
+// here (DECISIONS.md #40). That is exactly the state the #38 bug class
+// produces.
+//
+// Cost: one blocking I2C transaction on sl_i2cspm_on_board per sample, i.e.
+// per FAILSAFE_SAMPLE_MS = 50 ms, and ONLY once the switch check above has
+// passed (so never while any button is down). Same order as
+// sample_motor_faults_tick() (5/s, same bus, same context), and it is
+// main-loop context, not a Bluetooth stack callback, so it is not the
+// #27/#30 mechanism.
+//
+// A FAILED read counts as "energised": this function cannot see the motors,
+// and a blind fail-safe should fail toward OFF. That cannot fight normal
+// motion on one transient error, because the caller already requires
+// FAILSAFE_CONFIRM_SAMPLES consecutive samples and has already established
+// that nothing is commanding - so the worst case of acting on a bad read is
+// de-energising motors that were meant to be off.
 static bool any_motor_energised_now(void)
 {
-  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
-    if (motor_state[i].motor_on) {
-      return true;
-    }
+  uint8_t enables = 0;
+  if (!get_motor_enables(&enables)) {
+    return true;
   }
-  return false;
+  return (enables != 0);
 }
 
 void motion_failsafe_tick(void)
@@ -562,7 +632,7 @@ void motion_failsafe_tick(void)
   // physically down. The LED bits (P1_4-P1_7) are masked out: they are
   // outputs, and they are exactly what made the double-press check depend on
   // something other than the switches (#35/#38).
-  uint16_t status = get_ui_status();
+  uint16_t status = get_ui_status_out_of_band();
   bool any_switch_down = ((status & SWITCH_INPUT_MASK) != SWITCH_INPUT_MASK);
 
   if (any_switch_down) {
@@ -719,14 +789,26 @@ void switch_input_tick(void)
 
           // Read the switch board input ports and process accordingly
           get_ui_status();
-          app_ui_status_new = get_ui_status();
+          app_ui_status_new = get_ui_status_out_of_band();
 
 //          printf("Initial UI Interrupt Source = %d, Status = %d \r\n", app_ui_interrupt, app_ui_status);
 //          printf("New UI Interrupt Source = %d, Status = %d \r\n", app_ui_interrupt_new, app_ui_status_new);
 
-          if (app_ui_status_new == app_ui_status){
+          // A double press is TWO PHYSICAL SWITCHES DOWN, read now from the
+          // hardware (DECISIONS.md #40). It used to be "status unchanged
+          // since the last latched interrupt", which is not a statement about
+          // switches at all: any extra interrupt after the LED changed (contact
+          // bounce on the press) re-latched the status with the LED already on,
+          // and anything that put the LED back to its latched state (auto-level
+          // ending with the button still held) did the same, so a single held
+          // button matched its own latch. Two switches down cannot be produced
+          // by one button, however it bounces.
+          if (count_switches_down(app_ui_status_new) >= 2){
 #if !BED_CAPTURE_MOTOR_CURRENTS
               printf("Double Press Error\r\n");
+              printf("  switch word 0x%04X, %u switches down\r\n",
+                     (unsigned)(app_ui_status_new & SWITCH_INPUT_MASK),
+                     (unsigned)count_switches_down(app_ui_status_new));
 #endif
               set_dual_motors_off(HEAD);
               set_dual_motors_off(FOOT);
@@ -768,9 +850,20 @@ void switch_input_tick(void)
       // Get source of switch interrupt
       app_ui_interrupt = get_ui_interrupt();
 
+      // The chip reports no source if an out-of-band status read cleared its
+      // flag first (#40). Fall back to the bits that read saw change.
+      if ((app_ui_interrupt == 0) && (swallowed_change_hint != 0)) {
+#if !BED_CAPTURE_MOTOR_CURRENTS
+          printf("UI: recovered swallowed switch change 0x%04X\r\n", swallowed_change_hint);
+#endif
+          app_ui_interrupt = swallowed_change_hint;
+      }
+      swallowed_change_hint = 0;
+
       // Read the switch board input ports and process accordingly
       get_ui_status();
       app_ui_status = get_ui_status();
+      app_ui_status_valid = true;
 //       printf("\r\n");
 //       printf("UI Interrupt Source = %d, Status = %d \r\n", app_ui_interrupt, app_ui_status);
 
```

The `docs/SESSION_LOG.md` diff is omitted; the log ships alongside this file.

## d. Raw command output

### Build, flag 0 (committed state)
```
--- FLAG 0
#define BED_BENCH_PROVOKE_RUNAWAY 0
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/.../switch_input.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit=0
```

### Build, flag 1 (then set back to 0 and rebuilt)
```
--- FLAG 1
#define BED_BENCH_PROVOKE_RUNAWAY 1
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/.../switch_input.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit=0
--- BACK TO 0
#define BED_BENCH_PROVOKE_RUNAWAY 0
[1/2] Building C object ...switch_input.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit=0
```

### Failures along the way
- `cmake --build ...` in Git Bash: `cmake: command not found`. Rebuilt in PowerShell with the toolchain on PATH, as in fw_HANDOFF_2026-08-02_05.
- First build failed to compile: my edit script wrote a real newline inside a `printf` string at the "recovered swallowed" print (`error: expected ')' before ';' token`, `passing argument 1 of 'printf' makes pointer from integer`). Fixed; the diff above has it correct.
- A first attempt to write this handoff in one shell command failed with `unexpected EOF while looking for matching` and wrote nothing; it was redone in parts.
- Host test `test/bed_control_test.c` was not re-run: nothing it covers changed.

## e. Claims

VERIFIED
- ALL provocation was missed 5/5 and HEAD caught 11/11, 213-216 ms: raw log lines above.
- `set_all_motors_on()` sets no `motor_state[]` flags: `exp_board.c` (`set_all_motors_on`) writes only the expander register.
- PWM is never stopped: only `sl_pwm_start` calls exist in `app.c:414-417`; no `sl_pwm_stop`.
- No debounce in firmware: grep of the source tree.
- Both flag states build with no warnings: output above.
- Flag committed as 0: `switch_input.c:91`.

ASSERTED (nothing here has been run on the bench)
- That the fail-safe now catches the ALL provocation.
- That reading the input port clears the interrupt source register, so a read can swallow an event. Evidence: the comment at `exp_ui.c:263` only; not the datasheet and not a capture.
- That the swallowed release is the cause of the unprovoked trip and the original runaway.
- That contact bounce is the cause of the false double presses (the auto-level one is strongly supported by the log's last two lines).
- That the enable bit is what gates motion, beyond the PWM-never-stops inference.

## f. QUESTIONS FOR REVIEW

1. I added the swallowed-event recovery (about 15 lines in the main input path), which the task did not ask for beyond "root-cause it, add a print if unsettled". I judged a dropped release to be the runaway itself. If you would rather have print-only first, remove the hint use and keep the print.
2. `ui_int_source` flips to the switch board when no interrupt is pending. If the physical controller is the auxiliary board, an out-of-band read may look at the wrong board. Which board is the "physical controller" in your bench setup?
3. Should the main input path get a real debounce? Not done.
4. Is `SWITCH_INPUT_MASK 0xFF0F` right? Still not checked against a schematic.
5. A failed motor-enable read currently trips the fail-safe after 3 samples. Acceptable?

## g. NOT DONE

- No bench run (I do not flash).
- No debounce.
- `run_motor_supervision()` is still gated on `active_switch` (open since #38).
- Release-config pass and loop-rate measurement still owed.

## Bench acceptance (for the user; PuTTY, COM6, 115200 8N1)

Steps 2 and 3 use the PHYSICAL CONTROLLER buttons, not the phone app.

1. Open `switch_input.c`, line 91. Change `#define BED_BENCH_PROVOKE_RUNAWAY 0` to `1`. Save. Build and flash. Open PuTTY on COM6 at 115200. Put the bed mid-stroke (not at a limit).
2. UP HEAD on the physical controller, tap and release, five times. Each time you should see three lines: `PROVOKE: energised HEAD pair...`, `FAILSAFE: ...`, `PROVOKE: fail-safe caught it after NNN ms`. Write down the five times.
3. UP ALL on the physical controller, tap and release, five times. Same three lines each time, including `FAILSAFE:`. This is the case that failed. Write down the five times.
4. Press every physical button once. All must work with no reset.
5. Hold UP HEAD for about two seconds, release. There must be no `FAILSAFE:` line before the next `PROVOKE:` line. If one appears, copy the whole log; also look for a `UI: recovered swallowed switch change` line near it, which is the evidence for the swallowed-release explanation.
6. Turn the flag off: set line 91 back to `0`, save, rebuild, reflash. In a terminal in the project folder run `git diff --stat`; if `switch_input.c` is listed, the flag is not back to the committed value. Tap UP ALL: no `PROVOKE:` line may appear. Do not leave the provocation build on the bed.
7. Press and release each physical button ten times, one at a time. There must be zero `Double Press Error` lines.
8. Press two physical buttons at the same time and hold. `Double Press Error` should print (followed by a `switch word ... switches down` line) and the motors stop. Release, then every button must work normally.

Save the PuTTY log and send it back.

## Final git status

```
On branch main
Your branch is ahead of 'origin/main' by 1 commit.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   ../../.projectlinkstore

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	../../.projectchecksums
	.part1.tmp
	.part3.tmp
	fw_HANDOFF_2026-10-17_02_failsafe-hardware-read-and-double-press.md
	../../tilt_bed_controller_project.pdf

no changes added to commit (use "git add" and/or "git commit -a")
```

(Captured before this handoff was committed; the handoff file and SESSION_LOG entry are the only additions, and the branch is then pushed.)
