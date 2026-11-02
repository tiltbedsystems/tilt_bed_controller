# Prompt AG - collision latch follow-ups (reviewer rulings on the AE and AF handoffs)

Written by the reviewer on 2026-11-02. Implement as specified. Where the code
contradicts this document, stop and report it under QUESTIONS FOR REVIEW
instead of improvising. Follow CLAUDE.md for the handoff file, the session
log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
BLE_BENCH_WIPE_BONDS stay 0. Nothing added may block (DECISIONS #27); no
I2C and no printing in a Bluetooth stack callback. config/ and autogen/ are
not hand-edited (no GATT change is needed).

Expected repo state: HEAD is the Prompt AF handoff commit (the commit that
adds docs/handoffs/fw_HANDOFF_2026-11-02_02_collision-latch-app-hold.md; its
parent is e646c3b), level with origin/main. Uncommitted: .projectlinkstore
(never commit it). This prompt file is untracked; commit it with the work.
If HEAD or origin/main differ, or anything else is uncommitted or untracked,
stop and report before changing anything.

Read first: the AF handoff sections b.3, b.6, b.11 and f.

Scope: ble_bed_service.c and docs only. No change to switch_input.c,
bed_control.c, app.c, the collision sequence or the motor-current code.

======================================================================
A. Three changes to the collision latch (AF QUESTIONS f.1, f.2, f.7)
======================================================================
A1. (f.1) A STOP_ALL from a connection that is NOT the control connection
    must not clear app_collision_latched. Everything else that STOP_ALL
    does today stays exactly as it is, for both roles. After a viewer's
    STOP_ALL during a latch, the latch must still clear by the control
    app's own release paths - its MOVE_STOP / AUTO_LEVEL_STOP, its own
    STOP_ALL, the control-phone disconnect, and silence 300 ms after its
    last repeat. So the silence timer must be running after a viewer
    STOP_ALL even if the control app sends nothing more. Implement it in
    the simplest way that gives that, and say how. A STOP_ALL from the
    control connection still clears the latch, as now.

A2. (f.2) The `any_collision` refusal in process_pending_command() sets
    app_collision_latched = true as well, so any app hold that meets a
    collision - including a quick re-press inside the collision sequence,
    and an app press during a physical-controller collision - must be
    released before the bed accepts a new start. The refusal already calls
    note_app_commanding(), so the silence clear stays armed. The
    "latch cleared" bench print now covers these refusals too (f.5 is
    resolved by this).

A3. (f.7) The trip branch in service_move_watchdog() also calls
    note_app_commanding(), so the silence timer always runs after a latch
    is set. Still NOT note_app_stopped_commanding() there. Confirm that
    note_app_commanding() only refreshes the app's half of the #36 latch
    and the silence deadline, and changes nothing else.

======================================================================
B. What changes for the user - state it in the handoff
======================================================================
Redo the AF b.6 tables for the cases these change, from code (ASSERTED
until the bench), with an app that keeps repeating and one that ends its
hold at the first 0x06:
B1. AF B3 - released and pressed again ~0.5 s after the trip, button still
    held past the end of the sequence.
B2. AF B5 - STOP_ALL during the latch, from the viewer phone and from the
    control phone, inside and after the sequence.
B3. An app MOVE press during a physical-controller collision sequence.
B4. Confirm AF B1, B2 and B4 are unchanged, or say what changed.
List every caller of note_app_stopped_commanding() again, as in AF b.3,
after the change.

======================================================================
C. Docs
======================================================================
C1. BLE_GATT_DESIGN.md section 7, row 0x06: a refusal during any collision
    sequence also latches; a viewer's STOP_ALL does not clear the latch.
    No Protocol Version change.
C2. DECISIONS #48: add an "Amended by Prompt AG" paragraph with the three
    changes and why (do not rewrite the existing entry).
C3. REGRESSION.md section 5: update 5.A3, 5.A4 and 5.A5 to the new
    behaviour; add an item for a viewer STOP_ALL during the latch.
C4. PRE_SHIP.md, new rows (next free ids), each with owner and how closed:
    - the runaway fail-safe cuts the collision reverse to ~150-200 ms after
      an app collision and after a physical collision once the button is
      released (AF b.11.1, f.3) - owner Andrew + Claude; first closed by
      observing the FAILSAFE line and the reverse on the bench
      (REGRESSION 5.A1), then a design ruling;
    - a BED LIGHTS press stops an app move but does not latch the #36
      inhibit, so the app's next repeat can restart it briefly while BED
      LIGHTS is held (AE QUESTIONS 6) - owner Claude; first closed by the
      bench record REGRESSION 8.d;
    - a collision flagged during a BED LIGHTS hold toggles bedlights_on at
      the release without writing the output (AE QUESTIONS 8) - owner
      Claude; not expected to happen, review before release.
C5. Rulings to record in the SESSION_LOG entry. AE QUESTIONS: 1 and 2
    closed by Prompt AF; 3 accepted ("Bed is level" in all four success
    outcomes); 4 accepted (settings-mode guard on the amber restore); 5
    accepted (a blocked 5 s settings entry still toggles at the release);
    6 PRE_SHIP row (C4); 7 yes, REGRESSION 8.d stays record-only until the
    bench; 8 PRE_SHIP row (C4). AF QUESTIONS: f.1, f.2, f.7 fixed here; f.3
    PRE_SHIP row (C4); f.4 accepted; f.5 resolved by A2; f.6 keep
    REGRESSION 5.A5; f.8 agreed - the reviewer's bench test uses a soft
    obstruction.

======================================================================
D. Build and report
======================================================================
D1. Build from the command line with BLE_DEBUG_WATCHDOG_LOG at 1 and at 0
    (put it back to 1). Raw build tails, any warnings, size change. Run the
    existing host test.
D2. Expected console lines for B1, B2 and B3 with BLE_DEBUG_WATCHDOG_LOG 1,
    in their real order, in the same form as AF b.10.
D3. Handoff section d: only this session's real command output. Do not
    paste copies of other files into it. If a command's output is lost,
    say so.
D4. Before committing, run git status and commit only the files this
    prompt changes, the handoff, docs/SESSION_LOG.md and this prompt file.
    Do not delete anything outside the repo's tracked files.
