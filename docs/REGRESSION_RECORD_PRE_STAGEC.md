# REGRESSION_RECORD_PRE_STAGEC.md — post-Stage-B regression record gating Stage C

Full [REGRESSION.md](REGRESSION.md) pass gating BLE Stage C (Command
characteristic → motors + watchdog, `BLE_GATT_DESIGN.md` §4-5) — run with a
phone bonded and subscribed to Bed Status throughout, per REGRESSION.md's
post-Stage-B requirement (its own preamble, added after DECISIONS #26/#27).
This is a working copy to check boxes in — REGRESSION.md itself stays
unchecked, per its own instruction.

**Not the DECISIONS #20 pre-BLE isolation record.** `REGRESSION_RECORD_D1.md`
was scoped to a full pass on the migrated SDK *before any BLE glue was
written*, isolating migration regressions from BLE regressions. That pass was
never run — BLE Stage B glue landed first — and cannot be run now that BLE
code exists in the tree; filling that file in retroactively would misrepresent
what it was for. This record is the actual gate for Stage C: a full pass on
current code, with a phone connected and subscribed the whole time, since
that is the condition both known BLE-drop bugs (DECISIONS #26/#27/#30)
actually reproduced under. See `REGRESSION_RECORD_D1.md`'s own pointer and
DECISIONS #20's added note for the full cross-reference.

## Test record header

| Field | Value |
|---|---|
| Date | 07/19/2026 |
| Firmware version (`version.h`) | 1.1.0 |
| Git commit | `af369f2` |
| Build config (Debug/Release) | Debug |
| Tester | Andrew Duncan |
| Bed/frame unit | Test Bed |
| Phone connected & subscribed (Bed Status) | Yes, continuous through the full pass |
| Notes (ambient, supply voltage) | Not separately recorded this run |

**Build config is Debug.** `EFM_ASSERT` compiles out in Release, and this
project's own bring-up history (DECISIONS #26, the `get_motor_faults()`
blocking-I2C bug) and the Task G I2C-health bisection (DECISIONS #30) both
depended on assertions/instrumentation that behave differently, or not at
all, in Release. **A Release-config confirmation pass is still owed before
Stage C ships** — this record does not stand in for one.

## 0. Setup / preconditions

- [x] Bench supply at nominal voltage, current limit set above expected peak
      (all four motors + margin)
- [x] Debug UART (VCOM) connected and logging to a file
- [x] Bed frame clear of obstructions, all four actuators mid-stroke
      (not at either limit)
- [x] Switch Board and (if fitted) Aux Board connected
- [x] Test obstruction available (rigid block for fast collision, pillow/box
      for soft collision)
- [x] Known NVM3 state recorded: note the four loaded values printed at boot

## 1. Power-on & initialization

- [x] ⚡ Welcome banner and version print on UART
- [x] ⚡ "NVM3 initialized successfully" prints
- [x] ⚡ Loaded Roll / Pitch / Raise / Lower values print and match expected
      stored values
- [x] "Accelerometer is sampling ..." prints
- [x] Expander-confirm LED sequence runs (red → green → blue → amber)
- [x] No motor moves at power-on

## 2. Manual motor control (button matrix)

For each row: press-and-hold → correct motor pair runs in correct direction
and the LED lights; release → motors stop, LED off. Watch the motor pair
*physically*, not just the LED.

| # | Button | Expected motors | Direction | LED | Hold OK | Release OK |
|---|---|---|---|---|---|---|
| 2.1 ⚡ | UP HEAD | A + D | up | green | [x] | [x] |
| 2.2 | UP LEFT | A + B | up | green | [x] | [x] |
| 2.3 | UP RIGHT | C + D | up | green | [x] | [x] |
| 2.4 | UP FOOT | B + C | up | green | [x] | [x] |
| 2.5 ⚡ | DOWN HEAD | A + D | down | green | [x] | [x] |
| 2.6 | DOWN LEFT | A + B | down | green | [x] | [x] |
| 2.7 | DOWN RIGHT | C + D | down | green | [x] | [x] |
| 2.8 | DOWN FOOT | B + C | down | green | [x] | [x] |
| 2.9 ⚡ | UP ALL | all four | up | blue | [x] | [x] |
| 2.10 ⚡ | DOWN ALL | all four | down | blue | [x] | [x] |

- [x] ⚡ Quick-tap (<0.5 s) on one side button: motors pulse briefly and stop
      cleanly; system accepts the next press normally
- [x] Rapid repeated taps (5×) on one button: no stuck-on motors, no stuck LEDs
- [x] Press one button while another is already held: second press does not
      start additional motors
- [x] ⚡ BLE link stays up (no disconnect) through the button matrix

## 2b. Per-motor PWM timer mapping (D1 migration, DECISIONS #25) — one-time at D1

Run once on the migrated firmware (and after any future PWM/timer config
change). Verifies the new one-motor-per-timer mapping (TIMER0/1/2/3) is
wired to the correct physical actuators.

- [x] motor1 alone: UP then DOWN — correct actuator, correct direction,
      normal speed (24 kHz PWM, no audible whine change vs. baseline)
- [x] motor2 alone: UP then DOWN — same checks
- [x] motor3 alone: UP then DOWN — same checks
- [x] motor4 alone: UP then DOWN — same checks
- [x] All four together (ALL UP / ALL DOWN): speeds visually matched,
      no motor noticeably faster/slower than the others

## 3. Double-press rejection

- [x] Press two motor buttons as close to simultaneously as possible:
      motors stop / never start, "Double Press Error" prints, LEDs clear
- [x] After releasing both, a single button press works normally
- [x] Repeat with an UP + a DOWN button combination

## 4. Limit switch inference

- [x] Drive one side UP until the actuators reach their physical upper limit:
      motors stop within ~1 s of the actuator internal switch cutting current
- [x] Same side DOWN to lower limit: stops
- [x] After a limit stop, the opposite direction still works immediately
- [x] ⚡ UP ALL until the first actuator reaches its limit: all motors pause
      (~1.5 s temp-off), then the remaining motors resume; sequence ends with
      all four at limit and all motors off
- [x] DOWN ALL equivalent of the above
- [x] Move away from a limit, then back toward it: limit is re-detected
      (flags cleared and re-set correctly)
- [x] ⚡ BLE link stays up (no disconnect) through the ALL UP/DOWN
      limit pause/resume sequence

## 5. Collision detection

- [x] ⚡ Raise one side into a rigid obstruction: motors stop, all LEDs blink
      (3 cycles), motors briefly reverse starting at blink 2, then everything
      stops and LEDs clear
- [x] Lower one side onto a rigid obstruction: same trip sequence
- [x] Soft obstruction (pillow/box) while raising: trips via baseline-margin
      element (may take longer than rigid — confirm it does trip)
- [x] Collision during auto-level: motors stop, LED sequence runs
      (note: no reverse in auto-level — known limitation, REFACTOR_PLAN P1.5)
- [x] ⚡ After the collision sequence completes, normal button operation
      resumes without power cycle
- [x] Collision sensitivity sanity: at HIGH rung, the rigid-obstruction trip
      is noticeably faster/lighter than at LOW rung
- [x] (Optional, destructive-adjacent — skip in routine runs) Max-load
      ceiling: verify `TRIPPED MAX LOAD` on a deliberately stalled motor
- [x] ⚡ BLE link stays up (no disconnect) through the collision sequence

## 6. Auto-level

Starting condition for 6.1: frame tilted several degrees in both roll and
pitch, all actuators clear of limits.

- [x] ⚡ 6.1 Hold AUTO_LEVEL: red LED blinks; ROLL levels first, then PITCH;
      motors stop; green LED on; "Bed is level" prints
- [x] 6.2 Verify with an independent level/inclinometer that the frame is
      actually level (within tolerance) after 6.1
- [x] 6.3 Bed already level: AUTO_LEVEL exits promptly with green LED,
      no motor motion beyond brief checks
- [x] 6.4 Release AUTO_LEVEL mid-leveling: motors stop immediately, LEDs
      clear, system returns to idle
- [x] 6.5 One axis level / other tilted: only the tilted axis is corrected
      (skipped axis reported as disabled on UART)
- [x] 6.6 Near-level start (within pulse tolerance): leveling uses pulsed
      "sneak" moves (Pulse on/off prints), no overshoot oscillation
- [x] 6.7 Auto-level respects limits: with one actuator at its limit on the
      driven axis, leveling stops that pair and does not fight the limit
- [x] ⚡ 6.8 BLE link stays up (no disconnect) through a full auto-level
      run, start to "Bed is level" - the original auto-level BLE-drop bug
      (DECISIONS.md #26) only manifested on a sustained multi-second move,
      so a quick/near-level test is not sufficient coverage for this check

## 7. Settings mode

- [x] ⚡ 7.1 Hold BED_LIGHTS 5 s: amber LED fast-blinks — settings mode active
- [x] 7.2 Zero-level store: hold AUTO_LEVEL; red LED on; after 2 s green LED
      on; UART prints stored ROLL/PITCH values
- [x] 7.3 Zero-level erase: keep holding AUTO_LEVEL 5 more s; blue LED on;
      "Erased Angles" prints
- [x] 7.4 Sensitivity up: hold UP_ALL 2 s per step from lowest rung; LED
      pattern steps red → blue → green (and all-three for OFF rung when
      stepping down); UART confirms NVM3 writes at each step
- [x] 7.5 Sensitivity down: hold DOWN_ALL 2 s per step through all rungs
- [x] 7.6 Mismatch recovery: (if reproducible) mismatched raise/lower rungs
      force both to HIGH with green LED
- [x] ⚡ 7.7 Persistence: power cycle; boot print shows the values stored in
      7.2/7.4/7.5
- [x] 7.8 Motor buttons in settings mode do NOT move motors
- [x] 7.9 Exit: power cycle returns to normal mode (settings mode has no
      soft exit — known, REFACTOR_PLAN P3.13)
- [x] ⚡ BLE link stays up (no disconnect) through settings mode entry,
      zero-level store/erase, and sensitivity adjustment

## 8. Bed lights

- [x] ⚡ Tap BED_LIGHTS: under-bed lighting on, amber LED on
- [x] Tap again: lighting off, amber LED off
- [x] Lights state survives a motor move and a collision sequence
      (amber restored after collision blink)

## 9. Cross-checks & UART health

- [x] No unexpected resets during the entire run (watch for repeated welcome
      banner)
- [x] No stuck LEDs at the end of any section
- [x] ⚡ End-of-run: all motors off, bed responds to a fresh button press
- [x] Save the UART log with the test record

## Result

| Section | Pass/Fail | Notes |
|---|---|---|
| 1 Power-on | PASS | |
| 2 Manual control | PASS | |
| 3 Double-press | PASS | |
| 4 Limit switches | PASS | |
| 5 Collision | PASS | |
| 6 Auto-level | PASS | |
| 7 Settings | PASS | |
| 8 Bed lights | PASS | |
| 9 Cross-checks | PASS | |

**Full pass clean; BLE link held with no disconnects across the entire
subscribed run.**

**Overall:** PASS — Signed: Andrew Duncan
