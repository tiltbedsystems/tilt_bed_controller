# Prompt AH - STOP waits for release (stop latch), bench record, and a report on live level

Written by the reviewer on 2026-11-03. Implement as specified. Where the code
contradicts this document, stop and report it under QUESTIONS FOR REVIEW
instead of improvising. Follow CLAUDE.md for the handoff file, the session
log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
BLE_BENCH_WIPE_BONDS stay 0. Nothing added may block (DECISIONS #27); no
I2C and no printing in a Bluetooth stack callback. config/ and autogen/ are
not hand-edited (no GATT change is needed).

Expected repo state: HEAD b848e48 (Prompt AG handoff), level with
origin/main. Uncommitted: .projectlinkstore (never commit it). This prompt
file is untracked; commit it with the work. If HEAD or origin/main differ,
or anything else is uncommitted or untracked, stop and report.

Read first: DECISIONS #48 (with its Prompt AG amendment) and the AG handoff
(fw_HANDOFF_2026-11-02_03) sections b.1-b.5 and f.

Three parts: A (code), B (docs: bench record and rulings), C (report only,
no code). One commit per part A and B; C goes in the handoff.

======================================================================
A. STOP waits for release (the stop latch)
======================================================================
Bench result 2026-11-02 (Andrew, S23, app 4e66328): holding HEAD up in the
app and tapping the app's STOP stops the bed only briefly; the held button's
next repeat starts it again. The log shows each STOP as a short hold (4
refreshes) followed at once by a new hold. User requirement: **after a STOP
the bed stays stopped, and no app motion is accepted until every app button
has been released.** This withdraws the reviewer's AG f.2 ruling. The app
side (STOP ends every hold in the app) comes in a separate app prompt; the
firmware must not rely on it.

A1. Generalise the collision latch into one app start latch with a reason:
    `app_collision_latched` becomes a latch with reason COLLISION or STOP
    (your naming). Behaviour for COLLISION stays exactly as #48 + AG.
A2. Fold AG f.6 in: one helper that sets the latch AND calls
    note_app_commanding(), used at every place that sets the latch, so the
    invariant from AG b.3 cannot be broken by a future edit. State every
    caller.
A3. Any STOP_ALL - from the control connection or a viewer - sets the latch
    with reason STOP (via the helper), after its existing stop actions. If
    the latch is already set with reason COLLISION, it stays COLLISION.
    STOP_ALL no longer clears the latch from either role. Everything else
    STOP_ALL does today is unchanged (#12 stop-from-anyone, app_stop_all(),
    0x00 to the sender).
A4. While latched with reason STOP, every app MOVE_START / AUTO_LEVEL_START
    from the control connection is refused with a new Command Result
    **0x07 STOP_LATCHED** (its own seq), starts nothing, arms no watchdog,
    and calls note_app_commanding() (via the existing refusal path). Reason
    COLLISION still answers 0x06. The `|| any_collision` term is unchanged.
A5. The latch clears only through note_app_stopped_commanding(): MOVE_STOP,
    AUTO_LEVEL_STOP, the control-phone disconnect, and silence 300 ms after
    the app's last start or refusal. List every caller again (as AG b.4) and
    confirm each is a release of the app's buttons.
A6. State exactly, from code, what happens in each case, with the Command
    Results the app receives and what the motors do:
    - app holding HEAD up (an app that keeps repeating), STOP_ALL from the
      same phone, finger kept down 2 s, then released, then a fresh press;
    - the same with an app that sends MOVE_STOP right after its STOP_ALL;
    - STOP_ALL tapped with nothing held, then a press within 300 ms, and a
      press after 300 ms;
    - app AUTO LEVEL hold, then STOP_ALL;
    - a viewer's STOP_ALL while the control phone holds a move;
    - STOP_ALL while a **physical** button is held: does the physical move
      restart on its own after the STOP? From code only. Do NOT change
      switch_input.c; if it can restart, report it and how to fix it.
A7. Bench prints behind BLE_DEBUG_WATCHDOG_LOG: when STOP_ALL sets the
    latch, `BLE: STOP_ALL latch set (conn=N)`; the existing "latch cleared"
    line gains the reason it was set, e.g.
    `BLE: start latch (stop) cleared (silence), K repeats refused`. Keep
    them in main-loop context, after the stop and the result.
A8. Docs: BLE_GATT_DESIGN.md section 7 - row 0x07 (meaning, when sent, app
    UI: "Stopped - release and press again"); update the 0x06 row's release
    list (STOP_ALL no longer clears). Additive within protocol v1; no
    Protocol Version change. DECISIONS: new entry (next number) - the bench
    result, the rule, why STOP_ALL no longer clears, that the AG f.2 ruling
    is withdrawn. REGRESSION.md: items for every A6 case, and update 5.A4
    and 5.A6 to the new behaviour.
A9. Build from the command line with BLE_DEBUG_WATCHDOG_LOG at 1 and at 0
    (put it back to 1); raw tails, warnings, size change; run the host test.

======================================================================
B. Bench record of 2026-11-02 and rulings (docs only)
======================================================================
Source: Andrew's bench run on b848e48 with the S23 on app 4e66328, log
`PuTTY Logs\RENAME_bench.txt` (read it; quote the lines you cite).
B1. Roll sign is bench-confirmed: wire roll positive = LEFT side raised
    (the app's bubble and words were correct with kRollWireSign = +1).
    Update BLE_GATT_DESIGN.md section 6 (remove "provisional") and any
    DECISIONS entry that calls it unconfirmed.
B2. REGRESSION 6.1 / 6.8 prints and DECISIONS #46: VERIFIED on the bench -
    `LEVELED (Bed Status bit 4) -> 1`, `Exit auto-level`, `Bed is level` on
    four app auto-levels, including both one-axis outcomes; `-> 0` at the
    next move.
B3. DECISIONS #48 / REGRESSION 5.A1 with an app that ends its hold at the
    first 0x06: VERIFIED - log lines 313-323 (trip, `COLLISION_STOP seq=94
    op=0x01 (latched)`, `hold end`, `Once`, `latch cleared (stop), 1
    repeats refused`, `FAILSAFE ...`). The worst case (an app that keeps
    repeating) is not yet bench-tested.
B4. PRE_SHIP F26: the FAILSAFE line was observed after an app collision
    (line 323). Record it; the row stays open for the design ruling.
B5. The collision location idea has a first data point: in that trip the
    whole rise was on motor 2 (output C, foot-right, `Delta 549`, others
    0 / 2 / -24) and Andrew confirms he pushed on the foot-right area. Add
    this to the Phase 1b notes wherever the heat-map / last-trip record is
    planned (PRE_SHIP or DECISIONS - your choice, say which).
B6. Sleep veto #45: every hold in the log shows "longest between passes
    0 ms".
B7. Rulings to record in the SESSION_LOG entry (AG QUESTIONS): f.1
    accepted; f.2 WITHDRAWN - replaced by Part A; f.3 moot under Part A
    (STOP_ALL no longer clears the latch from either role); f.4 fold into
    PRE_SHIP F26 ("what can cut the reverse short": the fail-safe, and any
    app release during the reverse); f.5 moot under Part A; f.6 done in A2.
B8. Not tested yet, for the next bench: the physical BED LIGHTS checks
    (REGRESSION 8, 8.a-8.d - the bench used the app's Lights button, the
    log has no `SW: 0x0008`), 5.A1 with a repeating app, 5.A3.

======================================================================
C. Report only - no code changes
======================================================================
The user wants (1) the level display to stay live while the app is open,
(2) "Level reached" to clear by itself if the bed is tilted by hand, and
(3) the app's "Hold to level" button to fill while the bed takes its first
reading. Report, with file:line citations, VERIFIED/ASSERTED marks:

C1. Tilt today: how and when the accelerometer is read (rate, I2C time per
    read, blocking or not, which context), how tilt reaches the app (the
    telemetry characteristic, bursts, tilt_fresh bit 3, REFRESH_TILT), and
    exactly what DECISIONS #27 / #30 forbid and why.
C2. Options for a live tilt stream while a phone is connected and
    subscribed (for example 2 Hz and 5 Hz): where the read would run, its
    cost per pass, its effect on the Bluetooth link and on sleep (#45,
    PRE_SHIP sleep-clock row), what happens during motor moves and
    auto-level (which already read the accelerometer), and any filtering
    the firmware already applies. Recommend one, with the risks.
C3. A self-clearing LEVELED bit: the tolerances auto-level uses to decide
    "level" (constants, units, file:line), and a proposal for clearing bit
    4 when live tilt leaves a band around level (hysteresis so it does not
    flicker), and whether bit 4 should ever SET without an auto-level run.
    Note what it needs from C2.
C4. The auto-level start delay: from an AUTO_LEVEL_START (or the physical
    press) to the first motor motion, how long it takes and what sets it
    (Step 1 averaging etc.), whether it varies, and whether Bed Status
    already tells the app which phase auto-level is in. What would the app
    need to fill the button accurately (a fixed duration, or a phase bit)?
C5. Anything else in the code that bears on these three (for example the
    pitch/roll filtering, the baseline set point), and a short list of the
    protocol additions each option would need.
