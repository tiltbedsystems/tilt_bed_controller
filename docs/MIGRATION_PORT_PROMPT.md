# MIGRATION_PORT_PROMPT.md — paste the block below into Claude Code

Run Claude Code inside the NEW project folder
(`...\_03 Software\tilt_bed_controller`). Prerequisite before pasting: you
have finished entering the components and pins in the Simplicity Studio 6
configurator per docs/HARDWARE_CONFIG.md, and the still-source-less project
builds clean in VS Code.

---------------------------------------------------------------------------

Read `docs/MIGRATION_STATUS.md` first — it is the orientation file for this
repo and explains everything below. Then read `docs/HARDWARE_CONFIG.md` and
DECISIONS #20 and #25 in `docs/DECISIONS.md` before writing anything.

## Context

This repo is the new home of the Tilt Bed Systems bed-controller firmware:
a fresh Simplicity SDK 2026.6.0 "Bluetooth – SoC Empty" project
(BGM220SC22HNA2 / EFR32BG22, Simplicity Studio 6 as configurator, **VS Code
+ GCC + CMake as the build environment** — there is no in-Studio build).
GitHub `main` currently has one commit: the stock base. The `docs/` folder
was just added locally and is NOT yet committed.

The OLD project (Gecko SDK 4.2.2, working, hardware-verified, frozen) is at:
`C:\Users\adunc\Desktop\_Tilt Beds\_03 Software\totem_controller`
It is READ-ONLY reference material. Never modify anything in it.

I have completed the configurator work: all software components and pin
assignments from HARDWARE_CONFIG.md are entered, including the new
one-motor-per-timer PWM mapping (DECISIONS #25: motor1→TIMER0,
motor2→TIMER1, motor3→TIMER2, motor4→TIMER3; TIMER4 reserved by the base
project — note the reserved timer shifted during configuration, see
HARDWARE_CONFIG.md §3). The stock project still builds clean after that
work.

I am an electrical engineer, cautious, first time through an SDK migration.
Work in small verified steps, STOP at each gate for my confirmation, explain
before acting, never force-push, commit in small clear units.

## Standing rules (these go into the new CLAUDE.md too)

- The firmware is a safety-relevant motor controller. Behavior must match
  the 4.2.2 baseline exactly at the end of this port — the radio stays
  dormant; no BLE glue is written in this phase (DECISIONS #20: full
  REGRESSION.md pass comes first).
- SDK is pinned to **Simplicity SDK 2026.6.x LTS** (patch bumps within
  2026.6 are fine; nothing else without a recorded decision). The old rule
  pinning Gecko SDK 4.2.2 is obsolete — do not carry it forward.
- Never hand-edit `autogen/` or `config/` — all config changes go through
  the Studio configurator. Per DECISIONS #25 there are no longer any
  "protected" hand-edited PWM files; if you ever find yourself wanting to
  hand-edit generated PWM config, stop — that path was deliberately closed.
- Keep the `bed_` module prefix. Only cosmetic "Totem"→"Tilt Bed" strings
  change.

## TASK 1 — housekeeping commits (do now, then STOP)

1. Commit the `docs/` folder as its own commit:
   "Add docs: migration status, hardware config, decisions through #25".
2. Read the repo's existing `AGENTS.md` (shipped by the Silicon Labs
   sample). Then write a new `CLAUDE.md` for this repo. Use the OLD repo's
   CLAUDE.md (in the old project folder) as the model for working style —
   explain-before-editing, ask when ambiguous, small verifiable steps — but
   with updated facts: SiSDK 2026.6.x pin (NOT 4.2.2), Studio-6-as-
   configurator / VS-Code-as-build workflow, pointer to
   docs/MIGRATION_STATUS.md as required first read, the standing rules
   above, and a note that AGENTS.md contains Silicon Labs' own agent
   guidance for this project type (follow it where it doesn't conflict;
   this CLAUDE.md and docs/DECISIONS.md win on conflict).
3. Check `.gitignore` covers the VS Code/CMake build outputs (build
   directories, *.o/*.d/*.map, etc.) without excluding `autogen/`,
   `config/`, or `cmake_gcc/` project files. Fix if needed.
4. Commit: "Add CLAUDE.md (SiSDK-era working rules)". Push. STOP and show
   me what you wrote in CLAUDE.md before we continue.

## TASK 2 — source port (after my go-ahead)

1. Copy from the old project into this repo root (read-only from source):
   adc_currents.c/.h, auto_level.c/.h, bed_actions.c/.h, bed_control.c/.h,
   bed_settings.c/.h, bed_status.c/.h, bed_types.h, debug_capture.h,
   dual_motor_control.c/.h, exp_board.c/.h, exp_ui.c/.h, mc3479.c/.h,
   motor_current_functions.c/.h, nvm3_functions.c/.h, pi4ioe5v6416.h,
   settings_mode.c/.h, switch_input.c/.h, version.h, and the test/ folder.
   Do NOT copy: the old app.c/app.h, main.c, autogen/, config/, the old SDK,
   old build folders, old project metadata (.slcp/.slps/.pintool/.cproject),
   or the old CLAUDE.md.
   Commit: "Port application sources from totem_controller (unmodified)".
   Committing them UNMODIFIED first means every adaptation after this is a
   reviewable diff.
2. The app.c merge (the one non-mechanical step): keep THIS project's
   main.c and the sample app.c/app.h skeleton (its sl_bt_on_event and boot
   handling stay intact and dormant); merge the old app.c's app_init() body
   and app_process_action() tick body into it so the firmware behaves
   exactly as the 4.2.2 baseline. Delete (do not port) the old app.c PWM
   WARNING block — obsolete per DECISIONS #25. Show me the merged app.c
   diff and explain your choices before committing.
3. Rename residual "Totem"/"Totem Sleep" strings to "Tilt Bed" /
   "Tilt Bed Systems" (a comment in dual_motor_control.c; comment + welcome
   printf in app.c). Keep the bed_ prefix everywhere.
4. Grep the ported sources for hardcoded TIMER0/TIMERn references or any
   assumption that motors share a timer/counter (synchronized edges).
   Report findings before changing anything (DECISIONS #25 consequence).
5. STOP for my review.

## TASK 3 — compile-fix loop (after my go-ahead)

Build (you may drive the CMake/GCC build from the CLI if the generated
cmake_gcc project supports it; otherwise give me exact VS Code steps and
I'll report errors back). Fix compile errors top-down, smallest change that
preserves behavior — expect Platform 4.2 → 5.x mechanical differences:
renamed sl_ APIs, moved headers, signature tweaks (sleeptimer, NVM3, IADC,
I2CSPM, PWM). Also confirm whether the old global define DEBUG_EFM is still
needed and wire it via the project config if so. One commit per coherent
cluster of fixes, message naming the API/subsystem. If any fix would change
BEHAVIOR (not just API surface), stop and ask me first.

Gate: clean build, zero errors, warnings reviewed. Then STOP.

## After TASK 3 (mine, not yours)

I flash and run the full docs/REGRESSION.md pass on the bench — including
the new §2b per-motor PWM mapping checks — before any BLE work begins.
Your job in this phase ends at the clean build.

---------------------------------------------------------------------------
