# PRE_SHIP.md

Everything that must be done or tested before the Tilt Bed controller and its
app ship. Created 2026-11-01 (Prompt AC). Before this file the items lived in
per-item notes (source comments, DECISIONS entries, handoff QUESTIONS and the
SESSION_LOG); the Prompt AA handoff (QUESTIONS 1) asked where the list should
live, and the reviewer answered with this file.

**Rules.** One row per item. Do not delete a row: when an item is closed, put
`CLOSED <date>` and the evidence (commit SHA, bench log and line numbers) in
the "How it is closed" cell. New items are added at the bottom of their group
with their source. The app repo has no list of its own, so app items are kept
here too.

**Owners.** Andrew = bench, hardware, production and business work.
Claude = the reviewer (decisions and prompts). Claude Code = code and doc
changes in a repo.

File:line references are to the firmware at `6796e6b` unless marked.

## Firmware

| # | Item | Why (one line) | Owner | How it is closed | Source |
|---|---|---|---|---|---|
| F1 | `SWITCH_SAMPLER_DIAG` 1 -> 0 (`switch_input.c:57`) | Bench prints on every switch edge and read failure (`SW: ...`) | Claude Code | Change to 0, build, check the `SW:` lines are gone | DECISIONS #41 (Status); `switch_input.c:57` comment "pre-ship: 0" |
| F2 | `BLE_DEBUG_WATCHDOG_LOG` 1 -> 0 (`ble_bed_service.h:36`) | Bench diagnostics: per-pass timing, the `BLE: hold end` line and the WATCHDOG STOP block | Claude Code | Change to 0, build (it built clean at 0 in Prompt AA) | Prompt Y handoff (2026-10-23); `ble_bed_service.h:29-36` "Set to 0 before shipping" |
| F3 | `BLE_DEBUG_SECURITY_LOG` 1 -> decide (`ble_bed_service.c:53`) | Bonding/connection prints; the comment says "Set to 0 for a quiet production log" | Claude decides, Claude Code changes | Decision recorded, then change or keep | `ble_bed_service.c:43-53` |
| F4 | Confirm the bench-only flags are 0 in the shipped build: `BLE_BENCH_WIPE_BONDS` (`ble_bed_service.c:70`), `BED_BENCH_PROVOKE_RUNAWAY` (`switch_input.c:98`), `BED_CAPTURE_MOTOR_CURRENTS` (`debug_capture.h:21`) | Each must never ship as 1 (bonds wiped every boot / a bed that moves by itself / CSV-only console) | Claude Code | Grep of the release commit shows all three at 0 | Comments at each define; standing rule in every prompt |
| F5 | PWM fixed at 100% duty, started at boot "only for initial testing, then remove" (`app.c:416-427`) | The comment says testing-only, but motion is gated by the expander enable bits and relies on PWM always running (`exp_board.c:302-309`) | Claude decides, Claude Code changes | Decision (keep 100% and fix the comment, or change the drive scheme) then REGRESSION §2 and §5 | `app.c:423`; DECISIONS #9 (speed control deferred); Prompt AC |
| F6 | Motor-current supervision runs during the BED LIGHTS hold | `app_lights_set()` sets `active_switch` (`bed_actions.c:199`), so `run_motor_supervision()` runs with no motor on (`switch_input.c:921-926`) and prints `Max Current` lines | Claude decides, Claude Code changes | Gate it, or accept it with a recorded reason; then REGRESSION §7-§8 | Prompt W handoff (2026-10-18_02) QUESTIONS 7 |
| F7 | Stack allows 4 connections, firmware tracks 2 | `SL_BT_CONFIG_MAX_CONNECTIONS (4)` (`config/sl_bluetooth_connection_config.h:38`) vs `open_connections[2]` (`ble_bed_service.c:192`); a third connection is silently untracked (`:351-359`) | Claude decides; configurator change by Andrew or code by Claude Code | Set 2 in the configurator (BLE_GATT_DESIGN §2 says 2) or handle a third; test with three phones | Prompt W handoff (2026-10-18_02) QUESTIONS 8 |
| F8 | Pairing window has no timeout | A forgotten settings mode leaves new bonding open until a BED LIGHTS press or a reset | Claude / Andrew (re-opens user decision #42.3) | Decision: keep "no timer" with a recorded reason, or add a timeout | DECISIONS #42 decision 3; Prompt V handoff (2026-10-18_01) QUESTIONS 5 |
| F9 | I2CSPM blocking timeout is 300 s | SDK 2026.6.0 passes `SL_I2CSPM_TRANSFER_TIMEOUT` 300000 as milliseconds; a hung bus could block one main-loop pass for up to 300 s, with the watchdog and fail-safe unable to run | Claude (own prompt), Claude Code | Own prompt: a bounded timeout without hand-editing the SDK, then a bench check | Prompt Y handoff QUESTIONS 5; reviewer ruling 5 (SESSION_LOG 2026-11-01) |
| F10 | Behaviour with the Switch board unplugged, and per-board read failures | Designed but never bench-run; a board that fails while the other answers is never marked absent | Andrew (bench) | Bench: unplug each UI board at idle and during a hold; record what moves and what prints | Sampler handoff (2026-10-17_05) QUESTIONS 4 and 6 |
| F11 | Sleep clock: LFRCO in default precision; EM2 likely never reached. **Test before shipping (Andrew).** | The SDK reports the LFRCO as 0xFFFF ppm and the BLE controller then holds the device in EM1 (`sl_btctrl_init.c:289-298`), so idle current is higher than EM2 and the radio's sleep-clock timing is unexplained | Andrew (bench); configurator change by Andrew | 1) Measure idle current, connected and not. 2) Set LFRCO high precision in the configurator; repeat 6.9 **with** DECISIONS #45 and **without** it (temporary build); measure idle current again | Prompt AA handoff part C (C1-C4) and QUESTIONS 3-4; DECISIONS #45 |
| F12 | Dead-man watchdog margin at a realistic distance | The 2026-11-01 pass (longest refresh gap 119-181 ms of 300 ms) was a desk bed at desk range | Andrew (bench) | Repeat REGRESSION 6.9 (10 app auto-level holds) in the van at a realistic phone distance, `BLE_DEBUG_WATCHDOG_LOG` 1; read "longest refresh gap" on every hold-end line | DECISIONS #45 bench result; Prompt AC |
| F13 | Full REGRESSION.md pass on current firmware | No full pass since 2026-07-19 (`af369f2`); Stage C, the sampler, the pairing window and #45 have landed since | Andrew (bench) | Full pass recorded in a new REGRESSION_RECORD file | MIGRATION_STATUS Phase 6 |
| F14 | Release-config confirmation pass | Every recorded pass was a Debug build | Andrew (bench) | Release build, then the pass (or the subset the reviewer names) | REGRESSION_RECORD_PRE_STAGEC.md:38; MIGRATION_STATUS Phase 6 |
| F15 | DECISIONS #29 timing constants review | Auto-level's filter (`NUM_ACCEL_READINGS`, `accel_alpha`) and the collision path (`NUM_CURRENT_READINGS`, `NUM_INITIAL_CURRENTS_IGNORED`, `NUM_CURRENTS_READ_DYNAMIC_THRESHOLD`, `alpha`) count loop passes, not milliseconds (DECISIONS #2) | Claude (design), Claude Code | Design prompt; change; REGRESSION §5 and §6 with a loop-rate measurement | DECISIONS #29 |
| F16 | `tilt_fresh` (Stage B5) | Bed Status flag bit 3 is always 0 (`ble_bed_service.c:1057`), so the app cannot tell a fresh angle from a stale one | Claude decides | Implement, or ship without it with the app treating tilt as possibly stale | DECISIONS #17, #33; BLE_GATT_DESIGN §6 |
| F17 | Gecko bootloader on every board before it ships | A board without the bootloader can never be updated over the air. Note: the application is linked at flash address 0x0 (`autogen/linkerfile.ld:32`), so the app's flash layout must change with it (configurator). **Extended 2026-11-02 (reviewer ruling on Prompt AC QUESTIONS 7): the bootloader and the flash layout need their own design** - where the bootloader sits, where the app is relinked, whether OTA stages a second image (the image is ~240 KiB with ~229 KiB free below NVM3, not the ~53 KB BLE_GATT_DESIGN §10 assumed) | Claude (plan, own design prompt), Andrew (configurator, flashing) | Design prompt and its record first; then bootloader project built; app relinked; each board flashed and checked | BLE_GATT_DESIGN §10; Prompt AC handoff B9, QUESTIONS 7; Prompt AE part E |
| F18 | "Connection stalled" shown when the physical controller takes over from the app | The app's refused repeats do not refresh the watchdog, so ~300 ms later the app gets WATCHDOG_STOP although the link is fine (`ble_bed_service.c:746-750`) | Claude decides; Claude Code (firmware or app) | A distinct result code, or app handling of FORCE_STOPPED then WATCHDOG_STOP; bench check | Prompt AA handoff QUESTIONS 5 |
| F19 | `BED_DEBUG_LEVELED_LOG` 1 -> 0 (`bed_actions.c`) | Bench print `LEVELED (Bed Status bit 4) -> 0/1` on every change of bit 4 | Claude Code | Change to 0, build, check the `LEVELED` lines are gone | Prompt AE part F; DECISIONS #46 |
| F20 | Collision detection is blind for the first ~450 supervision passes of every move | 100 passes ignored + 350 calibrating (`motor_current_functions.c:28-29`) before any collision or max-load trip can fire; roughly 0.7-1.0 s at auto-level's loop rate, never measured for manual moves | Claude (design), Claude Code | Design with F15 (pass counts -> ms, DECISIONS #2): state the blind time in ms and what covers it; change; REGRESSION §5 with a loop-rate measurement | Prompt AC handoff B1(a); reviewer ruling, Prompt AE part E |
| F21 | Auto-level's pulsed ("sneak") phase has no current supervision | Once `Latch` prints, `level_axis_pulse()` runs with no collision, max-load or limit inference (`auto_level.c:679-738`, `:770-771`; `switch_input.c:921`) | Claude (design), Claude Code | Design: supervise the pulse phase (or record why it is safe); change; REGRESSION §5 collision during auto-level near level | Prompt AC handoff B1(d), QUESTIONS 3; reviewer ruling, Prompt AE part E |
| F22 | The "continuous" re-baseline never reaches the trip test | It writes `dynamic_collision_current_threshold[]`, which nothing reads; the trip uses `max_current_threshold_set[]`; and it counts on motor 3's counter for every motor (`motor_current_functions.c:206-231`) | Claude (design), Claude Code | Decide: make it work (with F15/F20) or remove it; change; REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 4; reviewer ruling, Prompt AE part E |
| F23 | Auto-level uses the raise margin for the lowering motors | `is_raising` is forced true for auto-level (`bed_actions.c:170`; `auto_level.c:771`), so the two lowering motors are judged against the raise margin | Claude decides, Claude Code changes | Decision (keep with a recorded reason, or per-direction margins in auto-level); then REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 5; reviewer ruling, Prompt AE part E |
| F24 | Unconditional console prints to review before release | With every flag at 0 the firmware still prints `BLE SM: configure/...` (`ble_bed_service.c:1089, 1101, 1109`), `connection_parameters` (`:1270`), `Max Current for Motor` (`motor_current_functions.c:198`), `Once`, and the auto-level step lines | Claude decides, Claude Code changes | List each print with keep / flag / remove; change; check a release-build log | Prompt AC handoff QUESTIONS 12; reviewer ruling, Prompt AE part E |
| F25 | Hold 9 (2026-11-01 bench): the HEAD pair apparently did not run during PITCH levelling | `prompt_aa_bench.log` lines 201-204 show outputs 0 and 3 at 1 and 12 mA while FOOT ran; cause not established (an inferred limit refusing the pair, `dual_motor_control.c:353, 361`, is one candidate) | Andrew + Claude | A bench look with the limit prints enabled: repeat PITCH levelling from a HEAD-high start and record which pairs run and any limit lines | Prompt AC handoff B1(d), QUESTIONS 6; reviewer ruling, Prompt AE part E |
| F26 | The runaway fail-safe cuts the collision reverse to ~150-200 ms after an app collision, and after a physical collision once the button is released | During the reverse both app motion flags are false and no switch is down, so `motion_failsafe_tick()` de-energises it after 3 samples of 50 ms (`switch_input.c:804-896`) and prints `FAILSAFE: ...`; the reverse is meant to relieve pressure on the obstruction (ASSERTED from code, not seen) | Andrew + Claude | First: observe the `FAILSAFE` line and the length of the reverse on the bench (REGRESSION 5.A1); then a design ruling (accept, or e.g. the fail-safe stands down while `any_collision` is set) | Prompt AF handoff b.11.1, QUESTIONS f.3; Prompt AG C4 |
| F27 | A BED LIGHTS press stops an app move but does not latch the #36 inhibit | Lights are not motion and never latch (`bed_control.c:153-164`), so the app's next repeat can restart the move briefly while BED LIGHTS is held | Claude | First: the bench record REGRESSION 8.d; then a ruling | Prompt AE handoff QUESTIONS 6; Prompt AG C4 |
| F28 | A collision flagged during a BED LIGHTS hold toggles `bedlights_on` at the release without writing the output | Not expected to happen (no motor runs during a BED LIGHTS hold), but the flag and the amber/lighting output would then disagree | Claude | Review before release | Prompt AE handoff QUESTIONS 8; Prompt AG C4 |

## App

The app repository has no list of its own; these are recorded here.

| # | Item | Why (one line) | Owner | How it is closed | Source |
|---|---|---|---|---|---|
| A1 | Hide the debug menu in release builds | Bench tools must not reach customers | Claude Code (app) | Release build shows no debug menu | Chat 2026-11-01 (Prompt AC); APP_KICKOFF_PROMPT.md:52 |
| A2 | Upgrade `flutter_reactive_ble` once safe | Kotlin Gradle Plugin warning | Claude Code (app) | Upgrade, Android build clean, BLE smoke test | Chat 2026-11-01 (Prompt AC) |
| A3 | Connection-state error path not classified for pairing refusal | A refused pairing (bed not in settings mode) is not shown as such; the close code differs by phone (0x1016 iPhone, 0x1022 Galaxy S23, DECISIONS #43) | Claude Code (app) | Refused pairing shows a "put the bed in settings mode" message on both phones | Chat 2026-11-01 (Prompt AC) |
| A4 | Async write failures invisible to hold-to-run | A failed Command write during a hold is not surfaced | Claude Code (app) | Failed writes reported to the hold logic; bench check | Chat 2026-11-01 (Prompt AC) |
| A5 | Reconnect speed | Slow reconnect after the link drops | Claude Code (app) | Measured before/after | Chat 2026-11-01 (Prompt AC) |
| A6 | Dead `0x0005` subscription | The app subscribes to Settings RPC `54b10005`, which is not in the bed's GATT database (`config/btconf/gatt_configuration.btconf` has 0001-0004 and 0006 only) | Claude Code (app) | Remove it, or keep it behind the protocol version until 0005 exists | Chat 2026-11-01 (Prompt AC); Prompt AC handoff B4 |
| A7 | Scan-throttle backoff | Android throttles frequent scans | Claude Code (app) | Backoff implemented; repeated scan test | Chat 2026-11-01 (Prompt AC) |
| A8 | "Connection stalled" on a physical takeover | Same as F18 if fixed in the app | Claude Code (app) | See F18 | Prompt AA handoff QUESTIONS 5 |

## Hardware / production

| # | Item | Why (one line) | Owner | How it is closed | Source |
|---|---|---|---|---|---|
| H1 | Flash the Gecko bootloader on every board in production | Wired programming is the only way to add it; see F17 | Andrew | Production step written down; every board checked | BLE_GATT_DESIGN §10 |
| H2 | GBL signing / encryption keys | Field units should accept only authentic firmware; keys generated once and kept out of git | Andrew | Keys generated and stored; signed image accepted, unsigned refused | BLE_GATT_DESIGN §10 |
| H3 | Is a 32.768 kHz crystal fitted on the BGM220S module? | Decides whether LFXO is an option for F11; the SDK defaults suggest the S module has none | Andrew | Module datasheet read; answer recorded in HARDWARE_CONFIG.md | Prompt AA handoff C1 |

## Business / legal

| # | Item | Why (one line) | Owner | How it is closed | Source |
|---|---|---|---|---|---|
| L1 | Averta font licence | The web-font licence excludes mobile apps | Andrew | Buy an app licence, or replace the font in the app | Chat 2026-11-01 (Prompt AC) |
| L2 | Confirm the app's name in App Store Connect | The listed name must match the product | Andrew | Name confirmed in App Store Connect | Chat 2026-11-01 (Prompt AC); APP_SETUP_GUIDE.md:108 |

## Also recorded in the repo - not yet marked pre-ship (reviewer to decide)

Found while compiling this list. Listed so they are not lost; none is a
pre-ship item until the reviewer says so.

| Item | Source |
|---|---|
| No reverse after a collision during auto-level | REGRESSION §5 note; REFACTOR_PLAN P1.5 |
| Motor-fault reporting is an interim 200 ms poll; the expander's latch registers are unconfigured and the fault interrupt is not wired | `exp_board.c:342-348`, `:515-518` |
| The lower-collision "OFF" rung (750) is not actually off | SETPOINTS_INVENTORY.md Note A |
| Auto-level's pulsed ("sneak") phase runs with no current supervision. **Now row F21 (2026-11-02)** | Prompt AC handoff B1(d) (`auto_level.c:679-738`, `:770-781`) |
| "Bed is level" never prints: `bed_level` is never set true. **Fixed 2026-11-02 (Prompt AE part B, DECISIONS #46); bench check REGRESSION 6.1 / 6.10** | Prompt AC handoff B8 (`auto_level.c:166,603`; `app.c:681`) |
