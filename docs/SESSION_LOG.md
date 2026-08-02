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
