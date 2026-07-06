# BLE_GATT_DESIGN.md

BLE GATT layer design for refactor step D1, recorded 2026-07-05. This is the
interface contract between the firmware (`bed_control.c` / `bed_status.c` /
`bed_settings.c`) and the phone app. Where this document assigns behavior to
the firmware vs. the app, that assignment is deliberate: **the firmware is
always the safety authority** (source lock, clamping, watchdogs, supervision);
the app is a remote control and display, never a safety layer.

Binds with [DECISIONS.md](DECISIONS.md) #12–#17 and the D1 additions (#18+).
Where this document conflicts with older REFACTOR_PLAN.md text, this wins.

---

## 1. Design goals (ranked)

1. **Safety parity with the switches.** No app state — frozen UI, dropped
   link, backgrounded app, second phone — may leave a motor running without a
   live human intent signal. Collision/limit supervision (#13) already sits
   below the command API and is inherited for free; this layer adds the
   *intent liveness* guarantees BLE needs.
2. **Lightning-fast connect.** App-open (or widget tap) to live control in
   under ~1 s on a bonded phone. The product is a wireless remote first.
3. **Firmware-authoritative.** The two-source lock (#12), settings clamps
   (C1), and role enforcement (#14) are all decided on the BGM220. The app
   renders what the firmware reports; it never assumes.
4. **Protocol stability.** Small packed binary formats, little-endian,
   versioned, designed to grow without breaking old apps (reserved bytes,
   append-only status).

## 2. SDK and stack configuration

- **Migrate to Simplicity SDK 2026.6.0 LTS at D1** (see DECISIONS #20 for
  full rationale and the fresh-project-port migration mechanics). The Gecko
  SDK line is closed to Series 2 devices — GSDK 4.5 LTS covers Series 0/1
  only — so the BGM220 must move to SiSDK for any future BLE stack fixes.
  D1 starts from the SiSDK "Bluetooth – SoC Empty" sample for the
  BGM220SC22HNA2 and ports the application sources in; the existing 4.2.2
  project remains untouched on its own branch as the known-good fallback.
  Run the full REGRESSION.md pass on the ported project **before** adding
  any BLE glue code.
- Bluetooth components: `bluetooth_stack`, `bluetooth_feature_legacy_advertiser`,
  `bluetooth_feature_connection`, `bluetooth_feature_gatt_server`,
  `bluetooth_feature_sm` (security manager), `bluetooth_feature_system`,
  `gatt_configuration` (GATT Configurator), plus the OTA components in §9.
- `SL_BT_CONFIG_MAX_CONNECTIONS = 2` (control + viewer, #14).
- Adding components regenerates `autogen/` and `config/`. Since DECISIONS
  #25 (one motor per timer) there are **no hand-edited PWM configs to
  protect** — regeneration is safe. Still glance at the git diff of
  `config/` after any configurator change, as basic hygiene.
- Audit blocking delays (P3.16) before enabling the stack; the 2 ms-per-I2C
  re-driver delay is the known offender. The sl_bt event loop runs from the
  same super loop — long ticks add command latency (they do not break radio
  timing, which is interrupt-driven, but they eat the latency budget in §7).
- **Check RAM after adding the stack** (32 KB part). Trim
  `SL_BT_CONFIG_BUFFER_SIZE` if needed; 2 connections + small MTU keeps the
  stack footprint modest.

## 3. Service topology

| Service | UUID | Purpose |
|---|---|---|
| **Tilt Bed Service** (custom) | `54b10000-ef39-4f03-a91d-eb12b0d067c9` | Commands, telemetry, role, settings |
| Device Information (standard) | `0x180A` | Manufacturer, model, firmware rev (from `version.h`) |
| Silicon Labs OTA (standard Silabs) | `1d14d6ee-fd63-4fa1-bfa4-8f47b42119f0` | AppLoader OTA DFU (§9) |

Custom UUID family: `54b1XXXX-ef39-4f03-a91d-eb12b0d067c9`, `XXXX` below.

### Tilt Bed Service characteristics

| # | Name | UUID `XXXX` | Properties | Size | Direction |
|---|---|---|---|---|---|
| 1 | Command | `0001` | Write Without Response | 4 B | app → bed |
| 2 | Command Result | `0002` | Notify | 2 B | bed → app |
| 3 | Bed Status | `0003` | Read + Notify | 20 B | bed → app |
| 4 | Client Role | `0004` | Read + Notify (per-connection, user type) | 1 B | bed → app |
| 5 | Settings RPC | `0005` | Write + Indicate | 4 B req / 10 B resp | both |
| 6 | Protocol Version | `0006` | Read | 1 B | bed → app |

All six require an **encrypted, bonded link** (§8). Protocol Version starts
at `1`; the app refuses to operate above its known max and warns below its
known min.

## 4. Command characteristic (0001)

Packed, little-endian, 4 bytes:

```
offset 0  opcode   u8
offset 1  arg0     u8
offset 2  arg1     u8
offset 3  seq      u8   rolling per-connection sequence number
```

| Opcode | Name | arg0 | arg1 | Semantics |
|---|---|---|---|---|
| 0x01 | MOVE_START | target: 0 HEAD, 1 LEFT, 2 RIGHT, 3 FOOT, 4 ALL | dir: 0 DOWN, 1 UP | `bed_move_start(BED_SOURCE_APP, …)`. **Repeated every 100 ms while held** — a repeat with identical target/dir refreshes the dead-man watchdog (§5). A repeat with *different* target/dir is treated as MOVE_STOP then MOVE_START. |
| 0x02 | MOVE_STOP | — | — | `bed_move_stop(BED_SOURCE_APP)`. Sent on button release. |
| 0x03 | AUTO_LEVEL_START | — | — | `bed_auto_level_start(BED_SOURCE_APP)`. **Latched** (#12) — no keepalive; runs to completion or stop. |
| 0x04 | AUTO_LEVEL_STOP | — | — | `bed_auto_level_stop(BED_SOURCE_APP)`. |
| 0x05 | LIGHTS_SET | 0 off / 1 on | — | `bed_lights_set(BED_SOURCE_APP, …)`. Latched. |
| 0x06 | STOP_ALL | — | — | Explicit full stop (maps to the `app_stop_all()` path: motors + auto-level + LEDs). The app's big red button. Always accepted from **any** role, control or viewer — this is the BLE embodiment of the stop-from-anyone rule (#12). |

Notes:

- There is **deliberately no RESET opcode** (#16).
- Write Without Response is chosen for latency (no ATT round-trip at the
  100 ms repeat cadence). Loss tolerance comes from the repeated-intent
  design: a lost MOVE_START is corrected ≤100 ms later by the next repeat;
  a lost MOVE_STOP is corrected ≤300 ms later by the watchdog.
- All opcodes except STOP_ALL are subject to arbitration: commands from a
  VIEWER connection are rejected (result 0x02) *before* reaching
  `bed_control` — the viewer phone is not a third source, it is a
  non-command-capable window into the single APP source (#14).

## 5. Dead-man watchdog (new safety element, firmware-side)

Lives in the BLE glue module (`ble_bed_service.c`), **above** `bed_control`
and below the radio:

- On MOVE_START from the control connection: start/refresh a **300 ms**
  sleeptimer (ms-defined, per DECISIONS #2).
- Each identical repeated MOVE_START refreshes it.
- Expiry → clean full stop via the `app_stop_all()` path, and a
  Command Result `WATCHDOG_STOP` notification if the link is still up.
- MOVE_STOP, STOP_ALL, collision, or limit stop cancels the watchdog.
- AUTO_LEVEL and LIGHTS never arm it (latched commands; auto-level is
  already supervised below the API per #13).
- **Control-connection disconnect while APP is the active source → clean
  full stop, auto-level included.** `bed_move_stop()` alone is
  insufficient (it doesn't kill auto-level); use the stop-all path.

Timing rationale: 100 ms repeat / 300 ms expiry tolerates two consecutive
lost intervals at the 15–30 ms connection interval while keeping worst-case
uncommanded travel under ~⅓ s — comparable to human release-reaction time on
the physical switches. Both constants are named settings candidates but ship
as compile-time constants in v1.

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

Notify, 2 bytes: `{ seq u8, result u8 }`, echoing the command's seq.

| Result | Meaning | App UI |
|---|---|---|
| 0x00 | ACCEPTED | — |
| 0x01 | FORCE_STOPPED_OTHER_SOURCE | "Stopped — wall switch is in control" (the #12 stop-from-anyone path made visible) |
| 0x02 | REJECTED_VIEWER | "Another phone has control" |
| 0x03 | REJECTED_SETTINGS_MODE | "Bed is in settings mode" |
| 0x04 | REJECTED_INVALID | malformed / unknown opcode |
| 0x05 | WATCHDOG_STOP | connection stalled; movement stopped |

Latency budget for the perceived press→motion path: connection interval
15–30 ms (write lands next event) + one firmware tick + motor turn-on ≈
well under 100 ms — indistinguishable from the wall switch.

## 8. Connections, roles, bonding, fast connect

**Roles (#14).** Two connections max. First bonded phone to connect =
CONTROL; second = VIEWER. Client Role (0004) is a *user-type* characteristic
so its read value and notifications are computed per connection handle:
`0 = CONTROL, 1 = VIEWER`. Viewer command writes are answered
REJECTED_VIEWER (except STOP_ALL, always honored). **On control-phone
disconnect, the viewer is auto-promoted to CONTROL and notified** — proposed
as DECISIONS #21; without promotion the surviving phone must bounce its
connection to gain control, which reads as a bug.

**Bonding & security.** LE Secure Connections, Just Works (no display →
no MITM claim; acceptable threat model for a vehicle interior). All
characteristics require encryption + bonding. **New bonds are accepted only
during a physical pairing window: while settings mode is active on the
switch panel** (proposed DECISIONS #19). Outside the window,
pairing/bonding requests are rejected, so a stranger within radio range
cannot enroll. Store 8 bonds in NVM3.

**Advertising (fast-connect lever #1).** The board is powered from the van
battery bank — coin-cell advertising economics do not apply. Advertise
**continuously whenever a connection slot is free**, connectable undirected,
**interval 30 ms**, payload = flags + 128-bit Bed Service UUID; device name
(`Tilt Bed`) in the scan response. Keep advertising (for the viewer slot)
while one phone is connected.

**Connection parameters (fast-connect lever #2).** iOS does not let the app
choose the interval, so the **peripheral requests** it after connect:
interval 15–30 ms, latency 0, supervision timeout 1 s (safety does not
depend on the timeout — the §5 watchdog does that; 1 s avoids spurious
drops). Compliant with Apple accessory guidelines.

**App-side (fast-connect lever #3, recorded here for the app spec).**
Scan filtered on the service UUID → direct connect to the bonded identity →
encryption resumes from the bond (no pairing UI) → **skip service discovery
via the OS GATT cache** → enable Status + Command Result + Client Role
notifications → first Status notification renders the UI live. Target
< 1 s from app foreground on a bonded phone.

## 9. Settings RPC characteristic (0005)

Single generic characteristic mirroring `bed_settings.c`'s key table —
scales to new settings with zero GATT changes.

Request (Write, 4 B): `{ op u8, id u8, value i16 }`
`op`: 0 GET, 1 SET, 2 ERASE, 3 GET_META. `id` = `bed_setting_id_t`.

Response (Indicate, 10 B):
`{ op u8, id u8, result u8, value i16, min i16, max i16, default i16 }`
`result`: 0 OK, 1 OUT_OF_RANGE (rejected — `bed_settings_set()` rejects, it
does not silently clamp), 2 NVM3_FAIL, 3 BUSY (bed not idle), 4 DENIED
(viewer connection), 5 UNKNOWN_ID.

Rules: SET/ERASE accepted only from the CONTROL connection and only while
the bed is idle. Indication (acknowledged) rather than notify — settings are
rare and correctness matters more than latency. GET_META lets the app build
its sliders from firmware-true min/max/default instead of hardcoding.
Firmware clamping (C1) remains the safety authority regardless of anything
the app sends.

*Open item for the calibration-wizard phase (#7): the wizard is a latched,
lock-holding command sequence, not a settings write — it will need its own
opcodes on the Command characteristic plus wizard-progress reporting.
Deliberately out of v1 scope.*

## 10. OTA (D2, designed now)

- **Gecko Bootloader, "Bluetooth AppLoader OTA DFU" configuration** — the
  standard Silabs path; app image is replaced by AppLoader, no storage slot
  needed at this image size (~53 KB app in 504 KB flash: enormous headroom).
- The bootloader project is a **separate build**, flashed once over the
  wired J-Link/Simplicity programmer. **Every board must receive it at D1**
  even though the app-side OTA UI ships later — a board without the
  bootloader can never be updated remotely.
- Update flow: app writes OTA Control (enter DFU) → device reboots into
  AppLoader (advertises OTA service only) → app streams the signed `.gbl`
  → reboot into new firmware. The app refuses to start DFU while any motor
  is on or auto-level is active.
- Sign/encrypt GBL images from day one (keys generated once, kept out of
  git) so field units only accept authentic firmware.

## 11. Firmware module plan (D1)

New file `ble_bed_service.c/.h` — all sl_bt event handling and the glue:

```
sl_bt_on_event()
  boot            → configure security, start advertising
  connection      → track handles, assign CONTROL/VIEWER, request conn params
  disconnect      → clean-stop if control+APP-source (§5), promote viewer, re-advertise
  gatt write      → decode Command / Settings RPC, arbitrate role, call
                    bed_move_start / bed_move_stop / bed_auto_level_* /
                    bed_lights_set / bed_settings_* — nothing else; the
                    verbs and the lock (#12) are untouched
  gatt read (user)→ Client Role per connection
ble_bed_service_tick()  → watchdog check, adaptive status notify cadence
```

`bed_control.c` stays pure and host-testable (#12). Extend
`test/bed_control_test.c` with the app-source scenarios BLE will now
actually exercise (APP holds lock / switch press force-stops; switch holds /
app command force-stops; STOP_ALL from either).

## 12. Bench verification plan (append to REGRESSION.md at D1)

1. Full existing REGRESSION.md pass on the migrated SDK **before** any BLE
   code is added (isolates migration regressions from BLE regressions).
2. Bonded-phone connect time measured cold and warm (< 1 s warm target).
3. Hold-to-run: hold app UP, kill the app process mid-hold → motors stop
   within ~300 ms. Repeat with Bluetooth toggled off, phone walked out of
   range, and phone rebooted.
4. Two-source: app moving → wall-switch press → immediate stop, result 0x01
   on the phone. Switch moving → app press → immediate stop, switch LEDs
   consistent.
5. Two-phone: phone A controls, phone B connects → B shows viewer banner;
   B's moves rejected, B's STOP_ALL works; A disconnects → B promoted.
6. Settings: out-of-range SET rejected with OUT_OF_RANGE and live value
   unchanged; SET while moving returns BUSY; value survives power cycle.
7. Pairing window: bond attempt outside settings mode rejected; inside, ok.
8. Collision during app-commanded move: trip + reverse + LED sequence
   unchanged; app sees collision flag within one notify interval.
9. OTA: full DFU cycle; DFU attempt while moving refused by app UI;
   power-pull mid-DFU recovers to AppLoader (re-flashable, not bricked).
