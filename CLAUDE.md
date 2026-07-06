# tilt_bed_controller

Firmware for the Tilt Bed Systems camper-van bed controller, on **Simplicity
SDK 2026.6.x LTS**, Silicon Labs BGM220SC22HNA2 (EFR32BG22 die). Simplicity
Studio 6 is used only as the component/pin configurator; **the build is
VS Code + GCC + CMake** (`cmake_gcc/`) — there is no in-Studio build.

**Read `docs/MIGRATION_STATUS.md` first**, every session. It is the
orientation file: where the migration stands, what each other doc in
`docs/` is for, and what must not get lost in the move. `docs/DECISIONS.md`
is the binding decision log — where it conflicts with older text elsewhere
(including this file), DECISIONS.md wins.

This repo also ships `AGENTS.md` → `autogen/AGENTS-SLAB.md`, Silicon Labs'
own generated agent guidance for SiSDK projects. Follow it where it doesn't
conflict with this file or `docs/DECISIONS.md`; this file and DECISIONS.md
win on conflict.

## Who you're working with

The user is an electrical engineer, not an experienced software developer,
doing his first SDK migration on this project. Optimize for their
understanding, not just correctness:

- **Explain before editing.** Before changing code, briefly explain what
  you're about to change and why, in plain terms — assume EE background,
  not software background (registers, interrupts, timing, and ISRs are
  fine to reference directly; gloss software idioms like design patterns).
- **Ask when a request is ambiguous.** Don't guess at intent for anything
  affecting hardware behavior (motor direction, timing, current
  thresholds, pin assignments). Ask.
- **Restate and wait before large changes.** For anything beyond a small,
  self-contained fix, restate your understanding of the request and your
  plan, then wait for explicit approval before writing code.
- **Work in small, hardware-verifiable steps, and STOP at gates.** Prefer
  a sequence of small changes the user can each verify over one large
  change, especially for anything touching motor control, limit switches,
  collision detection, or current sensing. Commit in small, clear units.

## Hard constraints

- **SDK is pinned to Simplicity SDK 2026.6.x LTS.** Patch bumps within
  2026.6 are fine; anything else needs a recorded decision in
  `docs/DECISIONS.md` first (see DECISIONS #20 for why 2026.6.0 was
  chosen and why the old Gecko SDK line is closed to this part). The old
  4.2.2 pin from the previous project is obsolete — do not carry it
  forward or treat it as still binding here.
- **Never hand-edit `autogen/` or `config/`.** All configuration changes
  go through the Simplicity Studio 6 configurator (components + Pin
  Tool), which regenerates these directories. Per DECISIONS #25, the PWM
  retopology (one motor per timer) deliberately closed off the one case
  that used to require a hand-edited config file — if you ever find
  yourself wanting to hand-edit a generated file, that is a signal to
  stop and ask, not a shortcut to take.
- **No BLE glue in this phase.** The radio stays dormant until the
  migrated firmware passes the full `docs/REGRESSION.md` pass on hardware
  (DECISIONS #20). Don't add `sl_bt_on_event` handling, GATT logic, or
  anything beyond what the stock "Bluetooth – SoC Empty" sample already
  provides, even if it looks like a natural next step.
- **Keep the `bed_` module prefix.** Only cosmetic "Totem"/"Totem
  Sleep"→"Tilt Bed"/"Tilt Bed Systems" string changes are in scope; don't
  rename modules, functions, or the prefix.
- **You do not build or flash.** The user drives the VS Code/CMake build
  and all hardware testing on the bench himself. You may drive the
  CMake/GCC build from the CLI only if asked to (e.g. to iterate on
  compile errors); otherwise give exact steps and let the user report
  results back.

## Where this fits in the migration (D1)

See `docs/MIGRATION_STATUS.md` for the live checklist. Short version: this
is a **fresh-project port**, not an in-place SDK upgrade — a new "Bluetooth
– SoC Empty" project on 2026.6.0, with the application C sources ported in
from the old project and pin/peripheral config re-entered through the
configurator (DECISIONS #20). The old project
(`totem_controller`, Gecko SDK 4.2.2) is frozen, hardware-verified, and
READ-ONLY reference material during this migration — never modify it.

The gate at the end of this phase is a clean build with behavior matching
the 4.2.2 baseline exactly; the full `docs/REGRESSION.md` pass (including
the DECISIONS #25 per-motor PWM checks) happens on the bench, by the user,
before any BLE work begins.

## Project layout

- `app.c` / `app.h`, `main.c` — SoC Empty sample skeleton plus the ported
  application state machine and tick loop (the `app.c` merge is the one
  non-mechanical step in the port; see `docs/MIGRATION_STATUS.md`).
- `bed_*` modules — application logic ported from the old project:
  `bed_actions`, `bed_control`, `bed_settings`, `bed_status`, `bed_types.h`.
- `auto_level.c/h` — accelerometer-driven auto-leveling (pitch/roll,
  per-axis leveling status, baseline angle handling).
- `dual_motor_control.c/h` — pairs motors by bed side (HEAD/LEFT/RIGHT/
  FOOT) and drives them together. Now one motor per PWM timer
  (DECISIONS #25) — no shared-timer assumptions should remain.
- `motor_current_functions.c/h`, `adc_currents.c/h` — current-based motor
  supervision (stall/collision detection, thresholds, running averages)
  and the IADC driver feeding it.
- `exp_board.c/h`, `exp_ui.c/h`, `pi4ioe5v6416.h` — I2C GPIO expander
  drivers for the onboard motor outputs/lighting and the remote
  switch/UI board.
- `mc3479.c/h` — accelerometer driver used for leveling.
- `nvm3_functions.c/h` — persisted calibration/settings storage.
- `version.h`, `debug_capture.h` — firmware version; debug data-capture
  support (DECISIONS #5).
- `test/` — host-compiled unit tests (currently the `bed_control.c` source
  lock arbitration test; see DECISIONS #12).
- `autogen/`, `config/` — Simplicity Studio-generated init/config code
  from `.slcp` and the Pin Tool. Tracked in git because the build depends
  on them; treat as generated only — see hard constraints above.
- `cmake_gcc/` — the VS Code/GCC/CMake build project (`CMakeLists.txt`,
  presets, generated `.cmake` files). This is the actual build path.
- `tilt_bed_controller.slcp` / `.pintool` / `.slps` — Simplicity Studio 6
  project/component configuration and pin assignment tool state.
- `docs/` — migration and design docs. Start with `MIGRATION_STATUS.md`;
  `DECISIONS.md` is binding; `HARDWARE_CONFIG.md` is the authoritative
  component/pin record; `REGRESSION.md` is the hardware gate checklist.

## Working conventions

- This is safety-relevant embedded code (motors moving a bed frame). Be
  extra careful with anything affecting motor direction logic, limit/
  collision handling, current thresholds, timing around interrupts, and
  the two-source command lock (DECISIONS #12) — flag anything you change
  in this area explicitly.
- All detection/timing logic is defined in milliseconds via the
  sleeptimer, never loop-iteration or sample counts (DECISIONS #2).
- Match the existing style in each file rather than introducing new
  patterns. Don't add abstractions, refactors, or "cleanup" beyond what
  was asked — small, reviewable diffs are the priority given
  hardware-in-the-loop testing.
