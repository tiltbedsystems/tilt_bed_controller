# REGRESSION.md — bench regression checklist

Formal hardware checklist for the refactor phases in
[REFACTOR_PLAN.md](REFACTOR_PLAN.md). Run the **full pass** at every phase
boundary (end of Phase A, B, C) and after step B5 specifically. Items marked
⚡ form the **smoke subset** for per-step verification between boundaries.

**Since BLE Stage B (advertising + read-only telemetry, bench-verified), run
the full pass with a phone connected, bonded, and subscribed to Bed Status
notifications throughout - not as a separate BLE-only test.** The radio is
now active during every bed operation, not just idle. Both BLE-drop bugs
found so far (the Bed Status subscribe-time drop and the auto-level
sustained-move drop, DECISIONS.md #26) were triggered by ordinary bed
operation with a phone connected, not by anything BLE-specific in isolation
- a regression pass run without a subscribed phone would not have caught
either one. Each section below with hardware-in-the-loop timing has its own
"BLE link stays up" check; a disconnect during any of them is a regression
regardless of whether the bed behavior itself looks correct.

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
| Phone connected & subscribed (Bed Status) | |
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
- [ ] ⚡ BLE link stays up (no disconnect) through the button matrix

## 2b. Per-motor PWM timer mapping (D1 migration, DECISIONS #25) — one-time at D1

Run once on the migrated firmware (and after any future PWM/timer config
change). Verifies the new one-motor-per-timer mapping (TIMER0/1/2/3) is
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

## 3b. Switch sampler (DECISIONS #41)

The one-switch-reader sampler (`switch_input.c`). Build with
`SWITCH_SAMPLER_DIAG 1` so the `SW:` lines print, and
`BED_BENCH_PROVOKE_RUNAWAY 0` except for 3b.5. The bounce count in
`(bounces N)` is whatever the switch does (0 for a clean contact).

- [ ] 3b.1 Single press and release (e.g. UP HEAD, mask 0x8000), about 1 s
      hold: `SW: 0x8000 down (bounces 0)` ~10-20 ms after pressing and the
      motor moves; `SW: 0x8000 up (bounces 0)` ~10-20 ms after releasing and
      the motor stops. No `FAILSAFE:`, no `Double Press Error`, no
      `Unrecognized switch mask`, no `SW: ... failed`.
- [ ] 3b.2 Two buttons pressed together from idle (e.g. UP HEAD + UP LEFT,
      0x8000 and 0x4000): `SW: 0x4000 down`, then `SW: 0x8000 down` (lowest
      bit first), then `SW: 2 switches down - presses ignored`. No motor
      moves, no `Double Press Error`. On release two `SW: 0x.... up` lines and
      no `FAILSAFE:`.
- [ ] 3b.3 Second button pressed while the first is held and moving:
      `SW: 0xXXXX down` for the second, `SW: 2 switches down - presses
      ignored`, then `Double Press Error` and `  down mask 0xC000, 2 switches
      down` (for HEAD+LEFT); motors stop. Both releases print `SW: ... up`; no
      `FAILSAFE:`.
- [ ] 3b.4 BED LIGHTS held 5 s (mask 0x0008): `SW: 0x0008 down` at the press,
      the lights toggle and settings mode is entered at 5 s as in section 7,
      `SW: 0x0008 up` on release. No `Unrecognized switch mask`, no
      `FAILSAFE:`.
- [ ] 3b.5 Provoked runaway (`BED_BENCH_PROVOKE_RUNAWAY` hand-flipped to 1,
      and back to 0 afterwards). Tap UP HEAD: `SW: 0x8000 down`,
      `SW: 0x8000 up`, ~300 ms later `PROVOKE: energised HEAD pair with
      nothing commanding - fail-safe should stop this`, then `FAILSAFE: motors
      energised with nothing commanding - stopped`, then `PROVOKE: fail-safe
      caught it after N ms`. Tap UP ALL (0x0001): the same with
      `ALL MOTORS`; N should be close to the HEAD-pair figure. Repeat several
      times; there must be no `FAILSAFE:` without a `PROVOKE:` just before it.
- [ ] 3b.6 Power-up with a button held: no `SW:` line and no motion at boot;
      the later release prints `SW: ... up` and does nothing; the next press
      works.
- [ ] 3b.7 Idle: no `SW:` lines at all with no button touched.

**Recorded result, 2026-10-18 (firmware `d1f13d0`, debounce 3 samples,
log `sampler_bench.log`; full figures in DECISIONS #41 "Bench result"):**

| Item | Result |
|---|---|
| 3b.1 | ~700 accepted edges, no `FAILSAFE:` outside a provocation, no `Unrecognized switch mask`, no `read failed` |
| 3b.2 / 3b.3 | 67 two-button episodes across 30 pairs: all printed `presses ignored`; 57 also printed `Double Press Error` (one press had been delivered first, brief start, then stop); 10 moved nothing; every release came through |
| Bounce | 696 edges with 0 bounces, 4 with 1 |
| 3b.5 HEAD | caught after 209, 248, 209, 209, 209 ms |
| 3b.5 ALL | caught after 209, 210, 210, 217, 249 ms (was 271-307 ms before the sleep veto) |
| 3b.6 | UP HEAD held through reset: no motion; release printed `SW: 0x8000 up (bounces 0)`; next press worked |
| 3b.4, 3b.7 | not separately recorded |

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
- [ ] ⚡ BLE link stays up (no disconnect) through the ALL UP/DOWN
      limit pause/resume sequence

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
- [ ] ⚡ BLE link stays up (no disconnect) through the collision sequence

**Collisions during app holds (DECISIONS #48).** App connected as CONTROL,
`BLE_DEBUG_WATCHDOG_LOG` 1, UART logged. Use the rigid obstruction; one
pair is enough. With an app that does not yet handle COLLISION_STOP (0x06)
it keeps repeating while the finger is down, which is the case these items
test. With an app that does handle it, the hold ends at the first 0x06 and
the latch clears at once (`stop`) or 300 ms later (`silence`); the motors
must do the same in both cases.

- [ ] 5.A1 Collision during an app **MOVE** hold (B1). Raise into the
      obstruction from the app, keep the finger down ~3 s after the trip,
      release, wait, press again (away from the obstruction).
      Observe: trip lines, then **in the same burst** `BLE: COLLISION_STOP
      seq=N op=0x01 (latched)` and `BLE: hold end op=0x01 seq=N ...`; then
      `Once`; LED blink; the brief reverse at blink 2 (expect a
      `FAILSAFE: motors energised with nothing commanding - stopped` line
      during it - ASSERTED, see #48); everything off at ~1.5 s. **The pair
      does not re-energise toward the obstruction at any point while the
      finger is held.** On release: `BLE: collision latch cleared (stop), K
      repeats refused` with K ≈ 10 per second held. The new press moves
      normally (no 0x06). App: 0x06 for the held button (if it shows result
      codes); Bed Status bit 0 set for ~1.5 s.
- [ ] 5.A2 Collision during an app **AUTO LEVEL** hold (B2). Obstruct a
      levelling pair, keep holding ~3 s after the trip, release, press again
      (obstruction removed). Observe: trip lines, `BLE: COLLISION_STOP seq=N
      op=0x03 (latched)`, `BLE: hold end op=0x03 ...`, `Once`; LED blink, **no
      reverse and no motion at all** until the release; `BLE: collision latch
      cleared (stop), K repeats refused`. The new press starts a fresh
      auto-level run (`Step 1` prints).
- [ ] 5.A3 Release and re-press inside the collision sequence (B3). As 5.A1,
      but release ~0.5 s after the trip and press the **same** button again at
      once, and hold. Observe: `BLE: collision latch cleared (stop), K repeats
      refused` at the release; **nothing moves toward the obstruction while
      the LEDs blink** (the reverse at blink 2 may still run); at the end of
      the sequence (~1.5 s after the trip) an app that is still repeating
      **starts the move again** - expected under #48 (the re-press did not
      set the latch), with supervision running, so it trips again at the
      obstruction. Record the time from the end of the blink to the move.
- [ ] 5.A4 STOP_ALL during the latch (B5). As 5.A1, but while still holding
      after the trip press the app's STOP (STOP_ALL) - once inside the blink
      sequence, once after it. Observe: `BLE: collision latch cleared
      (stop_all), K repeats refused`; inside the sequence nothing moves
      toward the obstruction until it ends; after it, an app still repeating
      the held move **starts the move again** on its next repeat (as STOP_ALL
      during any ordinary hold). App: 0x00 for the STOP_ALL.
- [ ] 5.A5 Physical-controller collision unchanged: raise into the obstruction
      from the switch panel with the app connected and idle. No `BLE:
      COLLISION_STOP` line, no Command Result; Bed Status bit 0 set for
      ~1.5 s. An app MOVE press made and released during the blink gets 0x06
      and starts nothing.

## 6. Auto-level

Starting condition for 6.1: frame tilted several degrees in both roll and
pitch, all actuators clear of limits.

- [ ] ⚡ 6.1 Hold AUTO_LEVEL: red LED blinks; ROLL levels first, then PITCH;
      motors stop; green LED on; `Exit auto-level` then "Bed is level"
      print (DECISIONS #46: it prints on every successful end, after the
      outcome's own line), and `LEVELED (Bed Status bit 4) -> 1`
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
- [ ] ⚡ 6.8 BLE link stays up (no disconnect) through a full auto-level
      run, start to "Bed is level" (prints since DECISIONS #46; before it,
      use `Exit auto-level`) - the original auto-level BLE-drop bug
      (DECISIONS.md #26) only manifested on a sustained multi-second move,
      so a quick/near-level test is not sufficient coverage for this check
- [x] 6.9 App AUTO LEVEL (DECISIONS #45): 10 holds from off-level, no
      `BLE: WATCHDOG STOP` / no "Connection stalled" in the app. With
      `BLE_DEBUG_WATCHDOG_LOG` 1, each hold that ends normally prints one
      `BLE: hold end op=0x03 ...` line; "longest between passes" should be
      ~0-1 ms.

**Recorded result, 2026-11-01 (firmware `c945146`, `BLE_DEBUG_WATCHDOG_LOG`
1, desk bed, iPhone with app TestFlight build `e22e4b9`, log
`autolevel_stall_bench.log`).** VERIFIED (user's bench run). **App-driven
auto-level FAILS: intermittent watchdog stops** (the app's "Connection
stalled - movement stopped").

| Check | Result |
|---|---|
| Connection parameters (iPhone) | `interval=24 latency=0 timeout=72` on connect, then `timeout=100` after the bed's request (lines 13, 15-16; same at 152/154, 160/162, 174/176) |
| WATCHDOG STOP blocks | six: lines 28, 53, 58, 63, 122, 127, all `op=0x03` (AUTO_LEVEL_START) |
| Since last refresh / last write received | 300-301 ms / 301 ms, with the **same seq** as the last refresh in all six: nothing reached the application for 300 ms |
| Received->processed, longest pass | 0-1 ms; 2-17 ms: the bed was not busy |
| Passes, longest between passes | 23-39 passes in the 300 ms, longest gap between passes 27-28 ms: the CPU was sleeping between passes (a free-running loop makes hundreds of passes) |
| Refresh gaps before the stop | bunched, e.g. `180,0,0,0,207,0,148,28` (line 61), `20,2,1,1,112` (line 66), `19,1,0,0,83` (line 130): writes arriving late, then several in the same millisecond |
| Where the stops fell | every one in a stretch of an app auto-level hold with no motor on: Step 1 (lines 52-53, 57-58, 62-63, 121-122, 126-127) and the pause after an axis finishes (line 28 follows `ROLL Leveled successfully...`, line 27). None while a motor ran, none on the physical controller |
| App side | the app's Hold diagnostics for the same holds show a START every 100-103 ms (max gap 102-103 ms, err 0) (user's report) |
| Elsewhere | the same symptom on a second bed (Tilt Van) and with a Galaxy S23 (user's report) |
| Completed runs | auto-level still completed on later attempts (e.g. lines 67-85, 94-112, 131-149) |

The reviewer's reading, **a hypothesis, not a finding**: while the CPU
sleeps the link sometimes misses the phone's packets, which are then retried
and delivered in a burst (a sleep-clock accuracy or EM2 wake-up timing issue
being a candidate). Tested by keeping the CPU awake during app holds
(Prompt AA commit B).

**Recorded result, 2026-11-01 (firmware `75b0a61`, `BLE_DEBUG_WATCHDOG_LOG`
1, desk bed, Galaxy S23 SM-S911U, log `prompt_aa_bench.log`, 271 lines).**
VERIFIED (user's bench run). **6.9 PASSES** - 11 app AUTO LEVEL holds, no
watchdog stop. Full record: DECISIONS #45 "Bench result (2026-11-01, Galaxy
S23)".

| Check | Result |
|---|---|
| App AUTO LEVEL holds | 11 `BLE: hold end op=0x03` lines (40, 61, 85, 103, 121, 151, 169, 187, 209, 227, 248), each preceded by exactly one `Step 1` |
| WATCHDOG STOP | none in the log |
| "Connection stalled" in the app | none (user's report) |
| Longest between passes | `0 ms` on all 23 hold-end lines (12 `op=0x01` + 11 `op=0x03`; the prompt said 22) |
| Longest refresh gap per hold | 121-181 ms over the AUTO LEVEL holds; 119-181 ms over all 23 |
| Physical AUTO LEVEL (`SW: 0x0002`, lines 255-271) | levelled, no hold-end line |
| Connection parameters | `latency=0`, `timeout=500` then `timeout=100` (lines 15, 17, 19) |

The fix is verified; the mechanism is not (the device probably slept in EM1,
not EM2 - Prompt AA handoff C4). Not covered: the iPhone (the original
failing phone), and a realistic distance in the van (`docs/PRE_SHIP.md`).
"Bed is level" did not print in any of the twelve runs (6.1 expects it) -
open question, Prompt AC handoff.

- [ ] 6.10 LEVELED, Bed Status flag bit 4 (DECISIONS #46). With
      `BED_DEBUG_LEVELED_LOG` 1 each change prints one line
      `LEVELED (Bed Status bit 4) -> 0|1`; with the app subscribed, Bed
      Status bit 4 follows it.
  - [ ] 6.10a Physical AUTO LEVEL from off-level to success: `-> 1` next to
        `Bed is level`. Keep holding 3 s, then release: no further LEVELED line.
  - [ ] 6.10b The same from the app's AUTO LEVEL button.
  - [ ] 6.10c With it set, press any move (switch or app, one side or ALL):
        `-> 0` on the press. Set it again, then press AUTO LEVEL: `-> 0` on
        the press.
  - [ ] 6.10d Bed already level (6.3): `-> 1` with `Bed is disabled, no
        leveling required` and `Bed is level`.
  - [ ] 6.10e With it set, hold BED LIGHTS 5 s into settings mode: `-> 0`
        at entry.
  - [ ] 6.10f With it set, STOP_ALL from the app: no LEVELED line (it stays 1).

## 7. Settings mode

- [ ] ⚡ 7.1 Hold BED_LIGHTS 5 s: amber LED fast-blinks — settings mode active.
      The under-bed lights do not change at the press, during the hold,
      at entry, or at the release (DECISIONS #47)
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
- [ ] 7.9 Exit: after releasing the entry hold, press BED_LIGHTS once.
      "Settings mode: exit" prints; amber stops fast-blinking and shows the
      under-bed lights state (on if the lights are on, off if not); red,
      green and blue off; the under-bed lights themselves do not change.
      Holding that press does not re-enter settings mode, and its release
      does nothing. Afterwards one press of each button works normally.
      A power cycle also exits.
- [ ] 7.10 Pairing window (DECISIONS #42/#43). Build with
      `BLE_DEBUG_SECURITY_LOG` 1. Note the phone model in the test record.
  - [ ] 7.10a Bonded phone reconnects, bed NOT in settings mode:
        `connection_opened conn=N bonding=0xNN` (not `0xff`), then
        `security_mode=1`, and no `confirm_bonding` line. The app connects.
  - [ ] 7.10b Refusal: delete the bond on the phone, bed NOT in settings
        mode, connect from the app: `confirm_bonding ... -> REJECTED (not in
        settings mode)`, `bonding_failed ... reason=0x1205`, `BLE: closing
        conn=N - pairing refused`, then exactly one `connection_closed
        conn=N` (0x1022 on the 2026-10-18 S23 run; 0x1016 or 0x1013 are also
        possible depending on which side finishes first). The phone shows a
        pairing failure, not a hang.
  - [ ] 7.10c Acceptance in settings mode: enter settings mode (7.1), connect
        from the app: `confirm_bonding ... -> ACCEPTED (settings mode)`,
        then `bonded conn=N`. The app connects.
  - [ ] 7.10d Reconnect after exit: exit settings mode (7.9), close and
        reopen the app: `connection_opened ... bonding=0xNN`,
        `security_mode=1`, no `confirm_bonding` line, and the app can drive
        the bed.
- [ ] 7.11 STOP during a settings hold (DECISIONS #44). In settings mode,
      hold UP ALL: `SW: 0x0001 down`, `2 second timer start`. Before 2 s,
      press STOP in the app (it gets Command Result ACCEPTED; no PuTTY line
      for STOP). Keep holding past 2 s, then release: no `Writing values to
      NVM3...`, no `Settings mode: exit`, no ladder LED, amber keeps
      fast-blinking. Hold UP ALL again: the step happens only after a full
      2 s. Repeat for DOWN ALL and for AUTO LEVEL (no `Writing values to
      NVM3...` after the STOP; the next AUTO LEVEL hold needs the full 2 s).
- [ ] ⚡ BLE link stays up (no disconnect) through settings mode entry,
      zero-level store/erase, and sensitivity adjustment

**Recorded result, 2026-10-18 (firmware `944f19e`, debounce 2 samples,
log `prompt_v_bench.log`; full figures in DECISIONS #42 "Bench result").**
This section has no pairing-window checklist item yet; the pairing rows
below record the DECISIONS #42 checks the user ran alongside 7.9.

| Item | Result |
|---|---|
| 7.9 | `Settings mode: exit` four times, each on one new BED LIGHTS press; afterwards every button's press and release came through and a full auto-level completed |
| Pairing, bonded phone reconnects | no `confirm_bonding` line, reached `security_mode=1` (before and after a settings-mode session) |
| Pairing refused (phone bond deleted, not in settings mode) | `-> REJECTED (not in settings mode)`, `bonding_failed reason=0x1205`, then the phone closed the link (`connection_closed reason=0x1013`); phone showed `GATT_AUTH_FAIL` (status 137) |
| Pairing in settings mode | `-> ACCEPTED (settings mode)`, then `bonded` |
| 7.1-7.8, BLE link item | not separately recorded |

**Recorded result, 2026-10-18 (Prompt W firmware: code of `840d7b8`,
`BLE_DEBUG_SECURITY_LOG` 1, phone Samsung Galaxy S23 SM-S911U, log
`prompt_w_bench.log`; recorded 2026-10-23, full figures in DECISIONS #43 and
#44 "Bench result").** VERIFIED (user's bench run). Items 7.10 and 7.11 were
added after this run, on 2026-10-23; the rows below are the checks the user
ran that those items now cover.

| Item | Result |
|---|---|
| Boot | `store_bonding_configuration -> 0x0000` (policy 2 accepted; eviction not observed) |
| 7.10a | `connection_opened conn=1 bonding=0x01`, `security_mode=1`, no `confirm_bonding` |
| 7.10b | `-> REJECTED (not in settings mode)`, `bonding_failed reason=0x1205`, `closing conn=2 - pairing refused`, one `connection_closed conn=2 reason=0x1022` (`SL_STATUS_BT_CTRL_LL_RESPONSE_TIMEOUT`, link-layer procedure timeout; 0x1016 had been predicted) |
| 7.10c | `-> ACCEPTED (settings mode)`, then `bonded conn=3` |
| 7.10d | not in this log (Prompt V row above) |
| 7.11 (UP ALL only) | two UP ALL holds with no NVM3 write (STOP pressed in the app: user's report; STOP prints nothing); the next full hold wrote 350/250; DOWN ALL then wrote 550/275 |

**Recorded result, 2026-11-01 (firmware `c945146`, desk bed, iPhone with
app TestFlight build `e22e4b9`, log `autolevel_stall_bench.log`; full figures
in DECISIONS #43 "Bench result (2026-11-01, iPhone)").** VERIFIED (user's
bench run), except where marked as the user's report.

| Item | Result |
|---|---|
| 7.10a | `connection_opened conn=1 bonding=0x02`, `security_mode=1`, no `confirm_bonding` (lines 12-17) |
| 7.10b | twice (lines 151-158, 159-166): `-> REJECTED (not in settings mode)`, `bonding_failed reason=0x1205`, `closing conn=N - pairing refused`, one `connection_closed reason=0x1016` each; iPhone showed "CBATTErrorDomain Code=15 Encryption is insufficient" (user's report) |
| 7.10c | `-> ACCEPTED (settings mode)`, then `bonded conn=4` (lines 173-180) |
| 7.10d | not in this log |
| Two phones | a second phone connected but could not control the bed (user's report; DECISIONS #34's viewer role - not in this log) |

## 8. Bed lights

- [ ] ⚡ Tap BED_LIGHTS: under-bed lighting on, amber LED on - **on the
      release**, not the press (DECISIONS #47). Hold it ~1 s to see it: nothing
      changes until you let go
- [ ] Tap again: lighting off, amber LED off, on the release
- [ ] 8.a Hold BED_LIGHTS ~4.5 s, release: the lights toggle on the release,
      no settings mode (amber does not fast-blink)
- [ ] 8.b Hold BED_LIGHTS 5 s into settings mode (amber fast-blinks), keep
      holding 1 s, release: lighting unchanged throughout. Repeat with the
      lights on and with them off
- [ ] 8.c In settings mode, press BED_LIGHTS once: `Settings mode: exit`,
      amber shows the lights state, lighting unchanged at the press and at
      the release
- [ ] 8.d App holding a move (or AUTO LEVEL): press and hold BED_LIGHTS -
      the bed stops **on the press**; release: the lights do not change.
      Note what the app's still-held button does next (DECISIONS #47; a
      lights press does not latch the #36 inhibit, so the app's next repeat
      may start the bed again - record it)
- [ ] Lights state survives a motor move and a collision sequence
      (amber restored after collision blink)
- [ ] Lights on, app STOP_ALL (with and without a move running): lighting
      and amber both stay on (Prompt AE part C; before, amber went off). The
      same after a watchdog stop and after the control phone disconnects
      mid-hold. A brief amber flicker at the stop is possible: the stop turns
      every LED off and then amber back on, as app MOVE_STOP already does
      (DECISIONS #36 "The indicator blip").

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
| 3b Switch sampler | | |
| 4 Limit switches | | |
| 5 Collision | | |
| 6 Auto-level | | |
| 7 Settings | | |
| 8 Bed lights | | |
| 9 Cross-checks | | |

**Overall:** PASS / FAIL — Signed: ____________
