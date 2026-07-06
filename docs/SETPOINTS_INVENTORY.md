# SETPOINTS_INVENTORY.md

Every hardcoded setpoint, threshold, timing constant, and magic number in the
application source that a user or installer might ever want to adjust.
Compiled July 2026 against commit `7456774`; line numbers refer to that
commit. Generated SDK/config code is excluded except where noted.

Conventions used below:

- **Key** — proposed NVM3 key name (and number). Existing keys 1–4 are already
  in use (nvm3_functions.c:17–20) and are kept; new keys start at 16 (0x10) to
  leave room.
- **Safety** — ⚠️ means the value participates in collision detection, stall
  protection, or limit-switch inference: expose it to an app **only through a
  firmware-enforced clamp** (see the last section), never raw.
- Angles marked *scaled* are degrees × 100 (`SCALE_FACTOR`), stored as int16.
- Currents are in mA as produced by `adc_currents.c` (1.5 × ADC counts,
  ~6000 mA full scale).

---

## 1. Collision detection (motor_current_functions.c, app.c)

| File:Line | Name | Value | What it does | Key | Default | Min | Max | Safety |
|---|---|---|---|---|---|---|---|---|
| app.c:58 | `RAISE_HIGH_SENSITIVITY` | 350 | Raising collision margin, most sensitive rung: motors stop when the summed current rise above the armed baseline exceeds this (mA) | `KEY_RAISE_MARGIN_HIGH` (16) | 350 | 150 | 500 | ⚠️ |
| app.c:59 | `RAISE_MED_SENSITIVITY` | 550 | Raising margin, medium rung (mA) | `KEY_RAISE_MARGIN_MED` (17) | 550 | 300 | 800 | ⚠️ |
| app.c:60 | `RAISE_LOW_SENSITIVITY` | 750 | Raising margin, least sensitive rung (mA) | `KEY_RAISE_MARGIN_LOW` (18) | 750 | 500 | 1200 | ⚠️ |
| app.c:61 | `RAISE_OFF_SENSITIVITY` | 10000 | "Off" rung: margin so large it never trips while raising | — (see note A) | 10000 | — | — | ⚠️ |
| app.c:62 | `LOWER_HIGH_SENSITIVITY` | 250 | Lowering collision margin, most sensitive rung (mA) | `KEY_LOWER_MARGIN_HIGH` (19) | 250 | 100 | 400 | ⚠️ |
| app.c:63 | `LOWER_MED_SENSITIVITY` | 275 (was 250; changed July 2026, see note B) | Lowering margin, medium rung | `KEY_LOWER_MARGIN_MED` (20) | 275 | 150 | 500 | ⚠️ |
| app.c:64 | `LOWER_LOW_SENSITIVITY` | 300 | Lowering margin, least sensitive rung (mA) | `KEY_LOWER_MARGIN_LOW` (21) | 300 | 200 | 700 | ⚠️ |
| app.c:65 | `LOWER_OFF_SENSITIVITY` | 750 | "Off" rung while lowering — **not actually off**, trips at 750 mA (raising OFF is 10000) | — (see note A) | 750 | — | — | ⚠️ |
| nvm3_functions.c:122,125 | raise default | 350 | Fresh-unit default raise margin (matches HIGH) | existing key 3 | 350 | 150 | 1200 | ⚠️ |
| nvm3_functions.c:132,135 | lower default | 250 (was 150; changed July 2026, see note B) | Fresh-unit default lower margin (matches LOWER_HIGH) | existing key 4 | 250 | 100 | 700 | ⚠️ |
| motor_current_functions.c:24 | `MAX_LOAD_CURRENT` | 3000 | Absolute per-motor stall ceiling while raising (mA, filtered current); trips regardless of the dynamic threshold | `KEY_MAX_LOAD_CURRENT` (22) | 3000 | 1000 | 3500 | ⚠️ hard-clamp |
| motor_current_functions.c:393 | *(buried literal)* `MAX_LOAD_CURRENT - 500` | 2500 | Absolute per-motor ceiling while **lowering** — the 500 offset is an unnamed magic number | `KEY_MAX_LOAD_CURRENT_LOWER` (23) | 2500 | 800 | 3000 | ⚠️ hard-clamp |
| motor_current_functions.c:38 | `ON_CURRENT_THRESHOLD` | 80 | Filtered current above which a motor counts as "moving". **Limit-switch inference depends on this**: too high → false limit stops; too low → missed limit → motor keeps driving a stalled actuator | `KEY_ON_CURRENT_THRESHOLD` (24) | 80 | 40 | 200 | ⚠️ |
| motor_current_functions.c:35 | `NUM_CURRENTS_READ_DYNAMIC_THRESHOLD` | 350 | Readings taken after motion start before the dynamic collision threshold arms (collision detection is blind during this window, ~0.3 s) | `KEY_DYN_ARM_READINGS` (25) | 350 | 100 | 1000 | ⚠️ |
| motor_current_functions.c:36 | `NUM_CURRENTS_READ_CONTINUOUS_DYNAMIC_THRESHOLD` | 300 | Re-check window for lowering the threshold if load decreases mid-move | `KEY_DYN_RECHECK_READINGS` (26) | 300 | 100 | 1000 | |
| motor_current_functions.c:40 | `CONITNUOUS_DYNAMIC_THRESHOLD_CHANGE_MARGIN` | 500 | Current drop (mA) required before the dynamic threshold is lowered mid-move | `KEY_DYN_LOWER_MARGIN` (27) | 500 | 200 | 1500 | |
| motor_current_functions.c:33 | `NUM_INITIAL_CURRENTS_IGNORED` | 100 | Readings discarded at motion start (inrush spike) | `KEY_INRUSH_IGNORE_COUNT` (28) | 100 | 20 | 300 | ⚠️ (delays supervision) |
| motor_current_functions.c:30 | `NUM_CURRENT_READINGS` | 200 | Running-average window for the primary current average (array size — RAM, not just tuning; see note C) | compile-time only | 200 | — | — | |
| motor_current_functions.c:31 | `NUM_SECOND_CURRENT_READINGS` | 200 | Window for the second-stage average (dynamic threshold raise logic) | compile-time only | 200 | — | — | |
| motor_current_functions.c:86 | `alpha` | 0.05 | Low-pass filter coefficient for motor current (smaller = smoother/slower) | `KEY_CURRENT_LPF_ALPHA` (29, store ×1000) | 50 | 10 | 500 | ⚠️ (affects trip latency) |
| motor_current_functions.c:28 | `RAISE_DYNAMIC_THRESHOLD` | 100 | Max slope (mA per check) treated as "slow cable stretch" and absorbed into the threshold — feature currently disabled (caller commented out, app.c:1871–1876) | `KEY_CABLE_STRETCH_SLOPE` (30) | 100 | 20 | 300 | ⚠️ if re-enabled |
| motor_current_functions.c:367 | *(literal)* `> 2` | 3 reads | Consecutive "not moving" readings before a motor is declared stopped (limit-switch debounce) | `KEY_NOT_MOVING_DEBOUNCE` (31) | 3 | 2 | 10 | ⚠️ |
| app.c:68 | `DYNAMIC_THRESHOLD_TIME_INTERVAL` | 2500 ms | Period of the (disabled) cable-stretch threshold-raise timer | with 30 | 2500 | 500 | 10000 | |

**Note A — "OFF" rungs.** Exposing a true collision-off to a phone app is a
policy decision. Recommendation: do not store OFF as a margin value at all;
make it an explicit `KEY_COLLISION_ENABLE` boolean that the firmware may
refuse (or auto-re-enable on power cycle), and cap all stored margins at the
Max values above. Also note the raise/lower asymmetry today: raising OFF is
truly off (10000) but lowering OFF still trips at 750.

**Note B — resolved July 2026.** Originally, the fresh-unit lower default
(150) matched no ladder rung (250/250/300/750), and LOWER_HIGH == LOWER_MED
(both 250) made two rungs identical. `LOWER_MED_SENSITIVITY` was changed
from 250 to 275, and the fresh-unit NVM3 default in nvm3_functions.c was
changed from 150 to 250 to match `LOWER_HIGH_SENSITIVITY`. The ladder is now
250/275/300/750, and fresh units boot at the safest (HIGH) lower-sensitivity
rung. Old values (visible commented out at nvm3_functions.c:141–149) were
100/250/350/10000 — not adopted. Step A5 in REFACTOR_PLAN.md is closed.

**Note C — array-size constants.** Values that size static arrays
(`NUM_CURRENT_READINGS`, `NUM_ACCEL_READINGS`, etc.) cannot become runtime
NVM3 setpoints without rework (they'd need max-size allocation with a runtime
effective length). Listed for completeness; keep compile-time unless there's
a real need.

## 2. Auto-level (auto_level.c, app.c)

| File:Line | Name | Value | What it does | Key | Default | Min | Max | Safety |
|---|---|---|---|---|---|---|---|---|
| auto_level.c:40 | `TOLERANCE` | 0.01° | Angle window declared "level" (scaled ×100 → 1 count). Very tight — one accel LSB of noise matters | `KEY_LEVEL_TOLERANCE` (32, scaled) | 1 | 1 | 100 | |
| auto_level.c:41 | `CLOSE_TOLERANCE` | 0.1° | If an axis starts within this window, auto-level skips (disables) that axis | `KEY_CLOSE_TOLERANCE` (33, scaled) | 10 | 2 | 200 | |
| auto_level.c:42 | `PULSE_START_TOLERANCE` | 0.3° | Within this window, leveling switches from continuous drive to pulsed "sneak" moves | `KEY_PULSE_TOLERANCE` (34, scaled) | 30 | 5 | 300 | |
| auto_level.c:43 | `CONTINUOUS_LEVEL_START_TOLERANCE` | 0.2° | Intended hysteresis between pulse and continuous modes — **computed into a variable that is never read (dead)** | delete or wire up | 20 | 5 | 300 | |
| auto_level.c:46 | `SNEAK_MOTOR_OFF_TIME` | 350 ms | Pulse-mode motor OFF dwell (settle time between nudges) | `KEY_SNEAK_OFF_MS` (35) | 350 | 100 | 2000 | |
| auto_level.c:47 | `SNEAK_MOTOR_ON_TIME` | 75 ms | Pulse-mode motor ON burst length | `KEY_SNEAK_ON_MS` (36) | 75 | 25 | 500 | ⚠️ (longer bursts can overshoot into obstruction) |
| auto_level.c:48 | `PULSE_LATCH_TIME` | 1000 ms | Once pulsing starts, stay in pulse mode at least this long (anti-chatter) — latch timer start is currently commented out (auto_level.c:637–640); the latch sets but only auto-clears on success/reset | `KEY_PULSE_LATCH_MS` (37) | 1000 | 0 | 5000 | |
| auto_level.c:45 | `STEADY_ACCEL_READ_TIMER_TIME` | 1500 ms | Settle time at auto-level start before trusting the averaged angles | `KEY_ACCEL_SETTLE_MS` (38) | 1500 | 500 | 5000 | |
| app.c:55 | `ACCEL_TIMER_DURATION` | 1000 ms | Pause between finishing one axis and starting the other | `KEY_AXIS_SWITCH_PAUSE_MS` (39) | 1000 | 250 | 5000 | |
| auto_level.c:36 | `NUM_ACCEL_READINGS` | 130 | Running-average window for angle readings (array size — note C) | compile-time only | 130 | — | — | |
| auto_level.c:94 | `accel_alpha` | 0.1 | Low-pass filter coefficient on raw angle readings | `KEY_ACCEL_LPF_ALPHA` (40, ×1000) | 100 | 10 | 1000 | |
| auto_level.c:31 | `PITCH_OFFSET` | 90° | Geometry constant: pitch derived from atan2(y,z) minus 90°. Mounting-orientation dependent — installer-adjustable only if the accel can be mounted differently | `KEY_PITCH_OFFSET` (41, whole deg) | 90 | 0 | 270 | |
| existing | baseline roll angle | 0 | User "zero" roll captured in settings mode (scaled) | existing key 1 | 0 | −1500 | 1500 | |
| existing | baseline pitch angle | 0 | User "zero" pitch captured in settings mode (scaled) | existing key 2 | 0 | −1500 | 1500 | |

Baseline angle min/max: ±15° is a generous cap on how far off-level a stored
"custom zero" may be; a corrupt or absurd stored value should be rejected at
load, since auto-level will chase it with motors.

## 3. Motor control & UX timing (app.c, dual_motor_control.c)

| File:Line | Name | Value | What it does | Key | Default | Min | Max | Safety |
|---|---|---|---|---|---|---|---|---|
| app.c:1283 | `PWM_duty` | 100 % | Motor PWM duty cycle — effectively motor speed/torque scaling for all four motors | `KEY_MOTOR_PWM_DUTY` (42) | 100 | 30 | 100 | ⚠️ (changes every current signature; collision thresholds were tuned at 100 %) |
| config/sl_pwm_init_motor*_config.h | PWM frequency | 24 kHz | Motor driver PWM frequency (MPS spec 20–100 kHz). Generated config — change via configurator only; since DECISIONS #25 (one motor per timer) fully auto-generated, no hand edits | compile-time only | 24 kHz | 20 kHz | 100 kHz | ⚠️ |
| dual_motor_control.c:18 | `TEMP_MOTOR_OFF_TIME` | 1500 ms | During ALL-UP/ALL-DOWN, when one actuator hits its limit, all motors pause this long, then the rest resume | `KEY_TEMP_OFF_MS` (43) | 1500 | 500 | 5000 | ⚠️ (interacts with limit inference) |
| app.c:71 | `DOUBLE_PRESS_ERROR_CHECK_TIME_INTERVAL` | 50 ms | Period of the re-read that catches two buttons pressed together | `KEY_DOUBLE_PRESS_MS` (44) | 50 | 20 | 200 | ⚠️ |
| app.c:74 | `RELEASE_INPUT_CHECK_INTERVAL` | 500 ms | Period of the stuck-button poll — feature currently commented out (app.c:1928–1961) | with feature | 500 | 100 | 2000 | ⚠️ if re-enabled |
| app.c:53 | `COLLISION_BLINK_CYCLES` | 3 | LED blink count in the collision sequence; also sets how long the post-collision reverse runs (reverse starts at blink 2, ends when blinking ends) | `KEY_COLLISION_BLINKS` (45) | 3 | 2 | 10 | ⚠️ (indirectly sets reverse duration) |
| app.c:266 | 500 ms | 500 ms | Collision LED blink half-period (also scales the reverse duration, with 45) | `KEY_COLLISION_BLINK_MS` (46) | 500 | 100 | 1000 | ⚠️ |
| app.c:273 | 200 ms | 200 ms | Fast-blink half-period (settings mode, errors) | compile-time fine | 200 | — | — | |
| app.c:294 | 5000 ms | 5000 ms | Hold time: BED_LIGHTS → enter settings mode; AUTO_LEVEL-in-settings → erase stored angles | `KEY_SETTINGS_HOLD_MS` (47) | 5000 | 2000 | 10000 | |
| app.c:315 | 2000 ms | 2000 ms | Hold time: store zero-level / step sensitivity in settings mode | `KEY_SETTINGS_STORE_MS` (48) | 2000 | 500 | 5000 | |
| app.c:357 | 500 ms | 500 ms | Auto-level red LED blink half-period | compile-time fine | 500 | — | — | |
| app.c:555,557 | 100 ms ×2 | 200 ms total | Expander reset pulse + recovery (RC on reset pins; hardware-derived) | compile-time only | 100 | — | — | |
| motor_current_functions.c:154 | 1 ms | 1 ms | Blocking IADC warm-up/scan wait per current scan | compile-time only | 1 | — | — | ⚠️ (do not shorten; conversion window) |
| exp_ui.c:82 | 2 ms | 2 ms | Re-driver/load-switch power-up wait before every off-board I2C transaction | compile-time only | 2 | — | — | |

## 4. Hardware calibration constants (installer-level, not user-level)

| File:Line | Name | Value | What it does | Recommendation |
|---|---|---|---|---|
| adc_currents.c:183 | `result + result/2` | ×1.5 | ADC-count → mA conversion (1.21 V ref, 200 mV/A sense, 12-bit) | Keep compile-time; becomes `KEY_CURRENT_SCALE` only if sense hardware ever varies between builds |
| adc_currents.c:105 | `digAvg` | AVG8 | IADC hardware averaging per sample | Compile-time |
| mc3479.c:350 | sample rate | 1000 Hz | Accel internal rate | Compile-time |
| mc3479.c:351 | decimation | ÷10 → 100 Hz | Accel output data rate | Compile-time |
| mc3479.c:353 | LPF bandwidth | ~80 Hz | Accel analog-side filtering | Compile-time |
| mc3479.c:113 | `bits_per_g` | 16384 | Accel counts/g at 2 g range (datasheet 4.2) | Compile-time (note: comment cites 4 g = 8192, code default range is 2 g) |

## 5. Dead constants found during the sweep (no current effect)

`NUM_STEADY_ACCEL_READINGS` (app.c:54), `CONTINUOUS_LEVEL_START_TOLERANCE`
(auto_level.c:43 — computed, never read), `NUM_ACCEL_READINGS_IGNORED`
(auto_level.c:38), `NUM_CYCLES_PER_ACCEL_CHECK` (auto_level.c:39 — caller
commented out), `LOWER_DELTA_MULTIPLIER` (motor_current_functions.c:25),
`NUM_CURRENTS_READ_FAST_DYNAMIC_THRESHOLD` (motor_current_functions.c:34),
`SECOND_NUM_CURRENT_READINGS` (motor_current_functions.c:43 — duplicate),
`NUM_MOTOR_MOVEMENT_READINGS` (dual_motor_control.c:17), `slope_time_interval_ms`
(app.c:219). Slated for deletion in refactor step A1 — none should get NVM3 keys.

---

## Safety-critical set: firmware-enforced limits regardless of app requests

When these become app-adjustable (REFACTOR_PLAN.md step C1), the firmware
setter must clamp to the ranges below **before** storing or applying, and the
bed should reject — not silently clamp — values outside them, reporting the
rejection back over BLE. These bounds hold even if the phone app is buggy,
malicious, or mid-update:

1. **`MAX_LOAD_CURRENT` (raise) — never above 3500 mA, never below 1000 mA.**
   This is the last-resort stall protection; the sense chain saturates at
   ~6000 mA and the MP6522 and actuators have fixed ratings. The lowering
   variant tracks it at −500 mA and must stay below the raise value.
2. **Raise/lower collision margins — hard ceiling 1200 mA (raise) / 700 mA
   (lower), hard floor 100 mA.** Above the ceilings the "collision" detector
   is functionally off while appearing on. Below ~100 mA normal load
   variation false-trips constantly, which trains users to disable it.
3. **Collision-off — an explicit enable flag, not a big margin** (Note A).
   Recommend firmware re-enables collision detection on every power cycle,
   and never allows off while auto-level is running (auto-level moves without
   anyone holding a button).
4. **`ON_CURRENT_THRESHOLD` — 40–200 mA.** Both collision arming and
   limit-switch inference sit on this. Out-of-range values can make the
   firmware believe a driving motor has stopped (masking a stall) or that a
   stopped motor is driving (defeating limit stops).
5. **`PWM_duty` — floor 30 %.** Below some duty the actuators may stall
   without tripping anything, because every current threshold was tuned at
   100 %. Any duty change should force a collision-threshold recalibration
   (the dynamic threshold partially self-adjusts, but `MAX_LOAD_CURRENT` and
   the margins do not).
6. **Inrush-ignore and arming windows (`NUM_INITIAL_CURRENTS_IGNORED`,
   `NUM_CURRENTS_READ_DYNAMIC_THRESHOLD`) — cap at 300 / 1000 readings.**
   These define how long after motion start the system is blind to
   collisions; an app must not be able to extend the blind window
   arbitrarily.
7. **Baseline angles (existing keys 1–2) — reject |angle| > 15°** at load and
   at store. Auto-level drives motors toward whatever zero is stored.
8. **`TEMP_MOTOR_OFF_TIME` and the not-moving debounce count** — bounded per
   the tables; they gate how the system reacts to an actuator that has
   stopped against its limit while others keep driving the frame.
