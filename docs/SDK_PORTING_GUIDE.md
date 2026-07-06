# SDK_PORTING_GUIDE.md

Step-by-step migration of the totem_controller firmware from Gecko SDK 4.2.2
to **Simplicity SDK 2026.6.0 LTS**, by creating a fresh "Bluetooth – SoC
Empty" project and porting the application code into it (DECISIONS #20).

Written for someone doing this for the first time and nervous about it.

> **Workflow errata (2026-07-05, learned during execution):** Simplicity
> Studio 6 has no internal IDE — Studio is the *configurator* (Software
> Components + Pin Tool GUIs) and **all building/flashing/debugging happens
> in VS Code** via the "Simplicity Studio for VS Code" extension (project
> created with Target IDE = "VS Code (GCC)", With Project Files = "Link sdk
> and copy project sources"). Wherever this guide says "hammer icon" or
> implies building inside Studio, read "build in VS Code". Phase 4's
> component/pin content is superseded by **HARDWARE_CONFIG.md**, which is
> the authoritative checklist.

The guiding safety principle of the whole procedure:

> **Your existing 4.2.2 project is never modified. You do not let your own
> code touch the new SDK until you have proven the new SDK can build and run
> a stock example by itself. Every phase ends in a check you confirm before
> moving to the next.**

If any gate fails, you stop there — nothing you've done has endangered the
working firmware, and the failure is isolated to one small, known step.

## Two official guides to keep open in a browser tab

Silicon Labs' exact button/tile labels in the new Simplicity Studio 6 /
Simplicity Installer may differ slightly from the wording below (that
tooling is new as of 2025.12–2026.6). Keep these open and trust them over me
where a label differs:

- **Simplicity Studio 6 User's Guide → Install Simplicity Studio**
  (the Simplicity Installer walkthrough).
- **Simplicity Studio Project Migration Guide** (referenced from the SiSDK
  2026.6.0 release notes — the GSDK→SiSDK migration reference).

Do **not** follow the "SLT-CLI / slc-cli / CMake / VS Code" instructions if
you land on them — that's the command-line path, not yours. You want the
graphical Simplicity Studio path throughout.

---

## Phase 0 — Build the safety net (30 min, do not skip)

The point of Phase 0 is that after it, nothing you do later can lose your
working firmware.

1. In your **current** Simplicity Studio 5, confirm the 4.2.2 project still
   builds clean (hammer icon → 0 errors). You want a known-good starting
   point, not a surprise later that predates the migration.
2. In your git client, make sure everything is committed on your working
   branch. Then **tag** this commit so it's findable forever, e.g.
   `pre-sisdk-migration`. (Command line: `git tag pre-sisdk-migration`
   then `git push --tags`. Or use your git GUI's "tag" feature on the
   latest commit.)
3. Create a new branch for the migration so `main`/your working branch
   stays pristine: e.g. `git checkout -b sisdk-migration`. All migration
   work happens here.
4. Confirm the known-good binary is committed: `GNU ARM v10.3.1 - Debug/
   totem_controller.hex` should be in git (your .gitignore keeps it
   intentionally). This is your "flash back to working" escape hatch if you
   ever need to prove the hardware still works.
5. **Cold backup outside git too:** copy the entire project folder to a
   backup location (e.g. `totem_controller_BACKUP_4.2.2`) on your drive or a
   USB stick. Belt and suspenders — this is the version you know works.

**Gate 0:** working project builds, everything committed, tagged, on a new
branch, and a cold copy exists off to the side. ✅ before continuing.

---

## Phase 1 — Install Simplicity Studio 6 + Simplicity SDK 2026.6.0 (1–2 hr, mostly download/wait)

You can leave Simplicity Studio 5 installed. Don't uninstall it until the
whole migration is proven — it's part of your escape hatch.

1. Go to the **Simplicity Studio v6** download page on silabs.com (search
   "Simplicity Studio download"). Download the **Windows Simplicity
   Installer**.
2. Run the installer. Sign in with your Silicon Labs account when prompted
   (create one if you somehow don't have it — free).
3. When it offers install types (tiles such as **Full Install**,
   **Technology Install**, **Advanced**):
   - Simplest: choose **Full Install**. It's large but you never wonder
     whether a needed component is missing.
   - If disk space matters: **Advanced** (or **Technology Install**) →
     ensure **Bluetooth** and the **32-bit / MCU** content are selected.
4. When it asks which **SDK** to install, select **Simplicity SDK
   2026.6.0** (the LTS). If both an SDK and a toolchain are offered, let it
   install the bundled **GNU ARM toolchain (12.2.rel1)** — SiSDK 2026.6
   expects a newer GCC than your old 10.3.1, and letting the installer
   provide it avoids a whole class of toolchain mismatch errors.
5. Accept the license, install, and wait. This pulls a lot down (the new
   Conan-package delivery), so give it time and a stable connection.
6. Launch Simplicity Studio 6 when it finishes.

**Gate 1:** Simplicity Studio 6 launches, and under its SDK/Package manager
you can see **Simplicity SDK 2026.6.0** installed and a **GNU ARM 12.x**
toolchain present. ✅ before continuing.

---

## Phase 2 — Prove the new SDK builds a stock example, before your code is anywhere near it (30–45 min)

This is the most important gate in the whole guide. If a stock Bluetooth
example builds and runs, you've proven install + toolchain + part support +
radio all work — so any problem later is *your port*, not the environment.

1. Connect your board via the debug programmer (J-Link) if you have it
   handy — not strictly required to build, but you'll want it for the
   optional flash test at the end of this phase.
2. Start the new-project wizard. In Studio 6 this is under **File → New →
   Silicon Labs Project Wizard…** (or a **Create New Project** button on
   the Launcher/Welcome screen — same wizard either way).
3. **Target/board & SDK selection screen:**
   - If your board is connected and detected, pick it. If not, choose
     **"Start from a part"** and type your exact OPN: **BGM220SC22HNA2**.
   - **SDK:** select **Simplicity SDK 2026.6.0**.
   - **Toolchain:** **GNU ARM 12.x**.
   - Click **Next**.
4. **Example project selection screen:** in the filter/search box type
   **SoC Empty**. Select **"Bluetooth – SoC Empty"**. Click **Next**.
   - Why this one: it ships with the BLE stack, advertising, GATT server,
     and OTA-DFU scaffolding already configured and building. You start
     from a working radio instead of assembling one.
5. **Naming screen:** name it something like **`totem_controller_ble`**.
   Leave "link SDK and copy project sources" at the default (copy sources).
   Click **Finish**.
6. Studio generates the project and opens it. **Build it** (hammer icon).
   Wait for "Build Finished" with **0 errors**.
   - If it fails here, it's an install/toolchain issue, not your code —
     stop and resolve it (usually: wrong toolchain selected, or SDK not
     fully installed). This is exactly why we build the stock example first.
7. **(Recommended) Flash-and-sniff test:** flash the built SoC Empty to your
   board. On your phone install a BLE scanner (**nRF Connect** or
   **LightBlue**) and confirm you can see it advertising (it'll show a
   generic name like "Empty Example"). This proves the radio path end to
   end on your actual hardware.
8. Re-flash your known-good `totem_controller.hex` afterward if you want the
   bench board back to normal behavior meanwhile (optional).

**Gate 2:** stock "Bluetooth – SoC Empty" builds with 0 errors on SiSDK
2026.6.0, and (recommended) advertises when flashed. ✅ before your code
enters the picture.

---

## Phase 3 — Bring your application source files into the new project (30 min)

Now, and only now, your code meets the new SDK. You're copying **only your
own source files** — never the old SDK, old autogen, or old build output.

**Files to copy from the old project into the new project folder:**

Your hand-written modules (the whole set):
```
adc_currents.c/.h      auto_level.c/.h        bed_actions.c/.h
bed_control.c/.h       bed_settings.c/.h      bed_status.c/.h
bed_types.h            debug_capture.h        dual_motor_control.c/.h
exp_board.c/.h         exp_ui.c/.h            mc3479.c/.h
motor_current_functions.c/.h                  nvm3_functions.c/.h
pi4ioe5v6416.h         settings_mode.c/.h     switch_input.c/.h
version.h
```
Plus your `test/` folder (host unit tests — they don't affect the embedded
build but you want them in the repo). Do NOT copy the old `CLAUDE.md`: it
pins the SDK at 4.2.2 and forbids SDK changes, which is now wrong. Write a
fresh CLAUDE.md for the new repo instead (same working-style rules, updated
SDK/tooling facts). The `docs/` folder is maintained in the new repo
directly.

**Do NOT copy:**
```
app.c / app.h          ← SEE THE SPECIAL CASE BELOW — do not blind-copy
main.c                 ← the new project has its own; keep the new one
autogen/  config/      ← generated; the new project owns these
gecko_sdk_4.2.2/       ← the old SDK; gone
GNU ARM v10.3.1 - *    ← old build output
*.slcp *.slps *.pintool *.cproject *.project  ← old project metadata
```

**The one special case — `app.c` / `app.h`:** both your old project *and*
the SoC Empty sample have an `app.c`/`app.h` with `app_init()` and
`app_process_action()`. The sample's version also contains the Bluetooth
event handler (`sl_bt_on_event`) and the boot/advertising setup. You cannot
just overwrite one with the other. This is the single non-mechanical merge
in the whole port, and it's exactly what Claude Code is good at:

- **Recommended approach:** keep the **sample's** `main.c` and the sample's
  `app.c` skeleton (for the `sl_bt_on_event` and boot handling), and **merge
  your** `app_init()` body and `app_process_action()` tick body into it —
  your init calls and your tick sub-systems get called from within the
  sample's structure. Hand this to Claude Code with both files visible and
  let it produce the merged `app.c`; review the diff.
- Until BLE glue is written (that's the *next* phase of work, not this
  migration), the merged `app.c` just needs to call your existing
  `app_init()`/`app_process_action()` logic so the firmware behaves exactly
  as it does today, now sitting on the new SDK with a dormant radio.

**Mechanics of adding the files in Studio:**
1. Copy the files into the new project's root folder in Windows Explorer.
2. In Studio, right-click the project → **Refresh** (F5). The files appear.
3. Studio's build normally picks up `.c` files in the project tree
   automatically. If any aren't compiled, right-click the file → check it's
   **not** excluded from build (Resource Configurations → Exclude from
   Build unchecked). (Explanation: "included in build" just means the
   compiler is told to compile that file — the equivalent of listing it in
   the old `.slcp`'s `source:` list.)

**Gate 3:** all your source files are present in the new project tree and
the special-case `app.c` merge is done (even if it doesn't compile yet).
✅ before continuing.

---

## Phase 4 — Re-create your hardware configuration in the configurator (1–2 hr, fiddly but mechanical)

Your old `.slcp` and `.pintool` are the **written record of every component
and pin you need**. You're going to reproduce that configuration in the new
project through the GUI configurator. Keep the old `totem_controller.slcp`
and `totem_controller.pintool` open (in a text editor / the old Studio) as
your checklist.

1. In the new project, open its `.slcp` file → it opens the **Project
   Configurator**. Go to the **Software Components** tab.
2. Add each component your old `.slcp` lists. From your old config, that's:
   - **IADC** (emlib_iadc) — Platform → Peripheral.
   - **I2CSPM** — two instances named **off_board** and **on_board**.
   - **IO Stream: USART** — instance **io_stream** (your debug UART), plus
     the stdio retarget so `printf` works.
   - **NVM3** (nvm3_default).
   - **PWM** — four instances **motor1, motor2, motor3, motor4**.
   - **Simple Button** — three instances **fault_int, local_button,
     ui_int**.
   - **Simple LED** — instance **local_LED**.
   - **Sleeptimer**.
   - **Device Init** (usually pulled in automatically).
   - Component *names may have shifted slightly* between Platform 4.2 and
     5.x — use the search box and match by function; the migration guide
     lists any renames.
3. **Pin Tool:** open the new project's pin configuration and re-enter each
   pin assignment from your old `.pintool` — each PWM output, each I2C
   SDA/SCL for both buses, each button interrupt pin, the LED pin, the UART
   TX/RX. Go slowly and cross off each one against the old file. **This is
   the highest-attention part** — a wrong motor pin here is a wrong motor
   later. Double-check against your schematic, not just the old file.
4. Save. Studio regenerates `autogen/` and `config/` for the new project —
   this is expected and correct (unlike in the old project, where you were
   told never to hand-edit them; here you're *generating* them fresh through
   the tool, which is the right way).

**Note on the old PWM hand-edits (REFACTOR_PLAN P3.17): superseded by
DECISIONS #25.** Do NOT reproduce the old shared-TIMER0 hand-edited PWM
configs. The new project uses one motor per timer (motor1→TIMER0,
motor2→TIMER1, motor3→TIMER2, motor4→TIMER3 — TIMER4 is reserved by the
SoC Empty base), so all PWM config is auto-generated and regeneration-safe.
The exact component/pin/timer map lives in HARDWARE_CONFIG.md, which is
the authoritative checklist for this whole phase. Keep PWM frequency at
24 kHz.

**Gate 4:** every component and pin from the old `.slcp`/`.pintool` is
reproduced in the new project, verified against your schematic. ✅

---

## Phase 5 — The compile-fix loop (Claude Code territory)

Now build the new project with your code in it. Expect a batch of errors —
these are almost all mechanical: a header path that moved, an API renamed
between Platform 4.2 and 5.x, a changed enum name. This is precisely the
kind of grind Claude Code handles well, because it sees the exact compiler
error and the fix is usually one line.

1. Build (hammer). Read the first error, not the hundredth — early errors
   (often a missing/renamed header) cascade into many later ones.
2. Work top-down. Feed Claude Code the error text and the file; apply,
   rebuild, repeat. Common categories to expect:
   - `sl_` API renames (Platform 4.x → 5.x).
   - Header include path changes.
   - NVM3 / sleeptimer / IADC API signature tweaks.
3. Keep changes small and rebuild often — same discipline as your refactor
   phases. Commit each meaningful cluster of fixes.

**Gate 5:** the new project builds with **0 errors** — your full firmware,
on SiSDK 2026.6.0, radio still dormant. ✅

---

## Phase 6 — Regression BEFORE any BLE code (the real safety gate)

Do **not** write a single line of BLE glue until the ported firmware passes
your existing regression on hardware. This isolates "did the SDK move break
anything" from "did adding BLE break anything" — two separate risk sources
you never want tangled.

1. Flash the ported firmware to the bench board.
2. Run the **entire REGRESSION.md** pass — every button, auto-level,
   collision trip + reverse + LED sequence, limit-switch stops, settings
   mode, NVM3 persistence across power cycle. Everything.
3. Any behavior difference from the 4.2.2 baseline is a migration
   regression — chase it down now, while the radio is still off and the
   cause is unambiguous.

**Gate 6:** full regression green on the migrated firmware. ✅

**Only after Gate 6** do you start the actual BLE work (the D1 GATT design
in BLE_GATT_DESIGN.md) — adding the Bed Service, the command/telemetry
characteristics, the dead-man watchdog, and so on. That's a fresh design +
build session, not part of this migration.

---

## If it goes wrong

Nothing here can hurt your working firmware — that's the whole design of the
procedure. At any failed gate:

- The old 4.2.2 project is untouched on its tagged commit and in your cold
  backup. Re-flash `totem_controller.hex` and you're back to known-good.
- A failed **Gate 2** (stock example won't build) is an environment problem
  — reinstall / re-select toolchain; your code isn't involved yet.
- A failed **Gate 5/6** (your code) is isolated to the port; the design and
  logic are proven, so it's a mechanical SDK-difference hunt, not a
  redesign.
- If the SiSDK port genuinely stalls, the recorded contingency
  (DECISIONS #20) is to add BLE on the final GSDK 4.4.x as a temporary
  unblock — but exhaust the SiSDK path first; that's the maintained future.

## One-line summary of the order

Phase 0 backup → 1 install → 2 **prove stock example builds** → 3 copy your
sources (merge app.c) → 4 rebuild components + pins in configurator → 5
compile-fix loop → 6 **full regression before any BLE**.
