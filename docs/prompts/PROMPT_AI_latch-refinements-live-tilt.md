# Prompt AI - latch refinements, auto-level "settling" bit, live idle tilt, self-clearing LEVELED

Written by the reviewer on 2026-11-03. Implement as specified. Where the code
contradicts this document, stop and report it under QUESTIONS FOR REVIEW
instead of improvising. Follow CLAUDE.md for the handoff file, the session
log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
BLE_BENCH_WIPE_BONDS stay 0. Nothing added may block in a Bluetooth stack
callback (DECISIONS #27 rule 1); every accelerometer read stays in main-loop
context and within #27 rule 2 / #30's bus-load limits. config/ and autogen/
are not hand-edited (no GATT change is needed).

Expected repo state: HEAD cd2a148 (Prompt AH handoff), level with
origin/main. Uncommitted: .projectlinkstore (never commit it). This prompt
file is untracked; commit it with the work. If HEAD or origin/main differ,
or anything else is uncommitted or untracked, stop and report.

Read first: the AH handoff (fw_HANDOFF_2026-11-03_01) sections b.A, b.C and
f; DECISIONS #27, #29, #30, #31, #33, #46, #48, #49; PRE_SHIP F11, F15, F16.

One commit per part (A-E), building after each.

======================================================================
A. Latch refinements (reviewer rulings on AH f.1-f.3) + reset cause
======================================================================
A1. (f.1) Collision wins. In the start refusal, test COLLISION first: if
    the latch is COLLISION or any_collision is set, answer 0x06 and set the
    latch to COLLISION (upgrading STOP); otherwise (latch STOP) answer 0x07.
    Confirm latch_app_starts() permits the STOP -> COLLISION upgrade and
    never the reverse.
A2. (f.2) Call service_app_commanding() once more at the top of
    ble_bed_service_tick(), before process_pending_command(), so "300 ms
    since the app's last start" is judged when the next command is
    processed. Keep the existing call. State every behaviour this changes,
    including for #36 and the collision latch.
A3. (f.3) STOP_ALL sets the STOP latch only when the app is commanding at
    that moment (app_commanding true - a button held, or released less than
    300 ms ago) or a latch is already set. A STOP_ALL with the app idle
    stops the bed exactly as before but sets no latch, so the user's next
    press is a normal start. This applies to both roles. Restate AH A6.3
    and A6.5 (and any other A6 case that changes) for the new rule; the
    print `BLE: STOP_ALL latch set` appears only when it latches (print
    `BLE: STOP_ALL (no app hold, no latch)` otherwise, same flag).
A4. (f.6) Print the reset cause once at boot, after the banner (main-loop
    context, before Bluetooth starts), using the SDK's reset-cause API for
    the EFR32BG22 (find it; cite the header), then clear the cause register
    if the API requires it. One line, e.g. `Reset cause: 0x%08lX (<decoded
    names>)`. Unconditional is fine (add it to PRE_SHIP F24's print review).

======================================================================
B. Bed Status flag bit 5 = AUTO_LEVEL_SETTLING (AH C4)
======================================================================
B1. Bit 5 (0x20) is 1 while an auto-level run (either source) is in its
    Step 1 settling read - `auto_level_active && first_check`, or the
    precise equivalent you find - and 0 otherwise. A change notifies at once
    (byte 0 is already compared by status_flags_changed()).
B2. Document in BLE_GATT_DESIGN.md: section 6 (bit 5) and section 4
    (AUTO_LEVEL_START: first motion comes at least 1.5 s after the first
    accepted start - STEADY_ACCEL_READ_TIMER_TIME - plus 0.35 s if the first
    axis starts within the pulse band; none if both axes are within
    0.1 deg). The app will fill "Hold to level" over 1.5 s and snap to full
    when bit 5 clears. Additive within v1; no Protocol Version change.
B3. Behind BED_DEBUG_LEVELED_LOG (or a new flag that defaults to 1 and is
    listed in PRE_SHIP): one line per change of bit 5.

======================================================================
C. Live idle tilt (AH C2, Option B as recommended)
======================================================================
C1. While at least one connection is subscribed to Bed Status, no motor is
    energised, auto-level is not running and settings mode is not active:
    sample the accelerometer at 10 Hz. A periodic sleeptimer callback only
    sets a flag; tilt_telemetry_tick() in the main loop does one read_accel()
    per flag. Respect ACCEL_I2C_MIN_INTERVAL_MS. State the bus occupancy.
C2. Filter the idle samples with a new millisecond-defined filter: an
    exponential average with time constant TILT_IDLE_TAU_MS = 500 (alpha
    from the measured dt), on its own state, producing the same internal
    sign convention and baseline-relative value as the existing chains, so
    pitch is still negated only once at the wire. Seed it from the most
    recent value of the existing telemetry chain (or the first sample) when
    the idle window opens, so the shown angle does not jump at the end of a
    move or burst. The moving and auto-level chains are unchanged.
C3. While idle and subscribed, notify Bed Status at 5 Hz (moving cadence
    unchanged at 10 Hz). Ship the idle filter's value on offsets 4-7 in that
    state.
C4. tilt_fresh (bit 3) = 1 while the last idle sample is younger than
    300 ms or the existing per-pass chain is running; 0 otherwise (PRE_SHIP
    F16). State exactly when it is 0.
C5. Sleep: the CPU may sleep between samples; no new EM0 veto for the idle
    sampler. State what wakes it and the expected duty cycle. Add to
    PRE_SHIP F11 that the idle current must be measured with and without a
    phone subscribed, and a new row: a phone that stays subscribed with the
    app in the background keeps the 10 Hz sampler running (the app will
    unsubscribe when backgrounded - app prompt; the firmware needs nothing
    now).
C6. Bench diagnostics behind a new flag TILT_IDLE_DIAG (default 1, listed
    in PRE_SHIP): every 10 s while the idle sampler runs, one line with the
    sample count and the min/max/peak-to-peak of the filtered roll and
    pitch over those 10 s (degrees x 100). This measures the noise at rest
    for D's band.
C7. DECISIONS entry: the idle chain, why a time-based filter (and that it
    partly reverses #33's "same filter in both chains" for the idle case
    only), the stand-down rules, the risks from AH C2 and how the bench
    checks them.

======================================================================
D. LEVELED (bit 4) clears itself (AH C3)
======================================================================
D1. Clear bit 4 through app_set_leveled(false) when the idle filtered tilt
    shows |roll| or |pitch| above LEVELED_EXIT_BAND (0.3 deg, scaled 30)
    continuously for LEVELED_EXIT_HOLD_MS (2000), evaluated only after the
    idle filter has run at least 3 x TILT_IDLE_TAU_MS since the window
    opened. Named constants, both to be tuned on the bench from C6's
    numbers.
D2. Bit 4 is never SET from tilt; it still sets only at an auto-level
    success (#46). No IN_LEVEL_BAND bit.
D3. Update BLE_GATT_DESIGN.md section 6 (bit 4's "cleared when" list) and
    DECISIONS #46 with an amendment paragraph.

======================================================================
E. Docs, rulings, bench lines
======================================================================
E1. REGRESSION.md: items for A1-A4, B, C (live display at rest, after a
    move, after auto-level; BLE link stays up; 10 s noise lines) and D
    (tilt the bed by hand off level with a phone connected: LEVELED -> 0
    after ~2 s; nothing changes for a brief bump).
E2. Rulings to record in the SESSION_LOG entry (AH QUESTIONS): f.1 collision
    wins (A1); f.2 fixed (A2); f.3 latch only when the app is commanding
    (A3); f.4 accepted - the app prompt will stop repeating after STOP; f.5
    keep the suffix; f.6 line 287 was Andrew's deliberate power cut (bench
    step 13), reset-cause print added (A4); f.7 accepted (six auto-levels;
    the location row stays in PRE_SHIP until Phase 1b has a home); f.8 B, D
    and B-phase-bit as specified here; f.9 open - RIGHT-raised not yet
    checked, the next bench does it.
E3. Build from the command line with every new flag at 1 and at 0 (put
    them back), BLE_DEBUG_WATCHDOG_LOG at 1 and 0; raw tails, warnings,
    size change; host test.
E4. In the handoff, the exact console lines expected for: boot (reset
    cause); app STOP while holding and STOP with nothing held; an app
    auto-level (bit 5 on, off, LEVELED -> 1); the bed at rest for 30 s with
    the app open (the 10 s noise lines); tilting the bed by hand after a
    successful level (LEVELED -> 0 after ~2 s).
E5. Handoff section d: only this session's real command output; do not
    paste other files into it. Before committing, run git status and commit
    only the files this prompt changes, the handoff, docs/SESSION_LOG.md and
    this prompt file.
