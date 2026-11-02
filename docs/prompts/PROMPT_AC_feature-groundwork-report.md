# Prompt AC - record the AA bench result, create PRE_SHIP.md, report on the code behind the next features

Written by the reviewer on 2026-11-01. Implement as specified. Where the code
contradicts this document, stop and report it under QUESTIONS FOR REVIEW
instead of improvising. Follow CLAUDE.md for the handoff file, the session
log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
BLE_BENCH_WIPE_BONDS stay 0. config/ and the SDK are not hand-edited.

**No source code changes in this prompt.** One docs commit (A), then a
report-only section (B) written into the handoff. Do not build unless you
need a build to answer a question (say so if you do).

Expected repo state: HEAD 6796e6b (Prompt AA handoff), level with
origin/main. Uncommitted: .projectlinkstore (never commit it). This prompt
file is untracked; commit it with commit A. If HEAD or origin/main differ,
stop and report before changing anything.

======================================================================
A. Docs (one commit, docs only)
======================================================================
A1. Record the Prompt AA bench result as VERIFIED (user's bench run,
    2026-11-01, Galaxy S23 SM-S911U, firmware 75b0a61). Log:
    C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\prompt_aa_bench.log
    Confirm each point from the log, with line numbers, before recording:
    - 11 app AUTO LEVEL holds (op=0x03 hold-end lines), no "BLE: WATCHDOG
      STOP" anywhere, exactly one "Step 1" per hold;
    - every one of the 22 "BLE: hold end" lines (op=0x01 and op=0x03)
      reads "longest between passes 0 ms";
    - longest refresh gap per hold ranges 119-181 ms (state min and max);
    - a physical-controller AUTO LEVEL (SW 0x0002, lines ~255-271) levelled
      and printed no hold-end line;
    - connection parameters: latency 0, timeout 500 then 100.
    The user also reports no "Connection stalled" in the app. Record under
    DECISIONS #45 (a "Bench result" subsection), tick REGRESSION.md 6.9,
    and add the SESSION_LOG entry. State that the mechanism remains
    unproven (EM1 sleep per the AA handoff C4); the fix is verified.
A2. Create docs/PRE_SHIP.md: every item that must be done or tested before
    shipping, one row each, with columns: item, why (one line), owner
    (Andrew / Claude / Claude Code), how it is closed (a test or a change),
    source (DECISIONS / handoff / chat date). Group: Firmware, App,
    Hardware/production, Business/legal. Include at least these, adding any
    others you find recorded in the repo (search for "pre-ship", "pre ship",
    "before shipping", "must be 0"):
    Firmware:
    - SWITCH_SAMPLER_DIAG, BLE_DEBUG_WATCHDOG_LOG and any other bench
      diagnostic flags set to 0 (list every such define with file:line);
    - PWM fixed at 100% ("only for initial testing");
    - motor-current supervision running during the BED LIGHTS hold;
    - stack allows 4 connections, firmware tracks 2;
    - pairing window has no timeout;
    - I2CSPM 300 s blocking timeout (Prompt Y QUESTIONS 5);
    - behaviour with the Switch board unplugged / per-board read failures;
    - sleep clock (LFRCO default precision, EM2 likely never reached):
      measure idle current, then try LFRCO high precision with and without
      DECISIONS #45 - marked "test before shipping (Andrew)";
    - dead-man watchdog margin: repeat the 10-hold app auto-level test in
      the van at a realistic distance and read "longest refresh gap";
    - full REGRESSION.md pass on current firmware, and the Release-config
      pass;
    - DECISIONS #29 timing constants review; tilt_fresh (Stage B5);
    - Gecko bootloader on every board before it ships (BLE_GATT_DESIGN.md
      section 10);
    - "Connection stalled" shown when the physical controller takes over
      from the app (Prompt AA QUESTIONS 5) - needs a distinct result or app
      handling.
    App (record them here too; the app repo has no such list):
    - hide the debug menu in release builds;
    - upgrade flutter_reactive_ble once safe (Kotlin Gradle Plugin warning);
    - connection-state error path not classified for pairing refusal;
    - async write failures invisible to hold-to-run;
    - reconnect speed; dead 0x0005 subscription; scan-throttle backoff.
    Business/legal:
    - the Averta font licence (web-font licence) excludes mobile apps -
      licence it for apps or use another font;
    - confirm the app's name in App Store Connect.
A3. Commit A. Show the PRE_SHIP.md file and the other diffs in the
    handoff.

======================================================================
B. Report only - change nothing
======================================================================
Answer each from the code, with file:line for every claim, marking each
VERIFIED (read in code or SDK) or ASSUMED. Keep each answer focused; the
reviewer will design from it. Where a question asks how something "could"
be done, give options and their costs, not a decision.

B1. Collision detection.
    (a) Exactly how a collision trip is decided today: which currents are
        measured (per motor? total?), sampling rate and method, how the
        baseline is formed, how the raise/lower sensitivity values enter
        the test, and where the trip is declared ("Tripped on steady!" and
        any other trip prints).
    (b) At the moment of a trip, which per-motor values exist in memory
        (current now, baseline, peak, history arrays) and for how long.
    (c) Could the firmware report, at trip time, each motor's share of the
        rise that caused the trip, without blocking and without new I2C?
        Options, size of the data, and where it could be sent (Bed Status,
        Command Result, a new characteristic).
    (d) For each move type (HEAD, FOOT, LEFT, RIGHT up/down, ALL up/down,
        auto-level), which motors carry current, so the reviewer knows
        what each move can sense.
B2. Level reference ("set level"). How the zero-level baseline is captured
    today (which button sequence or mode, the averaging used), what is
    stored in NVM3 (keys, units, the "Loaded Roll Angle / Pitch Angle"
    lines), and any limits on when it may run.
B3. Settings mode. Entry and exit paths; every setting adjustable today and
    how (collision sensitivity steps, anything else); how values are stored
    (bed_settings.c key table: list every key with min, max, default); what
    motion and commands are blocked in settings mode (cite the Command
    arbitration); exactly where the pairing window (DECISIONS #42) is tied
    to settings mode. Then options for an app-entered settings mode that
    allows settings changes but does NOT open the pairing window (the user
    has decided only a physically entered settings mode may accept new
    phones).
B4. Settings RPC (BLE_GATT_DESIGN.md section 9, characteristic 0005).
    Anything implemented? Is 0005 in the GATT database (gatt_configuration
    / autogen)? What the app's subscription to it does today.
B5. Lights. What LIGHTS_SET drives (which outputs), whether the state is
    reported in Bed Status (so the app's Lights button can show on/off),
    and any interaction with settings mode or the status LEDs.
B6. Motor outputs to bed corners. How motor outputs 0-3 map to HEAD,
    LEFT, RIGHT, FOOT today (the pair table for every move), and how
    auto-level chooses which pair to drive. The accelerometer's axis and
    sign conventions (roll, pitch, the pitch-sign decision), where each
    constant lives. List every call site a runtime corner-mapping table
    (stored in NVM3) would have to touch, and anything that assumes the
    board's orientation (logo toward the head).
B7. Timed moves. Is there any existing way to run one motor (or one
    output) for a fixed short time, with collision and fail-safe active?
    The smallest practical run time given the loop and PWM. Any actuator
    speed constant recorded in the repo (say if none).
B8. Auto-level outcomes. Every way an auto-level run ends (level reached,
    an axis disabled, collision, watchdog, user release, any timeout), the
    print for each, and whether any of these reach the app today (Bed
    Status command field, Command Result codes).
B9. Headroom. Current flash and RAM use (from the last build output or
    arm-none-eabi-size) against the BGM220SC22HNA limits, and the size of
    the NVM3 area.

In the handoff, put B1-B9 under a "b. Analysis" heading in that order.
