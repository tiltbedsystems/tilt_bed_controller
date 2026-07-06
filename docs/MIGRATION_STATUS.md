# MIGRATION_STATUS.md

Orientation note for the `tilt_bed_controller` repo (created 2026-07-05).
Read this first. It explains where the project is in the D1 migration and
what each doc is for, so nothing gets lost in the move from the old
`totem_controller` (Gecko SDK 4.2.2) project.

## What this repo is

The active firmware line for the Tilt Bed Systems camper-van bed controller,
on **Simplicity SDK 2026.6.0 LTS**, BGM220SC22HNA2 (EFR32BG22 die), VS Code /
GCC / CMake project format under Simplicity Studio 6. It replaces the old
`totem_controller` project (frozen archive; old business name "Totem Sleep").
The `bed_`-prefixed module naming is retained deliberately — do NOT rename it.

## Where we are in D1 (fresh-project SDK migration — DECISIONS #20)

Following SDK_PORTING_GUIDE.md, gates in order:

- [x] Phase 0 — old 4.2.2 project tagged/branched/backed-up (untouched)
- [x] Phase 1 — SiSDK 2026.6.0 + Studio 6 installed
- [x] Phase 2 — stock "Bluetooth – SoC Empty" builds + flashes clean (Gate 2)
- [x] Phase 4 (done before 3) — configurator components + pins fully
      entered per HARDWARE_CONFIG.md, verified against Pin Tool screenshot
- [x] Phase 3 — source port: `.c/.h` copy + the `app.c` merge
- [x] Phase 5 — compile-fix loop (Platform 4.2 → 5.x API renames). Clean
      build confirmed (zero errors, zero warnings under `-Wall -Wextra`) at
      commit `1165ecf`. Bounded semantic-drift check across sl_pwm, nvm3,
      sl_sleeptimer, sl_i2cspm, sl_simple_button, and IADC init found no
      behavioral drift versus the 4.2.2 baseline.
- [ ] Phase 6 — **full REGRESSION.md pass on migrated firmware, BEFORE any
      BLE glue** (this is the hard gate; DECISIONS #20). Test record
      prepped: `docs/REGRESSION_RECORD_D1.md`.

Only after Phase 6 does actual BLE work (BLE_GATT_DESIGN.md) begin.

## The docs, and what changed recently

- **HARDWARE_CONFIG.md** — NEW, authoritative. Components, pin table, and the
  PWM timer mapping to reproduce in the configurator. Supersedes any chat/tool
  recap for this content.
- **DECISIONS.md** — decision log. Most recent: **#25 (PWM one-motor-per-
  timer)** and #18–#24 (the BLE/app-era decisions). #25 in particular changes
  how PWM is configured — read it before touching motor config.
- **SDK_PORTING_GUIDE.md** — the step-by-step migration procedure (the phases
  above). Written for the GUI path; note the IDE is now VS Code, not the old
  in-Studio "Simplicity IDE".
- **BLE_GATT_DESIGN.md** — the D1 BLE protocol contract (services,
  characteristics, packet layouts, watchdog, roles, OTA). Not started yet;
  it's the Phase-6-onward work and the app's interface spec.
- **REFACTOR_PLAN.md** — the A–D plan. Phases A–C are complete on the old
  project; D1 is this migration. The D1 note has been updated to reflect #25
  and the fresh-project migration.
- **REGRESSION.md** — the hardware regression checklist; the Phase 6 gate.
  A new per-motor PWM check (for #25) should be folded in (see
  HARDWARE_CONFIG.md §4).
- **SETPOINTS_INVENTORY.md**, **APP_KICKOFF_PROMPT.md**, **APP_SETUP_GUIDE.md**
  — setpoint reference and the (separate, parallel) Flutter-app track.

## Key things not to lose in the migration

1. **PWM is now one-motor-per-timer** (motor1→TIMER0, motor2→TIMER1,
   motor3→TIMER2, motor4→TIMER3; TIMER4 reserved by the base project). The old
   shared-TIMER0 hand-edited `sl_pwm_init_motorX_config.h` approach and its
   `app.c` WARNING block are **deleted, not ported**. (DECISIONS #25)
2. **Keep the `bed_` module prefix.** Only cosmetic "Totem"→"Tilt Bed"
   strings change (a comment in `dual_motor_control.c`, a comment + one
   printf in `app.c`).
3. **`app.c` merge is the one non-mechanical step**: keep the SoC Empty
   sample's `main.c` + `sl_bt_on_event` structure; merge the old
   `app_init()` / `app_process_action()` bodies into it. Until BLE work
   begins, the radio stays dormant and behavior must match the 4.2.2 baseline.
4. **Grep for hardcoded `TIMER0`/`TIMERx`** references during the port and
   flag any (part of the #25 change).
5. **Regression before BLE.** Do not write BLE glue until the migrated
   firmware passes the full REGRESSION.md pass on hardware.
