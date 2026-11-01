# fw_HANDOFF_2026-11-01_01 — Prompt AA: keep the CPU awake during app holds

Firmware repo session of 2026-11-01 (system date `Sun Nov 1 2026`, from
`date` at the start and again before writing this file). Implements
`docs/prompts/PROMPT_AA_sleep-veto-during-app-holds.md` (reviewer, written
2026-11-01), parts A to D. Two work commits (A docs, B code + docs), each
built from the command line, then this handoff plus the session's
`docs/SESSION_LOG.md` entry in a separate, last commit.

Nothing in this session has been run on hardware. The fix (B1) is
**untested**; see section e.

## a. Header

| Field | Value |
|---|---|
| Repo | `tilt_bed_controller` (firmware) |
| Branch | `main` |
| HEAD before the session | `c945146`, equal to `origin/main` after a `git fetch` (both `c945146cfcc9cd4f9d9067288282e16f8a8e8ba0`, d.1) |
| Work commits | `0993ee9` (A, docs), `75b0a61` (B) |
| HEAD after the work commits | `75b0a61` |
| Handoff commit | the commit that adds this file and the session's SESSION_LOG entry |
| Push | `git push` to `origin/main` after the handoff commit (standing rule); the result is reported to the user in chat, and a failure would be recorded here in a follow-up commit |

`git status` at the end of the work (after both work commits and the
session's SESSION_LOG append, immediately before this file was written):

```
On branch main
Your branch is ahead of 'origin/main' by 2 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

no changes added to commit (use "git add" and/or "git commit -a")
```

`.projectlinkstore` was modified before the session and is never committed.

## b. Plans, designs and analysis

### b.0 What was asked, and what was done

| Part | Asked | Done | Commit |
|---|---|---|---|
| A1 | Record the 2026-11-01 bench as VERIFIED (log + user's report): six WATCHDOG STOP blocks with the reviewer's reading stated as a hypothesis; iPhone connection parameters; iPhone refusal/acceptance; two phones; the Prompt W trip benign | REGRESSION.md §6 recorded result; REGRESSION.md §7 recorded result; DECISIONS #43 "Bench result (2026-11-01, iPhone)"; SESSION_LOG record entry | `0993ee9` |
| A2 | Record the reviewer's rulings on Prompt Y QUESTIONS 1-8 in SESSION_LOG; add the I2CSPM timeout to the pre-ship list | Rulings recorded. **No pre-ship list exists**, so the item is recorded in the SESSION_LOG with the other known pre-ship items, not in a new file (QUESTIONS 1) | `0993ee9` |
| B1 | Sleep veto while the watchdog is armed, via a small accessor | `ble_bed_service_app_hold_active()`; veto in `app_is_ok_to_sleep()` | `75b0a61` |
| B2 | Hold-end summary line behind `BLE_DEBUG_WATCHDOG_LOG`, after the stop, whole-hold G and B; WATCHDOG STOP block unchanged | Done; hold-ending events listed in b.2.2 | `75b0a61` |
| B3 | DECISIONS entry | #45 | `75b0a61` |
| B4 | Fix the `bed_control.c:73` comment | Done, comment only | `75b0a61` |
| B5 | REGRESSION.md item | 6.9, unchecked | `75b0a61` |
| C1-C4 | Report the sleep-clock configuration; change nothing | b.3; nothing changed. One finding conflicts with the EM2 reading (QUESTIONS 2) | — |
| D | Expected lines; physical hold and idle bed unaffected | b.4 | — |

`BED_BENCH_PROVOKE_RUNAWAY` 0 and `BLE_BENCH_WIPE_BONDS` 0, untouched.
`config/`, `autogen/` and the SDK were only read. Nothing added blocks: the
veto is a flag read; the new print runs once per hold, after the stop.

### b.1 Part A — the bench record

**The log** (`C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\autolevel_stall_bench.log`,
183 lines, 7615 bytes, Nov 1 14:56; reproduced in full in d.3) was read
line by line before anything was changed. Every figure in the prompt's
Evidence section is in it:

| Prompt's claim | Log | Confirmed |
|---|---|---|
| Six WATCHDOG STOP blocks at lines 28, 53, 58, 63, 122, 127 | yes, all `op=0x03` | Yes |
| since last refresh 300-301 ms; last write received 301 ms ago, same seq | 300/301/300/300/300/300 ms; "last write received 301 ms ago" in all six, seq equal on both lines (59, 193, 208, 214, 235, 241) | Yes |
| received->processed 0-1 ms; longest pass 2-17 ms | 1,0,0,1,0,0 ms; 17,2,17,2,16,2 ms | Yes |
| passes 23-39; longest between passes 27-28 ms | 39,27,23,28,31,32; 28,28,27,27,28,27 | Yes |
| gaps `180,0,0,0,207,0,148,28` and `20,2,1,1,112` | lines 61 and 66 (also `19,1,0,0,83` line 130, `104,119,90,89,122,56,209,0` line 125) | Yes |
| line 28 follows `ROLL Leveled successfully...` | line 27 | Yes |
| connection parameters 13-16: interval=24 latency=0, timeout=72 then 100 | line 13 `timeout=72 security_mode=0`; 15-16 `timeout=100` | Yes |
| refusal 155-158, 0x1016 | yes; a second identical refusal follows at 163-166 (conn=3) | Yes |
| acceptance 173-180 | yes (`bonded conn=4` at 180) | Yes |

Two precision notes, recorded rather than treated as contradictions:

- **"The steady check after an axis finishes"**: in code the stretch after
  `ROLL Leveled successfully...` is the 1000 ms pause before switching axis
  (`app.c` auto-level block, `auto_level_timer`, `ACCEL_TIMER_DURATION` 1000
  ms), with all motors off. The record calls it "the pause after an axis
  finishes".
- **"Waking about once per connection event"**: 23-39 passes in 300 ms is an
  average of ~8-13 ms per pass, with the longest gap 27-28 ms (about one
  30 ms connection interval). So the loop was waking more often than once
  per connection event, but still clearly sleeping (a free-running loop makes
  hundreds of passes in 300 ms; DECISIONS #29 measured ~630 passes/s).

From the user's report (not in the PuTTY log), recorded as such: the app's
Hold diagnostics (START every 100-103 ms, max gap 102-103 ms, err 0); the
same symptom on the Tilt Van bed and with a Galaxy S23; the iPhone message
"CBATTErrorDomain Code=15 Encryption is insufficient"; two phones connected,
the second could not control the bed; the Prompt W `Tripped on steady!` trip
was benign.

The reviewer's reading (the CPU sleeping makes the link miss packets that
are then retried in a burst) is written everywhere as **a hypothesis**.

Where it was recorded (full text in c.1):

- **REGRESSION.md §6**: a "Recorded result, 2026-11-01" table after 6.8,
  marked "App-driven auto-level FAILS: intermittent watchdog stops".
- **REGRESSION.md §7**: a "Recorded result, 2026-11-01" table mapping the
  iPhone run onto 7.10a-d plus the two-phone report.
- **DECISIONS #43**: "Bench result (2026-11-01, iPhone)" — the refusal closes
  with 0x1016 on the iPhone (the value #43 originally predicted), so the close
  code is phone-dependent (S23: 0x1022).
- **SESSION_LOG**: a record entry (committed in A) with the bench record and
  the rulings.

**A2 rulings** recorded verbatim in substance (SESSION_LOG record entry):
(1) keep B, T and the last-write op/seq; (2) no deliberate-stall step;
(3) no change, stale data blocking completion is sufficient; (4) addressed
by this prompt; (5) the 300 s I2CSPM timeout is a pre-ship item for its own
prompt; (6) fix `bed_control.c:73` in B; (7) keep the Stage C box ticked;
(8) no change now.

On (5): `grep -rn -i "pre-ship\|preship\|before shipping\|ship list"` over
`docs/*.md *.c *.h` (excluding SESSION_LOG and handoffs) finds only
per-item notes (`SWITCH_SAMPLER_DIAG` "pre-ship: 0", `BLE_DEBUG_WATCHDOG_LOG`
"0 before shipping", DECISIONS #41's "Set it to 0 before shipping") — d.1.
There is no list. Creating one would be choosing a new document's home,
which the prompt did not specify, so I did not; the SESSION_LOG entry lists
the four known pre-ship items in one place, and QUESTIONS 1 asks where the
list should live.

### b.2 Part B — design

#### b.2.1 B1: the sleep veto

**What it does.** `app_is_ok_to_sleep()` (`app.c:510-561` after B) gains
one veto, placed after the switch-sampler veto (`app.c:524-534`, the `if` at `:532`):

```c
  if (ble_bed_service_app_hold_active()) {
    return false;
  }
```

with a comment in the style of the neighbouring vetoes (evidence, bound,
DECISIONS #45). The accessor (`ble_bed_service.c:1011-1014`, declared
`ble_bed_service.h:74-79`) is:

```c
bool ble_bed_service_app_hold_active(void)
{
  return move_watchdog_armed;
}
```

It is outside `#if BLE_DEBUG_WATCHDOG_LOG`: the veto is the fix, not a
diagnostic. Built with the flag at 1 and at 0 (d.2).

**How the power manager uses it** (Prompt Y handoff b.4.2, SDK
`sl_power_manager.c:260-268`): `sl_power_manager_sleep()` asks
`app_is_ok_to_sleep()` once on entry; `false` makes it return at once without
entering any sleep mode (not even EM1). The main loop then runs the next pass
immediately — EM0, free-running, as the physical controller does while its
switch sampler is active.

**When `move_watchdog_armed` is set and cleared** (line numbers as of
`75b0a61`; d.1 has the grep):

- **Set** only in `move_watchdog_arm()` (`ble_bed_service.c:479`), at `:515`.
  `move_watchdog_arm()` is `static` and is called only at:
  - `:790` — MOVE_START accepted (a fresh start or an identical/changed
    repeat; both refresh), and
  - `:830` — AUTO_LEVEL_START accepted (the first of a hold or a repeat).
  The switch path never calls it (it is `static` in `ble_bed_service.c`;
  `grep -rn move_watchdog_arm` finds no other file, d.1).
- **Cleared** only in `move_watchdog_cancel()` (`:522`), at `:535`. Callers:

  | Line | Context | Ends the hold? |
  |---|---|---|
  | `:582` | `service_move_watchdog()`: collision | yes |
  | `:615` | `service_move_watchdog()`: **watchdog expiry** | yes (WATCHDOG STOP) |
  | `:666` | STOP_ALL | yes |
  | `:778` | MOVE_START refused by `bed_move_start()` (wall switch holds the bed) | yes |
  | `:799` | MOVE_STOP | yes |
  | `:817` | AUTO_LEVEL_START when `app_auto_level_active` is false — cancels whatever was armed (e.g. a move hold), then `:830` re-arms if `bed_auto_level_start()` succeeds | ends a previous hold; a new one starts |
  | `:838` | AUTO_LEVEL_STOP | yes |
  | `:1022` | control-phone disconnect stop (`disconnect_stop_pending`) | yes |

- **Not cancelled, by design (#35):** the bed going idle under an app move
  (`service_move_watchdog()`, `app_bed_is_idle() && app_move_active`) leaves
  the watchdog armed; the next repeat refreshes it or it expires.

**The veto covers the whole hold.** A hold starts with the first accepted
START (`:790`/`:830` sets the flag) and every accepted repeat keeps it set.
Through Step 1, the pause between axes, the leveling moves, and after
`Exit auto-level` while the button is still held (the app keeps repeating),
`move_watchdog_armed` stays true.

**It ends with the hold.** Every way a hold ends goes through
`move_watchdog_cancel()` (the table above). The bound for a phone that just
stops sending: the 300 ms one-shot (`MOVE_WATCHDOG_MS`, `:188`) is
restarted only by `move_watchdog_arm()`; when it fires, its ISR sets
`move_watchdog_expired`, and the next pass's `service_move_watchdog()` (the
CPU is awake, because of the veto) cancels it at `:615`. So the veto ends at
most ~300 ms plus one pass after the last accepted write. Two edge cases,
both still bounded by that timer:

- The control phone disconnects while the bed's active source is not APP
  (e.g. the wall switch took the bed): `disconnect_stop_pending` is only set
  when `bed_active_source() == BED_SOURCE_APP` (`:923-925`), so the cancel at
  `:1022` does not run, but the timer still expires within 300 ms.
- Refused repeats (two-source inhibit, `:746`) do not refresh, so the timer
  expires within 300 ms of the last *accepted* write (QUESTIONS 5).

One theoretical gap: `sl_sleeptimer_restart_timer()`'s return value is not
checked (`:517-519`, unchanged code). If it ever failed, the flag would be
set with no timer running and the CPU would stay awake until the next stop.
The call takes valid constant arguments and a static handle, so failure is
not expected. ASSERTED.

#### b.2.2 B2: the hold-end line

**Hold-ending events** in this code, from the table above: MOVE_STOP,
AUTO_LEVEL_STOP, STOP_ALL, the control-disconnect stop, a collision, a
MOVE_START force-stopped by the wall switch, and an AUTO_LEVEL_START that
replaces a move hold. Each goes through `move_watchdog_cancel()` with the
watchdog armed. The watchdog expiry also goes through it, but prints its own
block instead (below).

**Design** (all behind `BLE_DEBUG_WATCHDOG_LOG`):

- New whole-hold accumulators (`ble_bed_service.c:269-272`):
  `wd_diag_hold_refreshes`, `wd_diag_hold_longest_gap_ms`,
  `wd_diag_hold_longest_between_ticks`. Reset on a **fresh** arm (the
  `else` branch of `move_watchdog_arm()`, where the ring buffer was already
  being reset); on each **refresh** the count is incremented and the gap
  (the same value pushed into the ring buffer) maxed in.
- `ble_bed_service_debug_pass_begin()` now also maxes the pass-to-pass gap
  into `wd_diag_hold_longest_between_ticks` (one compare). The existing
  since-last-refresh figure is unchanged.
- `move_watchdog_cancel()` (`:522-539`): **if the watchdog was armed**,
  latch `wd_diag_hold_end_pending` and snapshot op, seq (`last_move_seq`),
  refreshes, longest gap and longest between-passes. A cancel when not armed
  latches nothing.
- The expiry branch clears the latch (`:622`, first line inside its existing
  `#if` block): a hold that ends in a watchdog stop gets the WATCHDOG STOP
  block, not a hold-end line. The block itself is byte-for-byte unchanged
  (c.2).
- `ble_bed_service_tick()` prints the latched line (`:1037-1047`), right
  after `process_pending_command()`, `service_move_watchdog()` and
  `service_app_commanding()` and the disconnect-stop block before them. Every
  cancel that can end a hold happens in one of those, and in each the stop
  itself (`app_stop_all()`, `bed_move_stop()` + `app_apply_stop_outputs()`,
  `bed_auto_level_stop()` + outputs) runs in the same call, so the print is
  **after** the stop. It is placed before the `status_subscriber_count == 0`
  early return, so it prints whether or not a phone is subscribed.

Format (exact):

```
BLE: hold end op=0x%02x seq=%u: %u refreshes, longest refresh gap %u ms, longest between passes %lu ms
```

- `op`: the hold's opcode (0x01 MOVE_START, 0x03 AUTO_LEVEL_START).
- `seq`: the seq of the hold's last accepted START.
- `refreshes`: accepted repeats after the first START (so accepted STARTs =
  refreshes + 1). Clamped at 65535.
- `longest refresh gap`: G over the whole hold, ms, clamped at 65535.
- `longest between passes`: B over the whole hold, ms. It covers from the
  end of the pass that armed the hold to the cancel.

One line per hold; never per pass. Cost: ~110 characters of blocking UART,
~10 ms, once per hold, after the stop. If two holds ended in one tick (only
possible as "AUTO_LEVEL_START replaces a move hold", which ends one), only
one line could be pending; the replaced hold's line prints and the new hold
has not ended. Nothing is lost there.

#### b.2.3 B3: DECISIONS #45 — the effect on #29 (from code)

Full text in c.2. The part the prompt asked for specifically: app-driven
auto-level now runs at the physical path's pass rate in the stretches where
it used to sleep (Step 1, the 1000 ms axis pause, post-completion hold).
`NUM_ACCEL_READINGS` (130 passes) and `accel_alpha` (0.1 per pass) therefore
change their time spans there: from ~1.0-1.7 s of passes at the bench's
~77-130 passes/s to ~206 ms at #29's measured ~630 passes/s. The LPF time
constant shrinks in the same ratio. The accelerometer is still read at most
every 10 ms, so a free-running average holds each sample several times — the
same as on the physical path. With a motor on, nothing changes. Timer-based
stretches (1500 ms steady read, 1000 ms axis pause) are unchanged in length.
Net: the app path's steady checks now match the hardware-verified physical
path. ASSERTED; the 630/s figure is #29's, measured 2026-07-17 under
different conditions.

#### b.2.4 B4, B5

- `bed_control.c:66-77`: the comment now says a force-stop from the other
  source "becomes a full stop of movement, app_stop_movement(), via
  ARBITRATE_FORCE_STOP (since DECISIONS.md #36; it leaves the under-bed
  lighting alone)". Confirmed against the code: all five
  `ARBITRATE_FORCE_STOP` branches call `app_stop_movement()` (`:104-106,
  117-118, 132-134, 145-146, 158-159`, d.1). Comment only. The host unit test
  that links `bed_control.c` still passes (30 PASS, 0 failed, d.2).
- REGRESSION.md 6.9 (after 6.8): "App AUTO LEVEL (DECISIONS #45): 10 holds
  from off-level, no `BLE: WATCHDOG STOP` / no 'Connection stalled' in the
  app", plus what the hold-end line should show. Unchecked.

### b.3 Part C — sleep-clock and EM2 configuration (report only, nothing changed)

Every claim is marked **VERIFIED** (cited; excerpts in d.1) or **ASSUMED**.

#### C1. Low-frequency clock

- **LF clock source = LFRCO.** `SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE` =
  `..._LFRCO` (`config/sl_clock_manager_tree_config.h:78`); the EM2/3 group
  clock and the RTCC clock both take the default LF source (`:181`
  `SL_CLOCK_MANAGER_EM23GRPACLK_SOURCE`, `:203`
  `SL_CLOCK_MANAGER_RTCCCLK_SOURCE`). **VERIFIED.**
- **LFXO disabled**: `SL_CLOCK_MANAGER_LFXO_EN 0`
  (`config/sl_clock_manager_oscillator_config.h:132`). **VERIFIED.**
- **LFRCO precision mode = default**, not high:
  `SL_CLOCK_MANAGER_LFRCO_PRECISION cmuPrecisionDefault`
  (`config/sl_clock_manager_oscillator_config.h:297`). The setting's own doc
  (`:283-293`): high precision "uses hardware to automatically re-calibrate
  the LFRCO against a crystal driven by the HFXO". **VERIFIED.**
- **This part supports high precision**: `LFRCO_PRECISION_MODE 1`
  (`bgm220sc22hna.h:194`), which defines `PLFRCO_PRESENT` (`em_cmu.h:153-155`),
  and `LFRCO_CFG_HIGHPRECEN` exists (`bgm22_lfrco.h:269`). **VERIFIED.**
- **Sleeptimer peripheral**: `SL_SLEEPTIMER_PERIPHERAL_DEFAULT`
  (`config/sl_sleeptimer_config.h:56`), which resolves to RTCC when present
  (`sli_sleeptimer.h:48-51`); this part has one (`bgm220sc22hna.h:419-420`).
  So the sleeptimer — and the Bluetooth controller's sleep clock, below —
  run from RTCCCLK = LFRCO. **VERIFIED.**
- **Project config = SDK module default**: the project's oscillator and tree
  configs differ from the SDK's `hardware/module/config/BGM220SC22HNA/`
  copies by one trailing blank line each (`diff --strip-trailing-cr`, d.1).
  **VERIFIED.**
- **32.768 kHz crystal on the BGM220S module: not stated** in the project or
  the SDK files I read. `LFXO_PRESENT` in `bgm220sc22hna.h:407` means the
  LFXO *peripheral* exists, not that a crystal is fitted.
  `docs/HARDWARE_CONFIG.md` mentions no LFXO or 32 kHz crystal (`grep`
  empty, d.1). Indirect evidence only: the SDK's module defaults choose
  LFRCO with LFXO disabled for BGM220**S**C22HNA, but LFXO enabled and
  selected for BGM220**P**C22HNA (d.1). That suggests the S module has no
  LFXO crystal and the P module has one — **ASSUMED**; the module datasheet
  would settle it (not read).

#### C2. The sleep-clock accuracy the link layer is given

- **There is no SCA setting in `config/` or `autogen/`** (`grep -rn -i
  "accuracy\|sleep.clock\|SCA\b" config/ autogen/` prints nothing, d.1).
  **VERIFIED.**
- **It is computed at runtime** in `sl_btctrl_init_functional()`
  (`bluetooth_le_controller/src/sl_btctrl_init.c:289-290`):
  `config->sleepClockAccuracy = sl_sleeptimer_get_clock_accuracy();` ->
  `sleeptimer_hal_get_clock_accuracy()` for RTCC
  (`sl_sleeptimer_hal_rtcc.c:281-286`) ->
  `sl_clock_manager_get_clock_branch_precision(SL_CLOCK_BRANCH_RTCCCLK)` ->
  `CMU_LF_ClockPrecisionGet(cmuClock_RTCCCLK)`
  (`sl_clock_manager_hal_s2.c:443-447`) -> for LFRCO
  (`emlib/src/em_cmu.c:2618-2645`): **500 ppm if `LFRCO->CFG.HIGHPRECEN` is
  set, otherwise 0xFFFF**. With `cmuPrecisionDefault` the bit is not set, so
  **the controller is given 0xFFFF (65535 ppm)**. **VERIFIED** as code; the
  runtime register was not read (ASSUMED that nothing else sets HIGHPRECEN).
- **SDK's documented meaning**: `SL_BTCTRL_SLEEP_CLOCK_ACCURACY_LIMIT 500`,
  "Highest limit of the (SCA) Sleep Clock Accuracy in ppm"
  (`sl_btctrl_linklayer_defs.h:58-61`). When the accuracy exceeds it, and the
  flag `SL_BTCTRL_CONFIG_FLAG_USE_INACCURATE_SLEEP_CLOCK` (0x80000, `:91`) is
  not set, **the controller adds an EM1 requirement**
  (`sl_btctrl_init.c:291-296`: `sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1)`),
  i.e. the power manager may not go below EM1. **VERIFIED** as code.
- **Is the flag set?** No visible source sets it: the only references are its
  definition and the two checks (`grep`, d.1). The controller's default
  configuration comes from `sl_btctrl_init_config()`, declared in
  `sl_btctrl_linklayer.h:395` and implemented in the closed controller
  library, called by `sl_btctrl_init()` (`sl_btctrl_init.c:677-685`).
  **ASSUMED not set** — not visible.

#### C3. EM2 / HFXO wake-up settings relevant to radio timing

- `power_manager_deepsleep_blocking_hfxo_restore` component in the project
  (`tilt_bed_controller.slcp:479`): on leaving EM2 the power manager waits,
  in EM1, until the HFXO is ready before returning to the application
  (`sl_power_manager.c:354-366`). **VERIFIED.**
- `SL_POWER_MANAGER_CONFIG_VOLTAGE_SCALING_FAST_WAKEUP 0`
  (`config/sl_power_manager_config.h:52`); its doc: enabling it "decreases
  wakeup time by about 30 us" (`:48`). **VERIFIED.**
- `SL_POWER_MANAGER_INIT_EMU_EM2_DEBUG_ENABLE 1` (`:93`): keeps debug
  connectivity in EM2 at higher EM2 current (`:87-89`). **VERIFIED.**
- `SL_RAIL_UTIL_RAIL_POWER_MANAGER_INIT 1`
  (`config/sl_rail_util_power_manager_init_config.h:40`). **VERIFIED.**
- With the power manager present the controller sets
  `SL_BTCTRL_CONFIG_FLAG_SYNCHRONIZE_TO_SLEEP_CLOCK`, commented "Configure
  RAIL to use timersync in sleep handling for Bluetooth"
  (`sl_btctrl_init.c:284-287`). **VERIFIED** as code; what the closed RAIL
  does with it is **ASSUMED** (it keeps the radio timer in step with the
  sleep clock across sleep).
- HFXO: crystal mode, 38.4 MHz, CTUNE 140 with the manufacturing CTUNE token
  enabled, precision 50 ppm (`config/sl_clock_manager_oscillator_config.h:72,
  78, 84, 105, 98`). **VERIFIED.**

#### C4. Is there a configuration mismatch that would make the link layer miss packets in EM2?

**What the cited code shows (VERIFIED):** the bed runs its sleep clock from
the LFRCO in default precision; the SDK reports that clock's accuracy as
0xFFFF ppm, far above the controller's 500 ppm limit; and the controller's
visible response to that is to forbid EM2 by holding an EM1 requirement.

**What follows, ASSUMED** (depends on closed code, or not observed):

1. **The bed probably never entered EM2** on the 2026-11-01 bench. Unless the
   closed library sets the "inaccurate sleep clock" flag, the CPU slept in
   **EM1** (WFI, HFXO running) between passes. If so, the reviewer's reading
   cannot be literally "missed while in EM2", although it can still be "missed
   while the CPU sleeps". QUESTIONS 2.
2. So there is no "EM2 with a bad sleep clock" mismatch in the visible
   configuration. The SDK's guard appears to prevent exactly that case.
3. **But the link layer is told its sleep clock is 65535 ppm.** How the
   closed controller uses an out-of-range value (clamp to its 500 ppm limit?
   window widening from the raw value?) is not visible. The Bluetooth
   specification's SCA field only goes up to the 251-500 ppm class (general
   knowledge, not cited here). Whether the link-layer timing in EM1 still
   uses the LFRCO-clocked RTCC via timersync (`SYNCHRONIZE_TO_SLEEP_CLOCK`),
   and how much the uncalibrated LFRCO drifts, are not visible either. A
   timing error there could make the bed's receive window miss the phone's
   packets, with the phone retrying until one gets through — matching the
   bunched gaps. **A hypothesis**, consistent with the bench, not shown.
4. **The stall is below the application.** During each 300 ms gap the loop
   made 23-39 passes, and each pass runs `sl_main_process_action()`, which
   dispatches pending Bluetooth events. So a write sitting in the host's
   event queue for 300 ms is unlikely; the delay is at or below the stack's
   delivery of the write. **ASSUMED** (that each `sl_bt_step()` drains
   pending events).
5. **B1 cannot tell these apart.** Keeping the CPU in EM0 removes CPU sleep
   from the picture whatever the mechanism. If B1 removes the stalls, the
   mechanism is "something about the CPU sleeping", not yet which.

Two checks that would narrow it (QUESTIONS 3, 4; neither done — one is a code
change not asked for, the other a configurator change, both the reviewer's
call): (a) a one-line boot print of `sl_sleeptimer_get_clock_accuracy()` to
confirm 0xFFFF on the hardware; (b) set LFRCO precision to High in the
Studio configurator (SCA becomes 500, the EM1 hold lifts, the LFRCO is
recalibrated against the HFXO) and re-run the bench **without** B1.

### b.4 Part D — the reviewer's bench steps, expected lines

Firmware `75b0a61`, `BLE_DEBUG_WATCHDOG_LOG` 1, `BLE_DEBUG_SECURITY_LOG` 1,
`SWITCH_SAMPLER_DIAG` 1. **All expected, not observed.**

#### D1. A normal app auto-level hold, from off-level, held to completion then released

```
Step 1: ROLL and PITCH check                    <- exactly once per hold
Step 2: ROLL Not Level
Leveling...
Max Current for Motor 0: ...                     <- x4, as before
Max Current for Motor 1: ...
Max Current for Motor 2: ...
Max Current for Motor 3: ...
Latch
Pulse off...
ROLL Leveled successfully...
Axis set to PITCH                                <- after the 1000 ms pause; no WATCHDOG STOP before it
Leveling...
...
PITCH Leveled successfully...
Exit auto-level
BLE: hold end op=0x03 seq=N: R refreshes, longest refresh gap G ms, longest between passes B ms   <- on release
```

- **No `BLE: WATCHDOG STOP`** anywhere, and no "Connection stalled" in the app.
- **One `BLE: hold end` line per hold**, printed when the app's
  AUTO_LEVEL_STOP is processed (release). Released part-way, it follows
  whatever was printed last.
- **B ~0-1 ms**: the loop free-runs for the whole hold. Before B1 the bench
  showed 27-28 ms within a single 300 ms window, so a whole-hold B of tens of
  ms means the veto is not taking effect. B also covers the stack's event
  processing between passes; a few ms would not by itself mean the CPU slept.
- **G ~100-130 ms** if writes now arrive on the app's 100 ms cadence (30 ms
  connection events add up to ~30 ms of jitter). Gaps of 150-210 ms like
  those before the stops would mean writes are still being delayed, even if
  none reached 300 ms.
- **R ≈ (hold duration in ms / 100) - 1**; `seq` is the last START's seq (the
  app increments seq per write, wrapping at 255).

Reading the result against the hypothesis:

| Bench result | Meaning |
|---|---|
| No WATCHDOG STOP in 10 holds, B ~0-1, G ~100-130 | consistent with "CPU sleep causes the late writes"; fix works |
| WATCHDOG STOP blocks still appear, with `longest between passes` ~0-1 ms | the hypothesis is wrong: stalls are not caused by CPU sleep; look at the link/phone (C4 item 3) |
| B tens of ms | the veto is not in effect (or something else blocks between passes) — report |

An app MOVE hold (e.g. hold UP HEAD in the app, release) prints `BLE: hold
end op=0x01 ...` on release. STOP_ALL, a disconnect while holding, or a
collision during a hold also print one hold-end line, after the stop.

#### D2. A physical-controller hold is unaffected

The physical path never arms the dead-man watchdog: `move_watchdog_arm()` is
`static` in `ble_bed_service.c` and is called only from the Command-write
handler for accepted MOVE_START / AUTO_LEVEL_START (`:790`, `:830`; d.1).
So during a physical AUTO LEVEL or motor hold, `ble_bed_service_app_hold_active()`
is false and the new veto does nothing. The CPU is kept awake there, as
before, by the switch-sampler veto (and the motor vetoes once a motor runs).
**No `BLE: hold end` line** is printed for a physical hold; the PuTTY output
is as before.

#### D3. An idle connected bed still sleeps

With the app connected and no button held in it, no MOVE_START /
AUTO_LEVEL_START is being accepted, so `move_watchdog_armed` is false (it was
cleared by the last hold's stop, or 300 ms after its last write).
`app_is_ok_to_sleep()` then reaches `return true` (`app.c:560`) exactly as
before B1, as long as no motor flag, no sampler session, no motor-enable
shadow and no tilt burst is active. The tilt burst is the same as before: T1
on subscribe and T3 after a move each keep the CPU awake for 500 ms. LIGHTS
and REFRESH_TILT never arm the watchdog. **The CPU still sleeps when the app
is connected but no button is held.** The depth (EM1 or EM2) is set by the
power manager's requirements, which this change does not touch — per C2,
probably EM1.

## c. Diffs (complete)

`git show` of each committed commit, verbatim. The source diff (c.2) is
complete. The docs diff c.1 omits only `docs/SESSION_LOG.md` (it ships
alongside this file; `git show 0993ee9 -- . ':(exclude)docs/SESSION_LOG.md'`);
it is under 400 lines and not otherwise truncated. This handoff's own commit
is not reproduced.

### c.1 Commit A `0993ee9` (docs only; SESSION_LOG.md omitted)

```diff
commit 0993ee9db33a7d409ea04722eeefabbfef986587
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Sun Nov 1 15:37:11 2026 -0800

    Docs: 2026-11-01 auto-level stall bench record, Prompt Y rulings
    
    - REGRESSION.md §6: 2026-11-01 recorded result - six WATCHDOG STOP blocks
      during app auto-level with no motor on, CPU sleeping between passes,
      bunched writes; the reviewer's reading recorded as a hypothesis.
    - REGRESSION.md §7 and DECISIONS #43: iPhone pairing refusal closed with
      0x1016 (CBATTErrorDomain Code=15 on the phone), acceptance in settings
      mode; two-phone viewer behaviour (user's report).
    - SESSION_LOG: the bench record and the reviewer's rulings on Prompt Y's
      QUESTIONS 1-8; Prompt Y QUESTIONS 9 closed (benign trip).
    - docs/prompts: Prompt AA.
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 4def63f..479c6c1 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2033,6 +2033,31 @@ Prompt V (#42). Every line number below was checked against the log.
 
 The settings-hold results from the same run are recorded under #44.
 
+### Bench result (2026-11-01, iPhone)
+
+Recorded 2026-11-01. The user's bench run on the desk bed, firmware
+`c945146` (the code of `c7d743d`), `BLE_DEBUG_SECURITY_LOG` 1, phone an
+**iPhone** running the app's TestFlight build `e22e4b9`. Log:
+`_03-Software/PuTTY Logs/autolevel_stall_bench.log` (183 lines). Line numbers
+below were checked against the log. **VERIFIED (user's bench run)**, except
+where marked as the user's report.
+
+- **Refusal, iPhone** (lines 151-158, bed not in settings mode):
+  `confirm_bonding conn=2 handle=0x02 (REPLACING existing bond) -> REJECTED
+  (not in settings mode)`, `bonding_failed conn=2 reason=0x1205`, `BLE:
+  closing conn=2 - pairing refused`, `connection_closed conn=2
+  reason=0x1016` - `SL_STATUS_BT_CTRL_CONNECTION_TERMINATED_BY_LOCAL_HOST`
+  (`sl_status.h:274`), the value #43 predicted. The phone retried once and
+  got the same sequence (lines 159-166, conn=3, again 0x1016). So the close
+  code depends on the phone: 0x1022 on the Galaxy S23 (2026-10-18), 0x1016 on
+  the iPhone. The iPhone showed "CBATTErrorDomain Code=15 Encryption is
+  insufficient" (user's report; not in the PuTTY log).
+- **Acceptance in settings mode, iPhone** (lines 167-180): BED LIGHTS held
+  (167-172; entry prints nothing), `connection_opened conn=4 bonding=0x02`,
+  `confirm_bonding conn=4 handle=0x02 (REPLACING existing bond) -> ACCEPTED
+  (settings mode)`, `security_mode=1`, `cccd_seed`, `bonded conn=4
+  bonding=0x02 security_mode=1`. `Settings mode: exit` at line 182.
+
 ## 44. STOP in settings mode cancels any hold in progress and stays in settings mode
 
 Date: 2026-10-18 (decision and implementation), recorded 2026-10-23. User
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index 8ad21b6..1ba46ad 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -211,6 +211,31 @@ pitch, all actuators clear of limits.
       (DECISIONS.md #26) only manifested on a sustained multi-second move,
       so a quick/near-level test is not sufficient coverage for this check
 
+**Recorded result, 2026-11-01 (firmware `c945146`, `BLE_DEBUG_WATCHDOG_LOG`
+1, desk bed, iPhone with app TestFlight build `e22e4b9`, log
+`autolevel_stall_bench.log`).** VERIFIED (user's bench run). **App-driven
+auto-level FAILS: intermittent watchdog stops** (the app's "Connection
+stalled - movement stopped").
+
+| Check | Result |
+|---|---|
+| Connection parameters (iPhone) | `interval=24 latency=0 timeout=72` on connect, then `timeout=100` after the bed's request (lines 13, 15-16; same at 152/154, 160/162, 174/176) |
+| WATCHDOG STOP blocks | six: lines 28, 53, 58, 63, 122, 127, all `op=0x03` (AUTO_LEVEL_START) |
+| Since last refresh / last write received | 300-301 ms / 301 ms, with the **same seq** as the last refresh in all six: nothing reached the application for 300 ms |
+| Received->processed, longest pass | 0-1 ms; 2-17 ms: the bed was not busy |
+| Passes, longest between passes | 23-39 passes in the 300 ms, longest gap between passes 27-28 ms: the CPU was sleeping between passes (a free-running loop makes hundreds of passes) |
+| Refresh gaps before the stop | bunched, e.g. `180,0,0,0,207,0,148,28` (line 61), `20,2,1,1,112` (line 66), `19,1,0,0,83` (line 130): writes arriving late, then several in the same millisecond |
+| Where the stops fell | every one in a stretch of an app auto-level hold with no motor on: Step 1 (lines 52-53, 57-58, 62-63, 121-122, 126-127) and the pause after an axis finishes (line 28 follows `ROLL Leveled successfully...`, line 27). None while a motor ran, none on the physical controller |
+| App side | the app's Hold diagnostics for the same holds show a START every 100-103 ms (max gap 102-103 ms, err 0) (user's report) |
+| Elsewhere | the same symptom on a second bed (Tilt Van) and with a Galaxy S23 (user's report) |
+| Completed runs | auto-level still completed on later attempts (e.g. lines 67-85, 94-112, 131-149) |
+
+The reviewer's reading, **a hypothesis, not a finding**: while the CPU
+sleeps the link sometimes misses the phone's packets, which are then retried
+and delivered in a burst (a sleep-clock accuracy or EM2 wake-up timing issue
+being a candidate). Tested by keeping the CPU awake during app holds
+(Prompt AA commit B).
+
 ## 7. Settings mode
 
 - [ ] ⚡ 7.1 Hold BED_LIGHTS 5 s: amber LED fast-blinks — settings mode active
@@ -293,6 +318,19 @@ ran that those items now cover.
 | 7.10d | not in this log (Prompt V row above) |
 | 7.11 (UP ALL only) | two UP ALL holds with no NVM3 write (STOP pressed in the app: user's report; STOP prints nothing); the next full hold wrote 350/250; DOWN ALL then wrote 550/275 |
 
+**Recorded result, 2026-11-01 (firmware `c945146`, desk bed, iPhone with
+app TestFlight build `e22e4b9`, log `autolevel_stall_bench.log`; full figures
+in DECISIONS #43 "Bench result (2026-11-01, iPhone)").** VERIFIED (user's
+bench run), except where marked as the user's report.
+
+| Item | Result |
+|---|---|
+| 7.10a | `connection_opened conn=1 bonding=0x02`, `security_mode=1`, no `confirm_bonding` (lines 12-17) |
+| 7.10b | twice (lines 151-158, 159-166): `-> REJECTED (not in settings mode)`, `bonding_failed reason=0x1205`, `closing conn=N - pairing refused`, one `connection_closed reason=0x1016` each; iPhone showed "CBATTErrorDomain Code=15 Encryption is insufficient" (user's report) |
+| 7.10c | `-> ACCEPTED (settings mode)`, then `bonded conn=4` (lines 173-180) |
+| 7.10d | not in this log |
+| Two phones | a second phone connected but could not control the bed (user's report; DECISIONS #34's viewer role - not in this log) |
+
 ## 8. Bed lights
 
 - [ ] ⚡ Tap BED_LIGHTS: under-bed lighting on, amber LED on
diff --git a/docs/prompts/PROMPT_AA_sleep-veto-during-app-holds.md b/docs/prompts/PROMPT_AA_sleep-veto-during-app-holds.md
new file mode 100644
index 0000000..5d7d8f7
--- /dev/null
+++ b/docs/prompts/PROMPT_AA_sleep-veto-during-app-holds.md
@@ -0,0 +1,136 @@
+# Prompt AA - keep the CPU awake during app holds (watchdog-stall fix)
+
+Written by the reviewer on 2026-11-01. Implement as specified. Where the code
+contradicts this document, stop and report it under QUESTIONS FOR REVIEW
+instead of improvising. Follow CLAUDE.md for the handoff file, the session
+log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
+BLE_BENCH_WIPE_BONDS stay 0. Nothing added may block (DECISIONS #27).
+config/ and the SDK are not hand-edited.
+
+Expected repo state: HEAD c945146 (Prompt Y handoff), level with origin/main.
+Uncommitted: .projectlinkstore (never commit it). This prompt file is
+untracked; commit it with commit A. If HEAD or origin/main differ, stop and
+report before changing anything.
+
+Make TWO commits, A and B, building from the command line after each (the
+user permits it). Report the raw build tail and any warnings.
+
+======================================================================
+Evidence (user's bench, 2026-11-01, desk bed, iPhone TestFlight build
+e22e4b9, firmware c945146)
+======================================================================
+Log: C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\autolevel_stall_bench.log
+Read it before starting. Six WATCHDOG STOP blocks (lines 28, 53, 58, 63,
+122, 127). In every one:
+- since last refresh 300-301 ms, last write received 301 ms ago with the
+  SAME seq as the last refresh: nothing reached the application for 300 ms;
+- received->processed 0-1 ms, longest pass 2-17 ms: the bed was not busy;
+- passes 23-39, longest between passes 27-28 ms: the loop was waking about
+  once per connection event (interval=24, i.e. 30 ms) - the CPU was sleeping
+  between passes;
+- refresh gaps show bunching, e.g. "180,0,0,0,207,0,148,28" and
+  "20,2,1,1,112": writes arrive late, then several in the same millisecond.
+The app's Hold diagnostics for the same holds show the phone issuing a
+START every 100-103 ms (max gap 102-103 ms, err 0).
+
+Every stall falls where an app hold is active and the CPU is allowed to
+sleep: app auto-level Step 1, and the steady check after an axis finishes
+(line 28 follows "ROLL Leveled successfully..."). No stall happened while a
+motor ran (motor_on vetoes sleep) and none on the physical controller (the
+switch sampler vetoes sleep). App MOVE holds never stall (motor running).
+The user also saw the same symptom on a second bed (Tilt Van) and on a
+Galaxy S23.
+
+Reviewer's reading (ASSUMED, not proven): while the CPU is in EM2 the link
+sometimes misses the phone's packets, which are then retried and delivered
+in a burst. A sleep-clock accuracy or EM2 wake-up timing issue is a
+candidate. Commit B tests the reading by removing EM2 during app holds.
+
+======================================================================
+A. Docs (docs only)
+======================================================================
+A1. SESSION_LOG / REGRESSION / DECISIONS #43 area as appropriate: record
+    the 2026-11-01 bench as VERIFIED (user's bench run), from the log and the
+    user's report:
+    - the six WATCHDOG STOP blocks and the reading above (state the reading
+      as the reviewer's hypothesis, not a finding);
+    - connection parameters from the iPhone: interval=24 latency=0, initial
+      timeout=72 then 100 after the bed's request (log lines 13-16);
+    - iPhone pairing refusal (lines 155-158): REJECTED (not in settings
+      mode), bonding_failed 0x1205, the bed closed the link with reason
+      0x1016; the iPhone showed "CBATTErrorDomain Code=15 Encryption is
+      insufficient"; acceptance in settings mode at lines 173-180;
+    - two phones connected, the second could not control the bed (user's
+      report; DECISIONS #34 behaviour);
+    - the user reports the "Tripped on steady!" collision trip in the
+      Prompt W log was benign (closes Prompt Y QUESTIONS 9).
+A2. Record the reviewer's rulings on Prompt Y's QUESTIONS in the
+    SESSION_LOG: (1) keep fields B and T and the last-write op/seq;
+    (2) no deliberate-stall bench step needed; (3) no change - stale data
+    blocking completion is sufficient; (4) addressed by this prompt;
+    (5) the 300 s I2CSPM timeout is a pre-ship item for its own prompt -
+    add it to the pre-ship list wherever that list lives; (6) stale comment
+    at bed_control.c:73 - fix it in commit B (comment only); (7) keep the
+    Stage C box ticked; (8) no change now.
+
+======================================================================
+B. Sleep veto while an app hold is active
+======================================================================
+B1. In app_is_ok_to_sleep() (app.c), add a veto: return false while the
+    app's dead-man watchdog is armed (move_watchdog_armed in
+    ble_bed_service.c). Expose it through a small accessor in
+    ble_bed_service.h, e.g. bool ble_bed_service_app_hold_active(void).
+    Use the watchdog-armed state, not app_move_active/app_auto_level_active:
+    it is bounded by MOVE_WATCHDOG_MS after the last write, so a lost phone
+    can never pin the CPU awake. Comment it in the same style as the other
+    vetoes, citing this prompt's evidence and the new DECISIONS entry. State
+    in the handoff when move_watchdog_armed is set and cleared, with line
+    numbers, so the reviewer can confirm the veto covers the whole hold and
+    ends with it.
+B2. Hold-end summary print, behind BLE_DEBUG_WATCHDOG_LOG (bench only, so
+    the fix can be confirmed without a stall): when an app hold ends
+    WITHOUT a watchdog stop (AUTO_LEVEL_STOP, MOVE_STOP, STOP_ALL, or
+    whatever ends the hold in this code - list them), print one line AFTER
+    the stop has been carried out, e.g.
+      "BLE: hold end op=0x03 seq=N: R refreshes, longest refresh gap G ms,
+       longest between passes B ms"
+    where G and B cover the whole hold (not just since the last refresh).
+    One line per hold, never per pass. Expected after B1: B about 0-1 ms
+    during the whole hold. Keep the WATCHDOG STOP block unchanged.
+B3. Add DECISIONS entry (next free number): during an app hold the CPU stays
+    in EM0, for the same reason motors and the switch sampler veto sleep;
+    the evidence; the bound (MOVE_WATCHDOG_MS); that this also closes Prompt
+    Y QUESTIONS 4 (app-driven Step 1 now runs the loop like the physical
+    controller does, so DECISIONS #29's pass-based averaging sees the same
+    pass rate) - state any effect on #29 you can see in the code.
+B4. Fix the stale comment at bed_control.c:73 (force-stops call
+    app_stop_movement(), not app_stop_all(), since #36). Comment only.
+B5. REGRESSION.md: add an unchecked item "app AUTO LEVEL: 10 holds from
+    off-level, no WATCHDOG STOP / no 'Connection stalled'".
+
+======================================================================
+C. Report only - change nothing
+======================================================================
+C1. Low-frequency clock used in EM2 by the radio/sleeptimer: which source
+    (LFXO or LFRCO; precision mode if LFRCO), from config/ and autogen/
+    with file:line. Whether the BGM220 module variant in the .slcp has a
+    32.768 kHz crystal, if the project or SDK states it (say if not
+    found).
+C2. The Bluetooth stack's sleep-clock accuracy setting (the ppm value the
+    link layer assumes), from config/ (e.g. sl_bluetooth_config.h), with
+    file:line, and the SDK's documented meaning.
+C3. Any EM2 / HFXO wake-up settings relevant to radio timing, with
+    file:line.
+C4. From C1-C3 and the SDK documentation you can cite: is there a
+    configuration mismatch that would make the link layer miss packets in
+    EM2? Mark every claim VERIFIED (cited) or ASSUMED. Do not change any
+    configuration.
+
+======================================================================
+D. For the reviewer's bench steps
+======================================================================
+In the handoff, give the exact lines expected for a normal app auto-level
+hold with B1 and B2 in place (including the new hold-end line), and confirm
+that a physical-controller hold and an idle connected bed are unaffected by
+B1 (state from code whether the CPU still sleeps when the app is connected
+but no button is held).
```

### c.2 Commit B `75b0a61` (source and docs)

```diff
commit 75b0a61333a713fc74453bda1072a836fca867ab
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Sun Nov 1 15:39:19 2026 -0800

    Keep the CPU in EM0 during app holds; hold-end diagnostic line (DECISIONS #45)
    
    - B1: app_is_ok_to_sleep() vetoes sleep while the app's dead-man watchdog
      is armed (new ble_bed_service_app_hold_active()). Bounded by
      MOVE_WATCHDOG_MS after the app's last write. Every 2026-11-01 watchdog
      stop fell in an app hold with no motor on and the CPU sleeping.
    - B2: behind BLE_DEBUG_WATCHDOG_LOG, one "BLE: hold end ..." line per app
      hold that ends without a watchdog stop, printed after the stop: refresh
      count, longest refresh gap and longest gap between loop passes over the
      whole hold. WATCHDOG STOP block unchanged.
    - B3: DECISIONS #45 (closes Prompt Y QUESTIONS 4).
    - B4: bed_control.c:73 comment - force-stops call app_stop_movement() since
      #36 (comment only).
    - B5: REGRESSION.md 6.9, unchecked.
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

diff --git a/app.c b/app.c
index 2c36b16..42d5dfa 100644
--- a/app.c
+++ b/app.c
@@ -521,6 +521,18 @@ bool app_is_ok_to_sleep(void)
     return false;
   }
 
+  // An app hold (DECISIONS.md #45) keeps the loop awake too, whether or not a
+  // motor is on: auto-level Step 1 and the pause between axes run with all
+  // motors off. On the 2026-11-01 bench every WATCHDOG STOP fell in such a
+  // stretch, with the CPU sleeping between passes and the phone's writes
+  // reaching the app late and bunched; none with a motor on or on the
+  // physical controller, which the two vetoes above already keep awake.
+  // Bounded: the watchdog disarms within MOVE_WATCHDOG_MS of the app's last
+  // write, so a lost phone cannot pin the CPU awake.
+  if (ble_bed_service_app_hold_active()) {
+    return false;
+  }
+
   // The motor-enable shadow, not just motor_state[]: set_all_motors_on()
   // energises the outputs without setting motor_state[].motor_on (#40), so a
   // runaway could previously let the CPU sleep between fail-safe samples - the
diff --git a/bed_control.c b/bed_control.c
index ba2210c..9eb2810 100644
--- a/bed_control.c
+++ b/bed_control.c
@@ -70,9 +70,11 @@ typedef enum {
  * stop-class ones - runs through this uniformly: a stop request from the
  * source that already holds the lock is just that source's own natural
  * stop; a stop (or any other command) from the other source becomes a full
- * app_stop_all() via ARBITRATE_FORCE_STOP, matching DECISIONS.md #12's "any
- * input whatsoever from the other source is interpreted ONLY as an
- * immediate clean STOP" - not a partial, verb-specific stop.
+ * stop of movement, app_stop_movement(), via ARBITRATE_FORCE_STOP (since
+ * DECISIONS.md #36; it leaves the under-bed lighting alone), matching
+ * DECISIONS.md #12's "any input whatsoever from the other source is
+ * interpreted ONLY as an immediate clean STOP" - not a partial, verb-specific
+ * stop.
  */
 static arbitrate_result_t arbitrate(bed_source_t src)
 {
diff --git a/ble_bed_service.c b/ble_bed_service.c
index bbbafd8..4900b49 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -266,12 +266,31 @@ static bool     wd_diag_have_exit = false;
 static uint32_t wd_diag_passes = 0;
 static uint32_t wd_diag_longest_pass_ticks = 0;
 static uint32_t wd_diag_longest_between_ticks = 0; // pass exit -> next pass entry (sleep + stack)
+// Whole-hold figures for the "hold end" line, reset on a fresh arm.
+static uint16_t wd_diag_hold_refreshes = 0;
+static uint16_t wd_diag_hold_longest_gap_ms = 0;
+static uint32_t wd_diag_hold_longest_between_ticks = 0;
+// Latched by move_watchdog_cancel() when it ends an armed hold; printed by
+// ble_bed_service_tick() once the stop that ended the hold has been done.
+// The watchdog-expiry path clears it: a watchdog stop has its own block.
+static bool     wd_diag_hold_end_pending = false;
+static uint8_t  wd_diag_hold_end_opcode = 0;
+static uint8_t  wd_diag_hold_end_seq = 0;
+static uint16_t wd_diag_hold_end_refreshes = 0;
+static uint16_t wd_diag_hold_end_longest_gap_ms = 0;
+static uint32_t wd_diag_hold_end_longest_between_ticks = 0;
 
 void ble_bed_service_debug_pass_begin(void)
 {
   uint32_t now = sl_sleeptimer_get_tick_count();
-  if (wd_diag_have_exit && (now - wd_diag_pass_exit_tick) > wd_diag_longest_between_ticks) {
-    wd_diag_longest_between_ticks = now - wd_diag_pass_exit_tick;
+  if (wd_diag_have_exit) {
+    uint32_t between = now - wd_diag_pass_exit_tick;
+    if (between > wd_diag_longest_between_ticks) {
+      wd_diag_longest_between_ticks = between;
+    }
+    if (between > wd_diag_hold_longest_between_ticks) {
+      wd_diag_hold_longest_between_ticks = between;
+    }
   }
   wd_diag_pass_entry_tick = now;
 }
@@ -464,15 +483,25 @@ static void move_watchdog_arm(void)
   if (move_watchdog_armed) {
     // A refresh: keep the gap since the previous one.
     uint32_t gap_ms = sl_sleeptimer_tick_to_ms(now - wd_diag_refresh_tick);
-    wd_diag_gap_ms[wd_diag_gap_next] = (gap_ms > 0xFFFF) ? 0xFFFF : (uint16_t)gap_ms;
+    uint16_t gap16 = (gap_ms > 0xFFFF) ? 0xFFFF : (uint16_t)gap_ms;
+    wd_diag_gap_ms[wd_diag_gap_next] = gap16;
     wd_diag_gap_next = (uint8_t)((wd_diag_gap_next + 1) % WD_DIAG_GAPS);
     if (wd_diag_gap_count < WD_DIAG_GAPS) {
       wd_diag_gap_count++;
     }
+    if (wd_diag_hold_refreshes < 0xFFFF) {
+      wd_diag_hold_refreshes++;
+    }
+    if (gap16 > wd_diag_hold_longest_gap_ms) {
+      wd_diag_hold_longest_gap_ms = gap16;
+    }
   } else {
     // A fresh arm starts a new hold: earlier gaps belong to an earlier hold.
     wd_diag_gap_next = 0;
     wd_diag_gap_count = 0;
+    wd_diag_hold_refreshes = 0;
+    wd_diag_hold_longest_gap_ms = 0;
+    wd_diag_hold_longest_between_ticks = 0;
   }
   wd_diag_refresh_tick = now;
   wd_diag_refresh_rx_tick = wd_diag_processing_rx_tick;
@@ -492,6 +521,17 @@ static void move_watchdog_arm(void)
 
 static void move_watchdog_cancel(void)
 {
+#if BLE_DEBUG_WATCHDOG_LOG
+  if (move_watchdog_armed) {
+    // An armed hold is ending. Keep its figures for the "hold end" line.
+    wd_diag_hold_end_pending = true;
+    wd_diag_hold_end_opcode = wd_diag_refresh_opcode;
+    wd_diag_hold_end_seq = last_move_seq;
+    wd_diag_hold_end_refreshes = wd_diag_hold_refreshes;
+    wd_diag_hold_end_longest_gap_ms = wd_diag_hold_longest_gap_ms;
+    wd_diag_hold_end_longest_between_ticks = wd_diag_hold_longest_between_ticks;
+  }
+#endif
   move_watchdog_armed = false;
   move_watchdog_expired = false;
   sl_sleeptimer_stop_timer(&move_watchdog_timer);
@@ -579,6 +619,7 @@ static void service_move_watchdog(void)
   app_stop_all();
   send_command_result(control_connection, last_move_seq, CMD_RESULT_WATCHDOG_STOP);
 #if BLE_DEBUG_WATCHDOG_LOG
+  wd_diag_hold_end_pending = false; // this hold ended in a watchdog stop, reported below instead
   // Printed after the stop and the result, so the print cannot delay either.
   printf("BLE: WATCHDOG STOP op=0x%02x seq=%u: %lu ms since last refresh (limit %u ms)\r\n",
          wd_diag_refresh_opcode, last_move_seq,
@@ -967,6 +1008,11 @@ bool ble_bed_service_app_motion_active(void)
   return app_move_active || app_auto_level_active;
 }
 
+bool ble_bed_service_app_hold_active(void)
+{
+  return move_watchdog_armed;
+}
+
 void ble_bed_service_tick(void)
 {
   // Control link dropped mid-command (BLE_GATT_DESIGN.md #5). Deferred out
@@ -988,6 +1034,18 @@ void ble_bed_service_tick(void)
   service_move_watchdog();
   service_app_commanding();
 
+#if BLE_DEBUG_WATCHDOG_LOG
+  // One line per app hold that ended WITHOUT a watchdog stop, printed here:
+  // every stop that ends a hold has already been carried out above.
+  if (wd_diag_hold_end_pending) {
+    wd_diag_hold_end_pending = false;
+    printf("BLE: hold end op=0x%02x seq=%u: %u refreshes, longest refresh gap %u ms, longest between passes %lu ms\r\n",
+           wd_diag_hold_end_opcode, wd_diag_hold_end_seq,
+           (unsigned)wd_diag_hold_end_refreshes, (unsigned)wd_diag_hold_end_longest_gap_ms,
+           (unsigned long)sl_sleeptimer_tick_to_ms(wd_diag_hold_end_longest_between_ticks));
+  }
+#endif
+
   if (status_subscriber_count == 0) {
     return; // #24: no subscribers - no notify traffic, no accel-sampling gate to feed yet
   }
diff --git a/ble_bed_service.h b/ble_bed_service.h
index 20a76fe..54a57ac 100644
--- a/ble_bed_service.h
+++ b/ble_bed_service.h
@@ -72,4 +72,10 @@ bool ble_bed_service_status_subscribed(void);
 // is commanding motion" from "nobody is, and the motors should be off".
 bool ble_bed_service_app_motion_active(void);
 
+// True while an app hold is in progress: the dead-man watchdog is armed by an
+// accepted MOVE_START / AUTO_LEVEL_START and not yet cancelled or expired.
+// Bounded by MOVE_WATCHDOG_MS after the app's last write. Used by
+// app_is_ok_to_sleep() to keep the CPU in EM0 for the hold (DECISIONS.md #45).
+bool ble_bed_service_app_hold_active(void);
+
 #endif /* BLE_BED_SERVICE_H_ */
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 479c6c1..770e53d 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2115,3 +2115,87 @@ Same run and log as #43's bench result (`prompt_w_bench.log`, Galaxy S23).
 - **DOWN ALL restored 550/275** (lines 70-79): `Stored Raise Sensitivity is:
   550`, `Stored Lower Sensitivity is: 275`.
 - `Settings mode: exit` at line 81 on a new BED LIGHTS press.
+
+## 45. During an app hold the CPU stays in EM0
+
+Date: 2026-11-01. Reviewer's instruction (Prompt AA, part B), from the
+2026-11-01 bench.
+
+### Decision
+
+While an app hold is in progress, `app_is_ok_to_sleep()` (`app.c`) vetoes
+sleep, so the main loop runs in EM0 for the whole hold. This is the same
+reason a running motor (`motor_state[].motor_on`, the motor-enable shadow)
+and an active switch-sampler session already veto sleep: those stretches need
+the loop running.
+
+"An app hold" is defined as **the dead-man watchdog being armed**
+(`ble_bed_service_app_hold_active()`, which returns `move_watchdog_armed` in
+`ble_bed_service.c`). It is armed by every accepted MOVE_START and
+AUTO_LEVEL_START (`move_watchdog_arm()`), and cleared by
+`move_watchdog_cancel()` on MOVE_STOP, AUTO_LEVEL_STOP, STOP_ALL, the
+control-phone disconnect stop, a collision, a MOVE_START force-stopped by the
+wall switch, a fresh AUTO_LEVEL_START (which re-arms at once), and watchdog
+expiry. `app_move_active` / `app_auto_level_active` are deliberately **not**
+used: they can stay true with nobody repeating, whereas the watchdog state is
+**bounded** - it ends at most `MOVE_WATCHDOG_MS` (300 ms) plus one loop pass
+after the app's last accepted write, so a phone that disappears cannot keep
+the CPU awake.
+
+### Evidence
+
+2026-11-01 bench (`autolevel_stall_bench.log`, recorded in REGRESSION.md §6):
+six watchdog stops during app auto-level holds, every one in a stretch where
+no motor was on (Step 1, or the pause between axes), with the CPU sleeping
+between passes (23-39 passes in 300 ms, longest gap between passes 27-28 ms),
+the bed not busy (longest pass 2-17 ms, received->processed 0-1 ms), and the
+phone's writes arriving late and bunched (`180,0,0,0,207,0,148,28`) although
+the app issued them every 100-103 ms. No stop with a motor running, none on
+the physical controller - the two cases that already veto sleep.
+
+Why the writes arrive late while the CPU sleeps is **not established**. The
+reviewer's hypothesis is a sleep-clock accuracy or EM2 wake-up timing effect
+on the link. Note from the code (Prompt AA handoff, part C): the LF clock is
+the LFRCO in default precision, which the SDK reports to the Bluetooth
+controller as 0xFFFF ppm, and the controller then adds an EM1 requirement
+(`sl_btctrl_init.c:289-298`) - so the device may never have been in EM2 at
+all. This decision keeps the CPU out of every sleep mode during a hold
+either way; whether it removes the stalls is for the bench to show.
+
+### Effect on auto-level timing (closes Prompt Y QUESTIONS 4)
+
+App-driven auto-level now runs the loop the way the physical controller
+does, whose AUTO LEVEL hold keeps the switch sampler (and so EM0) active.
+DECISIONS #29's pass-counted constants therefore see the same pass rate on
+both paths. What changes, from the code:
+
+- `NUM_ACCEL_READINGS` (130 passes, `auto_level.c:60`) and `accel_alpha`
+  (0.1 per pass) during app-driven Step 1 and the pause between axes: before,
+  the loop ran ~77-130 passes/s there (23-39 per 300 ms on the 2026-11-01
+  bench), so the 130-pass average spanned roughly 1.0-1.7 s; now it runs at
+  the free-running rate #29 measured during auto-level (~630 passes/s, 130
+  passes ~206 ms). The LPF's time constant shrinks in the same ratio.
+- The accelerometer is still read at most every 10 ms
+  (`ACCEL_I2C_MIN_INTERVAL_MS`), so a free-running average holds each sample
+  several times - exactly as on the physical path.
+- With motors on nothing changes: a running motor already vetoed sleep.
+- The 1500 ms steady read and the 1000 ms axis pause are sleeptimer-based and
+  unchanged.
+
+So app-driven auto-level's steady checks now use the same, shorter
+averaging window as the hardware-verified physical path, instead of a longer
+one that depended on how often the CPU happened to wake. ASSERTED from code;
+not measured.
+
+### Cost
+
+EM0 for the duration of a hold only (seconds, user-initiated). An idle bed,
+connected or not, sleeps as before.
+
+### Status
+
+Builds clean with `BLE_DEBUG_WATCHDOG_LOG` at 1 and at 0. **Not run on the
+bench.** A bench-only `BLE: hold end ...` line (behind
+`BLE_DEBUG_WATCHDOG_LOG`) reports each hold's refresh count, longest refresh
+gap and longest gap between loop passes, so the fix can be checked without a
+stall: the gap between passes should now be ~0-1 ms for the whole hold.
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index 1ba46ad..c1aad53 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -210,6 +210,11 @@ pitch, all actuators clear of limits.
       run, start to "Bed is level" - the original auto-level BLE-drop bug
       (DECISIONS.md #26) only manifested on a sustained multi-second move,
       so a quick/near-level test is not sufficient coverage for this check
+- [ ] 6.9 App AUTO LEVEL (DECISIONS #45): 10 holds from off-level, no
+      `BLE: WATCHDOG STOP` / no "Connection stalled" in the app. With
+      `BLE_DEBUG_WATCHDOG_LOG` 1, each hold that ends normally prints one
+      `BLE: hold end op=0x03 ...` line; "longest between passes" should be
+      ~0-1 ms.
 
 **Recorded result, 2026-11-01 (firmware `c945146`, `BLE_DEBUG_WATCHDOG_LOG`
 1, desk bed, iPhone with app TestFlight build `e22e4b9`, log
```

## d. Raw output of commands run

No secrets appeared; nothing is redacted. `<SDK>` stands for
`C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p` (the
`SDK_PATH` in `cmake_gcc/tilt_bed_controller.cmake:5`) where a command line
was shortened; output lines are verbatim. What is not reproduced, and why, is
in section g.

### d.1 Session command log, in the order run

Git, greps, SDK and config excerpts (every citation in b.3 and e), builds as
they ran, and the host unit test.

```
$ date
Sun Nov  1 15:29:02 PST 2026
$ git status
On branch main
Your branch is up to date with 'origin/main'.

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/prompts/PROMPT_AA_sleep-veto-during-app-holds.md

no changes added to commit (use "git add" and/or "git commit -a")
$ git fetch
exit 0
$ git rev-parse HEAD origin/main
c945146cfcc9cd4f9d9067288282e16f8a8e8ba0
c945146cfcc9cd4f9d9067288282e16f8a8e8ba0
$ git log --oneline -3
c945146 Handoff 2026-10-23 01: Prompt Y (watchdog diagnostics, Stage C status, Prompt W record)
c7d743d Watchdog-stop diagnostics behind BLE_DEBUG_WATCHDOG_LOG (bench only)
17073a3 Settings-mode entry: require fresh sampler state
$ grep -rn -i "pre-ship\|preship\|before shipping\|ship list" docs/*.md *.c *.h | grep -v "^docs/SESSION_LOG\|^docs/handoffs"
docs/DECISIONS.md:1802:  per-bit bounce tally. Set it to 0 before shipping.
docs/SWITCH_SAMPLER_DESIGN.md:101:  #define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
ble_bed_service.h:35:// 0 before shipping.
$ grep -n "^## " docs/DECISIONS.md | tail -4
1730:## 41. One switch reader
1843:## 42. Pairing window: new bonds only while settings mode is active (implements #19)
1937:## 43. Bond store full: overwrite the least recently used bond; the firmware disconnects a phone whose pairing it refused
2036:## 44. STOP in settings mode cancels any hold in progress and stays in settings mode
$ for m in BGM220SC22HNA BGM220PC22HNA: grep LFXO_EN / LFRCO_PRECISION / DEFAULT_LF_CLOCK_SOURCE in <SDK>/platform_core/hardware/module/config/$m/
== BGM220SC22HNA
132:#define SL_CLOCK_MANAGER_LFXO_EN    0
297:#define SL_CLOCK_MANAGER_LFRCO_PRECISION    cmuPrecisionDefault
78:#define SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE    SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE_LFRCO
== BGM220PC22HNA
132:#define SL_CLOCK_MANAGER_LFXO_EN    1
297:#define SL_CLOCK_MANAGER_LFRCO_PRECISION    cmuPrecisionDefault
78:#define SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE    SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE_LFXO
$ diff --strip-trailing-cr <SDK module BGM220SC22HNA> config/ (oscillator, tree)
323a324
> 
244a245
> 
$ grep -rn -i "crystal\|LFXO\|32.768\|32 kHz" <module config>/BGM220SC22HNA/*.h | grep -v "define\|<o \|<q \|<e>\|<i> Enable\|LFXO_CFG" 
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/hardware/module/config/BGM220SC22HNA/sl_clock_manager_oscillator_config.h:53:// <h> HFXO Settings (if High Frequency crystal is used)
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/hardware/module/config/BGM220SC22HNA/sl_clock_manager_oscillator_config.h:129:// <e> LFXO Settings (if Low Frequency crystal is used)
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/hardware/module/config/BGM220SC22HNA/sl_clock_manager_oscillator_config.h:285:// <i> against a crystal driven by the HFXO. Hardware detects temperature
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/hardware/module/config/BGM220SC22HNA/sl_device_init_hfxo_config.h:38:// <cmuHfxoOscMode_Crystal=> Crystal oscillator
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/hardware/module/config/BGM220SC22HNA/sl_device_init_hfxo_config.h:40:// <i> Default: cmuHfxoOscMode_Crystal
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/hardware/module/config/BGM220SC22HNA/sl_device_init_hfxo_config.h:41:#define SL_DEVICE_INIT_HFXO_MODE           cmuHfxoOscMode_Crystal
$ grep -n "#define" config/sl_power_manager_config.h config/sl_rail_util_power_manager_init_config.h
config/sl_power_manager_config.h:34:#define SL_POWER_MANAGER_CONFIG_H
config/sl_power_manager_config.h:44:#define SL_POWER_MANAGER_CUSTOM_HF_OSCILLATOR_IRQ_HANDLER  0
config/sl_power_manager_config.h:52:#define SL_POWER_MANAGER_CONFIG_VOLTAGE_SCALING_FAST_WAKEUP   0
config/sl_power_manager_config.h:60:#define SL_POWER_MANAGER_ENABLE_EM01_VOLTAGE_SCALING   0
config/sl_power_manager_config.h:67:#define SL_POWER_MANAGER_DEBUG  0
config/sl_power_manager_config.h:73:#define SL_POWER_MANAGER_DEBUG_POOL_SIZE  10
config/sl_power_manager_config.h:84:#define SL_POWER_MANAGER_INIT_EMU_EM4_PIN_RETENTION_MODE EMU_EM4CTRL_EM4IORETMODE_DISABLE
config/sl_power_manager_config.h:93:#define SL_POWER_MANAGER_INIT_EMU_EM2_DEBUG_ENABLE 1
config/sl_power_manager_config.h:101:#define SL_POWER_MANAGER_RAMP_DVDD_EN 1
config/sl_power_manager_config.h:111:#define SL_POWER_MANAGER_RAMP_DVDD_TOLERANCE 40
config/sl_rail_util_power_manager_init_config.h:32:#define SL_RAIL_UTIL_POWER_MANAGER_INIT_CONFIG_H
config/sl_rail_util_power_manager_init_config.h:40:#define SL_RAIL_UTIL_RAIL_POWER_MANAGER_INIT 1
$ grep -n -i "lfxo\|lfrco\|32.768\|32 kHz\|crystal\|PC14\|PC15\|PD0\|PD1" docs/HARDWARE_CONFIG.md
$ grep -n "HFXO_PRECISION\|HFXO_EN \|HFXO_MODE\|HFXO_CTUNE " config/sl_clock_manager_oscillator_config.h
63:#define SL_CLOCK_MANAGER_HFXO_EN    SL_CLOCK_MANAGER_HFXO_EN_ENABLE
72:#define SL_CLOCK_MANAGER_HFXO_MODE    HFXO_CFG_MODE_XTAL
84:#define SL_CLOCK_MANAGER_HFXO_CTUNE    140
98:#define SL_CLOCK_MANAGER_HFXO_PRECISION    50
$ grep -n "power_manager\|device_init\|clock_manager\|sleeptimer\|rail_util_power" tilt_bed_controller.slcp
78:  id: clock_manager
81:  id: device_init
215:  id: clock_manager_init_hal_s2
218:  id: clock_manager_runtime
221:  id: clock_manager_runtime_hal_s2
251:  id: device_init_dcdc
254:  id: device_init_dcdc_s2
464:  id: power_manager
467:  id: power_manager_core
470:  id: power_manager_core_common
473:  id: power_manager_core_impl_sleep_loop
476:  id: power_manager_core_impl_sleep_loop_common
479:  id: power_manager_deepsleep_blocking_hfxo_restore
482:  id: power_manager_handler_implementation
608:  id: sl_rail_util_power_manager_init
617:  id: sleeptimer
620:  id: sleeptimer_cortexm
$ grep -n "^[a-z].*(" <SDK>/bluetooth_le_controller/src/sl_btctrl_init.c | sed -n 1,30p
173:SL_WEAK void sl_btctrl_debug_init(struct sl_btctrl_config *config)
178:sl_status_t sl_btctrl_init_internal(struct sl_btctrl_config *config)
198:sl_status_t sl_btctrl_init_functional(struct sl_btctrl_config *config)
677:sl_status_t sl_btctrl_init(void)
687:void sl_btctrl_deinit(void)
703:void sl_btctrl_deinit_functional(void)
$ sed -n 284,300p <SDK>/bluetooth_le_controller/src/sl_btctrl_init.c
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
  // Configure RAIL to use timersync in sleep handling for Bluetooth.
  config->flags |= SL_BTCTRL_CONFIG_FLAG_SYNCHRONIZE_TO_SLEEP_CLOCK;
#endif // SL_CATALOG_POWER_MANAGER_PRESENT

#if defined(SL_CATALOG_SLEEPTIMER_PRESENT)
  config->sleepClockAccuracy = sl_sleeptimer_get_clock_accuracy();
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
  if (!(config->flags & SL_BTCTRL_CONFIG_FLAG_USE_INACCURATE_SLEEP_CLOCK)) {
    if (config->sleepClockAccuracy > SL_BTCTRL_SLEEP_CLOCK_ACCURACY_LIMIT) {
      sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
    }
  }
#endif // SL_CATALOG_POWER_MANAGER_PRESENT
#endif // SL_CATALOG_SLEEPTIMER_PRESENT

  status = sl_btctrl_init_ll(config);
$ sed -n 58,62p;88,94p <SDK>/bluetooth_le_controller/inc/sl_btctrl_linklayer_defs.h
/**
 * Highest limit of the (SCA) Sleep Clock Accuracy in ppm
 */
#define SL_BTCTRL_SLEEP_CLOCK_ACCURACY_LIMIT 500

enum sl_btctrl_config_flags {
  SL_BTCTRL_CONFIG_FLAG_ENABLE_CONN_EVENT_LENGTH_EXTENSION = 0x20000,
  SL_BTCTRL_CONFIG_FLAG_SCANNER_RECEPTION_EARLY_ABORT = 0x40000,
  SL_BTCTRL_CONFIG_FLAG_USE_INACCURATE_SLEEP_CLOCK = 0x80000,
  SL_BTCTRL_CONFIG_FLAG_SYNCHRONIZE_TO_SLEEP_CLOCK = 0x100000,
};

$ sed -n 281,286p <SDK>/platform_core/platform/service/sleeptimer/src/sl_sleeptimer_hal_rtcc.c
uint16_t sleeptimer_hal_get_clock_accuracy(void)
{
  uint16_t precision;
  sl_clock_manager_get_clock_branch_precision(SL_CLOCK_BRANCH_RTCCCLK, &precision);
  return precision;
}
$ sed -n 48,51p <SDK>/platform_core/platform/service/sleeptimer/inc/sli_sleeptimer.h
#if SL_SLEEPTIMER_PERIPHERAL == SL_SLEEPTIMER_PERIPHERAL_DEFAULT
#if defined(RTCC_PRESENT) && RTCC_COUNT >= 1
#undef SL_SLEEPTIMER_PERIPHERAL
#define SL_SLEEPTIMER_PERIPHERAL SL_SLEEPTIMER_PERIPHERAL_RTCC
$ sed -n 2618,2650p <SDK>/platform_core/platform/emlib/src/em_cmu.c
uint16_t CMU_LF_ClockPrecisionGet(CMU_Clock_TypeDef clock)
{
  CMU_Select_TypeDef src = CMU_ClockSelectGet(clock);
  uint16_t precision;

  switch (src) {
    case cmuSelect_LFXO:
      precision = lfxo_precision;
      break;

#if defined(PLFRCO_PRESENT)
#if defined(LFRCO_CFG_HIGHPRECEN)
    case cmuSelect_LFRCO:
    case cmuSelect_PLFRCO:

      CMU->CLKEN0_SET = CMU_CLKEN0_LFRCO;

      if (LFRCO->CFG & _LFRCO_CFG_HIGHPRECEN_MASK) {
        precision = 500;
      } else {
        precision = 0xFFFF;
      }
      break;
#endif
#endif

    default:
      precision = 0xFFFF;
      break;
  }

  return precision;
}
$ sed -n 153,155p <SDK>/platform_core/platform/emlib/inc/em_cmu.h
#if defined(LFRCO_PRECISION_MODE) && (LFRCO_PRECISION_MODE == 1)
#define PLFRCO_PRESENT
#endif
$ grep -n "LFRCO_PRECISION_MODE\|RTCC_PRESENT\|LFXO_PRESENT" <SDK>/.../BGM22/Include/bgm220sc22hna.h
194:#define LFRCO_PRECISION_MODE                              1                                    /** Precision mode of LFRCO enabled or disabled */
407:#define LFXO_PRESENT                                        /** LFXO is available in this part */
419:#define RTCC_PRESENT                                        /** RTCC is available in this part */
$ sed -n 280,298p config/sl_clock_manager_oscillator_config.h
// </h>

// <h> LFRCO Settings
// <o SL_CLOCK_MANAGER_LFRCO_PRECISION> Precision Mode
// <i> Precision mode uses hardware to automatically re-calibrate the LFRCO
// <i> against a crystal driven by the HFXO. Hardware detects temperature
// <i> changes and initiates a re-calibration of the LFRCO as needed when
// <i> operating in EM0, EM1, or EM2. If a re-calibration is necessary and the
// <i> HFXO is not active, the precision mode hardware will automatically
// <i> enable HFXO for a short time to perform the calibration. EM4 operation is
// <i> not allowed while precision mode is enabled.
// <i> If high precision is selected on devices that do not support it, default
// <i> precision will be used.
// <cmuPrecisionDefault=> Default precision
// <cmuPrecisionHigh=> High precision
// <d> cmuPrecisionDefault
#ifndef SL_CLOCK_MANAGER_LFRCO_PRECISION
#define SL_CLOCK_MANAGER_LFRCO_PRECISION    cmuPrecisionDefault
#endif
$ grep -rn "sl_btctrl_init_config\b\|sl_btctrl_init()" <SDK>/bluetooth_le_controller/inc <SDK>/bluetooth_le_controller/src <SDK>/bluetooth_le_host/src <SDK>/bluetooth_le_host/inc autogen/ | head
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/bluetooth_le_controller/inc/sl_btctrl_linklayer.h:395:void sl_btctrl_init_config(struct sl_btctrl_config *config);
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/bluetooth_le_controller/src/sl_btctrl_init.c:681:  sl_btctrl_init_config(&config);
$ grep -c "sl_btctrl_init\b" <SDK>/bluetooth_le_host/build/gcc/cortex-m33/ble_system/release/libble_system.a (binary match)
4
$ sed -n 46,53p;86,94p config/sl_power_manager_config.h

// <q SL_POWER_MANAGER_CONFIG_VOLTAGE_SCALING_FAST_WAKEUP> Enable fast wakeup (disable voltage scaling in EM2/3 mode)
// <i> Enable or disable voltage scaling in EM2/3 modes (when available). This decreases wakeup time by about 30 us.
// <i> Deprecated. It is replaced by the function sl_power_manager_em23_voltage_scaling_enable_fast_wakeup()
// <i> Default: 0
#ifndef SL_POWER_MANAGER_CONFIG_VOLTAGE_SCALING_FAST_WAKEUP
#define SL_POWER_MANAGER_CONFIG_VOLTAGE_SCALING_FAST_WAKEUP   0
#endif

// <e SL_POWER_MANAGER_INIT_EMU_EM2_DEBUG_ENABLE > Enable EM2 debugging feature
// <i> Enable or disable debugging features.
// <i> Force PD0B/PD0D to stay on during EM2 entry. This allows the debugger to remain connected in EM2 and EM3.
// <i> Enabling debug connectivity results in an increased power consumption in EM2/EM3.
// <i> Default: 1
#ifndef SL_POWER_MANAGER_INIT_EMU_EM2_DEBUG_ENABLE
#define SL_POWER_MANAGER_INIT_EMU_EM2_DEBUG_ENABLE 1
#endif
$ sed -n 34,41p config/sl_rail_util_power_manager_init_config.h
// <<< Use Configuration Wizard in Context Menu >>>

// <h> RAIL power manager configuration

// <o SL_RAIL_UTIL_RAIL_POWER_MANAGER_INIT> Enable RAIL power manager initialization
// <i> Default: 1
#define SL_RAIL_UTIL_RAIL_POWER_MANAGER_INIT 1

$ sed -n 94,106p config/sl_clock_manager_oscillator_config.h

// <o SL_CLOCK_MANAGER_HFXO_PRECISION> Precision in PPM <0-65535>
// <d> 50
#ifndef SL_CLOCK_MANAGER_HFXO_PRECISION
#define SL_CLOCK_MANAGER_HFXO_PRECISION    50
#endif

// <q SL_CLOCK_MANAGER_CTUNE_MFG_HFXO_EN> CTUNE HXFO manufacturing
// <i> Enable to use CTUNE HFXO manufacturing value for calibration
// <d> 1
#ifndef SL_CLOCK_MANAGER_CTUNE_MFG_HFXO_EN
#define SL_CLOCK_MANAGER_CTUNE_MFG_HFXO_EN    1
#endif
$ sed -n 477,481p tilt_bed_controller.slcp
- package: simplicity_sdk
  vendor: silabs
  id: power_manager_deepsleep_blocking_hfxo_restore
- package: simplicity_sdk
  vendor: silabs
$ grep -n description 
grep: : No such file or directory
$ sed -n 350,368p <SDK>/platform_core/platform/service/power_manager/src/sleep_loop/sl_power_manager.c
#ifdef SLI_DEVICE_SUPPORTS_EM1P
  requirement_high_accuracy_hf_clock_back_to_zero = false;
#endif

  if (is_states_saved == true) {
    is_sleeping_waiting_for_clock_restore = false;
    // Restore clocks
    if (is_hf_x_oscillator_not_preserved) {
      sli_power_manager_restore_high_freq_accuracy_clk();
      is_hf_x_oscillator_not_preserved = false;
    }
    // If possible, go back to sleep in EM1 while waiting for HF accuracy restore
    while (!sli_power_manager_is_high_freq_accuracy_clk_ready(false)) {
      sli_power_manager_apply_em(SL_POWER_MANAGER_EM1);
      primask_state = yield_critical_with_primask(primask_state);
    }
    sli_power_manager_restore_states();
    is_states_saved = false;
  }
$ grep -n "move_watchdog_armed\|move_watchdog_arm()\|move_watchdog_cancel()\|static void move_watchdog\|^static void service_move_watchdog\|^void ble_bed_service_tick\|service_app_commanding();\|case CMD_OPCODE\|if (stop_all_pending)\|if (disconnect_stop_pending)\|if (any_collision)" ble_bed_service.c
245:static bool    move_watchdog_armed = false;
256:static uint32_t wd_diag_refresh_tick = 0;         // last move_watchdog_arm()
453:static void move_watchdog_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
460:static void move_watchdog_arm(void)
464:  if (move_watchdog_armed) {
486:  move_watchdog_armed = true;
493:static void move_watchdog_cancel(void)
495:  move_watchdog_armed = false;
532:static void service_move_watchdog(void)
534:  if (!move_watchdog_armed) {
538:  if (any_collision) {
542:    move_watchdog_cancel();
575:  move_watchdog_cancel();
620:  if (stop_all_pending) {
625:    move_watchdog_cancel();
715:    case CMD_OPCODE_MOVE_START: {
737:          move_watchdog_cancel();
749:      move_watchdog_arm();
754:    case CMD_OPCODE_MOVE_STOP:
758:      move_watchdog_cancel();
766:    case CMD_OPCODE_AUTO_LEVEL_START:
776:        move_watchdog_cancel();
789:      move_watchdog_arm();
793:    case CMD_OPCODE_AUTO_LEVEL_STOP:
797:      move_watchdog_cancel();
805:    case CMD_OPCODE_LIGHTS_SET:
970:void ble_bed_service_tick(void)
974:  if (disconnect_stop_pending) {
976:    move_watchdog_cancel();
989:  service_app_commanding();
$ git add docs/DECISIONS.md docs/REGRESSION.md docs/SESSION_LOG.md docs/prompts/PROMPT_AA_sleep-veto-during-app-holds.md && git status --short && git commit -q -F - <<EOF ... && git log --oneline -1
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/SESSION_LOG.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/prompts/PROMPT_AA_sleep-veto-during-app-holds.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  docs/DECISIONS.md
M  docs/REGRESSION.md
M  docs/SESSION_LOG.md
A  docs/prompts/PROMPT_AA_sleep-veto-during-app-holds.md
0993ee9 Docs: 2026-11-01 auto-level stall bench record, Prompt Y rulings
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
$ grep -n "ARBITRATE_FORCE_STOP\|app_stop_movement\|app_stop_all" bed_control.c
63:  ARBITRATE_FORCE_STOP  // a different source holds the lock - force a clean stop instead
73: * stop of movement, app_stop_movement(), via ARBITRATE_FORCE_STOP (since
94:  return ARBITRATE_FORCE_STOP;
104:  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
106:    app_stop_movement();
117:  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
118:    app_stop_movement();
132:  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
134:    app_stop_movement();
145:  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
146:    app_stop_movement();
158:  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
159:    app_stop_movement();
[1/5] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_control.c.obj
[2/5] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/5] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/5] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[5/5] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
$ sed -i (BLE_DEBUG_WATCHDOG_LOG 1 -> 0) ble_bed_service.h && grep -n "define BLE_DEBUG_WATCHDOG_LOG" ble_bed_service.h
36:#define BLE_DEBUG_WATCHDOG_LOG 0
$ build (flag 0)
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
$ sed -i (0 -> 1) && grep
36:#define BLE_DEBUG_WATCHDOG_LOG 1
$ build (flag 1 again)
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
$ arm-none-eabi-size
   text	   data	    bss	    dec	    hex	filename
 244988	    780	  72480	 318248	  4db28	cmake_gcc/build/base/tilt_bed_controller.out
$ git diff --stat
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_control.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.h', LF will be replaced by CRLF the next time Git touches it
 .projectlinkstore |  2 +-
 app.c             | 12 +++++++++++
 bed_control.c     |  8 ++++---
 ble_bed_service.c | 64 ++++++++++++++++++++++++++++++++++++++++++++++++++++---
 ble_bed_service.h |  6 ++++++
 5 files changed, 85 insertions(+), 7 deletions(-)
$ git add app.c bed_control.c ble_bed_service.c ble_bed_service.h docs/DECISIONS.md docs/REGRESSION.md && git status --short && git commit -q -F - <<EOF ... && git log --oneline -1
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_control.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  app.c
M  bed_control.c
M  ble_bed_service.c
M  ble_bed_service.h
M  docs/DECISIONS.md
M  docs/REGRESSION.md
75b0a61 Keep the CPU in EM0 during app holds; hold-end diagnostic line (DECISIONS #45)
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
$ grep -n "move_watchdog_armed\|move_watchdog_arm();\|move_watchdog_cancel();\|static void move_watchdog_arm\|static void move_watchdog_cancel\|wd_diag_hold_end_pending\|bool ble_bed_service_app_hold_active\|BLE: hold end\|case CMD_OPCODE_\|if (stop_all_pending)\|if (disconnect_stop_pending)\|if (any_collision)\|bed_conflict_inhibited()\|if (!bed_move_start\|if (!app_auto_level_active)\|if (!bed_auto_level_start" ble_bed_service.c
245:static bool    move_watchdog_armed = false;
276:static bool     wd_diag_hold_end_pending = false;
479:static void move_watchdog_arm(void)
483:  if (move_watchdog_armed) {
515:  move_watchdog_armed = true;
522:static void move_watchdog_cancel(void)
525:  if (move_watchdog_armed) {
527:    wd_diag_hold_end_pending = true;
535:  move_watchdog_armed = false;
574:  if (!move_watchdog_armed) {
578:  if (any_collision) {
582:    move_watchdog_cancel();
615:  move_watchdog_cancel();
622:  wd_diag_hold_end_pending = false; // this hold ended in a watchdog stop, reported below instead
661:  if (stop_all_pending) {
666:    move_watchdog_cancel();
746:  if (bed_conflict_inhibited()
756:    case CMD_OPCODE_MOVE_START: {
774:        if (!bed_move_start(BED_SOURCE_APP, target, dir)) {
778:          move_watchdog_cancel();
790:      move_watchdog_arm();
795:    case CMD_OPCODE_MOVE_STOP:
799:      move_watchdog_cancel();
807:    case CMD_OPCODE_AUTO_LEVEL_START:
816:      if (!app_auto_level_active) {
817:        move_watchdog_cancel();
819:        if (!bed_auto_level_start(BED_SOURCE_APP)) {
830:      move_watchdog_arm();
834:    case CMD_OPCODE_AUTO_LEVEL_STOP:
838:      move_watchdog_cancel();
846:    case CMD_OPCODE_LIGHTS_SET:
1011:bool ble_bed_service_app_hold_active(void)
1013:  return move_watchdog_armed;
1020:  if (disconnect_stop_pending) {
1022:    move_watchdog_cancel();
1040:  if (wd_diag_hold_end_pending) {
1041:    wd_diag_hold_end_pending = false;
1042:    printf("BLE: hold end op=0x%02x seq=%u: %u refreshes, longest refresh gap %u ms, longest between passes %lu ms\r\n",
$ grep -n "ble_bed_service_app_hold_active\|bool app_is_ok_to_sleep\|return true;" app.c ble_bed_service.h
app.c:510:bool app_is_ok_to_sleep(void)
app.c:532:  if (ble_bed_service_app_hold_active()) {
app.c:560:  return true;
ble_bed_service.h:79:bool ble_bed_service_app_hold_active(void);
$ which gcc cc clang
which: no cc in (/c/Users/adunc/bin:/mingw64/bin:/usr/local/bin:/usr/bin:/bin:/mingw64/bin:/usr/bin:/c/Users/adunc/bin:/c/WINDOWS/system32:/c/WINDOWS:/c/WINDOWS/System32/Wbem:/c/WINDOWS/System32/WindowsPowerShell/v1.0:/c/WINDOWS/System32/OpenSSH:/c/Program Files/dotnet:/c/Program Files/Microsoft SQL Server/150/Tools/Binn:/c/Program Files/PuTTY:/cmd:/mingw64/bin:/usr/bin:/c/MinGW/bin:/c/Program Files/UVtools:/c/Program Files (x86)/Microsoft SQL Server/90/Tools/binn:/c/Program Files/NVIDIA Corporation/NVIDIA App/NvDLISR:/c/Program Files/Sunshine:/c/Program Files/Sunshine/tools:/c/Program Files (x86)/NVIDIA Corporation/PhysX/Common:/c/Users/adunc/AppData/Local/Programs/Python/Python312/Scripts:/c/Users/adunc/AppData/Local/Programs/Python/Python312:/c/Users/adunc/AppData/Local/Microsoft/WindowsApps:/c/src/flutter/bin:/c/Users/adunc/AppData/Local/Programs/Microsoft VS Code/bin:/c/Users/adunc/AppData/Local/GitHubDesktop/bin:/c/Users/adunc/.local/bin:/c/Users/adunc/AppData/Local/Android/Sdk/platform-tools:%USERPROFILE%/AppData/Local/Microsoft/WindowsApps:/c/Users/adunc/AppData/Local/Programs/Python/Python312:/usr/bin/vendor_perl:/usr/bin/core_perl)
which: no clang in (/c/Users/adunc/bin:/mingw64/bin:/usr/local/bin:/usr/bin:/bin:/mingw64/bin:/usr/bin:/c/Users/adunc/bin:/c/WINDOWS/system32:/c/WINDOWS:/c/WINDOWS/System32/Wbem:/c/WINDOWS/System32/WindowsPowerShell/v1.0:/c/WINDOWS/System32/OpenSSH:/c/Program Files/dotnet:/c/Program Files/Microsoft SQL Server/150/Tools/Binn:/c/Program Files/PuTTY:/cmd:/mingw64/bin:/usr/bin:/c/MinGW/bin:/c/Program Files/UVtools:/c/Program Files (x86)/Microsoft SQL Server/90/Tools/binn:/c/Program Files/NVIDIA Corporation/NVIDIA App/NvDLISR:/c/Program Files/Sunshine:/c/Program Files/Sunshine/tools:/c/Program Files (x86)/NVIDIA Corporation/PhysX/Common:/c/Users/adunc/AppData/Local/Programs/Python/Python312/Scripts:/c/Users/adunc/AppData/Local/Programs/Python/Python312:/c/Users/adunc/AppData/Local/Microsoft/WindowsApps:/c/src/flutter/bin:/c/Users/adunc/AppData/Local/Programs/Microsoft VS Code/bin:/c/Users/adunc/AppData/Local/GitHubDesktop/bin:/c/Users/adunc/.local/bin:/c/Users/adunc/AppData/Local/Android/Sdk/platform-tools:%USERPROFILE%/AppData/Local/Microsoft/WindowsApps:/c/Users/adunc/AppData/Local/Programs/Python/Python312:/usr/bin/vendor_perl:/usr/bin/core_perl)
/c/MinGW/bin/gcc
exit 2
$ ls test/
bed_control_test.c
$ (in test/) gcc -std=c99 -Wall -I.. -o <scratchpad>/aa/bed_control_test.exe bed_control_test.c ../bed_control.c && <scratchpad>/aa/bed_control_test.exe
gcc exit 0
PASS: scenario 1a: app bed_move_start acquires the lock as APP
PASS: scenario 1a-ret: bed_move_start returns true when it performs the normal action
PASS: scenario 1b: switch move while APP holds the lock forces a stop, not a switch move
PASS: scenario 1b-ret: bed_move_start returns false when force-stopped instead of performing the move
PASS: scenario 1c: lock still shows APP immediately after the forced stop (lazy release happens on the next call/tick)
PASS: scenario 2a: switch bed_move_start acquires the lock as SWITCHES
PASS: scenario 2a-ret: bed_move_start returns true when it performs the normal action
PASS: scenario 2b: app auto-level while SWITCHES holds the lock forces a stop, not an app auto-level start
PASS: scenario 2b-ret: bed_auto_level_start returns false when force-stopped instead of starting
PASS: scenario 2c: app lights-set while SWITCHES holds the lock forces a stop, not a lights change
PASS: scenario 2c-ret: bed_lights_set returns false when force-stopped instead of setting lights
PASS: scenario 3a: non-active-source stop request still results in a full stop (never refused)
PASS: scenario 3a-ret: bed_move_stop returns true even when the stop was a forced full stop
PASS: scenario 3b: active source's own stop calls its natural stop action, not a forced stop
PASS: scenario 3b-ret: bed_move_stop returns true for its own normal stop
PASS: scenario 3c: non-active-source auto-level stop forces a full stop and still returns true
PASS: scenario 4a: tick releases the lock once idle
PASS: scenario 4b: next source's start acquires the lock after release
PASS: scenario 4b-ret: bed_move_start returns true acquiring the lock after release
PASS: scenario 5a: a conflict latches the inhibit
PASS: scenario 5b: app repeat while inhibited is refused and starts nothing
PASS: scenario 5c: switch press while inhibited is refused too - both sources, not just the app
PASS: scenario 5d: auto-level start is inhibited as well - it is motion
PASS: scenario 5e: latch survives one source releasing while the other still commands
PASS: scenario 5f: app still refused while it alone is still holding
PASS: scenario 5g: latch clears once BOTH sources have released
PASS: scenario 5h: a fresh press after both released starts normally
PASS: scenario 6a: inhibited
PASS: scenario 6b: bed_move_stop still works while inhibited
PASS: scenario 6c: lights are not motion and are not inhibited

0 check(s) failed
test exit 0
$ sed -n 71,78p;173,181p;195,203p config/sl_clock_manager_tree_config.h
// <o SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE> Default Clock Source Selection for LF clock branches
//   <SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE_LFRCO=> LFRCO
//   <SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE_LFXO=> LFXO
//   <SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE_ULFRCO=> ULFRCO
// <i> Selection of the low frequency clock source. LF clock branches can select this value by chosing the DEFAULT_HF value.
// <d> SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE_LFRCO
#ifndef SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE
#define SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE    SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE_LFRCO
// <o SL_CLOCK_MANAGER_EM23GRPACLK_SOURCE> Clock Source Selection for EM23GRPACLK branch
//   <SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE=> DEFAULT_LF
//   <CMU_EM23GRPACLKCTRL_CLKSEL_LFRCO=> LFRCO
//   <CMU_EM23GRPACLKCTRL_CLKSEL_LFXO=> LFXO
//   <CMU_EM23GRPACLKCTRL_CLKSEL_ULFRCO=> ULFRCO
// <i> Selection of the Clock source for EM23GRPACLK
// <d> SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE
#ifndef SL_CLOCK_MANAGER_EM23GRPACLK_SOURCE
#define SL_CLOCK_MANAGER_EM23GRPACLK_SOURCE    SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE
// <o SL_CLOCK_MANAGER_RTCCCLK_SOURCE> Clock Source Selection for RTCCCLK branch
//   <SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE=> DEFAULT_LF
//   <CMU_RTCCCLKCTRL_CLKSEL_LFRCO=> LFRCO
//   <CMU_RTCCCLKCTRL_CLKSEL_LFXO=> LFXO
//   <CMU_RTCCCLKCTRL_CLKSEL_ULFRCO=> ULFRCO
// <i> Selection of the Clock source for RTCCCLK
// <d> SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE
#ifndef SL_CLOCK_MANAGER_RTCCCLK_SOURCE
#define SL_CLOCK_MANAGER_RTCCCLK_SOURCE    SL_CLOCK_MANAGER_DEFAULT_LF_CLOCK_SOURCE
$ sed -n 129,133p config/sl_clock_manager_oscillator_config.h
// <e> LFXO Settings (if Low Frequency crystal is used)
// <i> Enable to configure LFXO
#ifndef SL_CLOCK_MANAGER_LFXO_EN
#define SL_CLOCK_MANAGER_LFXO_EN    0
#endif
$ sed -n 44,56p config/sl_sleeptimer_config.h

// <o SL_SLEEPTIMER_PERIPHERAL> Timer Peripheral Used by Sleeptimer
//   <SL_SLEEPTIMER_PERIPHERAL_DEFAULT=> Default (auto select)
//   <SL_SLEEPTIMER_PERIPHERAL_RTCC=> RTCC
//   <SL_SLEEPTIMER_PERIPHERAL_PRORTC=> Radio internal RTC (PRORTC)
//   <SL_SLEEPTIMER_PERIPHERAL_RTC=> RTC
//   <SL_SLEEPTIMER_PERIPHERAL_SYSRTC=> SYSRTC
//   <SL_SLEEPTIMER_PERIPHERAL_BURTC=> Back-Up RTC (BURTC)
//   <SL_SLEEPTIMER_PERIPHERAL_WTIMER=> WTIMER
//   <SL_SLEEPTIMER_PERIPHERAL_TIMER=> TIMER
// <i> Selection of the Timer Peripheral Used by the Sleeptimer
// <i> If BURTC is needed as an EM4 wake-up source, it must not be used by Sleeptimer.
#define SL_SLEEPTIMER_PERIPHERAL  SL_SLEEPTIMER_PERIPHERAL_DEFAULT
$ grep -rn -i "accuracy\|sleep.clock\|SCA\b" config/ autogen/   (no output = none)
$ sed -n 438,448p <SDK>/platform_core/platform/service/clock_manager/src/sl_clock_manager_hal_s2.c
    case SL_CLOCK_BRANCH_WDOG1CLK:
      *precision = 0U;
      return_status =  SL_STATUS_NOT_SUPPORTED;
      break;
#endif
#if defined(_CMU_RTCCCLKCTRL_MASK)
    case SL_CLOCK_BRANCH_RTCCCLK:
      *precision = CMU_LF_ClockPrecisionGet(cmuClock_RTCCCLK);
      if (*precision == 0xFFFF) {
        return_status =  SL_STATUS_NOT_AVAILABLE;
      }
$ sed -n 287,290p <SDK>/platform_core/platform/service/clock_manager/src/sl_clock_manager_hal_s2.c (unused here)
$ sed -n 395,396p <SDK>/bluetooth_le_controller/inc/sl_btctrl_linklayer.h
void sl_btctrl_init_config(struct sl_btctrl_config *config);

$ sed -n 677,685p <SDK>/bluetooth_le_controller/src/sl_btctrl_init.c
sl_status_t sl_btctrl_init(void)
{
  struct sl_btctrl_config config;

  sl_btctrl_init_config(&config);
  config.buffer_memory = SL_BT_CONTROLLER_BUFFER_MEMORY;

  return sl_btctrl_init_internal(&config);
}
$ grep -rn "move_watchdog_arm\b\|move_watchdog_arm(" --include=*.c --include=*.h .
./ble_bed_service.c:256:static uint32_t wd_diag_refresh_tick = 0;         // last move_watchdog_arm()
./ble_bed_service.c:479:static void move_watchdog_arm(void)
./ble_bed_service.c:790:      move_watchdog_arm();
./ble_bed_service.c:830:      move_watchdog_arm();
$ sed -n 510,561p app.c   (app_is_ok_to_sleep after commit B)
bool app_is_ok_to_sleep(void)
{
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    if (motor_state[i].motor_on) {
      return false;  // A motor is running - keep the supervision loop fast.
    }
  }

  // The switch sampler is polled from the main loop (10 ms cadence), so a
  // sampling session needs the loop awake (DECISIONS.md #41).
  if (switch_sampler_active()) {
    return false;
  }

  // An app hold (DECISIONS.md #45) keeps the loop awake too, whether or not a
  // motor is on: auto-level Step 1 and the pause between axes run with all
  // motors off. On the 2026-11-01 bench every WATCHDOG STOP fell in such a
  // stretch, with the CPU sleeping between passes and the phone's writes
  // reaching the app late and bunched; none with a motor on or on the
  // physical controller, which the two vetoes above already keep awake.
  // Bounded: the watchdog disarms within MOVE_WATCHDOG_MS of the app's last
  // write, so a lost phone cannot pin the CPU awake.
  if (ble_bed_service_app_hold_active()) {
    return false;
  }

  // The motor-enable shadow, not just motor_state[]: set_all_motors_on()
  // energises the outputs without setting motor_state[].motor_on (#40), so a
  // runaway could previously let the CPU sleep between fail-safe samples - the
  // suspected cause of the ALL MOTORS fail-safe taking 271-307 ms against
  // 214-223 ms for the HEAD pair. The shadow is conservative (true until a
  // successful write leaves all four enables low), and needs no I2C.
  if (motor_outputs_maybe_on()) {
    return false;
  }

  // A tilt telemetry burst also needs the loop (DECISIONS.md #33). The
  // sampler runs from app_process_action(), so without this a standalone
  // refresh - app connects, or REFRESH_TILT arrives, with the bed sitting
  // still - would arm a burst and then have no loop to run on: the CPU
  // sleeps, no passes happen, the filter never fills, nothing updates.
  //
  // This pins EM0 for TILT_TELEMETRY_BURST_MS. It is bounded (the burst
  // always expires), and it can only ever happen while a phone is subscribed
  // - tilt_telemetry_tick() will not arm a burst otherwise - so an idle bed
  // with no app connected sleeps exactly as it does today.
  if (tilt_telemetry_burst_active()) {
    return false;
  }

  return true;
}
$ sed -n 812,832p ble_bed_service.c   (AUTO_LEVEL_START case after commit B)
      // app repeats this every 100 ms while held and it arms the same
      // dead-man watchdog a move does.
      note_app_commanding();

      if (!app_auto_level_active) {
        move_watchdog_cancel();
        app_move_active = false;
        if (!bed_auto_level_start(BED_SOURCE_APP)) {
          result = CMD_RESULT_FORCE_STOPPED_OTHER_SOURCE;
          break;
        }
        app_apply_auto_level_start_outputs();
        app_auto_level_active = true;
      }
      // A repeat must NOT re-enter bed_auto_level_start() - that resets the
      // levelling state machine, so at 10 Hz it would never make progress.
      // It only refreshes the watchdog, exactly like a MOVE_START repeat.
      last_move_seq = seq;
      move_watchdog_arm();
      result = CMD_RESULT_ACCEPTED;
      break;
$ sed -n 1016,1048p ble_bed_service.c   (tick: disconnect stop, services, hold-end print)
void ble_bed_service_tick(void)
{
  // Control link dropped mid-command (BLE_GATT_DESIGN.md #5). Deferred out
  // of the disconnect callback; done here, first, before anything else.
  if (disconnect_stop_pending) {
    disconnect_stop_pending = false;
    move_watchdog_cancel();
    app_move_active = false;
    app_auto_level_active = false;
    note_app_stopped_commanding(); // the app is gone - release its half of the latch
    app_stop_all();
  }

  // Drained BEFORE the subscriber check: a Command write is valid from a
  // bonded connection whether or not it subscribed to Bed Status, and its
  // Command Result must still be sent. Only the telemetry notify below is
  // gated on having a subscriber.
  process_pending_command();
  service_move_watchdog();
  service_app_commanding();

#if BLE_DEBUG_WATCHDOG_LOG
  // One line per app hold that ended WITHOUT a watchdog stop, printed here:
  // every stop that ends a hold has already been carried out above.
  if (wd_diag_hold_end_pending) {
    wd_diag_hold_end_pending = false;
    printf("BLE: hold end op=0x%02x seq=%u: %u refreshes, longest refresh gap %u ms, longest between passes %lu ms\r\n",
           wd_diag_hold_end_opcode, wd_diag_hold_end_seq,
           (unsigned)wd_diag_hold_end_refreshes, (unsigned)wd_diag_hold_end_longest_gap_ms,
           (unsigned long)sl_sleeptimer_tick_to_ms(wd_diag_hold_end_longest_between_ticks));
  }
#endif

$ grep -n "bed_active_source() == BED_SOURCE_APP" -A2 ble_bed_service.c   (when the disconnect stop is flagged)
923:    if (bed_active_source() == BED_SOURCE_APP) {
924-      disconnect_stop_pending = true;
925-    }
$ grep -rn "move_watchdog_arm" --include=*.c --include=*.h .
./ble_bed_service.c:245:static bool    move_watchdog_armed = false;
./ble_bed_service.c:256:static uint32_t wd_diag_refresh_tick = 0;         // last move_watchdog_arm()
./ble_bed_service.c:479:static void move_watchdog_arm(void)
./ble_bed_service.c:483:  if (move_watchdog_armed) {
./ble_bed_service.c:515:  move_watchdog_armed = true;
./ble_bed_service.c:525:  if (move_watchdog_armed) {
./ble_bed_service.c:535:  move_watchdog_armed = false;
./ble_bed_service.c:574:  if (!move_watchdog_armed) {
./ble_bed_service.c:790:      move_watchdog_arm();
./ble_bed_service.c:830:      move_watchdog_arm();
./ble_bed_service.c:1013:  return move_watchdog_armed;
$ grep -rn "USE_INACCURATE_SLEEP_CLOCK" <SDK>/bluetooth_le_controller <SDK>/bluetooth_le_host <SDK>/bluetooth_common <SDK>/platform_core config autogen --include=*.h --include=*.c --include=*.slcc --include=*.jinja
<SDK>/bluetooth_le_controller/inc/sl_btctrl_linklayer_defs.h:91:  SL_BTCTRL_CONFIG_FLAG_USE_INACCURATE_SLEEP_CLOCK = 0x80000,
<SDK>/bluetooth_le_controller/src/sl_btctrl_init.c:292:  if (!(config->flags & SL_BTCTRL_CONFIG_FLAG_USE_INACCURATE_SLEEP_CLOCK)) {
<SDK>/bluetooth_le_controller/src/sl_btctrl_init.c:693:  bool inaccurate_clock_enabled = sl_btctrl_get_config_flags(SL_BTCTRL_CONFIG_FLAG_USE_INACCURATE_SLEEP_CLOCK);
$ date; git status
Sun Nov  1 15:44:45 PST 2026
On branch main
Your branch is ahead of 'origin/main' by 2 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

no changes added to commit (use "git add" and/or "git commit -a")
```

### d.2 Builds (verbatim)

Method as in the previous handoffs: a scratchpad helper puts the Silicon
Labs ninja and cmake on `PATH`, runs `cmake --build --preset default_config`
in `cmake_gcc/`, saves all output plus `exit N`, prints it, then
`grep -n -i 'warning\|error'` over it (empty = none). Flags `-Wall -Wextra`
(`cmake_gcc/tilt_bed_controller.cmake:363-364`).

Baseline (source = `c945146`; docs for A already edited):

```
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
```

After commit A `0993ee9`:

```
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
```

Part B edits, `BLE_DEBUG_WATCHDOG_LOG` 1 (before the B docs and commit):

```
[1/5] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_control.c.obj
[2/5] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/5] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/5] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[5/5] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

Temporary `sed` to `BLE_DEBUG_WATCHDOG_LOG 0` (`ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 0`):

```
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

`sed` back to 1 (`ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1`) — size `text 244988 data 780 bss 72480`:

```
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

After commit B `75b0a61` (the image now in `cmake_gcc/build/`):

```
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
```

No build produced a warning or an error.

### d.3 The 2026-11-01 bench log (user's run), with line numbers

```
-rw-r--r-- 1 adunc 197610 7615 Nov  1 14:56 C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/PuTTY Logs/autolevel_stall_bench.log
183 C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/PuTTY Logs/autolevel_stall_bench.log
     1	Welcome to the Tilt Bed application
     2	Application Version 1.1.0 
     3	Accelerometer is sampling ...
     4	NVM3 initialized successfully
     5	Loaded Roll Angle: 0
     6	Loaded Pitch Angle: 0
     7	Loaded Raise Collision Sensitivity: 550
     8	Loaded Lower Collision Sensitivity: 275
     9	BLE SM: configure -> 0x0000
    10	BLE SM: store_bonding_configuration -> 0x0000
    11	BLE SM: set_bondable_mode(1) -> 0x0000
    12	BLE: connection_opened conn=1 bonding=0x02
    13	BLE: connection_parameters conn=1 interval=24 latency=0 timeout=72 security_mode=0
    14	BLE: characteristic_status DROPPED conn=1 client_config_flags=0x0001 (matches neither control nor viewer connection)
    15	BLE: connection_parameters conn=1 interval=24 latency=0 timeout=100 security_mode=0
    16	BLE: connection_parameters conn=1 interval=24 latency=0 timeout=100 security_mode=1
    17	BLE: cccd_seed conn=1 bed_status=0x0001 sc=0x0000
    18	Max Current for Motor 0: 703
    19	Max Current for Motor 1: 21
    20	Max Current for Motor 2: 14
    21	Max Current for Motor 3: 768
    22	Step 1: ROLL and PITCH check
    23	Step 2: ROLL Not Level
    24	Latch
    25	Pulse on...
    26	Pulse off...
    27	ROLL Leveled successfully...
    28	BLE: WATCHDOG STOP op=0x03 seq=59: 300 ms since last refresh (limit 300 ms)
    29	  last write received 301 ms ago (op=0x03 seq=59); last refresh received->processed 1 ms
    30	  since last refresh: 39 passes, longest pass 17 ms, longest between passes 28 ms, this pass 0 ms so far
    31	  last 8 refresh gaps, oldest first (ms): 90,119,89,90,88,119,90,150
    32	Step 1: ROLL and PITCH check
    33	Step 2: PITCH Not Level
    34	Leveling...
    35	Max Current for Motor 0: 713
    36	Max Current for Motor 1: 823
    37	Max Current for Motor 2: 1101
    38	Max Current for Motor 3: 726
    39	Latch
    40	Pulse off...
    41	PITCH Leveled successfully...
    42	Exit auto-level
    43	ROLL is disabled and Pitch is level
    44	Max Current for Motor 0: 724
    45	Max Current for Motor 1: 33
    46	Max Current for Motor 2: 24
    47	Max Current for Motor 3: 716
    48	Max Current for Motor 0: 733
    49	Max Current for Motor 1: 764
    50	Max Current for Motor 2: 22
    51	Max Current for Motor 3: 25
    52	Step 1: ROLL and PITCH check
    53	BLE: WATCHDOG STOP op=0x03 seq=193: 301 ms since last refresh (limit 300 ms)
    54	  last write received 301 ms ago (op=0x03 seq=193); last refresh received->processed 0 ms
    55	  since last refresh: 27 passes, longest pass 2 ms, longest between passes 28 ms, this pass 1 ms so far
    56	  last 1 refresh gaps, oldest first (ms): 104
    57	Step 1: ROLL and PITCH check
    58	BLE: WATCHDOG STOP op=0x03 seq=208: 300 ms since last refresh (limit 300 ms)
    59	  last write received 301 ms ago (op=0x03 seq=208); last refresh received->processed 0 ms
    60	  since last refresh: 23 passes, longest pass 17 ms, longest between passes 27 ms, this pass 0 ms so far
    61	  last 8 refresh gaps, oldest first (ms): 180,0,0,0,207,0,148,28
    62	Step 1: ROLL and PITCH check
    63	BLE: WATCHDOG STOP op=0x03 seq=214: 300 ms since last refresh (limit 300 ms)
    64	  last write received 301 ms ago (op=0x03 seq=214); last refresh received->processed 1 ms
    65	  since last refresh: 28 passes, longest pass 2 ms, longest between passes 27 ms, this pass 0 ms so far
    66	  last 5 refresh gaps, oldest first (ms): 20,2,1,1,112
    67	Step 1: ROLL and PITCH check
    68	Step 2: ROLL Not Level
    69	Leveling...
    70	Max Current for Motor 0: 755
    71	Max Current for Motor 1: 832
    72	Max Current for Motor 2: 1056
    73	Max Current for Motor 3: 766
    74	Latch
    75	Pulse off...
    76	ROLL Leveled successfully...
    77	Axis set to PITCH
    78	Leveling...
    79	Max Current for Motor 0: 730
    80	Max Current for Motor 1: 767
    81	Max Current for Motor 2: 1098
    82	Max Current for Motor 3: 789
    83	Latch
    84	PITCH Leveled successfully...
    85	Exit auto-level
    86	Max Current for Motor 0: 728
    87	Max Current for Motor 1: 30
    88	Max Current for Motor 2: 23
    89	Max Current for Motor 3: 749
    90	Max Current for Motor 0: 19
    91	Max Current for Motor 1: 33
    92	Max Current for Motor 2: 1100
    93	Max Current for Motor 3: 733
    94	Step 1: ROLL and PITCH check
    95	Step 2: ROLL Not Level
    96	Leveling...
    97	Max Current for Motor 0: 701
    98	Max Current for Motor 1: 778
    99	Max Current for Motor 2: 1123
   100	Max Current for Motor 3: 733
   101	Latch
   102	Pulse off...
   103	ROLL Leveled successfully...
   104	Axis set to PITCH
   105	Leveling...
   106	Max Current for Motor 0: 696
   107	Max Current for Motor 1: 825
   108	Max Current for Motor 2: 1112
   109	Max Current for Motor 3: 732
   110	Latch
   111	PITCH Leveled successfully...
   112	Exit auto-level
   113	Max Current for Motor 0: 758
   114	Max Current for Motor 1: 24
   115	Max Current for Motor 2: 18
   116	Max Current for Motor 3: 774
   117	Max Current for Motor 0: 23
   118	Max Current for Motor 1: 36
   119	Max Current for Motor 2: 1112
   120	Max Current for Motor 3: 749
   121	Step 1: ROLL and PITCH check
   122	BLE: WATCHDOG STOP op=0x03 seq=235: 300 ms since last refresh (limit 300 ms)
   123	  last write received 301 ms ago (op=0x03 seq=235); last refresh received->processed 0 ms
   124	  since last refresh: 31 passes, longest pass 16 ms, longest between passes 28 ms, this pass 0 ms so far
   125	  last 8 refresh gaps, oldest first (ms): 104,119,90,89,122,56,209,0
   126	Step 1: ROLL and PITCH check
   127	BLE: WATCHDOG STOP op=0x03 seq=241: 300 ms since last refresh (limit 300 ms)
   128	  last write received 301 ms ago (op=0x03 seq=241); last refresh received->processed 0 ms
   129	  since last refresh: 32 passes, longest pass 2 ms, longest between passes 27 ms, this pass 0 ms so far
   130	  last 5 refresh gaps, oldest first (ms): 19,1,0,0,83
   131	Step 1: ROLL and PITCH check
   132	Step 2: ROLL Not Level
   133	Leveling...
   134	Max Current for Motor 0: 757
   135	Max Current for Motor 1: 745
   136	Max Current for Motor 2: 1174
   137	Max Current for Motor 3: 781
   138	Latch
   139	Pulse off...
   140	ROLL Leveled successfully...
   141	Axis set to PITCH
   142	Leveling...
   143	Max Current for Motor 0: 717
   144	Max Current for Motor 1: 822
   145	Max Current for Motor 2: 1122
   146	Max Current for Motor 3: 724
   147	Latch
   148	PITCH Leveled successfully...
   149	Exit auto-level
   150	BLE: connection_closed conn=1 reason=0x1013
   151	BLE: connection_opened conn=2 bonding=0x02
   152	BLE: connection_parameters conn=2 interval=24 latency=0 timeout=72 security_mode=0
   153	BLE: characteristic_status DROPPED conn=2 client_config_flags=0x0001 (matches neither control nor viewer connection)
   154	BLE: connection_parameters conn=2 interval=24 latency=0 timeout=100 security_mode=0
   155	BLE SM: confirm_bonding conn=2 handle=0x02 (REPLACING existing bond) -> REJECTED (not in settings mode)
   156	BLE SM: bonding_failed conn=2 reason=0x1205
   157	BLE: closing conn=2 - pairing refused
   158	BLE: connection_closed conn=2 reason=0x1016
   159	BLE: connection_opened conn=3 bonding=0x02
   160	BLE: connection_parameters conn=3 interval=24 latency=0 timeout=72 security_mode=0
   161	BLE: characteristic_status DROPPED conn=3 client_config_flags=0x0001 (matches neither control nor viewer connection)
   162	BLE: connection_parameters conn=3 interval=24 latency=0 timeout=100 security_mode=0
   163	BLE SM: confirm_bonding conn=3 handle=0x02 (REPLACING existing bond) -> REJECTED (not in settings mode)
   164	BLE SM: bonding_failed conn=3 reason=0x1205
   165	BLE: closing conn=3 - pairing refused
   166	BLE: connection_closed conn=3 reason=0x1016
   167	SW: 0x0008 down (bounces 0)
   168	Max Current for Motor 0: 0
   169	Max Current for Motor 1: 5
   170	Max Current for Motor 2: 1
   171	Max Current for Motor 3: 3
   172	SW: 0x0008 up (bounces 0)
   173	BLE: connection_opened conn=4 bonding=0x02
   174	BLE: connection_parameters conn=4 interval=24 latency=0 timeout=72 security_mode=0
   175	BLE: characteristic_status DROPPED conn=4 client_config_flags=0x0001 (matches neither control nor viewer connection)
   176	BLE: connection_parameters conn=4 interval=24 latency=0 timeout=100 security_mode=0
   177	BLE SM: confirm_bonding conn=4 handle=0x02 (REPLACING existing bond) -> ACCEPTED (settings mode)
   178	BLE: connection_parameters conn=4 interval=24 latency=0 timeout=100 security_mode=1
   179	BLE: cccd_seed conn=4 bed_status=0x0001 sc=0x0000
   180	BLE SM: bonded conn=4 bonding=0x02 security_mode=1
   181	SW: 0x0008 down (bounces 0)
   182	Settings mode: exit
   183	SW: 0x0008 up (bounces 0)
```

### d.4 Failed and anomalous commands

1. Recreating the build helper by copying the previous session's script
   failed (that scratchpad file no longer exists); the script was then
   written fresh with the same contents and paths:

   ```
   $ sed 's#scratchpad"#scratchpad/aa"#' ".../scratchpad/build.sh" > ".../scratchpad/aa/build.sh"; grep -n '^S=' ".../scratchpad/aa/build.sh"; sh ".../scratchpad/aa/build.sh" baseline
   sed: can't read C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/0e56dbb1-17a9-421f-b1eb-bcd73dc7e488/scratchpad/build.sh: No such file or directory
   ```

2. Reading `CMU_LF_ClockPrecisionGet()`: `em_cmu.c` has two definitions
   (series 1 and 2), so the line-number variable held two values and the
   `sed` range failed (exit 1). The series-2 definition (line 2618) was then
   read directly; its excerpt is in d.1.

   ```
   $ F="<SDK>/platform_core/platform/emlib/src/em_cmu.c"; n=$(grep -n "^uint16_t CMU_LF_ClockPrecisionGet" "$F" | cut -d: -f1); sed -n "${n},$((n+45))p" "$F"
   /usr/bin/bash: line 1: 2618
   10609: syntax error in expression (error token is "10609")
   ```

   (The same command first printed the `sl_clock_manager_hal_s2.c` oscillator
   precision function, lines 120-170, which is not cited.)

3. **Anomaly, unexplained:** in the last evidence batch,
   `grep -rn "move_watchdog_arm(" --include=*.c --include=*.h .` printed only
   `./ble_bed_service.c:256:...// last move_watchdog_arm()` (d.1), where
   `:479`, `:790` and `:830` should also match. Re-run immediately as
   `grep -rn "move_watchdog_arm" ...`, it lists every expected line (d.1, the
   next command). The claims in b.2.1 and V4 rest on the re-run and on the
   earlier `grep -n` after commit B.

## e. VERIFIED vs ASSERTED

### VERIFIED (evidence attached)

| # | Claim | Evidence |
|---|---|---|
| V1 | Start state as the prompt expected: HEAD `c945146` = `origin/main` after `git fetch`; only `.projectlinkstore` modified; the prompt file untracked | d.1 `git status`, `git fetch` (exit 0), `git rev-parse HEAD origin/main` |
| V2 | Every figure in the prompt's Evidence section, and the line numbers it cites | d.3, the log with line numbers; table in b.1 |
| V3 | No pre-ship list exists in the repo | d.1 `grep -rn -i "pre-ship\|..."` (three per-item notes only) |
| V4 | `move_watchdog_armed` set only at `ble_bed_service.c:515` (in `move_watchdog_arm()`, called at `:790`, `:830`), cleared only at `:535` (in `move_watchdog_cancel()`, called at `:582, :615, :666, :778, :799, :817, :838, :1022`); no other file calls `move_watchdog_arm` | d.1/d.1 greps after commit B (`grep -n ... ble_bed_service.c`; `grep -rn move_watchdog_arm`) |
| V5 | The disconnect stop is flagged only when the active source is APP | d.1 `ble_bed_service.c:923-925` |
| V6 | All five `ARBITRATE_FORCE_STOP` branches call `app_stop_movement()` (B4's comment is accurate) | d.1 `grep -n ... bed_control.c` (104/106, 117/118, 132/134, 145/146, 158/159) |
| V7 | The veto is in `app_is_ok_to_sleep()` before `return true` | d.1 `sed -n 510,561p app.c` |
| V8 | The hold-end print runs after the disconnect stop, `process_pending_command()`, `service_move_watchdog()`, `service_app_commanding()`, and before the subscriber early return | d.1 `sed -n 1016,1048p ble_bed_service.c` |
| V9 | LF clock = LFRCO (default and the EM23 and RTCC branches), LFXO disabled, LFRCO precision default | d.1 `config/sl_clock_manager_tree_config.h:71-78,173-181,195-203`; `config/sl_clock_manager_oscillator_config.h:129-133,280-298` |
| V10 | The part supports LFRCO high precision; it has an RTCC; the sleeptimer defaults to the RTCC | d.1 `bgm220sc22hna.h` grep (194, 407, 419-420); `em_cmu.h:153-155`; `sli_sleeptimer.h:48-51`; `config/sl_sleeptimer_config.h:44-56` |
| V11 | Project clock configs = SDK BGM220SC22HNA module defaults; SDK's BGM220PC22HNA default uses LFXO | d.1 module grep and `diff --strip-trailing-cr` |
| V12 | No SCA setting in `config/` or `autogen/` | d.1 grep, no output |
| V13 | SCA chain: controller <- sleeptimer <- clock manager RTCCCLK <- `CMU_LF_ClockPrecisionGet`, 0xFFFF for LFRCO without HIGHPRECEN, 500 with it | d.1 `sl_btctrl_init.c:284-300`, `sl_sleeptimer_hal_rtcc.c:281-286`, `sl_clock_manager_hal_s2.c:438-448`, `em_cmu.c:2618-2650` |
| V14 | Controller adds an EM1 requirement when SCA > 500 and `USE_INACCURATE_SLEEP_CLOCK` is not set; no visible source sets that flag; defaults come from `sl_btctrl_init_config()` (declared, not implemented in source) | d.1 `sl_btctrl_linklayer_defs.h:58-61,88-94`; d.1 `grep -rn USE_INACCURATE_SLEEP_CLOCK` (definition + two checks); d.1 `sl_btctrl_linklayer.h:395`, `sl_btctrl_init.c:677-685` |
| V15 | C3 settings and their documented meanings | d.1 excerpts (`sl_power_manager_config.h:46-53,86-94`; `sl_rail_util_power_manager_init_config.h:34-41`; oscillator `:94-106`; slcp `:477-481`; `sl_power_manager.c:350-368`) |
| V16 | Every build exited 0 with no warning or error: baseline, after A, B (flag 1), B (flag 0, temporary), B (flag 1 again), after B | d.2 |
| V17 | Flag restored to 1 and committed as 1 | d.2 `grep -n` after each `sed`; c.2 diff (no change to the define line) |
| V18 | Host unit test (links `bed_control.c`): 30 PASS, `0 check(s) failed`, exit 0 | d.1 (full output) |
| V19 | Image after B: text 244988, data 780, bss 72480 | d.2 `arm-none-eabi-size` |

### ASSERTED (reasoned or compiled, nothing observed)

- **AS1.** That B1 removes the stalls. This is the hypothesis under test.
- **AS2.** That the hold-end line looks like b.4 D1, with B ~0-1 ms and G
  ~100-130 ms.
- **AS3.** That nothing else changes: a physical hold and an idle connected
  bed are unaffected (from code, b.4 D2/D3).
- **AS4.** That the device never entered EM2 on the bench (depends on the
  closed `sl_btctrl_init_config()` not setting the flag; and on nothing else
  setting `LFRCO->CFG.HIGHPRECEN`).
- **AS5.** The link-layer mechanism in C4 item 3 (LFRCO-based timing error
  causing missed packets) — a hypothesis.
- **AS6.** That a host-queue stall is unlikely (C4 item 4; depends on
  `sl_bt_step()` draining pending events each pass).
- **AS7.** The averaging-window figures in DECISIONS #45 (#29's 630 passes/s
  under different conditions; the bench's 77-130 passes/s).
- **AS8.** That `sl_sleeptimer_restart_timer()` never fails here (unchanged
  code; its failure would leave the veto on until the next stop).
- **AS9.** The BGM220S module has no 32 kHz crystal (inferred from the SDK's
  module defaults only).

## f. QUESTIONS FOR REVIEW

1. **No pre-ship list exists.** Ruling 5 says to add the I2CSPM 300 s
   timeout "to the pre-ship list wherever that list lives". There is none
   (V3). I recorded the item, with the other three known pre-ship items, in
   the SESSION_LOG record entry, and did not invent a new document. Where
   should the list live — a section in `docs/MIGRATION_STATUS.md`, or a new
   `docs/PRE_SHIP.md`?
2. **The EM2 hypothesis vs the code.** The LFRCO is in default precision, so
   the SDK tells the BLE controller 0xFFFF ppm, and the controller then
   (visibly) forbids EM2 with an EM1 requirement (b.3 C2). If the closed
   default config does not set `USE_INACCURATE_SLEEP_CLOCK`, the bed has been
   sleeping in EM1, never EM2. B1 is still a valid test of "CPU sleep causes
   the stalls", but the record now says "while the CPU sleeps", and DECISIONS
   #45 notes this. Do you want the hypothesis restated?
3. **Confirm the clock accuracy on hardware?** A one-line boot print of
   `sl_sleeptimer_get_clock_accuracy()` (expected `0xffff`) would turn AS4's
   first half into a fact. Not added: not asked for.
4. **LFRCO high-precision mode.** Setting `SL_CLOCK_MANAGER_LFRCO_PRECISION`
   to `cmuPrecisionHigh` through the Studio configurator would give the
   controller a 500 ppm clock, lift the EM1 hold, and recalibrate the LFRCO
   against the HFXO. It is a `config/` change (configurator only), so not
   done. It would also change idle power (EM2 becomes possible). A separate
   prompt, and a bench run with B1 removed, would show whether the clock is
   the cause.
5. **Two-source inhibit ends a hold with WATCHDOG_STOP.** When the wall
   switch has taken the bed, the app's repeats are refused
   (`FORCE_STOPPED_OTHER_SOURCE`, `ble_bed_service.c:746`) and do not refresh
   the watchdog, so ~300 ms later the app also gets `WATCHDOG_STOP`
   ("Connection stalled") although the link is fine. This was already the
   behaviour before this session (not changed). With B1 the CPU stays awake
   for those ~300 ms. Is the app's message in that case acceptable?
6. **The hold-end line during a hold that is replaced.** An AUTO_LEVEL_START
   arriving while a move hold is armed ends the move hold (hold-end line,
   `op=0x01`) and starts an auto-level hold. This is reported as two holds.
   Fine, or should a replacement be marked?
7. **Wording of the bench reading.** The prompt says "steady check after an
   axis finishes"; in code it is the 1000 ms axis-switch pause. And the loop
   woke about every 8-13 ms on average, not once per connection event (b.1).
   Recorded precisely; neither changes the conclusion.

## g. NOT DONE

- **Nothing asked for was skipped**, except the pre-ship list itself (QUESTIONS 1).
- **Bench runs:** none (the user drives the bench). Section b.4 is a
  prediction.
- **No configuration changed** (part C is report-only). The LFRCO precision
  question is QUESTIONS 4.
- **Raw output of file-reading commands** used while planning (reading the
  prompt and the docs; `sed`/`grep` over `ble_bed_service.c`, `app.c`,
  `bed_control.c`; the first round of SDK/config reads for part C) is not
  reproduced in full where it was not captured in the session log. The
  excerpts in d.1 were re-run for every claim in sections b.3 and e. The
  failed commands are in d.4. Commands that only edited scratch files for
  this handoff are not reproduced.
- `SWITCH_SAMPLER_DIAG` and `BLE_DEBUG_WATCHDOG_LOG` are both 1 (pre-ship 0).
