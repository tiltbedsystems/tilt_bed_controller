# fw_HANDOFF_2026-08-02_01 — pitch wire sign + continuous tilt sampling (PLANNING ONLY)

> **Retroactive file.** Written 2026-08-02 during a later session, for a
> planning session that had already completed. It exists because the reviewer
> was sent only the `docs/SESSION_LOG.md` summary of the plan below, never the
> plan, and issued a verdict on that basis. That verdict is withdrawn; this
> file is the artifact that should have gone with it.
>
> **Provenance of the plan text.** The full plan was still present in the
> conversation context when this file was written, so **§B below is the
> delivered plan reproduced verbatim — it is NOT a reconstruction.** The only
> additions are clearly fenced and labelled `[EXPANSION — not in the delivered
> plan]`. Everything outside those fences is the original text as the user
> received it.
>
> Two things in this file *are* reconstruction, and are marked inline where
> they appear: the end-of-session `git status` (never captured at the time)
> and the classification of which tool calls were made (recovered from
> context, not from a log).

---

## a. Header

| Field | Value |
|---|---|
| Repo | `tilt_bed_controller` |
| Branch | `main` |
| HEAD before session | `6cbd2fd` |
| HEAD after session | `6cbd2fd` (no commits — planning only, by instruction) |
| Firmware baseline | `29fdf0d` — parked, untouched |
| Session date | 2026-08-02 |
| Scope | PLANNING ONLY. No source file modified. |

### `git status` at end of session

**RECONSTRUCTION — not captured at the time.** `git status` was not run at the
end of the planning session; the requirement to do so did not exist yet. The
closest available evidence is the status captured at the *start* of the next
session, before that session had modified anything. The only planning-session
change it reflects is the `docs/SESSION_LOG.md` append.

```
On branch main
Your branch is ahead of 'origin/main' by 20 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	.projectchecksums
	tilt_bed_controller_project.pdf

no changes added to commit (use "git add" and/or "git commit -a")
```

`.projectlinkstore`, `.projectchecksums` and `tilt_bed_controller_project.pdf`
were already dirty/untracked before the planning session began and were not
touched by it.

---

## b. FULL TEXT OF THE PLAN AS DELIVERED

Everything from here to the end of §b is verbatim, except where fenced as
`[EXPANSION]`.

---

No source files touched. Firmware verified still bit-for-bit `29fdf0d` (`git diff --stat 29fdf0d..HEAD` shows only `docs/`, `CLAUDE.md`, `.gitignore`, `.claude/`).

# Plan: pitch wire sign + continuous tilt telemetry

## Item 1 — Pitch sign on the BLE wire

### The change

`bed_status.c:93`:

```c
-  out->pitch_angle_scaled = baseline_accel_average_pitch_angle;
+  // Product convention on the wire is POSITIVE = head raised. The internal
+  // value is the opposite sign (see auto_level.c's frame diagram: "If HEAD is
+  // high, PITCH is NEGATIVE"), and it must stay that way - latest_pitch_angle
+  // feeds check_level()/set_auto_level_motor_direction(), so flipping it
+  // upstream would invert which pair auto-level drives. The wire encoding is
+  // the correct boundary for the convention change. Roll is unaffected.
+  out->pitch_angle_scaled = -baseline_accel_average_pitch_angle;
```

Roll stays as-is — the frame diagram's roll convention (left high = positive) already matches.

### Overflow

Safe with wide margin. `accel_average_pitch_angle` derives from `atan2(y,z)×57.3 − 90`, so its representable range is (−270°, +90°] → (−27000, +9000] scaled. Subtracting `baseline_pitch_angle`, clamped to ±1500 by `bed_settings.c:36`, gives a worst case around −28500..+10500. Negation is only unsafe at exactly `INT16_MIN` (−32768), which this path cannot produce. In the real operating range (±15°) the margin is enormous. Your ±9000 figure is the practical range; even the geometric extreme is safe.

### Every other pitch consumer

**Internal — must NOT change sign.** All of these consume the pre-negation value and are unaffected because the negation happens downstream of them:

| Location | Role | Note |
|---|---|---|
| `auto_level.c:221` | `latest_pitch_angle` source | explicitly out of scope |
| `auto_level.c:225,229` | scaled + LPF | |
| `auto_level.c:241-256` | running average → `baseline_accel_average_pitch_angle` | |
| `auto_level.c:334-361` | `check_level(PITCH)` → `level_status[PITCH].positive` | **safety** |
| `auto_level.c:430-438` | `set_auto_level_motor_direction(PITCH)` | **safety** — the inversion your constraint protects |
| `auto_level.c:264` | `returnAccelValue(PITCH)` | returns the **raw** average, not baseline-adjusted |
| `settings_mode.c:294` | stores that raw value as the NVM3 baseline | **see warning below** |
| `settings_mode.c:311,326` | print / erase stored baseline | |
| `bed_settings.c:36` | ±1500 range on the baseline | |
| `app.c:461` | boot print "Loaded Pitch Angle" | internal convention |
| `auto_level.c:276` | `print_accel_value(PITCH)` UART debug | internal convention |
| `dual_motor_control.c:188` | axis selection only | sign-independent |
| `auto_level.c:394-411` | frame diagram comment | stays true; should gain a cross-reference to the wire convention |

**Warning worth recording:** every zero-level baseline already stored in NVM3 on every bed in the field is in the internal convention. Negating at the wire never touches it. If anyone later "simplifies" this by flipping the sign upstream instead, every stored baseline becomes wrong-signed and auto-level drives to the wrong target. That is the strongest argument for the boundary you chose.

**Wire/external — affected:** `bed_status.c:93` (the change), `bed_status.h:50` (field comment should state the convention), `ble_bed_service.c:80-81` (sign-agnostic byte packing — no change), `BLE_GATT_DESIGN.md` §6.

**CSV capture tooling — checked, not affected.** `BED_CAPTURE_MOTOR_CURRENTS` (`debug_capture.h`) streams motor currents only; no pitch or roll in the capture path. One thing to note though: `auto_level.c:279-298` holds commented-out CSV-style `printf("%d,", ...)` lines for accel values. If those are ever re-enabled for a tilt capture they emit the **internal** convention, so a future capture log would disagree in sign with the app. Worth a comment there when the change lands.

### Doc amendment — `BLE_GATT_DESIGN.md` §6

Amend the offset-6 row and add a convention note:

```
offset 4   roll_scaled      i16  ×100, deviation from level baseline
                                 POSITIVE = LEFT side raised
offset 6   pitch_scaled     i16  ×100, deviation from level baseline
                                 POSITIVE = HEAD raised
```

plus a short paragraph: the wire convention is deliberately the negation of firmware's internal `baseline_accel_average_pitch_angle`; the internal sign is fixed by the accelerometer mounting frame and feeds auto-level's direction logic, so the conversion belongs at the encoding boundary (`bed_status.c`) and nowhere else.

### Companion app — confirmed, by reading it

Not assumed. In `../tilt_bed_app`:

- `lib/protocol/bed_status.dart:190` — `data.getInt16(6, Endian.little) / 100.0`, straight signed decode, no transform.
- `lib/ui/widgets/telemetry_panel.dart:65` — displays `status.pitchDegrees` directly.
- No sign transform anywhere in `lib/`.
- Tests are sign-neutral: `bed_status_test.dart` does literal encode/decode round-trips; `mock_bed_transport_test.dart` asserts on `.abs()`.

And a bonus finding: the mock's `_pitchDeg()` (`mock_bed_transport.dart:163`) computes `headAvg - footAvg` — **already positive when the head is raised**. So the mock has been on the target convention all along and real hardware has been the odd one out. The firmware fix removes a latent mock-vs-hardware divergence rather than creating one.

**Conclusion: no app change required.**

---

## Item 2 — Continuous tilt sampling

### Does the 10 ms rate limit make per-tick sampling safe?

Partly — and the part it doesn't cover is the part that matters most.

**Where the answer is yes.** The `28def95` gate binds in **wall-clock**, not loop passes: at most one burst per 10 ms however fast the loop spins. And the bench-clean configuration at `29fdf0d` *already* calls `read_accel()` on every super-loop pass while motors are energised and the CPU is held in EM0 — that is exactly what auto-level does (`app.c:569` → `accel_running_average()` → `read_accel()`, with `app_is_ok_to_sleep()` returning false throughout). That operating point measured ~13% bus occupancy and passed a full regression with a phone subscribed, including §6.8 sustained auto-level. A manual move puts the same accel load on the same bus in the same power state. **For the I2C bus, per-tick sampling during motor-on states is not a new operating point — it is the one already validated.**

**Where the answer is no.** Three things the gate does not cover:

1. **The math is not gated.** The 10 ms limit wraps only the I2C burst. Everything below it runs on *every* call — and it is **double**-precision: `sqrt()` and two `atan2()`, with `PI`/`RAD_TO_DEG` as double literals (`auto_level.c:217-221`). The build is `-mfpu=fpv5-sp-d16 -mfloat-abi=hard` — **single**-precision hardware FPU. So every one of those is software-emulated double math. Extending per-tick calls to manual moves adds that to a loop that doesn't currently carry it. It doesn't hold the bus, so it isn't the #30 mechanism, but it lengthens every pass — and per #29 that shifts every loop-pass-calibrated constant in the supervision path, including collision detection. **This is the specific thing I would not assume is free, and it has not been profiled.**

2. **The idle case is genuinely new, and it's the opposite problem.** `app_is_ok_to_sleep()` (`app.c:500`) vetoes sleep only while a motor is energised. Idle, the loop sleeps — so there is no fast tick to hang idle sampling on. Idle sampling needs a wake source, and adding one (or vetoing sleep) is new scheduling behaviour next to the radio, never exercised on the bench. The safest form is to piggyback the existing 1 Hz status-notify sleeptimer rather than add a second timer.

3. **The 13% / 2.31 ms numbers are auto-level-only.** The manual-move path has never been measured. #29's entire lesson is that a number measured on one path doesn't transfer to another.

### Recommended architecture

A **separate telemetry sampler that never touches auto-level's buffer**:

- New `tilt_telemetry_tick()`, called once per `app_process_action()` alongside `sample_motor_faults_tick()` — which is the existing precedent for exactly this pattern (`exp_board.c:299`, main-loop context, internally rate-limited, shared bus).
- **Gate 1: `status_subscriber_count > 0`.** Per §6's "No subscribers → none, and no extra accel sampling". The comment at `ble_bed_service.c:340` already anticipates this gate by name. A switch-only bed with no phone is then bit-for-bit unchanged — which also means the entire existing regression baseline stays valid for that case.
- **Gate 2: skip entirely while auto-level or zero-level is running.** Those already sample. Without this you feed the running average twice per pass, halving its effective window in time — straight into #29.
- **Gate 3: cadence by state, in ms via sleeptimer (#2)** — ~10 Hz while a motor is on, ~1 Hz idle, matching §6's cadence table. This is what bounds the double math, which the I2C gate does not.
- **Do not write `accel_readings_*[]`.** Telemetry keeps its own value.

### Buffer analysis — does pre-filling change auto-level's first check?

You asked me not to assume this is harmless. Three findings:

**1. The buffer is already never reset.** `reset_level_data()` (`auto_level.c:366-384`) does not touch `accel_readings_roll/pitch[]`, `index`, `accel_running_sum_*`, or the LPF state. They are zero-initialised at boot and persist for the whole power cycle. **Pre-filled at auto-level entry is already the steady state** for every run after the first since boot. The genuinely cold start is the first run after power-on only.

**2. What actually protects the first check is the 1500 ms settle, and it has ~5× margin.** `steady_accel_read_both_axes()` doesn't sample at all — its `accel_running_average()` call is commented out at `auto_level.c:550`. It waits `STEADY_ACCEL_READ_TIMER_TIME` = 1500 ms before the first `check_level()`, while app.c:569 feeds the buffer every pass. At the measured ~2.31 ms/pass, 130 samples ≈ 300 ms. The buffer is fully overwritten about five times over before the first check. Pre-existing contents cannot survive into it.

**3. So under the recommended architecture, auto-level's first check is unchanged** — with one real difference to declare: the shared LPF accumulator `latest_*_filtered` would arrive *converged on recent tilt* instead of holding the last value from the previous run. That is a change, it is in the safe direction, and it belongs on the regression list rather than being waved through.

**The trap this avoids.** If the sampler *did* share the buffer at a decimated cadence, the buffer's span becomes `130 × interval`. At 100 ms that is **13 seconds against a 1500 ms settle window** — `check_level()` would average pre-press history into its first decision, and auto-level could pick its first motor direction from data predating the user's press. That is safety-relevant misbehaviour and it is precisely #29's failure mode. It is the single strongest reason not to unify the two paths.

**Constraint to record either way:** `NUM_ACCEL_READINGS × (auto-level sampling interval)` must stay well below `STEADY_ACCEL_READ_TIMER_TIME`, verified against a *measured* loop rate.

### `tilt_fresh`

**Yes, and it is not optional if Item 2 ships.** Today hardcoded 0 is *honest* — the data really is stale outside auto-level. Once sampling is continuous, 0 becomes wrong in the other direction, telling the app to grey out live data. The two must land together.

- **Semantics:** 1 iff the accel sample backing the shipped angle is younger than a staleness threshold. Timestamp at sample time in ms via sleeptimer (#2).
- **Threshold:** comfortably above the slowest cadence or it flaps. With 1 Hz idle, ~2.5–3 s.
- **Notification-storm hazard.** `tilt_fresh` is bit 3 of the flags byte, and `status_flags_changed()` memcmps bytes 0-3 (`ble_bed_service.c:315`) — so **every transition forces an immediate notification**. A bit chattering at the staleness boundary becomes a notify storm on the same radio these two bugs were about. Needs hysteresis or a generous threshold, and an explicit regression check.
- **What it becomes:** once sampling is continuous it stops meaning "is the feature on" and becomes a real health bit — 0 between subscribe and first sample, 0 if I2C fails, 0 if the loop stalls. Worth having.
- **Cross-repo dependency.** The app deliberately passes `fresh: true` instead of `status.tiltFresh` (`telemetry_panel.dart:55`, `TODO(tilt-fresh)`) *because* firmware hardcodes 0. Firmware implementing the bit changes nothing on screen until that TODO is reverted. Until then the app shows stale tilt as live — no worse than today, but the benefit isn't delivered. Sequence: firmware first, then the app revert.

### Required bench regression before Item 2 could be considered safe

Full `REGRESSION.md`, phone bonded and subscribed throughout, per the post-Stage-B requirement — **including §6.8 sustained auto-level with a phone subscribed**, which is mandatory here both because you required it and because that check is the one that caught #26/#30.

New checks the current list does not cover:

1. **Sustained manual move, phone subscribed** — each pair and ALL, held to physical limit. This is the new operating point: per-tick sampling with motors on, outside auto-level. Watch for `reason=0x1008`.
2. **Long idle-subscribed soak (≥10 min)** — the other new operating point. Confirms the idle wake cadence neither drops the link nor stalls tilt updates.
3. **Loop-rate measurement on both paths, before and after** — auto-level *and* manual move. Per #29 the constants are loop-pass calibrated; any change in passes/sec must be reconciled against collision-detection timing before this is called safe.
4. **Collision detection re-verified on a manual move** (§5) once the loop rate has moved. This is the safety consequence of #29, not a formality.
5. **`tilt_fresh` behaviour** — count notifications/sec across the staleness boundary to prove no storm; verify 1 while sampling and 0 after forcing a stall.
6. **A/B discipline** — one binary, one variable at a time, as in #30.
7. **Release-config pass.** Still owed, and this work makes it more urgent: `board_exp_transaction()`'s `EFM_ASSERT` (`exp_board.c:272`) compiles out in Release, on the same bus this change loads further.

---

## Risks

| # | Risk | Severity | Mitigation |
|---|---|---|---|
| R1 | Added double-precision math per pass lengthens the loop, shifting every loop-pass-calibrated constant in the supervision path — collision detection included (#29) | **High — safety** | Cadence-gate the sampler; measure passes/sec on both paths before/after; re-run §5 collision |
| R2 | Third BLE supervision-timeout drop on this code path | **High** | Subscriber gate; per-tick only when motors on; full subscribed regression incl. §6.8; A/B discipline |
| R3 | Sharing auto-level's buffer at a decimated cadence corrupts the first `check_level()` and can pick the wrong first direction | **High — safety** | Do not share the buffer. Separate telemetry value |
| R4 | Idle wake source is new scheduling behaviour next to the radio | Medium | Piggyback the existing 1 Hz notify timer; long idle soak |
| R5 | `tilt_fresh` chatter → notify storm | Medium | Generous threshold + hysteresis; count notifications at the boundary |
| R6 | LPF arrives converged at auto-level entry instead of carrying the last run's value | Low — direction is favourable | Declare it; verify §6 unchanged |
| R7 | Item 2 ships without the app-side `TODO(tilt-fresh)` revert → benefit not delivered | Low | Sequence firmware → app |
| R8 | Someone later "simplifies" the pitch sign upstream, invalidating every stored NVM3 baseline in the field | Medium | Comment at both `read_accel()` and `bed_status.c`; note in `BLE_GATT_DESIGN.md` §6 |
| R9 | Release-config behaviour still unverified on a bus this change loads further | Medium | The owed Release pass gates Stage C anyway |

## Recommended sequencing

1. **Item 1 alone**, its own commit. One line plus comments and the §6 amendment; no timing impact, no safety path touched. Verify with the phone: raise the head, confirm positive. Cheap, independently revertable.
2. **Item 2 gated behind review of this plan.** It touches the safety-relevant loop, so per `CLAUDE.md` it needs your explicit approval and small verifiable steps. I'd suggest splitting it further: (a) sampler + subscriber gate with `tilt_fresh` still 0, measure loop rate, regress; then (b) `tilt_fresh` implementation; then (c) the app-side revert.
3. Item 2 probably warrants a `DECISIONS.md` entry implementing #17 and superseding the staleness comment at `auto_level.h:28-31`.

## One thing I deliberately left out

Moving the trig behind the same 10 ms gate as the I2C burst looks like a free win — between real samples the inputs are identical, so `sqrt`/`atan2` recompute the same result. It is **not** free: the LPF is currently fed every pass, so gating it changes the filter's effective time constant. That is a control-law change, squarely a #29 matter, and it should not be bundled into Item 2.

---

**END OF VERBATIM PLAN TEXT.**

---

### `[EXPANSION — not in the delivered plan]` Options considered and rejected

The delivered plan states the rejection reasoning at the points cited below,
but does not lay the alternatives out as a numbered list. The reviewer asked
for that explicitly, so it is restated here. No new reasoning is introduced —
each entry cites where in the verbatim text above it comes from.

**Item 1 — where to apply the sign flip**

| Option | Verdict | Why |
|---|---|---|
| O1.1 Negate in `read_accel()` (`auto_level.c:221`) | **Rejected** | Ruled out by the user's constraint, and independently by the plan: `latest_pitch_angle` feeds `check_level()` → `set_auto_level_motor_direction()`, so it would invert which pair auto-level drives. Safety-relevant. See "The change" and the internal-consumer table. |
| O1.2 Negate at `returnAccelValue(PITCH)` / the NVM3 baseline | **Rejected** | Not stated as a discrete option in the delivered text, but foreclosed by its "Warning worth recording": every baseline already stored in the field is in the internal convention, so any upstream flip invalidates all of them. |
| O1.3 Negate in `ble_bed_service_encode_status()` (`ble_bed_service.c:80-81`) | **Rejected** | That layer is sign-agnostic byte packing of an `int16`. Putting a semantic transform there splits the convention across two files. The plan assigns the conversion to `bed_status.c` "and nowhere else". |
| O1.4 Negate at `bed_status.c:93` | **ADOPTED** | The single boundary between internal representation and wire representation. |
| O1.5 Fix it in the companion app instead | **Rejected** | The app decodes the wire value with no transform (`bed_status.dart:190`) and its mock already uses the target convention (`mock_bed_transport.dart:163`). Fixing it app-side would leave firmware and mock disagreeing and push the problem onto every future client. |

**Item 2 — sampler architecture**

| Option | Verdict | Why |
|---|---|---|
| O2.1 Unconditional `read_accel()` on every main-loop pass | **Rejected** | Recreates the DECISIONS #27/#30 operating point in the idle case with no subscriber gate, and adds ungated double-precision math to every pass. See "Where the answer is no", points 1–3. |
| O2.2 Single writer: move `accel_running_average()` out of the auto-level branch into an unconditional tick, decimated | **Rejected — the strongest rejection in the plan** | `130 × 100 ms = 13 s` of buffer span against a 1500 ms settle window, so `check_level()` would average pre-press history and auto-level could choose its first motor direction from data predating the user's press. Safety-relevant; #29's failure mode. See "The trap this avoids". |
| O2.3 Separate telemetry sampler, subscriber-gated, cadence by state, auto-level's buffer untouched | **ADOPTED** | See "Recommended architecture". Modelled on the existing `sample_motor_faults_tick()` precedent (`exp_board.c:299`). |
| O2.4 Gate the trig behind the same 10 ms limit as the I2C burst | **Deliberately excluded from Item 2** | Looks free, is not: the LPF is fed every pass, so gating changes its effective time constant — a control-law change, a #29 matter. See "One thing I deliberately left out". |
| O2.5 Add a dedicated sleeptimer to wake for idle sampling | **Rejected in favour of piggybacking** | A new periodic wake next to the radio's schedule is new, unexercised behaviour; the existing 1 Hz status-notify timer already provides a wake. See "Where the answer is no", point 2. |
| O2.6 Leave `tilt_fresh` hardcoded 0 while shipping Item 2 | **Rejected** | Today 0 is honest. With continuous sampling it becomes wrong in the other direction — it would tell the app to grey out live data. "The two must land together." |

---

## c. `git diff` for source changes

**None.** No `.c`, `.h`, or build file was modified. The session was planning
only by explicit instruction.

The one file changed was `docs/SESSION_LOG.md` (an append of the planning
entry). Its diff was 92 insertions / 0 deletions; the diffstat is in §d below.
Under the current rule a docs-only diff over 400 lines may be truncated —
this one is under that and is reproducible with:

```
git diff docs/SESSION_LOG.md
```

but it was **not captured verbatim at the time**, because the requirement did
not exist yet. Marking that as a gap rather than papering over it. The content
is the 2026-08-02 entry now visible in `docs/SESSION_LOG.md`.

---

## d. Raw command output

Every shell command run during the planning session, with its output verbatim,
in order. Failures included.

### d.1 Firmware-parked check

```
$ git diff --stat 29fdf0d..HEAD && ls *.c *.h
=== firmware parked check: what did the 2 commits after 29fdf0d touch? ===
 .claude/hooks/check-session-log.ps1 |  98 ++++++++++++++++
 .claude/settings.json               |  23 ++++
 .gitignore                          |   8 +-
 CLAUDE.md                           |  42 ++++++-
 docs/SESSION_LOG.md                 | 228 ++++++++++++++++++++++++++++++++++++
 5 files changed, 396 insertions(+), 3 deletions(-)

=== tree files ===
adc_currents.c
adc_currents.h
app.c
app.h
app_bm.c
auto_level.c
auto_level.h
bed_actions.c
bed_actions.h
bed_control.c
bed_control.h
bed_settings.c
bed_settings.h
bed_status.c
bed_status.h
bed_types.h
ble_bed_service.c
ble_bed_service.h
debug_capture.h
dual_motor_control.c
dual_motor_control.h
exp_board.c
exp_board.h
exp_ui.c
exp_ui.h
main.c
mc3479.c
mc3479.h
motor_current_functions.c
motor_current_functions.h
nvm3_functions.c
nvm3_functions.h
pi4ioe5v6416.h
settings_mode.c
settings_mode.h
sl_gatt_service_device_information_override.c
switch_input.c
switch_input.h
version.h
```

### d.2 `BLE_GATT_DESIGN.md` §6, and the CSV-capture check

```
$ sed -n '/^## 6\./,/^## 7\./p' docs/BLE_GATT_DESIGN.md
## 6. Bed Status characteristic (0003)

Packed, little-endian, **20 bytes exactly** — fits a notification at the
default ATT MTU (23), so telemetry works even before/without MTU exchange.
Mirrors `bed_status_t` (C3):

```
offset 0   flags            u8   bit0 any_collision
                                 bit1 in_settings_mode (physical settings mode)
                                 bit2 lights_on
                                 bit3 tilt_fresh   ← NEW, see below
                                 bits4-7 reserved (0)
offset 1   active_source    u8   0 NONE, 1 SWITCHES, 2 APP
offset 2   command          u8   0 NONE, 1 MOVE, 2 AUTO_LEVEL, 3 LIGHTS
offset 3   command_detail   u8   bits0-2 target (0-4), bit3 direction (0 down/1 up)
                                 valid only when command == MOVE
offset 4   roll_scaled      i16  ×100, deviation from level baseline
offset 6   pitch_scaled     i16  ×100
offset 8   motor[0..3]      4 × { status u8, current_ma u16 }  = 12 B
             status: bit0 on, bit1 moving_up, bit2 at_upper_limit,
                     bit3 at_lower_limit, bit4 fault, bits5-7 reserved
             indexed by PHYSICAL motor position A–D (matching motor_state[]),
             NOT logical corner — the corner mapping arrives with the
             calibration wizard (#7); until then the app labels A–D.
```

**`tilt_fresh` requires a one-bit firmware addition** implementing
DECISIONS #17's app-era plan: continuous accelerometer sampling runs at a
variable cadence *only while at least one connection has Status
notifications enabled* — occasional (~1 Hz) when idle, fast while moving or
auto-leveling. `tilt_fresh = 0` whenever the last sample is older than the
staleness threshold; the app greys out the tilt display rather than showing
a stale angle as live.

Notify cadence (adaptive, subscription-driven):

| Bed state | Cadence |
|---|---|
| No subscribers | none (and no extra accel sampling) |
| Idle, subscribed | 1 Hz, plus immediate on any state-flag change |
| Moving / auto-leveling | 10 Hz |
| Collision / fault edge | immediate |

## 7. Command Result characteristic (0002)
```

```
$ grep -n "BED_CAPTURE|capture|CSV|csv" debug_capture.h
debug_capture.h:2: * debug_capture.h
debug_capture.h:4: * Bench-only current-capture mode (refactor step C3, DECISIONS.md #5). When
debug_capture.h:5: * BED_CAPTURE_MOTOR_CURRENTS is 1, motor_current_functions.c streams
debug_capture.h:6: * timestamped CSV over the debug UART, and every other printf in the
debug_capture.h:8: * compiles out via "#if !BED_CAPTURE_MOTOR_CURRENTS" - so a PuTTY capture
debug_capture.h:9: * log is pure CSV, loadable directly into Excel with no cleanup. The one
debug_capture.h:11: * trips mid-capture (DECISIONS.md #4) - the leading '#' makes contaminated
debug_capture.h:12: * captures easy to spot and reject.
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
debug_capture.h:23:// Zeroes the capture CSV's time_ms column to a new t=0, so each bench test
debug_capture.h:29:// motor_current_functions.c owns the real clock. No-op when capture mode is
debug_capture.h:31:#if BED_CAPTURE_MOTOR_CURRENTS
debug_capture.h:32:void capture_reset_timer(void);
debug_capture.h:34:#define capture_reset_timer() ((void)0)
```

### d.3 Docs mentions of pitch, and DECISIONS #17

```
$ grep -rn "tilt_fresh\|PITCH is NEGATIVE\|pitch" docs/REGRESSION.md docs/SETPOINTS_INVENTORY.md docs/BLE_GATT_DESIGN.md
=== docs mentioning pitch/tilt sign or tilt_fresh ===
docs/REGRESSION.md:141:pitch, all actuators clear of limits.
docs/SETPOINTS_INVENTORY.md:87:| auto_level.c:31 | `PITCH_OFFSET` | 90° | Geometry constant: pitch derived from atan2(y,z) minus 90°. Mounting-orientation dependent — installer-adjustable only if the accel can be mounted differently | `KEY_PITCH_OFFSET` (41, whole deg) | 90 | 0 | 270 | |
docs/SETPOINTS_INVENTORY.md:89:| existing | baseline pitch angle | 0 | User "zero" pitch captured in settings mode (scaled) | existing key 2 | 0 | −1500 | 1500 | |
docs/BLE_GATT_DESIGN.md:149:                                 bit3 tilt_fresh   ← NEW, see below
docs/BLE_GATT_DESIGN.md:156:offset 6   pitch_scaled     i16  ×100
docs/BLE_GATT_DESIGN.md:165:**`tilt_fresh` requires a one-bit firmware addition** implementing
docs/BLE_GATT_DESIGN.md:169:auto-leveling. `tilt_fresh = 0` whenever the last sample is older than the

=== DECISIONS #17 headline ===
## 17. Continuous tilt sampling — deferred to app era (recorded during C3)

Today `accel_running_average()` (and the roll/pitch values it produces) only
runs while auto-level or the settings zero-level capture is active — outside
those two windows the angle is a stale, last-known value. C3's telemetry
snapshot ships with this limitation documented rather than adding a
continuous accelerometer read (out of scope for a read-only telemetry step).

The real fix is a variable-cadence continuous sampling loop, deferred to the
app/D1 era: sample occasionally for a live tilt display while an app is
connected but idle, and more frequently while the bed is moving or
auto-leveling — ideally with the cadence driven by what the connected app is
actually requesting (e.g. a subscribed BLE notification vs. no one watching).
Recorded so this isn't lost before D1.

## 18. Dead-man keepalive for app movement (repeated-intent protocol)
```

### d.4 Companion-app availability and pitch consumers

```
$ ls -d "…/tilt_bed_app" && grep -rn "pitch" "…/tilt_bed_app" --include=*.swift --include=*.kt --include=*.ts --include=*.js --include=*.dart --include=*.md -i
C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\tilt_bed_app
…/tilt_bed_app/BLE_GATT_DESIGN.md:156:offset 6   pitch_scaled     i16  ×100
…/tilt_bed_app/DECISIONS_D1_additions.md:82:Today `accel_running_average()` (and the roll/pitch values it produces) only
…/tilt_bed_app/docs/APP_ARCHITECTURE.md:120:  final pitch = data.getInt16(6, Endian.little) / 100.0;  // SIGNED, degrees
…/tilt_bed_app/docs/APP_ARCHITECTURE.md:127:Roll/pitch are **signed** two's-complement int16 — decoding them unsigned is
…/tilt_bed_app/docs/APP_ARCHITECTURE.md:282:   `telemetry_panel.dart` was greying the roll/pitch readout whenever
…/tilt_bed_app/docs/SESSION_LOG.md:117:- **A3 pitch sign is WRONG (convention):** pitch reads **negative when the
…/tilt_bed_app/docs/SESSION_LOG.md:179:  forever outside those paths), and the pitch sign belonging at the wire
…/tilt_bed_app/docs/SESSION_LOG.md:203:  and fix the pitch sign at the wire encoding with "positive = head raised"
…/tilt_bed_app/docs/SESSION_LOG.md:221:5. **That the pitch sign should be flipped in firmware's `read_accel()`.**
…/tilt_bed_app/docs/SESSION_LOG.md:222:   `latest_pitch_angle` feeds auto-level's control decisions, so inverting
…/tilt_bed_app/lib/protocol/bed_status.dart:16:///   offset 6   pitch_scaled   i16  x100
…/tilt_bed_app/lib/protocol/bed_status.dart:125:    required this.pitchDegrees,
…/tilt_bed_app/lib/protocol/bed_status.dart:152:  /// Pitch deviation from level, in degrees (wire value / 100).
…/tilt_bed_app/lib/protocol/bed_status.dart:153:  final double pitchDegrees;
…/tilt_bed_app/lib/protocol/bed_status.dart:190:    final pitch = data.getInt16(6, Endian.little) / 100.0;
…/tilt_bed_app/lib/protocol/bed_status.dart:210:      pitchDegrees: pitch,
…/tilt_bed_app/lib/protocol/bed_status.dart:239:    data.setInt16(6, (pitchDegrees * 100).round(), Endian.little);
…/tilt_bed_app/lib/transport/mock_bed_transport.dart:145:  /// Tilt gain: degrees of roll/pitch per unit of average corner-position
…/tilt_bed_app/lib/transport/mock_bed_transport.dart:162:  /// PITCH deviation from level, degrees: head pair (A,D) vs foot pair (B,C).
…/tilt_bed_app/lib/transport/mock_bed_transport.dart:163:  double _pitchDeg() {
```

### d.5 App mock transport and pitch consumers

```
$ sed -n '155,185p' lib/transport/mock_bed_transport.dart
=== mock _pitchDeg (generates mock wire values) ===
  /// ROLL deviation from level, degrees: left pair (A,B) vs right pair (C,D).
  double _rollDeg() {
    final leftAvg = (_motors[0].position + _motors[1].position) / 2;
    final rightAvg = (_motors[2].position + _motors[3].position) / 2;
    return (leftAvg - rightAvg) * kTiltGainDeg;
  }

  /// PITCH deviation from level, degrees: head pair (A,D) vs foot pair (B,C).
  double _pitchDeg() {
    final headAvg = (_motors[0].position + _motors[3].position) / 2;
    final footAvg = (_motors[1].position + _motors[2].position) / 2;
    return (headAvg - footAvg) * kTiltGainDeg;
  }

$ grep -rn "pitchDegrees\|head raised\|headRaised" lib/ test/
=== any sign transform / head-raised semantics in UI or protocol? ===
lib/protocol/bed_status.dart:125:    required this.pitchDegrees,
lib/protocol/bed_status.dart:153:  final double pitchDegrees;
lib/protocol/bed_status.dart:210:      pitchDegrees: pitch,
lib/protocol/bed_status.dart:239:    data.setInt16(6, (pitchDegrees * 100).round(), Endian.little);
lib/transport/mock_bed_transport.dart:896:      pitchDegrees: pitch,
lib/ui/widgets/telemetry_panel.dart:65:              value: status.pitchDegrees,
test/protocol/bed_status_test.dart:59:      expect(s.pitchDegrees, 1.75);
test/protocol/bed_status_test.dart:105:      expect(BedStatus.decode(bytes).pitchDegrees, closeTo(-12.34, 1e-9));
test/transport/mock_bed_transport_test.dart:202:      expect(latest!.pitchDegrees.abs(), lessThan(0.2),
test/transport/mock_bed_transport_test.dart:208:      expect(latest!.pitchDegrees.abs(), greaterThan(0.5),
test/transport/mock_bed_transport_test.dart:239:      expect(latest!.pitchDegrees.abs(), lessThanOrEqualTo(0.2));
test/widget/hold_to_run_widget_test.dart:138:        pitchDegrees: 0,
Shell cwd was reset to C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\tilt_bed_controller
```

### d.6 App handling of `tilt_fresh`

```
$ grep -rn "tiltFresh\|tilt_fresh" lib/ docs/APP_ARCHITECTURE.md
lib/protocol/bed_status.dart:9:///                                  bit3 tilt_fresh
lib/protocol/bed_status.dart:119:    required this.tiltFresh,
lib/protocol/bed_status.dart:136:  /// hardcodes tilt_fresh = 0 on every packet (a deferred TODO, DECISIONS
lib/protocol/bed_status.dart:140:  final bool tiltFresh;
lib/protocol/bed_status.dart:204:      tiltFresh: flags & _flagTiltFresh != 0,
lib/protocol/bed_status.dart:225:    if (tiltFresh) flags |= _flagTiltFresh;
lib/transport/mock_bed_transport.dart:458:  /// Force the tilt reading stale so the UI greys it out (§6 tilt_fresh=0).
lib/transport/mock_bed_transport.dart:884:    final tiltFresh = !_forceTiltStale; // sampling is live while subscribed
lib/transport/mock_bed_transport.dart:890:      tiltFresh: tiltFresh,
lib/ui/debug_menu.dart:185:                  subtitle: const Text('tilt_fresh = 0, UI greys it out'),
lib/ui/widgets/telemetry_panel.dart:12:/// tilt_fresh implementation (BLE_GATT_DESIGN.md §6/§17, DECISIONS #17).
lib/ui/widgets/telemetry_panel.dart:13:/// Firmware currently hardcodes tilt_fresh = 0 on every packet, so gating on
lib/ui/widgets/telemetry_panel.dart:16:/// `status.tiltFresh` again once firmware sends a real freshness bit.
lib/ui/widgets/telemetry_panel.dart:55:            // fresh: true (not status.tiltFresh) -- see the TODO(tilt-fresh)
docs/APP_ARCHITECTURE.md:7:the codebase 2026-07-22 (includes the tilt_fresh-gating and protocol-version-
docs/APP_ARCHITECTURE.md:114:                                       // bit2 lights_on, bit3 tilt_fresh
docs/APP_ARCHITECTURE.md:270:| tilt_fresh (Bed Status flags bit3) | — | **Decoded but not acted on.** Firmware currently hardcodes this bit to 0 (deferred TODO, DECISIONS #17); the app decodes it correctly but does not grey the tilt display on it (see Recent fixes). Re-enable once firmware sends a real freshness bit. |
docs/APP_ARCHITECTURE.md:283:   `status.tiltFresh` was false. Since firmware hardcodes `tilt_fresh = 0`
docs/APP_ARCHITECTURE.md:286:   display call sites instead of `status.tiltFresh`, with a `TODO(tilt-fresh)`

$ sed -n '275,292p' docs/APP_ARCHITECTURE.md
=== context around APP_ARCHITECTURE:275-292 ===

## 9. Recent fixes (2026-07-22)

Two correctness issues found in an architecture audit, fixed in the same
session as this doc:

1. **Tilt display permanently stale on real hardware.**
   `telemetry_panel.dart` was greying the roll/pitch readout whenever
   `status.tiltFresh` was false. Since firmware hardcodes `tilt_fresh = 0`
   today, the tilt display would always render greyed/stale on a real bed
   even though the angle is live. Fixed by passing `fresh: true` at the
   display call sites instead of `status.tiltFresh`, with a `TODO(tilt-fresh)`
   comment in both `bed_status.dart` and `telemetry_panel.dart` marking where
   to re-wire it once firmware implements the real bit.

2. **Protocol-version check raced Bed Status notifications.**
   `BleBedTransport.connect()` called `_subscribeNotifications()` before
   reading and verifying Protocol Version. A Bed Status notification arriving
Shell cwd was reset to C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\tilt_bed_controller
```

### d.7 FPU build flags

```
$ grep -rn "mfpu\|mfloat-abi\|mcpu\|fpv" cmake_gcc/*.cmake cmake_gcc/CMakeLists.txt
cmake_gcc/tilt_bed_controller.cmake:358:    $<$<COMPILE_LANGUAGE:C>:-mcpu=cortex-m33>
cmake_gcc/tilt_bed_controller.cmake:360:    $<$<COMPILE_LANGUAGE:C>:-mfpu=fpv5-sp-d16>
cmake_gcc/tilt_bed_controller.cmake:361:    $<$<COMPILE_LANGUAGE:C>:-mfloat-abi=hard>
cmake_gcc/tilt_bed_controller.cmake:372:    $<$<COMPILE_LANGUAGE:CXX>:-mcpu=cortex-m33>
cmake_gcc/tilt_bed_controller.cmake:374:    $<$<COMPILE_LANGUAGE:CXX>:-mfpu=fpv5-sp-d16>
cmake_gcc/tilt_bed_controller.cmake:375:    $<$<COMPILE_LANGUAGE:CXX>:-mfloat-abi=hard>
cmake_gcc/tilt_bed_controller.cmake:388:    $<$<COMPILE_LANGUAGE:ASM>:-mcpu=cortex-m33>
cmake_gcc/tilt_bed_controller.cmake:390:    $<$<COMPILE_LANGUAGE:ASM>:-mfpu=fpv5-sp-d16>
cmake_gcc/tilt_bed_controller.cmake:391:    $<$<COMPILE_LANGUAGE:ASM>:-mfloat-abi=hard>
cmake_gcc/tilt_bed_controller.cmake:401:    -mcpu=cortex-m33
```

### d.8 Failures

**One command failed during the session**, in the *preceding* turn (the
SESSION_LOG correction), not the planning turn. Recorded for completeness
since it is the only non-zero exit in the surrounding work:

```
$ python3 -c "import json;json.load(open('.claude/settings.json'));print('settings.json OK')"
Python was not found; run without arguments to install from the Microsoft Store, or disable this
shortcut from Settings > Apps > Advanced app settings > App execution aliases.
[exit code 49]
```

Recovered by validating with PowerShell's `ConvertFrom-Json` instead, which
succeeded. **No command failed during the planning session itself.**

### d.9 Non-shell tool calls

The planning session also used `Read` and `Grep` tool calls rather than shell
commands. They are listed here for completeness; their "raw output" is the
file content itself, which is reproducible from the repo at `29fdf0d`.

| Tool | Target | Established |
|---|---|---|
| Read | `auto_level.c` (full, 611 lines) | buffer/reset/settle facts, frame diagram |
| Read | `bed_status.c` (full), `auto_level.h` (full) | wire assignment, staleness comment |
| Read | `app.c:490-620` | `app_is_ok_to_sleep()`, auto-level branch |
| Read | `ble_bed_service.c:255-354` | notify cadence, `status_flags_changed()`, `tilt_fresh` hardcode |
| Read | `settings_mode.c:235-325` | zero-level capture, NVM3 baseline store |
| Read | `exp_board.c:265-320` | `sample_motor_faults_tick()` precedent |
| Grep | `pitch\|PITCH` across `*.{c,h}` | the full internal-consumer table |
| Grep | `read_accel\|accel_running_average\|…` | call-site enumeration |
| Grep | `ble_bed_service_tick\|sample_motor_faults_tick\|…` | main-loop wiring, `main.c:63-67` |

---

## e. VERIFIED vs ASSERTED, with evidence

### VERIFIED — code facts, each with its citation

Every item here was read directly from the tree at `29fdf0d`. None of it is a
bench result.

| # | Claim | Evidence |
|---|---|---|
| V1 | `read_accel()` has exactly one caller: `accel_running_average()` | `auto_level.c:237`; Grep §d.9 row 7 |
| V2 | `accel_running_average()` has exactly two callers | `app.c:569`, `settings_mode.c:258` |
| V3 | Auto-level's sample buffer is **never reset** — `reset_level_data()` does not touch `accel_readings_roll/pitch[]`, `index`, `accel_running_sum_*`, or LPF state | `auto_level.c:366-384` vs declarations at `auto_level.c:81-86` |
| V4 | `steady_accel_read_both_axes()` does not sample; its `accel_running_average()` call is commented out | `auto_level.c:550` |
| V5 | First `check_level()` is deferred 1500 ms by `STEADY_ACCEL_READ_TIMER_TIME` | `auto_level.c:50`, `auto_level.c:549-564` |
| V6 | `app_is_ok_to_sleep()` vetoes sleep only while a motor is energised | `app.c:500-508` |
| V7 | `ble_bed_service_tick()` early-returns on zero subscribers, and its comment names the intended sampling gate | `ble_bed_service.c:339-341` |
| V8 | `status_flags_changed()` memcmps bytes 0-3, so the flags byte (holding `tilt_fresh` bit 3) forces an immediate notify on any transition; tilt *angles* are deliberately excluded | `ble_bed_service.c:313-324`, comment at `:308-312` |
| V9 | `tilt_fresh` is hardcoded `false` | `ble_bed_service.c:347` |
| V10 | Build is single-precision hardware FPU while `read_accel()` uses double-precision `sqrt()`/`atan2()` | `cmake_gcc/tilt_bed_controller.cmake:360-361` (raw output §d.7) vs `auto_level.c:41-42, 217-221` |
| V11 | Pitch reaches the wire through exactly one assignment; the packer is sign-agnostic | `bed_status.c:93`; `ble_bed_service.c:80-81` |
| V12 | Internal convention is head-high ⇒ pitch negative | `auto_level.c:394-396` (frame diagram), consistent with `auto_level.c:221` |
| V13 | NVM3 baseline is stored from the **raw** average, in internal convention | `settings_mode.c:294` → `returnAccelValue(PITCH)` → `auto_level.c:264` |
| V14 | Baseline pitch is clamped to ±1500 | `bed_settings.c:36` |
| V15 | CSV capture streams motor currents only — no pitch/roll | `debug_capture.h:4-12, 21` (raw output §d.2) |
| V16 | Commented-out CSV-style accel prints exist and would emit the internal convention if re-enabled | `auto_level.c:279-298` |
| V17 | Companion app applies **no** sign transform: straight signed decode, direct display | `bed_status.dart:190`, `telemetry_panel.dart:65`; Grep §d.5 shows no other consumer |
| V18 | App tests are sign-neutral (literal round-trips / `.abs()`) | `bed_status_test.dart:59,105`; `mock_bed_transport_test.dart:202,208,239` |
| V19 | App mock already uses positive = head raised, so hardware is the outlier | `mock_bed_transport.dart:162-166` (raw output §d.5) |
| V20 | App deliberately passes `fresh: true` instead of `status.tiltFresh` because firmware hardcodes 0 | `telemetry_panel.dart:12-16, 55`; `APP_ARCHITECTURE.md:270, 282-286` (raw output §d.6) |
| V21 | `sample_motor_faults_tick()` is an existing rate-limited main-loop sampler on the same I2C bus — the precedent the proposal copies | `exp_board.c:279-309` |
| V22 | §6 of the design doc already specifies subscription-driven variable-cadence sampling and `tilt_fresh` | `BLE_GATT_DESIGN.md` §6 (raw output §d.2) |
| V23 | DECISIONS #17 already records continuous sampling as the deferred real fix | raw output §d.3 |
| V24 | Firmware unchanged since `29fdf0d` | raw output §d.1 |

### ASSERTED — reasoning only, nothing observed

| # | Claim | Why it is only ASSERTED |
|---|---|---|
| A1 | The 10 ms rate limit makes per-tick sampling safe **for the I2C bus** during manual moves | Reasoning by equivalence from the #30 measurement (auto-level path). No measurement was taken on the manual-move path. Nothing was built or run. |
| A2 | The unbounded cost is the double-precision math | Read from build flags (V10) and source, **not profiled**. No timing taken. |
| A3 | Auto-level's first check is unaffected under the recommended architecture | Follows from the 1500 ms vs ~300 ms margin (V3–V5 plus the ~2.31 ms/pass figure from DECISIONS #29). Not observed on hardware. |
| A4 | The ~300 ms buffer-fill figure | Arithmetic on the #29 loop-rate measurement, which was taken on the auto-level path only. |
| A5 | A `tilt_fresh` at the staleness boundary would cause a notify storm | Follows from V8, but no rate was measured. |
| A6 | The proposed regression list is sufficient to call Item 2 safe | Judgement. Unreviewed. |
| A7 | Overflow analysis for the negation | Arithmetic from V12/V14. Not exercised at the extremes on hardware. |
| A8 | "No app change required" | Rests on V17–V19, which are code reads of the app repo. The app was not built or run from this session. |

---

## f. QUESTIONS FOR REVIEW

1. **Is the Item 1 boundary accepted as `bed_status.c:93`?** The plan argues
   strongly for it (NVM3 baselines in the field are in the internal
   convention), but it does mean the wire and internal representations
   permanently disagree in sign. That is a deliberate, documented asymmetry
   and someone will eventually find it surprising.
2. **Should roll get an explicit convention statement too?** Roll already
   matches the product convention, so no code change — but §6 currently
   states neither convention. The plan proposes documenting both. Confirm.
3. **Item 2 architecture: separate telemetry value vs single source of
   truth.** The recommendation deliberately creates two tilt values (auto-
   level's averaged one, telemetry's own). That is the safe choice but it is
   duplication, and a future reader may try to unify them — which is exactly
   option O2.2, the rejected one. Is the duplication acceptable, and should
   the prohibition be recorded in `DECISIONS.md` so it survives?
4. **Idle cadence: piggyback the 1 Hz notify timer, or a dedicated timer?**
   The plan prefers piggybacking on radio-scheduling grounds, but that
   couples sampling cadence to notification cadence. If those should be
   independently tunable, a dedicated timer is the cleaner design and the
   worse risk.
5. **`tilt_fresh` staleness threshold.** Proposed ~2.5–3 s against a 1 Hz
   idle cadence. Not derived from a requirement — pulled from "comfortably
   above the slowest cadence". Should be set by what the app's UX needs.
6. **Sequencing of the app-side `TODO(tilt-fresh)` revert.** Proposed
   firmware-first. That leaves a window where firmware sends a truthful bit
   and the app ignores it. Acceptable, or should they land together?
7. **Does Item 2 need its own `DECISIONS.md` entry before implementation,
   or after?** The plan says it "probably warrants" one implementing #17. Not
   written — see NOT DONE.

---

## g. NOT DONE

| Item | Reason |
|---|---|
| Item 1 implementation | Explicitly out of scope: "PLANNING ONLY. Do not modify any source file." |
| Item 2 implementation | Same, and additionally gated on review of this plan. |
| `BLE_GATT_DESIGN.md` §6 amendment | It is a deliverable *of the plan*, not of the planning session. The exact proposed text is in §b. |
| `DECISIONS.md` entry for Item 2 | Identified as probably needed (§b, "Recommended sequencing" item 3); not written. Raised as review question 7. |
| Any bench measurement | The session cannot build or flash (`CLAUDE.md` hard constraint). Every timing claim is therefore ASSERTED — see A1–A5. |
| Profiling of the double-precision math cost | Would need the bench. It is the load-bearing unknown in Item 2 and the first thing to measure. |
| Loop-rate measurement on the manual-move path | Same. Does not exist for any build; the only measured figure (~2.31 ms/pass) is auto-level. |
| Verbatim capture of the `docs/SESSION_LOG.md` diff | The requirement did not exist during the planning session. Noted as a gap in §c rather than back-filled. |
| End-of-session `git status` | Same. §a carries a marked reconstruction instead. |
