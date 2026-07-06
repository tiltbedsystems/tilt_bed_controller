# REGRESSION.md — bench regression checklist

Formal hardware checklist for the refactor phases in
[REFACTOR_PLAN.md](REFACTOR_PLAN.md). Run the **full pass** at every phase
boundary (end of Phase A, B, C) and after step B5 specifically. Items marked
⚡ form the **smoke subset** for per-step verification between boundaries.

Copy this file's checklist into the test record (or print it) per run —
don't check boxes in the committed copy.

## Test record header

| Field | Value |
|---|---|
| Date | |
| Firmware version (`version.h`) | |
| Git commit | |
| Build config (Debug/Release) | |
| Tester | |
| Bed/frame unit | |
| Notes (ambient, supply voltage) | |

## 0. Setup / preconditions

- [ ] Bench supply at nominal voltage, current limit set above expected peak
      (all four motors + margin)
- [ ] Debug UART (VCOM) connected and logging to a file
- [ ] Bed frame clear of obstructions, all four actuators mid-stroke
      (not at either limit)
- [ ] Switch Board and (if fitted) Aux Board connected
- [ ] Test obstruction available (rigid block for fast collision, pillow/box
      for soft collision)
- [ ] Known NVM3 state recorded: note the four loaded values printed at boot

## 1. Power-on & initialization

- [ ] ⚡ Welcome banner and version print on UART
- [ ] ⚡ "NVM3 initialized successfully" prints
- [ ] ⚡ Loaded Roll / Pitch / Raise / Lower values print and match expected
      stored values
- [ ] "Accelerometer is sampling ..." prints
- [ ] Expander-confirm LED sequence runs (red → green → blue → amber)
- [ ] No motor moves at power-on

## 2. Manual motor control (button matrix)

For each row: press-and-hold → correct motor pair runs in correct direction
and the LED lights; release → motors stop, LED off. Watch the motor pair
*physically*, not just the LED.

| # | Button | Expected motors | Direction | LED | Hold OK | Release OK |
|---|---|---|---|---|---|---|
| 2.1 ⚡ | UP HEAD | A + D | up | green | [ ] | [ ] |
| 2.2 | UP LEFT | A + B | up | green | [ ] | [ ] |
| 2.3 | UP RIGHT | C + D | up | green | [ ] | [ ] |
| 2.4 | UP FOOT | B + C | up | green | [ ] | [ ] |
| 2.5 ⚡ | DOWN HEAD | A + D | down | green | [ ] | [ ] |
| 2.6 | DOWN LEFT | A + B | down | green | [ ] | [ ] |
| 2.7 | DOWN RIGHT | C + D | down | green | [ ] | [ ] |
| 2.8 | DOWN FOOT | B + C | down | green | [ ] | [ ] |
| 2.9 ⚡ | UP ALL | all four | up | blue | [ ] | [ ] |
| 2.10 ⚡ | DOWN ALL | all four | down | blue | [ ] | [ ] |

- [ ] ⚡ Quick-tap (<0.5 s) on one side button: motors pulse briefly and stop
      cleanly; system accepts the next press normally
- [ ] Rapid repeated taps (5×) on one button: no stuck-on motors, no stuck LEDs
- [ ] Press one button while another is already held: second press does not
      start additional motors

## 2b. Per-motor PWM timer mapping (D1 migration, DECISIONS #25) — one-time at D1

Run once on the migrated firmware (and after any future PWM/timer config
change). Verifies the new one-motor-per-timer mapping (TIMER0/1/2/4) is
wired to the correct physical actuators.

- [ ] motor1 alone: UP then DOWN — correct actuator, correct direction,
      normal speed (24 kHz PWM, no audible whine change vs. baseline)
- [ ] motor2 alone: UP then DOWN — same checks
- [ ] motor3 alone: UP then DOWN — same checks
- [ ] motor4 alone: UP then DOWN — same checks
- [ ] All four together (ALL UP / ALL DOWN): speeds visually matched,
      no motor noticeably faster/slower than the others

## 3. Double-press rejection

- [ ] Press two motor buttons as close to simultaneously as possible:
      motors stop / never start, "Double Press Error" prints, LEDs clear
- [ ] After releasing both, a single button press works normally
- [ ] Repeat with an UP + a DOWN button combination

## 4. Limit switch inference

- [ ] Drive one side UP until the actuators reach their physical upper limit:
      motors stop within ~1 s of the actuator internal switch cutting current
- [ ] Same side DOWN to lower limit: stops
- [ ] After a limit stop, the opposite direction still works immediately
- [ ] ⚡ UP ALL until the first actuator reaches its limit: all motors pause
      (~1.5 s temp-off), then the remaining motors resume; sequence ends with
      all four at limit and all motors off
- [ ] DOWN ALL equivalent of the above
- [ ] Move away from a limit, then back toward it: limit is re-detected
      (flags cleared and re-set correctly)

## 5. Collision detection

- [ ] ⚡ Raise one side into a rigid obstruction: motors stop, all LEDs blink
      (3 cycles), motors briefly reverse starting at blink 2, then everything
      stops and LEDs clear
- [ ] Lower one side onto a rigid obstruction: same trip sequence
- [ ] Soft obstruction (pillow/box) while raising: trips via baseline-margin
      element (may take longer than rigid — confirm it does trip)
- [ ] Collision during auto-level: motors stop, LED sequence runs
      (note: no reverse in auto-level — known limitation, REFACTOR_PLAN P1.5)
- [ ] ⚡ After the collision sequence completes, normal button operation
      resumes without power cycle
- [ ] Collision sensitivity sanity: at HIGH rung, the rigid-obstruction trip
      is noticeably faster/lighter than at LOW rung
- [ ] (Optional, destructive-adjacent — skip in routine runs) Max-load
      ceiling: verify `TRIPPED MAX LOAD` on a deliberately stalled motor

## 6. Auto-level

Starting condition for 6.1: frame tilted several degrees in both roll and
pitch, all actuators clear of limits.

- [ ] ⚡ 6.1 Hold AUTO_LEVEL: red LED blinks; ROLL levels first, then PITCH;
      motors stop; green LED on; "Bed is level" prints
- [ ] 6.2 Verify with an independent level/inclinometer that the frame is
      actually level (within tolerance) after 6.1
- [ ] 6.3 Bed already level: AUTO_LEVEL exits promptly with green LED,
      no motor motion beyond brief checks
- [ ] 6.4 Release AUTO_LEVEL mid-leveling: motors stop immediately, LEDs
      clear, system returns to idle
- [ ] 6.5 One axis level / other tilted: only the tilted axis is corrected
      (skipped axis reported as disabled on UART)
- [ ] 6.6 Near-level start (within pulse tolerance): leveling uses pulsed
      "sneak" moves (Pulse on/off prints), no overshoot oscillation
- [ ] 6.7 Auto-level respects limits: with one actuator at its limit on the
      driven axis, leveling stops that pair and does not fight the limit

## 7. Settings mode

- [ ] ⚡ 7.1 Hold BED_LIGHTS 5 s: amber LED fast-blinks — settings mode active
- [ ] 7.2 Zero-level store: hold AUTO_LEVEL; red LED on; after 2 s green LED
      on; UART prints stored ROLL/PITCH values
- [ ] 7.3 Zero-level erase: keep holding AUTO_LEVEL 5 more s; blue LED on;
      "Erased Angles" prints
- [ ] 7.4 Sensitivity up: hold UP_ALL 2 s per step from lowest rung; LED
      pattern steps red → blue → green (and all-three for OFF rung when
      stepping down); UART confirms NVM3 writes at each step
- [ ] 7.5 Sensitivity down: hold DOWN_ALL 2 s per step through all rungs
- [ ] 7.6 Mismatch recovery: (if reproducible) mismatched raise/lower rungs
      force both to HIGH with green LED
- [ ] ⚡ 7.7 Persistence: power cycle; boot print shows the values stored in
      7.2/7.4/7.5
- [ ] 7.8 Motor buttons in settings mode do NOT move motors
- [ ] 7.9 Exit: power cycle returns to normal mode (settings mode has no
      soft exit — known, REFACTOR_PLAN P3.13)

## 8. Bed lights

- [ ] ⚡ Tap BED_LIGHTS: under-bed lighting on, amber LED on
- [ ] Tap again: lighting off, amber LED off
- [ ] Lights state survives a motor move and a collision sequence
      (amber restored after collision blink)

## 9. Cross-checks & UART health

- [ ] No unexpected resets during the entire run (watch for repeated welcome
      banner)
- [ ] No stuck LEDs at the end of any section
- [ ] ⚡ End-of-run: all motors off, bed responds to a fresh button press
- [ ] Save the UART log with the test record

## Result

| Section | Pass/Fail | Notes |
|---|---|---|
| 1 Power-on | | |
| 2 Manual control | | |
| 3 Double-press | | |
| 4 Limit switches | | |
| 5 Collision | | |
| 6 Auto-level | | |
| 7 Settings | | |
| 8 Bed lights | | |
| 9 Cross-checks | | |

**Overall:** PASS / FAIL — Signed: ____________
