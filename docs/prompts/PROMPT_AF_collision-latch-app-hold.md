# Prompt AF - Part A of Prompt AE: COLLISION_STOP (0x06) and the app collision latch

Written by the reviewer on 2026-11-02. Implement as specified. Where the code
contradicts this document, stop and report it under QUESTIONS FOR REVIEW
instead of improvising. Follow CLAUDE.md for the handoff file, the session
log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
BLE_BENCH_WIPE_BONDS stay 0. Nothing added may block (DECISIONS #27); no
I2C and no printing in a Bluetooth stack callback. config/ and autogen/ are
not hand-edited (no GATT change is needed).

Expected repo state: HEAD is the Prompt AE handoff commit (the commit that
adds docs/handoffs/fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md;
its parent is e3cc865), level with origin/main. Uncommitted:
.projectlinkstore (never commit it). This prompt file is untracked; commit it
with the work. If HEAD or origin/main differ, or the AE handoff is not
committed and pushed, stop and report before changing anything.

Background - read first: the AE handoff section b.A (b.A.1 to b.A.4). The
reviewer and Andrew accepted its **Option 1** on 2026-11-02. The reviewer's
Prompt AE A2 ("clear the two flags so the repeats start nothing") was wrong:
b.A.2 shows clearing them restarts the bed. This prompt replaces Prompt AE
Part A in full.

The defect being fixed (b.A.1 item 4, from code, not yet seen on a bed): a
collision during an app MOVE hold clears app_move_active, so the app's next
MOVE_START repeat (~100 ms later) re-energises the same pair in the original
direction while current supervision is gated off by any_collision.

======================================================================
A. The design (Option 1, as written in b.A.3, with these rulings)
======================================================================
A1. Add `#define CMD_RESULT_COLLISION_STOP 0x06` and
    `static bool app_collision_latched = false;` in ble_bed_service.c.

A2. At the trip - service_move_watchdog(), the `if (any_collision)` branch
    (ble_bed_service.c:579-586 at e3cc865), which runs only while the move
    watchdog is armed, i.e. only during an app hold:
    - move_watchdog_cancel();
    - app_move_active = false;
    - app_auto_level_active = false;
    - app_collision_latched = true;
    - send_command_result(control_connection, last_move_seq,
      CMD_RESULT_COLLISION_STOP);
    Do NOT call note_app_stopped_commanding() here: the user is still
    holding, and the app's half of the #36 latch must stay as it is.

A3. In process_pending_command(), after the viewer check and the
    settings-mode check and BEFORE the #36 conflict check, for
    MOVE_START and AUTO_LEVEL_START only:
    `if (app_collision_latched || any_collision)` ->
    note_app_commanding(); send_command_result(conn, seq,
    CMD_RESULT_COLLISION_STOP); return. No bed_control call, no
    last_move_seq update, no watchdog arm.
    Ruling: the `|| any_collision` term stays. It also refuses an app
    motion start during the ~1.5 s collision sequence after ANY trip,
    including a physical-controller collision with the app idle. State in
    the handoff exactly how long that window is, from code.

A4. Clear app_collision_latched inside note_app_stopped_commanding(), so
    every existing release path clears it: MOVE_STOP, AUTO_LEVEL_STOP,
    STOP_ALL, the control-phone disconnect, and the MOVE_WATCHDOG_MS
    silence timeout (service_app_commanding()). List every caller of
    note_app_stopped_commanding() after the change and confirm each one is
    a real release. If any caller is NOT a release (it would unlatch while
    the user may still be holding), stop and report instead.

A5. The physical controller is not affected: no change to switch_input.c,
    bed_control.c, the collision sequence in app.c or the motor-current
    code. A collision during a physical move sends no Command Result (no
    command); the app learns from Bed Status bit 0 as today.

A6. Question to answer, not to change: during the latch, the app repeats
    are refused and do not arm the watchdog, so DECISIONS #45's sleep veto
    (ble_bed_service_app_hold_active()) is off. Confirm from code that
    nothing moves under the latch except the collision sequence's own
    reverse, so the veto is not needed then.

======================================================================
B. What the held button does - state it exactly in the handoff
======================================================================
For each case give the sequence of Command Results the app receives and
what the motors do, from code (ASSERTED until the bench):
B1. Collision during an app MOVE hold, finger still down for 3 s, then
    released, then a fresh press after the sequence has ended.
B2. The same during an app AUTO LEVEL hold.
B3. Released and pressed again quickly (about 0.5 s after the trip, inside
    the collision sequence).
B4. The link drops during the latch, then reconnects (the latch must be
    clear after the disconnect, and the reconnected app must start nothing
    until a fresh press).
B5. STOP_ALL from the app during the latch.

======================================================================
C. Bench print
======================================================================
Behind BLE_DEBUG_WATCHDOG_LOG (main-loop context only):
- at the trip: `BLE: COLLISION_STOP seq=N op=0x0M (latched)`;
- when the latch clears: `BLE: collision latch cleared (<reason>), K
  repeats refused`, where reason is stop / stop_all / disconnect / silence.
  Count the refused repeats; do not print per repeat.
Print after the motors are off and the result is sent, as the watchdog
print already does.

======================================================================
D. Docs and tests
======================================================================
D1. BLE_GATT_DESIGN.md section 7: row 0x06 COLLISION_STOP - sent once at a
    trip during an app hold (last_move_seq), and in answer to every app
    MOVE_START / AUTO_LEVEL_START while latched or while a collision
    sequence runs (that command's own seq); the app ends its hold and
    shows the collision screen; additive within protocol v1, no version
    change. Do not change the Protocol Version value. Note that the app
    repo's copy was written earlier and will be brought into line by the
    next app prompt; this copy is authoritative.
D2. DECISIONS entry (next number): the defect, Option 1, why Options 2-4
    were rejected (from b.A.3), and that Prompt AE A2 was withdrawn.
D3. REGRESSION.md: items for B1, B2, B3 and B5, each with what to observe.
D4. If the existing host test (the arbitration test) can cover the latch
    without a large new harness, add cases for B1, B3 and the silence
    clear; otherwise say why not and what it would take.
D5. Build from the command line (the user permits it), raw build tail, any
    warnings. Report flash/RAM use change.

======================================================================
E. For the reviewer's bench steps
======================================================================
In the handoff, give the exact console lines expected for B1, B2, B3 and B5
with BLE_DEBUG_WATCHDOG_LOG 1, including the existing trip lines
("Tripped on steady!" or "TRIPPED MAX LOAD", the per-motor deltas, the
"BLE: hold end" line) in their real order.
