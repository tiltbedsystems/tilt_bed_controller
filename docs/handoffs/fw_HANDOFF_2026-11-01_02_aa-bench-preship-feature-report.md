# fw_HANDOFF_2026-11-01_02 — Prompt AC: AA bench record, PRE_SHIP.md, code report for the next features

Firmware repo session of 2026-11-01 (system date `Sun Nov  1 2026`, checked
with `date` at the start, 20:58 PST, and again before writing this file,
21:12 PST). Prompt: `docs/prompts/PROMPT_AC_feature-groundwork-report.md`.

## a. Header

| | |
|---|---|
| Repo | `tilt_bed_controller` (firmware) |
| Branch | `main` |
| HEAD before | `6796e6b` (Handoff 2026-11-01 01: fix one section cross-reference), level with `origin/main` after `git fetch` |
| HEAD after the work | `fad70fc` (commit A, docs only) |
| After that | this handoff and the session's SESSION_LOG entry, committed together, then `git push` |
| Source changes | **none** (the prompt forbids them) |
| Builds | **none** (not needed; B9 sizes an existing build output, see B9) |

`git status` at the end of the work (after commit A, before this file was
written):

```
On branch main
Your branch is ahead of 'origin/main' by 1 commit.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
```

`.projectlinkstore` is the Studio file the prompt says never to commit; it is
not committed.

**What was asked and what was done**

| Part | Asked | Done |
|---|---|---|
| A1 | Record the Prompt AA bench result as VERIFIED, after checking each point against the log with line numbers | Done (DECISIONS #45 "Bench result", REGRESSION 6.9 ticked, SESSION_LOG record entry). **One point did not match:** the log has 23 hold-end lines, not 22 (12 `op=0x01` + 11 `op=0x03`). Recorded as 23; QUESTIONS 1 |
| A2 | Create `docs/PRE_SHIP.md` | Done: 18 firmware, 8 app, 3 hardware/production, 2 business/legal rows, plus a "recorded but not yet marked pre-ship" list |
| A3 | Commit A; show PRE_SHIP.md and the other diffs here | `fad70fc`; section c |
| B1-B9 | Report only, file:line for every claim | Section b, in order |

## b. Analysis

Every claim is marked **VERIFIED** (read in code, config, a doc, or a log;
the reference is given) or **ASSUMED**. File:line references are to
`6796e6b` (no source file changed in this session). The raw output behind the
greps is in section d.

Vocabulary used throughout, for a reader without the code open:

- **Output 0-3** = the four motor driver channels on the power board. The
  firmware calls them `MOTORA`-`MOTORD` (`dual_motor_control.c:35-38`:
  `MOTORA = OUTPUT0` ... `MOTORD = OUTPUT3`). VERIFIED.
- **A pass** = one trip round the main loop (`app_process_action()`). Several
  safety constants are counted in passes, not milliseconds (DECISIONS #29).
- **Supervision** = `run_motor_supervision()` (`motor_current_functions.c:380-411`):
  one current scan plus the limit, max-load and collision checks.

---

### B1. Collision detection

#### B1(a) How a collision trip is decided today

**What is measured.** Each motor's current individually; there is no
total-current measurement. VERIFIED:

- IADC scan of four single-ended inputs, PA5-PA8 = motor 0-3
  (`adc_currents.c:52-55`), 8x hardware averaging per conversion
  (`adc_currents.c:105`), IADC clocked from FSRCO with a 10 MHz ADC clock
  (`adc_currents.c:25-26, 89`).
- The ISR converts each result to mA as `raw + raw/2` (x1.5, 200 mV/A
  sense; `adc_currents.c:174-183`) into `scanResult[]`.

**When it is sampled.** Once per pass while supervision runs, triggered in
software, not on a timer. VERIFIED:

- `scan_all_currents()` starts a scan, then **blocks 1 ms**
  (`sl_sleeptimer_delay_millisecond(1)`, `motor_current_functions.c:133`),
  then copies the four results (`:136`).
- Supervision runs (i) for any switch- or app-driven move while
  `active_switch != OFF` and not auto-level, settings mode or a collision
  (`switch_input.c:921-926`), and (ii) in auto-level only inside
  `level_axis()` when not pulsing (`auto_level.c:770-771`).
- The sampling **rate** is therefore the loop rate. Measured only during
  auto-level: ~630 passes/s (1.587 ms/pass) or ~433 passes/s (2.31 ms/pass)
  depending on accel I2C load (DECISIONS #29, `docs/DECISIONS.md:648`). The
  manual-move loop rate has never been measured (DECISIONS #29 caveat,
  `:676-688`). VERIFIED (as recorded).

**Signal chain per motor, per pass** (`motor_current_functions.c:153-176`).
VERIFIED:

1. The first `NUM_INITIAL_CURRENTS_IGNORED` = **100 passes** of every move are
   discarded (`:28`, `:155-158`): nothing below is updated, so
   `filtered_current[]` stays 0.
2. Low-pass filter: `filtered = 0.05*raw + 0.95*filtered` (`alpha`, `:83`,
   `:162`).
3. Moving average of the filtered value over `NUM_CURRENT_READINGS` =
   **200 passes** (`:27`, `:166-171`), into `average_current_single_reading[]`.

**How the baseline is formed** (`set_dynamic_collosion_threshold()`,
`:178-232`). VERIFIED:

- For the first `NUM_CURRENTS_READ_DYNAMIC_THRESHOLD` = 350 passes after the
  ignore window (`:29`, `:182-188`), each motor's **peak** moving average is
  kept in `dynamic_max_average_calibration_current[]`. The moving average is
  still filling from zero for the first 200 of those passes, so the peak is
  in practice reached late in the window.
- At the end of the window, for all four motors at once (`:190-195`):
  `max_current_threshold_set[i] = peak[i]` - **this is the baseline the trip
  test uses** - and `dynamic_collision_detection_active = true`. The
  `Max Current for Motor N: X` lines in every bench log are printed here
  (`:198`).
- So collision detection is **blind for the first ~450 supervision passes
  of every move** (100 ignored + ~350 calibrating). At the measured
  auto-level rate that is roughly 0.7-1.0 s (ASSUMED arithmetic from
  DECISIONS #29's figures; DECISIONS #29 already computed ~808 ms for the
  350-pass part alone).
- After activation, a "continuous" re-check is meant to lower the threshold
  if a weight is removed (`:206-231`). **As written it does not reach the
  trip test:** it only rewrites `dynamic_collision_current_threshold[]`
  (`:222`), which nothing reads except its own reset (`grep`, d: the array
  is written at `:192, :222, :242` and declared `extern` in
  `motor_current_functions.h:25`, and never read). `max_current_threshold_set[]`,
  the real baseline, is not changed. It also counts the window on motor 3's
  counter for every motor (`dynamic_reading_count[3]`, `:210`). VERIFIED
  from code; QUESTIONS 4.

**How the raise/lower sensitivity values enter** (`collision_check()`,
`:333-373`). VERIFIED:

- `security_margin` = the **lower** margin if `!is_raising`, else the
  **raise** margin (`:336-340`).
- `is_raising` is the move direction for switch and app moves
  (`bed_actions.c:45`, `:73`) and is forced **true for auto-level**
  (`bed_actions.c:170`; `level_axis()` passes `true`, `auto_level.c:771`),
  so auto-level always uses the raise margin, including for the two motors
  that are lowering. QUESTIONS 5.
- Margins are the NVM3 values `raise_collision_current_security_margin` /
  `lower_collision_current_security_margin` (`bed_settings.c:37-38`),
  loaded at boot (`Loaded Raise/Lower Collision Sensitivity` lines,
  `app.c:472-473`).

**The trip test** (`:342-352`). VERIFIED:

```
for each motor i (all four, on or not):
    delta[i] = moving_average[i] - baseline[i]          (:344)
    if delta[i] > 0: sum += delta[i]                    (:345-346)
if sum > security_margin: TRIP                          (:352)
```

So the trip is on the **sum of the positive rises across all four motors**,
compared with one margin. Motors that are off contribute their noise above
their own (near-zero) baseline.

**Where trips are declared** (all VERIFIED):

| Trip | Where | Prints (when `BED_CAPTURE_MOTOR_CURRENTS` is 0) |
|---|---|---|
| Summed rise > margin | `collision_check()`, `motor_current_functions.c:352-371` | `Tripped on steady!`, `Securit margin: N` (sic), then `Motor N Delta Current: X` for all four (`:357-362`) |
| Max load, raising motor | `max_load_check()`, `:296-311`: `filtered_current[motor] > 3000` (`MAX_LOAD_CURRENT`, `:25`) | `TRIPPED MAX LOAD`, `Motor N Filtered Current: X` (`:301-303`) |
| Max load, lowering motor | `:312-327`: `> 2500` | same (`:317-319`) |

Both trip types call `set_all_motors_off()`, set `any_collision = true`, and
reset the current data (`:305-310`, `:321-326`, `:365-370`). With capture
mode on, both print `#COLLISION,<ms>` instead (`:299`, `:315`, `:354`).
`app.c:580-619` then runs the collision sequence: `Once`, all LEDs blink at
500 ms (`COLLISION_BLINK_CYCLES` 3, `app.c:84`), reverse at blink 2
(`app.c:604-605`), stop at blink 3 (`:608-617`). VERIFIED.

#### B1(b) What exists in memory at the moment of a trip, and for how long

All per motor, `NUM_MOTORS` = 4 (`bed_types.h:12`). VERIFIED from
`motor_current_functions.c`:

| Variable | Line | Type | Contents | Lifetime after a trip |
|---|---|---|---|---|
| `latest_motor_current[4]` | `:59` | u32 | raw mA from the last scan | until the next scan (next supervision pass, i.e. the next move) |
| `filtered_current[4]` | `:81` | u32 | LPF output (also sent in Bed Status) | **zeroed in the same call** (`reset_motor_current_data()`, `:246`) |
| `readings[4][200]` | `:60` | u16 | the 200-pass history of the LPF value | **zeroed in the same call** (`:249-251`) |
| `average_current_single_reading[4]` | `:62` | u16 | moving average now | **zeroed in the same call** (`:244`) |
| `max_current_threshold_set[4]` | `:91` | u16 | the baseline (peak during calibration) | **zeroed in the same call** (`:247`) |
| `average_current_delta_single[4]` | `:69` | i32 | each motor's rise above its baseline - computed in the statement that decides the trip (`:344`) | **survives**: not in the reset function; overwritten only when `collision_check()` next runs with detection active, ~450 passes into the next move |
| `security_margin` | `:102` | i16 | margin used | survives (static) |
| `dynamic_max_average_calibration_current[4]` | `:90` | u16 | re-accumulating peak (continuous phase) | zeroed in the same call (`:241`) |

The per-motor deltas are also printed at the trip (`:360-362`), so the UART
log already has each motor's share. For a max-load trip only the offending
motor's filtered current is printed.

#### B1(c) Could the firmware report each motor's share of the rise at trip time, without blocking and without new I2C?

**Yes, from data already computed.** `average_current_delta_single[]`
(per-motor rise) and `security_margin` exist at the trip and survive the
reset (B1(b)). The baseline and moving average are zeroed in the same call,
so a snapshot has to be taken **before** `reset_motor_current_data()`
(`:370`, and `:310`/`:326` for max load). Taking a snapshot is a copy of
~30 bytes in RAM: no I2C, no blocking, no NVM3. VERIFIED (code);
ASSUMED (cost estimate).

"Share" would be `delta[i] / sum(positive deltas)`; the app can compute it
from the raw deltas, so the firmware need only send the deltas.

**Snapshot contents, options by size** (ASSUMED designs, for the reviewer):

| Variant | Fields | Bytes |
|---|---|---|
| Minimal | trip type (steady / max-load raise / max-load lower) u8, 4 x delta i16, margin i16 | 11 |
| Full | + command/target/direction u8, 4 x baseline u16, 4 x moving average u16 | 28 |

**Where it could be sent** (costs ASSUMED from the code structure):

1. **Bed Status (0003).** It is exactly 20 bytes (`gattdb_bed_status_len`,
   `autogen/gatt_db.h:44`) with only flag bits 4-7 free
   (`ble_bed_service.c:80-85`). The only way to carry the deltas without a
   GATT change is to **overload** the four `current_ma` fields (offsets
   9-10, 12-13, 15-16, 18-19; `ble_bed_service.c:112-118`) with the deltas
   while flag bit 0 (`any_collision`) is set. Today those fields read 0 at
   that moment anyway, because `filtered_current[]` was just zeroed. Cost:
   small firmware change, a protocol version bump (BLE_GATT_DESIGN §6
   "Protocol version and this change"), app change; the meaning of a field
   changes with a flag, and the data is visible only during the ~1.5 s
   collision sequence. A notification is guaranteed at least once because
   the collision bit is a "state" change (`status_flags_changed()`,
   `ble_bed_service.c:977-988`). Works for switch-driven collisions too,
   whenever a phone is subscribed.
2. **Command Result (0002).** 2 bytes `{seq, result}`
   (`gattdb_command_result_len`, `autogen/gatt_db.h:43`). Carrying data
   needs a longer value (GATT change) and a new result code. It only fits
   app-driven moves: a switch-driven collision has no command to answer.
   Note: **today a collision during an app hold sends no Command Result at
   all** - the watchdog is cancelled silently (`ble_bed_service.c:578-585`).
   A new result code alone (e.g. `COLLISION_STOP`, no payload) would already
   tell the app "the bed stopped for a collision" for app moves.
3. **A new characteristic** ("Last Trip", Read + Notify, e.g. UUID
   `54b10007`). Holds the full 28-byte snapshot if the MTU allows, or a
   20-byte cut (type, command detail, margin, 4 x delta, 4 x baseline = 20)
   at the default MTU. Readable after the fact, so it is not lost if no
   phone was subscribed. Cost: a GATT configurator change (Studio, by
   Andrew - `config/` and `autogen/` are not hand-edited), firmware, a
   protocol version bump, app work. Persisting it to NVM3 would mean a flash
   write; if wanted, do it later from the main loop, never in the trip path.
4. **UART only** - today's behaviour.

#### B1(d) Which motors carry current for each move type

From the pairing code (`dual_motor_control.c:347-428`) and the bench log of
this session (`prompt_aa_bench.log`). VERIFIED (code); the log lines show
which outputs calibrated high, consistent with the code (the log does not
record which target the app sent, so the log column is supporting evidence
only):

| Move | Outputs energised | Code | Log example (Max Current lines) |
|---|---|---|---|
| HEAD up / down | 0 (A), 3 (D) | `:351-369` | lines 20-23: 749, 24, 17, 755 |
| LEFT up / down | 0 (A), 1 (B) | `:370-388` | lines 210-213: 754, 797, 10, 17 |
| RIGHT up / down | 2 (C), 3 (D) | `:389-407` | lines 127-130: 5, 22, 1122, 713 |
| FOOT up / down | 1 (B), 2 (C) | `:408-426` | lines 104-107: 2, 809, 1120, 16 |
| ALL up / down | all four, one expander write (`set_all_motors_on()`, `exp_board.c:138-156`) | `bed_actions.c:264-266`, `switch_input.c:521-555` | (not in this log) |
| Auto-level ROLL | all four: LEFT pair and RIGHT pair, **opposite directions** | `auto_level.c:654-663`, `:758-761` | lines 135-138: 693, 717, 1170, 712 |
| Auto-level PITCH | all four: HEAD pair and FOOT pair, opposite directions | `auto_level.c:666-676`, `:762-765` | lines 33-36: 705, 771, 1130, 721 |
| Auto-level pulses ("sneak") | same pairs as the axis, 75 ms on / 350 ms off | `auto_level.c:66-67`, `:703-723` | `Pulse on...` lines |

What each move can sense (VERIFIED from code unless marked):

- Every move measures all four outputs, but in a single-side move only two
  carry load, so the trip sum is two real rises plus two motors' noise.
- **Auto-level's pulsed phase runs with no current supervision at all**: no
  collision test, no max-load test, no limit inference.
  `level_axis_pulse()` (`auto_level.c:679-738`) never calls
  `run_motor_supervision()`; `level_axis()` calls it only when
  `!pulse_latch` (`:770-771`); and the switch-path supervision is gated off
  during auto-level (`switch_input.c:921`). Once `Latch` prints
  (`auto_level.c:813-818`), supervision stops until the axis finishes.
  QUESTIONS 3.
- During the collision reverse (blink 2 to blink 3, ~500 ms) supervision is
  gated off by `any_collision` (`switch_input.c:921`).
- Observation, cause not established: in hold 9 of this bench, PITCH
  levelled with outputs 0 and 3 near zero (lines 201-204: 1, 777, 1172, 12),
  i.e. the HEAD pair apparently did not run while FOOT did.
  `set_dual_motors_on()` refuses a pair when either motor carries an
  inferred limit flag in the requested direction (`dual_motor_control.c:353,
  361`). Whether that is what happened is ASSUMED/unknown. QUESTIONS 6.

---

### B2. Level reference ("set level")

**How it is captured.** VERIFIED from `settings_mode.c`:

1. Enter settings mode: hold BED LIGHTS 5 s (B3).
2. Press and hold AUTO LEVEL. On the press, `active_switch =
   SWITCH_AUTO_LEVEL_VALUE`, `zeroLevelFirstPass = true`,
   `resetZeroLevelFirstPass = true` (`settings_mode.c:212-218`).
3. While held, every pass: a 2 s one-shot timer starts (prints `2 second
   timer start`, red LED on, `:306-318`) and `accel_running_average()` runs
   every pass while `zeroLevelFirstPass` (`:320-322`).
4. At 2 s: `Writing values to NVM3...`, then
   `bed_settings_set(BED_SETTING_BASELINE_ROLL_ANGLE, returnAccelValue(ROLL))`
   and the same for PITCH (`:336-367`). Green LED on if both succeed, with
   `Stored ROLL value is: N` / `Stored PITCH value is: N` (`:370-376`);
   red fast-blink if either fails (`:378-380`).
5. Keep holding a further 5 s: both keys are **erased** (`Erased Angles`,
   blue LED; `:327-332`, `:384-395`).
6. Release at any time cancels the timers (`:219-226`); STOP from the app
   does the same and stays in settings mode (DECISIONS #44).

**The averaging used.** `returnAccelValue()` returns
`accel_average_roll_angle` / `accel_average_pitch_angle`
(`auto_level.c:487-497`): the auto-level filter chain's 130-pass moving
average of a 0.1-alpha LPF of the angle (`NUM_ACCEL_READINGS` 130,
`auto_level.c:60`; `accel_alpha` 0.1, `:94`; `accel_filter_update()`,
`:307-331`), **without** the stored baseline subtracted. The accelerometer is
read at most every 10 ms (`ACCEL_I2C_MIN_INTERVAL_MS`, `:36`; MC3479 at
100 Hz, `mc3479.c:352`). The filter is not reset at the start of the hold;
over 2 s the 130-slot buffer is refilled many times (ASSUMED: the loop stays
awake while the button is held because the switch sampler is active,
`app.c:520-522`, so it runs hundreds of passes per second).

**What is stored.** VERIFIED:

| NVM3 key | Define | Variable | Units | Range | Default |
|---|---|---|---|---|---|
| 1 | `CUSTOM_ROLL_ANGLE_KEY` (`nvm3_functions.h:16`) | `baseline_roll_angle` | degrees x100 (`SCALE_FACTOR` 100, `bed_types.h:14`), int16 | -1500..1500 | 0 |
| 2 | `CUSTOM_PITCH_ANGLE_KEY` (`:17`) | `baseline_pitch_angle` | same, **internal sign convention** (head raised = negative, `auto_level.c:264-282`) | -1500..1500 | 0 |

(`bed_settings.c:35-36`; each value is one 2-byte NVM3 object,
`nvm3_functions.c:13-16`.) The boot lines `Loaded Roll Angle: N` /
`Loaded Pitch Angle: N` print these two variables after `bed_settings_init()`
(`app.c:466-471`); `0` / `0` in this session's log (lines 7-8) means either
nothing stored or zero stored. A stored value outside the range is rejected
at boot with a `Rejected stored ...` line and the default used
(`bed_settings.c:47-58`). At run time, every angle auto-level uses is
`average - baseline` (`auto_level.c:329-330`).

**Limits on when it may run.** VERIFIED:

- Only in settings mode, only from the physical panel. The app has no
  opcode for it (BLE_GATT_DESIGN §4; `ble_bed_service.c:161-168`) and every
  motion opcode is refused in settings mode (`ble_bed_service.c:731-734`).
- No motion can happen meanwhile: in settings mode the panel's motion
  branch is skipped (`switch_input.c:517`) and supervision is off
  (`switch_input.c:921`).
- No check that the bed is still, and no check of the value beyond the
  +/-15.00 degree range. A range failure prints `Rejected baseline ... out
  of range` and `Failed to store ...` (`bed_settings.c:69-74`;
  `settings_mode.c:346-350`).
- **Erase takes effect only after a reboot.** `bed_settings_erase()`
  deletes the NVM3 object but leaves the live variable unchanged
  (`bed_settings.c:89-93`; documented at `bed_settings.h:39-40`). Until the
  next boot auto-level keeps using the erased baseline. QUESTIONS 10.

---

### B3. Settings mode

**Entry.** VERIFIED:

- Physical only: hold BED LIGHTS 5 s. The press itself is a normal lights
  command: `bed_lights_set(BED_SOURCE_SWITCHES, !app_lights_on())`
  (`switch_input.c:665-670`) - **so entering settings mode toggles the
  under-bed lights** - and `app_lights_set()` sets `active_switch =
  SWITCH_BED_LIGHT_VALUE` (`bed_actions.c:197-202`).
- `settings_mode_tick()` starts a 5 s timer while `switch_pressed`,
  `active_switch == SWITCH_BED_LIGHT_VALUE`, not already in settings mode,
  the sampler state fresh, and BED LIGHTS physically down in the sampler's
  debounced state (`settings_mode.c:277-290`). On expiry `in_settings_mode
  = true` (`:285-289`). Entry prints nothing.
- Release before 5 s: `settings_mode_cancel_entry()` (`switch_input.c:671-675`).
- The app cannot enter: its LIGHTS_SET returns the bed to idle at once
  (`app_apply_lights_outputs()` -> `app_release_active_switch()`,
  `bed_actions.c:283-293`), and the entry test needs BED LIGHTS physically
  down.

**Exit.** VERIFIED: a **new** BED LIGHTS press while in settings mode
(`settings_mode.c:248-256` -> `settings_mode_exit()`, `:132-163`: prints
`Settings mode: exit`, cancels holds, motors off, LEDs restored, panel state
cleared, switch source released), or a reset (`in_settings_mode` starts
false, `:55`). No timeout. STOP_ALL does **not** exit (`bed_actions.c:221-228`).

**Every setting adjustable today.** VERIFIED: two.

1. **Collision sensitivity** - UP ALL held 2 s = one rung more sensitive,
   DOWN ALL held 2 s = one rung less (`settings_mode.c:404-449`). Ladder
   (`settings_mode.c:29-50`):

   | Rung | Raise margin (mA) | Lower margin (mA) | LEDs shown |
   |---|---|---|---|
   | HIGH (most sensitive) | 350 | 250 | green |
   | MED | 550 | 275 | blue |
   | LOW | 750 | 300 | red |
   | OFF | 10000 | 750 | green + blue + red |

   A stored pair that matches no rung snaps to HIGH (`:418-419`, `:441-442`).
   Both keys are written together (`:450-492`). Holding longer than 4 s
   steps again every 2 s: after the write the timer is stopped
   (`:490-491`) and restarted on the next pass while the button is still
   held (`:408-409`). ASSERTED from code (DECISIONS #44's bench record shows
   single steps from released holds only).
2. **Level reference** store/erase (B2).

**Storage: the `bed_settings.c` key table** (`bed_settings.c:34-39`).
VERIFIED. Every setter range-checks and **rejects** (does not clamp)
(`bed_settings.c:65-87`):

| Id | NVM3 key | Default | Min | Max | Live variable |
|---|---|---|---|---|---|
| `BED_SETTING_BASELINE_ROLL_ANGLE` | 1 | 0 | -1500 | 1500 | `baseline_roll_angle` |
| `BED_SETTING_BASELINE_PITCH_ANGLE` | 2 | 0 | -1500 | 1500 | `baseline_pitch_angle` |
| `BED_SETTING_RAISE_COLLISION_MARGIN` | 3 | 350 | 150 | 10000 | `raise_collision_current_security_margin` |
| `BED_SETTING_LOWER_COLLISION_MARGIN` | 4 | 250 | 100 | 750 | `lower_collision_current_security_margin` |

(Defaults 350/250 = the HIGH rung. This bed has 550/275 = MED stored, log
lines 9-10.)

**What is blocked in settings mode.** VERIFIED:

| Path | Behaviour | Where |
|---|---|---|
| Physical motion buttons | Never reach `bed_control`: `process_inputs()` takes the settings branch, which only calls `settings_mode_process_outputs()`; the normal motion branch of `process_outputs()` is skipped | `switch_input.c:600`, `:717-720`, `:517`, `:592-594` |
| Current supervision, double-press check | Off | `switch_input.c:921` |
| Motion fail-safe | Skipped (settings mode counts as "commanding") | `switch_input.c:806` |
| App MOVE_START/STOP, AUTO_LEVEL_START/STOP, LIGHTS_SET | `REJECTED_SETTINGS_MODE` (0x03), before the opcode switch | `ble_bed_service.c:731-734` |
| App STOP_ALL | Accepted from any role; cancels any settings hold; stays in settings mode | `ble_bed_service.c:661-675`; `bed_actions.c:221-228` |
| App REFRESH_TILT | Accepted | `ble_bed_service.c:712-720` |
| Command arbitration | `app_bed_is_idle()` is false in settings mode (`bed_actions.c:360-363`), so `arbitrate()` never releases the source lock (`bed_control.c:79-95`); the switches hold it from the entry press until `settings_mode_exit()` releases it (`settings_mode.c:162`) | as cited |

**Exactly where the pairing window is tied to settings mode.** One line,
VERIFIED: `uint8_t confirm = app_in_settings_mode() ? 1 : 0;` in the
`sl_bt_evt_sm_confirm_bonding` handler (`ble_bed_service.c:1240`), with
`app_in_settings_mode()` returning `in_settings_mode`
(`bed_actions.c:370-373`). A refused pairing is marked and closed on the
following `bonding_failed` (`:1248-1251`, `:1211-1217`). The stack is
bondable at all times (`:1107`). Nothing else ties the two.

**Options for an app-entered settings mode that changes settings but does
NOT open the pairing window** (the user's decision: only a physically
entered settings mode may accept new phones). ASSUMED designs and costs:

1. **No "mode" for the app at all - the Settings RPC (BLE_GATT_DESIGN §9).**
   SET/ERASE accepted only from the CONTROL connection and only while the
   bed is idle; `in_settings_mode` is never touched, so the pairing window
   cannot open. Cost: characteristic 0005 added in the GATT configurator
   (Andrew, Studio), the RPC handler (main-loop context, like every other
   write), app UI, protocol version bump. Covers sensitivity directly. A
   level-reference capture needs one more operation (e.g. a "capture level
   now" op that runs the existing 2 s averaging from a non-switch trigger),
   because today the capture is welded to the AUTO LEVEL hold inside
   `settings_mode_tick()` (`settings_mode.c:305-396`).
2. **A second flag beside `in_settings_mode`** (e.g. "entered physically"),
   with the pairing test becoming `in_settings_mode && entered_physically`.
   Cost: small change at `ble_bed_service.c:1240`, plus a new app path to
   enter and leave. Risk: everything else keyed on `in_settings_mode`
   applies to the app-entered mode too - the fail-safe exclusion
   (`switch_input.c:806`), the motion refusals, the amber fast-blink
   (`settings_mode.c:294`), Bed Status flag bit 1 (`bed_status.c:144`), the
   telemetry gate (`app.c:759-760`), the source lock never releasing - and
   it needs exits the physical mode does not (control-phone disconnect, a
   timeout, a physical press).
3. **A separate app "settings session"** opened by new Command opcodes
   (enter/exit), which blocks motion like settings mode but is a different
   flag, with the actual changes made through option 1's RPC. Cost: both
   protocol additions; clearer separation than option 2.

Whatever the option, the physical gestures (2 s holds) stay physical; the
app would send values, not emulate button holds.

---

### B4. Settings RPC (BLE_GATT_DESIGN §9, characteristic 0005)

- **Nothing is implemented.** No source file references a settings RPC,
  `54b10005` or a `gattdb_settings...` symbol (`grep` over `*.c *.h *.btconf
  *.json` outside `docs/`: no match, d). VERIFIED.
- **0005 is not in the GATT database.** The Tilt Bed Service in
  `config/btconf/gatt_configuration.btconf` has `54b10001` Command,
  `54b10002` Command Result, `54b10003` Bed Status, `54b10004` Client Role
  and `54b10006` Protocol Version (lines 79-121), and `autogen/gatt_db.h`
  defines handles only for those (`gattdb_command` 27 ... 
  `gattdb_protocol_version` 38, `:23-28`). VERIFIED.
- **The app's subscription to it today:** the app code is not in this repo,
  so what it does is not visible here. From the firmware side there is no
  0005 attribute to discover, so a subscription cannot succeed and the
  firmware sends nothing for it; it has no effect on the bed. ASSUMED
  (firmware side reasoned from the GATT database; app side not read).
  Recorded as PRE_SHIP A6.

---

### B5. Lights

**What LIGHTS_SET drives.** VERIFIED:

- App: opcode 0x05, arg0 0/1 (`ble_bed_service.c:846-857`) ->
  `bed_lights_set(BED_SOURCE_APP, on)` (`bed_control.c:156-164`) ->
  `app_lights_set()` stores `bedlights_on` (`bed_actions.c:197-202`) ->
  `app_apply_lights_outputs()` (`bed_actions.c:283-293`).
- Outputs: the on-board expander's **P1_6 `EN_LIGHTING`** (the under-bed
  lighting load switch; `set_underbed_lighting_on/off()`,
  `exp_board.c:373-411`; pin table `:470-477`) and the **amber LED** on the
  UI boards (`set_LED_on/off(LED_AMBER)`).
- Physical BED LIGHTS does the same on its press (`switch_input.c:557-570`,
  `:665-670`).
- Lights are not motion: never inhibited by the two-source latch, but a
  lights command from the source that does not hold the bed force-stops the
  bed and returns false (`bed_control.c:153-164`), which the app sees as
  `FORCE_STOPPED_OTHER_SOURCE` (`ble_bed_service.c:851-853`).
- At boot the lights are off: P1 outputs are written 0
  (`exp_board.c:495-497`) and `bedlights_on` starts false
  (`bed_actions.c:37`). The state is not stored in NVM3.

**Is the state reported in Bed Status?** Yes. VERIFIED:
`out->lights_on = app_lights_on()` (`bed_status.c:145`) -> flag **bit 2**
(`ble_bed_service.c:83`). A change of bit 2 is a "state" change and
triggers a notification (`ble_bed_service.c:979`). So the app's Lights
button can show on/off from bit 2. Caveat: it is the **commanded** state,
not a read-back of P1_6; a failed expander write would not show (the write
has only an `EFM_ASSERT`, `exp_board.c:390`). ASSUMED consequence.

**Interaction with settings mode and the status LEDs.** VERIFIED from code;
none of this is bench-observed:

- Entering settings mode toggles the lights (B3).
- In settings mode the app's LIGHTS_SET is refused (`ble_bed_service.c:731-734`);
  a physical BED LIGHTS press exits settings mode instead of toggling
  (`settings_mode.c:248-256`).
- In settings mode the amber LED fast-blinks as the settings indicator
  (`settings_mode.c:294`), so it does not show the lights; on exit it is
  restored to the lights state (`settings_mode.c:149-152`).
- After a collision sequence amber is restored if the lights are on
  (`app.c:612-614`); after an app MOVE_STOP / AUTO_LEVEL_STOP too
  (`bed_actions.c:304-306`); a two-source conflict leaves it alone
  (`bed_actions.c:343-349`).
- **But `app_stop_all()` turns amber off without touching the lighting
  output** (`set_all_LED_off()`, `bed_actions.c:219`), and nothing restores
  it. `app_stop_all()` runs on STOP_ALL, watchdog expiry and the
  control-phone disconnect stop (`ble_bed_service.c:670, 619, 1026`). After
  any of those, with the lights on, the amber indicator is off while the
  lights stay on (Bed Status bit 2 still says on). QUESTIONS 8.
- The BED LIGHTS hold runs current supervision with no motor on (PRE_SHIP
  F6).

---

### B6. Motor outputs to bed corners

**Mapping today.** VERIFIED (`dual_motor_control.c:35-38`, `:86-120`
diagram, `:347-428`):

| Side | Outputs (motor letters) |
|---|---|
| HEAD | 0 (A) + 3 (D) |
| LEFT | 0 (A) + 1 (B) |
| RIGHT | 2 (C) + 3 (D) |
| FOOT | 1 (B) + 2 (C) |

Corners: A = head-left (output 0), D = head-right (output 3), B = foot-left
(output 1), C = foot-right (output 2), per the diagram at
`dual_motor_control.c:86-99`. VERIFIED (diagram); the physical wiring is
ASSUMED to match it.

Pair table for every move (VERIFIED):

| Move | Pairs | Direction bits |
|---|---|---|
| HEAD/LEFT/RIGHT/FOOT up or down | that side's pair (table above) | both motors the same (`set_dual_motors_direction_up/down()`, `:464-530`) |
| ALL up / down | all four | all four the same (`exp_board.c:220-279`) |
| Auto-level ROLL, roll positive (LEFT high) | RIGHT up, LEFT down | `auto_level.c:655-658` |
| Auto-level ROLL, roll negative (RIGHT high) | LEFT up, RIGHT down | `:659-662` |
| Auto-level PITCH, pitch positive (FOOT high) | HEAD up, FOOT down | `:667-670` |
| Auto-level PITCH, pitch negative (HEAD high) | FOOT up, HEAD down | `:671-674` |
| Collision reverse | same pair(s), opposite direction (not in auto-level: no case for it) | `dual_motor_control.c:300-345` |

Direction is one expander bit per output: P0_4..P0_7 = `UP_DNn_MOTOR1..4`,
1 = up (`exp_board.c:199-218`, `:474-477`); enable P0_0..P0_3
(`:470-473`). VERIFIED.

**How auto-level chooses the pair.** Step 1 waits for a 1500 ms steady read
(`STEADY_ACCEL_READ_TIMER_TIME`, `auto_level.c:65`, `:785-802`) and marks an
axis disabled if it is already within 0.1 degree (`CLOSE_TOLERANCE`, `:62`,
`:798`). Then ROLL first unless disabled (`app.c:637-639`), using
`check_level()`'s sign (`auto_level.c:536-600`) to pick the direction above;
full-speed until within 0.3 degree (`PULSE_START_TOLERANCE`, `:63`), then
75/350 ms pulses until within 0.01 degree (`TOLERANCE`, `:61`); then a
1000 ms pause (`ACCEL_TIMER_DURATION`, `app.c:85`, `:693-713`) and the other
axis. VERIFIED.

**A comment that contradicts the code.** `auto_level.c:622-626` says
auto-level "USES HARDWIRED MOTORS 0 -> 3, NOT A -> D". The code drives sides
through `set_dual_motors_on(side)`, which resolves through `MOTORA`-`MOTORD`
(`auto_level.c:713-718`, `:758-765`; `dual_motor_control.c:347-428`). Today
the two are the same (`MOTORA = OUTPUT0` ...). If the letters were remapped,
auto-level **would** follow the remap. VERIFIED.

**Accelerometer axes and signs.** VERIFIED (code):

- `roll = atan2(x, |a|)` in degrees (`auto_level.c:283`);
  `pitch = atan2(y, z) - 90` (`:284`, `PITCH_OFFSET` 90 at `:43`); both x100
  as int16 (`:287-288`).
- The `-90` means pitch reads 0 when the y axis carries the full 1 g
  (atan2(1, 0) = 90 degrees). So the board is mounted with its y axis
  vertical, x left-right and z head-foot. VERIFIED as arithmetic; the
  physical mounting is ASSUMED from it.
- Internal convention (diagram `auto_level.c:630-648`): LEFT high = roll
  positive, RIGHT high = roll negative, HEAD high = pitch **negative**, FOOT
  high = pitch positive.
- The pitch-sign decision: kept negative-for-head-up internally because
  auto-level's direction logic and every stored baseline depend on it
  (`auto_level.c:264-282`); negated exactly once for the wire, POSITIVE = head
  raised (`bed_status.c:129-141`). Roll's wire sign (positive = LEFT raised)
  is marked "NOT yet bench-confirmed" (`bed_status.h:53-55`; BLE_GATT_DESIGN
  §6).
- Accelerometer: MC3479, 1000 Hz internal, decimate 10 = 100 Hz output, LPF
  ~80 Hz (`mc3479.c:339-355`), default 2 g range (comment `:341`).

**Orientation assumptions** ("logo toward the head"). VERIFIED:

- No code or doc mentions a logo (`grep -i logo`, d).
- The orientation assumption is the auto-level diagram's "POWER BOARD FACES
  THIS WAY", arrows toward HEAD (`auto_level.c:638-641`), and the axis
  formulas above. A board mounted any other way changes the signs (rotated
  180 degrees about the vertical: roll and the z-axis meaning flip) or
  swaps roll and pitch (rotated 90 degrees). ASSUMED geometry.
- SETPOINTS_INVENTORY.md:87 records `PITCH_OFFSET` as
  "Mounting-orientation dependent" (its line reference `auto_level.c:31` is
  stale; the define is now at `:43`).

**Every call site a runtime corner-mapping table (NVM3) would have to
touch.** VERIFIED (the sites); the "change needed" column is ASSUMED design:

| # | Site | What it assumes | Change needed |
|---|---|---|---|
| 1 | `MOTORA`-`MOTORD` (`dual_motor_control.c:35-38`) | letter -> output fixed at compile time; already plain variables, not `const` | load from NVM3 at boot, before any motion and before `switch_sampler_init()` can deliver a press (`app.c:411-414`, `466`) |
| 2 | `set_dual_motors_on/off()`, `set_dual_motors_direction_up/down()` (`dual_motor_control.c:347-530`) | side -> letters (HEAD = A+D, ...) | none if only the letter->output map changes |
| 3 | `limit_switch_motor_off()` (`:123-140`), `auto_level_limit_switch_motor_off()` (`:174-203`) | compare an output index against `MOTORx` | none (they already compare against the variables) |
| 4 | `set_all_motors_*()` (`exp_board.c:138-197, 220-279`) | mask of all four | none |
| 5 | Per-output arrays: `motor_state[]` (`dual_motor_control.c:26`), `at_limit_switch[]` (`:53`), the current arrays (`motor_current_functions.c:59-92`), IADC channel i = output i (`adc_currents.c:52-55`) | index = physical output | none for the logic; it stays physical |
| 6 | Bed Status `motors[0..3]` (`bed_status.c:85-92`; BLE_GATT_DESIGN §6: "indexed by PHYSICAL motor position ... the corner mapping arrives with the calibration wizard") | app labels A-D | the app needs the mapping: a new field/characteristic, or reorder at the wire (protocol change either way) |
| 7 | NVM3 table (`bed_settings.c:34-39`, keys `nvm3_functions.h:16-19`) | one int16 per key with min/max | new key(s); validation that the value is a permutation of 0-3, which min/max alone cannot express; keys must stay clear of the Bluetooth stack's own NVM3 objects in the same instance (ASSUMED: the stack uses its own key range) |
| 8 | `reverse_motor_direction_and_start()` (`dual_motor_control.c:300-345`), the provocation (`switch_input.c:501-505`) | sides | none |
| 9 | Debug prints `Max Current for Motor N` etc. (`motor_current_functions.c:198, 303, 361`) | output index | cosmetic only |
| 10 | Accelerometer axes (`auto_level.c:283-284`, `PITCH_OFFSET` `:43`) and auto-level direction (`:651-677`) | board orientation relative to the bed | **not fixed by a corner map**: a separate axis map (swap, signs, offset) is needed if boards are mounted differently (DECISIONS #7's wizard solves both) |
| 11 | Stored level baselines (keys 1-2) and the wire sign (`bed_status.c:141`) | the current axis convention | an axis-map change invalidates stored baselines (`auto_level.c:275-279`); needs a migration rule |

---

### B7. Timed moves

**Is there an existing way to run one motor (or output) for a fixed short
time?** No. VERIFIED:

- Every command path drives a **pair** or **all four**:
  `bed_target_t` is HEAD/LEFT/RIGHT/FOOT/ALL only (`bed_control.h:30-34`);
  `app_apply_move_outputs()` -> `set_dual_motors_on()` or
  `set_all_motors_on()` (`bed_actions.c:262-271`). `set_motor_on(int motor)`
  exists (`exp_board.c:116-136`) but is called only from inside
  `set_dual_motors_on()` (`grep`, d).
- The only fixed-duration energisations in the firmware:
  1. Auto-level pulses: **75 ms on** (`SNEAK_MOTOR_ON_TIME`), 350 ms off
     (`SNEAK_MOTOR_OFF_TIME`) (`auto_level.c:66-67`), sleeptimer one-shots
     (`:204-228`), all four motors (`:712-719`). **No current supervision
     during pulses** (B1(d)); the motion fail-safe is excluded during
     auto-level (`switch_input.c:806`); the app watchdog covers app holds.
  2. The collision reverse: on at blink 2, off at blink 3 of a 500 ms timer,
     ~500 ms, unsupervised (`app.c:600-610`).
  3. The ALL-move limit pause: all off for 1500 ms (`TEMP_MOTOR_OFF_TIME`,
     `dual_motor_control.c:18`, `:142-170`) - an off time, not an on time.

**Smallest practical run time given the loop and PWM.** ASSUMED estimates
from the cited constants:

| Limit | Value | Source |
|---|---|---|
| Timer resolution | sleeptimer on RTCC/LFRCO, ~30.5 us per tick at 32.768 kHz (ASSUMED frequency) | Prompt AA handoff C1 |
| Off-edge jitter | one main-loop pass: the timer callback sets a flag, the loop acts on it - ~1.6-2.3 ms per pass measured in auto-level | DECISIONS #29 (`docs/DECISIONS.md:648`); flag pattern `auto_level.c:214-221` |
| Expander write | each `set_motor_on/off()` = a 1+1-byte write-read plus a 2-byte write on a 100 kbit/s bus (`config/sl_i2cspm_on_board_config.h:44-49`, mode 0) - a few hundred us each; a pair = two calls, so the second motor starts later than the first | `exp_board.c:116-136`; ASSUMED timing |
| PWM | 24 kHz, 100% duty (`config/sl_pwm_init_motor1_config.h:44`; `app.c:416-421`): no effect on run time; at 100% the enable bit is the only gate (`exp_board.c:302-309`) | VERIFIED |
| Protection | **collision and max-load are blind for the first ~450 supervision passes** (B1(a)); limit inference needs 3 consecutive "not moving" passes (`motor_current_functions.c:283`) | VERIFIED (counts); ~0.7-1.0 s ASSUMED |

So: a fixed run as short as **~75 ms is already proven in use** (auto-level
pulses), but with no current protection. A run **with** collision and
max-load protection as designed needs to last longer than the ~450-pass
blind window (roughly 0.7-1.0 s at auto-level's loop rate; the manual-move
rate is unmeasured). Below that the only protections would be the dead-man
watchdog (app) and the motion fail-safe, if the new path is not excluded
from it. A separate note: `check_motor_filtered_current[]`, which drives
limit inference, is never reset between moves (declared
`motor_current_functions.c:82`, updated at `:146`, absent from
`reset_motor_current_data()` `:234-254`), so a short run starts with the
previous move's last value. VERIFIED (code); effect ASSUMED.

**Actuator speed constant recorded in the repo:** **none**. VERIFIED
(`grep -i 'mm/s|actuator speed|stroke|inches per'`, d). The only figure is an
unsourced "assumed actuator speed" giving ~1-3 mm of travel in 150-200 ms
(`docs/handoffs/fw_HANDOFF_2026-08-05_03_runaway-provocation.md:104-110,
471`). DECISIONS #15 (`docs/DECISIONS.md:215`) records that a future wizard
"collects actuator stroke".

---

### B8. Auto-level outcomes

Every way a run ends, its print, and what reaches the app. VERIFIED from code
except where marked:

| # | Outcome | Print(s) | Reaches the app? |
|---|---|---|---|
| 1 | Both axes levelled | `ROLL Leveled successfully...` / `PITCH Leveled successfully...` (`auto_level.c:831-835`), then `Exit auto-level` (`app.c:680`); green LED (`app.c:678`). **`Bed is level` never prints** - `bed_level` is only ever set false (`auto_level.c:166, 603`) and read at `app.c:681` | Not as an outcome. Motors' on bits go 0; Bed Status `command` stays AUTO_LEVEL while the hold continues (`bed_status.c:31-32`, `active_switch` stays `SWITCH_AUTO_LEVEL_VALUE` until release) |
| 2 | Both axes already within 0.1 degree at Step 1 | `Bed is disabled, no leveling required` (`app.c:641-650`); green LED | Same as 1 |
| 3 | ROLL disabled, PITCH levelled | `Exit auto-level`, `ROLL is disabled and Pitch is level` (`app.c:684-686`) | Same as 1 |
| 4 | PITCH disabled, ROLL levelled | `Exit auto-level`, `PITCH is disabled and ROLL is level` (`app.c:687-689`) | Same as 1 |
| 5 | Collision (summed rise or max load) | `Tripped on steady!` + deltas, or `TRIPPED MAX LOAD` (B1(a)); `Once` (`app.c:595`); `auto_level_active = false` (`app.c:590`); **no reverse** (no auto-level case, `dual_motor_control.c:342-343`) | Bed Status flag bit 0 during the ~1.5 s sequence (`bed_status.c:143`). **No Command Result**: the watchdog is cancelled silently (`ble_bed_service.c:578-585`). `app_auto_level_active` is **not** cleared there (only `app_move_active` is, `:583`), so the app's later repeats are accepted and re-arm the watchdog without restarting anything (`:816-831`). QUESTIONS 9 |
| 6 | Dead-man watchdog (app only) | `BLE: WATCHDOG STOP ...` block, only with `BLE_DEBUG_WATCHDOG_LOG` (`ble_bed_service.c:621-641`) | Command Result `WATCHDOG_STOP` 0x05 (`:620`) |
| 7 | User releases | Physical: `SW: 0x0002 up` (diag only), `bed_auto_level_stop()` (`switch_input.c:647-650`, `:537-542`). App: AUTO_LEVEL_STOP (`ble_bed_service.c:834-844`); `BLE: hold end op=0x03 ...` (diag) | App: ACCEPTED for its stop; Bed Status `command` -> NONE |
| 8 | STOP_ALL (either phone) or control-phone disconnect | none of its own (disconnect prints `connection_closed`) | STOP_ALL: ACCEPTED (`:674`) |
| 9 | The other source acts (two-source conflict) | none | App holding, wall switch pressed: next repeat gets `FORCE_STOPPED_OTHER_SOURCE` (`:746-750`), then `WATCHDOG_STOP` ~300 ms later (PRE_SHIP F18) |
| 10 | An inferred limit during auto-level | none; that pair is switched off (`dual_motor_control.c:246-247, 278-279, 174-203`) and the run carries on with it off - **no timeout, no "axis failed" outcome**; the run ends only by 1-9. The code marks this gap: `ADD CODE SUPERVISING MOTOR MOVEMENT, IF ALL MOTORS ARE NOT MOVING - THIS AXIS DID NOT LEVEL PROPERLY` (`auto_level.c:825-827`) | No |
| - | Timeouts | **none** overall. The only timers are the 1500 ms steady read, the 1000 ms axis pause and the pulse timers. `leveling_success[]` is set (`auto_level.c:837`) and reset (`bed_actions.c:164`) but never read (`grep`, d) | - |
| - | Double press / motion fail-safe | Not reachable: the double-press check is gated off during auto-level (`switch_input.c:921`), the fail-safe excludes it (`:806`) | - |

**Summary for the app:** today the app can tell only "auto-level command
active" (Bed Status `command` = 2), "collision in progress" (flag bit 0),
motor on/off bits, tilt, and the Command Result codes ACCEPTED /
FORCE_STOPPED_OTHER_SOURCE / WATCHDOG_STOP / REJECTED_*. It cannot tell
"levelled, waiting for release" from "still levelling", nor "axis disabled",
nor that a run ended in a collision (other than the flag) or stalled at a
limit. VERIFIED (Bed Status layout `ble_bed_service.c:76-120`; result codes
`:170-176`).

---

### B9. Headroom

**Source of the numbers.** No build was run. The existing build output
`cmake_gcc/build/base/tilt_bed_controller.out` (2026-11-01 15:38:34) was
sized with the project's own toolchain (`arm-none-eabi-size`, from
`DEFAULT_TOOLCHAIN_DIR`, `cmake_gcc/autogen_toolchain.cmake:28`). It
post-dates every source file's last modification (the newest,
`ble_bed_service.h`, 15:38:33), no source/config/autogen file changed
between `75b0a61` and `HEAD` (`git diff --stat` empty, d), and its text size
(244988) equals the Prompt AA record ("Text 244988 B", SESSION_LOG). So it
is the build of the current source. VERIFIED (the evidence); "it is exactly
HEAD's build" ASSUMED from that evidence.

**Device limits.** BGM220SC22HNA: flash 512 KiB (`FLASH_SIZE 0x00080000`),
page 8 KiB, RAM 32 KiB (`SRAM_SIZE 0x00008000`)
(`platform_core/platform/Device/SiliconLabs/BGM22/Include/bgm220sc22hna.h:279-283`
in the SDK). The linker gives the application `FLASH` = 0x0-0x7E000
(504 KiB; the last 8 KiB page is outside it) and `RAM` = 32 KiB
(`autogen/linkerfile.ld:30-37`). VERIFIED.

**Flash** (VERIFIED from `arm-none-eabi-size` and the map file, d):

| | Bytes | |
|---|---|---|
| Image (`.vectors` + `.text` + tables, to `__etext` 0x3BD08) + `.data` load image (768) | 245,768 | ends at 0x3C008 |
| NVM3 area (`.nvm`, `linker_nvm_begin` 0x74000 - `linker_nvm_end` 0x7E000) | 40,960 | = `NVM3_DEFAULT_NVM_SIZE` 40960 (`config/nvm3_default_config.h:38`), 5 pages |
| Free between image end and NVM3 | 229,368 | 0x3C008-0x74000 |
| Application flash used | 245,768 of 475,136 (to the NVM3 base) = **51.7%** | |

BLE_GATT_DESIGN §10's "~53 KB app in 504 KB flash" is out of date: the image
is now ~240 KiB. That matters for OTA: an AppLoader-style update needs no
storage slot, but any scheme that stages a second image in flash would need
about as much free space as the image, and 229 KiB is close to the image
size. ASSUMED (OTA layout not designed here). Also: the application is
linked at address 0x0 (`autogen/linkerfile.ld:32`); a Gecko bootloader, if
it sits at the start of flash as is usual on Series 2 parts (ASSUMED), would
need the application moved up, reducing this headroom by the bootloader's
size (PRE_SHIP F17; QUESTIONS 7).

**RAM** (VERIFIED from `arm-none-eabi-size -A`, d):

| Section | Bytes |
|---|---|
| `.stack` | 2,752 |
| `.bss` | 11,904 |
| `text_application_ram` (RAM functions) | 480 |
| `.data` | 768 |
| **Statically allocated** | **15,904 (48.5% of 32,768)** |
| `.memory_manager_heap` | 16,864 - the linker sizes it to fill the rest of RAM (`autogen/linkerfile.ld:275-287`; `__HeapBase` 0x20003E20 to `__HeapLimit` 0x20008000 in the map) |

The heap is where the Bluetooth stack and other SDK components allocate at
run time, so "free RAM" is the heap's unused part at run time, which no
build output can show. ASSUMED that it is the main consumer; a run-time
query of the memory manager would give the real figure (not done).

**NVM3** (VERIFIED): 40,960 B in the default instance, cache 200 objects,
max object 254 B (`config/nvm3_default_config.h:14, 21, 38`). The
application stores four 2-byte objects (keys 1-4). The Bluetooth bond store
(up to 8 bonds, `BLE_BED_SERVICE_MAX_BONDS`, `ble_bed_service.c:136`) shares
the default instance (ASSUMED from `ble_bed_service.c:63-67`'s comment).

---

### b.10 Part A - the bench record (what was checked, and how)

Every point in A1 was checked against `prompt_aa_bench.log` before anything
was written. The log with line numbers is in d.3; the counting commands in
d.1. Results:

| A1 point | Log evidence | Matches the prompt? |
|---|---|---|
| 11 app AUTO LEVEL holds (op=0x03 hold-end lines) | `grep -c 'hold end op=0x03'` = 11; lines 40, 61, 85, 103, 121, 151, 169, 187, 209, 227, 248 | yes |
| No `BLE: WATCHDOG STOP` | `grep -c WATCHDOG` = 0 | yes |
| Exactly one `Step 1` per hold | awk pairing: 1 `Step 1` before each of the 11 op=0x03 lines, and 1 after the last (the physical run, line 256) | yes |
| Every one of the 22 hold-end lines reads `longest between passes 0 ms` | `grep -c 'BLE: hold end'` = **23** (12 op=0x01, 11 op=0x03); `grep -vc 'longest between passes 0 ms'` over them = 0 | substance yes; **count no (23)** |
| Longest refresh gap per hold 119-181 ms | op=0x03: 121 148 150 150 150 150 150 150 151 179 181; op=0x01: 119 120 120 121 121 127 139 149 149 149 149 150 | 119-181 holds over all 23 lines; over the 11 AUTO LEVEL holds it is 121-181. Both recorded |
| Physical AUTO LEVEL (SW 0x0002, ~255-271) levelled, no hold-end line | `SW: 0x0002 down` 255, Step 1 256, `ROLL Leveled` 261, `PITCH Leveled` 269, `Exit auto-level` 270, `SW: 0x0002 up` 271; last hold-end line is 248 | yes |
| Latency 0, timeout 500 then 100 | lines 15, 17 (`timeout=500`), 19 (`timeout=100`), all `latency=0` | yes |

Also seen and recorded: "Bed is level" printed in none of the 12 runs
(`grep -c` = 0), explained by B8 row 1.

### b.11 Part A - PRE_SHIP.md design

The full file is in section c (it is new, so its diff is the file). Choices
made, for the reviewer:

- **Columns** exactly as asked (item, why, owner, how closed, source), with
  a row id (F1, A1, ...) so later prompts can name a row.
- **Owners:** Andrew = bench/hardware/production/business; Claude = the
  reviewer (decisions, prompts); Claude Code = code/doc changes. Where a
  decision precedes a change, the row says "Claude decides, Claude Code
  changes". Rows that re-open a user decision (F8, the pairing-window
  timeout) name Andrew too.
- **Closing rule:** rows are never deleted; a closed row gets `CLOSED
  <date>` and its evidence. Mirrors DECISIONS.md's append-only style.
- **Every item the prompt listed is present** (F1-F2, F4, F5-F18; A1-A7;
  L1-L2). F11 carries the prompt's exact marking "Test before shipping
  (Andrew)".
- **Added from the repo:** F3 (`BLE_DEBUG_SECURITY_LOG`, "Set to 0 for a
  quiet production log", `ble_bed_service.c:52`); F4 (the three bench flags
  that must stay 0, each with file:line); A8 (cross-reference to F18); H1
  and H2 (BLE_GATT_DESIGN §10: bootloader in production, GBL signing keys);
  H3 (Prompt AA handoff C1: whether the module has a 32 kHz crystal, which
  decides F11's options).
- **Bench diagnostic flags, complete list** (F1-F4; `grep` of every
  `#define *DEBUG*|DIAG*|BENCH*|CAPTURE*|...` in `*.c *.h`, d.1):
  `BLE_DEBUG_SECURITY_LOG 1` (`ble_bed_service.c:53`),
  `BLE_BENCH_WIPE_BONDS 0` (`:70`), `SWITCH_SAMPLER_DIAG 1`
  (`switch_input.c:57`), `BED_BENCH_PROVOKE_RUNAWAY 0` (`switch_input.c:98`),
  `BLE_DEBUG_WATCHDOG_LOG 1` (`ble_bed_service.h:36`),
  `BED_CAPTURE_MOTOR_CURRENTS 0` (`debug_capture.h:21`). (`WD_DIAG_GAPS`
  `ble_bed_service.c:253` and `PROVOKE_DELAY_MS` `switch_input.c:104` match
  the pattern but are sizes/timings inside those features, not flags.)
- **A separate "also recorded, not yet marked pre-ship" table** for items
  found in the repo that nobody has called pre-ship (auto-level no reverse
  after collision; interim motor-fault polling; the lower OFF rung; the
  pulse phase unsupervised; "Bed is level" never printing). Kept apart so
  the list does not claim more than has been decided. QUESTIONS 11.
- **App rows' sources** are "Chat 2026-11-01 (Prompt AC)": the prompt is
  the only record; the app repo has no list (per the prompt). A6 adds the
  firmware-side fact (no 0005 in the GATT database, B4).
- Not done: I did not open the app repo, so the app rows' "why" lines are
  the prompt's words, not checked against app code.

## c. Diffs (complete)

One commit with changes, `fad70fc` (commit A, docs only). The
`docs/SESSION_LOG.md` part of its diff is omitted under the standing
exception (the log ships with this file). Everything else in the commit is
below, untruncated: `git diff 6796e6b fad70fc -- . ':(exclude)docs/SESSION_LOG.md'`
(340 lines). `docs/PRE_SHIP.md` and the prompt file are new, so their diffs
are the whole files. No source file changed.

```diff
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 770e53d..cb3011d 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2199,3 +2199,56 @@ bench.** A bench-only `BLE: hold end ...` line (behind
 `BLE_DEBUG_WATCHDOG_LOG`) reports each hold's refresh count, longest refresh
 gap and longest gap between loop passes, so the fix can be checked without a
 stall: the gap between passes should now be ~0-1 ms for the whole hold.
+
+### Bench result (2026-11-01, Galaxy S23)
+
+Recorded 2026-11-01 (Prompt AC). The user's bench run, firmware `75b0a61`
+(`BLE_DEBUG_WATCHDOG_LOG` 1), phone a **Galaxy S23 (SM-S911U)**. Log:
+`_03-Software/PuTTY Logs/prompt_aa_bench.log` (271 lines). Line numbers below
+were checked against the log. **VERIFIED (user's bench run)**, except where
+marked as the user's report.
+
+- **11 app AUTO LEVEL holds, no watchdog stop.** Eleven `BLE: hold end
+  op=0x03` lines (40, 61, 85, 103, 121, 151, 169, 187, 209, 227, 248). No
+  `BLE: WATCHDOG STOP` anywhere in the log (`grep -c WATCHDOG` = 0). Each of
+  the eleven is preceded by exactly one `Step 1: ROLL and PITCH check` since
+  the previous one (25, 46, 67, 91, 109, 132, 157, 175, 193, 215, 233), so no
+  hold restarted the levelling state machine. Every one printed `Exit
+  auto-level` before its hold-end line: six levelled both axes (40, 61, 85,
+  151, 209, 248), three levelled PITCH with ROLL disabled (103, 121, 169),
+  two levelled ROLL with PITCH disabled (187, 227).
+- **Every hold-end line reads `longest between passes 0 ms`.** There are
+  **23** hold-end lines, not 22: 12 `op=0x01` (MOVE_START holds: 24, 45, 66,
+  90, 108, 126, 131, 156, 174, 192, 214, 232) and the 11 `op=0x03` above.
+  Lines 126 and 131 are two move holds in a row with no auto-level between
+  them. All 23 read `longest between passes 0 ms` (`grep -vc` = 0). The value
+  is printed in whole milliseconds (`ble_bed_service.c:1042-1045`), so 0 means
+  under 1 ms: the loop was not sleeping between passes during any hold.
+- **Longest refresh gap per hold:** 121-181 ms over the 11 AUTO LEVEL holds
+  (min 121 at line 40, max 181 at line 227; eight of the eleven at 148-151);
+  119-181 ms over all 23 lines (the 119 is a move hold, line 66). Against the
+  300 ms watchdog that leaves at least 119 ms of margin on this bench.
+- **Physical controller unaffected.** A physical DOWN HEAD press (`SW: 0x0800`,
+  lines 249-254) and a physical AUTO LEVEL hold (`SW: 0x0002`, lines 255-271)
+  that levelled ROLL then PITCH (`Exit auto-level` at 270) printed no hold-end
+  line, as designed: the line is tied to the dead-man watchdog, which only the
+  app path arms.
+- **Connection parameters:** `interval=24 latency=0 timeout=500` on connect
+  (lines 15, 17), then `timeout=100` after the bed's request (line 19).
+- **App side:** no "Connection stalled" message during the run (user's
+  report).
+
+**Result: the fix is verified** on this bench - keeping the CPU in EM0 during
+app holds removed the watchdog stops that the 2026-11-01 iPhone run showed six
+of. **The mechanism remains unproven.** Per the Prompt AA handoff (part C, C4)
+the Bluetooth controller probably held the device in EM1, not EM2, so the
+earlier stalls happened while the CPU slept in EM1; why the phone's writes
+reached the application late and bunched while it slept is still not
+established. This result shows that CPU sleep during a hold was the trigger,
+not which part of sleeping caused it. The sleep-clock question (LFRCO
+precision) is carried in `docs/PRE_SHIP.md`.
+
+Two things this run does not cover: it used the Galaxy S23, not the iPhone
+that produced the original stalls, and a desk bed at desk range (the van test
+at a realistic distance is in `docs/PRE_SHIP.md`). "Bed is level" (REGRESSION
+6.1) printed in none of the twelve runs; see the Prompt AC handoff, QUESTIONS.
diff --git a/docs/PRE_SHIP.md b/docs/PRE_SHIP.md
new file mode 100644
index 0000000..bc90f0b
--- /dev/null
+++ b/docs/PRE_SHIP.md
@@ -0,0 +1,85 @@
+# PRE_SHIP.md
+
+Everything that must be done or tested before the Tilt Bed controller and its
+app ship. Created 2026-11-01 (Prompt AC). Before this file the items lived in
+per-item notes (source comments, DECISIONS entries, handoff QUESTIONS and the
+SESSION_LOG); the Prompt AA handoff (QUESTIONS 1) asked where the list should
+live, and the reviewer answered with this file.
+
+**Rules.** One row per item. Do not delete a row: when an item is closed, put
+`CLOSED <date>` and the evidence (commit SHA, bench log and line numbers) in
+the "How it is closed" cell. New items are added at the bottom of their group
+with their source. The app repo has no list of its own, so app items are kept
+here too.
+
+**Owners.** Andrew = bench, hardware, production and business work.
+Claude = the reviewer (decisions and prompts). Claude Code = code and doc
+changes in a repo.
+
+File:line references are to the firmware at `6796e6b` unless marked.
+
+## Firmware
+
+| # | Item | Why (one line) | Owner | How it is closed | Source |
+|---|---|---|---|---|---|
+| F1 | `SWITCH_SAMPLER_DIAG` 1 -> 0 (`switch_input.c:57`) | Bench prints on every switch edge and read failure (`SW: ...`) | Claude Code | Change to 0, build, check the `SW:` lines are gone | DECISIONS #41 (Status); `switch_input.c:57` comment "pre-ship: 0" |
+| F2 | `BLE_DEBUG_WATCHDOG_LOG` 1 -> 0 (`ble_bed_service.h:36`) | Bench diagnostics: per-pass timing, the `BLE: hold end` line and the WATCHDOG STOP block | Claude Code | Change to 0, build (it built clean at 0 in Prompt AA) | Prompt Y handoff (2026-10-23); `ble_bed_service.h:29-36` "Set to 0 before shipping" |
+| F3 | `BLE_DEBUG_SECURITY_LOG` 1 -> decide (`ble_bed_service.c:53`) | Bonding/connection prints; the comment says "Set to 0 for a quiet production log" | Claude decides, Claude Code changes | Decision recorded, then change or keep | `ble_bed_service.c:43-53` |
+| F4 | Confirm the bench-only flags are 0 in the shipped build: `BLE_BENCH_WIPE_BONDS` (`ble_bed_service.c:70`), `BED_BENCH_PROVOKE_RUNAWAY` (`switch_input.c:98`), `BED_CAPTURE_MOTOR_CURRENTS` (`debug_capture.h:21`) | Each must never ship as 1 (bonds wiped every boot / a bed that moves by itself / CSV-only console) | Claude Code | Grep of the release commit shows all three at 0 | Comments at each define; standing rule in every prompt |
+| F5 | PWM fixed at 100% duty, started at boot "only for initial testing, then remove" (`app.c:416-427`) | The comment says testing-only, but motion is gated by the expander enable bits and relies on PWM always running (`exp_board.c:302-309`) | Claude decides, Claude Code changes | Decision (keep 100% and fix the comment, or change the drive scheme) then REGRESSION §2 and §5 | `app.c:423`; DECISIONS #9 (speed control deferred); Prompt AC |
+| F6 | Motor-current supervision runs during the BED LIGHTS hold | `app_lights_set()` sets `active_switch` (`bed_actions.c:199`), so `run_motor_supervision()` runs with no motor on (`switch_input.c:921-926`) and prints `Max Current` lines | Claude decides, Claude Code changes | Gate it, or accept it with a recorded reason; then REGRESSION §7-§8 | Prompt W handoff (2026-10-18_02) QUESTIONS 7 |
+| F7 | Stack allows 4 connections, firmware tracks 2 | `SL_BT_CONFIG_MAX_CONNECTIONS (4)` (`config/sl_bluetooth_connection_config.h:38`) vs `open_connections[2]` (`ble_bed_service.c:192`); a third connection is silently untracked (`:351-359`) | Claude decides; configurator change by Andrew or code by Claude Code | Set 2 in the configurator (BLE_GATT_DESIGN §2 says 2) or handle a third; test with three phones | Prompt W handoff (2026-10-18_02) QUESTIONS 8 |
+| F8 | Pairing window has no timeout | A forgotten settings mode leaves new bonding open until a BED LIGHTS press or a reset | Claude / Andrew (re-opens user decision #42.3) | Decision: keep "no timer" with a recorded reason, or add a timeout | DECISIONS #42 decision 3; Prompt V handoff (2026-10-18_01) QUESTIONS 5 |
+| F9 | I2CSPM blocking timeout is 300 s | SDK 2026.6.0 passes `SL_I2CSPM_TRANSFER_TIMEOUT` 300000 as milliseconds; a hung bus could block one main-loop pass for up to 300 s, with the watchdog and fail-safe unable to run | Claude (own prompt), Claude Code | Own prompt: a bounded timeout without hand-editing the SDK, then a bench check | Prompt Y handoff QUESTIONS 5; reviewer ruling 5 (SESSION_LOG 2026-11-01) |
+| F10 | Behaviour with the Switch board unplugged, and per-board read failures | Designed but never bench-run; a board that fails while the other answers is never marked absent | Andrew (bench) | Bench: unplug each UI board at idle and during a hold; record what moves and what prints | Sampler handoff (2026-10-17_05) QUESTIONS 4 and 6 |
+| F11 | Sleep clock: LFRCO in default precision; EM2 likely never reached. **Test before shipping (Andrew).** | The SDK reports the LFRCO as 0xFFFF ppm and the BLE controller then holds the device in EM1 (`sl_btctrl_init.c:289-298`), so idle current is higher than EM2 and the radio's sleep-clock timing is unexplained | Andrew (bench); configurator change by Andrew | 1) Measure idle current, connected and not. 2) Set LFRCO high precision in the configurator; repeat 6.9 **with** DECISIONS #45 and **without** it (temporary build); measure idle current again | Prompt AA handoff part C (C1-C4) and QUESTIONS 3-4; DECISIONS #45 |
+| F12 | Dead-man watchdog margin at a realistic distance | The 2026-11-01 pass (longest refresh gap 119-181 ms of 300 ms) was a desk bed at desk range | Andrew (bench) | Repeat REGRESSION 6.9 (10 app auto-level holds) in the van at a realistic phone distance, `BLE_DEBUG_WATCHDOG_LOG` 1; read "longest refresh gap" on every hold-end line | DECISIONS #45 bench result; Prompt AC |
+| F13 | Full REGRESSION.md pass on current firmware | No full pass since 2026-07-19 (`af369f2`); Stage C, the sampler, the pairing window and #45 have landed since | Andrew (bench) | Full pass recorded in a new REGRESSION_RECORD file | MIGRATION_STATUS Phase 6 |
+| F14 | Release-config confirmation pass | Every recorded pass was a Debug build | Andrew (bench) | Release build, then the pass (or the subset the reviewer names) | REGRESSION_RECORD_PRE_STAGEC.md:38; MIGRATION_STATUS Phase 6 |
+| F15 | DECISIONS #29 timing constants review | Auto-level's filter (`NUM_ACCEL_READINGS`, `accel_alpha`) and the collision path (`NUM_CURRENT_READINGS`, `NUM_INITIAL_CURRENTS_IGNORED`, `NUM_CURRENTS_READ_DYNAMIC_THRESHOLD`, `alpha`) count loop passes, not milliseconds (DECISIONS #2) | Claude (design), Claude Code | Design prompt; change; REGRESSION §5 and §6 with a loop-rate measurement | DECISIONS #29 |
+| F16 | `tilt_fresh` (Stage B5) | Bed Status flag bit 3 is always 0 (`ble_bed_service.c:1057`), so the app cannot tell a fresh angle from a stale one | Claude decides | Implement, or ship without it with the app treating tilt as possibly stale | DECISIONS #17, #33; BLE_GATT_DESIGN §6 |
+| F17 | Gecko bootloader on every board before it ships | A board without the bootloader can never be updated over the air. Note: the application is linked at flash address 0x0 (`autogen/linkerfile.ld:32`), so the app's flash layout must change with it (configurator) | Claude (plan), Andrew (configurator, flashing) | Bootloader project built; app relinked; each board flashed and checked | BLE_GATT_DESIGN §10; Prompt AC |
+| F18 | "Connection stalled" shown when the physical controller takes over from the app | The app's refused repeats do not refresh the watchdog, so ~300 ms later the app gets WATCHDOG_STOP although the link is fine (`ble_bed_service.c:746-750`) | Claude decides; Claude Code (firmware or app) | A distinct result code, or app handling of FORCE_STOPPED then WATCHDOG_STOP; bench check | Prompt AA handoff QUESTIONS 5 |
+
+## App
+
+The app repository has no list of its own; these are recorded here.
+
+| # | Item | Why (one line) | Owner | How it is closed | Source |
+|---|---|---|---|---|---|
+| A1 | Hide the debug menu in release builds | Bench tools must not reach customers | Claude Code (app) | Release build shows no debug menu | Chat 2026-11-01 (Prompt AC); APP_KICKOFF_PROMPT.md:52 |
+| A2 | Upgrade `flutter_reactive_ble` once safe | Kotlin Gradle Plugin warning | Claude Code (app) | Upgrade, Android build clean, BLE smoke test | Chat 2026-11-01 (Prompt AC) |
+| A3 | Connection-state error path not classified for pairing refusal | A refused pairing (bed not in settings mode) is not shown as such; the close code differs by phone (0x1016 iPhone, 0x1022 Galaxy S23, DECISIONS #43) | Claude Code (app) | Refused pairing shows a "put the bed in settings mode" message on both phones | Chat 2026-11-01 (Prompt AC) |
+| A4 | Async write failures invisible to hold-to-run | A failed Command write during a hold is not surfaced | Claude Code (app) | Failed writes reported to the hold logic; bench check | Chat 2026-11-01 (Prompt AC) |
+| A5 | Reconnect speed | Slow reconnect after the link drops | Claude Code (app) | Measured before/after | Chat 2026-11-01 (Prompt AC) |
+| A6 | Dead `0x0005` subscription | The app subscribes to Settings RPC `54b10005`, which is not in the bed's GATT database (`config/btconf/gatt_configuration.btconf` has 0001-0004 and 0006 only) | Claude Code (app) | Remove it, or keep it behind the protocol version until 0005 exists | Chat 2026-11-01 (Prompt AC); Prompt AC handoff B4 |
+| A7 | Scan-throttle backoff | Android throttles frequent scans | Claude Code (app) | Backoff implemented; repeated scan test | Chat 2026-11-01 (Prompt AC) |
+| A8 | "Connection stalled" on a physical takeover | Same as F18 if fixed in the app | Claude Code (app) | See F18 | Prompt AA handoff QUESTIONS 5 |
+
+## Hardware / production
+
+| # | Item | Why (one line) | Owner | How it is closed | Source |
+|---|---|---|---|---|---|
+| H1 | Flash the Gecko bootloader on every board in production | Wired programming is the only way to add it; see F17 | Andrew | Production step written down; every board checked | BLE_GATT_DESIGN §10 |
+| H2 | GBL signing / encryption keys | Field units should accept only authentic firmware; keys generated once and kept out of git | Andrew | Keys generated and stored; signed image accepted, unsigned refused | BLE_GATT_DESIGN §10 |
+| H3 | Is a 32.768 kHz crystal fitted on the BGM220S module? | Decides whether LFXO is an option for F11; the SDK defaults suggest the S module has none | Andrew | Module datasheet read; answer recorded in HARDWARE_CONFIG.md | Prompt AA handoff C1 |
+
+## Business / legal
+
+| # | Item | Why (one line) | Owner | How it is closed | Source |
+|---|---|---|---|---|---|
+| L1 | Averta font licence | The web-font licence excludes mobile apps | Andrew | Buy an app licence, or replace the font in the app | Chat 2026-11-01 (Prompt AC) |
+| L2 | Confirm the app's name in App Store Connect | The listed name must match the product | Andrew | Name confirmed in App Store Connect | Chat 2026-11-01 (Prompt AC); APP_SETUP_GUIDE.md:108 |
+
+## Also recorded in the repo - not yet marked pre-ship (reviewer to decide)
+
+Found while compiling this list. Listed so they are not lost; none is a
+pre-ship item until the reviewer says so.
+
+| Item | Source |
+|---|---|
+| No reverse after a collision during auto-level | REGRESSION §5 note; REFACTOR_PLAN P1.5 |
+| Motor-fault reporting is an interim 200 ms poll; the expander's latch registers are unconfigured and the fault interrupt is not wired | `exp_board.c:342-348`, `:515-518` |
+| The lower-collision "OFF" rung (750) is not actually off | SETPOINTS_INVENTORY.md Note A |
+| Auto-level's pulsed ("sneak") phase runs with no current supervision | Prompt AC handoff B1(d) (`auto_level.c:679-738`, `:770-781`) |
+| "Bed is level" never prints: `bed_level` is never set true | Prompt AC handoff B8 (`auto_level.c:166,603`; `app.c:681`) |
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index c1aad53..489fbae 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -210,7 +210,7 @@ pitch, all actuators clear of limits.
       run, start to "Bed is level" - the original auto-level BLE-drop bug
       (DECISIONS.md #26) only manifested on a sustained multi-second move,
       so a quick/near-level test is not sufficient coverage for this check
-- [ ] 6.9 App AUTO LEVEL (DECISIONS #45): 10 holds from off-level, no
+- [x] 6.9 App AUTO LEVEL (DECISIONS #45): 10 holds from off-level, no
       `BLE: WATCHDOG STOP` / no "Connection stalled" in the app. With
       `BLE_DEBUG_WATCHDOG_LOG` 1, each hold that ends normally prints one
       `BLE: hold end op=0x03 ...` line; "longest between passes" should be
@@ -241,6 +241,28 @@ and delivered in a burst (a sleep-clock accuracy or EM2 wake-up timing issue
 being a candidate). Tested by keeping the CPU awake during app holds
 (Prompt AA commit B).
 
+**Recorded result, 2026-11-01 (firmware `75b0a61`, `BLE_DEBUG_WATCHDOG_LOG`
+1, desk bed, Galaxy S23 SM-S911U, log `prompt_aa_bench.log`, 271 lines).**
+VERIFIED (user's bench run). **6.9 PASSES** - 11 app AUTO LEVEL holds, no
+watchdog stop. Full record: DECISIONS #45 "Bench result (2026-11-01, Galaxy
+S23)".
+
+| Check | Result |
+|---|---|
+| App AUTO LEVEL holds | 11 `BLE: hold end op=0x03` lines (40, 61, 85, 103, 121, 151, 169, 187, 209, 227, 248), each preceded by exactly one `Step 1` |
+| WATCHDOG STOP | none in the log |
+| "Connection stalled" in the app | none (user's report) |
+| Longest between passes | `0 ms` on all 23 hold-end lines (12 `op=0x01` + 11 `op=0x03`; the prompt said 22) |
+| Longest refresh gap per hold | 121-181 ms over the AUTO LEVEL holds; 119-181 ms over all 23 |
+| Physical AUTO LEVEL (`SW: 0x0002`, lines 255-271) | levelled, no hold-end line |
+| Connection parameters | `latency=0`, `timeout=500` then `timeout=100` (lines 15, 17, 19) |
+
+The fix is verified; the mechanism is not (the device probably slept in EM1,
+not EM2 - Prompt AA handoff C4). Not covered: the iPhone (the original
+failing phone), and a realistic distance in the van (`docs/PRE_SHIP.md`).
+"Bed is level" did not print in any of the twelve runs (6.1 expects it) -
+open question, Prompt AC handoff.
+
 ## 7. Settings mode
 
 - [ ] ⚡ 7.1 Hold BED_LIGHTS 5 s: amber LED fast-blinks — settings mode active
diff --git a/docs/prompts/PROMPT_AC_feature-groundwork-report.md b/docs/prompts/PROMPT_AC_feature-groundwork-report.md
new file mode 100644
index 0000000..8a61fa6
--- /dev/null
+++ b/docs/prompts/PROMPT_AC_feature-groundwork-report.md
@@ -0,0 +1,140 @@
+# Prompt AC - record the AA bench result, create PRE_SHIP.md, report on the code behind the next features
+
+Written by the reviewer on 2026-11-01. Implement as specified. Where the code
+contradicts this document, stop and report it under QUESTIONS FOR REVIEW
+instead of improvising. Follow CLAUDE.md for the handoff file, the session
+log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
+BLE_BENCH_WIPE_BONDS stay 0. config/ and the SDK are not hand-edited.
+
+**No source code changes in this prompt.** One docs commit (A), then a
+report-only section (B) written into the handoff. Do not build unless you
+need a build to answer a question (say so if you do).
+
+Expected repo state: HEAD 6796e6b (Prompt AA handoff), level with
+origin/main. Uncommitted: .projectlinkstore (never commit it). This prompt
+file is untracked; commit it with commit A. If HEAD or origin/main differ,
+stop and report before changing anything.
+
+======================================================================
+A. Docs (one commit, docs only)
+======================================================================
+A1. Record the Prompt AA bench result as VERIFIED (user's bench run,
+    2026-11-01, Galaxy S23 SM-S911U, firmware 75b0a61). Log:
+    C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\prompt_aa_bench.log
+    Confirm each point from the log, with line numbers, before recording:
+    - 11 app AUTO LEVEL holds (op=0x03 hold-end lines), no "BLE: WATCHDOG
+      STOP" anywhere, exactly one "Step 1" per hold;
+    - every one of the 22 "BLE: hold end" lines (op=0x01 and op=0x03)
+      reads "longest between passes 0 ms";
+    - longest refresh gap per hold ranges 119-181 ms (state min and max);
+    - a physical-controller AUTO LEVEL (SW 0x0002, lines ~255-271) levelled
+      and printed no hold-end line;
+    - connection parameters: latency 0, timeout 500 then 100.
+    The user also reports no "Connection stalled" in the app. Record under
+    DECISIONS #45 (a "Bench result" subsection), tick REGRESSION.md 6.9,
+    and add the SESSION_LOG entry. State that the mechanism remains
+    unproven (EM1 sleep per the AA handoff C4); the fix is verified.
+A2. Create docs/PRE_SHIP.md: every item that must be done or tested before
+    shipping, one row each, with columns: item, why (one line), owner
+    (Andrew / Claude / Claude Code), how it is closed (a test or a change),
+    source (DECISIONS / handoff / chat date). Group: Firmware, App,
+    Hardware/production, Business/legal. Include at least these, adding any
+    others you find recorded in the repo (search for "pre-ship", "pre ship",
+    "before shipping", "must be 0"):
+    Firmware:
+    - SWITCH_SAMPLER_DIAG, BLE_DEBUG_WATCHDOG_LOG and any other bench
+      diagnostic flags set to 0 (list every such define with file:line);
+    - PWM fixed at 100% ("only for initial testing");
+    - motor-current supervision running during the BED LIGHTS hold;
+    - stack allows 4 connections, firmware tracks 2;
+    - pairing window has no timeout;
+    - I2CSPM 300 s blocking timeout (Prompt Y QUESTIONS 5);
+    - behaviour with the Switch board unplugged / per-board read failures;
+    - sleep clock (LFRCO default precision, EM2 likely never reached):
+      measure idle current, then try LFRCO high precision with and without
+      DECISIONS #45 - marked "test before shipping (Andrew)";
+    - dead-man watchdog margin: repeat the 10-hold app auto-level test in
+      the van at a realistic distance and read "longest refresh gap";
+    - full REGRESSION.md pass on current firmware, and the Release-config
+      pass;
+    - DECISIONS #29 timing constants review; tilt_fresh (Stage B5);
+    - Gecko bootloader on every board before it ships (BLE_GATT_DESIGN.md
+      section 10);
+    - "Connection stalled" shown when the physical controller takes over
+      from the app (Prompt AA QUESTIONS 5) - needs a distinct result or app
+      handling.
+    App (record them here too; the app repo has no such list):
+    - hide the debug menu in release builds;
+    - upgrade flutter_reactive_ble once safe (Kotlin Gradle Plugin warning);
+    - connection-state error path not classified for pairing refusal;
+    - async write failures invisible to hold-to-run;
+    - reconnect speed; dead 0x0005 subscription; scan-throttle backoff.
+    Business/legal:
+    - the Averta font licence (web-font licence) excludes mobile apps -
+      licence it for apps or use another font;
+    - confirm the app's name in App Store Connect.
+A3. Commit A. Show the PRE_SHIP.md file and the other diffs in the
+    handoff.
+
+======================================================================
+B. Report only - change nothing
+======================================================================
+Answer each from the code, with file:line for every claim, marking each
+VERIFIED (read in code or SDK) or ASSUMED. Keep each answer focused; the
+reviewer will design from it. Where a question asks how something "could"
+be done, give options and their costs, not a decision.
+
+B1. Collision detection.
+    (a) Exactly how a collision trip is decided today: which currents are
+        measured (per motor? total?), sampling rate and method, how the
+        baseline is formed, how the raise/lower sensitivity values enter
+        the test, and where the trip is declared ("Tripped on steady!" and
+        any other trip prints).
+    (b) At the moment of a trip, which per-motor values exist in memory
+        (current now, baseline, peak, history arrays) and for how long.
+    (c) Could the firmware report, at trip time, each motor's share of the
+        rise that caused the trip, without blocking and without new I2C?
+        Options, size of the data, and where it could be sent (Bed Status,
+        Command Result, a new characteristic).
+    (d) For each move type (HEAD, FOOT, LEFT, RIGHT up/down, ALL up/down,
+        auto-level), which motors carry current, so the reviewer knows
+        what each move can sense.
+B2. Level reference ("set level"). How the zero-level baseline is captured
+    today (which button sequence or mode, the averaging used), what is
+    stored in NVM3 (keys, units, the "Loaded Roll Angle / Pitch Angle"
+    lines), and any limits on when it may run.
+B3. Settings mode. Entry and exit paths; every setting adjustable today and
+    how (collision sensitivity steps, anything else); how values are stored
+    (bed_settings.c key table: list every key with min, max, default); what
+    motion and commands are blocked in settings mode (cite the Command
+    arbitration); exactly where the pairing window (DECISIONS #42) is tied
+    to settings mode. Then options for an app-entered settings mode that
+    allows settings changes but does NOT open the pairing window (the user
+    has decided only a physically entered settings mode may accept new
+    phones).
+B4. Settings RPC (BLE_GATT_DESIGN.md section 9, characteristic 0005).
+    Anything implemented? Is 0005 in the GATT database (gatt_configuration
+    / autogen)? What the app's subscription to it does today.
+B5. Lights. What LIGHTS_SET drives (which outputs), whether the state is
+    reported in Bed Status (so the app's Lights button can show on/off),
+    and any interaction with settings mode or the status LEDs.
+B6. Motor outputs to bed corners. How motor outputs 0-3 map to HEAD,
+    LEFT, RIGHT, FOOT today (the pair table for every move), and how
+    auto-level chooses which pair to drive. The accelerometer's axis and
+    sign conventions (roll, pitch, the pitch-sign decision), where each
+    constant lives. List every call site a runtime corner-mapping table
+    (stored in NVM3) would have to touch, and anything that assumes the
+    board's orientation (logo toward the head).
+B7. Timed moves. Is there any existing way to run one motor (or one
+    output) for a fixed short time, with collision and fail-safe active?
+    The smallest practical run time given the loop and PWM. Any actuator
+    speed constant recorded in the repo (say if none).
+B8. Auto-level outcomes. Every way an auto-level run ends (level reached,
+    an axis disabled, collision, watchdog, user release, any timeout), the
+    print for each, and whether any of these reach the app today (Bed
+    Status command field, Command Result codes).
+B9. Headroom. Current flash and RAM use (from the last build output or
+    arm-none-eabi-size) against the BGM220SC22HNA limits, and the size of
+    the NVM3 area.
+
+In the handoff, put B1-B9 under a "b. Analysis" heading in that order.
```

## d. Raw output of commands run

### d.1 Session command log, in the order run

Every shell command after the first few was run through a small logging
wrapper (`r.sh` in the session scratchpad) that writes the command, its
combined output and its exit code. This is that log, unedited. No secrets
appear in it. Commands run before the wrapper existed are listed in g
(NOT DONE) and were re-run here.

```
$ date; git fetch origin; git rev-parse HEAD origin/main; git status
Sun Nov  1 21:00:12 PST 2026
6796e6b06122e2b3f7f8272b92fc9e6ed75c3b6a
6796e6b06122e2b3f7f8272b92fc9e6ed75c3b6a
On branch main
Your branch is up to date with 'origin/main'.

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/prompts/PROMPT_AC_feature-groundwork-report.md

no changes added to commit (use "git add" and/or "git commit -a")
[exit 0]

$ wc -l "../PuTTY Logs/prompt_aa_bench.log"; grep -n 'BLE: hold end' "../PuTTY Logs/prompt_aa_bench.log"; grep -c 'BLE: hold end' "../PuTTY Logs/prompt_aa_bench.log"; grep -c 'hold end op=0x01' "../PuTTY Logs/prompt_aa_bench.log"; grep -c 'hold end op=0x03' "../PuTTY Logs/prompt_aa_bench.log"
271 ../PuTTY Logs/prompt_aa_bench.log
24:BLE: hold end op=0x01 seq=38: 37 refreshes, longest refresh gap 121 ms, longest between passes 0 ms
40:BLE: hold end op=0x03 seq=97: 57 refreshes, longest refresh gap 121 ms, longest between passes 0 ms
45:BLE: hold end op=0x01 seq=122: 23 refreshes, longest refresh gap 149 ms, longest between passes 0 ms
61:BLE: hold end op=0x03 seq=160: 36 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
66:BLE: hold end op=0x01 seq=179: 17 refreshes, longest refresh gap 119 ms, longest between passes 0 ms
85:BLE: hold end op=0x03 seq=225: 44 refreshes, longest refresh gap 179 ms, longest between passes 0 ms
90:BLE: hold end op=0x01 seq=241: 14 refreshes, longest refresh gap 149 ms, longest between passes 0 ms
103:BLE: hold end op=0x03 seq=15: 28 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
108:BLE: hold end op=0x01 seq=37: 20 refreshes, longest refresh gap 149 ms, longest between passes 0 ms
121:BLE: hold end op=0x03 seq=68: 29 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
126:BLE: hold end op=0x01 seq=85: 15 refreshes, longest refresh gap 120 ms, longest between passes 0 ms
131:BLE: hold end op=0x01 seq=101: 14 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
151:BLE: hold end op=0x03 seq=152: 49 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
156:BLE: hold end op=0x01 seq=170: 16 refreshes, longest refresh gap 127 ms, longest between passes 0 ms
169:BLE: hold end op=0x03 seq=211: 39 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
174:BLE: hold end op=0x01 seq=232: 19 refreshes, longest refresh gap 139 ms, longest between passes 0 ms
187:BLE: hold end op=0x03 seq=11: 33 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
192:BLE: hold end op=0x01 seq=37: 24 refreshes, longest refresh gap 121 ms, longest between passes 0 ms
209:BLE: hold end op=0x03 seq=117: 78 refreshes, longest refresh gap 151 ms, longest between passes 0 ms
214:BLE: hold end op=0x01 seq=139: 20 refreshes, longest refresh gap 120 ms, longest between passes 0 ms
227:BLE: hold end op=0x03 seq=184: 43 refreshes, longest refresh gap 181 ms, longest between passes 0 ms
232:BLE: hold end op=0x01 seq=205: 19 refreshes, longest refresh gap 149 ms, longest between passes 0 ms
248:BLE: hold end op=0x03 seq=255: 48 refreshes, longest refresh gap 148 ms, longest between passes 0 ms
23
12
11
[exit 0]

$ grep 'BLE: hold end' "../PuTTY Logs/prompt_aa_bench.log" | grep -vc 'longest between passes 0 ms'; grep -c 'WATCHDOG' "../PuTTY Logs/prompt_aa_bench.log"; grep -c 'Connection stalled' "../PuTTY Logs/prompt_aa_bench.log"; grep -n 'Step 1' "../PuTTY Logs/prompt_aa_bench.log"; grep -n 'connection_parameters' "../PuTTY Logs/prompt_aa_bench.log"; grep -n '^SW:' "../PuTTY Logs/prompt_aa_bench.log"
0
0
0
25:Step 1: ROLL and PITCH check
46:Step 1: ROLL and PITCH check
67:Step 1: ROLL and PITCH check
91:Step 1: ROLL and PITCH check
109:Step 1: ROLL and PITCH check
132:Step 1: ROLL and PITCH check
157:Step 1: ROLL and PITCH check
175:Step 1: ROLL and PITCH check
193:Step 1: ROLL and PITCH check
215:Step 1: ROLL and PITCH check
233:Step 1: ROLL and PITCH check
256:Step 1: ROLL and PITCH check
15:BLE: connection_parameters conn=1 interval=24 latency=0 timeout=500 security_mode=0
17:BLE: connection_parameters conn=1 interval=24 latency=0 timeout=500 security_mode=1
19:BLE: connection_parameters conn=1 interval=24 latency=0 timeout=100 security_mode=1
249:SW: 0x0800 down (bounces 0)
254:SW: 0x0800 up (bounces 0)
255:SW: 0x0002 down (bounces 0)
271:SW: 0x0002 up (bounces 0)
[exit 0]

$ grep 'hold end op=0x03' "../PuTTY Logs/prompt_aa_bench.log" | sed -E 's/.*gap ([0-9]+) ms.*/\1/' | sort -n | tr '\n' ' '; echo; grep 'hold end op=0x01' "../PuTTY Logs/prompt_aa_bench.log" | sed -E 's/.*gap ([0-9]+) ms.*/\1/' | sort -n | tr '\n' ' '; echo
121 148 150 150 150 150 150 150 151 179 181 
119 120 120 121 121 127 139 149 149 149 149 150 
[exit 0]

$ sed -n 249,271p "../PuTTY Logs/prompt_aa_bench.log"; grep -n 'hold end\|Exit auto-level\|disabled' "../PuTTY Logs/prompt_aa_bench.log" | awk -F: '$1>240'
SW: 0x0800 down (bounces 0)
Max Current for Motor 0: 713
Max Current for Motor 1: 21
Max Current for Motor 2: 15
Max Current for Motor 3: 725
SW: 0x0800 up (bounces 0)
SW: 0x0002 down (bounces 0)
Step 1: ROLL and PITCH check
Step 2: ROLL Not Level
Leveling...
Latch
Pulse off...
ROLL Leveled successfully...
Axis set to PITCH
Leveling...
Max Current for Motor 0: 714
Max Current for Motor 1: 737
Max Current for Motor 2: 1155
Max Current for Motor 3: 701
Latch
PITCH Leveled successfully...
Exit auto-level
SW: 0x0002 up (bounces 0)
247:Exit auto-level
248:BLE: hold end op=0x03 seq=255: 48 refreshes, longest refresh gap 148 ms, longest between passes 0 ms
270:Exit auto-level
[exit 0]

$ grep -rn -i 'pre-ship\|pre ship\|preship\|before shipping\|before ship\|must be 0\|before it ships\|before release\|test before' --include=*.md --include=*.c --include=*.h . | grep -v '^./docs/prompts/PROMPT_AC' | grep -v 'autogen/' | cut -c1-260
./ble_bed_service.h:35:// 0 before shipping.
./docs/BLE_GATT_DESIGN.md:103:| 0x07 | REFRESH_TILT | 0 | 0 | Opens a tilt sampling burst (DECISIONS #33, trigger T2) so the app can pull a fresh angle on demand. **Commands no motion, cancels no motion.** Accepted from **any** role and in settings mode — se
./docs/DECISIONS.md:1802:  per-bit bounce tally. Set it to 0 before shipping.
./docs/handoffs/fw_HANDOFF_2026-10-17_05_one-switch-reader-sampler.md:180:2. **Diagnostics go through a small macro `SW_DIAG(...)`** which is `printf` when `SWITCH_SAMPLER_DIAG && !BED_CAPTURE_MOTOR_CURRENTS`, else nothing. The `!BED_CAPTURE_MOTOR_CURRENTS` pa
./docs/handoffs/fw_HANDOFF_2026-10-17_05_one-switch-reader-sampler.md:758:+#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
./docs/handoffs/fw_HANDOFF_2026-10-17_05_one-switch-reader-sampler.md:1651:+  per-bit bounce tally. Set it to 0 before shipping.
./docs/handoffs/fw_HANDOFF_2026-10-17_05_one-switch-reader-sampler.md:1906:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
./docs/handoffs/fw_HANDOFF_2026-10-18_01_switch-followups-settings-exit-pairing-window.md:345: #define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
./docs/handoffs/fw_HANDOFF_2026-10-18_01_switch-followups-settings-exit-pairing-window.md:1137:switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
./docs/handoffs/fw_HANDOFF_2026-10-18_01_switch-followups-settings-exit-pairing-window.md:1313:switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
./docs/handoffs/fw_HANDOFF_2026-10-18_01_switch-followups-settings-exit-pairing-window.md:1440:- **`SWITCH_SAMPLER_DIAG` is still 1** (pre-ship should be 0); not asked to change it.
./docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:227:  - `timeout_2000ms`: if the 2 s timer fired just before release, the old code left the flag. The next AUTO_LEVEL press resets `zeroLevelFirstPass = true` (`:213-217`) 
./docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:228:  - `timeout_5000ms`: if the 5 s erase timer fired just before release, the old code left the flag. The AUTO_LEVEL block checks `if (timeout_5000ms)` with no other cond
./docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:1555:switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
./docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:426:1. Pre-ship it must be 0.
./docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:1098:+a new define BLE_DEBUG_WATCHDOG_LOG (default 1, pre-ship 0):
./docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:1184:    default 1, set to 0 before shipping):
./docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:1402:+// 0 before shipping.
./docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:1820:+// 0 before shipping.
./docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:2678:  0 before shipping. Not changed (bench diagnostics).
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:51:| A2 | Record the reviewer's rulings on Prompt Y QUESTIONS 1-8 in SESSION_LOG; add the I2CSPM timeout to the pre-ship list | Rulings recorded. **No pre-ship list exists**, so the item i
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:121:by this prompt; (5) the 300 s I2CSPM timeout is a pre-ship item for its own
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:125:On (5): `grep -rn -i "pre-ship\|preship\|before shipping\|ship list"` over
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:127:per-item notes (`SWITCH_SAMPLER_DIAG` "pre-ship: 0", `BLE_DEBUG_WATCHDOG_LOG`
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:128:"0 before shipping", DECISIONS #41's "Set it to 0 before shipping") — d.1.
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:131:the four known pre-ship items in one place, and QUESTIONS 1 asks where the
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:721:+    (5) the 300 s I2CSPM timeout is a pre-ship item for its own prompt -
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:722:+    add it to the pre-ship list wherever that list lives; (6) stale comment
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:1141:$ grep -rn -i "pre-ship\|preship\|before shipping\|ship list" docs/*.md *.c *.h | grep -v "^docs/SESSION_LOG\|^docs/handoffs"
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:1142:docs/DECISIONS.md:1802:  per-bit bounce tally. Set it to 0 before shipping.
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:1143:docs/SWITCH_SAMPLER_DESIGN.md:101:  #define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:1144:switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:1145:ble_bed_service.h:35:// 0 before shipping.
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:2119:| V3 | No pre-ship list exists in the repo | d.1 `grep -rn -i "pre-ship\|..."` (three per-item notes only) |
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:2160:1. **No pre-ship list exists.** Ruling 5 says to add the I2CSPM 300 s
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:2161:   timeout "to the pre-ship list wherever that list lives". There is none
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:2162:   (V3). I recorded the item, with the other three known pre-ship items, in
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:2201:- **Nothing asked for was skipped**, except the pre-ship list itself (QUESTIONS 1).
./docs/handoffs/fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md:2213:- `SWITCH_SAMPLER_DIAG` and `BLE_DEBUG_WATCHDOG_LOG` are both 1 (pre-ship 0).
./docs/prompts/PROMPT_AA_sleep-veto-during-app-holds.md:71:    (5) the 300 s I2CSPM timeout is a pre-ship item for its own prompt -
./docs/prompts/PROMPT_AA_sleep-veto-during-app-holds.md:72:    add it to the pre-ship list wherever that list lives; (6) stale comment
./docs/prompts/PROMPT_Y_watchdog-diagnostics-and-followups.md:71:a new define BLE_DEBUG_WATCHDOG_LOG (default 1, pre-ship 0):
./docs/SESSION_LOG.md:1232:- Decisions for the reviewer, listed in the handoff under QUESTIONS FOR REVIEW: the PRESSED gate on `sl_button_on_change`, the read-failure counting rule, release latency (~20-30 ms added by the debounce), behaviour on a bench with n
./docs/SESSION_LOG.md:1274:- `SWITCH_SAMPLER_DIAG` is still 1 (pre-ship: 0). `.projectlinkstore` still modified, never committed.
./docs/SESSION_LOG.md:1314:- `SWITCH_SAMPLER_DIAG` still 1 (pre-ship 0). `.projectlinkstore` still modified, never committed.
./docs/SESSION_LOG.md:1357:- `BLE_DEBUG_WATCHDOG_LOG` and `SWITCH_SAMPLER_DIAG` are both 1 (pre-ship 0). `.projectlinkstore` still modified, never committed.
./docs/SESSION_LOG.md:1421:   milliseconds, SDK 2026.6.0) is a **pre-ship item for its own prompt**. The
./docs/SESSION_LOG.md:1422:   ruling says to add it "to the pre-ship list wherever that list lives"; no
./docs/SESSION_LOG.md:1423:   single pre-ship list exists in this repo (Prompt AA handoff, QUESTIONS).
./docs/SESSION_LOG.md:1424:   Until one does, the known pre-ship items are: `SWITCH_SAMPLER_DIAG` -> 0
./docs/SESSION_LOG.md:1460:- QUESTIONS for the reviewer (handoff f): no pre-ship list exists; the SCA/EM1 finding vs the EM2 hypothesis; a one-line boot print of the sleep-clock accuracy to confirm it; LFRCO high-precision as a configurator change (not made); 
./docs/SESSION_LOG.md:1461:- Pre-ship: `BLE_DEBUG_WATCHDOG_LOG` and `SWITCH_SAMPLER_DIAG` -> 0; Release-config pass; I2CSPM 300 s timeout. `.projectlinkstore` still modified, never committed.
./docs/SESSION_LOG.md:1468:- ...that a pre-ship list file exists: it does not.
./docs/SWITCH_SAMPLER_DESIGN.md:101:  #define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
./switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
[exit 0]

$ ls *.c *.h test/ 2>&1 | tr '\n' ' '; echo; grep -n '^[[:space:]]*#[[:space:]]*define[[:space:]]\+[A-Z_]*\(DEBUG\|DIAG\|BENCH\|CAPTURE\|TEST\|PROVOKE\|WIPE\|LOG\|TRACE\|VERBOSE\)[A-Z_]*[[:space:]]' *.c *.h
adc_currents.c adc_currents.h app.c app.h app_bm.c auto_level.c auto_level.h bed_actions.c bed_actions.h bed_control.c bed_control.h bed_settings.c bed_settings.h bed_status.c bed_status.h bed_types.h ble_bed_service.c ble_bed_service.h debug_capture.h dual_motor_control.c dual_motor_control.h exp_board.c exp_board.h exp_ui.c exp_ui.h main.c mc3479.c mc3479.h motor_current_functions.c motor_current_functions.h nvm3_functions.c nvm3_functions.h pi4ioe5v6416.h settings_mode.c settings_mode.h sl_gatt_service_device_information_override.c switch_input.c switch_input.h version.h  test/: bed_control_test.c 
ble_bed_service.c:53:#define BLE_DEBUG_SECURITY_LOG 1
ble_bed_service.c:70:#define BLE_BENCH_WIPE_BONDS 0
ble_bed_service.c:253:#define WD_DIAG_GAPS 8
switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
switch_input.c:98:#define BED_BENCH_PROVOKE_RUNAWAY 0
switch_input.c:104:#define PROVOKE_DELAY_MS 300
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
[exit 0]

$ grep -n '^[[:space:]]*#[[:space:]]*if' *.c *.h | grep -v 'SL_CATALOG\|_H_*$\|ifndef [A-Z_]*_H' | grep -v 'defined(__cplusplus)'
app.c:396:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:440:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:455:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:460:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:468:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:568:#if BLE_DEBUG_WATCHDOG_LOG
app.c:594:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:632:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:646:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:654:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:679:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:703:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:709:#if !BED_CAPTURE_MOTOR_CURRENTS
app.c:765:#if BLE_DEBUG_WATCHDOG_LOG
auto_level.c:706:#if !BED_CAPTURE_MOTOR_CURRENTS
auto_level.c:728:#if !BED_CAPTURE_MOTOR_CURRENTS
auto_level.c:752:#if !BED_CAPTURE_MOTOR_CURRENTS
auto_level.c:815:#if !BED_CAPTURE_MOTOR_CURRENTS
auto_level.c:830:#if !BED_CAPTURE_MOTOR_CURRENTS
bed_settings.c:53:#if !BED_CAPTURE_MOTOR_CURRENTS
bed_settings.c:70:#if !BED_CAPTURE_MOTOR_CURRENTS
bed_settings.c:80:#if !BED_CAPTURE_MOTOR_CURRENTS
ble_bed_service.c:248:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:481:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:524:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:608:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:621:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:692:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:890:#if BLE_DEBUG_SECURITY_LOG
ble_bed_service.c:1037:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:1114:#if BLE_BENCH_WIPE_BONDS
ble_bed_service.c:1141:#if BLE_DEBUG_SECURITY_LOG
ble_bed_service.c:1163:#if BLE_DEBUG_SECURITY_LOG
ble_bed_service.c:1176:#if BLE_DEBUG_SECURITY_LOG
ble_bed_service.c:1206:#if BLE_DEBUG_SECURITY_LOG
ble_bed_service.c:1212:#if BLE_DEBUG_SECURITY_LOG
ble_bed_service.c:1241:#if BLE_DEBUG_SECURITY_LOG
ble_bed_service.c:1262:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:1344:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:1366:#if BLE_DEBUG_SECURITY_LOG
motor_current_functions.c:35:#if BED_CAPTURE_MOTOR_CURRENTS
motor_current_functions.c:298:#if BED_CAPTURE_MOTOR_CURRENTS
motor_current_functions.c:314:#if BED_CAPTURE_MOTOR_CURRENTS
motor_current_functions.c:353:#if BED_CAPTURE_MOTOR_CURRENTS
motor_current_functions.c:398:#if BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:135:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:311:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:329:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:342:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:347:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:352:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:358:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:363:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:372:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:392:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:410:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:433:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:453:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:458:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:463:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:469:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:474:#if !BED_CAPTURE_MOTOR_CURRENTS
settings_mode.c:481:#if !BED_CAPTURE_MOTOR_CURRENTS
sl_gatt_service_device_information_override.c:41:#if !defined(FIRMWARE_REVISION_STRING)
sl_gatt_service_device_information_override.c:53:#if !defined(MODEL_NUMBER_STRING) && defined(SL_BOARD_NAME)
sl_gatt_service_device_information_override.c:58:#if !defined(HARDWARE_REVISION_STRING) && defined(SL_BOARD_REV)
sl_gatt_service_device_information_override.c:66:#if defined(gattdb_firmware_revision_string) && defined(gattdb_firmware_revision_string_len) && defined(FIRMWARE_REVISION_STRING)
sl_gatt_service_device_information_override.c:73:#if defined(gattdb_model_number_string) && defined(gattdb_model_number_string_len) && defined(MODEL_NUMBER_STRING)
sl_gatt_service_device_information_override.c:80:#if defined(gattdb_hardware_revision_string) && defined(gattdb_hardware_revision_string_len) && defined(HARDWARE_REVISION_STRING)
sl_gatt_service_device_information_override.c:87:#if defined(gattdb_system_id) && defined(gattdb_system_id_len)
sl_gatt_service_device_information_override.c:103:#if defined(gattdb_firmware_revision_string) && defined(gattdb_firmware_revision_string_len) && defined(FIRMWARE_REVISION_STRING)
sl_gatt_service_device_information_override.c:115:#if defined(gattdb_model_number_string) && defined(gattdb_model_number_string_len) && defined(MODEL_NUMBER_STRING)
sl_gatt_service_device_information_override.c:129:#if defined(gattdb_hardware_revision_string) && defined(gattdb_hardware_revision_string_len) && defined(HARDWARE_REVISION_STRING)
sl_gatt_service_device_information_override.c:143:#if defined(gattdb_system_id) && defined(gattdb_system_id_len)
switch_input.c:244:#if SWITCH_SAMPLER_DIAG && !BED_CAPTURE_MOTOR_CURRENTS
switch_input.c:471:#if BED_BENCH_PROVOKE_RUNAWAY
switch_input.c:635:#if BED_BENCH_PROVOKE_RUNAWAY
switch_input.c:694:#if BED_BENCH_PROVOKE_RUNAWAY
switch_input.c:704:#if !BED_CAPTURE_MOTOR_CURRENTS
switch_input.c:793:#if BED_BENCH_PROVOKE_RUNAWAY
switch_input.c:871:#if !BED_CAPTURE_MOTOR_CURRENTS
switch_input.c:874:#if BED_BENCH_PROVOKE_RUNAWAY
switch_input.c:945:#if !BED_CAPTURE_MOTOR_CURRENTS
ble_bed_service.h:38:#if BLE_DEBUG_WATCHDOG_LOG
debug_capture.h:31:#if BED_CAPTURE_MOTOR_CURRENTS
[exit 0]

$ sed -n 45,75p ble_bed_service.c; sed -n 25,40p ble_bed_service.h; sed -n 50,60p switch_input.c; sed -n 90,106p switch_input.c; sed -n 1,30p debug_capture.h
// connection_opened/connection_closed). Originally added to diagnose the
// fresh-pairing bring-up bug; kept on rather than removed now that pairing
// is bench-confirmed working (62773f6 / this session), because each print
// fires once per connection/bonding event - not per tick, so it carries
// none of the timing-loop cost this session's other findings are about -
// and reason/status codes here (e.g. connection_closed's 0x1008) are the
// most useful diagnostic for exactly the class of BLE-drop bug that has
// already bitten this project once. Set to 0 for a quiet production log.
#define BLE_DEBUG_SECURITY_LOG 1

// Bench-only, gated OFF by default (matches debug_capture.h's
// BED_CAPTURE_MOTOR_CURRENTS convention - a hardcoded, source-visible flag
// that must be hand-flipped for a bench session, never left on by accident).
// When 1, wipes the BLE bonding table on every boot so bring-up testing
// always starts from an empty bond table, regardless of what accumulated in
// NVM3 across reflashes (NVM3 survives a reflash; nothing before this
// normally called sl_bt_sm_delete_bondings(), so stale firmware-side bonds
// were accumulating while the phone side kept getting cleared - see the
// AUTH FAIL bench findings). sl_bt_sm_delete_bondings() only touches the
// Bluetooth stack's own bonding database - a distinct NVM3 object from the
// application's settings storage (nvm3_default_config.h / bed_settings.c /
// nvm3_functions.c use their own separate keys) - so this cannot disturb
// calibration/settings data.
// NEVER leave enabled (1) in a normal build - a production unit must keep
// customer bonds across a reset, exactly like it keeps their settings.
#define BLE_BENCH_WIPE_BONDS 0

/*******************************************************************************
 *** Bed Status wire encoding (BLE_GATT_DESIGN.md #6)
 ******************************************************************************/


#include "sl_bt_api.h"
#include "bed_status.h"

// Bench diagnostics for dead-man watchdog stops (the app's "Connection
// stalled" message, Command Result WATCHDOG_STOP). When 1, ble_bed_service.c
// records refresh and Command-write ticks and app.c's app_process_action()
// records its own pass times; one short block is printed on each watchdog
// stop, and the connection_parameters print adds latency and supervision
// timeout. Measures only: nothing blocks and nothing prints per pass. Set to
// 0 before shipping.
#define BLE_DEBUG_WATCHDOG_LOG 1

#if BLE_DEBUG_WATCHDOG_LOG
// Called first and last in app_process_action(), to time each main-loop pass.
void ble_bed_service_debug_pass_begin(void);

// Switch sampler timing (DECISIONS.md #41). Milliseconds via sleeptimer, never
// loop passes (#2).
#define SWITCH_SAMPLE_MS            10
#define SWITCH_DEBOUNCE_SAMPLES     2   // 2 identical reads = >=10 ms stable
#define SWITCH_READ_FAIL_LIMIT      5   // 50 ms of failed reads -> all released
#define SWITCH_STATE_STALE_MS       100 // fail-safe distrusts older data
#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0

// Every SWITCH bit of the packed Port0/Port1 status word, and nothing else.
// Port 0 is all eight single-side switches; Port 1 bits 0-3 are UP_ALL,
 * naturally) silently compiles the trigger sites out, and the provocation
 * does nothing at all. That happened; -Wunused-function caught it.
 *
 * WHY IT MUST NEVER SHIP: a production unit with this enabled would drive a
 * motor pair with nothing held, every time a button was tapped, relying on the
 * fail-safe as its only stop. That is a bed that moves on its own - the exact
 * behaviour this whole subsystem exists to prevent.
 */
#define BED_BENCH_PROVOKE_RUNAWAY 0

// How long after the button release to energise. Long enough that the release
// has been fully processed and every variable is back to idle, so the
// fail-safe faces a real "nothing is commanding" state rather than a race with
// the release path. Milliseconds via sleeptimer (DECISIONS #2).
#define PROVOKE_DELAY_MS 300

// Flags
/*
 * debug_capture.h
 *
 * Bench-only current-capture mode (refactor step C3, DECISIONS.md #5). When
 * BED_CAPTURE_MOTOR_CURRENTS is 1, motor_current_functions.c streams
 * timestamped CSV over the debug UART, and every other printf in the
 * firmware (app.c, auto_level.c, motor_current_functions.c, bed_settings.c)
 * compiles out via "#if !BED_CAPTURE_MOTOR_CURRENTS" - so a PuTTY capture
 * log is pure CSV, loadable directly into Excel with no cleanup. The one
 * exception is a "#COLLISION,<time_ms>" marker line printed if a collision
 * trips mid-capture (DECISIONS.md #4) - the leading '#' makes contaminated
 * captures easy to spot and reject.
 *
 * NEVER leave enabled (1) in a normal build - adds UART TX overhead to the
 * supervision loop and silences all other diagnostic output.
 */

#ifndef DEBUG_CAPTURE_H_
#define DEBUG_CAPTURE_H_

#define BED_CAPTURE_MOTOR_CURRENTS 0

// Zeroes the capture CSV's time_ms column to a new t=0, so each bench test
// (motor/scenario) gets its own timeline without a rebuild or power cycle.
// Called on every button press/auto-level start (app_move_start(),
// app_auto_level_start() in app.c) - reset at the START of each new test, not
// its release, so the idle time between releasing one button and pressing
// the next doesn't get baked into the next test's first timestamp.
// motor_current_functions.c owns the real clock. No-op when capture mode is
// off.
[exit 0]

$ grep -rn -i 'initial testing\|duty\|PWM_\|sl_pwm_set\|100%' *.c *.h | cut -c1-220
app.c:50:#include "sl_pwm_instances.h"
app.c:416:  int PWM_duty = 100;
app.c:418:  sl_pwm_set_duty_cycle(&sl_pwm_motor1, PWM_duty);
app.c:419:  sl_pwm_set_duty_cycle(&sl_pwm_motor2, PWM_duty);
app.c:420:  sl_pwm_set_duty_cycle(&sl_pwm_motor3, PWM_duty);
app.c:421:  sl_pwm_set_duty_cycle(&sl_pwm_motor4, PWM_duty);
app.c:423:  // // Start motor PWM's - only for initial testing, then remove
app.c:424:  sl_pwm_start(&sl_pwm_motor1);
app.c:425:  sl_pwm_start(&sl_pwm_motor2);
app.c:426:  sl_pwm_start(&sl_pwm_motor3);
app.c:427:  sl_pwm_start(&sl_pwm_motor4);
ble_bed_service.c:184:// This same period does double duty as the app's "has stopped commanding"
exp_board.c:306:// any duty cycle - the enable bit is what gates motion, which makes this the
[exit 0]

$ cat -n nvm3_functions.h bed_settings.h bed_types.h | grep -v '^ *[0-9]*\s*$'
     1	/*
     2	 * nvm3_variables.h
     3	 *
     4	 *  Created on: Nov 24, 2024
     5	 *      Author: Andrew Duncan
     6	 */
     8	#ifndef NVM3_FUNCTIONS_H_
     9	#define NVM3_FUNCTIONS_H_
    11	#include <stdint.h>
    13	#include "nvm3_default.h"
    14	#include "nvm3_functions.h"
    16	#define CUSTOM_ROLL_ANGLE_KEY           1
    17	#define CUSTOM_PITCH_ANGLE_KEY          2
    18	#define RAISE_COLLISION_SENSITIVITY_KEY 3
    19	#define LOWER_COLLISION_SENSITIVITY_KEY 4
    21	Ecode_t storeValue(uint32_t key, int16_t value);
    22	Ecode_t readValue(uint32_t key, int16_t *value);
    23	Ecode_t eraseValue(uint32_t key);
    24	Ecode_t eraseAllStoredValues(void);
    25	Ecode_t eraseStoredCollisionThresholds(void);
    26	bool isValueStored(uint32_t key);
    28	#endif /* NVM3_FUNCTIONS_H_ */
    29	/*
    30	 * bed_settings.h
    31	 *
    32	 * Single writer for every runtime-adjustable, NVM3-backed setting.
    33	 * Firmware-enforced clamping is the safety mechanism here: every setter
    34	 * validates against the setting's min/max before applying or persisting,
    35	 * and rejects (does not silently clamp) out-of-range values.
    36	 *
    37	 *  Created on: Jul 6, 2026
    38	 */
    40	#ifndef BED_SETTINGS_H_
    41	#define BED_SETTINGS_H_
    43	#include <stdint.h>
    44	#include <stdbool.h>
    46	typedef enum {
    47	  BED_SETTING_BASELINE_ROLL_ANGLE,
    48	  BED_SETTING_BASELINE_PITCH_ANGLE,
    49	  BED_SETTING_RAISE_COLLISION_MARGIN,
    50	  BED_SETTING_LOWER_COLLISION_MARGIN,
    51	  BED_SETTING_COUNT
    52	} bed_setting_id_t;
    54	// Loads every setting from NVM3 at startup. Missing or unreadable values
    55	// fall back to their default; stored values outside the setting's min/max
    56	// are rejected (logged) and the default is used for this session - NVM3
    57	// itself is not rewritten on a rejected load.
    58	void bed_settings_init(void);
    60	// Validates value against the setting's min/max; if in range, applies it to
    61	// the live variable and persists it to NVM3, returning true. If out of
    62	// range, rejects it (logged) and leaves the live value and NVM3 untouched,
    63	// returning false. If NVM3 write itself fails, the live value is still
    64	// applied but false is returned.
    65	bool bed_settings_set(bed_setting_id_t id, int16_t value);
    67	// Deletes the setting's NVM3 object only - matches eraseStoredAngles()'s
    68	// existing behavior: the live value is unchanged until next boot/reload.
    69	bool bed_settings_erase(bed_setting_id_t id);
    71	int16_t bed_settings_get_default(bed_setting_id_t id);
    72	int16_t bed_settings_get_min(bed_setting_id_t id);
    73	int16_t bed_settings_get_max(bed_setting_id_t id);
    75	#endif /* BED_SETTINGS_H_ */
    76	/*
    77	 * bed_types.h
    78	 *
    79	 * Shared value types used across the bed control modules (app, auto_level,
    80	 * dual_motor_control, motor_current_functions), consolidated to a single
    81	 * definition each.
    82	 */
    84	#ifndef BED_TYPES_H_
    85	#define BED_TYPES_H_
    87	#define NUM_MOTORS 4
    88	#define NUM_AXES 2 // Pitch and roll
    89	#define SCALE_FACTOR 100
    91	typedef enum {
    92	    ROLL,
    93	    PITCH
    94	} axis_t;
    96	typedef enum {
    97	    HEAD,
    98	    LEFT,
    99	    RIGHT,
   100	    FOOT
   101	} side_of_bed_t;
   103	typedef enum active_switch {
   104	  SWITCH_UP_ALL_VALUE,
   105	  SWITCH_AUTO_LEVEL_VALUE,
   106	  SWITCH_DOWN_ALL_VALUE,
   107	  SWITCH_DOWN_HEAD_VALUE,
   108	  SWITCH_DOWN_LEFT_VALUE,
   109	  SWITCH_DOWN_RIGHT_VALUE,
   110	  SWITCH_DOWN_FOOT_VALUE,
   111	  SWITCH_UP_HEAD_VALUE,
   112	  SWITCH_UP_LEFT_VALUE,
   113	  SWITCH_UP_RIGHT_VALUE,
   114	  SWITCH_UP_FOOT_VALUE,
   115	  NUM_MOTOR_SWITCHES,
   116	  SWITCH_BED_LIGHT_VALUE,
   117	  OFF = -1
   118	} active_switch_t;
   120	#endif /* BED_TYPES_H_ */
[exit 0]

$ ls config/btconf/ 2>&1; grep -rln 'gattdb_\|characteristic' autogen config --include=*.h --include=*.c --include=*.xml | head; grep -n 'gattdb_' autogen/gatt_db.h
gatt_configuration.btconf
slc_args.json
autogen/gatt_db.c
autogen/gatt_db.h
autogen/sbom/cyclonedx_bom.xml
8:#include "sli_bt_gattdb_def.h"
10:extern const sli_bt_gattdb_t gattdb;
12:#define gattdb_generic_attribute              1
13:#define gattdb_service_changed_char           3
14:#define gattdb_database_hash                  6
15:#define gattdb_client_support_features        8
16:#define gattdb_device_name                    11
17:#define gattdb_device_information             14
18:#define gattdb_manufacturer_name_string       16
19:#define gattdb_model_number_string            18
20:#define gattdb_hardware_revision_string       20
21:#define gattdb_firmware_revision_string       22
22:#define gattdb_system_id                      24
23:#define gattdb_tilt_bed_service               25
24:#define gattdb_command                        27
25:#define gattdb_command_result                 29
26:#define gattdb_bed_status                     32
27:#define gattdb_client_role                    35
28:#define gattdb_protocol_version               38
30:#define gattdb_generic_attribute_len          2
31:#define gattdb_service_changed_char_len       4
32:#define gattdb_database_hash_len              16
33:#define gattdb_client_support_features_len    1
34:#define gattdb_device_name_len                13
35:#define gattdb_device_information_len         2
36:#define gattdb_manufacturer_name_string_len   12
37:#define gattdb_model_number_string_len        10
38:#define gattdb_hardware_revision_string_len   5
39:#define gattdb_firmware_revision_string_len   8
40:#define gattdb_system_id_len                  8
41:#define gattdb_tilt_bed_service_len           16
42:#define gattdb_command_len                    4
43:#define gattdb_command_result_len             2
44:#define gattdb_bed_status_len                 20
45:#define gattdb_protocol_version_len           1
[exit 0]

$ grep -n -i 'characteristic\|uuid=\|properties\|<service\|name=' config/btconf/gatt_configuration.btconf | cut -c1-200
3:<gatt gatt_caching="true" generic_attribute_service="true" header="gatt_db.h" name="Custom BLE GATT" out="gatt_db.c" prefix="gattdb_">
6:  <service advertise="false" name="Generic Access" requirement="mandatory" sourceId="org.bluetooth.service.generic_access" type="primary" uuid="1800">
7:    <informativeText>Abstract: The generic_access service contains generic information about the device. All available Characteristics are readonly. </informativeText>
10:    <characteristic const="false" id="device_name" name="Device Name" sourceId="org.bluetooth.characteristic.gap.device_name" uuid="2A00">
12:      <properties>
15:      </properties>
16:    </characteristic>
19:    <characteristic const="true" name="Appearance" sourceId="org.bluetooth.characteristic.gap.appearance" uuid="2A01">
22:      <properties>
24:      </properties>
25:    </characteristic>
29:  <service advertise="false" id="device_information" name="Device Information" requirement="mandatory" sourceId="org.bluetooth.service.device_information" type="primary" uuid="180A">
33:    <characteristic const="true" id="manufacturer_name_string" name="Manufacturer Name String" sourceId="org.bluetooth.characteristic.manufacturer_name_string" uuid="2A29">
34:      <informativeText>Abstract: The value of this characteristic is a UTF-8 string representing the name of the manufacturer of the device.</informativeText>
36:      <properties>
38:      </properties>
39:    </characteristic>
42:    <characteristic const="false" id="model_number_string" name="Model Number String" sourceId="org.bluetooth.characteristic.model_number_string" uuid="2A24">
43:      <informativeText>Abstract: The value of this characteristic is a UTF-8 string representing the model number assigned by the device vendor.</informativeText>
45:      <properties>
47:      </properties>
48:    </characteristic>
51:    <characteristic const="false" id="hardware_revision_string" name="Hardware Revision String" sourceId="org.bluetooth.characteristic.hardware_revision_string" uuid="2A27">
52:      <informativeText>Summary: The value of this characteristic is a UTF-8 string representing the hardware revision for the hardware within the device.</informativeText>
54:      <properties>
56:      </properties>
57:    </characteristic>
60:    <characteristic const="false" id="firmware_revision_string" name="Firmware Revision String" sourceId="org.bluetooth.characteristic.firmware_revision_string" uuid="2A26">
61:      <informativeText>Summary: The value of this characteristic is a UTF-8 string representing the firmware revision for the firmware within the device.</informativeText>
63:      <properties>
65:      </properties>
66:    </characteristic>
69:    <characteristic const="false" id="system_id" name="System ID" sourceId="org.bluetooth.characteristic.system_id" uuid="2A23">
70:      <informativeText>Abstract: The SYSTEM ID characteristic consists of a structure with two fields. The first field are the LSOs and the second field contains the MSOs. This is a 64-bit structur
72:      <properties>
74:      </properties>
75:    </characteristic>
79:  <service advertise="true" id="tilt_bed_service" name="Tilt Bed Service" requirement="mandatory" sourceId="" type="primary" uuid="54b10000-ef39-4f03-a91d-eb12b0d067c9">
82:    <characteristic const="false" id="command" name="Command" sourceId="" uuid="54b10001-ef39-4f03-a91d-eb12b0d067c9">
84:      <properties>
86:      </properties>
87:    </characteristic>
90:    <characteristic const="false" id="command_result" name="Command Result" sourceId="" uuid="54b10002-ef39-4f03-a91d-eb12b0d067c9">
92:      <properties>
94:      </properties>
95:    </characteristic>
98:    <characteristic const="false" id="bed_status" name="Bed Status" sourceId="" uuid="54b10003-ef39-4f03-a91d-eb12b0d067c9">
100:      <properties>
103:      </properties>
104:    </characteristic>
107:    <characteristic const="false" id="client_role" name="Client Role" sourceId="" uuid="54b10004-ef39-4f03-a91d-eb12b0d067c9">
109:      <properties>
112:      </properties>
113:    </characteristic>
116:    <characteristic const="true" id="protocol_version" name="Protocol Version" sourceId="" uuid="54b10006-ef39-4f03-a91d-eb12b0d067c9">
118:      <properties>
120:      </properties>
121:    </characteristic>
[exit 0]

$ grep -n '^#' docs/BLE_GATT_DESIGN.md; grep -n '0005\|54b10005' docs/BLE_GATT_DESIGN.md | cut -c1-220
1:# BLE_GATT_DESIGN.md
15:## 1. Design goals (ranked)
31:## 2. SDK and stack configuration
59:## 3. Service topology
69:### Tilt Bed Service characteristics
84:## 4. Command characteristic (0001)
178:## 5. Dead-man watchdog (new safety element, firmware-side)
207:## 6. Bed Status characteristic (0003)
236:### Tilt sign conventions
273:### Protocol version and this change
317:## 7. Command Result characteristic (0002)
334:## 8. Connections, roles, bonding, fast connect
379:## 9. Settings RPC characteristic (0005)
405:## 10. OTA (D2, designed now)
421:## 11. Firmware module plan (D1)
443:## 12. Bench verification plan (append to REGRESSION.md at D1)
77:| 5 | Settings RPC | `0005` | Write + Indicate | 4 B req / 10 B resp | both |
379:## 9. Settings RPC characteristic (0005)
[exit 0]

$ grep -n 'bool ui_read_inputs\|EFM_ASSERT\|I2CSPM_Transfer\|void set_LED_on\|void set_LED_off\|redriver_acquire\|redriver_release\|ADDRESS' exp_ui.c exp_ui.h | cut -c1-200
exp_ui.c:22:// UI_AUX_I2C_BUS_ADDRESS / UI_SWITCH_I2C_BUS_ADDRESS live in exp_ui.h.
exp_ui.c:30:// Number of holders of the off-board bus re-drivers (see ui_redriver_acquire()).
exp_ui.c:83:  ui_redriver_acquire();
exp_ui.c:86:  ret = I2CSPM_Transfer(UI_I2C_DEVICE, &seq);
exp_ui.c:89:  ui_redriver_release();
exp_ui.c:103:  // EFM_ASSERT(ret == i2cTransferDone);
exp_ui.c:112:  // EFM_ASSERT(ret == i2cTransferDone);
exp_ui.c:125:  // EFM_ASSERT(ret == i2cTransferDone);
exp_ui.c:134:  // EFM_ASSERT(ret == i2cTransferDone);
exp_ui.c:175:  // EFM_ASSERT(ret == i2cTransferDone);
exp_ui.c:179:  // EFM_ASSERT(ret == i2cTransferDone);
exp_ui.c:185:  // EFM_ASSERT(ret == i2cTransferDone);
exp_ui.c:191:  // EFM_ASSERT(ret == i2cTransferDone);
exp_ui.c:199:  // EFM_ASSERT(ret == i2cTransferDone);
exp_ui.c:203:  // EFM_ASSERT(ret == i2cTransferDone);
exp_ui.c:211:void ui_redriver_acquire(void)
exp_ui.c:221:void ui_redriver_release(void)
exp_ui.c:235:bool ui_read_inputs(uint8_t board_addr, uint16_t *status)
exp_ui.c:252:void set_LED_on(led_color_t LED_color)
exp_ui.c:255:  ui_target = UI_AUX_I2C_BUS_ADDRESS;
exp_ui.c:257:  ui_target = UI_SWITCH_I2C_BUS_ADDRESS;
exp_ui.c:264:void set_LED_off(led_color_t LED_color)
exp_ui.c:267:  ui_target = UI_AUX_I2C_BUS_ADDRESS;
exp_ui.c:269:  ui_target = UI_SWITCH_I2C_BUS_ADDRESS;
exp_ui.c:284:  ui_target = UI_AUX_I2C_BUS_ADDRESS;
exp_ui.c:286:  ui_target = UI_SWITCH_I2C_BUS_ADDRESS;
exp_ui.h:19:#define UI_AUX_I2C_BUS_ADDRESS     0x20 // Aux Board
exp_ui.h:20:#define UI_SWITCH_I2C_BUS_ADDRESS  0x21 // Switch Board
exp_ui.h:46:bool ui_read_inputs(uint8_t board_addr, uint16_t *status);
exp_ui.h:54:void ui_redriver_acquire(void);
exp_ui.h:55:void ui_redriver_release(void);
exp_ui.h:57:void set_LED_on(led_color_t LED_color);
exp_ui.h:58:void set_LED_off(led_color_t LED_color);
[exit 0]

$ grep -rn 'TIMEOUT\|I2CSPM_TRANSFER' config/sl_i2cspm_*.h 2>&1 | head; grep -rn 'SL_BT_CONFIG_MAX_CONNECTIONS\b\|define SL_BT_CONFIG_MAX_CONNECTIONS' config/ autogen/ | head
config/sl_bluetooth_config.h:44:#define SL_BT_CONFIG_MAX_CONNECTIONS (0)
config/sl_bluetooth_config.h:48:#define SL_BT_CONFIG_MAX_CONNECTIONS_SUM     (SL_BT_CONFIG_MAX_CONNECTIONS + SL_BT_COMPONENT_CONNECTIONS)
config/sl_bluetooth_connection_config.h:35:// <o SL_BT_CONFIG_MAX_CONNECTIONS> Max number of connections reserved for user <0-32>
config/sl_bluetooth_connection_config.h:38:#define SL_BT_CONFIG_MAX_CONNECTIONS     (4)
[exit 0]

$ sed -n 36,50p config/sl_bluetooth_config.h; sed -n 30,40p config/sl_bluetooth_connection_config.h; grep -n 'BT_COMPONENT_CONNECTIONS' -r config autogen | head -5

// <<< Use Configuration Wizard in Context Menu >>>

// <h> Bluetooth Stack Configuration

#ifdef SL_CATALOG_BLUETOOTH_FEATURE_CONNECTION_PRESENT
#include "sl_bluetooth_connection_config.h"
#else
#define SL_BT_CONFIG_MAX_CONNECTIONS (0)
#endif

// Max number of connections reserved by user application and components
#define SL_BT_CONFIG_MAX_CONNECTIONS_SUM     (SL_BT_CONFIG_MAX_CONNECTIONS + SL_BT_COMPONENT_CONNECTIONS)

#ifdef SL_CATALOG_BLUETOOTH_FEATURE_ADVERTISER_PRESENT

#ifndef SL_BT_CONNECTION_CONFIG_H
#define SL_BT_CONNECTION_CONFIG_H

// <<< Use Configuration Wizard in Context Menu >>>
// <o SL_BT_CONFIG_MAX_CONNECTIONS> Max number of connections reserved for user <0-32>
// <i> Default: 4
// <i> Define the number of connections the application needs.
#define SL_BT_CONFIG_MAX_CONNECTIONS     (4)

// <o SL_BT_CONFIG_CONNECTION_DATA_LENGTH> Preferred maximum TX payload octets <27-251>
config/sl_bluetooth_config.h:48:#define SL_BT_CONFIG_MAX_CONNECTIONS_SUM     (SL_BT_CONFIG_MAX_CONNECTIONS + SL_BT_COMPONENT_CONNECTIONS)
autogen/sl_bluetooth.h:41:#define SL_BT_COMPONENT_CONNECTIONS 0
[exit 0]

$ grep -n -i 'i2cspm.*timeout\|300 s\|300s\|300000\|I2CSPM_TRANSFER_TIMEOUT' docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md docs/SESSION_LOG.md | cut -c1-240 | head -20
docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:520:passes `SL_I2CSPM_TRANSFER_TIMEOUT` = 300000 (`sl_i2cspm.c:48`, commented
docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:524:up to **300 s**. An absent device just NACKs (~0.1 ms) — that is not a hang.
docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:2315:#define SL_I2CSPM_TRANSFER_TIMEOUT 300000
docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:2316:  uint32_t timeout_ms = SL_I2CSPM_TRANSFER_TIMEOUT;
docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:2583:| V14 | I2CSPM passes 300000 as a millisecond timeout | d.4 `sl_i2cspm.c:47-48,132,165`; `sl_i2c.h:353` |
docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md:2644:5. **300 s I2C timeout.** SDK 2026.6.0's I2CSPM passes 300000 as
docs/SESSION_LOG.md:68:  `I2CSPM_Transfer()` silently spinning its 300000-iteration retry loop past
docs/SESSION_LOG.md:1340:- SDK I2CSPM in 2026.6.0 passes `SL_I2CSPM_TRANSFER_TIMEOUT` 300000 to `sl_i2c_*_blocking()` whose timeout is in **milliseconds** (`sl_i2cspm.c:48,132`; `sl_i2c.h:353`) — a hung bus could block a pass for up to 30
docs/SESSION_LOG.md:1356:- QUESTIONS for the reviewer (handoff f): the extra "longest between passes" field; the BT-off test; B's wording vs. what it does; app Step 1 vs physical Step 1 pass rate; the 300 s I2C timeout; the `bed_control.c:7
docs/SESSION_LOG.md:1420:5. The 300 s I2CSPM timeout (`SL_I2CSPM_TRANSFER_TIMEOUT` 300000 passed as
docs/SESSION_LOG.md:1427:   (MIGRATION_STATUS.md, Phase 6); the I2CSPM 300 s timeout.
docs/SESSION_LOG.md:1461:- Pre-ship: `BLE_DEBUG_WATCHDOG_LOG` and `SWITCH_SAMPLER_DIAG` -> 0; Release-config pass; I2CSPM 300 s timeout. `.projectlinkstore` still modified, never committed.
[exit 0]

$ grep -n -i 'averta\|font\|app store\|flutter_reactive_ble\|debug menu\|kotlin\|scan.throttl\|0x0005 subscription' docs/*.md | grep -v '^docs/SESSION_LOG' | cut -c1-200 | head -20
docs/APP_KICKOFF_PROMPT.md:52:     it must be selectable at runtime from a debug menu.
docs/APP_SETUP_GUIDE.md:108:1. **App Store Connect** (https://appstoreconnect.apple.com):
docs/APP_SETUP_GUIDE.md:109:   - Users and Access → Integrations → **App Store Connect API** → generate
docs/APP_SETUP_GUIDE.md:126:   as an internal tester in App Store Connect → your app → TestFlight.
[exit 0]

$ grep -n -i 'release.config\|release build\|release-config' docs/*.md | grep -v '^docs/SESSION_LOG' | cut -c1-200 | head -20
docs/APP_KICKOFF_PROMPT.md:108:  (secrets stay in Codemagic UI, never the repo), Android release config,
docs/MIGRATION_STATUS.md:46:      the **Release-config confirmation pass is still owed before Stage C
docs/MIGRATION_STATUS.md:69:      current firmware and the Release-config pass (Phase 6 above) have not
docs/MIGRATION_STATUS.md:123:   Release-config confirmation pass before Stage C ships.
docs/REGRESSION_RECORD_PRE_STAGEC.md:38:all, in Release. **A Release-config confirmation pass is still owed before
[exit 0]

$ grep -n -i 'no timeout\|window.*timeout\|timeout.*window\|4 connections\|four connections\|MAX_CONNECTIONS' docs/DECISIONS.md docs/BLE_GATT_DESIGN.md docs/handoffs/*.md | cut -c1-220 | head -20
docs/DECISIONS.md:1971:  `SL_BT_CONFIG_MAX_CONNECTIONS_SUM`; it stores handles rather than being
docs/BLE_GATT_DESIGN.md:46:- `SL_BT_CONFIG_MAX_CONNECTIONS = 2` (control + viewer, #14).
docs/handoffs/fw_HANDOFF_2026-10-18_01_switch-followups-settings-exit-pairing-window.md:1427:5. **No window timeout.** Decision 3 says no timer, implemented as such. Flagging only: a forgotten window stays open until a p
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:105:- The stack's connection count: `SL_BT_CONFIG_MAX_CONNECTIONS_SUM = SL_BT_CONFIG_MAX_CONNECTIONS + SL_BT_COMPONENT_CONNECTIONS` (`
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:109:1. **State** (`ble_bed_service.c:196-204`): `static uint8_t refused_pairing_conn[SL_BT_CONFIG_MAX_CONNECTIONS_SUM];`. Each entry i
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:110:   - To include `SL_BT_CONFIG_MAX_CONNECTIONS_SUM`, the file now includes `sl_bluetooth.h` (autogen), which includes `sl_bluetooth
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:595:+#include "sl_bluetooth.h"            // SL_BT_CONFIG_MAX_CONNECTIONS_SUM - sizes refused_pairing_conn[]
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:611:+static uint8_t refused_pairing_conn[SL_BT_CONFIG_MAX_CONNECTIONS_SUM];
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:622:+  for (uint8_t i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS_SUM; i++) {
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:627:+  for (uint8_t i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS_SUM; i++) {
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:640:+  for (uint8_t i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS_SUM; i++) {
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:650:+  for (uint8_t i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS_SUM; i++) {
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:819:+  `SL_BT_CONFIG_MAX_CONNECTIONS_SUM`; it stores handles rather than being
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:1526:35:#include "sl_bluetooth.h"            // SL_BT_CONFIG_MAX_CONNECTIONS_SUM - sizes refused_pairing_conn[]
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:1527:204:static uint8_t refused_pairing_conn[SL_BT_CONFIG_MAX_CONNECTIONS_SUM];
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:1597:$ grep -n "define SL_BT_CONFIG_MAX_CONNECTIONS\b\|define SL_BT_CONFIG_MAX_CONNECTIONS \|MAX_CONNECTIONS_SUM\|define SL_BT_COMPONE
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:1598:config/sl_bluetooth_config.h:44:#define SL_BT_CONFIG_MAX_CONNECTIONS (0)
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:1599:config/sl_bluetooth_config.h:48:#define SL_BT_CONFIG_MAX_CONNECTIONS_SUM     (SL_BT_CONFIG_MAX_CONNECTIONS + SL_BT_COMPONENT_CONN
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:1600:config/sl_bluetooth_connection_config.h:38:#define SL_BT_CONFIG_MAX_CONNECTIONS     (4)
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:2013:8. **Two role slots vs four stack connections.** The stack allows 4 connections (`SL_BT_CONFIG_MAX_CONNECTIONS 4`), but `open_con
[exit 0]

$ grep -n -i 'unplug\|board absent\|no switch board\|per-board' docs/DECISIONS.md docs/SWITCH_SAMPLER_DESIGN.md docs/handoffs/fw_HANDOFF_2026-10-17_05_one-switch-reader-sampler.md docs/handoffs/fw_HANDOFF_2026-10-18_01_switch-followups-settings-exit-pairing-window.md | cut -c1-220 | head -15
[exit 0]

$ grep -n '^## 29\|^## 17\|^## 33\|Stage B5\|B5' docs/DECISIONS.md | cut -c1-200 | head -15; grep -n 'P1.5' docs/REFACTOR_PLAN.md | head -3; grep -n -i 'Note A' docs/SETPOINTS_INVENTORY.md | head -3
10:## 1. Collision detection architecture (design brief for refactor step B5)
66:## 5. Data-capture tool — build early, before B5
71:Build this **before** starting refactor step B5 so the thresholds that ship
122:## 11. Direction-aware per-pair supervision — deferred to after B5
127:sequenced **after B5** (the supervision deduplication) so it is not
152:refactor. Until it lands, B5 keeps existing behavior: auto-level
196:`run_motor_supervision()` (B5) runs in the main tick, beneath the command
230:## 17. Continuous tilt sampling — deferred to app era (recorded during C3)
631:## 29. Auto-level's filter constants are calibrated in loop passes, not time — and the same problem reaches the collision-detection safety path
851:## 33. Live tilt telemetry — separate sampler, four triggers, and the first Command write path
29:| app.c:61 | `RAISE_OFF_SENSITIVITY` | 10000 | "Off" rung: margin so large it never trips while raising | — (see note A) | 10000 | — | — | ⚠️ |
33:| app.c:65 | `LOWER_OFF_SENSITIVITY` | 750 | "Off" rung while lowering — **not actually off**, trips at 750 mA (raising OFF is 10000) | — (see note A) | 750 | — | — | ⚠️ |
50:**Note A — "OFF" rungs.** Exposing a true collision-off to a phone app is a
[exit 0]

$ grep -n -i 'lights.*supervision\|supervision.*lights\|BED LIGHTS hold\|BED_LIGHT.*run_motor' docs/DECISIONS.md docs/handoffs/*.md docs/SESSION_LOG.md | cut -c1-220 | head -10
docs/handoffs/fw_HANDOFF_2026-10-17_05_one-switch-reader-sampler.md:1986:- **Everything about behaviour on the bed.** No bench run, no flash, no host test. In particular: that the sampler detects press/release correctly;
docs/handoffs/fw_HANDOFF_2026-10-18_01_switch-followups-settings-exit-pairing-window.md:235:All of this is what the code should print; none of it has been observed. Build as committed (`BED_BENCH_PROVOKE_RUNAWAY 0`, `SWI
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:60:**Another observation, not acted on.** During each 5 s BED LIGHTS entry hold (lines 72-77 and 156-161), four `Max Current for Motor
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:196:| `:505` | dead-man watchdog expiry (`service_move_watchdog()`) | Practically no. The watchdog is armed only by an *accepted* MOVE
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:1999:- That C4 does not stop a normal 5 s BED LIGHTS hold from entering settings mode. Reasoned from the sampler delivering edges from
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:2001:- That the near-zero `Max Current` lines during the BED LIGHTS entry hold come from `run_motor_supervision()` on `active_switch =
docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md:2012:7. **Max Current lines during the BED LIGHTS entry hold** (b.1): `run_motor_supervision()` appears to run, calibrating current th
[exit 0]

$ ls cmake_gcc; find cmake_gcc -maxdepth 4 \( -name '*.out' -o -name '*.map' -o -name '*.elf' -o -name '*.hex' -o -name '*.s37' \) -exec ls -l --time-style=long-iso {} \;
CMakeLists.txt
CMakePresets.json
autogen_toolchain.cmake
build
tilt_bed_controller.cmake
tilt_bed_controller_project.cmake
toolchain.cmake
-rw-r--r-- 1 adunc 197610 691327 2026-11-01 15:38 cmake_gcc/build/base/tilt_bed_controller.hex
-rw-r--r-- 1 adunc 197610 5640483 2026-11-01 15:38 cmake_gcc/build/base/tilt_bed_controller.map
-rw-r--r-- 1 adunc 197610 2087384 2026-11-01 15:38 cmake_gcc/build/base/tilt_bed_controller.out
-rw-r--r-- 1 adunc 197610 706710 2026-11-01 15:38 cmake_gcc/build/base/tilt_bed_controller.s37
[exit 0]

$ git log -4 --format='%h %ad %s' --date=iso; which arm-none-eabi-size 2>&1; grep -n -i 'arm-none-eabi\|TOOLCHAIN\|GCC_PATH\|ARM_GCC' cmake_gcc/toolchain.cmake | head
6796e6b 2026-11-01 15:45:35 -0800 Handoff 2026-11-01 01: fix one section cross-reference (d.5 -> d.4)
63acb4d 2026-11-01 15:45:24 -0800 Handoff 2026-11-01 01: Prompt AA (sleep veto during app holds)
75b0a61 2026-11-01 15:39:19 -0800 Keep the CPU in EM0 during app holds; hold-end diagnostic line (DECISIONS #45)
0993ee9 2026-11-01 15:37:11 -0800 Docs: 2026-11-01 auto-level stall bench record, Prompt Y rulings
which: no arm-none-eabi-size in (/c/Users/adunc/bin:/mingw64/bin:/usr/local/bin:/usr/bin:/bin:/mingw64/bin:/usr/bin:/c/Users/adunc/bin:/c/WINDOWS/system32:/c/WINDOWS:/c/WINDOWS/System32/Wbem:/c/WINDOWS/System32/WindowsPowerShell/v1.0:/c/WINDOWS/System32/OpenSSH:/c/Program Files/dotnet:/c/Program Files/Microsoft SQL Server/150/Tools/Binn:/c/Program Files/PuTTY:/cmd:/mingw64/bin:/usr/bin:/c/MinGW/bin:/c/Program Files/UVtools:/c/Program Files (x86)/Microsoft SQL Server/90/Tools/binn:/c/Program Files/NVIDIA Corporation/NVIDIA App/NvDLISR:/c/Program Files/Sunshine:/c/Program Files/Sunshine/tools:/c/Program Files (x86)/NVIDIA Corporation/PhysX/Common:/c/Users/adunc/AppData/Local/Programs/Python/Python312/Scripts:/c/Users/adunc/AppData/Local/Programs/Python/Python312:/c/Users/adunc/AppData/Local/Microsoft/WindowsApps:/c/src/flutter/bin:/c/Users/adunc/AppData/Local/Programs/Microsoft VS Code/bin:/c/Users/adunc/AppData/Local/GitHubDesktop/bin:/c/Users/adunc/.local/bin:/c/Users/adunc/AppData/Local/Android/Sdk/platform-tools:%USERPROFILE%/AppData/Local/Microsoft/WindowsApps:/c/Users/adunc/AppData/Local/Programs/Python/Python312:/usr/bin/vendor_perl:/usr/bin/core_perl)
14:# Users can manually override any toolchain path using the environment
16:set(AUTOGEN_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/autogen_toolchain.cmake")
17:if (EXISTS "${AUTOGEN_TOOLCHAIN_FILE}")
18:include("${AUTOGEN_TOOLCHAIN_FILE}")
21:if (DEFINED ENV{ARM_GCC_DIR})
22:  set(TOOLCHAIN_DIR "$ENV{ARM_GCC_DIR}/bin/")
23:elseif (DEFAULT_TOOLCHAIN_DIR)
24:  set(TOOLCHAIN_DIR ${DEFAULT_TOOLCHAIN_DIR})
51:if (NOT DEFINED TOOLCHAIN_DIR OR NOT DEFINED POST_BUILD_EXE OR NOT DEFINED NINJA_RUNTIME_PATH)
54:    "The required Toolchain, Post-build, or Ninja runtime paths are not set.\n"
[exit 0]

$ grep -n 'DEFAULT_TOOLCHAIN_DIR' cmake_gcc/autogen_toolchain.cmake; ls -l --time-style=full-iso app.c ble_bed_service.c ble_bed_service.h cmake_gcc/build/base/tilt_bed_controller.out | cut -c40-; git diff --stat 75b0a61 HEAD -- '*.c' '*.h' config autogen cmake_gcc
26:  set(DEFAULT_TOOLCHAIN_DIR "${TOOLCHAIN_SLT_PATH}/bin/")
28:  set(DEFAULT_TOOLCHAIN_DIR "${USER_DIR}/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/")
11-01 15:38:06.180686600 -0800 app.c
11-01 15:38:06.045439500 -0800 ble_bed_service.c
11-01 15:38:33.816820800 -0800 ble_bed_service.h
11-01 15:38:34.587869900 -0800 cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

$ ls ~/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/ | grep size; ~/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size -A cmake_gcc/build/base/tilt_bed_controller.out; ~/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size -B cmake_gcc/build/base/tilt_bed_controller.out
arm-none-eabi-size.exe
cmake_gcc/build/base/tilt_bed_controller.out  :
section                   size         addr
.vectors                   320            0
.stack                    2752    536870912
.bss                     11904    536873664
text_application_ram       480    536885568
.text                   244180          800
.ARM.exidx                   8       244980
.copy.table                 12       244988
.zero.table                  0       245000
.data                      768    536886048
.memory_manager_heap     16864    536886816
.nvm                     40960   4294451200
.ARM.attributes             54            0
.comment                   149            0
.debug_line_str            612            0
.debug_info             688606            0
.debug_abbrev            97237            0
.debug_aranges           16792            0
.debug_line             312621            0
.debug_str              177887            0
.debug_loclists         206201            0
.debug_rnglists          22227            0
.debug_frame             47148            0
Total                  1887782


   text	   data	    bss	    dec	    hex	filename
 244988	    780	  72480	 318248	  4db28	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

$ ls autogen/*.ld; grep -n 'MEMORY\|FLASH\|RAM\|ORIGIN\|LENGTH\|linker_nvm\|nvm' autogen/linkerfile.ld | head -40
autogen/linkerfile.ld
30: MEMORY
32:   FLASH      (rx)  : ORIGIN = 0x0, LENGTH = 0x7e000
33:   RAM        (rwx) : ORIGIN = 0x20000000, LENGTH = 0x8000
36:   DUMMY      (!ax) : ORIGIN = 0xfff82000, LENGTH = 0x7e000
46: * ----- START OF APPLICATION FLASH -----
48: * R X - RAM functions (copied to RAM at startup)
56: * RW  - Initialised RAM data (copied to RAM at startup)
59: * ----- END OF APPLICATION FLASH -----
61: * ----- START OF APPLICATION RAM -----
65: * RW  - BSS (zero-init RAM)
66: * RW  - uninitialised RAM
67: * R X - RAM functions
68: * RW  - Initialised RAM
69: * RW  - Heap (automatically expanded to end of RAM)
70: * ----- END OF APPLICATION RAM -----
79: * data loaded changes type. That means e.g. we need three segments for all RAM
85:  vectors PT_LOAD FLAGS(4);       /* Flash content before RAM functions is R */
86:  readonly PT_LOAD FLAGS(4);      /* Readonly content after RAM functions */
91:  ram_pre_code PT_LOAD FLAGS(6);  /* All RAM content before RAMfuncs is RW */
92:  ram_code PT_LOAD FLAGS(5);      /* RAMfuncs need to be RX */
93:  ram_data PT_LOAD FLAGS(6);      /* Relocatable RAM data needs own segment */
94:  ram_post_data PT_LOAD FLAGS(6); /* All RAM content after data is RW */
111:  } > FLASH :vectors
122:  } > RAM :ram_pre_code
132:  } > RAM :ram_pre_code
137:  } > RAM :ram_pre_code
149:  } > RAM AT > FLASH :ram_code
156:  } > FLASH :readonly
185:  } > FLASH :text
190:  } > FLASH :text
196:  } > FLASH :text
216:  } > FLASH :tables
229:  } > FLASH :tables
260:  } > RAM AT > FLASH :ram_data
273:  } > FLASH :empty
275:/* Calculate heap size based on RAM limits. */
276:heap_limit = ORIGIN(RAM) + LENGTH(RAM);  /* End of RAM */
286:    __HeapLimit = ORIGIN(RAM) + LENGTH(RAM);
287:  } > RAM :ram_post_data
299:  .nvm (NOLOAD) : {
[exit 0]

$ grep -n 'define NVM3_DEFAULT\|NVM3_DEFAULT_NVM_SIZE\|CACHE_SIZE\|MAX_OBJECT' config/nvm3_default_config.h
2:#define NVM3_DEFAULT_CONFIG_H
8:#ifndef NVM3_DEFAULT_CACHE_SIZE
9:// <o NVM3_DEFAULT_CACHE_SIZE> NVM3 Default Instance Cache Size
14:#define NVM3_DEFAULT_CACHE_SIZE  200
17:#ifndef NVM3_DEFAULT_MAX_OBJECT_SIZE
18:// <o NVM3_DEFAULT_MAX_OBJECT_SIZE> NVM3 Default Instance Max Object Size
21:#define NVM3_DEFAULT_MAX_OBJECT_SIZE  254
30:#define NVM3_DEFAULT_REPACK_HEADROOM  0
33:#ifndef NVM3_DEFAULT_NVM_SIZE
34:// <o NVM3_DEFAULT_NVM_SIZE> NVM3 Default Instance Size
38:#define NVM3_DEFAULT_NVM_SIZE  40960
[exit 0]

$ grep -rn 'define FLASH_SIZE\|define SRAM_SIZE\|define FLASH_PAGE_SIZE\|define FLASH_BASE\|define SRAM_BASE' $(find ~/.silabs -path '*Device/SiliconLabs/BGM22/Include/bgm220sc22hna.h' 2>/dev/null | head -1)
279:#define FLASH_BASE                                        (0x00000000UL) /**< Flash Base Address */
280:#define FLASH_SIZE                                        (0x00080000UL) /**< Available Flash Memory */
281:#define FLASH_PAGE_SIZE                                   (0x00002000UL) /**< Flash Memory page size */
282:#define SRAM_BASE                                         (0x20000000UL) /**< SRAM Base Address */
283:#define SRAM_SIZE                                         (0x00008000UL) /**< Available SRAM Memory */
[exit 0]

$ sed -n 28,40p autogen/linkerfile.ld; sed -n 270,320p autogen/linkerfile.ld
 ******************************************************************************/

 MEMORY
 {
   FLASH      (rx)  : ORIGIN = 0x0, LENGTH = 0x7e000
   RAM        (rwx) : ORIGIN = 0x20000000, LENGTH = 0x8000
   /* Dummy region is used for the dummy segment, which in turn never gets data
    * loaded, but is only used to calculate the size of objects placed in it. */
   DUMMY      (!ax) : ORIGIN = 0xfff82000, LENGTH = 0x7e000
 }

/*******************************************************************************
 * Due to an issue in GNU LD, which results in wrongly applying the writeable
  /* Sets the difference between VMA and LMA to 0 for the next NOLOAD sections */
  .empty :
  {
  } > FLASH :empty

/* Calculate heap size based on RAM limits. */
heap_limit = ORIGIN(RAM) + LENGTH(RAM);  /* End of RAM */
heap_size = heap_limit - __HeapBase;
  .memory_manager_heap (NOLOAD):
  {
    . = ALIGN(8);
    __HeapBase = .;
    . += heap_size;
    __end__ = .;
    _end = __end__;
    KEEP(*(.memory_manager_heap*))
    __HeapLimit = ORIGIN(RAM) + LENGTH(RAM);
  } > RAM :ram_post_data

  __ram_end__ = 0x20000000 + 0x8000;
  __main_flash_end__ = 0x0 + 0x7e000;

   /* This is where we handle flash storage blocks. We use dummy sections for finding the configured
   * block sizes and then "place" them at the end of flash when the size is known. */
  .internal_storage (NOLOAD) : {
    KEEP(*(.internal_storage*))
  } > DUMMY :dummy_seg


  .nvm (NOLOAD) : {
    KEEP(*(.simee*))
  } > DUMMY :dummy_seg

  __ramfuncs_start__ = __vma_ramfuncs_start__;
  __ramfuncs_end__ = __vma_ramfuncs_end__;

  linker_nvm_end = __main_flash_end__;
  linker_nvm_begin = linker_nvm_end - SIZEOF(.nvm);
  linker_storage_end = linker_nvm_begin;
  __nvm3Base = linker_nvm_begin;

  linker_storage_begin = linker_storage_end - SIZEOF(.internal_storage);
  ASSERT((linker_storage_begin >= (__etext + SIZEOF(.data))), "FLASH memory overflowed !")



}
[exit 0]

$ find ~/.silabs -path '*Device/SiliconLabs/BGM22/Include/bgm220sc22hna.h' 2>/dev/null | head -1; grep -n 'linker_nvm\|__nvm3Base\|nvm3Storage\|__ram_end\|__StackTop\|__HeapLimit\|__HeapBase\|linker_vectors_begin\|__etext\|__end__' cmake_gcc/build/base/tilt_bed_controller.map | head -20
/c/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/Device/SiliconLabs/BGM22/Include/bgm220sc22hna.h
34712:                0x00000000                        linker_vectors_begin = .
34728:                0x20000ac0                        __StackTop = .
44556:                0x0003bcfc        0x4 LONG 0x3bd08 __etext
44852:                0x0003bd08                        __etext = LOADADDR (.data)
44855:                0x00000001                        ASSERT (((__etext % 0x4) == 0x0), Data section needs to be word-aligned in LMA)
44871:                0x000041e0                        heap_size = (heap_limit - __HeapBase)
44876:                0x20003e20                        __HeapBase = .
44879:                0x20008000                        __end__ = .
44880:                0x20008000                        _end = __end__
44882:                0x20008000                        __HeapLimit = (ORIGIN (RAM) + LENGTH (RAM))
44883:                0x20008000                        __ram_end__ = 0x20008000
44895:                0x0007e000                        linker_nvm_end = __main_flash_end__
44896:                0x00074000                        linker_nvm_begin = (linker_nvm_end - SIZEOF (.nvm))
44897:                0x00074000                        linker_storage_end = linker_nvm_begin
44898:                0x00074000                        __nvm3Base = linker_nvm_begin
44900:                0x00000001                        ASSERT ((linker_storage_begin >= (__etext + SIZEOF (.data))), FLASH memory overflowed !)
[exit 0]

$ grep -n 'bed_level\b\|bed_level =' *.c *.h; grep -n 'Bed is level' docs/REGRESSION.md
app.c:681:              if (bed_level){
auto_level.c:166:bool bed_level = false;
auto_level.c:603:  bed_level = false;
auto_level.h:39:extern bool bed_level;
196:      motors stop; green LED on; "Bed is level" prints
210:      run, start to "Bed is level" - the original auto-level BLE-drop bug
[exit 0]

$ grep -n 'set_motor_on(\|set_motor_off(\|set_dual_motors_on(\|set_all_motors_on(' *.c | grep -v '^exp_board.c.*void'
app.c:536:  // The motor-enable shadow, not just motor_state[]: set_all_motors_on()
auto_level.c:713:                set_dual_motors_on(LEFT);
auto_level.c:714:                set_dual_motors_on(RIGHT);
auto_level.c:717:                set_dual_motors_on(HEAD);
auto_level.c:718:                set_dual_motors_on(FOOT);
auto_level.c:759:              set_dual_motors_on(LEFT);
auto_level.c:760:              set_dual_motors_on(RIGHT);
auto_level.c:763:              set_dual_motors_on(HEAD);
auto_level.c:764:              set_dual_motors_on(FOOT);
bed_actions.c:266:    set_all_motors_on();
bed_actions.c:269:    set_dual_motors_on(target_to_side(target));
bed_actions.c:352:  // a stale flag survives the stop and set_dual_motors_on() then refuses to
dual_motor_control.c:159:      set_all_motors_on();
dual_motor_control.c:212: * and set_dual_motors_on()'s guards below then refuse to energise that pair -
dual_motor_control.c:304:      set_all_motors_on();
dual_motor_control.c:308:      set_all_motors_on();
dual_motor_control.c:312:      set_dual_motors_on(HEAD);
dual_motor_control.c:316:      set_dual_motors_on(LEFT);
dual_motor_control.c:320:      set_dual_motors_on(RIGHT);
dual_motor_control.c:324:      set_dual_motors_on(FOOT);
dual_motor_control.c:328:      set_dual_motors_on(HEAD);
dual_motor_control.c:332:      set_dual_motors_on(LEFT);
dual_motor_control.c:336:      set_dual_motors_on(RIGHT);
dual_motor_control.c:340:      set_dual_motors_on(FOOT);
dual_motor_control.c:347:void set_dual_motors_on(side_of_bed_t side)
dual_motor_control.c:354:          set_motor_on(MOTORA);
dual_motor_control.c:357:          set_motor_on(MOTORD);
dual_motor_control.c:362:          set_motor_on(MOTORA);
dual_motor_control.c:365:          set_motor_on(MOTORD);
dual_motor_control.c:373:          set_motor_on(MOTORA);
dual_motor_control.c:376:          set_motor_on(MOTORB);
dual_motor_control.c:381:          set_motor_on(MOTORA);
dual_motor_control.c:384:          set_motor_on(MOTORB);
dual_motor_control.c:392:          set_motor_on(MOTORC);
dual_motor_control.c:395:          set_motor_on(MOTORD);
dual_motor_control.c:400:          set_motor_on(MOTORC);
dual_motor_control.c:403:          set_motor_on(MOTORD);
dual_motor_control.c:411:          set_motor_on(MOTORB);
dual_motor_control.c:414:          set_motor_on(MOTORC);
dual_motor_control.c:419:          set_motor_on(MOTORB);
dual_motor_control.c:422:          set_motor_on(MOTORC);
dual_motor_control.c:434:      set_motor_off(MOTORA);
dual_motor_control.c:437:      set_motor_off(MOTORD);
dual_motor_control.c:441:      set_motor_off(MOTORA);
dual_motor_control.c:444:      set_motor_off(MOTORB);
dual_motor_control.c:448:      set_motor_off(MOTORC);
dual_motor_control.c:451:      set_motor_off(MOTORD);
dual_motor_control.c:455:      set_motor_off(MOTORB);
dual_motor_control.c:458:      set_motor_off(MOTORC);
exp_board.c:303:// register, the same register and the same transaction set_motor_on() starts
exp_board.c:308:// motor_state[].motor_on, which is bookkeeping that set_all_motors_on() never
exp_board.c:327:// app_is_ok_to_sleep(): set_all_motors_on() energises without setting
switch_input.c:502:    set_all_motors_on();
switch_input.c:504:    set_dual_motors_on(HEAD);
switch_input.c:524:              set_all_motors_on();
switch_input.c:548:              set_all_motors_on();
switch_input.c:577:                    set_dual_motors_on(motor_switch_table[t].side); // Motor statuses are set in "set_dual_motors_on/off()" function
switch_input.c:764:// motor_state[].motor_on - that is bookkeeping, and set_all_motors_on() sets
[exit 0]

$ grep -n 'FREQUENCY\|POLARITY' config/sl_pwm_init_motor*_config.h; grep -n 'SPEED_MODE\|REFERENCE_CLOCK\|define SL_I2CSPM_.*_SPEED\|HLR\|FREQ' config/sl_i2cspm_*config.h
config/sl_pwm_init_motor1_config.h:42:// <o SL_PWM_MOTOR1_FREQUENCY> PWM frequency [Hz]
config/sl_pwm_init_motor1_config.h:44:#define SL_PWM_MOTOR1_FREQUENCY       24000
config/sl_pwm_init_motor1_config.h:46:// <o SL_PWM_MOTOR1_POLARITY> Polarity
config/sl_pwm_init_motor1_config.h:50:#define SL_PWM_MOTOR1_POLARITY        PWM_ACTIVE_HIGH
config/sl_pwm_init_motor2_config.h:42:// <o SL_PWM_MOTOR2_FREQUENCY> PWM frequency [Hz]
config/sl_pwm_init_motor2_config.h:44:#define SL_PWM_MOTOR2_FREQUENCY       24000
config/sl_pwm_init_motor2_config.h:46:// <o SL_PWM_MOTOR2_POLARITY> Polarity
config/sl_pwm_init_motor2_config.h:50:#define SL_PWM_MOTOR2_POLARITY        PWM_ACTIVE_HIGH
config/sl_pwm_init_motor3_config.h:42:// <o SL_PWM_MOTOR3_FREQUENCY> PWM frequency [Hz]
config/sl_pwm_init_motor3_config.h:44:#define SL_PWM_MOTOR3_FREQUENCY       24000
config/sl_pwm_init_motor3_config.h:46:// <o SL_PWM_MOTOR3_POLARITY> Polarity
config/sl_pwm_init_motor3_config.h:50:#define SL_PWM_MOTOR3_POLARITY        PWM_ACTIVE_HIGH
config/sl_pwm_init_motor4_config.h:42:// <o SL_PWM_MOTOR4_FREQUENCY> PWM frequency [Hz]
config/sl_pwm_init_motor4_config.h:44:#define SL_PWM_MOTOR4_FREQUENCY       24000
config/sl_pwm_init_motor4_config.h:46:// <o SL_PWM_MOTOR4_POLARITY> Polarity
config/sl_pwm_init_motor4_config.h:50:#define SL_PWM_MOTOR4_POLARITY        PWM_ACTIVE_HIGH
config/sl_i2cspm_off_board_config.h:38:// <o SL_I2CSPM_OFF_BOARD_REFERENCE_CLOCK> Reference clock frequency
config/sl_i2cspm_off_board_config.h:42:#define SL_I2CSPM_OFF_BOARD_REFERENCE_CLOCK 0
config/sl_i2cspm_off_board_config.h:44:// <o SL_I2CSPM_OFF_BOARD_SPEED_MODE> Speed mode
config/sl_i2cspm_off_board_config.h:49:#define SL_I2CSPM_OFF_BOARD_SPEED_MODE      0
config/sl_i2cspm_on_board_config.h:38:// <o SL_I2CSPM_ON_BOARD_REFERENCE_CLOCK> Reference clock frequency
config/sl_i2cspm_on_board_config.h:42:#define SL_I2CSPM_ON_BOARD_REFERENCE_CLOCK 0
config/sl_i2cspm_on_board_config.h:44:// <o SL_I2CSPM_ON_BOARD_SPEED_MODE> Speed mode
config/sl_i2cspm_on_board_config.h:49:#define SL_I2CSPM_ON_BOARD_SPEED_MODE      0
[exit 0]

$ grep -rn -i 'mm/s\|mm per s\|actuator speed\|stroke\|inches per' --include=*.md --include=*.c --include=*.h . | grep -v autogen | cut -c1-200 | head
./docs/DECISIONS.md:45:150–650 mA over a stroke, weakly correlated between motors, with roughly 3×
./docs/DECISIONS.md:215:  that also collects actuator stroke and rough bed length/width for future
./docs/handoffs/fw_HANDOFF_2026-08-05_03_runaway-provocation.md:108:travel from energise to de-energise. At typical actuator speed that is on the
./docs/handoffs/fw_HANDOFF_2026-08-05_03_runaway-provocation.md:471:| A3 | ~150–200 ms / 1–3 mm of travel | Arithmetic from `FAILSAFE_SAMPLE_MS × FAILSAFE_CONFIRM_SAMPLES` plus an assumed actuato
./docs/handoffs/fw_HANDOFF_2026-08-05_03_runaway-provocation.md:481:   looks exactly like a pass. The instructions say to start mid-stroke, but it
./docs/handoffs/fw_HANDOFF_2026-08-05_03_runaway-provocation.md:522:- **Put the bed mid-stroke**, not at either limit. `set_dual_motors_on()`
./docs/handoffs/fw_HANDOFF_2026-10-17_02_failsafe-hardware-read-and-double-press.md:346:1. Open `switch_input.c`, line 91. Change `#define BED_BENCH_PROVOKE_RUNAWAY 0` to `1`. Save. Build and flash. O
./docs/REGRESSION.md:41:- [ ] Bed frame clear of obstructions, all four actuators mid-stroke
./docs/REGRESSION_RECORD_D1.md:72:- [ ] Bed frame clear of obstructions, all four actuators mid-stroke
./docs/REGRESSION_RECORD_PRE_STAGEC.md:46:- [x] Bed frame clear of obstructions, all four actuators mid-stroke
[exit 0]

$ sed -n 471p docs/handoffs/fw_HANDOFF_2026-08-05_03_runaway-provocation.md | cut -c1-400; sed -n 104,110p docs/handoffs/fw_HANDOFF_2026-08-05_03_runaway-provocation.md; sed -n 36,50p config/sl_i2cspm_on_board_config.h
| A3 | ~150–200 ms / 1–3 mm of travel | Arithmetic from `FAILSAFE_SAMPLE_MS × FAILSAFE_CONFIRM_SAMPLES` plus an assumed actuator speed. The **measured** number comes from the `PROVOKE:` print. |
delay would risk testing the race instead of the fail-safe.

**How far the bed will actually move.** The fail-safe samples every 50 ms and
needs 3 consecutive quiet samples, so worst case is roughly **150–200 ms** of
travel from energise to de-energise. At typical actuator speed that is on the
order of **1–3 mm** — you will more likely *hear* the motors start and stop
than see the bed move. Do not expect a dramatic lurch; watch the console, not
// <h>I2CSPM settings

// <o SL_I2CSPM_ON_BOARD_REFERENCE_CLOCK> Reference clock frequency
// <i> Frequency in Hz of the reference clock.
// <i> Select 0 to use the frequency of the currently selected clock.
// <i> Default: 0
#define SL_I2CSPM_ON_BOARD_REFERENCE_CLOCK 0

// <o SL_I2CSPM_ON_BOARD_SPEED_MODE> Speed mode
// <0=> Standard mode (100kbit/s)
// <1=> Fast mode (400kbit/s)
// <2=> Fast mode plus (1Mbit/s)
// <i> Default: 0
#define SL_I2CSPM_ON_BOARD_SPEED_MODE      0
// </h> end I2CSPM config
[exit 0]

$ grep -n 'passes/s\|ms/pass\|ms per pass\|passes per second\|pass rate' docs/DECISIONS.md | cut -c1-220 | head -12
648:| Loop rate | ~630 passes/sec (1.587 ms/pass) | ~433 passes/sec (2.31 ms/pass) |
666:without-rate-limit figure (2.31 ms/pass, the closer match to current shipped
676:1.587/2.31 ms/pass figures above are auto-level-specific measurements and
780:super-loop pass, measured at ~2.31 ms/pass, so 130 samples ≈ 300 ms and the
805:130-sample running average, same LPF, same per-pass rate), on separate
914:average, same low-pass filter, same per-pass rate, so the app's number is as
926:arithmetic, ordering and per-pass rate are unchanged; its outputs are mirrored
932:loop passes (#29). At the measured ~2.31 ms/pass that is ~200 passes,
979:loop-pass calibrated, so a change in passes/sec on the manual-move path *is*
986:130-sample average and LPF fed at the same per-pass rate, giving the same
2169:DECISIONS #29's pass-counted constants therefore see the same pass rate on
2174:  the loop ran ~77-130 passes/s there (23-39 per 300 ms on the 2026-11-01
[exit 0]

$ grep -rn -i 'logo\|orientation\|mounted\|faces this way\|POWER BOARD FACES' --include=*.c --include=*.h --include=*.md . | grep -v autogen | grep -v 'docs/handoffs\|SESSION_LOG\|docs/prompts' | cut -c1-200
./auto_level.c:267:  // convention, it is fixed by how the accelerometer is mounted, and two
./auto_level.c:640:  *                                              |   |  POWER BOARD FACES  |   |
./CLAUDE.md:9:orientation file: where the migration stands, what each other doc in
./docs/DECISIONS.md:64:This routine folds into the orientation-calibration wizard (decision 7).
./docs/DECISIONS.md:85:## 7. Orientation-calibration wizard (app era)
./docs/DECISIONS.md:89:mounting orientation. Motor characterization (decision 4) runs in the same
./docs/MIGRATION_PORT_PROMPT.md:11:Read `docs/MIGRATION_STATUS.md` first — it is the orientation file for this
./docs/MIGRATION_STATUS.md:3:Orientation note for the `tilt_bed_controller` repo (created 2026-07-05).
./docs/SETPOINTS_INVENTORY.md:87:| auto_level.c:31 | `PITCH_OFFSET` | 90° | Geometry constant: pitch derived from atan2(y,z) minus 90°. Mounting-orientation dependent — installer-adjustable only i
./readme.md:87:Before programming the radio board mounted on the mainboard, make sure the power supply switch is in the AEM position (right side) as shown below.
[exit 0]

$ grep -n 'MOTORA\|MOTORB\|MOTORC\|MOTORD\|OUTPUT[0-3]' *.c *.h | grep -v '^dual_motor_control.c' | cut -c1-160; grep -c 'MOTOR[ABCD]' dual_motor_control.c
dual_motor_control.h:16:extern const uint8_t OUTPUT0;
dual_motor_control.h:17:extern const uint8_t OUTPUT1;
dual_motor_control.h:18:extern const uint8_t OUTPUT2;
dual_motor_control.h:19:extern const uint8_t OUTPUT3;
dual_motor_control.h:22:extern uint8_t MOTORA;
dual_motor_control.h:23:extern uint8_t MOTORB;
dual_motor_control.h:24:extern uint8_t MOTORC;
dual_motor_control.h:25:extern uint8_t MOTORD;
104
[exit 0]

$ grep -n 'init_accel\|MC3479_REG_MODE\|DEC\|RANGE\|XYZ\|axis' mc3479.c | head -30
41:#define MC3479_WR_RANGE_SELECT      0x20 // Reads/writes accelerometer range and low pass filtering
50:#define MC3479_RD_XOUT_LSB          0x0D // Reads x-axis acceleration LSB
51:#define MC3479_RD_XOUT_MSB          0x0E // Reads x-axis acceleration MSB
52:#define MC3479_RD_YOUT_LSB          0x0F // Reads y-axis acceleration LSB
53:#define MC3479_RD_YOUT_MSB          0x10 // Reads y-axis acceleration MSB
54:#define MC3479_RD_ZOUT_LSB          0x11 // Reads z-axis acceleration LSB
55:#define MC3479_RD_ZOUT_MSB          0x12 // Reads z-axis acceleration MSB
85:  MC3479_DECIMATE_NONE   = 0x00,
86:  MC3479_DECIMATE_2      = 0x01,
87:  MC3479_DECIMATE_4      = 0x02,
88:  MC3479_DECIMATE_5      = 0x03,
89:  MC3479_DECIMATE_8      = 0x04,
90:  MC3479_DECIMATE_10     = 0x05,
91:  MC3479_DECIMATE_16     = 0x06,
92:  MC3479_DECIMATE_20     = 0x07,
93:  MC3479_DECIMATE_40     = 0x08,
94:  MC3479_DECIMATE_67     = 0x09,
95:  MC3479_DECIMATE_80     = 0x0A,
96:  MC3479_DECIMATE_100    = 0x0B,
97:  MC3479_DECIMATE_200    = 0x0C,
98:  MC3479_DECIMATE_250    = 0x0D,
99:  MC3479_DECIMATE_500    = 0x0E,
100:  MC3479_DECIMATE_1000   = 0x0F
188:  cmd[0] = MC3479_WR_RANGE_SELECT;
339:void init_accel()
352:  accel_set_decimation(MC3479_DECIMATE_10); // Output registers update at 100 Hz
[exit 0]

$ awk '/Step 1/{s++} /hold end op=0x03/{printf "line %d: %d Step 1 since previous op=0x03 line\n", NR, s; s=0} END{printf "after last op=0x03 line: %d Step 1\n", s}' '../PuTTY Logs/prompt_aa_bench.log'
line 40: 1 Step 1 since previous op=0x03 line
line 61: 1 Step 1 since previous op=0x03 line
line 85: 1 Step 1 since previous op=0x03 line
line 103: 1 Step 1 since previous op=0x03 line
line 121: 1 Step 1 since previous op=0x03 line
line 151: 1 Step 1 since previous op=0x03 line
line 169: 1 Step 1 since previous op=0x03 line
line 187: 1 Step 1 since previous op=0x03 line
line 209: 1 Step 1 since previous op=0x03 line
line 227: 1 Step 1 since previous op=0x03 line
line 248: 1 Step 1 since previous op=0x03 line
after last op=0x03 line: 1 Step 1
[exit 0]

$ grep -c 'Exit auto-level' '../PuTTY Logs/prompt_aa_bench.log'; grep -c 'Bed is level' '../PuTTY Logs/prompt_aa_bench.log'; grep -n 'disabled' '../PuTTY Logs/prompt_aa_bench.log'; tail -c 300 docs/DECISIONS.md | od -c | tail -3
12
0
102:ROLL is disabled and Pitch is level
120:ROLL is disabled and Pitch is level
168:ROLL is disabled and Pitch is level
186:PITCH is disabled and ROLL is level
226:PITCH is disabled and ROLL is level
0000420   ~   0   -   1       m   s       f   o   r       t   h   e    
0000440   w   h   o   l   e       h   o   l   d   .  \n
0000454
[exit 0]

$ head -3 docs/handoffs/fw_HANDOFF_2026-10-18_01_switch-followups-settings-exit-pairing-window.md docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md docs/handoffs/fw_HANDOFF_2026-10-17_05_one-switch-reader-sampler.md docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md | cut -c1-160; sed -n 36,40p docs/REGRESSION_RECORD_PRE_STAGEC.md; sed -n 44,47p docs/BLE_GATT_DESIGN.md
==> docs/handoffs/fw_HANDOFF_2026-10-18_01_switch-followups-settings-exit-pairing-window.md <==
# Handoff 2026-10-18 #01 — Prompt V: sampler bench result, switch follow-ups, settings-mode exit, pairing window

## a. Header

==> docs/handoffs/fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md <==
# Handoff 2026-10-18 #02 — Prompt W: bond store policy, disconnect refused pairings, settings-mode holds

## a. Header

==> docs/handoffs/fw_HANDOFF_2026-10-17_05_one-switch-reader-sampler.md <==
# Handoff 2026-10-17 #05 — one switch reader (switch sampler, DECISIONS #41)

## a. Header

==> docs/handoffs/fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md <==
# fw_HANDOFF_2026-10-23_01 — Prompt Y: watchdog-stop diagnostics, Prompt W follow-ups

Firmware repo session of 2026-10-23 (system date `Fri Oct 23 2026`, checked
blocking-I2C bug) and the Task G I2C-health bisection (DECISIONS #30) both
depended on assertions/instrumentation that behave differently, or not at
all, in Release. **A Release-config confirmation pass is still owed before
Stage C ships** — this record does not stand in for one.

  `bluetooth_feature_sm` (security manager), `bluetooth_feature_system`,
  `gatt_configuration` (GATT Configurator), plus the OTA components in §9.
- `SL_BT_CONFIG_MAX_CONNECTIONS = 2` (control + viewer, #14).
- Adding components regenerates `autogen/` and `config/`. Since DECISIONS
[exit 0]

$ git commit -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/36b867bc-2bf4-4441-bb00-3e095f202750/scratchpad/msgA.txt' && git log --oneline -1
[main fad70fc] Docs: Prompt AA bench result (DECISIONS #45, REGRESSION 6.9), create PRE_SHIP.md
 5 files changed, 333 insertions(+), 1 deletion(-)
 create mode 100644 docs/PRE_SHIP.md
 create mode 100644 docs/prompts/PROMPT_AC_feature-groundwork-report.md
fad70fc Docs: Prompt AA bench result (DECISIONS #45, REGRESSION 6.9), create PRE_SHIP.md
[exit 0]

$ grep -n 'define MAX_LOAD_CURRENT\|define NUM_CURRENT_READINGS\|define NUM_INITIAL_CURRENTS_IGNORED\|define NUM_CURRENTS_READ\|define ON_CURRENT_THRESHOLD\|define CONITNUOUS\|static float alpha\|sl_sleeptimer_delay_millisecond(1)\|Tripped on steady\|TRIPPED MAX LOAD\|Motor %d Delta Current\|Max Current for Motor\|security_margin = \|average_current_delta_single\[i\] = \|dynamic_reading_count\[3\]\|readings\[NUM_MOTORS\]\|latest_motor_current\[NUM_MOTORS\]\|average_current_single_reading\[NUM_MOTORS\]\|max_current_threshold_set\[NUM_MOTORS\]\|dynamic_collision_current_threshold\[NUM_MOTORS\]\|uint32_t filtered_current\|average_current_delta_sum = 0;\|void reset_motor_current_data\|void run_motor_supervision\|collision_check(is_raising)' motor_current_functions.c
25:#define MAX_LOAD_CURRENT 3000
27:#define NUM_CURRENT_READINGS 200
28:#define NUM_INITIAL_CURRENTS_IGNORED 100
29:#define NUM_CURRENTS_READ_DYNAMIC_THRESHOLD 350
30:#define NUM_CURRENTS_READ_CONTINUOUS_DYNAMIC_THRESHOLD 300 // Amount of currents read after dynamic collision detection is active to see if threshold needs to be reduced (if a weight is removed during movement)
32:#define ON_CURRENT_THRESHOLD 80
33:#define CONITNUOUS_DYNAMIC_THRESHOLD_CHANGE_MARGIN 500 // Amount of current in mA that the dynamic current threshold needs to lower to have a new threshold set
59:static uint32_t latest_motor_current[NUM_MOTORS]; // Array to hold motor currents read directly from the current sensors
60:static uint16_t readings[NUM_MOTORS][NUM_CURRENT_READINGS] = {0}; // 2D array to hold the past "NUM_CURRENT_READINGS" current values read for each motor
62:static uint16_t average_current_single_reading[NUM_MOTORS] = {0}; // Array to hold single average value
68:static int32_t average_current_delta_sum = 0; // Difference between average current single read and the dynamic threshold (increase in current during movement)
81:uint32_t filtered_current[NUM_MOTORS] = {0}; // Store filtered values for each motor
83:static float alpha = 0.05; // Smoothing factor (adjust based on noise level)
91:uint16_t max_current_threshold_set[NUM_MOTORS] = {0}; // Max current used to set threshold
92:uint16_t dynamic_collision_current_threshold[NUM_MOTORS] = {0}; // dynamic_max_average_calibration_current + COLLISION_CURRENT_SECURITY_MARGIN
133:  sl_sleeptimer_delay_millisecond(1);
198:          printf("Max Current for Motor %d: %d\r\n", i, dynamic_max_average_calibration_current[i]);
210:      if (dynamic_reading_count[3] <= NUM_CURRENTS_READ_CONTINUOUS_DYNAMIC_THRESHOLD){
223://            printf("LOWERED Max Current for Motor %d: %d\r\n", i, dynamic_max_average_calibration_current[i]);
234:void reset_motor_current_data(){
302:          printf("TRIPPED MAX LOAD\r\n");
318:          printf("TRIPPED MAX LOAD\r\n");
335:        average_current_delta_sum = 0;
337:            security_margin = lower_collision_current_security_margin;
339:            security_margin = raise_collision_current_security_margin;
344:                average_current_delta_single[i] = (average_current_single_reading[i] - max_current_threshold_set[i]);
357:                printf("Tripped on steady!\r\n");
361:                printf("Motor %d Delta Current:  %ld\r\n", i, average_current_delta_single[i]);
380:void run_motor_supervision(active_switch_t sw, bool is_raising)
396:  collision_check(is_raising);
[exit 0]

$ grep -n 'digAvg\|iadcPosInputPortAPin\|scanResult\[result.id\]\|IADC_IEN_SCANTABLEDONE\|iadcCmdStartScan' adc_currents.c
52:#define IADC_INPUT_0_PORT_PIN     iadcPosInputPortAPin5;  // Motor 0
53:#define IADC_INPUT_1_PORT_PIN     iadcPosInputPortAPin6;  // Motor 1
54:#define IADC_INPUT_2_PORT_PIN     iadcPosInputPortAPin7;  // Motor 2
55:#define IADC_INPUT_3_PORT_PIN     iadcPosInputPortAPin8;  // Motor 3
105:  initAllConfigs.configs[0].digAvg = _IADC_CFG_DIGAVG_AVG8;
153:  IADC_enableInt(IADC0, IADC_IEN_SCANTABLEDONE);
183:    scanResult[result.id] = result.data + result.data/2;
199:  IADC_command(IADC0, iadcCmdStartScan);
[exit 0]

$ grep -n 'run_motor_supervision\|initial_switch_press\|count_switches_down(switch_sampler_down()) >= 2\|active_switch != OFF) && !any_collision' switch_input.c auto_level.c app.c
switch_input.c:732: * run_motor_supervision() and leaves nothing that would ever stop them.
switch_input.c:921:  if ((active_switch != OFF) && !any_collision && (!auto_level_active) && (!in_settings_mode)){
switch_input.c:922:      if (initial_switch_press){
switch_input.c:924:          initial_switch_press = false;
switch_input.c:926:      run_motor_supervision(active_switch, is_raising);
switch_input.c:936:      // run_motor_supervision() above is deliberately OUTSIDE this gate:
switch_input.c:944:      if (switch_sourced_move && (count_switches_down(switch_sampler_down()) >= 2)){
auto_level.c:771:          run_motor_supervision(SWITCH_AUTO_LEVEL_VALUE, true);
app.c:496: * Power Manager, so its super loop free-spun and run_motor_supervision() ran
[exit 0]

$ grep -n 'leveling_success\|bed_leveling_disabled' *.c | grep -v '^auto_level.c:16[5-7]'; grep -n 'dynamic_collision_current_threshold' *.c *.h; grep -n 'check_motor_filtered_current' motor_current_functions.c; grep -n 'average_current_delta_single' motor_current_functions.c
auto_level.c:604:  bed_leveling_disabled = false;
auto_level.c:837:      leveling_success[axis] = true; // Leveled successfully, no error
bed_actions.c:164:      leveling_success[i] = false;
ble_bed_service.c:319:// app_auto_level_start() resets first_check, leveling_success[] and the axis,
motor_current_functions.c:92:uint16_t dynamic_collision_current_threshold[NUM_MOTORS] = {0}; // dynamic_max_average_calibration_current + COLLISION_CURRENT_SECURITY_MARGIN
motor_current_functions.c:192:          dynamic_collision_current_threshold[i] = dynamic_max_average_calibration_current[i] + raise_collision_current_security_margin;
motor_current_functions.c:222:            dynamic_collision_current_threshold[i] = dynamic_max_average_calibration_current[i] + raise_collision_current_security_margin;
motor_current_functions.c:224://            printf("LOWERED Threshold for Motor %d: %d\r\n", i, dynamic_collision_current_threshold[i]);
motor_current_functions.c:242:    dynamic_collision_current_threshold[i] = 0;
motor_current_functions.h:25:extern uint16_t dynamic_collision_current_threshold[NUM_MOTORS];
82:static uint32_t check_motor_filtered_current[NUM_MOTORS] = {0}; // Store filtered values for each motor
146:      check_motor_filtered_current[i] = low_pass_filter(latest_motor_current[i], check_motor_filtered_current[i], alpha);
149:      is_motor_moving[i] = (check_motor_filtered_current[i] > ON_CURRENT_THRESHOLD);
69:static int32_t average_current_delta_single[NUM_MOTORS] = {0};
344:                average_current_delta_single[i] = (average_current_single_reading[i] - max_current_threshold_set[i]);
345:                if (average_current_delta_single[i] > 0) {
346:                    average_current_delta_sum += average_current_delta_single[i];
361:                printf("Motor %d Delta Current:  %ld\r\n", i, average_current_delta_single[i]);
[exit 0]

$ sed -n 180,200p mc3479.c; sed -n 339,355p mc3479.c
 */

static void accel_set_lpf_bw(MC3479_lpf_bandwidth_t accel_lpf_bw)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd[2];

  // Set accel sample rate bits
  cmd[0] = MC3479_WR_RANGE_SELECT;
  cmd[1] = accel_lpf_bw;
  ret = MC3479_transaction(I2C_FLAG_WRITE, &cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

static void accel_set_decimation(MC3479_decimation_t accel_decimation)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd[2];

  // Set accel sample rate bits
  cmd[0] = MC3479_WR_FIFO_CTRL2;
void init_accel()
{
  /*
   * Accelerometer power up defaults to 2g range in STANDBY mode.
   * All other functionality (sampling rate, filtering, FIFO, interrupts, etc.)
   * must be set.
   *
   * Set internal sample rate to maximum, decimate by 10 for 100 Hz output rate,
   * and set low pass filtering bandwidth to approximately 80 Hz. Can also
   * configure FIFO or motion sensing registers.
   */

  accel_set_sample_rate(MC3479_SAMPLE_RATE_1000HZ); // Maximum internal sample rate
  accel_set_decimation(MC3479_DECIMATE_10); // Output registers update at 100 Hz

  accel_set_lpf_bw(MC3479_LPF_BW_3); // Filter corner at 1000/12 ~= 80 Hz
}
[exit 0]

$ grep -rn -i 'settings_rpc\|54b10005\|gattdb_settings' --include=*.c --include=*.h --include=*.btconf --include=*.json . | grep -v '^./docs' ; echo 'exit of grep pipeline above (1 = no match)'
exit of grep pipeline above (1 = no match)
[exit 0]

$ grep -n 'BED_TARGET_\|BED_SOURCE_\|BED_DIR_' bed_control.h; grep -n 'define NUM_MOTORS\|define SCALE_FACTOR\|SWITCH_BED_LIGHT_VALUE\|^typedef enum' bed_types.h; grep -n 'define SWITCH_UP_ALL\|define SWITCH_AUTO_LEVEL\|define SWITCH_DOWN_ALL\|define SWITCH_BED_LIGHTS' app.h
24:  BED_SOURCE_NONE,
25:  BED_SOURCE_SWITCHES,
26:  BED_SOURCE_APP
30:  BED_TARGET_HEAD,
31:  BED_TARGET_LEFT,
32:  BED_TARGET_RIGHT,
33:  BED_TARGET_FOOT,
34:  BED_TARGET_ALL
38:  BED_DIR_DOWN,
39:  BED_DIR_UP
12:#define NUM_MOTORS 4
14:#define SCALE_FACTOR 100
16:typedef enum {
21:typedef enum {
28:typedef enum active_switch {
41:  SWITCH_BED_LIGHT_VALUE,
115:#define SWITCH_UP_ALL      0x0001
116:#define SWITCH_AUTO_LEVEL  0x0002
117:#define SWITCH_DOWN_ALL    0x0004
118:#define SWITCH_BED_LIGHTS  0x0008
[exit 0]

$ grep -n 'CUSTOM_ROLL_ANGLE_KEY\|CUSTOM_PITCH_ANGLE_KEY\|RAISE_COLLISION_SENSITIVITY_KEY\|LOWER_COLLISION_SENSITIVITY_KEY' bed_settings.c nvm3_functions.h; grep -n 'baseline_roll_angle\b\|baseline_pitch_angle\b' app.c auto_level.c | head
bed_settings.c:35:  { CUSTOM_ROLL_ANGLE_KEY,           0,   -1500, 1500,  &baseline_roll_angle,                     "baseline roll angle"    },
bed_settings.c:36:  { CUSTOM_PITCH_ANGLE_KEY,          0,   -1500, 1500,  &baseline_pitch_angle,                    "baseline pitch angle"   },
bed_settings.c:37:  { RAISE_COLLISION_SENSITIVITY_KEY, 350, 150,   10000, &raise_collision_current_security_margin, "raise collision margin" },
bed_settings.c:38:  { LOWER_COLLISION_SENSITIVITY_KEY, 250, 100,   750,   &lower_collision_current_security_margin, "lower collision margin" },
nvm3_functions.h:16:#define CUSTOM_ROLL_ANGLE_KEY           1
nvm3_functions.h:17:#define CUSTOM_PITCH_ANGLE_KEY          2
nvm3_functions.h:18:#define RAISE_COLLISION_SENSITIVITY_KEY 3
nvm3_functions.h:19:#define LOWER_COLLISION_SENSITIVITY_KEY 4
app.c:470:  printf("Loaded Roll Angle: %d\r\n", baseline_roll_angle);
app.c:471:  printf("Loaded Pitch Angle: %d\r\n", baseline_pitch_angle);
auto_level.c:88:float latest_roll_angle = 0.0; // X-Z angle, deviation from the baseline_roll_angle
auto_level.c:89:float latest_pitch_angle = 0.0; // Y-Z angle, deviation from the baseline_pitch_angle
auto_level.c:131:static int16_t abs_baseline_roll_angle = 0;
auto_level.c:132:static int16_t abs_baseline_pitch_angle = 0;
auto_level.c:161:int16_t baseline_roll_angle; // User inputted baseline xz angle (roll) in degrees for surface levelness SCALED VALUE
auto_level.c:162:int16_t baseline_pitch_angle; // User inputted baseline yz angle (pitch) in degrees for surface levelness SCALED VALUE
auto_level.c:329:  st->baseline_roll  = st->avg_roll  - (baseline_roll_angle);
auto_level.c:330:  st->baseline_pitch = st->avg_pitch - (baseline_pitch_angle);
[exit 0]

$ git diff --stat 6796e6b fad70fc
 docs/DECISIONS.md                                  |  53 ++++++++
 docs/PRE_SHIP.md                                   |  85 +++++++++++++
 docs/REGRESSION.md                                 |  24 +++-
 docs/SESSION_LOG.md                                |  32 +++++
 .../prompts/PROMPT_AC_feature-groundwork-report.md | 140 +++++++++++++++++++++
 5 files changed, 333 insertions(+), 1 deletion(-)
[exit 0]

$ date; git status; git log --oneline -3
Sun Nov  1 21:12:30 PST 2026
On branch main
Your branch is ahead of 'origin/main' by 1 commit.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
fad70fc Docs: Prompt AA bench result (DECISIONS #45, REGRESSION 6.9), create PRE_SHIP.md
6796e6b Handoff 2026-11-01 01: fix one section cross-reference (d.5 -> d.4)
63acb4d Handoff 2026-11-01 01: Prompt AA (sleep veto during app holds)
[exit 0]

$ grep -n 'Deletes the setting\|live value is unchanged' bed_settings.h; grep -n 'Securit margin\|Once' motor_current_functions.c app.c; grep -n 'Text 244988' docs/SESSION_LOG.md | cut -c1-120
39:// Deletes the setting's NVM3 object only - matches eraseStoredAngles()'s
40:// existing behavior: the live value is unchanged until next boot/reload.
motor_current_functions.c:358:                printf("Securit margin: %d\r\n", security_margin);
app.c:573:  // Once this sequence is done (which will take a few seconds), then collision can be
app.c:595:      printf("Once\r\n");
1448:- Builds: baseline, after A, B with the flag at 1, at 0 (temporary, reverted) and at 1 again, after B — all exit 
[exit 0]

```

### d.2 Builds

None run. B9 sizes the existing `cmake_gcc/build/base/tilt_bed_controller.out`
(output in d.1).

### d.3 The Prompt AA bench log (user's run), with line numbers

`_03-Software/PuTTY Logs/prompt_aa_bench.log`, 271 lines, `cat -n`:

```
     1	Welcome to the Tilt Bed application
     2	Application Version 1.1.0 
     3	Welcome to the Tilt Bed application
     4	Application Version 1.1.0 
     5	Accelerometer is sampling ...
     6	NVM3 initialized successfully
     7	Loaded Roll Angle: 0
     8	Loaded Pitch Angle: 0
     9	Loaded Raise Collision Sensitivity: 550
    10	Loaded Lower Collision Sensitivity: 275
    11	BLE SM: configure -> 0x0000
    12	BLE SM: store_bonding_configuration -> 0x0000
    13	BLE SM: set_bondable_mode(1) -> 0x0000
    14	BLE: connection_opened conn=1 bonding=0x01
    15	BLE: connection_parameters conn=1 interval=24 latency=0 timeout=500 security_mode=0
    16	BLE: characteristic_status DROPPED conn=1 client_config_flags=0x0001 (matches neither control nor viewer connection)
    17	BLE: connection_parameters conn=1 interval=24 latency=0 timeout=500 security_mode=1
    18	BLE: cccd_seed conn=1 bed_status=0x0001 sc=0x0000
    19	BLE: connection_parameters conn=1 interval=24 latency=0 timeout=100 security_mode=1
    20	Max Current for Motor 0: 749
    21	Max Current for Motor 1: 24
    22	Max Current for Motor 2: 17
    23	Max Current for Motor 3: 755
    24	BLE: hold end op=0x01 seq=38: 37 refreshes, longest refresh gap 121 ms, longest between passes 0 ms
    25	Step 1: ROLL and PITCH check
    26	Step 2: ROLL Not Level
    27	Leveling...
    28	Latch
    29	Pulse off...
    30	ROLL Leveled successfully...
    31	Axis set to PITCH
    32	Leveling...
    33	Max Current for Motor 0: 705
    34	Max Current for Motor 1: 771
    35	Max Current for Motor 2: 1130
    36	Max Current for Motor 3: 721
    37	Latch
    38	PITCH Leveled successfully...
    39	Exit auto-level
    40	BLE: hold end op=0x03 seq=97: 57 refreshes, longest refresh gap 121 ms, longest between passes 0 ms
    41	Max Current for Motor 0: 742
    42	Max Current for Motor 1: 20
    43	Max Current for Motor 2: 12
    44	Max Current for Motor 3: 810
    45	BLE: hold end op=0x01 seq=122: 23 refreshes, longest refresh gap 149 ms, longest between passes 0 ms
    46	Step 1: ROLL and PITCH check
    47	Step 2: ROLL Not Level
    48	Leveling...
    49	Latch
    50	Pulse off...
    51	ROLL Leveled successfully...
    52	Axis set to PITCH
    53	Leveling...
    54	Max Current for Motor 0: 666
    55	Max Current for Motor 1: 760
    56	Max Current for Motor 2: 1109
    57	Max Current for Motor 3: 726
    58	Latch
    59	PITCH Leveled successfully...
    60	Exit auto-level
    61	BLE: hold end op=0x03 seq=160: 36 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
    62	Max Current for Motor 0: 755
    63	Max Current for Motor 1: 25
    64	Max Current for Motor 2: 19
    65	Max Current for Motor 3: 772
    66	BLE: hold end op=0x01 seq=179: 17 refreshes, longest refresh gap 119 ms, longest between passes 0 ms
    67	Step 1: ROLL and PITCH check
    68	Step 2: ROLL Not Level
    69	Leveling...
    70	Latch
    71	Pulse off...
    72	Pulse on...
    73	Pulse off...
    74	ROLL Leveled successfully...
    75	Axis set to PITCH
    76	Leveling...
    77	Max Current for Motor 0: 719
    78	Max Current for Motor 1: 780
    79	Max Current for Motor 2: 1072
    80	Max Current for Motor 3: 711
    81	Latch
    82	Pulse off...
    83	PITCH Leveled successfully...
    84	Exit auto-level
    85	BLE: hold end op=0x03 seq=225: 44 refreshes, longest refresh gap 179 ms, longest between passes 0 ms
    86	Max Current for Motor 0: 723
    87	Max Current for Motor 1: 21
    88	Max Current for Motor 2: 13
    89	Max Current for Motor 3: 745
    90	BLE: hold end op=0x01 seq=241: 14 refreshes, longest refresh gap 149 ms, longest between passes 0 ms
    91	Step 1: ROLL and PITCH check
    92	Step 2: PITCH Not Level
    93	Leveling...
    94	Max Current for Motor 0: 709
    95	Max Current for Motor 1: 761
    96	Max Current for Motor 2: 1122
    97	Max Current for Motor 3: 735
    98	Latch
    99	Pulse off...
   100	PITCH Leveled successfully...
   101	Exit auto-level
   102	ROLL is disabled and Pitch is level
   103	BLE: hold end op=0x03 seq=15: 28 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
   104	Max Current for Motor 0: 2
   105	Max Current for Motor 1: 809
   106	Max Current for Motor 2: 1120
   107	Max Current for Motor 3: 16
   108	BLE: hold end op=0x01 seq=37: 20 refreshes, longest refresh gap 149 ms, longest between passes 0 ms
   109	Step 1: ROLL and PITCH check
   110	Step 2: PITCH Not Level
   111	Leveling...
   112	Max Current for Motor 0: 705
   113	Max Current for Motor 1: 808
   114	Max Current for Motor 2: 1157
   115	Max Current for Motor 3: 712
   116	Latch
   117	Pulse off...
   118	PITCH Leveled successfully...
   119	Exit auto-level
   120	ROLL is disabled and Pitch is level
   121	BLE: hold end op=0x03 seq=68: 29 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
   122	Max Current for Motor 0: 3
   123	Max Current for Motor 1: 800
   124	Max Current for Motor 2: 1174
   125	Max Current for Motor 3: 17
   126	BLE: hold end op=0x01 seq=85: 15 refreshes, longest refresh gap 120 ms, longest between passes 0 ms
   127	Max Current for Motor 0: 5
   128	Max Current for Motor 1: 22
   129	Max Current for Motor 2: 1122
   130	Max Current for Motor 3: 713
   131	BLE: hold end op=0x01 seq=101: 14 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
   132	Step 1: ROLL and PITCH check
   133	Step 2: ROLL Not Level
   134	Leveling...
   135	Max Current for Motor 0: 693
   136	Max Current for Motor 1: 717
   137	Max Current for Motor 2: 1170
   138	Max Current for Motor 3: 712
   139	Latch
   140	Pulse off...
   141	ROLL Leveled successfully...
   142	Axis set to PITCH
   143	Leveling...
   144	Max Current for Motor 0: 678
   145	Max Current for Motor 1: 799
   146	Max Current for Motor 2: 1139
   147	Max Current for Motor 3: 712
   148	Latch
   149	PITCH Leveled successfully...
   150	Exit auto-level
   151	BLE: hold end op=0x03 seq=152: 49 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
   152	Max Current for Motor 0: 2
   153	Max Current for Motor 1: 826
   154	Max Current for Motor 2: 1194
   155	Max Current for Motor 3: 17
   156	BLE: hold end op=0x01 seq=170: 16 refreshes, longest refresh gap 127 ms, longest between passes 0 ms
   157	Step 1: ROLL and PITCH check
   158	Step 2: PITCH Not Level
   159	Leveling...
   160	Max Current for Motor 0: 720
   161	Max Current for Motor 1: 774
   162	Max Current for Motor 2: 1129
   163	Max Current for Motor 3: 731
   164	Latch
   165	Pulse off...
   166	PITCH Leveled successfully...
   167	Exit auto-level
   168	ROLL is disabled and Pitch is level
   169	BLE: hold end op=0x03 seq=211: 39 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
   170	Max Current for Motor 0: 4
   171	Max Current for Motor 1: 21
   172	Max Current for Motor 2: 1171
   173	Max Current for Motor 3: 715
   174	BLE: hold end op=0x01 seq=232: 19 refreshes, longest refresh gap 139 ms, longest between passes 0 ms
   175	Step 1: ROLL and PITCH check
   176	Step 2: ROLL Not Level
   177	Leveling...
   178	Max Current for Motor 0: 692
   179	Max Current for Motor 1: 815
   180	Max Current for Motor 2: 1164
   181	Max Current for Motor 3: 714
   182	Latch
   183	Pulse off...
   184	ROLL Leveled successfully...
   185	Exit auto-level
   186	PITCH is disabled and ROLL is level
   187	BLE: hold end op=0x03 seq=11: 33 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
   188	Max Current for Motor 0: 2
   189	Max Current for Motor 1: 756
   190	Max Current for Motor 2: 1143
   191	Max Current for Motor 3: 16
   192	BLE: hold end op=0x01 seq=37: 24 refreshes, longest refresh gap 121 ms, longest between passes 0 ms
   193	Step 1: ROLL and PITCH check
   194	Step 2: ROLL Not Level
   195	Latch
   196	Pulse on...
   197	Pulse off...
   198	ROLL Leveled successfully...
   199	Axis set to PITCH
   200	Leveling...
   201	Max Current for Motor 0: 1
   202	Max Current for Motor 1: 777
   203	Max Current for Motor 2: 1172
   204	Max Current for Motor 3: 12
   205	Latch
   206	Pulse off...
   207	PITCH Leveled successfully...
   208	Exit auto-level
   209	BLE: hold end op=0x03 seq=117: 78 refreshes, longest refresh gap 151 ms, longest between passes 0 ms
   210	Max Current for Motor 0: 754
   211	Max Current for Motor 1: 797
   212	Max Current for Motor 2: 10
   213	Max Current for Motor 3: 17
   214	BLE: hold end op=0x01 seq=139: 20 refreshes, longest refresh gap 120 ms, longest between passes 0 ms
   215	Step 1: ROLL and PITCH check
   216	Step 2: ROLL Not Level
   217	Leveling...
   218	Max Current for Motor 0: 704
   219	Max Current for Motor 1: 774
   220	Max Current for Motor 2: 1141
   221	Max Current for Motor 3: 739
   222	Latch
   223	Pulse off...
   224	ROLL Leveled successfully...
   225	Exit auto-level
   226	PITCH is disabled and ROLL is level
   227	BLE: hold end op=0x03 seq=184: 43 refreshes, longest refresh gap 181 ms, longest between passes 0 ms
   228	Max Current for Motor 0: 745
   229	Max Current for Motor 1: 21
   230	Max Current for Motor 2: 14
   231	Max Current for Motor 3: 752
   232	BLE: hold end op=0x01 seq=205: 19 refreshes, longest refresh gap 149 ms, longest between passes 0 ms
   233	Step 1: ROLL and PITCH check
   234	Step 2: ROLL Not Level
   235	Leveling...
   236	Latch
   237	Pulse off...
   238	ROLL Leveled successfully...
   239	Axis set to PITCH
   240	Leveling...
   241	Max Current for Motor 0: 670
   242	Max Current for Motor 1: 758
   243	Max Current for Motor 2: 1151
   244	Max Current for Motor 3: 681
   245	Latch
   246	PITCH Leveled successfully...
   247	Exit auto-level
   248	BLE: hold end op=0x03 seq=255: 48 refreshes, longest refresh gap 148 ms, longest between passes 0 ms
   249	SW: 0x0800 down (bounces 0)
   250	Max Current for Motor 0: 713
   251	Max Current for Motor 1: 21
   252	Max Current for Motor 2: 15
   253	Max Current for Motor 3: 725
   254	SW: 0x0800 up (bounces 0)
   255	SW: 0x0002 down (bounces 0)
   256	Step 1: ROLL and PITCH check
   257	Step 2: ROLL Not Level
   258	Leveling...
   259	Latch
   260	Pulse off...
   261	ROLL Leveled successfully...
   262	Axis set to PITCH
   263	Leveling...
   264	Max Current for Motor 0: 714
   265	Max Current for Motor 1: 737
   266	Max Current for Motor 2: 1155
   267	Max Current for Motor 3: 701
   268	Latch
   269	PITCH Leveled successfully...
   270	Exit auto-level
   271	SW: 0x0002 up (bounces 0)
```

### d.4 Failed and anomalous commands

- `which arm-none-eabi-size` printed "no arm-none-eabi-size in (...)": the
  toolchain is not on PATH. Resolved by using `DEFAULT_TOOLCHAIN_DIR` from
  `cmake_gcc/autogen_toolchain.cmake:28` (both in d.1).
- `grep -rn -i 'settings_rpc\|54b10005\|gattdb_settings' ...` matched
  nothing (grep exit 1, masked by the following `echo`). That is the B4
  evidence, not an error.
- `git add` printed "LF will be replaced by CRLF" warnings for the five
  committed files (d.1 does not contain the `git add`; it ran outside the
  wrapper, output: five such warnings, then `git status --short` showing the
  five staged files and ` M .projectlinkstore`). The repo's line-ending
  setting; no content change.
- The first `bash` call of the session `cd`-ed into the repo; the harness
  printed "Shell cwd was reset" after several calls. No effect.

## e. VERIFIED vs ASSERTED

### VERIFIED (evidence attached)

| # | Claim | Evidence |
|---|---|---|
| V1 | Start state: HEAD `6796e6b` = `origin/main` after `git fetch`; only `.projectlinkstore` modified and the prompt file untracked | d.1, first command |
| V2 | AA bench log: 11 op=0x03 hold-end lines, 12 op=0x01, 23 total; all read `longest between passes 0 ms`; no WATCHDOG; one Step 1 per AUTO LEVEL hold; gaps 121-181 (op=0x03) / 119-181 (all); physical AUTO LEVEL 255-271 with no hold-end line; `latency=0`, `timeout=500` then `100` | d.1 log commands; d.3 log with line numbers |
| V3 | "Bed is level" printed in none of the 12 runs, and `bed_level` is never set true | `grep -c 'Bed is level'` = 0 (d.1); `grep -n bed_level` -> only `auto_level.c:166`, `:603` (false) and `app.c:681` (read) (d.1) |
| V4 | Collision trip = sum of positive per-motor (moving average - baseline) > raise/lower margin; baseline = peak during a 350-pass window after 100 ignored passes | `motor_current_functions.c:27-29, 153-176, 178-204, 333-352` (d.1 grep) |
| V5 | `dynamic_collision_current_threshold[]` is never read; the continuous re-check uses `dynamic_reading_count[3]` for all motors | d.1 grep: written at `:192, :222, :242`, extern at `.h:25`, no read; `:210` |
| V6 | Auto-level always uses the raise margin | `bed_actions.c:170`; `auto_level.c:771`; `motor_current_functions.c:336-340` |
| V7 | Auto-level's pulsed phase runs no current supervision | `auto_level.c:679-738` (no call), `:770-771` (only when `!pulse_latch`); `switch_input.c:921` (`!auto_level_active`); d.1 grep of `run_motor_supervision` call sites |
| V8 | At a trip, everything except `average_current_delta_single[]`, `latest_motor_current[]` and `security_margin` is zeroed in the same call | `motor_current_functions.c:234-254, 365-370`; `:69` declared outside the reset |
| V9 | A collision during an app hold sends no Command Result and does not clear `app_auto_level_active` | `ble_bed_service.c:578-585` |
| V10 | Move -> outputs table (HEAD 0+3, LEFT 0+1, RIGHT 2+3, FOOT 1+2, ALL and auto-level all four) | `dual_motor_control.c:35-38, 347-428`; `auto_level.c:712-719, 758-765`; supporting log lines 20-23, 104-107, 127-130, 210-213 (d.3) |
| V11 | Level reference: captured by a 2 s AUTO LEVEL hold in settings mode from the auto-level filter's average; keys 1-2, x100 degrees, -1500..1500; erase leaves the live value until reboot | `settings_mode.c:212-218, 305-396`; `auto_level.c:487-497`; `bed_settings.c:35-36, 89-93`; `bed_settings.h:39-40`; `nvm3_functions.h:16-17` |
| V12 | Settings key table (4 keys, defaults/min/max) and the sensitivity ladder | `bed_settings.c:34-39`; `settings_mode.c:29-50` |
| V13 | The pairing window's only tie to settings mode is `ble_bed_service.c:1240` | read of the handler `:1238-1254`; `bed_actions.c:370-373` |
| V14 | Settings RPC 0005: not implemented, not in the GATT database | btconf `:79-121`, `autogen/gatt_db.h:23-28` (d.1); code grep no match (d.1) |
| V15 | Lights: P1_6 `EN_LIGHTING` + amber LED; reported in Bed Status flag bit 2; `app_stop_all()` turns amber off without restoring it | `exp_board.c:373-411, 470-477`; `bed_status.c:145`; `ble_bed_service.c:83`; `bed_actions.c:219` |
| V16 | The auto-level "hardwired 0-3" comment contradicts the code, which goes through `MOTORA-D` | `auto_level.c:622-626` vs `:713-718, 758-765` and `dual_motor_control.c:347-428` |
| V17 | Accelerometer formulas and sign handling | `auto_level.c:43, 264-288, 630-648`; `bed_status.c:129-141`; `bed_status.h:53-55` |
| V18 | No existing single-motor or fixed-time move except auto-level pulses (75/350 ms) and the collision reverse | `bed_control.h:30-34`; `bed_actions.c:262-271`; d.1 grep of `set_motor_on` callers; `auto_level.c:66-67`; `app.c:600-610` |
| V19 | No actuator speed constant in the repo | d.1 grep |
| V20 | No timeout or "axis failed" outcome in auto-level; `leveling_success[]` never read | `auto_level.c:825-827`; d.1 grep |
| V21 | Flash/RAM/NVM3 figures | d.1 `arm-none-eabi-size -A/-B`, map-file symbols, `autogen/linkerfile.ld:30-37, 275-320`, `config/nvm3_default_config.h:14,21,38`, device header `:279-283` |
| V22 | Stack allows 4 connections (configured), firmware tracks 2 | `config/sl_bluetooth_connection_config.h:38`, `config/sl_bluetooth_config.h:40-48`, `autogen/sl_bluetooth.h:41` (d.1); `ble_bed_service.c:192` |
| V23 | Every bench/debug flag and its value | d.1 define grep (six flags, listed in b.11) |
| V24 | PWM 100% at boot, "only for initial testing" comment; PWM 24 kHz; I2C on-board bus at standard mode | `app.c:416-427`; `config/sl_pwm_init_motor1_config.h:44` (all four, d.1); `config/sl_i2cspm_on_board_config.h:44-49` |
| V25 | The application is linked at flash address 0x0 | `autogen/linkerfile.ld:32` (d.1) |
| V26 | Commit A `fad70fc` contains only docs and the prompt file | d.1 `git commit` output (5 files); section c |

### ASSERTED (reasoned, not observed)

| # | Claim | Why it is only asserted |
|---|---|---|
| AS1 | The existing `.out` is the build of the current source | Strong evidence (timestamps, empty source diff since `75b0a61`, text size equals the AA record), but the build that produced it was not observed |
| AS2 | Collision detection is blind for ~0.7-1.0 s at the start of a move | Pass counts are code facts; the time uses DECISIONS #29's auto-level loop rate; the manual-move rate is unmeasured |
| AS3 | Snapshot / transport options and their costs (B1(c)), and the app-entered settings options (B3) | Designs, not implemented |
| AS4 | The app's 0005 subscription does nothing useful | Firmware side reasoned from the GATT database; app code not read |
| AS5 | The board is mounted with its y axis vertical | Inferred from `pitch = atan2(y, z) - 90`; not physically checked |
| AS6 | A Gecko bootloader would need the application moved off address 0x0 | General knowledge of Series 2 bootloader layout; not read in the SDK here |
| AS7 | The heap is mostly Bluetooth-stack allocations; runtime free RAM unknown | Not measured |
| AS8 | Holding a sensitivity button longer than 4 s steps more than one rung | Code reading (`settings_mode.c:408-409, 490-491`); not bench-observed |
| AS9 | The amber LED goes off with the lights still on after STOP_ALL / watchdog / disconnect stop | Code reading; not bench-observed |
| AS10 | Why the HEAD pair did not run in hold 9's PITCH phase (lines 201-204) | Unknown; a stale inferred limit is one possibility |
| AS11 | That the AA fix generalises to the iPhone and to van distances | Not tested (PRE_SHIP F12) |
| AS12 | Timing estimates in B7 (I2C write time, timer resolution) | Arithmetic from config, not measured |

## f. QUESTIONS FOR REVIEW

1. **22 vs 23 hold-end lines.** The prompt says 22; the log has 23 (12
   `op=0x01` + 11 `op=0x03`; lines 126 and 131 are two move holds back to
   back). All 23 read 0 ms, so the substance holds. I recorded 23. Also,
   "119-181 ms" holds over all 23 lines; over the 11 AUTO LEVEL holds it is
   121-181. I recorded both. OK?
2. **"Bed is level" never prints.** `bed_level` is never set true
   (`auto_level.c:166, 603`), so the `Bed is level` branch at `app.c:681`
   is dead and REGRESSION 6.1 / 6.8 ("start to 'Bed is level'") can never
   pass as written. Fix the firmware (set it when both axes level), or reword
   6.1/6.8 to `Exit auto-level`?
3. **Auto-level's pulsed phase has no current supervision** (B1(d), V7):
   no collision, max-load or limit inference once `Latch` prints. Pre-ship
   item? (Listed under "also recorded" in PRE_SHIP.md, not as a row.)
4. **The continuous re-baseline does nothing to the trip** (V5): it updates
   an array nothing reads, and counts on motor 3's counter. Intended
   behaviour ("lower the threshold if a weight is removed") never reaches
   `collision_check()`. Leave for the #29 / B5 redesign, or a row now?
5. **Auto-level uses the raise margin for every motor**, including the two
   that are lowering (V6). Intended?
6. **Hold 9 (lines 201-204): PITCH levelled with the HEAD pair at 1 and
   12 mA.** Cause not established (AS10). Worth a bench look with the limit
   prints enabled?
7. **Bootloader and flash layout.** The app is linked at 0x0 (V25). Adding
   the Gecko bootloader (PRE_SHIP F17) likely needs the app relinked
   (configurator) - and the image is now ~240 KiB, not the ~53 KB
   BLE_GATT_DESIGN §10 assumed. Should F17 become its own design prompt?
8. **Amber indicator after STOP_ALL / watchdog / disconnect stop** (AS9):
   the lights stay on but amber goes off and stays off. Fix, or accept?
9. **Collision during an app hold** (V9): no Command Result is sent, and
   `app_auto_level_active` stays true, so the app's repeats keep being
   accepted silently after the collision. Should a collision produce a
   result code (B1(c) option 2), and clear the app's hold state?
10. **Settings-mode details** found in B2/B3: erase is not effective until a
    reboot; a held sensitivity button keeps stepping every 2 s (AS8);
    entering settings mode toggles the under-bed lights. Any of these
    wanted as PRE_SHIP rows?
11. **PRE_SHIP.md structure:** the owner assignments, the closing rule, and
    the separate "also recorded, not yet pre-ship" table - accept, or move
    those five items into rows?
12. **Unconditional console prints.** With every flag at 0, the firmware
    still prints `BLE SM: configure/...` (`ble_bed_service.c:1089, 1101,
    1109`), `connection_parameters` (`:1270`), `Max Current for Motor`
    (`motor_current_functions.c:198`), `Once`, and the auto-level step lines.
    A PRE_SHIP row to decide which stay?

## g. NOT DONE

- **No source change, no build** - as the prompt specifies. B9 uses an
  existing build output (AS1).
- **Bench runs:** none (the user drives the bench). Everything in section b
  about behaviour on the bed is from code, except the log-backed points.
- **The app repo was not opened.** The app rows in PRE_SHIP.md and B4's app
  side rest on the prompt's wording.
- **MIGRATION_STATUS.md not updated** (not asked; its Stage C text was
  already corrected on 2026-10-23).
- **Raw output of file-reading commands** used while reading code and docs is
  not reproduced in full: the `Read`-tool reads of `motor_current_functions.c/.h`,
  `adc_currents.c`, `app.c`, `switch_input.c`, `bed_actions.c`,
  `dual_motor_control.c/.h`, `auto_level.c/.h`, `settings_mode.c`,
  `bed_settings.c`, `nvm3_functions.c`, `bed_control.c`, `bed_status.c/.h`,
  `ble_bed_service.c/.h`, `exp_board.c`; and `sed -n` reads of
  `docs/MIGRATION_STATUS.md:1-80`, `docs/DECISIONS.md` (#7 `:85-100`, #9
  `:104-115`, #29 `:631-690, 975-990`, #42 `:1843-1893`, #43-#45
  `:2036-2201`), `docs/REGRESSION.md:172-244`, `docs/BLE_GATT_DESIGN.md`
  (`:59-110, 207-235, 317-334, 379-421`), `docs/SESSION_LOG.md:1395-1469`,
  the Prompt AA handoff (`:309-446, 2158-2213`), the Prompt W handoff
  (`:2010-2016`), the Prompt V handoff (`:1427-1428`), and the 2026-08-05_03
  handoff (`:104-110, 471`). Their output is the content of those files at
  `6796e6b` (none changed this session except the four docs in commit A).
  Every claim in section b that rests on them is cited by file:line, and the
  key lines were re-grepped in d.1. The first state check and the first
  log counts were run before the command logger existed; both were re-run
  inside it (d.1, first commands) and gave the same results.
- **The push** happens after this file is committed, so its output cannot be
  in this file. If it fails, the error is reported in a follow-up handoff
  (the rule: no merge, rebase or reset).
