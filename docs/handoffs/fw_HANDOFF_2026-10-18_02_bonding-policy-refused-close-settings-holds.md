# Handoff 2026-10-18 #02 — Prompt W: bond store policy, disconnect refused pairings, settings-mode holds

## a. Header

- Repo: `tilt_bed_controller`
- Branch: `main`
- Date: 2026-10-18 (from `date`: `Sun Oct 18 10:32:56 PDT 2026` at session start, `Sun Oct 18 10:45:18 PDT 2026` when this file was started). Sequence `02`: `docs/handoffs/` already held `fw_HANDOFF_2026-10-18_01_...` for this date.
- HEAD before the session: `3f11c12` (Handoff 2026-10-18 01: Prompt V ...), level with `origin/main` (`3f11c1299e3e5fcea33290a247fc3cea71c6adf9`); `.projectlinkstore` modified; `docs/prompts/PROMPT_W_bonding-policy-reject-disconnect-settings-holds.md` untracked. **This matches the prompt's "Expected repo state" exactly.**
- HEAD after the work commits: `9ba0ca3` (`9ba0ca3059f2e54d5cde819608e874c11b0434af`). Four work commits, in the order the prompt asked for:

| Part | Commit | Subject |
|---|---|---|
| A | `9798c5a` | Record 2026-10-18 Prompt V bench result (docs only) |
| B | `c6ee6b0` | Bonding: overwrite least recently used bond when full; close refused pairings (DECISIONS #43) |
| C | `840d7b8` | Settings mode: STOP cancels holds, releases clear stale timeouts, entry needs BED LIGHTS down |
| D | `9ba0ca3` | Docs cleanup: #19 forward pointer, P3.13 done, sampler design debounce note |

- This handoff and the `docs/SESSION_LOG.md` entry are committed together after `9ba0ca3`, in a docs-only commit whose SHA is not known while this file is being written. Then `git push` (normal, never forced). If the push fails, its raw error goes into this file in a follow-up commit, and no merge, rebase or reset is attempted.
- The prompt file was committed in commit A, unmodified.
- `.projectlinkstore` is modified and was deliberately NOT committed (prompt: "never commit it").
- `BED_BENCH_PROVOKE_RUNAWAY` is 0 (`switch_input.c:98`) and `BLE_BENCH_WIPE_BONDS` is 0 (`ble_bed_service.c:70`) in the committed tree, as the prompt requires. `SWITCH_SAMPLER_DIAG` is still 1 (`switch_input.c:57`) and `BLE_DEBUG_SECURITY_LOG` is 1 (`ble_bed_service.c:53`).
- **One item is NOT DONE: D2 (CLAUDE.md).** The permission system refused my edit to CLAUDE.md, because CLAUDE.md is my own instruction file. I did not work around that. The proposed diff is in b.4 for the user to apply. See also QUESTIONS 1.

`git status` after the last work commit, immediately before this handoff and the log entry were written:

```
On branch main
Your branch is ahead of 'origin/main' by 4 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
```

## b. Designs, analysis and answers produced this session

Each part is written out in full here. A reviewer should not need the prompt, DECISIONS.md or the code to follow it. `file:line` references are to the committed tree at `9ba0ca3` unless stated.

### b.1 Part A — what the Prompt V bench log actually says (commit `9798c5a`)

Source: `C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\prompt_v_bench.log`, 193 lines, 6563 bytes, modified Oct 18 10:02. Read with grep/sed (raw output in d.2.1). Every item the prompt asked me to confirm is confirmed, with three wording points recorded as such.

| Prompt's claim | What the log shows | Evidence (d.2.1) |
|---|---|---|
| Debounce 2: 88 accepted edges, all "(bounces 0)" | 88 `SW: 0xNNNN down/up (bounces N)` lines; 88 with `(bounces 0)`; 0 with a non-zero bounce count | `grep -c` |
| Two-button tests gave "presses ignored" / "Double Press Error" as designed | 6 `presses ignored` lines (log lines 38, 43, 48, 55, 60, 67) and 3 `Double Press Error` lines (49, 61, 68), each directly after the third, fifth and sixth `presses ignored`. As in the Prompt V session's own finding, a DPE episode prints **both** messages | `grep -n` |
| No FAILSAFE, no "Unrecognized switch mask" | both counts 0 | `grep -c` |
| Settings mode left with one BED LIGHTS press, four times | `Settings mode: exit` at lines 79, 86, 93, 171, each directly after a `SW: 0x0008 down` | `grep -n`, `sed -n 94,182p` |
| Every button worked afterwards, including a full auto-level | After the exit at line 93, every one of the 12 panel buttons gave a clean down/up pair (11 in turn from 0x8000 UP HEAD to 0x0002 AUTO LEVEL, then 0x0008 BED LIGHTS), and AUTO LEVEL (0x0002) ran through `ROLL Leveled successfully`, `PITCH Leveled successfully`, `Exit auto-level`. **Wording point:** the other presses print no `Max Current for Motor` lines. Those lines print only once the dynamic current threshold has been learned, ~300 ms into a move (`motor_current_functions.c:179-198`), so short taps print none. The log therefore shows the edges arriving, not the motors moving; "worked" is the user's observation | `sed -n 94,182p` |
| (a) bonded phone reconnected with no confirm_bonding line | lines 142-148: `connection_opened conn=1 bonding=0x01`, `security_mode=1`, `cccd_seed`, then `connection_closed conn=1 reason=0x1013`; no `confirm_bonding` | same |
| (b) bond deleted on phone, not in settings mode: REJECTED, bonding_failed 0x1205, phone closed with 0x1013 | lines 149-155, exactly those three lines in that order, preceded by `connection_opened conn=2 bonding=0x01` (the firmware still held the bond) and `security_mode=0` throughout | same |
| (c) in settings mode: ACCEPTED then bonded | lines 162-169: `-> ACCEPTED (settings mode)`, `security_mode=1`, `cccd_seed`, `bonded conn=3 bonding=0x01 security_mode=1` | same |
| (d) after exit, reconnected with no confirm_bonding, app movement worked | exit at 171; `connection_closed conn=3` at 175; `connection_opened conn=4 bonding=0x01` at 176, `security_mode=1` at 179, no `confirm_bonding`; then 12 `Max Current` lines with **no `SW:` line before them**, so the movement was not from the switch panel. That fits app-driven movement; the log cannot name the source | same |
| Phone showed "status 137 (GATT_AUTH_FAIL), type ... CHARACTERISTIC_READ" | **Not in the PuTTY log** (`grep -c 'GATT_AUTH\|137'` = 0). This is the phone's screen, reported by the user via the prompt. Recorded as user-reported | `grep -c` |

**Another observation, not acted on.** During each 5 s BED LIGHTS entry hold (lines 72-77 and 156-161), four `Max Current for Motor` lines print, with values 0-10 and no motor running. That fits `switch_input_tick()`'s supervision block, which runs `run_motor_supervision(active_switch, ...)` whenever `active_switch != OFF` outside settings mode (`switch_input.c:921-925`), and `active_switch` is `SWITCH_BED_LIGHT_VALUE` during the hold. This behaviour predates this session and I did not trace it further. See QUESTIONS 7.

**Where it was recorded (commit `9798c5a`).**
- DECISIONS #42 gains a dated sub-section "Bench result (2026-10-18)" holding the table above, marked **VERIFIED (user's bench run)**, with the log path and line numbers. The phone message is labelled as reported by the user, not in the log. "Every button worked" is labelled as the user's observation.
- REGRESSION.md §7 gains a "Recorded result, 2026-10-18" table after its last item: 7.9 (exit), plus three pairing rows. **The prompt says "the settings exit and pairing checks", but REGRESSION.md has no pairing check** (`grep -n -i "pair\|bond" docs/REGRESSION.md`, d.2.2, re-run on the committed tree, finds only line 9's "connected, bonded, and subscribed", unrelated "motor pair" hits, and this session's own additions at lines 242-250). I did not invent a checklist item. The table says so explicitly ("This section has no pairing-window checklist item yet"), and QUESTIONS 3 asks whether to add one. Boxes are left unchecked.
- The SESSION_LOG entry for this session records it as VERIFIED.

### b.2 Part B — Bluetooth bonding (commit `c6ee6b0`, `ble_bed_service.c`)

#### B1. Bond store full -> overwrite the bond used the longest time ago

**SDK fact** (`sl_bt_api.h:12054-12086` in the SiSDK 2026.6.0 install the build uses, raw text in d.2.3). The doc for `sl_bt_sm_store_bonding_configuration(uint8_t max_bonding_count, uint8_t policy_flags)` lists:
- `0`: "If database is full, pairing will be performed but no bonding is stored" (line 12071-12072)
- `1`: "New bonding will overwrite the oldest existing bonding" (line 12073)
- `2`: "New bonding will overwrite the bonding that was used the longest time ago" (lines 12074-12075)

So the value the prompt asked for is **2**, confirmed. The SDK has no named constant for these values (`grep -i "bonding_policy\|policy_flags\|SL_BT_SM_.*POLICY"` finds only the doc's `@param` and the prototype). I used the literal `2` with the doc cited in the comment. The doc also says: "With external bonding database the parameter is ignored". This project uses `bluetooth_feature_builtin_bonding_database` (`tilt_bed_controller.slcp:182`), so the parameter applies.

**Change** (`ble_bed_service.c:936-944`):

```c
      // Bond store full (8 bonds): a new bond overwrites the one used the
      // longest time ago - policy 2 in sl_bt_api.h's
      // sl_bt_sm_store_bonding_configuration() doc (DECISIONS.md #43). The
      // previous policy 0 did not reject a 9th phone: per the same doc it
      // paired it but stored no bond for it.
      // Eviction is safe now because since #42 every new bond needs someone
      // at the bed's switch panel (settings mode), so a stranger cannot
      // trigger it; a phone that was evicted pairs again in settings mode.
      sc = sl_bt_sm_store_bonding_configuration(BLE_BED_SERVICE_MAX_BONDS, 2);
```

The old comment ("Reject new bonds once the 8-bond store is full, rather than silently evicting an existing one") is removed. It was wrong: policy 0 does not reject.

**"Least recently used" vs "oldest".** The prompt's heading says "least recently used" and quotes policy 2's doc text, but its section E calls it "overwrite oldest bond". Policy 1 is "oldest existing bonding" (by creation); policy 2 is "used the longest time ago" (by last use). I implemented 2 as the B1 text specifies, and the docs say "used the longest time ago" / "least recently used" throughout.

**Trade-off.** With policy 2, a household phone that has not connected for a long time is the one evicted, and it has to re-pair in settings mode. With policy 0, a 9th phone could connect, but the bed never remembered it. Each connection would then trigger a new pairing, which outside settings mode #42 now rejects. In practice, under policy 0 a 9th phone could only ever use the bed while it was in settings mode.

#### B2. Disconnect a phone whose pairing the firmware refused

**Why.** In the Prompt V run, the Android app closed the link itself after the rejection (`connection_closed reason=0x1013`; `sl_status.h:271` `SL_STATUS_BT_CTRL_REMOTE_USER_TERMINATED`). Another phone or app may stay connected, unencrypted and useless, and occupy one of the module's two role slots.

**SDK facts** (d.2.3):
- `sl_status_t sl_bt_connection_close(uint8_t connection);` (`sl_bt_api.h:8515`). Its doc (8495-8513): "Close a Bluetooth connection gracefully by performing the ACL Termination procedure ... Disconnecting a connection is an asynchronous operation. The disconnection is completed when a sl_bt_evt_connection_closed event for the given connection handle is received." So it queues the termination and returns; it does not wait.
- The other option, `sl_bt_connection_forcefully_close` (8537), was not used. The prompt asks for the graceful close.
- The stack's connection count: `SL_BT_CONFIG_MAX_CONNECTIONS_SUM = SL_BT_CONFIG_MAX_CONNECTIONS + SL_BT_COMPONENT_CONNECTIONS` (`config/sl_bluetooth_config.h:48`) = `4` (`config/sl_bluetooth_connection_config.h:38`) + `0` (`autogen/sl_bluetooth.h:41`) = **4**. Note that this is 4, not 2: the stack can hold 4 connections, while this module tracks 2 (`open_connections[2]`). See QUESTIONS 8.

**Design as implemented.**

1. **State** (`ble_bed_service.c:196-204`): `static uint8_t refused_pairing_conn[SL_BT_CONFIG_MAX_CONNECTIONS_SUM];`. Each entry is a connection handle or `SLOT_EMPTY` (0xff). The prompt asked for a "per-connection flag sized for the stack's connection count". I implemented it as a table that **stores handles** rather than a bool array **indexed by** handle. This is the one judgement call in B2. Reason: the stack's documentation does not say handles run 1..N. The bench logs only show handles 1-4 (`grep -ho "connection_opened conn=[0-9]*"` over all four PuTTY logs: conn=1 x4, conn=2, conn=3, conn=4). A bool array indexed by handle would silently go out of bounds, or need a bounds check that drops the close, if a handle ever exceeded N. A handle table has the same size and makes no assumption. See QUESTIONS 4.
   - To include `SL_BT_CONFIG_MAX_CONNECTIONS_SUM`, the file now includes `sl_bluetooth.h` (autogen), which includes `sl_bluetooth_config.h` and defines `SL_BT_COMPONENT_CONNECTIONS` (`autogen/sl_bluetooth.h:36,41`).
   - Initialised to all `SLOT_EMPTY` in the `system_boot` handler with `memset` (`:956-957`), because a zero-filled static would read as "handle 0 refused". `system_boot` always precedes any connection.
2. **Helpers** (`:308-344`): `pairing_refused_mark(conn)`, which adds the handle if it is not already present and does nothing if the table is full (see below); `pairing_refused_is_marked(conn)`; `pairing_refused_clear(conn)`, which removes every entry equal to `conn`.
3. **confirm_bonding** (`:1083-1099`): after computing `confirm = app_in_settings_mode() ? 1 : 0` (unchanged from #42), if `!confirm` it calls `pairing_refused_mark(cb->connection)`, then `sl_bt_sm_bonding_confirm(cb->connection, confirm)` as before. **It does not close here**: if the termination were queued before the stack sends the pairing failure, the phone might see a dropped link instead of a clean pairing error.
4. **bonding_failed** (`:1041-1066`, case label at `:1049`), now a braced case:

   ```c
    case sl_bt_evt_sm_bonding_failed_id: {
      uint8_t connection = evt->data.evt_sm_bonding_failed.connection;
   #if BLE_DEBUG_SECURITY_LOG
      printf("BLE SM: bonding_failed conn=%u reason=0x%04x\r\n", connection, ...reason);
   #endif
      if (pairing_refused_is_marked(connection)) {
   #if BLE_DEBUG_SECURITY_LOG
        printf("BLE: closing conn=%u - pairing refused\r\n", connection);
   #endif
        sc = sl_bt_connection_close(connection);
        (void)sc; // best-effort: if the phone is already closing the link, its own connection_closed follows
      }
      break;
    }
   ```
   The close itself is unconditional on the mark. Only the print is under `BLE_DEBUG_SECURITY_LOG` (currently 1), matching every other security print in this handler. QUESTIONS 5 asks whether the reviewer wanted it always on.
5. **Clearing** (`:984`, `:1016`): `pairing_refused_clear()` on `connection_opened` (a new link that reuses a handle starts unrefused) and on `connection_closed` (before `handle_connection_closed()`).

**Non-blocking (DECISIONS #27).** Everything added is a short loop over 4 bytes, plus one BGAPI command that the SDK documents as asynchronous. No I2C, no wait loop, no delay.

**Edge cases traced:**
- *The phone closes first.* If `connection_closed` is processed before `bonding_failed`, the mark is already cleared, so no close is issued and no "closing" line prints. If the phone's terminate crosses with ours, `sl_bt_connection_close()` may return an error (ignored) and exactly one `connection_closed` arrives, with whichever reason won (0x1013 remote, or 0x1016 local, `sl_status.h:271,274`).
- *bonding_failed never arrives after a rejection.* Then nothing is closed: the same as before #43. The mark is cleared when the link eventually closes.
- *The phone asks again on the same link.* `pairing_refused_mark` sees it is already marked. The first `bonding_failed` already queued the close.
- *A refused link later accepted.* In principle, if the user entered settings mode before the first `bonding_failed` and the phone retried, the mark would still be set, and a later failure on that link would close it. In practice the first `bonding_failed` closes the link before any retry. The prompt says to clear only on open and close, so I did not clear on accept.
- *Table full.* Impossible while every close clears its handle: the table has one entry per stack connection. If it happened, the link is simply not closed, which is the pre-#43 behaviour. The comment says so.
- *Auto-reconnecting app.* If an app reconnects immediately after being closed, the cycle connect -> REJECTED -> bonding_failed -> closing -> closed will repeat for as long as it retries. Each cycle is bounded and harmless, but the log will be noisy. In Prompt V the next connection after the refusal (conn=3, line 162) came after the user had entered settings mode, so whether the app auto-retries is not known.

#### B3. Bond handle back in the confirm_bonding print

`ble_bed_service.c:1087`: `"BLE SM: confirm_bonding conn=%u handle=0x%02x (%s) -> %s\r\n"` with `cb->bonding_handle`, which is `uint8_t` (`sl_bt_api.h:11946`; `SL_BT_INVALID_BONDING_HANDLE ((uint8_t) 0xFF)`, `:79`), so `%02x` prints `ff` for a new bond.

#### DECISIONS #43 and BLE_GATT_DESIGN §8

- DECISIONS **#43** (next free number; #42 was the last) records B1 and B2 with their reasons, and the "not on confirm_bonding" and "nothing blocks" points. Its Status says: builds, not run on the bench; the 0x1016 expectation is inferred from `sl_status.h`; eviction is not observed.
- BLE_GATT_DESIGN §8 "Bonding & security" now says a refused phone is disconnected by the firmware once the stack reports the pairing failure, and that when the 8-bond store is full a new bond overwrites the bond used the longest time ago, with the reason (#43).

### b.3 Part C — settings-mode holds (commit `840d7b8`)

**Background, from the code.** In settings mode:
- Holding UP_ALL or DOWN_ALL sets `active_switch` and starts `handle_2000ms_timer`. When its callback sets `timeout_2000ms`, the next `settings_mode_tick()` steps the sensitivity ladder, writes NVM3, stops the timer and clears the flag (`settings_mode.c:403-491`). Holding on restarts the timer, giving one step per 2 s.
- Holding AUTO_LEVEL starts the 2 s timer (store the zero angle when it fires, `:335-380`). After the store it starts the 5 s timer (erase when it fires, `:383-394`).
- The callbacks only set the flags (`:76-81`, `:94-99`). The flags are consumed only in `settings_mode_tick()`, inside `if (active_switch == X)` blocks.

**The tick order matters** (`app.c:708,715,747`): `settings_mode_tick()`, then `switch_input_tick()` (delivers switch edges and so runs the release branches), then `ble_bed_service_tick()` (runs STOP_ALL -> `app_stop_all()`).

#### C1. `settings_mode_cancel_holds()` (`settings_mode.c:114-126`, declared `settings_mode.h:41-46`)

```c
void settings_mode_cancel_holds(void)
{
  sl_sleeptimer_stop_timer(&handle_2000ms_timer);
  timeout_2000ms = false;
  sl_sleeptimer_stop_timer(&handle_5000ms_timer);
  timeout_5000ms = false;
}
```

This writes nothing to NVM3, does not touch `in_settings_mode`, LEDs or `active_switch`. `settings_mode_exit()` now calls it in place of the identical four lines (`:141`), so the exit's behaviour is byte-for-byte the same.

#### C2. STOP ALL in settings mode cancels the hold and stays in settings mode

**Change** (`bed_actions.c:221-228`), in `app_stop_all()` just before its final `active_switch = OFF`:

```c
  if (in_settings_mode) {
    // STOP in settings mode abandons a sensitivity or zero-angle hold in
    // progress and stays in settings mode (Prompt W, user decision 3). ...
    settings_mode_cancel_holds();
  }
```

`bed_actions.c` already includes `settings_mode.h` (`:22`), which declares both `in_settings_mode` and the new function.

**The confirm the prompt asked for: is `app_stop_all()` only reached from the STOP_ALL opcode path? No.** `grep -rn app_stop_all` (d.2.2) finds three call sites, all in `ble_bed_service.c`:

| Call site | Path | Can it run in settings mode? |
|---|---|---|
| `:535` | STOP_ALL opcode (`process_pending_command()`) | Yes. That is the intended path |
| `:505` | dead-man watchdog expiry (`service_move_watchdog()`) | Practically no. The watchdog is armed only by an *accepted* MOVE_START / AUTO_LEVEL_START (`move_watchdog_arm()` at `:652`, `:692`), and both are refused with REJECTED_SETTINGS_MODE before arming (`:593-596`). It would need a watchdog armed within 300 ms before settings mode was entered, but entry needs a 5 s BED LIGHTS hold, and an app move during that hold is a two-source conflict that stops the app's movement first |
| `:883` | control phone disconnected while APP held the bed (`ble_bed_service_tick()`, flag set at `:786` when `bed_active_source() == BED_SOURCE_APP`) | Practically no, for the same reason: APP cannot start motion in settings mode |

**This contradicts the prompt's premise, so I report it here as instructed. I implemented C2 exactly as specified, inside `app_stop_all()`.** I judged that implementing the specified call site is not improvising, and that the two extra paths are harmless if ever reached. In each, cancelling a settings hold during a full stop matches what that stop is for: nothing is written and settings mode is not left. If the reviewer wants the cancel only on the STOP_ALL opcode, the alternative is to move the call to `ble_bed_service.c:535`, after `app_stop_all()`. See QUESTIONS 2.

There is also an `app_stop_all()` stub in `test/bed_control_test.c:92`. The host test does not link `bed_actions.c` (its own header comment says so, `test/bed_control_test.c:12-15`), so it is unaffected. I did not run the host test.

**What STOP does in settings mode, traced** (UP ALL held, before the 2 s step):
1. `app_stop_all()`: `auto_level_active` is false. `active_switch` is `SWITCH_UP_ALL_VALUE`, so `app_move_stop()` runs. That clears `motor_state[].motor_on`, resets the current-supervision data and calls `set_all_motors_off()` (`bed_actions.c:135-151`); nothing is energised in settings mode anyway. Then `set_all_motors_off()`, `set_all_LED_off()`, `reset_limit_switch_flags()`, **`settings_mode_cancel_holds()`** (2 s timer stopped, `timeout_2000ms` cleared), `active_switch = OFF`. The app gets Command Result ACCEPTED.
2. With UP ALL still held, `settings_mode_tick()` no longer enters the UP_ALL block (`active_switch` is OFF), so no timer restarts and nothing is stepped or stored.
3. Releasing UP ALL: in settings mode `process_inputs()` goes straight to `process_outputs()` -> `settings_mode_process_outputs()` (`switch_input.c:592-594`, `:717-719`, no `switch_held` gate). The UP_ALL case's press arm needs `switch_pressed`, and its release arm needs `active_switch == SWITCH_UP_ALL_VALUE`, so neither runs. Nothing happens.
4. The next UP ALL press: `switch_pressed && active_switch == OFF`, so `active_switch = SWITCH_UP_ALL_VALUE`. The tick finds the timer not running and starts it with `timeout_2000ms` false, so **the step needs the full 2 s**.

**Without C2** (for comparison): the 2 s timer would keep running after STOP and its callback would set `timeout_2000ms`. Nothing would consume it, because `active_switch` is OFF. The next UP ALL, DOWN ALL **or AUTO LEVEL** press would then act on it within one tick: an immediate sensitivity step, or an immediate zero-angle store using one accelerometer average.

**The amber LED after STOP (the prompt's check).** The settings blink is `fast_blink_LED(LED_AMBER)`, called from `settings_mode_tick()` every pass while `in_settings_mode` (`settings_mode.c:293`). `fast_blink_LED()` (`app.c:329-342`) starts the **periodic** 200 ms `LED_200ms_timer` (`app.c:193-197`, `sl_sleeptimer_start_periodic_timer`) if it is not running, and toggles amber on each timeout using `single_LED_on[AMBER]` as the remembered state. `set_all_LED_off()` (`app.c:260-267`) only calls `set_LED_off()` on each LED: it does not stop that timer and does not change `single_LED_on[]`. So:
- At STOP, amber goes dark immediately (the I2C write turns it off).
- If amber was in its "on" phase, the next toggle "turns it off" (no visible change) and the one after turns it on, so it stays dark for at most two periods, about 400 ms. If it was in its "off" phase, the next toggle turns it on, so it is dark for 200 ms or less.
- After that it blinks normally. **It is not stuck.** The blink phase may shift by one toggle. This is reasoned from the code, not observed.

#### C3. The stale-timeout race: each release branch calls `settings_mode_cancel_holds()`

**The race.** A timer callback can set its flag after the last `settings_mode_tick()` and before the release edge is processed in `switch_input_tick()` of the same or a later pass. The release branch then sets `active_switch = OFF`, so the `if (active_switch == X)` block that would consume the flag never runs again for this hold, and the flag stays set. The next hold of any of the three buttons acts on it immediately. The old release branches stopped the timer(s) but left the flags.

**Why the replacement is equivalent, apart from clearing the stale flags, branch by branch:**

- **UP_ALL release** (`settings_mode.c:200-208`). Old: `sl_sleeptimer_stop_timer(&handle_2000ms_timer)`. New: the same stop, plus `timeout_2000ms = false`, plus stopping `handle_5000ms_timer` and `timeout_5000ms = false`.
  - `timeout_2000ms = false`: this is the fix. After this release, the only code that reads the flag is the UP_ALL/DOWN_ALL/AUTO_LEVEL tick blocks, and none of them can run for *this* hold, because `active_switch` is now OFF. So a set flag here can only ever be consumed by a *later* hold, which is exactly the bug. Clearing it loses nothing this hold would legitimately have done: the release turns the ladder LEDs off and the user has let go before the step.
  - Stopping the 5 s timer and clearing `timeout_5000ms`: a no-op in every reachable state. During an UP_ALL hold in settings mode the 5 s timer cannot be running. It is used only for (i) the entry hold, where it is stopped and cleared at the moment settings mode is entered (`:284-288`), and (ii) the AUTO_LEVEL hold, which ends with the AUTO_LEVEL release (stops it) or a STOP (now cancels it). `active_switch` is single-valued and UP_ALL can only be pressed from `active_switch == OFF` (`:194`), so an AUTO_LEVEL hold cannot be in progress at the same time. The only state in which `timeout_5000ms` could be true here is a stale flag left by the old AUTO_LEVEL race, which C3 also removes; clearing it is the fix again.
- **DOWN_ALL release** (`:236-244`): identical reasoning to UP_ALL (same timer, same flag, same 5 s argument; press arm `:230`).
- **AUTO_LEVEL release** (`:219-227`). Old: stop the 2 s timer and the 5 s timer. New: the same two stops, plus `timeout_2000ms = false` and `timeout_5000ms = false`.
  - `timeout_2000ms`: if the 2 s timer fired just before release, the old code left the flag. The next AUTO_LEVEL press resets `zeroLevelFirstPass = true` (`:213-217`) and the first tick then **stores the zero angle immediately**, from a single `accel_running_average()` call (`:320-345`); or the next UP/DOWN press steps immediately. Now it is cleared.
  - `timeout_5000ms`: if the 5 s erase timer fired just before release, the old code left the flag. The AUTO_LEVEL block checks `if (timeout_5000ms)` with no other condition (`:383`), so the next AUTO_LEVEL press would **erase the stored angles on its first tick**. Now it is cleared.
  - Both are the fix; neither can affect the hold being released, for the same reason as above.

**Other writers of `active_switch = OFF` that could end a settings hold without a release** (`grep -n "active_switch = OFF" *.c`, d.2.2), checked so that C2 + C3 cover every path:
- `switch_input.c:966` (Double Press Error): its block is gated `!in_settings_mode` (`:921`). It cannot run.
- `switch_input.c:847` (motion fail-safe): skipped in settings mode (`:806`). It cannot run.
- `app.c:599` (end of the collision blink): needs `any_collision`, which needs a motor running. Nothing is energised in settings mode.
- `switch_input.c:529-582`: the normal-mode `process_outputs()` arms, gated `!in_settings_mode` (`:517`).
- `bed_actions.c:317` (`app_release_active_switch`) and `:357` (`app_stop_movement`): reached from app motion/LIGHTS commands and the two-source force-stop, all refused in settings mode before they get there (`ble_bed_service.c:593-596`).
- `bed_actions.c:229` (`app_stop_all`): now covered by C2.
- `settings_mode.c:161` (exit) and `:259` (BED_LIGHTS release, not a hold).

So C2 and C3 together cover every reachable way a settings hold can end.

#### C4. Settings-mode entry also requires BED LIGHTS physically down

`settings_mode.c:273-277`:

```c
  // switch_pressed is only the direction of the LAST edge delivered, for any
  // button, so the entry hold also requires BED LIGHTS to be physically down
  // in the sampler's debounced state (#41).
  if ((switch_pressed == true) && (active_switch == SWITCH_BED_LIGHT_VALUE) && (!in_settings_mode)
      && ((switch_sampler_down() & SWITCH_BED_LIGHTS) != 0)){
```

Everything else in the block is unchanged. Facts used: `switch_sampler_down()` returns the debounced down-mask `deb_down` (`switch_input.c:457-460`), in the same bit layout as `app_ui_interrupt` (`switch_input.h:37-38`). `SWITCH_BED_LIGHTS` is `0x0008` (`app.h:118`), inside `SWITCH_INPUT_MASK 0xFF0F` (`switch_input.c:63`). The bench logs print BED LIGHTS as `SW: 0x0008`.

**Effect on a normal entry.** The press edge for BED LIGHTS is generated from `deb_down` changing, so from the moment the press is delivered until the release edge is delivered, the bit is set. A normal 5 s hold therefore still enters. The release edge comes from the same `deb_down` change that clears the bit, and the release calls `settings_mode_cancel_entry()` from `process_inputs()` (`switch_input.c:674`). So the new condition going false never leaves the 5 s entry timer running on its own.

**What it adds.** It closes the case the last handoff's QUESTIONS 7 raised: `switch_pressed` true because of some *other* button's press while `active_switch` still says BED LIGHTS. That cannot enter settings mode now. I did not add `switch_state_fresh()` (stale-sampler check): the prompt specifies the condition exactly. See QUESTIONS 6.

### b.4 Part D — docs cleanup (commit `9ba0ca3`) and the refused D2

- **D1** DECISIONS #19: one added line, `Implemented as #42 (2026-10-18).`, nothing else in #19 changed.
- **D2** CLAUDE.md — **NOT DONE.** The edit was refused by the permission system ("[Self-Modification]": CLAUDE.md is my instruction file). As the refusal instructed, I did not work around it. The diff the prompt asked to be shown, produced on a scratch copy (`diff -u`), for the user to apply by hand:

```diff
--- a/CLAUDE.md
+++ b/CLAUDE.md
@@ -52,11 +52,9 @@
   that used to require a hand-edited config file — if you ever find
   yourself wanting to hand-edit a generated file, that is a signal to
   stop and ask, not a shortcut to take.
-- **No BLE glue in this phase.** The radio stays dormant until the
-  migrated firmware passes the full `docs/REGRESSION.md` pass on hardware
-  (DECISIONS #20). Don't add `sl_bt_on_event` handling, GATT logic, or
-  anything beyond what the stock "Bluetooth – SoC Empty" sample already
-  provides, even if it looks like a natural next step.
+- **BLE status.** BLE Stage B (telemetry) and Stage C (app commands drive
+  the bed, DECISIONS #34) are built; the live status and what is still gated
+  on the full `docs/REGRESSION.md` pass is in `docs/MIGRATION_STATUS.md`.
 - **Keep the `bed_` module prefix.** Only cosmetic "Totem"/"Totem
   Sleep"→"Tilt Bed"/"Tilt Bed Systems" string changes are in scope; don't
   rename modules, functions, or the prefix.
```

  **A problem with that text, found while preparing it:** `docs/MIGRATION_STATUS.md` is itself stale on Stage C. Line 51 reads `- [ ] **Stage C** — Command characteristic → motors + dead-man watchdog`, and lines 51-56 say "Not started. Hard-gated on the Phase 6 REGRESSION.md pass"; lines 70 and 100-102 say the same. DECISIONS #34 ("Stage C — the app drives the bed", `docs/DECISIONS.md:991`) and the code (`ble_bed_service.c` process_pending_command) show it is built. Pointing CLAUDE.md at MIGRATION_STATUS.md as "the live status" would point readers at a wrong status. The prompt says "BLE Stage B and C are built - MIGRATION_STATUS.md, DECISIONS #34", but MIGRATION_STATUS.md does not say that. See QUESTIONS 1.
- **D3** REFACTOR_PLAN.md P3.13 ("Settings mode has no exit"): appended `**DONE (2026-10-18, commit `dd8d069`):** a new press of BED LIGHTS exits settings mode (`settings_mode_exit()`).`; the original text is left as it was.
- **D4** SWITCH_SAMPLER_DESIGN.md: one line under the title: `Note (2026-10-18): the debounce is now 2 samples (commit 47db449); the body below is the historical design.`; the body is unchanged.

### b.5 Part E — the reviewer's bench steps, with the expected PuTTY lines

Firmware: `9ba0ca3` (code identical to `840d7b8`), `BLE_DEBUG_SECURITY_LOG` 1. All of this is **expected, not observed**.

**E1. Pairing refused** (delete the bond on the phone, bed NOT in settings mode, connect from the app):

```
BLE: connection_opened conn=N bonding=0x01
BLE: connection_parameters conn=N interval=24 security_mode=0
BLE: characteristic_status DROPPED conn=N client_config_flags=0x0001 (matches neither control nor viewer connection)   <- may or may not appear, as in Prompt V
BLE: connection_parameters conn=N interval=24 security_mode=0
BLE SM: confirm_bonding conn=N handle=0x01 (REPLACING existing bond) -> REJECTED (not in settings mode)
BLE SM: bonding_failed conn=N reason=0x1205
BLE: closing conn=N - pairing refused
BLE: connection_closed conn=N reason=0x1016
```

- `bonding=0x01` and `handle=0x01` assume the firmware still holds this phone's bond in the same slot as in Prompt V (`connection_opened conn=2 bonding=0x01`). `handle=` should equal the `bonding=` value of the `connection_opened` line. For a phone the bed has never bonded, expect `bonding=0xff` and `handle=0xff (new bond)`.
- `reason=0x1016` is `SL_STATUS_BT_CTRL_CONNECTION_TERMINATED_BY_LOCAL_HOST` (`sl_status.h:274`): the bed closed it. **This is the line that shows B2 worked.** Prompt V had `0x1013` (remote user terminated) here.
- **If the phone closes first:** (i) if its terminate reaches the bed before `bonding_failed` is handled, there is no `closing conn` line and `connection_closed ... reason=0x1013` appears, before or instead of the `bonding_failed` line. (ii) If they cross, the `closing conn` line prints and the single `connection_closed` shows 0x1013 or 0x1016, whichever side won. In every case there is exactly one `connection_closed` for conn=N.
- The phone should still show the pairing failure (GATT_AUTH_FAIL in Prompt V).
- If the app auto-reconnects, expect the whole block to repeat with a new conn number each time until the app gives up or the bed enters settings mode.

**E2. STOP in the app while holding UP ALL in settings mode, before the 2 s step:**
1. Enter settings mode (hold BED LIGHTS 5 s, amber fast-blinks, release).
2. Hold UP ALL. Expect `SW: 0x0001 down (bounces 0)` and `2 second timer start`. **Before 2 s**, press STOP in the app. The app should get Command Result ACCEPTED. There is no PuTTY line for STOP itself.
3. Expect no `Writing values to NVM3...`, no `Successfully stored raise security margin`, no `Stored Raise Sensitivity is: ...`, and none of the ladder LEDs (red, green, blue) on. Amber goes dark for at most ~0.4 s, then keeps fast-blinking. Settings mode is not left: no `Settings mode: exit`.
4. Keep holding UP ALL past 2 s: still nothing printed. Release: `SW: 0x0001 up (bounces 0)` and nothing else.
5. Hold UP ALL again: `SW: 0x0001 down (bounces 0)`, `2 second timer start`, and only **after a full 2 s** `Writing values to NVM3...`, `Successfully stored raise security margin`, `Successfully stored lower security margin`, `Stored Raise Sensitivity is: N`, `Stored Lower Sensitivity is: M`, with the new rung's LED pattern.
- Masks: UP ALL `0x0001`, AUTO LEVEL `0x0002`, DOWN ALL `0x0004`, BED LIGHTS `0x0008` (`app.h:115-118`); `0x8000` is UP HEAD (`switch_input.c:49`).
- The same check applies to AUTO LEVEL (STOP before 2 s: no `Writing values to NVM3...`; the next AUTO LEVEL hold needs the full 2 s) and DOWN ALL.

**E3. Settings-mode entry and exit still working after C4:**
1. Hold BED LIGHTS: `SW: 0x0008 down (bounces 0)`. The under-bed lights toggle as on any press. Four `Max Current for Motor N: ...` lines with near-zero values may appear during the hold (pre-existing, see b.1). At 5 s amber fast-blinks: settings mode is active. There is no PuTTY line for entry.
2. Release: `SW: 0x0008 up (bounces 0)`, nothing else. Amber keeps blinking.
3. Press BED LIGHTS once: `SW: 0x0008 down (bounces 0)`, `Settings mode: exit`, then on release `SW: 0x0008 up (bounces 0)`. Amber shows the lights state.
4. Negative check (C4's purpose, hard to provoke): a short BED LIGHTS tap (< 5 s) must never enter settings mode. That was already true and should stay true.

**B1 (overwrite the least recently used bond) on the bench, without changing code: it cannot be fully checked with one or two phones.** Eviction only happens when the store already holds 8 bonds and a 9th device bonds. A phone keeps the same identity address when it re-pairs, so re-pairing the same phone replaces its own bond ("REPLACING existing bond", as Prompt V showed) rather than taking a new slot. Two phones occupy at most two slots, and the bond count already in NVM3 is unknown (bonds survive reflashes; `BLE_BENCH_WIPE_BONDS` is 0). What *can* be checked without code changes:
- At boot, `BLE SM: store_bonding_configuration -> 0x0000`, which shows the stack accepted policy 2 (an invalid policy would return an error and `app_assert_status` would halt).
- A full check needs 7-8 distinct BLE identities, for example a Linux laptop with `btmgmt` changing its static address between pairings, or several more phones. Each pairs in settings mode. Then the least recently connected one should fail to reconnect, with `confirm_bonding ... (REPLACING ...)` absent and the phone reporting an auth failure. That needs extra equipment and was not attempted. A firmware-side count would need code, e.g. `sl_bt_sm_get_bonding_handles()`, which is a code change and so out of scope here.

## c. Diffs (complete, untruncated)

Each is `git show --format= <sha>` on the committed commit. All four are under 400 lines, so none is truncated, docs included. The `docs/SESSION_LOG.md` diff is not reproduced here (it ships alongside this file), and this handoff's own commit is not reproduced either.

### c.1 Commit A `9798c5a` (docs only)

```
commit 9798c5a495e5f561f05593b857df1b507b00744a
Record 2026-10-18 Prompt V bench result (docs only)

 docs/DECISIONS.md                                  |  43 +++++++
 docs/REGRESSION.md                                 |  13 ++
 ...ding-policy-reject-disconnect-settings-holds.md | 143 +++++++++++++++++++++
 3 files changed, 199 insertions(+)
```

```diff
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 3d52e6e..7d2f506 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -1888,3 +1888,46 @@ or `... -> REJECTED (not in settings mode)`.
 Builds clean from the command line. **Not run on the bench.** That a rejected
 phone sees a clean failure (rather than retrying or hanging) is the SDK's
 documented behaviour for confirm = 0, not observed here.
+
+### Bench result (2026-10-18)
+
+The user ran `944f19e` (Prompt V: debounce 2 samples, settings-mode exit,
+pairing window; `BLE_DEBUG_SECURITY_LOG` 1) on the bed with the Flutter app
+on a Samsung S10. Log: `_03-Software/PuTTY Logs/prompt_v_bench.log` (193
+lines). Every figure below was re-counted from that log. **VERIFIED (user's
+bench run).**
+
+- **Debounce 2:** 88 accepted edges, all `(bounces 0)`. Six two-button
+  episodes, all printed `SW: 2 switches down - presses ignored`; three of
+  them also printed `Double Press Error` (as designed - one press delivered
+  first, then stopped). No `FAILSAFE`, no `Unrecognized switch mask`.
+- **Settings mode exit:** `Settings mode: exit` printed four times (log
+  lines 79, 86, 93, 171), each on a single new BED LIGHTS press. After the
+  third exit every button's press and release came through, and a full
+  auto-level ran to `PITCH Leveled successfully` / `Exit auto-level`. The
+  log shows the other presses' edges but no motor-current lines for them
+  (those print only after ~300 ms of running), so "every button worked" is
+  the user's observation, not something the log alone shows.
+- **Pairing window:**
+  - (a) bonded phone reconnecting (lines 142-148): `connection_opened
+    bonding=0x01`, `security_mode=1`, no `confirm_bonding` line.
+  - (b) phone-side bond deleted, not in settings mode (lines 149-155):
+    `confirm_bonding conn=2 (REPLACING existing bond) -> REJECTED (not in
+    settings mode)`, `bonding_failed conn=2 reason=0x1205`, then
+    `connection_closed conn=2 reason=0x1013` - the phone closed the link
+    itself; the firmware did not.
+  - (c) in settings mode (lines 162-169): `-> ACCEPTED (settings mode)`,
+    then `bonded conn=3`.
+  - (d) after the exit (line 171) the phone reconnected (lines 176-181,
+    `bonding=0x01`, `security_mode=1`) with no `confirm_bonding` line, and
+    the motor-current lines that follow with no `SW:` line before them are
+    app-driven movement.
+- **Phone side** (reported by the user from the phone's screen, not in the
+  PuTTY log): the app showed `Connection failed: ... status 137
+  (GATT_AUTH_FAIL), type ... CHARACTERISTIC_READ` for (b). So the refused
+  phone gets a clean failure, not a hang - observed with this phone and app
+  only.
+
+That the Android app closed the link after the rejection is that app's
+behaviour, not the firmware's. Prompt W part B (recorded as #43, below)
+makes the firmware close it.
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index 82db63f..3a0ef79 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -237,6 +237,19 @@ pitch, all actuators clear of limits.
 - [ ] ⚡ BLE link stays up (no disconnect) through settings mode entry,
       zero-level store/erase, and sensitivity adjustment
 
+**Recorded result, 2026-10-18 (firmware `944f19e`, debounce 2 samples,
+log `prompt_v_bench.log`; full figures in DECISIONS #42 "Bench result").**
+This section has no pairing-window checklist item yet; the pairing rows
+below record the DECISIONS #42 checks the user ran alongside 7.9.
+
+| Item | Result |
+|---|---|
+| 7.9 | `Settings mode: exit` four times, each on one new BED LIGHTS press; afterwards every button's press and release came through and a full auto-level completed |
+| Pairing, bonded phone reconnects | no `confirm_bonding` line, reached `security_mode=1` (before and after a settings-mode session) |
+| Pairing refused (phone bond deleted, not in settings mode) | `-> REJECTED (not in settings mode)`, `bonding_failed reason=0x1205`, then the phone closed the link (`connection_closed reason=0x1013`); phone showed `GATT_AUTH_FAIL` (status 137) |
+| Pairing in settings mode | `-> ACCEPTED (settings mode)`, then `bonded` |
+| 7.1-7.8, BLE link item | not separately recorded |
+
 ## 8. Bed lights
 
 - [ ] ⚡ Tap BED_LIGHTS: under-bed lighting on, amber LED on
diff --git a/docs/prompts/PROMPT_W_bonding-policy-reject-disconnect-settings-holds.md b/docs/prompts/PROMPT_W_bonding-policy-reject-disconnect-settings-holds.md
new file mode 100644
index 0000000..067883a
--- /dev/null
+++ b/docs/prompts/PROMPT_W_bonding-policy-reject-disconnect-settings-holds.md
@@ -0,0 +1,143 @@
+# Prompt W - bond store policy, disconnect refused pairings, settings-mode holds
+
+Written by the reviewer on 2026-10-18 after the user's bench run of Prompt V
+(log: C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\prompt_v_bench.log).
+Implement as specified. Where the code contradicts this document, stop and
+report it under QUESTIONS FOR REVIEW instead of improvising. Follow CLAUDE.md
+for the handoff file, the session log, committing and pushing.
+BED_BENCH_PROVOKE_RUNAWAY and BLE_BENCH_WIPE_BONDS stay 0.
+
+Commit this prompt file with the first commit. Expected repo state: HEAD
+3f11c12, level with origin/main; .projectlinkstore modified (never commit it).
+
+Make FOUR separate commits, A to D, building from the command line after each
+(the user permits it). Report the raw build tail and any warnings.
+
+======================================================================
+A. Record the Prompt V bench result (docs only)
+======================================================================
+Read the log and confirm before recording (all from the user's run):
+  - Debounce 2: 88 accepted edges, all "(bounces 0)"; two-button tests gave
+    "presses ignored" / "Double Press Error" as designed; no FAILSAFE, no
+    "Unrecognized switch mask".
+  - Settings mode left with one BED LIGHTS press, four times
+    ("Settings mode: exit"); every button worked afterwards, including a
+    full auto-level.
+  - Pairing: (a) bonded phone reconnected with no confirm_bonding line;
+    (b) phone-side bond deleted, not in settings mode:
+    "confirm_bonding conn=2 (REPLACING existing bond) -> REJECTED",
+    "bonding_failed conn=2 reason=0x1205", then
+    "connection_closed conn=2 reason=0x1013" (the phone closed it);
+    (c) in settings mode: "-> ACCEPTED (settings mode)" then "bonded";
+    (d) after exit the phone reconnected with no confirm_bonding line and
+    app movement worked.
+  - The phone (Samsung S10, the Flutter app) showed: "Connection failed:
+    ... status 137 (GATT_AUTH_FAIL), type ... CHARACTERISTIC_READ".
+Record as VERIFIED (user's bench run) in the SESSION_LOG entry, as a dated
+"Bench result" note under DECISIONS #42, and in REGRESSION.md (the settings
+exit and pairing checks).
+
+======================================================================
+B. Bluetooth bonding (ble_bed_service.c) - user decisions 1 and 2
+======================================================================
+B1. Bond store full -> overwrite the least recently used bond.
+    Change sl_bt_sm_store_bonding_configuration(BLE_BED_SERVICE_MAX_BONDS, 0)
+    to the policy the SDK documents as "New bonding will overwrite the
+    bonding that was used the longest time ago" - confirm the value (2)
+    from sl_bt_api.h and cite the line. Rewrite the comment above it, which
+    is wrong today (policy 0 pairs but stores nothing; it does not reject).
+    Reason, for the comment and the DECISIONS entry: since #42 every new
+    bond needs someone at the physical controller, so eviction can no
+    longer be triggered by a stranger; the evicted phone re-pairs in
+    settings mode.
+
+B2. Disconnect a phone whose pairing the firmware refused.
+    Today the Android app happened to close the link itself after the
+    rejection (0x1013); another phone or app may not, and the bed has only
+    two connection slots. Design:
+      - In the confirm_bonding handler, when rejecting, remember that
+        connection (per-connection flag sized for the stack's connection
+        count).
+      - In the sl_bt_evt_sm_bonding_failed handler, if that connection's
+        flag is set, call the SDK's non-blocking connection-close command
+        for it (confirm the exact function name and signature in
+        sl_bt_api.h for this SDK) and print
+        "BLE: closing conn=%u - pairing refused".
+      - Clear the flag on connection_closed and on any new connection with
+        that handle.
+    Do NOT close on the confirm_bonding event itself: closing before the
+    stack has sent the pairing failure could leave the phone without a
+    clean error. Nothing here may block (DECISIONS #27): the close command
+    only queues the request.
+
+B3. Put the stored bond number back in the confirm_bonding print:
+    "BLE SM: confirm_bonding conn=%u handle=0x%02x (%s) -> %s".
+
+Record B1 and B2 in one new DECISIONS entry (next free number). Update
+BLE_GATT_DESIGN.md section 8 to match.
+
+======================================================================
+C. Settings mode holds - user decision 3, and two hardening fixes
+======================================================================
+Background: in settings mode, holding UP ALL or DOWN ALL for 2 s steps the
+collision sensitivity, and holding AUTO LEVEL for 2 s stores the zero angle
+(held to 5 s, erases it). Each hold runs handle_2000ms_timer /
+handle_5000ms_timer; the setting changes when the timeout flag is consumed.
+
+C1. Add settings_mode_cancel_holds() in settings_mode.c (declared in
+    settings_mode.h): stops handle_2000ms_timer and handle_5000ms_timer and
+    clears timeout_2000ms and timeout_5000ms. No NVM3 write, does NOT leave
+    settings mode. settings_mode_exit() calls it instead of its own four
+    lines.
+
+C2. STOP ALL in settings mode cancels any hold in progress and STAYS in
+    settings mode (user decision). Call settings_mode_cancel_holds() from
+    app_stop_all() when in_settings_mode is true. Confirm app_stop_all() is
+    only reached from the STOP_ALL opcode path (grep callers; report). The
+    amber fast blink must keep running afterwards: check that
+    app_stop_all()'s set_all_LED_off() does not leave the blink stuck (the
+    blink is driven from settings_mode_tick() every pass) and report what
+    the amber LED does.
+
+C3. Fix the stale-timeout race the last handoff found (its QUESTIONS 11):
+    the UP_ALL and DOWN_ALL release branches in
+    settings_mode_process_outputs() stop handle_2000ms_timer but leave
+    timeout_2000ms set, and the AUTO_LEVEL release stops both timers but
+    leaves both flags. Each release branch calls settings_mode_cancel_holds()
+    instead. Explain in the handoff why that is equivalent for each branch.
+
+C4. Harden the settings-mode entry test (last handoff's QUESTIONS 7). In
+    settings_mode_tick(), the entry condition relies on the global
+    switch_pressed (the direction of the last delivered edge). Add the
+    physical check: BED LIGHTS must be down in the sampler's debounced
+    state, i.e. (switch_sampler_down() & SWITCH_BED_LIGHTS) != 0, in
+    addition to the existing conditions. Keep everything else.
+
+======================================================================
+D. Docs cleanup (docs only)
+======================================================================
+D1. DECISIONS #19: add one forward-pointer line, "Implemented as #42
+    (2026-10-18)". Change nothing else in #19.
+D2. CLAUDE.md: the "No BLE glue in this phase" statement is stale (BLE
+    Stage B and C are built - MIGRATION_STATUS.md, DECISIONS #34). Replace
+    it with the current status in one or two lines, pointing to
+    MIGRATION_STATUS.md. Show the diff.
+D3. docs/REFACTOR_PLAN.md P3.13 ("Settings mode has no exit"): mark it done,
+    pointing to commit dd8d069.
+D4. docs/SWITCH_SAMPLER_DESIGN.md: add a one-line note at the top that the
+    debounce is now 2 samples (commit 47db449); leave the body as the
+    historical design.
+
+======================================================================
+E. For the reviewer's bench steps
+======================================================================
+In the handoff, give the exact PuTTY lines expected for:
+  1. pairing refused (phone bond deleted, not in settings mode), including
+     the new "closing conn" line, and what happens if the phone closes
+     first;
+  2. STOP in the app while holding UP ALL in settings mode (before the
+     2 s step): nothing stored, amber keeps blinking, next UP ALL hold
+     needs the full 2 s;
+  3. settings-mode entry and exit still working after C4.
+Also propose how B1 (overwrite oldest bond) could be checked on the bench
+with one or two phones, without changing code. If it cannot, say so.
```

### c.2 Commit B `c6ee6b0` (source + docs)

```
commit c6ee6b0b5653fc73335ff8e01782f5c392a13012
Bonding: overwrite least recently used bond when full; close refused pairings (DECISIONS #43)

 ble_bed_service.c       | 94 +++++++++++++++++++++++++++++++++++++++++++++----
 docs/BLE_GATT_DESIGN.md |  8 +++--
 docs/DECISIONS.md       | 56 +++++++++++++++++++++++++++++
 3 files changed, 150 insertions(+), 8 deletions(-)
```

```diff
diff --git a/ble_bed_service.c b/ble_bed_service.c
index 1626ddc..d554398 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -32,6 +32,7 @@
 
 #include "app_assert.h"
 #include "gatt_db.h"
+#include "sl_bluetooth.h"            // SL_BT_CONFIG_MAX_CONNECTIONS_SUM - sizes refused_pairing_conn[]
 #include "sl_sleeptimer.h"
 
 #include "auto_level.h"              // tilt_telemetry_arm_burst() - triggers T1 and T2
@@ -192,6 +193,16 @@ static uint8_t open_connections[2] = { SLOT_EMPTY, SLOT_EMPTY };
 static uint8_t control_connection = SLOT_EMPTY;
 static uint8_t viewer_connection  = SLOT_EMPTY;
 
+// Connections whose pairing request the pairing window refused (#42), so the
+// sl_bt_evt_sm_bonding_failed handler can close them (DECISIONS.md #43). It
+// holds connection handles rather than being indexed by them, so it assumes
+// nothing about how the stack numbers its handles, and it has one entry per
+// connection the stack can hold at once. Set in confirm_bonding, cleared on
+// connection_opened and connection_closed for that handle. Filled with
+// SLOT_EMPTY at system_boot, before any connection can exist. Stack events
+// only, so main-loop context; no volatile needed.
+static uint8_t refused_pairing_conn[SL_BT_CONFIG_MAX_CONNECTIONS_SUM];
+
 static bool control_status_subscribed = false;
 static bool viewer_status_subscribed  = false;
 static uint8_t status_subscriber_count = 0;
@@ -294,6 +305,43 @@ static void track_connection_closed(uint8_t connection)
   }
 }
 
+static void pairing_refused_mark(uint8_t connection)
+{
+  for (uint8_t i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS_SUM; i++) {
+    if (refused_pairing_conn[i] == connection) {
+      return; // already marked (the phone asked again on the same link)
+    }
+  }
+  for (uint8_t i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS_SUM; i++) {
+    if (refused_pairing_conn[i] == SLOT_EMPTY) {
+      refused_pairing_conn[i] = connection;
+      return;
+    }
+  }
+  // No free entry: cannot happen while every close clears its handle, since
+  // there is one entry per stack connection. If it did, this link is simply
+  // not closed by the firmware - the behaviour before #43.
+}
+
+static bool pairing_refused_is_marked(uint8_t connection)
+{
+  for (uint8_t i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS_SUM; i++) {
+    if (refused_pairing_conn[i] == connection) {
+      return true;
+    }
+  }
+  return false;
+}
+
+static void pairing_refused_clear(uint8_t connection)
+{
+  for (uint8_t i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS_SUM; i++) {
+    if (refused_pairing_conn[i] == connection) {
+      refused_pairing_conn[i] = SLOT_EMPTY;
+    }
+  }
+}
+
 // Shared by sl_bt_evt_sm_bonded (fresh pairing) and sl_bt_evt_connection_
 // parameters (covers a RESUMED bond too - sm_bonded's own doc says it fires
 // for a pairing/bonding PROCEDURE completing, not for simply re-establishing
@@ -885,9 +933,15 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
       app_assert_status(sc);
       printf("BLE SM: configure -> 0x%04x\r\n", (unsigned)sc);
 
-      // Reject new bonds once the 8-bond store is full, rather than
-      // silently evicting an existing one.
-      sc = sl_bt_sm_store_bonding_configuration(BLE_BED_SERVICE_MAX_BONDS, 0);
+      // Bond store full (8 bonds): a new bond overwrites the one used the
+      // longest time ago - policy 2 in sl_bt_api.h's
+      // sl_bt_sm_store_bonding_configuration() doc (DECISIONS.md #43). The
+      // previous policy 0 did not reject a 9th phone: per the same doc it
+      // paired it but stored no bond for it.
+      // Eviction is safe now because since #42 every new bond needs someone
+      // at the bed's switch panel (settings mode), so a stranger cannot
+      // trigger it; a phone that was evicted pairs again in settings mode.
+      sc = sl_bt_sm_store_bonding_configuration(BLE_BED_SERVICE_MAX_BONDS, 2);
       app_assert_status(sc);
       printf("BLE SM: store_bonding_configuration -> 0x%04x\r\n", (unsigned)sc);
 
@@ -899,6 +953,9 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
       app_assert_status(sc);
       printf("BLE SM: set_bondable_mode(1) -> 0x%04x\r\n", (unsigned)sc);
 
+      // No connection exists yet, so no pairing has been refused (#43).
+      memset(refused_pairing_conn, SLOT_EMPTY, sizeof(refused_pairing_conn));
+
 #if BLE_BENCH_WIPE_BONDS
       sc = sl_bt_sm_delete_bondings();
       printf("BLE SM: BENCH-ONLY delete_bondings() -> 0x%04x\r\n", (unsigned)sc);
@@ -924,6 +981,7 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
     case sl_bt_evt_connection_opened_id: {
       uint8_t connection = evt->data.evt_connection_opened.connection;
       track_connection_opened(connection);
+      pairing_refused_clear(connection); // a new link on a reused handle starts unrefused (#43)
 
 #if BLE_DEBUG_SECURITY_LOG
       // bonding != 0xff here means the stack recognized this as a RESUMING
@@ -955,6 +1013,7 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
              evt->data.evt_connection_closed.connection,
              evt->data.evt_connection_closed.reason);
 #endif
+      pairing_refused_clear(evt->data.evt_connection_closed.connection); // #43
       handle_connection_closed(evt->data.evt_connection_closed.connection);
       break;
 
@@ -978,13 +1037,31 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
     // An sl_status_t code - see sl_status.h SL_STATUS_BT_SMP_* for
     // pairing-specific failures (e.g. 0x1203 = AUTHENTICATION_REQUIREMENTS,
     // IO-capability mismatch).
-    case sl_bt_evt_sm_bonding_failed_id:
+    //
+    // DISCONNECT A REFUSED PHONE (DECISIONS.md #43). If this connection's
+    // pairing was refused by the window below, close it here: the bed has
+    // only two role slots and a phone or app that stays connected after a
+    // refusal would hold one. Done on this event, not on confirm_bonding,
+    // so the stack has already sent the pairing failure and the phone gets a
+    // clean error first. sl_bt_connection_close() only queues the
+    // termination and returns (it does not block, #27); connection_closed
+    // follows and clears the mark.
+    case sl_bt_evt_sm_bonding_failed_id: {
+      uint8_t connection = evt->data.evt_sm_bonding_failed.connection;
 #if BLE_DEBUG_SECURITY_LOG
       printf("BLE SM: bonding_failed conn=%u reason=0x%04x\r\n",
-             evt->data.evt_sm_bonding_failed.connection,
+             connection,
              evt->data.evt_sm_bonding_failed.reason);
 #endif
+      if (pairing_refused_is_marked(connection)) {
+#if BLE_DEBUG_SECURITY_LOG
+        printf("BLE: closing conn=%u - pairing refused\r\n", connection);
+#endif
+        sc = sl_bt_connection_close(connection);
+        (void)sc; // best-effort: if the phone is already closing the link, its own connection_closed follows
+      }
       break;
+    }
 
     // Fires for every bonding attempt now that sl_bt_sm_configure()'s bit 3
     // (BONDING_REQUEST_REQUIRED) is set (DECISIONS.md #28). bonding_handle
@@ -1007,11 +1084,16 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
       const sl_bt_evt_sm_confirm_bonding_t *cb = &evt->data.evt_sm_confirm_bonding;
       uint8_t confirm = app_in_settings_mode() ? 1 : 0;
 #if BLE_DEBUG_SECURITY_LOG
-      printf("BLE SM: confirm_bonding conn=%u (%s) -> %s\r\n",
+      printf("BLE SM: confirm_bonding conn=%u handle=0x%02x (%s) -> %s\r\n",
              cb->connection,
+             cb->bonding_handle,
              (cb->bonding_handle == SL_BT_INVALID_BONDING_HANDLE) ? "new bond" : "REPLACING existing bond",
              confirm ? "ACCEPTED (settings mode)" : "REJECTED (not in settings mode)");
 #endif
+      if (!confirm) {
+        // The bonding_failed that follows closes this link (#43).
+        pairing_refused_mark(cb->connection);
+      }
       sl_bt_sm_bonding_confirm(cb->connection, confirm);
       break;
     }
diff --git a/docs/BLE_GATT_DESIGN.md b/docs/BLE_GATT_DESIGN.md
index fb32dbc..7eaedfa 100644
--- a/docs/BLE_GATT_DESIGN.md
+++ b/docs/BLE_GATT_DESIGN.md
@@ -349,8 +349,12 @@ during a physical pairing window: while settings mode is active on the
 switch panel** (DECISIONS #19, implemented as DECISIONS #42). Outside the
 window, pairing/bonding requests are rejected, so a stranger within radio
 range cannot enroll. Re-pairing a phone that already has a bond is gated the
-same way; a bonded phone reconnecting is not affected. Store 8 bonds in
-NVM3.
+same way; a bonded phone reconnecting is not affected. **A refused phone is
+disconnected by the firmware** once the stack reports the pairing failure
+(DECISIONS #43), so it cannot sit on one of the two connection slots. Store 8
+bonds in NVM3; **when the store is full, a new bond overwrites the bond used
+the longest time ago** (DECISIONS #43). That is safe because every new bond
+needs the pairing window; an evicted phone pairs again in settings mode.
 
 **Advertising (fast-connect lever #1).** The board is powered from the van
 battery bank — coin-cell advertising economics do not apply. Advertise
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 7d2f506..11b8de1 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -1931,3 +1931,59 @@ bench run).**
 That the Android app closed the link after the rejection is that app's
 behaviour, not the firmware's. Prompt W part B (recorded as #43, below)
 makes the firmware close it.
+
+## 43. Bond store full: overwrite the least recently used bond; the firmware disconnects a phone whose pairing it refused
+
+Date: 2026-10-18. User decisions (Prompt W, decisions 1 and 2), building on
+#42.
+
+### 1. Bond store full -> overwrite the bond used the longest time ago
+
+`sl_bt_sm_store_bonding_configuration(BLE_BED_SERVICE_MAX_BONDS, 2)` at
+system boot (`ble_bed_service.c`), was `(..., 0)`. The SDK documents the
+policy values (`sl_bt_api.h`, the function's doc): 0 = "If database is
+full, pairing will be performed but no bonding is stored"; 1 = "New bonding
+will overwrite the oldest existing bonding"; 2 = "New bonding will
+overwrite the bonding that was used the longest time ago". The bond count
+stays 8 (#19).
+
+The old code comment said policy 0 rejects a new bond when the store is
+full. It does not: per the doc above it pairs the phone and stores nothing.
+The comment is rewritten.
+
+**Why eviction is acceptable now.** Since #42 every new bond needs someone
+at the bed's switch panel to put it into settings mode, so eviction can no
+longer be triggered by a stranger within radio range. A phone that was
+evicted has to pair again, in settings mode, like a new phone.
+
+### 2. Disconnect a phone whose pairing was refused
+
+In the Prompt V bench run (#42 "Bench result") the Android app closed the
+link itself after the rejection (`connection_closed reason=0x1013`, remote
+user terminated). Another phone or app may not, and a refused phone that
+stays connected holds one of the bed's two role slots.
+
+- In the `sl_bt_evt_sm_confirm_bonding` handler, when the window rejects a
+  request, the connection handle is recorded in `refused_pairing_conn[]`
+  (one entry per connection the stack can hold,
+  `SL_BT_CONFIG_MAX_CONNECTIONS_SUM`; it stores handles rather than being
+  indexed by them, so it does not depend on how the stack numbers handles).
+- In the `sl_bt_evt_sm_bonding_failed` handler, if that connection is
+  recorded, the firmware prints `BLE: closing conn=N - pairing refused` and
+  calls `sl_bt_connection_close(connection)`.
+- The record is cleared on `connection_closed` and on `connection_opened`
+  for that handle, and the table is emptied at system boot.
+
+**Not on the confirm_bonding event itself:** closing before the stack has
+sent the pairing failure could leave the phone without a clean error.
+**Nothing blocks (#27):** `sl_bt_connection_close()` is documented as
+asynchronous - it starts the termination and the result arrives later as
+`sl_bt_evt_connection_closed`.
+
+### Status
+
+Builds clean from the command line. **Not run on the bench.** The expected
+outcome is that a refused phone now sees `connection_closed ...
+reason=0x1016` (terminated by the local host) instead of 0x1013, unless the
+phone closes first. That is inferred from `sl_status.h`, not observed.
+Eviction (part 1) has not been observed either: it needs a full 8-bond store.
```

### c.3 Commit C `840d7b8` (source)

```
commit 840d7b87665a0c14f94b86a81506c30dd5bc1947
Settings mode: STOP cancels holds, releases clear stale timeouts, entry needs BED LIGHTS down

 bed_actions.c   |  8 ++++++++
 settings_mode.c | 32 +++++++++++++++++++++++---------
 settings_mode.h |  7 +++++++
 3 files changed, 38 insertions(+), 9 deletions(-)
```

```diff
diff --git a/bed_actions.c b/bed_actions.c
index 93d7d4f..e22a70e 100644
--- a/bed_actions.c
+++ b/bed_actions.c
@@ -218,6 +218,14 @@ void app_stop_all(void)
   }
   set_all_LED_off();
   reset_limit_switch_flags(); // stale inferred limits must not survive a stop (#37)
+  if (in_settings_mode) {
+    // STOP in settings mode abandons a sensitivity or zero-angle hold in
+    // progress and stays in settings mode (Prompt W, user decision 3). Without
+    // this the hold's timer keeps running after active_switch is cleared
+    // below, and its timeout would act on the next hold the moment it starts.
+    // The amber blink resumes by itself: settings_mode_tick() keeps it going.
+    settings_mode_cancel_holds();
+  }
   active_switch = OFF;
 }
 
diff --git a/settings_mode.c b/settings_mode.c
index c030307..1fe9165 100644
--- a/settings_mode.c
+++ b/settings_mode.c
@@ -111,6 +111,20 @@ void settings_mode_cancel_entry(void)
   timeout_5000ms = false;
 }
 
+// Abandons any settings-mode hold in progress - a sensitivity step (UP_ALL/
+// DOWN_ALL, 2 s) or a zero-angle store/erase (AUTO_LEVEL, 2 s / 5 s). Stops
+// both timers AND clears both timeout flags: a flag left set by a timer that
+// fired just before the hold ended would otherwise be acted on by the NEXT
+// hold the moment it starts. Writes nothing to NVM3 and does not leave
+// settings mode.
+void settings_mode_cancel_holds(void)
+{
+  sl_sleeptimer_stop_timer(&handle_2000ms_timer);
+  timeout_2000ms = false;
+  sl_sleeptimer_stop_timer(&handle_5000ms_timer);
+  timeout_5000ms = false;
+}
+
 // Leaves settings mode and returns the panel to a clean normal-mode idle.
 // Triggered by a NEW press of BED LIGHTS while in settings mode (see
 // settings_mode_process_outputs()). Also closes the BLE pairing window, which
@@ -124,10 +138,7 @@ void settings_mode_exit(void)
 
   // Abandon any store/erase/sensitivity hold in progress: nothing half-done
   // survives the exit, and nothing is written to NVM3 here.
-  sl_sleeptimer_stop_timer(&handle_2000ms_timer);
-  timeout_2000ms = false;
-  sl_sleeptimer_stop_timer(&handle_5000ms_timer);
-  timeout_5000ms = false;
+  settings_mode_cancel_holds();
 
   // Settings mode is excluded from the motion fail-safe, so leave nothing
   // energised behind it.
@@ -189,7 +200,7 @@ void settings_mode_process_outputs(void)
       else if (active_switch == SWITCH_UP_ALL_VALUE){
 
         active_switch = OFF;
-        sl_sleeptimer_stop_timer(&handle_2000ms_timer);
+        settings_mode_cancel_holds(); // also clears a timeout that fired just before the release
         set_LED_off(LED_RED);
         set_LED_off(LED_GREEN);
         set_LED_off(LED_BLUE);
@@ -208,8 +219,7 @@ void settings_mode_process_outputs(void)
       // On release
       else if ((!switch_pressed) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
         active_switch = OFF;
-        sl_sleeptimer_stop_timer(&handle_2000ms_timer);
-        sl_sleeptimer_stop_timer(&handle_5000ms_timer);
+        settings_mode_cancel_holds(); // also clears a timeout that fired just before the release
         set_LED_off(LED_RED);
         set_LED_off(LED_GREEN);
         set_LED_off(LED_BLUE);
@@ -226,7 +236,7 @@ void settings_mode_process_outputs(void)
       else if (active_switch == SWITCH_DOWN_ALL_VALUE){
 
         active_switch = OFF;
-        sl_sleeptimer_stop_timer(&handle_2000ms_timer);
+        settings_mode_cancel_holds(); // also clears a timeout that fired just before the release
         set_LED_off(LED_RED);
         set_LED_off(LED_GREEN);
         set_LED_off(LED_BLUE);
@@ -260,7 +270,11 @@ void settings_mode_process_outputs(void)
 // active) the zero-angle store/erase and sensitivity-ladder logic.
 void settings_mode_tick(void)
 {
-  if ((switch_pressed == true) && (active_switch == SWITCH_BED_LIGHT_VALUE) && (!in_settings_mode)){
+  // switch_pressed is only the direction of the LAST edge delivered, for any
+  // button, so the entry hold also requires BED LIGHTS to be physically down
+  // in the sampler's debounced state (#41).
+  if ((switch_pressed == true) && (active_switch == SWITCH_BED_LIGHT_VALUE) && (!in_settings_mode)
+      && ((switch_sampler_down() & SWITCH_BED_LIGHTS) != 0)){
     sl_sleeptimer_is_timer_running(&handle_5000ms_timer, &is_5000ms_timer_running);
 
     if (!is_5000ms_timer_running){
diff --git a/settings_mode.h b/settings_mode.h
index e9549ac..8f9f4f9 100644
--- a/settings_mode.h
+++ b/settings_mode.h
@@ -38,6 +38,13 @@ void settings_mode_process_outputs(void);
 // during the entry hold (the double-press-check block).
 void settings_mode_cancel_entry(void);
 
+// Abandons any settings-mode hold in progress (UP_ALL/DOWN_ALL sensitivity
+// step, AUTO_LEVEL zero-angle store/erase): stops both settings timers and
+// clears both timeout flags. No NVM3 write; does NOT leave settings mode.
+// Called by the hold-release branches, by settings_mode_exit(), and by
+// app_stop_all() (bed_actions.c) when STOP arrives in settings mode.
+void settings_mode_cancel_holds(void);
+
 // Leaves settings mode: stops both settings timers (no NVM3 write), turns the
 // motors off, stops the amber fast blink and restores amber to the under-bed
 // lights state, turns red/green/blue off, and clears the panel bookkeeping
```

### c.4 Commit D `9ba0ca3` (docs only)

```
commit 9ba0ca3059f2e54d5cde819608e874c11b0434af
Docs cleanup: #19 forward pointer, P3.13 done, sampler design debounce note

 docs/DECISIONS.md             | 2 ++
 docs/REFACTOR_PLAN.md         | 2 ++
 docs/SWITCH_SAMPLER_DESIGN.md | 2 ++
 3 files changed, 6 insertions(+)
```

```diff
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 11b8de1..58246e4 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -272,6 +272,8 @@ to the switch panel is what prevents a stranger within radio range from
 bonding. Up to 8 bonds stored in NVM3. All GATT characteristics require an
 encrypted, bonded link.
 
+Implemented as #42 (2026-10-18).
+
 ## 20. SDK lands on Simplicity SDK 2026.6.0 LTS at D1 — via fresh-project port, not in-place upgrade
 
 The D1 migration (#10) targets **Simplicity SDK 2026.6.0 LTS** (released
diff --git a/docs/REFACTOR_PLAN.md b/docs/REFACTOR_PLAN.md
index b2b2351..f582434 100644
--- a/docs/REFACTOR_PLAN.md
+++ b/docs/REFACTOR_PLAN.md
@@ -205,6 +205,8 @@ filtering chain safety-relevant, not just diagnostics.
 13. **Settings mode has no exit.** `in_settings_mode` is set true
     (app.c:1507) and never set false — the only exit is a power cycle. If
     intentional, it deserves a comment; if not, it's a missing transition.
+    **DONE (2026-10-18, commit `dd8d069`):** a new press of BED LIGHTS exits
+    settings mode (`settings_mode_exit()`).
 
 14. **Multi-bit interrupt masks silently match nothing.** `process_inputs()`
     switches on the raw packed interrupt word; if two switches change in the
diff --git a/docs/SWITCH_SAMPLER_DESIGN.md b/docs/SWITCH_SAMPLER_DESIGN.md
index ba34381..b434d1a 100644
--- a/docs/SWITCH_SAMPLER_DESIGN.md
+++ b/docs/SWITCH_SAMPLER_DESIGN.md
@@ -1,5 +1,7 @@
 # Switch sampler design (one switch reader) - implementation spec
 
+Note (2026-10-18): the debounce is now 2 samples (commit 47db449); the body below is the historical design.
+
 Written by the reviewer on 2026-10-17 for the session that implements it.
 Implement as specified; where the code contradicts this document, stop and
 report it under QUESTIONS FOR REVIEW instead of improvising.
```

### c.5 Proposed CLAUDE.md diff (NOT applied; produced on a scratch copy with `diff -u a/CLAUDE.md b/CLAUDE.md`)

```diff
--- a/CLAUDE.md	2026-10-18 10:45:29.226376200 -0700
+++ b/CLAUDE.md	2026-10-18 10:45:29.256529800 -0700
@@ -52,11 +52,9 @@
   that used to require a hand-edited config file — if you ever find
   yourself wanting to hand-edit a generated file, that is a signal to
   stop and ask, not a shortcut to take.
-- **No BLE glue in this phase.** The radio stays dormant until the
-  migrated firmware passes the full `docs/REGRESSION.md` pass on hardware
-  (DECISIONS #20). Don't add `sl_bt_on_event` handling, GATT logic, or
-  anything beyond what the stock "Bluetooth – SoC Empty" sample already
-  provides, even if it looks like a natural next step.
+- **BLE status.** BLE Stage B (telemetry) and Stage C (app commands drive
+  the bed, DECISIONS #34) are built; the live status and what is still gated
+  on the full `docs/REGRESSION.md` pass is in `docs/MIGRATION_STATUS.md`.
 - **Keep the `bed_` module prefix.** Only cosmetic "Totem"/"Totem
   Sleep"→"Tilt Bed"/"Tilt Bed Systems" string changes are in scope; don't
   rename modules, functions, or the prefix.
```

## d. Raw command output

No secrets appeared in any output, so nothing is redacted. In d.2.3, `$SDKH` is `C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/bluetooth_le_host/inc/sl_bt_api.h` and `$SDKSTATUS` is `.../p/platform_core/platform/common/inc/sl_status.h` (the `SDK_PATH` in `cmake_gcc/tilt_bed_controller.cmake:5`). `[exit N]` is the command's exit status. In d.2.1, `$L` is `prompt_v_bench.log`, run in the `PuTTY Logs` directory.

**What is not pasted verbatim, and why.** Following the earlier handoffs' practice, I did not paste the outputs of my plain source reads (`cat -n settings_mode.c`, `sed -n` ranges of `ble_bed_service.c`, `bed_actions.c`, `app.c`, `switch_input.c`, `exp_ui.c`, `switch_input.h`, `settings_mode.h`, and the docs). Those outputs are the files as they stood at `3f11c12`; the evidence they produced is re-run as targeted greps in d.2.2 on the committed tree. The session's first command printed `docs/prompts/PROMPT_W_...md`, and that output is identical to the file committed in `9798c5a`.

### d.1 Session-start commands (verbatim, except the prompt file's content, which is the committed file)

```
$ cat docs/prompts/PROMPT_W_bonding-policy-reject-disconnect-settings-holds.md && date && git status && git log --oneline -8
[... prompt file content, identical to the committed docs/prompts/PROMPT_W_bonding-policy-reject-disconnect-settings-holds.md ...]
Sun Oct 18 10:32:56 PDT 2026
On branch main
Your branch is up to date with 'origin/main'.

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/prompts/PROMPT_W_bonding-policy-reject-disconnect-settings-holds.md

no changes added to commit (use "git add" and/or "git commit -a")
3f11c12 Handoff 2026-10-18 01: Prompt V (sampler bench result, switch follow-ups, settings exit, pairing window)
944f19e Pairing window: new bonds only while settings mode is active (DECISIONS #42)
dd8d069 Settings mode: exit on a new BED LIGHTS press
47db449 Switch follow-ups: debounce 2 samples, double press releases latch, drop unused include
acd410a Record 2026-10-18 switch sampler bench result (docs only)
3d96128 Handoff 05: one switch reader (sampler) implementation
d1f13d0 One switch reader: sampler, debounce, re-driver refcount, sleep veto (DECISIONS #41)
cb9c7b2 Handoff 04: status check only, no source changes
```

A failed command from exploration (kept as evidence):

```
$ ls *.c *.h && wc -l ble_bed_service.c settings_mode.c settings_mode.h app.c switch_sampler.h
adc_currents.c
adc_currents.h
app.c
app.h
app_bm.c
auto_level.c
auto_level.h
bed_actions.c
bed_actions.h
bed_control.c
bed_control.h
bed_settings.c
bed_settings.h
bed_status.c
bed_status.h
bed_types.h
ble_bed_service.c
ble_bed_service.h
debug_capture.h
dual_motor_control.c
dual_motor_control.h
exp_board.c
exp_board.h
exp_ui.c
exp_ui.h
main.c
mc3479.c
mc3479.h
motor_current_functions.c
motor_current_functions.h
nvm3_functions.c
nvm3_functions.h
pi4ioe5v6416.h
settings_mode.c
settings_mode.h
sl_gatt_service_device_information_override.c
switch_input.c
switch_input.h
version.h
 1141 ble_bed_service.c
  479 settings_mode.c
   48 settings_mode.h
  762 app.c
wc: switch_sampler.h: No such file or directory
 2430 total
[exit 1]
```

(There is no `switch_sampler.h`; the sampler API is in `switch_input.h`.) Another failure: my first `grep` for the reason codes used `.../p/platform/common/inc/sl_status.h`, which does not exist (`grep: ...: No such file or directory`, exit 2). The right path is the one in d.2.3.

### d.2 Evidence commands

#### d.2.1 Bench log (part A)

```
$ ls -la (PuTTY Logs)
total 72
drwxr-xr-x 1 adunc 197610     0 Oct 18 10:06 .
drwxr-xr-x 1 adunc 197610     0 Oct 17 17:32 ..
-rw-r--r-- 1 adunc 197610 12120 Oct 17 18:52 prompt_q_bench.log
-rw-r--r-- 1 adunc 197610  6563 Oct 18 10:02 prompt_v_bench.log
-rw-r--r-- 1 adunc 197610  4825 Oct 17 17:33 provoke_runaway.log
-rw-r--r-- 1 adunc 197610 35108 Oct 18 08:56 sampler_bench.log
$ wc -l $L
193 prompt_v_bench.log
$ grep -cE "^SW: 0x[0-9A-F]{4} (down|up) \(bounces" $L
88
$ grep -cE "^SW: 0x[0-9A-F]{4} (down|up) \(bounces 0\)" $L
88
$ grep -cE "bounces [1-9]" $L
0
$ grep -c "presses ignored" $L
6
$ grep -c "Double Press Error" $L
3
$ grep -ci "FAILSAFE" $L
0
$ grep -c "Unrecognized switch mask" $L
0
$ grep -n "presses ignored\|Double Press Error" $L
38:SW: 2 switches down - presses ignored
43:SW: 2 switches down - presses ignored
48:SW: 2 switches down - presses ignored
49:Double Press Error
55:SW: 2 switches down - presses ignored
60:SW: 2 switches down - presses ignored
61:Double Press Error
67:SW: 2 switches down - presses ignored
68:Double Press Error
$ grep -n "Settings mode" $L
79:Settings mode: exit
86:Settings mode: exit
93:Settings mode: exit
171:Settings mode: exit
$ grep -c "GATT_AUTH\|137" $L
0
$ sed -n 94,182p $L
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x0800 down (bounces 0)
SW: 0x0800 up (bounces 0)
SW: 0x4000 down (bounces 0)
SW: 0x4000 up (bounces 0)
SW: 0x2000 down (bounces 0)
SW: 0x2000 up (bounces 0)
SW: 0x1000 down (bounces 0)
SW: 0x1000 up (bounces 0)
SW: 0x0100 down (bounces 0)
SW: 0x0100 up (bounces 0)
SW: 0x0400 down (bounces 0)
SW: 0x0400 up (bounces 0)
SW: 0x0200 down (bounces 0)
SW: 0x0200 up (bounces 0)
SW: 0x0001 down (bounces 0)
SW: 0x0001 up (bounces 0)
SW: 0x0004 down (bounces 0)
SW: 0x0004 up (bounces 0)
SW: 0x0002 down (bounces 0)
Step 1: ROLL and PITCH check
Step 2: ROLL Not Level
Leveling...
Max Current for Motor 0: 737
Max Current for Motor 1: 761
Max Current for Motor 2: 1207
Max Current for Motor 3: 765
Latch
Pulse off...
ROLL Leveled successfully...
Axis set to PITCH
Leveling...
Max Current for Motor 0: 736
Max Current for Motor 1: 798
Max Current for Motor 2: 1221
Max Current for Motor 3: 758
Latch
PITCH Leveled successfully...
Exit auto-level
SW: 0x0002 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
BLE: connection_opened conn=1 bonding=0x01
BLE: connection_parameters conn=1 interval=24 security_mode=0
BLE: characteristic_status DROPPED conn=1 client_config_flags=0x0001 (matches neither control nor viewer connection)
BLE: connection_parameters conn=1 interval=24 security_mode=1
BLE: cccd_seed conn=1 bed_status=0x0001 sc=0x0000
BLE: connection_parameters conn=1 interval=24 security_mode=1
BLE: connection_closed conn=1 reason=0x1013
BLE: connection_opened conn=2 bonding=0x01
BLE: connection_parameters conn=2 interval=24 security_mode=0
BLE: characteristic_status DROPPED conn=2 client_config_flags=0x0001 (matches neither control nor viewer connection)
BLE: connection_parameters conn=2 interval=24 security_mode=0
BLE SM: confirm_bonding conn=2 (REPLACING existing bond) -> REJECTED (not in settings mode)
BLE SM: bonding_failed conn=2 reason=0x1205
BLE: connection_closed conn=2 reason=0x1013
SW: 0x0008 down (bounces 0)
Max Current for Motor 0: 0
Max Current for Motor 1: 10
Max Current for Motor 2: 2
Max Current for Motor 3: 9
SW: 0x0008 up (bounces 0)
BLE: connection_opened conn=3 bonding=0x01
BLE: connection_parameters conn=3 interval=24 security_mode=0
BLE: characteristic_status DROPPED conn=3 client_config_flags=0x0001 (matches neither control nor viewer connection)
BLE: connection_parameters conn=3 interval=24 security_mode=0
BLE SM: confirm_bonding conn=3 (REPLACING existing bond) -> ACCEPTED (settings mode)
BLE: connection_parameters conn=3 interval=24 security_mode=1
BLE: cccd_seed conn=3 bed_status=0x0001 sc=0x0000
BLE SM: bonded conn=3 bonding=0x01 security_mode=1
SW: 0x0008 down (bounces 0)
Settings mode: exit
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
BLE: connection_closed conn=3 reason=0x1013
BLE: connection_opened conn=4 bonding=0x01
BLE: connection_parameters conn=4 interval=24 security_mode=0
BLE: characteristic_status DROPPED conn=4 client_config_flags=0x0001 (matches neither control nor viewer connection)
BLE: connection_parameters conn=4 interval=24 security_mode=1
BLE: cccd_seed conn=4 bed_status=0x0001 sc=0x0000
BLE: connection_parameters conn=4 interval=24 security_mode=1
Max Current for Motor 0: 3
```

Full log, `cat "$L"` (193 lines, the user's file, CRLF line ends stripped by the terminal):

```
Welcome to the Tilt Bed application
Application Version 1.1.0 
Welcome to the Tilt Bed application
Application Version 1.1.0 
Accelerometer is sampling ...
NVM3 initialized successfully
Loaded Roll Angle: 0
Loaded Pitch Angle: 0
Loaded Raise Collision Sensitivity: 550
Loaded Lower Collision Sensitivity: 275
BLE SM: configure -> 0x0000
BLE SM: store_bonding_configuration -> 0x0000
BLE SM: set_bondable_mode(1) -> 0x0000
SW: 0x8000 down (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x0800 down (bounces 0)
Max Current for Motor 0: 752
Max Current for Motor 1: 16
Max Current for Motor 2: 6
Max Current for Motor 3: 800
SW: 0x0800 up (bounces 0)
SW: 0x0004 down (bounces 0)
Max Current for Motor 0: 688
Max Current for Motor 1: 809
Max Current for Motor 2: 1148
Max Current for Motor 3: 698
SW: 0x0004 up (bounces 0)
SW: 0x4000 down (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 2 switches down - presses ignored
SW: 0x4000 up (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x4000 down (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 2 switches down - presses ignored
SW: 0x4000 up (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 0x4000 down (bounces 0)
SW: 2 switches down - presses ignored
Double Press Error
  down mask 0xC000, 2 switches down
SW: 0x4000 up (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x4000 down (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 2 switches down - presses ignored
SW: 0x4000 up (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x4000 down (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 2 switches down - presses ignored
Double Press Error
  down mask 0xC000, 2 switches down
SW: 0x4000 up (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 0x4000 down (bounces 0)
SW: 2 switches down - presses ignored
Double Press Error
  down mask 0xC000, 2 switches down
SW: 0x4000 up (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x0008 down (bounces 0)
Max Current for Motor 0: 0
Max Current for Motor 1: 0
Max Current for Motor 2: 0
Max Current for Motor 3: 0
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
Settings mode: exit
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
Settings mode: exit
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
Settings mode: exit
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
SW: 0x8000 down (bounces 0)
SW: 0x8000 up (bounces 0)
SW: 0x0800 down (bounces 0)
SW: 0x0800 up (bounces 0)
SW: 0x4000 down (bounces 0)
SW: 0x4000 up (bounces 0)
SW: 0x2000 down (bounces 0)
SW: 0x2000 up (bounces 0)
SW: 0x1000 down (bounces 0)
SW: 0x1000 up (bounces 0)
SW: 0x0100 down (bounces 0)
SW: 0x0100 up (bounces 0)
SW: 0x0400 down (bounces 0)
SW: 0x0400 up (bounces 0)
SW: 0x0200 down (bounces 0)
SW: 0x0200 up (bounces 0)
SW: 0x0001 down (bounces 0)
SW: 0x0001 up (bounces 0)
SW: 0x0004 down (bounces 0)
SW: 0x0004 up (bounces 0)
SW: 0x0002 down (bounces 0)
Step 1: ROLL and PITCH check
Step 2: ROLL Not Level
Leveling...
Max Current for Motor 0: 737
Max Current for Motor 1: 761
Max Current for Motor 2: 1207
Max Current for Motor 3: 765
Latch
Pulse off...
ROLL Leveled successfully...
Axis set to PITCH
Leveling...
Max Current for Motor 0: 736
Max Current for Motor 1: 798
Max Current for Motor 2: 1221
Max Current for Motor 3: 758
Latch
PITCH Leveled successfully...
Exit auto-level
SW: 0x0002 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
BLE: connection_opened conn=1 bonding=0x01
BLE: connection_parameters conn=1 interval=24 security_mode=0
BLE: characteristic_status DROPPED conn=1 client_config_flags=0x0001 (matches neither control nor viewer connection)
BLE: connection_parameters conn=1 interval=24 security_mode=1
BLE: cccd_seed conn=1 bed_status=0x0001 sc=0x0000
BLE: connection_parameters conn=1 interval=24 security_mode=1
BLE: connection_closed conn=1 reason=0x1013
BLE: connection_opened conn=2 bonding=0x01
BLE: connection_parameters conn=2 interval=24 security_mode=0
BLE: characteristic_status DROPPED conn=2 client_config_flags=0x0001 (matches neither control nor viewer connection)
BLE: connection_parameters conn=2 interval=24 security_mode=0
BLE SM: confirm_bonding conn=2 (REPLACING existing bond) -> REJECTED (not in settings mode)
BLE SM: bonding_failed conn=2 reason=0x1205
BLE: connection_closed conn=2 reason=0x1013
SW: 0x0008 down (bounces 0)
Max Current for Motor 0: 0
Max Current for Motor 1: 10
Max Current for Motor 2: 2
Max Current for Motor 3: 9
SW: 0x0008 up (bounces 0)
BLE: connection_opened conn=3 bonding=0x01
BLE: connection_parameters conn=3 interval=24 security_mode=0
BLE: characteristic_status DROPPED conn=3 client_config_flags=0x0001 (matches neither control nor viewer connection)
BLE: connection_parameters conn=3 interval=24 security_mode=0
BLE SM: confirm_bonding conn=3 (REPLACING existing bond) -> ACCEPTED (settings mode)
BLE: connection_parameters conn=3 interval=24 security_mode=1
BLE: cccd_seed conn=3 bed_status=0x0001 sc=0x0000
BLE SM: bonded conn=3 bonding=0x01 security_mode=1
SW: 0x0008 down (bounces 0)
Settings mode: exit
SW: 0x0008 up (bounces 0)
SW: 0x0008 down (bounces 0)
SW: 0x0008 up (bounces 0)
BLE: connection_closed conn=3 reason=0x1013
BLE: connection_opened conn=4 bonding=0x01
BLE: connection_parameters conn=4 interval=24 security_mode=0
BLE: characteristic_status DROPPED conn=4 client_config_flags=0x0001 (matches neither control nor viewer connection)
BLE: connection_parameters conn=4 interval=24 security_mode=1
BLE: cccd_seed conn=4 bed_status=0x0001 sc=0x0000
BLE: connection_parameters conn=4 interval=24 security_mode=1
Max Current for Motor 0: 3
Max Current for Motor 1: 772
Max Current for Motor 2: 1183
Max Current for Motor 3: 18
Max Current for Motor 0: 727
Max Current for Motor 1: 780
Max Current for Motor 2: 1184
Max Current for Motor 3: 716
Max Current for Motor 0: 718
Max Current for Motor 1: 749
Max Current for Motor 2: 1167
Max Current for Motor 3: 709
```

#### d.2.2 Code and doc greps (on the committed tree, `9ba0ca3`)

```
$ grep -rn "app_stop_all" --include=*.c --include=*.h .
./bed_actions.c:208:void app_stop_all(void)
./bed_actions.c:323:// This is what a two-source conflict uses. app_stop_all() clears every panel
./bed_actions.c:327:// output itself was never touched by app_stop_all() - only its indicator -
./bed_actions.c:330:// STOP_ALL still calls app_stop_all() directly and keeps its full behaviour.
./bed_actions.c:343:  // Movement indicators ONLY. This used to call app_stop_all(), whose
./bed_actions.h:35:void app_stop_all(void);
./bed_control.c:73: * app_stop_all() via ARBITRATE_FORCE_STOP, matching DECISIONS.md #12's "any
./ble_bed_service.c:39:#include "bed_actions.h"             // app_stop_all(), the app-path output helpers
./ble_bed_service.c:237:// loop, not in the disconnect callback: app_stop_all() drives LEDs over I2C
./ble_bed_service.c:474:    // app_stop_all() would cut across that sequence.
./ble_bed_service.c:505:  app_stop_all();
./ble_bed_service.c:535:    app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
./ble_bed_service.c:783:    // app_stop_all() drives LEDs over I2C. Blocking I2C in a stack callback
./ble_bed_service.c:883:    app_stop_all();
./settings_mode.h:45:// app_stop_all() (bed_actions.c) when STOP arrives in settings mode.
./test/bed_control_test.c:92:void app_stop_all(void)
[exit 0]

$ grep -n "settings_mode_cancel_holds" *.c *.h
bed_actions.c:227:    settings_mode_cancel_holds();
settings_mode.c:120:void settings_mode_cancel_holds(void)
settings_mode.c:141:  settings_mode_cancel_holds();
settings_mode.c:203:        settings_mode_cancel_holds(); // also clears a timeout that fired just before the release
settings_mode.c:222:        settings_mode_cancel_holds(); // also clears a timeout that fired just before the release
settings_mode.c:239:        settings_mode_cancel_holds(); // also clears a timeout that fired just before the release
settings_mode.h:46:void settings_mode_cancel_holds(void);
[exit 0]

$ grep -n "sl_sleeptimer_stop_timer(&handle_2000ms_timer)\|sl_sleeptimer_stop_timer(&handle_5000ms_timer)\|timeout_2000ms = \|timeout_5000ms = " settings_mode.c
67:static bool timeout_5000ms = false;
69:static bool timeout_2000ms = false;
80:  timeout_5000ms = true;
98:  timeout_2000ms = true;
110:  sl_sleeptimer_stop_timer(&handle_5000ms_timer);
111:  timeout_5000ms = false;
122:  sl_sleeptimer_stop_timer(&handle_2000ms_timer);
123:  timeout_2000ms = false;
124:  sl_sleeptimer_stop_timer(&handle_5000ms_timer);
125:  timeout_5000ms = false;
286:        sl_sleeptimer_stop_timer(&handle_5000ms_timer);
287:        timeout_5000ms = false;
338:            sl_sleeptimer_stop_timer(&handle_2000ms_timer);
339:            timeout_2000ms = false;
384:            timeout_5000ms = false;
385:            sl_sleeptimer_stop_timer(&handle_5000ms_timer);
489:          sl_sleeptimer_stop_timer(&handle_2000ms_timer);
490:          timeout_2000ms = false;
[exit 0]

$ grep -n "store_bonding_configuration\|sl_bt_connection_close\|pairing_refused_\|refused_pairing_conn\|closing conn\|confirm_bonding conn" ble_bed_service.c
35:#include "sl_bluetooth.h"            // SL_BT_CONFIG_MAX_CONNECTIONS_SUM - sizes refused_pairing_conn[]
204:static uint8_t refused_pairing_conn[SL_BT_CONFIG_MAX_CONNECTIONS_SUM];
308:static void pairing_refused_mark(uint8_t connection)
311:    if (refused_pairing_conn[i] == connection) {
316:    if (refused_pairing_conn[i] == SLOT_EMPTY) {
317:      refused_pairing_conn[i] = connection;
326:static bool pairing_refused_is_marked(uint8_t connection)
329:    if (refused_pairing_conn[i] == connection) {
336:static void pairing_refused_clear(uint8_t connection)
339:    if (refused_pairing_conn[i] == connection) {
340:      refused_pairing_conn[i] = SLOT_EMPTY;
938:      // sl_bt_sm_store_bonding_configuration() doc (DECISIONS.md #43). The
944:      sc = sl_bt_sm_store_bonding_configuration(BLE_BED_SERVICE_MAX_BONDS, 2);
946:      printf("BLE SM: store_bonding_configuration -> 0x%04x\r\n", (unsigned)sc);
957:      memset(refused_pairing_conn, SLOT_EMPTY, sizeof(refused_pairing_conn));
984:      pairing_refused_clear(connection); // a new link on a reused handle starts unrefused (#43)
1016:      pairing_refused_clear(evt->data.evt_connection_closed.connection); // #43
1046:    // clean error first. sl_bt_connection_close() only queues the
1056:      if (pairing_refused_is_marked(connection)) {
1058:        printf("BLE: closing conn=%u - pairing refused\r\n", connection);
1060:        sc = sl_bt_connection_close(connection);
1087:      printf("BLE SM: confirm_bonding conn=%u handle=0x%02x (%s) -> %s\r\n",
1095:        pairing_refused_mark(cb->connection);
[exit 0]

$ grep -n "define BED_BENCH_PROVOKE_RUNAWAY\|define BLE_BENCH_WIPE_BONDS\|define SWITCH_SAMPLER_DIAG\|define BLE_DEBUG_SECURITY_LOG\|define SWITCH_DEBOUNCE_SAMPLES" *.c *.h
ble_bed_service.c:53:#define BLE_DEBUG_SECURITY_LOG 1
ble_bed_service.c:70:#define BLE_BENCH_WIPE_BONDS 0
switch_input.c:54:#define SWITCH_DEBOUNCE_SAMPLES     2   // 2 identical reads = >=10 ms stable
switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
switch_input.c:98:#define BED_BENCH_PROVOKE_RUNAWAY 0
[exit 0]

$ grep -n "active_switch = OFF" *.c
app.c:143:active_switch_t active_switch = OFF;
app.c:599:      active_switch = OFF;
bed_actions.c:229:  active_switch = OFF;
bed_actions.c:317:  active_switch = OFF;
bed_actions.c:357:  active_switch = OFF;
settings_mode.c:161:  active_switch = OFF;
settings_mode.c:202:        active_switch = OFF;
settings_mode.c:221:        active_switch = OFF;
settings_mode.c:238:        active_switch = OFF;
settings_mode.c:259:        active_switch = OFF;
switch_input.c:529:              active_switch = OFF;
switch_input.c:541:              active_switch = OFF;
switch_input.c:553:              active_switch = OFF;
switch_input.c:568:              active_switch = OFF;
switch_input.c:582:                    active_switch = OFF;
switch_input.c:847:  active_switch = OFF;
switch_input.c:966:          active_switch = OFF;
[exit 0]

$ grep -n "in_settings_mode" switch_input.c app.c
switch_input.c:517:  if((!any_collision) && (!in_settings_mode)){
switch_input.c:592:  else if (in_settings_mode){
switch_input.c:600:  if (!in_settings_mode){
switch_input.c:717:   else if (in_settings_mode){
switch_input.c:806:  if (auto_level_active || in_settings_mode || ble_bed_service_app_motion_active()) {
switch_input.c:921:  if ((active_switch != OFF) && !any_collision && (!auto_level_active) && (!in_settings_mode)){
app.c:111:// Settings Mode variables moved to settings_mode.c (in_settings_mode,
app.c:744:                      auto_level_active || in_settings_mode);
[exit 0]

$ grep -n "settings_mode_tick();\|switch_input_tick();\|motion_failsafe_tick();\|ble_bed_service_tick();" app.c
708:  settings_mode_tick();
715:  switch_input_tick();
732:  motion_failsafe_tick();
747:  ble_bed_service_tick();
[exit 0]

$ grep -n "define SL_BT_CONFIG_MAX_CONNECTIONS\b\|define SL_BT_CONFIG_MAX_CONNECTIONS \|MAX_CONNECTIONS_SUM\|define SL_BT_COMPONENT_CONNECTIONS" config/sl_bluetooth_config.h config/sl_bluetooth_connection_config.h autogen/sl_bluetooth.h
config/sl_bluetooth_config.h:44:#define SL_BT_CONFIG_MAX_CONNECTIONS (0)
config/sl_bluetooth_config.h:48:#define SL_BT_CONFIG_MAX_CONNECTIONS_SUM     (SL_BT_CONFIG_MAX_CONNECTIONS + SL_BT_COMPONENT_CONNECTIONS)
config/sl_bluetooth_connection_config.h:38:#define SL_BT_CONFIG_MAX_CONNECTIONS     (4)
autogen/sl_bluetooth.h:41:#define SL_BT_COMPONENT_CONNECTIONS 0
[exit 0]

$ grep -n "include" autogen/sl_bluetooth.h
34:#include <stdbool.h>
35:#include "sl_power_manager.h"
36:#include "sl_bluetooth_config.h"
37:#include "sl_bt_stack_init.h"
38:#include "sl_bt_api.h"
[exit 0]

$ grep -n "bluetooth_feature_builtin_bonding_database\|external_bonding" tilt_bed_controller.slcp
182:  id: bluetooth_feature_builtin_bonding_database
[exit 0]

$ grep -n "void fast_blink_LED" -A13 app.c
329:void fast_blink_LED(led_color_t LED_color){
330-
331-  sl_sleeptimer_is_timer_running(&LED_200ms_timer, &is_LED_timer_running);
332-
333-  if (!is_LED_timer_running){
334-    start_200ms_LED_timer();
335-    set_LED_on(LED_color);
336-    single_LED_on[LED_color] = true;
337-  }
338-  if (LED_timer_timeout){
339-    toggle_single_LED(LED_color);
340-    LED_timer_timeout = false;
341-  }
342-}
[exit 0]

$ grep -n "static void start_200ms_LED_timer" -A4 app.c
193:static void start_200ms_LED_timer()
194-{
195-  uint32_t ticks = sl_sleeptimer_ms_to_tick(200);
196-  sl_sleeptimer_start_periodic_timer(&LED_200ms_timer, ticks, LED_timer_callback, NULL, 0, 0);
197-}
[exit 0]

$ grep -n "void set_all_LED_off" -A8 app.c
260:void set_all_LED_off(){
261-
262-  // Set all LEDs on to start
263-  for (uint8_t i = 0; i < NUM_LEDS; i++){
264-    set_LED_off(i);
265-    all_LEDs_on = false;
266-  }
267-}
268-
[exit 0]

$ grep -n "uint16_t switch_sampler_down" -A3 switch_input.c; grep -n "define SWITCH_BED_LIGHTS \|define SWITCH_INPUT_MASK" app.h switch_input.c
457:uint16_t switch_sampler_down(void)
458-{
459-  return deb_down;
460-}
app.h:118:#define SWITCH_BED_LIGHTS  0x0008
switch_input.c:63:#define SWITCH_INPUT_MASK 0xFF0F
[exit 0]

$ grep -n "printf(\"Max Current for Motor" motor_current_functions.c
198:          printf("Max Current for Motor %d: %d\r\n", i, dynamic_max_average_calibration_current[i]);
[exit 0]

$ grep -n "Stage C" docs/MIGRATION_STATUS.md
35:      that have since been found and fixed (DECISIONS #26/#27). Stage C
51:- [ ] **Stage C** — Command characteristic → motors + dead-man watchdog
70:  minus Stage C's command path) is implemented and bench-verified; Stage C
100:5. **Regression before Stage C.** DECISIONS #20's original rule (no BLE glue
102:   path: Stage C is hard-gated on Phase 6, per "BLE status" above. Stage B
[exit 0]

$ grep -n "^## 34\." docs/DECISIONS.md
991:## 34. Stage C — the app drives the bed: all six opcodes, dead-man watchdog, arbitration
[exit 0]

$ grep -n -i "pair\|bond" docs/REGRESSION.md
9:the full pass with a phone connected, bonded, and subscribed to Bed Status
60:For each row: press-and-hold → correct motor pair runs in correct direction
61:and the LED lights; release → motors stop, LED off. Watch the motor pair
133:      `SW: 0x8000 up`, ~300 ms later `PROVOKE: energised HEAD pair with
137:      `ALL MOTORS`; N should be close to the HEAD-pair figure. Repeat several
150:| 3b.2 / 3b.3 | 67 two-button episodes across 30 pairs: all printed `presses ignored`; 57 also printed `Double Press Error` (one press had been delivered first, brief start, then stop); 10 moved nothing; every release came through |
208:      driven axis, leveling stops that pair and does not fight the limit
242:This section has no pairing-window checklist item yet; the pairing rows
248:| Pairing, bonded phone reconnects | no `confirm_bonding` line, reached `security_mode=1` (before and after a settings-mode session) |
249:| Pairing refused (phone bond deleted, not in settings mode) | `-> REJECTED (not in settings mode)`, `bonding_failed reason=0x1205`, then the phone closed the link (`connection_closed reason=0x1013`); phone showed `GATT_AUTH_FAIL` (status 137) |
250:| Pairing in settings mode | `-> ACCEPTED (settings mode)`, then `bonded` |
[exit 0]

$ grep -n "define SWITCH_UP_HEAD\|define SWITCH_UP_ALL \|define SWITCH_AUTO_LEVEL \|define SWITCH_DOWN_ALL " app.h switch_input.c
app.h:115:#define SWITCH_UP_ALL      0x0001
app.h:116:#define SWITCH_AUTO_LEVEL  0x0002
app.h:117:#define SWITCH_DOWN_ALL    0x0004
switch_input.c:49:#define SWITCH_UP_HEAD     0x8000
[exit 0]

$ grep -n "naturally refuses" ble_bed_service.c
272:  // naturally refuses to start once both role slots are occupied.
[exit 0]

$ (cd "../PuTTY Logs" && grep -ho "connection_opened conn=[0-9]*" *.log | sort | uniq -c)
      4 connection_opened conn=1
      1 connection_opened conn=2
      1 connection_opened conn=3
      1 connection_opened conn=4
[exit 0]

$ sed -n 12,16p test/bed_control_test.c
 * timer state. Those symbols are ALSO defined for real in bed_actions.c
 * (C4 relocated them out of app.c - see DECISIONS.md #12). This file
 * must NEVER be part of the embedded (Simplicity Studio / arm-none-eabi)
 * build - linking it alongside bed_actions.c would be a duplicate-symbol
 * error, and it isn't meant to run on target anyway. Build and run on the
[exit 0]
```

#### d.2.3 SDK headers (SiSDK 2026.6.0 install used by the build)

```
$ sed -n 12054,12086p "$SDKH"
/***************************************************************************//**
 *
 * Set the maximum allowed bonding count and bonding policy. The maximum number
 * of bondings that can be supported depends on how much user data is stored in
 * the NVM3 and the NVM3 size. When bond policy value 1 or 2 is selected, the
 * stack will automatically write the new bond, as per the policy, only if the
 * maximum allowed bonding count has been reached. If the stack can't write a
 * new bond for any other reason (e.g., NVM3 is full), an error will be thrown
 * through the bonding_failed event indicating why the bonding was not written.
 * The application has to manually release space from the NVM (e.g., by deleting
 * one of the existing bonds or application data) so that a new bond can be
 * saved. The default value is 13.
 *
 * @param[in] max_bonding_count Maximum allowed bonding count.
 *     - <b>Range:</b> 1 to 32
 * @param[in] policy_flags @parblock
 *   Bonding policy. Values:
 *     - <b>0:</b> If database is full, pairing will be performed but no bonding
 *       is stored
 *     - <b>1:</b> New bonding will overwrite the oldest existing bonding
 *     - <b>2:</b> New bonding will overwrite the bonding that was used the
 *       longest time ago
 *
 *   Default: 0
 *
 *   With external bonding database the parameter is ignored.
 *   @endparblock
 *
 * @return SL_STATUS_OK if successful. Error code otherwise.
 *
 ******************************************************************************/
sl_status_t sl_bt_sm_store_bonding_configuration(uint8_t max_bonding_count,
                                                 uint8_t policy_flags);
[exit 0]

$ sed -n 8493,8515p "$SDKH"
/***************************************************************************//**
 *
 * Close a Bluetooth connection gracefully by performing the ACL Termination
 * procedure or cancel an ongoing connection establishment process. The
 * parameter is a connection handle which is reported in @ref
 * sl_bt_evt_connection_opened event or @ref sl_bt_connection_open command
 * response.
 *
 * Disconnecting a connection is an asynchronous operation. The disconnection is
 * completed when a @ref sl_bt_evt_connection_closed event for the given
 * connection handle is received. To open a new connection to the same remote
 * device, wait for the @ref sl_bt_evt_connection_closed event and then initiate
 * the connection.
 *
 * @param[in] connection Handle of the connection to be closed
 *
 * @return SL_STATUS_OK if successful. Error code otherwise.
 *
 * @b Events
 *   - @ref sl_bt_evt_connection_closed
 *
 ******************************************************************************/
sl_status_t sl_bt_connection_close(uint8_t connection);
[exit 0]

$ grep -n "sl_status_t sl_bt_connection_close\|sl_status_t sl_bt_connection_forcefully_close" "$SDKH"
8515:sl_status_t sl_bt_connection_close(uint8_t connection);
8537:sl_status_t sl_bt_connection_forcefully_close(uint8_t connection);
[exit 0]

$ sed -n 11943,11957p "$SDKH"
PACKSTRUCT( struct sl_bt_evt_sm_confirm_bonding_s
{
  uint8_t connection;     /**< Connection handle */
  uint8_t bonding_handle; /**< Bonding handle for the request.
                                 - <b>Range:</b> 0 to 31

                                 - 0xFF: SL_BT_INVALID_BONDING_HANDLE
                                 - NOTE! When the bonding handle is anything
                                   other than SL_BT_INVALID_BONDING_HANDLE
                                   (0xff), a bonding already exists for this
                                   connection. Overwriting the existing bonding
                                   is a potential security risk. */
});

typedef struct sl_bt_evt_sm_confirm_bonding_s sl_bt_evt_sm_confirm_bonding_t;
[exit 0]

$ grep -n "define SL_BT_INVALID_BONDING_HANDLE" "$SDKH"
79:#define SL_BT_INVALID_BONDING_HANDLE ((uint8_t) 0xFF)
[exit 0]

$ grep -n -i "bonding_policy\|policy_flags\|SL_BT_SM_.*POLICY" "$SDKH"
12069: * @param[in] policy_flags @parblock
12086:                                                 uint8_t policy_flags);
[exit 0]

$ grep -n "REMOTE_USER_TERMINATED \|TERMINATED_BY_LOCAL_HOST \|SMP_PAIRING_NOT_SUPPORTED " "$SDKSTATUS"
271:#define SL_STATUS_BT_CTRL_REMOTE_USER_TERMINATED                                                         ((sl_status_t)0x1013)     ///< User on the remote device terminated the connection.
274:#define SL_STATUS_BT_CTRL_CONNECTION_TERMINATED_BY_LOCAL_HOST                                            ((sl_status_t)0x1016)     ///< Local device terminated the connection.
360:#define SL_STATUS_BT_SMP_PAIRING_NOT_SUPPORTED                                                           ((sl_status_t)0x1205)      ///< Pairing is not supported by the device
[exit 0]
```

### d.3 Builds (verbatim)

All builds used a helper script in the session scratchpad, the same method as the previous handoff. It puts the Silicon Labs ninja and cmake directories on `PATH` (`.../conan/p/ninja1a38fc85adcf7/p` and `.../conan/p/cmakefa35ab0687064/p/bin`), runs `cmake --build --preset default_config` in `cmake_gcc/`, writes all output plus `exit N` to a file, prints it, then prints `grep -n -i 'warning\|error'` over it. An empty grep means none were found.

Baseline, before any edit (HEAD `3f11c12`):

```
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
```

After commit A `9798c5a` (docs only):

```
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
```

After the part B edits (before commit `c6ee6b0`; the DECISIONS/BLE_GATT_DESIGN edits came after this build and are docs only):

```
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

After the part C edits (before commit `840d7b8`):

```
[1/6] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_status.c.obj
[2/6] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[3/6] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/settings_mode.c.obj
[4/6] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[5/6] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[6/6] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

After commit D `9ba0ca3` (docs only; this is the image currently in `cmake_gcc/build/`, built from the code of `840d7b8`):

```
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
```

No build produced a warning or an error.

### d.4 Git and tool output

Commit A (the CRLF lines are git's line-ending warnings, printed on `git add`; `git commit -q` prints nothing):

```
$ git add docs/DECISIONS.md docs/REGRESSION.md docs/prompts/PROMPT_W_bonding-policy-reject-disconnect-settings-holds.md && git status --short && git commit -q -F - <<'EOF' ... && git log --oneline -1
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/prompts/PROMPT_W_bonding-policy-reject-disconnect-settings-holds.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  docs/DECISIONS.md
M  docs/REGRESSION.md
A  docs/prompts/PROMPT_W_bonding-policy-reject-disconnect-settings-holds.md
9798c5a Record 2026-10-18 Prompt V bench result (docs only)
```

Commit B:

```
$ git add docs/DECISIONS.md docs/BLE_GATT_DESIGN.md ble_bed_service.c && git commit -q -F - <<'EOF' ... && git log --oneline -1
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
c6ee6b0 Bonding: overwrite least recently used bond when full; close refused pairings (DECISIONS #43)
```

Commit C:

```
$ git add settings_mode.c settings_mode.h bed_actions.c && git commit -q -F - <<'EOF' ... && git log --oneline -1
warning: in the working copy of 'bed_actions.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'settings_mode.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'settings_mode.h', LF will be replaced by CRLF the next time Git touches it
840d7b8 Settings mode: STOP cancels holds, releases clear stale timeouts, entry needs BED LIGHTS down
```

The refused CLAUDE.md edit (the Edit tool's error, verbatim apart from the advice text that followed it):

```
Permission for this action was denied by the Claude Code auto mode classifier. Reason: [Self-Modification].
```

Commit D (then the build in d.3, then `git status`):

```
$ git status --short && git add docs/DECISIONS.md docs/REFACTOR_PLAN.md docs/SWITCH_SAMPLER_DESIGN.md && git commit -q -F - <<'EOF' ... && git log --oneline -1
 M .projectlinkstore
 M docs/DECISIONS.md
 M docs/REFACTOR_PLAN.md
 M docs/SWITCH_SAMPLER_DESIGN.md
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REFACTOR_PLAN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/SWITCH_SAMPLER_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
9ba0ca3 Docs cleanup: #19 forward pointer, P3.13 done, sampler design debounce note
$ git status
On branch main
Your branch is ahead of 'origin/main' by 4 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
```

Before writing this file:

```
$ date && git log --date=short --format='%h %ad %s' -6 && ls docs/handoffs | grep 2026-10-18 && git status && git rev-parse HEAD origin/main
Sun Oct 18 10:45:18 PDT 2026
9ba0ca3 2026-10-18 Docs cleanup: #19 forward pointer, P3.13 done, sampler design debounce note
840d7b8 2026-10-18 Settings mode: STOP cancels holds, releases clear stale timeouts, entry needs BED LIGHTS down
c6ee6b0 2026-10-18 Bonding: overwrite least recently used bond when full; close refused pairings (DECISIONS #43)
9798c5a 2026-10-18 Record 2026-10-18 Prompt V bench result (docs only)
3f11c12 2026-10-18 Handoff 2026-10-18 01: Prompt V (sampler bench result, switch follow-ups, settings exit, pairing window)
944f19e 2026-10-18 Pairing window: new bonds only while settings mode is active (DECISIONS #42)
fw_HANDOFF_2026-10-18_01_switch-followups-settings-exit-pairing-window.md
On branch main
Your branch is ahead of 'origin/main' by 4 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
9ba0ca3059f2e54d5cde819608e874c11b0434af
3f11c1299e3e5fcea33290a247fc3cea71c6adf9
```

The commit of this file and the push come after it is written, so their output is not in it. If the push fails, its raw error is added in a follow-up commit.

## e. Claims: VERIFIED vs ASSERTED

### VERIFIED (evidence attached)

- **The Prompt V bench result as recorded in part A** (user's bench run, re-counted from the raw log): 88 accepted edges, all `(bounces 0)`; 6 `presses ignored` and 3 `Double Press Error`; 0 FAILSAFE, 0 `Unrecognized switch mask`; `Settings mode: exit` at lines 79, 86, 93, 171; pairing (a)-(d) at lines 142-148, 149-155, 162-169, 171-181. Evidence: d.2.1 (counts, `grep -n`, the full log).
- **The phone's GATT_AUTH_FAIL message is user-reported, not in the log**: `grep -c 'GATT_AUTH\|137'` = 0 (d.2.1).
- **Policy value 2 = "overwrite the bonding that was used the longest time ago"**: `sl_bt_api.h:12074-12075` (d.2.3). No named constant exists (d.2.3 grep). Builtin bonding database in use: `tilt_bed_controller.slcp:182` (d.2.2).
- **`sl_bt_connection_close(uint8_t connection)` is the graceful, asynchronous close in this SDK**: prototype `sl_bt_api.h:8515`, doc 8495-8513 (d.2.3).
- **The stack's connection count is 4**: `config/sl_bluetooth_config.h:48`, `config/sl_bluetooth_connection_config.h:38`, `autogen/sl_bluetooth.h:41` (d.2.2).
- **`bonding_handle` is `uint8_t`, so `%02x` is correct**: `sl_bt_api.h:11946`, `:79` (d.2.3).
- **0x1013 / 0x1016 / 0x1205 meanings**: `sl_status.h:271,274,360` (d.2.3).
- **`app_stop_all()` has three callers, not one**: `ble_bed_service.c:505, 535, 883` (d.2.2).
- **The double-press block, the fail-safe and normal-mode `process_outputs()` cannot run in settings mode**: `switch_input.c:921`, `:806`, `:517` (d.2.2).
- **Tick order is settings -> switch edges -> BLE**: `app.c:708, 715, 747` (d.2.2).
- **The settings blink timer is periodic and `set_all_LED_off()` does not stop it**: `app.c:193-197`, `:260-267`, `:329-342` (d.2.2).
- **`switch_sampler_down()` returns the debounced mask; BED LIGHTS is 0x0008**: `switch_input.c:457-460`, `app.h:118`, `switch_input.c:63` (d.2.2).
- **Each commit's build**: exit 0, with the warning/error grep empty, after A (no work), B (ble_bed_service.c rebuilt), C (5 objects rebuilt) and D (no work) (d.3).
- **`BED_BENCH_PROVOKE_RUNAWAY` 0 and `BLE_BENCH_WIPE_BONDS` 0 in the committed tree**: `switch_input.c:98`, `ble_bed_service.c:70` (d.2.2).
- **The CLAUDE.md edit was refused by the permission system** (the tool's error text is in d.4).
- **`docs/MIGRATION_STATUS.md` still says Stage C is not started**: lines 51, 70, 100-102 (d.2.2); DECISIONS #34 at `docs/DECISIONS.md:991` says it is built.

### ASSERTED (reasoned, builds, but nothing observed it)

- That policy 2 is accepted by the stack at boot (`store_bonding_configuration -> 0x0000`) and that a 9th bond evicts the least recently used one. Not run; eviction needs a full store.
- That a refused phone is now closed by the bed (`closing conn` line, `connection_closed reason=0x1016`), and that it still gets a clean pairing error first. Not run.
- That bonding_failed always follows a rejection on the same connection (the Prompt V log shows it once; the code depends on it for the close).
- That connection handles may not run 1..4 (why the table stores handles). Not checked against SDK documentation, which does not say either way.
- That STOP in settings mode now leaves nothing stored and the next hold needs the full 2 s; that amber is dark for at most ~400 ms and then blinks. Traced in code, not run.
- That C3's release-branch change is equivalent apart from clearing the stale flags. Reasoned branch by branch in b.3, not run. The race itself has never been observed on the bench.
- That C4 does not stop a normal 5 s BED LIGHTS hold from entering settings mode. Reasoned from the sampler delivering edges from `deb_down`, not run.
- That the watchdog-expiry and control-disconnect paths into `app_stop_all()` are practically unreachable in settings mode (b.3 table).
- That the near-zero `Max Current` lines during the BED LIGHTS entry hold come from `run_motor_supervision()` on `active_switch == SWITCH_BED_LIGHT_VALUE` (b.1). Not traced to the print.
- All expected PuTTY lines in b.5.

## f. QUESTIONS FOR REVIEW

1. **D2 (CLAUDE.md) was not applied.** The permission system refused it as self-modification; the proposed diff is in b.4 for the user to apply by hand. Before applying it: the replacement text points to `docs/MIGRATION_STATUS.md` for "the live status", but that file still says Stage C is "Not started" (lines 51, 70, 100-102), contradicting DECISIONS #34. Should MIGRATION_STATUS.md's BLE status be corrected first (or in the same change), and by whom?
2. **C2 premise was false.** `app_stop_all()` is also called on watchdog expiry (`ble_bed_service.c:505`) and on control-phone disconnect while APP held the bed (`:883`), not only on STOP_ALL (`:535`). I implemented the call inside `app_stop_all()` as specified, judging the two other paths practically unreachable in settings mode and harmless if reached (b.3). Keep it there, or move it to the STOP_ALL site only?
3. **REGRESSION.md has no pairing-window checklist item.** I recorded the pairing result in a table under §7 and said so. Should §7 (or a new BLE section) gain explicit items: bonded reconnect with no confirm_bonding; refusal outside settings mode, now including `closing conn` / 0x1016; acceptance in settings mode; and the new "STOP during a settings hold" check from E2?
4. **B2 data structure.** The prompt said "per-connection flag sized for the stack's connection count". I used a 4-entry table *of handles*, not a bool array *indexed by* handle, so the code does not assume handles run 1..4 (b.2). Acceptable?
5. **The `closing conn` print is under `BLE_DEBUG_SECURITY_LOG`** (currently 1), like every other security print; the close itself is not gated. Should that print be unconditional?
6. **C4 does not check `switch_state_fresh()`.** If the sampler has had no good read for `SWITCH_STATE_STALE_MS`, `deb_down` may still show BED LIGHTS down. The fail-safe treats stale as "no switch down"; the entry test now does not. Add it, or leave it as specified?
7. **Max Current lines during the BED LIGHTS entry hold** (b.1): `run_motor_supervision()` appears to run, calibrating current thresholds, while `active_switch == SWITCH_BED_LIGHT_VALUE`, with no motor energised. This is pre-existing and was not changed. Worth a look?
8. **Two role slots vs four stack connections.** The stack allows 4 connections (`SL_BT_CONFIG_MAX_CONNECTIONS 4`), but `open_connections[]` tracks 2 and `track_connection_opened()` silently ignores a third. The comment at `start_advertising()` says advertising refuses to start once both role slots are full, which is not obviously true with a stack limit of 4. Not touched; out of scope.
9. **User decision 3 (STOP in settings mode cancels the hold and stays) is not in DECISIONS.md.** The prompt asked for a DECISIONS entry only for B1/B2. Should C2 get one (or a line under #43)?
10. **"Least recently used" vs "oldest".** The prompt's section E says "B1 (overwrite oldest bond)", but B1 specifies policy 2 ("used the longest time ago"). Policy 1 is "oldest existing". I implemented 2 as specified.

## g. NOT DONE

- **D2, CLAUDE.md "No BLE glue" replacement.** The permission system refused the edit (self-modification). The diff is shown in b.4 for the user to apply. See QUESTIONS 1 for a problem with the target text.
- **No bench test of anything in B or C.** I do not flash or bench-test (CLAUDE.md). The steps are in b.5.
- **B1 eviction check.** It cannot be done with one or two phones without code changes (b.5).
- **Host unit test (`test/bed_control_test.c`) not run.** It does not link the changed files (`bed_actions.c`, `settings_mode.c`, `ble_bed_service.c`), and the prompt did not ask for it.
