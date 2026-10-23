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
- [ ] Phase 6 — full REGRESSION.md pass on migrated firmware. Originally
      scoped as the hard gate BEFORE any BLE glue (DECISIONS #20); in
      practice, BLE Stage B (below) proceeded ahead of this gate since it
      cannot drive a motor and gave a live testbed for two BLE-drop bugs
      that have since been found and fixed (DECISIONS #26/#27). Test record
      prepped: `docs/REGRESSION_RECORD_D1.md` — still blank, never run, and
      it can no longer be run as scoped (it was the pre-BLE isolation pass;
      BLE code now exists — see that file's own pointer).
      **Corrected 2026-10-23:** the gate actually applied to Stage C was the
      post-Stage-B full pass recorded in
      `docs/REGRESSION_RECORD_PRE_STAGEC.md` (2026-07-19, commit `af369f2`,
      **Debug** build, phone subscribed throughout; recorded in `29fdf0d`).
      Stage C was then built (see "BLE status" below). **Still open:** a
      full REGRESSION.md pass on the *current* firmware (which now includes
      the Stage C, sampler and pairing-window items) has not been run, and
      the **Release-config confirmation pass is still owed before Stage C
      ships** (`REGRESSION_RECORD_PRE_STAGEC.md`'s own header note).

## BLE status (BLE_GATT_DESIGN.md)

- [x] **Stage B** — advertising, bonding, per-connection CONTROL/VIEWER role
      tracking, read-only Bed Status telemetry (`ble_bed_service.c`).
      **Bench-verified**: bonding works, Bed Status telemetry updates live
      while subscribed, and auto-level no longer drops the link (both
      BLE-drop bugs were the same root cause — DECISIONS #26/#27).
- [x] **Stage C** — Command characteristic → motors + dead-man watchdog
      (BLE_GATT_DESIGN.md §4-5). **Built and bench-tested** (corrected
      2026-10-23; this line previously said "Not started"). Record:
      DECISIONS #34 (built, `ab12d09`, 2026-08-02); the first bench run
      found the 10 Hz app-move stutter, fixed in `a22406f` (2026-08-02,
      #35, fix bench-confirmed per #36); auto-level became hold-to-run and
      watchdogged in `5351aef` (2026-08-04, #36); later bench-driven fixes
      #37-#41 (2026-08-04 → 2026-10-18). Pairing window (#42, `944f19e`,
      2026-10-18) and bond-store policy + closing refused pairings (#43,
      `c6ee6b0`, 2026-10-18) are bench-verified on 2026-10-18 (#42/#43
      "Bench result"). **Still open:** the watchdog's stall case and an
      intermittent `WATCHDOG_STOP` during app auto-level holds are under
      investigation (2026-10-23 diagnostics); the full REGRESSION.md pass on
      current firmware and the Release-config pass (Phase 6 above) have not
      been run.

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
  characteristics, packet layouts, watchdog, roles, OTA). Stage B (§3/§6/§8)
  and Stage C (§4/§5, the command→motor path) are both implemented and
  bench-tested — see "BLE status" above (corrected 2026-10-23).
- **REFACTOR_PLAN.md** — the A–D plan. Phases A–C are complete on the old
  project; D1 is this migration. The D1 note has been updated to reflect #25
  and the fresh-project migration.
- **REGRESSION.md** — the hardware regression checklist; the Phase 6 gate.
  Includes the §2b per-motor PWM check (#25). Now also requires a phone
  connected and subscribed to Bed Status throughout the pass, with a
  "BLE link stays up" check added to each timing-sensitive section
  (DECISIONS #26/#27) — run it that way from here on, not BLE-free.
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
   `app_init()` / `app_process_action()` bodies into it. The radio is no
   longer dormant (Stage B is live) — non-BLE bed behavior must still match
   the 4.2.2 baseline, but "until BLE work begins" no longer describes the
   current state.
4. **Grep for hardcoded `TIMER0`/`TIMERx`** references during the port and
   flag any (part of the #25 change).
5. **Regression before Stage C.** DECISIONS #20's original rule (no BLE glue
   before the full REGRESSION.md pass) was not followed as written. Stage B
   (telemetry-only, cannot drive a motor) proceeded ahead of Phase 6 and is
   bench-verified. Stage C was gated instead on the post-Stage-B full pass in
   `REGRESSION_RECORD_PRE_STAGEC.md` (2026-07-19, Debug), and has since been
   built and bench-tested (DECISIONS #34-#43; corrected 2026-10-23 — this
   item previously said Stage C was hard-gated and not started). What is
   still owed: a full REGRESSION.md pass on current firmware, and the
   Release-config confirmation pass before Stage C ships.
