# fw_HANDOFF_2026-10-23_01 — Prompt Y: watchdog-stop diagnostics, Prompt W follow-ups

Firmware repo session of 2026-10-23 (system date `Fri Oct 23 2026`, checked
with `date` at the start and again before writing this file). Implements
`docs/prompts/PROMPT_Y_watchdog-diagnostics-and-followups.md` (reviewer,
written 2026-10-18), parts A to D. Three work commits (A, B, C), each built
from the command line, then this handoff plus the `docs/SESSION_LOG.md` entry
in a separate, last commit.

Nothing here has been run on hardware in this session. Everything about
behaviour is ASSERTED; see section e.

## a. Header

| Field | Value |
|---|---|
| Repo | `tilt_bed_controller` (firmware) |
| Branch | `main` |
| HEAD before the session | `18d8378` (level with `origin/main`; verified with `git rev-parse HEAD origin/main`, both `18d83780dcffedd64f1d18544808fa01024ba71b`) |
| Work commits | `f6614c8` (A, docs), `17073a3` (B), `c7d743d` (C) |
| HEAD after the work commits | `c7d743d` |
| Handoff commit | the commit that adds this file and the SESSION_LOG entry (its SHA cannot be quoted inside itself) |
| Push | `git push` to `origin/main` after the handoff commit (standing rule). Its result is reported to the user in chat; if it fails, a follow-up commit records the raw error here. |

`git status` at the end of the work (after the three work commits and the
`docs/SESSION_LOG.md` append, immediately before this file was written):

```
$ git status   (after the three work commits and the SESSION_LOG append, before the handoff commit)
On branch main
Your branch is ahead of 'origin/main' by 3 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

no changes added to commit (use "git add" and/or "git commit -a")
```

`.projectlinkstore` is a Simplicity Studio working file, modified before the
session started and never committed (the prompt says never to commit it).

## b. Plans, designs and analysis

### b.0 What the prompt asked, and what was done

| Part | Asked | Done | Commit |
|---|---|---|---|
| A1 | Commit the reviewer's CLAUDE.md edit as it is; show its diff | Done, unedited | `f6614c8` |
| A2 | Correct MIGRATION_STATUS.md's Stage C "Not started" to match #34/#42/#43, with dates and commits; keep genuinely gated items marked | Done (four places, see b.1) | `f6614c8` |
| A3 | Confirm the Prompt W bench result from the log; look up 0x1022; record as VERIFIED under #43, in REGRESSION.md, in SESSION_LOG | Done; one item rests partly on the user's report (b.1.3) | `f6614c8` + SESSION_LOG |
| A4 | New DECISIONS entry for user decision 3 | DECISIONS #44 | `f6614c8` |
| A5 | REGRESSION.md items for the pairing window and STOP during a settings hold, unchecked | 7.10a-d and 7.11 | `f6614c8` |
| B | `settings_mode_tick()` entry also requires `switch_state_fresh()` | Done, one condition | `17073a3` |
| C1-C4 | Watchdog-stop diagnostics behind `BLE_DEBUG_WATCHDOG_LOG` (default 1) | Done | `c7d743d` |
| C5 | Report only: what can block the main loop on the auto-level path; does the CPU sleep in Step 1 | b.4 below; no code | — |
| D | Expected lines for three bench checks | b.5 below | — |

`BED_BENCH_PROVOKE_RUNAWAY` stays 0 (`switch_input.c`) and
`BLE_BENCH_WIPE_BONDS` stays 0 (`ble_bed_service.c:70`); neither was touched.
Nothing added blocks (DECISIONS #27): see b.3.6.

### b.1 Part A — docs

#### b.1.1 A1: CLAUDE.md

The working copy already carried the reviewer's edit (the "No BLE glue in this
phase" bullet replaced by a "BLE status" bullet). It was committed exactly as
found; the diff is in c.1 and in d (`git diff CLAUDE.md`, run before
committing). The new bullet itself says MIGRATION_STATUS.md "is being
corrected"; A2 is that correction, in the same commit.

#### b.1.2 A2: MIGRATION_STATUS.md

The stale claim was in four places, not three: besides lines ~51, ~70 and
~100-104, the Phase 6 checklist item (lines 31-42) also said "Stage C (the
motor-driving command path) remains hard-gated on this pass". All four are
corrected, each marked "corrected 2026-10-23".

What the record actually shows (each item has a source):

- **The gate used for Stage C** was not Phase 6 / `REGRESSION_RECORD_D1.md`
  (still blank, and it can no longer be run as scoped — it was the pre-BLE
  isolation pass). It was the post-Stage-B full pass in
  `docs/REGRESSION_RECORD_PRE_STAGEC.md`: date 07/19/2026, commit `af369f2`,
  **Debug** build, phone subscribed throughout, recorded in commit `29fdf0d`
  (SESSION_LOG entry "2026-08-01 — Backfill").
- **Stage C built:** DECISIONS #34, commit `ab12d09`, 2026-08-02
  (`git log --date=short`, d.1).
- **First bench run** found the 10 Hz app-move stutter; fixed in `a22406f`
  (2026-08-02, DECISIONS #35); DECISIONS #36 says "#35's stutter fix is
  bench-confirmed", and that the disconnect stop during auto-level was
  "bench-confirmed".
- **Auto-level hold-to-run and watchdogged:** `5351aef`, 2026-08-04
  (DECISIONS #36).
- **Later bench-driven fixes** #37-#41, 2026-08-04 to 2026-10-18.
- **Pairing window** #42 (`944f19e`, 2026-10-18) and **bond store policy +
  closing refused pairings** #43 (`c6ee6b0`, 2026-10-18): bench results
  recorded 2026-10-18 (Prompt V) and now 2026-10-18 (Prompt W, A3).

Still genuinely open, and kept marked as such:

- A full REGRESSION.md pass on the **current** firmware (which now contains
  the Stage C, sampler and pairing-window items) has not been run.
- The **Release-config confirmation pass** is still owed before Stage C ships
  (`REGRESSION_RECORD_PRE_STAGEC.md`'s own header note; every bench run so
  far, including PRE_STAGEC, is Debug).
- The watchdog's stall case and the intermittent `WATCHDOG_STOP` during app
  auto-level holds (Part C's subject) are under investigation.

The Stage C checkbox is changed from `[ ]` to `[x]` with "Built and
bench-tested". I judged that "built and bench-tested" is supported (it has
been driven from the app on the bench in every session since 2026-08-02) but
it is not "passed regression"; the item says so explicitly. See QUESTIONS 7.

#### b.1.3 A3: Prompt W bench result

Source: `C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\prompt_w_bench.log`,
179 lines, 5702 bytes, dated Oct 18 16:09; reproduced in full, with line
numbers, in d.3. Firmware: the Prompt W build, i.e. the code of `840d7b8` (the
previous handoff's bench steps named `9ba0ca3`, which adds three docs files
only — `git show --stat`, d.1). The log does not identify the commit itself.
Phone: Samsung Galaxy S23 (SM-S911U), per the prompt (not the S10 used for
Prompt V). The log has no timestamps.

Each item the prompt asked me to confirm, against the log:

| Prompt's claim | Log lines | Confirmed? |
|---|---|---|
| `store_bonding_configuration -> 0x0000` | 12 | Yes |
| Bonded reconnect with no `confirm_bonding` | 14-19: `connection_opened conn=1 bonding=0x01`, `security_mode=0` then `=1`, `cccd_seed`; no `confirm_bonding` | Yes |
| Refusal with `closing conn=2 - pairing refused` and `connection_closed reason=0x1022` | 25-32: `REJECTED (not in settings mode)` (29), `bonding_failed conn=2 reason=0x1205` (30), `BLE: closing conn=2 - pairing refused` (31), `BLE: connection_closed conn=2 reason=0x1022` (32). Exactly one `connection_closed` for conn=2 | Yes |
| Acceptance in settings mode | 33-46: BED LIGHTS held (33-38; entry prints nothing), `confirm_bonding conn=3 handle=0x01 (REPLACING existing bond) -> ACCEPTED (settings mode)` (43), `security_mode=1` (44), `cccd_seed` (45), `bonded conn=3` (46); `Settings mode: exit` (48) | Yes |
| STOP during an UP ALL hold wrote nothing | 54-56 and 57-59: two UP ALL holds, each `SW: 0x0001 down`, `2 second timer start`, `SW: 0x0001 up`, and no `Writing values to NVM3...` and no `Settings mode: exit` | **The "wrote nothing" part, yes. That STOP was pressed in the app during these holds, and that they lasted past 2 s, is not visible in the log** — STOP prints nothing by design (previous handoff E2 step 2), and there are no timestamps. Recorded as the user's report. |
| The next full hold stepped to 350/250 | 60-69: `Writing values to NVM3...` (63), both `Successfully stored ...` (64-65), `Stored Raise Sensitivity is: 350` (66), `Stored Lower Sensitivity is: 250` (67). Loaded values were 550/275 (9-10), i.e. MED -> HIGH | Yes |
| DOWN ALL restored 550/275 | 70-79: `Stored Raise Sensitivity is: 550` (76), `Stored Lower Sensitivity is: 275` (77) | Yes |

**0x1022.** `sl_status.h:286` (SDK 2026.6.0, `platform_core/platform/common/inc/sl_status.h`):

```
#define SL_STATUS_BT_CTRL_LL_RESPONSE_TIMEOUT ((sl_status_t)0x1022) ///< Connection terminated due to link-layer procedure timeout.
```

(0x1016, the predicted value, is `SL_STATUS_BT_CTRL_CONNECTION_TERMINATED_BY_LOCAL_HOST`
at `:274`; 0x1013, Prompt V's value, is `..._REMOTE_USER_TERMINATED` at `:271`.)
Meaning: the controller ended the connection because a link-layer procedure
did not get its response in time. The order of lines shows the bed's
`sl_bt_connection_close()` ran first (line 31) and the phone did not
terminate it (no 0x1013). Which LL procedure timed out is not visible. One
reading, **ASSERTED**: the bed's LL termination needs the phone to acknowledge
it, and the phone, which was itself handling the pairing failure, did not do
so in time, so the controller reported a response timeout instead of a clean
local termination. Either way the refused phone no longer holds a connection,
which is what #43 part 2 is for. REGRESSION.md 7.10b now accepts 0x1022,
0x1016 or 0x1013, with exactly one `connection_closed`.

**Two things in the log I did not analyse** (outside the prompt's list, noted
so they are not lost): lines 83-86, 89-91, 137-139 and 176-179 show the
repeated `Step 1: ROLL and PITCH check` that Part C is about; and lines
146-152 show a collision trip during an auto-level (`Tripped on steady!`,
`Securit margin: 550`, deltas 140/199/129/87, `Once`) followed by a fresh
`Step 1`. That trip has not been examined.

Recorded in: DECISIONS #43 "Bench result (2026-10-18)" (part 1 and 2 items),
DECISIONS #44 "Bench result (2026-10-18)" (the settings-hold items),
REGRESSION.md §7 recorded-result table, and the SESSION_LOG entry.

#### b.1.4 A4: DECISIONS #44

Next free number was 44 (the last heading was `## 43.`, `docs/DECISIONS.md`).
Content: the decision (STOP in settings mode abandons any hold in progress,
writes nothing, stays in settings mode; the next hold needs its full 2 s),
the implementation in `840d7b8` (`settings_mode_cancel_holds()`,
`settings_mode.c:120-126`, called from `app_stop_all()` when in settings mode,
`bed_actions.c:221-228`), the three callers of `app_stop_all()` (STOP_ALL
`ble_bed_service.c:535`, watchdog expiry `:505`, control-phone disconnect
`:883` — line numbers as of `840d7b8`/HEAD before this session), the
reviewer's acceptance that cancelling on any full stop is correct, and the
bench result. The release-branch and entry-condition fixes in the same commit
are mentioned as related bug fixes, not as part of the decision. Full text in
c.1.

#### b.1.5 A5: REGRESSION.md

Added to §7, after 7.9 and before the "BLE link stays up" item, all unchecked:

- **7.10 Pairing window** (#42/#43), with `BLE_DEBUG_SECURITY_LOG` 1 and the
  phone model noted: 7.10a bonded reconnect (no `confirm_bonding`), 7.10b
  refusal and the bed closes the link (exactly one `connection_closed`;
  0x1022, 0x1016 or 0x1013 acceptable), 7.10c acceptance in settings mode,
  7.10d reconnect after exit and the app can drive the bed.
- **7.11 STOP during a settings hold** (#44): UP ALL, STOP in the app before
  2 s, hold past 2 s, release; nothing written, settings mode kept, amber
  keeps blinking; the next hold needs a full 2 s; repeat for DOWN ALL and
  AUTO LEVEL.

And a "Recorded result, 2026-10-18 (Prompt W ...)" table after the existing
Prompt V table, mapping the run onto 7.10a-c and 7.11 (UP ALL only), with
7.10d marked "not in this log". The Prompt V table's sentence "This section
has no pairing-window checklist item yet" is left as it was (it was true when
written); the new table says the items were added on 2026-10-23.

### b.2 Part B — settings-mode entry requires fresh sampler state

**Change** (`settings_mode.c:276-278`, commit `17073a3`): the entry condition

```c
if ((switch_pressed == true) && (active_switch == SWITCH_BED_LIGHT_VALUE) && (!in_settings_mode)
    && switch_state_fresh() && ((switch_sampler_down() & SWITCH_BED_LIGHTS) != 0)){
```

`switch_state_fresh()` (`switch_input.c:464-469`) is true when the sampler is
idle, or when it is active and has had a good read within
`SWITCH_STATE_STALE_MS` = 100 ms (`switch_input.c:56`). The motion fail-safe
uses it the same way (`switch_input.c:818-819`). `settings_mode.c` already
included `switch_input.h`, so no include was added. Comment updated to say
why. Nothing else changed.

**What it does, precisely.** While the sampler's data is stale, the entry
block is skipped: no 5 s timer is *started* and settings mode is not
*entered*. It does not *stop* a 5 s timer that was already running — nothing
in this block ever stopped it; the only cancel sites are the BED LIGHTS
release edge (`switch_input.c:671-674`) and the double-press handler
(`switch_input.c:975-976`). So if reads fail mid-hold:

- the timer keeps running and its callback may set `timeout_5000ms`;
- entry cannot complete while the data is stale;
- when reads recover, either BED LIGHTS is still down (fresh, debounced) and
  the next tick enters settings mode immediately on the already-set flag — a
  correct outcome, the user is still holding it — or the recovered reads
  deliver the release edge, which calls `settings_mode_cancel_entry()` and
  clears both timer and flag.

The prompt's wording "stale sampler data cannot hold the entry timer
running" is therefore slightly different from what one condition can do: it
stops stale data from *completing* the entry, not from leaving the timer
running. I implemented the one condition as specified and did not add a
timer stop. See QUESTIONS 3.

A normal 5 s hold is unaffected as long as the sampler gets a good read at
least every 100 ms (it samples every 10 ms, `SWITCH_SAMPLE_MS`,
`switch_input.c:53`). ASSERTED — not run.

### b.3 Part C — watchdog-stop diagnostics (design)

#### b.3.1 The question

The app's "Connection stalled - movement stopped" is Command Result 0x05
`CMD_RESULT_WATCHDOG_STOP` (`ble_bed_service.c:176`). It is sent from exactly
one place, `service_move_watchdog()`'s expiry branch (`ble_bed_service.c:580`
after this commit), which runs when no MOVE_START/AUTO_LEVEL_START has
refreshed the 300 ms dead-man timer (`MOVE_WATCHDOG_MS`, `:188`). The app
repeats AUTO_LEVEL_START every 100 ms with write-without-response.

The prompt's reading of the log — each extra `Step 1: ROLL and PITCH check` is
the app's next AUTO_LEVEL_START restarting auto-level after a watchdog stop —
**is consistent with the code**: `Step 1` prints only when `first_print[0]` is
true (`app.c:615-620`), and only `app_auto_level_start()` sets it back
(`bed_actions.c:159-162`). The app path calls that only when
`app_auto_level_active` is false (the AUTO_LEVEL_START case,
`ble_bed_service.c:775-781` after this commit), and only STOP_ALL,
AUTO_LEVEL_STOP, the disconnect stop and watchdog expiry clear it. A user
releasing and re-pressing within the same hold would produce the same print;
the new block distinguishes the two (a watchdog stop prints it, a release
does not).

Two possibilities to separate:

1. **Phone side**: the writes are not arriving (the phone, its BLE stack, or
   the link did not deliver a write for 300 ms).
2. **Bed side**: the writes arrive but the main loop does not get to them in
   time — a long pass, or the loop not running at all between passes.

#### b.3.2 What is recorded (C1, C2)

All behind `#define BLE_DEBUG_WATCHDOG_LOG 1` in `ble_bed_service.h:36`. The
define lives in the header, not in the `.c` file like
`BLE_DEBUG_SECURITY_LOG`, because `app.c` needs it too.

Statics in `ble_bed_service.c:248-288`, all sleeptimer ticks (32768 Hz, one
tick ~30.5 µs) or small counters:

| Variable | Written by | Meaning |
|---|---|---|
| `wd_diag_rx_tick` | Command write handler (`sl_bt_evt_gatt_server_attribute_value`, the branch that fills `pending_command[]`, `:1286-1288`) | when the stack delivered the most recent non-STOP_ALL Command write |
| `wd_diag_processing_rx_tick` | `process_pending_command()` snapshot (`:651-653`) | receive tick of the write now being processed |
| `wd_diag_refresh_tick` | `move_watchdog_arm()` (`:460-485`) | tick of the last arm/refresh |
| `wd_diag_refresh_rx_tick` | `move_watchdog_arm()` | receive tick of the write that did that arm |
| `wd_diag_refresh_opcode` | `move_watchdog_arm()` | 0x01 MOVE_START or 0x03 AUTO_LEVEL_START |
| `wd_diag_gap_ms[8]`, `_next`, `_count` | `move_watchdog_arm()` | ring buffer of ms between consecutive refreshes |
| `wd_diag_pass_entry_tick`, `_exit_tick`, `_have_exit` | `ble_bed_service_debug_pass_begin()/end()` | current pass start; previous pass end |
| `wd_diag_passes` | `..._pass_end()` | passes completed since the last arm/refresh |
| `wd_diag_longest_pass_ticks` | `..._pass_end()` | longest `app_process_action()` pass since the last arm/refresh |
| `wd_diag_longest_between_ticks` | `..._pass_begin()` | longest gap from one pass's end to the next pass's start since the last arm/refresh (see b.3.5) |

`move_watchdog_arm()` logic (pseudo-code; real code in c.3):

```
now = tick
if watchdog already armed:            # a refresh within one hold
    push ms(now - refresh_tick) into the ring buffer (clamped to 65535)
else:                                 # a fresh arm: new hold
    empty the ring buffer
refresh_tick    = now
refresh_rx_tick = processing_rx_tick
refresh_opcode  = pending_command[opcode]   # same write being processed (see below)
passes = longest_pass = longest_between = 0
...existing arm code unchanged...
```

`pending_command[]` is read rather than the `cmd[]` snapshot because
`move_watchdog_arm()` does not see `cmd[]`. It is the same write:
`move_watchdog_arm()` is called only from `process_pending_command()`, and no
stack event can be dispatched between that function's snapshot and the call
(both run in the super loop; the stack callback is dispatched from
`sl_main_process_action()`, not from an interrupt). A comment says so.

The "fresh arm" test uses `move_watchdog_armed` before the existing code sets
it. A fresh AUTO_LEVEL_START hold always takes this branch because the
AUTO_LEVEL_START case calls `move_watchdog_cancel()` before
`bed_auto_level_start()` when `app_auto_level_active` is false. A MOVE_START
after a force-stop cancel also takes it. A MOVE_START repeat, and every
AUTO_LEVEL_START repeat, takes the refresh branch.

Pass timing (`app.c:556-558` and `:753-755`): `ble_bed_service_debug_pass_begin()`
is the first statement of `app_process_action()` (after the local
declaration) and `..._pass_end()` the last, after `ble_bed_service_tick()`.
`app_process_action()` has no early return, so every pass is bracketed.
Each hook is one `sl_sleeptimer_get_tick_count()` and a few compares and
stores. Stats reset on every arm/refresh, so they always describe "since the
last refresh". The pass in which the refresh happens is counted as the first
pass since it (its end comes after the refresh); its duration includes the
part before the refresh. This is a slight over-count, in the conservative
direction.

#### b.3.3 What is printed (C3)

Only in the watchdog expiry branch. Values are snapshotted **before** the
stop, so the stop's own I2C time is not counted; the print comes **after**
`app_stop_all()` and `send_command_result()`, so it cannot delay either.
Four lines (format strings exactly as committed):

```
BLE: WATCHDOG STOP op=0x%02x seq=%u: %lu ms since last refresh (limit %u ms)
  last write received %lu ms ago (op=0x%02x seq=%u); last refresh received->processed %lu ms
  since last refresh: %lu passes, longest pass %lu ms, longest between passes %lu ms, this pass %lu ms so far
  last %u refresh gaps, oldest first (ms): a,b,c,...
```

| Field | Source | What it tells |
|---|---|---|
| `op`, `seq` (line 1) | opcode/seq of the last refresh (`seq` is the existing `last_move_seq`, which the WATCHDOG_STOP result echoes) | which hold timed out; matches the app's Command Result |
| X `ms since last refresh` | now - refresh tick | normally 300-301; much more means the expiry was serviced late |
| Y `last write received ... ago (op, seq)` | now - rx tick; op/seq from `pending_command[]` (the last write received, which may be later than the last refresh) | if Y ~ X, nothing arrived after the last refresh; if Y < X, something arrived and did not refresh |
| R `received->processed` | refresh tick - its write's rx tick | how long the last refresh waited between the stack dispatching it and the main loop processing it |
| P `passes` | passes completed since the last refresh | ~3-10 means the loop was mostly asleep; hundreds or more means free-running |
| L `longest pass` | longest `app_process_action()` since the last refresh | a bed-side block inside the app |
| B `longest between passes` | longest pass-end to next-pass-start | sleep plus `sl_main_process_action()` (stack, other components) |
| T `this pass ... so far` | now - current pass entry | the pass in which the expiry was noticed (excluded from L because it has not ended) |
| gaps | ring buffer, oldest first, up to 8 | the refresh rhythm before the stop: steady ~100 then silence, or bunching/irregular |

Integer milliseconds (`sl_sleeptimer_tick_to_ms()` truncates). Format uses
`%lu` with `(unsigned long)` casts for `uint32_t`; builds clean under
`-Wall -Wextra` (d.2).

#### b.3.4 C4 — connection_parameters

`sl_bt_evt_connection_parameters_t` in this SDK (`bluetooth_le_host/inc/sl_bt_api.h:6947-6966`):
`connection`, `interval` ("Time = Value x 1.25 ms"), `latency` ("Peripheral
latency (how many connection intervals the peripheral can skip)", `:6951`),
`timeout` ("Supervision timeout. Time = Value x 10 ms", `:6953`),
`security_mode`. Under the flag the print becomes

```
BLE: connection_parameters conn=%u interval=%u latency=%u timeout=%u security_mode=%u
```

raw values, units in a comment. With the flag at 0 the original print is kept
unchanged (`#else` branch). The bed asks for interval 12-24, latency 0,
timeout 100 (1 s) on every `connection_opened`
(`ble_bed_service.c:141-147`, `sl_bt_connection_set_parameters()`); the
phone, as central, decides. Latency above 0 lets the bed skip up to that
many connection events when it has nothing to send, so a phone's write could
wait up to (latency+1) intervals.

#### b.3.5 Why one field beyond C1/C2's list

C3 says "Use whatever fields make this unambiguous". C1 and C2 cover a write
not arriving (Y) and a long pass inside `app_process_action()` (L, T, R).
They cannot see time spent **outside** `app_process_action()`: the CPU
asleep in `sl_power_manager_sleep()`, or the stack and other components in
`sl_main_process_action()`. b.4.2 shows the CPU can sleep during app-driven
Step 1, which is exactly where the stops happen. So I added B, "longest
between passes", computed inside the same two hooks (no extra hook in
`main.c`). It is the one addition beyond the listed measurements, and it is
also measure-only. Also printed: T ("this pass so far"), because L excludes
the pass in progress, and that pass is the one that noticed the expiry; and
the last received write's op/seq, read from the existing `pending_command[]`
(no new storage). If the reviewer wants strictly the listed fields, delete B
and T (QUESTIONS 1).

#### b.3.6 Cost and the "nothing may block" rule

- Per pass: two `sl_sleeptimer_get_tick_count()` calls and a few
  compares/stores. No I2C, no print, no wait.
- Per Command write: one tick read in the stack callback (copying ticks is not
  work in the #27 sense; the callback still does no I2C and no print).
- Per arm/refresh: one tick read, one `tick_to_ms`, a ring-buffer store.
- On a watchdog stop only: four `printf()` lines, ~360 characters. VCOM is
  115200 baud (`config/sl_iostream_usart_io_stream_config.h:40`) and the
  USART write is blocking per character (`sl_iostream_usart.c:259-263`), so
  ~31 ms (360 x 86.8 µs). This is after the stop and after the result. It
  extends that one pass, which already costs ~135 ms in `app_stop_all()`'s
  LED writes (b.4.1), so the next AUTO_LEVEL_START is processed ~30 ms
  later than it would be without the print. Same class as the existing
  connection-event prints.
- On a connection_parameters event: the same print with ~20 more characters
  (~2 ms more).
- Memory: ~60 bytes of statics. Text grew to 244596 bytes (`arm-none-eabi-size`, d.2).

With the flag at 0, all diagnostic code compiles out and the connection
print is the original one; built and verified clean (d.2), then set back to
1. Pre-ship it must be 0.

### b.4 C5 — what can block the main loop on the auto-level path (report only, nothing changed)

All timings below are **estimates from bit-time arithmetic**, not
measurements. Both I2C buses are standard mode, 100 kbit/s
(`config/sl_i2cspm_on_board_config.h:49`, `config/sl_i2cspm_off_board_config.h:49`,
`SPEED_MODE 0`), so one bit is 10 µs and one byte with ACK is ~90 µs.
Building blocks:

| Operation | Bus | Estimate |
|---|---|---|
| Expander register read (write 1 byte, repeated start, read 1 byte) | either | ~0.4 ms |
| Expander register write (2 bytes) | either | ~0.3 ms |
| On-board read-modify-write (`set_I2C_redriver_on/off`, `set_motor_*`, `set_all_motors_off`) | on-board | ~0.7 ms |
| Address NACK (absent board) | off-board | ~0.1 ms |
| `ui_transaction()` with the re-driver **not** held: `set_I2C_redriver_on()` (RMW) + `sl_sleeptimer_delay_millisecond(2)` + the transfer + `set_I2C_redriver_off()` (RMW) (`exp_ui.c:81-91`, `:211-230`; `exp_board.c:414-452`) | both | ~3.5-3.8 ms |
| `set_LEDs_on/off()` for one board = read + write = 2 `ui_transaction()`s (`exp_ui.c:94-136`) | both | ~7-7.5 ms |
| `set_LED_on/off()` = both boards, blindly (`exp_ui.c:252-274`); the Aux board is absent and NACKs, but its half still pays both re-driver toggles and both 2 ms delays | both | **~15 ms** |
| `set_all_LED_off()` = 4 x `set_LED_off()` (`app.c:260-267`) | both | **~60 ms** |
| MC3479 burst read, 1 + 6 bytes (`mc3479.c:237-259`) | on-board | ~0.85 ms |

The re-driver is held only while the switch sampler has a session open, i.e.
while a physical button is involved (`switch_input.c:333`, released at
`:444`). During an app-only auto-level hold it is not held, so every LED
write pays the full cost above.

#### b.4.1 The auto-level path, pass by pass (app-driven, Step 1)

In `app_process_action()` order:

1. **Collision block** (`app.c:564-603`): skipped (`any_collision` false).
2. **Auto-level block** (`app.c:609-700`), Step 1 (`first_check` true):
   - `accel_running_average()` -> `read_accel()` (`auto_level.c:241-296`):
     the MC3479 burst read only if 10 ms have passed since the last one
     (`ACCEL_I2C_MIN_INTERVAL_MS`, `auto_level.c:36,251`), ~0.85 ms; a
     `sqrt` and two `atan2` in double precision (software on this core's
     single-precision FPU, tens of µs); `accel_filter_update()`, µs.
   - `auto_level_blink_LED(LED_RED)` (`app.c:303-316`): a timer-running
     check every pass; on the first call of a run, starts the 500 ms periodic
     timer and calls `set_LED_on(RED)` (~15 ms); then one `set_LED_on/off`
     toggle (~15 ms) per 500 ms.
   - `steady_accel_read_both_axes()` (`auto_level.c:785-802`): timer check;
     `reset_motor_current_data()` **every pass** (`motor_current_functions.c:234-254`:
     clears a 4 x 200 `uint16_t` array plus small arrays, ~800 stores, ~10-20 µs);
     at 1500 ms (`STEADY_ACCEL_READ_TIMER_TIME`, `auto_level.c:65`) two
     `check_level()` calls, µs.
   - the `Step 1: ROLL and PITCH check` print once per run, 30 characters,
     ~2.6 ms of blocking UART.
3. `settings_mode_tick()`: not in settings mode; the entry check is false; µs.
4. `switch_input_tick()`: the double-press check is gated on the switch
   source; `switch_sampler_tick()` when idle is an atomic load plus one GPIO
   read and returns (`switch_input.c:323-332`). µs. (With a physical press:
   re-driver acquire ~2.7 ms once, then ~0.5 ms of reads per 10 ms.)
5. `motion_failsafe_tick()`: rate-limited to 50 ms and returns before any I2C
   when `auto_level_active` (`switch_input.c:797-810`).
6. `sample_motor_faults_tick()`: one on-board read (~0.4 ms) every 200 ms
   (`exp_board.c:354-364`).
7. `tilt_telemetry_tick()`: returns at gate 2c during auto-level
   (`auto_level.c:473-475`); only the T3 bookkeeping before it.
8. `bed_control_tick()`: one compare (`bed_control.c:164-169`).
9. `ble_bed_service_tick()`:
   - an AUTO_LEVEL_START **repeat**: `note_app_commanding()`, the watchdog
     restart, `send_command_result()` (queues a notification; does not wait).
     µs.
   - the **first** AUTO_LEVEL_START of a hold: `bed_auto_level_start()` ->
     `app_auto_level_start()` (resets and timer stops, µs) and
     `app_apply_auto_level_start_outputs()` (`bed_actions.c:275-279`):
     `set_LED_on(RED)` ~15 ms + `set_all_motors_off()` ~0.7 ms.
   - `publish_status()` every 100 ms while auto-level is the command:
     `bed_status_get()` uses the cached fault byte, no I2C (`bed_status.c:77-78`);
     `write_attribute_value` + `notify_all` queue and return.
   - **on a watchdog stop**, `app_stop_all()` during auto-level
     (`bed_actions.c:208-230`): `app_auto_level_stop()` =
     `auto_level_stop_blink_LED()` (`set_LED_off`, ~15 ms) +
     `set_all_LED_off()` (~60 ms); then `set_all_motors_off()` (~0.7 ms) and
     `set_all_LED_off()` again (~60 ms). **~135 ms**, plus the new print
     (~31 ms). This comes after the expiry, so it cannot cause it, but it
     delays the next restart.

**Typical Step 1 pass:** ~0.1 ms; ~1 ms with an accelerometer read; ~16 ms
with an LED toggle. **Longest single Step 1 pass:** ~16-17 ms (the first
AUTO_LEVEL_START's `set_LED_on` at the end of one pass, and the blink's first
`set_LED_on` at the start of the next, are in different passes). **No path on
the Step 1 loop comes near 300 ms under normal I2C conditions.**

Step 2 adds, once at the start of a leveling move: four direction RMWs and
four enable RMWs (`set_dual_motors_direction_*`, `set_dual_motors_on`,
`dual_motor_control.c:347+, :464+`), ~6 ms, and `scan_all_currents()`; then
`run_motor_supervision()` every pass, and four `Max Current for Motor N`
prints (~120 characters, ~10 ms) once. Not estimated in detail; nothing found
that blocks for hundreds of ms.

**The abnormal case, which matters:** in SDK 2026.6.0, `I2CSPM_Transfer()`
passes `SL_I2CSPM_TRANSFER_TIMEOUT` = 300000 (`sl_i2cspm.c:48`, commented
"how many polls") as `timeout_ms` (`:132`) to `sl_i2c_leader_*_blocking()`,
whose parameter is "Timeout duration in milliseconds" (`sl_i2c.h:353`). So a
transfer on a bus that hangs (a device holding SDA or SCL low) can block for
up to **300 s**. An absent device just NACKs (~0.1 ms) — that is not a hang.
In the Debug build, a failed on-board transfer also hits `EFM_ASSERT`
(`exp_board.c:125` etc.). Neither would produce a clean
watchdog-stop-and-restart pattern; a hang would freeze the bed. Reported, not
changed. See QUESTIONS 5.

#### b.4.2 Does the CPU sleep during auto-level Step 1, and what wakes it?

`app_is_ok_to_sleep()` (`app.c:510-549`) vetoes sleep only if: any
`motor_state[i].motor_on`; the switch sampler is active; the motor-enable
shadow may be on (`motor_outputs_maybe_on()`, `exp_board.c:329-332`); or a
tilt burst is running. In app-driven Step 1:

- `motor_state[].motor_on` are all false — `app_auto_level_start()` clears
  them (`bed_actions.c:172-174`) and Step 1 never sets them.
- The sampler is idle — no physical button (its session ends when everything
  is released, `switch_input.c:432-446`).
- The shadow is 0 — `app_apply_auto_level_start_outputs()` calls
  `set_all_motors_off()`, which leaves all four enables low on success
  (`exp_board.c:103-106`, `:179-197`).
- No tilt burst — T1 is on first subscribe, T2 on REFRESH_TILT, T3 on a
  motors-on -> all-off edge; none is triggered by Step 1. (After a watchdog
  stop *during leveling*, T3 would arm a 500 ms burst; after a stop during
  Step 1 it would not.)

So **the application allows sleep during app-driven Step 1** (ASSERTED from
code). Whether the Bluetooth stack also allows it (`sli_bt_is_ok_to_sleep()`,
`autogen/sl_power_manager_handler.c:61`) is inside the closed stack library
and not known. Note the contrast: during a **physical** AUTO LEVEL hold the
sampler is active, so sleep is vetoed and the loop free-runs. App-driven and
physical Step 1 therefore run at very different pass rates. Because
auto-level's averaging is in loop passes (`NUM_ACCEL_READINGS` 130,
`auto_level.c:60`; DECISIONS #29), app-driven Step 1 may average far fewer
fresh samples in its 1500 ms than physical Step 1. That is a separate
question from the watchdog; QUESTIONS 4.

How sleep works here (SDK, read): `sl_power_manager_sleep()` checks
`sl_power_manager_is_ok_to_sleep()` (stack + app) **once**, on entry
(`sl_power_manager.c:263`). After each interrupt it decides whether to go
back to sleep with `sl_power_manager_sleep_on_isr_exit()`
(`sl_power_manager.c:348`; `autogen/sl_power_manager_handler.c:77-116`): it
goes back to sleep only if the wakeup was the power manager's own internal
timer, or the Bluetooth stack or the USART stream says SLEEP; any other
interrupt returns to the main loop. So, during app-driven Step 1, the main
loop runs when:

- **an application sleeptimer fires** (each returns to the loop): Bed Status
  notify timer, ~every 100 ms while auto-level is the command
  (`STATUS_NOTIFY_MOVING_MS`, restarted on each publish); auto-level LED blink,
  every 500 ms; steady-read one-shot at 1500 ms; heartbeat every 1000 ms
  (`app.c:165-169`); the move watchdog at 300 ms (normally restarted before it
  fires); the tilt burst timer if running;
- **the Bluetooth stack has an event for the application** — every received
  Command write becomes a `gatt_server_attribute_value` event. The stack's
  ISR-exit hook is closed source; that it returns WAKEUP when it has an event
  queued is the hook's documented purpose, not something read in code;
- a **GPIO interrupt** from the UI expander (a physical press);
- USART RX.

Not wakeups: the 10 ms accelerometer gate, the 200 ms fault sample, the 50 ms
fail-safe and the app-commanding deadline are all tick comparisons, not
timers. They run only when something else wakes the loop.

What this means for the watchdog: a received write wakes the loop and is
processed in that same loop iteration (`sl_main_process_action()` dispatches
it, then `app_process_action()` drains it). Sleep should not delay a write
that arrives. If the diagnostics show R ~ 0 and B ~ 100 ms with P small, that
is this sleep pattern working as described, not a fault. PWM does not hold
the CPU awake: `sl_pwm.c` has no power-manager calls (`grep -c` = 0, d.4).

#### b.4.3 Summary of C5

Under normal I2C conditions nothing on the auto-level Step 1 path blocks for
more than ~16 ms per pass, and sleep does not stop a received write from
being processed. If that holds on the bench, a 300 ms gap is more likely on
the phone/link side. That is an inference from code; the Part C print exists
to test it.

### b.5 Part D — the reviewer's bench steps, with expected lines

Firmware: `c7d743d` (HEAD after the work commits; this handoff's commit adds
docs only), `BLE_DEBUG_WATCHDOG_LOG` 1, `BLE_DEBUG_SECURITY_LOG` 1,
`SWITCH_SAMPLER_DIAG` 1. All lines below are **expected, not observed**.

#### D1. The extended connection_parameters line (at every connect)

```
BLE: connection_opened conn=N bonding=0xNN
BLE: connection_parameters conn=N interval=24 latency=0 timeout=T security_mode=0
BLE: connection_parameters conn=N interval=24 latency=0 timeout=T security_mode=1
BLE: cccd_seed conn=N bed_status=0x0001 sc=0x0000
```

- `interval=24` (30 ms) is what every 2026-10-18 log shows; the new fields
  are `latency` and `timeout`.
- **`latency=0` is expected.** Anything above 0 is the thing to report.
- `timeout=100` (1 s) if the phone accepted the bed's request; another value
  (Android often starts at 500, i.e. 5 s) means the phone kept its own. The
  first line after `connection_opened` carries the phone's initial choice;
  the bed's update request may change later lines. A rejected request is not
  printed (`sl_bt_evt_connection_set_parameters_failed` is not handled).
- The `characteristic_status DROPPED` line may also appear, as in earlier logs.

#### D2. A normal app auto-level hold (no watchdog stop)

Hold AUTO LEVEL in the app until it finishes, or release part-way.

```
Step 1: ROLL and PITCH check          <- exactly ONCE per hold
Step 2: ROLL Not Level                <- or straight to "Exit auto-level" / "Bed is disabled, no leveling required"
Leveling...
...
Exit auto-level
```

- **No `BLE: WATCHDOG STOP` line.**
- **One `Step 1` per press.** Two or more in a row without a release is the
  symptom, and each should now come with a `WATCHDOG STOP` block just before
  it.
- Release prints nothing (AUTO_LEVEL_STOP has no print).

If the symptom appears, the block will say which side. Reading it:

| Pattern | Points to |
|---|---|
| X ~ 300-301, Y ~ X, same `seq` on both lines, R ~ 0, L and T small (< ~20 ms), B up to ~100-110 ms | **Phone/link side**: the last write arrived and was processed at once; nothing came for 300 ms; the bed was idle or asleep and would have woken for a write |
| L or T large (hundreds of ms), or R large | **Bed side, a long pass** in `app_process_action()` |
| B large (≥ ~200 ms) with P small, and Y < X (a write arrived but was not processed in time) | **Bed side, loop not running** between passes (sleep not ending, or time in the stack) |
| Y < X with a different `op`/`seq` on line 2 | a write arrived after the last refresh and did not refresh (rejected, or a different opcode) — check the app's Command Results |
| gaps irregular before the stop (e.g. `100,100,180,40,...`) | the phone is sending late or bunched |
| `last 0 refresh gaps` | the stop came before the hold's second write was processed |

If the app increments `seq` per write, comparing line 1's `seq` (last
refresh) with line 2's (last write received) shows how many writes arrived
after the last refresh (expected 0).

#### D3. A deliberate watchdog stop

**Turning the phone's Bluetooth off mid-hold probably does NOT produce a
`WATCHDOG STOP`.** A phone switching Bluetooth off normally sends a link
termination first. The bed then gets `connection_closed` (reason 0x1013),
`handle_connection_closed()` flags `disconnect_stop_pending`, and the next
`ble_bed_service_tick()` calls `move_watchdog_cancel()` before
`app_stop_all()` (`ble_bed_service.c:974-981`). The last refresh was at most
~100 ms earlier, so the disconnect is normally handled well before the 300 ms
watchdog would fire. DECISIONS #36 / SESSION_LOG 2026-08-04 already noted
this ("Switching Bluetooth off produces a clean disconnect"). Expected:

```
BLE: connection_closed conn=N reason=0x1013
```

and no `WATCHDOG STOP`. The bed stops through the disconnect path either way.
**If the phone's radio stops without sending a termination**, the watchdog
fires first and the supervision timeout follows (~1 s after the last packet
if `timeout=100`):

```
BLE: WATCHDOG STOP op=0x03 seq=S: 300 ms since last refresh (limit 300 ms)
  last write received 300 ms ago (op=0x03 seq=S); last refresh received->processed 0 ms
  since last refresh: P passes, longest pass L ms, longest between passes B ms, this pass T ms so far
  last 8 refresh gaps, oldest first (ms): 100,100,99,101,100,100,100,100
BLE: connection_closed conn=N reason=0x1008
```

- X: 300 or 301 (the timer fires at 300 ms and its interrupt wakes the loop).
- During Step 1 (CPU allowed to sleep): P roughly 3-10, B up to ~100 ms,
  L ~0-16 ms. During leveling (a motor on, loop free-running): P large,
  B ~0-1 ms, L ~16 ms (an LED toggle).
- The gaps should be ~100 each, as the app repeats every 100 ms; fewer than
  8 if the stop came early in the hold.
- 0x1008 = `SL_STATUS_BT_CTRL_CONNECTION_TIMEOUT`, "Link supervision timeout
  has expired" (`sl_status.h:260`, d.4).

More reliable ways to produce a real stall (my suggestion, QUESTIONS 2):
put the phone in a closed metal box (a biscuit tin) mid-hold, or walk out of
range — the link stalls without a termination, so the watchdog fires
(expected block as above), then 0x1008. A third, if the app allows it: keep
the link up but stop the repeats (for example backgrounding the app mid-hold,
if that does not send AUTO_LEVEL_STOP) — that is the case the watchdog
uniquely covers, and the block should show Y ~ X with the link still up and no
`connection_closed`.

The block can also be checked with a move: hold UP HEAD in the app and stall
the link; line 1 then shows `op=0x01`.

## c. Diffs (complete)

Each is `git show <sha>` of the committed commit, verbatim. Source diffs (c.2,
c.3) are complete. The docs diff c.1 is 415 lines including headers and is
**not truncated**. The `docs/SESSION_LOG.md` diff is not reproduced (it ships
alongside this file); this handoff's own commit is not reproduced either.
Git printed `LF will be replaced by CRLF` warnings on `git add` (d.1); the
committed blobs are as shown.

### c.1 Commit A `f6614c8` (docs only)

```diff
commit f6614c828e012207fbed6f9315fb0af046e2dd43
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Fri Oct 23 16:05:46 2026 -0700

    Docs: Stage C status, Prompt W bench result, DECISIONS #44, pairing/STOP checks
    
    - CLAUDE.md: the reviewer's "BLE status" bullet replaces "No BLE glue"
      (user-authorised edit, committed as made).
    - MIGRATION_STATUS.md: Stage C is built and bench-tested (DECISIONS #34-#43),
      not "Not started"; the full pass on current firmware and the Release-config
      pass stay open.
    - DECISIONS #43: 2026-10-18 Prompt W bench result (Galaxy S23); refused
      pairing closed with reason 0x1022 (LL response timeout), not 0x1016.
    - DECISIONS #44: STOP in settings mode cancels any hold and stays in
      settings mode (user decision 3, implemented in 840d7b8).
    - REGRESSION.md: items 7.10 (pairing window) and 7.11 (STOP during a
      settings hold), unchecked; Prompt W recorded result.
    - docs/prompts: Prompt Y.
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

diff --git a/CLAUDE.md b/CLAUDE.md
index 51e82fc..979f15e 100644
--- a/CLAUDE.md
+++ b/CLAUDE.md
@@ -52,11 +52,11 @@ understanding, not just correctness:
   that used to require a hand-edited config file — if you ever find
   yourself wanting to hand-edit a generated file, that is a signal to
   stop and ask, not a shortcut to take.
-- **No BLE glue in this phase.** The radio stays dormant until the
-  migrated firmware passes the full `docs/REGRESSION.md` pass on hardware
-  (DECISIONS #20). Don't add `sl_bt_on_event` handling, GATT logic, or
-  anything beyond what the stock "Bluetooth – SoC Empty" sample already
-  provides, even if it looks like a natural next step.
+- **BLE status.** BLE is built and in use: Stage B (telemetry) and Stage C
+  (the app drives the bed, DECISIONS #34), the pairing window (#42) and the
+  bond/refusal handling (#43). DECISIONS.md is the authoritative record.
+  (`docs/MIGRATION_STATUS.md` still describes Stage C as not started and is
+  being corrected.) BLE work is done only when a prompt asks for it.
 - **Keep the `bed_` module prefix.** Only cosmetic "Totem"/"Totem
   Sleep"→"Tilt Bed"/"Tilt Bed Systems" string changes are in scope; don't
   rename modules, functions, or the prefix.
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 58246e4..4def63f 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -1989,3 +1989,104 @@ outcome is that a refused phone now sees `connection_closed ...
 reason=0x1016` (terminated by the local host) instead of 0x1013, unless the
 phone closes first. That is inferred from `sl_status.h`, not observed.
 Eviction (part 1) has not been observed either: it needs a full 8-bond store.
+
+### Bench result (2026-10-18)
+
+Recorded 2026-10-23 from the user's bench run of the Prompt W firmware (the
+code of `840d7b8`; the bench steps named `9ba0ca3`, which adds docs only;
+the log itself does not identify the commit), `BLE_DEBUG_SECURITY_LOG` 1.
+Log: `_03-Software/PuTTY Logs/prompt_w_bench.log` (179 lines). **The phone
+was a Samsung Galaxy S23 (SM-S911U)** for this run, not the S10 used for
+Prompt V (#42). Every line number below was checked against the log.
+**VERIFIED (user's bench run).**
+
+- **Bond store policy (part 1):** line 12, `BLE SM: store_bonding_configuration
+  -> 0x0000`. The stack accepted policy 2. Eviction itself was **not**
+  observed: it needs a full 8-bond store.
+- **Bonded phone reconnects** (lines 14-19): `connection_opened conn=1
+  bonding=0x01`, `security_mode=0` then `security_mode=1`, `cccd_seed`, and
+  no `confirm_bonding` line.
+- **Refused pairing is closed by the bed (part 2)** (lines 25-32, phone-side
+  bond deleted, bed not in settings mode): `confirm_bonding conn=2
+  handle=0x01 (REPLACING existing bond) -> REJECTED (not in settings mode)`,
+  `bonding_failed conn=2 reason=0x1205`, `BLE: closing conn=2 - pairing
+  refused`, then one `connection_closed conn=2 reason=0x1022`.
+  - **0x1022, not the predicted 0x1016.** `sl_status.h:286`:
+    `SL_STATUS_BT_CTRL_LL_RESPONSE_TIMEOUT`, "Connection terminated due to
+    link-layer procedure timeout." (0x1016, `sl_status.h:274`, is
+    `..._CONNECTION_TERMINATED_BY_LOCAL_HOST`; 0x1013, `:271`, is
+    `..._REMOTE_USER_TERMINATED`, the Prompt V result.) So the link did end
+    after the bed's `sl_bt_connection_close()` (the `closing conn` line comes
+    first, and there is no 0x1013), but the controller reported it as a
+    link-layer procedure timeout rather than a clean local termination.
+    Which procedure timed out is not visible in the log (no timestamps).
+    One reading, **ASSERTED**: the phone did not acknowledge the bed's
+    termination in time (for example because it was tearing the link down
+    itself after the pairing failure). Either way the refused phone no
+    longer holds a connection, which is what part 2 is for.
+- **Pairing in settings mode** (lines 33-46): BED LIGHTS held (settings-mode
+  entry prints nothing), then `confirm_bonding conn=3 handle=0x01
+  (REPLACING existing bond) -> ACCEPTED (settings mode)`, `security_mode=1`,
+  `cccd_seed`, `bonded conn=3`. `Settings mode: exit` follows at line 48.
+- **Not in this log:** a reconnect after the settings-mode exit (conn=3
+  stays up to the end of the log). Prompt V showed it (#42, item d).
+
+The settings-hold results from the same run are recorded under #44.
+
+## 44. STOP in settings mode cancels any hold in progress and stays in settings mode
+
+Date: 2026-10-18 (decision and implementation), recorded 2026-10-23. User
+decision (Prompt W, decision 3).
+
+### Decision
+
+A STOP while settings mode is active abandons any hold in progress - a
+sensitivity step (UP ALL / DOWN ALL, 2 s) or a zero-angle store/erase
+(AUTO LEVEL, 2 s / 5 s) - writes nothing to NVM3, and **leaves the bed in
+settings mode**. The next hold starts from zero and needs its full 2 s.
+
+### Implementation (`840d7b8`)
+
+`settings_mode_cancel_holds()` (`settings_mode.c:120-126`) stops both
+settings timers and clears both timeout flags. It is called from
+`app_stop_all()` when `in_settings_mode` is true (`bed_actions.c:221-228`).
+Without it the hold's timer kept running after `app_stop_all()` cleared
+`active_switch`, and its timeout acted on the next hold the moment it
+started.
+
+`app_stop_all()` has three callers, all in `ble_bed_service.c`: the STOP_ALL
+opcode (`:535`), dead-man watchdog expiry (`:505`), and the control-phone
+disconnect stop (`:883`). So the cancel runs on **every full stop**, not only
+on the app's STOP button. **The reviewer accepts this:** cancelling a settings
+hold on any full stop is correct. (In practice the watchdog path is hard to
+reach in settings mode, because motion opcodes are refused there before they
+arm the watchdog - Prompt W handoff, section b.)
+
+The same commit also makes the UP ALL, DOWN ALL and AUTO LEVEL release
+branches clear a timeout that fired just before the release (they call
+`settings_mode_cancel_holds()`), and requires BED LIGHTS to be physically
+down for the entry hold. Those are bug fixes in the same area, not part of
+this decision.
+
+### Bench result (2026-10-18)
+
+Same run and log as #43's bench result (`prompt_w_bench.log`, Galaxy S23).
+**VERIFIED (user's bench run)**, with one part resting on the user's report:
+
+- **STOP during an UP ALL hold wrote nothing** (lines 54-59): two UP ALL
+  holds, each `SW: 0x0001 down (bounces 0)`, `2 second timer start`,
+  `SW: 0x0001 up (bounces 0)`, with no `Writing values to NVM3...` and no
+  `Settings mode: exit`. STOP itself prints nothing, by design, and the log
+  has no timestamps, so **that STOP was pressed in the app during these holds
+  and that they were held past 2 s is the user's report**, not something the
+  log alone shows.
+- **The next full hold stepped one rung** (lines 60-69): `Writing values to
+  NVM3...`, both `Successfully stored ...` lines, `Stored Raise Sensitivity
+  is: 350`, `Stored Lower Sensitivity is: 250` - from the loaded 550/275
+  (lines 9-10) to the HIGH rung. The two `2 second timer start` lines before
+  the write and one after it are the existing pattern: the expired one-shot
+  is restarted in the same tick (`settings_mode.c:407-412`) before the write
+  stops it (`:489`), then restarted on the next tick.
+- **DOWN ALL restored 550/275** (lines 70-79): `Stored Raise Sensitivity is:
+  550`, `Stored Lower Sensitivity is: 275`.
+- `Settings mode: exit` at line 81 on a new BED LIGHTS press.
diff --git a/docs/MIGRATION_STATUS.md b/docs/MIGRATION_STATUS.md
index 1db473b..7ca28f6 100644
--- a/docs/MIGRATION_STATUS.md
+++ b/docs/MIGRATION_STATUS.md
@@ -32,14 +32,19 @@ Following SDK_PORTING_GUIDE.md, gates in order:
       scoped as the hard gate BEFORE any BLE glue (DECISIONS #20); in
       practice, BLE Stage B (below) proceeded ahead of this gate since it
       cannot drive a motor and gave a live testbed for two BLE-drop bugs
-      that have since been found and fixed (DECISIONS #26/#27). Stage C
-      (the motor-driving command path) remains hard-gated on this pass.
-      Test record prepped: `docs/REGRESSION_RECORD_D1.md` — still blank,
-      not yet run. REGRESSION.md itself was updated this session to require
-      a phone connected and subscribed throughout the pass (see below), so
-      REGRESSION_RECORD_D1.md's checklist copy (taken before that update)
-      should be refreshed from the current REGRESSION.md before this pass
-      is run.
+      that have since been found and fixed (DECISIONS #26/#27). Test record
+      prepped: `docs/REGRESSION_RECORD_D1.md` — still blank, never run, and
+      it can no longer be run as scoped (it was the pre-BLE isolation pass;
+      BLE code now exists — see that file's own pointer).
+      **Corrected 2026-10-23:** the gate actually applied to Stage C was the
+      post-Stage-B full pass recorded in
+      `docs/REGRESSION_RECORD_PRE_STAGEC.md` (2026-07-19, commit `af369f2`,
+      **Debug** build, phone subscribed throughout; recorded in `29fdf0d`).
+      Stage C was then built (see "BLE status" below). **Still open:** a
+      full REGRESSION.md pass on the *current* firmware (which now includes
+      the Stage C, sampler and pairing-window items) has not been run, and
+      the **Release-config confirmation pass is still owed before Stage C
+      ships** (`REGRESSION_RECORD_PRE_STAGEC.md`'s own header note).
 
 ## BLE status (BLE_GATT_DESIGN.md)
 
@@ -48,11 +53,21 @@ Following SDK_PORTING_GUIDE.md, gates in order:
       **Bench-verified**: bonding works, Bed Status telemetry updates live
       while subscribed, and auto-level no longer drops the link (both
       BLE-drop bugs were the same root cause — DECISIONS #26/#27).
-- [ ] **Stage C** — Command characteristic → motors + dead-man watchdog
-      (BLE_GATT_DESIGN.md §4-5). **Not started. Hard-gated on the Phase 6
-      REGRESSION.md pass above**, which must now be run with a phone
-      connected and subscribed throughout (REGRESSION.md's own updated
-      preamble).
+- [x] **Stage C** — Command characteristic → motors + dead-man watchdog
+      (BLE_GATT_DESIGN.md §4-5). **Built and bench-tested** (corrected
+      2026-10-23; this line previously said "Not started"). Record:
+      DECISIONS #34 (built, `ab12d09`, 2026-08-02); the first bench run
+      found the 10 Hz app-move stutter, fixed in `a22406f` (2026-08-02,
+      #35, fix bench-confirmed per #36); auto-level became hold-to-run and
+      watchdogged in `5351aef` (2026-08-04, #36); later bench-driven fixes
+      #37-#41 (2026-08-04 → 2026-10-18). Pairing window (#42, `944f19e`,
+      2026-10-18) and bond-store policy + closing refused pairings (#43,
+      `c6ee6b0`, 2026-10-18) are bench-verified on 2026-10-18 (#42/#43
+      "Bench result"). **Still open:** the watchdog's stall case and an
+      intermittent `WATCHDOG_STOP` during app auto-level holds are under
+      investigation (2026-10-23 diagnostics); the full REGRESSION.md pass on
+      current firmware and the Release-config pass (Phase 6 above) have not
+      been run.
 
 ## The docs, and what changed recently
 
@@ -66,9 +81,9 @@ Following SDK_PORTING_GUIDE.md, gates in order:
   above). Written for the GUI path; note the IDE is now VS Code, not the old
   in-Studio "Simplicity IDE".
 - **BLE_GATT_DESIGN.md** — the D1 BLE protocol contract (services,
-  characteristics, packet layouts, watchdog, roles, OTA). Stage B (§3/§6/§8
-  minus Stage C's command path) is implemented and bench-verified; Stage C
-  (§4/§5, the command→motor path) is not started — see "BLE status" above.
+  characteristics, packet layouts, watchdog, roles, OTA). Stage B (§3/§6/§8)
+  and Stage C (§4/§5, the command→motor path) are both implemented and
+  bench-tested — see "BLE status" above (corrected 2026-10-23).
 - **REFACTOR_PLAN.md** — the A–D plan. Phases A–C are complete on the old
   project; D1 is this migration. The D1 note has been updated to reflect #25
   and the fresh-project migration.
@@ -98,7 +113,11 @@ Following SDK_PORTING_GUIDE.md, gates in order:
 4. **Grep for hardcoded `TIMER0`/`TIMERx`** references during the port and
    flag any (part of the #25 change).
 5. **Regression before Stage C.** DECISIONS #20's original rule (no BLE glue
-   before the full REGRESSION.md pass) held for the motor-driving command
-   path: Stage C is hard-gated on Phase 6, per "BLE status" above. Stage B
-   (telemetry-only, no command path — cannot drive a motor either way) was
-   allowed to proceed ahead of Phase 6 in practice, and is bench-verified.
+   before the full REGRESSION.md pass) was not followed as written. Stage B
+   (telemetry-only, cannot drive a motor) proceeded ahead of Phase 6 and is
+   bench-verified. Stage C was gated instead on the post-Stage-B full pass in
+   `REGRESSION_RECORD_PRE_STAGEC.md` (2026-07-19, Debug), and has since been
+   built and bench-tested (DECISIONS #34-#43; corrected 2026-10-23 — this
+   item previously said Stage C was hard-gated and not started). What is
+   still owed: a full REGRESSION.md pass on current firmware, and the
+   Release-config confirmation pass before Stage C ships.
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index 3a0ef79..8ad21b6 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -234,6 +234,33 @@ pitch, all actuators clear of limits.
       Holding that press does not re-enter settings mode, and its release
       does nothing. Afterwards one press of each button works normally.
       A power cycle also exits.
+- [ ] 7.10 Pairing window (DECISIONS #42/#43). Build with
+      `BLE_DEBUG_SECURITY_LOG` 1. Note the phone model in the test record.
+  - [ ] 7.10a Bonded phone reconnects, bed NOT in settings mode:
+        `connection_opened conn=N bonding=0xNN` (not `0xff`), then
+        `security_mode=1`, and no `confirm_bonding` line. The app connects.
+  - [ ] 7.10b Refusal: delete the bond on the phone, bed NOT in settings
+        mode, connect from the app: `confirm_bonding ... -> REJECTED (not in
+        settings mode)`, `bonding_failed ... reason=0x1205`, `BLE: closing
+        conn=N - pairing refused`, then exactly one `connection_closed
+        conn=N` (0x1022 on the 2026-10-18 S23 run; 0x1016 or 0x1013 are also
+        possible depending on which side finishes first). The phone shows a
+        pairing failure, not a hang.
+  - [ ] 7.10c Acceptance in settings mode: enter settings mode (7.1), connect
+        from the app: `confirm_bonding ... -> ACCEPTED (settings mode)`,
+        then `bonded conn=N`. The app connects.
+  - [ ] 7.10d Reconnect after exit: exit settings mode (7.9), close and
+        reopen the app: `connection_opened ... bonding=0xNN`,
+        `security_mode=1`, no `confirm_bonding` line, and the app can drive
+        the bed.
+- [ ] 7.11 STOP during a settings hold (DECISIONS #44). In settings mode,
+      hold UP ALL: `SW: 0x0001 down`, `2 second timer start`. Before 2 s,
+      press STOP in the app (it gets Command Result ACCEPTED; no PuTTY line
+      for STOP). Keep holding past 2 s, then release: no `Writing values to
+      NVM3...`, no `Settings mode: exit`, no ladder LED, amber keeps
+      fast-blinking. Hold UP ALL again: the step happens only after a full
+      2 s. Repeat for DOWN ALL and for AUTO LEVEL (no `Writing values to
+      NVM3...` after the STOP; the next AUTO LEVEL hold needs the full 2 s).
 - [ ] ⚡ BLE link stays up (no disconnect) through settings mode entry,
       zero-level store/erase, and sensitivity adjustment
 
@@ -250,6 +277,22 @@ below record the DECISIONS #42 checks the user ran alongside 7.9.
 | Pairing in settings mode | `-> ACCEPTED (settings mode)`, then `bonded` |
 | 7.1-7.8, BLE link item | not separately recorded |
 
+**Recorded result, 2026-10-18 (Prompt W firmware: code of `840d7b8`,
+`BLE_DEBUG_SECURITY_LOG` 1, phone Samsung Galaxy S23 SM-S911U, log
+`prompt_w_bench.log`; recorded 2026-10-23, full figures in DECISIONS #43 and
+#44 "Bench result").** VERIFIED (user's bench run). Items 7.10 and 7.11 were
+added after this run, on 2026-10-23; the rows below are the checks the user
+ran that those items now cover.
+
+| Item | Result |
+|---|---|
+| Boot | `store_bonding_configuration -> 0x0000` (policy 2 accepted; eviction not observed) |
+| 7.10a | `connection_opened conn=1 bonding=0x01`, `security_mode=1`, no `confirm_bonding` |
+| 7.10b | `-> REJECTED (not in settings mode)`, `bonding_failed reason=0x1205`, `closing conn=2 - pairing refused`, one `connection_closed conn=2 reason=0x1022` (`SL_STATUS_BT_CTRL_LL_RESPONSE_TIMEOUT`, link-layer procedure timeout; 0x1016 had been predicted) |
+| 7.10c | `-> ACCEPTED (settings mode)`, then `bonded conn=3` |
+| 7.10d | not in this log (Prompt V row above) |
+| 7.11 (UP ALL only) | two UP ALL holds with no NVM3 write (STOP pressed in the app: user's report; STOP prints nothing); the next full hold wrote 350/250; DOWN ALL then wrote 550/275 |
+
 ## 8. Bed lights
 
 - [ ] ⚡ Tap BED_LIGHTS: under-bed lighting on, amber LED on
diff --git a/docs/prompts/PROMPT_Y_watchdog-diagnostics-and-followups.md b/docs/prompts/PROMPT_Y_watchdog-diagnostics-and-followups.md
new file mode 100644
index 0000000..c450a93
--- /dev/null
+++ b/docs/prompts/PROMPT_Y_watchdog-diagnostics-and-followups.md
@@ -0,0 +1,109 @@
+# Prompt Y - watchdog-stop diagnostics, Prompt W follow-ups
+
+Written by the reviewer on 2026-10-18. Implement as specified. Where the code
+contradicts this document, stop and report it under QUESTIONS FOR REVIEW
+instead of improvising. Follow CLAUDE.md for the handoff file, the session
+log, committing and pushing. BED_BENCH_PROVOKE_RUNAWAY and
+BLE_BENCH_WIPE_BONDS stay 0. Nothing added may block (DECISIONS #27).
+
+Expected repo state: HEAD 18d8378, level with origin/main. Uncommitted:
+.projectlinkstore (never commit it) and CLAUDE.md (an edit the user
+authorised and the reviewer made - see A1). This prompt file is untracked;
+commit it with commit A.
+
+Make THREE commits, A to C, building from the command line after each (the
+user permits it). Report the raw build tail and any warnings.
+
+======================================================================
+A. Docs (docs only)
+======================================================================
+A1. Commit the reviewer's CLAUDE.md edit as it is (the "No BLE glue" bullet
+    replaced by a "BLE status" bullet). Show its diff. Do not edit it.
+A2. docs/MIGRATION_STATUS.md says BLE Stage C is "Not started" (lines ~51,
+    70, 100-102 per the last handoff). Correct it to match DECISIONS #34,
+    #42 and #43: Stage C built and bench-tested; give the dates and commits
+    you can cite. Keep anything that is genuinely still gated (e.g. the full
+    REGRESSION.md pass) marked as such. Show the diff.
+A3. Record the Prompt W bench result. Log:
+    C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\prompt_w_bench.log
+    Confirm from the log before recording: store_bonding_configuration ->
+    0x0000; bonded reconnect with no confirm_bonding; refusal with
+    "closing conn=2 - pairing refused" and connection_closed reason=0x1022
+    (not the predicted 0x1016 - look up 0x1022 in sl_status.h and state its
+    meaning); acceptance in settings mode; STOP during an UP ALL hold wrote
+    nothing; the next full hold stepped to 350/250; DOWN ALL restored
+    550/275. Note the phone was a Galaxy S23 (SM-S911U) for this run, not
+    the S10. Record as VERIFIED (user's bench run) under DECISIONS #43, in
+    REGRESSION.md, and in the SESSION_LOG entry.
+A4. Add DECISIONS entry (next free number) for user decision 3: STOP in
+    settings mode cancels any hold in progress and stays in settings mode
+    (implemented in 840d7b8 inside app_stop_all(), which also runs on
+    watchdog expiry and control-phone disconnect - the reviewer accepts
+    that: cancelling a settings hold on any full stop is correct).
+A5. REGRESSION.md: add explicit checklist items for the pairing window
+    (bonded reconnect, refusal + bed closes the link, acceptance in
+    settings mode, reconnect after exit) and for "STOP during a settings
+    hold". Boxes unchecked.
+
+======================================================================
+B. Settings-mode entry: freshness check (small)
+======================================================================
+In settings_mode_tick()'s entry condition, also require
+switch_state_fresh() (as the fail-safe does), so stale sampler data cannot
+hold the entry timer running. One condition; nothing else changes.
+
+======================================================================
+C. Dead-man watchdog stop diagnostics (bench diagnostics, no behaviour change)
+======================================================================
+Problem (user's bench, 2026-10-18, Galaxy S23, wireless debugging both on
+and off): while holding AUTO LEVEL in the app, the app intermittently shows
+"Connection stalled - movement stopped", which is Command Result 0x05
+(CMD_RESULT_WATCHDOG_STOP). prompt_w_bench.log shows "Step 1: ROLL and
+PITCH check" printed 3-4 times in a row at the start of several holds: each
+is the app's next AUTO_LEVEL_START restarting auto-level after a watchdog
+stop (process_pending_command: AUTO_LEVEL_START when !app_auto_level_active
+calls bed_auto_level_start()). The app repeats AUTO_LEVEL_START every 100 ms
+with writeCharacteristicWithoutResponse; MOVE_WATCHDOG_MS is 300. Physical
+auto-level works.
+
+We need data to decide whether the 300 ms gap is on the phone side (writes
+not arriving) or the bed side (main loop not getting to them). Add, behind
+a new define BLE_DEBUG_WATCHDOG_LOG (default 1, pre-ship 0):
+
+C1. Refresh timing. Each time move_watchdog_arm() is called, record the
+    sleeptimer tick, and keep the gaps (ms) between the last 8 refreshes in
+    a small ring buffer. Also record the tick at which the Bluetooth write
+    for the command was RECEIVED in the stack callback (the place that fills
+    pending_command), so receive-time and process-time can be compared.
+C2. Main-loop pass time. In app_process_action(), measure each pass's
+    duration (sleeptimer ticks at entry and exit) and keep the longest pass
+    seen since the last watchdog refresh. Measure only - no prints per
+    pass. Also count passes since the last refresh.
+C3. On every watchdog stop (where CMD_RESULT_WATCHDOG_STOP is sent), print
+    one line block, e.g.:
+      "BLE: WATCHDOG STOP op=0x03 seq=N: X ms since last refresh,
+       last write received Y ms ago, longest loop pass Z ms, P passes,
+       last gaps a,b,c,d,e,f,g,h ms"
+    Use whatever fields make this unambiguous; keep it to a few lines.
+C4. Extend the existing connection_parameters print with latency and
+    supervision timeout (check the event's field names in sl_bt_api.h for
+    this SDK and cite them). Peripheral latency above 0 would delay the
+    phone's writes, so we need to see it.
+C5. Report only, change nothing: from the code, list everything on the
+    auto-level path (app.c auto-level block, steady_accel_read_both_axes,
+    auto_level_single_axis, auto_level_blink_LED / set_LED_on/off, the
+    switch sampler idle check) that can block the main loop, with an
+    estimate of how long, e.g. LED writes go to both UI boards and each
+    ui_transaction() without a held re-driver costs two on-board
+    read-modify-writes plus a 2 ms delay, and the absent Aux board NACKs.
+    Also state whether app_is_ok_to_sleep() lets the CPU sleep during
+    auto-level Step 1 (no motor on yet) and what wakes it.
+Nothing in C may block or print per pass. All prints happen only on a
+watchdog stop or a connection event.
+
+======================================================================
+D. For the reviewer's bench steps
+======================================================================
+In the handoff, give the exact lines expected for: a normal app auto-level
+hold (no WATCHDOG STOP); a deliberate watchdog stop (e.g. turn the phone's
+Bluetooth off mid-hold); and the extended connection_parameters line.
```

### c.2 Commit B `17073a3` (source)

```diff
commit 17073a379b761a739b967d072f30e28ab34ce1eb
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Fri Oct 23 16:06:22 2026 -0700

    Settings-mode entry: require fresh sampler state
    
    settings_mode_tick()'s entry condition now also requires
    switch_state_fresh(), as the motion fail-safe does, so stale sampler data
    counts as "BED LIGHTS not down" and cannot complete a settings-mode entry.
    One condition; nothing else changes.
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

diff --git a/settings_mode.c b/settings_mode.c
index 1fe9165..2c7bcfa 100644
--- a/settings_mode.c
+++ b/settings_mode.c
@@ -272,9 +272,10 @@ void settings_mode_tick(void)
 {
   // switch_pressed is only the direction of the LAST edge delivered, for any
   // button, so the entry hold also requires BED LIGHTS to be physically down
-  // in the sampler's debounced state (#41).
+  // in the sampler's debounced state (#41) - and that state to be fresh, so
+  // stale data counts as "not down", as it does for the motion fail-safe.
   if ((switch_pressed == true) && (active_switch == SWITCH_BED_LIGHT_VALUE) && (!in_settings_mode)
-      && ((switch_sampler_down() & SWITCH_BED_LIGHTS) != 0)){
+      && switch_state_fresh() && ((switch_sampler_down() & SWITCH_BED_LIGHTS) != 0)){
     sl_sleeptimer_is_timer_running(&handle_5000ms_timer, &is_5000ms_timer_running);
 
     if (!is_5000ms_timer_running){
```

### c.3 Commit C `c7d743d` (source)

```diff
commit c7d743d96e50427ec67528bf1e27e32822556e86
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Fri Oct 23 16:08:48 2026 -0700

    Watchdog-stop diagnostics behind BLE_DEBUG_WATCHDOG_LOG (bench only)
    
    No behaviour change. New define BLE_DEBUG_WATCHDOG_LOG (ble_bed_service.h,
    default 1, set to 0 before shipping):
    - C1: move_watchdog_arm() records its tick and keeps the gaps between the
      last 8 refreshes; the Command write handler records the tick at which the
      write was received, so receive and process times can be compared.
    - C2: app_process_action() times each pass (entry/exit ticks) via two
      hooks; longest pass, longest time between passes and pass count since the
      last refresh are kept. Measure only.
    - C3: on every watchdog stop, after the stop and the WATCHDOG_STOP result, a
      four-line "BLE: WATCHDOG STOP ..." block is printed.
    - C4: connection_parameters print adds latency and supervision timeout
      (sl_bt_evt_connection_parameters_t .latency / .timeout).
    Nothing blocks; nothing prints per pass.
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

diff --git a/app.c b/app.c
index 9326733..2c36b16 100644
--- a/app.c
+++ b/app.c
@@ -553,6 +553,10 @@ void app_process_action(void)
 {
   bool local_heartbeat;
 
+#if BLE_DEBUG_WATCHDOG_LOG
+  ble_bed_service_debug_pass_begin(); // pass timing for the watchdog-stop print; measures only
+#endif
+
   // If there is a collision --> stop all motors, then flash all LEDs a few times
   // Once this sequence is done (which will take a few seconds), then collision can be
   // set back to false and motor operation can resume
@@ -745,6 +749,10 @@ void app_process_action(void)
 
   bed_control_tick();
   ble_bed_service_tick();
+
+#if BLE_DEBUG_WATCHDOG_LOG
+  ble_bed_service_debug_pass_end();
+#endif
 }
 
 /**************************************************************************//**
diff --git a/ble_bed_service.c b/ble_bed_service.c
index d554398..bbbafd8 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -245,6 +245,49 @@ static volatile bool move_watchdog_expired = false;  // set in timer ISR context
 static bool    move_watchdog_armed = false;
 static uint8_t last_move_seq = 0;                    // echoed on WATCHDOG_STOP
 
+#if BLE_DEBUG_WATCHDOG_LOG
+// Watchdog-stop diagnostics (BLE_DEBUG_WATCHDOG_LOG, ble_bed_service.h).
+// Sleeptimer ticks only; printed solely by service_move_watchdog() on expiry.
+// Everything here is written from main-loop context (the stack callback runs
+// from the super loop too - see process_pending_command()), so no volatile.
+#define WD_DIAG_GAPS 8
+static uint32_t wd_diag_rx_tick = 0;              // last Command write into pending_command[] (stack callback)
+static uint32_t wd_diag_processing_rx_tick = 0;   // receive tick of the write being processed now
+static uint32_t wd_diag_refresh_tick = 0;         // last move_watchdog_arm()
+static uint32_t wd_diag_refresh_rx_tick = 0;      // receive tick of the write that did that arm
+static uint8_t  wd_diag_refresh_opcode = 0;
+static uint16_t wd_diag_gap_ms[WD_DIAG_GAPS];     // ring buffer: ms between consecutive refreshes
+static uint8_t  wd_diag_gap_next = 0;
+static uint8_t  wd_diag_gap_count = 0;
+// Main-loop pass timing since the last arm/refresh, fed by app.c.
+static uint32_t wd_diag_pass_entry_tick = 0;
+static uint32_t wd_diag_pass_exit_tick = 0;
+static bool     wd_diag_have_exit = false;
+static uint32_t wd_diag_passes = 0;
+static uint32_t wd_diag_longest_pass_ticks = 0;
+static uint32_t wd_diag_longest_between_ticks = 0; // pass exit -> next pass entry (sleep + stack)
+
+void ble_bed_service_debug_pass_begin(void)
+{
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  if (wd_diag_have_exit && (now - wd_diag_pass_exit_tick) > wd_diag_longest_between_ticks) {
+    wd_diag_longest_between_ticks = now - wd_diag_pass_exit_tick;
+  }
+  wd_diag_pass_entry_tick = now;
+}
+
+void ble_bed_service_debug_pass_end(void)
+{
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  if ((now - wd_diag_pass_entry_tick) > wd_diag_longest_pass_ticks) {
+    wd_diag_longest_pass_ticks = now - wd_diag_pass_entry_tick;
+  }
+  wd_diag_passes++;
+  wd_diag_pass_exit_tick = now;
+  wd_diag_have_exit = true;
+}
+#endif
+
 // The app-sourced move currently believed to be running, so a repeat with a
 // DIFFERENT target/dir can be turned into MOVE_STOP + MOVE_START (#4).
 static bool            app_move_active = false;
@@ -416,6 +459,30 @@ static void move_watchdog_callback(sl_sleeptimer_timer_handle_t *handle, void *d
 
 static void move_watchdog_arm(void)
 {
+#if BLE_DEBUG_WATCHDOG_LOG
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  if (move_watchdog_armed) {
+    // A refresh: keep the gap since the previous one.
+    uint32_t gap_ms = sl_sleeptimer_tick_to_ms(now - wd_diag_refresh_tick);
+    wd_diag_gap_ms[wd_diag_gap_next] = (gap_ms > 0xFFFF) ? 0xFFFF : (uint16_t)gap_ms;
+    wd_diag_gap_next = (uint8_t)((wd_diag_gap_next + 1) % WD_DIAG_GAPS);
+    if (wd_diag_gap_count < WD_DIAG_GAPS) {
+      wd_diag_gap_count++;
+    }
+  } else {
+    // A fresh arm starts a new hold: earlier gaps belong to an earlier hold.
+    wd_diag_gap_next = 0;
+    wd_diag_gap_count = 0;
+  }
+  wd_diag_refresh_tick = now;
+  wd_diag_refresh_rx_tick = wd_diag_processing_rx_tick;
+  // Still the write being processed: only called from process_pending_command(),
+  // and no stack event can be dispatched between its snapshot and here.
+  wd_diag_refresh_opcode = pending_command[CMD_OFS_OPCODE];
+  wd_diag_passes = 0;
+  wd_diag_longest_pass_ticks = 0;
+  wd_diag_longest_between_ticks = 0;
+#endif
   move_watchdog_armed = true;
   move_watchdog_expired = false;
   sl_sleeptimer_restart_timer(&move_watchdog_timer,
@@ -498,12 +565,39 @@ static void service_move_watchdog(void)
   // No MOVE_START repeat for MOVE_WATCHDOG_MS - the app is gone, the phone
   // slept, or the link stalled. Full clean stop, then tell the app why if it
   // is still there to hear it.
+#if BLE_DEBUG_WATCHDOG_LOG
+  // Snapshot before the stop, so the stop's own I2C time is not counted.
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  uint32_t since_refresh_ms = sl_sleeptimer_tick_to_ms(now - wd_diag_refresh_tick);
+  uint32_t since_rx_ms      = sl_sleeptimer_tick_to_ms(now - wd_diag_rx_tick);
+  uint32_t this_pass_ms     = sl_sleeptimer_tick_to_ms(now - wd_diag_pass_entry_tick);
+#endif
   move_watchdog_cancel();
   app_move_active = false;
   app_auto_level_active = false;   // hold-to-run auto-level is watchdogged too (#36)
   note_app_stopped_commanding();
   app_stop_all();
   send_command_result(control_connection, last_move_seq, CMD_RESULT_WATCHDOG_STOP);
+#if BLE_DEBUG_WATCHDOG_LOG
+  // Printed after the stop and the result, so the print cannot delay either.
+  printf("BLE: WATCHDOG STOP op=0x%02x seq=%u: %lu ms since last refresh (limit %u ms)\r\n",
+         wd_diag_refresh_opcode, last_move_seq,
+         (unsigned long)since_refresh_ms, (unsigned)MOVE_WATCHDOG_MS);
+  printf("  last write received %lu ms ago (op=0x%02x seq=%u); last refresh received->processed %lu ms\r\n",
+         (unsigned long)since_rx_ms, pending_command[CMD_OFS_OPCODE], pending_command[CMD_OFS_SEQ],
+         (unsigned long)sl_sleeptimer_tick_to_ms(wd_diag_refresh_tick - wd_diag_refresh_rx_tick));
+  printf("  since last refresh: %lu passes, longest pass %lu ms, longest between passes %lu ms, this pass %lu ms so far\r\n",
+         (unsigned long)wd_diag_passes,
+         (unsigned long)sl_sleeptimer_tick_to_ms(wd_diag_longest_pass_ticks),
+         (unsigned long)sl_sleeptimer_tick_to_ms(wd_diag_longest_between_ticks),
+         (unsigned long)this_pass_ms);
+  printf("  last %u refresh gaps, oldest first (ms):", (unsigned)wd_diag_gap_count);
+  for (uint8_t i = 0; i < wd_diag_gap_count; i++) {
+    uint8_t idx = (uint8_t)((wd_diag_gap_next + WD_DIAG_GAPS - wd_diag_gap_count + i) % WD_DIAG_GAPS);
+    printf("%s%u", (i == 0) ? " " : ",", (unsigned)wd_diag_gap_ms[idx]);
+  }
+  printf("\r\n");
+#endif
 }
 
 /*******************************************************************************
@@ -554,6 +648,9 @@ static void process_pending_command(void)
   uint8_t len  = pending_command_len;
   uint8_t conn = pending_command_conn;
   command_pending = false;
+#if BLE_DEBUG_WATCHDOG_LOG
+  wd_diag_processing_rx_tick = wd_diag_rx_tick;
+#endif
 
   // A short write has no trustworthy seq field. Echo 0 rather than a byte
   // that was never sent; the app sees REJECTED_INVALID either way.
@@ -1104,8 +1201,17 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
     // no-security, which the check below correctly ignores.
     case sl_bt_evt_connection_parameters_id: {
       const sl_bt_evt_connection_parameters_t *params = &evt->data.evt_connection_parameters;
+#if BLE_DEBUG_WATCHDOG_LOG
+      // sl_bt_api.h: interval x 1.25 ms; latency = connection intervals the
+      // peripheral may skip; timeout = supervision timeout x 10 ms. Latency
+      // above 0 would delay the phone's Command writes.
+      printf("BLE: connection_parameters conn=%u interval=%u latency=%u timeout=%u security_mode=%u\r\n",
+             params->connection, params->interval, params->latency, params->timeout,
+             params->security_mode);
+#else
       printf("BLE: connection_parameters conn=%u interval=%u security_mode=%u\r\n",
              params->connection, params->interval, params->security_mode);
+#endif
       if (params->security_mode != sl_bt_connection_mode1_level1) {
         if (assign_role_if_needed(params->connection)) {
           // The resumed-bond path: sm_bonded does not fire here (see the
@@ -1177,6 +1283,9 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
       pending_command_len  = av->value.len;
       pending_command_conn = av->connection;
       command_pending = true;
+#if BLE_DEBUG_WATCHDOG_LOG
+      wd_diag_rx_tick = sl_sleeptimer_get_tick_count(); // a tick read, not work
+#endif
       break;
     }
 
diff --git a/ble_bed_service.h b/ble_bed_service.h
index 7fd1068..20a76fe 100644
--- a/ble_bed_service.h
+++ b/ble_bed_service.h
@@ -26,6 +26,21 @@
 #include "sl_bt_api.h"
 #include "bed_status.h"
 
+// Bench diagnostics for dead-man watchdog stops (the app's "Connection
+// stalled" message, Command Result WATCHDOG_STOP). When 1, ble_bed_service.c
+// records refresh and Command-write ticks and app.c's app_process_action()
+// records its own pass times; one short block is printed on each watchdog
+// stop, and the connection_parameters print adds latency and supervision
+// timeout. Measures only: nothing blocks and nothing prints per pass. Set to
+// 0 before shipping.
+#define BLE_DEBUG_WATCHDOG_LOG 1
+
+#if BLE_DEBUG_WATCHDOG_LOG
+// Called first and last in app_process_action(), to time each main-loop pass.
+void ble_bed_service_debug_pass_begin(void);
+void ble_bed_service_debug_pass_end(void);
+#endif
+
 // Packs a bed_status_t snapshot into the 20-byte Bed Status characteristic
 // wire format (BLE_GATT_DESIGN.md #6), little-endian, matching gattdb_
 // bed_status_len exactly. tilt_fresh is passed in separately rather than
```

## d. Raw output of commands run

No secrets appeared in any output; nothing is redacted. `<SDK>` in a command
line stands for `C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p`
(the `SDK_PATH` in `cmake_gcc/tilt_bed_controller.cmake:5`); the output lines
are verbatim. What is not reproduced, and why, is in section g.

### d.1 Git, grep and SDK lookups, in the order run

```
$ date
Fri Oct 23 15:59:37 PDT 2026
$ git status
On branch main
Your branch is up to date with 'origin/main'.

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   CLAUDE.md

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/prompts/PROMPT_Y_watchdog-diagnostics-and-followups.md

no changes added to commit (use "git add" and/or "git commit -a")
$ git log --oneline -8
18d8378 Handoff 2026-10-18 02: Prompt W (bond policy, close refused pairings, settings holds)
9ba0ca3 Docs cleanup: #19 forward pointer, P3.13 done, sampler design debounce note
840d7b8 Settings mode: STOP cancels holds, releases clear stale timeouts, entry needs BED LIGHTS down
c6ee6b0 Bonding: overwrite least recently used bond when full; close refused pairings (DECISIONS #43)
9798c5a Record 2026-10-18 Prompt V bench result (docs only)
3f11c12 Handoff 2026-10-18 01: Prompt V (sampler bench result, switch follow-ups, settings exit, pairing window)
944f19e Pairing window: new bonds only while settings mode is active (DECISIONS #42)
dd8d069 Settings mode: exit on a new BED LIGHTS press
$ git rev-parse HEAD origin/main
18d83780dcffedd64f1d18544808fa01024ba71b
18d83780dcffedd64f1d18544808fa01024ba71b
$ git diff CLAUDE.md
warning: in the working copy of 'CLAUDE.md', LF will be replaced by CRLF the next time Git touches it
diff --git a/CLAUDE.md b/CLAUDE.md
index 51e82fc..979f15e 100644
--- a/CLAUDE.md
+++ b/CLAUDE.md
@@ -52,11 +52,11 @@ understanding, not just correctness:
   that used to require a hand-edited config file — if you ever find
   yourself wanting to hand-edit a generated file, that is a signal to
   stop and ask, not a shortcut to take.
-- **No BLE glue in this phase.** The radio stays dormant until the
-  migrated firmware passes the full `docs/REGRESSION.md` pass on hardware
-  (DECISIONS #20). Don't add `sl_bt_on_event` handling, GATT logic, or
-  anything beyond what the stock "Bluetooth – SoC Empty" sample already
-  provides, even if it looks like a natural next step.
+- **BLE status.** BLE is built and in use: Stage B (telemetry) and Stage C
+  (the app drives the bed, DECISIONS #34), the pairing window (#42) and the
+  bond/refusal handling (#43). DECISIONS.md is the authoritative record.
+  (`docs/MIGRATION_STATUS.md` still describes Stage C as not started and is
+  being corrected.) BLE work is done only when a prompt asks for it.
 - **Keep the `bed_` module prefix.** Only cosmetic "Totem"/"Totem
   Sleep"→"Tilt Bed"/"Tilt Bed Systems" string changes are in scope; don't
   rename modules, functions, or the prefix.
$ git diff --stat .projectlinkstore
 .projectlinkstore | 2 +-
 1 file changed, 1 insertion(+), 1 deletion(-)
$ git log --date=short --format="%h %ad %s" ab12d09 a22406f 5351aef 944f19e c6ee6b0 840d7b8 --no-walk
840d7b8 2026-10-18 Settings mode: STOP cancels holds, releases clear stale timeouts, entry needs BED LIGHTS down
c6ee6b0 2026-10-18 Bonding: overwrite least recently used bond when full; close refused pairings (DECISIONS #43)
944f19e 2026-10-18 Pairing window: new bonds only while settings mode is active (DECISIONS #42)
5351aef 2026-08-04 Auto-level hold-to-run; two-source conflict becomes a latched inhibit
a22406f 2026-08-02 Fix the 10 Hz app-move stutter: a panel check running on app moves
ab12d09 2026-08-02 Stage C: the app drives the bed
$ git show --stat --format="%h %s" 9ba0ca3 18d8378
9ba0ca3 Docs cleanup: #19 forward pointer, P3.13 done, sampler design debounce note

 docs/DECISIONS.md             | 2 ++
 docs/REFACTOR_PLAN.md         | 2 ++
 docs/SWITCH_SAMPLER_DESIGN.md | 2 ++
 3 files changed, 6 insertions(+)
18d8378 Handoff 2026-10-18 02: Prompt W (bond policy, close refused pairings, settings holds)

 docs/SESSION_LOG.md                                |   41 +
 ..._bonding-policy-refused-close-settings-holds.md | 2022 ++++++++++++++++++++
 2 files changed, 2063 insertions(+)
$ grep -rn "app_stop_all()" --include=*.c .
./bed_actions.c:323:// This is what a two-source conflict uses. app_stop_all() clears every panel
./bed_actions.c:327:// output itself was never touched by app_stop_all() - only its indicator -
./bed_actions.c:330:// STOP_ALL still calls app_stop_all() directly and keeps its full behaviour.
./bed_actions.c:343:  // Movement indicators ONLY. This used to call app_stop_all(), whose
./bed_control.c:73: * app_stop_all() via ARBITRATE_FORCE_STOP, matching DECISIONS.md #12's "any
./ble_bed_service.c:39:#include "bed_actions.h"             // app_stop_all(), the app-path output helpers
./ble_bed_service.c:237:// loop, not in the disconnect callback: app_stop_all() drives LEDs over I2C
./ble_bed_service.c:474:    // app_stop_all() would cut across that sequence.
./ble_bed_service.c:505:  app_stop_all();
./ble_bed_service.c:535:    app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
./ble_bed_service.c:783:    // app_stop_all() drives LEDs over I2C. Blocking I2C in a stack callback
./ble_bed_service.c:883:    app_stop_all();
$ grep -n "0x1022\|0x1016\|0x1013)" <SDK>/platform_core/platform/common/inc/sl_status.h
271:#define SL_STATUS_BT_CTRL_REMOTE_USER_TERMINATED                                                         ((sl_status_t)0x1013)     ///< User on the remote device terminated the connection.
274:#define SL_STATUS_BT_CTRL_CONNECTION_TERMINATED_BY_LOCAL_HOST                                            ((sl_status_t)0x1016)     ///< Local device terminated the connection.
286:#define SL_STATUS_BT_CTRL_LL_RESPONSE_TIMEOUT                                                            ((sl_status_t)0x1022)     ///< Connection terminated due to link-layer procedure timeout.
$ grep -n "uint16_t latency\|uint16_t timeout\|struct sl_bt_evt_connection_parameters_s" <SDK>/bluetooth_le_host/inc/sl_bt_api.h
4683:                                              uint16_t timeout);
4776:                                                   uint16_t timeout,
5080:                                                                    uint16_t timeout,
5174:                                                            uint16_t timeout,
5273:                                                                              uint16_t timeout,
5360:                                                                      uint16_t timeout,
6947:PACKSTRUCT( struct sl_bt_evt_connection_parameters_s
6951:  uint16_t latency;       /**< Peripheral latency (how many connection intervals
6953:  uint16_t timeout;       /**< Supervision timeout. Time = Value x 10 ms */
6968:typedef struct sl_bt_evt_connection_parameters_s sl_bt_evt_connection_parameters_t;
$ git add CLAUDE.md docs/MIGRATION_STATUS.md docs/DECISIONS.md docs/REGRESSION.md docs/prompts/PROMPT_Y_watchdog-diagnostics-and-followups.md && git status --short && git commit -q -F - <<EOF ... && git log --oneline -1
warning: in the working copy of 'CLAUDE.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/MIGRATION_STATUS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/prompts/PROMPT_Y_watchdog-diagnostics-and-followups.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  CLAUDE.md
M  docs/DECISIONS.md
M  docs/MIGRATION_STATUS.md
M  docs/REGRESSION.md
A  docs/prompts/PROMPT_Y_watchdog-diagnostics-and-followups.md
f6614c8 Docs: Stage C status, Prompt W bench result, DECISIONS #44, pairing/STOP checks
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
$ git diff settings_mode.c
warning: in the working copy of 'settings_mode.c', LF will be replaced by CRLF the next time Git touches it
diff --git a/settings_mode.c b/settings_mode.c
index 1fe9165..2c7bcfa 100644
--- a/settings_mode.c
+++ b/settings_mode.c
@@ -272,9 +272,10 @@ void settings_mode_tick(void)
 {
   // switch_pressed is only the direction of the LAST edge delivered, for any
   // button, so the entry hold also requires BED LIGHTS to be physically down
-  // in the sampler's debounced state (#41).
+  // in the sampler's debounced state (#41) - and that state to be fresh, so
+  // stale data counts as "not down", as it does for the motion fail-safe.
   if ((switch_pressed == true) && (active_switch == SWITCH_BED_LIGHT_VALUE) && (!in_settings_mode)
-      && ((switch_sampler_down() & SWITCH_BED_LIGHTS) != 0)){
+      && switch_state_fresh() && ((switch_sampler_down() & SWITCH_BED_LIGHTS) != 0)){
     sl_sleeptimer_is_timer_running(&handle_5000ms_timer, &is_5000ms_timer_running);
 
     if (!is_5000ms_timer_running){
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/settings_mode.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
$ git add settings_mode.c && git commit -q -F - <<EOF ... && git log --oneline -1
warning: in the working copy of 'settings_mode.c', LF will be replaced by CRLF the next time Git touches it
17073a3 Settings-mode entry: require fresh sampler state
$ build (BLE_DEBUG_WATCHDOG_LOG 1)
[1/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[2/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
$ sed -i "s/^#define BLE_DEBUG_WATCHDOG_LOG 1$/#define BLE_DEBUG_WATCHDOG_LOG 0/" ble_bed_service.h && grep -n "define BLE_DEBUG_WATCHDOG_LOG" ble_bed_service.h
36:#define BLE_DEBUG_WATCHDOG_LOG 0
$ build (BLE_DEBUG_WATCHDOG_LOG 0)
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
$ sed -i (back to 1) && grep
36:#define BLE_DEBUG_WATCHDOG_LOG 1
$ build (BLE_DEBUG_WATCHDOG_LOG 1 again)
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
$ grep -rn -- "-Wall\|-Wextra" cmake_gcc/*.cmake cmake_gcc/CMakeLists.txt
cmake_gcc/tilt_bed_controller.cmake:363:    $<$<COMPILE_LANGUAGE:C>:-Wall>
cmake_gcc/tilt_bed_controller.cmake:364:    $<$<COMPILE_LANGUAGE:C>:-Wextra>
cmake_gcc/tilt_bed_controller.cmake:379:    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
cmake_gcc/tilt_bed_controller.cmake:380:    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
$ git diff --stat
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.h', LF will be replaced by CRLF the next time Git touches it
 .projectlinkstore |   2 +-
 app.c             |   8 ++++
 ble_bed_service.c | 109 ++++++++++++++++++++++++++++++++++++++++++++++++++++++
 ble_bed_service.h |  15 ++++++++
 4 files changed, 133 insertions(+), 1 deletion(-)
$ arm-none-eabi-size (if available)
$ arm-none-eabi-size cmake_gcc/build/base/tilt_bed_controller.out
   text	   data	    bss	    dec	    hex	filename
 244596	    780	  72480	 317856	  4d9a0	cmake_gcc/build/base/tilt_bed_controller.out
$ git diff app.c ble_bed_service.c ble_bed_service.h
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.h', LF will be replaced by CRLF the next time Git touches it
diff --git a/app.c b/app.c
index 9326733..2c36b16 100644
--- a/app.c
+++ b/app.c
@@ -553,6 +553,10 @@ void app_process_action(void)
 {
   bool local_heartbeat;
 
+#if BLE_DEBUG_WATCHDOG_LOG
+  ble_bed_service_debug_pass_begin(); // pass timing for the watchdog-stop print; measures only
+#endif
+
   // If there is a collision --> stop all motors, then flash all LEDs a few times
   // Once this sequence is done (which will take a few seconds), then collision can be
   // set back to false and motor operation can resume
@@ -745,6 +749,10 @@ void app_process_action(void)
 
   bed_control_tick();
   ble_bed_service_tick();
+
+#if BLE_DEBUG_WATCHDOG_LOG
+  ble_bed_service_debug_pass_end();
+#endif
 }
 
 /**************************************************************************//**
diff --git a/ble_bed_service.c b/ble_bed_service.c
index d554398..bbbafd8 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -245,6 +245,49 @@ static volatile bool move_watchdog_expired = false;  // set in timer ISR context
 static bool    move_watchdog_armed = false;
 static uint8_t last_move_seq = 0;                    // echoed on WATCHDOG_STOP
 
+#if BLE_DEBUG_WATCHDOG_LOG
+// Watchdog-stop diagnostics (BLE_DEBUG_WATCHDOG_LOG, ble_bed_service.h).
+// Sleeptimer ticks only; printed solely by service_move_watchdog() on expiry.
+// Everything here is written from main-loop context (the stack callback runs
+// from the super loop too - see process_pending_command()), so no volatile.
+#define WD_DIAG_GAPS 8
+static uint32_t wd_diag_rx_tick = 0;              // last Command write into pending_command[] (stack callback)
+static uint32_t wd_diag_processing_rx_tick = 0;   // receive tick of the write being processed now
+static uint32_t wd_diag_refresh_tick = 0;         // last move_watchdog_arm()
+static uint32_t wd_diag_refresh_rx_tick = 0;      // receive tick of the write that did that arm
+static uint8_t  wd_diag_refresh_opcode = 0;
+static uint16_t wd_diag_gap_ms[WD_DIAG_GAPS];     // ring buffer: ms between consecutive refreshes
+static uint8_t  wd_diag_gap_next = 0;
+static uint8_t  wd_diag_gap_count = 0;
+// Main-loop pass timing since the last arm/refresh, fed by app.c.
+static uint32_t wd_diag_pass_entry_tick = 0;
+static uint32_t wd_diag_pass_exit_tick = 0;
+static bool     wd_diag_have_exit = false;
+static uint32_t wd_diag_passes = 0;
+static uint32_t wd_diag_longest_pass_ticks = 0;
+static uint32_t wd_diag_longest_between_ticks = 0; // pass exit -> next pass entry (sleep + stack)
+
+void ble_bed_service_debug_pass_begin(void)
+{
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  if (wd_diag_have_exit && (now - wd_diag_pass_exit_tick) > wd_diag_longest_between_ticks) {
+    wd_diag_longest_between_ticks = now - wd_diag_pass_exit_tick;
+  }
+  wd_diag_pass_entry_tick = now;
+}
+
+void ble_bed_service_debug_pass_end(void)
+{
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  if ((now - wd_diag_pass_entry_tick) > wd_diag_longest_pass_ticks) {
+    wd_diag_longest_pass_ticks = now - wd_diag_pass_entry_tick;
+  }
+  wd_diag_passes++;
+  wd_diag_pass_exit_tick = now;
+  wd_diag_have_exit = true;
+}
+#endif
+
 // The app-sourced move currently believed to be running, so a repeat with a
 // DIFFERENT target/dir can be turned into MOVE_STOP + MOVE_START (#4).
 static bool            app_move_active = false;
@@ -416,6 +459,30 @@ static void move_watchdog_callback(sl_sleeptimer_timer_handle_t *handle, void *d
 
 static void move_watchdog_arm(void)
 {
+#if BLE_DEBUG_WATCHDOG_LOG
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  if (move_watchdog_armed) {
+    // A refresh: keep the gap since the previous one.
+    uint32_t gap_ms = sl_sleeptimer_tick_to_ms(now - wd_diag_refresh_tick);
+    wd_diag_gap_ms[wd_diag_gap_next] = (gap_ms > 0xFFFF) ? 0xFFFF : (uint16_t)gap_ms;
+    wd_diag_gap_next = (uint8_t)((wd_diag_gap_next + 1) % WD_DIAG_GAPS);
+    if (wd_diag_gap_count < WD_DIAG_GAPS) {
+      wd_diag_gap_count++;
+    }
+  } else {
+    // A fresh arm starts a new hold: earlier gaps belong to an earlier hold.
+    wd_diag_gap_next = 0;
+    wd_diag_gap_count = 0;
+  }
+  wd_diag_refresh_tick = now;
+  wd_diag_refresh_rx_tick = wd_diag_processing_rx_tick;
+  // Still the write being processed: only called from process_pending_command(),
+  // and no stack event can be dispatched between its snapshot and here.
+  wd_diag_refresh_opcode = pending_command[CMD_OFS_OPCODE];
+  wd_diag_passes = 0;
+  wd_diag_longest_pass_ticks = 0;
+  wd_diag_longest_between_ticks = 0;
+#endif
   move_watchdog_armed = true;
   move_watchdog_expired = false;
   sl_sleeptimer_restart_timer(&move_watchdog_timer,
@@ -498,12 +565,39 @@ static void service_move_watchdog(void)
   // No MOVE_START repeat for MOVE_WATCHDOG_MS - the app is gone, the phone
   // slept, or the link stalled. Full clean stop, then tell the app why if it
   // is still there to hear it.
+#if BLE_DEBUG_WATCHDOG_LOG
+  // Snapshot before the stop, so the stop's own I2C time is not counted.
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  uint32_t since_refresh_ms = sl_sleeptimer_tick_to_ms(now - wd_diag_refresh_tick);
+  uint32_t since_rx_ms      = sl_sleeptimer_tick_to_ms(now - wd_diag_rx_tick);
+  uint32_t this_pass_ms     = sl_sleeptimer_tick_to_ms(now - wd_diag_pass_entry_tick);
+#endif
   move_watchdog_cancel();
   app_move_active = false;
   app_auto_level_active = false;   // hold-to-run auto-level is watchdogged too (#36)
   note_app_stopped_commanding();
   app_stop_all();
   send_command_result(control_connection, last_move_seq, CMD_RESULT_WATCHDOG_STOP);
+#if BLE_DEBUG_WATCHDOG_LOG
+  // Printed after the stop and the result, so the print cannot delay either.
+  printf("BLE: WATCHDOG STOP op=0x%02x seq=%u: %lu ms since last refresh (limit %u ms)\r\n",
+         wd_diag_refresh_opcode, last_move_seq,
+         (unsigned long)since_refresh_ms, (unsigned)MOVE_WATCHDOG_MS);
+  printf("  last write received %lu ms ago (op=0x%02x seq=%u); last refresh received->processed %lu ms\r\n",
+         (unsigned long)since_rx_ms, pending_command[CMD_OFS_OPCODE], pending_command[CMD_OFS_SEQ],
+         (unsigned long)sl_sleeptimer_tick_to_ms(wd_diag_refresh_tick - wd_diag_refresh_rx_tick));
+  printf("  since last refresh: %lu passes, longest pass %lu ms, longest between passes %lu ms, this pass %lu ms so far\r\n",
+         (unsigned long)wd_diag_passes,
+         (unsigned long)sl_sleeptimer_tick_to_ms(wd_diag_longest_pass_ticks),
+         (unsigned long)sl_sleeptimer_tick_to_ms(wd_diag_longest_between_ticks),
+         (unsigned long)this_pass_ms);
+  printf("  last %u refresh gaps, oldest first (ms):", (unsigned)wd_diag_gap_count);
+  for (uint8_t i = 0; i < wd_diag_gap_count; i++) {
+    uint8_t idx = (uint8_t)((wd_diag_gap_next + WD_DIAG_GAPS - wd_diag_gap_count + i) % WD_DIAG_GAPS);
+    printf("%s%u", (i == 0) ? " " : ",", (unsigned)wd_diag_gap_ms[idx]);
+  }
+  printf("\r\n");
+#endif
 }
 
 /*******************************************************************************
@@ -554,6 +648,9 @@ static void process_pending_command(void)
   uint8_t len  = pending_command_len;
   uint8_t conn = pending_command_conn;
   command_pending = false;
+#if BLE_DEBUG_WATCHDOG_LOG
+  wd_diag_processing_rx_tick = wd_diag_rx_tick;
+#endif
 
   // A short write has no trustworthy seq field. Echo 0 rather than a byte
   // that was never sent; the app sees REJECTED_INVALID either way.
@@ -1104,8 +1201,17 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
     // no-security, which the check below correctly ignores.
     case sl_bt_evt_connection_parameters_id: {
       const sl_bt_evt_connection_parameters_t *params = &evt->data.evt_connection_parameters;
+#if BLE_DEBUG_WATCHDOG_LOG
+      // sl_bt_api.h: interval x 1.25 ms; latency = connection intervals the
+      // peripheral may skip; timeout = supervision timeout x 10 ms. Latency
+      // above 0 would delay the phone's Command writes.
+      printf("BLE: connection_parameters conn=%u interval=%u latency=%u timeout=%u security_mode=%u\r\n",
+             params->connection, params->interval, params->latency, params->timeout,
+             params->security_mode);
+#else
       printf("BLE: connection_parameters conn=%u interval=%u security_mode=%u\r\n",
              params->connection, params->interval, params->security_mode);
+#endif
       if (params->security_mode != sl_bt_connection_mode1_level1) {
         if (assign_role_if_needed(params->connection)) {
           // The resumed-bond path: sm_bonded does not fire here (see the
@@ -1177,6 +1283,9 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
       pending_command_len  = av->value.len;
       pending_command_conn = av->connection;
       command_pending = true;
+#if BLE_DEBUG_WATCHDOG_LOG
+      wd_diag_rx_tick = sl_sleeptimer_get_tick_count(); // a tick read, not work
+#endif
       break;
     }
 
diff --git a/ble_bed_service.h b/ble_bed_service.h
index 7fd1068..20a76fe 100644
--- a/ble_bed_service.h
+++ b/ble_bed_service.h
@@ -26,6 +26,21 @@
 #include "sl_bt_api.h"
 #include "bed_status.h"
 
+// Bench diagnostics for dead-man watchdog stops (the app's "Connection
+// stalled" message, Command Result WATCHDOG_STOP). When 1, ble_bed_service.c
+// records refresh and Command-write ticks and app.c's app_process_action()
+// records its own pass times; one short block is printed on each watchdog
+// stop, and the connection_parameters print adds latency and supervision
+// timeout. Measures only: nothing blocks and nothing prints per pass. Set to
+// 0 before shipping.
+#define BLE_DEBUG_WATCHDOG_LOG 1
+
+#if BLE_DEBUG_WATCHDOG_LOG
+// Called first and last in app_process_action(), to time each main-loop pass.
+void ble_bed_service_debug_pass_begin(void);
+void ble_bed_service_debug_pass_end(void);
+#endif
+
 // Packs a bed_status_t snapshot into the 20-byte Bed Status characteristic
 // wire format (BLE_GATT_DESIGN.md #6), little-endian, matching gattdb_
 // bed_status_len exactly. tilt_fresh is passed in separately rather than
$ git add app.c ble_bed_service.c ble_bed_service.h && git commit -q -F - <<EOF ... && git log --oneline -1
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.h', LF will be replaced by CRLF the next time Git touches it
c7d743d Watchdog-stop diagnostics behind BLE_DEBUG_WATCHDOG_LOG (bench only)
$ date
Fri Oct 23 16:12:47 PDT 2026
$ git log --date=short --format="%h %ad %s" -5
c7d743d 2026-10-23 Watchdog-stop diagnostics behind BLE_DEBUG_WATCHDOG_LOG (bench only)
17073a3 2026-10-23 Settings-mode entry: require fresh sampler state
f6614c8 2026-10-23 Docs: Stage C status, Prompt W bench result, DECISIONS #44, pairing/STOP checks
18d8378 2026-10-18 Handoff 2026-10-18 02: Prompt W (bond policy, close refused pairings, settings holds)
9ba0ca3 2026-10-18 Docs cleanup: #19 forward pointer, P3.13 done, sampler design debounce note
$ ls docs/handoffs | grep 2026-10-23
$ git show --stat --format="%h %s" f6614c8 17073a3 c7d743d
f6614c8 Docs: Stage C status, Prompt W bench result, DECISIONS #44, pairing/STOP checks

 CLAUDE.md                                          |  10 +-
 docs/DECISIONS.md                                  | 101 +++++++++++++++++++
 docs/MIGRATION_STATUS.md                           |  59 +++++++----
 docs/REGRESSION.md                                 |  43 ++++++++
 .../PROMPT_Y_watchdog-diagnostics-and-followups.md | 109 +++++++++++++++++++++
 5 files changed, 297 insertions(+), 25 deletions(-)
17073a3 Settings-mode entry: require fresh sampler state

 settings_mode.c | 5 +++--
 1 file changed, 3 insertions(+), 2 deletions(-)
c7d743d Watchdog-stop diagnostics behind BLE_DEBUG_WATCHDOG_LOG (bench only)

 app.c             |   8 ++++
 ble_bed_service.c | 109 ++++++++++++++++++++++++++++++++++++++++++++++++++++++
 ble_bed_service.h |  15 ++++++++
 3 files changed, 132 insertions(+)
$ git status
On branch main
Your branch is ahead of 'origin/main' by 3 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
$ sed -n 66,80p bed_control.c
/*
 * Releases the lock lazily (if the bed is already idle), then either grants
 * the lock to an idle bed, confirms the calling source already holds it, or
 * signals that a different source holds it. Every verb - including the
 * stop-class ones - runs through this uniformly: a stop request from the
 * source that already holds the lock is just that source's own natural
 * stop; a stop (or any other command) from the other source becomes a full
 * app_stop_all() via ARBITRATE_FORCE_STOP, matching DECISIONS.md #12's "any
 * input whatsoever from the other source is interpreted ONLY as an
 * immediate clean STOP" - not a partial, verb-specific stop.
 */
static arbitrate_result_t arbitrate(bed_source_t src)
{
  if (app_bed_is_idle()) {
    active_source = BED_SOURCE_NONE;
$ grep -n "app_stop_movement\|app_stop_all" bed_control.c
73: * app_stop_all() via ARBITRATE_FORCE_STOP, matching DECISIONS.md #12's "any
104:    app_stop_movement();
116:    app_stop_movement();
132:    app_stop_movement();
144:    app_stop_movement();
157:    app_stop_movement();
$ grep -n "BLE_DEBUG_WATCHDOG_LOG\|wd_diag_rx_tick =\|wd_diag_processing_rx_tick =\|static void move_watchdog_arm\|WATCHDOG STOP\|debug_pass_begin\|debug_pass_end\|latency=%u\|move_watchdog_cancel();\|app_stop_all();\|send_command_result(control_connection" ble_bed_service.c ble_bed_service.h app.c settings_mode.c
ble_bed_service.c:248:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:249:// Watchdog-stop diagnostics (BLE_DEBUG_WATCHDOG_LOG, ble_bed_service.h).
ble_bed_service.c:254:static uint32_t wd_diag_rx_tick = 0;              // last Command write into pending_command[] (stack callback)
ble_bed_service.c:255:static uint32_t wd_diag_processing_rx_tick = 0;   // receive tick of the write being processed now
ble_bed_service.c:270:void ble_bed_service_debug_pass_begin(void)
ble_bed_service.c:279:void ble_bed_service_debug_pass_end(void)
ble_bed_service.c:460:static void move_watchdog_arm(void)
ble_bed_service.c:462:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:542:    move_watchdog_cancel();
ble_bed_service.c:568:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:575:  move_watchdog_cancel();
ble_bed_service.c:579:  app_stop_all();
ble_bed_service.c:580:  send_command_result(control_connection, last_move_seq, CMD_RESULT_WATCHDOG_STOP);
ble_bed_service.c:581:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:583:  printf("BLE: WATCHDOG STOP op=0x%02x seq=%u: %lu ms since last refresh (limit %u ms)\r\n",
ble_bed_service.c:625:    move_watchdog_cancel();
ble_bed_service.c:629:    app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
ble_bed_service.c:651:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:652:  wd_diag_processing_rx_tick = wd_diag_rx_tick;
ble_bed_service.c:737:          move_watchdog_cancel();
ble_bed_service.c:758:      move_watchdog_cancel();
ble_bed_service.c:776:        move_watchdog_cancel();
ble_bed_service.c:797:      move_watchdog_cancel();
ble_bed_service.c:976:    move_watchdog_cancel();
ble_bed_service.c:980:    app_stop_all();
ble_bed_service.c:1204:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:1208:      printf("BLE: connection_parameters conn=%u interval=%u latency=%u timeout=%u security_mode=%u\r\n",
ble_bed_service.c:1286:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:1287:      wd_diag_rx_tick = sl_sleeptimer_get_tick_count(); // a tick read, not work
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
ble_bed_service.h:38:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.h:40:void ble_bed_service_debug_pass_begin(void);
ble_bed_service.h:41:void ble_bed_service_debug_pass_end(void);
app.c:556:#if BLE_DEBUG_WATCHDOG_LOG
app.c:557:  ble_bed_service_debug_pass_begin(); // pass timing for the watchdog-stop print; measures only
app.c:753:#if BLE_DEBUG_WATCHDOG_LOG
app.c:754:  ble_bed_service_debug_pass_end();
settings_mode.c:278:      && switch_state_fresh() && ((switch_sampler_down() & SWITCH_BED_LIGHTS) != 0)){
$ grep -n "void scan_all_currents" -A20 adc_currents.c
$ grep -n "void set_dual_motors_direction_up\|void set_dual_motors_on" -A12 dual_motor_control.c
347:void set_dual_motors_on(side_of_bed_t side)
348-
349-{
350-  switch (side) {
351-    case HEAD:
352-      // If trying to raise HEAD and BOTH Motor A and Motor D (HEAD) are NOT at UPPER limit switch, move UP
353-      if ((motor_state[MOTORA].motor_up && motor_state[MOTORD].motor_up) && !(at_limit_switch[MOTORA].upper || at_limit_switch[MOTORD].upper)){
354-          set_motor_on(MOTORA);
355-          motor_state[MOTORA].motor_on = true;
356-
357-          set_motor_on(MOTORD);
358-          motor_state[MOTORD].motor_on = true;
359-      }
--
464:void set_dual_motors_direction_up (side_of_bed_t side)
465-{
466-  switch (side) {
467-    case HEAD:
468-      set_motor_direction_up(MOTORA);
469-      motor_state[MOTORA].motor_up = true;
470-
471-      set_motor_direction_up(MOTORD);
472-      motor_state[MOTORD].motor_up = true;
473-      break;
474-    case LEFT:
475-      set_motor_direction_up(MOTORA);
476-      motor_state[MOTORA].motor_up = true;
```

### d.2 Builds (verbatim)

Method (same as the previous two handoffs): a helper script in the session
scratchpad puts the Silicon Labs ninja and cmake directories on `PATH`
(`.../conan/p/ninja1a38fc85adcf7/p`, `.../conan/p/cmakefa35ab0687064/p/bin`),
runs `cmake --build --preset default_config` in `cmake_gcc/`, writes all
output plus `exit N` to a file, prints it, then prints
`grep -n -i 'warning\|error'` over it. An empty grep means none were found.
Warnings flags: `-Wall -Wextra` (`cmake_gcc/tilt_bed_controller.cmake:363-364`).

Baseline, before any edit (HEAD `18d8378`):

```
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
```

After commit A `f6614c8` (docs only):

```
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
```

After the part B edit (before commit `17073a3`):

```
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/settings_mode.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

After the part C edits, `BLE_DEBUG_WATCHDOG_LOG` 1 (before commit `c7d743d`):

```
[1/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[2/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

Temporary `sed` to `BLE_DEBUG_WATCHDOG_LOG 0` (`ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 0`), build:

```
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

`sed` back to 1 (`ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1`), build — this is the image now in `cmake_gcc/build/`, the code of `c7d743d`:

```
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

No build produced a warning or an error. The same six builds, with the `sed`
and `grep` lines between them and `arm-none-eabi-size`, also appear in d.1 as
they were run.

### d.3 The Prompt W bench log (user's run, 2026-10-18), with line numbers

`ls -la` and `wc -l`, then `cat -n`:

```
-rw-r--r-- 1 adunc 197610 5702 Oct 18 16:09 C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/PuTTY Logs/prompt_w_bench.log
179 C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/PuTTY Logs/prompt_w_bench.log
     1	Welcome to the Tilt Bed application
     2	Application Version 1.1.0 
     3	Welcome to the Tilt Bed application
     4	Application Version 1.1.0 
     5	Accelerometer is sampling ...
     6	NVM3 initialized successfully
     7	Loaded Roll Angle: 0
     8	Loaded Pitch Angle: 0
     9	Loaded Raise Collision Sensitivity: 550
    10	Loaded Lower Collision Sensitivity: 275
    11	BLE SM: configure -> 0x0000
    12	BLE SM: store_bonding_configuration -> 0x0000
    13	BLE SM: set_bondable_mode(1) -> 0x0000
    14	BLE: connection_opened conn=1 bonding=0x01
    15	BLE: connection_parameters conn=1 interval=24 security_mode=0
    16	BLE: characteristic_status DROPPED conn=1 client_config_flags=0x0001 (matches neither control nor viewer connection)
    17	BLE: connection_parameters conn=1 interval=24 security_mode=1
    18	BLE: cccd_seed conn=1 bed_status=0x0001 sc=0x0000
    19	BLE: connection_parameters conn=1 interval=24 security_mode=1
    20	Max Current for Motor 0: 6
    21	Max Current for Motor 1: 843
    22	Max Current for Motor 2: 1195
    23	Max Current for Motor 3: 24
    24	BLE: connection_closed conn=1 reason=0x1013
    25	BLE: connection_opened conn=2 bonding=0x01
    26	BLE: connection_parameters conn=2 interval=24 security_mode=0
    27	BLE: characteristic_status DROPPED conn=2 client_config_flags=0x0001 (matches neither control nor viewer connection)
    28	BLE: connection_parameters conn=2 interval=24 security_mode=0
    29	BLE SM: confirm_bonding conn=2 handle=0x01 (REPLACING existing bond) -> REJECTED (not in settings mode)
    30	BLE SM: bonding_failed conn=2 reason=0x1205
    31	BLE: closing conn=2 - pairing refused
    32	BLE: connection_closed conn=2 reason=0x1022
    33	SW: 0x0008 down (bounces 0)
    34	Max Current for Motor 0: 0
    35	Max Current for Motor 1: 0
    36	Max Current for Motor 2: 0
    37	Max Current for Motor 3: 0
    38	SW: 0x0008 up (bounces 0)
    39	BLE: connection_opened conn=3 bonding=0x01
    40	BLE: connection_parameters conn=3 interval=24 security_mode=0
    41	BLE: characteristic_status DROPPED conn=3 client_config_flags=0x0001 (matches neither control nor viewer connection)
    42	BLE: connection_parameters conn=3 interval=24 security_mode=0
    43	BLE SM: confirm_bonding conn=3 handle=0x01 (REPLACING existing bond) -> ACCEPTED (settings mode)
    44	BLE: connection_parameters conn=3 interval=24 security_mode=1
    45	BLE: cccd_seed conn=3 bed_status=0x0001 sc=0x0000
    46	BLE SM: bonded conn=3 bonding=0x01 security_mode=1
    47	SW: 0x0008 down (bounces 0)
    48	Settings mode: exit
    49	SW: 0x0008 up (bounces 0)
    50	SW: 0x0008 down (bounces 0)
    51	SW: 0x0008 up (bounces 0)
    52	SW: 0x0008 down (bounces 0)
    53	SW: 0x0008 up (bounces 0)
    54	SW: 0x0001 down (bounces 0)
    55	2 second timer start
    56	SW: 0x0001 up (bounces 0)
    57	SW: 0x0001 down (bounces 0)
    58	2 second timer start
    59	SW: 0x0001 up (bounces 0)
    60	SW: 0x0001 down (bounces 0)
    61	2 second timer start
    62	2 second timer start
    63	Writing values to NVM3...
    64	Successfully stored raise security margin
    65	Successfully stored lower security margin
    66	Stored Raise Sensitivity is: 350
    67	Stored Lower Sensitivity is: 250
    68	2 second timer start
    69	SW: 0x0001 up (bounces 0)
    70	SW: 0x0004 down (bounces 0)
    71	2 second timer start
    72	2 second timer start
    73	Writing values to NVM3...
    74	Successfully stored raise security margin
    75	Successfully stored lower security margin
    76	Stored Raise Sensitivity is: 550
    77	Stored Lower Sensitivity is: 275
    78	2 second timer start
    79	SW: 0x0004 up (bounces 0)
    80	SW: 0x0008 down (bounces 0)
    81	Settings mode: exit
    82	SW: 0x0008 up (bounces 0)
    83	Step 1: ROLL and PITCH check
    84	Step 1: ROLL and PITCH check
    85	Step 1: ROLL and PITCH check
    86	Step 1: ROLL and PITCH check
    87	Step 2: ROLL Not Level
    88	Leveling...
    89	Step 1: ROLL and PITCH check
    90	Step 1: ROLL and PITCH check
    91	Step 1: ROLL and PITCH check
    92	Step 2: ROLL Not Level
    93	Leveling...
    94	Latch
    95	Pulse off...
    96	ROLL Leveled successfully...
    97	Axis set to PITCH
    98	Leveling...
    99	Max Current for Motor 0: 749
   100	Max Current for Motor 1: 892
   101	Max Current for Motor 2: 1196
   102	Max Current for Motor 3: 803
   103	Latch
   104	PITCH Leveled successfully...
   105	Exit auto-level
   106	Step 1: ROLL and PITCH check
   107	Step 2: ROLL Not Level
   108	Leveling...
   109	Latch
   110	Pulse off...
   111	ROLL Leveled successfully...
   112	Axis set to PITCH
   113	Leveling...
   114	Latch
   115	PITCH Leveled successfully...
   116	Exit auto-level
   117	Max Current for Motor 0: 484
   118	Max Current for Motor 1: 528
   119	Max Current for Motor 2: 702
   120	Max Current for Motor 3: 531
   121	Max Current for Motor 0: 766
   122	Max Current for Motor 1: 861
   123	Max Current for Motor 2: 14
   124	Max Current for Motor 3: 21
   125	Max Current for Motor 0: 6
   126	Max Current for Motor 1: 24
   127	Max Current for Motor 2: 1136
   128	Max Current for Motor 3: 913
   129	Max Current for Motor 0: 5
   130	Max Current for Motor 1: 852
   131	Max Current for Motor 2: 1219
   132	Max Current for Motor 3: 25
   133	Max Current for Motor 0: 757
   134	Max Current for Motor 1: 28
   135	Max Current for Motor 2: 18
   136	Max Current for Motor 3: 807
   137	Step 1: ROLL and PITCH check
   138	Step 1: ROLL and PITCH check
   139	Step 1: ROLL and PITCH check
   140	Step 2: ROLL Not Level
   141	Leveling...
   142	Max Current for Motor 0: 663
   143	Max Current for Motor 1: 773
   144	Max Current for Motor 2: 1160
   145	Max Current for Motor 3: 836
   146	Tripped on steady!
   147	Securit margin: 550
   148	Motor 0 Delta Current:  140
   149	Motor 1 Delta Current:  199
   150	Motor 2 Delta Current:  129
   151	Motor 3 Delta Current:  87
   152	Once
   153	Step 1: ROLL and PITCH check
   154	Step 2: ROLL Not Level
   155	Leveling...
   156	Latch
   157	Pulse off...
   158	ROLL Leveled successfully...
   159	Axis set to PITCH
   160	Leveling...
   161	Max Current for Motor 0: 860
   162	Max Current for Motor 1: 885
   163	Max Current for Motor 2: 1298
   164	Max Current for Motor 3: 856
   165	Latch
   166	PITCH Leveled successfully...
   167	Exit auto-level
   168	Max Current for Motor 0: 7
   169	Max Current for Motor 1: 26
   170	Max Current for Motor 2: 1268
   171	Max Current for Motor 3: 787
   172	Max Current for Motor 0: 739
   173	Max Current for Motor 1: 893
   174	Max Current for Motor 2: 14
   175	Max Current for Motor 3: 20
   176	Step 1: ROLL and PITCH check
   177	Step 1: ROLL and PITCH check
   178	Step 1: ROLL and PITCH check
   179	Step 1: ROLL and PITCH check
```

### d.4 Evidence excerpts (SDK and repo), run 2026-10-23 after the work commits

Line numbers in repo files are as of `c7d743d`.

```
$ sed -n 6945,6966p <SDK>/bluetooth_le_host/inc/sl_bt_api.h
 * @brief Data structure of the parameters event
 ******************************************************************************/
PACKSTRUCT( struct sl_bt_evt_connection_parameters_s
{
  uint8_t  connection;    /**< Connection handle */
  uint16_t interval;      /**< Connection interval. Time = Value x 1.25 ms */
  uint16_t latency;       /**< Peripheral latency (how many connection intervals
                               the peripheral can skip) */
  uint16_t timeout;       /**< Supervision timeout. Time = Value x 10 ms */
  uint8_t  security_mode; /**< Enum @ref sl_bt_connection_security_t. Connection
                               security mode. Values:
                                 - <b>sl_bt_connection_mode1_level1 (0x0):</b>
                                   No security
                                 - <b>sl_bt_connection_mode1_level2 (0x1):</b>
                                   Unauthenticated pairing with encryption
                                 - <b>sl_bt_connection_mode1_level3 (0x2):</b>
                                   Authenticated pairing with encryption
                                 - <b>sl_bt_connection_mode1_level4 (0x3):</b>
                                   Authenticated Secure Connections pairing with
                                   encryption using a 128-bit strength
                                   encryption key */
});
$ sed -n 260,268p;344,349p <SDK>/platform_core/platform/service/power_manager/src/sleep_loop/sl_power_manager.c

  sli_power_manager_suspend_log_transmission();

  if (sl_power_manager_is_ok_to_sleep() != true) {
    sli_power_manager_resume_log_transmission();
    exit_critical_with_primask(primask_state);
    return;
  }

    }

    // Stop the internal power manager sleeptimer.
    sl_sleeptimer_stop_timer(&clock_wakeup_timer_handle);
  } while (sl_power_manager_sleep_on_isr_exit() == true);

$ sed -n 77,116p autogen/sl_power_manager_handler.c
bool sl_power_manager_sleep_on_isr_exit(void)
{
  sl_power_manager_on_isr_exit_t answer;
  bool sleep = false;
  bool force_wakeup = false;
   
  // This function allow the power manager to return into sleep, if the latest timer 
  // to expire was power manager's internal one used to restore HFXO on time or 
  // the HFXO interrupt. 
  // Most of the time we want to get back to sleep until the next event occurs.
  sleep = sl_power_manager_is_latest_wakeup_internal();

  answer = sli_bt_sleep_on_isr_exit();
  if (answer == SL_POWER_MANAGER_WAKEUP) {
    force_wakeup = true;
  } else if (answer == SL_POWER_MANAGER_SLEEP) {
    sleep = true;
  }

  answer = sl_iostream_usart_io_stream_sleep_on_isr_exit();
  if (answer == SL_POWER_MANAGER_WAKEUP) {
    force_wakeup = true;
  } else if (answer == SL_POWER_MANAGER_SLEEP) {
    sleep = true;
  }

  // Application hook
  answer = app_sleep_on_isr_exit();
  if (answer == SL_POWER_MANAGER_WAKEUP) {
    force_wakeup = true;
  } else if (answer == SL_POWER_MANAGER_SLEEP) {
    sleep = true;
  }
  
  if (force_wakeup) {
    sleep = false;
  }

  return sleep;
}$ sed -n 47,48p;132p;165p <SDK>/platform_core/platform/driver/i2cspm/src/sl_i2cspm.c
// Transfer timeout (how many polls).
#define SL_I2CSPM_TRANSFER_TIMEOUT 300000
  uint32_t timeout_ms = SL_I2CSPM_TRANSFER_TIMEOUT;
      status = sl_i2c_leader_send_blocking(handle, follower_addr, seq->buf[0].data, seq->buf[0].len, timeout_ms);
$ sed -n 353p <SDK>/platform_core/platform/driver/i2c/inc/sl_i2c.h
 * @param[in] timeout            Timeout duration in milliseconds (0 = no timeout).
$ grep -n "SPEED_MODE      " config/sl_i2cspm_on_board_config.h config/sl_i2cspm_off_board_config.h
config/sl_i2cspm_on_board_config.h:49:#define SL_I2CSPM_ON_BOARD_SPEED_MODE      0
config/sl_i2cspm_off_board_config.h:49:#define SL_I2CSPM_OFF_BOARD_SPEED_MODE      0
$ grep -n "BAUDRATE  " config/sl_iostream_usart_io_stream_config.h; sed -n 255,264p <SDK>/.../sl_iostream_usart.c
40:#define SL_IOSTREAM_USART_IO_STREAM_BAUDRATE              115200
                            char c)
{
  sl_iostream_usart_context_t *usart_context = (sl_iostream_usart_context_t *)context;

  USART_Tx(usart_context->usart, (uint8_t)c);

#if defined(SL_IOSTREAM_UART_FLUSH_TX_BUFFER)
  /* Wait until transmit buffer is empty */
  while (!(USART_StatusGet(usart_context->usart) & USART_STATUS_TXBL)) ;
#endif
$ grep -c "power_manager" <SDK>/platform_core/platform/driver/pwm/src/sl_pwm.c
0
$ sed -n 211,230p exp_ui.c   (re-driver acquire/release)
void ui_redriver_acquire(void)
{
  if (redriver_holds == 0) {
    // Re-driver on/off time is < 100 nsec, but supply load switch needs 2 msec
    set_I2C_redriver_on();
    sl_sleeptimer_delay_millisecond(2);
  }
  redriver_holds++;
}

void ui_redriver_release(void)
{
  if (redriver_holds > 0) {
    redriver_holds--;
    if (redriver_holds == 0) {
      // Turn re-driver/power OFF
      set_I2C_redriver_off();
    }
  }
}
$ sed -n 252,274p exp_ui.c   (set_LED_on/off -> both boards)
void set_LED_on(led_color_t LED_color)
{
  // Blindly write LED update to both UI boards
  ui_target = UI_AUX_I2C_BUS_ADDRESS;
  set_LEDs_on(LED_color);
  ui_target = UI_SWITCH_I2C_BUS_ADDRESS;
  set_LEDs_on(LED_color);

  // Set LED state on
  LED_state_on[LED_color] = true;
}

void set_LED_off(led_color_t LED_color)
{
  // Blindly write LED update to both UI boards
  ui_target = UI_AUX_I2C_BUS_ADDRESS;
  set_LEDs_off(LED_color);
  ui_target = UI_SWITCH_I2C_BUS_ADDRESS;
  set_LEDs_off(LED_color);

  // Set LED state off
  LED_state_on[LED_color] = false;
}
$ sed -n 414,452p exp_board.c | grep -n "board_exp_transaction"  (redriver on/off = RMW each)
9:  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
17:  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
29:  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
37:  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
$ grep -n "define ACCEL_I2C_MIN_INTERVAL_MS\|define NUM_ACCEL_READINGS\|define STEADY_ACCEL_READ_TIMER_TIME" auto_level.c
36:#define ACCEL_I2C_MIN_INTERVAL_MS 10 // Matches the MC3479's 100 Hz output rate (decimate-by-10, init_accel()) - sampling faster only returns duplicate data
60:#define NUM_ACCEL_READINGS 130 // Number of accelerometer samples to average
65:#define STEADY_ACCEL_READ_TIMER_TIME 1500 // Time in ms
$ sed -n 303,316p app.c   (auto_level_blink_LED)
static void auto_level_blink_LED(led_color_t LED_color){

  sl_sleeptimer_is_timer_running(&auto_level_LED_blink_timer, &is_auto_level_LED_blink_timer_on);

  if (!is_auto_level_LED_blink_timer_on){
    start_auto_level_LED_blink_oneshot_timer();
    set_LED_on(LED_color);
    single_LED_on[LED_color] = true;
  }
  if (auto_level_LED_blink_timer_timeout){
    toggle_single_LED(LED_color);
    auto_level_LED_blink_timer_timeout = false;
  }
}
$ sed -n 510,549p app.c | grep -n "return"   (app_is_ok_to_sleep vetoes)
5:      return false;  // A motor is running - keep the supervision loop fast.
12:    return false;
22:    return false;
36:    return false;
39:  return true;
$ sed -n 273,279p bed_actions.c
// Mirrors process_outputs()'s SWITCH_AUTO_LEVEL press branch. The auto-level
// loop in app.c drives the motors and the blinking LED from here on.
void app_apply_auto_level_start_outputs(void)
{
  set_LED_on(LED_RED);
  set_all_motors_off();
}
$ sed -n 153,174p bed_actions.c   (app_auto_level_start)
void app_auto_level_start(void)
{
  capture_reset_timer();
  active_switch = SWITCH_AUTO_LEVEL_VALUE;
  initial_switch_press = true;
  auto_level_active = true;
  first_check = true;
  for (uint8_t i=0; i < 10; i++){
      first_print[i] = true;
  }
  for (uint8_t i=0; i < NUM_AXES; i++){
      leveling_success[i] = false;
  }
  double_check = false;
  auto_level_motors_on = false;
  auto_level_current_axis = ROLL; // Start with ROLL
  app_reset_auto_level_timer();
  is_raising = true; // To prevent accidentally tripping on lower collision security margin immediately following auto-level
  reset_level_data();
  for (uint8_t i=0; i < NUM_MOTORS; i++){
      motor_state[i].motor_on = false;
  };
$ sed -n 769,790p ble_bed_service.c   (AUTO_LEVEL_START case)
      }
      // HOLD-TO-RUN as of #36, matching the physical auto-level button. The
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
$ sed -n 966,982p ble_bed_service.c   (disconnect stop cancels the watchdog first)
{
  return app_move_active || app_auto_level_active;
}

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

$ sed -n 323,332p switch_input.c   (sampler idle check)
static void switch_sampler_tick(void)
{
  uint32_t now = sl_sleeptimer_get_tick_count();
  bool wake;

  if (!sampler_active) {
    sl_atomic_load(wake, ui_wake_request);
    if (!wake && !ui_int_asserted()) {
      return;
    }
$ sed -n 797,810p switch_input.c   (fail-safe early return)
  uint32_t now = sl_sleeptimer_get_tick_count();
  if (sl_sleeptimer_tick_to_ms(now - failsafe_last_sample_tick) < FAILSAFE_SAMPLE_MS) {
    return;
  }
  failsafe_last_sample_tick = now;

  // Auto-level legitimately drives motors with no button held in the panel
  // sense, and settings mode drives them from its own logic. Neither is a
  // runaway, so neither is counted as quiet.
  if (auto_level_active || in_settings_mode || ble_bed_service_app_motion_active()) {
    failsafe_quiet_samples = 0;
    failsafe_tripped = false;
    return;
  }
$ sed -n 671,676p switch_input.c; sed -n 975,977p switch_input.c   (entry timer cancel sites)
            } else if ((!switch_pressed) && (switch_held == SWITCH_BED_LIGHT_VALUE)){
                switch_held = OFF;
                /*** For "Settings Mode" ***/
                settings_mode_cancel_entry();

            }
          /*** For "Settings Mode" ***/
          settings_mode_cancel_entry();
      }
$ sed -n 56p switch_input.c; sed -n 464,469p switch_input.c
#define SWITCH_STATE_STALE_MS       100 // fail-safe distrusts older data
bool switch_state_fresh(void)
{
  return !sampler_active ||
         (sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count() - last_good_sample_tick)
          <= SWITCH_STATE_STALE_MS);
}
$ sed -n 141,147p ble_bed_service.c   (requested connection parameters)
// 15-30 ms interval (1.25 ms units), latency 0, 1 s supervision timeout
// (BLE_GATT_DESIGN.md #8/#22) - link-loss safety is the Stage C watchdog,
// not this timeout.
#define CONN_MIN_INTERVAL        12
#define CONN_MAX_INTERVAL        24
#define CONN_LATENCY              0
#define CONN_SUPERVISION_TIMEOUT 100
$ grep -n "0x1008)" <SDK>/platform_core/platform/common/inc/sl_status.h
260:#define SL_STATUS_BT_CTRL_CONNECTION_TIMEOUT                                                             ((sl_status_t)0x1008)     ///< Link supervision timeout has expired.
$ grep -rn "settings_mode_cancel_entry" --include=*.c --include=*.h .
./settings_mode.c:108:void settings_mode_cancel_entry(void)
./settings_mode.h:39:void settings_mode_cancel_entry(void);
./switch_input.c:674:                settings_mode_cancel_entry();
./switch_input.c:976:          settings_mode_cancel_entry();
```

### d.5 The one command that failed

While reading the power manager source, a compound command's second half
failed because `n` was empty (the `grep` for the function signature found
nothing, so `sed` got `,+90p`). Command and complete output:

```
$ F="<SDK>/platform_core/platform/service/power_manager/src/sleep_loop/sl_power_manager.c"; grep -n "sleep_on_isr_exit\|is_ok_to_sleep\|^void sl_power_manager_sleep\|while (" "$F" | head -20; n=$(grep -n "^void sl_power_manager_sleep(void)" "$F" | cut -d: -f1); sed -n "$n,$((n+90))p" "$F"; grep -n "sli_bt_sleep_on_isr_exit\|sli_bt_is_ok_to_sleep" -B2 -A12 "<SDK>/bluetooth_le_host/inc/sl_bt_stack_init.h" "<SDK>/bluetooth_le_host/src/"*.h 2>/dev/null | head -40
263:  if (sl_power_manager_is_ok_to_sleep() != true) {
348:  } while (sl_power_manager_sleep_on_isr_exit() == true);
362:    while (!sli_power_manager_is_high_freq_accuracy_clk_ready(false)) {
394:  } while (sl_power_manager_sleep_on_isr_exit() == true);
sed: -e expression #1, char 1: unknown command: `,'
```

(The function is declared `__NO_INLINE void sl_power_manager_sleep(void)`, so
the anchored grep missed it. It was then read with `sed -n 240,400p`; the
lines cited are in d.4.) The final `grep` printed nothing.

## e. VERIFIED vs ASSERTED

### VERIFIED (evidence attached)

| # | Claim | Evidence |
|---|---|---|
| V1 | Repo state at start matched the prompt: HEAD `18d8378` level with `origin/main`; `.projectlinkstore` and `CLAUDE.md` modified; the prompt file untracked | d.1: `git status`, `git rev-parse HEAD origin/main` (both `18d83780...`) |
| V2 | The CLAUDE.md edit was committed exactly as found | d.1 `git diff CLAUDE.md` before the commit = the CLAUDE.md hunk in c.1 |
| V3 | Stage C build/fix commit dates: `ab12d09` 2026-08-02, `a22406f` 2026-08-02, `5351aef` 2026-08-04, `944f19e`/`c6ee6b0`/`840d7b8` 2026-10-18 | d.1 `git log --date=short ... --no-walk` |
| V4 | `9ba0ca3` and `18d8378` are docs-only, so the Prompt W bench firmware's code is `840d7b8` | d.1 `git show --stat` |
| V5 | Prompt W log: every row of the b.1.3 table except "STOP was pressed" | d.3, the log with line numbers |
| V6 | 0x1022 = `SL_STATUS_BT_CTRL_LL_RESPONSE_TIMEOUT` "Connection terminated due to link-layer procedure timeout"; 0x1016 local host; 0x1013 remote user; 0x1008 supervision timeout | d.1/d.4 greps: `sl_status.h:286`, `:274`, `:271`, `:260` |
| V7 | `sl_bt_evt_connection_parameters_t` has `latency` and `timeout` (x 10 ms) | d.4 excerpt `sl_bt_api.h:6945-6966` (fields at `:6951`, `:6953`) |
| V8 | `app_stop_all()` is called from STOP_ALL, watchdog expiry and control-disconnect only | d.1 `grep -rn "app_stop_all()"` (`ble_bed_service.c:505,535,883` at the time of the grep, before C moved lines) |
| V9 | "Step 1" prints only after `app_auto_level_start()`; the app calls it only when `app_auto_level_active` is false | `app.c:615-620`; `bed_actions.c:159-162` (d.4 excerpt `153-174`); `ble_bed_service.c:775-781` (d.4 excerpt `769-790`) |
| V10 | The settings-mode entry timer is cancelled only by the BED LIGHTS release and the double-press handler | d.4 excerpts `switch_input.c:671-676`, `:975-977`; `grep -n settings_mode_cancel_entry` (two call sites) in d.1 |
| V11 | `switch_state_fresh()` semantics and the 100 ms stale limit | d.4 excerpt `switch_input.c:56`, `:464-469` |
| V12 | The disconnect stop cancels the watchdog before `app_stop_all()` | d.4 excerpt `ble_bed_service.c:966-982` (cancel at 976, stop at 980) |
| V13 | Both I2C buses at standard mode 100 kbit/s; VCOM 115200 blocking per character | d.4: `SPEED_MODE 0` lines; `BAUDRATE 115200`; `sl_iostream_usart.c:255-264` |
| V14 | I2CSPM passes 300000 as a millisecond timeout | d.4 `sl_i2cspm.c:47-48,132,165`; `sl_i2c.h:353` |
| V15 | `ui_transaction()` toggles the re-driver around every transfer when not held, with a 2 ms delay; `set_I2C_redriver_on/off` are each a read + write on the on-board expander; `set_LED_on/off` write both boards | d.4 excerpts `exp_ui.c:211-230`, `:252-274`; `exp_board.c:414-452` (four `board_exp_transaction` calls) |
| V16 | Power manager checks `app_is_ok_to_sleep()` once on entry, re-sleeps after an ISR only if internal / BT / USART say so | d.4 excerpts `sl_power_manager.c:260-268,344-349`; `autogen/sl_power_manager_handler.c:77-116` |
| V17 | `sl_pwm.c` makes no power-manager calls | d.4 `grep -c power_manager` = 0 |
| V18 | The app's sleep vetoes are only the four listed in b.4.2 | d.4 `app.c:510-549` `return` lines |
| V19 | Every build exited 0 with no warning or error under `-Wall -Wextra`: baseline, after A, after B, after C (flag 1, flag 0, flag 1) | d.2 (all six builds verbatim); `tilt_bed_controller.cmake:363-364` (d.1 grep) |
| V20 | The flag was restored to 1 after the flag-0 build, and the committed header says 1 | d.2 `grep -n` after each `sed`; d.4 line-number grep `ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1` |
| V21 | Image size after C: text 244596, data 780, bss 72480 | d.2 `arm-none-eabi-size` |
| V22 | `bed_control.c:73`'s comment says force-stops call `app_stop_all()`; all five call `app_stop_movement()` | d.1 `sed -n 66,80p bed_control.c` and `grep` (104, 116, 132, 144, 157) |

### ASSERTED (reasoned or compiled, nothing observed)

- **A1.** The Prompt W STOP item: that STOP was pressed in the app during log
  lines 54-59 and the holds lasted past 2 s — the user's report; the log
  cannot show it (b.1.3).
- **A2.** The reading of 0x1022 (the phone did not acknowledge the bed's
  termination in time). Plausible, not shown.
- **A3.** B leaves a normal 5 s entry unchanged and blocks entry on stale data
  as described in b.2. Compiles; not run.
- **A4.** C changes no behaviour: by inspection every added statement reads a
  tick or writes a diagnostic static, and the prints run after the stop and
  the result. Compiles in both flag states; not run.
- **A5.** That the WATCHDOG STOP block and the connection_parameters line look
  like section D predicts, including the typical P, B, L values.
- **A6.** All C5 timings (bit-time arithmetic, b.4). In particular ~15 ms per
  `set_LED_on/off`, ~135 ms for `app_stop_all()` during auto-level, ~31 ms
  for the stop print, and "no Step 1 path near 300 ms".
- **A7.** That the CPU sleeps during app-driven Step 1: the application allows
  it (from code); the Bluetooth stack's own veto is unknown.
- **A8.** That a received write wakes the loop and is processed in the same
  iteration (depends on the closed-source stack's ISR-exit hook doing what its
  documentation says).
- **A9.** That turning Bluetooth off produces a clean disconnect rather than a
  watchdog stop (depends on the phone; consistent with the 2026-08-04 note).
- **A10.** Android's typical initial supervision timeout (500) — general
  knowledge, not from this project.

## f. QUESTIONS FOR REVIEW

1. **Extra fields in the stop block.** Beyond C1/C2's list I added "longest
   between passes" (B), "this pass so far" (T) and the last received write's
   op/seq, under C3's "whatever fields make this unambiguous" (b.3.5). B is
   the only way the block can see time spent asleep or in the stack. Keep, or
   remove to match C1/C2 strictly?
2. **The deliberate-stop test in D.** "Turn the phone's Bluetooth off" will
   probably be handled by the disconnect path with no WATCHDOG STOP (b.5 D3).
   Do you want the bench step changed to shielding the phone (metal box) or
   walking out of range, or backgrounding the app if it does not send
   AUTO_LEVEL_STOP?
3. **B's wording.** One condition cannot stop an already-running entry timer;
   it stops stale data from completing the entry. A fresh read with BED
   LIGHTS still down then enters at once on the already-set flag; a fresh
   release cancels. I believe that is the intended safety property. Should
   stale data also stop the timer (a second change, `settings_mode_cancel_entry()`
   when stale)? Not done.
4. **App vs physical Step 1 pass rate.** The application allows the CPU to
   sleep during app-driven Step 1 but not during a physical AUTO LEVEL hold
   (sampler active). Auto-level's averaging is in loop passes (#29), so the
   app-driven steady read may average far fewer fresh samples. Not the
   watchdog question, and not changed. Worth a separate look (a sleep veto
   while `auto_level_active`, or #29's time-based constants)?
5. **300 s I2C timeout.** SDK 2026.6.0's I2CSPM passes 300000 as
   milliseconds. A hung bus would block the loop for up to 5 minutes, with the
   motors in whatever state they were. Reported only. Raise as its own item?
   (Fixing it would mean either a project-side wrapper or an SDK
   configuration question; `config/` and the SDK are not to be hand-edited.)
6. **Stale comment.** `bed_control.c:73` says force-stops become
   `app_stop_all()`; since #36 they call `app_stop_movement()`. Comment only;
   not touched (out of scope).
7. **Stage C checkbox.** I ticked `[x]` "Built and bench-tested" in
   MIGRATION_STATUS.md, with the full pass and Release pass called out as
   still open. If you would rather keep the box unticked until the full pass,
   say so.
8. **`app_stop_all()` during auto-level costs ~135 ms of LED I2C** (two
   `set_all_LED_off()` calls, one inside `app_auto_level_stop()` and one after
   it). After a watchdog stop this delays the next restart. Not changed.
9. **Collision trip in the Prompt W log** (lines 146-152, `Tripped on steady!`
   during an app auto-level at margin 550) has not been examined.

## g. NOT DONE

- **Nothing asked for was skipped.** A1-A5, B, C1-C5 and D are done as
  described above, with the deviations stated in QUESTIONS 1-3.
- **Bench runs:** none — the user drives the bench. Everything in section D is
  a prediction.
- **Host unit test** (`test/bed_control_test.c`) not run: this session did
  not touch `bed_control.c` and the test does not link `ble_bed_service.c`,
  `settings_mode.c` or `app.c`.
- **Raw output of file-reading commands** (cat/sed/grep used to read repo
  source and docs while planning, ~50 commands) is not reproduced in full:
  their output is the repository's own content at `18d8378` (or the commits
  in c), which the diffs and the excerpts in d.4 cover for every claim made.
  The one failed command is reproduced in d.5. Commands that only edited
  scratch files for this handoff are not reproduced.
- `SWITCH_SAMPLER_DIAG` and `BLE_DEBUG_WATCHDOG_LOG` are both 1; both must be
  0 before shipping. Not changed (bench diagnostics).
