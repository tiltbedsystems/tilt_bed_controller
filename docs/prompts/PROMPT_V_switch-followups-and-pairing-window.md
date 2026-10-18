# Prompt V - switch follow-ups, settings-mode exit, pairing window

Written by the reviewer on 2026-10-18. Implement as specified. Where the code
contradicts this document, stop and report it under QUESTIONS FOR REVIEW
instead of improvising. Follow CLAUDE.md for the handoff file, the session
log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY stays 0.

Commit this prompt file together with the work it describes.

Expected repo state: HEAD 3d96128, level with origin/main;
.projectlinkstore modified (never commit it); this file untracked.

Make FOUR separate commits, in this order, so each can be reviewed and
reverted on its own: A, B, C, D below. Build from the command line after
each (the user permits it) and report the raw tail and any warnings.

======================================================================
A. Record the 2026-10-18 bench result for the switch sampler (docs only)
======================================================================
The user ran the bench test for d1f13d0 (DECISIONS #41). Log:
C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\sampler_bench.log
Read it and confirm these figures from it before writing them down:
  - Provoked runaway, HEAD pair: caught after 209, 248, 209, 209, 209 ms.
  - Provoked runaway, ALL MOTORS: 209, 210, 210, 217, 249 ms (was
    271-307 ms before the sleep veto - the gap is gone).
  - No FAILSAFE line except directly after a PROVOKE line; no
    "Unrecognized switch mask"; no "read failed".
  - ~70 two-button presses across many pairs: each gave "presses ignored"
    (nothing moved) or "Double Press Error" (brief start, then stop), and
    every release came through cleanly.
  - Bounce tally over ~700 accepted edges: 696 with 0 bounces, 4 with 1.
  - Power-up with UP HEAD held: no motion; release printed "SW: 0x8000 up";
    next press worked.
Record this as VERIFIED (user's bench run) in the SESSION_LOG entry, add a
dated "bench result" note under DECISIONS #41, and add a section to
docs/REGRESSION.md titled "Switch sampler (DECISIONS #41)" holding the
checklist from fw_HANDOFF_2026-10-17_05 section b.12 plus these results.

======================================================================
B. Switch follow-ups (small)
======================================================================
B1. SWITCH_DEBOUNCE_SAMPLES 3 -> 2 (switch_input.c). User decision, based
    on the bounce data above: stops are confirmed ~10 ms sooner (10-20 ms
    after release instead of 20-30 ms), and a single-sample bounce is still
    rejected. Update the constant's comment ("2 identical reads = >=10 ms
    stable") and every comment or DECISIONS #41 statement that says 3
    samples or 20-30 ms.

B2. Double Press Error releases the physical controller's half of the
    conflict latch. In switch_input_tick()'s "Double Press Error" block,
    after clear_switch_panel_state(), add
        bed_note_source_released(BED_SOURCE_SWITCHES);
    with a comment: same reason as in motion_failsafe_tick() - clearing
    switch_held means the held buttons' later releases find no match and
    never reach bed_move_stop(), so without this the switches' half of the
    #36 latch could stay set until some later press/release.

B3. bed_actions.c includes switch_input.h but (per the 05 handoff) uses
    nothing from it now. Remove the include only if the build stays clean
    and a grep shows no use; otherwise leave it and say why.

======================================================================
C. Settings mode: exit by pressing BED LIGHTS again (user decision)
======================================================================
Today in_settings_mode is only ever set true (settings_mode.c:223); the only
way out is a reset. Add an exit.

Behaviour:
  - While in settings mode, a NEW press of BED LIGHTS exits settings mode.
    (The press that entered it was held through entry; its release arrives
    while in settings mode and must NOT exit. Only a later press exits.)
  - The exit press does nothing else: it does not toggle the under-bed
    lights and cannot start a new entry, however long it is held. Its
    release, arriving in normal mode, must do nothing.
  - On exit:
      * in_settings_mode = false; print "Settings mode: exit".
      * Stop both settings timers (handle_2000ms_timer, handle_5000ms_timer)
        and clear their timeout flags, so an exit in the middle of a
        store/erase/sensitivity hold leaves nothing half-done. Do not write
        NVM3 on exit.
      * set_all_motors_off() (settings mode is excluded from the motion
        fail-safe, so do not leave anything energised behind it).
      * Stop the amber fast blink. fast_blink_LED() runs on app.c's
        LED_200ms_timer and there is no stop function for it: add one in
        app.c (mirror auto_level_stop_blink_LED(), but for LED_200ms_timer
        and LED_timer_timeout), declare it in app.h. Then set the amber LED
        to match the real under-bed lights state (app_lights_on()). Turn
        red/green/blue off.
      * Clear the panel's bookkeeping: the entry press set
        switch_held = SWITCH_BED_LIGHT_VALUE in normal mode, and its release
        was handled by the settings branch, so switch_held is stale for the
        whole of settings mode (VERIFY this from process_inputs(); report).
        Without clearing it every button is dead after exit. Clear
        switch_held and switch_driving (clear_switch_panel_state() is
        static in switch_input.c - add a small exported function there for
        this, rather than exposing the variables), set active_switch = OFF,
        and call bed_note_source_released(BED_SOURCE_SWITCHES).
  - Put the exit in one function (e.g. settings_mode_exit()) so part D and
    anything later can call it.
  - Implement the trigger in settings_mode_process_outputs()'s
    SWITCH_BED_LIGHTS case (press branch). Explain in the handoff why the
    entry press's release cannot trigger it, citing the code path.
  - Check the Bed Status settings-mode flag (ble_bed_service.c:81, from
    in_settings_mode) updates on exit with no further change; report.

======================================================================
D. Pairing window: new bonds only while settings mode is active
======================================================================
This is docs/BLE_GATT_DESIGN.md section 8's "proposed DECISIONS #19", now
decided by the user.

The mechanism exists: SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED is
set (ble_bed_service.c ~874-881), so the stack raises
sl_bt_evt_sm_confirm_bonding, and the handler (~line 1000) currently calls
sl_bt_sm_bonding_confirm(conn, 1) for every request.

User decisions - implement, do not re-open:
  1. Only NEW bonds are gated. An already-bonded phone reconnects freely
     at any time (a resumed bond does not raise confirm_bonding - confirm
     that from the existing comments/code and report).
  2. Re-pairing a known phone is gated too: a bond-replacement request
     (bonding_handle != SL_BT_INVALID_BONDING_HANDLE) goes through the same
     window. This changes the DECISIONS #28 auto-replace path from
     automatic to conditional - say so in the DECISIONS entry.
  3. The window is "while settings mode is active" - no timer. It closes
     on the part C exit and on reset.
  4. Outside the window, reject with sl_bt_sm_bonding_confirm(conn, 0) so
     the phone gets a clean pairing failure rather than a hang.

Change: confirm = app_in_settings_mode() ? 1 : 0. Replace the
"auto-confirming" print with one that always prints (keep it under
BLE_DEBUG_SECURITY_LOG, currently 1 at ble_bed_service.c:52):
  "BLE SM: confirm_bonding conn=%u (%s) -> ACCEPTED (settings mode)"  or
  "... -> REJECTED (not in settings mode)", where %s is "new bond" or
  "REPLACING existing bond" as today. Update the stale comments at
  ~874-895 that say the pairing-window gate is a later change.

Record it in DECISIONS.md. If number 19 is already used, use the next free
number and say it implements BLE_GATT_DESIGN.md section 8's "proposed #19".
Update BLE_GATT_DESIGN.md section 8 to drop "proposed" and point to it.

Answer in the handoff (do not implement): what the 8-bond-full case should
do now that new bonds need physical access; whether settings mode's amber
fast blink is a sufficient "window open" signal; and anything in the
settings-mode flow that assumed bonding could not happen during it.

======================================================================
E. For the reviewer's bench steps
======================================================================
In the handoff, list the exact PuTTY lines expected for:
  1. a single press and release (debounce now 2);
  2. entering settings mode (BED LIGHTS held ~6 s), then exiting with one
     BED LIGHTS press - including what the LEDs and the lights do;
  3. after exit: one press of each button works normally;
  4. pairing: (a) bonded phone, not in settings mode, reconnects;
     (b) phone with its bond deleted, not in settings mode - rejected;
     (c) same phone in settings mode - accepted; (d) after exiting
     settings mode the phone reconnects.
