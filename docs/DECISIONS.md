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
