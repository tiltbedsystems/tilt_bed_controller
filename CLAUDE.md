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

## Session handoff (required)

**Before ending any session, APPEND a handoff entry to
`docs/SESSION_LOG.md`.** This is not optional and it is not a summary for the
user — it is the next session's only record of what is actually known versus
what was merely claimed. A `Stop` hook in `.claude/settings.json` blocks the
session from ending until the file has been written — and, separately, until
this session has written its own handoff file under `docs/handoffs/`. Both
artifacts are required; see **Session handoff file (required)** below for the
second one and why it exists.

Rules:

- **Append. Never overwrite.** Newest entry at the bottom. Do not edit,
  reword, or delete an earlier entry. If an earlier entry turns out to be
  wrong, correct it in a *new* entry that says so — the same way
  `DECISIONS.md` corrects itself (see #27) rather than rewriting history.
- **Log even when no commits were made.** "Investigated X, changed nothing,
  here's what I ruled out" is exactly the kind of session whose findings get
  lost.

Every entry must contain:

1. **Date.**
2. **Commits** — short SHA plus one line each.
3. **VERIFIED vs ASSERTED-but-untested.** This is the important one. VERIFIED
   means there is evidence: a bench run that was actually executed, a
   measurement, a header that was actually read. ASSERTED means it is
   reasoned, compiles, and looks right, but nothing observed it. Code that
   builds is not code that was tested, and neither is a fix whose premise was
   never checked on hardware. Split them explicitly; when in doubt it is
   ASSERTED.
4. **What's left open** — including work deliberately deferred, and why.
5. **What the next session must NOT assume** — superseded decisions, numbers
   that are valid only in the context they were measured in, gates that are
   only partly satisfied, records that must not be backfilled.

Remember that you do not build or flash (see Hard constraints). Anything that
would need the bench to confirm, and that the user has not reported back on,
is ASSERTED — regardless of how confident the reasoning is.

## Session handoff file (required)

**A second, separate artifact. In addition to the `docs/SESSION_LOG.md` entry
above — not instead of it.** The `Stop` hook enforces both: the session cannot
end until `docs/SESSION_LOG.md` has been written *and* **this session** has
written its own handoff file in `docs/handoffs/`. A file left by an earlier
session on the same date does not satisfy it, and unlike the log hook this one
**fails closed** — if it cannot confirm, it blocks.

The two have different readers and different jobs. `SESSION_LOG.md` is a
summary for the next Claude Code session, and it keeps that job unchanged.
The handoff file is for a **reviewer outside this repository**, who sees only
this file — no conversation, no transcript, no repo browsing. A summary cannot
serve as evidence of the work it summarises; a reviewer who is handed a
description of a plan instead of the plan cannot review it. See
`docs/handoffs/README.md` for the incident that established this.

### Filename

```
docs/handoffs/fw_HANDOFF_<YYYY-MM-DD>_<NN>_<slug>.md
```

- `<NN>` — two-digit sequence within that date, starting at `01`.
- `<slug>` — short kebab-case description.
- **The `fw_` prefix is mandatory.** The sibling app repo uses `app_`, and
  both repos' files upload to the same place; a bare name collides and one
  silently overwrites the other.

### Required contents, in this order

- **a. Header.** Repo name, branch, HEAD before the session, HEAD after, and
  the full output of `git status` at the end.
- **b. THE FULL TEXT of every plan, design, recommendation, or analysis the
  session produced.** Verbatim, complete, inline in this file. Never a
  summary. Never a pointer to another document — not even one in this repo.
  If a plan existed only in the conversation, write it into this file in
  full. **This is the single most important requirement and the specific
  failure this file exists to prevent.**
- **c. The complete `git diff` for every source change**, verbatim — not a
  description of it. A docs-only diff over 400 lines may be truncated with an
  explicit note saying so; **source diffs are never truncated.** One standing
  exception: the `docs/SESSION_LOG.md` diff never needs reproducing, because
  the log ships alongside the handoff in the same upload — note its omission
  and move on.
- **d. The raw output of every command run** — builds, greps, git —
  **including failures.** A command that errored is evidence too.
  **Redaction:** if output contains a secret, replace it inline with
  `[REDACTED: what it was]` — never silently drop the line or the command.
  A reader must be able to see that something was removed and what kind of
  thing it was. This carve-out covers command output only; it does not
  license summarising under (b), which governs plans.
- **e. Every claim split VERIFIED vs ASSERTED, with the evidence attached to
  each VERIFIED claim** — `file:line` for a code fact, raw output for a run
  result. This repo's existing VERIFIED/ASSERTED discipline (see the section
  above) is the model; the change here is that the evidence travels with the
  claim instead of the claim standing alone.
- **f. `QUESTIONS FOR REVIEW`** — judgement calls the session did not feel
  authorised to make.
- **g. `NOT DONE`** — anything asked for and not done, with the reason.

Sections (b) and (e) are where this file earns its keep. Length is not a
reason to summarise: if the choice is between a long file and an incomplete
one, write the long file.

### Committing

**A handoff describes the commits that precede it, and is committed
separately, last.** Do the work, commit it, then write the handoff against
what was committed and commit that on its own. This keeps a work commit's diff
free of the document describing it, and means the handoff can quote real
SHAs and a real end-of-session `git status` instead of predicting them.

`docs/handoffs/` is version-controlled. These files are evidence; evidence that
only exists in an upload is not evidence.

Retention: none. Nothing is pruned or archived, and one file per session
accumulates indefinitely. Revisit if the directory gets heavy.

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
  component/pin record; `REGRESSION.md` is the hardware gate checklist;
  `SESSION_LOG.md` is the append-only session handoff record (see "Session
  handoff (required)" above); `handoffs/` holds the per-session
  reviewer-facing handoff files (see "Session handoff file (required)" above,
  and `docs/handoffs/README.md`).

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
