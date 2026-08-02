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
