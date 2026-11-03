# fw_HANDOFF_2026-11-03_02 — Prompt AI: latch refinements, reset cause, AUTO_LEVEL_SETTLING bit, live idle tilt, self-clearing LEVELED

Firmware repo session of 2026-11-03. System date checked with `date` at the
start (`Tue Nov  3 08:35:07 PST 2026`, d.0 P1) and again before naming this
file (`Tue Nov  3 08:59:57 PST 2026`, d.2 #39; `09:02:42`, #40). `_01` for this
date already existed (the Prompt AH handoff), so this is `_02`. Prompt:
`docs/prompts/PROMPT_AI_latch-refinements-live-tilt.md` (committed in
`8e1242d`).

## a. Header

| | |
|---|---|
| Repo | `tilt_bed_controller` (firmware) |
| Branch | `main` |
| HEAD before | `cd2a148` (Handoff 2026-11-03 01: Prompt AH), level with `origin/main` after `git fetch` - as the prompt expects (d.0 P1-P2) |
| HEAD after the work | `05c4c2f` - five work commits, one per part: `8e1242d` (A), `0d45fff` (B), `3e9349f` (C), `8996259` (D), `05c4c2f` (E) |
| After that | this handoff and the session's SESSION_LOG entry, committed together, then `git push` (normal push, no force) |
| Builds | from the command line (the prompt asks for it, E3): after every part and a final flag matrix - every build exit 0 with no warning/error line (d.2 #12-#15, #20-#22, #25-#26, #31, #34, #36) |
| Host test | `0 check(s) failed` (#37) |
| Bench | **nothing ran on a bed in this session** |

`git status` at the end of the work (after `05c4c2f` and the SESSION_LOG
entry, before this file was written; d.2 #40):

```
On branch main
Your branch is ahead of 'origin/main' by 5 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

no changes added to commit (use "git add" and/or "git commit -a")
```

`.projectlinkstore` is the Studio file the prompt says never to commit; it is
not committed. `docs/SESSION_LOG.md` is committed with this file.

**What was asked and what was done**

| Part | Asked | Done |
|---|---|---|
| A1 | Collision wins in the start refusal; confirm `latch_app_starts()` allows STOP->COLLISION, never the reverse | `8e1242d`, `ble_bed_service.c:871-886`. b.A.1 |
| A2 | `service_app_commanding()` also at the top of `ble_bed_service_tick()`; keep the existing call; state every behaviour change incl. #36 and the collision latch | `:1174` (existing call `:1193`). b.A.2 - including one narrow new #36 case (f.1) |
| A3 | STOP_ALL latches only when `app_commanding` or a latch is set; restate AH A6.3, A6.5 and any other changed case; the two prints | `:746-786`. b.A.3 (all six A6 cases restated) |
| A4 | Reset cause at boot via the SDK API, header cited, cleared if required | `app.c:388-428`, `:445-456`; `sl_hal_emu.h`. b.A.4 |
| B1-B3 | Bit 5 AUTO_LEVEL_SETTLING, docs, print | `0d45fff`. b.B |
| C1-C7 | Idle sampler, ms filter, 5 Hz, `tilt_fresh`, sleep, diag, DECISIONS | `3e9349f`, DECISIONS #50. b.C |
| D1-D3 | LEVELED self-clear | `8996259`. b.D |
| E1-E5 | REGRESSION, rulings, builds, expected lines, handoff | `05c4c2f` (E1); SESSION_LOG (E2); b.E (E3); b.F (E4); this file (E5) |

---

## b. Designs and analysis

Line references are to `05c4c2f` (= HEAD for every source file) unless
marked. DECISIONS #49's amendment cites line numbers as they were at
`8e1242d` (three lines lower for `ble_bed_service.c` from `:86` on, because
`0d45fff` and `3e9349f` added lines above). Every claim is VERIFIED (code
read, or a command output in section d) or ASSERTED; section e splits them.
Nothing ran on a bed.

### b.A Part A - latch refinements and reset cause (`8e1242d`; DECISIONS #49 "Amended by Prompt AI")

Terms as in the AH handoff: `app_start_latch` (NONE / COLLISION / STOP),
`app_commanding` + `app_command_deadline_tick` (the "silence timer": set by
`note_app_commanding()`, 300 ms = `MOVE_WATCHDOG_MS`),
`note_app_stopped_commanding(reason)` (the app's release: clears
`app_commanding`, releases the app's half of #36 in bed_control, clears the
start latch).

#### b.A.1 A1 - collision wins

The refusal in `process_pending_command()` (`ble_bed_service.c:871-886`):

```c
if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
    && (app_start_latch != APP_LATCH_NONE || any_collision)) {
  if (app_start_latch == APP_LATCH_COLLISION || any_collision) {   // was: == APP_LATCH_STOP first
    latch_app_starts(APP_LATCH_COLLISION);
    send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);     // 0x06
  } else {                                                         // latch STOP, no collision
    latch_app_starts(APP_LATCH_STOP);
    send_command_result(conn, seq, CMD_RESULT_STOP_LATCHED);       // 0x07
  }
  /* debug refused count */ return;
}
```

| Latch before | `any_collision` | Before (AH) | After (AI) | Latch after |
|---|---|---|---|---|
| NONE | 0 | not refused here | unchanged | NONE |
| NONE | 1 | 0x06 | 0x06 | COLLISION |
| COLLISION | 0 or 1 | 0x06 | 0x06 | COLLISION |
| STOP | 0 | 0x07 | 0x07 | STOP |
| **STOP** | **1** | **0x07, stays STOP** | **0x06, upgraded to COLLISION** | COLLISION |

**`latch_app_starts()` (`:597-603`)**, unchanged:

```c
if (app_start_latch != APP_LATCH_COLLISION) { app_start_latch = reason; }
note_app_commanding();
```

Its only guard refuses to overwrite COLLISION. So: NONE -> either reason;
STOP -> COLLISION happens (STOP != COLLISION, the assignment runs); COLLISION
-> STOP never happens (guarded). VERIFIED by reading the function (#18).
Callers after A: the trip (COLLISION), STOP_ALL (STOP, now conditional,
b.A.3), the refusal (COLLISION when a collision is involved, else STOP). The
only caller that upgrades is the refusal's first branch, only from STOP.
Comments above the enum (`:338-356`) and above the helper (`:589-596`) were
updated to say so.

Effect on the release print: after an upgrade, `note_app_stopped_commanding()`
records `set_reason = "collision"`, so the bench sees `BLE: start latch
(collision) cleared (...)` although a STOP set the latch first (REGRESSION
5b.8).

#### b.A.2 A2 - the silence release judged first

`ble_bed_service_tick()` (`:1168-1194`) now begins:

```c
service_app_commanding();        // NEW: judged first (Prompt AI A2)
if (disconnect_stop_pending) { ... note_app_stopped_commanding("disconnect"); app_stop_all(); }
process_pending_command();
service_move_watchdog();
service_app_commanding();        // existing call, kept
```

`service_app_commanding()` (`:629-638`, unchanged): if `app_commanding` and
the tick count has reached `app_command_deadline_tick`, call
`note_app_stopped_commanding("silence")`. Calling it twice per pass is safe:
the second call returns at once unless a command processed in between set
`app_commanding` again, and then its deadline is 300 ms ahead.

"At the top" was taken literally: the new call is the first statement,
before the disconnect stop as well as before `process_pending_command()`.

**Every behaviour this changes** (ASSERTED from code; the AH handoff f.2
described the before-state):

1. **The general rule.** A MOVE_START / AUTO_LEVEL_START (or STOP_ALL, b.A.3)
   processed 300 ms or more after the app's last start, refusal or latching
   STOP_ALL now always meets the state *after* the silence release: start
   latch NONE, `app_commanding` false, the app's half of #36 released. Before,
   that held only if some main-loop pass ran between the deadline and the
   command's own pass; in the command's own pass the command came first, so it
   met the stale state.
2. **STOP latch.** A press 300 ms or more after the last latching event is a
   normal start (AH A6.3 (b) no longer depends on a pass in between). Before:
   refused 0x07 and re-latched for its whole hold if no pass had run.
3. **COLLISION latch.** Such a press is cleared by silence first, then still
   refused 0x06 and re-latched while the collision sequence runs (`||
   any_collision`); after the sequence it is a normal start. Before: refused
   and latched if no pass had run, regardless of the sequence.
4. **#36 two-source inhibit, the general case.** A held repeat that arrives
   300 ms or more after the previous one (a link stall) is a new press: the
   silence release calls `bed_note_source_released(BED_SOURCE_APP)`
   (`bed_control.c:51-54`), which clears `conflict_inhibit` if the switches
   have also released (`:44-48`), and the repeat starts the bed. This was
   already so whenever a pass ran in the gap; AH f.2 named it as the
   trade-off.
5. **#36, one new case - narrow, safety-relevant, reported (f.1), not
   changed.** During an *accepted* app hold (watchdog armed,
   `app_move_active` or `app_auto_level_active` true), suppose the link
   stalls and a repeat arrives 300 ms or more after the previous one, and is
   processed in the **first** pass after the silence deadline, before
   `service_move_watchdog()` has acted on the watchdog's expiry (the
   watchdog timer is started a few microseconds after the deadline is
   computed, both 300 ms from the same repeat). New order: the top call
   releases the app (`source_commanding[BED_SOURCE_APP]` = false in
   bed_control); then the repeat is an *identical* repeat - `app_move_active`
   still true - so it only refreshes the watchdog (`move_watchdog_arm()` also
   clears a pending `move_watchdog_expired` and restarts the timer, `:554-558`) and **the move
   continues while bed_control believes the app released**.
   `source_commanding[APP]` is set again only by `bed_move_start()` /
   `bed_auto_level_start()` (`bed_control.c:99`, `:127`), which an identical
   repeat does not call. Consequence for the rest of that hold: if the wall
   switch is pressed, bed_control force-stops and latches the inhibit as
   usual (`bed_control.c:104-107`), and the app's repeats are refused 0x01;
   but when the **switch** is released, both `source_commanding[]` entries
   are false, the inhibit clears (`:44-48`), and the app's next held repeat
   starts the bed - #12's "stop then hand over" for that one hold, which #36
   exists to prevent. Before A2 the repeat was processed first and pushed the
   deadline, so the app stayed "commanding". The window is about one main-loop
   pass at the 300 ms point of a stall (passes run every few ms during an app
   hold, #45). Proposed one-line fix in f.1.
6. **Disconnect stop in the same pass as an expired deadline.** The latch
   print says `silence` instead of `disconnect`; the stop is identical (the
   disconnect block still runs; its own `note_app_stopped_commanding()` is a
   second release, a no-op in bed_control by the AG b.1.1 argument).
7. **Watchdog expiry.** In the expiry pass the silence release now runs
   before the watchdog stop instead of after; both call
   `note_app_stopped_commanding()`; the stop and the WATCHDOG_STOP result are
   the same. No latch can be set while the watchdog is armed, so no print
   changes.
8. **With a phone subscribed, Part C makes the pass-dependency mostly moot**:
   the idle sampler brings the main loop round every 100 ms at rest, so a pass
   almost always runs between a deadline and a later press anyway. A2 still
   matters when no phone is subscribed to Bed Status (a control phone can
   write Commands without subscribing).

#### b.A.3 A3 - STOP_ALL latches only when the app is commanding

STOP_ALL branch of `process_pending_command()` (`:746-786`):

```c
if (stop_all_pending) {
  ... seq, conn, stop_all_pending = false;
  bool latch_stop = app_commanding || (app_start_latch != APP_LATCH_NONE);   // judged first
  move_watchdog_cancel(); app_move_active = false; app_auto_level_active = false;
  bed_note_source_released(BED_SOURCE_APP);
  app_stop_all();
  send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
  if (latch_stop) { latch_app_starts(APP_LATCH_STOP); }        // COLLISION stays COLLISION
#if BLE_DEBUG_WATCHDOG_LOG
  if (latch_stop) printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn, collision ? ", kept as collision" : "");
  else            printf("BLE: STOP_ALL (no app hold, no latch)\r\n");
#endif
}
```

- `latch_stop` is taken before anything in the branch can change
  `app_commanding` (none of the calls that follow touch it before the
  latch), and after A2's top-of-tick silence check, so "commanding at that
  moment" means: an app MOVE_START / AUTO_LEVEL_START (accepted or refused)
  or a latching STOP_ALL within the last 300 ms, with no MOVE_STOP /
  AUTO_LEVEL_STOP / disconnect release since.
- The second term is implied by the first (whenever a latch is set,
  `app_commanding` is true - the AH b.A.2 invariant); it is written out
  because the prompt names it.
- Everything else STOP_ALL does is unchanged, for both roles.
- The prints are exactly the prompt's text; the no-latch line has no
  `conn=` (the prompt's text has none; f.9).

**AH A6 cases, restated for the new rule** (from code; ASSERTED):

| AH case | Is the app commanding at the STOP_ALL? | Result now | Changed? |
|---|---|---|---|
| A6.1 App holding HEAD up, STOP from the same phone, finger kept down, release, fresh press | Yes - repeats every 100 ms | Exactly AH's table: `STOP_ALL latch set (conn=N)`, `hold end ...`, 0x07 per repeat, `start latch (stop) cleared (stop), K repeats refused` at the release, fresh press 0x00 | No |
| A6.2 App sends MOVE_STOP right after its STOP_ALL | STOP_ALL first: yes -> latch, then MOVE_STOP clears it (`cleared (stop), 0 repeats refused`) | As AH | No |
| A6.2 variant: MOVE_STOP lands *before* the STOP_ALL, in a different pass | No - MOVE_STOP cleared `app_commanding` | `BLE: STOP_ALL (no app hold, no latch)`; nothing latched; a press right after is a normal start. Before: latched, cleared by silence 300 ms later, a press inside it refused 0x07 | **Yes** |
| A6.2 "keeps repeating after its MOVE_STOP" | - | The next repeat starts the move, as AH (ruled f.4: the app will stop repeating after STOP) | No |
| **A6.3** STOP with nothing held, press within 300 ms, press after 300 ms | No (nothing held, and the last hold ended with a stop write, or more than 300 ms ago) | `BLE: STOP_ALL (no app hold, no latch)` only; **(a) a press within 300 ms is a normal start (0x00)**; (b) the same; no `start latch` line in either. Before: (a) 0x07 until released; (b) `cleared (silence)` ~300 ms after the STOP | **Yes** |
| A6.3 exception: STOP within 300 ms of the app's last start when no stop write arrived (lost MOVE_STOP, or an app that ends holds by just stopping its repeats) | Yes - `app_commanding` still true | Latches; `latch_app_starts()` restarts the deadline, so it clears by silence 300 ms after the STOP_ALL (`cleared (silence), 0 repeats refused`); a press inside that is 0x07 until released | (narrower than before) |
| A6.4 App AUTO LEVEL hold, STOP | Yes | As AH: no restart from Step 1; 0x07 per repeat; LEVELED unchanged | No |
| **A6.5** Viewer STOP_ALL while the control phone holds a move | Yes - the control app is commanding | As AH: `latch set (conn=V)`, the control phone gets 0x07, nothing restarts | No |
| A6.5 new row: viewer STOP_ALL with the control app idle | No | `BLE: STOP_ALL (no app hold, no latch)`; the bed is stopped; the control phone's next press is a normal start. Before: latched 300 ms | **Yes** |
| **A6.6** Physical button held, app STOP | No (the app is idle) | Physical behaviour identical to AH (edge-triggered sampler, `switch_held` still set, nothing restarts until released and pressed again); console `BLE: STOP_ALL (no app hold, no latch)` instead of `latch set`. AH's "related" note: an app press while the physical button is still held is now accepted at once (bed idle, no inhibit) rather than after 300 ms; the physical release then force-stops it (#12) | Print and the 300 ms only |
| STOP_ALL during a COLLISION latch (5.A4 / 5.A6) | Yes (a latch implies commanding) | `latch set (conn=N), kept as collision`, 0x06 continues | No |
| STOP_ALL during a physical-controller collision sequence, app idle | No | No latch; an app start during the sequence is refused 0x06 by `any_collision` and latches COLLISION (#48 as amended) | Print only |

The prompt's gloss for `app_commanding` - "a button held, or released less
than 300 ms ago" - holds only for a release without a stop write. A clean
release (MOVE_STOP / AUTO_LEVEL_STOP) clears `app_commanding` at once, so a
STOP 100 ms after a clean release does **not** latch. I implemented the rule
as the variable (`app_commanding`), not the gloss (f.4).

#### b.A.4 A4 - reset cause at boot

**API found** (VERIFIED by reading the installed SDK, d.2 #3-#9):

- `platform_core/platform/peripheral/inc/sl_hal_emu.h:1177-1180`
  `__INLINE void sl_hal_emu_clear_reset_cause(void) { EMU->CMD_SET =
  EMU_CMD_RSTCAUSECLR; }` and `:1196-1199` `__INLINE uint32_t
  sl_hal_emu_get_reset_cause(void) { return EMU->RSTCAUSE; }`. Its doc
  (`:1173-1175`, `:1188-1190`): "The reset cause bits must be cleared by
  software before a new reset occurs. Otherwise, reset causes may
  accumulate." So the API requires the clear. The header's own example
  (`:1642-1643`) reads then clears; `service/watchdog_manager/src/
  sl_watchdog_manager.c:487-488` does the same.
- The legacy `em_rmu.h` `RMU_ResetCauseGet()` exists too, but `em_rmu.c` is
  not compiled in this project (the `emlib_rmu` component is not in the
  `.slcp`; adding it is a configurator change). `sl_hal_emu.h` is on the
  include path (`cmake_gcc/tilt_bed_controller.cmake:235`).
- Bits: `efr32bg22_emu.h:602-662` - POR (bit 0), PIN (1), EM4 (2), WDOG0 (3),
  LOCKUP (5), SYSREQ (6), DVDDBOD (7), DVDDLEBOD (8), DECBOD (9), AVDDBOD (10),
  IOVDD0BOD (11), DCI (16), VREGIN (31); the register at `:66`.
- **Nothing reads or clears it first:** of the 178 SDK `.c` files the build
  compiles, only `sl_se_manager_util.c` mentions the reset cause, in the
  definition of `sl_se_get_reset_cause()`, which nothing calls (#7: the
  search over all compiled SDK sources would have found a caller). No
  bootloader is fitted (PRE_SHIP F17).
- **Inlining dependency:** `__INLINE` is plain `inline` for GCC
  (`cmsis_gcc.h:44`); the out-of-line copies live in
  `peripheral/src/sl_hal_emu.c:115-116`, which is **not** compiled (#7:
  `grep -c sl_hal_emu` on the cmake file = 0). With C99 `inline` semantics a
  call that is not inlined would fail to link ("undefined reference") - a
  loud failure, not a silent one. `-Os` is unconditional
  (`tilt_bed_controller.cmake:365`); the link succeeded and `nm` finds no
  `sl_hal_emu_*` symbol (#14), so both were inlined. f.7.

**Code** (`app.c`): `#include "sl_hal_emu.h"` (`:47`); a table
`reset_cause_names[]` of `{ EMU_RSTCAUSE_*, "name" }` from the device header
(`:388-407`) and `print_reset_cause()` (`:409-428`), both inside `#if
!BED_CAPTURE_MOTOR_CURRENTS` like every boot print; in `app_init()`, right
after the version line (`:445-456`):

```c
uint32_t reset_cause = sl_hal_emu_get_reset_cause();
sl_hal_emu_clear_reset_cause();
#if !BED_CAPTURE_MOTOR_CURRENTS
print_reset_cause(reset_cause);      // "Reset cause: 0x%08lX (POR PIN ...)", "(none)" if 0, "other" for unknown bits
#else
(void)reset_cause;
#endif
```

Context: `app_init()` runs from `main()` after `sl_main_init()` and before
the super loop, so main-thread context, not an interrupt and not a stack
callback; the Bluetooth stack is initialised by then but its boot event is
not delivered until the first `sl_main_process_action()` (ASSERTED from
`main.c:46-63`). No new flag ("unconditional is fine"); added to PRE_SHIP F24.
Expected values (ASSERTED, from the bit names; nothing observed): power cut
-> `0x00000001 (POR)`, possibly with BOD bits; debugger reset/reflash ->
`SYSREQ` (0x40) or `PIN` (0x02).

Size of Part A: text +504 B (245,732 -> 246,236; #13).

### b.B Part B - Bed Status flag bit 5 = AUTO_LEVEL_SETTLING (`0d45fff`)

**Definition** (`bed_actions.c:77-81`):

```c
bool app_auto_level_settling(void)
{
  return auto_level_active && !any_collision
         && (active_switch == SWITCH_AUTO_LEVEL_VALUE) && first_check;
}
```

"`auto_level_active && first_check`, or the precise equivalent you find":
the auto-level block in `app.c` runs Step 1 (`steady_accel_read_both_axes()`)
only when `auto_level_active && !any_collision && active_switch ==
SWITCH_AUTO_LEVEL_VALUE` (`app.c:687`) and then `first_check` (`:691`). The
two middle terms are the block's own guards, so the bit is 1 exactly when
Step 1 runs. Without them the bit could read 1 while Step 1 is frozen (e.g.
in the pass of a collision, or if a path clears `active_switch` without
clearing `auto_level_active`). VERIFIED code read; f.10.

**Transitions** (from code):

- 0 -> 1 in the pass that accepts the start: `app_auto_level_start()`
  (`bed_actions.c:208-235`) sets `active_switch`, `auto_level_active`,
  `first_check` together - for the app in `ble_bed_service_tick()` (the same
  tick's `bed_status_get()` already sees 1, so the start's notify carries it),
  for the physical button in `switch_input_tick()` (before the BLE tick).
- 1 -> 0 when `steady_accel_read_both_axes()` sees the 1500 ms timer
  (`auto_level.c`, `first_check = false`), or at the release / STOP / STOP_ALL
  / watchdog / disconnect (`auto_level_active` false), or a collision. Note
  `first_check` is not reset at a release (`app_auto_level_stop()` leaves it
  true); the bit is still 0 because `auto_level_active` is false.
- Encoded `flags |= 0x20` (`ble_bed_service.c:86`); byte 0 is compared by
  `status_flags_changed()`, so both edges are notified at once.

**Print** (B3): `app_auto_level_settling_log_tick()` (`bed_actions.c:83-93`),
behind the existing `BED_DEBUG_LEVELED_LOG` (the prompt allows it; PRE_SHIP
F19 updated): a static last value, one line `AUTO_LEVEL_SETTLING (Bed Status
bit 5) -> 0|1` per change. Called once per pass in `app.c:791`, right after
the auto-level block, so the 1 prints after `Step 1: ROLL and PITCH check`
and the 0 before `Step 2` / `Bed is disabled ...`. It polls (the bit is
derived, not stored), costs one comparison per pass, and compiles to nothing
at flag 0. Suppressed under `BED_CAPTURE_MOTOR_CURRENTS` like the LEVELED line.

**Docs (B2):** BLE_GATT_DESIGN §6 - bit 5 in the layout, a new "AUTO_LEVEL_
SETTLING (bit 5)" section (definition, edges, app use: fill over 1.5 s from
the bit going to 1, snap to full when it clears), and the protocol paragraph
(additive within v1, no version change); §4 - "AUTO_LEVEL_START - when the
bed first moves": at least 1.5 s after the first accepted start
(`STEADY_ACCEL_READ_TIMER_TIME`) plus a few passes, a further 0.35 s
(`SNEAK_MOTOR_OFF_TIME`) if the first axis starts within the pulse band
(0.1° < |angle| <= 0.3°), none if both axes are within 0.1°. DECISIONS #46
"Amended by Prompt AI" item 1 records the bit (B asked for no DECISIONS
entry; a wire change belongs in the log).

Size of Part B: text +176 B (246,236 -> 246,412; #20); at flag 0 246,260.

### b.C Part C - live idle tilt (`3e9349f`; DECISIONS #50)

#### b.C.1 The idle window and the sampler (C1)

The window: **a connection subscribed to Bed Status, no motor energised, no
burst running, auto-level not running, settings mode not active.** In
`tilt_telemetry_tick()` (`auto_level.c:714-777`), after the existing gates:

```c
void tilt_telemetry_tick(bool status_subscribed, bool other_sampler_active)
{
  motors_on = any_motor_energised(); T3 edge tracking (unchanged)
  auto_level_chain_ran = auto_level_chain_updated; auto_level_chain_updated = false;   // C4
  perpass_ran_last_pass = tilt_perpass_ran; tilt_perpass_ran = false;                 // seed + C4
  if (!status_subscribed)        { tilt_idle_close(); return; }        // gate 2a
  if (motors_just_stopped)       tilt_telemetry_arm_burst();           // T3 (unchanged)
  if (other_sampler_active)      { tilt_idle_close(); return; }        // gate 2c
  if (motors_on || tilt_burst_running) {                               // gate 2b: per-pass chain, unchanged
    tilt_idle_close(); read_accel(); accel_filter_update(&telemetry_filter);
    tilt_telemetry_valid = true; tilt_perpass_ran = true; return;
  }
  if (!tilt_idle_open) tilt_idle_open_window(perpass_ran_last_pass);   // idle
  if (tilt_idle_sample_due) { tilt_idle_sample_due = false; tilt_idle_sample(); }
}
```

- **Timer:** `tilt_idle_open_window()` (`:552-577`) starts a periodic
  sleeptimer of `TILT_IDLE_SAMPLE_MS` = 100 (`sl_sleeptimer_restart_
  periodic_timer`); `tilt_idle_close()` (`:579-593`) stops it. The callback
  (`:540-545`) only sets `tilt_idle_sample_due` (volatile) - ISR context, no
  I2C (#27 rule 1).
- **One `read_accel()` per flag**, in the main loop. `read_accel()` keeps its
  own `ACCEL_I2C_MIN_INTERVAL_MS` gate (10 ms, a static last-read tick shared
  by all callers, `:279-284`); at 100 ms spacing every flag is a real I2C read,
  except at a window edge if another chain read within the last 10 ms (then it
  reuses that sample - the sensor's latest output anyway).
- **Bus occupancy** (ASSERTED arithmetic on recorded figures): one write-read
  on `sl_i2cspm_on_board` per 100 ms - 1 register byte out, 6 in, ~0.85 ms of
  wire time at 100 kbit/s, ~1.4 ms maximum measured (#30) - so **~0.9-1.4 %**
  of the bus, against ~13 % that #30 proved safe during auto-level. The
  existing motor-fault poll (`exp_board.c:354-364`, at most every 200 ms) runs
  on these extra passes too, so at rest with a phone subscribed it now
  actually reaches its 5 reads/s (its transfer size not read this session).
  One consumer per pass: the gates make the idle sampler, the per-pass chain
  and auto-level's chain mutually exclusive.
- **Why bursts are excluded** (my reading, f.2): C1 lists four conditions and
  not "no burst"; but C2 says the seed is taken "when the idle window opens,
  so the shown angle does not jump at the end of a move or burst", and the AH
  Option B text says the idle filter "should be seeded from the burst's final
  value when idle resumes". Both only make sense if the window opens when the
  burst ends. During a burst the per-pass chain already samples every pass
  and is shipped, as before.

#### b.C.2 The filter (C2)

`tilt_idle_sample()` (`auto_level.c:651-681`):

```c
now = tick; read_accel();
x_roll  = latest_roll_angle_scaled  - baseline_roll_angle;     // SCALED x100, baseline-relative,
x_pitch = latest_pitch_angle_scaled - baseline_pitch_angle;    // internal sign (as every chain)
if (!have_value) { y = x; have_value = true; }
else {
  dt_ms = (now - last_sample_tick) * 1000 / sl_sleeptimer_get_timer_frequency();   // measured
  alpha = 1 - expf(-dt_ms / TILT_IDLE_TAU_MS);                                     // tau 500 ms
  y += alpha * (x - y);
}
last_sample_tick = now; out = round_to_int16(y);
if (now - open_tick >= TILT_IDLE_CONVERGED_MS) { LEVELED exit (D); diag (C6); }
```

- A first-order low-pass whose step response is `1 - exp(-t / tau)` whatever
  the sample spacing, because alpha is computed from the measured `dt`. At
  10 Hz, alpha ≈ 0.181. A late sample gets a larger alpha, so the time
  constant holds even if a wake is delayed (#2, #29).
- Same input the other chains use (the raw sample minus the stored baseline),
  so the output is in the internal sign convention and baseline-relative;
  `bed_status.c`'s single pitch negation still applies once (C5 of AH).
  Filtering `x - baseline` equals filtering `x` then subtracting, since the
  baseline is constant while the window is open; settings mode (where it can
  change) closes the window, and the next opening reseeds.
- Float state, rounded (not truncated) to `int16_t` at the output.
- **Seed** (`tilt_idle_open_window()`): if `perpass_ran_last_pass` - the
  per-pass chain updated in the pass immediately before the window opened,
  i.e. the window opens at the end of a move's T3 burst or of a T1/T2 burst -
  the filter starts from `telemetry_filter.baseline_roll/pitch` and the window
  counts that as its latest value (fresh). Otherwise - the window opens after
  auto-level, after settings mode, or straight after a subscription that found
  the per-pass chain idle - `tilt_idle_sample_due` is set so the **first
  sample** is taken in the opening pass and the filter starts from it. Why not
  always the telemetry chain's value: after auto-level or settings mode it has
  not run, possibly for hours (AH C5: it is never reset), so seeding from it
  would show an old angle and then drift. f.3.
- The moving chain (per-pass telemetry) and auto-level's chain are
  untouched: `accel_filter_update()`, `NUM_ACCEL_READINGS`, `accel_alpha`
  unchanged (c.1).

#### b.C.3 Bed Status at rest (C3)

- **Value:** `bed_status_get()` (`bed_status.c:113-139`) selects, in order:
  auto-level / settings -> auto-level's chain; **idle window holding a value
  -> the idle value (new)**; telemetry ever valid -> the per-pass chain; else
  auto-level's last-known.
- **Cadence** (`ble_bed_service.c:1225-1239`): interval = 100 ms if `command`
  is MOVE or AUTO_LEVEL (unchanged); else **200 ms (5 Hz) if
  `tilt_idle_active()` or a burst is running outside settings mode**; else
  1000 ms. The burst is included because it is "idle" by C1's own four
  conditions and its tilt is live (the T1 burst after the app subscribes,
  the T3 burst after a move). `restart_status_notify_timer()` /
  `publish_status()` now take the interval in ms, and the interval last
  started is kept in `status_notify_interval_ms` (`:218`).
- **A change of cadence publishes at once** (`:1236-1237`). Without it: after
  an app MOVE_STOP the stop is processed in the BLE tick, the command change
  publishes with a 1 s timer (the T3 burst is armed only in the next pass's
  `tilt_telemetry_tick()`), and the next notify came up to 1 s later - a frozen
  display after every app move. The cost is one extra notify at each cadence
  change. ASSERTED.

#### b.C.4 `tilt_fresh`, bit 3 (C4)

Set in `bed_status_get()` with the chain it picks (`bed_status.c:126-138`) and
passed by `ble_bed_service_tick()` to the encoder (`:1223`); the encoder's
separate parameter is kept.

| Shipped chain | `tilt_fresh` = 1 when | Source |
|---|---|---|
| auto-level's (auto-level or settings mode) | `accel_running_average()` ran in this pass | `auto_level_chain_updated` set in `accel_running_average()` (`auto_level.c:372`), latched once per pass in `tilt_telemetry_tick()`, which runs after the auto-level block and `settings_mode_tick()` and before `ble_bed_service_tick()` (`app.c` order) |
| idle | the idle value is younger than `TILT_IDLE_FRESH_MS` (300 ms) | `tilt_idle_fresh()` (`auto_level.c:703-708`) |
| per-pass telemetry | it updated in this pass | `tilt_perpass_ran` |
| auto-level last-known fallback | never | - |

**Exactly when it is 0** (with a phone subscribed - otherwise nothing is
notified): (1) before any sample since boot (fallback); (2) in settings mode
except while the zero-level capture runs; (3) in the one pass that starts an
auto-level run (`bed_status_get()` already picks auto-level's chain, which
has not run yet in that pass) - typically the start's own immediate notify;
(4) at rest if the idle value is 300 ms or more old: the main loop or the
I2C read stalled, or the timer stopped delivering; (5) in a pass where the
per-pass chain is shipped but did not run - not reachable from code with a
subscriber (the selection and the gates agree), listed for completeness.

"The existing per-pass chain is running" was read as "the chain that is
shipped updated in this pass", which includes auto-level's chain during a run
(else the app would grey out the angle during every auto-level). f.5.

#### b.C.5 Sleep (C5)

- **No new EM0 veto** (`app.c:617-619` comment; `app_is_ok_to_sleep()`
  unchanged). The burst and motor vetoes are unchanged.
- **What wakes it:** the 100 ms sleeptimer interrupt. After an interrupt
  taken in sleep, `sl_power_manager_sleep_on_isr_exit()`
  (`autogen/sl_power_manager_handler.c:77-114`, VERIFIED read, #11) returns
  "sleep" only if the wake was the power manager's own internal one or the
  Bluetooth / iostream / app hook asks for sleep; the app hook is the weak
  `SL_POWER_MANAGER_IGNORE` (`:46-48`). So the main loop runs one pass, does
  the read, and sleeps again - the same mechanism the 1 Hz notify timer has
  always relied on. That `sli_bt_sleep_on_isr_exit()` does not answer "sleep"
  for a sleeptimer interrupt is ASSERTED (not read).
- **Duty cycle** (ASSERTED estimate): per sample ~1.5-2 ms awake (the I2C
  read ~0.85-1.4 ms; `read_accel()`'s double-precision `sqrt` / two `atan2` -
  software double on this FPU; `expf`), so ~1.5-2 % at 10 Hz, plus the 5 Hz
  notify passes and the fault poll: **~2-3 % extra EM0 time** while a phone is
  subscribed and the bed is at rest. PRE_SHIP F11 notes the device probably
  sits in EM1 between events anyway.
- PRE_SHIP: F11 now asks for the idle current with and without a phone
  subscribed; new **F30**: a phone that stays subscribed with the app in the
  background keeps the 10 Hz sampler running (the app will unsubscribe when
  backgrounded - app prompt; nothing in the firmware now).

#### b.C.6 Bench diagnostics (C6)

Behind `TILT_IDLE_DIAG` (1, `auto_level.c:73`; PRE_SHIP new **F29**), in
`tilt_idle_diag_sample()` (`:619-648`): once the filter has run
`TILT_IDLE_CONVERGED_MS` (1500 ms) since the window opened, every idle sample
updates count, min and max of the **filtered** roll and pitch outputs; when
the first sample at least `TILT_IDLE_DIAG_PERIOD_MS` (10 000 ms, measured in
ticks) after the period's first sample arrives, one line:

```
TILT idle: %u samples in %lu ms, roll min %d max %d p2p %d, pitch min %d max %d p2p %d (deg x100, pitch internal sign)
```

then the next sample starts a new period. A window close discards a partial
period. Starting after 3 tau (not at the window open) is my choice: the
numbers are for sizing D's band, which also judges only after 3 tau; f.6.
First line ~12 s after the app opens (0.5 s T1 burst + 1.5 s + 10 s). The
print is in the main loop, ~170 characters; if the UART write blocks, ~15 ms
at 115200 baud every 10 s, never with a motor on (ASSERTED; baud not read).

#### b.C.7 DECISIONS #50 (C7)

New entry: the problem, the decision (window, sampling, filter, seed, wire,
`tilt_fresh`, sleep, diag), why a time-based filter and that it partly
reverses #33's "same filter in both chains" for the idle case only, the
stand-down rules, the six AH C2 risks with how the bench checks each (now
REGRESSION §6b), the sleep estimate, status. Complete text in c.2.

#### b.C.8 Other things found in the code that bear on C (ASSERTED, not changed)

- **Idle -> per-pass step.** When a move or burst starts, the shown value
  switches to the per-pass chain, which resumes from its own 130-slot buffer
  as last filled at the end of its previous window. If the bed was tilted
  while idle (part D's case), the display steps back to the old angle and
  catches up over ~130 passes (~0.2-0.3 s). Before Part C the same chain was
  shown before and after, so there was no step. A fix would reset or seed the
  per-pass chain from the idle value when its window opens - a change to the
  moving chain's initial state, which C2 says stays unchanged. f.8.
- **Auto-level start** (pre-existing): the first ~130 passes of a run ship
  auto-level's chain still blending the previous run's samples (AH C5); with
  bit 3 now meaningful, those passes read `tilt_fresh` = 1. Step 1 (bit 5)
  covers that time.
- **After an auto-level that ran motors**, the T3 burst (armed when the motors
  stopped, while auto-level was still active) runs the per-pass chain for its
  remaining ~0.5 s from that chain's stale buffer (pre-existing), then the
  idle window opens seeded from it.

Size of Part C: text +1,968 B (246,412 -> 248,380; #25), `expf` 120 B of it
(#26 `nm -S`); at `TILT_IDLE_DIAG` 0: 248,004.

### b.D Part D - LEVELED (bit 4) clears itself (`8996259`)

**Constants** (`auto_level.c:76-82`, `:111`): `LEVELED_EXIT_BAND` 0.3
(degrees) -> `leveled_exit_band = LEVELED_EXIT_BAND * SCALE_FACTOR` = 30 (the
same double-to-int16 file-scope pattern as `pulse_start_tolerance`; 0.3 x 100
is 30.0 exactly in double, AH #19); `LEVELED_EXIT_HOLD_MS` 2000. Both named,
both to be tuned from the C6 lines.

**Algorithm** (`tilt_idle_leveled_exit_sample()`, `:598-614`), called from
`tilt_idle_sample()` on every idle sample once the filter has run
`TILT_IDLE_CONVERGED_MS` (3 x 500 = 1500 ms) since the window opened:

```c
outside = abs(roll_out) > 30 || abs(pitch_out) > 30;      // filtered, baseline-relative
if (!outside) { out_of_band = false; leveled_exit = false; return; }
if (!out_of_band) { out_of_band = true; out_since = now; }
if (now - out_since >= 2000 ms) leveled_exit = true;
```

- "Continuously": any sample back inside the band restarts the 2 s.
  Milliseconds from the tick counter, not a sample count (#2).
- `tilt_idle_open_window()` and `tilt_idle_close()` reset the tracker, so a
  new window starts from scratch (and waits 1.5 s again).
- `tilt_idle_leveled_exit_due()` (`:698-701`) = window open && the flag.
  `app.c:840-842`, right after `tilt_telemetry_tick()`: `if
  (tilt_idle_leveled_exit_due()) app_set_leveled(false);`. `app_set_leveled()`
  returns at once when there is no change, so it is idempotent; it prints
  `LEVELED (Bed Status bit 4) -> 0` once (behind `BED_DEBUG_LEVELED_LOG`), and
  byte 0 notifies at once. The call is in `app.c` so `auto_level.c` keeps no
  dependency on `bed_actions` (the same reason its gates are passed in).
- **Never sets bit 4**: nothing in this path can call `app_set_leveled(true)`;
  the only setters remain `app.c`'s two success branches (#46). No
  IN_LEVEL_BAND bit.
- **Timing** (ASSERTED, from tau): a step tilt of 1° crosses 0.3° in the
  filter after ~0.18 s (`-0.5 s x ln(0.7)`), so LEVELED clears ~2.2 s after
  the tilt if the window has been open at least 1.5 s; earliest 3.5 s after a
  window opens. A bump that returns within ~1.8 s does nothing.
- With no phone subscribed nothing samples, so the bit cannot clear then; a
  REFRESH_TILT or any burst closes the window and restarts the 1.5 s.

**Docs (D3):** BLE_GATT_DESIGN §6 bit 4 - a new "Cleared when" bullet and
"never set from tilt"; DECISIONS #46 "Amended by Prompt AI" item 2.

Size of Part D: text +160 B (248,380 -> 248,540; #31).

### b.E Part E - docs, rulings, builds (`05c4c2f`; E2 in SESSION_LOG)

**E1, REGRESSION** (complete diff in c.2): 1.R reset cause; §5b intro, 5b.3
rewritten for A3 (with an optional lost-stop case), new 5b.3d (A2 - and the
note that A2 cannot be isolated on the bench now that the idle sampler runs a
pass every 100 ms), 5b.5 and 5b.6 for A3, new 5b.8 collision wins (A1), 5b.7
widened; 6.11 (bit 5, a-d); new §6b (6b.1-6b.9): live display at rest with
the 10 s TILT lines, after a move, after auto-level, link stays up, LEVELED ->
0 ~2 s after a hand tilt, nothing for a brief bump, settings / REFRESH_TILT,
no phone + F11, backgrounded app (F30).

**E2:** the rulings are in the SESSION_LOG entry (f.1-f.9 of the AH handoff).

**E3, builds** (the build wrapper prints the last 25 lines and every
warning/error line of the full output; d.3):

| Build | text / data / bss | Evidence |
|---|---|---|
| Baseline at `cd2a148` (`ninja: no work to do`) | 245,732 / 788 / 72,472 (= AH record) | #12 |
| A, defaults | 246,236 / 788 / 72,472 | #13 |
| A, `BLE_DEBUG_WATCHDOG_LOG` 0 | 244,380 / 780 / 72,480 | #14 |
| A, back to 1 | 246,236 / 788 / 72,472 | #15 |
| B, defaults / `BED_DEBUG_LEVELED_LOG` 0 / back | 246,412 / 246,260 / 246,412 | #20, #21 |
| C, defaults / `TILT_IDLE_DIAG` 0 / back | 248,380 / 248,004 / 248,380 | #25, #26 |
| D, defaults | 248,540 / 788 / 72,472 | #31 |
| Final matrix: defaults | 248,540 / 788 / 72,472 | #34 |
| `TILT_IDLE_DIAG` 0 | 248,164 / 788 / 72,472 | #34 |
| `BED_DEBUG_LEVELED_LOG` 0 | 248,396 / 788 / 72,472 | #34 |
| `BLE_DEBUG_WATCHDOG_LOG` 0 | 246,652 / 780 / 72,480 | #34 |
| all three 0 | 246,132 / 780 / 72,480 | #34 |
| `BED_CAPTURE_MOTOR_CURRENTS` 1 (extra: my new code has `!BED_CAPTURE_MOTOR_CURRENTS` branches) | 245,028 / 788 / 72,472 | #34 |
| restored defaults (after restoring `debug_capture.h`'s working copy, #36) | 248,540 / 788 / 72,472 | #34, #36 |
| Host test | `0 check(s) failed` | #37 |

Every build: exit 0, nothing after the `--- warnings/errors` line. **Size
change against `cd2a148`, defaults: text +2,808 B, data 0, bss column 0.** The
Berkeley `bss` column cannot change here: it is `.stack` + `.bss` +
`.memory_manager_heap`, and the heap takes the rest of RAM (#27 `size -A`:
`.bss` 12,000, heap 16,760); the new statics (~70 B, #26 `nm -S`) come out of
the heap. The `.bss` section's baseline was not measured.

**Flags at the end:** `BED_BENCH_PROVOKE_RUNAWAY` 0, `BLE_BENCH_WIPE_BONDS`
0, `BED_CAPTURE_MOTOR_CURRENTS` 0, `BLE_DEBUG_WATCHDOG_LOG` 1,
`BED_DEBUG_LEVELED_LOG` 1, `TILT_IDLE_DIAG` 1 (#37). The final matrix's `sed`
rewrote `debug_capture.h`'s CRLF working copy as LF; `git diff` showed no
content change (#35) and the working copy was restored from HEAD (#36). No
`config/`, `autogen/`, `bed_control.c` or `.btconf` change since `cd2a148`
(#37, empty `git diff --stat`).

### b.F E4 - console lines to expect (from code; nothing observed)

`<...>` = a value; "unchanged" = lines that already printed before this
session, as in the 2026-11-02 log.

**1. Boot (power cut and restore):**

```
Welcome to the Tilt Bed application
Application Version <M>.<m>.<b> 
Reset cause: 0x00000001 (POR)                 <- new (A4); other bits possible, e.g. a BOD
... (the existing boot lines, unchanged: Accelerometer is sampling ..., NVM3 initialized successfully, Loaded ..., BLE SM: configure -> 0x0000, ...)
```

After a debugger reset/reflash: `Reset cause: 0x00000040 (SYSREQ)` or
`0x00000002 (PIN)`. A second identical reset must show the same single cause,
not an accumulation.

**2a. App STOP while holding HEAD up (AH A6.1, unchanged by A3):**

```
BLE: STOP_ALL latch set (conn=<N>)
BLE: hold end op=0x01 seq=<S>: <R> refreshes, longest refresh gap <G> ms, longest between passes 0 ms
                                              (each held repeat: Command Result 0x07, no line)
BLE: start latch (stop) cleared (stop), <K> repeats refused        <- at the release (MOVE_STOP)
```

**2b. App STOP with nothing held (A3):**

```
BLE: STOP_ALL (no app hold, no latch)
```

No `hold end` line (no watchdog was armed), no `start latch` line; the next
press is a normal start (0x00).

**3. App auto-level from off-level, both axes levelled** (new lines marked;
the rest as in the 2026-11-02 log):

```
LEVELED (Bed Status bit 4) -> 0               <- only if it was 1 (a new run clears it)
Step 1: ROLL and PITCH check
AUTO_LEVEL_SETTLING (Bed Status bit 5) -> 1   <- new (B)
                                              (~1.5 s, no motor)
AUTO_LEVEL_SETTLING (Bed Status bit 5) -> 0   <- new (B)
Step 2: ROLL Not Level
... (Leveling... / Latch / Pulse on... / Max Current lines / ROLL Leveled successfully... / Axis set to PITCH / ... unchanged)
LEVELED (Bed Status bit 4) -> 1
Exit auto-level
Bed is level
BLE: hold end op=0x03 seq=<S>: ...            <- at the release
```

From a level bed: `-> 1` ... `-> 0`, then `LEVELED ... -> 1`, `Bed is
disabled, no leveling required`, `Bed is level`.

**4. Bed at rest 30 s with the app open** (new, C6): nothing for ~12 s after
the app opens, then about every 10 s:

```
TILT idle: <~101> samples in <~10000> ms, roll min <a> max <b> p2p <b-a>, pitch min <c> max <d> p2p <d-c> (deg x100, pitch internal sign)
```

From code: ~101-102 samples and T between 10 000 and ~10 100 ms per line,
depending on how the 100 ms period rounds to sleeptimer ticks (not checked).
A count well below 100 means the sampler is not running at 10 Hz.

**5. Tilting the bed by hand after a successful level** (new, D):

```
LEVELED (Bed Status bit 4) -> 1               <- end of the run
... (at least ~3.5 s after the run ended, bed tilted > 0.3 deg and held)
LEVELED (Bed Status bit 4) -> 0               <- ~2.2 s after the tilt
```

and the TILT line for that 10 s period shows a large p2p. Nothing for a bump
released within ~1.8 s; never a `-> 1` from tilt.

---

## c. Diffs

Complete and not truncated. c.1 is every source change of the session; c.2 every docs change except the new prompt file (144 lines, written by the reviewer; it is the committed `docs/prompts/PROMPT_AI_latch-refinements-live-tilt.md`) and `docs/SESSION_LOG.md` (CLAUDE.md's standing exception: it ships with this file). The docs diff is 697 lines; it is included in full rather than cut at 400. Per-commit split: `git show --stat <sha>` in d.2 #19, #23, #29, #32, #38.

### c.1 Source diff - `git diff cd2a148 05c4c2f -- '*.c' '*.h'` (complete)

````diff
diff --git a/app.c b/app.c
index 3ba6078..5f6df15 100644
--- a/app.c
+++ b/app.c
@@ -44,6 +44,7 @@
 
 #include "sl_component_catalog.h"
 #include "em_gpio.h"
+#include "sl_hal_emu.h"   // sl_hal_emu_get_reset_cause() / sl_hal_emu_clear_reset_cause()
 #include "pin_config.h"
 #include "sl_atomic.h"
 #include "sl_pwm.h"
@@ -382,6 +383,49 @@ void app_reset_auto_level_timer(void)
 
 // find_sensitivity_index()/apply_sensitivity_level() moved to settings_mode.c
 
+// Reset cause at boot (Prompt AI A4). EMU->RSTCAUSE bits for the EFR32BG22,
+// named as in the device header (efr32bg22_emu.h, EMU_RSTCAUSE_*). More than
+// one bit can be set at once (e.g. a power cut can show POR with a BOD).
+#if !BED_CAPTURE_MOTOR_CURRENTS
+static const struct {
+  uint32_t mask;
+  const char *name;
+} reset_cause_names[] = {
+  { EMU_RSTCAUSE_POR,       "POR" },
+  { EMU_RSTCAUSE_PIN,       "PIN" },
+  { EMU_RSTCAUSE_EM4,       "EM4" },
+  { EMU_RSTCAUSE_WDOG0,     "WDOG0" },
+  { EMU_RSTCAUSE_LOCKUP,    "LOCKUP" },
+  { EMU_RSTCAUSE_SYSREQ,    "SYSREQ" },
+  { EMU_RSTCAUSE_DVDDBOD,   "DVDDBOD" },
+  { EMU_RSTCAUSE_DVDDLEBOD, "DVDDLEBOD" },
+  { EMU_RSTCAUSE_DECBOD,    "DECBOD" },
+  { EMU_RSTCAUSE_AVDDBOD,   "AVDDBOD" },
+  { EMU_RSTCAUSE_IOVDD0BOD, "IOVDD0BOD" },
+  { EMU_RSTCAUSE_DCI,       "DCI" },
+  { EMU_RSTCAUSE_VREGIN,    "VREGIN" },
+};
+
+static void print_reset_cause(uint32_t cause)
+{
+  uint32_t known = 0;
+  printf("Reset cause: 0x%08lX (", (unsigned long)cause);
+  bool first = true;
+  for (uint8_t i = 0; i < sizeof(reset_cause_names) / sizeof(reset_cause_names[0]); i++) {
+    if (cause & reset_cause_names[i].mask) {
+      printf("%s%s", first ? "" : " ", reset_cause_names[i].name);
+      first = false;
+    }
+    known |= reset_cause_names[i].mask;
+  }
+  if (cause & ~known) {
+    printf("%sother", first ? "" : " ");
+    first = false;
+  }
+  printf("%s)\r\n", first ? "none" : "");
+}
+#endif
+
 /*******************************************************************************
  *****************************   PUBLIC FUNCTIONS   ****************************
  ******************************************************************************/
@@ -398,6 +442,19 @@ void app_init(void)
   printf("Application Version %d.%d.%d \r\n", VERSION_MAJOR, VERSION_MINOR, VERSION_BUILD);
 #endif
 
+  // Why the last reset happened (Prompt AI A4): EMU->RSTCAUSE, read with the
+  // SDK's EMU API (sl_hal_emu.h). The register accumulates bits until cleared,
+  // so clear it after reading (the API's own instruction), or the next boot
+  // would report this cause too. Nothing in the SDK code this project builds
+  // reads or clears it before here. Unconditional (PRE_SHIP F24's review).
+  uint32_t reset_cause = sl_hal_emu_get_reset_cause();
+  sl_hal_emu_clear_reset_cause();
+#if !BED_CAPTURE_MOTOR_CURRENTS
+  print_reset_cause(reset_cause);
+#else
+  (void)reset_cause;
+#endif
+
   // Initialize the Incremental Analog to Digital Converter (IADC)
   initIADC ();
 
@@ -557,6 +614,10 @@ bool app_is_ok_to_sleep(void)
     return false;
   }
 
+  // The idle tilt sampler (Prompt AI C5, DECISIONS #50) deliberately has NO
+  // veto here: it samples on a 100 ms sleeptimer whose interrupt brings the
+  // main loop round, so the CPU may sleep between samples.
+
   return true;
 }
 #endif // SL_CATALOG_POWER_MANAGER_PRESENT
@@ -725,6 +786,10 @@ void app_process_action(void)
       }
   }
 
+  // Bench print on each change of Bed Status bit 5 (AUTO_LEVEL_SETTLING),
+  // after the block above has run this pass's Step 1 work.
+  app_auto_level_settling_log_tick();
+
 
 /*******************************************************************************
 *** "Settings" Mode
@@ -769,6 +834,13 @@ void app_process_action(void)
   tilt_telemetry_tick(ble_bed_service_status_subscribed(),
                       auto_level_active || in_settings_mode);
 
+  // LEVELED (Bed Status bit 4) clears itself when the idle tilt has left the
+  // level band for long enough (Prompt AI part D). Clear only - bit 4 is set
+  // only by a successful auto-level (#46). Idempotent: prints once.
+  if (tilt_idle_leveled_exit_due()) {
+    app_set_leveled(false);
+  }
+
   bed_control_tick();
   ble_bed_service_tick();
 
diff --git a/auto_level.c b/auto_level.c
index 076a89e..b2f8920 100644
--- a/auto_level.c
+++ b/auto_level.c
@@ -57,6 +57,30 @@
 // ever runs while a phone is subscribed.
 #define TILT_TELEMETRY_BURST_MS 500
 
+// Live idle tilt (Prompt AI part C, DECISIONS #50). While a phone is
+// subscribed and the bed is idle, the accelerometer is sampled at 10 Hz and
+// filtered by a time-constant (millisecond) filter of its own - see the idle
+// sampler block below. All in milliseconds via sleeptimer (DECISIONS #2).
+#define TILT_IDLE_SAMPLE_MS 100        // 10 Hz sample period
+#define TILT_IDLE_TAU_MS 500           // filter time constant; bench-tunable from TILT_IDLE_DIAG's lines
+#define TILT_IDLE_CONVERGED_MS (3 * TILT_IDLE_TAU_MS) // ~95 % settled after the window opens
+#define TILT_IDLE_FRESH_MS 300         // tilt_fresh: an idle value younger than this is fresh (3 periods)
+
+// Bench diagnostics for the idle sampler: every TILT_IDLE_DIAG_PERIOD_MS while
+// it runs (after TILT_IDLE_CONVERGED_MS), one line with the sample count and
+// the min/max/peak-to-peak of the filtered roll and pitch. Measures the noise
+// at rest for the LEVELED exit band. Never per sample. Pre-ship: 0 (PRE_SHIP.md).
+#define TILT_IDLE_DIAG 1
+#define TILT_IDLE_DIAG_PERIOD_MS 10000
+
+// LEVELED (Bed Status bit 4) clears itself (Prompt AI part D, DECISIONS #46
+// amendment): when the idle filtered tilt is outside LEVELED_EXIT_BAND on roll
+// or pitch continuously for LEVELED_EXIT_HOLD_MS, judged only once the idle
+// filter has run TILT_IDLE_CONVERGED_MS since its window opened. Never SETS
+// the bit. Both bench-tunable from TILT_IDLE_DIAG's noise lines.
+#define LEVELED_EXIT_BAND 0.3       // degrees
+#define LEVELED_EXIT_HOLD_MS 2000
+
 #define NUM_ACCEL_READINGS 130 // Number of accelerometer samples to average
 #define TOLERANCE 0.01 // Tolerance auto-level code reaches before determining bed is level
 #define CLOSE_TOLERANCE 0.1 // Auto-level won't begin leveling if within this tolerance
@@ -84,6 +108,7 @@ int32_t accel_vector_mag = 0; // Magnitude of acceleration vector
 int16_t tolerance = TOLERANCE * SCALE_FACTOR;  // Tolerance in degrees * SCALE FACTOR for determining levelness
 int16_t close_tolerance = CLOSE_TOLERANCE * SCALE_FACTOR; // Tolerance of beginning sneak leveling to adjust slowly
 int16_t pulse_start_tolerance = PULSE_START_TOLERANCE * SCALE_FACTOR;
+static const int16_t leveled_exit_band = LEVELED_EXIT_BAND * SCALE_FACTOR; // 30
 
 float latest_roll_angle = 0.0; // X-Z angle, deviation from the baseline_roll_angle
 float latest_pitch_angle = 0.0; // Y-Z angle, deviation from the baseline_pitch_angle
@@ -124,6 +149,11 @@ typedef struct {
 // bed_status.c) is untouched by the refactor.
 static accel_filter_state_t auto_level_filter = {0};
 
+// Set by accel_running_average() whenever auto-level's chain updates;
+// latched once per pass by tilt_telemetry_tick() for tilt_fresh (Prompt AI C4).
+static bool auto_level_chain_updated = false;
+static bool auto_level_chain_ran = false;
+
 static int16_t accel_average_roll_angle = 0; // Average value of roll SCALED VALUE
 static int16_t accel_average_pitch_angle = 0; // Average value of pitch SCALED VALUE
 int16_t baseline_accel_average_roll_angle = 0; // Average value of roll, deviation from user's level baseline - read by bed_status.c for telemetry
@@ -339,6 +369,7 @@ void accel_running_average(){
 
       // Filter into AUTO-LEVEL's own state. Never telemetry's.
       accel_filter_update(&auto_level_filter);
+      auto_level_chain_updated = true;
 
       // Mirror the outputs into the globals every existing reader already
       // uses (check_level(), returnAccelValue(), print_accel_value(),
@@ -391,6 +422,7 @@ static sl_sleeptimer_timer_handle_t tilt_burst_timer;
 static volatile bool tilt_burst_running = false;
 static bool tilt_telemetry_valid = false; // has this sampler ever produced a value?
 static bool tilt_prev_motors_on = false;  // for the T3 edge
+static bool tilt_perpass_ran = false;     // the per-pass chain updated in this pass (tilt_fresh)
 
 static void tilt_burst_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
 {
@@ -440,6 +472,241 @@ static bool any_motor_energised(void)
   return false;
 }
 
+bool tilt_telemetry_perpass_ran(void)
+{
+  return tilt_perpass_ran;
+}
+
+bool accel_auto_level_chain_ran(void)
+{
+  return auto_level_chain_ran;
+}
+
+/*******************************************************************************
+ *** Idle tilt sampler (Prompt AI part C, DECISIONS #50)
+ *
+ * The per-pass window above covers a move and the 500 ms bursts. The rest of
+ * the time a subscribed phone used to see the last value of the last window.
+ * The IDLE window is the rest of the time: a phone subscribed, no motor
+ * energised, no burst running, not auto-level, not settings mode. In it:
+ *
+ *   - a periodic sleeptimer (TILT_IDLE_SAMPLE_MS) whose callback ONLY sets
+ *     tilt_idle_sample_due (ISR context - no I2C, DECISIONS #27 rule 1);
+ *   - tilt_telemetry_tick(), in the main loop, does one read_accel() per flag
+ *     (read_accel() itself keeps ACCEL_I2C_MIN_INTERVAL_MS) - ~1 % bus
+ *     occupancy at 10 Hz against the ~13 % #30 proved safe;
+ *   - a first-order low-pass filter defined by its time constant
+ *     TILT_IDLE_TAU_MS, alpha = 1 - exp(-dt / tau) from the MEASURED time
+ *     between samples, so it behaves the same whatever the loop rate (#2,
+ *     #29) - unlike the pass-counted chains, which are unchanged;
+ *   - its own state, never auto-level's buffer (#31) nor telemetry's.
+ *
+ * It filters the same baseline-relative, INTERNAL-sign value as the other
+ * chains (sample minus the stored baseline), so bed_status.c's single pitch
+ * negation still applies once.
+ *
+ * Seeding, so the shown angle does not jump: if the per-pass chain ran in
+ * the pass just before the window opened (the end of a move's settle burst,
+ * or of any burst), the filter starts from that chain's value; otherwise
+ * (after auto-level, settings mode, or a fresh subscription that found the
+ * per-pass chain idle) from the first idle sample, taken in the opening pass.
+ *
+ * No sleep veto (C5): the CPU may sleep between samples; the timer interrupt
+ * brings the main loop round for each one.
+ ******************************************************************************/
+
+static sl_sleeptimer_timer_handle_t tilt_idle_timer;
+static volatile bool tilt_idle_sample_due = false; // set in timer ISR context
+static bool     tilt_idle_open = false;        // the idle window is open
+static bool     tilt_idle_have_value = false;  // the filter holds a value
+static float    tilt_idle_roll = 0.0f;         // filter state, SCALED, baseline-relative, internal sign
+static float    tilt_idle_pitch = 0.0f;
+static int16_t  tilt_idle_roll_out = 0;
+static int16_t  tilt_idle_pitch_out = 0;
+static uint32_t tilt_idle_open_tick = 0;
+static uint32_t tilt_idle_last_sample_tick = 0;
+static bool     tilt_idle_out_of_band = false;   // LEVELED exit: outside the band since out_since
+static uint32_t tilt_idle_out_since_tick = 0;
+static bool     tilt_idle_leveled_exit = false;  // outside the band for LEVELED_EXIT_HOLD_MS
+
+#if TILT_IDLE_DIAG && !BED_CAPTURE_MOTOR_CURRENTS
+static bool     tilt_diag_running = false;
+static uint32_t tilt_diag_start_tick = 0;
+static uint16_t tilt_diag_count = 0;
+static int16_t  tilt_diag_roll_min, tilt_diag_roll_max;
+static int16_t  tilt_diag_pitch_min, tilt_diag_pitch_max;
+#endif
+
+static void tilt_idle_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
+{
+  (void)handle;
+  (void)data;
+  tilt_idle_sample_due = true; // ISR context - flag only, the tick does the read
+}
+
+static int16_t round_to_int16(float x)
+{
+  return (int16_t)((x >= 0.0f) ? (x + 0.5f) : (x - 0.5f));
+}
+
+static void tilt_idle_open_window(bool seed_from_perpass)
+{
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  tilt_idle_open = true;
+  tilt_idle_open_tick = now;
+  tilt_idle_out_of_band = false;
+  tilt_idle_leveled_exit = false;
+  if (seed_from_perpass) {
+    tilt_idle_roll  = (float)telemetry_filter.baseline_roll;
+    tilt_idle_pitch = (float)telemetry_filter.baseline_pitch;
+    tilt_idle_roll_out  = telemetry_filter.baseline_roll;
+    tilt_idle_pitch_out = telemetry_filter.baseline_pitch;
+    tilt_idle_have_value = true;
+    tilt_idle_last_sample_tick = now;
+    tilt_idle_sample_due = false;
+  } else {
+    tilt_idle_have_value = false;
+    tilt_idle_sample_due = true; // the first sample is taken in this pass
+  }
+#if TILT_IDLE_DIAG && !BED_CAPTURE_MOTOR_CURRENTS
+  tilt_diag_running = false;
+#endif
+  sl_sleeptimer_restart_periodic_timer(&tilt_idle_timer,
+                                       sl_sleeptimer_ms_to_tick(TILT_IDLE_SAMPLE_MS),
+                                       tilt_idle_timer_callback, NULL, 0, 0);
+}
+
+static void tilt_idle_close(void)
+{
+  if (!tilt_idle_open) {
+    return;
+  }
+  sl_sleeptimer_stop_timer(&tilt_idle_timer);
+  tilt_idle_open = false;
+  tilt_idle_have_value = false;
+  tilt_idle_sample_due = false;
+  tilt_idle_out_of_band = false;
+  tilt_idle_leveled_exit = false;
+#if TILT_IDLE_DIAG && !BED_CAPTURE_MOTOR_CURRENTS
+  tilt_diag_running = false; // a partial period is discarded
+#endif
+}
+
+// LEVELED exit (Prompt AI part D), one evaluation per idle sample on the
+// converged filter output. "Continuously": the hold restarts whenever a
+// sample is back inside the band. Milliseconds, not sample counts (#2).
+static void tilt_idle_leveled_exit_sample(uint32_t now)
+{
+  bool outside = (abs(tilt_idle_roll_out) > leveled_exit_band)
+                 || (abs(tilt_idle_pitch_out) > leveled_exit_band);
+  if (!outside) {
+    tilt_idle_out_of_band = false;
+    tilt_idle_leveled_exit = false;
+    return;
+  }
+  if (!tilt_idle_out_of_band) {
+    tilt_idle_out_of_band = true;
+    tilt_idle_out_since_tick = now;
+  }
+  if (sl_sleeptimer_tick_to_ms(now - tilt_idle_out_since_tick) >= LEVELED_EXIT_HOLD_MS) {
+    tilt_idle_leveled_exit = true;
+  }
+}
+
+#if TILT_IDLE_DIAG && !BED_CAPTURE_MOTOR_CURRENTS
+// One sample of the filtered output into the 10 s statistics; prints and
+// starts a new period once TILT_IDLE_DIAG_PERIOD_MS has passed.
+static void tilt_idle_diag_sample(uint32_t now)
+{
+  if (!tilt_diag_running) {
+    tilt_diag_running = true;
+    tilt_diag_start_tick = now;
+    tilt_diag_count = 0;
+    tilt_diag_roll_min = tilt_diag_roll_max = tilt_idle_roll_out;
+    tilt_diag_pitch_min = tilt_diag_pitch_max = tilt_idle_pitch_out;
+  }
+  if (tilt_diag_count < 0xFFFF) {
+    tilt_diag_count++;
+  }
+  if (tilt_idle_roll_out < tilt_diag_roll_min)   tilt_diag_roll_min = tilt_idle_roll_out;
+  if (tilt_idle_roll_out > tilt_diag_roll_max)   tilt_diag_roll_max = tilt_idle_roll_out;
+  if (tilt_idle_pitch_out < tilt_diag_pitch_min) tilt_diag_pitch_min = tilt_idle_pitch_out;
+  if (tilt_idle_pitch_out > tilt_diag_pitch_max) tilt_diag_pitch_max = tilt_idle_pitch_out;
+
+  uint32_t elapsed_ms = sl_sleeptimer_tick_to_ms(now - tilt_diag_start_tick);
+  if (elapsed_ms >= TILT_IDLE_DIAG_PERIOD_MS) {
+    // Pitch in the INTERNAL sign (head raised = negative), as every pitch
+    // printed from this file; peak-to-peak is the same either way.
+    printf("TILT idle: %u samples in %lu ms, roll min %d max %d p2p %d, pitch min %d max %d p2p %d (deg x100, pitch internal sign)\r\n",
+           (unsigned)tilt_diag_count, (unsigned long)elapsed_ms,
+           tilt_diag_roll_min, tilt_diag_roll_max, tilt_diag_roll_max - tilt_diag_roll_min,
+           tilt_diag_pitch_min, tilt_diag_pitch_max, tilt_diag_pitch_max - tilt_diag_pitch_min);
+    tilt_diag_running = false; // the next sample starts the next period
+  }
+}
+#endif
+
+// One idle sample: one read_accel(), one filter step with alpha from the
+// measured time since the previous sample.
+static void tilt_idle_sample(void)
+{
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  read_accel();
+  float roll  = (float)(latest_roll_angle_scaled  - baseline_roll_angle);
+  float pitch = (float)(latest_pitch_angle_scaled - baseline_pitch_angle);
+
+  if (!tilt_idle_have_value) {
+    tilt_idle_roll  = roll;
+    tilt_idle_pitch = pitch;
+    tilt_idle_have_value = true;
+  } else {
+    float dt_ms = (float)(now - tilt_idle_last_sample_tick) * 1000.0f
+                  / (float)sl_sleeptimer_get_timer_frequency();
+    float alpha = 1.0f - expf(-dt_ms / (float)TILT_IDLE_TAU_MS);
+    tilt_idle_roll  += alpha * (roll  - tilt_idle_roll);
+    tilt_idle_pitch += alpha * (pitch - tilt_idle_pitch);
+  }
+  tilt_idle_last_sample_tick = now;
+  tilt_idle_roll_out  = round_to_int16(tilt_idle_roll);
+  tilt_idle_pitch_out = round_to_int16(tilt_idle_pitch);
+
+  // The LEVELED exit and the statistics only on a converged value:
+  // TILT_IDLE_CONVERGED_MS after the window opened.
+  if (sl_sleeptimer_tick_to_ms(now - tilt_idle_open_tick) >= TILT_IDLE_CONVERGED_MS) {
+    tilt_idle_leveled_exit_sample(now);
+#if TILT_IDLE_DIAG && !BED_CAPTURE_MOTOR_CURRENTS
+    tilt_idle_diag_sample(now);
+#endif
+  }
+}
+
+bool tilt_idle_active(void)
+{
+  return tilt_idle_open && tilt_idle_have_value;
+}
+
+int16_t tilt_idle_roll_scaled(void)
+{
+  return tilt_idle_roll_out;
+}
+
+int16_t tilt_idle_pitch_scaled(void)
+{
+  return tilt_idle_pitch_out;
+}
+
+bool tilt_idle_leveled_exit_due(void)
+{
+  return tilt_idle_open && tilt_idle_leveled_exit;
+}
+
+bool tilt_idle_fresh(void)
+{
+  return tilt_idle_active()
+         && (sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count() - tilt_idle_last_sample_tick)
+             < TILT_IDLE_FRESH_MS);
+}
+
 // status_subscribed and other_sampler_active are passed in rather than read
 // here so this file keeps no dependency on the BLE layer or on app.c's mode
 // flags, and so both gates are visible at the call site in
@@ -454,11 +721,22 @@ void tilt_telemetry_tick(bool status_subscribed, bool other_sampler_active)
   bool motors_just_stopped = (tilt_prev_motors_on && !motors_on);
   tilt_prev_motors_on = motors_on;
 
+  // Which chains updated in this pass, for tilt_fresh (Prompt AI C4).
+  // accel_running_average() is called before this in every pass that calls it
+  // (app.c's auto-level block, then settings_mode_tick()), so this latches
+  // "auto-level's chain ran this pass". tilt_perpass_ran is set below.
+  auto_level_chain_ran = auto_level_chain_updated;
+  auto_level_chain_updated = false;
+  bool perpass_ran_last_pass = tilt_perpass_ran;
+  tilt_perpass_ran = false;
+
   // Gate 2a. No subscriber, no sampler, and no burst either - a switch-only
   // bed with no phone connected must stay bit-for-bit what it was, including
   // never vetoing sleep. This is what keeps the existing regression baseline
-  // valid for that case, so the T3 arm below sits AFTER this return.
+  // valid for that case, so the T3 arm below sits AFTER this return. The idle
+  // sampler stops too: no timer, no wakes.
   if (!status_subscribed) {
+    tilt_idle_close();
     return;
   }
 
@@ -470,18 +748,32 @@ void tilt_telemetry_tick(bool status_subscribed, bool other_sampler_active)
   // accel_running_average() every pass; running this as well would put two
   // samplers on the bus per pass and halve the effective averaging window in
   // time. Auto-level's value is what bed_status.c ships in that state anyway.
+  // The idle sampler stands down for the same reason.
   if (other_sampler_active) {
+    tilt_idle_close();
     return;
   }
 
-  // Gate 2b: is the sampling window open?
-  if (!motors_on && !tilt_burst_running) {
+  // Gate 2b: the per-pass window - a motor energised or a burst running.
+  // Unchanged chain; the idle sampler stands down while it runs.
+  if (motors_on || tilt_burst_running) {
+    tilt_idle_close();
+    read_accel();
+    accel_filter_update(&telemetry_filter);
+    tilt_telemetry_valid = true;
+    tilt_perpass_ran = true;
     return;
   }
 
-  read_accel();
-  accel_filter_update(&telemetry_filter);
-  tilt_telemetry_valid = true;
+  // Otherwise idle (Prompt AI C1): subscribed, no motor, no burst, neither
+  // auto-level nor settings mode. One read per timer flag.
+  if (!tilt_idle_open) {
+    tilt_idle_open_window(perpass_ran_last_pass);
+  }
+  if (tilt_idle_sample_due) {
+    tilt_idle_sample_due = false;
+    tilt_idle_sample();
+  }
 }
 
 int16_t returnAccelValue(axis_t axis) {
diff --git a/auto_level.h b/auto_level.h
index 0b90451..724c614 100644
--- a/auto_level.h
+++ b/auto_level.h
@@ -91,5 +91,39 @@ bool tilt_telemetry_value_valid(void);
 int16_t tilt_telemetry_roll_scaled(void);
 int16_t tilt_telemetry_pitch_scaled(void);
 
+// True if the per-pass telemetry chain updated in the current main-loop pass
+// (a motor energised or a burst running). For tilt_fresh (Prompt AI C4).
+bool tilt_telemetry_perpass_ran(void);
+
+// True if auto-level's chain (accel_running_average()) updated in the current
+// pass - during auto-level and the settings zero-level capture. For tilt_fresh.
+bool accel_auto_level_chain_ran(void);
+
+/***************************************************************************//**
+ * Idle tilt sampler (Prompt AI part C, DECISIONS #50). Runs inside
+ * tilt_telemetry_tick() while a phone is subscribed and the bed is idle (no
+ * motor, no burst, not auto-level, not settings mode): 10 Hz, one accel read
+ * per sleeptimer flag, its own millisecond-defined filter.
+ ******************************************************************************/
+
+// True while the idle window is open and its filter holds a value.
+// bed_status.c ships the idle value then.
+bool tilt_idle_active(void);
+
+// Latest idle value, SCALED, deviation from the user's baseline, INTERNAL sign
+// convention (bed_status.c applies the wire convention, as for the others).
+int16_t tilt_idle_roll_scaled(void);
+int16_t tilt_idle_pitch_scaled(void);
+
+// True while tilt_idle_active() and the last idle filter update is younger
+// than TILT_IDLE_FRESH_MS (300 ms).
+bool tilt_idle_fresh(void);
+
+// LEVELED exit (Prompt AI part D): true while the idle window is open, its
+// filter has run TILT_IDLE_CONVERGED_MS, and the filtered roll or pitch has
+// been outside LEVELED_EXIT_BAND (0.3 deg) continuously for
+// LEVELED_EXIT_HOLD_MS (2000 ms). app.c then calls app_set_leveled(false).
+bool tilt_idle_leveled_exit_due(void);
+
 
 #endif /* AUTO_LEVEL_H_ */
diff --git a/bed_actions.c b/bed_actions.c
index d0c8fea..b682547 100644
--- a/bed_actions.c
+++ b/bed_actions.c
@@ -41,8 +41,9 @@ static bool bedlights_on = false; // Tracks if bedlights are turned on/off
 static bool double_check = false; // Used to double check level only one time
 
 // Bench print: one line on every change of the LEVELED flag (Bed Status flag
-// bit 4), so the bench log shows the bit without a phone. Fires only on a
-// change, never per pass. Pre-ship: 0 (PRE_SHIP.md).
+// bit 4), and of AUTO_LEVEL_SETTLING (bit 5, Prompt AI), so the bench log
+// shows the bits without a phone. Fires only on a change, never per pass.
+// Pre-ship: 0 (PRE_SHIP.md F19).
 #define BED_DEBUG_LEVELED_LOG 1
 
 // LEVELED (Bed Status flag bit 4, DECISIONS.md #46). Set when an auto-level
@@ -67,6 +68,30 @@ bool app_leveled(void)
   return bed_leveled;
 }
 
+// AUTO_LEVEL_SETTLING (Bed Status flag bit 5). Exactly the condition under
+// which app.c's auto-level block runs Step 1 (steady_accel_read_both_axes()):
+// the run is active, no collision sequence, active_switch still AUTO LEVEL,
+// and first_check not yet cleared by the STEADY_ACCEL_READ_TIMER_TIME expiry.
+// The last two terms are what app.c's block itself requires; without them the
+// bit could read 1 while Step 1 is not running.
+bool app_auto_level_settling(void)
+{
+  return auto_level_active && !any_collision
+         && (active_switch == SWITCH_AUTO_LEVEL_VALUE) && first_check;
+}
+
+void app_auto_level_settling_log_tick(void)
+{
+#if BED_DEBUG_LEVELED_LOG && !BED_CAPTURE_MOTOR_CURRENTS
+  static bool last_settling = false;
+  bool settling = app_auto_level_settling();
+  if (settling != last_settling) {
+    last_settling = settling;
+    printf("AUTO_LEVEL_SETTLING (Bed Status bit 5) -> %u\r\n", settling ? 1u : 0u);
+  }
+#endif
+}
+
 // Shared handling for a single-side motor button press
 static void motor_button_pressed(active_switch_t switch_value, side_of_bed_t side, bool up)
 {
diff --git a/bed_actions.h b/bed_actions.h
index 8c2f0b3..db2a31d 100644
--- a/bed_actions.h
+++ b/bed_actions.h
@@ -64,4 +64,14 @@ bool app_lights_on(void);
 void app_set_leveled(bool leveled);
 bool app_leveled(void);
 
+// AUTO_LEVEL_SETTLING (Bed Status flag bit 5, Prompt AI part B): an auto-level
+// run of either source is in its Step 1 settling read. Derived each call; no
+// state. Main-loop context only.
+bool app_auto_level_settling(void);
+
+// Bench print: one line on each change of app_auto_level_settling(), behind
+// BED_DEBUG_LEVELED_LOG. Call once per app_process_action(); does nothing when
+// the flag is 0.
+void app_auto_level_settling_log_tick(void);
+
 #endif /* BED_ACTIONS_H_ */
diff --git a/bed_status.c b/bed_status.c
index 7a56bbb..19b0d04 100644
--- a/bed_status.c
+++ b/bed_status.c
@@ -100,29 +100,44 @@ void bed_status_get(bed_status_t *out)
   //       telemetry sampler stands down (its gate 2c), so auto-level's chain
   //       is the only one being fed. It is also the value auto-level is
   //       actually steering on, which is what the user wants to watch.
+  //   otherwise, while the idle sampler holds a value -> the IDLE value
+  //       (Prompt AI part C). Live at rest with a phone subscribed.
   //   otherwise, once telemetry has produced anything -> TELEMETRY's value.
-  //       Live during a manual move and during a post-move settle burst.
+  //       Live during a manual move and during a settle burst.
   //   otherwise -> auto-level's last-known value.
   //       Only before the telemetry sampler has ever run: no phone had
   //       subscribed yet, or nothing has triggered a burst since boot. Same
-  //       stale-last-known behaviour as before this change, which is why
-  //       tilt_fresh stays hardcoded 0 (DECISIONS.md #17/#33).
+  //       stale-last-known behaviour as before (DECISIONS.md #17/#33).
   //
-  // Both chains produce auto_level.c's INTERNAL sign convention, so the wire
-  // negation below applies to whichever was chosen.
+  // tilt_fresh (Bed Status flag bit 3, Prompt AI C4) goes with the choice:
+  // 1 only while the chosen chain is live - auto-level's or the per-pass
+  // telemetry chain updated in this pass, or the idle value is younger than
+  // 300 ms; 0 for the last-known fallback and for a chain that has stopped.
+  //
+  // All three chains produce auto_level.c's INTERNAL sign convention, so the
+  // wire negation below applies to whichever was chosen.
   int16_t roll_scaled;
   int16_t pitch_scaled;
+  bool tilt_fresh;
 
   if (auto_level_active || in_settings_mode) {
     roll_scaled  = baseline_accel_average_roll_angle;
     pitch_scaled = baseline_accel_average_pitch_angle;
+    tilt_fresh   = accel_auto_level_chain_ran();
+  } else if (tilt_idle_active()) {
+    roll_scaled  = tilt_idle_roll_scaled();
+    pitch_scaled = tilt_idle_pitch_scaled();
+    tilt_fresh   = tilt_idle_fresh();
   } else if (tilt_telemetry_value_valid()) {
     roll_scaled  = tilt_telemetry_roll_scaled();
     pitch_scaled = tilt_telemetry_pitch_scaled();
+    tilt_fresh   = tilt_telemetry_perpass_ran();
   } else {
     roll_scaled  = baseline_accel_average_roll_angle;
     pitch_scaled = baseline_accel_average_pitch_angle;
+    tilt_fresh   = false;
   }
+  out->tilt_fresh = tilt_fresh;
 
   out->roll_angle_scaled = roll_scaled;
 
@@ -144,6 +159,7 @@ void bed_status_get(bed_status_t *out)
   out->in_settings_mode = app_in_settings_mode();
   out->lights_on = app_lights_on();
   out->leveled = app_leveled();
+  out->auto_level_settling = app_auto_level_settling();
 
   translate_command(app_active_command(), &out->command, &out->command_target, &out->command_direction);
 
diff --git a/bed_status.h b/bed_status.h
index 8c571f3..c7214f7 100644
--- a/bed_status.h
+++ b/bed_status.h
@@ -42,10 +42,9 @@ typedef struct {
   // corner pair; see dual_motor_control.c).
   bed_motor_status_t motors[NUM_MOTORS];
 
-  // Current tilt, deviation from the user's level baseline (scaled). Only
-  // continuously fresh while auto-level or the settings zero-level capture
-  // is running - stale (last-known, possibly 0) otherwise. See
-  // DECISIONS.md #17.
+  // Current tilt, deviation from the user's level baseline (scaled), from
+  // whichever filter chain bed_status_get() picks. tilt_fresh says whether it
+  // is live (Prompt AI C4); see bed_status_get().
   //
   // SIGN CONVENTION - these are wire-facing values, and pitch does NOT match
   // auto_level.c's internal sign. bed_status_get() negates pitch exactly once
@@ -56,11 +55,13 @@ typedef struct {
   //   pitch_angle_scaled POSITIVE = HEAD raised
   int16_t roll_angle_scaled;
   int16_t pitch_angle_scaled;
+  bool tilt_fresh;           // Bed Status flag bit 3: the angles above are live (Prompt AI C4)
 
   bool any_collision;
   bool in_settings_mode;
   bool lights_on;
   bool leveled;              // last auto-level run succeeded, nothing moved since (#46)
+  bool auto_level_settling;  // an auto-level run is in its Step 1 settling read (Prompt AI)
 
   bed_command_t command;
   bed_target_t command_target;       // valid only when command == BED_COMMAND_MOVE
diff --git a/ble_bed_service.c b/ble_bed_service.c
index 29bcfee..e8dbde0 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -83,6 +83,7 @@ void ble_bed_service_encode_status(const bed_status_t *in, bool tilt_fresh, uint
   if (in->lights_on)        flags |= 0x04;
   if (tilt_fresh)            flags |= 0x08;
   if (in->leveled)          flags |= 0x10; // byte 0, so a change notifies at once (status_flags_changed())
+  if (in->auto_level_settling) flags |= 0x20; // AUTO_LEVEL_SETTLING (Prompt AI); byte 0 likewise
   out[0] = flags;
 
   // bed_source_t / bed_command_t / bed_target_t / bed_direction_t enum
@@ -148,8 +149,9 @@ void ble_bed_service_encode_status(const bed_status_t *in, bool tilt_fresh, uint
 #define CONN_SUPERVISION_TIMEOUT 100
 
 // Adaptive Bed Status notify cadence (BLE_GATT_DESIGN.md #6).
-#define STATUS_NOTIFY_IDLE_MS   1000
-#define STATUS_NOTIFY_MOVING_MS  100
+#define STATUS_NOTIFY_IDLE_MS       1000
+#define STATUS_NOTIFY_IDLE_LIVE_MS   200 // idle with live tilt: idle sampler or a burst (Prompt AI C3)
+#define STATUS_NOTIFY_MOVING_MS      100
 
 // Command characteristic wire format (BLE_GATT_DESIGN.md #4): 4 bytes,
 // { opcode, arg0, arg1, seq }.
@@ -213,6 +215,7 @@ static uint8_t status_subscriber_count = 0;
 static sl_sleeptimer_timer_handle_t status_notify_timer;
 static volatile bool status_notify_due = false;
 static uint8_t last_sent_status[gattdb_bed_status_len];
+static uint32_t status_notify_interval_ms = 0; // the interval the notify timer was last started with
 
 // Deferred Command write. The stack callback ONLY fills these in and returns;
 // every decision and every side effect happens later, in main-loop context,
@@ -341,9 +344,12 @@ static bool app_auto_level_active = false;
 //     during an app hold, or a start was refused because a collision sequence
 //     is running. Answered COLLISION_STOP. Without it the held button's next
 //     repeat re-energised the same pair, into the obstruction.
-//   STOP (DECISIONS #49): a STOP_ALL from either connection. Answered
-//     STOP_LATCHED. Without it the held button's next repeat restarted the
-//     bed right after the STOP.
+//   STOP (DECISIONS #49, amended by Prompt AI): a STOP_ALL from either
+//     connection while the app was commanding. Answered STOP_LATCHED. Without
+//     it the held button's next repeat restarted the bed right after the STOP.
+// A STOP latch is upgraded to COLLISION when a start is refused during a
+// collision sequence (collision wins, Prompt AI); COLLISION is never
+// downgraded to STOP.
 // Set ONLY through latch_app_starts(), which also starts the silence timeout,
 // so the latch can never be left set with nothing to clear it.
 typedef enum {
@@ -585,7 +591,9 @@ static void note_app_commanding(void)
 // the only place app_commanding is cleared, and it clears the latch too,
 // app_commanding is true whenever the latch is set, so the latch always clears
 // by silence MOVE_WATCHDOG_MS after the app's last start, refusal or STOP_ALL
-// (Prompt AG b.3). A COLLISION latch is never downgraded to STOP.
+// (Prompt AG b.3). A COLLISION latch is never downgraded to STOP; a STOP latch
+// is upgraded to COLLISION by the start refusal when a collision is involved
+// (Prompt AI A1).
 static void latch_app_starts(app_start_latch_t reason)
 {
   if (app_start_latch != APP_LATCH_COLLISION) {
@@ -740,6 +748,12 @@ static void process_pending_command(void)
     uint8_t conn = stop_all_conn;
     stop_all_pending = false;
 
+    // Judged before anything below can change it (Prompt AI A3). app_commanding
+    // is true while an app button is held, or for MOVE_WATCHDOG_MS after the
+    // app's last start without a stop write; it is always true while a latch
+    // is set (latch_app_starts()), so the second term only states that.
+    bool latch_stop = app_commanding || (app_start_latch != APP_LATCH_NONE);
+
     move_watchdog_cancel();
     app_move_active = false;
     app_auto_level_active = false;
@@ -754,11 +768,20 @@ static void process_pending_command(void)
     // its start; that is not a new press. Refuse app starts until the app
     // releases (an explicit stop, a disconnect, or silence). From either
     // role, and it never clears the latch: a COLLISION latch stays COLLISION.
-    latch_app_starts(APP_LATCH_STOP);
+    // Only when the app is commanding (Prompt AI A3): with the app idle there
+    // is no held button to wait for, so the user's next press is a normal
+    // start. The stop above is the same either way.
+    if (latch_stop) {
+      latch_app_starts(APP_LATCH_STOP);
+    }
 #if BLE_DEBUG_WATCHDOG_LOG
     // After the stop and the result.
-    printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
-           (app_start_latch == APP_LATCH_COLLISION) ? ", kept as collision" : "");
+    if (latch_stop) {
+      printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
+             (app_start_latch == APP_LATCH_COLLISION) ? ", kept as collision" : "");
+    } else {
+      printf("BLE: STOP_ALL (no app hold, no latch)\r\n");
+    }
 #endif
   }
 
@@ -824,8 +847,13 @@ static void process_pending_command(void)
   // App start latch (#48, #49). Motion-starting opcodes only, and before the
   // #36 check so the app is told "collision" or "stopped", not "wall switch".
   //
-  // STOP (#49): a STOP_ALL since the app last released - answered
-  // STOP_LATCHED.
+  // Collision wins (Prompt AI A1): if the latch is COLLISION or a collision
+  // sequence is running, the answer is COLLISION_STOP and the latch becomes
+  // (or stays) COLLISION, upgrading a STOP latch. Only a STOP latch with no
+  // collision is answered STOP_LATCHED.
+  //
+  // STOP (#49): a STOP_ALL while the app was commanding, since the app last
+  // released - answered STOP_LATCHED.
   //
   // COLLISION (#48): a collision during this app hold, button not yet
   // released - and also while ANY collision sequence runs: a quick release
@@ -842,12 +870,12 @@ static void process_pending_command(void)
   // no watchdog arm.
   if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
       && (app_start_latch != APP_LATCH_NONE || any_collision)) {
-    if (app_start_latch == APP_LATCH_STOP) {
-      latch_app_starts(APP_LATCH_STOP);
-      send_command_result(conn, seq, CMD_RESULT_STOP_LATCHED);
-    } else {
+    if (app_start_latch == APP_LATCH_COLLISION || any_collision) {
       latch_app_starts(APP_LATCH_COLLISION);
       send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);
+    } else {
+      latch_app_starts(APP_LATCH_STOP);
+      send_command_result(conn, seq, CMD_RESULT_STOP_LATCHED);
     }
 #if BLE_DEBUG_WATCHDOG_LOG
     if (wd_diag_latch_refused < 0xFFFF) {
@@ -1086,10 +1114,10 @@ static void status_notify_timer_callback(sl_sleeptimer_timer_handle_t *handle, v
   status_notify_due = true;
 }
 
-static void restart_status_notify_timer(bool moving)
+static void restart_status_notify_timer(uint32_t interval_ms)
 {
-  uint32_t interval_ms = moving ? STATUS_NOTIFY_MOVING_MS : STATUS_NOTIFY_IDLE_MS;
   uint32_t ticks = sl_sleeptimer_ms_to_tick((uint16_t)interval_ms);
+  status_notify_interval_ms = interval_ms;
   sl_sleeptimer_restart_timer(&status_notify_timer, ticks, status_notify_timer_callback, NULL, 0, 0);
 }
 
@@ -1111,7 +1139,7 @@ static bool status_flags_changed(const uint8_t *new_packed, const uint8_t *old_p
   return false;
 }
 
-static void publish_status(const uint8_t *packed, bool moving)
+static void publish_status(const uint8_t *packed, uint32_t interval_ms)
 {
   sl_bt_gatt_server_write_attribute_value(gattdb_bed_status, 0, gattdb_bed_status_len, packed);
   sl_status_t sc = sl_bt_gatt_server_notify_all(gattdb_bed_status, gattdb_bed_status_len, packed);
@@ -1119,7 +1147,7 @@ static void publish_status(const uint8_t *packed, bool moving)
 
   memcpy(last_sent_status, packed, gattdb_bed_status_len);
   status_notify_due = false;
-  restart_status_notify_timer(moving);
+  restart_status_notify_timer(interval_ms);
 }
 
 bool ble_bed_service_status_subscribed(void)
@@ -1139,6 +1167,12 @@ bool ble_bed_service_app_hold_active(void)
 
 void ble_bed_service_tick(void)
 {
+  // The silence release, judged first (Prompt AI A2), so "MOVE_WATCHDOG_MS
+  // since the app's last start" is decided at the moment the next command is
+  // processed - not only if some earlier pass happened to run after the
+  // deadline. Also run after the commands below, as before.
+  service_app_commanding();
+
   // Control link dropped mid-command (BLE_GATT_DESIGN.md #5). Deferred out
   // of the disconnect callback; done here, first, before anything else.
   if (disconnect_stop_pending) {
@@ -1186,11 +1220,22 @@ void ble_bed_service_tick(void)
   bed_status_get(&status);
 
   uint8_t packed[gattdb_bed_status_len];
-  ble_bed_service_encode_status(&status, false, packed); // tilt_fresh: Stage B5
-
-  if (status_notify_due || status_flags_changed(packed, last_sent_status)) {
-    bool moving = (status.command == BED_COMMAND_MOVE) || (status.command == BED_COMMAND_AUTO_LEVEL);
-    publish_status(packed, moving);
+  ble_bed_service_encode_status(&status, status.tilt_fresh, packed); // tilt_fresh: Prompt AI C4
+
+  // Cadence (BLE_GATT_DESIGN.md #6): 10 Hz moving; 5 Hz idle while the tilt
+  // is live - the idle sampler, or a burst outside settings mode (Prompt AI
+  // C3); 1 Hz otherwise. A change of cadence publishes at once, so the new
+  // rate starts now rather than after a 1 s timer still running from before
+  // (e.g. after an app MOVE_STOP, whose settle burst starts a pass later).
+  bool moving = (status.command == BED_COMMAND_MOVE) || (status.command == BED_COMMAND_AUTO_LEVEL);
+  bool idle_live = tilt_idle_active()
+                   || (tilt_telemetry_burst_active() && !app_in_settings_mode());
+  uint32_t interval_ms = moving ? STATUS_NOTIFY_MOVING_MS
+                       : (idle_live ? STATUS_NOTIFY_IDLE_LIVE_MS : STATUS_NOTIFY_IDLE_MS);
+
+  if (status_notify_due || status_flags_changed(packed, last_sent_status)
+      || (interval_ms != status_notify_interval_ms)) {
+    publish_status(packed, interval_ms);
   }
 }
 
diff --git a/ble_bed_service.h b/ble_bed_service.h
index 54a57ac..2dc100e 100644
--- a/ble_bed_service.h
+++ b/ble_bed_service.h
@@ -43,10 +43,9 @@ void ble_bed_service_debug_pass_end(void);
 
 // Packs a bed_status_t snapshot into the 20-byte Bed Status characteristic
 // wire format (BLE_GATT_DESIGN.md #6), little-endian, matching gattdb_
-// bed_status_len exactly. tilt_fresh is passed in separately rather than
-// read from bed_status_t - the continuous accel sampling that makes it
-// meaningful is a later, separately-flagged change (DECISIONS.md #17/#24);
-// until then callers pass false.
+// bed_status_len exactly. tilt_fresh is passed in separately; since Prompt AI
+// (idle sampling, DECISIONS.md #50) the caller passes bed_status_t.tilt_fresh,
+// which bed_status_get() sets with its choice of tilt chain.
 void ble_bed_service_encode_status(const bed_status_t *in, bool tilt_fresh, uint8_t *out);
 
 // Forwards every sl_bt stack event the Tilt Bed Service cares about: boot,
````

### c.2 Docs diff - `git diff cd2a148 05c4c2f -- docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/PRE_SHIP.md docs/REGRESSION.md` (complete)

````diff
diff --git a/docs/BLE_GATT_DESIGN.md b/docs/BLE_GATT_DESIGN.md
index f9ff270..1f77571 100644
--- a/docs/BLE_GATT_DESIGN.md
+++ b/docs/BLE_GATT_DESIGN.md
@@ -99,13 +99,29 @@ offset 3  seq      u8   rolling per-connection sequence number
 | 0x03 | AUTO_LEVEL_START | — | — | `bed_auto_level_start(BED_SOURCE_APP)`. **Hold-to-run** (DECISIONS #36), matching the physical auto-level button: **repeated every 100 ms while held**, and it arms and refreshes the same 300 ms dead-man watchdog a MOVE_START does. A repeat must not re-enter the verb — that would reset the levelling state machine. Release sends AUTO_LEVEL_STOP, which stops immediately. |
 | 0x04 | AUTO_LEVEL_STOP | — | — | `bed_auto_level_stop(BED_SOURCE_APP)`. |
 | 0x05 | LIGHTS_SET | 0 off / 1 on | — | `bed_lights_set(BED_SOURCE_APP, …)`. Latched. |
-| 0x06 | STOP_ALL | — | — | Explicit full stop (maps to the `app_stop_all()` path: motors + auto-level + LEDs). The app's big red button. Always accepted from **any** role, control or viewer — this is the BLE embodiment of the stop-from-anyone rule (#12). Then latches app motion starts until the app releases (DECISIONS #49; §7, result 0x07). |
+| 0x06 | STOP_ALL | — | — | Explicit full stop (maps to the `app_stop_all()` path: motors + auto-level + LEDs). The app's big red button. Always accepted from **any** role, control or viewer — this is the BLE embodiment of the stop-from-anyone rule (#12). Then, if the app is commanding (a button held, or a start in the last 300 ms with no stop write), latches app motion starts until the app releases (DECISIONS #49 as amended by Prompt AI; §7, result 0x07). With the app idle it stops the bed and latches nothing. |
 | 0x07 | REFRESH_TILT | 0 | 0 | Opens a tilt sampling burst (DECISIONS #33, trigger T2) so the app can pull a fresh angle on demand. **Commands no motion, cancels no motion.** Accepted from **any** role and in settings mode — see the arbitration note. Both args must be 0; anything else is REJECTED_INVALID. |
 
 **Implementation status: all seven opcodes are implemented** (Stage C,
 DECISIONS #34). Out-of-range args are `REJECTED_INVALID`, as is any write
 that is not exactly 4 bytes.
 
+**AUTO_LEVEL_START - when the bed first moves** (Prompt AI part B; from
+code, not measured). The first motion comes **at least 1.5 s after the first
+accepted AUTO_LEVEL_START** (`STEADY_ACCEL_READ_TIMER_TIME`, 1500 ms: Step 1,
+the settling read, while Bed Status bit 5 AUTO_LEVEL_SETTLING is 1), plus a
+few main-loop passes. Then:
+
+- if the first axis to level is more than 0.3° off, its motors start at once;
+- if it is within the pulse band (0.1° < |angle| ≤ 0.3°,
+  `PULSE_START_TOLERANCE`), the first pulse comes **a further 0.35 s** later
+  (`SNEAK_MOTOR_OFF_TIME`);
+- if **both** axes are within 0.1° (`CLOSE_TOLERANCE`), there is **no
+  motion**: the run ends with LEVELED (bit 4) set.
+
+The app should fill "Hold to level" over 1.5 s and snap to full when bit 5
+clears (§6), rather than rely on the 1.5 s alone.
+
 `STOP_ALL` does not share the deferred-command slot the other opcodes use.
 That slot is last-write-wins, which is safe for everything that is
 self-correcting — a lost MOVE_START is re-sent 100 ms later, a lost MOVE_STOP
@@ -216,7 +232,8 @@ offset 0   flags            u8   bit0 any_collision
                                  bit2 lights_on
                                  bit3 tilt_fresh   ← NEW, see below
                                  bit4 leveled      ← see "LEVELED (bit 4)" below
-                                 bits5-7 reserved (0)
+                                 bit5 auto_level_settling ← see "AUTO_LEVEL_SETTLING (bit 5)" below
+                                 bits6-7 reserved (0)
 offset 1   active_source    u8   0 NONE, 1 SWITCHES, 2 APP
 offset 2   command          u8   0 NONE, 1 MOVE, 2 AUTO_LEVEL, 3 LIGHTS
 offset 3   command_detail   u8   bits0-2 target (0-4), bit3 direction (0 down/1 up)
@@ -251,12 +268,22 @@ ends successfully. "Successfully" is any of:
 - any motion starts - a single-side move, an ALL move, a new auto-level run
   (either source), or the collision sequence's reverse;
 - a collision trips (summed rise or max load), in the same pass as the trip;
-- settings mode is entered.
+- settings mode is entered;
+- **the bed is tilted off level while idle** (added 2026-11-03, Prompt AI
+  part D, DECISIONS #46 amendment): with a phone subscribed and the idle tilt
+  sampler running (§6 table above), once its filter has run 1.5 s since the
+  idle window opened, the filtered roll or pitch is more than **0.3°** from
+  the level baseline **continuously for 2 s**. A brief bump that comes back
+  inside 0.3° restarts the 2 s. Both numbers are bench-tunable
+  (`LEVELED_EXIT_BAND`, `LEVELED_EXIT_HOLD_MS`). With no phone subscribed
+  nothing is sampled, so this cannot happen then.
 
 Nothing else clears it. In particular it **stays set** while the user keeps
 holding AUTO LEVEL after the run succeeded, and after the release, and
 through STOP / STOP_ALL, a watchdog stop or a disconnect stop - none of those
-moves the bed. It is 0 at boot.
+moves the bed. It is 0 at boot. **It is never set from tilt**: only a
+successful auto-level run sets it, so a bed that happens to sit within 0.3°
+does not read as "levelled".
 
 **App use:** "levelled - you can let go" while AUTO LEVEL is still held
 (`command` still reads AUTO_LEVEL until release), and "bed is level" on the
@@ -267,6 +294,34 @@ A change of bit 4 is a state-flag change, so it is notified at once
 other flag bits. Collision during a run is shown by bit 0, not by bit 4: a
 run that ends in a collision never sets bit 4.
 
+### AUTO_LEVEL_SETTLING (bit 5)
+
+Added 2026-11-03 (Prompt AI part B; DECISIONS #46, Prompt AI amendment), within protocol
+v1 - see "Protocol version and this change" below.
+
+**1** while an auto-level run, from either source, is in its **Step 1
+settling read**: the firmware has accepted the start and is waiting
+`STEADY_ACCEL_READ_TIMER_TIME` (1500 ms, `auto_level.c`) for the frame to
+settle before it reads the level of both axes. No motor runs during Step 1.
+**0** otherwise - before the run, from the moment Step 1's reading is taken
+(the run then levels, or ends at once if no levelling is needed), after the
+release, a STOP, a collision, and at all times outside auto-level.
+
+Precisely, it is the condition under which the firmware runs Step 1:
+`auto_level_active && !any_collision && active_switch == AUTO_LEVEL &&
+first_check` (`app_auto_level_settling()`, `bed_actions.c`). A change is a
+state-flag change (byte 0), so it is notified at once, like bits 0-4.
+
+**App use:** fill the "Hold to level" button over 1.5 s from the bit going
+to 1, and snap it to full when the bit clears. The 1.5 s is only for the
+animation; the bit gives the exact start and end, so a later retune of the
+firmware constant does not desynchronise the app. Expected sequence for an
+app hold: the first `0x00` to AUTO_LEVEL_START, bit 5 -> 1 in the same or
+the next notification, ~1.5 s later bit 5 -> 0, then either the first motor
+`on` bit (levelling), or LEVELED (bit 4) -> 1 with no motion (nothing to
+level), or - when the first axis is within the pulse band - no motion for a
+further ~0.35 s before the first pulse (§4).
+
 ### Tilt sign conventions
 
 **Pitch — SPECIFICATION. `POSITIVE = HEAD raised`.**
@@ -336,23 +391,43 @@ layout does not change, and a client that ignores bit 4 behaves exactly as
 before. Amending v1 rests on the same pre-ship reasoning as above, and the
 after-first-ship rule above is unchanged by it.
 
-**`tilt_fresh` requires a one-bit firmware addition** implementing
-DECISIONS #17's app-era plan: continuous accelerometer sampling runs at a
-variable cadence *only while at least one connection has Status
-notifications enabled* — occasional (~1 Hz) when idle, fast while moving or
-auto-leveling. `tilt_fresh = 0` whenever the last sample is older than the
-staleness threshold; the app greys out the tilt display rather than showing
-a stale angle as live.
+**AUTO_LEVEL_SETTLING, flag bit 5 (2026-11-03, Prompt AI): also amended in
+place, no version bump**, on the same grounds as bit 4: additive, the bit was
+reserved and always 0, the byte layout is unchanged, a client that ignores it
+behaves exactly as before.
+
+**`tilt_fresh` (bit 3) and live tilt - implemented 2026-11-03 (Prompt AI
+part C, DECISIONS #50).** This was DECISIONS #17's app-era plan; it was
+specified here but `tilt_fresh` was always 0 until then. Accelerometer
+sampling runs *only while at least one connection has Status notifications
+enabled*:
+
+| State (subscribed) | Tilt on offsets 4-7 | Sampled | `tilt_fresh` |
+|---|---|---|---|
+| Auto-level, or settings mode | auto-level's chain | every main-loop pass while it runs (settings: only during the zero-level capture) | 1 in a pass where that chain ran; 0 otherwise (settings outside the capture; the pass that starts a run) |
+| A motor energised, or a 500 ms burst (after subscribing, REFRESH_TILT, or the end of a move) | the per-pass telemetry chain (#33) | every pass | 1 |
+| Idle - no motor, no burst, not auto-level, not settings | the idle chain: 10 Hz, its own 500 ms time-constant filter | every 100 ms | 1 while the last idle value is younger than 300 ms; 0 if sampling stalls |
+| No sample since boot | auto-level's last-known value | - | 0 |
+
+The app greys out the tilt display when `tilt_fresh` is 0 rather than showing
+a stale angle as live. Bit 3 is in byte 0, so a change is notified at once.
+Values are deviations from the stored level baseline in every state, and the
+sign conventions below apply to all of them.
 
 Notify cadence (adaptive, subscription-driven):
 
 | Bed state | Cadence |
 |---|---|
 | No subscribers | none (and no extra accel sampling) |
-| Idle, subscribed | 1 Hz, plus immediate on any state-flag change |
+| Idle, subscribed, tilt live (idle sampler, or a burst outside settings mode) | 5 Hz (Prompt AI), plus immediate on any state-flag change |
+| Idle, subscribed, otherwise (settings mode) | 1 Hz, plus immediate on any state-flag change |
 | Moving / auto-leveling | 10 Hz |
 | Collision / fault edge | immediate |
 
+A change between these cadences publishes at once, so a new rate starts
+immediately. Additive within v1: no byte moves, and a client that ignores
+`tilt_fresh` sees the same angles more often.
+
 ## 7. Command Result characteristic (0002)
 
 Notify, 2 bytes: `{ seq u8, result u8 }`, echoing the command's seq.
@@ -403,9 +478,14 @@ the finger; a new press is a new hold. The firmware does not rely on this:
 the repeats of any hold that met a collision are refused until it releases,
 including a new press made while the collision sequence is still running.
 
-**STOP_LATCHED (0x07), DECISIONS #49.** A STOP_ALL - from the control or a
-viewer connection - stops the bed (answered 0x00 to the sender, as always) and
-then sets the firmware's **stop latch**. While it is set, every MOVE_START and
+**STOP_LATCHED (0x07), DECISIONS #49 (amended by Prompt AI, 2026-11-03).** A
+STOP_ALL - from the control or a viewer connection - stops the bed (answered
+0x00 to the sender, as always). **If the app is commanding at that moment** -
+an app button held, or the app's last MOVE_START / AUTO_LEVEL_START less than
+300 ms ago with no MOVE_STOP / AUTO_LEVEL_STOP since - it then sets the
+firmware's **stop latch**. With the app idle, a STOP_ALL stops the bed exactly
+the same way but sets no latch, and the next press is a normal start. While
+the latch is set, every MOVE_START and
 AUTO_LEVEL_START from the control connection is answered 0x07, echoing that
 command's own seq, and starts nothing; it does not arm the watchdog. The user
 may still have a finger on a held app button, and the app keeps repeating its
@@ -415,19 +495,29 @@ in answer to a start; a STOP_ALL itself is answered 0x00.
 The stop latch clears exactly like the collision latch: MOVE_STOP,
 AUTO_LEVEL_STOP, the control connection dropping while the app holds the bed,
 or 300 ms with no MOVE_START / AUTO_LEVEL_START (accepted or refused) and no
-STOP_ALL from the app. A further STOP_ALL does not clear it. So:
+latching STOP_ALL. A further STOP_ALL does not clear it (while latched the app
+is commanding, so a further STOP_ALL re-latches and restarts the 300 ms). So:
 
 - a held app button after a STOP stays stopped until the finger is lifted,
   then a fresh press is a normal start;
-- a press made within 300 ms of a STOP tapped with nothing held is refused
-  (0x07) and must be released and pressed again; a press after that is a
-  normal start.
-
-If the collision latch (0x06) is already set when a STOP_ALL arrives, it stays
-a collision latch and starts keep getting 0x06. While the stop latch is set,
-a start is answered 0x07 even if a collision sequence is running (Bed Status
-bit 0). Checked in the same place as 0x06: after the viewer and settings-mode
-rejections, before the #36 two-source check.
+- a STOP tapped with nothing held sets no latch, so a press right after it is
+  a normal start. (Before Prompt AI it latched for 300 ms and such a press was
+  refused.) The exception is a STOP within 300 ms of the app's last start
+  when no stop write arrived (a lost MOVE_STOP): the app still counts as
+  commanding, so it latches, and clears by silence 300 ms after that STOP.
+
+The 300 ms is judged when the next command is processed (Prompt AI): a start
+that arrives 300 ms or more after the app's last start, refusal or latching
+STOP_ALL finds the latch already released and is a normal start.
+
+**Collision wins.** If the collision latch (0x06) is already set when a
+STOP_ALL arrives, it stays a collision latch and starts keep getting 0x06. If
+the stop latch is set and a start arrives while a collision sequence is
+running (Bed Status bit 0), the start is answered **0x06** and the latch
+becomes a collision latch (before Prompt AI it was answered 0x07). A
+collision latch never turns back into a stop latch. Checked in the same place
+as 0x06: after the viewer and settings-mode rejections, before the #36
+two-source check.
 
 App behaviour: on 0x07, end the hold (stop repeating; a MOVE_STOP /
 AUTO_LEVEL_STOP is fine) and show "Stopped - release and press again" until
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 8292ab6..74f457f 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2364,6 +2364,49 @@ button was not used in this run (no `SW: 0x0002` line).
 REGRESSION 6.1 (the prints), 6.8 and 6.10b pass for the app path. Not
 covered: the physical button (6.10a), 6.10d, 6.10e, 6.10f.
 
+### Amended by Prompt AI (2026-11-03)
+
+Reviewer's Prompt AI, from the Prompt AH handoff (b.C, C3-C4, QUESTIONS f.8).
+The text above is left as written and is superseded where it says otherwise.
+
+1. **Flag bit 5 = AUTO_LEVEL_SETTLING (Prompt AI part B).** Next to bit 4 in
+   the same byte: 1 while an auto-level run of either source is in its Step 1
+   settling read, 0 otherwise. Defined as exactly the condition under which
+   `app.c`'s auto-level block runs Step 1 - `auto_level_active &&
+   !any_collision && active_switch == SWITCH_AUTO_LEVEL_VALUE && first_check`
+   (`app_auto_level_settling()`, `bed_actions.c`) - the prompt's
+   `auto_level_active && first_check` plus the block's own two guards, so the
+   bit cannot read 1 while Step 1 is not running. Encoded `flags |= 0x20`;
+   byte 0, so a change is notified at once. Bench print behind
+   `BED_DEBUG_LEVELED_LOG`: `AUTO_LEVEL_SETTLING (Bed Status bit 5) -> 0|1`,
+   one line per change, from `app_auto_level_settling_log_tick()` called once
+   per pass right after the auto-level block. **Why:** the app fills "Hold to
+   level" while the bed takes its first reading; a fixed 1.5 s in the app
+   would silently go wrong if the firmware constant changed, the bit gives
+   the exact start and end. Additive within protocol v1 (BLE_GATT_DESIGN §6);
+   no Protocol Version change. Builds clean; not run on the bench.
+2. **LEVELED (bit 4) clears itself when the bed is tilted off level while
+   idle (Prompt AI part D).** A new "cleared when": with a phone subscribed
+   and the idle tilt sampler running (DECISIONS #50), once its filter has run
+   `3 x TILT_IDLE_TAU_MS` (1500 ms) since the idle window opened, the filtered
+   roll or pitch is outside `LEVELED_EXIT_BAND` (0.3°, scaled 30) continuously
+   for `LEVELED_EXIT_HOLD_MS` (2000 ms) - evaluated on every idle sample, the
+   hold restarting whenever a sample is back inside the band
+   (`tilt_idle_leveled_exit_sample()`, `auto_level.c`). Then `app.c` calls
+   `app_set_leveled(false)` right after `tilt_telemetry_tick()`; it prints
+   `LEVELED (Bed Status bit 4) -> 0` once and the change is notified at once
+   (byte 0). **Bit 4 is still never SET from tilt**: it sets only at a
+   successful auto-level end, as above. No IN_LEVEL_BAND bit. **Why:** "Level
+   reached" on the app must not stay up after the bed has been tilted by hand
+   or moved by someone getting in; 0.3° is auto-level's pulse threshold
+   (three times the 0.1° "no levelling needed" band, so it cannot flicker
+   against a just-levelled bed), 2 s rejects a person sitting down. Both
+   numbers are to be tuned on the bench from the `TILT idle` noise lines (#50):
+   the band should stay well above the peak-to-peak at rest. Nothing samples
+   with no phone subscribed, so the bit cannot clear then; it is re-judged
+   1.5 s after the next idle window opens. A REFRESH_TILT or any burst closes
+   the idle window and restarts the 1.5 s. Builds clean; not run on the bench.
+
 ## 47. BED LIGHTS toggles the lights on the release of a short press
 
 Date: 2026-11-02. User decision 2026-11-02, implemented per the reviewer's
@@ -2773,3 +2816,216 @@ bed_control.c only and cannot reach this code). **Not run on the bench.**
 REGRESSION §5 (5.A1-5.A6 updated) and §5b (new). What happens in each case,
 and the open points, are in the Prompt AH handoff
 (`fw_HANDOFF_2026-11-03_01_...`, b.A).
+
+### Amended by Prompt AI (2026-11-03)
+
+Reviewer's rulings on the Prompt AH handoff (`fw_HANDOFF_2026-11-03_01_...`,
+QUESTIONS f.1-f.3). `ble_bed_service.c` only; the text above is left as
+written and is superseded where it says otherwise. Line numbers are after the
+change.
+
+1. **Collision wins (f.1).** The start refusal (`:868-883`) now tests
+   COLLISION first: if the latch is COLLISION or `any_collision` is set, the
+   start is answered 0x06 and the latch is set to COLLISION - upgrading a STOP
+   latch; otherwise (latch STOP, no collision) 0x07. `latch_app_starts()`
+   (`:594-600`) already permitted the upgrade: its only guard refuses to
+   overwrite COLLISION, so STOP -> COLLISION happens and COLLISION -> STOP
+   never does. Replaces "With the stop latch set, a start is answered 0x07
+   even during a collision sequence (the STOP check comes first)". **Why:** a
+   collision is the more specific and more serious cause; the user should see
+   the collision screen, and the latch should carry the collision reason.
+2. **The silence release is judged first (f.2).** `service_app_commanding()`
+   is also called at the top of `ble_bed_service_tick()` (`:1171`), before the
+   disconnect stop and `process_pending_command()`; the existing call after
+   `service_move_watchdog()` stays (`:1190`). So "300 ms since the app's last
+   start" is decided when the next command is processed, not only if some
+   earlier pass ran after the deadline. What it changes (ASSERTED from code):
+   - A start processed 300 ms or more after the app's last start, refusal or
+     latching STOP_ALL now always finds the start latch NONE and the app's
+     half of the #36 latch released. Before, that held only if a main-loop
+     pass ran between the deadline and the command's own pass; otherwise the
+     start was refused (0x07 / 0x06) and re-latched for its whole hold.
+   - STOP latch: a press 300 ms or more after a latching STOP is a normal
+     start (AH A6.3 (b) no longer depends on a pass in between).
+   - COLLISION latch: such a press is still refused 0x06 and re-latched while
+     the collision sequence runs (`|| any_collision`); after the sequence it is
+     a normal start. Same results as before whenever a pass ran in the gap.
+   - #36: a held repeat that arrives 300 ms or more after the previous one (a
+     link stall) is a new press: the silence release clears the app's half of
+     the inhibit and, if the switches have released too, the inhibit clears
+     and the repeat starts the bed. This was already the case whenever a pass
+     ran in the gap (the trade-off stated in the AH handoff f.2).
+   - **One new case, narrow:** during an accepted app hold, if the late
+     repeat is processed in the very first pass after the deadline and the
+     watchdog expiry has not yet been acted on, the silence release now runs
+     first (bed_control's `source_commanding[APP]` becomes false), and the
+     repeat is then an identical repeat (`app_move_active` /
+     `app_auto_level_active` still true) that refreshes the watchdog - the move
+     continues with bed_control believing the app released. If the wall switch
+     is then pressed during that hold, the conflict stop and the inhibit happen
+     as usual, but the inhibit clears at the switch release and the app's next
+     held repeat starts the bed: #12's "stop then hand over" for that one hold.
+     Before, the repeat was processed first and pushed the deadline, so the app
+     stayed commanding. The window is one main-loop pass at the 300 ms point of
+     a stall (passes run every few ms during a hold, #45). Reported in the
+     Prompt AI handoff QUESTIONS; not changed.
+   - The disconnect stop in a pass where the deadline has also passed: the
+     latch print says `silence` instead of `disconnect`; the stop is the same.
+   - The watchdog expiry: in the expiry pass the silence release now runs
+     before the watchdog stop instead of after; the stop is the same.
+3. **STOP_ALL latches only when the app is commanding (f.3).** The STOP_ALL
+   branch (`:743-783`) first takes `latch_stop = app_commanding ||
+   app_start_latch != APP_LATCH_NONE` (`:752`) - an app button held, or the
+   app's last start less than 300 ms ago with no stop write, or a latch
+   already set (the second term is implied by the first: a latch is never set
+   without `app_commanding`). Everything STOP_ALL did before is unchanged
+   (both roles, watchdog cancelled, both app flags cleared, the app's half of
+   #36 released, `app_stop_all()`, 0x00 to the sender). Then only if
+   `latch_stop`: `latch_app_starts(APP_LATCH_STOP)` and `BLE: STOP_ALL latch
+   set (conn=N)` (`, kept as collision` as before); otherwise no latch and
+   `BLE: STOP_ALL (no app hold, no latch)` (same flag, `BLE_DEBUG_WATCHDOG_LOG`).
+   **Why:** with no app button held there is nothing to wait for; latching made
+   the user's next press within 300 ms fail for no reason. Per role: a
+   viewer's STOP_ALL while the control phone holds still latches (the control
+   app is commanding); a viewer's STOP_ALL with the control app idle does not.
+   After a clean release (MOVE_STOP / AUTO_LEVEL_STOP), `app_commanding` is
+   false at once, so a STOP right after it does not latch; after a release
+   whose stop write was lost it latches until silence.
+
+**Status:** builds clean (no warnings) with `BLE_DEBUG_WATCHDOG_LOG` 1 and 0;
+host test unchanged. **Not run on the bench.** REGRESSION §5b updated.
+What each AH A6 case does now is in the Prompt AI handoff.
+
+## 50. Live idle tilt: a 10 Hz idle sampler with its own millisecond filter, 5 Hz idle notify, `tilt_fresh` live
+
+Date: 2026-11-03. Reviewer's Prompt AI part C (Option B of the Prompt AH
+handoff b.C2, "as recommended"). Implements the idle sampling #33 deferred and
+PRE_SHIP F16 (`tilt_fresh`).
+
+### The problem
+
+With a phone open and the bed still, the accelerometer was read only during a
+500 ms burst - after the app subscribed (T1), after a REFRESH_TILT (T2), after
+a move (T3). The rest of the time Bed Status carried the last value of the
+last burst, notified once a second, and `tilt_fresh` was always 0. The user
+wants the level display live while the app is open, and part D's
+self-clearing LEVELED needs a live value to clear on.
+
+### Decision
+
+**The idle window** is: at least one connection subscribed to Bed Status, no
+motor energised, **no burst running**, auto-level not running, settings mode
+not active. In it (`auto_level.c`, the idle sampler block, `:476-665` at the part C commit):
+
+- **Sampling, 10 Hz.** A periodic sleeptimer (`TILT_IDLE_SAMPLE_MS` 100,
+  started when the window opens, stopped when it closes) whose callback only
+  sets `tilt_idle_sample_due` - ISR context, no I2C (#27 rule 1).
+  `tilt_telemetry_tick()`, main loop, does **one `read_accel()` per flag**;
+  `read_accel()` keeps its own `ACCEL_I2C_MIN_INTERVAL_MS` gate (#30).
+- **Filter.** A first-order low-pass on its own state, `y += alpha * (x - y)`
+  with `alpha = 1 - exp(-dt / TILT_IDLE_TAU_MS)`, `dt` the **measured** time
+  between samples (sleeptimer ticks), `TILT_IDLE_TAU_MS` = 500. `x` is the
+  sample minus the stored baseline, in the internal sign convention - exactly
+  what the other chains produce - so `bed_status.c`'s single pitch negation
+  still applies once. Output rounded to `int16_t`.
+- **Seed.** If the per-pass telemetry chain ran in the pass just before the
+  window opened (the end of a move's T3 burst, or of any burst), the filter
+  starts from that chain's value, so the shown angle does not jump at the end
+  of a move or burst. Otherwise (after auto-level, after settings mode, or a
+  window that opens with the per-pass chain idle) it starts from the first
+  idle sample, taken in the opening pass.
+- **On the wire.** `bed_status_get()` ships the idle value while the window is
+  open (after auto-level/settings and before the per-pass chain in its
+  order). Notify cadence: **5 Hz** (`STATUS_NOTIFY_IDLE_LIVE_MS` 200) when not
+  moving and the tilt is live - the idle window open, or a burst running
+  outside settings mode; 10 Hz moving, unchanged; 1 Hz otherwise. A change of
+  cadence publishes at once (without that, the first 5 Hz notify after an
+  app move waited up to 1 s on the timer started at the move's end).
+- **`tilt_fresh` (bit 3).** 1 while the shipped chain is live: auto-level's
+  chain or the per-pass telemetry chain updated in this pass, or the idle
+  value is younger than `TILT_IDLE_FRESH_MS` (300 ms, three sample periods).
+  0 for the auto-level last-known fallback (no phone has ever caused a
+  sample), in settings mode outside the zero-level capture, in the pass that
+  starts an auto-level run (auto-level's chain has not run yet in that pass),
+  and whenever the idle value is 300 ms or more old (the main loop or the
+  read stalled).
+- **Sleep.** No new EM0 veto: the CPU may sleep between samples. Each 100 ms
+  timer interrupt brings the main loop round (`sl_power_manager_sleep_on_
+  isr_exit()` returns "do not sleep" for a wake that was not the power
+  manager's own unless the Bluetooth or iostream hook asks to sleep -
+  `autogen/sl_power_manager_handler.c:77-114`), one pass runs, the CPU sleeps
+  again.
+- **Bench diagnostics** behind `TILT_IDLE_DIAG` (1, PRE_SHIP): every 10 s while
+  the sampler runs, starting once the filter has run `3 x TILT_IDLE_TAU_MS`
+  since the window opened, one line: `TILT idle: N samples in T ms, roll min
+  .. max .. p2p .., pitch min .. max .. p2p .. (deg x100, pitch internal
+  sign)`.
+
+### Why a time-based filter, and what this reverses of #33
+
+#33 gave the telemetry chain **the same filter as auto-level** (130-slot
+running average plus LPF alpha 0.1, per pass) so the two could not drift
+apart. That filter counts passes. At 10 Hz its 130 slots would span 13 s and
+its LPF about 1 s - the sluggish display #32 warned about - and the idle loop
+rate is not a constant anyway (#29). So the idle chain gets a filter defined
+by its **time constant in milliseconds** (#2, #32's constraint), independent
+of the sample rate. **This partly reverses #33's "same filter in both
+chains", for the idle case only.** The moving chain (per-pass telemetry during
+a move or burst) and auto-level's chain are unchanged and stay pass-based
+(#29, PRE_SHIP F15). #31 is untouched: the idle sampler writes only its own
+state.
+
+### Stand-down rules
+
+The idle sampler stops (timer stopped, state dropped, a partial diagnostics
+period discarded) whenever any of these holds, checked every pass in
+`tilt_telemetry_tick()`:
+
+- no connection subscribed (gate 2a) - a switch-only bed with no phone is
+  bit-for-bit unchanged: no timer, no wakes;
+- auto-level or settings mode (gate 2c) - their own chain samples every pass;
+- a motor energised or a burst running (gate 2b) - the per-pass chain samples
+  every pass and is shipped.
+
+So at most one of the three chains reads the accelerometer in any pass.
+
+### Risks (Prompt AH handoff b.C2) and how the bench checks them
+
+1. **A periodic wake next to the radio's schedule while idle** has never been
+   on the bench (#33 deferred idle sampling for this reason). Bus load is
+   small - one write-read (1 byte out, 6 in) per 100 ms, ~0.85 ms wire time,
+   at most ~1.4 ms measured (#30): **~0.9-1.4 % occupancy**, against the
+   ~13 % #30 proved safe during auto-level. The 200 ms motor-fault poll
+   (`exp_board.c:27`) also runs on these passes, at most 5 reads a second.
+   Bench: REGRESSION §6b - the link stays up with the app open at rest for
+   minutes, `BLE_DEBUG_WATCHDOG_LOG` 1; the holds of §5 and §6 still show no
+   watchdog stop.
+2. **The read blocks the main loop** up to ~1.4 ms in an idle pass. No motor
+   runs then (gate 2b); the switch sampler's 10 ms cadence absorbs it.
+3. **Two filter algorithms** meet at the window edges. Per-pass -> idle:
+   seeded, no step. Idle -> per-pass (a move or burst starts): the per-pass
+   chain resumes from its own buffer, last filled at the end of its previous
+   window; if the bed was tilted while idle, the shown value steps back to
+   that old value and catches up over its ~130-pass window (~0.2-0.3 s at the
+   #29 loop rates). After an auto-level run that ended without motion the idle
+   seed is one raw sample (a step the size of one sample's noise). Bench:
+   REGRESSION §6b, "after a move" and "after auto-level".
+4. **The moving and auto-level chains stay pass-based** - F15 is still open.
+5. **A phone that stays subscribed in the background** keeps the 10 Hz
+   sampler and the 5 Hz notify running. The app will unsubscribe when
+   backgrounded (app prompt); nothing in the firmware now. PRE_SHIP row F30.
+6. **The new filter needs its own acceptance**: noise at rest (the 10 s
+   `TILT idle` lines, which also size part D's exit band) and lag when the
+   bed is tilted by hand (part D's check). Expected lag from tau alone: a 1°
+   step crosses 0.3° after ~0.18 s and reaches 95 % after 1.5 s.
+
+Sleep (PRE_SHIP F11): ten short wakes a second plus the 5 Hz notify, ~1.5-2
+ms each in EM0 (the read, `read_accel()`'s double-precision `atan2`/`sqrt`,
+`expf`), so an extra **~2-3 %** EM0 duty while a phone is subscribed and the
+bed is idle - an estimate, not measured. F11 now asks for the idle current
+with and without a phone subscribed.
+
+### Status
+
+Builds clean (no warnings) with `TILT_IDLE_DIAG` 1 and 0; text +1,968 B
+(`expf` is 120 B of it). **Not run on the bench.** REGRESSION §6b.
diff --git a/docs/PRE_SHIP.md b/docs/PRE_SHIP.md
index 79bf953..aead975 100644
--- a/docs/PRE_SHIP.md
+++ b/docs/PRE_SHIP.md
@@ -32,24 +32,26 @@ File:line references are to the firmware at `6796e6b` unless marked.
 | F8 | Pairing window has no timeout | A forgotten settings mode leaves new bonding open until a BED LIGHTS press or a reset | Claude / Andrew (re-opens user decision #42.3) | Decision: keep "no timer" with a recorded reason, or add a timeout | DECISIONS #42 decision 3; Prompt V handoff (2026-10-18_01) QUESTIONS 5 |
 | F9 | I2CSPM blocking timeout is 300 s | SDK 2026.6.0 passes `SL_I2CSPM_TRANSFER_TIMEOUT` 300000 as milliseconds; a hung bus could block one main-loop pass for up to 300 s, with the watchdog and fail-safe unable to run | Claude (own prompt), Claude Code | Own prompt: a bounded timeout without hand-editing the SDK, then a bench check | Prompt Y handoff QUESTIONS 5; reviewer ruling 5 (SESSION_LOG 2026-11-01) |
 | F10 | Behaviour with the Switch board unplugged, and per-board read failures | Designed but never bench-run; a board that fails while the other answers is never marked absent | Andrew (bench) | Bench: unplug each UI board at idle and during a hold; record what moves and what prints | Sampler handoff (2026-10-17_05) QUESTIONS 4 and 6 |
-| F11 | Sleep clock: LFRCO in default precision; EM2 likely never reached. **Test before shipping (Andrew).** | The SDK reports the LFRCO as 0xFFFF ppm and the BLE controller then holds the device in EM1 (`sl_btctrl_init.c:289-298`), so idle current is higher than EM2 and the radio's sleep-clock timing is unexplained | Andrew (bench); configurator change by Andrew | 1) Measure idle current, connected and not. 2) Set LFRCO high precision in the configurator; repeat 6.9 **with** DECISIONS #45 and **without** it (temporary build); measure idle current again | Prompt AA handoff part C (C1-C4) and QUESTIONS 3-4; DECISIONS #45 |
+| F11 | Sleep clock: LFRCO in default precision; EM2 likely never reached. **Test before shipping (Andrew).** | The SDK reports the LFRCO as 0xFFFF ppm and the BLE controller then holds the device in EM1 (`sl_btctrl_init.c:289-298`), so idle current is higher than EM2 and the radio's sleep-clock timing is unexplained | Andrew (bench); configurator change by Andrew | 1) Measure idle current, connected and not - and, since the idle tilt sampler (DECISIONS #50, 2026-11-03), **with and without a phone subscribed to Bed Status** (subscribed: 10 Hz accel reads and 5 Hz notifies, estimated +2-3 % EM0 duty). 2) Set LFRCO high precision in the configurator; repeat 6.9 **with** DECISIONS #45 and **without** it (temporary build); measure idle current again | Prompt AA handoff part C (C1-C4) and QUESTIONS 3-4; DECISIONS #45; Prompt AI C5 |
 | F12 | Dead-man watchdog margin at a realistic distance | The 2026-11-01 pass (longest refresh gap 119-181 ms of 300 ms) was a desk bed at desk range | Andrew (bench) | Repeat REGRESSION 6.9 (10 app auto-level holds) in the van at a realistic phone distance, `BLE_DEBUG_WATCHDOG_LOG` 1; read "longest refresh gap" on every hold-end line | DECISIONS #45 bench result; Prompt AC |
 | F13 | Full REGRESSION.md pass on current firmware | No full pass since 2026-07-19 (`af369f2`); Stage C, the sampler, the pairing window and #45 have landed since | Andrew (bench) | Full pass recorded in a new REGRESSION_RECORD file | MIGRATION_STATUS Phase 6 |
 | F14 | Release-config confirmation pass | Every recorded pass was a Debug build | Andrew (bench) | Release build, then the pass (or the subset the reviewer names) | REGRESSION_RECORD_PRE_STAGEC.md:38; MIGRATION_STATUS Phase 6 |
 | F15 | DECISIONS #29 timing constants review | Auto-level's filter (`NUM_ACCEL_READINGS`, `accel_alpha`) and the collision path (`NUM_CURRENT_READINGS`, `NUM_INITIAL_CURRENTS_IGNORED`, `NUM_CURRENTS_READ_DYNAMIC_THRESHOLD`, `alpha`) count loop passes, not milliseconds (DECISIONS #2) | Claude (design), Claude Code | Design prompt; change; REGRESSION §5 and §6 with a loop-rate measurement | DECISIONS #29 |
-| F16 | `tilt_fresh` (Stage B5) | Bed Status flag bit 3 is always 0 (`ble_bed_service.c:1057`), so the app cannot tell a fresh angle from a stale one | Claude decides | Implement, or ship without it with the app treating tilt as possibly stale | DECISIONS #17, #33; BLE_GATT_DESIGN §6 |
+| F16 | `tilt_fresh` (Stage B5) | Bed Status flag bit 3 is always 0 (`ble_bed_service.c:1057`), so the app cannot tell a fresh angle from a stale one. **Implemented 2026-11-03** (Prompt AI C4, DECISIONS #50; BLE_GATT_DESIGN §6 table); not yet bench-checked | Claude decides | Implement, or ship without it with the app treating tilt as possibly stale. Now: bench REGRESSION §6b (bit 3 = 1 at rest with the app open, during moves and auto-level), then close | DECISIONS #17, #33, #50; BLE_GATT_DESIGN §6 |
 | F17 | Gecko bootloader on every board before it ships | A board without the bootloader can never be updated over the air. Note: the application is linked at flash address 0x0 (`autogen/linkerfile.ld:32`), so the app's flash layout must change with it (configurator). **Extended 2026-11-02 (reviewer ruling on Prompt AC QUESTIONS 7): the bootloader and the flash layout need their own design** - where the bootloader sits, where the app is relinked, whether OTA stages a second image (the image is ~240 KiB with ~229 KiB free below NVM3, not the ~53 KB BLE_GATT_DESIGN §10 assumed) | Claude (plan, own design prompt), Andrew (configurator, flashing) | Design prompt and its record first; then bootloader project built; app relinked; each board flashed and checked | BLE_GATT_DESIGN §10; Prompt AC handoff B9, QUESTIONS 7; Prompt AE part E |
 | F18 | "Connection stalled" shown when the physical controller takes over from the app | The app's refused repeats do not refresh the watchdog, so ~300 ms later the app gets WATCHDOG_STOP although the link is fine (`ble_bed_service.c:746-750`) | Claude decides; Claude Code (firmware or app) | A distinct result code, or app handling of FORCE_STOPPED then WATCHDOG_STOP; bench check | Prompt AA handoff QUESTIONS 5 |
-| F19 | `BED_DEBUG_LEVELED_LOG` 1 -> 0 (`bed_actions.c`) | Bench print `LEVELED (Bed Status bit 4) -> 0/1` on every change of bit 4 | Claude Code | Change to 0, build, check the `LEVELED` lines are gone | Prompt AE part F; DECISIONS #46 |
+| F19 | `BED_DEBUG_LEVELED_LOG` 1 -> 0 (`bed_actions.c`) | Bench print `LEVELED (Bed Status bit 4) -> 0/1` on every change of bit 4, and since 2026-11-03 also `AUTO_LEVEL_SETTLING (Bed Status bit 5) -> 0/1` on every change of bit 5 (Prompt AI B3) | Claude Code | Change to 0, build, check the `LEVELED` and `AUTO_LEVEL_SETTLING` lines are gone | Prompt AE part F; DECISIONS #46; Prompt AI B3 |
 | F20 | Collision detection is blind for the first ~450 supervision passes of every move | 100 passes ignored + 350 calibrating (`motor_current_functions.c:28-29`) before any collision or max-load trip can fire; roughly 0.7-1.0 s at auto-level's loop rate, never measured for manual moves | Claude (design), Claude Code | Design with F15 (pass counts -> ms, DECISIONS #2): state the blind time in ms and what covers it; change; REGRESSION §5 with a loop-rate measurement | Prompt AC handoff B1(a); reviewer ruling, Prompt AE part E |
 | F21 | Auto-level's pulsed ("sneak") phase has no current supervision | Once `Latch` prints, `level_axis_pulse()` runs with no collision, max-load or limit inference (`auto_level.c:679-738`, `:770-771`; `switch_input.c:921`) | Claude (design), Claude Code | Design: supervise the pulse phase (or record why it is safe); change; REGRESSION §5 collision during auto-level near level | Prompt AC handoff B1(d), QUESTIONS 3; reviewer ruling, Prompt AE part E |
 | F22 | The "continuous" re-baseline never reaches the trip test | It writes `dynamic_collision_current_threshold[]`, which nothing reads; the trip uses `max_current_threshold_set[]`; and it counts on motor 3's counter for every motor (`motor_current_functions.c:206-231`) | Claude (design), Claude Code | Decide: make it work (with F15/F20) or remove it; change; REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 4; reviewer ruling, Prompt AE part E |
 | F23 | Auto-level uses the raise margin for the lowering motors | `is_raising` is forced true for auto-level (`bed_actions.c:170`; `auto_level.c:771`), so the two lowering motors are judged against the raise margin | Claude decides, Claude Code changes | Decision (keep with a recorded reason, or per-direction margins in auto-level); then REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 5; reviewer ruling, Prompt AE part E |
-| F24 | Unconditional console prints to review before release | With every flag at 0 the firmware still prints `BLE SM: configure/...` (`ble_bed_service.c:1089, 1101, 1109`), `connection_parameters` (`:1270`), `Max Current for Motor` (`motor_current_functions.c:198`), `Once`, and the auto-level step lines | Claude decides, Claude Code changes | List each print with keep / flag / remove; change; check a release-build log | Prompt AC handoff QUESTIONS 12; reviewer ruling, Prompt AE part E |
+| F24 | Unconditional console prints to review before release | With every flag at 0 the firmware still prints `BLE SM: configure/...` (`ble_bed_service.c:1089, 1101, 1109`), `connection_parameters` (`:1270`), `Max Current for Motor` (`motor_current_functions.c:198`), `Once`, the auto-level step lines, and (added 2026-11-03, Prompt AI A4) `Reset cause: 0x... (...)` at boot (`app.c:445-456`; only `BED_CAPTURE_MOTOR_CURRENTS` suppresses it) | Claude decides, Claude Code changes | List each print with keep / flag / remove; change; check a release-build log | Prompt AC handoff QUESTIONS 12; reviewer ruling, Prompt AE part E |
 | F25 | Hold 9 (2026-11-01 bench): the HEAD pair apparently did not run during PITCH levelling | `prompt_aa_bench.log` lines 201-204 show outputs 0 and 3 at 1 and 12 mA while FOOT ran; cause not established (an inferred limit refusing the pair, `dual_motor_control.c:353, 361`, is one candidate) | Andrew + Claude | A bench look with the limit prints enabled: repeat PITCH levelling from a HEAD-high start and record which pairs run and any limit lines | Prompt AC handoff B1(d), QUESTIONS 6; reviewer ruling, Prompt AE part E |
 | F26 | What can cut the collision reverse short: (1) the runaway fail-safe, after an app collision and after a physical collision once the button is released; (2) any app release during the reverse (MOVE_STOP / AUTO_LEVEL_STOP / STOP_ALL) | (1) During the reverse both app motion flags are false and no switch is down, so `motion_failsafe_tick()` de-energises it after 3 samples of 50 ms (`switch_input.c:804-896`) and prints `FAILSAFE: ...`. (2) The app's stop verbs call `app_apply_stop_outputs()` / `app_stop_all()`, which turn every motor off. The reverse is meant to relieve pressure on the obstruction. **Observed 2026-11-02:** `FAILSAFE: motors energised with nothing commanding - stopped` after an app collision (`round3_bench_2026-11-02.log` line 323, after the trip at 313-322); the log has no timestamps, so how long the reverse ran is not known | Andrew + Claude | Bench: the length of the reverse (5.A1). Then a design ruling covering both (accept, or e.g. the fail-safe stands down while `any_collision` is set, and app stops do not cut the reverse) | Prompt AF handoff b.11.1, QUESTIONS f.3; Prompt AG C4; Prompt AG handoff f.4 folded in by the reviewer (Prompt AH B4, B7) |
 | F27 | A BED LIGHTS press stops an app move but does not latch the #36 inhibit | Lights are not motion and never latch (`bed_control.c:153-164`), so the app's next repeat can restart the move briefly while BED LIGHTS is held | Claude | First: the bench record REGRESSION 8.d; then a ruling | Prompt AE handoff QUESTIONS 6; Prompt AG C4 |
 | F28 | A collision flagged during a BED LIGHTS hold toggles `bedlights_on` at the release without writing the output | Not expected to happen (no motor runs during a BED LIGHTS hold), but the flag and the amber/lighting output would then disagree | Claude | Review before release | Prompt AE handoff QUESTIONS 8; Prompt AG C4 |
+| F29 | `TILT_IDLE_DIAG` 1 -> 0 (`auto_level.c`) | Bench print `TILT idle: N samples in T ms, roll ... pitch ...` every 10 s while the idle tilt sampler runs | Claude Code | Change to 0, build, check the `TILT idle` lines are gone. First use the lines to set `TILT_IDLE_TAU_MS` and the LEVELED exit band | Prompt AI C6; DECISIONS #50 |
+| F30 | A phone that stays subscribed to Bed Status with the app in the background keeps the 10 Hz idle sampler (and the 5 Hz notify) running | The firmware samples whenever a connection is subscribed; it cannot tell a backgrounded app. Costs the subscribed-idle current of F11 for as long as the phone stays connected | Claude Code (app) | The app unsubscribes from Bed Status when backgrounded (app prompt); the firmware needs nothing now. Bench: background the app, check the `TILT idle` lines stop | Prompt AI C5; DECISIONS #50 risk 5 |
 
 ## App
 
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index c1abc9b..b964922 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -48,6 +48,14 @@ don't check boxes in the committed copy.
 ## 1. Power-on & initialization
 
 - [ ] ⚡ Welcome banner and version print on UART
+- [ ] 1.R Reset cause (Prompt AI A4): right after the version line, one line
+      `Reset cause: 0x%08lX (<names>)`. Record it for: (a) a power cycle
+      (bench supply off/on) - expected from code `POR`, possibly with BOD
+      bits; (b) a reflash/reset from the debugger - expected `SYSREQ` or
+      `PIN`; (c) a press of the module's reset pin, if reachable - `PIN`.
+      Reboot again the same way: the line must not carry the previous boot's
+      bits (the register is cleared after the read). The expected names are
+      from the device header, not observed yet.
 - [ ] ⚡ "NVM3 initialized successfully" prints
 - [ ] ⚡ Loaded Roll / Pitch / Raise / Lower values print and match expected
       stored values
@@ -279,7 +287,11 @@ stopped` (PRE_SHIP F26). Full record: DECISIONS #48 "Bench result
 
 App connected as CONTROL, `BLE_DEBUG_WATCHDOG_LOG` 1, UART logged, no
 obstruction. "STOP" = the app's STOP button (STOP_ALL). Items 5b.1-5b.6 are
-the cases the Prompt AH handoff (b.A.6) works through from code. An app that
+the cases the Prompt AH handoff (b.A.6) works through from code, **as
+amended by Prompt AI (DECISIONS #49 amendment): a STOP latches only while the
+app is commanding (a button held, or released less than 300 ms ago without a
+stop write); a STOP with the app idle prints `BLE: STOP_ALL (no app hold, no
+latch)` and the next press is normal; collision wins over STOP (5b.8).** An app that
 does not yet end its holds on STOP keeps repeating the held button, which is
 the case 5b.1 tests; an app that does (the next app prompt) is 5b.2. **In
 every item: after the STOP nothing moves until the held button is released
@@ -301,15 +313,26 @@ and pressed again.**
       moves normally. If the app instead keeps repeating after its MOVE_STOP,
       the next repeat **starts the move** - record it (the firmware cannot tell
       a repeat after a MOVE_STOP from a new press; Prompt AH handoff f).
-- [ ] 5b.3 Nothing held: (a) tap STOP, press HEAD up within ~0.2 s and hold
-      1 s, release; (b) tap STOP again, wait at least 1 s, press HEAD up.
-      Observe (a): 0x07 for the press and its repeats, nothing moves; at the
-      release `BLE: start latch (stop) cleared (stop), K repeats refused`; a
-      fresh press then moves. (b): `BLE: start latch (stop) cleared (silence),
-      0 repeats refused` about 300 ms after the STOP; the press moves normally
-      (0x00). If (b)'s press is refused with 0x07, record it (Prompt AH
-      handoff f: in a main-loop pass the command is processed before the
-      silence check).
+- [ ] 5b.3 Nothing held (Prompt AI A3): (a) with no app button touched for
+      at least 1 s, tap STOP, press HEAD up within ~0.2 s and hold 1 s,
+      release; (b) tap STOP again, wait at least 1 s, press HEAD up. Observe
+      (a): `BLE: STOP_ALL (no app hold, no latch)`; the press **moves
+      normally (0x00)**, no 0x07, no `start latch` line. (b): the same - no
+      latch line at all. (Before Prompt AI, (a) was refused 0x07 until
+      released.) (c) Optional, the lost-stop case: press and release HEAD up
+      quickly and tap STOP within ~0.2 s of the release - if the app's
+      MOVE_STOP reached the bed, `no app hold, no latch`; if the app sends no
+      stop write at the lift, `STOP_ALL latch set` then `start latch (stop)
+      cleared (silence)` ~300 ms later. Record which.
+- [ ] 5b.3d (Prompt AI A2) Hold HEAD up in the app, tap STOP, keep holding
+      ~1 s, release, and press HEAD up again about 0.5 s after the release.
+      Observe: the release clears the latch (`cleared (stop)`, or `cleared
+      (silence)` if the app sent no stop write); the new press moves (0x00),
+      never 0x07. A2 itself (the silence check before the command, for the
+      case where no main-loop pass ran after the 300 ms deadline) cannot be
+      isolated on the bench: with a phone subscribed the idle tilt sampler now
+      runs a pass every 100 ms. Any press refused 0x07 more than 300 ms after
+      the last app start or latching STOP is a failure - record it.
 - [ ] 5b.4 App AUTO LEVEL hold from off-level, tap STOP mid-run, keep
       holding ~2 s, release, press AUTO LEVEL again. Observe: auto-level stops
       at the STOP (motors off, red LED off); `STOP_ALL latch set` and `BLE:
@@ -318,8 +341,10 @@ and pressed again.**
       from Step 1); at the release `BLE: start latch (stop) cleared (stop), K
       repeats refused`; the fresh press starts a new run (`Step 1`). If the
       bed was LEVELED, no `LEVELED` line at the STOP.
-- [ ] 5b.5 Second phone connected as VIEWER. Control phone holding a move;
-      tap STOP on the viewer. Observe: the bed stops; viewer gets 0x00; `BLE:
+- [ ] 5b.5 Second phone connected as VIEWER. (Prompt AI A3: first, with the
+      control phone idle, tap STOP on the viewer: `BLE: STOP_ALL (no app hold,
+      no latch)`, and a control press right after moves normally.) Then:
+      control phone holding a move; tap STOP on the viewer. Observe: the bed stops; viewer gets 0x00; `BLE:
       STOP_ALL latch set (conn=V)`; the control phone gets 0x07 for every held
       repeat and **nothing restarts**; on the control release `BLE: start
       latch (stop) cleared (stop), K repeats refused`. Optional: lift the
@@ -330,12 +355,23 @@ and pressed again.**
       app while still holding it ~2 s; release; press it again. Expected from
       code: the bed stops at the STOP and **does not restart while the button
       is still held** (no new press edge, no `SW:` line until the release);
-      App: 0x00; `BLE: STOP_ALL latch set (conn=N)`; at the release an
+      App: 0x00; `BLE: STOP_ALL (no app hold, no latch)` (Prompt AI A3 - the
+      app was idle; before, `STOP_ALL latch set`); at the release an
       `SW: ... up` line and nothing moves; the next physical press moves
       normally. Repeat holding the physical AUTO LEVEL button: auto-level
       stops and does not restart (no `Step 1`) until released and pressed
       again.
-- [ ] 5b.7 BLE link stays up (no disconnect) through 5b.1-5b.6.
+- [ ] 5b.8 Collision wins (Prompt AI A1). App holding HEAD up; tap STOP and
+      **keep the HEAD finger down** (0x07 per repeat); while still holding,
+      press a **physical** move button and push on the frame until a
+      collision trips (`Tripped on ...`, `Once`). Observe: from the trip
+      until the release, the held app repeats get **0x06**, not 0x07 (the
+      app shows the collision screen); at the app release `BLE: start latch
+      (collision) cleared (stop), K repeats refused` - "collision", although
+      the latch was set by the STOP. Nothing restarts. (Before Prompt AI the
+      repeats stayed 0x07 and the line said "(stop)".) Harder to stage than
+      the others: record what was actually done.
+- [ ] 5b.7 BLE link stays up (no disconnect) through 5b.1-5b.6 and 5b.8.
 
 ## 6. Auto-level
 
@@ -431,6 +467,19 @@ open question, Prompt AC handoff.
   - [ ] 6.10e With it set, hold BED LIGHTS 5 s into settings mode: `-> 0`
         at entry.
   - [ ] 6.10f With it set, STOP_ALL from the app: no LEVELED line (it stays 1).
+- [ ] 6.11 AUTO_LEVEL_SETTLING, Bed Status flag bit 5 (Prompt AI part B).
+      With `BED_DEBUG_LEVELED_LOG` 1 each change prints
+      `AUTO_LEVEL_SETTLING (Bed Status bit 5) -> 0|1`.
+  - [ ] 6.11a App AUTO LEVEL from off-level: `Step 1: ROLL and PITCH check`
+        then `AUTO_LEVEL_SETTLING (Bed Status bit 5) -> 1`; about 1.5 s
+        later `-> 0`, before `Step 2: ...` (or before `Bed is disabled, no
+        leveling required` from a level bed). The app's "Hold to level"
+        fill ends when the bit clears. No motor runs while it is 1.
+  - [ ] 6.11b Release AUTO LEVEL within 1 s of the press: `-> 0` at the
+        release; no `Step 2`.
+  - [ ] 6.11c The physical AUTO LEVEL button: the same lines as 6.11a.
+  - [ ] 6.11d From a start within the pulse band (6.6): `-> 0`, then ~0.35 s
+        with no motion, then `Pulse on...`.
 
 **Recorded result, 2026-11-02 (firmware `b848e48`, Galaxy S23 on app
 `4e66328`, log `round3_bench_2026-11-02.log`).** VERIFIED (user's bench run).
@@ -444,6 +493,48 @@ line reads `longest between passes 0 ms` (62 of 62; DECISIONS #45). Not
 covered: the physical AUTO LEVEL button (6.1 and 6.10a as written), 6.2-6.7,
 6.10d-6.10f. Full record: DECISIONS #46 "Bench result (2026-11-02)".
 
+## 6b. Live idle tilt and self-clearing LEVELED (Prompt AI parts C-D, DECISIONS #50, #46 amendment)
+
+App open and subscribed (CONTROL), `TILT_IDLE_DIAG` 1, `BED_DEBUG_LEVELED_LOG`
+1, `BLE_DEBUG_WATCHDOG_LOG` 1, UART logged. "TILT line" = `TILT idle: N
+samples in T ms, roll min .. max .. p2p .., pitch min .. max .. p2p .. (deg
+x100, pitch internal sign)`. Record every TILT line's p2p values: they set
+`TILT_IDLE_TAU_MS` and the LEVELED exit band (0.3° = 30).
+
+- [ ] 6b.1 At rest, app open, 30 s, nobody touching the bed. Observe: the
+      app's angles update about 5 times a second and are **not greyed**
+      (`tilt_fresh` = 1); a TILT line about every 10 s, the first ~12 s
+      after the app opened (0.5 s burst + 1.5 s settling + 10 s), with N
+      about 100 and T about 10000; roll and pitch p2p small (record them).
+      If N is well below 100 the sampler is not running at 10 Hz - record it.
+- [ ] 6b.2 After a move (app or switch): no jump in the shown angles when the
+      move ends; the shown angle settles within ~1 s; TILT lines resume ~12 s
+      after the move ends. During the move the angles update at 10 Hz as
+      before.
+- [ ] 6b.3 After an auto-level (app): when the run ends, the shown angle does
+      not jump by more than a few hundredths of a degree; LEVELED stays 1 at
+      rest - **no `LEVELED ... -> 0` line for 60 s** with nobody touching the
+      bed; TILT lines resume. Record the shown roll/pitch.
+- [ ] 6b.4 BLE link stays up: 10 minutes at rest with the app open (no
+      `connection_closed`), then 5 app moves and 2 app auto-levels: no `BLE:
+      WATCHDOG STOP`, every `hold end` line `longest between passes 0 ms`.
+- [ ] 6b.5 **LEVELED clears by tilt (part D).** After a successful
+      auto-level (`LEVELED ... -> 1`), wait at least 3 s, then tilt the bed by
+      hand off level by clearly more than 0.3° (lift or press one side or
+      corner) and hold it. Observe: `LEVELED (Bed Status bit 4) -> 0` about
+      **2 s** after the tilt (~2.2 s with the filter lag); the app's "Level
+      reached" clears. Let go: no `-> 1` line (it is never set from tilt).
+- [ ] 6b.6 **A brief bump does nothing.** With LEVELED 1, push the frame off
+      level and let go within ~1 s, a few times. Observe: no `LEVELED` line.
+- [ ] 6b.7 Settings mode and REFRESH_TILT: entering settings mode stops the
+      TILT lines (no new line while in it); after leaving, they resume ~12 s
+      later. A REFRESH_TILT from the app restarts the 12 s.
+- [ ] 6b.8 No phone: close the app (unsubscribe) - TILT lines stop. Then
+      PRE_SHIP F11: idle current with the app open and subscribed, and not.
+- [ ] 6b.9 PRE_SHIP F30: put the app in the background (phone still
+      connected). Record whether the TILT lines stop (they will not until the
+      app unsubscribes when backgrounded).
+
 ## 7. Settings mode
 
 - [ ] ⚡ 7.1 Hold BED_LIGHTS 5 s: amber LED fast-blinks — settings mode active.
````

---

## d. Raw output of commands run

### d.1 How this log was produced

From d.2 #1 onward most shell commands went through a logging wrapper
(`$S/run.sh`; `$S` = this session's scratchpad), which runs the command in the
repo root and appends the command, its complete output and its exit code to
`cmdlog.txt`. d.2 is that file, with **one omission**: in #2, line 413 of
`cmake_gcc/tilt_bed_controller.cmake` matched the pattern `emu` inside a
41,202-character `# BEGIN_SIMPLICITY_STUDIO_METADATA=...` line (Studio's
encoded project metadata); it is replaced by a marker saying so. Nothing else
in d.2 is changed. No output contained a secret; nothing is redacted. Some
d.2 commands print lines of files (`sed -n`, `grep`) to read or cite them;
they are this session's real command output and are left in.

Commands run before the wrapper existed, or outside it (edits made with
Python or `sed` directly, and two that failed), are in d.0 with their output,
except where the output was only the contents of a committed file or of the
prompt - those are listed, marked *[file content, not reproduced]*, as the AH
handoff did. Files were also read with the editor's read tool (no shell
command): the prompt, the AH handoff (lines 1-510, 629-1008, 1936-1976,
2895-3074), DECISIONS (#27-#33 at 486-990, #46-#49 at 2267-2776), PRE_SHIP,
`auto_level.c`, `auto_level.h`, `app.c`, `ble_bed_service.c` (1-1300),
`ble_bed_service.h`, `bed_status.c`, `bed_status.h`, `bed_actions.c` (1-80,
130-280), `bed_actions.h`, `bed_control.c`, `main.c`, BLE_GATT_DESIGN (84-450),
REGRESSION (1-58, 272-451), SESSION_LOG (1668-1715).

```sh
#!/bin/sh
# Session command logger: runs "$*" in the repo root, prints and appends command + output to cmdlog.txt
S="C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad"
N=$(( $(cat "$S/n.txt" 2>/dev/null || echo 0) + 1 )); echo $N > "$S/n.txt"
cd "C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller" || exit 99
{ echo "### #$N \$ $*"; sh -c "$*" 2>&1; echo "[exit $?]"; echo; } | tee -a "$S/cmdlog.txt"
```

### d.3 Build method (referred to as `build.sh` in d.2)

```sh
#!/bin/sh
# Session build wrapper: Silicon Labs ninja + cmake on PATH, build the default preset.
P=~/.silabs/slt/installs/conan/p
export PATH="$P/ninja1a38fc85adcf7/p:$P/cmakefa35ab0687064/p/bin:$PATH"
S="C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad"
cd "C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/cmake_gcc" || exit 99
cmake --build --preset default_config > "$S/build_out.txt" 2>&1
rc=$?
tail -25 "$S/build_out.txt"
echo "exit $rc"
echo "--- warnings/errors (grep -n -i 'warning\|error' over the whole output):"
grep -n -i 'warning\|error' "$S/build_out.txt"
exit $rc
```

Nothing printed after the `--- warnings/errors` line means no line of the
full build output contains "warning" or "error". Every build's whole output
was under 25 lines, so its tail is its whole output. The other scripts the log
runs are `rst_scan.sh` (#7), `matrix.sh` (#34), `cite.sh` (#42) and
`regr_edit.py` (#33); their full text:

```sh
#!/bin/sh
# rst_scan.sh - Lists the SDK .c files this build compiles, then which of them mention the reset cause.
P=C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p
grep -o '${SDK_PATH}/[^"]*\.c' cmake_gcc/tilt_bed_controller.cmake | sed 's#^${SDK_PATH}/##' > sdk_sources.tmp
echo "SDK .c files compiled: $(wc -l < sdk_sources.tmp)"
while read f; do grep -Hn 'RSTCAUSE\|ResetCause\|reset_cause' "$P/$f" 2>/dev/null; done < sdk_sources.tmp
rm -f sdk_sources.tmp
echo "--- sl_hal_emu.c compiled?"; grep -c 'sl_hal_emu' cmake_gcc/tilt_bed_controller.cmake
echo "--- __INLINE definition for GCC in CMSIS:"; grep -rn 'define __INLINE' $P/cmsis 2>/dev/null | head -5
```

```sh
#!/bin/sh
# matrix.sh - Final flag matrix (Prompt AI E3): sets flags with sed, builds, prints size, restores.
S="C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad"
SZ=/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe
setf() { sed -i "s/^#define $2 [01]\(\r\?\)\$/#define $2 $3\1/" "$1"; }
show() { grep -n 'define TILT_IDLE_DIAG \|define BED_DEBUG_LEVELED_LOG \|define BLE_DEBUG_WATCHDOG_LOG \|define BED_CAPTURE_MOTOR_CURRENTS ' auto_level.c bed_actions.c ble_bed_service.h debug_capture.h | tr -d '\r'; }
build() { echo "=== $1"; show; sh "$S/build.sh" | grep -v '^\[[0-9]*/[0-9]*\] Building' ; "$SZ" cmake_gcc/build/base/tilt_bed_controller.out | tail -1; }
build "defaults"
setf auto_level.c TILT_IDLE_DIAG 0;          build "TILT_IDLE_DIAG 0";          setf auto_level.c TILT_IDLE_DIAG 1
setf bed_actions.c BED_DEBUG_LEVELED_LOG 0;  build "BED_DEBUG_LEVELED_LOG 0";  setf bed_actions.c BED_DEBUG_LEVELED_LOG 1
setf ble_bed_service.h BLE_DEBUG_WATCHDOG_LOG 0; build "BLE_DEBUG_WATCHDOG_LOG 0"; setf ble_bed_service.h BLE_DEBUG_WATCHDOG_LOG 1
setf auto_level.c TILT_IDLE_DIAG 0; setf bed_actions.c BED_DEBUG_LEVELED_LOG 0; setf ble_bed_service.h BLE_DEBUG_WATCHDOG_LOG 0
build "all three 0"
setf auto_level.c TILT_IDLE_DIAG 1; setf bed_actions.c BED_DEBUG_LEVELED_LOG 1; setf ble_bed_service.h BLE_DEBUG_WATCHDOG_LOG 1
setf debug_capture.h BED_CAPTURE_MOTOR_CURRENTS 1; build "BED_CAPTURE_MOTOR_CURRENTS 1 (extra)"; setf debug_capture.h BED_CAPTURE_MOTOR_CURRENTS 0
build "restored defaults"
echo "=== git status --short (source files must not appear)"; git status --short
```

`cite.sh` (#42) defines `p() { print lines a and b of file f, numbered }` and
calls it for each cited range (the ranges are visible in its output).
`regr_edit.py` (#33) is a Python script of `str.replace` calls whose
replacement texts are exactly the REGRESSION hunks in c.2; it prints `ok`.

### d.0 Commands run before the wrapper, and outside it

In the order run. `P n` numbers are used in e.

- **P1** `cat docs/prompts/PROMPT_AI_latch-refinements-live-tilt.md && date && git status && git log --oneline -5`
  - the prompt: *[file content, not reproduced - the committed prompt file]*
  - then:
    ```
    Tue Nov  3 08:35:07 PST 2026
    On branch main
    Your branch is up to date with 'origin/main'.

    Changes not staged for commit:
      (use "git add <file>..." to update what will be committed)
      (use "git restore <file>..." to discard changes in working directory)
    	modified:   .projectlinkstore

    Untracked files:
      (use "git add <file>..." to include in what will be committed)
    	docs/prompts/PROMPT_AI_latch-refinements-live-tilt.md

    no changes added to commit (use "git add" and/or "git commit -a")
    cd2a148 Handoff 2026-11-03 01: Prompt AH (stop latch, bench record of 2026-11-02, live-level report)
    169b2f7 Bench record of 2026-11-02 and rulings (Prompt AH part B, docs only)
    48c5cf6 STOP waits for release: STOP_ALL latches app starts, STOP_LATCHED 0x07 (DECISIONS #49, Prompt AH part A)
    b848e48 Handoff 2026-11-02 03: Prompt AG (collision latch follow-ups: viewer STOP_ALL, refusals latch, trip arms silence)
    3d7d8e6 Collision latch follow-ups: viewer STOP_ALL keeps it, refusals latch, trip arms silence (DECISIONS #48, Prompt AG)
    ```
- **P2** `git fetch origin 2>&1; git rev-parse HEAD origin/main; ls docs docs/handoffs | tail -30; wc -l docs/*.md *.c *.h`
  - `git fetch` printed nothing; then:
    ```
    cd2a148b15e6165656f4868879298f05efd93e25
    cd2a148b15e6165656f4868879298f05efd93e25
    ```
  - the directory listing and line counts: *[file names and sizes of committed files, not reproduced]*
- **P3** `wc -l docs/handoffs/fw_HANDOFF_2026-11-03_01_...md && grep -n "^#" ...` -> `3074 ...`; the headings *[file content, not reproduced]*
- **P4** `cat docs/MIGRATION_STATUS.md | head -60` *[file content, not reproduced]*
- **P5** created `run.sh` and `build.sh` (text above), then `ls ~/.silabs/slt/installs/conan/p/ | grep -i 'ninja\|cmake\|gcc'`:
  ```
  cmake94b5e5c99cc1e
  cmakefa35ab0687064
  gcc-a1a188b577d6ca
  gcc-a999d2e027337f
  ninja1a38fc85adcf7
  ninja6c55a7e9bcbcf
  ```
- **P6** `grep -n '^## 2[6-9]\.\|^## 3[0-9]\.' docs/DECISIONS.md` *[file content (headings), not reproduced]*
- **P7** `ls cmake_gcc; grep -n -i 'sdk\|SISDK' cmake_gcc/*.cmake | head -10; grep -rn -i 'rmu\|rstcause\|reset_cause\|resetcause' cmake_gcc/*.cmake tilt_bed_controller.slcp autogen/*.c autogen/*.h | head -20` - 41.4 KB of output, mostly the same Studio metadata line as d.2 #2; the parts used: `cmake_gcc/tilt_bed_controller.cmake:5:set(SDK_PATH "C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p")`, `:6:set(COPIED_SDK_PATH "simplicity_sdk_2026.6.0")`. *[rest: file content, not reproduced]*
- **P8** `grep -n '^#' docs/BLE_GATT_DESIGN.md` *[file content (headings), not reproduced]*
- **P9** `grep -n '^#' docs/REGRESSION.md; grep -n '^## ' docs/SESSION_LOG.md | tail -3` *[file content (headings), not reproduced]*
- **P10** `tail -c 300 docs/DECISIONS.md | od -c | tail -3; for f in ...; do printf '%s CR=' $f; tr -cd '\r' < $f | wc -c; done`:
  ```
  0000420   F   _   2   0   2   6   -   1   1   -   0   3   _   0   1   _
  0000440   .   .   .   `   ,       b   .   A   )   .  \n
  0000454
  docs/DECISIONS.md CR=0
  docs/BLE_GATT_DESIGN.md CR=0
  docs/PRE_SHIP.md CR=0
  docs/REGRESSION.md CR=0
  docs/SESSION_LOG.md CR=0
  app.c CR=0
  ble_bed_service.c CR=0
  auto_level.c CR=846
  ```
- **P11** the DECISIONS #49 amendment appended with `cat >> docs/DECISIONS.md <<'EOF' ... EOF; grep -n '^### Amended by Prompt AI' docs/DECISIONS.md` -> `2777:### Amended by Prompt AI (2026-11-03)`
- **P12** `sed -i` correcting two line ranges in that amendment; `grep -n ':868-883\|:743-783' docs/DECISIONS.md`:
  ```
  2784:1. **Collision wins (f.1).** The start refusal (`:868-883`) now tests
  2834:   branch (`:743-783`) first takes `latch_stop = app_commanding ||
  ```
- **P13** Python edit of PRE_SHIP F24; `grep -n 'F24' docs/PRE_SHIP.md | head -2; sed -n '436,458p' app.c` - the F24 row (as in c.2) and *[app.c 436-458, file content, not reproduced]*
- **P14** `sed -i` correcting F24's line reference; `grep -c 'app.c:445-456' docs/PRE_SHIP.md` -> `1`
- **P15** `grep -n 'app_auto_level_settling_log_tick\|...' app.c && sed -n '778,792p' app.c` -> `680:*** Auto Level`, `683:  if ((auto_level_active) && ...`, `787:  app_auto_level_settling_log_tick();` (line numbers before Part C), then *[file content, not reproduced]*
- **P16** Python edit of BLE_GATT_DESIGN (B2) and PRE_SHIP F19; `git diff --stat docs`:
  ```
   docs/BLE_GATT_DESIGN.md | 52 ++++++++++++++++++++++++++++++++++++++++++++++++-
   docs/PRE_SHIP.md        |  2 +-
   2 files changed, 52 insertions(+), 2 deletions(-)
  ```
  (plus git's `LF will be replaced by CRLF` warnings, one per file)
- **P17** Python insert of the DECISIONS #46 amendment item 1; `grep -n '^### Amended by Prompt AI\|^## 47\.' docs/DECISIONS.md; sed -i ... docs/BLE_GATT_DESIGN.md; grep -n 'Added 2026-11-03 (Prompt AI part B' docs/BLE_GATT_DESIGN.md`:
  ```
  2367:### Amended by Prompt AI (2026-11-03)
  2389:## 47. BED LIGHTS toggles the lights on the release of a short press
  2799:### Amended by Prompt AI (2026-11-03)
  289:Added 2026-11-03 (Prompt AI part B; DECISIONS #46, Prompt AI amendment), within protocol
  ```
- **P18** `grep -n 'restart_status_notify_timer\|publish_status(' ble_bed_service.c` (Part C, before the build):
  ```
  1117:static void restart_status_notify_timer(uint32_t interval_ms)
  1142:static void publish_status(const uint8_t *packed, uint32_t interval_ms)
  1150:  restart_status_notify_timer(interval_ms);
  1238:    publish_status(packed, interval_ms);
  ```
- **P19** `cat >> docs/DECISIONS.md <<'EOF' ...(DECISIONS #50)... EOF` - **failed**, nothing appended:
  ```
  /usr/bin/bash: -c: line 119: unexpected EOF while looking for matching `''
  ```
  (exit 2). Then `grep -c '^## 50\.' docs/DECISIONS.md; tail -3 docs/DECISIONS.md` -> `0` and the last three lines of the #49 amendment (unchanged). The text was then written to a scratch file and appended with `tr -d '\r' < dec50.md >> docs/DECISIONS.md`; `grep -n '^## 50\.'` -> `2878:## 50. Live idle tilt: ...`, and `sed -n '474,480p;662,668p' auto_level.c` *[file content, not reproduced]*.
- **P20** `sed -i` setting #50's cited range to `:476-665`; `grep -n ':476-665' docs/DECISIONS.md` -> `2897:not active. In it (...`:476-665` at the part C commit):`; `grep -n ... docs/BLE_GATT_DESIGN.md` -> `389:**`tilt_fresh` requires a one-bit firmware addition** implementing`, `397:Notify cadence (adaptive, subscription-driven):`, `402:| Idle, subscribed | 1 Hz, plus immediate on any state-flag change |`
- **P21** Python edit of PRE_SHIP F11, F16, new F29, F30; `grep -n '^| F29\|^| F30\|^| F16\|^| F11' docs/PRE_SHIP.md | cut -c1-90` -> rows at lines 35, 40, 53, 54 (as in c.2)
- **P22** `grep -n 'leveled_exit_band = \|...' auto_level.c; grep -n 'tilt_idle_leveled_exit_due()' app.c && python ... (D3 docs); git diff --stat`:
  ```
  81:#define LEVELED_EXIT_BAND 0.3       // degrees
  82:#define LEVELED_EXIT_HOLD_MS 2000
  111:static const int16_t leveled_exit_band = LEVELED_EXIT_BAND * SCALE_FACTOR; // 30
  598:static void tilt_idle_leveled_exit_sample(uint32_t now)
  676:    tilt_idle_leveled_exit_sample(now);
  698:bool tilt_idle_leveled_exit_due(void)
  840:  if (tilt_idle_leveled_exit_due()) {
   .projectlinkstore       |  2 +-
   app.c                   |  7 +++++++
   auto_level.c            | 47 +++++++++++++++++++++++++++++++++++++++++++++--
   auto_level.h            |  6 ++++++
   docs/BLE_GATT_DESIGN.md | 14 ++++++++++++--
   docs/DECISIONS.md       | 21 +++++++++++++++++++++
   6 files changed, 92 insertions(+), 5 deletions(-)
  ```
- **P23** `python - <<'PYEOF' ...(REGRESSION edits)... PYEOF` - **failed** the same way, nothing written:
  ```
  /usr/bin/bash: -c: line 118: unexpected EOF while looking for matching `''
  ```
  (exit 2). `git diff --stat docs/REGRESSION.md; echo "(empty = untouched)"` -> `(empty = untouched)`. The script was written to `regr_edit.py` and run in d.2 #33.
- **P24** `python -I regr_fix.py` (rewording 5b.3d) -> `ok`
- **P25** `wc -c -l cmdlog.txt` -> `1257 121485 .../cmdlog.txt`; an `awk` listing of #2's output with long lines shortened (shows line 413's 41,202 characters - the omission in d.2 #2)
- **P26** `awk 'length($0) > 400 ...' cmdlog.txt; grep -c $'\r' cmdlog.txt; grep -n -i 'secret\|token\|password\|api_key' cmdlog.txt` - long lines are command echoes and PRE_SHIP rows printed by #1, plus #2's line 413; CR count `0`; no secret match.
- **P27** appended the SESSION_LOG entry (`tr -d '\r' < sessionlog_entry.md >> docs/SESSION_LOG.md`), then d.2 #40.
- **P28** a Python edit of this file's draft correcting eight line citations (after #42/#43).

### d.2 Session command log (the wrapper, numbered)

````
### #1 $ grep -n '^## \|^### ' docs/DECISIONS.md | tail -40; grep -n 'F11\|F15\|F16\|F24' docs/PRE_SHIP.md
1961:### 2. Disconnect a phone whose pairing was refused
1985:### Status
1993:### Bench result (2026-10-18)
2036:### Bench result (2026-11-01, iPhone)
2061:## 44. STOP in settings mode cancels any hold in progress and stays in settings mode
2066:### Decision
2073:### Implementation (`840d7b8`)
2096:### Bench result (2026-10-18)
2119:## 45. During an app hold the CPU stays in EM0
2124:### Decision
2145:### Evidence
2165:### Effect on auto-level timing (closes Prompt Y QUESTIONS 4)
2190:### Cost
2195:### Status
2203:### Bench result (2026-11-01, Galaxy S23)
2256:### Bench result (2026-11-02, Galaxy S23)
2267:## 46. Bed Status flag bit 4 = LEVELED; "Bed is level" prints on every successful auto-level end
2272:### Decision
2291:### Implementation
2321:### Choices made in the implementation (for the reviewer)
2336:### Status
2340:### Bench result (2026-11-02)
2367:## 47. BED LIGHTS toggles the lights on the release of a short press
2373:### Decision
2399:### Implementation (`switch_input.c`)
2421:### Every other path this touches, after the change
2456:### Status
2460:## 48. A collision during an app hold latches the app path: COLLISION_STOP (0x06) until the app releases
2469:### The defect (from code, not seen on a bed)
2484:### Decision (Option 1)
2533:### Options rejected (AE handoff b.A.3)
2549:### Interactions found while implementing (ASSERTED from code)
2586:### Status
2593:### Amended by Prompt AG (2026-11-02)
2651:### Bench result (2026-11-02)
2684:## 49. STOP waits for release: a STOP_ALL latches app motion starts (STOP_LATCHED 0x07) until the app releases
2692:### The bench result
2708:### Decision (user requirement)
2754:### Why STOP_ALL no longer clears the latch
2768:### Status
35:| F11 | Sleep clock: LFRCO in default precision; EM2 likely never reached. **Test before shipping (Andrew).** | The SDK reports the LFRCO as 0xFFFF ppm and the BLE controller then holds the device in EM1 (`sl_btctrl_init.c:289-298`), so idle current is higher than EM2 and the radio's sleep-clock timing is unexplained | Andrew (bench); configurator change by Andrew | 1) Measure idle current, connected and not. 2) Set LFRCO high precision in the configurator; repeat 6.9 **with** DECISIONS #45 and **without** it (temporary build); measure idle current again | Prompt AA handoff part C (C1-C4) and QUESTIONS 3-4; DECISIONS #45 |
39:| F15 | DECISIONS #29 timing constants review | Auto-level's filter (`NUM_ACCEL_READINGS`, `accel_alpha`) and the collision path (`NUM_CURRENT_READINGS`, `NUM_INITIAL_CURRENTS_IGNORED`, `NUM_CURRENTS_READ_DYNAMIC_THRESHOLD`, `alpha`) count loop passes, not milliseconds (DECISIONS #2) | Claude (design), Claude Code | Design prompt; change; REGRESSION §5 and §6 with a loop-rate measurement | DECISIONS #29 |
40:| F16 | `tilt_fresh` (Stage B5) | Bed Status flag bit 3 is always 0 (`ble_bed_service.c:1057`), so the app cannot tell a fresh angle from a stale one | Claude decides | Implement, or ship without it with the app treating tilt as possibly stale | DECISIONS #17, #33; BLE_GATT_DESIGN §6 |
44:| F20 | Collision detection is blind for the first ~450 supervision passes of every move | 100 passes ignored + 350 calibrating (`motor_current_functions.c:28-29`) before any collision or max-load trip can fire; roughly 0.7-1.0 s at auto-level's loop rate, never measured for manual moves | Claude (design), Claude Code | Design with F15 (pass counts -> ms, DECISIONS #2): state the blind time in ms and what covers it; change; REGRESSION §5 with a loop-rate measurement | Prompt AC handoff B1(a); reviewer ruling, Prompt AE part E |
46:| F22 | The "continuous" re-baseline never reaches the trip test | It writes `dynamic_collision_current_threshold[]`, which nothing reads; the trip uses `max_current_threshold_set[]`; and it counts on motor 3's counter for every motor (`motor_current_functions.c:206-231`) | Claude (design), Claude Code | Decide: make it work (with F15/F20) or remove it; change; REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 4; reviewer ruling, Prompt AE part E |
48:| F24 | Unconditional console prints to review before release | With every flag at 0 the firmware still prints `BLE SM: configure/...` (`ble_bed_service.c:1089, 1101, 1109`), `connection_parameters` (`:1270`), `Max Current for Motor` (`motor_current_functions.c:198`), `Once`, and the auto-level step lines | Claude decides, Claude Code changes | List each print with keep / flag / remove; change; check a release-build log | Prompt AC handoff QUESTIONS 12; reviewer ruling, Prompt AE part E |
75:| H3 | Is a 32.768 kHz crystal fitted on the BGM220S module? | Decides whether LFXO is an option for F11; the SDK defaults suggest the S module has none | Andrew | Module datasheet read; answer recorded in HARDWARE_CONFIG.md | Prompt AA handoff C1 |
[exit 0]

### #2 $ grep -n -i 'rmu\|emu\.c\|rstcause\|reset_cause\|resetcause' cmake_gcc/tilt_bed_controller.cmake; grep -n -i 'rmu\|emu' tilt_bed_controller.slcp; grep -rln -i 'RSTCAUSE\|ResetCause\|reset_cause' autogen config 2>/dev/null
72:    "${SDK_PATH}/platform_core/platform/emlib/src/em_emu.c"
413:# BEGIN_SIMPLICITY_STUDIO_METADATA=[OMITTED: the rest of this matched line - 41202 characters of Studio-encoded project metadata in cmake_gcc/tilt_bed_controller.cmake, matched by "emu"; not a secret, not reproduced]
299:  id: emlib_emu
[exit 1]

### #3 $ SDK=C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p; ls $SDK; find $SDK/platform_core -iname '*rmu*' 2>/dev/null | head; grep -rln 'ResetCause\|reset_cause' $SDK/platform_core --include=*.h 2>/dev/null | head -20
License.txt
bgapi_protocol
bluetooth_apploader
bluetooth_bgbuild
bluetooth_common
bluetooth_le_app
bluetooth_le_controller
bluetooth_le_host
bluetooth_le_middleware
bluetooth_mesh
bluetooth_mesh_app
bluetooth_mesh_middleware
boards
bootloader
bootloader_sample_app
cmsis
cmsis_dsp
cmsis_nn
common_15_4
compute
compute_app
conaninfo.txt
conanmanifest.txt
connect_sdk
connect_sdk_sample_app
connect_stack
coremark
cpc
cpc_app
cpc_extra
delta_dfu
freertos
generative_ai
mbedtls
micriumos
multiprotocol
multiprotocol_app
ninja
openthread
openthread_app
openthread_stack
platform_core
platform_sample_app
platform_security_sample_app
printf
proprietary_default_radio_confs
radio_config_generator
rail_library
rail_library_alpha_chips
rail_sdk
rail_sdk_sample_app
realtime_locationing
se_firmware_x301
se_firmware_xg21
se_firmware_xg22
se_firmware_xg23
se_firmware_xg24
se_firmware_xg25
se_firmware_xg26
se_firmware_xg27
se_firmware_xg28
se_firmware_xg29
se_firmware_xg2d
security_libcryptosoc
security_sxsymcrypt
security_tfm
segger
simplicity_sdk.slcs
toolchains.slct
unity_test_framework
usb
wisun
wisun_cli_app
wisun_config_generator
wisun_ddp_app
wisun_ncp_app
wisun_rcp_app
wisun_sdk
wisun_sdk_sample_app
zigbee
zigbee_app
zwave
zwave_cc_configurator
zwave_sample_app
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/emlib/component/emlib_rmu.slcc
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/emlib/inc/em_rmu.h
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/emlib/src/em_rmu.c
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/emlib/inc/em_rmu.h
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/peripheral/inc/sl_hal_emu.h
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/inc/sl_se_manager_util.h
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/sl_trustzone/inc/common/sli_tz_service_se_manager.h
[exit 0]

### #4 $ SDK=C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform; grep -n -i 'reset_cause\|rstcause\|ResetCause' $SDK/peripheral/inc/sl_hal_emu.h; grep -n -i 'ResetCause\|_SERIES_2\|RMU_RSTCAUSE\|EMU_RSTCAUSE' $SDK/emlib/inc/em_rmu.h | head -40; grep -n 'ResetCause' $SDK/emlib/src/em_rmu.c
1173: *   This function clears all the reset cause bits of the RSTCAUSE register.
1175: *   Otherwise, reset causes may accumulate. See sl_hal_emu_get_reset_cause().
1177:__INLINE void sl_hal_emu_clear_reset_cause(void)
1179:  EMU->CMD_SET = EMU_CMD_RSTCAUSECLR;
1189: *   sl_hal_emu_clear_reset_cause(). This function call will return the main cause for
1196:__INLINE uint32_t sl_hal_emu_get_reset_cause(void)
1198:  return EMU->RSTCAUSE;
1642: *    uint32_t reset_cause = sl_hal_emu_get_reset_cause();
1643: *    sl_hal_emu_clear_reset_cause();
138:void RMU_ResetCauseClear(void);
139:uint32_t RMU_ResetCauseGet(void);
61:    in @ref RMU_ResetCauseMasks_Typedef. */
162:} RMU_ResetCauseMasks_Typedef;
171:static const RMU_ResetCauseMasks_Typedef  resetCauseMasks[NUM_RSTCAUSES] =
222:   the RMU_ResetCauseGet function. */
276: *   Otherwise, reset causes may accumulate. See @ref RMU_ResetCauseGet().
278:void RMU_ResetCauseClear(void)
314: *   RMU_ResetCauseClear(). This function call will return the main cause for
321:uint32_t RMU_ResetCauseGet(void)
325:    // RMU_ResetCauseGet() has already been called since boot. Return what was already obtained.
[exit 0]

### #5 $ P=C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform; sed -n '1,40p;1160,1200p;1630,1650p' $P/peripheral/inc/sl_hal_emu.h
/***************************************************************************//**
 * @file
 * @brief Energy Management Unit (EMU) peripheral API
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#ifndef SL_HAL_EMU_H
#define SL_HAL_EMU_H

#include "em_device.h"

#if defined(EMU_PRESENT)

#ifdef __cplusplus
extern "C" {
#endif
 ******************************************************************************/
__INLINE float sl_hal_emu_get_temperature(void)
{
  return ((float) ((EMU->TEMP & (_EMU_TEMP_TEMP_MASK | _EMU_TEMP_TEMPLSB_MASK) )
                   >> _EMU_TEMP_TEMPLSB_SHIFT)
          ) / 4.0f - EMU_TEMP_ZERO_C_IN_KELVIN;
}

/***************************************************************************//**
 * @brief
 *   Clear the reset cause register.
 *
 * @details
 *   This function clears all the reset cause bits of the RSTCAUSE register.
 *   The reset cause bits must be cleared by software before a new reset occurs.
 *   Otherwise, reset causes may accumulate. See sl_hal_emu_get_reset_cause().
 ******************************************************************************/
__INLINE void sl_hal_emu_clear_reset_cause(void)
{
  EMU->CMD_SET = EMU_CMD_RSTCAUSECLR;
}

/***************************************************************************//**
 * @brief
 *   Get the cause of the last reset.
 *
 * @details
 *   To be useful, the reset cause must be cleared by software before a new
 *   reset occurs. Otherwise, reset causes may accumulate, See
 *   sl_hal_emu_clear_reset_cause(). This function call will return the main cause for
 *   reset, which can be a bit mask (several causes) and clear away "noise".
 *
 * @return
 *   A reset cause mask. See the reference manual for a description
 *   of the reset cause mask.
 ******************************************************************************/
__INLINE uint32_t sl_hal_emu_get_reset_cause(void)
{
  return EMU->RSTCAUSE;
}

 *    // Set DCDC mode to regulation
 *    sl_hal_emu_set_dcdc_mode(SL_HAL_EMU_DCDC_MODE_REGULATION);
 *    #endif
 *
 *    // RAM power management
 *    // Power down all RAM blocks except the first one
 *    sl_hal_emu_ram_power_down(SRAM_BASE, SRAM_BASE + SRAM_SIZE);
 *
 *    // Power up all RAM blocks when needed again
 *    sl_hal_emu_ram_power_up();
 *
 *    // Clear reset cause when done handling it
 *    uint32_t reset_cause = sl_hal_emu_get_reset_cause();
 *    sl_hal_emu_clear_reset_cause();
 *  }
 *  @endcode
 *
 * @} (end addtogroup emu)
 ******************************************************************************/
/* *INDENT-ON* */

[exit 0]

### #6 $ grep -n 'peripheral/inc\|peripheral/src\|sl_hal_emu\|emlib/inc"' cmake_gcc/tilt_bed_controller.cmake | head; sed -n 's/^ *"\\/\(.*\.c\)"/\1/p' cmake_gcc/tilt_bed_controller.cmake > 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/sdk_sources.txt'; wc -l < 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/sdk_sources.txt'; P=C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p; for f in $(cat 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/sdk_sources.txt'); do grep -ln 'RSTCAUSE\|ResetCause\|reset_cause' $P/$f 2>/dev/null; done
82:    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_gpio.c"
83:    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_i2c.c"
84:    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_ldma.c"
85:    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_prs.c"
86:    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_syscfg.c"
87:    "${SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_system.c"
230:    "${SDK_PATH}/platform_core/platform/emlib/inc"
235:    "${SDK_PATH}/platform_core/platform/peripheral/inc"
sed: -e expression #1, char 21: unknown option to `s'
0
[exit 0]

### #7 $ sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/rst_scan.sh'
SDK .c files compiled: 178
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1188:#if defined(SLI_SE_COMMAND_STATUS_READ_RSTCAUSE_AVAILABLE)
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1190: * Read the EMU->RSTCAUSE after a tamper reset. This function should be called
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1191: * if EMU->RSTCAUSE has been cleared upon boot.
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1193:sl_status_t sl_se_get_reset_cause(sl_se_command_context_t *cmd_ctx,
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1194:                                  uint32_t *reset_cause)
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1196:  if (cmd_ctx == NULL || reset_cause == NULL) {
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1202:  sli_se_command_init(cmd_ctx, SLI_SE_COMMAND_STATUS_READ_RSTCAUSE);
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1204:    SLI_SE_DATATRANSFER_DEFAULT(reset_cause, sizeof(uint32_t));
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1208:#endif // SLI_SE_COMMAND_STATUS_READ_RSTCAUSE_AVAILABLE
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1212: * Read the cached value of the EMU->TAMPERRSTCAUSE register after a tamper
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1213: * reset. This function should be called if EMU->TAMPERRSTCAUSE has been cleared
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1216:sl_status_t sl_se_get_tamper_reset_cause(sl_se_command_context_t *cmd_ctx,
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1218:                                         uint32_t *reset_cause)
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1220:  if (cmd_ctx == NULL || reset_cause == NULL || was_tamper_reset == NULL) {
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/security/sl_component/se_manager/src/sl_se_manager_util.c:1250:  *reset_cause = set_bit_position;
--- sl_hal_emu.c compiled?
0
--- __INLINE definition for GCC in CMSIS:
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/cmsis/Core/Include/cmsis_armclang.h:41:  #define __INLINE                               inline
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/cmsis/Core/Include/cmsis_clang.h:46:  #define __INLINE                               inline
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/cmsis/Core/Include/cmsis_compiler.h:72:    #define __INLINE                               inline
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/cmsis/Core/Include/cmsis_compiler.h:145:    #define __INLINE                               inline
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/cmsis/Core/Include/cmsis_compiler.h:215:    #define __INLINE                               inline
[exit 0]

### #8 $ P=C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p; grep -n 'define __INLINE\|define __STATIC_INLINE' $P/cmsis/Core/Include/cmsis_gcc.h; grep -n -- '-std=\|-O[0-3sg]\|-fgnu89' cmake_gcc/tilt_bed_controller.cmake cmake_gcc/toolchain.cmake cmake_gcc/autogen_toolchain.cmake cmake_gcc/CMakePresets.json | head; grep -rn 'sl_hal_emu_\(get\|clear\)_reset_cause' $P/platform_core --include=*.c | head -5; ls $P/platform_core/platform/peripheral/src | grep -i emu
44:  #define __INLINE                               inline
47:  #define __STATIC_INLINE                        static inline
cmake_gcc/tilt_bed_controller.cmake:365:    $<$<COMPILE_LANGUAGE:C>:-Os>
cmake_gcc/tilt_bed_controller.cmake:381:    $<$<COMPILE_LANGUAGE:CXX>:-Os>
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/peripheral/src/sl_hal_emu.c:115:extern __INLINE void sl_hal_emu_clear_reset_cause(void);
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/peripheral/src/sl_hal_emu.c:116:extern __INLINE uint32_t sl_hal_emu_get_reset_cause(void);
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/service/watchdog_manager/src/sl_watchdog_manager.c:487:  uint32_t reset_cause = sl_hal_emu_get_reset_cause();
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/service/watchdog_manager/src/sl_watchdog_manager.c:488:  sl_hal_emu_clear_reset_cause();
sl_hal_emu.c
[exit 0]

### #9 $ sed -n '355,385p' cmake_gcc/tilt_bed_controller.cmake; P=C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p; H=$(find $P/platform_core/platform/Device/SiliconLabs/EFR32BG22 -name 'efr32bg22_emu.h' | head -1); echo $H; grep -n 'define EMU_RSTCAUSE_[A-Z0-9]* ' $H; grep -n 'RSTCAUSE  *;\|RSTCAUSE;' $H
    "-Wl,--end-group"
)
target_compile_options(slc PUBLIC
    $<$<COMPILE_LANGUAGE:C>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:C>:-mthumb>
    $<$<COMPILE_LANGUAGE:C>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:C>:-mfloat-abi=hard>
    $<$<COMPILE_LANGUAGE:C>:-mcmse>
    $<$<COMPILE_LANGUAGE:C>:-Wall>
    $<$<COMPILE_LANGUAGE:C>:-Wextra>
    $<$<COMPILE_LANGUAGE:C>:-Os>
    $<$<COMPILE_LANGUAGE:C>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:C>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:C>:-fomit-frame-pointer>
    $<$<COMPILE_LANGUAGE:C>:-g>
    $<$<COMPILE_LANGUAGE:C>:--specs=nano.specs>
    $<$<COMPILE_LANGUAGE:C>:-fno-lto>
    $<$<COMPILE_LANGUAGE:CXX>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:CXX>:-mthumb>
    $<$<COMPILE_LANGUAGE:CXX>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:CXX>:-mfloat-abi=hard>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-mcmse>
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Os>
    $<$<COMPILE_LANGUAGE:CXX>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:CXX>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:CXX>:-fomit-frame-pointer>
    $<$<COMPILE_LANGUAGE:CXX>:-g>
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/Device/SiliconLabs/EFR32BG22/Include/efr32bg22_emu.h
602:#define EMU_RSTCAUSE_POR                           (0x1UL << 0)                            /**< Power On Reset                              */
607:#define EMU_RSTCAUSE_PIN                           (0x1UL << 1)                            /**< Pin Reset                                   */
612:#define EMU_RSTCAUSE_EM4                           (0x1UL << 2)                            /**< EM4 Wakeup Reset                            */
617:#define EMU_RSTCAUSE_WDOG0                         (0x1UL << 3)                            /**< Watchdog 0 Reset                            */
622:#define EMU_RSTCAUSE_LOCKUP                        (0x1UL << 5)                            /**< M33 Core Lockup Reset                       */
627:#define EMU_RSTCAUSE_SYSREQ                        (0x1UL << 6)                            /**< M33 Core Sys Reset                          */
632:#define EMU_RSTCAUSE_DVDDBOD                       (0x1UL << 7)                            /**< HVBOD Reset                                 */
637:#define EMU_RSTCAUSE_DVDDLEBOD                     (0x1UL << 8)                            /**< LEBOD Reset                                 */
642:#define EMU_RSTCAUSE_DECBOD                        (0x1UL << 9)                            /**< LVBOD Reset                                 */
647:#define EMU_RSTCAUSE_AVDDBOD                       (0x1UL << 10)                           /**< LEBOD1 Reset                                */
652:#define EMU_RSTCAUSE_IOVDD0BOD                     (0x1UL << 11)                           /**< LEBOD2 Reset                                */
657:#define EMU_RSTCAUSE_DCI                           (0x1UL << 16)                           /**< DCI reset                                   */
662:#define EMU_RSTCAUSE_VREGIN                        (0x1UL << 31)                           /**< DCDC VREGIN comparator                      */
66:  __IM uint32_t  RSTCAUSE;                      /**< Reset cause                                        */
[exit 0]

### #10 $ grep -n 'accel_running_average\|zeroLevelFirstPass\|in_settings_mode = ' settings_mode.c; grep -n 'void sample_motor_faults_tick' -A 22 exp_board.c; grep -n 'active_switch = \|auto_level_active = ' switch_input.c bed_actions.c settings_mode.c app.c dual_motor_control.c motor_current_functions.c; grep -n 'sleep_on_isr_exit\|SL_POWER_MANAGER_\(IGNORE\|SLEEP\|WAKEUP\)' autogen/sl_power_manager_handler.c
55:bool in_settings_mode = false;
59:static bool zeroLevelFirstPass = true;
134:  in_settings_mode = false;
216:        zeroLevelFirstPass = true;
286:        in_settings_mode = true;
310:        if ((!is_2000ms_timer_running) && (zeroLevelFirstPass)){
314:            if (zeroLevelFirstPass){
315:                printf("zeroLevelFirstPass is true\r\n");
321:        if (zeroLevelFirstPass){
322:            accel_running_average();
328:        if ((!is_5000ms_timer_running) && (!zeroLevelFirstPass) && (resetZeroLevelFirstPass)){
339:            zeroLevelFirstPass = false;
354:void sample_motor_faults_tick(void)
355-{
356-  uint32_t now = sl_sleeptimer_get_tick_count();
357-
358-  if (sl_sleeptimer_tick_to_ms(now - last_fault_sample_tick) < MOTOR_FAULT_SAMPLE_INTERVAL_MS) {
359-    return;
360-  }
361-
362-  last_fault_sample_tick = now;
363-  cached_motor_faults = get_motor_faults();
364-}
365-
366-// Non-blocking accessor for the last value sample_motor_faults_tick() read.
367-// Safe to call from any context (e.g. bed_status_get()) - never touches I2C.
368-uint8_t get_cached_motor_faults(void)
369-{
370-  return cached_motor_faults;
371-}
372-
373-void set_underbed_lighting_on()
374-{
375-  I2C_TransferReturn_TypeDef ret;
376-  uint8_t write_cmd[2];
switch_input.c:529:              active_switch = OFF;
switch_input.c:541:              active_switch = OFF;
switch_input.c:553:              active_switch = OFF;
switch_input.c:568:              active_switch = OFF;
switch_input.c:582:                    active_switch = OFF;
switch_input.c:860:  active_switch = OFF;
switch_input.c:979:          active_switch = OFF;
bed_actions.c:73:  active_switch = switch_value;
bed_actions.c:159:      active_switch = up ? SWITCH_UP_ALL_VALUE : SWITCH_DOWN_ALL_VALUE;
bed_actions.c:187:  active_switch = SWITCH_AUTO_LEVEL_VALUE;
bed_actions.c:189:  auto_level_active = true;
bed_actions.c:214:  auto_level_active = false;
bed_actions.c:230:  active_switch = SWITCH_BED_LIGHT_VALUE;
bed_actions.c:266:  active_switch = OFF;
bed_actions.c:354:  active_switch = OFF;
bed_actions.c:394:  active_switch = OFF;
settings_mode.c:161:  active_switch = OFF;
settings_mode.c:196:        active_switch = SWITCH_UP_ALL_VALUE;
settings_mode.c:202:        active_switch = OFF;
settings_mode.c:215:        active_switch = SWITCH_AUTO_LEVEL_VALUE;
settings_mode.c:221:        active_switch = OFF;
settings_mode.c:232:        active_switch = SWITCH_DOWN_ALL_VALUE;
settings_mode.c:238:        active_switch = OFF;
settings_mode.c:259:        active_switch = OFF;
app.c:102:bool auto_level_active = false; // Tracks whether autolevel is running - extern via app.h, see bed_actions.c
app.c:143:active_switch_t active_switch = OFF;
app.c:590:      auto_level_active = false;
app.c:616:      active_switch = OFF;
app.c:643:          auto_level_active = false;
app.c:682:              auto_level_active = false;
32: * @return  SL_POWER_MANAGER_IGNORE, if the module did not trigger an ISR and it
35: *          SL_POWER_MANAGER_SLEEP, The module was the one that caused the system
38: *          SL_POWER_MANAGER_WAKEUP, The module was the one that caused the system
46:SL_WEAK sl_power_manager_on_isr_exit_t app_sleep_on_isr_exit(void)
48:  return SL_POWER_MANAGER_IGNORE;
77:bool sl_power_manager_sleep_on_isr_exit(void)
89:  answer = sli_bt_sleep_on_isr_exit();
90:  if (answer == SL_POWER_MANAGER_WAKEUP) {
92:  } else if (answer == SL_POWER_MANAGER_SLEEP) {
96:  answer = sl_iostream_usart_io_stream_sleep_on_isr_exit();
97:  if (answer == SL_POWER_MANAGER_WAKEUP) {
99:  } else if (answer == SL_POWER_MANAGER_SLEEP) {
104:  answer = app_sleep_on_isr_exit();
105:  if (answer == SL_POWER_MANAGER_WAKEUP) {
107:  } else if (answer == SL_POWER_MANAGER_SLEEP) {
[exit 0]

### #11 $ sed -n 70,120p autogen/sl_power_manager_handler.c; sed -n 850,865p switch_input.c; sed -n 970,985p switch_input.c; sed -n 300,345p settings_mode.c; grep -n 'MOTOR_FAULT_SAMPLE_INTERVAL_MS' exp_board.c | head -2
}

/***************************************************************************//**
 * Mandatory callback that must validate if the MCU can sleep after having
 * processed an interrupt when the system was sleeping.
 ******************************************************************************/
 SL_CODE_CLASSIFY(SL_CODE_COMPONENT_POWER_MANAGER, SL_CODE_CLASS_TIME_CRITICAL)
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
}
  // Nothing is commanding motion and a motor is on. De-energise.
  set_all_motors_off();
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    motor_state[i].motor_on = false;
  }
  set_LED_off(LED_GREEN);
  set_LED_off(LED_BLUE);
  set_LED_off(LED_RED);
  clear_switch_panel_state();
  active_switch = OFF;

  // Leave NO desynced bookkeeping behind. A fail-safe that stops the motors
  // and strands the state has converted a runaway into the stuck state this
  // project has already hit twice (#38 additions).
  //
          // switch_driving naming a button process_outputs would still
          // energise on, which is motion with bed_control never consulted.
          clear_switch_panel_state();
          // Release the switches' half of the #36 latch, for the same reason
          // as in motion_failsafe_tick(): clearing switch_held above means the
          // held buttons' later releases find no match and never reach
          // bed_move_stop(), so without this the switches' half of the latch
          // could stay set until some later press/release.
          bed_note_source_released(BED_SOURCE_SWITCHES);
          active_switch = OFF;
          // Reset motor readings
          reset_ignore_data_points_count();
          reset_motor_current_data();
          // Set all LEDs off
          set_LED_off(LED_GREEN);
          set_LED_off(LED_BLUE);
      (1) Start timers to trip timeout after 1 & 5 seconds
              -> during the 1 second timer, accel values are read and averaged
              -> the 5 second timer is used to reset stored values if button is continuously held
      (2) After timeout, store roll and pitch angles and turn on green LED to indicate successful NVM3 writes
      ******************************/

      if (active_switch == SWITCH_AUTO_LEVEL_VALUE){
        sl_sleeptimer_is_timer_running(&handle_2000ms_timer, &is_2000ms_timer_running);

        // Only start 2 second timer to read and store accel values once per button press
        if ((!is_2000ms_timer_running) && (zeroLevelFirstPass)){
            start_2000ms_oneshot_timer();
#if !BED_CAPTURE_MOTOR_CURRENTS
            printf("2 second timer start\r\n");
            if (zeroLevelFirstPass){
                printf("zeroLevelFirstPass is true\r\n");
            }
#endif
            set_LED_on(LED_RED);
        }

        if (zeroLevelFirstPass){
            accel_running_average();
        }

        sl_sleeptimer_is_timer_running(&handle_5000ms_timer, &is_5000ms_timer_running);

        // Only start 5 second timer to reset stored accel values after the store function has processed and the user has not already reset values
        if ((!is_5000ms_timer_running) && (!zeroLevelFirstPass) && (resetZeroLevelFirstPass)){
            start_5000ms_oneshot_timer();
#if !BED_CAPTURE_MOTOR_CURRENTS
            printf("5 second timer start\r\n");
#endif
        }


        // After 1 second of holding the button, write roll and pitch values
        if (timeout_2000ms){

            zeroLevelFirstPass = false;
            sl_sleeptimer_stop_timer(&handle_2000ms_timer);
            timeout_2000ms = false;

#if !BED_CAPTURE_MOTOR_CURRENTS
            printf("Writing values to NVM3...\r\n");
#endif
27:#define MOTOR_FAULT_SAMPLE_INTERVAL_MS       200
336:// to once per MOTOR_FAULT_SAMPLE_INTERVAL_MS so it doesn't add a second
[exit 0]

### #12 $ sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
ninja: no work to do.
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 245732	    788	  72472	 318992	  4de10	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

### #13 $ sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
[1/3] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/3] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/3] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 246236	    788	  72472	 319496	  4e008	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

### #14 $ '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-nm.exe' cmake_gcc/build/base/tilt_bed_controller.out | grep -i 'reset_cause\|sl_hal_emu' ; echo 'nm grep exit (1 = no such symbol):' $?; sed -i 's/^#define BLE_DEBUG_WATCHDOG_LOG 1$/#define BLE_DEBUG_WATCHDOG_LOG 0/' ble_bed_service.h && grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h && sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
00037a24 t reset_cause_names
nm grep exit (1 = no such symbol): 0
36:#define BLE_DEBUG_WATCHDOG_LOG 0
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 244380	    780	  72480	 317640	  4d8c8	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

### #15 $ sed -i 's/^#define BLE_DEBUG_WATCHDOG_LOG 0$/#define BLE_DEBUG_WATCHDOG_LOG 1/' ble_bed_service.h && grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h && sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out && git status --short
36:#define BLE_DEBUG_WATCHDOG_LOG 1
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 246236	    788	  72472	 319496	  4e008	cmake_gcc/build/base/tilt_bed_controller.out
 M .projectlinkstore
 M app.c
 M ble_bed_service.c
?? docs/prompts/PROMPT_AI_latch-refinements-live-tilt.md
[exit 0]

### #16 $ grep -n 'static void latch_app_starts\|if (app_start_latch != APP_LATCH_COLLISION)\|bool latch_stop = \|if (latch_stop)\|STOP_ALL (no app hold\|STOP_ALL latch set\|app_start_latch == APP_LATCH_COLLISION || any_collision\|CMD_RESULT_STOP_LATCHED);\|^void ble_bed_service_tick\|service_app_commanding();\|process_pending_command();\|note_app_stopped_commanding("' ble_bed_service.c; grep -n 'sl_hal_emu\|print_reset_cause\|reset_cause_names\[\] = ' app.c
594:static void latch_app_starts(app_start_latch_t reason)
596:  if (app_start_latch != APP_LATCH_COLLISION) {
633:    note_app_stopped_commanding("silence");
700:  note_app_stopped_commanding("watchdog");
752:    bool latch_stop = app_commanding || (app_start_latch != APP_LATCH_NONE);
771:    if (latch_stop) {
776:    if (latch_stop) {
777:      printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
780:      printf("BLE: STOP_ALL (no app hold, no latch)\r\n");
870:    if (app_start_latch == APP_LATCH_COLLISION || any_collision) {
875:      send_command_result(conn, seq, CMD_RESULT_STOP_LATCHED);
950:      note_app_stopped_commanding("stop");
989:      note_app_stopped_commanding("stop");
1165:void ble_bed_service_tick(void)
1171:  service_app_commanding();
1180:    note_app_stopped_commanding("disconnect"); // the app is gone - release its half of the latch
1188:  process_pending_command();
1190:  service_app_commanding();
47:#include "sl_hal_emu.h"   // sl_hal_emu_get_reset_cause() / sl_hal_emu_clear_reset_cause()
393:} reset_cause_names[] = {
409:static void print_reset_cause(uint32_t cause)
446:  // SDK's EMU API (sl_hal_emu.h). The register accumulates bits until cleared,
450:  uint32_t reset_cause = sl_hal_emu_get_reset_cause();
451:  sl_hal_emu_clear_reset_cause();
453:  print_reset_cause(reset_cause);
[exit 0]

### #17 $ git diff --stat; for f in app.c ble_bed_service.c auto_level.c bed_status.c bed_status.h bed_actions.c bed_actions.h auto_level.h ble_bed_service.h; do printf '%s CR=%s HEAD_CR=%s\n' $f $(tr -cd '\r' < $f | wc -c) $(git show HEAD:$f | tr -cd '\r' | wc -c); done
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
 .projectlinkstore       |  2 +-
 app.c                   | 57 +++++++++++++++++++++++++++++++++++++++++++++++++
 ble_bed_service.c       | 57 ++++++++++++++++++++++++++++++++++++++-----------
 docs/BLE_GATT_DESIGN.md | 43 +++++++++++++++++++++++++------------
 4 files changed, 131 insertions(+), 28 deletions(-)
app.c CR=0 HEAD_CR=0
ble_bed_service.c CR=0 HEAD_CR=0
auto_level.c CR=846 HEAD_CR=0
bed_status.c CR=0 HEAD_CR=0
bed_status.h CR=0 HEAD_CR=0
bed_actions.c CR=415 HEAD_CR=0
bed_actions.h CR=0 HEAD_CR=0
auto_level.h CR=95 HEAD_CR=0
ble_bed_service.h CR=0 HEAD_CR=0
[exit 0]

### #18 $ sed -n '594,600p;743,746p;780,783p;866,884p' ble_bed_service.c
static void latch_app_starts(app_start_latch_t reason)
{
  if (app_start_latch != APP_LATCH_COLLISION) {
    app_start_latch = reason;
  }
  note_app_commanding();
}
  if (stop_all_pending) {
    uint8_t seq  = stop_all_seq;
    uint8_t conn = stop_all_conn;
    stop_all_pending = false;
      printf("BLE: STOP_ALL (no app hold, no latch)\r\n");
    }
#endif
  }
  // must not release the latch. No bed_control call, no last_move_seq update,
  // no watchdog arm.
  if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
      && (app_start_latch != APP_LATCH_NONE || any_collision)) {
    if (app_start_latch == APP_LATCH_COLLISION || any_collision) {
      latch_app_starts(APP_LATCH_COLLISION);
      send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);
    } else {
      latch_app_starts(APP_LATCH_STOP);
      send_command_result(conn, seq, CMD_RESULT_STOP_LATCHED);
    }
#if BLE_DEBUG_WATCHDOG_LOG
    if (wd_diag_latch_refused < 0xFFFF) {
      wd_diag_latch_refused++;
    }
#endif
    return;
  }

[exit 0]

### #19 $ git add app.c ble_bed_service.c docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/PRE_SHIP.md docs/prompts/PROMPT_AI_latch-refinements-live-tilt.md && git status --short && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/msgA.txt' && git log --oneline -1 && git show --stat HEAD | tail -8
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/PRE_SHIP.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/prompts/PROMPT_AI_latch-refinements-live-tilt.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  app.c
M  ble_bed_service.c
M  docs/BLE_GATT_DESIGN.md
M  docs/DECISIONS.md
M  docs/PRE_SHIP.md
A  docs/prompts/PROMPT_AI_latch-refinements-live-tilt.md
8e1242d Latch refinements and reset cause (Prompt AI part A, DECISIONS #49 amended)

 app.c                                              |  57 ++++++++
 ble_bed_service.c                                  |  57 ++++++--
 docs/BLE_GATT_DESIGN.md                            |  43 ++++--
 docs/DECISIONS.md                                  |  79 +++++++++++
 docs/PRE_SHIP.md                                   |   2 +-
 .../PROMPT_AI_latch-refinements-live-tilt.md       | 144 +++++++++++++++++++++
 6 files changed, 354 insertions(+), 28 deletions(-)
[exit 0]

### #20 $ sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out && sed -i 's/^#define BED_DEBUG_LEVELED_LOG 1\r\?$/#define BED_DEBUG_LEVELED_LOG 0/' bed_actions.c && grep -n 'define BED_DEBUG_LEVELED_LOG' bed_actions.c && sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
[1/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_control.c.obj
[2/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_status.c.obj
[3/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[4/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/settings_mode.c.obj
[5/9] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[6/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/motor_current_functions.c.obj
[7/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[8/9] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[9/9] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 246412	    788	  72472	 319672	  4e0b8	cmake_gcc/build/base/tilt_bed_controller.out
47:#define BED_DEBUG_LEVELED_LOG 0
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 246260	    788	  72472	 319520	  4e020	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

### #21 $ sed -i 's/^#define BED_DEBUG_LEVELED_LOG 0$/#define BED_DEBUG_LEVELED_LOG 1\r/' bed_actions.c && grep -n 'define BED_DEBUG_LEVELED_LOG' bed_actions.c && printf 'CR=%s lines=%s\n' $(tr -cd '\r' < bed_actions.c | wc -c) $(wc -l < bed_actions.c) && sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out && git diff --stat
47:#define BED_DEBUG_LEVELED_LOG 1
CR=1 lines=440
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 246412	    788	  72472	 319672	  4e0b8	cmake_gcc/build/base/tilt_bed_controller.out
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_actions.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_actions.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_status.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_status.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
 .projectlinkstore |  2 +-
 app.c             |  4 ++++
 bed_actions.c     | 29 +++++++++++++++++++++++++++--
 bed_actions.h     | 10 ++++++++++
 bed_status.c      |  1 +
 bed_status.h      |  1 +
 ble_bed_service.c |  1 +
 7 files changed, 45 insertions(+), 3 deletions(-)
[exit 0]

### #22 $ sed -i 's/\r$//' bed_actions.c && printf 'CR=%s\n' $(tr -cd '\r' < bed_actions.c | wc -c) && git diff bed_actions.c | grep -c '^[-+]#define BED_DEBUG_LEVELED_LOG'; sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' | tail -3
CR=0
warning: in the working copy of 'bed_actions.c', LF will be replaced by CRLF the next time Git touches it
0
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
[exit 0]

### #23 $ git add app.c bed_actions.c bed_actions.h bed_status.c bed_status.h ble_bed_service.c docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/PRE_SHIP.md && git status --short && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/msgB.txt' && git log --oneline -1 && git show --stat HEAD | tail -11
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_actions.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_actions.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_status.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_status.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/PRE_SHIP.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  app.c
M  bed_actions.c
M  bed_actions.h
M  bed_status.c
M  bed_status.h
M  ble_bed_service.c
M  docs/BLE_GATT_DESIGN.md
M  docs/DECISIONS.md
M  docs/PRE_SHIP.md
0d45fff Bed Status flag bit 5 = AUTO_LEVEL_SETTLING (Prompt AI part B)

 app.c                   |  4 ++++
 bed_actions.c           | 29 +++++++++++++++++++++++++--
 bed_actions.h           | 10 ++++++++++
 bed_status.c            |  1 +
 bed_status.h            |  1 +
 ble_bed_service.c       |  1 +
 docs/BLE_GATT_DESIGN.md | 52 ++++++++++++++++++++++++++++++++++++++++++++++++-
 docs/DECISIONS.md       | 22 +++++++++++++++++++++
 docs/PRE_SHIP.md        |  2 +-
 9 files changed, 118 insertions(+), 4 deletions(-)
[exit 0]

### #24 $ H=$(find C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/service/sleeptimer -name sl_sleeptimer.h | head -1); echo $H; grep -n '^sl_status_t sl_sleeptimer_restart_periodic_timer\|^uint32_t sl_sleeptimer_get_timer_frequency\|^sl_status_t sl_sleeptimer_stop_timer' $H; grep -rn 'bed_status_get(' --include=*.c . | grep -v '^./test'
C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/service/sleeptimer/inc/sl_sleeptimer.h
252:sl_status_t sl_sleeptimer_restart_periodic_timer(sl_sleeptimer_timer_handle_t *handle,
270:sl_status_t sl_sleeptimer_stop_timer(sl_sleeptimer_timer_handle_t *handle);
336:uint32_t sl_sleeptimer_get_timer_frequency(void);
917:sl_status_t sl_sleeptimer_restart_periodic_timer_ms(sl_sleeptimer_timer_handle_t *handle,
./auto_level.c:281:  // at the wire boundary, in bed_status.c's bed_status_get(). That is the
./bed_status.c:73:void bed_status_get(bed_status_t *out)
./bed_status.c:79:  // whatever context calls bed_status_get() - this previously included the
./ble_bed_service.c:1218:  bed_status_get(&status);
./ble_bed_service.c:1541:          // bed_status_get() can block on I2C (bench-confirmed: this was
./exp_board.c:35:// main-loop context. bed_status_get() reads this via get_cached_motor_faults()
./exp_board.c:367:// Safe to call from any context (e.g. bed_status_get()) - never touches I2C.
[exit 0]

### #25 $ sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
[1/11] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_status.c.obj
[2/11] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[3/11] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_settings.c.obj
[4/11] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[5/11] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/auto_level.c.obj
[6/11] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/settings_mode.c.obj
[7/11] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/dual_motor_control.c.obj
[8/11] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/motor_current_functions.c.obj
[9/11] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[10/11] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[11/11] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 248380	    788	  72472	 321640	  4e868	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

### #26 $ printf 'auto_level.c CR=%s\n' $(tr -cd '\r' < auto_level.c | wc -c); sed -i 's/^#define TILT_IDLE_DIAG 1$/#define TILT_IDLE_DIAG 0/' auto_level.c && grep -n 'define TILT_IDLE_DIAG ' auto_level.c && sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out && sed -i 's/^#define TILT_IDLE_DIAG 0$/#define TILT_IDLE_DIAG 1/' auto_level.c && grep -n 'define TILT_IDLE_DIAG ' auto_level.c && sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out; '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-nm.exe' -S cmake_gcc/build/base/tilt_bed_controller.out | grep -i ' expf\|tilt_idle_\|tilt_diag_\|auto_level_chain_\|status_notify_interval'
auto_level.c CR=1095
73:#define TILT_IDLE_DIAG 0
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/auto_level.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 248004	    788	  72472	 321264	  4e6f0	cmake_gcc/build/base/tilt_bed_controller.out
73:#define TILT_IDLE_DIAG 1
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/auto_level.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 248380	    788	  72472	 321640	  4e868	cmake_gcc/build/base/tilt_bed_controller.out
00009894 0000000c T accel_auto_level_chain_ran
200037be 00000001 b auto_level_chain_ran
200037bf 00000001 b auto_level_chain_updated
0000e288 00000078 T expf
2000218c 00000004 b status_notify_interval_ms
2000304a 00000002 b tilt_diag_count
20003042 00000002 b tilt_diag_pitch_max
20003044 00000002 b tilt_diag_pitch_min
20003046 00000002 b tilt_diag_roll_max
20003048 00000002 b tilt_diag_roll_min
200037a0 00000001 b tilt_diag_running
20001c1c 00000004 b tilt_diag_start_tick
000098a0 00000014 T tilt_idle_active
000096dc 00000034 t tilt_idle_close
000098cc 00000028 T tilt_idle_fresh
200037a1 00000001 b tilt_idle_have_value
20001c20 00000004 b tilt_idle_last_sample_tick
200037a2 00000001 b tilt_idle_open
20001c24 00000004 b tilt_idle_open_tick
20001c28 00000004 b tilt_idle_pitch
2000304c 00000002 b tilt_idle_pitch_out
000098c0 0000000c T tilt_idle_pitch_scaled
20001c2c 00000004 b tilt_idle_roll
2000304e 00000002 b tilt_idle_roll_out
000098b4 0000000c T tilt_idle_roll_scaled
200037a3 00000001 b tilt_idle_sample_due
20001c30 00000020 b tilt_idle_timer
000096d0 0000000c t tilt_idle_timer_callback
[exit 0]

### #27 $ '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' -A cmake_gcc/build/base/tilt_bed_controller.out | grep -v '^\.debug\|^\.comment\|^\.ARM.attributes'
cmake_gcc/build/base/tilt_bed_controller.out  :
section                   size         addr
.vectors                   320            0
.stack                    2752    536870912
.bss                     12000    536873664
text_application_ram       480    536885664
.text                   247572          800
.ARM.exidx                   8       248372
.copy.table                 12       248380
.zero.table                  0       248392
.data                      776    536886144
.memory_manager_heap     16760    536886920
.nvm                     40960   4294451200
Total                  1902579


[exit 0]

### #28 $ grep -n 'define TILT_IDLE_\|^static void tilt_idle_\|^static int16_t round_to_int16\|^bool tilt_idle_\|^int16_t tilt_idle_\|^bool tilt_telemetry_perpass_ran\|^bool accel_auto_level_chain_ran\|^void tilt_telemetry_tick\|tilt_idle_open_window(perpass_ran_last_pass)\|float alpha = \|auto_level_chain_updated = true\|printf("TILT idle' auto_level.c; grep -n 'tilt_fresh   = \|} else if (tilt_idle_active())' bed_status.c; grep -n 'STATUS_NOTIFY_IDLE_LIVE_MS\|bool idle_live\|interval_ms != status_notify_interval_ms\|status.tilt_fresh' ble_bed_service.c
64:#define TILT_IDLE_SAMPLE_MS 100        // 10 Hz sample period
65:#define TILT_IDLE_TAU_MS 500           // filter time constant; bench-tunable from TILT_IDLE_DIAG's lines
66:#define TILT_IDLE_CONVERGED_MS (3 * TILT_IDLE_TAU_MS) // ~95 % settled after the window opens
67:#define TILT_IDLE_FRESH_MS 300         // tilt_fresh: an idle value younger than this is fresh (3 periods)
73:#define TILT_IDLE_DIAG 1
74:#define TILT_IDLE_DIAG_PERIOD_MS 10000
363:      auto_level_chain_updated = true;
466:bool tilt_telemetry_perpass_ran(void)
471:bool accel_auto_level_chain_ran(void)
528:static void tilt_idle_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
535:static int16_t round_to_int16(float x)
540:static void tilt_idle_open_window(bool seed_from_perpass)
565:static void tilt_idle_close(void)
582:static void tilt_idle_diag_sample(uint32_t now)
603:    printf("TILT idle: %u samples in %lu ms, roll min %d max %d p2p %d, pitch min %d max %d p2p %d (deg x100, pitch internal sign)\r\n",
614:static void tilt_idle_sample(void)
628:    float alpha = 1.0f - expf(-dt_ms / (float)TILT_IDLE_TAU_MS);
645:bool tilt_idle_active(void)
650:int16_t tilt_idle_roll_scaled(void)
655:int16_t tilt_idle_pitch_scaled(void)
660:bool tilt_idle_fresh(void)
671:void tilt_telemetry_tick(bool status_subscribed, bool other_sampler_active)
728:    tilt_idle_open_window(perpass_ran_last_pass);
126:    tilt_fresh   = accel_auto_level_chain_ran();
127:  } else if (tilt_idle_active()) {
130:    tilt_fresh   = tilt_idle_fresh();
134:    tilt_fresh   = tilt_telemetry_perpass_ran();
138:    tilt_fresh   = false;
153:#define STATUS_NOTIFY_IDLE_LIVE_MS   200 // idle with live tilt: idle sampler or a burst (Prompt AI C3)
1223:  ble_bed_service_encode_status(&status, status.tilt_fresh, packed); // tilt_fresh: Prompt AI C4
1231:  bool idle_live = tilt_idle_active()
1234:                       : (idle_live ? STATUS_NOTIFY_IDLE_LIVE_MS : STATUS_NOTIFY_IDLE_MS);
1237:      || (interval_ms != status_notify_interval_ms)) {
[exit 0]

### #29 $ git add app.c auto_level.c auto_level.h bed_status.c bed_status.h ble_bed_service.c ble_bed_service.h docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/PRE_SHIP.md && git status --short && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/msgC.txt' && git log --oneline -1 && git show --stat HEAD | tail -12
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'auto_level.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_status.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'bed_status.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'ble_bed_service.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/PRE_SHIP.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  app.c
M  auto_level.c
M  auto_level.h
M  bed_status.c
M  bed_status.h
M  ble_bed_service.c
M  ble_bed_service.h
M  docs/BLE_GATT_DESIGN.md
M  docs/DECISIONS.md
M  docs/PRE_SHIP.md
3e9349f Live idle tilt: 10 Hz idle sampler, ms filter, 5 Hz notify, tilt_fresh (Prompt AI part C, DECISIONS #50)

 app.c                   |   4 +
 auto_level.c            | 261 ++++++++++++++++++++++++++++++++++++++++++++++--
 auto_level.h            |  28 ++++++
 bed_status.c            |  25 ++++-
 bed_status.h            |   8 +-
 ble_bed_service.c       |  35 +++++--
 ble_bed_service.h       |   7 +-
 docs/BLE_GATT_DESIGN.md |  31 ++++--
 docs/DECISIONS.md       | 134 +++++++++++++++++++++++++
 docs/PRE_SHIP.md        |   6 +-
 10 files changed, 499 insertions(+), 40 deletions(-)
[exit 0]

### #30 $ git show --numstat HEAD -- auto_level.c; git show HEAD -- auto_level.c | grep '^-' ; git show HEAD:auto_level.c | tr -cd '\r' | wc -c
commit 3e9349f9dc546211329856110e255913ff9a851d
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Tue Nov 3 08:55:41 2026 -0800

    Live idle tilt: 10 Hz idle sampler, ms filter, 5 Hz notify, tilt_fresh (Prompt AI part C, DECISIONS #50)
    
    - Idle window = subscribed, no motor, no burst, not auto-level, not
      settings. A 100 ms periodic sleeptimer only sets a flag;
      tilt_telemetry_tick() does one read_accel() per flag (read_accel() keeps
      ACCEL_I2C_MIN_INTERVAL_MS). Stands down with gates 2a/2b/2c.
    - Own filter: y += alpha*(x - y), alpha = 1 - exp(-dt/TILT_IDLE_TAU_MS),
      dt measured, tau 500 ms, on baseline-relative internal-sign values.
      Seeded from the per-pass chain if it ran in the previous pass (end of a
      move/burst), else from the first idle sample. Moving and auto-level
      chains unchanged.
    - bed_status.c ships the idle value while the window is open and sets
      tilt_fresh (bit 3) from the chain it ships; ble_bed_service.c passes it.
    - Notify 5 Hz when not moving and tilt is live (idle window, or a burst
      outside settings mode); a cadence change publishes at once.
    - No new EM0 veto. TILT_IDLE_DIAG (1): every 10 s after 3*tau, one line of
      sample count and min/max/p2p of filtered roll and pitch.
    
    Docs: DECISIONS #50; BLE_GATT_DESIGN §6 (tilt_fresh table, cadence);
    PRE_SHIP F11, F16, new F29 (TILT_IDLE_DIAG), F30 (backgrounded app).
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

255	6	auto_level.c
--- a/auto_level.c
-  // valid for that case, so the T3 arm below sits AFTER this return.
-  // Gate 2b: is the sampling window open?
-  if (!motors_on && !tilt_burst_running) {
-  read_accel();
-  accel_filter_update(&telemetry_filter);
-  tilt_telemetry_valid = true;
0
[exit 0]

### #31 $ sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
[1/10] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_status.c.obj
[2/10] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_settings.c.obj
[3/10] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[4/10] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/settings_mode.c.obj
[5/10] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/dual_motor_control.c.obj
[6/10] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[7/10] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/motor_current_functions.c.obj
[8/10] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/auto_level.c.obj
[9/10] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[10/10] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 248540	    788	  72472	 321800	  4e908	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

### #32 $ git add app.c auto_level.c auto_level.h docs/BLE_GATT_DESIGN.md docs/DECISIONS.md && git status --short && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/msgD.txt' && git log --oneline -1 && git show --stat HEAD | tail -7
warning: in the working copy of 'app.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'auto_level.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  app.c
M  auto_level.c
M  auto_level.h
M  docs/BLE_GATT_DESIGN.md
M  docs/DECISIONS.md
8996259 LEVELED (bit 4) clears itself when tilted off level while idle (Prompt AI part D, DECISIONS #46 amended)

 app.c                   |  7 +++++++
 auto_level.c            | 47 +++++++++++++++++++++++++++++++++++++++++++++--
 auto_level.h            |  6 ++++++
 docs/BLE_GATT_DESIGN.md | 14 ++++++++++++--
 docs/DECISIONS.md       | 21 +++++++++++++++++++++
 5 files changed, 91 insertions(+), 4 deletions(-)
[exit 0]

### #33 $ python -I 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/regr_edit.py' && git diff --stat docs/REGRESSION.md && grep -n '^## 6b\|^- \[ \] 1.R\|^- \[ \] 5b.3d\|^- \[ \] 5b.8\|^- \[ \] 6.11 ' docs/REGRESSION.md
ok
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
 docs/REGRESSION.md | 116 ++++++++++++++++++++++++++++++++++++++++++++++-------
 1 file changed, 102 insertions(+), 14 deletions(-)
51:- [ ] 1.R Reset cause (Prompt AI A4): right after the version line, one line
327:- [ ] 5b.3d (Prompt AI A2) Hold HEAD up in the app, tap STOP, keep holding
361:- [ ] 5b.8 Collision wins (Prompt AI A1). App holding HEAD up; tap STOP and
467:- [ ] 6.11 AUTO_LEVEL_SETTLING, Bed Status flag bit 5 (Prompt AI part B).
493:## 6b. Live idle tilt and self-clearing LEVELED (Prompt AI parts C-D, DECISIONS #50, #46 amendment)
[exit 0]

### #34 $ sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/matrix.sh'
=== defaults
auto_level.c:73:#define TILT_IDLE_DIAG 1
bed_actions.c:47:#define BED_DEBUG_LEVELED_LOG 1
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
ninja: no work to do.
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
 248540	    788	  72472	 321800	  4e908	cmake_gcc/build/base/tilt_bed_controller.out
=== TILT_IDLE_DIAG 0
auto_level.c:73:#define TILT_IDLE_DIAG 0
bed_actions.c:47:#define BED_DEBUG_LEVELED_LOG 1
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
 248164	    788	  72472	 321424	  4e790	cmake_gcc/build/base/tilt_bed_controller.out
=== BED_DEBUG_LEVELED_LOG 0
auto_level.c:73:#define TILT_IDLE_DIAG 1
bed_actions.c:47:#define BED_DEBUG_LEVELED_LOG 0
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
[3/3] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
 248396	    788	  72472	 321656	  4e878	cmake_gcc/build/base/tilt_bed_controller.out
=== BLE_DEBUG_WATCHDOG_LOG 0
auto_level.c:73:#define TILT_IDLE_DIAG 1
bed_actions.c:47:#define BED_DEBUG_LEVELED_LOG 1
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 0
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
[5/5] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
 246652	    780	  72480	 319912	  4e1a8	cmake_gcc/build/base/tilt_bed_controller.out
=== all three 0
auto_level.c:73:#define TILT_IDLE_DIAG 0
bed_actions.c:47:#define BED_DEBUG_LEVELED_LOG 0
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 0
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
[6/6] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
 246132	    780	  72480	 319392	  4dfa0	cmake_gcc/build/base/tilt_bed_controller.out
=== BED_CAPTURE_MOTOR_CURRENTS 1 (extra)
auto_level.c:73:#define TILT_IDLE_DIAG 1
bed_actions.c:47:#define BED_DEBUG_LEVELED_LOG 1
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 1
[9/9] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
 245028	    788	  72472	 318288	  4db50	cmake_gcc/build/base/tilt_bed_controller.out
=== restored defaults
auto_level.c:73:#define TILT_IDLE_DIAG 1
bed_actions.c:47:#define BED_DEBUG_LEVELED_LOG 1
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
[8/8] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
 248540	    788	  72472	 321800	  4e908	cmake_gcc/build/base/tilt_bed_controller.out
=== git status --short (source files must not appear)
 M .projectlinkstore
 M debug_capture.h
 M docs/REGRESSION.md
[exit 0]

### #35 $ git diff --stat debug_capture.h; git diff debug_capture.h | cat -A | head -20; git show HEAD:debug_capture.h | tail -c 80 | od -c | tail -4; tail -c 80 debug_capture.h | od -c | tail -4
warning: in the working copy of 'debug_capture.h', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'debug_capture.h', LF will be replaced by CRLF the next time Git touches it
0000040   (   (   v   o   i   d   )   0   )  \n   #   e   n   d   i   f
0000060  \n  \n   #   e   n   d   i   f       /   *       D   E   B   U
0000100   G   _   C   A   P   T   U   R   E   _   H   _       *   /  \n
0000120
0000040   (   (   v   o   i   d   )   0   )  \n   #   e   n   d   i   f
0000060  \n  \n   #   e   n   d   i   f       /   *       D   E   B   U
0000100   G   _   C   A   P   T   U   R   E   _   H   _       *   /  \n
0000120
[exit 0]

### #36 $ git checkout -- debug_capture.h && grep -n 'define BED_CAPTURE_MOTOR_CURRENTS' debug_capture.h && git status --short && sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
21:#define BED_CAPTURE_MOTOR_CURRENTS 0
 M .projectlinkstore
 M docs/REGRESSION.md
[1/8] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_settings.c.obj
[2/8] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[3/8] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/settings_mode.c.obj
[4/8] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/motor_current_functions.c.obj
[5/8] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[6/8] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/auto_level.c.obj
[7/8] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[8/8] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 248540	    788	  72472	 321800	  4e908	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

### #37 $ which gcc; cd test && gcc -std=c99 -Wall -I.. -o 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/bed_control_test.exe' bed_control_test.c ../bed_control.c && 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/bed_control_test.exe' | tail -4; cd .. ; grep -n 'define BED_BENCH_PROVOKE_RUNAWAY\|define BLE_BENCH_WIPE_BONDS\|define BED_CAPTURE_MOTOR_CURRENTS\|define BLE_DEBUG_WATCHDOG_LOG\|define TILT_IDLE_DIAG \|define BED_DEBUG_LEVELED_LOG' switch_input.c ble_bed_service.c debug_capture.h ble_bed_service.h auto_level.c bed_actions.c | tr -d '\r'; git diff --stat cd2a148 HEAD -- config autogen bed_control.c '*.btconf'
/c/MinGW/bin/gcc
PASS: scenario 6b: bed_move_stop still works while inhibited
PASS: scenario 6c: lights are not motion and are not inhibited

0 check(s) failed
switch_input.c:98:#define BED_BENCH_PROVOKE_RUNAWAY 0
ble_bed_service.c:70:#define BLE_BENCH_WIPE_BONDS 0
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
auto_level.c:73:#define TILT_IDLE_DIAG 1
bed_actions.c:47:#define BED_DEBUG_LEVELED_LOG 1
[exit 0]

### #38 $ git add docs/REGRESSION.md && git status --short && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/msgE.txt' && git log --oneline -6 && git show --stat HEAD | tail -3
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  docs/REGRESSION.md
05c4c2f REGRESSION items for Prompt AI (part E, docs only)
8996259 LEVELED (bit 4) clears itself when tilted off level while idle (Prompt AI part D, DECISIONS #46 amended)
3e9349f Live idle tilt: 10 Hz idle sampler, ms filter, 5 Hz notify, tilt_fresh (Prompt AI part C, DECISIONS #50)
0d45fff Bed Status flag bit 5 = AUTO_LEVEL_SETTLING (Prompt AI part B)
8e1242d Latch refinements and reset cause (Prompt AI part A, DECISIONS #49 amended)
cd2a148 Handoff 2026-11-03 01: Prompt AH (stop latch, bench record of 2026-11-02, live-level report)

 docs/REGRESSION.md | 119 ++++++++++++++++++++++++++++++++++++++++++++++-------
 1 file changed, 105 insertions(+), 14 deletions(-)
[exit 0]

### #39 $ date; git log --date=short --format='%h %ad %s' -6; ls docs/handoffs | grep 2026-11-03; git diff cd2a148 HEAD --stat; git diff cd2a148 HEAD -- '*.c' '*.h' | wc -l; git diff cd2a148 HEAD -- docs | wc -l
Tue Nov  3 08:59:57 PST 2026
05c4c2f 2026-11-03 REGRESSION items for Prompt AI (part E, docs only)
8996259 2026-11-03 LEVELED (bit 4) clears itself when tilted off level while idle (Prompt AI part D, DECISIONS #46 amended)
3e9349f 2026-11-03 Live idle tilt: 10 Hz idle sampler, ms filter, 5 Hz notify, tilt_fresh (Prompt AI part C, DECISIONS #50)
0d45fff 2026-11-03 Bed Status flag bit 5 = AUTO_LEVEL_SETTLING (Prompt AI part B)
8e1242d 2026-11-03 Latch refinements and reset cause (Prompt AI part A, DECISIONS #49 amended)
cd2a148 2026-11-03 Handoff 2026-11-03 01: Prompt AH (stop latch, bench record of 2026-11-02, live-level report)
fw_HANDOFF_2026-11-03_01_stop-latch-and-live-level-report.md
 app.c                                              |  72 +++++
 auto_level.c                                       | 304 ++++++++++++++++++++-
 auto_level.h                                       |  34 +++
 bed_actions.c                                      |  29 +-
 bed_actions.h                                      |  10 +
 bed_status.c                                       |  26 +-
 bed_status.h                                       |   9 +-
 ble_bed_service.c                                  |  93 +++++--
 ble_bed_service.h                                  |   7 +-
 docs/BLE_GATT_DESIGN.md                            | 140 ++++++++--
 docs/DECISIONS.md                                  | 256 +++++++++++++++++
 docs/PRE_SHIP.md                                   |  10 +-
 docs/REGRESSION.md                                 | 119 +++++++-
 .../PROMPT_AI_latch-refinements-live-tilt.md       | 144 ++++++++++
 14 files changed, 1165 insertions(+), 88 deletions(-)
916
847
[exit 0]

### #40 $ date; git log --oneline -7; git status; git diff cd2a148 HEAD -- '*.c' '*.h' > 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/diff_src.txt'; git diff cd2a148 HEAD -- docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/PRE_SHIP.md docs/REGRESSION.md > 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/diff_docs.txt'; wc -l 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/diff_src.txt' 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/diff_docs.txt'
Tue Nov  3 09:02:42 PST 2026
05c4c2f REGRESSION items for Prompt AI (part E, docs only)
8996259 LEVELED (bit 4) clears itself when tilted off level while idle (Prompt AI part D, DECISIONS #46 amended)
3e9349f Live idle tilt: 10 Hz idle sampler, ms filter, 5 Hz notify, tilt_fresh (Prompt AI part C, DECISIONS #50)
0d45fff Bed Status flag bit 5 = AUTO_LEVEL_SETTLING (Prompt AI part B)
8e1242d Latch refinements and reset cause (Prompt AI part A, DECISIONS #49 amended)
cd2a148 Handoff 2026-11-03 01: Prompt AH (stop latch, bench record of 2026-11-02, live-level report)
169b2f7 Bench record of 2026-11-02 and rulings (Prompt AH part B, docs only)
On branch main
Your branch is ahead of 'origin/main' by 5 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

no changes added to commit (use "git add" and/or "git commit -a")
  916 C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/diff_src.txt
  697 C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/diff_docs.txt
 1613 total
[exit 0]

### #41 $ grep -n 'static void latch_app_starts\|bool latch_stop = \|if (latch_stop)\|STOP_ALL (no app hold\|app_start_latch == APP_LATCH_COLLISION || any_collision\|^void ble_bed_service_tick\|^  service_app_commanding();\|^  process_pending_command();\|STATUS_NOTIFY_IDLE_LIVE_MS\|bool idle_live\|interval_ms != status_notify_interval_ms\|status.tilt_fresh\|flags |= 0x20\|static uint32_t status_notify_interval_ms' ble_bed_service.c; grep -n 'define TILT_IDLE_\|define LEVELED_EXIT\|leveled_exit_band = \|auto_level_chain_updated = true\|^static void tilt_idle_\|^bool tilt_idle_\|^bool tilt_telemetry_perpass_ran\|^bool accel_auto_level_chain_ran\|^void tilt_telemetry_tick\|tilt_idle_open_window(perpass_ran_last_pass)\|float alpha = \|Idle tilt sampler (Prompt AI' auto_level.c; grep -n 'print_reset_cause(\|sl_hal_emu_\|reset_cause_names\[\] = \|app_auto_level_settling_log_tick();\|tilt_idle_leveled_exit_due()\|deliberately has NO' app.c; grep -n '^bool app_auto_level_settling\|^void app_auto_level_settling_log_tick\|define BED_DEBUG_LEVELED_LOG' bed_actions.c; grep -n 'tilt_fresh   = \|tilt_idle_active())' bed_status.c
86:  if (in->auto_level_settling) flags |= 0x20; // AUTO_LEVEL_SETTLING (Prompt AI); byte 0 likewise
153:#define STATUS_NOTIFY_IDLE_LIVE_MS   200 // idle with live tilt: idle sampler or a burst (Prompt AI C3)
218:static uint32_t status_notify_interval_ms = 0; // the interval the notify timer was last started with
597:static void latch_app_starts(app_start_latch_t reason)
755:    bool latch_stop = app_commanding || (app_start_latch != APP_LATCH_NONE);
774:    if (latch_stop) {
779:    if (latch_stop) {
783:      printf("BLE: STOP_ALL (no app hold, no latch)\r\n");
873:    if (app_start_latch == APP_LATCH_COLLISION || any_collision) {
1168:void ble_bed_service_tick(void)
1174:  service_app_commanding();
1191:  process_pending_command();
1193:  service_app_commanding();
1223:  ble_bed_service_encode_status(&status, status.tilt_fresh, packed); // tilt_fresh: Prompt AI C4
1231:  bool idle_live = tilt_idle_active()
1234:                       : (idle_live ? STATUS_NOTIFY_IDLE_LIVE_MS : STATUS_NOTIFY_IDLE_MS);
1237:      || (interval_ms != status_notify_interval_ms)) {
64:#define TILT_IDLE_SAMPLE_MS 100        // 10 Hz sample period
65:#define TILT_IDLE_TAU_MS 500           // filter time constant; bench-tunable from TILT_IDLE_DIAG's lines
66:#define TILT_IDLE_CONVERGED_MS (3 * TILT_IDLE_TAU_MS) // ~95 % settled after the window opens
67:#define TILT_IDLE_FRESH_MS 300         // tilt_fresh: an idle value younger than this is fresh (3 periods)
73:#define TILT_IDLE_DIAG 1
74:#define TILT_IDLE_DIAG_PERIOD_MS 10000
81:#define LEVELED_EXIT_BAND 0.3       // degrees
82:#define LEVELED_EXIT_HOLD_MS 2000
111:static const int16_t leveled_exit_band = LEVELED_EXIT_BAND * SCALE_FACTOR; // 30
372:      auto_level_chain_updated = true;
475:bool tilt_telemetry_perpass_ran(void)
480:bool accel_auto_level_chain_ran(void)
486: *** Idle tilt sampler (Prompt AI part C, DECISIONS #50)
540:static void tilt_idle_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
552:static void tilt_idle_open_window(bool seed_from_perpass)
579:static void tilt_idle_close(void)
598:static void tilt_idle_leveled_exit_sample(uint32_t now)
619:static void tilt_idle_diag_sample(uint32_t now)
651:static void tilt_idle_sample(void)
665:    float alpha = 1.0f - expf(-dt_ms / (float)TILT_IDLE_TAU_MS);
683:bool tilt_idle_active(void)
698:bool tilt_idle_leveled_exit_due(void)
703:bool tilt_idle_fresh(void)
714:void tilt_telemetry_tick(bool status_subscribed, bool other_sampler_active)
771:    tilt_idle_open_window(perpass_ran_last_pass);
47:#include "sl_hal_emu.h"   // sl_hal_emu_get_reset_cause() / sl_hal_emu_clear_reset_cause()
393:} reset_cause_names[] = {
409:static void print_reset_cause(uint32_t cause)
450:  uint32_t reset_cause = sl_hal_emu_get_reset_cause();
451:  sl_hal_emu_clear_reset_cause();
453:  print_reset_cause(reset_cause);
617:  // The idle tilt sampler (Prompt AI C5, DECISIONS #50) deliberately has NO
791:  app_auto_level_settling_log_tick();
840:  if (tilt_idle_leveled_exit_due()) {
47:#define BED_DEBUG_LEVELED_LOG 1
77:bool app_auto_level_settling(void)
83:void app_auto_level_settling_log_tick(void)
126:    tilt_fresh   = accel_auto_level_chain_ran();
127:  } else if (tilt_idle_active()) {
130:    tilt_fresh   = tilt_idle_fresh();
134:    tilt_fresh   = tilt_telemetry_perpass_ran();
138:    tilt_fresh   = false;
[exit 0]

### #42 $ sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/57b09a21-5c56-46ec-a3cc-d7ffeb2e6493/scratchpad/cite.sh'
--- ble_bed_service.c:338-356
338: // App start latch, with the reason it was set. While set, every MOVE_START
356:   APP_LATCH_NONE = 0,
--- ble_bed_service.c:586-596
586:                               + sl_sleeptimer_ms_to_tick(MOVE_WATCHDOG_MS);
596: // (Prompt AI A1).
--- ble_bed_service.c:597-603
597: static void latch_app_starts(app_start_latch_t reason)
603: }
--- ble_bed_service.c:624-633
624: }
633:   }
--- ble_bed_service.c:552-555
552:   wd_diag_longest_between_ticks = 0;
555:   move_watchdog_expired = false;
--- ble_bed_service.c:746-786
746:   if (stop_all_pending) {
786:   }
--- ble_bed_service.c:869-886
869:   // must not release the latch. No bed_control call, no last_move_seq update,
886:   }
--- ble_bed_service.c:1168-1194
1168: void ble_bed_service_tick(void)
1194: 
--- ble_bed_service.c:1225-1239
1225:   // Cadence (BLE_GATT_DESIGN.md #6): 10 Hz moving; 5 Hz idle while the tilt
1239:   }
--- app.c:388-407
388: // one bit can be set at once (e.g. a power cut can show POR with a BOD).
407: };
--- app.c:409-428
409: static void print_reset_cause(uint32_t cause)
428: 
--- app.c:683-687
683: /*******************************************************************************
687:   if ((auto_level_active) && (!any_collision) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
--- app.c:617-619
617:   // The idle tilt sampler (Prompt AI C5, DECISIONS #50) deliberately has NO
619:   // main loop round, so the CPU may sleep between samples.
--- app.c:840-842
840:   if (tilt_idle_leveled_exit_due()) {
842:   }
--- bed_actions.c:77-81
77: bool app_auto_level_settling(void)
81: }
--- bed_actions.c:83-93
83: void app_auto_level_settling_log_tick(void)
93: }
--- bed_actions.c:208-235
208: void app_auto_level_start(void)
235: }
--- bed_status.c:113-139
113:   // 1 only while the chosen chain is live - auto-level's or the per-pass
139:   }
--- bed_status.c:126-138
126:     tilt_fresh   = accel_auto_level_chain_ran();
138:     tilt_fresh   = false;
--- auto_level.c:76-82
76: // LEVELED (Bed Status bit 4) clears itself (Prompt AI part D, DECISIONS #46
82: #define LEVELED_EXIT_HOLD_MS 2000
--- auto_level.c:270-275
270: // Read data and convert to usable values
275:   // from the last one - the sensor hasn't produced a new sample yet, so this
--- auto_level.c:540-545
540: static void tilt_idle_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
545: }
--- auto_level.c:552-577
552: static void tilt_idle_open_window(bool seed_from_perpass)
577: }
--- auto_level.c:579-593
579: static void tilt_idle_close(void)
593: }
--- auto_level.c:598-616
598: static void tilt_idle_leveled_exit_sample(uint32_t now)
616: #if TILT_IDLE_DIAG && !BED_CAPTURE_MOTOR_CURRENTS
--- auto_level.c:619-648
619: static void tilt_idle_diag_sample(uint32_t now)
648: 
--- auto_level.c:651-681
651: static void tilt_idle_sample(void)
681: }
--- auto_level.c:698-701
698: bool tilt_idle_leveled_exit_due(void)
701: }
--- auto_level.c:703-708
703: bool tilt_idle_fresh(void)
708: }
--- auto_level.c:714-777
714: void tilt_telemetry_tick(bool status_subscribed, bool other_sampler_active)
777: }
--- C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/peripheral/inc/sl_hal_emu.h:1167-1180
1167: 
1180: }
--- C:/Users/adunc/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p/platform_core/platform/peripheral/inc/sl_hal_emu.h:1182-1199
1182: /***************************************************************************//**
1199: }
[exit 0]

### #43 $ grep -n '^static void service_app_commanding\|^static void move_watchdog_arm\|^  sl_sleeptimer_restart_timer(&move_watchdog_timer\|^// Sets the app start latch\|^static void note_app_commanding' ble_bed_service.c; grep -n '      if (first_check){' app.c; grep -n 'static uint32_t last_accel_i2c_tick\|accel_get_xyz_raw(&accel_vector' auto_level.c; awk 'NR>=598 && NR<=616 && /^}/ {print NR": }"}' auto_level.c
518:static void move_watchdog_arm(void)
556:  sl_sleeptimer_restart_timer(&move_watchdog_timer,
582:static void note_app_commanding(void)
589:// Sets the app start latch (#48, #49) and restarts the silence timeout in the
629:static void service_app_commanding(void)
691:      if (first_check){ // Check level on both axes
279:  static uint32_t last_accel_i2c_tick = 0;
283:    accel_get_xyz_raw(&accel_vector[0]);
614: }
[exit 0]
````

## e. VERIFIED vs ASSERTED

Nothing in this session ran on a bed. VERIFIED means a command output
(section d) or a line of code or SDK read; it never means "observed on
hardware". `#n` = d.2, `P n` = d.0.

### e.1 VERIFIED, with evidence

| # | Claim | Evidence |
|---|---|---|
| V1 | Start state as the prompt requires: HEAD `cd2a148` = `origin/main` after `git fetch`; `.projectlinkstore` modified, the prompt untracked, nothing else | P1, P2 |
| V2 | Every build exit 0 with no warning/error line: baseline, after each part, each flag at 0, all three at 0, `BED_CAPTURE_MOTOR_CURRENTS` 1, restored | #12-#15, #20-#22, #25, #26, #31, #34, #36 |
| V3 | Sizes as in b.E's table; final 248,540 / 788 / 72,472, text +2,808 B vs `cd2a148` (245,732) | #12, #34, #36 |
| V4 | The Berkeley bss column is `.stack` + `.bss` + `.memory_manager_heap`; `.bss` 12,000 B, heap 16,760 B at the end | #27 |
| V5 | Host test `0 check(s) failed`; `bed_control.c` unchanged | #37 (incl. the empty `git diff --stat cd2a148 HEAD -- config autogen bed_control.c '*.btconf'`) |
| V6 | Flags at the end: `BED_BENCH_PROVOKE_RUNAWAY` 0 (`switch_input.c:98`), `BLE_BENCH_WIPE_BONDS` 0 (`ble_bed_service.c:70`), `BED_CAPTURE_MOTOR_CURRENTS` 0 (`debug_capture.h:21`), `BLE_DEBUG_WATCHDOG_LOG` 1, `BED_DEBUG_LEVELED_LOG` 1, `TILT_IDLE_DIAG` 1 | #37 |
| V7 | No `config/`, `autogen/`, GATT or Protocol Version change | #37; c.1 has no such file |
| V8 | A1: the refusal tests `app_start_latch == APP_LATCH_COLLISION \|\| any_collision` first (`ble_bed_service.c:873`); `latch_app_starts()`'s only guard is `if (app_start_latch != APP_LATCH_COLLISION)` (`:599`), so STOP->COLLISION is allowed and COLLISION->STOP is not | #18 (code at `8e1242d`), #41, c.1 |
| V9 | A2: `service_app_commanding()` is called at `:1174` (first statement of `ble_bed_service_tick()`, `:1168`) and still at `:1193`; `process_pending_command()` at `:1191` | #41, c.1 |
| V10 | A2 #36 facts: `source_commanding[APP]` is set only in `bed_move_start()` / `bed_auto_level_start()` (`bed_control.c:99`, `:127`); `bed_note_source_released()` clears the inhibit when neither source is commanding (`:44-54`); `move_watchdog_arm()` clears `move_watchdog_expired` and restarts the timer (`ble_bed_service.c:554-558`) | code read (`bed_control.c`); #43 |
| V11 | A3: `latch_stop = app_commanding \|\| (app_start_latch != APP_LATCH_NONE)` taken first in the STOP_ALL branch (`:755`); the latch and the `latch set` print only if `latch_stop` (`:774`, `:779`); else `BLE: STOP_ALL (no app hold, no latch)` (`:783`) | #41, c.1 |
| V12 | A4 API: `sl_hal_emu_get_reset_cause()` / `sl_hal_emu_clear_reset_cause()` in `platform/peripheral/inc/sl_hal_emu.h` (`:1196-1199`, `:1177-1180`), doc says the bits must be cleared or they accumulate (`:1173-1175`, `:1188-1190`); RSTCAUSE bits in `efr32bg22_emu.h:602-662` | #4, #5, #9, #42 |
| V13 | A4: none of the 178 compiled SDK `.c` files reads or clears RSTCAUSE (only the definition of `sl_se_get_reset_cause()` matches); `sl_hal_emu.c` is not compiled; `__INLINE` = `inline` for GCC (`cmsis_gcc.h:44`); `-Os` unconditional (`tilt_bed_controller.cmake:365`); the link succeeds and `nm` has no `sl_hal_emu_*` symbol | #7, #8, #9, #13, #14 |
| V14 | A4 code: read, clear, print in `app_init()` after the version line (`app.c:445-456`), table and printer under `!BED_CAPTURE_MOTOR_CURRENTS` (`:389-428`) | #41, c.1 |
| V15 | B: `app_auto_level_settling()` (`bed_actions.c:77-81`) = the guards of `app.c:687` plus `first_check` (`:691`); encoded `flags \|= 0x20` (`ble_bed_service.c:86`); print tick at `app.c:791` behind `BED_DEBUG_LEVELED_LOG` | #41, #43, c.1 |
| V16 | C: idle window gates, timer flag-only callback, one `read_accel()` per flag, filter with measured `dt`, seed rule, `tilt_fresh` per chain, 5 Hz rule, immediate publish on cadence change - as in the code at the cited lines | c.1; #41; #42 |
| V17 | C: the new statics are in `.bss`, `expf` is linked (120 B) | #26 (`nm -S`) |
| V18 | C5: after an interrupt taken in sleep, `sl_power_manager_sleep_on_isr_exit()` returns "sleep" only for the power manager's own wake or a hook asking for it; the app hook is the weak IGNORE | #10, #11 (`autogen/sl_power_manager_handler.c:46-48`, `:77-114`) |
| V19 | `accel_running_average()` is called only from `app.c`'s auto-level block and `settings_mode.c:322`, both before `tilt_telemetry_tick()` in the pass; `bed_status_get()` has one caller (`ble_bed_service.c`) | #10, #24, `app.c` read |
| V20 | D: band 30, hold 2000 ms, judged after 1500 ms, clear via `app_set_leveled(false)` at `app.c:840-842`, no setter of `true` added | #41, #42, P22, c.1 |
| V21 | `git status` at the end of the work is as in a. | #40 |
| V22 | Part C's `auto_level.c` commit is 255 added / 6 removed lines, stored LF (no line-ending churn) | #30 |

### e.2 ASSERTED (reasoned from code, compiles, not observed)

- **AS1.** Every restated AH A6 case (b.A.3) - Command Results, motors, the
  prints - and A1's 0x06 + upgrade on a bed.
- **AS2.** A2's behaviour list (b.A.2), including the narrow #36 case (5): the
  window, its consequence, and that it is about one pass wide.
- **AS3.** The reset-cause values for a power cut (`POR`, maybe with BOD bits)
  and a debugger reset (`SYSREQ` or `PIN`); that `app_init()` runs before the
  Bluetooth boot event.
- **AS4.** Bit 5's timing (~1.5 s) and its notify at both edges on a phone.
- **AS5.** The idle sampler runs at 10 Hz with the CPU sleeping between
  samples (in particular that `sli_bt_sleep_on_isr_exit()` does not return
  "sleep" for a sleeptimer wake - not read); the 5 Hz notify; ~101 samples per
  10 s line.
- **AS6.** Bus occupancy ~0.9-1.4 % (wire-time arithmetic plus #30's recorded
  1.4 ms maximum, not re-measured); the fault poll's transfer size (not read);
  EM0 duty +2-3 %; the diag print's UART time.
- **AS7.** The filter's lag (0.3° crossed ~0.18 s after a 1° step) and the
  LEVELED clear ~2.2 s after a hand tilt; nothing for a bump under ~1.8 s.
- **AS8.** The link stays up with the idle sampler running (risk 1 of #50).
- **AS9.** The idle -> per-pass step and the auto-level-start stale value
  (b.C.8).
- **AS10.** That `sl_sleeptimer_ms_to_tick(100)` truncates to 3276 ticks (so
  ~102 samples per line rather than 101) - not checked.
- **AS11.** App behaviour (fills, greying, unsubscribing when backgrounded):
  the app repository was not looked at.

---

## f. QUESTIONS FOR REVIEW

**f.1 A2 opens a narrow #36 hole - fix it?** (b.A.2 item 5) A late repeat of
an accepted hold, processed in the first pass after the 300 ms deadline before
the watchdog expiry is acted on, now finds the app released in bed_control and
continues the move with `source_commanding[APP]` false; a wall-switch press in
the rest of that hold then hands over at the switch release instead of staying
latched. **Proposed one-line fix, not made:** make the new top-of-tick call
`if (!move_watchdog_armed) service_app_commanding();`. While a hold is armed
the watchdog is already the 300 ms judge (it expires within microseconds of
the deadline and does the release and the stop itself), and no start latch can
be set while it is armed (AH b.A.5), so A2's purpose - latched releases judged
before the next command - is untouched. Alternatively accept the window. Which?

**f.2 Is a burst part of the idle window?** (b.C.1) I excluded bursts from
the idle *sampler* (the per-pass chain runs and is shipped during a burst, the
idle window opens when it ends - C2's "end of a move or burst" and the AH
Option B text), but included them in the 5 Hz *cadence* (they meet C1's four
conditions and their tilt is live). The literal C1 would run the idle sampler
during bursts too (two chains, a choice of which to ship). Confirm.

**f.3 The seed rule.** (b.C.2) "Seed from the most recent value of the
existing telemetry chain (or the first sample)": I seed from the per-pass
chain only if it ran in the pass just before the window opened; otherwise from
the first idle sample. Reason: after auto-level or settings mode the per-pass
chain has not run (possibly for hours), and seeding from it would show an old
angle and drift - a visible jump at the end of every auto-level run that
needed no motion. Accept?

**f.4 The A3 gloss.** (b.A.3) The prompt glosses `app_commanding` as "a
button held, or released less than 300 ms ago". In the code a release with a
stop write clears it at once, so a STOP shortly after a clean release does
**not** latch; only a release without a stop write leaves 300 ms. I followed
the variable. Intended?

**f.5 `tilt_fresh` during auto-level and settings.** (b.C.4) I read "the
existing per-pass chain is running" as "the chain being shipped updated this
pass", which makes bit 3 = 1 during auto-level (auto-level's chain) and during
the settings zero-level capture, and 0 in settings mode otherwise. Reading it
as the #33 telemetry chain only would grey out the angle for every auto-level
run. Confirm.

**f.6 The 10 s TILT lines start 1.5 s after the window opens** (after the
filter has converged), so the first comes ~12 s after the app opens. My
choice, so the numbers describe what part D's band sees. Keep, or count from
the window opening?

**f.7 The reset cause relies on GCC inlining.** (b.A.4) `sl_hal_emu.c` is not
in the build; at the project's unconditional `-Os` both calls inline (V13). If
the optimisation level is ever lowered the link fails loudly with an undefined
reference. The alternatives need the configurator (a component that adds
`sl_hal_emu.c`, or `emlib_rmu` for `RMU_ResetCauseGet()`). Accept as is?

**f.8 The idle -> per-pass step.** (b.C.8) When a move or burst starts after
the bed was tilted while idle, the shown angle steps back to the per-pass
chain's old value for ~0.2-0.3 s. Fixing it means seeding the per-pass chain
from the idle value when its window opens - a change to the moving chain's
initial state, which C2 says stays unchanged. Leave until F15, or fix now?

**f.9 The no-latch print has no `conn=`.** `BLE: STOP_ALL (no app hold, no
latch)` is the prompt's text exactly; the latch line has `(conn=N)`. Adding
it would tell a viewer's STOP from the control phone's on the bench. Add?

**f.10 Bit 5's two extra guards.** (b.B) `!any_collision && active_switch ==
SWITCH_AUTO_LEVEL_VALUE` beyond the prompt's `auto_level_active &&
first_check` - the exact Step 1 condition. Accept as "the precise
equivalent"?

**f.11 Docs beyond the parts' lists.** A changed BLE_GATT_DESIGN §4 and §7
and DECISIONS #49 (so they do not contradict the code), and B added DECISIONS
#46 amendment item 1 for bit 5 (B did not ask for a DECISIONS entry). Fine?

---

## g. NOT DONE

- **No bench run** - the user drives the bench (CLAUDE.md). REGRESSION 1.R,
  §5b (5b.3, 5b.3d, 5b.5, 5b.6, 5b.8), 6.11 and §6b are for the next bench
  session; the TILT lines' p2p values are needed to tune `TILT_IDLE_TAU_MS`
  and `LEVELED_EXIT_BAND`.
- **No host-test cases** for the new code: `ble_bed_service.c`,
  `auto_level.c` and `bed_actions.c` need SDK headers, the sleeptimer and the
  stack; the host test compiles `bed_control.c` only (unchanged). Not asked.
- **f.1's fix** - proposed, not made (report, don't improvise).
- **PRE_SHIP F16 not closed** - implemented; the bench check comes first.
- **The `.bss` section's baseline size** was not measured (only the Berkeley
  total, which cannot move); the growth is ~70 B of statics by `nm`.
- **Not measured:** idle loop rate, idle current, the sampler's wake rate, the
  UART time of the TILT line.
- **The app side** (bit 5 fill, `tilt_fresh` ungating, 5 Hz, unsubscribe when
  backgrounded, STOP ending holds / 0x07) - a separate app prompt.
- **Not reproduced in d:** file contents read to cite them (d.1 lists them),
  #2's 41,202-character Studio metadata line, and the prompt file's diff (it is
  the committed `docs/prompts/PROMPT_AI_latch-refinements-live-tilt.md`,
  written by the reviewer). The `docs/SESSION_LOG.md` diff is omitted under
  CLAUDE.md's standing exception (it ships with this file).
- **The commit of this file with the SESSION_LOG entry, and the `git push`**
  run after this file is written, so their output cannot appear here; they are
  reported to the user. A push failure would be reported in a follow-up commit
  to this file, without merge, rebase or reset (CLAUDE.md "Pushing").
- Nothing else that was asked was left out.
