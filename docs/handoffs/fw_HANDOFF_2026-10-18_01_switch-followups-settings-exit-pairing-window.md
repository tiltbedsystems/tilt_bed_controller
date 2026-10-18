# Handoff 2026-10-18 #01 — Prompt V: sampler bench result, switch follow-ups, settings-mode exit, pairing window

## a. Header

- Repo: `tilt_bed_controller`
- Branch: `main`
- Date: 2026-10-18 (from `date` at session start: `Sun Oct 18 09:16:24 PDT 2026`).
- HEAD before the session: `3d96128` (Handoff 05: one switch reader (sampler) implementation), level with `origin/main`; `.projectlinkstore` modified; `docs/prompts/` untracked. **This matches the prompt's "Expected repo state" exactly.**
- HEAD after the work commits: `944f19e`. Four work commits, in the order the prompt asked for:

| Part | Commit | Subject |
|---|---|---|
| A | `acd410a` | Record 2026-10-18 switch sampler bench result (docs only) |
| B | `47db449` | Switch follow-ups: debounce 2 samples, double press releases latch, drop unused include |
| C | `dd8d069` | Settings mode: exit on a new BED LIGHTS press |
| D | `944f19e` | Pairing window: new bonds only while settings mode is active (DECISIONS #42) |

- This handoff and the `docs/SESSION_LOG.md` entry are committed together after `944f19e`, in a docs-only commit whose SHA is not known while this file is being written. Then `git push` (normal, never forced). The push happens after this file is committed; if it fails, its raw error is added to this file in a follow-up commit (CLAUDE.md), and no merge/rebase/reset is attempted.
- The prompt file `docs/prompts/PROMPT_V_switch-followups-and-pairing-window.md` was committed in commit A (the first commit), unmodified. See QUESTIONS 9.
- `.projectlinkstore` is modified and was deliberately NOT committed (prompt: "never commit it").
- `BED_BENCH_PROVOKE_RUNAWAY` is 0 (`switch_input.c:98`) and `BLE_BENCH_WIPE_BONDS` is 0 (`ble_bed_service.c:69`) in the committed tree. `SWITCH_SAMPLER_DIAG` is still 1 (`switch_input.c:57`) and `BLE_DEBUG_SECURITY_LOG` is 1 (`ble_bed_service.c:52`).

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

A note on CLAUDE.md's "No BLE glue in this phase": `docs/MIGRATION_STATUS.md` and DECISIONS #34 show BLE Stage B and Stage C are already built, so that line is stale; part D was done because the user's prompt orders it. See QUESTIONS 12.

## b. Designs, analysis and answers produced this session

Each part is written out in full here. A reviewer should not need the prompt, DECISIONS.md or the code to follow it; `file:line` references are to the committed tree at `944f19e` unless stated.

### b.1 Part A — what the bench log actually says

Source: `C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\sampler_bench.log`, 1155 lines, 35108 bytes, modified Oct 18 08:56. I read it with grep/sed and replayed it with a small script (below). Every figure the prompt asked me to confirm was confirmed. One point of wording differs from the prompt; nothing contradicts it.

| Prompt's figure | What the log shows | Evidence (d.2.1) |
|---|---|---|
| HEAD pair caught after 209, 248, 209, 209, 209 ms | same, lines 1087, 1092, 1097, 1102, 1107 | `grep PROVOKE\|FAILSAFE` |
| ALL MOTORS 209, 210, 210, 217, 249 ms | same, lines 1112, 1117, 1122, 1127, 1132 | same grep |
| No FAILSAFE except directly after PROVOKE | 10 `FAILSAFE:` lines, each on the line right after a `PROVOKE: energised` line | same grep |
| No "Unrecognized switch mask", no "read failed" | `grep -c -i "Unrecognized\|failed"` = 0 | |
| ~70 two-button presses: each gave "presses ignored" or "Double Press Error"; every release clean | **67 episodes over 30 different pairs. All 67 printed `presses ignored`; 57 of them ALSO printed `Double Press Error`; 10 printed only `presses ignored`.** No episode ended with a switch still down; every `down` has its `up`. | replay script |
| ~700 edges: 696 with 0 bounces, 4 with 1 | 350 down + 350 up = 700; 696 `(bounces 0)`, 4 `(bounces 1)` at lines 198 (0x1000), 272, 282, 286 (0x0004), all on a press | grep / uniq |
| Power-up with UP HEAD held: no motion; release printed "SW: 0x8000 up"; next press worked | line 1041 `SW: 0x8000 down`, reset at 1046 with it still down, boot lines, line 1057 `SW: 0x8000 up (bounces 0)` with no `Max Current` lines between boot and it, line 1058 next `down` followed by `Max Current` lines (it moved) | `sed -n 1040,1062p` |

**The wording point.** The prompt says each two-button press gave "presses ignored" *or* "Double Press Error". In the log the 57 "Double Press Error" episodes printed **both**: one button's press was delivered first (the sampler accepted it on an earlier sample than the other), the bed started, then the second button's press was accepted with two down, printed `presses ignored`, and on the next pass the double-press check printed `Double Press Error` and stopped it. That is the designed behaviour (05 handoff b.12 item 3), so I recorded it exactly rather than as the prompt phrased it.

**Replay script** (run with `python -I`, output in d.2.1). It walks the log, keeps a down-mask from the `SW: 0xNNNN down|up` lines, resets it on each `Welcome` line, opens an "episode" when two or more bits are down and closes it when the mask returns to 0, counts `presses ignored` and `Double Press Error` lines inside each episode, and flags any line that does not fit (a down for a bit already down, an up for a bit not down, a reset with something down, either message outside an episode). The only flags were the reset with 0x8000 down (line 1046) and the matching up-without-down (line 1057), which are the power-up test.

**Where it was recorded (commit `acd410a`).**
- DECISIONS #41 gains a dated sub-section "Bench result (2026-10-18)" holding the figures above, marked VERIFIED (user's bench run), with the log path. It says the ALL-motors result is *consistent with* the sleep-veto explanation, not proof that it was the only cause.
- REGRESSION.md gains section **3b "Switch sampler (DECISIONS #41)"** after section 3: the seven checklist items from `fw_HANDOFF_2026-10-17_05` b.12 (reworded into checklist form, same content), then a "Recorded result, 2026-10-18" table. Items 3b.4 (BED LIGHTS 5 s) and 3b.7 (idle) are marked "not separately recorded" because the log has no settings-mode entry and idle cannot be told from it. A "3b Switch sampler" row is added to the Result table. Boxes are left unchecked, as the file's own preamble requires.
- The SESSION_LOG entry for this session records it as VERIFIED.

### b.2 Part B — switch follow-ups (commit `47db449`)

**B1. `SWITCH_DEBOUNCE_SAMPLES` 3 -> 2** (`switch_input.c:54`). How the debounce works, so the effect can be checked: every 10 ms the sampler reads both boards; for each bit, if the raw reading differs from the debounced state, `pending_count[b]` is incremented, and when it reaches `SWITCH_DEBOUNCE_SAMPLES` the debounced bit flips and the edge is queued; if a reading agrees with the debounced state while `pending_count[b] > 0`, that was a bounce: `pending_count[b]` is reset and `bounce_count[b]` incremented (`switch_input.c:405-430`).

- With 2: the first sample that sees the new level arrives 0-10 ms after the contact changes, the second 10 ms later, so the edge is accepted 10-20 ms after the change (was 20-30 ms with 3). That is the "~10 ms sooner" stop.
- A bounce that shows in one sample only is still rejected: count goes to 1, the next sample agrees with the old state, count resets.
- What 2 does NOT reject: a bounce that shows the wrong level in two consecutive samples (i.e. lasting >10 ms). The bench data had none: 4 of 700 edges had a 1-sample bounce, none longer.
- Nothing else uses the constant: the session-end test only looks at `pending_count[b] != 0`, the fail-safe uses `switch_sampler_down()` plus its own 50 ms x 3 confirm, and the read-failure limit is a separate constant.
- Text updated to match: the constant's comment (`2 identical reads = >=10 ms stable`); DECISIONS #41 "Sampling session" bullet (now "2 identical reads; 3 until 2026-10-18") and "Behaviour changes" bullet (now "about 10-20 ms ... (20-30 ms with the original 3-sample debounce, until 2026-10-18)"); REGRESSION 3b.1 (`~10-20 ms`). A grep for `20-30|3 identical|>=20 ms` over `.c/.h`, DECISIONS and REGRESSION now finds only the historical "(20-30 ms with the original ...)" phrase (d.2.2). Left alone on purpose: DECISIONS #41's new "Bench result" note and REGRESSION 3b's "Recorded result" header both say "debounce 3 samples", which is correct for that run; `docs/SWITCH_SAMPLER_DESIGN.md` (a design record) and the 05 handoff (append-only evidence) still say 3.

**B2. Double Press Error releases the switches' half of the #36 latch** (`switch_input.c:965`). The inserted line and comment, after `clear_switch_panel_state()` and before `active_switch = OFF`, exactly as the prompt specified:

```c
          clear_switch_panel_state();
          // Release the switches' half of the #36 latch, for the same reason
          // as in motion_failsafe_tick(): clearing switch_held above means the
          // held buttons' later releases find no match and never reach
          // bed_move_stop(), so without this the switches' half of the latch
          // could stay set until some later press/release.
          bed_note_source_released(BED_SOURCE_SWITCHES);
          active_switch = OFF;
```

Why it is needed, traced through the code: the first press reached `bed_move_start(BED_SOURCE_SWITCHES, ...)`, which calls `note_commanding(SWITCHES, true)` (`bed_control.c:95-97`). The Double Press Error block then sets `switch_held = OFF`. When the two buttons are released, `process_inputs()` only calls `bed_move_stop()` (the thing that sets commanding false) when `switch_held` matches the released button (`switch_input.c:691-693`), so neither release does. `source_commanding[SWITCHES]` stays true. That only matters if `conflict_inhibit` is set (the latch clears only when both sources are not commanding, `bed_control.c:34-49`), i.e. after a switch/app conflict; then the bed would refuse new moves until some later switch release. `bed_note_source_released()` only clears the flag and possibly the latch; it starts nothing.

**B3. Unused include removed.** `bed_actions.c` no longer includes `switch_input.h`. A grep for every symbol `switch_input.h` declares (`switch_pressed`, `app_ui_interrupt`, `switch_sampler_init`, `switch_input_tick`, `switch_sampler_active`, `switch_sampler_down`, `switch_state_fresh`, `motion_failsafe_tick`, and now `switch_input_clear_panel_state`) finds only two comment lines (d.2.2), and the build stayed clean with no warnings (an undeclared function would warn under this toolchain's flags; an undeclared variable would be an error).

### b.3 Part C — settings mode exit (commit `dd8d069`)

**The problem.** `in_settings_mode` was only ever set true (`settings_mode.c:271`, was :223 before this commit added lines above it); the only way out was a reset.

**The design as implemented.**

1. Trigger, in `settings_mode_process_outputs()`'s `SWITCH_BED_LIGHTS` case, press branch (`settings_mode.c:238-250`). The branch body used to be `active_switch = SWITCH_BED_LIGHT_VALUE;`; it is now a call to `settings_mode_exit()`. The release branch is unchanged:

```c
    case SWITCH_BED_LIGHTS:
      if (switch_pressed && (active_switch == OFF)){
        // A NEW press of BED LIGHTS exits settings mode. ... So only a later press can get here.
        settings_mode_exit();
      }
      else if (active_switch == SWITCH_BED_LIGHT_VALUE){
        active_switch = OFF;
      }
      break;
```

Removing the old body loses nothing: in settings mode, `SWITCH_BED_LIGHT_VALUE` was only read by the entry check in `settings_mode_tick()` (which requires `!in_settings_mode`) and by this release branch. The release branch is still needed, for the entry press's release (step 4 of the trace below).

2. `settings_mode_exit()` (`settings_mode.c:118-152`), in this order:
   - `in_settings_mode = false;` then prints `Settings mode: exit` (inside `#if !BED_CAPTURE_MOTOR_CURRENTS`, like every other print in the file).
   - Stops `handle_2000ms_timer` and `handle_5000ms_timer` and clears `timeout_2000ms` and `timeout_5000ms`. No NVM3 write.
   - `set_all_motors_off();` (settings mode is excluded from the motion fail-safe). This also sets the motor-enable shadow to 0, so it no longer blocks sleep.
   - `fast_stop_blink_LED(LED_AMBER);` then `set_LED_on(LED_AMBER)` if `app_lights_on()`; then red, green and blue off.
   - `switch_input_clear_panel_state();` `active_switch = OFF;` `bed_note_source_released(BED_SOURCE_SWITCHES);`
   - Declared in `settings_mode.h` with a comment listing all of the above, so part D and anything later can call it.

3. `fast_stop_blink_LED()` (`app.c:351-357`), declared in `app.h`. Mirrors `auto_level_stop_blink_LED()` line for line, but for `LED_200ms_timer` and `LED_timer_timeout`:

```c
void fast_stop_blink_LED(led_color_t LED_color){
  sl_sleeptimer_stop_timer(&LED_200ms_timer);
  set_LED_off(LED_color);
  LED_timer_timeout = false;
  single_LED_on[LED_color] = false;
}
```

   Caveat, in its comment: `LED_timer_timeout` is shared with the 500 ms collision-blink timer (both use `LED_timer_callback`, `app.c:178-197`). On settings exit nothing should be driving motors (settings mode never energises them and the BLE motion opcodes are refused in settings mode, `ble_bed_service.c:545-547`), so no collision blink should be running; if one were, clearing the flag would only delay its next toggle by one 500 ms period.

4. `switch_input_clear_panel_state()` (`switch_input.c:176-179`), declared in `switch_input.h`: a one-line exported wrapper around the existing static `clear_switch_panel_state()`, so the variables stay private.

**VERIFY requested by the prompt: is `switch_held` stale for the whole of settings mode? Yes.** Trace of entry and the stay in settings mode (line numbers at `944f19e`):

1. BED LIGHTS goes down in normal mode. The sampler queues the press; `deliver_switch_edge(0x0008, true)` (`switch_input.c:905`) sets `app_ui_interrupt = 0x0008`, `switch_pressed = true` and calls `process_inputs()`.
2. `process_inputs()` normal branch (`in_settings_mode` is false): `switch_driving = OFF` (`:615-617`), then the BED LIGHTS case (`:665`): `switch_held == OFF`, so `switch_held = SWITCH_BED_LIGHT_VALUE`, and `bed_lights_set(SWITCHES, !app_lights_on())` → `app_lights_set()` sets `active_switch = SWITCH_BED_LIGHT_VALUE` and toggles `bedlights_on` (`bed_actions.c:197-202`); it returns true so `switch_driving = SWITCH_BED_LIGHT_VALUE`. `process_outputs()` then turns the under-bed lights and amber on or off (`switch_input.c:557-565`). **The lights toggle here, at the start of the entry hold.**
3. While held, `settings_mode_tick()` (runs each pass before `switch_input_tick()`, `app.c:708` / `:715`) sees `switch_pressed && active_switch == SWITCH_BED_LIGHT_VALUE && !in_settings_mode`, starts the 5 s timer, and at timeout sets `in_settings_mode = true` (`settings_mode.c:263-275`). No print.
4. BED LIGHTS is released: `deliver_switch_edge(0x0008, false)` → `process_inputs()` takes the `else if (in_settings_mode)` branch (`switch_input.c:717`), which only calls `process_outputs()` → `settings_mode_process_outputs()` (`:592-593`) → BED LIGHTS release branch: `active_switch = OFF`. **`switch_held` and `switch_driving` are not touched by any of this.** No other path in settings mode clears them: the normal-mode press/release code is skipped, the double-press block is gated `!in_settings_mode` (`:921`), and the fail-safe returns early in settings mode before its cleanup.
5. So `switch_held == SWITCH_BED_LIGHT_VALUE` for the whole stay. If it were not cleared on exit, every later normal-mode press would fail its `switch_held == OFF` test and be dead (DECISIONS #38's "bench observation 2" failure). Hence the clear in `settings_mode_exit()`.

**Why the entry press's release cannot trigger the exit** (the prompt asked for this, citing the code path). The exit is only in the branch `if (switch_pressed && (active_switch == OFF))`. (i) The entry press itself was delivered in normal mode (step 2): `in_settings_mode` becomes true 5 s later in `settings_mode_tick()`, not in the press path, and the sampler delivers exactly one press per accepted edge, so a held button is never delivered as a press again however long it is held. (ii) The entry press's release is delivered by `deliver_switch_edge(bit, false)` with `switch_pressed = false` (step 4), so it takes the `else if` branch, never the press branch. Therefore only a later, separate press reaches `settings_mode_exit()`.

**Why the exit press does nothing else.**
- It cannot toggle the lights: in settings mode `process_inputs()` takes only the settings branch, so `bed_lights_set()` is never called for it; `settings_mode_exit()` does not touch the lights output, only the amber indicator.
- It cannot start a new entry however long it is held: the entry condition needs `active_switch == SWITCH_BED_LIGHT_VALUE`. The exit sets `active_switch = OFF`, no new press is delivered while the button stays down, and the only code that writes `SWITCH_BED_LIGHT_VALUE` to `active_switch` is `app_lights_set()` (`bed_actions.c:199`, grep in d.2.2). Its two callers: the normal-mode press path (needs a new press), and the BLE LIGHTS_SET opcode, which calls `app_apply_lights_outputs()` → `app_release_active_switch()` in the same function call (`ble_bed_service.c:665-669`, `bed_actions.c:275-285`), so `settings_mode_tick()` never sees it set.
- Its release, arriving in normal mode, does nothing: `process_inputs()` BED LIGHTS release needs `switch_held == SWITCH_BED_LIGHT_VALUE` (cleared by the exit), and `process_outputs()` BED LIGHTS release needs `switch_driving == SWITCH_BED_LIGHT_VALUE` (cleared by the exit). `settings_mode_cancel_entry()` is in that release branch, so it is not called either, and it would be harmless.
- The double-press block does not run (it needs `active_switch != OFF`), and the fail-safe sees a switch down while it is held and nothing energised after.

**Bed Status settings-mode flag: updates on exit with no further change. VERIFIED by code reading.** `bed_status_get()` reads `app_in_settings_mode()` live every call (`bed_status.c:144`); `ble_bed_service_encode_status()` sets flag bit `0x02` from it (`ble_bed_service.c:81`); `ble_bed_service_tick()` runs every pass after `switch_input_tick()` (`app.c:715`, `:747`) and publishes whenever `status_flags_changed()` finds bytes 0-3 (flags, source, command, detail) different from the last sent (`ble_bed_service.c:791-800`, `:856-858`). So a subscribed phone gets a notification with bit `0x02` clear on the same main-loop pass as the exit. Not bench-observed.

**Other points about the exit.**
- "An exit in the middle of a store/erase/sensitivity hold" cannot actually be reached from the switches: during such a hold the held button plus BED LIGHTS is two switches down, and the sampler delivers no press while two are down (`switch_input.c:1004-1008`). The press is not re-delivered when the other button is released; the user has to release BED LIGHTS and press it again. The timer stop/flag clear is still worthwhile: (a) the prompt asks for it; (b) there is a pre-existing race where an UP_ALL or DOWN_ALL release stops `handle_2000ms_timer` but does not clear `timeout_2000ms` (`settings_mode.c:189-196` for UP_ALL, `:226-233` for DOWN_ALL), so a flag can be left set and consumed by the next hold. The exit clears it. (The race itself is not fixed; QUESTIONS 11.)
- Not reset on exit (not requested, and harmless): `zeroLevelFirstPass` and `resetZeroLevelFirstPass` (set again by the next AUTO_LEVEL press in settings mode) and `writeCollisionDetection` (set and used within one `settings_mode_tick()` call, so never true between passes).
- `motor_state[].motor_on` is not cleared (the prompt names `set_all_motors_off()` only; settings mode never sets those flags).
- REGRESSION.md 7.9 said "settings mode has no soft exit". It is rewritten to describe the new exit. This was not literally asked for; QUESTIONS 6. `docs/REFACTOR_PLAN.md` P3.13 ("Settings mode has no exit") and the two REGRESSION_RECORD files are not edited.

### b.4 Part D — pairing window (commit `944f19e`)

**What changes.** One handler, `sl_bt_evt_sm_confirm_bonding` in `ble_bed_service_on_event()` (`ble_bed_service.c:1006-1017`):

```c
    case sl_bt_evt_sm_confirm_bonding_id: {
      const sl_bt_evt_sm_confirm_bonding_t *cb = &evt->data.evt_sm_confirm_bonding;
      uint8_t confirm = app_in_settings_mode() ? 1 : 0;
#if BLE_DEBUG_SECURITY_LOG
      printf("BLE SM: confirm_bonding conn=%u (%s) -> %s\r\n",
             cb->connection,
             (cb->bonding_handle == SL_BT_INVALID_BONDING_HANDLE) ? "new bond" : "REPLACING existing bond",
             confirm ? "ACCEPTED (settings mode)" : "REJECTED (not in settings mode)");
#endif
      sl_bt_sm_bonding_confirm(cb->connection, confirm);
      break;
    }
```

Before, it printed `... bonding_handle=0x%02x (%s, auto-confirming)` and always passed 1. The new print follows the prompt's format exactly, which drops the raw `bonding_handle` value (QUESTIONS 1). `app_in_settings_mode()` is declared in `bed_actions.h`, already included. `sl_bt_on_event()` runs in main-loop context on this part (existing comment at `ble_bed_service.c:499-501`), the same context that writes `in_settings_mode`, so no locking is needed.

Unchanged: `sl_bt_sm_configure(BONDING_REQUIRED | SC_ONLY | BONDING_REQUEST_REQUIRED, NoInputNoOutput)`, `sl_bt_sm_store_bonding_configuration(8, 0)`, `sl_bt_sm_set_bondable_mode(1)`. Bondable stays 1 because the gate is enforced per request in the handler, which is the single decision point #28 planned. The stale comments are updated: the configure comment (`:874-880`) now says the handler decides every request and is the pairing window (#42); the bondable comment (`:894-897`) now says the window is enforced in the handler, not here; the handler's header comment (`:997-1005`) replaces "Today: always confirm - no gate here yet ..." with the window rule. The `settings_mode_exit()` comment gains "Also closes the BLE pairing window, which is simply 'in_settings_mode is true' (DECISIONS.md #42)" (added in this commit, not C, because #42 did not exist yet at C).

**The four user decisions, as implemented.**
1. *Only new bonds are gated; a bonded phone reconnects freely.* A reconnecting bonded phone does not raise `confirm_bonding`. Evidence: (a) SDK: the event "Indicates a user request to display that the new bonding request is received" (`sl_bt_api.h:11930`), and `SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED` is "Bonding requests need to be confirmed" (`:11759-11761`); re-establishing encryption from a stored key is not a bonding request. (b) Existing code: `assign_role_if_needed()`'s comment (`ble_bed_service.c:297-305`) and the `connection_parameters` handler (`:1019-1022`) already rely on a resumed bond not repeating the pairing procedure. (c) **Bench, VERIFIED:** in `sampler_bench.log`, lines 956-961 and 979-984, a phone with a stored bond (`connection_opened conn=1 bonding=0x01`) reconnected twice and reached `security_mode=1` with no `confirm_bonding` line, with `BLE_DEBUG_SECURITY_LOG` 1 (it would have printed the old "auto-confirming" line). That was the previous firmware, but this path is unchanged.
2. *Re-pairing a known phone is gated too.* `bonding_handle != SL_BT_INVALID_BONDING_HANDLE` goes through the same `confirm` value. This makes DECISIONS #28's auto-replace conditional: a phone with a stale bond can still recover without a reflash, but only by putting the bed in settings mode and pairing again. #42 says so. #28's own security analysis anticipated this exact step.
3. *Window = while settings mode is active, no timer.* Opens at settings-mode entry, closes in `settings_mode_exit()` (part C) and on reset (`in_settings_mode` is initialised false, `settings_mode.c:55`). The gate is evaluated when the request arrives: a pairing accepted just before the exit completes after it.
4. *Reject outside the window* with `sl_bt_sm_bonding_confirm(conn, 0)` ("0: Reject", `sl_bt_api.h:12189`). That the phone then sees a clean pairing failure is the SDK's documented behaviour, not observed here.

**DECISIONS numbering.** Number 19 is already used, by "19. Pairing window is physically gated", which records this same policy but was never built. Per the prompt, the implementation is recorded as **#42**, which says it implements BLE_GATT_DESIGN.md §8's "proposed #19". #19 was not edited (the log is corrected forward, not rewritten). BLE_GATT_DESIGN.md §8 now reads "(DECISIONS #19, implemented as DECISIONS #42)" instead of "(proposed DECISIONS #19)", and adds one sentence: re-pairing a bonded phone is gated the same way, and a bonded phone reconnecting is not affected.

### b.5 Answers requested in part D (analysis only, nothing implemented)

**(i) What should the 8-bond-full case do, now that new bonds need physical access?**

What happens today (VERIFIED from the SDK header, behaviour ASSERTED): the firmware calls `sl_bt_sm_store_bonding_configuration(8, 0)`. Per `sl_bt_api.h:12070-12072`, policy 0 means *"If database is full, pairing will be performed but no bonding is stored"*. **The code comment above that call (`ble_bed_service.c:888-889`, "Reject new bonds once the 8-bond store is full, rather than silently evicting an existing one") is therefore wrong about what happens.** A 9th phone, paired inside the window, would get an encrypted link and a role for that one connection, but nothing is stored. On its next connection it expects to resume a bond the bed does not have; encryption fails; any re-pair is a "new bond" request, rejected outside settings mode and stored nowhere inside it. Net effect: the 9th phone only works for connections that start while the bed is in settings mode, and there is no way for the owner to free a slot (no un-bond gesture, no delete-bond command).

Options:
- (A) Policy 2, "New bonding will overwrite the bonding that was used the longest time ago". Now that every new bond needs someone at the switch panel, evicting the least-recently-used phone no longer lets a stranger push out a legitimate phone remotely. Cost: the evicted phone fails on its next connect and must re-pair in settings mode (the #28/#42 path), with no warning to anyone at the moment of eviction. One-line change at the call site plus the comment.
- (B) Keep policy 0 and add a deliberate way to free slots: a settings-mode gesture that wipes all bonds (simple, physical, but loses everyone), or a Settings RPC (BLE_GATT_DESIGN §9) that lets a bonded phone delete another bond (needs app UI).
- (C) Do nothing and document the limit: an owner with more than 8 phones is unlikely.

Recommendation: (A) policy 2, plus correcting the comment. It keeps "physical access is the gate" (#19) as the only security boundary and needs no new UI. (B)'s wipe-all gesture is a reasonable later addition for resale/handover. Either way, the comment should be corrected now (QUESTIONS 3).

**(ii) Is settings mode's amber fast blink a sufficient "window open" signal?**

For the person standing at the panel, mostly yes: it is visible where the action happens, it lasts exactly as long as the window (no timer to outlive it), and the window closes with one press. Gaps:
- It is not specific to pairing. The same blink means "settings mode" for zero-angle and sensitivity work, and while it blinks amber cannot show the lights state. Users need to be told, in the app's pairing instructions, "hold BED LIGHTS until amber flashes fast, then pair; press BED LIGHTS once when done".
- No timer means a forgotten window stays open until a press or a reset. The blink is the only reminder, and it is only visible inside the van. A stranger outside cannot open the window, but can use one left open. Options if that matters: close the window after the first successful bond, or auto-exit settings mode after some minutes. Both re-open decision 3 ("no timer"), so I only list them.
- No confirmation that a bond was accepted: the phone shows success, the panel shows nothing. A green flash on `sl_bt_evt_sm_bonded` is possible, but the settings LEDs already mean sensitivity rungs, so it would need its own pattern.
- An already-connected app can see the window via Bed Status flag `0x02`, so it could warn "pairing is open". A new phone cannot, because it is not bonded yet.

Verdict: sufficient for this phase if the app's instructions name it. The forgotten-window risk is the real gap; flagged for the reviewer, not changed.

**(iii) Anything in the settings-mode flow that assumed bonding could not happen during it?**

- **STOP_ALL from a phone in settings mode** (most significant; pre-existing, now more likely because a phone is being paired precisely while the bed is in settings mode). STOP_ALL is honoured from any role before the settings-mode gate (`ble_bed_service.c:478-491`) and calls `app_stop_all()`, which does `set_all_LED_off()` (amber included) and `active_switch = OFF` (`bed_actions.c:208-222`). It does **not** exit settings mode and does **not** stop the settings timers. If a store/erase/sensitivity hold is in progress, its `active_switch` is wiped mid-hold while `handle_2000ms_timer`/`handle_5000ms_timer` keep running, so a stale `timeout_2000ms`/`timeout_5000ms` can be consumed by the next hold, which would then step or store early. The amber blink continues (the 200 ms timer still runs) but `single_LED_on[AMBER]` no longer matches the LED, so the blink phase may glitch once. Options: have STOP_ALL call `settings_mode_exit()` when in settings mode, or have it stop the settings timers. QUESTIONS 4.
- **A newly bonded phone gets a role and subscribes during settings mode.** Its motion and LIGHTS commands are refused with `REJECTED_SETTINGS_MODE` (`ble_bed_service.c:545-547`; LIGHTS_SET is handled below that gate, at `:660`). Its tilt display comes from `baseline_accel_average_*` while in settings mode (`bed_status.c:116-118`), which only updates during an AUTO_LEVEL hold, so it shows a stale angle until exit. Bed Status `active_source` reads SWITCHES for the whole stay, because `app_bed_is_idle()` is false in settings mode (`bed_actions.c:352-355`) so `bed_control_tick()` never releases the lock. Cosmetic.
- **Loop rate during a zero-angle capture.** `accel_running_average()` runs once per pass during the AUTO_LEVEL store hold, and DECISIONS #29 records that auto-level's filter is calibrated in loop passes. LE Secure Connections pairing does key-exchange work in the stack; if that slows the main loop during the 2 s capture, the averaged angle covers a different number of samples. ASSERTED as a possibility, not measured; low impact.
- **NVM3.** The stack writes the new bond to NVM3 while settings mode may also be writing angles or margins. Both happen in main-loop context, so they cannot interleave mid-write. ASSERTED from the context comment, not traced into the stack.
- **Window closes mid-pairing.** A request accepted just before the exit completes after it. Fine by design.
- **Rejected phone stays connected?** Not settings-mode specific, but new: outside the window a stranger's phone that is refused may stay connected, unencrypted and unable to read anything, using one of the two connection slots. The firmware does not disconnect it. QUESTIONS 2.

### b.6 Behaviour changes the bench should expect

- Every switch edge is accepted ~10 ms sooner (10-20 ms after the contact changes, was 20-30 ms). A button bounce lasting two samples (>10 ms) would now get through as a press/release; none was seen in 700 edges.
- Settings mode can be left with one BED LIGHTS press. New UART line: `Settings mode: exit`.
- Pairing is refused outside settings mode, for new phones and for re-pairing known ones. A bonded phone reconnects as before. New UART line format: `BLE SM: confirm_bonding conn=N (new bond|REPLACING existing bond) -> ACCEPTED (settings mode)` or `-> REJECTED (not in settings mode)`.
- After a Double Press Error the switches no longer hold their half of the conflict latch.

### b.7 Part E — the reviewer's bench steps, with the expected PuTTY lines

All of this is what the code should print; none of it has been observed. Build as committed (`BED_BENCH_PROVOKE_RUNAWAY 0`, `SWITCH_SAMPLER_DIAG 1`, `BLE_DEBUG_SECURITY_LOG 1`). `(bounces N)` is whatever the contact does. `Max Current for Motor N: ...` blocks (four lines) appear during holds long enough for the existing dynamic collision calibration; they are unchanged behaviour (the 2026-10-18 log has them, including during a BED LIGHTS hold) and are left out below.

**E1. Single press and release, debounce now 2** (UP HEAD, about 1 s):

```
SW: 0x8000 down (bounces 0)      <- ~10-20 ms after the button goes down; HEAD pair moves up, green LED on
SW: 0x8000 up (bounces 0)        <- ~10-20 ms after release; motors stop, green LED off
```

There must be no `FAILSAFE:`, no `Double Press Error`, no `Unrecognized switch mask`, no `SW: ... failed`. Repeat a few times with quick taps.

**E2. Enter settings mode (BED LIGHTS held ~6 s), then exit with one BED LIGHTS press.** Start with the under-bed lights OFF (if they start ON, swap on/off throughout).

```
SW: 0x0008 down (bounces 0)      <- at the press: under-bed lights turn ON, amber steady ON (the normal toggle; the entry press always toggles)
                                 <- ~5 s into the hold: settings mode entered. NO UART line (entry has never printed).
                                    Amber starts fast-blinking (200 ms). Lights stay ON.
SW: 0x0008 up (bounces 0)        <- release at ~6 s: nothing else printed, amber keeps fast-blinking, lights unchanged
   (wait a moment)
SW: 0x0008 down (bounces 0)      <- the exit press
Settings mode: exit              <- immediately after. Amber stops blinking and is steady ON (lights are on);
                                    red, green, blue OFF; under-bed lights stay ON (the exit press does not toggle them)
   (keep holding > 5 s)          <- nothing more: no re-entry, amber stays steady
SW: 0x0008 up (bounces 0)        <- release: nothing else printed, lights unchanged
```

If a phone is connected and subscribed, Bed Status flags bit `0x02` should be set during settings mode and clear at the exit. Also worth one run: press BED LIGHTS while holding another button in settings mode. Expected: `SW: 2 switches down - presses ignored`, no exit; then release both and press BED LIGHTS alone to exit.

**E3. After the exit, one press of each button works normally.** For each button, a single press-and-hold then release:

| Button | Mask in `SW:` line | Expected |
|---|---|---|
| UP HEAD / UP LEFT / UP RIGHT / UP FOOT | 0x8000 / 0x4000 / 0x0400 / 0x1000 | pair moves up, green LED; stops on release |
| DOWN HEAD / DOWN LEFT / DOWN RIGHT / DOWN FOOT | 0x0800 / 0x2000 / 0x0200 / 0x0100 | pair moves down, green LED; stops on release |
| UP ALL / DOWN ALL | 0x0001 / 0x0004 | all four move, blue LED; stop on release |
| AUTO LEVEL | 0x0002 | hold-to-run auto-level prints (`Step 1: ROLL and PITCH check` ...); stops on release |
| BED LIGHTS (tap) | 0x0008 | lights and amber toggle |

Each gives one `SW: 0x.... down` and one `SW: 0x.... up`, no `FAILSAFE:`, no `Unrecognized switch mask`. **A dead button here (down/up lines print but nothing happens) means the stale `switch_held` was not cleared**, which is the specific failure part C guards against.

**E4. Pairing.**

(a) Bonded phone, NOT in settings mode, reconnects (e.g. toggle the phone's Bluetooth or re-open the app):

```
BLE: connection_opened conn=N bonding=0xNN               <- NN not ff: the bed recognised the bond
BLE: connection_parameters conn=N interval=.. security_mode=0
BLE: connection_parameters conn=N interval=.. security_mode=1   <- encryption resumed from the bond
BLE: cccd_seed conn=N bed_status=0x.... sc=0x0000        <- if the app had subscribed
```

**No `BLE SM: confirm_bonding` line.** This is the same sequence as `sampler_bench.log` lines 956-961. The pre-existing `BLE: characteristic_status DROPPED ...` line seen there may also appear; it is unrelated.

(b) Same phone with its bond deleted on the phone side ("Forget This Device"), the bed's copy still stored, NOT in settings mode, then connect/pair from the app:

```
BLE: connection_opened conn=N bonding=0xNN
BLE: connection_parameters conn=N interval=.. security_mode=0
BLE SM: confirm_bonding conn=N (REPLACING existing bond) -> REJECTED (not in settings mode)
BLE SM: bonding_failed conn=N reason=0x....              <- record the reason code; not known in advance
BLE: connection_closed conn=N reason=0x....              <- if/when the phone gives up (not guaranteed)
```

If the bed did not resolve the phone's address (`bonding=0xff` at connection_opened), the middle line says `(new bond)` instead of `(REPLACING existing bond)`; record which. The phone should report a pairing failure, not hang. There must be no `BLE SM: bonded` line. The bed's stored bond for that phone should still be there afterwards (not checked by any print).

(c) Same phone, bed IN settings mode (E2's hold until amber fast-blinks, release), then pair again from the phone:

```
BLE SM: confirm_bonding conn=N (REPLACING existing bond) -> ACCEPTED (settings mode)
BLE SM: bonded conn=N bonding=0xNN security_mode=1
BLE: connection_parameters conn=N interval=.. security_mode=1
```

(possibly preceded by a fresh `BLE: connection_opened`). The app should then work, with movement refused (`REJECTED_SETTINGS_MODE`) until the exit.

(d) Exit settings mode with one BED LIGHTS press (`SW: 0x0008 down`, `Settings mode: exit`), then disconnect and reconnect the phone: the (a) sequence, again with **no** `confirm_bonding` line, and movement from the app works.

Optional (e): with the bed NOT in settings mode, try to pair a phone that has never been bonded: `... (new bond) -> REJECTED (not in settings mode)`. Note whether the phone disconnects or stays connected (QUESTIONS 2).

## c. Source diffs (complete, untruncated)

Per commit, `git diff <commit>~1 <commit> -- '*.c' '*.h'`. Commit A (`acd410a`) has no source changes (the command printed nothing).

### c.1 Commit B `47db449`

```diff
diff --git a/bed_actions.c b/bed_actions.c
index d9bcde7..93d7d4f 100644
--- a/bed_actions.c
+++ b/bed_actions.c
@@ -20,7 +20,6 @@
 #include "exp_board.h"
 #include "motor_current_functions.h"
 #include "settings_mode.h"
-#include "switch_input.h"
 
 /*
  * Tracks whether the in-progress move is an ALL-type move, independent of
diff --git a/switch_input.c b/switch_input.c
index e9c7ace..24b4472 100644
--- a/switch_input.c
+++ b/switch_input.c
@@ -51,7 +51,7 @@
 // Switch sampler timing (DECISIONS.md #41). Milliseconds via sleeptimer, never
 // loop passes (#2).
 #define SWITCH_SAMPLE_MS            10
-#define SWITCH_DEBOUNCE_SAMPLES     3   // 3 identical reads = >=20 ms stable
+#define SWITCH_DEBOUNCE_SAMPLES     2   // 2 identical reads = >=10 ms stable
 #define SWITCH_READ_FAIL_LIMIT      5   // 50 ms of failed reads -> all released
 #define SWITCH_STATE_STALE_MS       100 // fail-safe distrusts older data
 #define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
@@ -947,6 +947,12 @@ void switch_input_tick(void)
           // switch_driving naming a button process_outputs would still
           // energise on, which is motion with bed_control never consulted.
           clear_switch_panel_state();
+          // Release the switches' half of the #36 latch, for the same reason
+          // as in motion_failsafe_tick(): clearing switch_held above means the
+          // held buttons' later releases find no match and never reach
+          // bed_move_stop(), so without this the switches' half of the latch
+          // could stay set until some later press/release.
+          bed_note_source_released(BED_SOURCE_SWITCHES);
           active_switch = OFF;
           // Reset motor readings
           reset_ignore_data_points_count();
```

### c.2 Commit C `dd8d069`

```diff
diff --git a/app.c b/app.c
index 9b62411..9326733 100644
--- a/app.c
+++ b/app.c
@@ -341,6 +341,21 @@ void fast_blink_LED(led_color_t LED_color){
   }
 }
 
+// Single LED blink FAST stop - mirrors auto_level_stop_blink_LED(), but for
+// LED_200ms_timer. LED_timer_timeout is shared with the 500 ms collision blink
+// timer. This is only called on settings-mode exit, when nothing should be
+// driving motors (settings mode never energises them and BLE motion commands
+// are refused in it), so no collision blink should be running; if one were,
+// clearing the flag would only delay its next toggle by one 500 ms period.
+// Non-static: called by settings_mode.c, see app.h
+void fast_stop_blink_LED(led_color_t LED_color){
+
+  sl_sleeptimer_stop_timer(&LED_200ms_timer);
+  set_LED_off(LED_color);
+  LED_timer_timeout = false;
+  single_LED_on[LED_color] = false;
+}
+
 // sl_button_on_change() moved to switch_input.c
 
 // Resets both the on-board and switch board I2C GPIO expanders simultaneously
diff --git a/app.h b/app.h
index 0fcfbd3..269e68c 100644
--- a/app.h
+++ b/app.h
@@ -128,7 +128,8 @@ void app_reset_auto_level_timer(void);
 void set_all_LED_off(void);
 void auto_level_stop_blink_LED(led_color_t LED_color);
 
-// Settings-mode LED helper - non-static: called by settings_mode.c
+// Settings-mode LED helpers - non-static: called by settings_mode.c
 void fast_blink_LED(led_color_t LED_color);
+void fast_stop_blink_LED(led_color_t LED_color);
 
 #endif  // APP_H
diff --git a/settings_mode.c b/settings_mode.c
index 0e83660..7ebff55 100644
--- a/settings_mode.c
+++ b/settings_mode.c
@@ -15,9 +15,12 @@
 
 #include "app.h"
 #include "auto_level.h"
+#include "bed_actions.h"
+#include "bed_control.h"
 #include "bed_settings.h"
 #include "bed_types.h"
 #include "debug_capture.h"
+#include "exp_board.h"
 #include "exp_ui.h"
 #include "motor_current_functions.h"
 #include "switch_input.h"
@@ -108,6 +111,45 @@ void settings_mode_cancel_entry(void)
   timeout_5000ms = false;
 }
 
+// Leaves settings mode and returns the panel to a clean normal-mode idle.
+// Triggered by a NEW press of BED LIGHTS while in settings mode (see
+// settings_mode_process_outputs()).
+void settings_mode_exit(void)
+{
+  in_settings_mode = false;
+#if !BED_CAPTURE_MOTOR_CURRENTS
+  printf("Settings mode: exit\r\n");
+#endif
+
+  // Abandon any store/erase/sensitivity hold in progress: nothing half-done
+  // survives the exit, and nothing is written to NVM3 here.
+  sl_sleeptimer_stop_timer(&handle_2000ms_timer);
+  timeout_2000ms = false;
+  sl_sleeptimer_stop_timer(&handle_5000ms_timer);
+  timeout_5000ms = false;
+
+  // Settings mode is excluded from the motion fail-safe, so leave nothing
+  // energised behind it.
+  set_all_motors_off();
+
+  // Amber stops fast-blinking and goes back to showing the under-bed lights;
+  // the three settings LEDs go off.
+  fast_stop_blink_LED(LED_AMBER);
+  if (app_lights_on()) {
+    set_LED_on(LED_AMBER);
+  }
+  set_LED_off(LED_RED);
+  set_LED_off(LED_GREEN);
+  set_LED_off(LED_BLUE);
+
+  // The entry press set switch_held/switch_driving in normal mode and its
+  // release was handled by the settings branch, which never clears them.
+  // Without this every button is dead after the exit.
+  switch_input_clear_panel_state();
+  active_switch = OFF;
+  bed_note_source_released(BED_SOURCE_SWITCHES);
+}
+
 // Finds the ladder rung matching the current raise/lower margins, or -1 if mismatched
 static int8_t find_sensitivity_index(void)
 {
@@ -194,7 +236,12 @@ void settings_mode_process_outputs(void)
 
     case SWITCH_BED_LIGHTS:
       if (switch_pressed && (active_switch == OFF)){
-        active_switch = SWITCH_BED_LIGHT_VALUE;
+        // A NEW press of BED LIGHTS exits settings mode. The press that
+        // entered it was delivered in normal mode, before in_settings_mode
+        // was set, and is never delivered again however long it is held; its
+        // release arrives with switch_pressed false and takes the branch
+        // below. So only a later press can get here.
+        settings_mode_exit();
       }
       else if (active_switch == SWITCH_BED_LIGHT_VALUE){
 
diff --git a/settings_mode.h b/settings_mode.h
index d2ecac0..e9549ac 100644
--- a/settings_mode.h
+++ b/settings_mode.h
@@ -4,6 +4,7 @@
  * The "settings mode" subsystem: holding BED_LIGHTS 5 s enters it; while in
  * it, AUTO_LEVEL-hold stores/erases the custom zero angle and UP_ALL/
  * DOWN_ALL-hold steps the collision-sensitivity ladder, writing to NVM3.
+ * A later, separate press of BED_LIGHTS exits it (settings_mode_exit()).
  * Relocated from app.c on C4 commit 3 (Jul 2026) - pure relocation, no
  * behavior change. in_settings_mode is defined here now (was app.c); app.c's
  * own tick/dispatch logic still reads it via this header, same as
@@ -37,4 +38,11 @@ void settings_mode_process_outputs(void);
 // during the entry hold (the double-press-check block).
 void settings_mode_cancel_entry(void);
 
+// Leaves settings mode: stops both settings timers (no NVM3 write), turns the
+// motors off, stops the amber fast blink and restores amber to the under-bed
+// lights state, turns red/green/blue off, and clears the panel bookkeeping
+// (switch_held, switch_driving, active_switch, the switches' half of the #36
+// latch). Called on a new BED_LIGHTS press while in settings mode.
+void settings_mode_exit(void);
+
 #endif /* SETTINGS_MODE_H_ */
diff --git a/switch_input.c b/switch_input.c
index 24b4472..f66a4e4 100644
--- a/switch_input.c
+++ b/switch_input.c
@@ -168,6 +168,16 @@ static void clear_switch_panel_state(void)
   switch_driving = OFF;
 }
 
+// The same, for callers outside this file - without exposing the variables.
+// settings_mode_exit() needs it: the BED LIGHTS press that entered settings
+// mode set switch_held/switch_driving in normal mode, and its release was
+// handled by the settings branch, which never clears them. Left stale, every
+// button would be dead after the exit.
+void switch_input_clear_panel_state(void)
+{
+  clear_switch_panel_state();
+}
+
 // Maps each single-side switch mask to its active_switch_t value, motor side, and direction
 static const struct {
   uint16_t mask;
diff --git a/switch_input.h b/switch_input.h
index 8591394..8c8d4ea 100644
--- a/switch_input.h
+++ b/switch_input.h
@@ -43,6 +43,10 @@ bool switch_sampler_active(void);
 uint16_t switch_sampler_down(void);
 bool switch_state_fresh(void);
 
+// Clears the physical panel's own bookkeeping (switch_held, switch_driving -
+// DECISIONS.md #37). For settings_mode_exit(), whose entry press left them set.
+void switch_input_clear_panel_state(void);
+
 // Unconditional motion fail-safe (DECISIONS.md #38). Call once per
 // app_process_action(). De-energises the motors whenever the sampler's
 // debounced state says no switch is down (stale counts as none), the app is not
```

### c.3 Commit D `944f19e`

```diff
diff --git a/ble_bed_service.c b/ble_bed_service.c
index be58c12..1626ddc 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -872,10 +872,12 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
       // Bonded + encrypted, Just Works (no MITM claim - no display/passkey
       // entry hardware), LE Secure Connections only (BLE_GATT_DESIGN.md #8).
       // Bit 3 (BONDING_REQUEST_REQUIRED) is set so sl_bt_evt_sm_confirm_bonding
-      // fires and the app decides whether to allow a re-pair against an
-      // existing bond, instead of the stack auto-rejecting it (bench-
+      // fires for every bonding request and the app decides it, instead of
+      // the stack auto-rejecting a re-pair against an existing bond (bench-
       // confirmed 0x1205 SMP_PAIRING_NOT_SUPPORTED with this bit clear -
-      // DECISIONS.md #28, auto-replace fix for the stale-bond lockout bug).
+      // DECISIONS.md #28). That handler is the pairing window (DECISIONS.md
+      // #42): new bonds and replacements alike are accepted only while
+      // settings mode is active.
       sc = sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_BONDING_REQUIRED
                                | SL_BT_SM_CONFIGURATION_SC_ONLY
                                | SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED,
@@ -889,9 +891,10 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
       app_assert_status(sc);
       printf("BLE SM: store_bonding_configuration -> 0x%04x\r\n", (unsigned)sc);
 
-      // Bondable at all times for now - DECISIONS.md #19's physical
-      // pairing-window gate (settings-mode-only bonding) is a later,
-      // separately-approved step, not part of bring-up.
+      // Bondable at all times at the stack level. The physical pairing
+      // window (settings-mode-only bonding, DECISIONS.md #19/#42) is enforced
+      // per request in the sl_bt_evt_sm_confirm_bonding handler below - one
+      // decision point, not a second mechanism here.
       sc = sl_bt_sm_set_bondable_mode(1);
       app_assert_status(sc);
       printf("BLE SM: set_bondable_mode(1) -> 0x%04x\r\n", (unsigned)sc);
@@ -992,19 +995,24 @@ void ble_bed_service_on_event(sl_bt_msg_t *evt)
     // security risk"). Logged so a replace vs. a new bond is visible on the
     // bench.
     //
-    // Today: always confirm - no gate here yet. This is the single decision
-    // point for DECISIONS.md #19's still-unbuilt physical pairing window:
-    // when #19 lands, this becomes "confirm only if the pairing window is
-    // open, reject otherwise" (sl_bt_sm_bonding_confirm(cb->connection, 0)
-    // on the reject path) - not a second mechanism bolted on elsewhere.
+    // THE PAIRING WINDOW (DECISIONS.md #42, implementing #19): confirm only
+    // while settings mode is active on the switch panel, reject otherwise.
+    // New bonds and replacements of an existing bond go through the same
+    // window, so #28's auto-replace now happens only inside it. An
+    // already-bonded phone reconnecting resumes encryption from its stored
+    // bond and never raises this event, so it is not affected. The window
+    // closes on settings_mode_exit() and on reset. Rejecting (0) rather than
+    // ignoring the event gives the phone a clean pairing failure, not a hang.
     case sl_bt_evt_sm_confirm_bonding_id: {
       const sl_bt_evt_sm_confirm_bonding_t *cb = &evt->data.evt_sm_confirm_bonding;
+      uint8_t confirm = app_in_settings_mode() ? 1 : 0;
 #if BLE_DEBUG_SECURITY_LOG
-      printf("BLE SM: confirm_bonding conn=%u bonding_handle=0x%02x (%s, auto-confirming)\r\n",
-             cb->connection, cb->bonding_handle,
-             (cb->bonding_handle == SL_BT_INVALID_BONDING_HANDLE) ? "new bond" : "REPLACING existing bond");
+      printf("BLE SM: confirm_bonding conn=%u (%s) -> %s\r\n",
+             cb->connection,
+             (cb->bonding_handle == SL_BT_INVALID_BONDING_HANDLE) ? "new bond" : "REPLACING existing bond",
+             confirm ? "ACCEPTED (settings mode)" : "REJECTED (not in settings mode)");
 #endif
-      sl_bt_sm_bonding_confirm(cb->connection, 1);
+      sl_bt_sm_bonding_confirm(cb->connection, confirm);
       break;
     }
 
diff --git a/settings_mode.c b/settings_mode.c
index 7ebff55..c030307 100644
--- a/settings_mode.c
+++ b/settings_mode.c
@@ -113,7 +113,8 @@ void settings_mode_cancel_entry(void)
 
 // Leaves settings mode and returns the panel to a clean normal-mode idle.
 // Triggered by a NEW press of BED LIGHTS while in settings mode (see
-// settings_mode_process_outputs()).
+// settings_mode_process_outputs()). Also closes the BLE pairing window, which
+// is simply "in_settings_mode is true" (DECISIONS.md #42).
 void settings_mode_exit(void)
 {
   in_settings_mode = false;
```

### c.4 Docs diffs

Per commit, `git diff <commit>~1 <commit> -- 'docs/*' ':!docs/prompts/*'`, complete (about 250 lines in all, under the 400-line threshold). Two omissions, by rule: (1) `docs/SESSION_LOG.md` is not reproduced (it ships with this file); (2) the prompt file `docs/prompts/PROMPT_V_switch-followups-and-pairing-window.md`, added in `acd410a` (158 lines), is the reviewer's own document, committed unmodified, and is not reproduced. Its blob hash is in d.4 so it can be compared with the copy the reviewer holds.

Commit A `acd410a`:

```diff
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index a7119f8..2216537 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -1803,3 +1803,35 @@ Builds clean (no warnings, no errors) from the command line with
 `BED_BENCH_PROVOKE_RUNAWAY` at 0. **None of this has been run on the bench.**
 DECISIONS.md #40's "recovered swallowed switch change" mechanism is removed;
 whether the swallow hypothesis was right is unproven either way.
+
+### Bench result (2026-10-18)
+
+The user ran `d1f13d0` on the bed (debounce 3 samples, `SWITCH_SAMPLER_DIAG`
+1, the provocation flag hand-flipped to 1 for the runaway runs and back).
+Log: `_03-Software/PuTTY Logs/sampler_bench.log` (1155 lines). Every figure
+below was re-counted from that log, not copied from a summary. **VERIFIED
+(user's bench run).**
+
+- **Provoked runaway, HEAD pair:** caught after 209, 248, 209, 209, 209 ms.
+- **Provoked runaway, ALL MOTORS:** caught after 209, 210, 210, 217, 249 ms.
+  Before the sleep veto this was 271-307 ms against 214-223 ms for the HEAD
+  pair; the gap is gone. That is consistent with the "CPU slept between
+  fail-safe samples" explanation above; it does not prove it was the only
+  cause.
+- Every `FAILSAFE:` line (10) comes directly after a `PROVOKE:` line. No
+  `Unrecognized switch mask`, no `read failed`, no other `failed` line.
+- **Two-button presses:** 67 episodes with two switches down, across 30
+  different pairs. All 67 printed `SW: 2 switches down - presses ignored`; in
+  57 of them one button's press had already been delivered (a brief start)
+  and `Double Press Error` followed and stopped it; the other 10 moved
+  nothing. Every release came through: each `down` line has a matching `up`,
+  and no episode ended with a switch left down.
+- **Bounce tally:** 700 accepted edges (350 down, 350 up); 696 with 0 bounces,
+  4 with 1 bounce (one on 0x1000, three on 0x0004, all on press).
+- **Power-up with UP HEAD held:** the board was reset with 0x8000 down; after
+  the boot lines the only output was `SW: 0x8000 up (bounces 0)`, with no
+  motor-current lines (nothing moved); the next press of UP HEAD moved the
+  bed normally.
+
+The bounce data is the basis for reducing the debounce to 2 samples
+(2026-10-18, user decision): no bounce lasted more than one sample.
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index d311406..ea56c25 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -102,6 +102,58 @@ wired to the correct physical actuators.
 - [ ] After releasing both, a single button press works normally
 - [ ] Repeat with an UP + a DOWN button combination
 
+## 3b. Switch sampler (DECISIONS #41)
+
+The one-switch-reader sampler (`switch_input.c`). Build with
+`SWITCH_SAMPLER_DIAG 1` so the `SW:` lines print, and
+`BED_BENCH_PROVOKE_RUNAWAY 0` except for 3b.5. The bounce count in
+`(bounces N)` is whatever the switch does (0 for a clean contact).
+
+- [ ] 3b.1 Single press and release (e.g. UP HEAD, mask 0x8000), about 1 s
+      hold: `SW: 0x8000 down (bounces 0)` ~20-30 ms after pressing and the
+      motor moves; `SW: 0x8000 up (bounces 0)` ~20-30 ms after releasing and
+      the motor stops. No `FAILSAFE:`, no `Double Press Error`, no
+      `Unrecognized switch mask`, no `SW: ... failed`.
+- [ ] 3b.2 Two buttons pressed together from idle (e.g. UP HEAD + UP LEFT,
+      0x8000 and 0x4000): `SW: 0x4000 down`, then `SW: 0x8000 down` (lowest
+      bit first), then `SW: 2 switches down - presses ignored`. No motor
+      moves, no `Double Press Error`. On release two `SW: 0x.... up` lines and
+      no `FAILSAFE:`.
+- [ ] 3b.3 Second button pressed while the first is held and moving:
+      `SW: 0xXXXX down` for the second, `SW: 2 switches down - presses
+      ignored`, then `Double Press Error` and `  down mask 0xC000, 2 switches
+      down` (for HEAD+LEFT); motors stop. Both releases print `SW: ... up`; no
+      `FAILSAFE:`.
+- [ ] 3b.4 BED LIGHTS held 5 s (mask 0x0008): `SW: 0x0008 down` at the press,
+      the lights toggle and settings mode is entered at 5 s as in section 7,
+      `SW: 0x0008 up` on release. No `Unrecognized switch mask`, no
+      `FAILSAFE:`.
+- [ ] 3b.5 Provoked runaway (`BED_BENCH_PROVOKE_RUNAWAY` hand-flipped to 1,
+      and back to 0 afterwards). Tap UP HEAD: `SW: 0x8000 down`,
+      `SW: 0x8000 up`, ~300 ms later `PROVOKE: energised HEAD pair with
+      nothing commanding - fail-safe should stop this`, then `FAILSAFE: motors
+      energised with nothing commanding - stopped`, then `PROVOKE: fail-safe
+      caught it after N ms`. Tap UP ALL (0x0001): the same with
+      `ALL MOTORS`; N should be close to the HEAD-pair figure. Repeat several
+      times; there must be no `FAILSAFE:` without a `PROVOKE:` just before it.
+- [ ] 3b.6 Power-up with a button held: no `SW:` line and no motion at boot;
+      the later release prints `SW: ... up` and does nothing; the next press
+      works.
+- [ ] 3b.7 Idle: no `SW:` lines at all with no button touched.
+
+**Recorded result, 2026-10-18 (firmware `d1f13d0`, debounce 3 samples,
+log `sampler_bench.log`; full figures in DECISIONS #41 "Bench result"):**
+
+| Item | Result |
+|---|---|
+| 3b.1 | ~700 accepted edges, no `FAILSAFE:` outside a provocation, no `Unrecognized switch mask`, no `read failed` |
+| 3b.2 / 3b.3 | 67 two-button episodes across 30 pairs: all printed `presses ignored`; 57 also printed `Double Press Error` (one press had been delivered first, brief start, then stop); 10 moved nothing; every release came through |
+| Bounce | 696 edges with 0 bounces, 4 with 1 |
+| 3b.5 HEAD | caught after 209, 248, 209, 209, 209 ms |
+| 3b.5 ALL | caught after 209, 210, 210, 217, 249 ms (was 271-307 ms before the sleep veto) |
+| 3b.6 | UP HEAD held through reset: no motion; release printed `SW: 0x8000 up (bounces 0)`; next press worked |
+| 3b.4, 3b.7 | not separately recorded |
+
 ## 4. Limit switch inference
 
 - [ ] Drive one side UP until the actuators reach their physical upper limit:
@@ -202,6 +254,7 @@ pitch, all actuators clear of limits.
 | 1 Power-on | | |
 | 2 Manual control | | |
 | 3 Double-press | | |
+| 3b Switch sampler | | |
 | 4 Limit switches | | |
 | 5 Collision | | |
 | 6 Auto-level | | |
```

Commit B `47db449`:

```diff
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 2216537..5156472 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -1747,7 +1747,8 @@ about 200 ms later (five times). There was no debounce anywhere.
   the double-press check and the fail-safe use the sampler's debounced state.
 - **Sampling session.** Starts on the UI_INT edge flag or, at idle, on a level
   check of the UI_INT pin (so a missed edge still starts one). During a session
-  both boards are read every 10 ms and each bit is debounced (3 identical reads).
+  both boards are read every 10 ms and each bit is debounced (2 identical reads;
+  3 until 2026-10-18 - see "Bench result" below).
   The session ends only when everything is released, stable and no interrupt
   arrived since the last read.
 - **One event per bit, releases first.** Accepted edges are handed to
@@ -1791,7 +1792,8 @@ running continuously and blocking sleep while it is).
 
 ### Behaviour changes to expect
 
-- Release is now seen after the debounce, about 20-30 ms after the contact opens,
+- Release is now seen after the debounce, about 10-20 ms after the contact opens
+  (20-30 ms with the original 3-sample debounce, until 2026-10-18),
   so a move stops that much later than before (+ up to one sample period).
 - Double-press detection is continuous rather than 50 ms after the initial press.
 - `SWITCH_SAMPLER_DIAG` is 1: `SW:` lines print for every accepted edge, with a
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index ea56c25..56da5a6 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -110,8 +110,8 @@ The one-switch-reader sampler (`switch_input.c`). Build with
 `(bounces N)` is whatever the switch does (0 for a clean contact).
 
 - [ ] 3b.1 Single press and release (e.g. UP HEAD, mask 0x8000), about 1 s
-      hold: `SW: 0x8000 down (bounces 0)` ~20-30 ms after pressing and the
-      motor moves; `SW: 0x8000 up (bounces 0)` ~20-30 ms after releasing and
+      hold: `SW: 0x8000 down (bounces 0)` ~10-20 ms after pressing and the
+      motor moves; `SW: 0x8000 up (bounces 0)` ~10-20 ms after releasing and
       the motor stops. No `FAILSAFE:`, no `Double Press Error`, no
       `Unrecognized switch mask`, no `SW: ... failed`.
 - [ ] 3b.2 Two buttons pressed together from idle (e.g. UP HEAD + UP LEFT,
```

Commit C `dd8d069`:

```diff
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index 56da5a6..82db63f 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -227,8 +227,13 @@ pitch, all actuators clear of limits.
 - [ ] ⚡ 7.7 Persistence: power cycle; boot print shows the values stored in
       7.2/7.4/7.5
 - [ ] 7.8 Motor buttons in settings mode do NOT move motors
-- [ ] 7.9 Exit: power cycle returns to normal mode (settings mode has no
-      soft exit — known, REFACTOR_PLAN P3.13)
+- [ ] 7.9 Exit: after releasing the entry hold, press BED_LIGHTS once.
+      "Settings mode: exit" prints; amber stops fast-blinking and shows the
+      under-bed lights state (on if the lights are on, off if not); red,
+      green and blue off; the under-bed lights themselves do not change.
+      Holding that press does not re-enter settings mode, and its release
+      does nothing. Afterwards one press of each button works normally.
+      A power cycle also exits.
 - [ ] ⚡ BLE link stays up (no disconnect) through settings mode entry,
       zero-level store/erase, and sensitivity adjustment
 
```

Commit D `944f19e`:

```diff
diff --git a/docs/BLE_GATT_DESIGN.md b/docs/BLE_GATT_DESIGN.md
index d32f272..fb32dbc 100644
--- a/docs/BLE_GATT_DESIGN.md
+++ b/docs/BLE_GATT_DESIGN.md
@@ -346,9 +346,11 @@ connection to gain control, which reads as a bug.
 no MITM claim; acceptable threat model for a vehicle interior). All
 characteristics require encryption + bonding. **New bonds are accepted only
 during a physical pairing window: while settings mode is active on the
-switch panel** (proposed DECISIONS #19). Outside the window,
-pairing/bonding requests are rejected, so a stranger within radio range
-cannot enroll. Store 8 bonds in NVM3.
+switch panel** (DECISIONS #19, implemented as DECISIONS #42). Outside the
+window, pairing/bonding requests are rejected, so a stranger within radio
+range cannot enroll. Re-pairing a phone that already has a bond is gated the
+same way; a bonded phone reconnecting is not affected. Store 8 bonds in
+NVM3.
 
 **Advertising (fast-connect lever #1).** The board is powered from the van
 battery bank — coin-cell advertising economics do not apply. Advertise
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 5156472..3d52e6e 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -1837,3 +1837,54 @@ below was re-counted from that log, not copied from a summary. **VERIFIED
 
 The bounce data is the basis for reducing the debounce to 2 samples
 (2026-10-18, user decision): no bounce lasted more than one sample.
+
+## 42. Pairing window: new bonds only while settings mode is active (implements #19)
+
+Date: 2026-10-18. User decision. This implements `docs/BLE_GATT_DESIGN.md`
+§8's "proposed DECISIONS #19". Number 19 was already used - by #19 above,
+which recorded this same policy but was never built - so the implementation
+is recorded here, as #42.
+
+### Decision (user decisions - not to be re-opened)
+
+1. **Only new bonds are gated.** An already-bonded phone reconnects freely at
+   any time. A reconnecting bonded phone resumes encryption from its stored
+   bond and does not raise `sl_bt_evt_sm_confirm_bonding`, so the gate never
+   sees it. Evidence: the SDK documents the event as a "new bonding request"
+   (`sl_bt_api.h`, `sl_bt_evt_sm_confirm_bonding`), the existing code already
+   relies on `sm_bonded` not firing for a resumed bond (`assign_role_if_needed`
+   comment), and in the 2026-10-18 bench log (`sampler_bench.log` lines
+   956-961 and 979-984, built with `BLE_DEBUG_SECURITY_LOG` 1) a bonded phone
+   (`connection_opened ... bonding=0x01`) reconnected twice and reached
+   `security_mode=1` with no `confirm_bonding` line.
+2. **Re-pairing a known phone is gated too.** A replacement request
+   (`bonding_handle != SL_BT_INVALID_BONDING_HANDLE`) goes through the same
+   window. **This changes #28's auto-replace from automatic to conditional:**
+   a stale-bond phone is still recoverable without a reflash, but only by
+   putting the bed into settings mode and pairing again. #28's security
+   analysis anticipated exactly this ("once #19 gates all bonding - new and
+   replace alike - behind a physically-opened pairing window, auto-replace is
+   safe by construction").
+3. **The window is "while settings mode is active" - no timer.** It opens when
+   settings mode is entered (BED LIGHTS held 5 s) and closes on the settings
+   mode exit (a new BED LIGHTS press, `settings_mode_exit()`) and on reset
+   (`in_settings_mode` starts false).
+4. **Outside the window, reject** with `sl_bt_sm_bonding_confirm(conn, 0)`,
+   so the phone gets a clean pairing failure rather than a hang.
+
+### Implementation
+
+One decision point, as #28 planned: the `sl_bt_evt_sm_confirm_bonding`
+handler in `ble_bed_service.c` now passes
+`confirm = app_in_settings_mode() ? 1 : 0` to `sl_bt_sm_bonding_confirm()`
+instead of always 1. `sl_bt_sm_set_bondable_mode(1)` and the configuration
+bits are unchanged. Under `BLE_DEBUG_SECURITY_LOG` the handler prints, for
+every request,
+`BLE SM: confirm_bonding conn=N (new bond|REPLACING existing bond) -> ACCEPTED (settings mode)`
+or `... -> REJECTED (not in settings mode)`.
+
+### Status
+
+Builds clean from the command line. **Not run on the bench.** That a rejected
+phone sees a clean failure (rather than retrying or hanging) is the SDK's
+documented behaviour for confirm = 0, not observed here.
```

## d. Raw command output

How this section is organised:

1. **Commands not reproduced, and why.** (d.1 itself holds the session-start commands, verbatim.) Several Bash commands this session were file reads (`cat`/`sed -n`/`grep -n` over source files, docs, and the SDK header, used to read code before editing). Their output is the repository content at `3d96128` (now visible in the diffs) or SDK text; the relevant excerpts are quoted with line numbers in section b and re-run as greps in d.2. This follows the 05 handoff's practice of treating reads as reads. Nothing failed in an unexpected way this session; the only non-zero exits are zero-match greps (exit 1), shown in d.2.
2. **d.2 Evidence commands, re-run at handoff-writing time** against the committed tree (`944f19e`) and the bench log, so the output is raw rather than retyped. The bench-log commands and the replay script were first run earlier in the session with identical results.
3. **d.3 Builds**, verbatim from the files the build helper wrote at the time.
4. **d.4 Git**, transcribed from the tool output at the time, plus re-runs.

No secrets appeared in any output; nothing is redacted. In d.2, `$L` is the bench log path, `$SDKH` is `C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/bluetooth_le_host/inc/sl_bt_api.h` (the `SDK_PATH` in `cmake_gcc/tilt_bed_controller.cmake:5`), and `[exit N]` is the command's exit status.

### d.1 Session-start commands (verbatim)

```
$ ls docs/prompts/ && date && git log --oneline -8 && git status
PROMPT_V_switch-followups-and-pairing-window.md
Sun Oct 18 09:16:24 PDT 2026
3d96128 Handoff 05: one switch reader (sampler) implementation
d1f13d0 One switch reader: sampler, debounce, re-driver refcount, sleep veto (DECISIONS #41)
cb9c7b2 Handoff 04: status check only, no source changes
6b35be8 Handoff 03: note session was interrupted before section (b)
58e2f23 Handoff rules: reword plan/diff requirements (no change in what is required)
1046a14 .gitignore: ignore Studio-generated .projectchecksums and project PDF
ae6f13a Handoff for 3f0d409: fail-safe hardware read, double press, swallowed events
3f0d409 Fail-safe reads motor enables from hardware; double press = two switches down; recover swallowed switch events
On branch main
Your branch is up to date with 'origin/main'.

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/prompts/

no changes added to commit (use "git add" and/or "git commit -a")
```

Toolchain location (to run the build from the command line, as the 05 session did):

```
$ grep -E "^CMAKE_MAKE_PROGRAM|^CMAKE_COMMAND|^CMAKE_C_COMPILER:" cmake_gcc/build/CMakeCache.txt
CMAKE_MAKE_PROGRAM:FILEPATH=C:/Users/adunc/.silabs/slt/installs/conan/p/ninja1a38fc85adcf7/p/ninja.exe
CMAKE_COMMAND:INTERNAL=C:/Users/adunc/.silabs/slt/installs/conan/p/cmakefa35ab0687064/p/bin/cmake.exe
```

### d.2 Evidence commands (re-run)

```
### d.2.1 Bench log (part A)

$ ls -la "$L"; wc -l < "$L"
-rw-r--r-- 1 adunc 197610 35108 Oct 18 08:56 C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/PuTTY Logs/sampler_bench.log
1155
[exit 0]

$ grep -n "PROVOKE\|FAILSAFE" "$L"
1085:PROVOKE: energised HEAD pair with nothing commanding - fail-safe should stop this
1086:FAILSAFE: motors energised with nothing commanding - stopped
1087:PROVOKE: fail-safe caught it after 209 ms
1090:PROVOKE: energised HEAD pair with nothing commanding - fail-safe should stop this
1091:FAILSAFE: motors energised with nothing commanding - stopped
1092:PROVOKE: fail-safe caught it after 248 ms
1095:PROVOKE: energised HEAD pair with nothing commanding - fail-safe should stop this
1096:FAILSAFE: motors energised with nothing commanding - stopped
1097:PROVOKE: fail-safe caught it after 209 ms
1100:PROVOKE: energised HEAD pair with nothing commanding - fail-safe should stop this
1101:FAILSAFE: motors energised with nothing commanding - stopped
1102:PROVOKE: fail-safe caught it after 209 ms
1105:PROVOKE: energised HEAD pair with nothing commanding - fail-safe should stop this
1106:FAILSAFE: motors energised with nothing commanding - stopped
1107:PROVOKE: fail-safe caught it after 209 ms
1110:PROVOKE: energised ALL MOTORS with nothing commanding - fail-safe should stop this
1111:FAILSAFE: motors energised with nothing commanding - stopped
1112:PROVOKE: fail-safe caught it after 209 ms
1115:PROVOKE: energised ALL MOTORS with nothing commanding - fail-safe should stop this
1116:FAILSAFE: motors energised with nothing commanding - stopped
1117:PROVOKE: fail-safe caught it after 210 ms
1120:PROVOKE: energised ALL MOTORS with nothing commanding - fail-safe should stop this
1121:FAILSAFE: motors energised with nothing commanding - stopped
1122:PROVOKE: fail-safe caught it after 210 ms
1125:PROVOKE: energised ALL MOTORS with nothing commanding - fail-safe should stop this
1126:FAILSAFE: motors energised with nothing commanding - stopped
1127:PROVOKE: fail-safe caught it after 217 ms
1130:PROVOKE: energised ALL MOTORS with nothing commanding - fail-safe should stop this
1131:FAILSAFE: motors energised with nothing commanding - stopped
1132:PROVOKE: fail-safe caught it after 249 ms
[exit 0]

$ grep -c -i "Unrecognized\|failed" "$L"
0
[exit 1]

$ grep -c "presses ignored" "$L"; grep -c "Double Press Error" "$L"
67
57
[exit 0]

$ grep -o "(bounces [0-9]*)" "$L" | sort | uniq -c
    696 (bounces 0)
      4 (bounces 1)
[exit 0]

$ grep -n "(bounces [1-9]" "$L"
198:SW: 0x1000 down (bounces 1)
272:SW: 0x0004 down (bounces 1)
282:SW: 0x0004 down (bounces 1)
286:SW: 0x0004 down (bounces 1)
[exit 0]

$ grep -c "^SW: 0x[0-9A-F]* down" "$L"; grep -c "^SW: 0x[0-9A-F]* up" "$L"
350
350
[exit 0]

$ sed -n "1040,1062p" "$L"
Max Current for Motor 3: 752
SW: 0x8000 down (bounces 0)
Max Current for Motor 0: 761
Max Current for Motor 1: 18
Max Current for Motor 2: 9
Max Current for Motor 3: 751
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
SW: 0x8000 up (bounces 0)
SW: 0x8000 down (bounces 0)
Max Current for Motor 0: 786
Max Current for Motor 1: 17
Max Current for Motor 2: 9
Max Current for Motor 3: 795
[exit 0]

$ grep -n "BLE" "$L"
11:BLE SM: configure -> 0x0000
12:BLE SM: store_bonding_configuration -> 0x0000
13:BLE SM: set_bondable_mode(1) -> 0x0000
956:BLE: connection_opened conn=1 bonding=0x01
957:BLE: connection_parameters conn=1 interval=24 security_mode=0
958:BLE: characteristic_status DROPPED conn=1 client_config_flags=0x0001 (matches neither control nor viewer connection)
959:BLE: connection_parameters conn=1 interval=24 security_mode=1
960:BLE: cccd_seed conn=1 bed_status=0x0001 sc=0x0000
961:BLE: connection_parameters conn=1 interval=24 security_mode=1
976:BLE SM: configure -> 0x0000
977:BLE SM: store_bonding_configuration -> 0x0000
978:BLE SM: set_bondable_mode(1) -> 0x0000
979:BLE: connection_opened conn=1 bonding=0x01
980:BLE: connection_parameters conn=1 interval=24 security_mode=0
981:BLE: characteristic_status DROPPED conn=1 client_config_flags=0x0001 (matches neither control nor viewer connection)
982:BLE: connection_parameters conn=1 interval=24 security_mode=1
983:BLE: cccd_seed conn=1 bed_status=0x0001 sc=0x0000
984:BLE: connection_parameters conn=1 interval=24 security_mode=1
1054:BLE SM: configure -> 0x0000
1055:BLE SM: store_bonding_configuration -> 0x0000
1056:BLE SM: set_bondable_mode(1) -> 0x0000
1080:BLE SM: configure -> 0x0000
1081:BLE SM: store_bonding_configuration -> 0x0000
1082:BLE SM: set_bondable_mode(1) -> 0x0000
1143:BLE SM: configure -> 0x0000
1144:BLE SM: store_bonding_configuration -> 0x0000
1145:BLE SM: set_bondable_mode(1) -> 0x0000
[exit 0]

$ python -I <replay script> sampler_bench.log   # replays SW: edges, counts two-button episodes (script reproduced in d.2.1 note)
two-button episodes: 67
Counter({'ign+dpe': 57, 'ign': 10})
episodes with neither: []
distinct pairs: 30
anomalies: [(1046, 'reset with down=0x8000'), (1057, 'up without down (0x8000)')]
open episode at end: None down at end 0x0000
[exit 0]

### d.2.2 Code greps (on the committed tree, 944f19e)

$ grep -n -E "switch_pressed|app_ui_interrupt|switch_sampler_init|switch_input_tick|switch_sampler_active|switch_sampler_down|switch_state_fresh|motion_failsafe_tick|switch_input" bed_actions.c
227: * The switch path gets its physical outputs from switch_input.c's
228: * process_outputs(), which is driven by app_ui_interrupt + switch_pressed -
[exit 0]

$ grep -n -i -E "20-30|3 identical|DEBOUNCE_SAMPLES|>=20 ms" *.c *.h docs/DECISIONS.md docs/REGRESSION.md
switch_input.c:54:#define SWITCH_DEBOUNCE_SAMPLES     2   // 2 identical reads = >=10 ms stable
switch_input.c:416:        if (pending_count[b] >= SWITCH_DEBOUNCE_SAMPLES) {
docs/DECISIONS.md:1796:  (20-30 ms with the original 3-sample debounce, until 2026-10-18),
[exit 0]

$ grep -n "= SWITCH_BED_LIGHT_VALUE\|in_settings_mode = " *.c
bed_actions.c:199:  active_switch = SWITCH_BED_LIGHT_VALUE;
bed_status.c:144:  out->in_settings_mode = app_in_settings_mode();
settings_mode.c:55:bool in_settings_mode = false;
settings_mode.c:120:  in_settings_mode = false;
settings_mode.c:247:      else if (active_switch == SWITCH_BED_LIGHT_VALUE){
settings_mode.c:263:  if ((switch_pressed == true) && (active_switch == SWITCH_BED_LIGHT_VALUE) && (!in_settings_mode)){
settings_mode.c:271:        in_settings_mode = true;
switch_input.c:558:          if ((switch_pressed) && (switch_driving == SWITCH_BED_LIGHT_VALUE)){
switch_input.c:566:          } else if ((!switch_pressed) && (switch_driving == SWITCH_BED_LIGHT_VALUE)){
switch_input.c:667:                switch_held = SWITCH_BED_LIGHT_VALUE;
switch_input.c:669:                    switch_driving = SWITCH_BED_LIGHT_VALUE;
switch_input.c:671:            } else if ((!switch_pressed) && (switch_held == SWITCH_BED_LIGHT_VALUE)){
[exit 0]

$ grep -n "app_release_active_switch\|bed_lights_set" bed_actions.c ble_bed_service.c switch_input.c
bed_actions.c:284:  app_release_active_switch();
bed_actions.c:299:  app_release_active_switch();
bed_actions.c:307:void app_release_active_switch(void)
ble_bed_service.c:665:      if (!bed_lights_set(BED_SOURCE_APP, arg0 == 1)) {
switch_input.c:668:                if (bed_lights_set(BED_SOURCE_SWITCHES, !app_lights_on())) {
[exit 0]

$ grep -n "in_settings_mode" bed_status.c; grep -n "flags |= 0x02\|static bool status_flags_changed\|memcmp(new_packed" ble_bed_service.c
15:#include "settings_mode.h" // in_settings_mode - same
116:  if (auto_level_active || in_settings_mode) {
144:  out->in_settings_mode = app_in_settings_mode();
81:  if (in->in_settings_mode) flags |= 0x02;
791:static bool status_flags_changed(const uint8_t *new_packed, const uint8_t *old_packed)
793:  if (memcmp(new_packed, old_packed, 4) != 0) { // flags, active_source, command, command_detail
[exit 0]

$ grep -n "any_collision = true" *.c
motor_current_functions.c:306:          any_collision = true;
motor_current_functions.c:322:          any_collision = true;
motor_current_functions.c:366:            any_collision = true;
[exit 0]

$ grep -n "define BED_BENCH_PROVOKE_RUNAWAY\|define BLE_BENCH_WIPE_BONDS\|define BLE_DEBUG_SECURITY_LOG\|define SWITCH_SAMPLER_DIAG\|define SWITCH_DEBOUNCE_SAMPLES" switch_input.c ble_bed_service.c
switch_input.c:54:#define SWITCH_DEBOUNCE_SAMPLES     2   // 2 identical reads = >=10 ms stable
switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
switch_input.c:98:#define BED_BENCH_PROVOKE_RUNAWAY 0
ble_bed_service.c:52:#define BLE_DEBUG_SECURITY_LOG 1
ble_bed_service.c:69:#define BLE_BENCH_WIPE_BONDS 0
[exit 0]

$ grep -n "BONDING_REQUEST_REQUIRED\|store_bonding_configuration(\|set_bondable_mode(\|bonding_confirm(\|app_in_settings_mode" ble_bed_service.c
545:  if (app_in_settings_mode()) {
874:      // Bit 3 (BONDING_REQUEST_REQUIRED) is set so sl_bt_evt_sm_confirm_bonding
883:                               | SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED,
890:      sc = sl_bt_sm_store_bonding_configuration(BLE_BED_SERVICE_MAX_BONDS, 0);
898:      sc = sl_bt_sm_set_bondable_mode(1);
900:      printf("BLE SM: set_bondable_mode(1) -> 0x%04x\r\n", (unsigned)sc);
990:    // (BONDING_REQUEST_REQUIRED) is set (DECISIONS.md #28). bonding_handle
1008:      uint8_t confirm = app_in_settings_mode() ? 1 : 0;
1015:      sl_bt_sm_bonding_confirm(cb->connection, confirm);
[exit 0]

$ grep -n "CMD_OPCODE_LIGHTS_SET\|REJECTED_SETTINGS_MODE);\|stop_all_pending) {\|app_stop_all();" ble_bed_service.c
165:#define CMD_OPCODE_LIGHTS_SET        0x05
457:  app_stop_all();
478:  if (stop_all_pending) {
487:    app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
546:    send_command_result(conn, seq, CMD_RESULT_REJECTED_SETTINGS_MODE);
660:    case CMD_OPCODE_LIGHTS_SET:
835:    app_stop_all();
[exit 0]

$ grep -rln "switch_input\|settings_mode" test
[exit 1]

### d.2.3 SDK header (sl_bt_api.h, SiSDK 2026.6.0 install used by the build)

$ grep -n "Indicates a user request to display that the new bonding request" "$SDKH"
11930: * @brief Indicates a user request to display that the new bonding request is
[exit 0]

$ grep -n -A2 "Bonding requests need to be confirmed" "$SDKH"
11759:/** Bonding requests need to be confirmed. Received bonding requests are
11760- * notified by @ref sl_bt_evt_sm_confirm_bonding. */
11761-#define SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED             0x8
--
11985: *     - <b>1:</b> Bonding requests need to be confirmed. Received bonding
11986- *       requests are notified by @ref sl_bt_evt_sm_confirm_bonding
11987- *
[exit 0]

$ grep -n -B1 -A1 "If database is full, pairing will be performed but no bonding" "$SDKH"
12070- *   Bonding policy. Values:
12071: *     - <b>0:</b> If database is full, pairing will be performed but no bonding
12072- *       is stored
[exit 0]

$ grep -n -A1 "0:</b> Reject" "$SDKH"
12169: *     - <b>0:</b> Reject
12170- *     - <b>1:</b> Accept confirm value
--
12189: *     - <b>0:</b> Reject
12190- *     - <b>1:</b> Accept bonding request
[exit 0]
```

### d.3 Builds (verbatim)

All builds used a helper script in the session scratchpad, which puts the ninja and cmake directories above on `PATH`, runs `cmake --build --preset default_config` in `cmake_gcc/`, writes all output plus `exit N` to a file, prints it, then prints `grep -n -i 'warning\|error'` over it (empty = none found). Each build below is the exact file content followed by that grep's output.

Baseline, before any edit (HEAD `3d96128`):

```
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
```

After part A (docs only, so nothing to rebuild):

```
ninja: no work to do.
exit 0
--- grep -n -i 'warning\|error' ---
```

After part B (before commit `47db449`):

```
[1/3] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[2/3] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[3/3] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

After part C (before commit `dd8d069`):

```
[1/8] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app_bm.c.obj
[2/8] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_status.c.obj
[3/8] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[4/8] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/settings_mode.c.obj
[5/8] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[6/8] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[7/8] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/autogen/sl_event_handler.c.obj
[8/8] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

After part D (before commit `944f19e`; this is the image currently in `cmake_gcc/build/`):

```
[1/3] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/settings_mode.c.obj
[2/3] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/3] Linking C executable base\tilt_bed_controller.out
exit 0
--- grep -n -i 'warning\|error' ---
```

Per-commit build output tails are shown in full above (they are short). There were no warnings in any build.

### d.4 Git

Commit A (the CRLF lines are git's line-ending warnings, printed on `git add`):

```
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/prompts/PROMPT_V_switch-followups-and-pairing-window.md', LF will be replaced by CRLF the next time Git touches it
acd410a Record 2026-10-18 switch sampler bench result (docs only)
 M .projectlinkstore
```

Commit B:

```
warning: in the working copy of 'bed_actions.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
47db449 Switch follow-ups: debounce 2 samples, double press releases latch, drop unused include
```

Commit C:

```
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'app.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'settings_mode.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'settings_mode.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'switch_input.h', LF will be replaced by CRLF the next time Git touches it
dd8d069 Settings mode: exit on a new BED LIGHTS press
```

Commit D, then the log/status/flag check run right after it:

```
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'settings_mode.c', LF will be replaced by CRLF the next time Git touches it
944f19e Pairing window: new bonds only while settings mode is active (DECISIONS #42)
dd8d069 Settings mode: exit on a new BED LIGHTS press
47db449 Switch follow-ups: debounce 2 samples, double press releases latch, drop unused include
acd410a Record 2026-10-18 switch sampler bench result (docs only)
3d96128 Handoff 05: one switch reader (sampler) implementation
d1f13d0 One switch reader: sampler, debounce, re-driver refcount, sleep veto (DECISIONS #41)
On branch main
Your branch is ahead of 'origin/main' by 4 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
switch_input.c:54:#define SWITCH_DEBOUNCE_SAMPLES     2   // 2 identical reads = >=10 ms stable
switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
switch_input.c:98:#define BED_BENCH_PROVOKE_RUNAWAY 0
ble_bed_service.c:52:#define BLE_DEBUG_SECURITY_LOG 1
ble_bed_service.c:69:#define BLE_BENCH_WIPE_BONDS 0
```

Re-run at handoff time:

```
$ git log --oneline -6
944f19e Pairing window: new bonds only while settings mode is active (DECISIONS #42)
dd8d069 Settings mode: exit on a new BED LIGHTS press
47db449 Switch follow-ups: debounce 2 samples, double press releases latch, drop unused include
acd410a Record 2026-10-18 switch sampler bench result (docs only)
3d96128 Handoff 05: one switch reader (sampler) implementation
d1f13d0 One switch reader: sampler, debounce, re-driver refcount, sleep veto (DECISIONS #41)
[exit 0]

$ git show --stat --format="%h %s" acd410a 47db449 dd8d069 944f19e
acd410a Record 2026-10-18 switch sampler bench result (docs only)

 docs/DECISIONS.md                                  |  32 +++++
 docs/REGRESSION.md                                 |  53 +++++++
 ...PROMPT_V_switch-followups-and-pairing-window.md | 158 +++++++++++++++++++++
 3 files changed, 243 insertions(+)
47db449 Switch follow-ups: debounce 2 samples, double press releases latch, drop unused include

 bed_actions.c      | 1 -
 docs/DECISIONS.md  | 6 ++++--
 docs/REGRESSION.md | 4 ++--
 switch_input.c     | 8 +++++++-
 4 files changed, 13 insertions(+), 6 deletions(-)
dd8d069 Settings mode: exit on a new BED LIGHTS press

 app.c              | 15 +++++++++++++++
 app.h              |  3 ++-
 docs/REGRESSION.md |  9 +++++++--
 settings_mode.c    | 49 ++++++++++++++++++++++++++++++++++++++++++++++++-
 settings_mode.h    |  8 ++++++++
 switch_input.c     | 10 ++++++++++
 switch_input.h     |  4 ++++
 7 files changed, 94 insertions(+), 4 deletions(-)
944f19e Pairing window: new bonds only while settings mode is active (DECISIONS #42)

 ble_bed_service.c       | 38 +++++++++++++++++++++---------------
 docs/BLE_GATT_DESIGN.md |  8 +++++---
 docs/DECISIONS.md       | 51 +++++++++++++++++++++++++++++++++++++++++++++++++
 settings_mode.c         |  3 ++-
 4 files changed, 81 insertions(+), 19 deletions(-)
[exit 0]

$ git diff --stat 3d96128 944f19e
 app.c                                              |  15 ++
 app.h                                              |   3 +-
 bed_actions.c                                      |   1 -
 ble_bed_service.c                                  |  38 +++--
 docs/BLE_GATT_DESIGN.md                            |   8 +-
 docs/DECISIONS.md                                  |  89 +++++++++++-
 docs/REGRESSION.md                                 |  62 +++++++-
 ...PROMPT_V_switch-followups-and-pairing-window.md | 158 +++++++++++++++++++++
 settings_mode.c                                    |  50 ++++++-
 settings_mode.h                                    |   8 ++
 switch_input.c                                     |  18 ++-
 switch_input.h                                     |   4 +
 12 files changed, 428 insertions(+), 26 deletions(-)
[exit 0]

$ git rev-parse 944f19e:docs/prompts/PROMPT_V_switch-followups-and-pairing-window.md
509df87eaf2942d1d872c935bf2164166b4302fd
[exit 0]

$ git hash-object docs/prompts/PROMPT_V_switch-followups-and-pairing-window.md
509df87eaf2942d1d872c935bf2164166b4302fd
[exit 0]

$ git rev-parse HEAD origin/main
944f19ef567db516499d99305663d2695dee9071
3d96128c49a8b0facc65dff41eb4bb00250c8d62
[exit 0]
```

## e. Claims: VERIFIED vs ASSERTED

### VERIFIED (evidence attached)

1. **The 2026-10-18 bench figures (part A), as listed in b.1, including that the 57 Double Press Error episodes also printed "presses ignored".** Evidence: d.2.1 (greps, uniq tally, `sed -n 1040,1062p`, replay script output).
2. **A bonded phone reconnecting does not raise `confirm_bonding`** (on the previous firmware; that path is unchanged). Evidence: d.2.1 `grep -n "BLE"`, lines 956-961 and 979-984; SDK text `sl_bt_api.h:11930`, `:11759-11761` (d.2.3).
3. **Every work commit builds with exit 0 and no warnings or errors.** Evidence: d.3.
4. **`switch_held` stays `SWITCH_BED_LIGHT_VALUE` for the whole of settings mode** (so the exit must clear it). Evidence: code path in b.3, `switch_input.c:665-677`, `:717-719`, `:592-593`, `settings_mode.c:238-250` (diffs in c.2 show the surrounding code).
5. **The exit press cannot re-enter settings mode, and its release does nothing.** Evidence: the only writer of `active_switch = SWITCH_BED_LIGHT_VALUE` is `bed_actions.c:199` (d.2.2 grep); the BLE caller releases in the same call (`ble_bed_service.c:665-669`, `bed_actions.c:284`, d.2.2); release conditions at `switch_input.c:566` and `:671` (d.2.2 grep).
6. **The Bed Status settings flag follows the exit with no further change.** Evidence: `bed_status.c:144`, `ble_bed_service.c:81`, `:791-793` (d.2.2).
7. **`bed_actions.c` uses nothing from `switch_input.h`.** Evidence: d.2.2 grep (two comment lines only) plus the clean build B (d.3).
8. **No `20-30 ms` / `3 identical` statement is left in `.c/.h`, DECISIONS or REGRESSION apart from the deliberate historical phrase.** Evidence: d.2.2 grep.
9. **Store policy 0 means "pairing will be performed but no bonding is stored" when full, so the comment at `ble_bed_service.c:888-889` is wrong.** Evidence: `sl_bt_api.h:12070-12072` (d.2.3).
10. **Bench flags are off in the committed tree** (`BED_BENCH_PROVOKE_RUNAWAY 0`, `BLE_BENCH_WIPE_BONDS 0`). Evidence: d.2.2 flag grep.
11. **BLE motion commands and LIGHTS_SET are refused in settings mode; STOP_ALL is not, and calls `app_stop_all()`.** Evidence: d.2.2 grep (`:478`, `:487`, `:546`, `:660` in order).
12. **The host unit test is unaffected** (it references neither `switch_input` nor `settings_mode`). Evidence: d.2.2 `grep -rln ... test` (no match).

### ASSERTED (reasoned, builds, but nothing observed it)

1. Debounce 2 gives 10-20 ms acceptance and still rejects single-sample bounces (arithmetic from the code; the 2026-10-18 data was taken at 3).
2. The whole settings-mode exit sequence of E2/E3 on hardware: the amber blink stops, LED states, lights untouched, all buttons alive afterwards.
3. The pairing gate on hardware: REJECTED/ACCEPTED as designed; a rejected phone sees a clean pairing failure (SDK doc says reject; the phone's reaction is not observed); the bed's stored bond survives a rejected replacement.
4. B2 makes a difference in practice. It only matters after a switch/app conflict set the latch; the reasoning is in b.2.
5. That clearing `LED_timer_timeout` on exit cannot disturb a collision blink (reasoned: no motor driven in settings mode).
6. Everything in b.5: the 9th-phone behaviour beyond the SDK sentence, the STOP_ALL stale-timer consequence, the loop-rate effect during capture, NVM3 serialisation.
7. The E-section reason codes and exact BLE sequences in (b)-(d), which were not observed for this firmware.

## f. QUESTIONS FOR REVIEW

1. **Print format.** The new `confirm_bonding` line follows the prompt's format exactly and so drops the raw `bonding_handle=0x%02x` that the old line had. The handle identifies *which* stored bond a replacement would overwrite, which helps when debugging. Keep it out, or add it back (e.g. `... conn=%u handle=0x%02x (%s) -> ...`)?
2. **Rejected phones may stay connected.** After a REJECTED pairing the firmware does nothing more. If the phone does not disconnect itself, it holds one of the two connection slots while unable to read anything. Should the firmware close a connection whose pairing it rejected (e.g. on `sm_bonding_failed` when outside the window)? Not implemented.
3. **8-bond-full** (b.5 (i)): the comment at `ble_bed_service.c:888-889` contradicts the SDK (policy 0 pairs without storing a bond; it does not reject). Fix the comment now? Change to policy 2 (overwrite least-recently-used), which is now safe because new bonds need physical access?
4. **STOP_ALL in settings mode** (b.5 (iii)): it blanks the LEDs and clears `active_switch` but neither exits settings mode nor stops the settings timers. Make STOP_ALL call `settings_mode_exit()` while in settings mode, or stop the timers only, or leave it?
5. **No window timeout.** Decision 3 says no timer, implemented as such. Flagging only: a forgotten window stays open until a press or a reset (b.5 (ii)).
6. **REGRESSION.md 7.9** was rewritten to describe the new exit (it said there was none). Not literally in the prompt; done because the old step would now test the wrong thing. OK? REFACTOR_PLAN P3.13 and the two REGRESSION_RECORD files were left unedited.
7. **A BLE LIGHTS_SET that arrives while the exit press is still held** cannot re-enter settings mode (the BLE path releases `active_switch` in the same call), but in general `settings_mode_tick()`'s entry test uses the global `switch_pressed` (the last delivered edge), not "BED LIGHTS is physically down". It is safe today only because of that same-call release. Worth tightening the entry test to `switch_sampler_down() & SWITCH_BED_LIGHTS`? Not changed.
8. **BED LIGHTS pressed while another switch is down in settings mode** is ignored (two down) and not re-delivered when the other is released, so the user must release and press BED LIGHTS again to exit. Acceptable?
9. **Where the prompt file was committed.** "Commit this prompt file together with the work it describes": I put it in commit A (the first of the four), so reverting B, C or D alone never removes it. Fine, or should it have gone with D?
10. **DECISIONS #19 was not edited.** #42 refers back to it and #19 still reads as a bare policy statement. Add a forward pointer to #19 ("implemented as #42"), or keep #19 untouched as the log has done for #27/#28?
11. **Pre-existing stale-timeout race in settings mode**: UP_ALL/DOWN_ALL release stops `handle_2000ms_timer` but does not clear `timeout_2000ms`, so a timeout that fires between `settings_mode_tick()` and the release delivery in the same pass survives into the next hold (an early sensitivity step). The exit now clears it; the release paths do not. Out of scope here; fix in a later change?
12. **CLAUDE.md is stale** on "No BLE glue in this phase" (Stage B/C exist per MIGRATION_STATUS and DECISIONS #34). Update CLAUDE.md? Not touched.

## g. NOT DONE

- **No bench testing.** I do not build-and-flash to hardware (CLAUDE.md). Everything in B, C and D is ASSERTED until the E steps are run.
- **b.5's three questions are answered, not implemented**, as the prompt instructed.
- **`SWITCH_SAMPLER_DIAG` is still 1** (pre-ship should be 0); not asked to change it.
- **`docs/SWITCH_SAMPLER_DESIGN.md` still says 3 samples / 20-30 ms.** It is the design record, not a comment or a DECISIONS #41 statement, so it was outside B1's list.
- **`docs/REFACTOR_PLAN.md` P3.13 ("Settings mode has no exit") not updated**, and `MIGRATION_STATUS.md` not updated (neither was asked for).
- **`.projectlinkstore` not committed** (as instructed).
- **The push** happens after this file is committed, so a successful result is not in this file. If it fails, the raw error is appended here in a follow-up commit.
