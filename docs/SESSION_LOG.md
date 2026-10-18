# SESSION_LOG.md — session handoff log

Append-only record of what each working session actually did. **Newest entry
at the bottom.** Never rewrite or delete an earlier entry; if an entry turns
out to be wrong, say so in a later entry rather than editing history.

Required by the "Session handoff (required)" section of `CLAUDE.md`, and
enforced by the `Stop` hook in `.claude/settings.json`.

The point of this file is the **VERIFIED vs ASSERTED** split. This project has
already been bitten once by a claim recorded as fact that was never tested
(DECISIONS #27, which had to be corrected to "open, undiagnosed" after being
written up as fixed). A session that ends without stating which of its claims
were actually put on hardware has left a trap for the next session.

Entry template:

```
## YYYY-MM-DD — <short title>

**Commits**
- `abc1234` one line

**VERIFIED (evidence on hardware or by a run that was actually executed)**
**ASSERTED but NOT tested**
**Open**
**The next session must NOT assume**
```

---

## 2026-08-01 — Backfill: Stage-B BLE fixes and the Stage C regression record (covers 2026-07-17 → 2026-07-19)

First entry. Written 2026-08-01 as a backfill of the four commits below; the
work itself was done 2026-07-17 through 2026-07-19.

**Commits**

- `1bfebd8` (2026-07-17) — DECISIONS.md #29: auto-level's filter constants
  (`NUM_ACCEL_READINGS`, `accel_alpha`) are calibrated in loop passes, not
  time, so the averaging window and LPF time constant move with loop rate.
  Flags that the same problem reaches the collision-detection path. Recorded
  only — nothing fixed in this commit.
- `28def95` (2026-07-19) — Fix auto-level BLE drop: `read_accel()` now gates
  the accel I2C burst to `ACCEL_I2C_MIN_INTERVAL_MS` (10 ms, the MC3479's
  100 Hz output rate), reusing the cached raw sample between reads. Bus
  occupancy on `sl_i2cspm_on_board` drops ~60% → ~13%. DECISIONS #30 records
  the mechanism; #27 marked resolved.
- `af369f2` (2026-07-19) — Seed Bed Status CCCD subscription from stack
  ground truth on role assignment. `assign_role_if_needed()` now reports
  whether it just assigned a role; both call sites then call
  `seed_status_subscription_from_cccd()`, which reads
  `sl_bt_gatt_server_read_client_configuration()` directly instead of waiting
  for a CCCD *change* event that a resumed bond never generates. Also logs
  previously-silent unmatched `characteristic_status` events.
- `29fdf0d` (2026-07-19) — Record the post-Stage-B regression pass gating
  Stage C. New `docs/REGRESSION_RECORD_PRE_STAGEC.md`; pointer added to
  `REGRESSION_RECORD_D1.md` and a note to DECISIONS #20.

**VERIFIED (evidence on hardware or by a run that was actually executed)**

- **`28def95` root cause and fix.** Controlled A/B on a single binary with
  only the rate-limit differing. Unconditional per-pass blocking accel I2C
  during auto-level's motor-on EM0 free-spin starved the radio past its
  1000 ms supervision timeout (`connection_closed` reason `0x1008`);
  rate-limited, the disconnects are gone.
- **The competing hang hypothesis was refuted, not merely doubted.**
  `I2CSPM_Transfer()` silently spinning its 300000-iteration retry loop past
  a release-mode `EFM_ASSERT` was instrumented and measured: zero transfer
  failures of any kind, max transfer duration ~1.4 ms.
- **Full `REGRESSION.md` pass on `af369f2`** — all nine sections PASS, phone
  bonded and subscribed to Bed Status continuously throughout, no disconnects
  anywhere in the run. Recorded in `docs/REGRESSION_RECORD_PRE_STAGEC.md`,
  signed, firmware 1.1.0.
- **Loop cadence measured for the first time**, on the auto-level path:
  ~2.31 ms/pass on current code; 630 vs 433 passes/sec across the two arms of
  the #27 bisection, with the averaging window shrinking 31% (206 ms vs
  300 ms) purely as a side effect of the I2C change.
- **`sl_bt_gatt_server_read_client_configuration()`'s signature** was checked
  against the installed SiSDK 2026.6.0 header before `af369f2` was written.

**ASSERTED but NOT tested**

- **The `af369f2` CCCD-seeding fix itself.** The reasoning is from the SDK
  doc (CCCD events fire on *change* only) and the code compiles against the
  real header, but **no bench run has confirmed that the stack actually
  restores CCCD state across a resumed-bond reconnect**, which is the whole
  premise. The `cccd_seed` print exists precisely as that probe and its
  output has not been read out. The regression pass on `af369f2` does not
  cover this: nothing in `REGRESSION_RECORD_PRE_STAGEC.md` exercises a
  disconnect-and-reconnect-on-an-existing-bond cycle.
- **Anything about Release-config behavior.** The entire regression pass was
  a **DEBUG build**. `EFM_ASSERT` compiles out in Release, and this project's
  own history (DECISIONS #26's blocking-I2C bug, the #30 bisection) turned on
  assertions and instrumentation that behave differently or not at all in
  Release. **A Release-config confirmation pass is still owed before Stage C
  ships.**
- **The ~808 ms collision-detection figure in DECISIONS #29.** It is the
  1b2f8ee "350 samples within 303 ms" threshold re-derived against the
  *auto-level* measured cadence. Manual-move collision detection does not
  carry the same per-pass I2C load and has never been measured.

**Open**

- **DECISIONS #29** — loop-rate-dependent calibration of auto-level's filter
  constants, and the same problem reaching collision detection.
  Safety-relevant, open, deliberately not part of the BLE gate.
- **Release-config confirmation pass** — owed before Stage C ships.
- **Resumed-bond CCCD probe** — read the `cccd_seed` print on a real
  reconnect and record the result.
- **Subscription bookkeeping is not keyed by connection handle.** Left alone
  in `af369f2` on purpose; changing it would touch DECISIONS #21's viewer
  promotion logic.
- `REGRESSION_RECORD_D1.md` stays an empty template permanently — its
  precondition (a full pass *before any BLE glue*) was overtaken when Stage B
  landed first and can no longer be met.
- Known limitations carried forward: no motor reverse after a collision
  during auto-level (REFACTOR_PLAN P1.5); settings mode has no soft exit,
  power cycle only (P3.13).

**The next session must NOT assume**

- **...that the Stage C gate is satisfied.** It is satisfied *in Debug only*.
- **...that DECISIONS #27 is a live open bug.** It was resolved by #30; #27
  was marked resolved rather than rewritten, so reading it in isolation is
  misleading.
- **...that `REGRESSION_RECORD_D1.md` should be filled in.** Backfilling it
  would misrepresent what that record was for. Leave it as a template.
- **...that the CCCD seeding fix is proven.** It is reasoned and compiled,
  not observed.
- **...that ~808 ms applies to manual-move collision detection.** That number
  is auto-level-specific and extrapolating it is exactly the error #29 warns
  about.
- **...that any constant expressed in loop passes is stable in time.** Per
  DECISIONS #2 and #29, safety and control timing must be in milliseconds and
  checked against a *measured* loop rate.

---

## 2026-08-01 (later) — CORRECTION: `af369f2`'s CCCD seeding was VERIFIED on hardware, not ASSERTED

**Corrects the 2026-08-01 backfill entry above.** That entry is left standing
as written, per this file's append-never-overwrite rule; read it together with
this one.

**Commits**

- `ba7ca0b` — Enforce session handoffs: `docs/SESSION_LOG.md` + Stop hook.
  (Created the log; not listed in the backfill entry, which was written before
  the commit existed.)
- This commit — this correction entry.

**What the backfill entry got wrong**

It filed `af369f2`'s CCCD seeding under "ASSERTED but NOT tested" and listed
"Resumed-bond CCCD probe" as open, on the reasoning that the `cccd_seed` print
existed as a probe but its output had not been read out. **That was wrong.**
The probe had already been run on hardware, and passed, *before* the backfill
entry was written. The evidence existed; the entry failed to account for it.

This is the same class of error as DECISIONS #27, inverted. #27 recorded a fix
as confirmed when nothing had confirmed it. This entry recorded a
hardware-confirmed result as unconfirmed. Both put a claim in the record that
the evidence did not support, and both mislead the next session — one into
false confidence, one into re-running work that was already done and
re-opening a question that was already answered. The VERIFIED/ASSERTED split
is only worth anything if it is applied against the evidence that actually
exists, in both directions. Ask what has been run before classifying.

**Moved from ASSERTED to VERIFIED**

- **`af369f2`'s CCCD seeding, and the premise it was built on.** Bench
  captures, serial via PuTTY on COM6, on a resumed-bond reconnect with no
  re-pairing:

  ```
  BLE: cccd_seed conn=2 bed_status=0x0001 sc=0x0000
  BLE: cccd_seed conn=3 bed_status=0x0001 sc=0x0000
  ```

  `bed_status=0x0001` confirms the Silicon Labs stack **does** restore the
  client's CCCD across a resumed bond — which is exactly the premise the fix
  rests on, and the thing the backfill entry said had never been observed.

- **The end-to-end acceptance test passed.** On reconnect, live Bed Status
  telemetry flowed **without any unsubscribe/resubscribe toggle in nRF
  Connect**. That toggle was the original user-facing symptom; it is gone. The
  fix is confirmed at the behavior level, not just at the probe level.

**Also observed on the bench, and expected — do not re-investigate**

- **`characteristic_status DROPPED` at `security_mode=0`, on every
  connection.** The stack replays the stored CCCD before encryption completes
  and before a role has been assigned, so the event has no role slot to land
  in and is dropped. The post-encryption `cccd_seed` is what recovers it —
  that is the whole point of `af369f2`. This line is benign and by design.
  It is the logging `af369f2` added for previously-silent unmatched events
  doing its job, **not** a symptom. Do not chase it as a bug.
- **`REPLACING existing bond` after deleting the phone-side bond and power
  cycling.** Correct behavior: firmware NVM3 still holds the bond, so the
  re-pair hits the `f259605` auto-replace path (DECISIONS #28). Working as
  designed.

**Open — with one item now closed**

- ~~Resumed-bond CCCD probe~~ — **CLOSED.** Run, passed, evidence above.
- **Everything else in the backfill entry's Open list still stands**, in
  particular:
  - **The Release-config confirmation pass is still owed before Stage C
    ships.** The regression pass was Debug-only. Nothing in this correction
    touches that; `EFM_ASSERT` still compiles out in Release.
  - DECISIONS #29 (loop-rate-dependent calibration reaching the
    collision-detection path) — still open, still safety-relevant.
  - Subscription bookkeeping still not keyed by connection handle
    (deliberate; would touch DECISIONS #21).
  - `REGRESSION_RECORD_D1.md` still stays an empty template.

**The next session must NOT assume**

- **...that the backfill entry's ASSERTED list is current.** Its CCCD-seeding
  item is superseded by this entry. The rest of that list stands.
- **...that the `characteristic_status DROPPED` line indicates a fault.** See
  above — expected, benign, explained.
- **...that Stage C's gate is now clear.** The CCCD question is settled; the
  Debug-only regression pass is not. That gate is unchanged.
- **...that a claim in this log was classified against all available
  evidence.** This entry exists because one wasn't. If a classification looks
  wrong, check the bench record before building on it.

---

## 2026-08-02 — PLANNING ONLY: pitch wire sign (Item 1) and continuous tilt sampling (Item 2)

**Commits: none.** Planning session by explicit instruction; firmware parked
at `29fdf0d` and not touched. No source file was modified. The only file
changed this session is this log.

Confirmed by `git diff --stat 29fdf0d..HEAD` that the two commits after
`29fdf0d` (`ba7ca0b`, `6cbd2fd`) touch only `docs/`, `CLAUDE.md`,
`.gitignore` and `.claude/` — **the firmware tree is still bit-for-bit
`29fdf0d`.**

**VERIFIED (read directly from the tree this session — code facts, not bench
results)**

- `read_accel()` is called from exactly one place: `accel_running_average()`
  (auto_level.c:237). `accel_running_average()` is called from exactly two:
  app.c:569 and settings_mode.c:258. Confirms the staleness premise.
- **Auto-level's sample buffer is never reset.** `reset_level_data()`
  (auto_level.c:366-384) does not touch `accel_readings_roll/pitch[]`,
  `index`, `accel_running_sum_*`, or the LPF state `latest_*_filtered`. They
  are zero-initialised at boot and persist for the whole power cycle. So the
  buffer is *already* pre-filled at entry to every auto-level run after the
  first since boot — "starting cold" describes only the first run.
- `steady_accel_read_both_axes()` (auto_level.c:549) does not sample — its
  `accel_running_average()` call is commented out at line 550. It waits
  `STEADY_ACCEL_READ_TIMER_TIME` (1500 ms) before the first `check_level()`,
  while app.c:569 feeds the buffer every pass. At the measured ~2.31 ms/pass,
  130 samples ≈ 300 ms, so the buffer is fully overwritten ~5× before the
  first check.
- `app_is_ok_to_sleep()` (app.c:500) vetoes sleep **only while a motor is
  energised**. Idle, the loop sleeps — so idle is not a fast loop and needs a
  wake source for any idle sampling.
- `ble_bed_service_tick()` early-returns on `status_subscriber_count == 0`
  (ble_bed_service.c:339), and its own comment already names the intended
  gate: "no accel-sampling gate to feed yet".
- `status_flags_changed()` (ble_bed_service.c:313-324) memcmps bytes 0-3, so
  the flags byte — which holds `tilt_fresh` (bit 3) — forces an immediate
  notification on every transition. Tilt *angles* are deliberately excluded.
- `tilt_fresh` is hardcoded `false` at ble_bed_service.c:347.
- Build is `-mfpu=fpv5-sp-d16 -mfloat-abi=hard` (single-precision hardware
  FPU), while `read_accel()` uses **double**-precision `sqrt()` and two
  `atan2()` calls (auto_level.c:217-221; `PI`/`RAD_TO_DEG` are double
  literals). Those run on *every* call — the 10 ms gate bounds only the I2C
  burst, not this math.
- Wire path for pitch is a single assignment, bed_status.c:93; ble_bed_service.c:80-81
  is sign-agnostic byte packing. Negation cannot overflow int16: worst-case
  source range is about −28500..+10500, against INT16_MIN −32768.
- **Companion app (read-only, `../tilt_bed_app`): no change needed for the
  pitch sign, verified not assumed.** `bed_status.dart:190` decodes
  `getInt16(6)/100.0` with no transform; `telemetry_panel.dart:65` displays it
  directly; tests use literal round-trips or `.abs()`. The mock's `_pitchDeg()`
  (`headAvg - footAvg`, mock_bed_transport.dart:163) is *already* positive =
  head raised, so the firmware fix removes a latent mock-vs-hardware
  divergence rather than creating one.
- **Cross-repo dependency for `tilt_fresh`:** the app deliberately passes
  `fresh: true` instead of `status.tiltFresh` (telemetry_panel.dart:55, with
  `TODO(tilt-fresh)`) *because* firmware hardcodes 0. Firmware implementing
  the real bit changes nothing on screen until that TODO is reverted.

**ASSERTED but NOT tested — the whole plan**

- The argument that the `28def95` 10 ms rate limit makes per-tick sampling
  safe **for the I2C bus** during manual moves (same load, same bus, same
  power state as the bench-clean auto-level path) is reasoning from the #30
  measurement, not a new measurement. Nothing has been run.
- The claim that the added double-precision math is the unbounded cost is
  read from build flags and source, **not profiled**. No timing was taken.
- No loop-rate measurement exists for the manual-move path at all — the
  ~2.31 ms/pass figure is auto-level only (see #29).
- The conclusion that auto-level's first check is unaffected under the
  recommended architecture follows from the 1500 ms vs ~300 ms margin above;
  it has not been observed on hardware.

**Open**

- Both items are unimplemented and unapproved. Plan awaits review.
- Everything already open stands, in particular the **Release-config
  confirmation pass still owed before Stage C ships**, and DECISIONS #29.
- Candidate identified but deliberately NOT folded into either item: moving
  the trig behind the same 10 ms gate as the I2C burst. It looks like a free
  optimisation (between real samples the inputs are identical, so sqrt/atan2
  recompute the same result) but it would change the LPF's effective time
  constant, which is a control-law change and squarely a #29 matter.

**The next session must NOT assume**

- **...that any of this plan is validated.** It is a reading of the code plus
  an argument. The bench has seen none of it.
- **...that the auto-level 2.31 ms/pass figure applies to manual moves.**
- **...that per-tick sampling can share auto-level's buffer at a decimated
  cadence.** At a 100 ms cadence the 130-slot buffer spans 13 s against a
  1500 ms settle window, so the first `check_level()` would average pre-press
  history and could pick the first motor direction from it. Safety-relevant.
- **...that `tilt_fresh` can stay hardcoded 0 if Item 2 ships.** Today 0 is
  honest; with continuous sampling it becomes wrong in the other direction.
- **...that implementing `tilt_fresh` alone delivers the greyed-out UX.** The
  app-side `TODO(tilt-fresh)` must be reverted too.

---

## 2026-08-02 (later) — Process change: second handoff artifact in docs/handoffs/

**Commits: none.** Docs and config only by instruction; firmware still parked
at `29fdf0d` and no `.c`/`.h` touched. Everything below is uncommitted.

**Why.** This file is a summary written for the next session, and it keeps
that job unchanged. But it is also being uploaded to a reviewer outside the
repo, and a summary cannot serve as evidence of the work it summarises. The
2026-08-02 planning session produced a full plan; the reviewer received only
the log entry *about* it and issued a verdict anyway. That verdict is
withdrawn. A second artifact now carries the actual plans, diffs and raw
output.

**Changed (all uncommitted)**

- `docs/handoffs/` created, with `README.md` explaining the split.
- `docs/handoffs/fw_HANDOFF_2026-08-02_01_pitch-sign-tilt-sampling-plan.md` —
  retroactive; carries the planning session's plan **verbatim**, plus an
  explicitly-fenced expansion listing the rejected alternatives.
- `docs/handoffs/fw_HANDOFF_2026-08-02_02_handoff-artifact-process-change.md`
  — this session.
- `CLAUDE.md` — new "Session handoff file (required)" section, added
  immediately after the existing "Session handoff (required)" section. The
  existing section was **not** edited.
- `.claude/hooks/check-handoff-file.ps1` — new second Stop hook.
- `.claude/settings.json` — extended with a second Stop entry; the existing
  entry was not rewritten.

**VERIFIED**

- Both Stop hooks behave correctly. Exercised by piping synthetic payloads:
  handoff-file present → exit 0; run from a directory with no
  `docs/handoffs/` → exit 2 with the reminder; `stop_hook_active: true` →
  exit 0 (loop guard). Raw output in the `_02` handoff file, §d.
- `settings.json` parses and registers exactly two Stop entries, both
  pointing at scripts that exist.
- Firmware untouched: `git diff --stat 29fdf0d..HEAD` lists only `.claude/`,
  `.gitignore`, `CLAUDE.md`, `docs/SESSION_LOG.md`.
- The planning session's plan text was still in context, so `_01` is a
  **verbatim reproduction, not a reconstruction**. The two parts of `_01`
  that *are* reconstruction (end-of-session `git status`; the tool-call
  inventory) are marked as such inline.

**ASSERTED**

- That the hooks fire in a real session. They were pipe-tested, not observed
  firing — and `.claude/settings.json` changed mid-session, so the settings
  watcher may not pick the new entry up until a restart or a `/hooks` open.

**Open**

- Nothing is committed. Review, then commit.
- Seven review questions carried in `_01` §f, plus process questions in
  `_02` §f — chiefly that the hook checks only that a handoff file for today
  *exists*, so a second session on the same date is satisfied by the first
  session's file. Implemented as specified; flagged rather than tightened.
- Items 1 and 2 remain unimplemented and unapproved.
- The Release-config confirmation pass is still owed before Stage C ships.

**The next session must NOT assume**

- **...that `SESSION_LOG.md` is enough on its own any more.** Both artifacts
  are required and the Stop hook enforces both.
- **...that a pointer to a document satisfies the handoff file.** Plans go in
  inline and in full, even when they exist elsewhere in the repo. That
  substitution is the exact failure this change exists to prevent.
- **...that `docs/handoffs/` files are tracked.** They are new and
  uncommitted as of this entry.

---

## 2026-08-02 (later still) — Item 1 implemented; Item 2 recorded, not built

Plan review came back: **Item 1 APPROVED**, **Item 2 architecture accepted in
principle (O2.3) but NOT approved for implementation.**

**Commits**

- `140c37b` — Pitch on the BLE wire is POSITIVE = head raised. The only
  firmware commit. **Firmware is no longer at `29fdf0d`.**
- `de9c848` — DECISIONS #31 (prohibition on feeding auto-level's buffer from
  a decimated telemetry sampler) and #32 (telemetry tilt needs its own
  filter — open question).
- `9878d67` — Handoff spec: per-session hook check that fails closed, plus
  the process rulings.
- Plus the handoff commit that follows this entry.

**VERIFIED**

- The behavioural change is exactly one line —
  `out->pitch_angle_scaled = -baseline_accel_average_pitch_angle;`
  (`bed_status.c:105`). Everything else in `140c37b` is comments.
- The auto-level sampling path is untouched: `auto_level.c`'s diff is
  comment-only, and `accel_running_average()`, `read_accel()`'s logic and the
  sample buffer are byte-identical.
- Protocol version, checked rather than assumed: firmware serves `01` from
  `config/btconf/gatt_configuration.btconf:117`; the app's
  `kMaxSupportedProtocolVersion` is 1 and anything higher hits
  `tooNewRefuse` (`protocol_version.dart:17,36-37`). So bumping to `02` would
  break the current app for a change it needs no edit to absorb. Also: that
  value lives in a Studio-generated file, so a future bump goes through the
  configurator.
- The tightened Stop hook was exercised, five cases: session wrote a file
  today → 0; older session that never wrote one, files present on disk → 2
  (this is the tightening — it would have passed before); unparseable
  stdin → 2; missing transcript → 2; `stop_hook_active` → 0.
- No Item 2 code exists. `ble_bed_service.c` untouched in all three commits.

**ASSERTED — everything about whether Item 1 actually works**

- **Not built, not flashed, not measured.** Acceptance run: raise HEAD →
  pitch positive; FOOT → negative; LEFT → roll positive; RIGHT → negative.
- That it compiles. A unary minus and comment blocks, but nothing compiled it.
- The overflow analysis (−28500..+10500 against INT16_MIN).
- That no regression re-run is needed. Reasoned from what `bed_status_get()`
  touches — a pure read of already-computed state, no timing or safety path —
  not tested.
- **Roll's convention.** `POSITIVE = LEFT raised` is read off `auto_level.c`'s
  frame diagram, which describes the intended mounting rather than measuring
  it. The bench run that caught the pitch sign never checked roll, and that
  silence is not evidence. Written into `BLE_GATT_DESIGN.md` §6 as
  **PROVISIONAL**, mirrored in `bed_status.h`'s field comment.

**Open**

- The four-direction sign acceptance run. Item 1 is ASSERTED until it passes,
  and the roll line stays PROVISIONAL until then.
- `REGRESSION_RECORD_PRE_STAGEC.md` is signed against `29fdf0d` and is now one
  commit stale. The commit message says so and says why no re-run is required;
  the record itself has no pointer yet.
- **The Release-config confirmation pass is still owed before Stage C ships.**
- Item 2: unimplemented. Rulings captured in
  `fw_HANDOFF_2026-08-02_03` — piggyback the 1 Hz notify timer; `tilt_fresh`
  means "age of last SUCCESSFUL sample", ~3 s threshold, keep hysteresis; app
  revert only after 2(a) and 2(b) both land and are bench-verified.
- Five review questions in that handoff's §f.

**The next session must NOT assume**

- **...that firmware is still at `29fdf0d`.** It is at `140c37b` or later, and
  the signed regression record predates it.
- **...that Item 1 is proven.** One line, well argued, entirely untested.
- **...that roll's sign is established.** It is a hypothesis with a marker on
  it. Do not build client behaviour on it.
- **...that Item 2 may be implemented.** Accepted in principle is not
  approved. DECISIONS #31 is a prohibition, not a design.
- **...that the two tilt values Item 2 will create should be unified.** That
  is DECISIONS #31's whole subject — read it before touching it.
- **...that a handoff file left by an earlier session satisfies the Stop
  hook.** It now requires a file written by *this* session, and it fails
  closed.

---

## 2026-08-02 (fourth) — Live tilt telemetry + first Command write path

**Commits**

- `83cc628` — Live tilt telemetry: separate sampler, four triggers, first
  Command write. Plus the handoff commit that follows this entry.

Superseded the earlier motors-on-only scope: the sampling window is now
opened by four triggers (subscriber 0→1, REFRESH_TILT, motors any-on→all-off,
auto-level).

**VERIFIED — static only, nothing ran**

- A new `.c` file **cannot** be added without the Studio configurator:
  `cmake_gcc/tilt_bed_controller_project.cmake` lists sources explicitly and
  is generated. That is why the sampler lives in `auto_level.c` rather than a
  new module.
- `sl_bt_evt_gatt_server_attribute_value_t` fields checked against the
  installed SiSDK 2026.6.0 header (`sl_bt_api.h:10398-10407`) before the
  handler was written; `gattdb_command_result` and the 4-byte Command length
  checked in `autogen/gatt_db.h`.
- The stack callback does nothing but copy four bytes and set a flag. All
  decisions are in `process_pending_command()`, main-loop context.
- Telemetry never touches auto-level's filter state: `auto_level_filter`
  appears only inside `accel_running_average()`. No file-scope accel buffer,
  sums or index remain (grep returns empty).
- `tilt_fresh` and the flags byte are untouched.
- The sampler neither samples nor arms without a subscriber, so a phone-less
  bed keeps its exact previous behaviour including never vetoing sleep.

**ASSERTED — all of it**

- **Not built, not flashed, not measured.** No compiler has seen this.
- **That the `read_accel()` split leaves auto-level unchanged.** The LPF moved
  out of `read_accel()` into `accel_filter_update()` so each chain filters
  into its own accumulator. The equivalence argument is a side-by-side
  transcription check, by inspection only. This is in the safety path and is
  the single claim most worth disproving on the bench.
- That 500 ms is enough for the 130-slot filter to fill — arithmetic on a
  loop rate measured on the *auto-level* path, which #29 says not to
  extrapolate.
- Live update, post-move settle, the write path, the sleep veto, and the
  absence of new `0x1008` drops: all unobserved.

**Open**

- The full bench acceptance, written out step-by-step in
  `fw_HANDOFF_2026-08-02_04`. It includes a **before/after loop-rate
  measurement on the manual-move path** — no such figure has ever existed —
  and requires a temporary pass counter, since the #30 instrumentation was
  stripped. The BEFORE measurement must be taken on `23b84aa` before flashing
  the new firmware, or it is unrecoverable.
- **REGRESSION.md §5 collision, re-run on a manual move, before the long
  soaks.** Collision timing is loop-pass calibrated, so the loop rate moving
  *is* a change in collision behaviour. Sequenced first deliberately.
- Six review questions in that handoff's §f.
- The Release-config confirmation pass is still owed before Stage C ships.

**The next session must NOT assume**

- **...that any of this works.** It is unbuilt. A build succeeding would still
  not make it verified.
- **...that auto-level is untouched.** `read_accel()` was split. The change is
  argued to be equivalent; it has not been demonstrated to be.
- **...that the two tilt values may be unified.** DECISIONS #31 still governs;
  #33 is the positive half and says why the duplication is deliberate.
- **...that motion over BLE now works.** One opcode exists, REFRESH_TILT, and
  it commands nothing. 0x01–0x06 answer REJECTED_INVALID.
- **...that more than one Command write can be in flight.** One slot; a second
  write overwrites the first. Fine for REFRESH_TILT, not for MOVE_START.
- **...that `tilt_fresh` means anything yet.** Still hardcoded 0, and the
  app's `TODO(tilt-fresh)` ungating is still correct.

---

## 2026-08-02 (fifth) — Stage C: the app drives the bed

**Commits**

- `ab12d09` — Stage C: all six motion opcodes, dead-man watchdog, source
  arbitration. Plus the handoff commit after this entry.

**VERIFIED**

- **It compiles clean.** Whole app layer, zero warnings, links at 240956
  text / 780 data / 72480 bss. `base` config (the only one this project
  defines — not Release).
- **The previous commit's tilt telemetry compiles too.** `auto_level.c` did
  not rebuild on the first build, so it was forced to; it is clean. That
  retires `fw_HANDOFF_..._04`'s ASSERTED "it compiles".
- **Routing opcodes through `bed_control` alone would NOT have moved the
  bed.** `bed_move_start()` latches direction and `active_switch`, but the
  thing that energises a motor is `switch_input.c`'s `process_outputs()`
  (`switch_input.c:149-218`), driven by `app_ui_interrupt` + `switch_pressed`
  — a physical switch edge an app command does not produce. The app path now
  applies its own outputs via helpers in `bed_actions.c`.
- **`active_switch` is what releases the source lock** (`app_bed_is_idle()`,
  `bed_actions.c:222`; `bed_control.c:30-36`), and no switch-release event
  ever arrives for an app command. Without `app_release_active_switch()`, one
  app LIGHTS command would leave APP holding the lock forever and every wall
  switch press force-stopped.
- **Bed Status needed no code.** `bed_status.c:99,101` already read
  `app_active_command()` and `bed_active_source()`; those fields only ever
  showed the switch path because nothing had called the verbs with
  `BED_SOURCE_APP`. Traced, not assumed.
- No work happens in any stack callback: the write handler copies bytes, the
  disconnect handler sets a flag, the watchdog timer callback sets a flag.

**ASSERTED — all behaviour**

- **That the bed moves from the app at all.** Never flashed. A clean build
  proves the types line up and nothing more.
- That the output helpers energise the right motors and LEDs — transcribed
  from `process_outputs()` by eye.
- That the source lock actually releases after an app command. If this is
  wrong the wall switch stops responding; acceptance steps (a) and (h) are
  the check.
- That the watchdog stops the bed within ~300 ms of link loss, and that the
  disconnect stop fires and ends a latched auto-level.
- That live tilt keeps updating during app-driven movement. The sampler gates
  on "a motor is energised" and should not care who started it — still an
  assumption, and acceptance step (j) exists to test it rather than assume it.

**Open**

- Full bench acceptance, steps a–k in `fw_HANDOFF_2026-08-02_05`. Step (g),
  the watchdog link-drop test, is the one that matters most; step (i) is
  skippable with one phone.
- Five review questions in that handoff's §f — chiefly that the deferred
  disconnect stop costs up to one loop pass (~2.3 ms) before motors stop, and
  that `app_apply_*_outputs()` now duplicates `process_outputs()`'s branches
  by transcription and can drift.
- **Manual-move loop-rate measurement still owed**, carried from the previous
  session and now more relevant — this change adds I2C work to the command
  path.
- **The Release-config confirmation pass is still owed before Stage C ships.**

**The next session must NOT assume**

- **...that Stage C works.** It builds. Nothing has been on hardware.
- **...that a clean build says anything about the watchdog, the disconnect
  stop, or arbitration.** Those three are the least like anything previously
  run on this bed.
- **...that `process_outputs()` and the app helpers are one implementation.**
  They are two, kept in step by hand, side by side in `bed_actions.c` and
  `switch_input.c`.
- **...that the app can be locked out safely by leaving `active_switch` set.**
  It is the source-lock release mechanism; see the VERIFIED note above before
  touching it.
- **...that `tilt_fresh`, the pairing window, or #29's loop-pass constants
  were touched.** All explicitly out of scope and untouched.

---

## 2026-08-02 (sixth) — Stage C stutter root-caused; watchdog and arbitration fixed

**Commits**

- `a22406f` — Fix the 10 Hz app-move stutter. Plus the handoff commit after
  this entry.

**Root cause (VERIFIED as a code fact; its match to the symptom is ASSERTED)**

`switch_input_tick()`'s **double-press error check** was running on
app-sourced moves. It is a physical-panel consistency check: it compares
`app_ui_status` — latched at the last *physical* switch interrupt
(`switch_input.c:397`, the sole writer) — against a fresh read 50 ms later,
and on a **match** kills all four motors and sets `active_switch = OFF`
(`switch_input.c:363-365`). Its only guard was `active_switch != OFF`
(`:333`), and an app move sets `active_switch` through the same
`motor_button_pressed()` the switch path uses (`bed_actions.c:45`).

So: repeat → motors on → check fires ~50 ms later → motors off,
`active_switch = OFF` → watchdog idle branch clears `app_move_active` → next
repeat 100 ms later starts over. The app's repeat interval is the rate
limiter, hence exactly 10 Hz.

**The asymmetry is the LED bits.** The status word includes the LED enable
lines (`switch_input.c:62-69`, P1_4–P1_7). That is what normally makes a
legitimate physical hold safe — the latch happens before `process_outputs()`
lights the LED, so the re-read differs. It is also why the bug was selective:
the latched value freezes whichever LED was lit at the last physical
interrupt, so a later app move matches only if it lights the same one. Green
= single-side, blue = ALL. Auto-level was never affected because
`(!auto_level_active)` was already on that line — which the code predicted
for free, and is the strongest confirmation of the mechanism.

**It survived closing the app** because `app_ui_status` is only rewritten by
a physical switch interrupt. **This corrects the brief's guess** that the
missing watchdog explained the persistence — the watchdog re-armed on every
repeat. The latched status word is what survived.

**Fix:** gate the check on `bed_active_source() == BED_SOURCE_SWITCHES`.
`run_motor_supervision()` deliberately left OUTSIDE that gate — gating the
whole block would have silently removed collision and limit supervision from
app moves.

**`active_switch` was NOT split per source**, deliberately. Every other reader
behaves correctly when the app writes it; exactly one reader was wrong, and it
was wrong because it lacked a predicate, not because the variable is shared.
Splitting would touch six files across the safety path and lose the Bed Status
property. Full reasoning in the handoff §b.5.

**Also fixed**

- **Watchdog silent disarm.** Now cancels only on collision; on plain idle it
  clears `app_move_active` but **leaves the watchdog armed** — it guards the
  link, not the bed.
- **Two-source conflict stops instead of handing over.** New
  `TWO_SOURCE_LOCKOUT_MS` = 500 ms (sleeptimer, bench-tunable, #2). Armed on a
  force-stop and on the bed going idle under a move the app believed it held
  (which covers the switch-interrupts-app direction). Stops are never locked
  out.

**VERIFIED**

- Builds clean, `base` config, zero warnings.
- A wrong theory was falsified before committing to the right one: I suspected
  `get_ui_status()` overwrote the global it is compared against, which would
  make the comparison always-true. It uses a local (`exp_ui.c:253-258`); the
  line that looks like the assignment is inside a comment block
  (`switch_input.c:56-58`).

**ASSERTED**

- **That this is the root cause of the observed stutter.** Derived from code.
  It explains all six observations, both halves of the asymmetry, and the
  persistence — but nothing has been run.
- That the gate removes the stutter, the watchdog survives a disputed idle,
  and the lockout stops the takeover. All unrun.
- 500 ms is chosen, not measured.

**Open**

- **Acceptance step 0 confirms the root cause with no new firmware**: the
  existing `printf("Double Press Error\r\n")` should be repeating at ~10 Hz in
  the failing log. If it is absent, the diagnosis is wrong.
- Steps a–h in `fw_HANDOFF_2026-08-02_06`. Step (g.5) and (g.6) are the
  regression checks this fix could plausibly have caused: does the *real*
  physical double-press still error, and does collision still trip on an app
  move.
- Four review questions, chiefly whether the lockout should expire (current)
  or latch until MOVE_STOP.
- Manual-move loop-rate measurement still owed; Release-config pass still
  owed.

**The next session must NOT assume**

- **...that the stutter is fixed.** The root cause is argued from code and
  compiles; nothing has been on hardware.
- **...that the double-press check is now sound.** It still relies on the LED
  bits changing between latch and re-read to avoid false positives on
  legitimate physical holds. That is incidental, not designed, and untouched.
- **...that gating the block on source was safe by inspection.**
  `run_motor_supervision()` shares that block; if a future edit moves the gate
  outward, app moves lose collision protection silently.
- **...that the lockout latches.** It expires after 500 ms and a held app
  button will then restart the bed.
  *(Superseded 2026-08-02 by #36 — the timed lockout is gone and the
  replacement does latch. See the next entry.)*

---

## 2026-08-02 (seventh) — Auto-level hold-to-run; conflict latch replaces the timed lockout

**Commits**

- `5351aef` — Auto-level hold-to-run; two-source conflict becomes a latched
  inhibit. Plus the handoff commit after this entry.

The #35 stutter fix is bench-confirmed and was not touched — the double-press
gate is unchanged and `run_motor_supervision()` is still outside it.

**VERIFIED — executed, not merely compiled**

- **The host unit test was extended and all 30 checks pass** (`-Wall`, zero
  warnings). Eleven are new and cover the latch: a conflict latches it;
  **both** sources are refused while latched, not just the app (scenario 5c —
  this is the one that justified putting the latch in `bed_control.c` rather
  than the BLE layer); auto-level start is inhibited too; one source
  releasing is not enough; it clears only when both release; a fresh press
  then works; stops and lights are never inhibited.
- Every pre-existing arbitration check (scenarios 1–4) still passes.
- Firmware builds clean.
- `TWO_SOURCE_LOCKOUT_MS` is fully removed, not left dormant (grep returns
  nothing).
- **Hold-to-run auto-level cannot loop on completion.** Checked rather than
  assumed: `app.c:630-636` clears `auto_level_active` and lights green but
  leaves `active_switch` at `SWITCH_AUTO_LEVEL_VALUE`, so the bed is not idle,
  the watchdog's idle branch does not fire, and repeats simply refresh it.

**ASSERTED**

- Hold-to-run feel, prompt stop on release, and the LED behaviour during a
  conflict. Never run.
- **The watchdog's unique coverage is not really testable at the bench.**
  Switching Bluetooth off produces a clean *disconnect*, which the disconnect
  handler already covered. The case the watchdog uniquely adds is a link that
  **stalls without disconnecting**, which is hard to produce deliberately.
  Acceptance step (c) proves the watchdog is wired in and does not misfire —
  not the stall case itself.
- That the physical→app direction behaves on hardware. The host test proves
  the arbitration; interrupt timing and the `process_outputs()` interaction
  are not modelled.

**Open**

- Acceptance steps a–l in `fw_HANDOFF_2026-08-02_07`. Steps (e) and (h) are
  the two that would most likely expose a mistake: (e) is the case the old
  timed lockout got wrong, and (h) is the LED decision.
- Four review questions, chiefly that **`STOP_ALL` does not actually turn the
  under-bed lighting off** — it clears the amber LED but never calls
  `set_underbed_lighting_off()`. That predates this change and was left alone,
  but it means "STOP_ALL turns everything off" is not currently true.
- Manual-move loop-rate measurement still owed (three sessions now).
- Release-config pass still owed before Stage C ships.

**The next session must NOT assume**

- **...that a refused command may be ignored.** A refused MOVE_START must
  still refresh the app's half of the latch. Without it the 300 ms silence
  timeout expires mid-hold, the latch clears, and the next repeat restarts the
  bed — the exact behaviour #36 removes. Host scenario 5f guards this.
- **...that the inhibit only applies to the app.** A physical press while
  latched is refused too. That is why it lives in `bed_control.c`.
- **...that auto-level is latched.** It is hold-to-run and watchdogged as of
  #36; anything in older text saying AUTO_LEVEL never arms the watchdog is
  superseded.
- **...that a repeat may call `bed_auto_level_start()`.** It resets the
  levelling state machine; at 10 Hz levelling would never progress.
- **...that a limit stop pauses anything now.** #35's idle branch armed the
  500 ms lockout; it no longer does, so a held app button restarts the move
  after a limit stop. Raised as review question 3.

---

## 2026-08-04 — CORRECTION: the entry above is dated wrong

**Commits:** the correction commit that follows this entry. No code change.

**The entry above is headed "2026-08-02 (seventh)". It ran on 2026-08-04.** I
carried the date forward from the previous sessions in the same conversation
without checking the clock. Corrected here rather than by editing that entry,
per this file's own append-only rule.

**What was actually wrong, and what was not.** `git log --date=short` is the
evidence:

```
fef5323 2026-08-04 Handoff for 5351aef: ...
5351aef 2026-08-04 Auto-level hold-to-run; two-source conflict becomes a latched inhibit
e0d764b 2026-08-02 Handoff for a22406f: ...
...
140c37b 2026-08-02 Pitch on the BLE wire is POSITIVE = head raised
```

So only the last session was misdated. Every earlier entry and every handoff
file `_01` through `_06` genuinely ran on 2026-08-02 and is correctly named.

**The handoff file was misnamed and has been replaced.**
`fw_HANDOFF_2026-08-02_07_holdtorun-autolevel-and-conflict-latch.md` is
deleted; `fw_HANDOFF_2026-08-04_01_holdtorun-autolevel-and-conflict-latch.md`
carries the identical body plus a correction note. Deleting rather than
keeping both avoids two copies of one session's evidence in a
date-ordered directory.

**How it was caught: the Stop hook.** This is the first time
`check-handoff-file.ps1`'s per-session, fail-closed check has actually caught
something. It blocked the session because no file matching *today's* date had
been written. The looser version originally built — existence-only, any file
for today — would have passed silently here, and the misdated file would have
gone to the reviewer under the wrong date. Worth recording as evidence that
the tightening was worth doing.

**The next session must NOT assume**

- **...that a date carried over from earlier in a conversation is today's
  date.** Check it. Handoff filenames are date-keyed and the reviewer orders
  by them.
- **...that entry headings in this file are reliable dates.** The one above is
  wrong and is corrected here, not rewritten.

---

## 2026-08-04 (second) — Split active_switch; runaway root-caused but NOT closed

**Commits**

- `d9649a9` — Split active_switch: give the physical panel its own held-button
  state. Plus the handoff commit after this entry.

**The runaway is NOT closed. Read this before assuming it is.**

The *mechanism* is confirmed: `process_outputs()` runs from the end of
`process_inputs()` (`switch_input.c:297`) and its press arm energised on
`active_switch == value` — a value the **app** could have written. So a
physical press that `process_inputs` had just discarded still reached
`set_dual_motors_on()` with `bed_control` never informed. Compounded by two
things: the release conditions were asymmetric (`process_inputs` accepted
`|| active_switch == OFF`, `process_outputs` did not, and that arm is the
panel's only hardware de-energise), and `run_motor_supervision()` sits inside a
block gated on `active_switch != OFF`, so motors energised while it is OFF have
no supervision and nothing to stop them.

**But I could not construct a sequence I am confident reproduces the single
observed event** — every candidate ended with some other path calling
`set_all_motors_off()`. The split removes the class; it is not proof the
observed runaway is gone, and that observation was intermittent to begin with.

**VERIFIED (code facts)**

- `process_inputs`' press guard `active_switch == OFF` discarded every physical
  press while the app was moving (pre-change `switch_input.c:277`) — bench
  failures (d)/(e), and the "same button" exception falls straight out of the
  release branch still matching.
- Stale limit flags **block** `set_dual_motors_on()`
  (`dual_motor_control.c:330,338,349,357,368`), and the clear branch only runs
  while a motor is on **and moving** (`:232-238`). That is the "bed stops
  responding" half of the auto-level lockup and why a manual tap cleared it.
- `switch_held`/`switch_driving` are `static` in `switch_input.c` with no
  accessor — the app cannot write them.
- Firmware builds clean; host unit test still 30/30 (`bed_control.c` unchanged,
  so that is a regression check, not new coverage).

**ASSERTED**

- Everything behavioural. Nothing flashed. In particular A1: **the runaway is
  not claimed fixed.**
- Normal panel operation is the biggest regression risk — the panel's
  press/release matching variable changed. Acceptance step (i) exists for that.

**Open**

- Acceptance steps a–j in `fw_HANDOFF_2026-08-04_02`. Step (g) is a ~50-tap
  runaway hunt across four source states; pass 4 (app holding the same button
  being tapped) is the only one that exercises the confirmed mechanism.
- Four review questions: the `active_switch == OFF` **supervision gap is still
  open** (closing it means touching the #35 gate's enclosing condition); the
  auto-level path can still blip amber via `app_auto_level_stop()`'s
  `set_all_LED_off()`; clearing all limit flags on every stop is deliberately
  blunt; a refused press still occupies the panel's held slot until released.
- Release-config pass and the manual-move loop-rate measurement still owed.

**The next session must NOT assume**

- **...that the runaway is fixed.** The class is removed; the event was never
  reproduced. Treat it as open.
- **...that `active_switch` answers "is a physical button held?".** It never
  did. `switch_held` does. Two readers got that wrong (#35, #37); a third
  would be a pattern, not a coincidence.
- **...that motors energised while `active_switch == OFF` are supervised.**
  They are not — `run_motor_supervision()` is inside a block gated on it.
- **...that `bed_status.c` needed changing.** It did not; `active_switch`
  remains the command variable.

---

## 2026-08-05 — CORRECTION: the entry above is dated wrong, for the second time running

**Commits:** the correction commit that follows this entry. No firmware change.

The entry above is headed "2026-08-04 (second)". **It ran on 2026-08-05.**
`git log --date=short` is the evidence — `d9649a9` and `8724e74` are both
2026-08-05, while `5351aef`/`fef5323`/`c706893` are 2026-08-04.

The handoff file was misnamed twice over: wrong date **and** wrong sequence
number (it was the first session of 2026-08-05, so `_01`, not `_02`).
`fw_HANDOFF_2026-08-04_02_split-active-switch-and-runaway.md` is deleted and
replaced by `fw_HANDOFF_2026-08-05_01_split-active-switch-and-runaway.md`,
body identical plus a correction note. `fw_HANDOFF_2026-08-04_01_*` is
correctly named and untouched.

**This is the second consecutive session with the same error, and the previous
correction predicted it.** Its own "must NOT assume" line said not to carry a
date forward. That did not work, because the failure is not a belief about the
date — it is that the filename gets written at the end of a long session by
copying the previous session's name and incrementing `<NN>`, which inherits
the date silently and never surfaces as a decision.

**So the fix this time is in `CLAUDE.md`, not in a resolution.** The handoff
filename section now says explicitly: derive the date from the system date at
the moment of writing, never from a previous filename, a previous log entry,
or earlier in the conversation; and `<NN>` restarts at `01` on a new date
rather than continuing the previous day's numbering.

**The Stop hook caught it both times.** That is twice now that
`check-handoff-file.ps1`'s per-session, fail-closed check has been the only
thing standing between a misdated artifact and the reviewer. The
existence-only version originally built would have caught neither.

**The next session must NOT assume**

- **...that entry headings or handoff filenames in this repo are reliable
  dates without checking.** Two are wrong and corrected by later entries.
- **...that the `CLAUDE.md` rule makes this impossible.** It makes it a
  written step; the hook is still the only thing that enforces it.

---

## 2026-08-05 (second) — Double-press misfire, arbitration bypass, motion fail-safe

**Commits**

- `165fea1` — Fix the double-press misfire, the state it desynced, and add a
  motion fail-safe. Plus the handoff commit after this entry.

**Context correction accepted:** the runaway reproduction was taken on the
**pre-split** build. It corroborates the mechanism documented in #37 rather
than showing a second path; #37's posture stands as written.

**VERIFIED (code facts)**

- **The misfire is mine, from #35.** The check is only sound 50 ms after the
  latch, because the LED bits changing in between are what make a legitimate
  single press differ from its own latched status. #35 wrote the consume as
  `if (timeout && switch_sourced_move) { timeout = false; ... }` — gate inside
  the clear — so whenever the source was not SWITCHES the flag went stale and
  was consumed at an arbitrary later moment, comparing a steady state against
  an edge. `app_reset_double_press_timer()` compounded it by stopping the
  timer without discarding a pending expiry. The timer is periodic.
- **Observation 1 confirmed.** `run_motor_supervision()` is inside a block
  gated on `active_switch != OFF` (`switch_input.c:333`); the handler set
  `active_switch = OFF`, so supervision stopped and the app faithfully reported
  0 mA while motors ran. This is #37's §b.1 fact 2 on the bench.
- **Observation 2 is BOTH mechanisms, and (b) is confirmed.** With
  `switch_held` stale, the press guard fails for **every** button, so
  `bed_move_start()` was never called for LEFT DOWN either — yet LEFT DOWN
  moved the bed. The only remaining energise path is `process_outputs()`'
  press arm on `switch_driving`. **That is motion with `bed_control` bypassed
  entirely**, and it got its own fix rather than being folded into the handler
  cleanup.
- Firmware builds clean; host unit test 30/30.

**Recorded permanently in DECISIONS #38, worth repeating here:** motors
visibly running with **0 mA on all four** in the app means supervision is not
running. It is a live diagnostic for the most dangerous state this system has.

**ASSERTED**

- All of it — nothing flashed.
- **The fail-safe is untested in the direction that matters.** It has never
  been shown to catch a real runaway, only (in principle) to stay quiet. And
  the most likely thing to be wrong is that it misfires in normal use, which
  is why acceptance step (e) runs first.
- `SWITCH_INPUT_MASK` = `0xFF0F` is derived from the bit-map comment in
  `switch_input.c`, not from a schematic. If it is wrong the fail-safe either
  never fires or fires constantly.

**Open**

- Acceptance steps a–h in `fw_HANDOFF_2026-08-05_02`; **step (e) first**.
- **Supervision should probably not be gated on `active_switch` at all.** A
  gate that switches off collision detection is the wrong shape, and this one
  can be cleared by an error handler — which is what happened. Not changed
  because that gate also encloses the bench-confirmed #35 check. Recorded as
  the next thing to fix in this area.
- Four review questions, including the 150 ms fail-safe confirm window and the
  switch mask.
- Release-config pass and manual-move loop-rate measurement still owed.

**The next session must NOT assume**

- **...that the fail-safe protects anything yet.** It compiles and is wired in;
  it has never caught a runaway and has never been shown not to misfire.
- **...that `process_outputs()` energising implies `bed_control` approved.**
  It did not, until this commit. `switch_driving` is now cleared at the start
  of every press event, which is what makes the bypass unreachable.
- **...that clearing `active_switch` is a safe thing for an error handler to
  do.** It silently disables supervision.
- **...that the double-press check is sound.** Its correctness still rests on
  LED bits changing between latch and re-read, which is incidental rather than
  designed. Only its flag lifecycle was fixed.

---

## 2026-08-05 (third) — Bench-only runaway provocation; fail-safe cleanup gaps

**Commits**

- `732dfed` — Bench-only runaway provocation to prove the fail-safe, plus two
  cleanup gaps. Plus the handoff commit after this entry.

Closes the *means* to test #38's assumption A4. **It does not close A4.**

**VERIFIED**

- **The provocation would NOT have worked as first written.** The flag was
  defined next to the fail-safe near the end of the file; the trigger sites are
  in `process_inputs()`, earlier, so `#if BED_BENCH_PROVOKE_RUNAWAY` there
  evaluated an undefined macro to 0 and compiled them out.
  `-Wunused-function` on `provoke_arm` is the only thing that exposed it.
  **The failure would have looked exactly like success** — flag on, clean
  build, tap the button, no runaway, no print, obvious reading "the fail-safe
  works". A bench session would have produced a false VERIFIED. Flag moved to
  the top of the file; definitions now precede uses (grep in the handoff §d.3).
- Both flag states build clean and warning-free; the flag is committed `0`.
- The provocation energises through the same calls the real paths use
  (`set_dual_motors_on(HEAD)` / `set_all_motors_on()`).
- **Fail-safe trip state, reviewed from code.** Already correct: outputs,
  `motor_state` flags, movement LEDs, `switch_held`, `switch_driving`,
  `active_switch`. `app_move_active` cannot be stale — it is a precondition of
  the fail-safe running. `active_source` self-releases on the next
  `bed_control_tick()`.
- **Two real gaps, both closed:** the switches' half of the #36 latch (clearing
  `switch_held` means a later release never reaches `bed_move_stop()`, so
  `source_commanding[SWITCHES]` would stay set and the inhibit latch forever),
  and the inferred limit flags that every other stop path already clears.

**ASSERTED**

- **A1: the fail-safe catches a runaway. STILL UNPROVEN.** This session built
  the test and did not run it.
- That the provocation energises on hardware. Note `set_dual_motors_on()`
  refuses at a limit flag, so a bed at its upper limit produces no runaway and
  **looks like a pass** — the instructions say to start mid-stroke.
- ~150–200 ms / 1–3 mm of travel is arithmetic plus an assumed actuator speed;
  the measured number comes from the `PROVOKE:` print.

**Open**

- **Run the provocation.** Handoff `fw_HANDOFF_2026-08-05_03`, steps 1–8,
  including turning the flag back off and confirming with `git diff --stat`,
  and reflashing the normal build afterwards.
- If it does not fire, the first suspect is `SWITCH_INPUT_MASK` — still
  unverified against a schematic, carried from #38.
- Ungating supervision from `active_switch`, still open from #38.
- Release-config pass and manual-move loop-rate measurement still owed.

**The next session must NOT assume**

- **...that the fail-safe works.** It has never caught a runaway. The
  provocation exists; that is all.
- **...that a bench flag being ON means it is doing anything.** Build with it
  on and read the warnings. This one silently did nothing, and its failure mode
  was indistinguishable from the hoped-for result.
- **...that the provocation build is safe to leave on the bed.** It drives a
  motor pair every time UP HEAD or UP ALL is tapped.
- **...that a clean `git diff` on `switch_input.c` was checked automatically.**
  Nothing enforces the flag being off except reading it.

## 2026-10-17 — git housekeeping, first push in over two months

**Commits**

- `de48e7f` — CLAUDE.md: standing rule to push to origin/main at end of every session.

**VERIFIED**

- origin/main had 0 commits not in local; local was 40 ahead; no divergence.
- `git push` succeeded (6dcb262..de48e7f), exit 0; status then said up to date.

**ASSERTED**

- Nothing beyond the above; no source touched, no bench work.

**Open**

- Decide whether `.projectlinkstore` (tracked, dirty), `.projectchecksums` and `tilt_bed_controller_project.pdf` belong in .gitignore. Recommended yes; left untouched.
- All firmware items from 2026-08-05 #03 remain open, notably running the fail-safe provocation.

**The next session must NOT assume**

- ...that the fail-safe is proven; this session did nothing to that.
- ...that pushing is optional: CLAUDE.md now requires it, never forced.

## 2026-10-17 — provocation results: fail-safe blind to ALL, double-press misfires, swallowed switch events

**Commits**

- `3f0d409` — fail-safe reads motor enables from hardware; double press = two switches down; swallowed-event recovery; DECISIONS #40. Bench flag committed as 0.

**VERIFIED**

- Bench log (read in full): HEAD provocation caught 11/11 (213-216 ms); ALL provocation caught 0/5.
- `set_all_motors_on()` sets no `motor_state[]` flags, so the old flag-based check could not see it (code).
- PWM is started once and never stopped (`app.c:414-417`); no debounce exists in firmware (grep).
- Builds clean with the flag at 0 and at 1.

**ASSERTED (not run on the bench)**

- That the fail-safe now catches the ALL provocation.
- That reading the input port clears the expander's interrupt source, so out-of-band reads can swallow a release; basis is a code comment (`exp_ui.c:263`), not the datasheet. This is my explanation for the unprovoked `FAILSAFE:` and possibly the original runaway.
- That bounce and the auto-level exit are the false double-press causes.

**Open**

- Run the bench acceptance in `fw_HANDOFF_2026-10-17_02_*`. Watch for `UI: recovered swallowed switch change`: if it never appears the swallow hypothesis is wrong.
- No debounce on the main input path (a bounce can still cause a brief stop/restart).
- Which board the physical controller is (`ui_int_source` handling); `SWITCH_INPUT_MASK` vs schematic; ungate supervision from `active_switch`; Release-config pass; loop-rate measurement.

**The next session must NOT assume**

- ...that the unprovoked trip is explained: it is inferred, not observed.
- ...that the fail-safe is proven for ALL moves until the bench says so.
- ...that the provocation flag is off on the bed; check `switch_input.c:91` and `git diff --stat`.

## 2026-10-17 — status check only (git status / git log), nothing changed

**Commits**

- None. HEAD stayed at `6b35be8` (Handoff 03: note session was interrupted before section (b)).

**VERIFIED**

- `git status` and `git log --oneline -3` were run and their output is in `fw_HANDOFF_2026-10-17_04_status-check-only.md`: branch `main`, 3 commits ahead of `origin/main`, `.projectlinkstore` modified (unstaged), `docs/SWITCH_SAMPLER_DESIGN.md` untracked.

**ASSERTED-but-untested**

- Nothing. No source was touched, so there is nothing to build or bench-test.

**Open**

- `.projectlinkstore` is still dirty and `docs/SWITCH_SAMPLER_DESIGN.md` is still untracked. This session neither committed nor discarded them; they are not mine.
- Everything open in the earlier 2026-10-17 entries is unchanged (bench acceptance of the fail-safe fix, single-switch sampler plan review).

**The next session must NOT assume**

- ...that this session reviewed or touched the two uncommitted paths above.
- ...that anything about firmware behaviour was learned here.

## 2026-10-17 — switch sampler implemented (one switch reader, DECISIONS #41)

**Commits**

- `d1f13d0` — One switch reader: only the new sampler (switch_input.c) reads the UI input ports via `ui_read_inputs()`; 10 ms sampling with 3-sample debounce; edges delivered one bit at a time, releases first, no presses while 2+ down; re-driver reference count (`ui_redriver_acquire/release`); motor-enable shadow (`motor_outputs_maybe_on()`); sleep vetoed by sampler activity and the shadow; fail-safe and double-press check use the debounced state; DECISIONS #41; adds `docs/SWITCH_SAMPLER_DESIGN.md`.
- The handoff file `docs/handoffs/fw_HANDOFF_2026-10-17_05_one-switch-reader-sampler.md` and this entry are committed together after it.

**VERIFIED**

- Builds from the command line with no warnings and no errors: flag 0, flag 1 (compile check), and a final flag-0 rebuild. Raw output is in the handoff, d.3.
- `BED_BENCH_PROVOKE_RUNAWAY` is 0 in the committed source (`switch_input.c:98`); the last image in `cmake_gcc/build/` is the flag-0 one.
- The design's §3a greps pass: `EXP_CMD_READ_INPUTS` is issued only inside `ui_read_inputs()`, which is called only from the sampler; none of the removed names remain in `.c/.h`; the re-driver is switched only by acquire/release.
- Every port-0 writer in `exp_board.c` updates the motor-enable shadow (grep in the handoff).
- Every sleeptimer callback found in the firmware only sets a flag; `sl_button_on_change` is flag-only.
- Both I2C buses are standard mode, 100 kbit/s (`config/sl_i2c*_config.h:49`).

**ASSERTED-but-untested (nothing was run on the bench)**

- All behaviour: that two-button presses/releases now work, that the fail-safe no longer trips on a normal release, that the debounce values are right, that the shadow/sleep veto removes the ALL-MOTORS slowness, that BED LIGHTS settings entry still works.
- That `SL_SIMPLE_BUTTON_PRESSED` means UI_INT low and low means asserted (kept from the old code; not checked against the schematic).
- The §3c blocking-time table is arithmetic from a 100 kHz byte-time model, not a measurement.
- That nothing calls the re-driver from a non-main-loop context: timer callbacks verified by grep; BLE dispatch context taken from a comment; the ADC ISR was not inspected.

**Open**

- The bench checklist in the handoff (b.12) needs turning into steps and running: single press, two-button press and release, second press during a move, BED LIGHTS 5 s, the provoked runaway with the flag hand-flipped to 1 (and back to 0).
- Decisions for the reviewer, listed in the handoff under QUESTIONS FOR REVIEW: the PRESSED gate on `sl_button_on_change`, the read-failure counting rule, release latency (~20-30 ms added by the debounce), behaviour on a bench with no UI boards or a stuck UI_INT, `SWITCH_SAMPLER_DIAG` left at 1 (set 0 pre-ship).
- Not traced: whether the double-press path leaves the bed_control switch latch set (it does not call `bed_note_source_released`); `bed_actions.c` still includes `switch_input.h` unused.
- `.projectlinkstore` is still modified and uncommitted (the design says never commit it).

**The next session must NOT assume**

- ...that the sampler works: it builds, nothing more.
- ...that the repo state matched the design's §0: HEAD was `cb9c7b2`, level with `origin/main` (the earlier status-check task had committed and pushed), not `6b35be8` three ahead.
- ...that the old #40 "recovered swallowed switch change" explanation was confirmed: that mechanism is deleted, and whether the swallow hypothesis was right is still unproven either way.
- ...that the 271-307 ms vs 214-223 ms figures or the "CPU sleeping between samples" cause were observed by this session: they are the design's numbers and inference.
- ...that the flag-1 build is harmless: it must never be left on the bed.
