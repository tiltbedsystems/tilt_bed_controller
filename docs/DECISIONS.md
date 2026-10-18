# DECISIONS.md

Design decisions from design review sessions, recorded 2026-07-04.
These bind future work in [REFACTOR_PLAN.md](REFACTOR_PLAN.md) — where a
decision here conflicts with older text in the plan or in
[SETPOINTS_INVENTORY.md](SETPOINTS_INVENTORY.md), this document wins.

---

## 1. Collision detection architecture (design brief for refactor step B5)

Collision detection is three coordinated elements. **Any element tripping
stops the motors.**

- **(a) Rate-of-change element** — evaluated per motor AND summed across
  motors: trips when the current rise within a trailing time window exceeds a
  threshold. Provisional starting values from bench data (captured with an
  uncalibrated timebase, so sample counts do not map cleanly to time):
  ~160 mA raising / ~110 mA lowering per 250 samples, with a 30-sample
  confirmation counter, armed after the startup/inrush window. **Final
  thresholds must be set from timestamped captures** — see decisions 2 and 5.
- **(b) Fixed margin over armed baseline** — the existing mechanism, kept for
  slow/soft obstructions (pillows, boxes) that never produce a fast rise.
- **(c) `MAX_LOAD_CURRENT` absolute ceiling** — last-resort stall protection,
  independent of baselines and rates.

**Rejected: unloading-based detection** (detecting a collision on one side by
the load *dropping* on other motors). Bench data shows lowering current is
friction-dominated — Down ≈ 75–90 % of Up — so load transfer barely registers
in the current signal. Do not revisit without new sensing hardware.

## 2. All detection timing is defined in milliseconds, never sample counts

Every detection window, confirmation counter, and timing element in collision
detection (and new timing logic generally) must be defined in **milliseconds
via the sleeptimer** — never in loop-iteration or sample counts. The refactor
will change loop cadence, and sample-count-based tuning would silently
invalidate without any code appearing to change. This is why the provisional
values in decision 1 (per-250-samples) are explicitly provisional.

## 3. Filtering is not a problem to solve

Bench data shows sensor/ADC jitter of only 0.4–0.6 mA/sample. The visible
variation in the current traces is **real mechanical load wander**:
150–650 mA over a stroke, weakly correlated between motors, with roughly 3×
spread between the quietest and loudest motor. Detection design must be
robust to this wander (hence per-motor thresholds, decision 4). **Do not add
heavier filtering** — it would only add detection latency while hiding
nothing that matters.

## 4. Per-motor learned rate thresholds

A calibration routine runs each motor up and down for a few seconds on a
known-clear bed, measures each motor's worst natural current rise over the
detection window, and sets that motor's rate threshold to a margin factor ×
the measured value. Constraints:

- Clamped within firmware-enforced min/max limits (per the safety-critical
  section of SETPOINTS_INVENTORY.md) regardless of what calibration measures.
- Stored in NVM3.
- The capture is validated: if it contains collision-magnitude events, the
  result is rejected and calibration reruns.

This routine folds into the orientation-calibration wizard (decision 7).

## 5. Data-capture tool — build early, before B5

A debug mode that streams timestamped CSV over the debug UART:
`time_ms, motor, filtered_current_mA`. Needed for bench characterization of
multiple actuators and for setting the final rate thresholds (decision 1).
Build this **before** starting refactor step B5 so the thresholds that ship
in the new detector come from calibrated-timebase data, not the provisional
per-sample numbers.

## 6. Hall-effect position sensing (future generation)

- Single Hall channel per actuator, 4 total.
- Direction is inferred from the commanded direction, not measured.
- Pulse counting is done by a **companion MCU** that reports over UART/EUART.
- Position is re-zeroed at inferred limit stops.
- Single-channel counts are trusted only while the motor is being driven.
- Quadrature is not required, but if the chosen actuator provides a second
  channel, wire it and take it.

## 7. Orientation-calibration wizard (app era)

A guided setup wizard: per-actuator test pulses correlated with accelerometer
response solve both the actuator-to-corner mapping and the accelerometer
mounting orientation. Motor characterization (decision 4) runs in the same
wizard. The resulting mapping is stored in NVM3, and **all logic above the
mapping layer speaks logical corners** (HEAD/LEFT/RIGHT/FOOT), never raw
output numbers.

## 8. Hardware revision list (next board spin)

- 8 A continuous current path: FETs, shunt values / INA2180 gain, copper,
  connectors, fusing.
- Companion Hall-counter MCU (decision 6).
- I2C constant-current LED driver (PCA9955-class) on the Switch Board for
  dimmable LEDs.
- Future differential bus (RS-485) for off-board comms, replacing the I2C
  re-drivers.

## 9. PWM speed control — deferred

Deferred until after the rate-element detection (decision 1a) exists, because
duty-cycle changes reshape every current signature. Investigation order:

1. First investigate slow-vs-fast decay mode on the MP6522 motor drivers.
2. True proportional speed control is targeted for the Hall-sensor generation
   (decision 6), where position/velocity feedback exists.

The existing pulse mode ("sneak" leveling) stays as-is for now.

## 10. SDK migration timing

Any Gecko SDK migration happens at refactor step **D1** — when the BLE
components are added — after Phase C is complete and a full regression pass
([REGRESSION.md](REGRESSION.md)) is green. **Never before.** Until D1 the SDK
stays pinned at 4.2.2 exactly as CLAUDE.md requires.

## 11. Direction-aware per-pair supervision — deferred to after B5

Today collision supervision runs one summed rate/margin check for the
whole bed and applies a single raise-vs-lower margin per tick (via
`is_raising`), chosen from the button direction. Planned upgrade,
sequenced **after B5** (the supervision deduplication) so it is not
entangled with safety-critical refactoring:

- **Per-motor-pair direction awareness**: within one tick, a raising pair
  uses the raise margins and a lowering pair uses the lower margins —
  needed for auto-level and any move where one pair goes up while another
  goes down.
- **Split the summed rate-of-change element (decision 1a) into two
  per-direction summed elements** — one summed over the raising motors,
  one over the lowering motors — instead of a single all-motor sum. **The
  split-summed approach is preferred over applying a lowering weight/scale
  factor** to a single combined sum.
- **Scope exclusion: ALL-UP and ALL-DOWN keep the single combined sum.**
  The split-summed approach applies only to auto-level and any
  mixed-direction move (some motors raising, some lowering in the same
  tick). When ALL-UP or ALL-DOWN is pressed, all four motors move the same
  direction together, so there is nothing to split — they continue to use
  one summed element across all four motors, exactly as today. The
  direction-aware split is for the case where the summed elements would
  otherwise mix raising and lowering current signatures together.
- **Enables targeted per-pair collision relief**: reverse only the pair
  that collided, rather than all motors.

This is a capability addition with its own bench tuning (new per-direction
thresholds from timestamped captures, per decisions 2 and 5), not a
refactor. Until it lands, B5 keeps existing behavior: auto-level
supervises with `is_raising = true` (raise margin), exactly as before.

## 12. Command sources and the source lock

Exactly **two command sources**: **physical switches** (main + aux switch
boards together are ONE source) and **app** (all bonded phones collectively
are ONE source). A *command* is any user action — HEAD/LEFT/RIGHT/FOOT
up/down, ALL up/down, AUTO LEVEL, LIGHT. When the bed is idle no source is
active. When any source starts a movement or auto-level it becomes the
active source and holds a lock.

**Stop-from-anyone rule.** While a source holds the lock it controls
normally. Any input whatsoever from the OTHER source — movement,
auto-level, light, settings, any button — is interpreted ONLY as an
immediate clean STOP; it does not perform that button's normal function. A
non-active source can do exactly one thing: stop the bed. Stop always
succeeds from any source and is never refused. Once the bed stops and the
lock releases, the next input from any source becomes the new active source.

**Auto-level cancellation** falls out of the above: a manual command from
the active source cancels its own auto-level and takes over; any input from
the non-active source stops everything. With hold-to-run wall switches this
is an app-era latched-command behavior (a switch user releases AUTO-LEVEL to
stop it); it lands with the app in D1.

**Implementation note (C2 → C4):** C2 puts the lock and the command verbs in
`bed_control.c` but leaves the movement/settings state (`active_switch`,
timers, LED helpers, flags) in `app.c`, with the verbs calling small
app-side action functions — a deliberate mediator back-call to avoid risky
state surgery on the safety path. C4 ("split app.c") dissolves that back-call
by moving the state and action functions into a **dedicated `bed_actions.c/.h`
module** — deliberately NOT into `bed_control.c` itself. `bed_control.c`'s
`arbitrate()` is pure logic (touches only the active-source lock and a single
idle check) with zero hardware/SDK dependency, which is what lets
`test/bed_control_test.c` host-compile it and test the two-source arbitration
in isolation. That test is the only automated coverage of the source lock,
and the app (D1) will be the first thing to actually exercise the
second-source FORCE_STOP path on real hardware — so keeping `bed_control.c`
decoupled from the hardware action layer is a permanent architectural
choice, not an artifact of C2's temporary back-call.

## 13. Collision/limit supervision sits below the command API

`run_motor_supervision()` (B5) runs in the main tick, beneath the command
API. Every source inherits it automatically; no command path — switch or
app — can start motion that bypasses collision/limit detection. The command
API sets movement *intent*; supervision runs unconditionally in the tick.

## 14. BLE connection policy (D1 — recorded now, NOT built in C2)

The power board bonds with multiple phones but maintains only ONE active
connection at a time. First phone to connect holds control; a second
connecting phone enters read-only telemetry mode with a clear UI indicator
that control is held by another device. All phones collectively remain the
single "app" source (decision 12).

## 15. Deferred app feature list (app era — recorded, NOT built in C2)

- App mirrors all controller buttons.
- Settings menu (with a warning that controls are disabled while settings
  are changed): auto-level setpoint (manual entry or capture-current-
  position), raise/lower collision thresholds, and a calibration wizard
  that also collects actuator stroke and rough bed length/width for future
  calculations.
- Telemetry displays: instantaneous and windowed total current draw; a
  graphical estimate of collision-object location from per-actuator current
  rise; current bed tilt; and (with Hall sensors) bed position.

## 16. RESET is hardware-only — deliberately omitted from the command API

The physical RESET is a **hardware reset line** for recovering a locked-up
processor. A firmware/BLE command cannot safely replicate it: a hung
processor can't run a reset command, and a BLE command arrives over a stack
running on the very processor being reset. RESET is therefore intentionally
**omitted from the command API and the app** — there is no `bed_reset` verb.
Recorded so it is not revisited.

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

App movement is hold-to-run, like the switches. While a movement button is
held, the app re-sends the identical `MOVE_START(target, dir)` every
**100 ms**; the firmware treats an identical repeat as a watchdog refresh
and performs a **clean full stop** (the `app_stop_all()` path) if **300 ms**
elapse without one. Both constants are milliseconds via sleeptimer (#2).

**Rejected: a separate KEEPALIVE opcode** — repeating the intent itself is
self-healing (a lost START is corrected by the next repeat, a lost STOP by
the watchdog). **Rejected: relying on the BLE supervision timeout** — it
detects a dead radio link but not a frozen app with a live link; the
app-layer watchdog covers both. AUTO_LEVEL and LIGHTS are latched (#12) and
never arm the watchdog; auto-level remains supervised below the API (#13).

**Control-connection disconnect while APP is the active source is a clean
full stop, auto-level included** — `bed_move_stop()` alone is insufficient
because it does not cancel auto-level; the disconnect handler must use the
stop-all path.

## 19. Pairing window is physically gated

New BLE bonds are accepted **only while settings mode is active on the
physical switch panel**. Outside that window, pairing/bonding requests are
rejected. Rationale: the bed has no display or passkey entry, so pairing is
Just Works (no MITM protection); gating enrollment behind physical access
to the switch panel is what prevents a stranger within radio range from
bonding. Up to 8 bonds stored in NVM3. All GATT characteristics require an
encrypted, bonded link.

## 20. SDK lands on Simplicity SDK 2026.6.0 LTS at D1 — via fresh-project port, not in-place upgrade

The D1 migration (#10) targets **Simplicity SDK 2026.6.0 LTS** (released
June 2026; 30-month maintenance window, June-LTS annual cadence thereafter).
**Rejected: staying on the Gecko SDK line** — Silicon Labs has closed GSDK
to Series 2 devices: GSDK 4.5 LTS is scoped to Series 0/1 only, GSDK 4.4's
maintenance window has lapsed, and the BGM220 is Series 2. A shipping
product with a radio cannot sit on a BLE stack that will never again
receive bug or security fixes; OTA (D2) exists precisely to deliver such
fixes, and there are none to deliver on a frozen SDK.

**Migration mechanics — the load-bearing part of this decision:** do NOT
upgrade the existing 4.2.2 project in place. Instead:

1. The 4.2.2 project stays untouched on its own branch — building,
   hardware-verified, known-good hex retained.
2. Create a fresh project from the **"Bluetooth – SoC Empty"** sample for
   the BGM220SC22HNA2 on SiSDK 2026.6.0 — the radio, advertising, and OTA
   DFU support arrive already configured and building.
3. Port the application sources into it; re-enter pin/peripheral config
   through the configurator using the old `.slcp`/`.pintool` as the record.
4. Run the **full REGRESSION.md pass on the ported project before writing
   any BLE glue**, isolating migration regressions from BLE regressions.

Contingency: if the SiSDK port hits a genuine wall, the fallback is adding
BLE on the final GSDK 4.4.x as a temporary unblock — accepted as
technical debt with a recorded plan to complete the SiSDK port, never as
the shipping end-state. Note the SiSDK 2025.12+ delivery change (modular
Conan/SLT packaging) — install 2026.6.0 and build the stock SoC Empty
sample as a standalone verification step before any porting begins.

**Note, added 2026-07-19:** item 4's pre-BLE isolation pass
(`REGRESSION_RECORD_D1.md`) was never run — BLE Stage B glue landed first —
and is superseded for gating purposes by the post-BLE, phone-subscribed full
pass recorded in `REGRESSION_RECORD_PRE_STAGEC.md`, which is the actual gate
now in force for BLE Stage C.

## 21. Viewer auto-promotion

Two connections maximum (#14): first bonded phone = CONTROL, second =
VIEWER. Viewer command writes are rejected firmware-side —
**except STOP_ALL, which is always honored from any connection** (the BLE
embodiment of #12's stop-from-anyone rule). When the control phone
disconnects, the **viewer is automatically promoted to CONTROL** and
notified via the per-connection Client Role characteristic; requiring a
reconnect to gain control would read as a bug.

## 22. Fast-connect posture: always-on fast advertising

The board is powered from the van battery bank, so low-power advertising
economics do not apply. Advertise **continuously at ~30 ms interval**
whenever a connection slot is free (including while one phone is
connected, for the viewer slot). On connect the peripheral requests a
**15–30 ms connection interval** (iOS cannot set this from the app side;
Apple accessory guidelines compliant), latency 0, supervision timeout 1 s
— link-loss safety is #18's watchdog, not the supervision timeout. Target:
under ~1 s from app-foreground to live control on a bonded phone.

## 23. One generic Settings RPC characteristic, not per-setting characteristics

BLE settings access mirrors `bed_settings.c`'s key table through a single
request/response characteristic (`{op, id, value}` write → indicated
response carrying result + value + min/max/default). New settings require
zero GATT changes. GET_META returns firmware-true min/max/default so app
sliders are always built from the firmware's limits, never hardcoded.
`bed_settings_set()`'s reject-don't-clamp validation (C1) remains the
safety authority. SET/ERASE accepted only from the CONTROL connection and
only while the bed is idle. The calibration wizard (#7) is explicitly NOT a
settings write — it is a future latched command sequence with its own
opcodes.

## 24. Telemetry is subscription-driven (implements #17)

The Bed Status characteristic notifies at an adaptive cadence: nothing with
no subscribers, 1 Hz idle (plus immediate on state-flag edges), 10 Hz while
moving or auto-leveling. Continuous accelerometer sampling — the #17
deferral — runs only while at least one connection has Status notifications
enabled, at the same adaptive cadence. The status packet carries a
`tilt_fresh` flag; the app greys out the tilt display when it is stale
rather than presenting a last-known angle as live. The 20-byte status
packet is a deliberate ceiling: it fits a notification at the default ATT
MTU (23), so telemetry works even before MTU exchange.
## 25. PWM topology — one motor per timer (settled at D1, replaces the shared-TIMER0 hand-edit)

**Context.** Through the 4.2.2 firmware, three motors shared TIMER0 (CC0/CC1/
CC2 on PB04/PB03/PB02) with motor4 alone on TIMER1 CC0. The Silicon Labs
`sl_pwm` component models one instance as *one timer + one CC channel* and
has no concept of multiple CC channels off a single timer, so the shared-
TIMER0 arrangement required hand-editing the generated
`sl_pwm_init_motorX_config.h` files. Any configurator regeneration
overwrites those hand edits — captured by the WARNING block in the old
`app.c`. This limitation persists in SiSDK 2026.6.0 (confirmed in the Pin
Tool: PB02/PB03/PB04 all collapse onto a single `motor2` instance).

**Decision.** Give each motor its own timer. The BGM220's EFR32BG22 die has
five general-purpose timers (TIMER0 32-bit; TIMER1–4 16-bit; all 3-channel,
all EM1, all PWM-capable), and the BLE stack uses the radio protimer / the
secondary RTC — not the general-purpose TIMERs — so all five are the
application's to use.

Mapping (as actually entered in the Pin Tool, 2026-07-05). One general-
purpose timer is reserved by a component in the SoC Empty base (likely
sleeptimer), and **which one it reserves proved unstable across
configuration changes** — during entry the reserved timer shifted from
TIMER3 to TIMER4. The mapping below is what the Pin Tool accepted and is
the record; the essential property is one dedicated timer per motor, not
the specific timer numbers:

| Motor | Timer | Pin |
|---|---|---|
| motor1 | TIMER0 | PB04 |
| motor2 | TIMER1 | PB03 |
| motor3 | TIMER2 | PB02 |
| motor4 | TIMER3 | PC06 |

(TIMER4 is the reserved/unavailable one in the final configuration. TIMER3
and TIMER4 are functionally identical — 16-bit, 3-channel, PWM-capable —
so the swap has no functional consequence.)

**Consequences.**
- Each motor is a clean single-instance `sl_pwm` with auto-generated config.
  **No hand edits.** Regeneration (frequent during BLE work) can no longer
  corrupt PWM config. The old `app.c` WARNING block is deleted, not ported.
- Public PWM API is unchanged (`sl_pwm_set_duty_cycle()` etc.); motor logic
  in `dual_motor_control.c` should be minimally affected because it
  references instances, not raw timer registers. **Port task: grep for any
  hardcoded `TIMER0`/`TIMERx` references or any assumption that the three
  motors share a counter (e.g. synchronized edges) — independent linear
  actuators do not need edge sync, but flag anything found.**
- PWM frequency stays **24 kHz** — inside the MPS-recommended 20–100 kHz
  window for the motor drivers — carried over unchanged.

**Verification (own step within the D1 regression, keeping the SDK move and
the PWM re-topology as separable variables).** In the REGRESSION.md pass,
exercise **each motor individually** (motor1 up/down alone, then motor2,
motor3, motor4) confirming the correct actuator moves at the correct speed
and direction. This is the explicit check that the new per-timer mapping is
wired correctly, isolated from the rest of the migration.

**Relationship to #9.** Decision #9 (PWM speed control deferred to the
Hall-sensor generation) is unchanged. #25 is only about the *timer
allocation infrastructure*; it does not introduce proportional speed
control, which #9 still defers.

**Clarification on the "overwrite" mechanism (correcting an earlier loose
statement).** The config-header overwrite is not caused by Bluetooth
specifically — it is caused by *any* configurator regeneration of
`autogen/`/`config/`. Bluetooth is relevant only because active BLE
development means many more configurator touches, which is what makes the
old hand-edit workaround an ongoing liability and motivates settling this
now.

## 26. Motor driver fault telemetry — interim polled, interrupt path unfinished, real motor protection is elsewhere

**Context.** Commit 62773f6 found `bed_status_get()` calling `get_motor_faults()`
— a blocking `I2CSPM_Transfer` on `sl_i2cspm_on_board`, the same bus the MC3479
accelerometer uses — from inside a BLE stack event callback, which stalled the
super loop long enough to drop the BLE link. The fix hardcoded the Bed Status
packet's per-motor fault bit (§6) to 0 rather than restore a blocking call.

**Restored (this session), interim.** `bed_status_get()` now reads
`get_cached_motor_faults()` (`exp_board.c`), a non-blocking accessor over a
value sampled by `sample_motor_faults_tick()` — called once per
`app_process_action()` pass in main-loop context (never from a stack
callback), rate-limited to once per **200 ms** via `sl_sleeptimer` (ms-based,
#2) so it doesn't add a second per-tick blocking I2C transaction alongside
the accelerometer's on the same bus during auto-level.

**This is explicitly interim, not the final answer:**

- Polling reads the expander's **instantaneous** input state. A fault
  shorter than roughly 200 ms is missed entirely between samples — there is
  no latching between polls. The expander (`init_board_expander()`,
  `exp_board.c`) has interrupt-latch registers meant to catch exactly this;
  they were never configured (existing comment: "Configure latch registers
  once signal interrupt working to respond to potential motor fault from
  driver IC's").
- At 200 ms, the moving-state BLE status notify cadence is 100 ms (§6), so
  up to **two consecutive notifications can carry the same stale fault
  value**. Acceptable for a slow-developing driver fault (over-current/
  over-voltage/thermal shutdown per the MP6522 nFAULT behavior), not for a
  fast transient.
- The gap this closes is bigger than just the cache: before this session,
  `get_motor_faults()` had **zero callers anywhere** in the firmware, and
  `sl_button_on_change()` (`switch_input.c`) does not dispatch the
  `fault_int` Simple Button instance at all — it only handles `ui_int`, with
  an existing TODO comment ("Will need to expand logic to handle motor fault
  as well"). So end-to-end motor-fault handling — hardware pin through to
  any firmware response — has never been functional. `sample_motor_faults_tick()`
  gives `get_motor_faults()` its first caller and makes the fault bit
  live in telemetry; the interrupt-driven path (expander latch config +
  `fault_int` dispatch) remains unbuilt.

**This does not affect actual motor protection.** The MP6522 nFAULT pin is a
driver-IC self-report (over-current/over-voltage/thermal shutdown) surfaced
here purely as diagnostic telemetry for the app. The firmware's actual motor
protection — `max_load_check()`'s `MAX_LOAD_CURRENT` ceiling and the
rate-of-change/margin collision detection (#1) — runs independently off the
current sense path (`motor_current_functions.c`) at the full ~1 ms
supervision cadence and is untouched by any of the above.

**Open design question, deliberately not answered here.** Should a
driver-reported fault (over-current/over-voltage/thermal shutdown) actively
stop the motors, or remain diagnostic-only telemetry as it is today? This is
a real safety-relevant decision — not something to inherit by default from
whatever the interim polling happens to support — and should be made
deliberately in a future session, alongside deciding whether to finish the
interrupt-latched path first.

## 27. Auto-level's BLE-drop bug is OPEN and UNDIAGNOSED — this entry previously claimed otherwise, and that claim was wrong

**RESOLVED — diagnosed and fixed, see #30.**

**Status, corrected 2026-07-17.** The auto-level sustained-move BLE drop
(supervision timeout, `connection_closed reason=0x1008`, during a sustained
hold from off-level) is **open and undiagnosed**. It reproduces on current
code (bench-confirmed again 2026-07-17). 62773f6's original note —
"confirmed still present and untouched — not part of this fix" — was and
remains accurate. **This entry, as first written, asserted the opposite**
("bench testing this session confirmed auto-level no longer drops the
link") and that assertion was wrong. It is being corrected here rather than
silently rewritten so the error is visible, not just erased.

**Why the earlier version of this entry was wrong — stated plainly, since
this is the mistake, not a footnote to it.** It was written in `68d1d13`, a
**docs-only commit** — three files under `docs/`, zero lines of code changed
(`git show --stat 68d1d13`). Nothing changed in the firmware between 62773f6
(where the bug was last actually observed and recorded as present) and
`68d1d13` (where this entry declared it fixed) — there was no fixing build
for a bench claim to have been run against, because no build existed that
differed from the one 62773f6 already characterized as still dropping the
link. The claimed mechanism wasn't just unverified, it was **inconsistent
with the code**: it asserted `get_motor_faults()` ran "on literally every
tick" during the auto-level runs 62773f6's own bench session found still
dropping the link — but `ble_bed_service_tick()` early-returns whenever
`status_subscriber_count == 0` (`ble_bed_service.c`, near the top of that
function), and 62773f6's own root cause #1 is that `status_subscriber_count`
stayed stuck at 0 after any reconnect using a resumed bond — precisely the
state 62773f6 was diagnosing. `bed_status_get()`, and therefore
`get_motor_faults()`, could not have executed during those runs. A call that
never ran cannot have caused, and cannot have later stopped causing, the
drop that was observed. What happened was a same-root-cause inference —
plausible-sounding, tidy, wrong — written up as a verified fact without ever
running the build it described.

**Do not bisect `ef55454` (fault polling) or `62773f6` (notification-count
fix) looking for a regression either one introduced into auto-level.** Both
postdate a bug that was already present *at* 62773f6 (per 62773f6's own
bench note); neither can be its cause. See the accelerometer-I2C-rate
bisection (source-visible `BED_BISECT_ACCEL_I2C_RATE_LIMIT` flag,
`auto_level.c`) for the active investigation into the real cause.

**The general rule from the original version of this entry, unaffected by
the correction above and still correct — kept verbatim:**

- **Never call anything blocking from a BLE stack event callback**
  (`sl_bt_on_event` / `ble_bed_service_on_event` and anything they call
  synchronously). If a callback needs data that requires I2C, NVM3, or any
  other wait, it must set a flag and let a main-loop tick function
  (`app_process_action()` and the `*_tick()` functions it calls) do the
  actual work on the next pass. `bed_status_get()`'s fix (#26) and the
  existing `status_notify_due` flag pattern in `ble_bed_service.c` are the
  worked examples to copy.
- **Never add a new per-tick blocking I2C transaction on
  `sl_i2cspm_on_board`** (the accelerometer's bus) without checking what
  else already runs on that bus every tick during the states that matter
  (auto-level, zero-level capture in settings mode). `accel_running_average()`
  already owns that bus at full tick rate during those windows; anything
  else added there is additive load, not free, even from correct
  (non-callback) main-loop context — see #26's 200 ms rate-limiting for the
  pattern of gating a second consumer of that bus.
- Both bugs were invisible until BLE Stage B put the radio in continuous use
  during ordinary bed operation. Now that it's always active (not just
  during a dedicated BLE test), this class of bug can surface anywhere a
  future change touches the main loop, the accel bus, or a stack callback —
  see Task 2's audit (session notes, not a doc) for the sweep this prompted
  and DECISIONS.md's own bar going forward: a blocking call in a stack
  callback, or a new per-tick transaction on the accel's bus, should be
  treated as a specific, callable-out risk in any future review, not
  something that only gets caught by re-deriving this history from scratch.

**New rule, added by this correction — this is the second citation defect in
this document (the first being the stale-but-accurate 62773f6 note this
entry originally mishandled the other direction), so it is being stated
explicitly rather than left implicit:** **a fix is not "verified" until it
has been bench-run on the build that actually contains it.** Same-root-cause
reasoning — "these two symptoms look like they'd share a cause, and we fixed
the cause" — produces a hypothesis worth testing, never a verification by
itself, no matter how tidy the unifying story is. A docs commit recording a
bench result must correspond to an actual bench run on the code as it stood
at that commit; a docs commit must never *upgrade* a prior bench observation
to a different conclusion without new bench evidence of its own. When those
two things try to conflict, the bench observation wins and the docs commit
is the thing that's wrong — as it was here.

## 28. BLE bonding: a fresh pairing request against an existing bond auto-replaces it

**The bug this fixes.** Bench-confirmed 2026-07-15: a peer whose address the
firmware resolves to an existing bond (`sl_bt_evt_connection_opened`'s
`bonding` field != `0xff`), but which has no matching bond of its own (a
lost/forgotten/factory-reset phone, or a new phone), fails to pair —
`sl_bt_evt_sm_bonding_failed reason=0x1205` (`SL_STATUS_BT_SMP_PAIRING_NOT_SUPPORTED`),
phone disconnects (`reason=0x1013`, `REMOTE_USER_TERMINATED`). Verified this
was a **permanent, customer-unrecoverable lockout**: no settings-mode
gesture, no BLE command (every characteristic requires the very bonded link
that's broken), and no silent stack-side eviction clears it. The only
existing recovery was a manufacturer SWD/J-Link reflash with the bench-only
`BLE_BENCH_WIPE_BONDS` flag enabled, wiping *all* bonds, not just the stuck
one.

**Decision.** A fresh pairing request from a peer that resolves to an
existing bond MUST replace that bond, not fail. Implemented in
`ble_bed_service.c` by setting `sl_bt_sm_configure()`'s bit 3
(`SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED`), which makes
`sl_bt_evt_sm_confirm_bonding` fire for every bonding attempt instead of
never firing; the handler confirms unconditionally, which the event's own
SDK doc describes as replacing an existing bond ("a bonding already exists
... overwriting is a potential security risk").

**Security analysis — why this is safe. Record this verbatim; do not
re-litigate auto-replace without seeing this reasoning first.** The security
boundary is #19's pairing window, not the replace policy. Auto-replace does
not widen the attack surface: a stranger's phone resolves to no existing
bond, so it becomes a *new* bond — which `set_bondable_mode(1)` already
permits today regardless of replace policy. The narrow residual risk is an
attacker spoofing an already-bonded phone's identity to force a re-pair —
but that requires having already compromised that phone (its IRK), and Just
Works pairing (this product's threat model, #19 — no display/passkey
hardware, no MITM claim) provides no MITM protection either way, replace
policy or not. Once #19 gates *all* bonding — new and replace alike — behind
a physically-opened pairing window, auto-replace is safe by construction:
physical access to the switch panel is the real gate, not whether an
existing bond record happens to get overwritten. Auto-replace's actual
benefit is removing a separate un-bond step for the user, not removing the
need for physical access — that need doesn't go away and isn't being
argued away here.

**Status: implemented, not yet bench-verified as of this entry.** This was,
and until confirmed on the bench remains, a **hypothesis** about the SDK's
undocumented behavior with bit 3 clear: no text was found in the installed
SiSDK 2026.6.0 headers stating outright that a bit-3-clear configuration
auto-rejects a re-pair against an existing bond (the inference is well
evidenced — see the investigation this decision is based on — but it is not
a quoted guarantee). Given #27's correction immediately above, this status
line is deliberately explicit rather than assumed away: do not treat this as
resolved until an actual bench pairing test against a real stale-bond/
no-bond mismatch has been run and reported.

**Single decision point for #19.** The `confirm_bonding` handler
(`ble_bed_service.c`) is deliberately the one place this logic lives: today
it always confirms; when #19's physical pairing window lands, the same
handler gains one condition (confirm only if the window is open, reject
otherwise) rather than a second mechanism being built elsewhere.

## 29. Auto-level's filter constants are calibrated in loop passes, not time — and the same problem reaches the collision-detection safety path

**The finding.** `NUM_ACCEL_READINGS` (130, `auto_level.c`) is consumed once
per super-loop pass in `accel_running_average()`, so the running-average
window is `130 / loop_rate`, not a fixed duration. `accel_alpha` (0.1),
applied per call in `accel_low_pass_filter()`, means the LPF's time constant
is loop-rate-dependent too. Neither constant is defined in milliseconds;
both are silently retuned by anything that changes loop timing, with no code
change anywhere near `auto_level.c` needed to do it.

**Bench-measured, not assumed (2026-07-17), during the #27 bisection.** The
`BED_BISECT_ACCEL_I2C_RATE_LIMIT` bisection (gating the accel I2C burst to
10 ms, see the #27 investigation) changed loop timing *by accident* as a
side effect of the thing it was actually testing:

| | Rate limit ON | Rate limit OFF (back-solved) |
|---|---|---|
| Loop rate | ~630 passes/sec (1.587 ms/pass) | ~433 passes/sec (2.31 ms/pass) |
| 130-sample averaging window | ~206 ms | ~300 ms |

That's a **45% loop speedup and a 31% shrink in the averaging window**,
produced by a change whose stated purpose was "reduce I2C traffic," not
"retune the control law." Nothing about auto-level's leveling behavior was
touched on purpose, and its filter behavior changed anyway. This is the
concrete demonstration of the general risk: as long as these constants are
counted in passes, *any* change to loop timing — a new peripheral, an added
per-tick check, a different power-manager policy — retunes auto-level
without anyone deciding to.

**The same problem reaches actual motor protection — open safety-relevant
question, not fixed here, not part of the BLE gate.** 1b2f8ee's message
states current-based supervision is calibrated around a "~1 ms/tick
free-spin cadence," with a "350-sample dynamic threshold" stated to occur
"within 303ms." The loop rate has now been measured for the first time
(above) — during auto-level, with per-pass accel I2C load. Applying the
without-rate-limit figure (2.31 ms/pass, the closer match to current shipped
behavior, since the bisection flag does not exist in production code) to
350 samples gives **~808 ms, not 303 ms — 2.7x the original design
assumption**. Per DECISIONS #26, current-based protection (`max_load_check`
+ the rate-of-change/margin collision detection, #1) is the *actual* motor
protection, not the driver-fault telemetry — so a design assumption that's
off by 2.7x on the timing of a "350-sample dynamic threshold" is worth
knowing about regardless of anything BLE-related.

**Caveat on scope, stated explicitly so this isn't overclaimed:** the
1.587/2.31 ms/pass figures above are auto-level-specific measurements and
include auto-level's per-pass accel I2C load (`accel_running_average()` /
`read_accel()`), which does **not** run during a manual switch-driven move —
`run_motor_supervision()` is shared by both (`motor_current_functions.c`),
but a manual move's loop rate is a distinct, not-yet-measured quantity, not
something this entry has numbers for yet. The 2.7x figure characterizes
auto-level's collision-detection cadence specifically; it should not be read
as a measurement of manual-move collision-detection timing, which is exactly
what the Task G bisection's manual-move loop-rate counter (`app.c`,
uncommitted bisection build) exists to produce. Until that number exists,
treat "how far off is collision detection's timing assumption during an
ordinary manual move" as unanswered, not answered pessimistically or
optimistically by extrapolation from the auto-level figures.

**The rule.** Safety and control timing must be expressed in milliseconds
and verified against a *measured* loop rate, never defined in loop passes
against an *assumed* one. A constant that is "N samples" or "N passes" with
no stated time budget is a constant nobody has actually calibrated — it is
calibrated against whatever the loop rate happened to be on the bench the
day the number was chosen, and silently drifts every time loop timing
changes thereafter, with no compiler warning and no test failure to catch
it.

## 30. Auto-level's BLE-drop bug — root cause confirmed by controlled A/B, fixed

**Root cause, confirmed, not inferred.** Unconditional per-pass blocking
accel I2C (`accel_get_xyz_raw()` via `read_accel()`) during auto-level's
motor-on EM0 free-spin (`app_is_ok_to_sleep()` vetoes sleep while a motor is
on) accumulates enough bus occupancy on `sl_i2cspm_on_board` to starve the
BLE radio past its 1000 ms supervision timeout on a sustained hold
(`connection_closed reason=0x1008`). Confirmed by a controlled A/B on a
single binary, only the rate-limit flag differing: flag off → disconnects
reproduce; flag on → disconnects gone. Back-calculated bus occupancy: ~60%
with the accel I2C burst running every pass, ~13% rate-limited to the
sensor's actual 100 Hz output rate. This is what #27's own methodology rule
demanded and got — a fix is not verified until bench-run, here A/B-confirmed,
on the build that contains it. This entry is that verification; #27 is
marked resolved above rather than rewritten, per that same rule.

**Hang hypothesis explicitly refuted, not just unconfirmed.** Before landing
on occupancy as the mechanism, a competing hypothesis was tested and ruled
out: that a NACK/bus-glitch caused `I2CSPM_Transfer()` to silently spin its
bounded retry loop (`SL_I2CSPM_TRANSFER_TIMEOUT` = 300000 iterations,
confirmed by reading `platform/driver/i2cspm/src/sl_i2cspm.c` in the
installed SiSDK 2026.6.0) past the EFM_ASSERT that compiles out in a release
build. Bench instrumentation on the actual transfer path found **zero
failures of any kind** (NACK, bus error, arbitration lost, usage fault, SW
fault, or timeout) and a **measured max transfer duration of ~1.4 ms**, far
under any timeout threshold. The bug is bus occupancy, not bus reliability.

**Fix.** `auto_level.c`'s `read_accel()` rate-limits the I2C burst itself to
`ACCEL_I2C_MIN_INTERVAL_MS` (10 ms, matching the MC3479's 100 Hz output rate
— decimate-by-10 in `init_accel()`), reusing the cached `accel_vector[]`
between real reads. Everything downstream of the raw read — magnitude,
atan2, LPF, and `accel_running_average()`'s array/window — is unchanged and
still runs every super-loop pass on whichever sample is current; only the
I2C transaction itself is gated. This is deliberately the smallest change
that fixes the confirmed root cause.

**What this fix does NOT do — stays OPEN, tracked under #29.** Caching the
raw sample between I2C reads does not make `NUM_ACCEL_READINGS` or
`accel_alpha` any less loop-rate-dependent — the averaging window is still
"130 passes," not "N milliseconds." #29's time-based redesign of
`accel_running_average()`'s window is separate, control-behavior-changing
work, explicitly not part of this fix and not required to close out the BLE
drop.

## 31. Auto-level's sample buffer must never be fed by the telemetry sampler — the two tilt values are deliberately separate

Recorded **before** any Item 2 (continuous tilt sampling) implementation
exists, because the thing being prohibited is the change a future reader is
most likely to make on sight.

Item 2's accepted architecture (option O2.3) gives telemetry its **own** tilt
value and leaves `accel_running_average()`'s 130-slot buffer
(`accel_readings_roll[]` / `accel_readings_pitch[]` in `auto_level.c`) fed
only from the auto-level and zero-level paths, exactly as today. That means
two tilt values will exist in the tree at once. **The duplication is
deliberate. Do not unify them.**

### Why unifying breaks auto-level

The obvious "cleanup" is to make one sampler feed the shared buffer at one
decimated cadence and let both auto-level and telemetry read it. That was
evaluated as option O2.2 and rejected. The arithmetic:

```
NUM_ACCEL_READINGS          = 130 slots
telemetry cadence           = 100 ms  (10 Hz, the moving-state rate)
buffer span                 = 130 x 100 ms = 13 000 ms = 13 s

STEADY_ACCEL_READ_TIMER_TIME = 1500 ms
```

`steady_accel_read_both_axes()` calls the first `check_level()` after 1500 ms.
With the buffer spanning 13 s, that first check averages roughly 11.5 s of
history from **before the user pressed the button** — the bed's tilt while it
was being loaded, walked on, or moved by hand. `check_level()` sets
`level_status[axis].positive`, which `set_auto_level_motor_direction()` turns
into which pair drives up. So auto-level could pick its **first motor
direction from data predating the press**, and drive the bed away from level.
That is a safety-relevant misbehaviour, not a cosmetic one.

Today's cadence is what prevents this: the auto-level path samples every
super-loop pass, measured at ~2.31 ms/pass, so 130 samples ≈ 300 ms and the
buffer is fully overwritten about five times inside the 1500 ms window.
Nothing from before the press survives into the first check. **The margin is
the loop rate, and decimating the sampler destroys it.**

This is DECISIONS #29's failure mode — a constant calibrated in loop passes
rather than time — reappearing as a consequence of an unrelated refactor.

### The rule

- The telemetry sampler must not write `accel_readings_roll[]`,
  `accel_readings_pitch[]`, `accel_running_sum_*`, or `index`.
- If the auto-level sampling cadence is ever changed, re-check
  `NUM_ACCEL_READINGS x (sampling interval)` against
  `STEADY_ACCEL_READ_TIMER_TIME` **against a measured loop rate, not an
  assumed one** (#2, #29). It must stay comfortably below.
- Anyone proposing to collapse the two tilt values should read this entry
  first and produce the arithmetic above for their proposed cadence.

Status: Item 2 is accepted in principle (O2.3) but **not approved for
implementation**. No sampler exists yet. This entry is the prohibition only.

## 32. Telemetry tilt needs its own filter — open design question against Item 2

**CLOSED by #33.** The answer is: the same filter as auto-level (same
130-sample running average, same LPF, same per-pass rate), on separate
storage. Kept below as written — the reasoning about *why* an unfiltered
telemetry value would have been wrong is still the justification for sharing
the filter rather than inventing a lighter one. The ms-not-samples constraint
at the end still stands for any future change to either chain.

Recorded as open, not decided. Do not treat the absence of an answer here as
permission to skip the question.

Item 2's specification for the telemetry tilt value is, in full, "telemetry
keeps its own value" (#31). That says nothing about filtering, and the value
does **not** inherit auto-level's smoothing: `baseline_accel_average_*` is an
average over `NUM_ACCEL_READINGS` = 130 samples of an already low-pass
filtered input (`accel_alpha`), whereas a fresh telemetry value is whatever
the last sample said.

Consequences if it ships unfiltered:

- **Idle (~1 Hz):** a raw single sample per notification. Accelerometer noise
  and any vibration in the van show up directly as a jittering angle on the
  phone.
- **Moving (~10 Hz):** the display crawls and hunts while the bed is actually
  in motion, which is exactly when the user is watching it.

So the telemetry value needs a filter of its own. The constraint on its
design, and the reason this is recorded rather than left to implementation
time:

**Size it in MILLISECONDS, not in samples or loop passes** (#2, #29). Copying
`NUM_ACCEL_READINGS` or `accel_alpha` would import the exact loop-rate
dependence #29 exists to flag, into a value whose sampling cadence is
*designed* to change between idle and moving. A filter specified as "N
samples" would have a different time constant in each state.

Open sub-questions, none answered here:

- One time constant, or one per cadence state?
- What settling time is acceptable before the displayed angle is trustworthy,
  and does that interact with `tilt_fresh`'s meaning (#17, BLE_GATT_DESIGN #6)?
- Does the filter live in firmware or the app? Firmware costs code on the
  constrained side; the app can filter what it is sent but cannot recover
  detail that was never sampled.

Not designed here, by instruction. This entry exists so the question is not
rediscovered after the display is found to be jittery on a bench unit.

## 33. Live tilt telemetry — separate sampler, four triggers, and the first Command write path

The positive half of #31, recorded now that it is built rather than
prohibited. #31 says what must not be done; this says what was done.
Implements the app-era plan deferred in #17 and specified in
BLE_GATT_DESIGN #6.

### The problem

`accel_running_average()` ran only inside auto-level and the settings
zero-level capture, so `baseline_accel_average_*` — the values
`bed_status.c` ships in every Bed Status notification — were frozen at
whatever the last auto-level run computed, sometimes hours earlier. A user
levelling the bed by hand from the wall switch, which is the common case,
watched a dead number on the phone.

### One window, four triggers

There is **one** sampling window. While it is open the telemetry sampler
runs on every main-loop pass — the same rate auto-level's sampler runs at,
which is what makes the shared filter behave identically in both. The window
is open when **a motor is energised OR a burst timer is running**.

Four things arm the burst. Arming is idempotent and *extends* a running
burst (`sl_sleeptimer_restart_timer`), so bursts never stack:

| | Trigger | Armed from |
|---|---|---|
| T1 | `status_subscriber_count` 0 → 1 (app opens or reconnects) | `update_status_subscriber_count()` |
| T2 | a `REFRESH_TILT` command arrives | main-loop command handler |
| T3 | motors transition any-on → all-off | `tilt_telemetry_tick()` |
| T4 | auto-level | nothing added — its own sampler already covers it |

**T3 is the one that matters most and is the least obvious.** While motors
run, the accelerometer sees frame vibration and the actuators' own dynamic
acceleration, so the last in-motion sample is the *least* trustworthy of the
whole move — and it is exactly the sample the user would otherwise be left
staring at. The burst carries sampling past the end of motion so the number
they finally read is a settled one. Auto-level's own 1500 ms steady-read
exists for the same physical reason.

**Arming commands nothing.** It sets a timer. It cannot start, stop, or
delay a motor, and it cancels nothing. A movement starting mid-burst is not
a conflict: the window is already open, it stays open, and T3 re-arms when
that movement ends. There is deliberately **no arbitration** between the
sampler and the motion path, and none should be added — an interlock is the
obvious-looking change here and it would be the wrong one, because there is
nothing to interlock.

### Gates

- **Subscriber gate.** Nothing samples, and no burst arms, unless at least
  one connection has Bed Status notifications on. A switch-only bed with no
  phone is bit-for-bit what it was, including never vetoing sleep — which is
  what keeps the existing regression baseline valid for that configuration.
- **Other-sampler gate.** Skipped entirely while auto-level or the settings
  zero-level capture is running. Those already sample every pass; running
  both would put two samplers on the bus per pass and halve the effective
  averaging window in time.

### Storage is the constraint, not the algorithm

The filter is deliberately **shared**: same `NUM_ACCEL_READINGS`-deep running
average, same low-pass filter, same per-pass rate, so the app's number is as
smooth as auto-level's and the two cannot drift apart as one is tuned.

The **storage** is deliberately **not** shared. `accel_filter_state_t` holds
the buffer, index, running sums, LPF accumulator and outputs; there are two
instances and the shared code takes a caller-supplied pointer. Telemetry must
never write auto-level's buffer — that is #31, and the arithmetic for why is
there.

`read_accel()` was split as part of this: it now produces `latest_*_scaled`
and nothing more, with the low-pass filter moved into
`accel_filter_update()` so each chain filters into its own state. Auto-level's
arithmetic, ordering and per-pass rate are unchanged; its outputs are mirrored
back into the same globals every existing reader already uses.

### Burst duration

`TILT_TELEMETRY_BURST_MS`, 500 ms, in milliseconds via sleeptimer (#2), never
loop passes (#29). At the measured ~2.31 ms/pass that is ~200 passes,
comfortably more than the 130-slot filter needs to fill. Bench-tunable: longer
gives the filter more convergence margin and the frame more time to stop
rocking, but pins EM0 for that window; shorter risks freezing the display
before the bed has settled, which defeats T3.

### Sleep

`app_is_ok_to_sleep()` now also vetoes sleep while a burst is running. Without
it a standalone refresh — app connects, or REFRESH_TILT arrives, with the bed
sitting still — would arm a burst and have no loop to run on. This pins EM0
for the burst duration, bounded, and only ever while a phone is subscribed.

### Still deferred, deliberately

- **`tilt_fresh` stays hardcoded 0.** Between bursts the value genuinely *is*
  stale, so 0 remains honest, and the app's `TODO(tilt-fresh)` ungating stays
  correct. Implementing it only becomes meaningful alongside idle sampling.
- **Idle sampling** (the ~1 Hz "connected but nothing happening" cadence in
  BLE_GATT_DESIGN #6) is not built. The four triggers cover the cases a user
  actually notices; a periodic idle wake next to the radio's schedule is new
  behaviour that has never been on the bench, and it is separable.

### Why the write path opened with a non-motion opcode

`REFRESH_TILT` (0x07) is the first Command characteristic write this firmware
has ever handled. It was chosen to go first *because* it cannot move the bed:
it proves the whole chain — decode, validate, defer to the main loop, notify
Command Result — on real hardware before anything that drives a motor is
wired to it. Motion opcodes 0x01–0x06 are answered `REJECTED_INVALID` rather
than silently swallowed, so the app can distinguish "not built yet" from
"write lost".

**The handler does no work in the stack callback.** It copies four bytes, sets
a flag, and returns; the main loop acts one tick later. This is #27/#30: a
blocking call inside a Bluetooth callback is the confirmed mechanism behind
this project's `reason=0x1008` supervision-timeout drops, and REFRESH_TILT's
entire purpose is to trigger accelerometer reads — handling it inline would
have rebuilt that bug deliberately. Every opcode added later inherits the rule.

### Status

**ASSERTED. Not built, not flashed, not measured.** The acceptance run,
including a before/after loop-rate measurement on the manual-move path and a
re-run of REGRESSION.md §5 collision detection, is specified in
`docs/handoffs/fw_HANDOFF_2026-08-02_04_*.md`. #29 is why the loop-rate
measurement is part of acceptance and not a follow-up: collision timing is
loop-pass calibrated, so a change in passes/sec on the manual-move path *is*
a change in collision behaviour.

**Closes #32.** That entry asked how the telemetry value should be filtered.
The answer is above: the same filter as auto-level, on separate storage. The
concern behind it — that a raw unfiltered value would jitter at idle and
crawl while moving — does not arise, because the telemetry chain is the same
130-sample average and LPF fed at the same per-pass rate, giving the same
~300 ms window and the same smoothness. #32's constraint that any such filter
be sized in milliseconds rather than samples still stands for any future
change to either chain, and is unresolved for both of them equally (#29).

## 34. Stage C — the app drives the bed: all six opcodes, dead-man watchdog, arbitration

Implements BLE_GATT_DESIGN #4 (opcodes), #5 (watchdog) and #14 (roles).
Follows on from #33, which opened the write path with the one non-motion
opcode. The app can now move the bed.

### Nothing reimplements bed logic

Every opcode routes through the existing `bed_control.c` verbs with
`BED_SOURCE_APP` — the same verbs `switch_input.c`'s `process_inputs()` calls
with `BED_SOURCE_SWITCHES`. The two-source lock (#12) and the collision/limit
supervision underneath it (#13) therefore apply to app commands for free,
with no BLE-specific safety code.

### The gap that made this more than plumbing

`bed_move_start()` latches direction and `active_switch`, but the thing that
actually *energises* a motor is `switch_input.c`'s `process_outputs()`, and
that runs off `app_ui_interrupt` + `switch_pressed` — a physical switch edge.
An app command has no edge, so routing an opcode through the verb alone would
have set all the state and moved nothing.

The app path therefore applies its own outputs, via helpers added next to the
switch path's equivalents in `bed_actions.c`
(`app_apply_move_outputs()`, `app_apply_auto_level_start_outputs()`,
`app_apply_lights_outputs()`, `app_apply_stop_outputs()`). Each mirrors the
matching branch of `process_outputs()`.

One consequence is easy to miss and is why `app_release_active_switch()`
exists: **no switch-release event will ever arrive for an app command.**
`process_outputs()` is what normally returns `active_switch` to `OFF` on
release, and `app_bed_is_idle()` — which is what lets `bed_control` release
the source lock — is `active_switch == OFF && !in_settings_mode`. Without an
explicit reset, one app LIGHTS command would leave APP holding the lock
forever and the wall switch permanently force-stopped.

### STOP_ALL does not share the command slot

The deferred-command slot is last-write-wins: a second write arriving before
the main loop drains it overwrites the first. That is fine for everything
self-correcting — the app repeats MOVE_START every 100 ms, and a lost
MOVE_STOP is caught by the watchdog inside 300 ms — but **STOP_ALL is sent
once and nothing corrects for losing it.**

It gets a dedicated `volatile` flag, set in the Bluetooth handler and cleared
only by the main loop *after* the stop has been performed. No other path may
clear it, so no later command can displace a pending stop. It is also
processed first, before the shared slot is even examined, and accepted from
any role (#12's stop-from-anyone rule).

### Dead-man watchdog (#5)

300 ms sleeptimer, milliseconds not loop passes (#2), armed and refreshed
only by MOVE_START from the control connection. At the app's 100 ms repeat
cadence that tolerates two lost writes. AUTO_LEVEL and LIGHTS are latched and
never arm it. MOVE_STOP and STOP_ALL cancel it; so does a collision or the bed
going idle on its own after a limit stop. Expiry is `app_stop_all()` plus a
`WATCHDOG_STOP` result if the link is still up.

**Control-connection disconnect while APP is the active source is a full
`app_stop_all()`, not `bed_move_stop()`.** The spec calls this out and the
distinction is real: `bed_move_stop()` does not end a latched auto-level run,
so a phone dropping mid-auto-level would leave the bed levelling with nobody
connected to stop it.

That stop is **flagged in the disconnect callback and performed in the main
loop**, not inline. `app_stop_all()` drives LEDs through the I2C expander, and
blocking I2C inside a stack callback is #27/#30's confirmed radio-starvation
mechanism. Same reason the watchdog's sleeptimer callback only sets a flag.

### Arbitration and results

Viewer commands are rejected `REJECTED_VIEWER` before reaching `bed_control`.
Two exceptions, both from #4: STOP_ALL (stop-from-anyone) and REFRESH_TILT
(commands nothing, and its telemetry is notified to both connections anyway).
Settings mode rejects the motion opcodes with `REJECTED_SETTINGS_MODE`;
REFRESH_TILT is still accepted.

Wall-switch-holds-the-lock is not decided here — `bed_control` already
reports it. The start-class verbs return `false` when arbitration turned the
command into a force-stop, which maps directly to
`FORCE_STOPPED_OTHER_SOURCE`. The stop-class verbs always return `true` by
design, so they always report `ACCEPTED`.

### Bed Status needed no change

`active_source` and `command`/`command_detail` already track app-initiated
movement: `bed_status.c` reads `bed_active_source()` and
`app_active_command()`, `bed_move_start()` sets the source, and
`app_move_start()` sets `active_switch` through the same
`motor_button_pressed()` the switch path uses. The reason those fields only
ever showed the switch path before is that nothing had ever called the verbs
with `BED_SOURCE_APP`. Verified by reading the path, not by adding code.

### Status

Compiles clean (see the handoff). **Everything else is ASSERTED** — no bench
run yet. The watchdog, the disconnect stop, and wall-switch arbitration
against a live app move are the three behaviours least like anything
previously exercised on hardware.

## 35. The 10 Hz app-move stutter — a physical-panel check running on app moves

Root cause of the Stage C bench bug, and the two related defects fixed with
it. Read this before touching `active_switch` or the double-press check.

### Symptom

Hold a single-side direction in the app, involve the physical switch panel,
and the bed stutters at ~10 Hz — motors on and off at the app's 100 ms
MOVE_START repeat interval. It survived closing the app and only cleared on a
board reset. It attached to whichever app command type was in use during the
conflict: single-side, or ALL, but not both.

### Root cause

`switch_input_tick()`'s **double-press error check** was running on
app-sourced moves.

That check exists to catch two physical buttons pressed at once. It compares
`app_ui_status` — the switch-board status word latched at the last *physical*
switch interrupt — against a fresh read taken 50 ms later, and declares a
double press if they **match**. On a match it kills the motors, sets
`active_switch = OFF`, and clears the LEDs.

Its only guard was `active_switch != OFF`. An app move sets `active_switch`
through the same `motor_button_pressed()` the switch path uses (that sharing
is deliberate and is what makes Bed Status report app activity for free), so
the check ran on app moves too.

The loop:

1. App MOVE_START repeat → `bed_move_start()` → `active_switch` set,
   `initial_switch_press = true` → `app_apply_move_outputs()` → motors on.
2. Next tick, the block starts the 50 ms double-press timer.
3. Timer fires. No physical button is pressed, so the fresh status read can
   equal the stale latched one → **bogus "Double Press Error"** → motors off,
   `active_switch = OFF`.
4. `service_move_watchdog()`'s idle branch sees `app_bed_is_idle()` → clears
   `app_move_active`.
5. The next repeat, within 100 ms, starts the move from scratch. Go to 1.

The app's repeat interval is the rate limiter, which is why the stutter is at
exactly 10 Hz.

### Why the asymmetry — the LED bits are the fingerprint

The status word is not just switches. Port 1 bits 4-7 are the **LED enable
lines** (`EN_REDn`, `EN_GREENn`, `EN_BLUEn`, `EN_AMBERn`).

That is what normally makes a legitimate *physical* hold safe: the status is
latched at the interrupt, then `process_outputs()` lights that move's LED, so
the read 50 ms later differs and no error fires.

It is also what makes the bug selective. The latched `app_ui_status` freezes
whichever LED was lit at the last physical interrupt, and a subsequent app
move only matches it if it lights the **same** LED:

| App command | LED | Stutters if that LED was lit at the last physical interrupt |
|---|---|---|
| single side | GREEN | yes |
| ALL up/down | BLUE | yes |
| auto-level | RED | n/a — excluded by `!auto_level_active` on the same line |

Hence "the stutter attaches to whichever app command type was in use during
the conflict". Auto-level was never affected because the enclosing condition
already excluded it — an independent confirmation of the mechanism.

### Why it survived closing the app

`app_ui_status` is only rewritten by a **physical** switch interrupt. Closing
the app, reopening it, or swapping transport does not touch it, so the primed
value persisted and the stutter resumed as soon as the repeats did. A board
reset reinitialised it. (The earlier guess that the missing watchdog explained
the persistence was wrong — the watchdog re-armed on every repeat. The latched
status word is the state that survived.)

### Fix, and why NOT to separate `active_switch`

The check is gated on `bed_active_source() == BED_SOURCE_SWITCHES` — one
condition, alongside the `!auto_level_active` and `!in_settings_mode`
exclusions already on that line, and semantically exact: a physical-panel
consistency check has no business running on a move the panel did not start.

`run_motor_supervision()` is deliberately left **outside** that gate.
Collision and limit supervision must cover app moves; gating the whole block
would have silently removed them.

Splitting `active_switch` into per-source state was considered and rejected.
It is not the root cause — the root cause is a panel-specific check with too
weak a guard — and the sharing pays for itself: it is why Bed Status reports
app activity with no extra code. Splitting it would touch `bed_status.c`,
`bed_actions.c`, `app.c`'s auto-level gate, `settings_mode.c`,
`switch_input.c`, and the limit/collision supervision, all of which read it,
for no benefit the one-line gate does not already deliver.

### Two related defects fixed at the same time

**The watchdog was being silently disarmed.** The idle branch called
`move_watchdog_cancel()` whenever the bed looked idle, including while the app
still held a move and the bed's ownership was in dispute — exactly when the
dead-man protection matters. It now cancels only on a collision, where the
collision handler owns the stop and an expiry would cut across its LED
sequence. On plain idle it clears `app_move_active` and **leaves the watchdog
armed**: the watchdog guards the *link*, not the bed. A live app re-arms it on
the next repeat; a dead link still expires into a full stop.

**Two-source conflict now stops instead of handing over.** Previously an app
command arriving while the wall switch held the bed force-stopped it, and then
the app's next 100 ms repeat found the lock released and started the app's
movement — stop-then-takeover, which is neither what #12 says nor what the app
was told happened. A force-stop now arms a `TWO_SOURCE_LOCKOUT_MS` (500 ms,
bench-tunable, #2) lockout during which app MOVE_START and AUTO_LEVEL_START
are refused with `FORCE_STOPPED_OTHER_SOURCE`. The same lockout arms when the
bed goes idle underneath a move the app believed it held, which covers the
other direction: a wall switch press during an app move stops the bed and does
not hand control over.

Stops are never locked out. STOP_ALL, MOVE_STOP, AUTO_LEVEL_STOP and
REFRESH_TILT are unaffected.

### Status

Compiles clean. **ASSERTED** until the bench run — and the root cause is
falsifiable from the existing UART log, which should show a bogus
`Double Press Error` repeating at ~10 Hz during the stutter, from the printf
already present in `switch_input.c`.

## 36. Auto-level becomes hold-to-run; two-source conflict becomes a latched inhibit

Three bench-driven behaviour changes, from the Stage C run that also produced
#35. #35's stutter fix is bench-confirmed and untouched here.

### Auto-level is hold-to-run

`AUTO_LEVEL_START` was latched: one app press ran levelling to completion and
the green LED stayed lit until `AUTO_LEVEL_STOP` arrived. It now matches the
**physical** auto-level button — held to run, released to stop.

- The app repeats `AUTO_LEVEL_START` every 100 ms while held, and it arms and
  refreshes the same `MOVE_WATCHDOG_MS` (300 ms) dead-man watchdog a move
  does.
- A repeat must **not** re-enter `bed_auto_level_start()`. That resets
  `first_check`, `leveling_success[]` and the current axis, so at 10 Hz
  levelling would never progress. Guarded by `app_auto_level_active`, exactly
  as `app_move_active` guards MOVE_START.
- `AUTO_LEVEL_STOP` on release stops immediately and clears the auto-level
  LEDs.
- Watchdog expiry during auto-level is the same clean full stop plus
  `WATCHDOG_STOP` as for a move.

**Why the watchdog matters here.** A link *drop* during auto-level already
stopped the bed correctly via the disconnect handler — bench-confirmed. A link
that **stalls without disconnecting** was not covered, and hold-to-run is what
makes that gap reachable: under the old latched behaviour the bed would finish
levelling and stop by itself, whereas a held command that stops being repeated
must be caught by something.

Holding through completion behaves exactly as the physical button does:
`app.c` clears `auto_level_active` and lights the green LED on completion but
leaves `active_switch` at `SWITCH_AUTO_LEVEL_VALUE`, so the bed is not idle,
repeats keep refreshing the watchdog, and release cleans up. No restart loop.

### Two-source conflict is a latched inhibit

`TWO_SOURCE_LOCKOUT_MS` (500 ms, from #35) is **removed entirely**. It was
strictly weaker than what follows, and two mechanisms doing one job is worse
than one.

**Rule.** Either source touching the bed while the other is moving it stops
movement immediately, and nothing moves again until **both** sources have
released — at which point a fresh press on either one starts normally.

The old timer expired while a button was still held, so whichever source was
still holding simply won. That is stop-then-hand-over, exactly what
DECISIONS #12 exists to prevent.

**Where it lives.** `bed_control.c`, because that is the one place both
sources already meet, so a single check covers them symmetrically. The
motion-starting verbs refuse while latched; `bed_conflict_inhibited()` exposes
it for the BLE layer's early refusal.

**How release is detected.**

| Source | Signal |
|---|---|
| Physical | the panel's release interrupt, already generated — reaches `bed_control` through `bed_move_stop()`/`bed_auto_level_stop()` |
| App | `MOVE_STOP`/`AUTO_LEVEL_STOP` (immediate), **or** no motion repeat for `MOVE_WATCHDOG_MS` (fallback if a stop write is lost) |

The dead-man period doing a second job is deliberate: it is already the
definition of "the app has gone quiet", and reusing it avoids a second
constant meaning almost the same thing.

**Cessation is the only thing that clears the latch.** A refused command must
never clear it — and, just as importantly, a refused repeat must *refresh* the
app's half. Without that refresh the silence timeout would expire mid-hold,
the latch would clear, and the next repeat would restart the bed: precisely
the behaviour being removed. So an app that keeps repeating because the user
is still holding keeps being refused indefinitely, each refusal returning
`FORCE_STOPPED_OTHER_SOURCE` so the app can react.

**Scope: motion-starting opcodes only.** `MOVE_START` and `AUTO_LEVEL_START`
are refused. `STOP_ALL`, `MOVE_STOP` and `AUTO_LEVEL_STOP` are never
inhibited. `LIGHTS_SET` and `REFRESH_TILT` are not motion and are not
inhibited — lights also never *latch* the inhibit, though a lights command
from the non-holding source still force-stops per #12's existing behaviour.

### Which LEDs go out

The conflict stop kills the **movement** indicators only — green, blue, red.
It leaves the under-bed lighting and its amber indicator alone. The lights are
a latched setting the user chose separately, and a movement conflict plunging
the bed into darkness is a surprise rather than a feature. `STOP_ALL` remains
the way to turn everything off.

Implemented as `app_stop_movement()` in `bed_actions.c`: `app_stop_all()`
followed by restoring the amber indicator if the lights are on. The under-bed
lighting output was never touched by `app_stop_all()` — only its indicator —
so restoring the indicator is all that is needed to keep the two agreeing.
Every force-stop path in `bed_control.c` now uses it; `STOP_ALL` still calls
`app_stop_all()` directly and keeps its full behaviour.

### Verified

The host unit test (`test/bed_control_test.c`) was extended with eleven checks
covering the latch: that a conflict latches it, that **both** sources are
refused while latched (not just the app), that auto-level start is inhibited
too, that one source releasing is not enough, that it clears only when both
have released, that a fresh press then works, and that stops and lights are
never inhibited. All 30 checks pass on the host.

That is real executed evidence for the arbitration logic. Everything above it
— the LED behaviour, the auto-level hold-to-run feel, the watchdog on a
stalled link — remains **ASSERTED** until the bench run.

## 37. Splitting `active_switch`: the panel needed its own state

Reverses the decision recorded in #35, on the test #35 itself set: *"What would
change my mind: a second panel-specific reader. One is a missing predicate;
two is a missing abstraction."* The second reader turned up. #35's decision was
correct on the evidence then available; this is new evidence.

### The two readers

`active_switch` is the **command** variable — what the bed is doing, whoever
asked. Both sources write it, because both go through `bed_actions.c`. That
sharing is why `bed_status.c` reports app activity with no code, and it stays.

Two readers needed a different question — *"is a physical button currently
held?"* — and asked `active_switch` instead:

1. **The double-press check** (#35). Fixed there with a source predicate.
2. **`process_inputs()`'s press guard**, `active_switch == OFF`. While the app
   was moving the bed this was false, so **every physical press was silently
   discarded** — it never reached `bed_control`, no conflict was detected, the
   #36 latch never armed, and nothing stopped. That is bench failures (d)
   and (e).

Its "same button" exception falls straight out: with the app holding ALL UP,
`active_switch == SWITCH_UP_ALL_VALUE`, so the press was skipped but the
*release* branch matched and called `bed_move_stop()`, which force-stopped —
a brief blip on release, only for the matching button, after which the app's
100 ms repeat restarted the bed.

### The runaway

`process_outputs()` runs from the end of `process_inputs()`. Its press arm
energised on `switch_pressed && (active_switch == value)` — and `active_switch`
could hold that value because the **app** put it there. So a physical press
that `process_inputs` had just discarded still reached
`set_dual_motors_on()`: motors energised by the panel path with `bed_control`
never informed.

Both arms depended on a variable the app could change between press and
release, so the pairing was breakable. Two things made that dangerous rather
than merely redundant:

- **Asymmetric release conditions.** `process_inputs`' release accepted
  `active_switch == value || active_switch == OFF`; `process_outputs`' release
  arm accepted only `active_switch == value`. The physical path's *only*
  hardware de-energise is that arm — `bed_move_stop()` →
  `app_move_stop()` → `motor_button_released()` clears `motor_state` flags and
  leaves the outputs on.
- **No supervision when `active_switch == OFF`.** `switch_input_tick()`'s
  block, which contains `run_motor_supervision()`, is gated on
  `active_switch != OFF`. Motors energised in that state have no collision
  detection, no limit inference, and no path that will turn them off.

**Honest status:** the mechanism above is verified from the code. I could not
construct a single sequence I am confident reproduces the one observed
runaway — every candidate ended with some other path calling
`set_all_motors_off()`. One non-reproducing observation is not evidence of a
fix. What the split does is remove the *class*: see below.

### The split

`switch_input.c` gains two file-private variables, and `active_switch` keeps
its meaning unchanged:

- **`switch_held`** — which physical button is physically down. Set on press,
  cleared on release, **regardless of whether the command was accepted**, so a
  release always reaches `bed_control` and always releases this source's half
  of the #36 latch.
- **`switch_driving`** — the same value, but only when `bed_control` *accepted*
  the press. `process_outputs()` energises and de-energises on this one.

What that makes impossible:

- The app cannot write either variable, so **`process_outputs` can never
  energise a motor from an app-set value.**
- A refused press leaves `switch_driving == OFF`, so **a press that
  `bed_control` rejected cannot drive a motor.**
- `switch_driving` is unaffected by anything the app does, so **an accepted
  press is always paired with its own release** — the release arm cannot be
  orphaned by the app changing `active_switch`.
- `process_inputs` now gates on `switch_held`, so **a physical press during an
  app move reaches `bed_control` and latches #36**, in both directions.

`bed_status.c` needs no change: `active_switch` is still the command variable
and still tracks app-sourced activity. `run_motor_supervision()` stays outside
the double-press gate, unchanged. The #35 fix is untouched.

### The false limit flags — a separate defect

`limit_switch_statuses()` infers `at_limit_switch[motor].upper = true` from
"commanded on but not moving", and only clears it again while that motor is on
**and moving**. A motor left with a stale `motor_on` flag on a physically
stopped bed therefore latches a limit that nothing can clear.

That is not cosmetic. `set_dual_motors_on()` refuses to energise a pair whose
limit flag is set, so a stale flag makes the bed stop responding — the "physical
auto-level flashes red but never runs" half of the lockup, and why tapping a
manual button cleared it (that move re-entered the clearing branch).

Fixed by clearing the inferred state on every full stop
(`reset_limit_switch_flags()`, called from `app_stop_all()` and
`app_stop_movement()`). A stop means nothing is commanded, so every inference
is stale by definition; real limits are re-inferred within about a second of
the next move, which is already how the flags behave after any normal stop.

### The indicator blip

`app_stop_movement()` called `app_stop_all()`, whose `set_all_LED_off()`
cleared amber, and then turned amber back on — the blip itself. It now clears
the three movement LEDs individually and never touches amber.

**Residual, not fixed:** when the conflict interrupts *auto-level*,
`app_stop_movement()` still routes through `app_auto_level_stop()`, which calls
`set_all_LED_off()`. The blip can therefore still occur on that one path.
Recorded rather than fixed, because fixing it means changing a function the
physical auto-level path also uses.

## 38. The double-press misfire, the state it desynced, and the motion fail-safe

Post-split bench findings. Both reported symptoms occurred **only when
"Double Press Error" printed**, and everything below is downstream of that
check firing when it should not.

### The misfire — and it was introduced by #35

The check compares `app_ui_status`, latched at the last physical interrupt,
against a fresh read **50 ms later**, and calls a match a double press. It is
only sound at that 50 ms point: what makes a legitimate single press differ
from its own latched status is the **LED bits changing in between** (#35). Run
it at any other moment and it compares a steady state against an edge, and
matches whenever the LED happens to line up.

`double_press_error_timer_timeout` is set by a **periodic** 50 ms timer. #35
wrote the consume as:

```c
if (double_press_error_timer_timeout && switch_sourced_move){
    double_press_error_timer_timeout = false;
```

The clear is inside a condition that also requires the source to be SWITCHES.
So whenever it was not, **the flag was never consumed and went stale**, to be
picked up at an arbitrary later moment — exactly the decoupling the check
cannot survive. `app_reset_double_press_timer()` compounded it by stopping the
timer without discarding a pending expiry.

That gate was mine, added to fix the stutter, and putting it in the clearing
condition rather than around the check body is the defect. **The gate itself
stays** — it is bench-confirmed and is not what misfired.

Fixed: consume the flag unconditionally, gate only the check body; and clear
the flag when the timer is stopped.

### The handler cleared one third of the split state

```c
set_dual_motors_off(HEAD);
set_dual_motors_off(FOOT);
active_switch = OFF;
```

Correct before #37, when `active_switch` **was** all three. After the split it
left `switch_held` and `switch_driving` stale. Every other site in
`process_outputs()` clears its state on adjacent lines; this handler was the
exception.

**Observation 1 — confirmed.** `switch_input_tick()`'s block is gated on
`active_switch != OFF` and `run_motor_supervision()` is inside it. The handler
sets `active_switch = OFF`, so supervision stops, the current filter is never
fed, and the app faithfully reports 0 mA on all four while the motors run.
**This is exactly the state #37 §b.1 fact 2 described** — motors energised with
no collision detection, no limit inference and no path that will ever stop
them — and the bench has now been looking directly at it.

**Write this down permanently: the app's current display is a live diagnostic
for that state.** Motors visibly running with 0 mA on all four means
supervision is not running. It is the cheapest possible indicator of the most
dangerous state this system has.

**Observation 2 — it is BOTH mechanisms, and (b) is the serious one.**

- Stale `switch_held` still named LEFT DOWN, so every other button failed its
  `switch_held == OFF` press test and did nothing. That is why auto-level and
  lights went dead too.
- Stale `switch_driving` still named LEFT DOWN, and `process_outputs()`
  energises purely on `switch_driving == value`. So pressing LEFT DOWN again
  **moved the bed**.

The observation proves (b) by itself: with `switch_held` stale, `bed_move_start()`
was never called for **any** button, LEFT DOWN included. Yet LEFT DOWN moved
the bed. The only remaining energise path is `process_outputs()`' press arm on
`switch_driving`.

**That is motion after `bed_control` was never consulted — arbitration
bypassed entirely.** It is a worse defect than the symptom that revealed it,
and it gets its own fix rather than being folded into the handler cleanup:
`switch_driving` is now cleared at the start of **every** press event and
re-set only when the verb returns true, so a stale value cannot survive into
the press arm.

The app kept working throughout because the app path uses its own state and
consults neither variable.

### The motion fail-safe

Unconditional, every main-loop pass, trusting **no** bookkeeping:

> If a direct hardware read says no switch is physically down, the app is not
> commanding motion, auto-level is not running and settings mode is inactive —
> and a motor is energised — de-energise it.

- Not conditional on `active_switch`, `switch_held`, `switch_driving`,
  `app_ui_interrupt` or any table match. Those are precisely what desync; the
  fail-safe exists because they can.
- The direct read is **rate-limited to 50 ms** (`sample_motor_faults_tick()`
  precedent). A per-pass blocking I2C read on the UI expander is #27/#30's
  radio-starvation mechanism, so it is not an option.
- **The press-to-outputs window** is handled by requiring three consecutive
  quiet samples (~150 ms). In that window the switch is physically **down**, so
  the first test already fails — the confirm count only guards a torn read at
  the instant of release. It cannot introduce a stutter, because it never
  fires while any switch is down.
- Auto-level and settings mode legitimately drive motors with no button held
  and are excluded explicitly.
- Prints once per trip, not per pass.
- LED bits are masked out of the switch read (`SWITCH_INPUT_MASK`), so it
  cannot inherit the LED dependency that made the double-press check fragile.

### The de-energise asymmetry — closed

`app_move_stop()` now calls `set_all_motors_off()`. The release helpers only
ever cleared `motor_state[]` flags, so the physical path's **only** hardware
de-energise was `process_outputs()`' release arm; any stop that did not reach
that arm left the outputs live while every flag said stopped. A stop verb must
stop the motors. Idempotent — `process_outputs()` may call it again in the
same tick.

### Should supervision be gated on `active_switch` at all?

**No, and this is left open.** A gate that switches off collision detection is
the wrong shape for a safety check, and this one can be cleared by an error
handler — which is precisely what happened. The gate is not changed here
because it also encloses the double-press check whose #35 fix is
bench-confirmed, and untangling the two is its own change. The fail-safe above
is a backstop for the consequence, not a fix for the shape. **Recorded as the
next thing to fix in this area.**

## 39. Proving the motion fail-safe: a bench-only runaway provocation

#38 added the motion fail-safe and recorded that it was **untested in the
direction that matters** - only ever shown to stay quiet, never shown to catch
a runaway. That is the one property it exists for, and it had zero evidence.

### The provocation

`BED_BENCH_PROVOKE_RUNAWAY`, default 0, same convention as
`BLE_BENCH_WIPE_BONDS` (#28's bench flag): hardcoded, source-visible, must be
hand-flipped and hand-flipped back.

When 1, releasing a trigger button arms a timer that energises a motor pair
~300 ms later, with no button held and no source commanding - a manufactured
runaway. Tap **UP HEAD** for the single-side path (`set_dual_motors_on`), tap
**UP ALL** for all four (`set_all_motors_on`). Two paths because they energise
through different calls and both need proving.

It energises through the **same calls the real paths use**, so the fail-safe
faces a genuinely energised bed rather than a simulation.

The 300 ms delay is deliberate: long enough that the release is fully
processed and every variable is back to idle, so the fail-safe meets a real
"nothing is commanding" state rather than racing the release path.

Timing is **measured, not assumed** - the provocation stamps the energise tick
and the fail-safe prints elapsed milliseconds on the trip, both under the same
flag.

### A placement bug the compiler caught

The flag was first defined next to the fail-safe, near the end of the file,
where it reads most naturally. The trigger sites are in `process_inputs()`,
**earlier in the file**, so `#if BED_BENCH_PROVOKE_RUNAWAY` there evaluated an
undefined macro to 0 and compiled them out. **The provocation would have done
nothing at all**, and a bench session would have been spent concluding the
fail-safe works when it had never been provoked.

`-Wunused-function` on `provoke_arm` is what exposed it. The flag now sits at
the top of the file with a comment saying why it must, and both flag states
build warning-free.

Worth generalising: a bench flag whose only job is to change behaviour is
exactly the kind of thing that can silently not work, and the failure looks
identical to success.

### What the fail-safe leaves behind - reviewed, and two gaps closed

Asked directly: if it fires, is the system clean, or has a runaway just become
the stuck state?

Already correct:

- Hardware outputs, `motor_state[].motor_on`, movement LEDs, `switch_held`,
  `switch_driving`, `active_switch` - all cleared.
- `app_move_active` / `app_auto_level_active` need no clearing: the fail-safe
  only runs when `ble_bed_service_app_motion_active()` is already false.
- `bed_control`'s `active_source` self-releases on the next
  `bed_control_tick()`, since `app_bed_is_idle()` is now true.

Two gaps, both closed:

- **The switches' half of the #36 latch.** The fail-safe clears `switch_held`,
  so a later release event finds no match and never reaches `bed_move_stop()`
  - leaving `source_commanding[SWITCHES]` set and the inhibit latched
  permanently. It now calls `bed_note_source_released(BED_SOURCE_SWITCHES)`,
  which is justified rather than defensive: it has just established by direct
  hardware read that no switch is down.
- **Inferred limit flags.** Every other stop path clears these (#37); the
  fail-safe did not. A runaway is precisely the case where motors ran
  unsupervised, so any inference made during one is worthless. Now calls
  `reset_limit_switch_flags()`.

### Status

Both flag states build clean. **The fail-safe is still unproven** - the
provocation exists but has not been run. That is the whole point of the next
bench session.

## 40. The fail-safe reads motor hardware; double press = two switches down; out-of-band reads must not swallow events

Date: 2026-10-17. Prompted by the 732dfed provocation bench log.

### What the bench showed

- UP HEAD provocation: fail-safe caught it 11/11, 213-216 ms.
- UP ALL provocation: caught 0/5. The motors ran until DOWN ALL was pressed.
- One `FAILSAFE:` line with no `PROVOKE:` before it, after a real UP HEAD hold.
- 18 `Double Press Error` lines, none from two buttons pressed together.

### 1. The fail-safe now reads the hardware for the motors

`any_motor_energised_now()` read `motor_state[].motor_on`, which is
bookkeeping. `set_all_motors_on()` writes the expander outputs and sets no
flags, so "hardware on, flags off" was invisible to the check - the #38 bug
class it exists to catch. It now reads the EN_MOTOR1-4 bits (P0_0-P0_3) of the
on-board expander output register via `get_motor_enables()`, only after the
switch check passes, once per `FAILSAFE_SAMPLE_MS`.

- Why the enable bit is what gates motion: PWM is started once at boot and
  never stopped (`app.c`), and the bed is stationary at idle, so a running
  PWM alone does not move a motor. (Inferred from code and behaviour; no
  schematic is in the repo.)
- A failed read counts as "energised" (fail toward OFF). The existing
  3-sample confirm and the "nothing is commanding" precondition mean one
  transient cannot fight normal motion.
- Confirm window and sample rate unchanged. The provocation is unchanged;
  UP ALL calling `set_all_motors_on()` alone is now the hardware-on/flags-off
  test.

### 2. Double press is now "two physical switches down"

The old check compared the status latched at the last interrupt with a fresh
read and called a match a double press. That is not a statement about
switches. Two ways it matches on a single press, both from the code:

- Any extra interrupt after the LED changed (contact bounce on the press)
  re-latches the status with the LED already on.
- Anything that returns the LED to its latched state while the button is
  still held. The log's last two lines are this: `Exit auto-level`, then two
  errors, with the auto-level button held.

No debounce exists in firmware (grep) and none is documented in hardware. The
check now counts down switches in a fresh read (`SWITCH_INPUT_MASK`) and fires
at 2 or more. One button cannot produce that, however it bounces. It still
catches a second button pressed while the first is held, at the next 50 ms
sample. It also prints the switch word, so a false positive would be visible.

Not fixed: a bounce on a RELEASE or press can still produce a brief
spurious stop and restart, because the main input path has no debounce.

### 3. Out-of-band status reads must not swallow switch events

`get_ui_status()` clears the expander's interrupt flag (comment at
`exp_ui.c:263`). The double-press check and the fail-safe both call it on their
own timers. If one lands between a switch edge and the normal read, the normal
read finds no interrupt source and the event is dropped. A dropped release
leaves `switch_held`/`switch_driving` set and the motors running with nothing
held. That matches the unprovoked `FAILSAFE:` and the original runaway.

Candidates (a) stale flags with hardware off and (b) the limit-switch
re-energise were both checked. (b) cannot fire on a HEAD hold: it needs
`motors_temp_off`, which only an ALL move sets. (a) cannot survive a normal
release: `app_move_stop()` clears the flags and de-energises together.

Every out-of-band read now goes through `get_ui_status_out_of_band()`, which
records which switch bits changed since the last processed event. The normal
read uses them if the chip reports no source, and prints
`UI: recovered swallowed switch change 0x....` when it does. That print is
the evidence: if the hypothesis is wrong it will never appear.

### Status

Builds clean with the bench flag at 0 and at 1. **None of this has been run on
the bench.** The swallowed-event mechanism is inferred from code and the
`exp_ui.c` comment, not from the datasheet or a capture.

## 41. One switch reader

Date: 2026-10-17. Implements `docs/SWITCH_SAMPLER_DESIGN.md` (written by the
reviewer after the `prompt_q_bench.txt` log).

### Why

Three code paths read the UI expanders' input ports: the event dispatch, the
double-press check and the fail-safe. Each read clears the expander's interrupt
flag, so they raced for every edge. In the bench log a two-button release
arrived as one event carrying two bits, `process_inputs()` only matches
single-bit masks, the release was dropped, and the fail-safe stopped the motors
about 200 ms later (five times). There was no debounce anywhere.

### Decision

- **Only the switch sampler reads the UI input ports.** `ui_read_inputs()` in
  `exp_ui.c` is the only function that issues `EXP_CMD_READ_INPUTS` to a UI
  board and the sampler in `switch_input.c` is its only caller. The dispatch,
  the double-press check and the fail-safe use the sampler's debounced state.
- **Sampling session.** Starts on the UI_INT edge flag or, at idle, on a level
  check of the UI_INT pin (so a missed edge still starts one). During a session
  both boards are read every 10 ms and each bit is debounced (2 identical reads;
  3 until 2026-10-18 - see "Bench result" below).
  The session ends only when everything is released, stable and no interrupt
  arrived since the last read.
- **One event per bit, releases first.** Accepted edges are handed to
  `process_inputs()` one bit at a time, so it always sees a single-bit mask.
  All releases are delivered before any press.
- **No presses while 2 or more are down.** If two or more switches are down when
  presses are due, none is delivered. A second press during a move is caught by
  the double-press check, which is now "2 or more bits in the debounced
  down-mask", evaluated every pass instead of on a 50 ms timer.
- **Re-driver reference count.** `ui_redriver_acquire()/release()` own the
  off-board re-drivers (EN_I2C_DRIVER, on-board expander P1_5, which powers only
  the PCA9617A re-drivers and their pull-ups; the Switch board expander, its LEDs
  and the interrupt line are on always-on 3V3). Every UI transaction takes a
  hold for its own duration, so behaviour is unchanged when nothing holds it; a
  session holds it throughout, so there is no per-transaction toggling or 2 ms
  delay. EN only changes between transfers: all callers are main-loop context
  (every sleeptimer callback in the firmware only sets a flag, and
  `sl_button_on_change` is now flag-only).
- **Sleep veto.** `app_is_ok_to_sleep()` returns false while a session is active
  and while the motor-enable shadow (`motor_outputs_maybe_on()` in `exp_board.c`,
  updated by every port-0 writer, set to "on" at boot and after any failed write)
  says the motors might be energised. `set_all_motors_on()` energises without
  setting `motor_state[].motor_on` (#40), so a runaway could previously let the
  CPU sleep between fail-safe samples - the suspected cause of the ALL MOTORS
  fail-safe taking 271-307 ms against 214-223 ms for the HEAD pair.
- **Fail toward OFF.** The fail-safe treats stale (no good read for 100 ms during
  a session) or unknown state as "no switch down". After 5 consecutive samples in
  which every present board failed, all switches are treated as released and the
  boards are re-probed at the next session start.
- A button held at power-up is never acted on (no press event is generated for
  the baseline read), and its later release does nothing.

### Known limit

The expander can drop an edge that coincides with the ACK of its own read.
Mid-session the next 10 ms sample catches it. At session end a press in that
window is lost: the press does nothing, which is benign, and it is never a lost
release, because sessions only end with everything released. The UI_INT level
check at idle covers a line that is stuck asserted (at the cost of the sampler
running continuously and blocking sleep while it is).

### Behaviour changes to expect

- Release is now seen after the debounce, about 10-20 ms after the contact opens
  (20-30 ms with the original 3-sample debounce, until 2026-10-18),
  so a move stops that much later than before (+ up to one sample period).
- Double-press detection is continuous rather than 50 ms after the initial press.
- `SWITCH_SAMPLER_DIAG` is 1: `SW:` lines print for every accepted edge, with a
  per-bit bounce tally. Set it to 0 before shipping.

### Status

Builds clean (no warnings, no errors) from the command line with
`BED_BENCH_PROVOKE_RUNAWAY` at 0. **None of this has been run on the bench.**
DECISIONS.md #40's "recovered swallowed switch change" mechanism is removed;
whether the swallow hypothesis was right is unproven either way.

### Bench result (2026-10-18)

The user ran `d1f13d0` on the bed (debounce 3 samples, `SWITCH_SAMPLER_DIAG`
1, the provocation flag hand-flipped to 1 for the runaway runs and back).
Log: `_03-Software/PuTTY Logs/sampler_bench.log` (1155 lines). Every figure
below was re-counted from that log, not copied from a summary. **VERIFIED
(user's bench run).**

- **Provoked runaway, HEAD pair:** caught after 209, 248, 209, 209, 209 ms.
- **Provoked runaway, ALL MOTORS:** caught after 209, 210, 210, 217, 249 ms.
  Before the sleep veto this was 271-307 ms against 214-223 ms for the HEAD
  pair; the gap is gone. That is consistent with the "CPU slept between
  fail-safe samples" explanation above; it does not prove it was the only
  cause.
- Every `FAILSAFE:` line (10) comes directly after a `PROVOKE:` line. No
  `Unrecognized switch mask`, no `read failed`, no other `failed` line.
- **Two-button presses:** 67 episodes with two switches down, across 30
  different pairs. All 67 printed `SW: 2 switches down - presses ignored`; in
  57 of them one button's press had already been delivered (a brief start)
  and `Double Press Error` followed and stopped it; the other 10 moved
  nothing. Every release came through: each `down` line has a matching `up`,
  and no episode ended with a switch left down.
- **Bounce tally:** 700 accepted edges (350 down, 350 up); 696 with 0 bounces,
  4 with 1 bounce (one on 0x1000, three on 0x0004, all on press).
- **Power-up with UP HEAD held:** the board was reset with 0x8000 down; after
  the boot lines the only output was `SW: 0x8000 up (bounces 0)`, with no
  motor-current lines (nothing moved); the next press of UP HEAD moved the
  bed normally.

The bounce data is the basis for reducing the debounce to 2 samples
(2026-10-18, user decision): no bounce lasted more than one sample.

## 42. Pairing window: new bonds only while settings mode is active (implements #19)

Date: 2026-10-18. User decision. This implements `docs/BLE_GATT_DESIGN.md`
§8's "proposed DECISIONS #19". Number 19 was already used - by #19 above,
which recorded this same policy but was never built - so the implementation
is recorded here, as #42.

### Decision (user decisions - not to be re-opened)

1. **Only new bonds are gated.** An already-bonded phone reconnects freely at
   any time. A reconnecting bonded phone resumes encryption from its stored
   bond and does not raise `sl_bt_evt_sm_confirm_bonding`, so the gate never
   sees it. Evidence: the SDK documents the event as a "new bonding request"
   (`sl_bt_api.h`, `sl_bt_evt_sm_confirm_bonding`), the existing code already
   relies on `sm_bonded` not firing for a resumed bond (`assign_role_if_needed`
   comment), and in the 2026-10-18 bench log (`sampler_bench.log` lines
   956-961 and 979-984, built with `BLE_DEBUG_SECURITY_LOG` 1) a bonded phone
   (`connection_opened ... bonding=0x01`) reconnected twice and reached
   `security_mode=1` with no `confirm_bonding` line.
2. **Re-pairing a known phone is gated too.** A replacement request
   (`bonding_handle != SL_BT_INVALID_BONDING_HANDLE`) goes through the same
   window. **This changes #28's auto-replace from automatic to conditional:**
   a stale-bond phone is still recoverable without a reflash, but only by
   putting the bed into settings mode and pairing again. #28's security
   analysis anticipated exactly this ("once #19 gates all bonding - new and
   replace alike - behind a physically-opened pairing window, auto-replace is
   safe by construction").
3. **The window is "while settings mode is active" - no timer.** It opens when
   settings mode is entered (BED LIGHTS held 5 s) and closes on the settings
   mode exit (a new BED LIGHTS press, `settings_mode_exit()`) and on reset
   (`in_settings_mode` starts false).
4. **Outside the window, reject** with `sl_bt_sm_bonding_confirm(conn, 0)`,
   so the phone gets a clean pairing failure rather than a hang.

### Implementation

One decision point, as #28 planned: the `sl_bt_evt_sm_confirm_bonding`
handler in `ble_bed_service.c` now passes
`confirm = app_in_settings_mode() ? 1 : 0` to `sl_bt_sm_bonding_confirm()`
instead of always 1. `sl_bt_sm_set_bondable_mode(1)` and the configuration
bits are unchanged. Under `BLE_DEBUG_SECURITY_LOG` the handler prints, for
every request,
`BLE SM: confirm_bonding conn=N (new bond|REPLACING existing bond) -> ACCEPTED (settings mode)`
or `... -> REJECTED (not in settings mode)`.

### Status

Builds clean from the command line. **Not run on the bench.** That a rejected
phone sees a clean failure (rather than retrying or hanging) is the SDK's
documented behaviour for confirm = 0, not observed here.

### Bench result (2026-10-18)

The user ran `944f19e` (Prompt V: debounce 2 samples, settings-mode exit,
pairing window; `BLE_DEBUG_SECURITY_LOG` 1) on the bed with the Flutter app
on a Samsung S10. Log: `_03-Software/PuTTY Logs/prompt_v_bench.log` (193
lines). Every figure below was re-counted from that log. **VERIFIED (user's
bench run).**

- **Debounce 2:** 88 accepted edges, all `(bounces 0)`. Six two-button
  episodes, all printed `SW: 2 switches down - presses ignored`; three of
  them also printed `Double Press Error` (as designed - one press delivered
  first, then stopped). No `FAILSAFE`, no `Unrecognized switch mask`.
- **Settings mode exit:** `Settings mode: exit` printed four times (log
  lines 79, 86, 93, 171), each on a single new BED LIGHTS press. After the
  third exit every button's press and release came through, and a full
  auto-level ran to `PITCH Leveled successfully` / `Exit auto-level`. The
  log shows the other presses' edges but no motor-current lines for them
  (those print only after ~300 ms of running), so "every button worked" is
  the user's observation, not something the log alone shows.
- **Pairing window:**
  - (a) bonded phone reconnecting (lines 142-148): `connection_opened
    bonding=0x01`, `security_mode=1`, no `confirm_bonding` line.
  - (b) phone-side bond deleted, not in settings mode (lines 149-155):
    `confirm_bonding conn=2 (REPLACING existing bond) -> REJECTED (not in
    settings mode)`, `bonding_failed conn=2 reason=0x1205`, then
    `connection_closed conn=2 reason=0x1013` - the phone closed the link
    itself; the firmware did not.
  - (c) in settings mode (lines 162-169): `-> ACCEPTED (settings mode)`,
    then `bonded conn=3`.
  - (d) after the exit (line 171) the phone reconnected (lines 176-181,
    `bonding=0x01`, `security_mode=1`) with no `confirm_bonding` line, and
    the motor-current lines that follow with no `SW:` line before them are
    app-driven movement.
- **Phone side** (reported by the user from the phone's screen, not in the
  PuTTY log): the app showed `Connection failed: ... status 137
  (GATT_AUTH_FAIL), type ... CHARACTERISTIC_READ` for (b). So the refused
  phone gets a clean failure, not a hang - observed with this phone and app
  only.

That the Android app closed the link after the rejection is that app's
behaviour, not the firmware's. Prompt W part B (recorded as #43, below)
makes the firmware close it.
