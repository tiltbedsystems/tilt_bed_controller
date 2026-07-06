# REFACTOR_PLAN.md

Deep read-only analysis of the totem_controller firmware, performed July 2026
against commit `7456774`. No code was modified. Line numbers refer to the files
as of that commit.

End goals this plan is designed to serve:

1. **BLE control from a phone app** (bed movement commands over GATT)
2. **App-adjustable setpoints** (collision sensitivity, level tolerances, etc.)
3. **Live telemetry** (actuator currents, level angle, motor/limit status)
4. **OTA firmware updates**
5. **Future position sensing over a serial bus**

---

## Part 1 — Structure map

### The super loop

`main.c` runs the standard Silicon Labs bare-metal super loop: `sl_system_init()`
→ `app_init()` → forever `{ sl_system_process_action(); app_process_action(); }`.
There is no RTOS. Everything is cooperative: every feature is a block of code
inside `app_process_action()` (app.c:1351) that checks flags set by interrupt
callbacks and acts on them. This matters for BLE later — the BLE stack will also
run from this loop and is sensitive to long blocking delays.

### app.c (~2010 lines) — what lives where

| Lines | Section |
|---|---|
| 40–75 | `#define`s: switch bit masks, collision sensitivity levels, timing constants |
| 81–105 | `motor_mode_t` enum (unused) and the `active_switch` enum |
| 111–226 | ~45 file-scope flags, counters, and 12 sleeptimer handles |
| 234–424 | 10 nearly identical timer callback + `start_x_timer()` pairs |
| 431–531 | LED helpers (all-on/off/toggle, slow blink, fast blink) |
| 534–546 | `sl_button_on_change()` — the GPIO interrupt entry point; just sets `read_ui_data` |
| 549–558 | `reset_expanders()` |
| 561–815 | `process_outputs()` — 11-case switch: LEDs + motor on/off per button |
| 818–1223 | `process_inputs()` — 11-case switch: direction, state, and timer setup per button |
| 1232–1342 | `app_init()` — IADC, expanders, PWM, accel, heartbeat, NVM3 load |
| 1351–2010 | `app_process_action()` — the tick, containing five sub-systems described below |

`app_process_action()` is effectively five state machines interleaved in one
~660-line function:

1. **Collision UX** (1368–1405): when `any_collision` is set by
   `motor_current_functions.c`, blink all LEDs, reverse the motors briefly at
   blink #2, then stop everything and clear the flag.
2. **Auto-level orchestration** (1411–1490): while `auto_level_active`, feed the
   accel running average, run the first-check/steady-read, then call
   `auto_level_single_axis()` per axis, switching ROLL↔PITCH with a 1 s pause.
3. **Settings mode** (1499–1837): hold BED_LIGHTS 5 s to enter; then
   AUTO_LEVEL-hold stores/erases the custom zero angle, UP_ALL/DOWN_ALL-hold
   steps collision sensitivity up/down and writes it to NVM3.
4. **Double-press error check** (1848–1922): while a motor button is held, run
   the whole current-supervision chain every tick, and every 50 ms re-read the
   switch board to catch a second simultaneous press.
5. **UI dispatch** (1976–2009): when the expander interrupt flag is set, read
   interrupt source + port status over I2C, derive pressed/released, and call
   `process_inputs()`.

### Module interaction (who calls whom, who owns what)

```
                    sl_button_on_change (GPIO IRQ)
                              │  sets read_ui_data
                              ▼
   app.c ──────► exp_ui.c    get_ui_interrupt()/get_ui_status()  [I2C, off-board bus]
     │                       set_LED_on/off()  (writes to BOTH boards blindly)
     │
     ├─────────► dual_motor_control.c   set_dual_motors_on/off/direction(side)
     │                │                 limit_switch_statuses(), temp-off pause
     │                └───► exp_board.c set_motor_on/off/direction  [I2C, on-board bus]
     │
     ├─────────► motor_current_functions.c  scan_all_currents(), collision_check(),
     │                │                     max_load_check(), dynamic thresholds
     │                └───► adc_currents.c  IADC scan + IRQ handler → mA values
     │
     ├─────────► auto_level.c   auto_level_single_axis(), accel_running_average()
     │                │         check_level(), level_axis(), level_axis_pulse()
     │                ├───► mc3479.c  accel_get_xyz_raw()  [I2C, on-board bus]
     │                ├───► dual_motor_control.c  (motor pairs per axis)
     │                └───► motor_current_functions.c  (its own supervision loop)
     │
     └─────────► nvm3_functions.c  storeValue()/readValue()/readAllUserInputs()
```

Cross-module communication is almost entirely **shared global booleans and
arrays** declared `extern` in headers: `any_collision`, `motor_state[]`,
`auto_level_motors_on`, `motors_temp_off`, `initial_switch_press`,
`leveling_success[]`, `axis_disabled[]`, `first_check`,
`raise/lower_collision_current_security_margin`, and more. There is no single
owner of "bed state" — app.c, auto_level.c, and dual_motor_control.c all write
`motor_state[]` and each other's flags.

Two supervision chains exist that must agree:

- **Manual moves**: app.c:1865–1886 runs `scan_all_currents()` →
  `check_motor_movement()` → per-motor `motor_current_running_average()`,
  `max_load_check()`, `set_dynamic_collosion_threshold()`,
  `limit_switch_statuses()` → `collision_check()`.
- **Auto-level moves**: auto_level.c:590–609 (`level_axis()`) runs the *same
  sequence*, duplicated, with `collision_check(true)` hardcoded.

Limit switches are **inferred, not wired**: `limit_switch_statuses()`
(dual_motor_control.c:231) decides a motor hit its limit when its filtered
current drops below `ON_CURRENT_THRESHOLD` (80 mA) while it is commanded on.
The actuators' internal limit switches cut the motor, current collapses, and
the firmware notices. This makes `ON_CURRENT_THRESHOLD` and the current
filtering chain safety-relevant, not just diagnostics.

---

## Part 2 — Problems found

### P1. Real defects / hazards (fix first)

1. **File-scope loop index `i` shared with an interrupt callback.**
   app.c:222 declares `static uint8_t i;` used by nearly every loop in app.c
   — and also by `callback_dynamic_threshold_change_timer()` (app.c:369),
   which runs in interrupt context. If that timer is ever (re)enabled, the
   callback can corrupt `i` mid-loop in the foreground code, scrambling motor
   indexing. Today the timer is never started (all call sites are commented
   out), so the hazard is latent — but it is one uncomment away from being a
   motor-control bug. Every loop should use its own local index.

2. **Partial array initializers that look like full initializers.**
   `bool motor_is_moving_stable[NUM_MOTORS] = {true};`
   (motor_current_functions.c:65) sets element 0 true and elements 1–3
   **false** — C initializes unlisted elements to zero. Same pattern with
   `first_print[10] = {true}` (app.c:223). The code happens to recover, but
   the stated intent ("start as moving") is only true for motor 0.

3. **Lower-collision default doesn't match any selectable level.**
   A fresh unit loads `lower_collision_current_security_margin = 150`
   (nvm3_functions.c:132/135), but the settings-mode ladder only recognizes
   250/250/300/750 (app.c:62–65). Additionally `LOWER_HIGH_SENSITIVITY` and
   `LOWER_MED_SENSITIVITY` are both 250 — the HIGH and MED lower-margin steps
   are indistinguishable, so one rung of the ladder does nothing for lowering.
   Both look like drift between nvm3_functions.c and app.c (the old values are
   still visible, commented out, at nvm3_functions.c:141–149). **This is a
   safety-tuning question only you can answer** — the refactor plan below
   schedules it as a decision, not a silent code change.

4. **Motor fault inputs are never read.** `get_motor_faults()`
   (exp_board.c:244) is a stub returning 0, and nothing calls it. The MP6522
   driver fault pins are wired to the on-board expander (P1_0–P1_3) but
   ignored. Your own Software Notes.txt lists this as a TODO. Worth elevating:
   it is the only independent hardware check on the motor drivers.

5. **Collision recovery ignores auto-level.**
   `reverse_motor_direction_and_start()` (dual_motor_control.c:303) has no
   case for `SWITCH_AUTO_LEVEL_VALUE` — the code comment itself says "NEED TO
   UPDATE BECAUSE AUTO LEVEL WONT WORK HERE". A collision during auto-level
   stops the motors (good) but never reverses to relieve pressure.

### P2. Duplication (the bulk of the refactor value)

6. **The `active_switch` enum is defined three times** — app.c:90,
   auto_level.c:55, dual_motor_control.c:64 — as anonymous enums that must be
   kept in sync by hand. A reorder in one file silently breaks motor/limit
   logic in another. `NUM_MOTORS` is likewise defined in two headers
   (dual_motor_control.h:11, motor_current_functions.h:11) and `SCALE_FACTOR`
   in two .c files (auto_level.c:32, motor_current_functions.c:26).
   motor_current_functions.c even has a third copy of the readings count
   (`SECOND_NUM_CURRENT_READINGS`, line 43, unused).

7. **`process_inputs()` is ~400 lines of an 11-case switch where every case is
   ~90 % identical.** Each press branch does: set `active_switch`, set
   `manual_raise`, set `initial_switch_press`, set a motor direction, reset
   dynamic-collision data, stop two timers. Each release branch does the same
   cleanup. The only *varying* data per button is: which side (HEAD/LEFT/
   RIGHT/FOOT/ALL), which direction, which LED. That is a 12-row lookup table,
   not 400 lines.

8. **`process_outputs()` repeats the same shape** — 11 cases that differ only
   in LED color and which motor-on call to make.

9. **The settings-mode sensitivity ladder is two ~100-line near-identical
   blocks** (increase: app.c:1608–1704; decrease: app.c:1706–1803), each
   containing a 4-way cascade of `if (raise == X) { if (lower == Y) {...} }`
   with duplicated LED-setting code. As a table of levels
   `{raise, lower, led_pattern}` plus an index, each block is ~10 lines — and
   the HIGH/MED duplication bug (P1.3) becomes impossible to reintroduce.

10. **The motor-supervision chain is duplicated** between app.c:1865–1886 and
    auto_level.c:590–609 (see structure map). This is the safety-critical
    sequence; two copies means a future fix can land in one and not the other.

11. **Timer boilerplate ×14.** Every sleeptimer gets its own callback function,
    start function, `_timeout` flag, and `is_running` bool — ~15 lines each,
    ~200 lines total, all identical in shape.

### P3. Unclear state handling / fragility

12. **No explicit top-level state machine.** The system's real states — IDLE,
    MANUAL_MOVE, AUTO_LEVEL, COLLISION_RECOVERY, SETTINGS — are encoded as
    combinations of ~10 booleans (`auto_level_active`, `any_collision`,
    `in_settings_mode`, `switch_pressed`, `active_switch != OFF`, …) tested in
    different orders in different places. Nothing prevents contradictory
    combinations. BLE will add a second command source, which multiplies the
    combinations; this is the single biggest architectural risk to goal #1.

13. **Settings mode has no exit.** `in_settings_mode` is set true
    (app.c:1507) and never set false — the only exit is a power cycle. If
    intentional, it deserves a comment; if not, it's a missing transition.

14. **Multi-bit interrupt masks silently match nothing.** `process_inputs()`
    switches on the raw packed interrupt word; if two switches change in the
    same latch window the value matches no `case` and is dropped. The 50 ms
    double-press timer papers over one instance of this. A decode loop over
    set bits would handle it deterministically.

15. **`get_ui_status()` is deliberately called twice back-to-back**
    (app.c:1895–1896 and 1988–1989) — apparently first-read-clears-latch,
    second-read-gets-truth, but it's uncommented and each call is a full I2C
    transaction with a 2 ms re-driver power delay. Needs a comment or a
    `read_ui_status_fresh()` wrapper so nobody "optimizes" it away.

16. **Blocking delays inside the tick.** `scan_all_currents()` blocks 1 ms per
    call and runs every tick while a button is held; every UI I2C transaction
    blocks 2 ms for the re-driver load switch (exp_ui.c:82). Fine today;
    hostile to a BLE stack that wants the loop serviced promptly. (Also note
    `sl_sleeptimer_delay_millisecond` busy-waits at EM1 — power draw.)

17. **Fragile hand-edited generated files.** app.c:1253–1280 documents that
    `config/sl_pwm_init_motor1/2/3_config.h` contain **hand-corrected pin
    mappings that the Simplicity Studio configurator will overwrite** if the
    PWM components or pin tool are touched. Adding BLE components (goal #1/#4)
    will regenerate config — this warning must be front-of-mind then. Consider
    committing a `docs/pwm_config_expected.md` with the correct values so any
    regeneration can be diffed and re-fixed quickly.

18. **Large amounts of commented-out code** (release-check block app.c:1928–
    1961, slope-check, testing timers, ChatGPT experiments like
    `get_ui_live_inputs()` which has no live caller) obscure what actually
    runs. Several `#define`s are dead: `NUM_STEADY_ACCEL_READINGS`,
    `CONTINUOUS_LEVEL_START_TOLERANCE` (computed into a variable never read),
    `NUM_ACCEL_READINGS_IGNORED`, `LOWER_DELTA_MULTIPLIER`,
    `NUM_CURRENTS_READ_FAST_DYNAMIC_THRESHOLD`, `NUM_MOTOR_MOVEMENT_READINGS`,
    `DYNAMIC_THRESHOLD_TIME_INTERVAL` (only used by disabled code). Git
    already preserves history; the dead code can go.

19. **Minor**: `read_motor_movement()`'s overflow guard
    (motor_current_functions.c:371) is unreachable (`> 2` already caught);
    `exp_ui.c:32` uses `extern` with an initializer (non-standard); the
    `index` global in auto_level.c shadows a libc symbol; typo'd identifiers
    (`set_dynamic_collosion_threshold`, `CONITNUOUS_…`) invite call-site typos.

---

## Part 3 — Step-by-step refactor plan

Rules applied to every step: **small diff, project still builds in Simplicity
Studio, behavior verifiable on hardware before the next step.** Steps are
ordered highest-value-per-risk first. Each step is one commit. Suggested
bench check is listed with each step; a full regression pass (every button,
auto-level both axes, one deliberate collision, settings mode in/out of NVM3)
is worth doing at the phase boundaries rather than every step.

### Phase A — zero-behavior-change cleanups (low risk, immediate payoff)

**A1. Delete dead code and dead `#define`s.** Remove the commented-out blocks
and unused constants listed in P3.18 (reviewing each one together first — some
may be experiments you still want). No runtime change is possible from
deleting comments and unused macros.
*Verify: clean build, byte-identical behavior; one button + one auto-level as a smoke test.*
*Done July 2026, 3 commits. Scope notes: commented one-line debug printfs kept as bench instrumentation until the data-capture tool (DECISIONS.md #5) replaces them; the `release_input_check_timer` machinery in app.c deferred to B1/B2 (its ~22 no-op stop calls are woven through every `process_inputs()` case); the cable-stretch machinery (`raise_dynamic_threshold` + second-average state) deleted per DECISIONS.md #1, which also removed the P1.1 shared-`i` ISR callback.*

**A2. Localize the shared loop index `i`.** Replace every use of the
file-scope `i` (app.c:222) with a loop-local `uint8_t i`, including inside the
timer callback. Delete the file-scope declaration.
*Verify: build; all-up, all-down, single-side moves; auto-level. (Behavior should be identical — this closes a latent trap.)*

**A3. Fix the partial array initializers.** Initialize
`motor_is_moving_stable[]` and `first_print[]` explicitly (loop in an init
function, or full initializer lists).
*Verify: first button press after power-up behaves the same as the second (the affected window is the first few ticks).*

**A4. Create one shared types header.** New file `bed_types.h` containing the
named `active_switch_t` enum, `NUM_MOTORS`, `NUM_AXES`, `SCALE_FACTOR`, and
the `side_of_bed_t`/`axis_t` enums. Delete the three duplicate enum
definitions and the duplicate macros; include the header everywhere. Change
`active_switch` variables from `int8_t` to the named enum type.
*Verify: build with zero warnings about redefinition; every button still maps to the correct motors (this is the step where a silent enum mismatch would surface — test all 11 buttons).*
*Done July 2026. User-verified: 0 build errors, full REGRESSION.md pass. Warning count went 5→6 rather than dropping as predicted — confirmed by diff that none of the 6 warnings are caused by this step; all are pre-existing dead code (`is_pulse_latch_timer_running`/`start_pulse_latch_oneshot_timer` in auto_level.c, `is_release_input_check_timer_on`/`start_release_input_check_timer`/`stop_fast_blink_LED`/`app_ui_status_switch_check` in app.c) that a clean rebuild of the touched files simply re-surfaced. Cleanup of these six folded into B1/B2 below, alongside the already-deferred `release_input_check_timer` machinery.*

**A5. Decide and reconcile the lower-sensitivity values (P1.3).** This is a
~5-line change *after* you decide: what should the four LOWER ladder steps be,
and should the fresh-unit default (currently 150) equal LOWER_HIGH? Put the
final values in one place (they'll move into the settings module in C1 anyway)
and make nvm3_functions.c defaults reference the same constants app.c uses.
*Verify: erase NVM3 (settings-mode 5 s hold), power cycle, confirm printed loaded values; step sensitivity up/down through all four rungs and confirm the LED pattern and a bench collision trip at each rung.*
*Done July 2026: `LOWER_MED_SENSITIVITY` changed from 250 to 275 (app.c) so it no longer duplicates `LOWER_HIGH_SENSITIVITY` — the two rungs are now distinct. The fresh-unit NVM3 default in `nvm3_functions.c` was changed from 150 to 250, matching `LOWER_HIGH_SENSITIVITY` — fresh units now boot at the safest (HIGH) lower-sensitivity rung. Ladder is now 250/275/300/750.*

### Phase B — collapse the duplication inside app.c

**Carried over from Phase A, to fold into B1/B2:** delete the
`release_input_check_timer` machinery (app.c) deferred at A1, plus the six
dead-code warnings noted at A4 — `is_pulse_latch_timer_running` /
`start_pulse_latch_oneshot_timer` (auto_level.c) and
`is_release_input_check_timer_on` / `start_release_input_check_timer` /
`stop_fast_blink_LED` / `app_ui_status_switch_check` (app.c). All are dead
code with no live caller; natural to remove alongside the `process_inputs()`
rewrite these steps already touch.

**B1. Extract press/release helper functions.** Factor the repeated blocks in
`process_inputs()` into two helpers, e.g. `motor_button_pressed(side_or_all,
up)` and `motor_button_released()` (stop timers, clear flags, reset current
data, zero `motor_state[]`). Convert **two cases** (say UP_HEAD, DOWN_HEAD)
to use them; leave the rest untouched.
*Verify: head up/down on hardware behaves identically to left/right/foot (which still use old code) — direct A/B comparison on the same build.*

**B2. Convert the remaining single-side cases** to the helpers, then the
ALL-UP/ALL-DOWN cases, then BED_LIGHTS/AUTO_LEVEL. Three small commits.
*Verify after each: the converted buttons, plus one unconverted button as control.*

**B3. Table-drive the switch decode.** Introduce
`static const struct { uint16_t mask; active_switch_t value; side_of_bed_t side; bool is_up; led_color_t led; } switch_table[]`
and reduce `process_inputs()`/`process_outputs()` to a lookup + the helpers
from B1. Handle the multi-bit mask case (P3.14) explicitly: iterate set bits
or reject cleanly with a debug print.
*Verify: all 11 buttons, press-and-hold and quick-tap, plus a deliberate two-button press (should be rejected the same way the double-press check does today).*

**B4. Table-drive the sensitivity ladder.** Replace the two ~100-line blocks
with `static const struct { int16_t raise; int16_t lower; ... } sensitivity_levels[4]`
plus an index and one LED-pattern function.
*Verify: in settings mode, step up from every rung and down from every rung; confirm NVM3 writes (power cycle and check printed loaded values).*

**B5. Extract the shared motor-supervision function.** One function, e.g.
`run_motor_supervision(active_switch_t sw, bool raise)`, containing the
scan → movement-check → per-motor average/max-load/threshold/limit-switch →
collision-check sequence. Call it from both app.c and auto_level.c
(`level_axis`). **This is the highest-care step in the plan** — it touches the
collision and limit-switch path in both modes. Diff the extracted body against
both originals line by line before building; the auto-level variant hardcodes
`collision_check(true)` and `SWITCH_AUTO_LEVEL_VALUE`, which become
parameters.
*Verify on hardware, exhaustively: (1) manual raise into a deliberate obstruction → collision trip + LED sequence + reverse; (2) manual lower collision; (3) collision during auto-level; (4) drive one actuator to its physical limit in both directions and confirm the inferred limit-switch stop; (5) all-up to limit → temp-off pause resumes the others.*

**B6. (Optional) Timer helper.** A tiny wrapper
(`app_timer_t { handle, timeout_flag }` + `app_timer_start_oneshot(&t, ms)`)
to collapse the 14 callback/start pairs. Pure boilerplate reduction; do it
only if the remaining app.c still feels noisy after B1–B5.
*Verify: every timed behavior at its expected duration — settings entry 5 s, store 2 s, LED blink rates 500/200 ms, auto-level axis pause 1 s, temp motor off 1.5 s.*

### Phase C — architecture for BLE, telemetry, and adjustable setpoints

**C1. Settings module (`bed_settings.c/h`).** One struct holding every
runtime-adjustable value (populate from SETPOINTS_INVENTORY.md, starting with
the four that already live in NVM3). A key table drives load-all/store-one;
each setting carries `{default, min, max}` and the setter **clamps
firmware-side** — this is where the safety limits requested in the inventory
get enforced, regardless of what a future app sends. Migrate the four existing
NVM3 values first (keep keys 1–4 so existing units keep their calibration);
new settings can then be added one line at a time.
*Verify: existing stored values survive the upgrade on a previously-calibrated unit (flash over the old firmware without erasing); settings mode still stores/loads; out-of-range write attempt (forced in code temporarily) clamps.*

**C2. Command API (`bed_control.c/h`).** Introduce the functions a phone app
will eventually call, and make the *switches* call them too:
`bed_move_start(side_or_all, direction)`, `bed_move_stop()`,
`bed_auto_level_start()/stop()`, `bed_lights_set(bool)`. `process_inputs()`
becomes a thin translator from switch events to these calls. All safety
supervision (B5) stays *below* this API so any command source gets it for
free. One command source at a time: the API owns `active_switch` internally
and rejects a second concurrent command — this resolves the state-combination
risk (P3.12) before BLE arrives instead of after.
*Verify: full button regression — every behavior should be unchanged since switches now route through the API.*

**C3. Telemetry snapshot (`bed_status.c/h`).** A single
`bed_status_get(bed_status_t *out)` filling a struct: four motor currents
(filtered), roll/pitch angles (scaled), per-motor on/direction/limit flags,
collision flag, active command, settings-mode flag. Sourced from the existing
data — read-only, no new sampling. This is exactly the payload a BLE
characteristic or serial debug dump needs; it also gives you a one-call
debug print for bench work immediately.
*Verify: add a temporary heartbeat print of the snapshot; sanity-check values against known bench conditions (motor running ≈ expected mA, tilt the frame and watch angles).*

**C4. Split app.c.** With B/C done, app.c shrinks naturally; move the
remaining chunks into files matching the state machines:
`switch_input.c` (decode + dispatch), `settings_mode.c`, `collision_ux.c`,
leaving app.c as init + tick that calls each subsystem. Pure file moves, one
subsystem per commit, added to the Simplicity Studio project as sources.
Also move `bed_control`'s mediated state (`active_switch`, movement/settings
state, and the `app_*` action functions introduced in C2) down into
`bed_control.c`, dissolving the C2 app-side back-call (see DECISIONS.md #12).
*Verify per move: build + the moved subsystem's behavior.*

### Phase D — feature enablers (separate efforts, sequenced after C)

These are not refactor steps; they're the projects the refactor unblocks,
with the constraints worth knowing now:

- **D1. BLE.** Adding the Bluetooth stack components to the .slcp is done in
  the Simplicity Studio configurator and **will regenerate `autogen/` and
  `config/`**. NOTE (2026-07-05): the hand-edited PWM configs (P3.17) are
  **no longer a concern** — DECISIONS #25 moved to one-motor-per-timer, so
  PWM config is now fully auto-generated and regeneration-safe. See
  HARDWARE_CONFIG.md for the component/pin/timer map. Also note D1 is now a
  fresh-project **SDK migration** to SiSDK 2026.6.0 (DECISIONS #20,
  SDK_PORTING_GUIDE.md), not an in-place edit of the 4.2.2 project.
  Budget for the BLE stack's tick requirements: audit the blocking
  delays (P3.16) first; the 2 ms-per-I2C-transaction re-driver delay is the
  main one to redesign (e.g., leave the re-driver powered while any motor
  command is active). GATT design falls out of C2 (write characteristic →
  `bed_move_*`) and C3 (notify characteristic ← `bed_status_get`), with C1
  as the settings service — clamped firmware-side.
- **D2. OTA.** Requires a Gecko Bootloader project (separate build) and the
  BLE OTA-DFU components; flash is 504 KB total so plan the slot layout
  before the app grows. Do this *with* D1, since OTA rides on BLE.
- **D3. Position sensing serial bus.** The BGM220S has one USART (currently
  the debug VCOM iostream) plus one EUART. Decide early which port the
  position bus gets; if it takes the USART, debug printf moves to RTT or the
  EUART. The `bed_status_t` struct (C3) is where position data slots in when
  it exists.

### Suggested order recap

A1 → A2 → A3 → A4 → A5 → B1 → B2 → B3 → B4 → B5 → (B6) → C1 → C2 → C3 → C4 → D1/D2 → D3

Everything through B4 is low-risk cleanup with per-button bench checks. B5 is
the one step that demands a full safety regression. Phase C is where the
codebase becomes BLE-ready without yet touching the radio.
