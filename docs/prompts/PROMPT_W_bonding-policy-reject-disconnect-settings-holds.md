# Prompt W - bond store policy, disconnect refused pairings, settings-mode holds

Written by the reviewer on 2026-10-18 after the user's bench run of Prompt V
(log: C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\prompt_v_bench.log).
Implement as specified. Where the code contradicts this document, stop and
report it under QUESTIONS FOR REVIEW instead of improvising. Follow CLAUDE.md
for the handoff file, the session log, committing and pushing.
BED_BENCH_PROVOKE_RUNAWAY and BLE_BENCH_WIPE_BONDS stay 0.

Commit this prompt file with the first commit. Expected repo state: HEAD
3f11c12, level with origin/main; .projectlinkstore modified (never commit it).

Make FOUR separate commits, A to D, building from the command line after each
(the user permits it). Report the raw build tail and any warnings.

======================================================================
A. Record the Prompt V bench result (docs only)
======================================================================
Read the log and confirm before recording (all from the user's run):
  - Debounce 2: 88 accepted edges, all "(bounces 0)"; two-button tests gave
    "presses ignored" / "Double Press Error" as designed; no FAILSAFE, no
    "Unrecognized switch mask".
  - Settings mode left with one BED LIGHTS press, four times
    ("Settings mode: exit"); every button worked afterwards, including a
    full auto-level.
  - Pairing: (a) bonded phone reconnected with no confirm_bonding line;
    (b) phone-side bond deleted, not in settings mode:
    "confirm_bonding conn=2 (REPLACING existing bond) -> REJECTED",
    "bonding_failed conn=2 reason=0x1205", then
    "connection_closed conn=2 reason=0x1013" (the phone closed it);
    (c) in settings mode: "-> ACCEPTED (settings mode)" then "bonded";
    (d) after exit the phone reconnected with no confirm_bonding line and
    app movement worked.
  - The phone (Samsung S10, the Flutter app) showed: "Connection failed:
    ... status 137 (GATT_AUTH_FAIL), type ... CHARACTERISTIC_READ".
Record as VERIFIED (user's bench run) in the SESSION_LOG entry, as a dated
"Bench result" note under DECISIONS #42, and in REGRESSION.md (the settings
exit and pairing checks).

======================================================================
B. Bluetooth bonding (ble_bed_service.c) - user decisions 1 and 2
======================================================================
B1. Bond store full -> overwrite the least recently used bond.
    Change sl_bt_sm_store_bonding_configuration(BLE_BED_SERVICE_MAX_BONDS, 0)
    to the policy the SDK documents as "New bonding will overwrite the
    bonding that was used the longest time ago" - confirm the value (2)
    from sl_bt_api.h and cite the line. Rewrite the comment above it, which
    is wrong today (policy 0 pairs but stores nothing; it does not reject).
    Reason, for the comment and the DECISIONS entry: since #42 every new
    bond needs someone at the physical controller, so eviction can no
    longer be triggered by a stranger; the evicted phone re-pairs in
    settings mode.

B2. Disconnect a phone whose pairing the firmware refused.
    Today the Android app happened to close the link itself after the
    rejection (0x1013); another phone or app may not, and the bed has only
    two connection slots. Design:
      - In the confirm_bonding handler, when rejecting, remember that
        connection (per-connection flag sized for the stack's connection
        count).
      - In the sl_bt_evt_sm_bonding_failed handler, if that connection's
        flag is set, call the SDK's non-blocking connection-close command
        for it (confirm the exact function name and signature in
        sl_bt_api.h for this SDK) and print
        "BLE: closing conn=%u - pairing refused".
      - Clear the flag on connection_closed and on any new connection with
        that handle.
    Do NOT close on the confirm_bonding event itself: closing before the
    stack has sent the pairing failure could leave the phone without a
    clean error. Nothing here may block (DECISIONS #27): the close command
    only queues the request.

B3. Put the stored bond number back in the confirm_bonding print:
    "BLE SM: confirm_bonding conn=%u handle=0x%02x (%s) -> %s".

Record B1 and B2 in one new DECISIONS entry (next free number). Update
BLE_GATT_DESIGN.md section 8 to match.

======================================================================
C. Settings mode holds - user decision 3, and two hardening fixes
======================================================================
Background: in settings mode, holding UP ALL or DOWN ALL for 2 s steps the
collision sensitivity, and holding AUTO LEVEL for 2 s stores the zero angle
(held to 5 s, erases it). Each hold runs handle_2000ms_timer /
handle_5000ms_timer; the setting changes when the timeout flag is consumed.

C1. Add settings_mode_cancel_holds() in settings_mode.c (declared in
    settings_mode.h): stops handle_2000ms_timer and handle_5000ms_timer and
    clears timeout_2000ms and timeout_5000ms. No NVM3 write, does NOT leave
    settings mode. settings_mode_exit() calls it instead of its own four
    lines.

C2. STOP ALL in settings mode cancels any hold in progress and STAYS in
    settings mode (user decision). Call settings_mode_cancel_holds() from
    app_stop_all() when in_settings_mode is true. Confirm app_stop_all() is
    only reached from the STOP_ALL opcode path (grep callers; report). The
    amber fast blink must keep running afterwards: check that
    app_stop_all()'s set_all_LED_off() does not leave the blink stuck (the
    blink is driven from settings_mode_tick() every pass) and report what
    the amber LED does.

C3. Fix the stale-timeout race the last handoff found (its QUESTIONS 11):
    the UP_ALL and DOWN_ALL release branches in
    settings_mode_process_outputs() stop handle_2000ms_timer but leave
    timeout_2000ms set, and the AUTO_LEVEL release stops both timers but
    leaves both flags. Each release branch calls settings_mode_cancel_holds()
    instead. Explain in the handoff why that is equivalent for each branch.

C4. Harden the settings-mode entry test (last handoff's QUESTIONS 7). In
    settings_mode_tick(), the entry condition relies on the global
    switch_pressed (the direction of the last delivered edge). Add the
    physical check: BED LIGHTS must be down in the sampler's debounced
    state, i.e. (switch_sampler_down() & SWITCH_BED_LIGHTS) != 0, in
    addition to the existing conditions. Keep everything else.

======================================================================
D. Docs cleanup (docs only)
======================================================================
D1. DECISIONS #19: add one forward-pointer line, "Implemented as #42
    (2026-10-18)". Change nothing else in #19.
D2. CLAUDE.md: the "No BLE glue in this phase" statement is stale (BLE
    Stage B and C are built - MIGRATION_STATUS.md, DECISIONS #34). Replace
    it with the current status in one or two lines, pointing to
    MIGRATION_STATUS.md. Show the diff.
D3. docs/REFACTOR_PLAN.md P3.13 ("Settings mode has no exit"): mark it done,
    pointing to commit dd8d069.
D4. docs/SWITCH_SAMPLER_DESIGN.md: add a one-line note at the top that the
    debounce is now 2 samples (commit 47db449); leave the body as the
    historical design.

======================================================================
E. For the reviewer's bench steps
======================================================================
In the handoff, give the exact PuTTY lines expected for:
  1. pairing refused (phone bond deleted, not in settings mode), including
     the new "closing conn" line, and what happens if the phone closes
     first;
  2. STOP in the app while holding UP ALL in settings mode (before the
     2 s step): nothing stored, amber keeps blinking, next UP ALL hold
     needs the full 2 s;
  3. settings-mode entry and exit still working after C4.
Also propose how B1 (overwrite oldest bond) could be checked on the bench
with one or two phones, without changing code. If it cannot, say so.
