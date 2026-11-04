# Prompt AK - Prompt AI follow-ups (reviewer rulings on the AI handoff)

Written by the reviewer on 2026-11-03. Implement as specified. Where the code
contradicts this document, stop and report it under QUESTIONS FOR REVIEW
instead of improvising. Follow CLAUDE.md for the handoff file, the session
log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
BLE_BENCH_WIPE_BONDS stay 0. Nothing added may block (DECISIONS #27).
config/ and autogen/ are not hand-edited.

Expected repo state: HEAD 08afb33 (Prompt AI handoff), level with
origin/main. Uncommitted: .projectlinkstore (never commit it). This prompt
file is untracked; commit it with the work. If not, stop and report.

Read first: the AI handoff (fw_HANDOFF_2026-11-03_02) sections b.A.2, b.A.3
and f.

======================================================================
A. Code (one commit)
======================================================================
A1. (AI f.1) Make the new top-of-tick call conditional:
    `if (!move_watchdog_armed) service_app_commanding();` at the top of
    ble_bed_service_tick(). The existing call later in the tick stays as
    it is. Confirm from code that this closes the narrow #36 window in AI
    b.A.2 item 5 and leaves AI A2's purpose intact (latched releases are
    still judged before the next command, because no start latch can be
    set while the watchdog is armed). State every case whose behaviour
    differs from 08afb33.
A2. (AI f.9) The no-latch STOP_ALL print gains the connection:
    `BLE: STOP_ALL (no app hold, no latch) (conn=N)`.
A3. Build from the command line with BLE_DEBUG_WATCHDOG_LOG at 1 and at 0
    (put it back to 1); raw tails, warnings, size change; host test.

======================================================================
B. Docs (same commit or a second one)
======================================================================
B1. DECISIONS #49: a short "Amended by Prompt AK" paragraph for A1.
B2. PRE_SHIP: a row for AI f.7 - the reset-cause print depends on GCC
    inlining sl_hal_emu functions at -Os (sl_hal_emu.c is not in the
    build); owner Andrew + Claude; closed by adding the proper component
    in Simplicity Studio before release, or by confirming the build
    settings are frozen.
B3. Rulings to record in the SESSION_LOG entry (AI QUESTIONS): f.1 fixed
    (A1); f.2 confirmed (bursts are not part of the idle sampler; they
    count for the 5 Hz cadence); f.3 accepted (seed from the per-pass chain
    only if it ran in the pass before); f.4 intended (a clean release with
    a stop write clears app_commanding at once); f.5 confirmed (tilt_fresh
    = 1 while the shipped chain updated this pass, including auto-level and
    the settings capture); f.6 keep (the 10 s lines start after
    convergence); f.7 PRE_SHIP row (B2); f.8 leave until F15; f.9 done
    (A2); f.10 accepted (bit 5's two extra guards); f.11 fine.

======================================================================
C. Report
======================================================================
In the handoff: the A1 analysis, the raw build output, and the exact
console lines the reviewer's next bench test should expect for: an app
STOP while holding a move; an app STOP with nothing held; a STOP from a
second phone (viewer) while the first holds a move.
