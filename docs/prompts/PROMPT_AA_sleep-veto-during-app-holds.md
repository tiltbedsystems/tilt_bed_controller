# Prompt AA - keep the CPU awake during app holds (watchdog-stall fix)

Written by the reviewer on 2026-11-01. Implement as specified. Where the code
contradicts this document, stop and report it under QUESTIONS FOR REVIEW
instead of improvising. Follow CLAUDE.md for the handoff file, the session
log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
BLE_BENCH_WIPE_BONDS stay 0. Nothing added may block (DECISIONS #27).
config/ and the SDK are not hand-edited.

Expected repo state: HEAD c945146 (Prompt Y handoff), level with origin/main.
Uncommitted: .projectlinkstore (never commit it). This prompt file is
untracked; commit it with commit A. If HEAD or origin/main differ, stop and
report before changing anything.

Make TWO commits, A and B, building from the command line after each (the
user permits it). Report the raw build tail and any warnings.

======================================================================
Evidence (user's bench, 2026-11-01, desk bed, iPhone TestFlight build
e22e4b9, firmware c945146)
======================================================================
Log: C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\autolevel_stall_bench.log
Read it before starting. Six WATCHDOG STOP blocks (lines 28, 53, 58, 63,
122, 127). In every one:
- since last refresh 300-301 ms, last write received 301 ms ago with the
  SAME seq as the last refresh: nothing reached the application for 300 ms;
- received->processed 0-1 ms, longest pass 2-17 ms: the bed was not busy;
- passes 23-39, longest between passes 27-28 ms: the loop was waking about
  once per connection event (interval=24, i.e. 30 ms) - the CPU was sleeping
  between passes;
- refresh gaps show bunching, e.g. "180,0,0,0,207,0,148,28" and
  "20,2,1,1,112": writes arrive late, then several in the same millisecond.
The app's Hold diagnostics for the same holds show the phone issuing a
START every 100-103 ms (max gap 102-103 ms, err 0).

Every stall falls where an app hold is active and the CPU is allowed to
sleep: app auto-level Step 1, and the steady check after an axis finishes
(line 28 follows "ROLL Leveled successfully..."). No stall happened while a
motor ran (motor_on vetoes sleep) and none on the physical controller (the
switch sampler vetoes sleep). App MOVE holds never stall (motor running).
The user also saw the same symptom on a second bed (Tilt Van) and on a
Galaxy S23.

Reviewer's reading (ASSUMED, not proven): while the CPU is in EM2 the link
sometimes misses the phone's packets, which are then retried and delivered
in a burst. A sleep-clock accuracy or EM2 wake-up timing issue is a
candidate. Commit B tests the reading by removing EM2 during app holds.

======================================================================
A. Docs (docs only)
======================================================================
A1. SESSION_LOG / REGRESSION / DECISIONS #43 area as appropriate: record
    the 2026-11-01 bench as VERIFIED (user's bench run), from the log and the
    user's report:
    - the six WATCHDOG STOP blocks and the reading above (state the reading
      as the reviewer's hypothesis, not a finding);
    - connection parameters from the iPhone: interval=24 latency=0, initial
      timeout=72 then 100 after the bed's request (log lines 13-16);
    - iPhone pairing refusal (lines 155-158): REJECTED (not in settings
      mode), bonding_failed 0x1205, the bed closed the link with reason
      0x1016; the iPhone showed "CBATTErrorDomain Code=15 Encryption is
      insufficient"; acceptance in settings mode at lines 173-180;
    - two phones connected, the second could not control the bed (user's
      report; DECISIONS #34 behaviour);
    - the user reports the "Tripped on steady!" collision trip in the
      Prompt W log was benign (closes Prompt Y QUESTIONS 9).
A2. Record the reviewer's rulings on Prompt Y's QUESTIONS in the
    SESSION_LOG: (1) keep fields B and T and the last-write op/seq;
    (2) no deliberate-stall bench step needed; (3) no change - stale data
    blocking completion is sufficient; (4) addressed by this prompt;
    (5) the 300 s I2CSPM timeout is a pre-ship item for its own prompt -
    add it to the pre-ship list wherever that list lives; (6) stale comment
    at bed_control.c:73 - fix it in commit B (comment only); (7) keep the
    Stage C box ticked; (8) no change now.

======================================================================
B. Sleep veto while an app hold is active
======================================================================
B1. In app_is_ok_to_sleep() (app.c), add a veto: return false while the
    app's dead-man watchdog is armed (move_watchdog_armed in
    ble_bed_service.c). Expose it through a small accessor in
    ble_bed_service.h, e.g. bool ble_bed_service_app_hold_active(void).
    Use the watchdog-armed state, not app_move_active/app_auto_level_active:
    it is bounded by MOVE_WATCHDOG_MS after the last write, so a lost phone
    can never pin the CPU awake. Comment it in the same style as the other
    vetoes, citing this prompt's evidence and the new DECISIONS entry. State
    in the handoff when move_watchdog_armed is set and cleared, with line
    numbers, so the reviewer can confirm the veto covers the whole hold and
    ends with it.
B2. Hold-end summary print, behind BLE_DEBUG_WATCHDOG_LOG (bench only, so
    the fix can be confirmed without a stall): when an app hold ends
    WITHOUT a watchdog stop (AUTO_LEVEL_STOP, MOVE_STOP, STOP_ALL, or
    whatever ends the hold in this code - list them), print one line AFTER
    the stop has been carried out, e.g.
      "BLE: hold end op=0x03 seq=N: R refreshes, longest refresh gap G ms,
       longest between passes B ms"
    where G and B cover the whole hold (not just since the last refresh).
    One line per hold, never per pass. Expected after B1: B about 0-1 ms
    during the whole hold. Keep the WATCHDOG STOP block unchanged.
B3. Add DECISIONS entry (next free number): during an app hold the CPU stays
    in EM0, for the same reason motors and the switch sampler veto sleep;
    the evidence; the bound (MOVE_WATCHDOG_MS); that this also closes Prompt
    Y QUESTIONS 4 (app-driven Step 1 now runs the loop like the physical
    controller does, so DECISIONS #29's pass-based averaging sees the same
    pass rate) - state any effect on #29 you can see in the code.
B4. Fix the stale comment at bed_control.c:73 (force-stops call
    app_stop_movement(), not app_stop_all(), since #36). Comment only.
B5. REGRESSION.md: add an unchecked item "app AUTO LEVEL: 10 holds from
    off-level, no WATCHDOG STOP / no 'Connection stalled'".

======================================================================
C. Report only - change nothing
======================================================================
C1. Low-frequency clock used in EM2 by the radio/sleeptimer: which source
    (LFXO or LFRCO; precision mode if LFRCO), from config/ and autogen/
    with file:line. Whether the BGM220 module variant in the .slcp has a
    32.768 kHz crystal, if the project or SDK states it (say if not
    found).
C2. The Bluetooth stack's sleep-clock accuracy setting (the ppm value the
    link layer assumes), from config/ (e.g. sl_bluetooth_config.h), with
    file:line, and the SDK's documented meaning.
C3. Any EM2 / HFXO wake-up settings relevant to radio timing, with
    file:line.
C4. From C1-C3 and the SDK documentation you can cite: is there a
    configuration mismatch that would make the link layer miss packets in
    EM2? Mark every claim VERIFIED (cited) or ASSUMED. Do not change any
    configuration.

======================================================================
D. For the reviewer's bench steps
======================================================================
In the handoff, give the exact lines expected for a normal app auto-level
hold with B1 and B2 in place (including the new hold-end line), and confirm
that a physical-controller hold and an idle connected bed are unaffected by
B1 (state from code whether the CPU still sleeps when the app is connected
but no button is held).
