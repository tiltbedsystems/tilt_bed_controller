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
