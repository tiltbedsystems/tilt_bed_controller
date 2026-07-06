# APP_KICKOFF_PROMPT.md — paste the block below into Claude Code

Run Claude Code inside your (empty) `tilt_bed_app` folder, with
`BLE_GATT_DESIGN.md` and `DECISIONS_D1_additions.md` already copied in.
Paste everything between the lines.

---------------------------------------------------------------------------

You are starting a brand-new Flutter app from an empty folder (plus two
design docs already present: `BLE_GATT_DESIGN.md` — the BLE protocol
contract — and `DECISIONS_D1_additions.md`). Read both fully before writing
anything.

## What this app is

The companion app for Tilt Bed Systems, a self-leveling camper-van bed. The
bed controller is custom firmware on a Silicon Labs BGM220 that exposes the
GATT interface specified in `BLE_GATT_DESIGN.md`. The app is a wireless
remote first — mirror of the physical switch panel — plus a settings menu
and live telemetry. The #1 product requirement: **app-open to live control
in under ~1 second on a bonded phone.** The firmware is always the safety
authority (source lock, watchdogs, clamping); the app renders firmware state
and expresses user intent, never enforces safety itself.

## Who you're working with

I am an electrical engineer (power systems). I have deep embedded/firmware
context on this product but **zero mobile or Flutter experience** — this is
my first app. Work accordingly:

- **Explain before building.** Before each chunk of work, explain in plain
  terms what you're about to create and why — assume EE background, not
  software background. Gloss any Flutter/Dart idiom the first time you use
  it (what a Widget is, what a Provider is, what async/await means, etc.).
- **Restate and wait before large changes.** For anything beyond a small
  fix, restate the plan and wait for my approval.
- **Small verifiable steps.** After each step, tell me exactly what to run
  (`flutter run`, press `r`, etc.) and what I should see on screen. I test
  on a real device; you don't.
- **Never bury a manual step.** Anything I must do by hand (permissions
  files, store dashboards, signing) — call it out loudly and separately.

## Hard requirements

1. **Transport abstraction from day one.** Define an abstract `BedTransport`
   interface (connect/disconnect state stream, send command, status stream,
   command-result stream, client-role stream, settings RPC). Two
   implementations:
   - `MockBedTransport`: a simulated bed — motors that ramp current
     realistically, limits, an occasional simulated collision, settable
     tilt, a fake second-source event. All UI work happens against this;
     it must be selectable at runtime from a debug menu.
   - `BleBedTransport`: `flutter_blue_plus`, implementing
     `BLE_GATT_DESIGN.md` exactly — UUIDs, packet layouts (little-endian,
     20-byte status, 4-byte command), the 100 ms repeated MOVE_START while
     a button is held, MOVE_STOP on release, and STOP_ALL.
2. **Hold-to-run is sacred.** Movement buttons act only while pressed
   (Listener/pointer-down → start + 100 ms repeat timer; pointer-up/cancel
   → MOVE_STOP). App lifecycle pause/background, navigation away, or any
   exception ⇒ immediately send MOVE_STOP and stop the repeat timer. The
   firmware watchdog is the backstop, not the mechanism.
3. **Fast-connect flow** per spec §8: on app foreground, scan filtered on
   the service UUID → direct connect to the remembered bonded device →
   subscribe to Status, Command Result, and Client Role → UI goes live on
   the first Status notification. Instrument this path with timing logs so
   we can measure against the 1 s target.
4. **Role UX.** If Client Role = VIEWER, show a persistent banner
   ("Another device has control"), disable all controls except a prominent
   STOP button (STOP_ALL is always honored). Handle live promotion to
   CONTROL. Surface Command Result codes as human messages (spec §7 table).
5. **State management: Riverpod.** Providers layered as: transport →
   connection state machine (disconnected / scanning / connecting /
   connected-control / connected-viewer / error) → bed status model → UI.
   Explain this layering to me when you build it.
6. **Platform permission correctness** (get these right the first time):
   - Android 12+: `BLUETOOTH_SCAN` (neverForLocation) + `BLUETOOTH_CONNECT`
     runtime permissions with a friendly pre-prompt rationale screen.
   - iOS: `NSBluetoothAlwaysUsageDescription` in Info.plist.
7. **Testing:** unit tests for packet encode/decode against the exact byte
   layouts in the spec (these are the app-side twin of the firmware's
   host-run arbitration test), and widget tests for hold-to-run
   (pointer-down starts repeats, pointer-up sends stop, lifecycle pause
   sends stop). Run tests before declaring any phase done.
8. **Git discipline:** small commits with clear messages, one concern per
   commit, after each verified step.

## Build in phases — stop for my approval between phases

- **Phase 1 — Skeleton + mock.** `flutter create` (org
  `com.tiltbedsystems`, app `tilt_bed`), folder structure, Riverpod wiring,
  `BedTransport` + `MockBedTransport`, protocol encode/decode library with
  its unit tests, and a Remote screen: HEAD/LEFT/RIGHT/FOOT × UP/DOWN,
  ALL UP/DOWN, AUTO LEVEL, LIGHT, big STOP — all driving the mock, with
  live mock telemetry (currents, tilt) displayed simply. There is
  deliberately NO RESET button (firmware DECISIONS #16).
- **Phase 2 — Real BLE.** `BleBedTransport`, permissions flow, scan/
  bond/connect UX, fast-connect instrumentation, reconnect-on-foreground.
  Ends with a bench-test checklist for me mirroring BLE_GATT_DESIGN.md §12
  items 2–5.
- **Phase 3 — Settings.** Settings screen driven by GET_META (sliders built
  from firmware min/max/default, never hardcoded): auto-level setpoint
  (manual entry or capture-current-position), raise/lower collision
  thresholds. Warning banner that bed controls are disabled while editing.
- **Phase 4 — Telemetry.** Instantaneous + windowed total current, live
  tilt (greyed when `tilt_fresh` is 0), per-motor detail, and a first-pass
  graphical collision-location estimate from per-actuator current rise.
- **Phase 5 — Pipeline + OTA.** `codemagic.yaml` for iOS TestFlight builds
  (secrets stay in Codemagic UI, never the repo), Android release config,
  then the OTA screen implementing the Silabs AppLoader DFU flow (spec
  §10), refusing to start while the bed is moving.

## First actions, in order

1. Read the two design docs; summarize back the protocol in your own words
   so I can confirm your understanding (packet layouts, roles, watchdog
   timing, STOP_ALL semantics).
2. Propose the folder structure and the `BedTransport` interface signature.
   Wait for my approval.
3. Write `CLAUDE.md` for this repo capturing the working style and hard
   requirements above (model it on the firmware repo's CLAUDE.md: explain
   before editing, ask when ambiguous, small verifiable steps, hold-to-run
   and stop-on-lifecycle rules as hard constraints, protocol byte layouts
   only ever changed in lockstep with BLE_GATT_DESIGN.md).
4. Then begin Phase 1.

---------------------------------------------------------------------------

## After pasting (for you, Andrew — not part of the prompt)

- Claude Code will ask you to confirm its protocol summary — check the
  watchdog numbers (100 ms repeat / 300 ms expiry) and the 20-byte status
  layout against BLE_GATT_DESIGN.md yourself; it's the contract.
- When Phase 1 runs, you should see the full remote UI moving a *fake* bed
  on the emulator or your phone within the first session.
- Don't start Phase 2 until firmware D1 has something advertising on the
  bench — Phase 1 and firmware D1 can proceed the same week in their two
  repos.
