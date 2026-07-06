# Tilt Bed App — Manual Setup Guide (Windows, zero app experience assumed)

Everything below is a one-time environment setup you do by hand. After this,
Claude Code does the actual app development. Estimated total time: 1–2 hours
of active work, mostly installers.

A note on strategy before you start: **you develop and iterate on Android
(or Windows desktop), and treat iOS/TestFlight as a periodic validation
target, not your daily loop.** You're on Windows, so every iOS build goes
through Codemagic's cloud and lands via TestFlight — a 15–30 minute round
trip. That's fine weekly; it's unusable for "change a button, see the
result." Your fast loops are:

- **Loop A (UI work, no hardware):** run the app on the Android Emulator or
  as a Windows desktop app against a *mock* bed (the kickoff prompt has
  Claude Code build this in from day one). Seconds per iteration.
- **Loop B (real BLE, real bed):** run on a physical Android phone via USB,
  or as a Windows desktop app using your laptop's Bluetooth adapter.
  Seconds-to-a-minute per iteration.
- **Loop C (iOS validation):** Codemagic → TestFlight on your iPhone, at
  milestones.

If you don't own any Android device: a used Pixel or Samsung in the $60–120
range is the single best money you'll spend on this project. The Android
*emulator has no Bluetooth*, so real-BLE testing needs either a physical
Android phone or the Windows-desktop path (which works but rides on a
community-maintained BLE plugin — good for the bench, not the ship target).

---

## Part 1 — Flutter toolchain on Windows

1. **Install Git for Windows** (you likely have it from the firmware work):
   https://git-scm.com/download/win — defaults are fine.
2. **Install Android Studio**: https://developer.android.com/studio
   You won't write code in it, but it's the sanctioned way to get the
   Android SDK, platform tools, and emulator. During first-run setup, accept
   the standard install (SDK + emulator).
3. **Install the Flutter SDK**:
   - Download the latest *stable* Windows zip from
     https://docs.flutter.dev/get-started/install/windows
   - Extract to `C:\dev\flutter` (avoid `Program Files` — spaces and
     permissions cause pain).
   - Add `C:\dev\flutter\bin` to your user PATH: Start → "environment
     variables" → Environment Variables → User `Path` → Edit → New.
4. **Open a fresh PowerShell** and run:
   ```
   flutter doctor
   ```
   It will list what's missing. Typical fixes:
   ```
   flutter doctor --android-licenses     (accept all)
   ```
   If it complains about "Visual Studio" — that's only needed for
   Windows-desktop builds (Loop B option). If you want that path, install
   **Visual Studio 2022 Community** with the **"Desktop development with
   C++"** workload (this is Visual Studio, not VS Code — different product,
   ~10 GB). You can defer this and add it later.
5. Green-enough `flutter doctor` (Android toolchain ✓, at minimum) = done.

## Part 2 — A device to run on

**Physical Android phone (recommended for Loop B):**
1. Settings → About phone → tap **Build number** 7 times → "You are now a
   developer."
2. Settings → System → Developer options → enable **USB debugging**.
3. Plug into the PC, accept the "Allow USB debugging?" prompt on the phone.
4. `flutter devices` in PowerShell should list it.

**Android Emulator (Loop A only — no Bluetooth):**
1. Android Studio → More Actions → Virtual Device Manager → Create device →
   any recent Pixel → download a recent system image → Finish → ▶.

## Part 3 — Create the project shell

1. Make a folder, e.g. `C:\dev\tilt_bed_app`, `cd` into it.
2. Run Claude Code there and give it the kickoff prompt
   (`APP_KICKOFF_PROMPT.md`). It will run `flutter create`, set up the
   structure, and commit as it goes. **Before the first prompt**, copy two
   files into the folder so Claude Code can reference them:
   - `BLE_GATT_DESIGN.md` (the protocol spec — the app's contract)
   - `DECISIONS_D1_additions.md`
3. First smoke test once Claude Code says it's runnable:
   ```
   flutter run
   ```
   with your phone plugged in (or emulator running). Hot reload: press `r`
   in the terminal after edits; full restart: `R`.

## Part 4 — Version control

Same discipline as the firmware. In the app folder:
```
git init
git add . && git commit -m "flutter scaffold"
```
Push to Bitbucket/GitHub — **required** for Codemagic later, and Codemagic's
free tier connects to both. (GitHub is the smoother Codemagic integration if
you're indifferent.)

## Part 5 — iOS pipeline (do this when Apple enrollment completes; nothing blocks on it)

Prereqs you already have in motion: D-U-N-S number, business Apple ID with
2FA, Apple Developer **Organization** enrollment in progress ($99/yr).

Once enrollment is approved:

1. **App Store Connect** (https://appstoreconnect.apple.com):
   - Users and Access → Integrations → **App Store Connect API** → generate
     a key with **App Manager** role. Download the `.p8` file (one chance
     only — store it safely), note the Key ID and Issuer ID.
   - Apps → **＋ New App**: platform iOS, a bundle ID like
     `com.tiltbedsystems.tiltbed` (register the bundle ID at
     https://developer.apple.com/account → Identifiers first).
2. **Codemagic** (https://codemagic.io):
   - Sign up with the account hosting your repo; add the repo.
   - Team settings → integrations → **Apple Developer Portal**: upload the
     `.p8`, Key ID, Issuer ID. This enables *automatic code signing* —
     Codemagic creates and manages the certificates/profiles so you never
     touch a Mac.
   - Use the Flutter workflow: build **iOS release**, distribute to
     **TestFlight**, trigger on pushes to a `release` branch (Claude Code
     will write the `codemagic.yaml`; you just paste the three identifiers
     into Codemagic's UI as secrets — never into the repo).
3. **TestFlight on your iPhone**: install the TestFlight app, add yourself
   as an internal tester in App Store Connect → your app → TestFlight.
   Internal testers get builds minutes after processing, no Apple review.
4. **iOS BLE permission note** (Claude Code handles the code side): iOS
   requires `NSBluetoothAlwaysUsageDescription` in `Info.plist` — a
   user-facing sentence like "Tilt Bed uses Bluetooth to control your bed."
   Missing it = instant crash on first BLE use, only on iOS. It's in the
   kickoff prompt so it's not forgotten.

## Part 6 — Order of operations (what depends on what)

1. Parts 1–4 now. App development starts immediately against the **mock
   bed** — none of it waits on firmware D1 or Apple.
2. Firmware D1 (BLE on the BGM220) proceeds in parallel in the firmware
   repo with Claude Code there.
3. First real-BLE milestone: Loop B phone + bench board, running the
   REGRESSION-style checks in BLE_GATT_DESIGN.md §12.
4. Apple/Codemagic (Part 5) whenever enrollment clears — first TestFlight
   build is worth doing early with the mock-only app just to prove the
   pipeline, before you need it.

## Troubleshooting quick hits

- `flutter doctor` can't find Android SDK → Android Studio → SDK Manager →
  note the SDK path → `flutter config --android-sdk <path>`.
- Phone not in `flutter devices` → different USB cable (charge-only cables
  are common), re-accept the debugging prompt.
- Windows-desktop build errors about CMake/MSVC → the Visual Studio C++
  workload from Part 1 step 4 isn't installed.
- Android 12+ BLE needs *runtime* permissions (`BLUETOOTH_SCAN` /
  `BLUETOOTH_CONNECT`) — if scanning silently finds nothing, it's almost
  always permissions; this is handled in the kickoff prompt.
