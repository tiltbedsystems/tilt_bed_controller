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
