# fw_HANDOFF_2026-11-02_01 — Prompt AE: LEVELED bit, amber after STOP_ALL, BED LIGHTS on release, PRE_SHIP rows; Part A stopped and reported

Firmware repo session of 2026-11-02 (system date `Mon Nov  2 2026`, checked
with `date` at the start, 07:05 PST, and again before writing this file,
07:19 PST). Prompt: `docs/prompts/PROMPT_AE_collision-result-leveled-bit-lights-release.md`
(committed in `ef36909`).

## a. Header

| | |
|---|---|
| Repo | `tilt_bed_controller` (firmware) |
| Branch | `main` |
| HEAD before | `01315cd` (Handoff 2026-11-01 02: Prompt AC), level with `origin/main` after `git fetch` |
| HEAD after the work | `e3cc865` (commit E) |
| Commits | `ef36909` (B), `f3a762e` (C), `3fb1a0c` (D), `e3cc865` (E). **No commit for Part A** - see b.A |
| After that | this handoff and the session's SESSION_LOG entry, committed together, then `git push` |
| Builds | from the command line after each commit (the prompt permits it): all exit 0, no warnings (d.2) |

`git status` at the end of the work (after commit E and the SESSION_LOG
entry, before this file was written):

```
On branch main
Your branch is ahead of 'origin/main' by 4 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

no changes added to commit (use "git add" and/or "git commit -a")
```

`.projectlinkstore` is the Studio file the prompt says never to commit; it is
not committed. `docs/SESSION_LOG.md` is committed with this file.

**What was asked and what was done**

| Part | Asked | Done |
|---|---|---|
| A | COLLISION_STOP 0x06 for a collision during an app hold; clear `app_move_active` / `app_auto_level_active` so the following repeats start nothing; docs | **Not implemented - stopped and reported, as the prompt instructs.** Clearing the two flags does *not* make the repeats start nothing: it makes them start the bed again. A further rule is needed. Options and a recommended design: b.A. Also found: **today's firmware already restarts a held app MOVE into the obstruction about 100 ms after the trip** (from code). |
| B | Bed Status bit 4 = LEVELED; notify on change; "Bed is level" prints on success; docs | Done, `ef36909`. DECISIONS #46 |
| C | Amber restored after `app_stop_all()` when the lights are on | Done, `f3a762e` (with a settings-mode guard - QUESTIONS 3) |
| D | BED LIGHTS toggles on release; press still force-stops an app move; docs | Done, `3fb1a0c`. DECISIONS #47 |
| E | PRE_SHIP rows; rulings into SESSION_LOG | Done, `e3cc865` (F20-F25, F17 extended); rulings in the SESSION_LOG entry |
| F | Expected bench lines; a print for bit 4 | b.F; the print is `LEVELED (Bed Status bit 4) -> 0/1`, behind new `BED_DEBUG_LEVELED_LOG` 1 (PRE_SHIP F19) |

## b. Designs and analysis

File:line references are to `e3cc865` unless marked. Every claim is marked
VERIFIED (read in code/docs, or a command output in section d) or ASSERTED.
Nothing in this session ran on the bed.

---

### b.A Part A - why it stopped, and the options

#### b.A.1 What the code does today at a collision during an app hold

VERIFIED from code:

1. A trip (summed rise, `collision_check()`, or max load, `max_load_check()`)
   turns all motors off and sets `any_collision = true`
   (`motor_current_functions.c:306-307`, `:322-323`, `:366-367`). Both are
   called only from `run_motor_supervision()` (`:387`, `:397`), which runs in
   `switch_input_tick()` (`switch_input.c:934-939`) or inside auto-level's
   `level_axis()` (`auto_level.c:770-771`) - both before
   `ble_bed_service_tick()` in the same main-loop pass (`app.c:626-773`
   order: auto-level block, `settings_mode_tick()`, `switch_input_tick()`,
   ..., `ble_bed_service_tick()`).
2. In that pass, `service_move_watchdog()` sees `any_collision` and does
   only this (`ble_bed_service.c:579-586`):
   ```c
   move_watchdog_cancel();
   app_move_active = false;
   return;
   ```
   No Command Result. `app_auto_level_active` is left as it was.
3. The collision sequence runs in `app.c:580-620`: all LEDs blink at 500 ms,
   the reverse starts at blink 2 (~1 s), all off and `active_switch = OFF`,
   `any_collision = false` at blink 3 (~1.5 s).
4. **App MOVE hold, next repeat** (the app repeats MOVE_START every 100 ms
   while the button is held). In `process_pending_command()`
   (`ble_bed_service.c:757-794`): `app_move_active` is false, so
   `bed_move_start(BED_SOURCE_APP, ...)` is called (`:775`). In
   `bed_control.c:97-111`: `conflict_inhibit` is false; `arbitrate()` -
   the bed is not idle (`active_switch` still holds the move's value until
   blink 3) and `active_source` is APP, so `ARBITRATE_NORMAL`;
   `app_move_start()` runs. Back in the service, `app_apply_move_outputs()`
   (`:783`) calls `set_dual_motors_on()` / `set_all_motors_on()`
   (`app_apply_move_outputs()` in `bed_actions.c`). **Nothing on this path
   reads `any_collision`** (`grep any_collision`, d.1: the only readers are
   `app.c`, `auto_level.c:772`, `bed_status.c:143`, `ble_bed_service.c:81,
   579`, `motor_current_functions.c:390, 401`, `switch_input.c:517, 934`).
   So the pair is **re-energised in the original direction, into the
   obstruction, ~100 ms after the trip**, with current supervision gated off
   (`switch_input.c:934` requires `!any_collision`). The sequence's reverse
   then takes over at blink 2 and everything stops at blink 3. After that,
   `app_move_active` is true again (set by the restart), the next identical
   repeat only refreshes the watchdog, `service_move_watchdog()` then sees
   the bed idle with `app_move_active` set and clears it (`:588-600`), and
   the repeat after that starts the move again - now with supervision, so
   it drives into the obstruction until the next trip (after the ~450-pass
   blind window, PRE_SHIP F20). **ASSERTED** (code reading only). I found no
   bench record of a collision during a held app move that says what
   happened next (`grep` of DECISIONS, SESSION_LOG and
   REGRESSION_RECORD_PRE_STAGEC, d.1).
5. **App AUTO LEVEL hold, next repeat.** `app_auto_level_active` is still
   true, so the repeat only re-arms the watchdog (`:817-831`) and starts
   nothing; `auto_level_active` was cleared by the collision block
   (`app.c:590`). The bed stays still until the release (AUTO_LEVEL_STOP).
   This is already the behaviour the prompt wants - by accident.

#### b.A.2 What A2 as written would do

A2: "clear `app_move_active` and `app_auto_level_active`, so the app's
following repeats start nothing". From the code above:

- `app_move_active` is **already** cleared today, and that is exactly what
  lets the MOVE repeat restart the move (b.A.1 item 4). Clearing it changes
  nothing.
- Clearing `app_auto_level_active` would make the next AUTO_LEVEL_START
  repeat take the `!app_auto_level_active` branch (`:817`) and call
  `bed_auto_level_start()` - a fresh auto-level run (`app_auto_level_start()`
  resets the state machine, sets `auto_level_active = true` and
  `active_switch = SWITCH_AUTO_LEVEL_VALUE`) - so auto-level would start
  again during or right after the collision sequence. That is a regression
  against today.

So the prompt's intended behaviour ("the bed must not move again until the
user releases and presses again") **cannot come from clearing the flags; it
needs a further rule**, and the prompt says: "If the code cannot give that
without a further rule, stop and report the options." Nothing in Part A was
implemented. A1 alone (sending 0x06) was not implemented either: A is one
unit ("tell the app, end the hold"), its documentation (A4) has to describe
the rule, and the rule is the reviewer's choice.

What the held button's next repeat does **today** (the answer A2 asked for,
for the current code): MOVE - accepted (0x00) and **restarts the move**;
AUTO LEVEL - accepted (0x00) and ignored (re-arms the watchdog only).

#### b.A.3 Options for the further rule

All run in main-loop context (`process_pending_command()` /
`service_move_watchdog()`), add no I2C and block nothing.

**Option 1 (recommended) - a collision latch on the app path, in
`ble_bed_service.c`.**

Design:

```c
#define CMD_RESULT_COLLISION_STOP 0x06

// Set when a collision trips during an app hold; the user is still holding
// the app button. Every motion start from the app is answered
// COLLISION_STOP and starts nothing until the app has released.
static bool app_collision_latched = false;

// service_move_watchdog(), replacing ble_bed_service.c:579-586:
if (any_collision) {
    move_watchdog_cancel();
    app_move_active = false;
    app_auto_level_active = false;     // A2
    app_collision_latched = true;
    // Motors were turned off at the trip, earlier in this pass (A1).
    send_command_result(control_connection, last_move_seq,
                        CMD_RESULT_COLLISION_STOP);
    return;
}

// process_pending_command(), after the viewer and settings-mode checks and
// BEFORE the #36 conflict check, for MOVE_START and AUTO_LEVEL_START only:
if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
    && (app_collision_latched || any_collision)) {
    note_app_commanding();   // still holding: keeps the silence timer alive
    send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);
    return;                  // no bed_control call, no watchdog arm
}

// Cleared (app_collision_latched = false) in:
//   MOVE_STOP, AUTO_LEVEL_STOP (explicit release),
//   STOP_ALL, the control-phone disconnect stop,
//   note_app_stopped_commanding() - i.e. MOVE_WATCHDOG_MS (300 ms) of
//   silence from the app, which covers a lost stop write.
```

What the held button's next repeat does under Option 1: **rejected with
COLLISION_STOP (0x06), its own seq; starts nothing; does not arm the
watchdog.** That continues until the app releases (stop write, or 300 ms of
silence). The next press after that is a normal MOVE_START /
AUTO_LEVEL_START - except during the ~1.5 s collision sequence itself,
where the `|| any_collision` term refuses it too (a deliberate quick
release-and-press inside the sequence would otherwise re-energise as in
b.A.1 item 4).

Properties:

- The physical controller is not affected: the latch is app-side only, and a
  physical collision sets no latch because the watchdog is not armed (A3: no
  Command Result; the app sees Bed Status bit 0 as today).
- The `|| any_collision` term also covers the trip pass's ordering:
  `process_pending_command()` runs before `service_move_watchdog()` in the
  same tick (`ble_bed_service.c:1034-1036`), so a repeat processed in the
  trip pass, before the latch is set, is refused too (today it would be an
  identical repeat and only refresh the watchdog, so this is belt and
  braces).
- No WATCHDOG_STOP follows a COLLISION_STOP: the watchdog is cancelled at the
  trip and not re-armed while latched. The DECISIONS #45 EM0 veto
  (`ble_bed_service_app_hold_active()` = watchdog armed) ends with it; nothing
  is moving, so nothing needs the loop awake.
- App UI: the collision screen from the first 0x06 until the user releases.
- Cost: one bool, one new result code, ~15 lines in `ble_bed_service.c`;
  BLE_GATT_DESIGN §7 row 0x06; DECISIONS entry; REGRESSION §5 items (a
  collision during an app MOVE hold and an app AUTO LEVEL hold: one 0x06,
  the held button does nothing, release + press works after the sequence).

**Option 2 - reuse the #36 two-source conflict latch.** At a trip during an
app hold, set bed_control's `conflict_inhibit` (a new
`bed_note_collision()`), so the app's repeats are refused by the existing
check (`ble_bed_service.c:747-752`) until both sources release. Cheaper, but:
the repeats get FORCE_STOPPED_OTHER_SOURCE (0x01), which tells the user "wall
switch is in control" - wrong; the latch also refuses the **physical**
controller until the app releases, changing physical behaviour; and it mixes
a collision into the two-source mechanism the host test covers. Not
recommended.

**Option 3 - keep the flags set instead of clearing them.** Leave
`app_auto_level_active` set (as today) and add an equivalent "ignore
repeats" state for MOVE. For MOVE this is Option 1 under another name,
because `service_move_watchdog()` clears `app_move_active` when the bed goes
idle at blink 3 (`:588-600`) and the restart follows; and it contradicts A2's
wording. Not recommended.

**Option 4 - app only.** Send 0x06 and have the app stop repeating until a
fresh press. Not sufficient on its own: a lost notification, or an app that
does not handle 0x06, leaves today's restart in place. The firmware rule is
needed whatever the app does.

#### b.A.4 Until Part A lands

From b.A.1 item 4 (ASSERTED): **do not bench-test a collision while holding
an app MOVE button** - from the code, the bed drives back into the
obstruction. A collision during an app AUTO LEVEL hold, or with the physical
controller, does not have this problem.

---

### b.B Part B - LEVELED (Bed Status flag bit 4) and "Bed is level"

**Design** (DECISIONS #46, BLE_GATT_DESIGN §6 "LEVELED (bit 4)").

State: one bool, `bed_leveled`, in `bed_actions.c` (which already owns
`bedlights_on`), with `app_set_leveled(bool)` and `app_leveled(void)`
(`bed_actions.c:43-68`, declared in `bed_actions.h:62-65`). Not `bed_level`
(`auto_level.c:166`): `reset_level_data()` clears `bed_level`, and
`app_auto_level_stop()` calls it on the release (`bed_actions.c`
`app_auto_level_stop()`), while bit 4 must survive the release.

Set (`app_set_leveled(true)`):

| Outcome (AC B8) | Where |
|---|---|
| 1 both axes levelled | `app.c:687-688`, success branch |
| 2 Step 1: both already within tolerance | `app.c:646-647`, "no leveling required" branch |
| 3 ROLL disabled, PITCH levelled | `app.c:687-688` (same branch) |
| 4 PITCH disabled, ROLL levelled | `app.c:687-688` (same branch) |

Both branches run once per run (each sets `auto_level_active = false`, which
gates the whole auto-level block, `app.c:626`). Either source: both paths
run the same block.

Cleared (`app_set_leveled(false)`):

| Event | Where | Covers |
|---|---|---|
| Any move | `app_move_start()`, `bed_actions.c:143` | switch and app moves, single side and ALL (the switch path reaches it via `bed_move_start()`, `bed_control.c:109`) |
| New auto-level run | `app_auto_level_start()`, `bed_actions.c:185` | both sources (`bed_control.c:137`) |
| Collision trip | end of `run_motor_supervision()`, `motor_current_functions.c:399-404` | every trip, summed rise or max load, in the trip's pass |
| Collision reverse | `app.c:605`, before `reverse_motor_direction_and_start()` | belt and braces: the trip has already cleared it |
| Settings-mode entry | `settings_mode.c:287` | the 5 s BED LIGHTS hold |

Not cleared by: release, STOP, STOP_ALL, watchdog stop, disconnect stop,
lights, REFRESH_TILT, a refused motion command. 0 at boot.

Wire: `bed_status_t.leveled` (`bed_status.h:63`), filled by
`bed_status_get()` (`bed_status.c:146`), encoded
`if (in->leveled) flags |= 0x10;` (`ble_bed_service.c:85`). Byte 0 is in the
4-byte `memcmp` of `status_flags_changed()` (`ble_bed_service.c:978-989`), so a change notifies in the same pass - no notify-logic change
(B2). VERIFIED from code.

Prints (B3): both success branches now set `bed_level = true` and print
`Bed is level` after their existing lines. The success branch's old chain
`if (bed_level) ... else if (ROLL disabled ...) ... else if (PITCH disabled
...)` became the two `else if` lines first, then `if (bed_level)`. Resulting
lines per outcome (ASSERTED, from code):

| Outcome | Lines, in order |
|---|---|
| 1 | `LEVELED (Bed Status bit 4) -> 1`, `Exit auto-level`, `Bed is level` |
| 2 | `LEVELED (Bed Status bit 4) -> 1`, `Bed is disabled, no leveling required`, `Bed is level` |
| 3 | `LEVELED ... -> 1`, `Exit auto-level`, `ROLL is disabled and Pitch is level`, `Bed is level` |
| 4 | `LEVELED ... -> 1`, `Exit auto-level`, `PITCH is disabled and ROLL is level`, `Bed is level` |

(The `LEVELED` line comes first because `app_set_leveled()` is called before
the `#if !BED_CAPTURE_MOTOR_CURRENTS` print block. If ROLL was disabled and
both end level, outcome 1 also prints the "ROLL is disabled" line; before,
the chain would have printed only "Bed is level" had it been reachable.)

Bench print (part F): `BED_DEBUG_LEVELED_LOG` 1 (`bed_actions.c:46`), one
line per change of the flag, never per pass; suppressed with
`BED_CAPTURE_MOTOR_CURRENTS` 1. PRE_SHIP F19 turns it off before release.
Built at 1 and at 0 (d.2).

Trade-offs / choices (QUESTIONS 2): "Bed is level" in all four success
outcomes (so it marks every success and matches bit 4), rather than only
outcome 1. A move whose pair is then refused at an inferred limit still
counts as "motion started" (bed_control accepted it). The bench-only runaway
provocation does not clear the bit.

Docs: BLE_GATT_DESIGN §6 (bit table, "LEVELED (bit 4)" subsection, a
protocol-version paragraph: additive, no version bump), DECISIONS #46,
REGRESSION 6.1/6.8 reworded and 6.10 added, PRE_SHIP F19 and the "also
recorded" row marked fixed. Full text in c.2.

---

### b.C Part C - amber after `app_stop_all()`

**Design.** In `app_stop_all()` (`bed_actions.c:239`), right after
`set_all_LED_off()`:

```c
if (bedlights_on && !in_settings_mode) {
  set_LED_on(LED_AMBER);
}
```

Same pattern as the collision sequence (`app.c:613-615`) and app MOVE_STOP
(`app_apply_stop_outputs()`, `bed_actions.c` "under-bed lighting survives a
move"). The lighting output (`EN_LIGHTING`) is not touched - it never was.
Callers: STOP_ALL, watchdog expiry, control-phone disconnect stop
(`ble_bed_service.c:671, 620, 1027`). VERIFIED from code.

The `!in_settings_mode` guard is mine (QUESTIONS 3): in settings mode amber is
the settings-mode fast blink, which `settings_mode_tick()` keeps toggling
(`settings_mode.c:293-295`, `app.c:329-342`); turning it on there would not
show the lights and would shift the blink's phase. With the guard, settings
mode behaves exactly as before.

Known side effect, unchanged class: `set_all_LED_off()` then amber on is the
off-then-on sequence DECISIONS #36 "The indicator blip" describes; MOVE_STOP
and the collision end already do it. A brief amber flicker at a STOP_ALL is
possible (REGRESSION §8 item says so). Avoiding it would mean clearing only
red/green/blue instead of `set_all_LED_off()`, i.e. not "exactly as" the
prompt's reference paths - not done.

---

### b.D Part D - BED LIGHTS toggles on release

**Design** (DECISIONS #47).

Press, normal mode (`switch_input.c:665-677`):

```c
switch_held = SWITCH_BED_LIGHT_VALUE;
if (bed_lights_set(BED_SOURCE_SWITCHES, app_lights_on())) {   // current state: no change
    switch_driving = SWITCH_BED_LIGHT_VALUE;
}
```

Passing the lights' **current** state keeps every effect of the press except
the toggle: arbitration runs now (an app move is force-stopped on the press,
D1, via `bed_control.c:156-164` -> `app_stop_movement()`), and on success
`app_lights_set()` sets `active_switch = SWITCH_BED_LIGHT_VALUE`
(`bed_actions.c:228-233`), which the settings-mode entry test needs
(`settings_mode.c:277-278`). No new bed_control verb; `bed_control.c`
unchanged.

Release, normal mode (`:678-689`):

```c
switch_held = OFF;
settings_mode_cancel_entry();
if (switch_driving == SWITCH_BED_LIGHT_VALUE) {          // the press was accepted
    bed_lights_set(BED_SOURCE_SWITCHES, !app_lights_on()); // the toggle
}
```

`process_outputs()` (`:557-570`): the press arm is gone; the release arm
writes amber and `EN_LIGHTING` from `app_lights_on()`, then clears
`switch_driving` and `active_switch` as before.

Release in settings mode: unchanged - `settings_mode_process_outputs()`
(`settings_mode.c:257-261`) only sets `active_switch = OFF`. Exit press in
settings mode: unchanged - `settings_mode_exit()` clears `switch_held` and
`switch_driving` (`settings_mode.c:160`), so that press's release matches
nothing in normal mode and toggles nothing.

**D2 - "short press".** Released before the settings-mode entry completes,
i.e. the release is delivered while `in_settings_mode` is still false.
Entry completes when `settings_mode_tick()` acts on the 5 s timer
(`settings_mode.c:279-289`), which runs before `switch_input_tick()` in each
pass and tests the sampler's debounced state (still "down" until the release
is debounced, 10-20 ms after the contact opens). The 5 s counts from the pass
after the debounced press.

- Released at 4.9 s: settings mode not entered; normal-mode release ->
  **toggle**.
- Released at 5.1 s: settings mode entered at 5.0 s (+ at most one pass);
  settings-branch release -> **no toggle**, settings mode active.

ASSERTED (code reading; the boundary itself was not measured).

**D3 - every other path, after the change** (ASSERTED from code):

| Path | Before | After |
|---|---|---|
| Double press during a BED LIGHTS hold | check runs (`switch_input.c:934-990`: `active_switch` set, switches hold the bed); `Double Press Error`, panel cleared; lights had toggled on the press | same check; panel cleared, so the BED LIGHTS release matches nothing -> **no toggle** |
| BED LIGHTS pressed while another button is down | press not delivered (sampler ignores presses with two down, `:1016-1021`) | unchanged, no toggle |
| Motion fail-safe | no motor; a held BED LIGHTS counts as a switch down | unchanged |
| Sampler release-first ordering | release of BED LIGHTS delivered before another press in the same sample | unchanged order; the toggle and `active_switch = OFF` now happen inside the release delivery, before the other press |
| Arbitration, press | app holding -> force-stop, no toggle | identical (force-stop on the press, D1); the refused press sets no `switch_driving`, so the release does nothing |
| Arbitration, release | release only cleared `switch_driving` / `active_switch` - if the app had taken the bed during the hold, that cleared `active_switch` under a running app move | a second bed_control call: normally the switches still hold the bed and it toggles; if the app took the bed, the release force-stops the app's movement instead and does not toggle |
| App LIGHTS_SET while BED LIGHTS is held | force-stopped (0x01); lights had already toggled on the press; `active_switch` -> OFF ends the settings entry | force-stopped (0x01); lights unchanged; settings entry ends; the physical release then toggles - **even after 5 s**, because settings mode was never entered (QUESTIONS 4) |
| Collision flagged during a BED LIGHTS hold (F6, no motor on - not expected) | press during `any_collision`: `bedlights_on` toggled, output not written | same gap moved to the release. Not fixed |
| An app move or AUTO LEVEL hold, then a BED LIGHTS press | force-stop on the press; lights do not latch the #36 inhibit (`bed_control.c:153-164`), so the app's next repeat (~100 ms) starts the bed again while BED LIGHTS is still held | **unchanged** (QUESTIONS 5); REGRESSION 8.d asks the bench to record it |

Docs: DECISIONS #47; REGRESSION 7.1 reworded; §8 first two items reworded
(toggle on the release) and 8.a-8.d added - the four items D4 asked for:
short press toggles on release (8 items 1-2, 8.a), 5 s hold enters settings
mode with lights unchanged (8.b, 7.1), press in settings mode exits with
lights unchanged (8.c), BED LIGHTS press stops an app move immediately (8.d).

---

### b.E Part E - PRE_SHIP rows

Added after F19 (B's bench flag), each with owner and how it is closed
(full rows in c.2, `e3cc865`):

| Row | Item | Owner |
|---|---|---|
| F20 | ~450-pass collision blind window at the start of every move (AC B1(a)) | Claude (design), Claude Code |
| F21 | Auto-level pulse phase has no current supervision (Q3); the "also recorded" row now points here | Claude (design), Claude Code |
| F22 | Continuous re-baseline never reaches the trip test (Q4) | Claude (design), Claude Code |
| F23 | Auto-level uses the raise margin for the lowering motors (Q5) | Claude decides, Claude Code changes |
| F24 | Unconditional console prints to review before release (Q12) | Claude decides, Claude Code changes |
| F25 | Hold 9: HEAD pair apparently did not run during PITCH levelling (Q6) | **Andrew + Claude**; closed by a bench look with limit prints |
| F17 | Extended: bootloader and flash layout need their own design (Q7) | Claude (plan, own design prompt), Andrew |

Line references in these rows are at `6796e6b`, the file's stated base, like
the rest of PRE_SHIP.md (`bed_actions.c:170`, `ble_bed_service.c:1089,
1101, 1109, 1270` are the AC handoff's numbers at that commit).

Rulings recorded in the SESSION_LOG entry: Q1 accepted (23 lines, both gap
ranges); Q10 - erase-needs-reboot fixed with the settings work (Phase 2),
held repeat-stepping stays; Q11 - PRE_SHIP.md structure accepted.

---

### b.F Expected lines for the reviewer's bench steps

Build: `e3cc865` (or the handoff commit), `BED_DEBUG_LEVELED_LOG` 1,
`SWITCH_SAMPLER_DIAG` 1, `BLE_DEBUG_WATCHDOG_LOG` 1. All ASSERTED from code.

**How to see bit 4 in the log:** the line `LEVELED (Bed Status bit 4) -> 1`
(or `-> 0`), printed by `app_set_leveled()` on every change of the flag, and
only on a change. The wire bit takes the flag's value in the same main-loop
pass (`ble_bed_service_tick()` runs last in `app_process_action()`) and, with
a phone subscribed, is notified at once. In the app's raw Bed Status, byte 0
gains 0x10 (e.g. `0x00 -> 0x10`, or `0x04 -> 0x14` with the lights on).

1. **Physical AUTO LEVEL that succeeds** (bed off-level, LEVELED 0 at start):
   ```
   SW: 0x0002 down (bounces N)
   Step 1: ROLL and PITCH check
   ... (the existing Step 2 / Max Current / Latch / Pulse lines, unchanged)
   ROLL Leveled successfully...
   Axis set to PITCH
   ...
   PITCH Leveled successfully...
   LEVELED (Bed Status bit 4) -> 1
   Exit auto-level
   Bed is level
   SW: 0x0002 up (bounces N)
   ```
   If LEVELED was 1 at the start (a second run), `LEVELED (Bed Status bit 4)
   -> 0` prints right after `SW: 0x0002 down`. No LEVELED line at the
   release. Green LED on until release.
2. **App AUTO LEVEL that succeeds:** the same levelling lines without the
   `SW:` lines; `LEVELED ... -> 1`, `Exit auto-level`, `Bed is level`; on
   release `BLE: hold end op=0x03 seq=N: ...`. No LEVELED line at the
   release; bit 4 stays 1 in Bed Status. (If one axis was already level, the
   `ROLL is disabled and Pitch is level` / `PITCH is disabled and ROLL is
   level` line comes between `Exit auto-level` and `Bed is level`; if both
   were, `Bed is disabled, no leveling required` then `Bed is level`.)
3. **Collision during an app MOVE hold.** Part A is **not implemented**, so
   **no COLLISION_STOP is sent**, and per b.A.1 the held button restarts the
   move. **Recommended: do not run this step until Part A lands** (b.A.4).
   What the code prints today, for the record:
   ```
   Tripped on steady!            (or: TRIPPED MAX LOAD)
   Securit margin: N
   Motor 0 Delta Current:  X     (x4)
   BLE: hold end op=0x01 seq=N: ...
   Once
   ```
   then no Command Result; the held button's repeats are ACCEPTED (0x00).
   Under Option 1 (b.A.3), it would be: the same trip lines and
   `BLE: hold end op=0x01 ...`; Command Result `{last seq, 0x06}`; every
   later repeat `{its seq, 0x06}`; nothing moves except the collision
   reverse; after release + a fresh press (after the sequence), normal.
4. **Short BED LIGHTS press** (lights off):
   ```
   SW: 0x0008 down (bounces N)
   SW: 0x0008 up (bounces N)
   ```
   Lighting and amber come on at the `up` line, not the `down` line. Bed
   Status bit 2 changes at the release. No LEVELED line.
5. **5 s BED LIGHTS hold:**
   ```
   SW: 0x0008 down (bounces N)
   Max Current for Motor 0: X    (x4 - supervision runs during the hold, PRE_SHIP F6)
   LEVELED (Bed Status bit 4) -> 0     (only if it was 1)
   SW: 0x0008 up (bounces N)
   ```
   Settings-mode entry prints nothing (amber starts fast-blinking at 5 s;
   Bed Status bit 1 -> 1). Lighting unchanged at the press, at entry and at
   the release. Then one more press: `SW: 0x0008 down`, `Settings mode:
   exit`, `SW: 0x0008 up`; lighting unchanged.
6. **STOP_ALL with the lights on:** no print of its own (with an app hold
   running, `BLE: hold end op=0x0N seq=N: ...`). Amber stays on (possibly a
   brief flicker, b.C); lighting stays on; Bed Status bit 2 stays 1.

---

## c. Diffs

### c.1 Source diffs (complete, every source change in the session)

The `git show` of each commit restricted to `*.c *.h`. Commit E has no
source change.

#### Commit B `ef36909` - `git show --format= ef36909 -- '*.c' '*.h'`

```diff
diff --git a/app.c b/app.c
index 42d5dfa..3ba6078 100644
--- a/app.c
+++ b/app.c
@@ -602,6 +602,7 @@ void app_process_action(void)
       LED_timer_timeout = false;
       LED_blink_counter++;
       if (LED_blink_counter == 2){
+        app_set_leveled(false); // the reverse is motion (#46)
         reverse_motor_direction_and_start(active_switch_during_collision);
       }
     }
@@ -642,9 +643,14 @@ void app_process_action(void)
           auto_level_active = false;
           auto_level_stop_blink_LED(LED_RED);
           set_LED_on(LED_GREEN);
+          bed_level = true;       // a successful end: nothing needed levelling
+          app_set_leveled(true);  // Bed Status bit 4 (#46)
           if (first_print[3]){
 #if !BED_CAPTURE_MOTOR_CURRENTS
               printf("Bed is disabled, no leveling required\r\n");
+              if (bed_level){
+                  printf("Bed is level\r\n");
+              }
 #endif
               first_print[3] = false;
           }
@@ -676,17 +682,21 @@ void app_process_action(void)
               auto_level_active = false;
               auto_level_stop_blink_LED(LED_RED);
               set_LED_on(LED_GREEN);
+              // Every way into this branch is a successful end (#46). bed_level
+              // was never set true before, so "Bed is level" never printed.
+              bed_level = true;
+              app_set_leveled(true);  // Bed Status bit 4 (#46)
 #if !BED_CAPTURE_MOTOR_CURRENTS
               printf("Exit auto-level\r\n");
-              if (bed_level){
-                  printf("Bed is level\r\n");
-              }
-              else if (axis_disabled[ROLL] && level_status[PITCH].level){
+              if (axis_disabled[ROLL] && level_status[PITCH].level){
                   printf("ROLL is disabled and Pitch is level\r\n");
               }
               else if (axis_disabled[PITCH] && level_status[ROLL].level){
                   printf("PITCH is disabled and ROLL is level\r\n");
               }
+              if (bed_level){
+                  printf("Bed is level\r\n");
+              }
 #endif
           } else {
 
diff --git a/bed_actions.c b/bed_actions.c
index e22a70e..213f703 100644
--- a/bed_actions.c
+++ b/bed_actions.c
@@ -13,6 +13,8 @@
 
 #include "bed_actions.h"
 
+#include <stdio.h>
+
 #include "app.h"
 #include "auto_level.h"
 #include "debug_capture.h"
@@ -38,6 +40,33 @@ static bool bedlights_on = false; // Tracks if bedlights are turned on/off
 
 static bool double_check = false; // Used to double check level only one time
 
+// Bench print: one line on every change of the LEVELED flag (Bed Status flag
+// bit 4), so the bench log shows the bit without a phone. Fires only on a
+// change, never per pass. Pre-ship: 0 (PRE_SHIP.md).
+#define BED_DEBUG_LEVELED_LOG 1
+
+// LEVELED (Bed Status flag bit 4, DECISIONS.md #46). Set when an auto-level
+// run of either source ends successfully; cleared when any motion starts, on
+// a collision trip and on entering settings mode. Releasing AUTO LEVEL, STOP
+// and STOP_ALL do not clear it: they move nothing.
+static bool bed_leveled = false;
+
+void app_set_leveled(bool leveled)
+{
+  if (leveled == bed_leveled) {
+    return;
+  }
+  bed_leveled = leveled;
+#if BED_DEBUG_LEVELED_LOG && !BED_CAPTURE_MOTOR_CURRENTS
+  printf("LEVELED (Bed Status bit 4) -> %u\r\n", leveled ? 1u : 0u);
+#endif
+}
+
+bool app_leveled(void)
+{
+  return bed_leveled;
+}
+
 // Shared handling for a single-side motor button press
 static void motor_button_pressed(active_switch_t switch_value, side_of_bed_t side, bool up)
 {
@@ -111,6 +140,7 @@ void app_move_start(bed_target_t target, bed_direction_t dir)
 {
   bool up = (dir == BED_DIR_UP);
   active_move_is_all = (target == BED_TARGET_ALL);
+  app_set_leveled(false); // any move, either source, ALL included (#46)
   capture_reset_timer();
   switch (target) {
     case BED_TARGET_HEAD:
@@ -152,6 +182,7 @@ void app_move_stop(void)
 
 void app_auto_level_start(void)
 {
+  app_set_leveled(false); // a new run, either source (#46)
   capture_reset_timer();
   active_switch = SWITCH_AUTO_LEVEL_VALUE;
   initial_switch_press = true;
diff --git a/bed_actions.h b/bed_actions.h
index 5ffb9bb..8c2f0b3 100644
--- a/bed_actions.h
+++ b/bed_actions.h
@@ -59,4 +59,9 @@ active_switch_t app_active_command(void);
 bool app_in_settings_mode(void);
 bool app_lights_on(void);
 
+// LEVELED (Bed Status flag bit 4, DECISIONS.md #46): the last auto-level run
+// ended successfully and nothing has moved since. Main-loop context only.
+void app_set_leveled(bool leveled);
+bool app_leveled(void);
+
 #endif /* BED_ACTIONS_H_ */
diff --git a/bed_status.c b/bed_status.c
index ae820e2..7a56bbb 100644
--- a/bed_status.c
+++ b/bed_status.c
@@ -143,6 +143,7 @@ void bed_status_get(bed_status_t *out)
   out->any_collision = any_collision;
   out->in_settings_mode = app_in_settings_mode();
   out->lights_on = app_lights_on();
+  out->leveled = app_leveled();
 
   translate_command(app_active_command(), &out->command, &out->command_target, &out->command_direction);
 
diff --git a/bed_status.h b/bed_status.h
index e0009a3..8c571f3 100644
--- a/bed_status.h
+++ b/bed_status.h
@@ -60,6 +60,7 @@ typedef struct {
   bool any_collision;
   bool in_settings_mode;
   bool lights_on;
+  bool leveled;              // last auto-level run succeeded, nothing moved since (#46)
 
   bed_command_t command;
   bed_target_t command_target;       // valid only when command == BED_COMMAND_MOVE
diff --git a/ble_bed_service.c b/ble_bed_service.c
index 4900b49..8102cd4 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -82,6 +82,7 @@ void ble_bed_service_encode_status(const bed_status_t *in, bool tilt_fresh, uint
   if (in->in_settings_mode) flags |= 0x02;
   if (in->lights_on)        flags |= 0x04;
   if (tilt_fresh)            flags |= 0x08;
+  if (in->leveled)          flags |= 0x10; // byte 0, so a change notifies at once (status_flags_changed())
   out[0] = flags;
 
   // bed_source_t / bed_command_t / bed_target_t / bed_direction_t enum
diff --git a/motor_current_functions.c b/motor_current_functions.c
index 84b85cd..ec7d1ff 100644
--- a/motor_current_functions.c
+++ b/motor_current_functions.c
@@ -11,6 +11,7 @@
 #include <math.h>
 
 #include "motor_current_functions.h"
+#include "bed_actions.h"
 #include "dual_motor_control.h"
 #include "adc_currents.h"
 #include "exp_board.h"
@@ -395,6 +396,12 @@ void run_motor_supervision(active_switch_t sw, bool is_raising)
   }
   collision_check(is_raising);
 
+  // Every trip (summed rise or max load) is set inside this function, so this
+  // one check covers them all, in the same pass as the trip (#46).
+  if (any_collision) {
+    app_set_leveled(false);
+  }
+
 #if BED_CAPTURE_MOTOR_CURRENTS
   {
     static bool header_printed = false;
diff --git a/settings_mode.c b/settings_mode.c
index 2c7bcfa..79cd388 100644
--- a/settings_mode.c
+++ b/settings_mode.c
@@ -284,6 +284,7 @@ void settings_mode_tick(void)
 
     if (timeout_5000ms){
         in_settings_mode = true;
+        app_set_leveled(false); // Bed Status bit 4 (#46)
         sl_sleeptimer_stop_timer(&handle_5000ms_timer);
         timeout_5000ms = false;
     }
```

#### Commit C `f3a762e` - `git show --format= f3a762e -- '*.c' '*.h'`

```diff
diff --git a/bed_actions.c b/bed_actions.c
index 213f703..d0c8fea 100644
--- a/bed_actions.c
+++ b/bed_actions.c
@@ -248,6 +248,12 @@ void app_stop_all(void)
       motor_state[i].motor_on = false;
   }
   set_all_LED_off();
+  // The lighting output is never touched here, so its indicator comes back,
+  // as after a collision and an app MOVE_STOP. Not in settings mode: there
+  // amber is the settings-mode blink, which settings_mode_tick() keeps going.
+  if (bedlights_on && !in_settings_mode) {
+    set_LED_on(LED_AMBER);
+  }
   reset_limit_switch_flags(); // stale inferred limits must not survive a stop (#37)
   if (in_settings_mode) {
     // STOP in settings mode abandons a sensitivity or zero-angle hold in
```

#### Commit D `3fb1a0c` - `git show --format= 3fb1a0c -- '*.c' '*.h'`

```diff
diff --git a/switch_input.c b/switch_input.c
index f66a4e4..7fe29c8 100644
--- a/switch_input.c
+++ b/switch_input.c
@@ -555,7 +555,8 @@ static void process_outputs()
           break;
 
       case SWITCH_BED_LIGHTS:
-          if ((switch_pressed) && (switch_driving == SWITCH_BED_LIGHT_VALUE)){
+          // Nothing on the press: the lights change on the release (#47).
+          if ((!switch_pressed) && (switch_driving == SWITCH_BED_LIGHT_VALUE)){
               if (app_lights_on()){
                   set_LED_on(LED_AMBER);
                   set_underbed_lighting_on();}
@@ -563,7 +564,6 @@ static void process_outputs()
                   set_LED_off(LED_AMBER);
                   set_underbed_lighting_off();
               }
-          } else if ((!switch_pressed) && (switch_driving == SWITCH_BED_LIGHT_VALUE)){
               switch_driving = OFF;
               active_switch = OFF;
           }
@@ -663,16 +663,29 @@ static void process_inputs()
             break;
 
         case SWITCH_BED_LIGHTS:
+            // The lights toggle on the RELEASE of a short press (DECISIONS.md
+            // #47), so that holding BED LIGHTS into settings mode does not
+            // cycle them. The press still goes through bed_control at once,
+            // with the lights' CURRENT state: it changes nothing, but it takes
+            // the bed for the switches - which is what the settings-mode entry
+            // hold needs - and an app move is force-stopped on the press, not
+            // the release (#12).
             if (switch_pressed && (switch_held == OFF)){
                 switch_held = SWITCH_BED_LIGHT_VALUE;
-                if (bed_lights_set(BED_SOURCE_SWITCHES, !app_lights_on())) {
+                if (bed_lights_set(BED_SOURCE_SWITCHES, app_lights_on())) {
                     switch_driving = SWITCH_BED_LIGHT_VALUE;
                 }
             } else if ((!switch_pressed) && (switch_held == SWITCH_BED_LIGHT_VALUE)){
                 switch_held = OFF;
                 /*** For "Settings Mode" ***/
                 settings_mode_cancel_entry();
-
+                // A short press: released before settings mode was entered. A
+                // hold that reached settings mode is released in the settings
+                // branch and never gets here. Toggle only if the press was
+                // accepted; process_outputs() below drives the outputs.
+                if (switch_driving == SWITCH_BED_LIGHT_VALUE) {
+                    bed_lights_set(BED_SOURCE_SWITCHES, !app_lights_on());
+                }
             }
             break;
 
```

### c.2 Docs diffs

The `git show` of each commit restricted to `docs/`, **excluding** two files:
`docs/prompts/PROMPT_AE_collision-result-leveled-bit-lights-release.md`
(added in `ef36909`, 130 lines; it is the reviewer's own prompt, committed
verbatim and not reproduced here - this is the explicit truncation note the
handoff rules allow for docs diffs over 400 lines), and `docs/SESSION_LOG.md`
(the standing exception: it ships alongside this file). Everything else is
complete.

#### `ef36909` - `git show --format= ef36909 -- docs/ ':!docs/prompts'`

```diff
diff --git a/docs/BLE_GATT_DESIGN.md b/docs/BLE_GATT_DESIGN.md
index 7eaedfa..a68b038 100644
--- a/docs/BLE_GATT_DESIGN.md
+++ b/docs/BLE_GATT_DESIGN.md
@@ -215,7 +215,8 @@ offset 0   flags            u8   bit0 any_collision
                                  bit1 in_settings_mode (physical settings mode)
                                  bit2 lights_on
                                  bit3 tilt_fresh   ← NEW, see below
-                                 bits4-7 reserved (0)
+                                 bit4 leveled      ← see "LEVELED (bit 4)" below
+                                 bits5-7 reserved (0)
 offset 1   active_source    u8   0 NONE, 1 SWITCHES, 2 APP
 offset 2   command          u8   0 NONE, 1 MOVE, 2 AUTO_LEVEL, 3 LIGHTS
 offset 3   command_detail   u8   bits0-2 target (0-4), bit3 direction (0 down/1 up)
@@ -233,6 +234,40 @@ offset 8   motor[0..3]      4 × { status u8, current_ma u16 }  = 12 B
              calibration wizard (#7); until then the app labels A–D.
 ```
 
+### LEVELED (bit 4)
+
+Added 2026-11-02 (DECISIONS #46), within protocol v1 - see "Protocol version
+and this change" below.
+
+**Set** when an auto-level run, from either source (switch panel or app),
+ends successfully. "Successfully" is any of:
+
+- both axes levelled;
+- Step 1 found both axes already within tolerance, so no levelling was
+  needed (`Bed is disabled, no leveling required`);
+- one axis was already within tolerance (disabled) and the other levelled.
+
+**Cleared** when:
+
+- any motion starts - a single-side move, an ALL move, a new auto-level run
+  (either source), or the collision sequence's reverse;
+- a collision trips (summed rise or max load), in the same pass as the trip;
+- settings mode is entered.
+
+Nothing else clears it. In particular it **stays set** while the user keeps
+holding AUTO LEVEL after the run succeeded, and after the release, and
+through STOP / STOP_ALL, a watchdog stop or a disconnect stop - none of those
+moves the bed. It is 0 at boot.
+
+**App use:** "levelled - you can let go" while AUTO LEVEL is still held
+(`command` still reads AUTO_LEVEL until release), and "bed is level" on the
+idle screen until something moves.
+
+A change of bit 4 is a state-flag change, so it is notified at once
+(`status_flags_changed()` compares bytes 0-3, `ble_bed_service.c`), like the
+other flag bits. Collision during a run is shown by bit 0, not by bit 4: a
+run that ends in a collision never sets bit 4.
+
 ### Tilt sign conventions
 
 **Pitch — SPECIFICATION. `POSITIVE = HEAD raised`.**
@@ -297,6 +332,12 @@ failure is invisible on both ends, which is worse than a refusal.
 (Note also that the version value lives in a Simplicity Studio-generated
 config file, so a future bump goes through the configurator, not a hand-edit.)
 
+**LEVELED, flag bit 4 (2026-11-02, DECISIONS #46): also amended in place, no
+version bump.** It is additive: the bit was reserved and always 0, the byte
+layout does not change, and a client that ignores bit 4 behaves exactly as
+before. Amending v1 rests on the same pre-ship reasoning as above, and the
+after-first-ship rule above is unchanged by it.
+
 **`tilt_fresh` requires a one-bit firmware addition** implementing
 DECISIONS #17's app-era plan: continuous accelerometer sampling runs at a
 variable cadence *only while at least one connection has Status
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index cb3011d..6001076 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2252,3 +2252,76 @@ Two things this run does not cover: it used the Galaxy S23, not the iPhone
 that produced the original stalls, and a desk bed at desk range (the van test
 at a realistic distance is in `docs/PRE_SHIP.md`). "Bed is level" (REGRESSION
 6.1) printed in none of the twelve runs; see the Prompt AC handoff, QUESTIONS.
+
+## 46. Bed Status flag bit 4 = LEVELED; "Bed is level" prints on every successful auto-level end
+
+Date: 2026-11-02. Reviewer's instruction (Prompt AE, part B), from the Prompt
+AC handoff (B8 rows 1-4, QUESTIONS 2).
+
+### Decision
+
+**Bit 4 (0x10) of the Bed Status flags byte is LEVELED** (BLE_GATT_DESIGN §6,
+"LEVELED (bit 4)"). It is set when an auto-level run of either source ends
+successfully - both axes levelled, Step 1 found no levelling needed, or one
+axis disabled and the other levelled - and cleared when any motion starts
+(any move, ALL, a new auto-level run, the collision reverse), on a collision
+trip, and on entering settings mode. Nothing else clears it: it stays set
+while AUTO LEVEL is still held after success, after the release, and through
+STOP, STOP_ALL, a watchdog stop and a disconnect stop. Additive within
+protocol v1 (the bit was reserved and always 0); the Protocol Version value
+is unchanged.
+
+**"Bed is level" now prints on every successful end** (the four outcomes
+above), after the lines that already printed. Before, `bed_level` was only
+ever set false (`auto_level.c:166`, `reset_level_data()` `:603`), so the
+`if (bed_level)` branch in `app.c` could never run and REGRESSION 6.1 / 6.8
+could not pass as written.
+
+### Implementation
+
+- The flag is `bed_leveled` in `bed_actions.c`, with `app_set_leveled()` /
+  `app_leveled()` (`bed_actions.c:43-69`, declared in `bed_actions.h`). It is
+  a separate flag from `bed_level` on purpose: `bed_level` is cleared by
+  `reset_level_data()`, which `app_auto_level_stop()` calls on the release,
+  and bit 4 must survive the release.
+- Set: `app.c` Step-1 "no levelling required" branch (`:646-647`) and the
+  success branch (`:687-688`), which also set `bed_level = true`.
+- Cleared: `app_move_start()` (`bed_actions.c:143`, every move of either
+  source, ALL included - the switch path reaches it through
+  `bed_move_start()` too); `app_auto_level_start()` (`:185`); the end of
+  `run_motor_supervision()` when `any_collision` is set
+  (`motor_current_functions.c:399-404`) - every trip, summed rise or max load,
+  is declared inside that function, so this is the same pass as the trip;
+  the collision reverse (`app.c:605`); settings-mode entry
+  (`settings_mode.c:287`).
+- Wire: `bed_status_t.leveled` (`bed_status.h`, filled in `bed_status.c`),
+  encoded as `flags |= 0x10` in `ble_bed_service_encode_status()`. The flags
+  byte is byte 0, which `status_flags_changed()` already compares, so a change
+  of bit 4 is notified at once like bits 0-2 - no change to the notify logic.
+- Bench print, behind a new flag `BED_DEBUG_LEVELED_LOG` (1 by default,
+  `bed_actions.c`, listed in `docs/PRE_SHIP.md`): one line
+  `LEVELED (Bed Status bit 4) -> 0|1` on each change of the flag, never per
+  pass. Suppressed when `BED_CAPTURE_MOTOR_CURRENTS` is 1, like the other
+  prints.
+
+All main-loop context; no new I2C, nothing blocks, nothing runs in a
+Bluetooth stack callback.
+
+### Choices made in the implementation (for the reviewer)
+
+- "Bed is level" prints in **all four** success outcomes, not only "both
+  axes levelled", so that one line marks every successful end and matches
+  bit 4. The existing lines are kept: `Exit auto-level`, `Bed is disabled, no
+  leveling required`, `ROLL is disabled and Pitch is level`, `PITCH is
+  disabled and ROLL is level`. Order: the existing line(s) first, then `Bed
+  is level`.
+- A move counts as "motion started" when `app_move_start()` runs, i.e. when
+  bed_control accepted the command - even if `set_dual_motors_on()` then
+  refuses a pair at an inferred limit. A refused command (conflict latch,
+  force-stop) starts nothing and leaves bit 4 alone.
+- The bench-only runaway provocation (`BED_BENCH_PROVOKE_RUNAWAY`, 0) does
+  not clear bit 4.
+
+### Status
+
+Builds clean (no warnings). **Not run on the bench.**
diff --git a/docs/PRE_SHIP.md b/docs/PRE_SHIP.md
index bc90f0b..b4e0fca 100644
--- a/docs/PRE_SHIP.md
+++ b/docs/PRE_SHIP.md
@@ -40,6 +40,7 @@ File:line references are to the firmware at `6796e6b` unless marked.
 | F16 | `tilt_fresh` (Stage B5) | Bed Status flag bit 3 is always 0 (`ble_bed_service.c:1057`), so the app cannot tell a fresh angle from a stale one | Claude decides | Implement, or ship without it with the app treating tilt as possibly stale | DECISIONS #17, #33; BLE_GATT_DESIGN §6 |
 | F17 | Gecko bootloader on every board before it ships | A board without the bootloader can never be updated over the air. Note: the application is linked at flash address 0x0 (`autogen/linkerfile.ld:32`), so the app's flash layout must change with it (configurator) | Claude (plan), Andrew (configurator, flashing) | Bootloader project built; app relinked; each board flashed and checked | BLE_GATT_DESIGN §10; Prompt AC |
 | F18 | "Connection stalled" shown when the physical controller takes over from the app | The app's refused repeats do not refresh the watchdog, so ~300 ms later the app gets WATCHDOG_STOP although the link is fine (`ble_bed_service.c:746-750`) | Claude decides; Claude Code (firmware or app) | A distinct result code, or app handling of FORCE_STOPPED then WATCHDOG_STOP; bench check | Prompt AA handoff QUESTIONS 5 |
+| F19 | `BED_DEBUG_LEVELED_LOG` 1 -> 0 (`bed_actions.c`) | Bench print `LEVELED (Bed Status bit 4) -> 0/1` on every change of bit 4 | Claude Code | Change to 0, build, check the `LEVELED` lines are gone | Prompt AE part F; DECISIONS #46 |
 
 ## App
 
@@ -82,4 +83,4 @@ pre-ship item until the reviewer says so.
 | Motor-fault reporting is an interim 200 ms poll; the expander's latch registers are unconfigured and the fault interrupt is not wired | `exp_board.c:342-348`, `:515-518` |
 | The lower-collision "OFF" rung (750) is not actually off | SETPOINTS_INVENTORY.md Note A |
 | Auto-level's pulsed ("sneak") phase runs with no current supervision | Prompt AC handoff B1(d) (`auto_level.c:679-738`, `:770-781`) |
-| "Bed is level" never prints: `bed_level` is never set true | Prompt AC handoff B8 (`auto_level.c:166,603`; `app.c:681`) |
+| "Bed is level" never prints: `bed_level` is never set true. **Fixed 2026-11-02 (Prompt AE part B, DECISIONS #46); bench check REGRESSION 6.1 / 6.10** | Prompt AC handoff B8 (`auto_level.c:166,603`; `app.c:681`) |
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index 489fbae..2f0efdc 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -193,7 +193,9 @@ Starting condition for 6.1: frame tilted several degrees in both roll and
 pitch, all actuators clear of limits.
 
 - [ ] ⚡ 6.1 Hold AUTO_LEVEL: red LED blinks; ROLL levels first, then PITCH;
-      motors stop; green LED on; "Bed is level" prints
+      motors stop; green LED on; `Exit auto-level` then "Bed is level"
+      print (DECISIONS #46: it prints on every successful end, after the
+      outcome's own line), and `LEVELED (Bed Status bit 4) -> 1`
 - [ ] 6.2 Verify with an independent level/inclinometer that the frame is
       actually level (within tolerance) after 6.1
 - [ ] 6.3 Bed already level: AUTO_LEVEL exits promptly with green LED,
@@ -207,7 +209,8 @@ pitch, all actuators clear of limits.
 - [ ] 6.7 Auto-level respects limits: with one actuator at its limit on the
       driven axis, leveling stops that pair and does not fight the limit
 - [ ] ⚡ 6.8 BLE link stays up (no disconnect) through a full auto-level
-      run, start to "Bed is level" - the original auto-level BLE-drop bug
+      run, start to "Bed is level" (prints since DECISIONS #46; before it,
+      use `Exit auto-level`) - the original auto-level BLE-drop bug
       (DECISIONS.md #26) only manifested on a sustained multi-second move,
       so a quick/near-level test is not sufficient coverage for this check
 - [x] 6.9 App AUTO LEVEL (DECISIONS #45): 10 holds from off-level, no
@@ -263,6 +266,22 @@ failing phone), and a realistic distance in the van (`docs/PRE_SHIP.md`).
 "Bed is level" did not print in any of the twelve runs (6.1 expects it) -
 open question, Prompt AC handoff.
 
+- [ ] 6.10 LEVELED, Bed Status flag bit 4 (DECISIONS #46). With
+      `BED_DEBUG_LEVELED_LOG` 1 each change prints one line
+      `LEVELED (Bed Status bit 4) -> 0|1`; with the app subscribed, Bed
+      Status bit 4 follows it.
+  - [ ] 6.10a Physical AUTO LEVEL from off-level to success: `-> 1` next to
+        `Bed is level`. Keep holding 3 s, then release: no further LEVELED line.
+  - [ ] 6.10b The same from the app's AUTO LEVEL button.
+  - [ ] 6.10c With it set, press any move (switch or app, one side or ALL):
+        `-> 0` on the press. Set it again, then press AUTO LEVEL: `-> 0` on
+        the press.
+  - [ ] 6.10d Bed already level (6.3): `-> 1` with `Bed is disabled, no
+        leveling required` and `Bed is level`.
+  - [ ] 6.10e With it set, hold BED LIGHTS 5 s into settings mode: `-> 0`
+        at entry.
+  - [ ] 6.10f With it set, STOP_ALL from the app: no LEVELED line (it stays 1).
+
 ## 7. Settings mode
 
 - [ ] ⚡ 7.1 Hold BED_LIGHTS 5 s: amber LED fast-blinks — settings mode active
```

#### `f3a762e` - `git show --format= f3a762e -- docs/ ':!docs/prompts'`

```diff
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index 2f0efdc..eca8337 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -383,6 +383,12 @@ bench run), except where marked as the user's report.
 - [ ] Tap again: lighting off, amber LED off
 - [ ] Lights state survives a motor move and a collision sequence
       (amber restored after collision blink)
+- [ ] Lights on, app STOP_ALL (with and without a move running): lighting
+      and amber both stay on (Prompt AE part C; before, amber went off). The
+      same after a watchdog stop and after the control phone disconnects
+      mid-hold. A brief amber flicker at the stop is possible: the stop turns
+      every LED off and then amber back on, as app MOVE_STOP already does
+      (DECISIONS #36 "The indicator blip").
 
 ## 9. Cross-checks & UART health
 
```

#### `3fb1a0c` - `git show --format= 3fb1a0c -- docs/ ':!docs/prompts'`

```diff
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 6001076..8aa2591 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2325,3 +2325,96 @@ Bluetooth stack callback.
 ### Status
 
 Builds clean (no warnings). **Not run on the bench.**
+
+## 47. BED LIGHTS toggles the lights on the release of a short press
+
+Date: 2026-11-02. User decision 2026-11-02, implemented per the reviewer's
+Prompt AE part D. Replaces the toggle-on-press behaviour (Prompt AC handoff
+B3: "entering settings mode toggles the under-bed lights").
+
+### Decision
+
+Holding BED LIGHTS to enter settings mode must not cycle the lights.
+
+- The lights toggle when a **short press is released**. A short press is one
+  **released before the settings-mode entry completes**: the 5 s entry timer
+  (`settings_mode.c:281-289`) has fired and `settings_mode_tick()` has set
+  `in_settings_mode` before the release is delivered -> not short.
+- A hold that reaches settings mode does **not** toggle the lights.
+- A press while in settings mode exits settings mode and does **not** toggle
+  the lights (unchanged).
+- Safety rule unchanged (DECISIONS #12): a BED LIGHTS press while the app is
+  moving the bed force-stops it **on the press**. Only the toggle moves to the
+  release.
+
+Examples: released at 4.9 s -> settings mode was not entered, the release is
+handled in normal mode -> **toggle**. Released at 5.1 s -> settings mode was
+entered at 5.0 s (plus at most one loop pass), the release is handled by the
+settings branch -> **no toggle**, settings mode active. The boundary is the
+moment `settings_mode_tick()` acts on the timer against the moment the
+sampler delivers the debounced release (10-20 ms after the contact opens,
+DECISIONS #41); `settings_mode_tick()` runs before `switch_input_tick()` in
+each pass, and its entry test reads the sampler's debounced state, which
+still says "down" until the release is debounced. The 5 s is measured from
+the pass after the debounced press, as before.
+
+### Implementation (`switch_input.c`)
+
+- **Press** (`:665-677`): `bed_lights_set(BED_SOURCE_SWITCHES,
+  app_lights_on())` - the current state, so nothing changes. It still runs
+  arbitration: if the app holds the bed it is force-stopped now and the call
+  returns false (no `switch_driving`); otherwise the switches take the bed and
+  `app_lights_set()` sets `active_switch = SWITCH_BED_LIGHT_VALUE`, which the
+  settings-mode entry test needs.
+- **Release in normal mode** (`:678-689`): as before `switch_held = OFF` and
+  `settings_mode_cancel_entry()`; then, only if this press was accepted
+  (`switch_driving == SWITCH_BED_LIGHT_VALUE`), `bed_lights_set(BED_SOURCE_
+  SWITCHES, !app_lights_on())` - the toggle, through bed_control like every
+  other switch command.
+- **`process_outputs()`** (`:557-570`): the press arm no longer writes the
+  outputs; the release arm writes amber and `EN_LIGHTING` from
+  `app_lights_on()` and then, as before, clears `switch_driving` and
+  `active_switch`.
+- Release in settings mode: unchanged - the settings branch
+  (`settings_mode.c:257-261`) only clears `active_switch`.
+- `bed_control.c` is unchanged; no new verb. The host test
+  (`test/bed_control_test.c`) still passes.
+
+### Every other path this touches, after the change
+
+- **Double press.** During a BED LIGHTS hold the double-press check runs, as
+  before (`active_switch` is BED LIGHTS and the switches hold the bed,
+  `switch_input.c:921-944`). A second button down -> `Double Press Error`,
+  panel state cleared -> the BED LIGHTS release matches nothing -> **no
+  toggle**. Before, the lights had already toggled on the press. A BED LIGHTS
+  press while another button is already down is not delivered at all (the
+  sampler ignores presses with two down) -> no toggle, as before.
+- **Motion fail-safe.** No motor is involved; a BED LIGHTS hold counts as a
+  switch down, as before. Unchanged.
+- **Sampler release-first ordering.** A BED LIGHTS release and another
+  button's press in the same sample: the release is delivered first, so the
+  toggle happens and `active_switch` returns to OFF before the other press is
+  handled. The order is unchanged; the toggle now happens inside it.
+- **Arbitration.** Press: as before (force-stop of an app move on the press,
+  no toggle afterwards because the press was refused). Release: a second
+  bed_control call. In the normal case the switches still hold the bed, so it
+  toggles. If the app took the bed during the hold (possible only after an
+  app LIGHTS_SET force-stopped the hold and the bed went idle - see next
+  point), the release now force-stops the app's movement instead of toggling;
+  before, the release set `active_switch = OFF` underneath a running app move.
+- **Lights from the app while BED LIGHTS is held.** The app's LIGHTS_SET is
+  force-stopped (`FORCE_STOPPED_OTHER_SOURCE`), the lights do not change, and
+  `active_switch` goes to OFF, so the settings-mode entry is abandoned. The
+  physical press is still accepted, so its release toggles the lights - **even
+  if held longer than 5 s**, because settings mode was never entered (the
+  definition above).
+- **Collision during the hold.** Supervision runs during a BED LIGHTS hold
+  (PRE_SHIP F6). If a trip were declared, `process_outputs()` is skipped while
+  `any_collision` is set, so a release then would toggle `bedlights_on`
+  without writing `EN_LIGHTING` until the next lights command. The same gap
+  existed for a press before. No motor is on, so such a trip is not expected.
+  ASSERTED from code; not fixed here.
+
+### Status
+
+Builds clean (no warnings). **Not run on the bench.**
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index eca8337..0a0a7a1 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -284,7 +284,9 @@ open question, Prompt AC handoff.
 
 ## 7. Settings mode
 
-- [ ] ⚡ 7.1 Hold BED_LIGHTS 5 s: amber LED fast-blinks — settings mode active
+- [ ] ⚡ 7.1 Hold BED_LIGHTS 5 s: amber LED fast-blinks — settings mode active.
+      The under-bed lights do not change at the press, during the hold,
+      at entry, or at the release (DECISIONS #47)
 - [ ] 7.2 Zero-level store: hold AUTO_LEVEL; red LED on; after 2 s green LED
       on; UART prints stored ROLL/PITCH values
 - [ ] 7.3 Zero-level erase: keep holding AUTO_LEVEL 5 more s; blue LED on;
@@ -379,8 +381,23 @@ bench run), except where marked as the user's report.
 
 ## 8. Bed lights
 
-- [ ] ⚡ Tap BED_LIGHTS: under-bed lighting on, amber LED on
-- [ ] Tap again: lighting off, amber LED off
+- [ ] ⚡ Tap BED_LIGHTS: under-bed lighting on, amber LED on - **on the
+      release**, not the press (DECISIONS #47). Hold it ~1 s to see it: nothing
+      changes until you let go
+- [ ] Tap again: lighting off, amber LED off, on the release
+- [ ] 8.a Hold BED_LIGHTS ~4.5 s, release: the lights toggle on the release,
+      no settings mode (amber does not fast-blink)
+- [ ] 8.b Hold BED_LIGHTS 5 s into settings mode (amber fast-blinks), keep
+      holding 1 s, release: lighting unchanged throughout. Repeat with the
+      lights on and with them off
+- [ ] 8.c In settings mode, press BED_LIGHTS once: `Settings mode: exit`,
+      amber shows the lights state, lighting unchanged at the press and at
+      the release
+- [ ] 8.d App holding a move (or AUTO LEVEL): press and hold BED_LIGHTS -
+      the bed stops **on the press**; release: the lights do not change.
+      Note what the app's still-held button does next (DECISIONS #47; a
+      lights press does not latch the #36 inhibit, so the app's next repeat
+      may start the bed again - record it)
 - [ ] Lights state survives a motor move and a collision sequence
       (amber restored after collision blink)
 - [ ] Lights on, app STOP_ALL (with and without a move running): lighting
```

#### `e3cc865` - `git show --format= e3cc865 -- docs/ ':!docs/prompts'`

```diff
diff --git a/docs/PRE_SHIP.md b/docs/PRE_SHIP.md
index b4e0fca..dcb0068 100644
--- a/docs/PRE_SHIP.md
+++ b/docs/PRE_SHIP.md
@@ -38,9 +38,15 @@ File:line references are to the firmware at `6796e6b` unless marked.
 | F14 | Release-config confirmation pass | Every recorded pass was a Debug build | Andrew (bench) | Release build, then the pass (or the subset the reviewer names) | REGRESSION_RECORD_PRE_STAGEC.md:38; MIGRATION_STATUS Phase 6 |
 | F15 | DECISIONS #29 timing constants review | Auto-level's filter (`NUM_ACCEL_READINGS`, `accel_alpha`) and the collision path (`NUM_CURRENT_READINGS`, `NUM_INITIAL_CURRENTS_IGNORED`, `NUM_CURRENTS_READ_DYNAMIC_THRESHOLD`, `alpha`) count loop passes, not milliseconds (DECISIONS #2) | Claude (design), Claude Code | Design prompt; change; REGRESSION §5 and §6 with a loop-rate measurement | DECISIONS #29 |
 | F16 | `tilt_fresh` (Stage B5) | Bed Status flag bit 3 is always 0 (`ble_bed_service.c:1057`), so the app cannot tell a fresh angle from a stale one | Claude decides | Implement, or ship without it with the app treating tilt as possibly stale | DECISIONS #17, #33; BLE_GATT_DESIGN §6 |
-| F17 | Gecko bootloader on every board before it ships | A board without the bootloader can never be updated over the air. Note: the application is linked at flash address 0x0 (`autogen/linkerfile.ld:32`), so the app's flash layout must change with it (configurator) | Claude (plan), Andrew (configurator, flashing) | Bootloader project built; app relinked; each board flashed and checked | BLE_GATT_DESIGN §10; Prompt AC |
+| F17 | Gecko bootloader on every board before it ships | A board without the bootloader can never be updated over the air. Note: the application is linked at flash address 0x0 (`autogen/linkerfile.ld:32`), so the app's flash layout must change with it (configurator). **Extended 2026-11-02 (reviewer ruling on Prompt AC QUESTIONS 7): the bootloader and the flash layout need their own design** - where the bootloader sits, where the app is relinked, whether OTA stages a second image (the image is ~240 KiB with ~229 KiB free below NVM3, not the ~53 KB BLE_GATT_DESIGN §10 assumed) | Claude (plan, own design prompt), Andrew (configurator, flashing) | Design prompt and its record first; then bootloader project built; app relinked; each board flashed and checked | BLE_GATT_DESIGN §10; Prompt AC handoff B9, QUESTIONS 7; Prompt AE part E |
 | F18 | "Connection stalled" shown when the physical controller takes over from the app | The app's refused repeats do not refresh the watchdog, so ~300 ms later the app gets WATCHDOG_STOP although the link is fine (`ble_bed_service.c:746-750`) | Claude decides; Claude Code (firmware or app) | A distinct result code, or app handling of FORCE_STOPPED then WATCHDOG_STOP; bench check | Prompt AA handoff QUESTIONS 5 |
 | F19 | `BED_DEBUG_LEVELED_LOG` 1 -> 0 (`bed_actions.c`) | Bench print `LEVELED (Bed Status bit 4) -> 0/1` on every change of bit 4 | Claude Code | Change to 0, build, check the `LEVELED` lines are gone | Prompt AE part F; DECISIONS #46 |
+| F20 | Collision detection is blind for the first ~450 supervision passes of every move | 100 passes ignored + 350 calibrating (`motor_current_functions.c:28-29`) before any collision or max-load trip can fire; roughly 0.7-1.0 s at auto-level's loop rate, never measured for manual moves | Claude (design), Claude Code | Design with F15 (pass counts -> ms, DECISIONS #2): state the blind time in ms and what covers it; change; REGRESSION §5 with a loop-rate measurement | Prompt AC handoff B1(a); reviewer ruling, Prompt AE part E |
+| F21 | Auto-level's pulsed ("sneak") phase has no current supervision | Once `Latch` prints, `level_axis_pulse()` runs with no collision, max-load or limit inference (`auto_level.c:679-738`, `:770-771`; `switch_input.c:921`) | Claude (design), Claude Code | Design: supervise the pulse phase (or record why it is safe); change; REGRESSION §5 collision during auto-level near level | Prompt AC handoff B1(d), QUESTIONS 3; reviewer ruling, Prompt AE part E |
+| F22 | The "continuous" re-baseline never reaches the trip test | It writes `dynamic_collision_current_threshold[]`, which nothing reads; the trip uses `max_current_threshold_set[]`; and it counts on motor 3's counter for every motor (`motor_current_functions.c:206-231`) | Claude (design), Claude Code | Decide: make it work (with F15/F20) or remove it; change; REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 4; reviewer ruling, Prompt AE part E |
+| F23 | Auto-level uses the raise margin for the lowering motors | `is_raising` is forced true for auto-level (`bed_actions.c:170`; `auto_level.c:771`), so the two lowering motors are judged against the raise margin | Claude decides, Claude Code changes | Decision (keep with a recorded reason, or per-direction margins in auto-level); then REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 5; reviewer ruling, Prompt AE part E |
+| F24 | Unconditional console prints to review before release | With every flag at 0 the firmware still prints `BLE SM: configure/...` (`ble_bed_service.c:1089, 1101, 1109`), `connection_parameters` (`:1270`), `Max Current for Motor` (`motor_current_functions.c:198`), `Once`, and the auto-level step lines | Claude decides, Claude Code changes | List each print with keep / flag / remove; change; check a release-build log | Prompt AC handoff QUESTIONS 12; reviewer ruling, Prompt AE part E |
+| F25 | Hold 9 (2026-11-01 bench): the HEAD pair apparently did not run during PITCH levelling | `prompt_aa_bench.log` lines 201-204 show outputs 0 and 3 at 1 and 12 mA while FOOT ran; cause not established (an inferred limit refusing the pair, `dual_motor_control.c:353, 361`, is one candidate) | Andrew + Claude | A bench look with the limit prints enabled: repeat PITCH levelling from a HEAD-high start and record which pairs run and any limit lines | Prompt AC handoff B1(d), QUESTIONS 6; reviewer ruling, Prompt AE part E |
 
 ## App
 
@@ -82,5 +88,5 @@ pre-ship item until the reviewer says so.
 | No reverse after a collision during auto-level | REGRESSION §5 note; REFACTOR_PLAN P1.5 |
 | Motor-fault reporting is an interim 200 ms poll; the expander's latch registers are unconfigured and the fault interrupt is not wired | `exp_board.c:342-348`, `:515-518` |
 | The lower-collision "OFF" rung (750) is not actually off | SETPOINTS_INVENTORY.md Note A |
-| Auto-level's pulsed ("sneak") phase runs with no current supervision | Prompt AC handoff B1(d) (`auto_level.c:679-738`, `:770-781`) |
+| Auto-level's pulsed ("sneak") phase runs with no current supervision. **Now row F21 (2026-11-02)** | Prompt AC handoff B1(d) (`auto_level.c:679-738`, `:770-781`) |
 | "Bed is level" never prints: `bed_level` is never set true. **Fixed 2026-11-02 (Prompt AE part B, DECISIONS #46); bench check REGRESSION 6.1 / 6.10** | Prompt AC handoff B8 (`auto_level.c:166,603`; `app.c:681`) |
```

---

## d. Raw output of commands run

### d.1 Session command log, in the order run

Every shell command after the first two was run through a small logging
wrapper (`r.sh` in the session scratchpad) that writes the command, its
combined output and its exit code. This is that log, unedited (the `git
commit` message bodies were passed with `-F <file>`; their text is the
commit messages in `git log`). No secrets appear in it. `[exit 1]` on the
last `ls ... | grep 2026-11-02` means "no match": no handoff for this date
existed yet. Builds were run with `build.sh` (d.2). Commands not run through
the wrapper are in d.3.

```
$ date; git fetch origin; git rev-parse HEAD origin/main; git status
Mon Nov  2 07:06:25 PST 2026
01315cdd72bf2b2a3437e6e79d9b44fd21bf7505
01315cdd72bf2b2a3437e6e79d9b44fd21bf7505
On branch main
Your branch is up to date with 'origin/main'.

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/prompts/PROMPT_AE_collision-result-leveled-bit-lights-release.md

no changes added to commit (use "git add" and/or "git commit -a")
[exit 0]

$ bash 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/build.sh'
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
[exit 0]

$ bash 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/build.sh'
[1/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_status.c.obj
[2/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_control.c.obj
[3/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[4/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/settings_mode.c.obj
[5/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/motor_current_functions.c.obj
[6/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[7/9] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[8/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[9/9] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
[exit 0]

$ cd test && gcc -std=c99 -Wall -I.. -o 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/bed_control_test.exe' bed_control_test.c ../bed_control.c && 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/bed_control_test.exe' | tail -5
PASS: scenario 6a: inhibited
PASS: scenario 6b: bed_move_stop still works while inhibited
PASS: scenario 6c: lights are not motion and are not inhibited

0 check(s) failed
[exit 0]

$ git add app.c bed_actions.c bed_actions.h bed_status.c bed_status.h ble_bed_service.c motor_current_functions.c settings_mode.c docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/PRE_SHIP.md docs/REGRESSION.md docs/prompts/PROMPT_AE_collision-result-leveled-bit-lights-release.md && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/msgB.txt' && git log --oneline -1 && git status --short
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_actions.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_actions.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_status.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_status.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/PRE_SHIP.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'settings_mode.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/prompts/PROMPT_AE_collision-result-leveled-bit-lights-release.md', LF will be replaced by CRLF the next time Git touches it
ef36909 Bed Status bit 4 = LEVELED; "Bed is level" prints on success (DECISIONS #46)
 M .projectlinkstore
[exit 0]

$ bash 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/build.sh'
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
[exit 0]

$ git add bed_actions.c docs/REGRESSION.md && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/msgC.txt' && git log --oneline -1 && bash 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/build.sh'
warning: in the working copy of 'bed_actions.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
f3a762e Restore the amber indicator after app_stop_all() when the lights are on
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
[exit 0]

$ bash 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/build.sh'
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
[exit 0]

$ git add switch_input.c docs/DECISIONS.md docs/REGRESSION.md && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/msgD.txt' && git log --oneline -1 && bash 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/build.sh'
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
3fb1a0c BED LIGHTS toggles the lights on the release of a short press (DECISIONS #47)
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
[exit 0]

$ git add docs/PRE_SHIP.md && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/msgE.txt' && git log --oneline -6 && bash 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/build.sh' && git status
warning: in the working copy of 'docs/PRE_SHIP.md', LF will be replaced by CRLF the next time Git touches it
e3cc865 PRE_SHIP: rows F20-F25 and F17 extended (reviewer rulings on Prompt AC)
3fb1a0c BED LIGHTS toggles the lights on the release of a short press (DECISIONS #47)
f3a762e Restore the amber indicator after app_stop_all() when the lights are on
ef36909 Bed Status bit 4 = LEVELED; "Bed is level" prints on success (DECISIONS #46)
01315cd Handoff 2026-11-01 02: Prompt AC (AA bench record, PRE_SHIP.md, feature groundwork report)
fad70fc Docs: Prompt AA bench result (DECISIONS #45, REGRESSION 6.9), create PRE_SHIP.md
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
On branch main
Your branch is ahead of 'origin/main' by 4 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
[exit 0]

$ sed -i 's/^#define BED_DEBUG_LEVELED_LOG 1/#define BED_DEBUG_LEVELED_LOG 0/' bed_actions.c && grep -n 'define BED_DEBUG_LEVELED_LOG' bed_actions.c && bash 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/build.sh'; git checkout -- bed_actions.c && grep -n 'define BED_DEBUG_LEVELED_LOG' bed_actions.c && bash 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/d50a718d-2692-4a63-96a9-4576d4ab7b4c/scratchpad/build.sh' && git status --short
46:#define BED_DEBUG_LEVELED_LOG 0
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
46:#define BED_DEBUG_LEVELED_LOG 1
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
 M .projectlinkstore
[exit 0]

$ grep -n '^#define BED_BENCH_PROVOKE_RUNAWAY\|^#define BLE_BENCH_WIPE_BONDS\|^#define BED_CAPTURE_MOTOR_CURRENTS\|^#define BED_DEBUG_LEVELED_LOG\|^#define BLE_DEBUG_WATCHDOG_LOG\|^#define SWITCH_SAMPLER_DIAG' *.c *.h
bed_actions.c:46:#define BED_DEBUG_LEVELED_LOG 1
ble_bed_service.c:70:#define BLE_BENCH_WIPE_BONDS 0
switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
switch_input.c:98:#define BED_BENCH_PROVOKE_RUNAWAY 0
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
[exit 0]

$ grep -n 'any_collision' *.c *.h
app.c:580:  if (any_collision){
app.c:583:    // Note: motors are shut off in "collision_check" function. All functions that are supervised with the "any_collision" flag are suspended until the flag is cleared.
app.c:587:    // (3) Turn off motors, LEDs, and reset active switch and "any_collision" flags
app.c:617:      any_collision = false;
app.c:626:  if ((auto_level_active) && (!any_collision) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
app.c:672:      } else if (!any_collision) { // Current axis is now level, switch to next axis after brief pause
auto_level.c:772:          if (any_collision){
bed_status.c:143:  out->any_collision = any_collision;
ble_bed_service.c:41:#include "motor_current_functions.h" // any_collision - cancels the move watchdog
ble_bed_service.c:81:  if (in->any_collision)    flags |= 0x01;
ble_bed_service.c:579:  if (any_collision) {
motor_current_functions.c:79:bool any_collision = false; // Boolean to indicate a collision has occurred on any motor
motor_current_functions.c:307:          any_collision = true;
motor_current_functions.c:323:          any_collision = true;
motor_current_functions.c:367:            any_collision = true;
motor_current_functions.c:390:    if (any_collision) {
motor_current_functions.c:401:  if (any_collision) {
switch_input.c:517:  if((!any_collision) && (!in_settings_mode)){
switch_input.c:934:  if ((active_switch != OFF) && !any_collision && (!auto_level_active) && (!in_settings_mode)){
app.h:99: * any_collision already works between motor_current_functions.c and app.c.
bed_status.h:60:  bool any_collision;
motor_current_functions.h:17:extern bool any_collision;
[exit 0]

$ grep -n 'if (any_collision) {' -A 7 ble_bed_service.c; grep -n 'case CMD_OPCODE_MOVE_START' -A 36 ble_bed_service.c | grep -n 'app_move_active\|bed_move_start\|app_apply_move_outputs\|move_watchdog_arm'; grep -n 'case CMD_OPCODE_AUTO_LEVEL_START' -A 25 ble_bed_service.c | grep -n 'app_auto_level_active\|bed_auto_level_start\|move_watchdog'
579:  if (any_collision) {
580-    // The collision handler owns the stop and its LED sequence from here.
581-    // Nothing left for the watchdog to guard, and letting it expire into an
582-    // app_stop_all() would cut across that sequence.
583-    move_watchdog_cancel();
584-    app_move_active = false;
585-    return;
586-  }
12:768-      if (app_move_active && (target != app_move_target || dir != app_move_dir)) {
15:771-        app_move_active = false;
18:774-      if (!app_move_active) {
19:775-        if (!bed_move_start(BED_SOURCE_APP, target, dir)) {
27:783-        app_apply_move_outputs(target);
28:784-        app_move_active = true;
35:791-      move_watchdog_arm();
10:817-      if (!app_auto_level_active) {
11:818-        move_watchdog_cancel();
13:820-        if (!bed_auto_level_start(BED_SOURCE_APP)) {
18:825-        app_auto_level_active = true;
20:827-      // A repeat must NOT re-enter bed_auto_level_start() - that resets the
24:831-      move_watchdog_arm();
[exit 0]

$ grep -n -i 'collision' docs/DECISIONS.md | grep -i 'app\|watchdog\|BLE' | head -20; grep -n -i 'collision' docs/REGRESSION_RECORD_PRE_STAGEC.md docs/SESSION_LOG.md | grep -i 'app\|phone\|watchdog' | head -20
147:- **Enables targeted per-pair collision relief**: reverse only the pair
198:app — can start motion that bypasses collision/limit detection. The command
631:## 29. Auto-level's filter constants are calibrated in loop passes, not time — and the same problem reaches the collision-detection safety path
1176:Collision and limit supervision must cover app moves; gating the whole block
2295:  the collision reverse (`app.c:605`); settings-mode entry
docs/SESSION_LOG.md:131:- **...that ~808 ms applies to manual-move collision detection.** That number
docs/SESSION_LOG.md:691:- **Watchdog silent disarm.** Now cancels only on collision; on plain idle it
docs/SESSION_LOG.md:725:  physical double-press still error, and does collision still trip on an app
docs/SESSION_LOG.md:741:  outward, app moves lose collision protection silently.
docs/SESSION_LOG.md:1446:- `move_watchdog_armed` is set only in `move_watchdog_arm()` (`ble_bed_service.c:515`, called at `:790` MOVE_START and `:830` AUTO_LEVEL_START) and cleared only in `move_watchdog_cancel()` (`:535`, called at `:582` collision, `:615` expiry, `:666` STOP_ALL, `:778` MOVE_START force-stopped, `:799` MOVE_STOP, `:817` fresh AUTO_LEVEL_START, `:838` AUTO_LEVEL_STOP, `:1022` disconnect stop). Both functions are `static`; the switch path never arms it.
docs/SESSION_LOG.md:1514:- From code (all cited in the handoff, section b): collision trip = sum of positive per-motor (200-pass moving average - peak baseline) > raise/lower margin, blind for the first ~450 supervision passes; auto-level's pulsed phase runs no current supervision; the "continuous" re-baseline writes an array nothing reads; auto-level always uses the raise margin; `bed_level` is never set true, so "Bed is level" never prints (none in the 12 bench runs); Settings RPC 0005 is not implemented and not in the GATT database; the pairing window's only tie to settings mode is `ble_bed_service.c:1240`; `app_stop_all()` turns the amber LED off while the lights stay on; a collision during an app hold sends no Command Result; the app is linked at flash 0x0.
docs/SESSION_LOG.md:1525:- QUESTIONS (handoff f): the 22/23 count; "Bed is level" (REGRESSION 6.1/6.8 cannot pass as written); unsupervised pulse phase; the ineffective re-baseline; raise margin in auto-level; hold 9's HEAD pair; bootloader vs the app at 0x0 and the ~240 KiB image; amber after STOP_ALL; collision result code during app holds; settings-mode details; PRE_SHIP structure; unconditional console prints.
[exit 0]

$ sed -n 685,700p docs/SESSION_LOG.md; sed -n 720,745p docs/SESSION_LOG.md
was wrong because it lacked a predicate, not because the variable is shared.
Splitting would touch six files across the safety path and lose the Bed Status
property. Full reasoning in the handoff §b.5.

**Also fixed**

- **Watchdog silent disarm.** Now cancels only on collision; on plain idle it
  clears `app_move_active` but **leaves the watchdog armed** — it guards the
  link, not the bed.
- **Two-source conflict stops instead of handing over.** New
  `TWO_SOURCE_LOCKOUT_MS` = 500 ms (sleeptimer, bench-tunable, #2). Armed on a
  force-stop and on the bed going idle under a move the app believed it held
  (which covers the switch-interrupts-app direction). Stops are never locked
  out.

**VERIFIED**
- **Acceptance step 0 confirms the root cause with no new firmware**: the
  existing `printf("Double Press Error\r\n")` should be repeating at ~10 Hz in
  the failing log. If it is absent, the diagnosis is wrong.
- Steps a–h in `fw_HANDOFF_2026-08-02_06`. Step (g.5) and (g.6) are the
  regression checks this fix could plausibly have caused: does the *real*
  physical double-press still error, and does collision still trip on an app
  move.
- Four review questions, chiefly whether the lockout should expire (current)
  or latch until MOVE_STOP.
- Manual-move loop-rate measurement still owed; Release-config pass still
  owed.

**The next session must NOT assume**

- **...that the stutter is fixed.** The root cause is argued from code and
  compiles; nothing has been on hardware.
- **...that the double-press check is now sound.** It still relies on the LED
  bits changing between latch and re-read to avoid false positives on
  legitimate physical holds. That is incidental, not designed, and untouched.
- **...that gating the block on source was safe by inspection.**
  `run_motor_supervision()` shares that block; if a future edit moves the gate
  outward, app moves lose collision protection silently.
- **...that the lockout latches.** It expires after 500 ms and a held app
  button will then restart the bed.
  *(Superseded 2026-08-02 by #36 — the timed lockout is gone and the
  replacement does latch. See the next entry.)*
[exit 0]

$ git diff --stat 01315cd e3cc865
 app.c                                              |  18 ++-
 bed_actions.c                                      |  37 +++++
 bed_actions.h                                      |   5 +
 bed_status.c                                       |   1 +
 bed_status.h                                       |   1 +
 ble_bed_service.c                                  |   1 +
 docs/BLE_GATT_DESIGN.md                            |  43 +++++-
 docs/DECISIONS.md                                  | 166 +++++++++++++++++++++
 docs/PRE_SHIP.md                                   |  13 +-
 docs/REGRESSION.md                                 |  52 ++++++-
 ..._collision-result-leveled-bit-lights-release.md | 130 ++++++++++++++++
 motor_current_functions.c                          |   7 +
 settings_mode.c                                    |   1 +
 switch_input.c                                     |  21 ++-
 14 files changed, 479 insertions(+), 17 deletions(-)
[exit 0]

$ '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
   text	   data	    bss	    dec	    hex	filename
 245188	    780	  72480	 318448	  4dbf0	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

$ date; git log --oneline 01315cd..HEAD; git status
Mon Nov  2 07:19:38 PST 2026
e3cc865 PRE_SHIP: rows F20-F25 and F17 extended (reviewer rulings on Prompt AC)
3fb1a0c BED LIGHTS toggles the lights on the release of a short press (DECISIONS #47)
f3a762e Restore the amber indicator after app_stop_all() when the lights are on
ef36909 Bed Status bit 4 = LEVELED; "Bed is level" prints on success (DECISIONS #46)
On branch main
Your branch is ahead of 'origin/main' by 4 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

no changes added to commit (use "git add" and/or "git commit -a")
[exit 0]

$ ls docs/handoffs | grep 2026-11-02

[exit 1]

$ sed -n '43,46p;54p;69p;143p;185p' bed_actions.c; grep -n 'app_set_leveled\|app_leveled\|is_raising = true\|void app_stop_all\|void app_lights_set\|bedlights_on && !in_settings_mode\|under-bed lighting survives' bed_actions.c bed_actions.h; grep -n 'leveled' bed_status.h bed_status.c; grep -n '0x10;\|static bool status_flags_changed\|memcmp(new_packed' ble_bed_service.c; grep -n 'process_pending_command();\|service_move_watchdog();\|service_app_commanding();\|if (bed_conflict_inhibited()\|if (!bed_move_start\|app_apply_move_outputs(target);\|if (app_bed_is_idle() && app_move_active)\|app_stop_all();' ble_bed_service.c
// Bench print: one line on every change of the LEVELED flag (Bed Status flag
// bit 4), so the bench log shows the bit without a phone. Fires only on a
// change, never per pass. Pre-ship: 0 (PRE_SHIP.md).
#define BED_DEBUG_LEVELED_LOG 1
void app_set_leveled(bool leveled)

  app_set_leveled(false); // any move, either source, ALL included (#46)
  app_set_leveled(false); // a new run, either source (#46)
bed_actions.c:54:void app_set_leveled(bool leveled)
bed_actions.c:65:bool app_leveled(void)
bed_actions.c:143:  app_set_leveled(false); // any move, either source, ALL included (#46)
bed_actions.c:185:  app_set_leveled(false); // a new run, either source (#46)
bed_actions.c:201:  is_raising = true; // To prevent accidentally tripping on lower collision security margin immediately following auto-level
bed_actions.c:228:void app_lights_set(bool on)
bed_actions.c:239:void app_stop_all(void)
bed_actions.c:254:  if (bedlights_on && !in_settings_mode) {
bed_actions.c:342:    set_LED_on(LED_AMBER); // under-bed lighting survives a move, as it does a collision
bed_actions.h:31:void app_lights_set(bool on);
bed_actions.h:35:void app_stop_all(void);
bed_actions.h:64:void app_set_leveled(bool leveled);
bed_actions.h:65:bool app_leveled(void);
bed_status.h:63:  bool leveled;              // last auto-level run succeeded, nothing moved since (#46)
bed_status.c:146:  out->leveled = app_leveled();
85:  if (in->leveled)          flags |= 0x10; // byte 0, so a change notifies at once (status_flags_changed())
111:    if (in->motors[i].fault)          status |= 0x10;
978:static bool status_flags_changed(const uint8_t *new_packed, const uint8_t *old_packed)
980:  if (memcmp(new_packed, old_packed, 4) != 0) { // flags, active_source, command, command_detail
588:  if (app_bed_is_idle() && app_move_active) {
620:  app_stop_all();
671:    app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
747:  if (bed_conflict_inhibited()
775:        if (!bed_move_start(BED_SOURCE_APP, target, dir)) {
783:        app_apply_move_outputs(target);
1027:    app_stop_all();
1034:  process_pending_command();
1035:  service_move_watchdog();
1036:  service_app_commanding();
[exit 0]

$ grep -n 'set_all_motors_off();\|collision_check(is_raising);\|max_load_check(i);\|app_set_leveled' motor_current_functions.c; grep -n 'case SWITCH_BED_LIGHTS\|bed_lights_set(BED_SOURCE_SWITCHES\|presses ignored\|if ((active_switch != OFF) && !any_collision\|count_switches_down(switch_sampler_down()) >= 2' switch_input.c; grep -n 'settings_mode_tick();\|switch_input_tick();\|ble_bed_service_tick();\|if (any_collision){\|LED_blink_counter == 2\|app_lights_on()){\|auto_level_active = false;\|fast_blink_LED(LED_AMBER)\|switch_input_clear_panel_state();\|else if (active_switch == SWITCH_BED_LIGHT_VALUE)' app.c settings_mode.c; grep -n 'app_stop_movement();\|app_lights_set(on);\|app_move_start(target, dir);\|app_auto_level_start();' bed_control.c
306:          set_all_motors_off();
322:          set_all_motors_off();
366:            set_all_motors_off();
387:    max_load_check(i);
397:  collision_check(is_raising);
402:    app_set_leveled(false);
557:      case SWITCH_BED_LIGHTS:
665:        case SWITCH_BED_LIGHTS:
675:                if (bed_lights_set(BED_SOURCE_SWITCHES, app_lights_on())) {
687:                    bed_lights_set(BED_SOURCE_SWITCHES, !app_lights_on());
934:  if ((active_switch != OFF) && !any_collision && (!auto_level_active) && (!in_settings_mode)){
957:      if (switch_sourced_move && (count_switches_down(switch_sampler_down()) >= 2)){
1020:      SW_DIAG("SW: %u switches down - presses ignored\r\n",
app.c:102:bool auto_level_active = false; // Tracks whether autolevel is running - extern via app.h, see bed_actions.c
app.c:580:  if (any_collision){
app.c:590:      auto_level_active = false;
app.c:604:      if (LED_blink_counter == 2){
app.c:613:      if (app_lights_on()){
app.c:643:          auto_level_active = false;
app.c:682:              auto_level_active = false;
app.c:734:  settings_mode_tick();
app.c:741:  switch_input_tick();
app.c:773:  ble_bed_service_tick();
settings_mode.c:160:  switch_input_clear_panel_state();
settings_mode.c:257:      else if (active_switch == SWITCH_BED_LIGHT_VALUE){
settings_mode.c:295:      fast_blink_LED(LED_AMBER);
106:    app_stop_movement();
109:  app_move_start(target, dir);
118:    app_stop_movement();
134:    app_stop_movement();
137:  app_auto_level_start();
146:    app_stop_movement();
159:    app_stop_movement();
162:  app_lights_set(on);
[exit 0]

```

### d.2 Build method

`build.sh` (scratchpad) puts the Silicon Labs ninja and cmake on `PATH`
(`~/.silabs/slt/installs/conan/p/ninja1a38fc85adcf7/p`,
`.../cmakefa35ab0687064/p/bin`), runs `cmake --build --preset
default_config` in `cmake_gcc/`, prints the last 25 lines and `exit N`,
then `grep -n -i 'warning\|error'` over the whole output (nothing after the
`---` line = none). Its runs are in d.1, in order:

| When | Result |
|---|---|
| Baseline, `01315cd` | `ninja: no work to do.`, exit 0, no warnings |
| B source edits (before B docs and commit) | 8 objects + link, exit 0, no warnings |
| C source edit (B committed as `ef36909`; its source = what was just built) | 1 object + link, exit 0, no warnings |
| After commit C `f3a762e` | no work to do, exit 0 |
| D source edit | 1 object + link, exit 0, no warnings |
| After commit D `3fb1a0c` | no work to do, exit 0 |
| After commit E `e3cc865` | no work to do, exit 0 |
| `BED_DEBUG_LEVELED_LOG` 0 (temporary), then back to 1 | each 1 object + link, exit 0, no warnings |

`arm-none-eabi-size` of the final build: text 245188, data 780, bss 72480
(d.1). The Prompt AC record was text 244988.

### d.3 Commands not run through the wrapper (output reproduced)

Run before the wrapper existed (both re-run inside it, first entry of d.1,
same result):

```
$ cat docs/prompts/PROMPT_AE_...md && date && git log --oneline -5 && git status
(the prompt text, then)
Mon Nov  2 07:05:58 PST 2026
01315cd Handoff 2026-11-01 02: Prompt AC (AA bench record, PRE_SHIP.md, feature groundwork report)
fad70fc Docs: Prompt AA bench result (DECISIONS #45, REGRESSION 6.9), create PRE_SHIP.md
6796e6b Handoff 2026-11-01 01: fix one section cross-reference (d.5 -> d.4)
63acb4d Handoff 2026-11-01 01: Prompt AA (sleep veto during app holds)
75b0a61 Keep the CPU in EM0 during app holds; hold-end diagnostic line (DECISIONS #45)
On branch main
Your branch is up to date with 'origin/main'.
(.projectlinkstore modified; the prompt file untracked)

$ git fetch origin; git rev-parse HEAD origin/main; git status -sb | head -2
01315cdd72bf2b2a3437e6e79d9b44fd21bf7505
01315cdd72bf2b2a3437e6e79d9b44fd21bf7505
## main...origin/main
 M .projectlinkstore
```

Locating the build tools:

```
$ find /c/Users/adunc/.silabs -name "ninja.exe" | head -3; find ... -name "cmake.exe" | head -3
/c/Users/adunc/.silabs/slt/installs/conan/p/ninja1a38fc85adcf7/p/ninja.exe
/c/Users/adunc/.silabs/slt/installs/conan/p/cmakefa35ab0687064/p/bin/cmake.exe
```

Line endings (the `LF will be replaced by CRLF` warnings in d.1 are the
repository's normal state, also present in earlier handoffs):

```
$ git config core.autocrlf; file app.c bed_actions.c switch_input.c ble_bed_service.c; git show HEAD:app.c | file -
true
app.c:             C source, ASCII text
bed_actions.c:     C source, ASCII text
switch_input.c:    C source, Unicode text, UTF-8 (with BOM) text, with CRLF line terminators
ble_bed_service.c: C source, ASCII text
/dev/stdin: C source, ASCII text

$ file switch_input.c    (after the D edit)
switch_input.c: C source, Unicode text, UTF-8 (with BOM) text, with CRLF line terminators
```

Working-tree diffs checked before commit B and D (`git diff --stat`):

```
 .projectlinkstore         |  2 +-
 app.c                     | 18 +++++++++---
 bed_actions.c             | 31 ++++++++++++++++++++
 bed_actions.h             |  5 ++++
 bed_status.c              |  1 +
 bed_status.h              |  1 +
 ble_bed_service.c         |  1 +
 docs/BLE_GATT_DESIGN.md   | 43 +++++++++++++++++++++++++++-
 docs/DECISIONS.md         | 73 +++++++++++++++++++++++++++++++++++++++++++++++
 docs/PRE_SHIP.md          |  3 +-
 docs/REGRESSION.md        | 23 +++++++++++++--
 motor_current_functions.c |  7 +++++
 settings_mode.c           |  1 +
 13 files changed, 200 insertions(+), 9 deletions(-)

 .projectlinkstore |  2 +-
 switch_input.c    | 21 +++++++++++++++++----
 2 files changed, 18 insertions(+), 5 deletions(-)
```

Doc edits were made with the Edit tool, `cat >> ... <<'EOF'` appends
(DECISIONS #46, #47, this SESSION_LOG entry) and small Python
string-replace scripts that assert each anchor occurs exactly once; their
only output was `ok`. The resulting changes are the diffs in c.2.

Reading code and docs used the Read tool and `sed -n` / `grep` / `cat`
outside the wrapper: `ble_bed_service.c` (40-1125), `app.c` (60-139,
480-782), `bed_control.c/.h`, `bed_actions.c/.h`, `switch_input.c` (whole),
`bed_status.c/.h`, `auto_level.c` (155-175, 596-622, 760-846),
`auto_level.h`, `motor_current_functions.c` (290-413) and `.h`,
`dual_motor_control.c` (290-430), `settings_mode.c` (50-310) and `.h`,
`ble_bed_service.h` (20-45); `docs/MIGRATION_STATUS.md` (1-60),
`docs/handoffs/README.md` (1-40), the Prompt AC handoff (1-58, 60-859,
1216-1240, 3048-3125), `docs/BLE_GATT_DESIGN.md` §6-§7,
`docs/REGRESSION.md` (170-391), `docs/PRE_SHIP.md`, `docs/DECISIONS.md`
(1425-1442, 2061-2254), `docs/SESSION_LOG.md` (tail, 685-745 inside the
wrapper), `test/bed_control_test.c` (head), `cmake_gcc/CMakePresets.json`,
`cmake_gcc/autogen_toolchain.cmake` (grep). Their content is the files at
`01315cd` (before the session's edits) or at the commit named; every claim
resting on them is cited by file:line in section b and re-grepped at HEAD
inside the wrapper (d.1, the two "verify cited line numbers" runs).

The final `git add` / `git commit` of this file and the SESSION_LOG entry,
and the `git push`, run after this file is written, so their output cannot
be in it (g).

---

## e. VERIFIED vs ASSERTED

Nothing in this session ran on the bed. VERIFIED below means a command
output (section d) or a line of code that was read; it never means
"observed on hardware".

### e.1 VERIFIED, with evidence

| # | Claim | Evidence |
|---|---|---|
| V1 | Start state: HEAD `01315cd` = `origin/main` after `git fetch` | d.3, `git rev-parse HEAD origin/main` (both `01315cdd...`) |
| V2 | Every build exits 0 with no `warning`/`error` lines: baseline, after B, C, D, E, and with `BED_DEBUG_LEVELED_LOG` 0 | d.1 `build.sh` runs (each ends `exit 0`, nothing after the `---` grep line); d.2 table |
| V3 | Image size text 245188 / data 780 / bss 72480 | d.1, `arm-none-eabi-size` output |
| V4 | Host test passes, 0 checks failed (after the B header changes; `bed_control.c` not modified in the session) | d.1, `bed_control_test.exe \| tail -5`: `0 check(s) failed`, `[exit 0]`; c.1 has no `bed_control.c` hunk |
| V5 | `BED_BENCH_PROVOKE_RUNAWAY` 0, `BLE_BENCH_WIPE_BONDS` 0, `BED_CAPTURE_MOTOR_CURRENTS` 0; bench flags `BED_DEBUG_LEVELED_LOG`, `SWITCH_SAMPLER_DIAG`, `BLE_DEBUG_WATCHDOG_LOG` at 1 | d.1 grep: `switch_input.c:98`, `ble_bed_service.c:70`, `debug_capture.h:21`, `bed_actions.c:46`, `switch_input.c:57`, `ble_bed_service.h:36` |
| V6 | No source change to `config/`, `autogen/`, Protocol Version or the GATT database | d.1 `git diff --stat 01315cd e3cc865` (no such paths); c.1 is the complete source diff |
| V7 | At a trip, `service_move_watchdog()` only cancels the watchdog and clears `app_move_active`; no Command Result; `app_auto_level_active` untouched | `ble_bed_service.c:579-586`, quoted in d.1 |
| V8 | The MOVE_START path calls `bed_move_start()` and `app_apply_move_outputs()` whenever `app_move_active` is false, with no `any_collision` test | `ble_bed_service.c:774-784` (d.1 grep); d.1 `grep -n any_collision` shows no reader on that path |
| V9 | The AUTO_LEVEL_START path calls `bed_auto_level_start()` whenever `app_auto_level_active` is false | `ble_bed_service.c:817-825` (d.1 grep) |
| V10 | Current supervision is gated off while `any_collision` is set | `switch_input.c:934` (d.1 grep) |
| V11 | Collision trips set `any_collision` only inside `run_motor_supervision()` paths, and LEVELED is cleared at the end of it | `motor_current_functions.c:307, 323, 367, 401` (d.1 grep); c.1 commit B hunk |
| V12 | Bit 4 is encoded in byte 0, which `status_flags_changed()` already compares, so a change notifies without new notify logic | `ble_bed_service.c:85`, `:978-989`; `bed_status.c:146`; `bed_status.h:63` (d.1 "verify cited line numbers" runs) |
| V13 | Main-loop order: `settings_mode_tick()`, `switch_input_tick()`, `ble_bed_service_tick()` | `app.c:734, 741, 773` (d.1 grep) |
| V14 | `bed_control.c` lights path force-stops an app move and does not set the #36 inhibit | `bed_control.c:153-164` (read); host test `scenario 6c: lights are not motion and are not inhibited` PASS (d.1) |
| V15 | No earlier bench record describes what follows a collision during a held app move | d.1 grep of DECISIONS, REGRESSION_RECORD_PRE_STAGEC and SESSION_LOG (no such record in the hits) |
| V16 | `switch_input.c` kept its UTF-8 BOM and CRLF endings after the D edit | d.3, `file switch_input.c` |

### e.2 ASSERTED (reasoned from code, compiles, not observed)

- **A1. Collision restart during a held app MOVE** (b.A.1 item 4): about
  100 ms after a trip, the next MOVE_START repeat re-energises the same pair
  in the original direction while the collision sequence runs and
  supervision is off; after the sequence the cycle can repeat with
  supervision on. The building blocks are VERIFIED (V7, V8, V10); the
  sequence of events and its timing are not observed.
- A2. A collision during a held app AUTO LEVEL leaves the bed still until
  release (b.A.1 item 5).
- A3. Clearing `app_auto_level_active` at a trip, as A2 is worded, would
  restart auto-level on the next repeat (b.A.2).
- A4. Part B behaves as designed: LEVELED set in all four success outcomes,
  cleared by move, new run, collision and settings-mode entry, survives
  release/STOP/STOP_ALL; prints in the order in the b.B table.
- A5. Part C: amber stays on after STOP_ALL, watchdog stop and disconnect
  stop when the lights are on; possibly a brief flicker; settings mode
  unchanged.
- A6. Part D: a short BED LIGHTS press toggles on release, not on press; a
  5 s hold enters settings mode without toggling; the press still stops an
  app move at once; the D3 table rows.
- A7. The D2 boundary (4.9 s toggles, 5.1 s enters settings mode) - not
  measured.
- A8. A BED LIGHTS press during an app hold lets the app's next repeat
  restart the bed (lights do not latch #36). Pre-existing, unchanged.
- A9. Every expected bench line in b.F.
- A10. Option 1 (b.A.3) would give "nothing moves until release and a fresh
  press". It is a design only; no code exists.

---

## f. QUESTIONS FOR REVIEW

1. **Collision restart during a held app MOVE - today's firmware (b.A.1
   item 4, b.A.4).** From code, a collision while the app MOVE button is
   held is followed ~100 ms later by the next repeat restarting the same
   pair into the obstruction, during the collision sequence and with current
   supervision off. This is pre-existing (not caused by this session),
   ASSERTED, and has no bench record either way. Questions: (a) should it be
   confirmed on the bench before the fix, or treated as real and fixed
   first? (b) until Part A lands, is the guidance "do not bench-test a
   collision while holding an app MOVE button" the right interim control,
   and does anything else need it (e.g. a note in REGRESSION §5)? Part A
   will come in a new prompt; it was not implemented here.
2. **Part A's rule.** Which option in b.A.3? Recommended: Option 1 (app-side
   collision latch in `ble_bed_service.c`, repeats answered 0x06 until the
   app releases, also refused while `any_collision`). Sub-questions: is the
   300 ms silence an acceptable "release" when the stop write is lost; is
   one 0x06 per refused repeat (with its own seq) what the app wants?
3. **"Bed is level" in all four success outcomes** (b.B), not only "both
   axes levelled". Chosen so the print matches bit 4. Accept?
4. **Settings-mode guard on the amber restore** (b.C,
   `bedlights_on && !in_settings_mode`). Mine, not in the prompt. Accept?
5. **BED LIGHTS hold over 5 s whose settings entry was blocked** (b.D, D3
   "App LIGHTS_SET while BED LIGHTS is held"): the release still toggles,
   because settings mode was never entered. Accept, or should any hold
   over 5 s never toggle?
6. **Lights presses do not latch the #36 inhibit** (A8, D3 last row), so a
   BED LIGHTS press stops an app move and the app's next repeat can restart
   it while BED LIGHTS is held. Unchanged here. Fix (with Part A, or
   separately), or accept?
7. **REGRESSION 8.d** records that behaviour rather than passing or failing
   it. Is record-only right until question 6 is ruled on?
8. **Collision flagged during a BED LIGHTS hold** (D3): the
   "`bedlights_on` toggles but the output is not written" gap moved from the
   press to the release. Not expected to happen (no motor running). Leave
   it, or add a PRE_SHIP row?

---

## g. NOT DONE

| Item | Reason |
|---|---|
| **Part A** (0x06 COLLISION_STOP, clearing the two flags, A4 docs) | The prompt's stop rule: the intended behaviour needs a further rule (b.A.2). Options reported in b.A.3. Per the user's direction after this stop, Part A will come in a new prompt and was not implemented in this session. |
| BLE_GATT_DESIGN §7 row for 0x06; a DECISIONS entry for Part A; REGRESSION §5 collision-during-app-hold items | Part of Part A. |
| Any bench run | CLAUDE.md: the user drives all hardware testing. Everything in b.F and e.2 is waiting for the bench. |
| The commit of this file and the SESSION_LOG entry, and the `git push` | Run after this file is written, so their output cannot appear here. They are recorded in the final report to the user; a push failure would be reported without merge, rebase or reset. |
