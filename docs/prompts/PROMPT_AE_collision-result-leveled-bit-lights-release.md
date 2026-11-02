# Prompt AE - Phase 1a: collision result, "levelled" status bit, amber fix, lights on release

Written by the reviewer on 2026-11-02. Implement as specified. Where the code
contradicts this document, stop and report it under QUESTIONS FOR REVIEW
instead of improvising. Follow CLAUDE.md for the handoff file, the session
log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
BLE_BENCH_WIPE_BONDS stay 0. Nothing added may block (DECISIONS #27); no
I2C or printing in a Bluetooth stack callback. config/ and autogen/ are not
hand-edited (this prompt needs no GATT change).

Expected repo state: HEAD 01315cd (Prompt AC handoff), level with
origin/main. Uncommitted: .projectlinkstore (never commit it). This prompt
file is untracked; commit it with the first commit. If HEAD or origin/main
differ, stop and report before changing anything.

Background: the Prompt AC handoff (fw_HANDOFF_2026-11-01_02) sections B1,
B3, B5 and B8, and its QUESTIONS 2, 8 and 9. Read them first. The app is
being rebuilt in parallel (Prompt AD, app repo) and will use the two
protocol additions below.

Protocol: both additions are additive within protocol v1 (a new flag bit
that was always 0, a new Command Result code). BLE_GATT_DESIGN.md
"Protocol version and this change" allows amending v1 in place before
first ship. Do NOT change the Protocol Version value. Update
BLE_GATT_DESIGN.md sections 6 and 7 to specify both.

Make one commit per part (A-E), building from the command line after each
(the user permits it). Report the raw build tail and any warnings.

======================================================================
A. Collision during an app hold: tell the app, end the hold
======================================================================
Today (AC handoff B8 row 5, QUESTIONS 9): a collision during an app hold
sends no Command Result, cancels the watchdog silently, and leaves
app_auto_level_active set, so the app's later repeats are accepted.
A1. Add Command Result 0x06 COLLISION_STOP. When a collision trip (summed
    rise or max load, either direction) happens while the app holds the bed
    (the dead-man watchdog was armed for the control connection), send
    COLLISION_STOP with last_move_seq to the control connection, from main-
    loop context after the motors are off.
A2. At the same point clear app_move_active and app_auto_level_active, so
    the app's following repeats start nothing. State exactly what the next
    MOVE_START / AUTO_LEVEL_START repeat from the still-held app button
    does after this change (accepted and ignored? restarts? rejected with
    which code?). The intended behaviour: the bed must not move again until
    the user releases and presses again. If the code cannot give that
    without a further rule, stop and report the options.
A3. A collision during a physical-controller move: no Command Result (there
    is no command); the app learns from Bed Status bit 0 as today.
A4. Document 0x06 in BLE_GATT_DESIGN.md section 7 (meaning, when sent, app
    UI: the collision screen) and add a DECISIONS entry.

======================================================================
B. "Levelled" status bit and the dead "Bed is level" print
======================================================================
B1. Bed Status flag bit 4 (0x10) = LEVELED: set when an auto-level run
    (either source) ends successfully - both axes levelled, or Step 1 found
    no levelling needed, or one axis disabled and the other levelled (AC
    handoff B8 rows 1-4). Clear it when any motion starts (any move, ALL,
    a new auto-level run, the collision reverse), on a collision trip, and
    on entering settings mode. It stays set while the user keeps holding
    AUTO LEVEL after success and after release, until one of those clears
    it.
B2. A change of bit 4 must trigger a Bed Status notification, like the
    other state bits (status_flags_changed()).
B3. Fix the dead branch (AC QUESTIONS 2): "Bed is level" prints when a run
    ends successfully. Keep the existing prints. Update REGRESSION.md 6.1 /
    6.8 so they can pass.
B4. Specify bit 4 in BLE_GATT_DESIGN.md section 6 and add a DECISIONS
    entry.

======================================================================
C. Amber indicator after STOP_ALL / watchdog / disconnect stop
======================================================================
AC QUESTIONS 8: app_stop_all() turns the amber LED off while the lights
stay on. After app_stop_all() restore amber if the lights are on, exactly
as the collision sequence and app MOVE_STOP already do. Do not change the
lighting output itself.

======================================================================
D. BED LIGHTS toggles on release (user decision)
======================================================================
User decision 2026-11-02: holding BED LIGHTS to enter settings mode must
not cycle the lights. The lights toggle when a short press is RELEASED;
a hold that reaches settings mode (5 s) does not toggle them; a press while
in settings mode exits settings mode and does not toggle them.
D1. Keep the safety rule that any physical input from the other source
    stops the bed immediately (DECISIONS #12): a BED LIGHTS PRESS while the
    app is moving the bed must still force-stop it on the press, not the
    release. Only the lights toggle moves to the release.
D2. Define "short press": released before the settings-mode entry
    completes. State what happens for a press released at 4.9 s (toggle)
    and at 5.1 s (settings mode, no toggle).
D3. Report every other path that this touches (double-press detection,
    the fail-safe, the sampler's release-first ordering, arbitration,
    lights from the app while a physical BED LIGHTS press is down) and how
    each behaves after the change.
D4. DECISIONS entry; REGRESSION.md items for: short press toggles on
    release; 5 s hold enters settings mode with lights unchanged; press in
    settings mode exits with lights unchanged; BED LIGHTS press stops an
    app move immediately.

======================================================================
E. PRE_SHIP.md rows (reviewer rulings on AC QUESTIONS)
======================================================================
Add rows (next free ids), each with owner and how closed:
- the ~450-pass blind window at the start of every move (AC B1(a));
- auto-level pulse phase has no current supervision (Q3);
- the continuous re-baseline never reaches the trip test (Q4);
- auto-level uses the raise margin for lowering motors (Q5);
- unconditional console prints to review before release (Q12);
- hold 9: HEAD pair apparently did not run during PITCH levelling (Q6) -
  owner Andrew + Claude, closed by a bench look with limit prints;
- bootloader and flash layout need their own design (Q7) - extend the
  existing bootloader row.
Rulings to record in the SESSION_LOG: Q1 accept 23 lines and both gap
ranges; Q10 erase-needs-reboot is fixed with the settings work (Phase 2),
held repeat-stepping stays; Q11 PRE_SHIP.md structure accepted.

======================================================================
F. For the reviewer's bench steps
======================================================================
In the handoff, give the exact lines expected for: a physical auto-level
that succeeds ("Bed is level", bit 4 set); an app auto-level that succeeds;
a collision during an app MOVE hold (the COLLISION_STOP result and what the
held button does next); a short BED LIGHTS press; a 5 s BED LIGHTS hold;
STOP_ALL with the lights on (amber stays on). Say how the reviewer can see
bit 4 in the log (add a one-line print on each change of bit 4, behind
BLE_DEBUG_WATCHDOG_LOG or a new bench flag that defaults to 1 and is listed
in PRE_SHIP.md).
