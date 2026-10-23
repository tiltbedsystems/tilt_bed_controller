# Prompt Y - watchdog-stop diagnostics, Prompt W follow-ups

Written by the reviewer on 2026-10-18. Implement as specified. Where the code
contradicts this document, stop and report it under QUESTIONS FOR REVIEW
instead of improvising. Follow CLAUDE.md for the handoff file, the session
log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
BLE_BENCH_WIPE_BONDS stay 0. Nothing added may block (DECISIONS #27).

Expected repo state: HEAD 18d8378, level with origin/main. Uncommitted:
.projectlinkstore (never commit it) and CLAUDE.md (an edit the user
authorised and the reviewer made - see A1). This prompt file is untracked;
commit it with commit A.

Make THREE commits, A to C, building from the command line after each (the
user permits it). Report the raw build tail and any warnings.

======================================================================
A. Docs (docs only)
======================================================================
A1. Commit the reviewer's CLAUDE.md edit as it is (the "No BLE glue" bullet
    replaced by a "BLE status" bullet). Show its diff. Do not edit it.
A2. docs/MIGRATION_STATUS.md says BLE Stage C is "Not started" (lines ~51,
    70, 100-102 per the last handoff). Correct it to match DECISIONS #34,
    #42 and #43: Stage C built and bench-tested; give the dates and commits
    you can cite. Keep anything that is genuinely still gated (e.g. the full
    REGRESSION.md pass) marked as such. Show the diff.
A3. Record the Prompt W bench result. Log:
    C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\prompt_w_bench.log
    Confirm from the log before recording: store_bonding_configuration ->
    0x0000; bonded reconnect with no confirm_bonding; refusal with
    "closing conn=2 - pairing refused" and connection_closed reason=0x1022
    (not the predicted 0x1016 - look up 0x1022 in sl_status.h and state its
    meaning); acceptance in settings mode; STOP during an UP ALL hold wrote
    nothing; the next full hold stepped to 350/250; DOWN ALL restored
    550/275. Note the phone was a Galaxy S23 (SM-S911U) for this run, not
    the S10. Record as VERIFIED (user's bench run) under DECISIONS #43, in
    REGRESSION.md, and in the SESSION_LOG entry.
A4. Add DECISIONS entry (next free number) for user decision 3: STOP in
    settings mode cancels any hold in progress and stays in settings mode
    (implemented in 840d7b8 inside app_stop_all(), which also runs on
    watchdog expiry and control-phone disconnect - the reviewer accepts
    that: cancelling a settings hold on any full stop is correct).
A5. REGRESSION.md: add explicit checklist items for the pairing window
    (bonded reconnect, refusal + bed closes the link, acceptance in
    settings mode, reconnect after exit) and for "STOP during a settings
    hold". Boxes unchecked.

======================================================================
B. Settings-mode entry: freshness check (small)
======================================================================
In settings_mode_tick()'s entry condition, also require
switch_state_fresh() (as the fail-safe does), so stale sampler data cannot
hold the entry timer running. One condition; nothing else changes.

======================================================================
C. Dead-man watchdog stop diagnostics (bench diagnostics, no behaviour change)
======================================================================
Problem (user's bench, 2026-10-18, Galaxy S23, wireless debugging both on
and off): while holding AUTO LEVEL in the app, the app intermittently shows
"Connection stalled - movement stopped", which is Command Result 0x05
(CMD_RESULT_WATCHDOG_STOP). prompt_w_bench.log shows "Step 1: ROLL and
PITCH check" printed 3-4 times in a row at the start of several holds: each
is the app's next AUTO_LEVEL_START restarting auto-level after a watchdog
stop (process_pending_command: AUTO_LEVEL_START when !app_auto_level_active
calls bed_auto_level_start()). The app repeats AUTO_LEVEL_START every 100 ms
with writeCharacteristicWithoutResponse; MOVE_WATCHDOG_MS is 300. Physical
auto-level works.

We need data to decide whether the 300 ms gap is on the phone side (writes
not arriving) or the bed side (main loop not getting to them). Add, behind
a new define BLE_DEBUG_WATCHDOG_LOG (default 1, pre-ship 0):

C1. Refresh timing. Each time move_watchdog_arm() is called, record the
    sleeptimer tick, and keep the gaps (ms) between the last 8 refreshes in
    a small ring buffer. Also record the tick at which the Bluetooth write
    for the command was RECEIVED in the stack callback (the place that fills
    pending_command), so receive-time and process-time can be compared.
C2. Main-loop pass time. In app_process_action(), measure each pass's
    duration (sleeptimer ticks at entry and exit) and keep the longest pass
    seen since the last watchdog refresh. Measure only - no prints per
    pass. Also count passes since the last refresh.
C3. On every watchdog stop (where CMD_RESULT_WATCHDOG_STOP is sent), print
    one line block, e.g.:
      "BLE: WATCHDOG STOP op=0x03 seq=N: X ms since last refresh,
       last write received Y ms ago, longest loop pass Z ms, P passes,
       last gaps a,b,c,d,e,f,g,h ms"
    Use whatever fields make this unambiguous; keep it to a few lines.
C4. Extend the existing connection_parameters print with latency and
    supervision timeout (check the event's field names in sl_bt_api.h for
    this SDK and cite them). Peripheral latency above 0 would delay the
    phone's writes, so we need to see it.
C5. Report only, change nothing: from the code, list everything on the
    auto-level path (app.c auto-level block, steady_accel_read_both_axes,
    auto_level_single_axis, auto_level_blink_LED / set_LED_on/off, the
    switch sampler idle check) that can block the main loop, with an
    estimate of how long, e.g. LED writes go to both UI boards and each
    ui_transaction() without a held re-driver costs two on-board
    read-modify-writes plus a 2 ms delay, and the absent Aux board NACKs.
    Also state whether app_is_ok_to_sleep() lets the CPU sleep during
    auto-level Step 1 (no motor on yet) and what wakes it.
Nothing in C may block or print per pass. All prints happen only on a
watchdog stop or a connection event.

======================================================================
D. For the reviewer's bench steps
======================================================================
In the handoff, give the exact lines expected for: a normal app auto-level
hold (no WATCHDOG STOP); a deliberate watchdog stop (e.g. turn the phone's
Bluetooth off mid-hold); and the extended connection_parameters line.
