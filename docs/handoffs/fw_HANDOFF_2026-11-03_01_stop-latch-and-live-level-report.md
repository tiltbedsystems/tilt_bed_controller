# fw_HANDOFF_2026-11-03_01 — Prompt AH: STOP waits for release (stop latch), bench record of 2026-11-02, live-level report

Firmware repo session of 2026-11-03 (system date checked with `date` at the
start, `Tue Nov  3 08:00:49 PST 2026`, and again before naming this file,
`Tue Nov  3 08:19:16 PST 2026` - d.2 #23). Prompt:
`docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md` (committed in
`48c5cf6`). The bench log the prompt calls `PuTTY Logs\RENAME_bench.txt` is
`_03-Software\PuTTY Logs\round3_bench_2026-11-02.log` (425 lines; the user
named it in the session request).

## a. Header

| | |
|---|---|
| Repo | `tilt_bed_controller` (firmware) |
| Branch | `main` |
| HEAD before | `b848e48` (Handoff 2026-11-02 03: Prompt AG), level with `origin/main` after `git fetch` - as the prompt expects (d.0 P2, d.2 #1) |
| HEAD after the work | `169b2f7` - two work commits: `48c5cf6` (Part A: `ble_bed_service.c`, three docs, the prompt file) and `169b2f7` (Part B: four docs) |
| After that | this handoff and the session's SESSION_LOG entry, committed together, then `git push` (normal push, no force) |
| Builds | from the command line (the prompt asks for it, A9): exit 0, no warnings, with `BLE_DEBUG_WATCHDOG_LOG` 1, 0, and 1 again (d.2 #5-#7); after both commits `ninja: no work to do` (#22) |
| Host test | `0 check(s) failed` (#8) |
| Bench | **nothing ran on a bed in this session** |

`git status` at the end of the work (after `169b2f7` and the SESSION_LOG
entry, before this file was written; d.2 #24):

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

`.projectlinkstore` is the Studio file the prompt says never to commit; it is
not committed. `docs/SESSION_LOG.md` is committed with this file.

**What was asked and what was done**

| Part | Asked | Done |
|---|---|---|
| A1 | One app start latch with a reason (COLLISION / STOP); COLLISION unchanged | `48c5cf6`: `app_start_latch` (`ble_bed_service.c:349-355`). b.A.1 |
| A2 | One helper that sets the latch AND calls `note_app_commanding()`; every caller listed | `latch_app_starts()` (`:589-595`); callers `:654`, `:757`, `:846`, `:849`. b.A.2 |
| A3 | Any STOP_ALL sets STOP via the helper after its stop actions; COLLISION stays; STOP_ALL never clears | `:738-762`. b.A.3 |
| A4 | STOP-latched starts refused with new 0x07 (own seq), nothing started, no watchdog, `note_app_commanding()`; COLLISION still 0x06; `|| any_collision` unchanged | `:843-858`, `#define CMD_RESULT_STOP_LATCHED 0x07` (`:179`). b.A.4; precedence question f.1 |
| A5 | Clears only through `note_app_stopped_commanding()`; list every caller; confirm each is a release | b.A.5 (five callers) |
| A6 | Six cases, from code, Command Results and motors | b.A.6 (tables); physical button: does **not** restart, switch_input.c unchanged |
| A7 | Prints behind `BLE_DEBUG_WATCHDOG_LOG` | `:758-762`, `:1170-1177`. b.A.7 |
| A8 | BLE_GATT_DESIGN §7 row 0x07 and 0x06 release list; DECISIONS new entry; REGRESSION items for every A6 case, 5.A4 / 5.A6 updated | `48c5cf6`: §7 (and one sentence on the §4 STOP_ALL row), DECISIONS #49, REGRESSION 5.A1-5.A6 + new §5b. b.A.8 |
| A9 | Builds at 1 and 0 (back to 1), raw tails, warnings, size change; host test | b.A.9; d.2 #4-#8, #22 |
| B1-B8 | Bench record and rulings | `169b2f7` (docs) and the SESSION_LOG entry (B7). b.B |
| C1-C5 | Report only | b.C. No code |

---

## b. Designs and analysis

Line references are to `48c5cf6` (= `169b2f7` for source files) unless
marked. `ble_bed_service.c` is the only source file changed; every other
file:line is unchanged from `b848e48`. Every claim is VERIFIED (code read, or
a command output in section d) or ASSERTED. Nothing ran on the bed.

Terms (all in `ble_bed_service.c`), as in the AG handoff:

- `app_start_latch` - the app start latch: `APP_LATCH_NONE`,
  `APP_LATCH_COLLISION` (#48), `APP_LATCH_STOP` (#49). Replaces the bool
  `app_collision_latched`.
- `app_commanding` + `app_command_deadline_tick` - "the app is still asking
  for motion". `note_app_commanding()` sets the flag and puts the deadline
  300 ms (`MOVE_WATCHDOG_MS`) ahead; `service_app_commanding()` runs every
  main-loop pass and, if the flag is set and the deadline has passed, calls
  `note_app_stopped_commanding("silence")`. "The silence timer".
- `note_app_stopped_commanding(reason)` - the app's release: clears
  `app_commanding`, releases the app's half of bed_control's #36 two-source
  latch, records the debug "cleared" print, clears the start latch.

### b.A Part A - the stop latch (DECISIONS #49)

#### b.A.0 The problem, from code (before `48c5cf6`)

The bench result (2026-11-02): holding HEAD up in the app and tapping STOP
stopped the bed only until the held button's next repeat. From code at
`b848e48`: the STOP_ALL branch of `process_pending_command()` cancelled the
watchdog, cleared `app_move_active` and `app_auto_level_active`, called
`note_app_stopped_commanding("stop_all")` (control phone; a viewer only when
no collision latch) and `app_stop_all()`. The app repeats MOVE_START every
100 ms while the finger is down; the next repeat found `app_move_active`
false and went through the ordinary start path (`bed_move_start()` +
`app_apply_move_outputs()`), so HEAD started again. An AUTO LEVEL hold
restarted from Step 1 the same way (`app_auto_level_active` false ->
`bed_auto_level_start()` -> `app_auto_level_start()` resets the run). The
#48 collision latch (the only thing that refused repeats) was cleared by a
control STOP_ALL, and a STOP with no collision never set anything.

The requirement: **after a STOP the bed stays stopped, and no app motion is
accepted until every app button has been released.** The firmware must not
rely on the app (the app side comes in a separate prompt).

#### b.A.1 A1 - one latch with a reason (`:337-355`)

```c
typedef enum {
  APP_LATCH_NONE = 0,
  APP_LATCH_COLLISION,
  APP_LATCH_STOP
} app_start_latch_t;
static app_start_latch_t app_start_latch = APP_LATCH_NONE;
```

The comment above it (`:337-348`) states both reasons, what answers each
(0x06 / 0x07), the release list, that STOP_ALL never clears it, and that it is
set only through `latch_app_starts()`. Every former use of
`app_collision_latched` was rewritten; `grep` finds none left (d.2 #2).
COLLISION behaves exactly as #48 + AG: set at the trip
(`service_move_watchdog()`), set by an `any_collision` refusal, answered 0x06,
released by the same paths - except that STOP_ALL no longer releases it
(A3), which is the change the prompt asks for.

Also renamed (debug only, inside `#if BLE_DEBUG_WATCHDOG_LOG`):
`wd_diag_collision_refused` -> `wd_diag_latch_refused` (it now counts 0x06
and 0x07 refusals), and a new `wd_diag_latch_clear_set_reason` holds the
reason the latch was set, for the cleared print (`:285-293`).

#### b.A.2 A2 - the helper (`:582-595`), and every caller

```c
// Sets the app start latch (#48, #49) and restarts the silence timeout in the
// same call. The latch is set ONLY here: ...
static void latch_app_starts(app_start_latch_t reason)
{
  if (app_start_latch != APP_LATCH_COLLISION) {
    app_start_latch = reason;
  }
  note_app_commanding();
}
```

- It never downgrades COLLISION to STOP (A3: "If the latch is already set
  with reason COLLISION, it stays COLLISION"). It can upgrade STOP to
  COLLISION, but no caller asks for that while STOP is set (see A4: a
  STOP-latched refusal passes `APP_LATCH_STOP`). So in practice COLLISION is
  set only from NONE.
- It always calls `note_app_commanding()`, so the AG b.3 invariant holds by
  construction: **the latch is assigned a non-NONE value only here, and
  `app_commanding` is cleared only in `note_app_stopped_commanding()`, which
  also sets the latch to NONE - so whenever the latch is set, the silence
  timer is running.**

Every place the latch is assigned (d.2 #2, #9):

| # | Caller | Line | Reason passed | When |
|---|---|---|---|---|
| 1 | `service_move_watchdog()`, trip branch | `:654` | COLLISION | `any_collision` while the watchdog is armed (an app MOVE / AUTO LEVEL hold) |
| 2 | `process_pending_command()`, STOP_ALL branch | `:757` | STOP | every STOP_ALL, either role (A3) |
| 3 | `process_pending_command()`, start refusal, STOP-latched | `:846` | STOP | a MOVE_START / AUTO_LEVEL_START while `app_start_latch == APP_LATCH_STOP` (re-asserts, restarts the silence timer) |
| 4 | `process_pending_command()`, start refusal, otherwise | `:849` | COLLISION | a MOVE_START / AUTO_LEVEL_START while COLLISION-latched or `any_collision` |
| - | `note_app_stopped_commanding()` | `:614` | (sets NONE) | the release - b.A.5 |

#### b.A.3 A3 - STOP_ALL (`:738-762`)

Before (`b848e48`):

```c
move_watchdog_cancel();
app_move_active = false;
app_auto_level_active = false;
if (conn == control_connection || !app_collision_latched) {
  note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
} else {
  bed_note_source_released(BED_SOURCE_APP); // viewer during a collision latch (AG)
}
app_stop_all();
send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
```

After:

```c
move_watchdog_cancel();
app_move_active = false;
app_auto_level_active = false;
bed_note_source_released(BED_SOURCE_APP); // never inhibited: releases the app's half of the #36 latch
app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
latch_app_starts(APP_LATCH_STOP);
#if BLE_DEBUG_WATCHDOG_LOG
printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
       (app_start_latch == APP_LATCH_COLLISION) ? ", kept as collision" : "");
#endif
```

What changes and what does not:

| Effect | Before (control, or any role with no collision latch) | After (both roles, always) |
|---|---|---|
| Accepted from any role, 0x00 to the sender | yes | yes (unchanged) |
| Watchdog cancelled, both app flags cleared | yes | yes (unchanged) |
| App's half of the #36 latch released | yes (inside `note_app_stopped_commanding()`) | yes (`bed_note_source_released(BED_SOURCE_APP)` called directly, as the AG viewer branch already did) |
| `app_stop_all()` | yes | yes (unchanged) |
| Start latch | **cleared** | **set to STOP** (COLLISION kept) |
| `app_commanding` / silence timer | cleared (timer stopped) | set; deadline 300 ms after the STOP_ALL |
| Debug refused count | reset | kept (it is 0 when the latch was NONE: it is reset at every clear and only counts refusals, which only happen while latched) |

The silence release later calls `bed_note_source_released(BED_SOURCE_APP)` a
second time; that is a no-op on bed_control, by the AG b.1.1 argument, which
still holds: the app's `source_commanding[]` can only be set true inside
`bed_move_start()` / `bed_auto_level_start()`, which no start reaches while
latched; and `conflict_inhibit` true implies some source is commanding
(`bed_control.c:31-48`), so with the app already released it is the switches,
and a second app release cannot clear it. ASSERTED from code (same as AG).

The watchdog expiry and the disconnect stop also call `app_stop_all()`, but
they are not STOP_ALL and do not set the stop latch (only the STOP_ALL opcode
does - "Any STOP_ALL").

#### b.A.4 A4 - the refusal (`:823-858`)

```c
if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
    && (app_start_latch != APP_LATCH_NONE || any_collision)) {
  if (app_start_latch == APP_LATCH_STOP) {
    latch_app_starts(APP_LATCH_STOP);
    send_command_result(conn, seq, CMD_RESULT_STOP_LATCHED);   // 0x07
  } else {
    latch_app_starts(APP_LATCH_COLLISION);
    send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP); // 0x06
  }
#if BLE_DEBUG_WATCHDOG_LOG
  if (wd_diag_latch_refused < 0xFFFF) {
    wd_diag_latch_refused++;
  }
#endif
  return;
}
```

- Position unchanged: after the viewer check (`:814`) and the settings-mode
  check (`:819`), before the #36 check (`:870`). So a viewer still gets 0x02,
  settings mode 0x03, and the latch is checked before "wall switch" (0x01).
- Own seq, nothing started (`return` before the `switch`), no
  `last_move_seq` update, no `move_watchdog_arm()`, and `note_app_commanding()`
  through the helper - exactly as the 0x06 refusal.
- **0x06 cases unchanged:** with the latch NONE or COLLISION, the condition
  and the result are what AG had (`app_collision_latched || any_collision` ->
  0x06 and latch COLLISION).
- **Precedence (a judgement call, f.1):** with the latch STOP and a collision
  sequence running (`any_collision`), the refusal answers **0x07** and keeps
  STOP. This follows A4 as written ("While latched with reason STOP, every
  app MOVE_START / AUTO_LEVEL_START ... is refused with ... 0x07"). The
  alternative (collision first: 0x06 and upgrade to COLLISION) is a swap of
  the two branches. Either way the start is refused, the latch stays set, and
  the same releases clear it; only the result code and the print's reason
  differ. The overlap needs a STOP_ALL followed by a collision with no app
  release between - in practice a physical-controller collision during the
  STOP latch, or a STOP_ALL during a collision sequence after the app's own
  release.

#### b.A.5 A5 - every caller of `note_app_stopped_commanding()`, after the change

From `grep -n` (d.2 #9). STOP_ALL is no longer a caller (six in AG, five now).

| # | Caller | Line | Reason printed | Runs when | Is it a release of the app's buttons? |
|---|---|---|---|---|---|
| 1 | `service_app_commanding()` | `:628` | `silence` | `app_commanding` and 300 ms since the last `note_app_commanding()`: the app's last MOVE_START / AUTO_LEVEL_START (accepted or refused), the trip, or a STOP_ALL | **Yes.** An app whose button is down repeats every 100 ms, and every repeat (accepted or refused) pushes the deadline 300 ms on, so 300 ms of silence means no button is held - or the link stalled for 300 ms, which the #36 design already treats as a release (it is also the dead-man period). Always armed while latched (b.A.2 invariant). See f.2 for when, within a pass, it runs |
| 2 | Watchdog expiry, `service_move_watchdog()` | `:695` | `watchdog` | Watchdog armed and expired | Cannot run while latched: every latch-setting path cancels the watchdog (trip `:651`, STOP_ALL `:743`) or runs with it already disarmed, and every start is refused before `move_watchdog_arm()` (`:914`, `:954`). So it never clears a latch in practice. Outside a latch it is the dead-man stop: no repeat for 300 ms = released or gone |
| 3 | MOVE_STOP | `:925` | `stop` | Control connection only (a viewer is answered 0x02 at `:814` first), arguments 0/0 | **Yes** - the app sends it when the finger lifts (BLE_GATT_DESIGN §4) |
| 4 | AUTO_LEVEL_STOP | `:964` | `stop` | Control connection only, arguments 0/0 | **Yes** - same, for AUTO LEVEL |
| 5 | Control-phone disconnect stop, `ble_bed_service_tick()` | `:1149` | `disconnect` | `disconnect_stop_pending`, set only when the closed link was the control connection **and** `bed_active_source() == BED_SOURCE_APP` (`:1047`) | **Yes** - the phone is gone, nothing can be held. **But it never runs during a STOP latch**: STOP_ALL's `app_stop_all()` sets `active_switch = OFF` (`bed_actions.c:266`), so the bed is idle and `bed_control_tick()` returns the source to NONE before any later drop; the latch is then cleared by row 1 within 300 ms of the app's last write. Same as AG f.1 (accepted by the reviewer, B7). For a COLLISION latch it still runs during the collision sequence (unchanged) |

Nothing else assigns `APP_LATCH_NONE` (d.2 #2: `:614` is the only
assignment of NONE besides the initialiser `:354`).

#### b.A.6 A6 - what happens in each case (ASSERTED, from code)

Notation: `S` = seq of the STOP_ALL; `N` = seq of the last accepted repeat of
the hold; "repeat" = the app's MOVE_START / AUTO_LEVEL_START every ~100 ms
while the finger is down; "pass" = one `app_process_action()`. Within a pass
`ble_bed_service_tick()` runs last (`app.c:773`), and in it: disconnect stop,
then `process_pending_command()` (STOP_ALL first, from its own flag `:738`,
then the shared slot), then `service_move_watchdog()`, then
`service_app_commanding()` (`:1140-1159`); the prints come after
(`:1161-1177`). Results are from code; nothing ran.

**A6.1 App holding HEAD up (an app that keeps repeating), STOP_ALL from the
same phone, finger kept down 2 s, released, a fresh press.**

| t | App sends | Command Result | Motors / state | Console (`BLE_DEBUG_WATCHDOG_LOG` 1) |
|---|---|---|---|---|
| 0 | STOP_ALL `S` | `{S, 0x00}` | `app_stop_all()` (`bed_actions.c:239-266`): `app_move_stop()` (active_switch was HEAD UP) and `set_all_motors_off()` - HEAD pair off; all LEDs off, amber back if the lights are on; inferred limits reset; `active_switch = OFF`. Watchdog cancelled. Latch STOP; silence deadline t+300 ms | `BLE: STOP_ALL latch set (conn=N)`, then `BLE: hold end op=0x01 seq=N: ...` |
| 0.1-2.0 s | repeats | `{seq, 0x07}` each (about 20) | **stay off.** Each refusal pushes the deadline on. No watchdog, so no #45 veto (nothing moves) | - |
| ~2.0 s | MOVE_STOP `M` (finger up) | `{M, 0x00}` | Latch cleared (`stop`). `bed_move_stop(APP)` on an idle bed: APP takes the lock for the call, `app_move_stop()` (motors off again); `app_apply_stop_outputs()` (LEDs off, amber back, `active_switch = OFF`). Stay off | `BLE: start latch (stop) cleared (stop), 20 repeats refused` (K = the number of refusals) |
| later | fresh MOVE_START `P` | `{P, 0x00}` | HEAD up starts normally; watchdog armed; LEVELED -> 0 if it was set | (the hold's own `hold end` line at its release) |

- If the MOVE_STOP is lost: cleared by `silence` at the first pass ≥300 ms
  after the last repeat; nothing moves in between.
- If a repeat is pending in the same pass as the STOP_ALL: STOP_ALL is
  processed first, then the repeat is refused `{seq, 0x07}`.
- Before #49 (the 2026-11-02 bench): the first repeat after the STOP_ALL got
  `{seq, 0x00}` and HEAD started again.

**A6.2 The same with an app that sends MOVE_STOP right after its STOP_ALL
(and ends the hold - the planned app).**

| t | App sends | Command Result | Motors / state | Console |
|---|---|---|---|---|
| 0 | STOP_ALL `S` | `{S, 0x00}` | as A6.1; latch STOP | `STOP_ALL latch set (conn=N)`, `hold end ...` |
| next write | MOVE_STOP `M` | `{M, 0x00}` | latch cleared (`stop`); stay off | `BLE: start latch (stop) cleared (stop), 0 repeats refused` |
| finger still down, then up | nothing (the hold has ended); or a second MOVE_STOP at the lift -> `{M2, 0x00}` | - | stay off | no latch line (nothing latched) |
| later | fresh MOVE_START `P` | `{P, 0x00}` | normal start | - |

- Same pass: STOP_ALL is processed first (own flag), then MOVE_STOP from the
  slot - the same result.
- If the app writes MOVE_STOP **before** the STOP_ALL and they land in
  different passes: MOVE_STOP `{M, 0x00}` with nothing latched; the STOP_ALL
  then sets STOP and nothing but silence clears it: `cleared (silence), 0
  repeats refused` ~300 ms later. A press inside those 300 ms gets 0x07 (as
  A6.3 (a)).
- **Not the planned app, but possible:** an app that sends MOVE_STOP after the
  STOP_ALL and then **keeps repeating** MOVE_START while the finger stays down:
  the MOVE_STOP clears the latch, the next repeat is a normal start, and HEAD
  moves again. The firmware cannot tell such a repeat from a new press. f.4.

**A6.3 STOP_ALL tapped with nothing held, then a press within 300 ms, and a
press after 300 ms.**

| t | App sends | Command Result | Motors / state | Console |
|---|---|---|---|---|
| 0 | STOP_ALL `S` | `{S, 0x00}` | nothing was moving; `app_stop_all()` turns all motors and LEDs off (amber back), resets limits; watchdog was not armed (no `hold end` line). Latch STOP, deadline 300 ms | `BLE: STOP_ALL latch set (conn=N)` |
| (a) < 300 ms | MOVE_START `P` | `{P, 0x07}` | **nothing moves**; deadline P+300 ms | - |
| (a) held | repeats | `{seq, 0x07}` each | nothing moves for as long as the finger is down | - |
| (a) release | MOVE_STOP `M` | `{M, 0x00}` | latch cleared (`stop`) | `start latch (stop) cleared (stop), K repeats refused` |
| (a) next press | MOVE_START | `{seq, 0x00}` | normal start | - |
| (b) first pass at or after 300 ms | - | - | silence: latch cleared | `BLE: start latch (stop) cleared (silence), 0 repeats refused` |
| (b) > 300 ms | MOVE_START `P` | `{P, 0x00}` | normal start | - |

- So a press made within 300 ms of a STOP tap must be released and pressed
  again (f.3).
- **(b) depends on a pass running between the deadline and the press.** In
  the press's own pass the command is processed (`:1157`) before the silence
  check (`:1159`). If no pass ran after the deadline, the press is refused
  0x07 and latches for its whole hold. With a phone subscribed, a pass runs at
  least at each 1 Hz status notify (`STATUS_NOTIFY_IDLE_MS`, `:151`; the
  sleeptimer callback wakes the CPU and the power manager returns to the
  super loop unless a hook asks to sleep, `autogen/sl_power_manager_handler.c`
  `sl_power_manager_sleep_on_isr_exit()`), plus BLE events; how often an idle
  connected bed runs a pass has not been measured. So a press 300-1000 ms
  after the STOP may be refused. ASSERTED; f.2 with a one-line fix.

**A6.4 App AUTO LEVEL hold, then STOP_ALL.**

| t | App sends | Command Result | Motors / state | Console |
|---|---|---|---|---|
| 0 | STOP_ALL `S` | `{S, 0x00}` | `app_auto_level_active` cleared; `app_stop_all()`: `auto_level_active` -> `app_auto_level_stop()` (`bed_actions.c:212-225`: run ended, red blink stopped, level data reset), all motors off, LEDs off, amber back, limits reset, `active_switch = OFF`. Latch STOP. **LEVELED unchanged** (STOP_ALL does not touch bit 4, #46) | `STOP_ALL latch set (conn=N)`, `BLE: hold end op=0x03 ...` |
| held | AUTO_LEVEL_START repeats | `{seq, 0x07}` each | **auto-level does not restart** (no `Step 1`). Before #49 the first repeat called `bed_auto_level_start()` -> a new run from Step 1 | - |
| release | AUTO_LEVEL_STOP `M` | `{M, 0x00}` | latch cleared (`stop`); `bed_auto_level_stop(APP)` -> `app_auto_level_stop()` again (idempotent), `app_apply_stop_outputs()` | `start latch (stop) cleared (stop), K repeats refused` |
| later | fresh AUTO_LEVEL_START | `{seq, 0x00}` | new run, `Step 1` | - |

**A6.5 A viewer's STOP_ALL while the control phone holds a move.**

| t | Sender | Command Result | Motors / state | Console |
|---|---|---|---|---|
| 0 | viewer: STOP_ALL `S` | `{S, 0x00}` **to the viewer** | the bed stops (`app_stop_all()`); latch STOP; deadline 300 ms | `BLE: STOP_ALL latch set (conn=V)`, `hold end` for the control hold |
| held | control: repeats | `{seq, 0x07}` each, to the control phone | **nothing restarts** | - |
| release | control: MOVE_STOP `M` | `{M, 0x00}` | latch cleared (`stop`) | `start latch (stop) cleared (stop), K repeats refused` |
| later | control: fresh press | `{seq, 0x00}` | normal start | - |

- The viewer cannot clear the latch: its MOVE_START / MOVE_STOP get 0x02 at
  `:814` before anything else. Its further STOP_ALLs re-set STOP and push the
  deadline on.
- Control goes quiet without a stop write (finger up, write lost): `silence`
  300 ms after its last repeat.
- Control phone drops: the bed is idle after the STOP_ALL, so no disconnect
  stop (`:1047`); `silence` within 300 ms of its last write (b.A.5 row 5).
- Before #49 (no collision latch): the viewer's STOP_ALL called
  `note_app_stopped_commanding()`, and the control app's next repeat restarted
  the move.

**A6.6 STOP_ALL while a physical button is held - does the physical move
restart on its own?** **No, from code.** switch_input.c is not changed.

Setup: the switch panel drives (e.g. HEAD UP held: `switch_held` and
`switch_driving` = HEAD UP, `active_switch` = HEAD UP, source SWITCHES); the
app taps STOP.

- STOP_ALL calls `app_stop_all()` directly - not through bed_control, so no
  arbitration: `app_move_stop()` (`active_switch != OFF`), motors off, LEDs
  off (amber back), limits reset, `active_switch = OFF`. 0x00 to the phone;
  app latch STOP (it affects app starts only). The switches' half of the #36
  latch stays set until the physical release.
- `switch_held` and `switch_driving` still name the held button
  (`switch_input.c:156-157`, file-static; `app_stop_all()` does not touch
  them).
- Why nothing restarts while the button stays down:
  1. The sampler queues a press only when the debounced state of a bit
     changes (`switch_input.c:400-421`: `pending_press |= bit` at `:419`, inside the
     "raw differs from debounced for `SWITCH_DEBOUNCE_SAMPLES`" branch). A
     button held steady produces no new press, and no `SW:` line.
  2. `process_outputs()` energises a motor only in the press arm of a
     delivered edge with a matching `switch_driving` (`:519-588`). No edge,
     no energise.
  3. `process_inputs()`'s press arms need `switch_held == OFF` (`:627`,
     `:642`, `:654`, `:673`, `:697`), which it is not.
  4. Supervision and the double-press check need `active_switch != OFF`
     (`:934`) - now OFF.
  5. `motion_failsafe_tick()` stands down while any switch is down
     (`:831-838`), and no motor is on anyway.
  6. For a held physical AUTO LEVEL: the `app.c` auto-level block needs
     `auto_level_active` (`app.c:626`), cleared by `app_auto_level_stop()`;
     only `app_auto_level_start()` (`bed_actions.c:189`) sets it, reached only
     from `bed_auto_level_start()`, i.e. a new press.
- At the release: `process_inputs()`' release arm (`switch_held` matches) ->
  `switch_held = OFF`, `bed_move_stop(SWITCHES)` -> on the idle bed SWITCHES
  takes the lock for the call -> `app_move_stop()` (motors off); then
  `process_outputs()`' release arm (`switch_driving` matches) -> motors off,
  LED off, `switch_driving = OFF`, `active_switch = OFF`. Nothing moves. The
  next physical press is a normal start.
- Related, pre-existing, not a restart: once the app latch clears (300 ms
  after the STOP_ALL if the app is idle), an app press made **while the
  physical button is still held** is accepted (bed idle, `conflict_inhibit`
  false: a conflict is latched only by a start against the other source's
  lock) and moves the bed; the physical release then force-stops it
  (`bed_move_stop(SWITCHES)` -> `arbitrate()` FORCE_STOP ->
  `app_stop_movement()`, #12). Unchanged by this session.

**Other paths, for completeness (ASSERTED, from code):**

- A STOP_ALL during a **collision** latch (5.A4 / 5.A6): latch stays
  COLLISION (`, kept as collision` in the print), the held repeats keep
  getting 0x06, **nothing restarts during or after the sequence** - the
  remaining restart path of the AG handoff b.7.4 / f.2 is closed. The
  STOP_ALL's `app_stop_all()` still cuts the collision reverse if it lands in
  it (PRE_SHIP F26).
- Settings mode: an app start gets 0x03 (`:819`) before the latch check, so
  0x07 is never sent in settings mode; the latch still clears by the usual
  releases.
- Sleep (#45): while latched the watchdog is never armed, so
  `ble_bed_service_app_hold_active()` is false and the app hold does not veto
  sleep - nothing the app commands moves. Same as the COLLISION latch (#48).

#### b.A.7 A7 - bench prints (`BLE_DEBUG_WATCHDOG_LOG` 1)

- `BLE: STOP_ALL latch set (conn=N)` - at every STOP_ALL, printed inside the
  STOP_ALL branch after `app_stop_all()`, the 0x00 result and the latch
  (`:758-762`), main-loop context. When the latch was already COLLISION the
  line ends `, kept as collision` - an addition beyond the prompt's text,
  so the bench can tell the two cases apart (f.5).
- `BLE: start latch (<set reason>) cleared (<reason>), K repeats refused` -
  replaces `BLE: collision latch cleared (<reason>), K repeats refused`.
  `<set reason>` is `stop` or `collision` (recorded in
  `note_app_stopped_commanding()`, `:604-610`); `<reason>` is `stop`,
  `disconnect`, `silence` (`stop_all` can no longer occur; `watchdog` cannot
  occur while latched). Printed from `ble_bed_service_tick()` after every stop
  of the pass (`:1170-1177`), as before.
- Order in a STOP_ALL pass: `STOP_ALL latch set` (inside
  `process_pending_command()`) then `hold end ...` (end of the tick).

#### b.A.8 A8 - docs (complete diff in c.2)

- **BLE_GATT_DESIGN §7:** row `0x07 | STOP_LATCHED | "Stopped - release and
  press again"`; a STOP_LATCHED section (meaning, when sent, release list,
  the two user-visible consequences, precedence with 0x06, app behaviour);
  the 0x06 release list now says STOP_ALL does not clear it from either
  connection; the protocol paragraph covers both codes. **§4:** one sentence
  added to the STOP_ALL opcode row ("Then latches app motion starts until the
  app releases (DECISIONS #49; §7, result 0x07)") - not asked for in A8, added
  so the opcode table does not contradict §7. Additive within v1; Protocol
  Version unchanged.
- **DECISIONS #49:** the bench result, the decision (the five bullets of
  b.A.1-A.7 in short), why STOP_ALL no longer clears the latch, that AG f.2
  is withdrawn, status. #48's text is not edited (its "Bench result" was added
  in Part B).
- **REGRESSION:** the #48 block's intro notes the print change; 5.A1, 5.A2,
  5.A3, 5.A5 print text updated; **5.A4** (control STOP_ALL during the latch:
  `kept as collision`, no `latch cleared` line, nothing moves during or after
  the sequence) and **5.A6** (viewer: same) rewritten; **new §5b**, items
  5b.1-5b.6 = A6.1-A6.6, 5b.7 BLE link stays up.

#### b.A.9 A9 - builds, size, host test

Built with the session wrapper (d.3); host test with MinGW gcc (d.2 #8).

| Build | Result | Evidence |
|---|---|---|
| Existing ELF before any build | text 245620, data 784, bss 72476 - the AG record | d.2 #4 |
| After the edit, `BLE_DEBUG_WATCHDOG_LOG` 1 | 1 object + link, exit 0, **no warning/error lines**; text **245732**, data **788**, bss **72472** | #5 |
| `BLE_DEBUG_WATCHDOG_LOG` 0 (temporary, `sed` on `ble_bed_service.h`) | 3 objects + link, exit 0, no warnings; text 243956, data 780, bss 72480 | #6 |
| Back to 1 | 3 objects + link, exit 0, no warnings; 245732 / 788 / 72472 (= the first); `git status --short` lists no `.h`; the header line reads `1` | #7 |
| After both commits | `ninja: no work to do.`, exit 0 | #22 |
| Host test `test/bed_control_test.c` | `0 check(s) failed` | #8 |

Size change against AG, `BLE_DEBUG_WATCHDOG_LOG` 1: **text +112 B, data
+4 B, bss -4 B** (flash +116 B; RAM net 0). The data +4 is the new
`wd_diag_latch_clear_set_reason` pointer initialised to `""` (a non-zero
initialiser lands in .data); the bss -4 is ASSERTED to be layout (the bool
`app_collision_latched` became a 4-byte enum, and debug variables moved).
At 0: text +32 B against AG's 243924 (the enum compare and the 0x07 branch).
The wrapper prints the last 25 lines and every line matching
warning/error of the full output; each build here printed fewer than 25
lines, so its tail is its whole output.

---

### b.B Part B - bench record of 2026-11-02 and rulings (docs, `169b2f7`)

Source for all of B: Andrew's bench run on `b848e48`, Galaxy S23 on app
`4e66328`, log `_03-Software\PuTTY Logs\round3_bench_2026-11-02.log`, 425
lines, read in full this session (the file itself is not reproduced; the
lines cited are quoted). Firmware facts used to read it are cited by
file:line.

Two observations about the log itself, not asked for:

- **The firmware restarted at line 287** (`Welcome to the Tilt Bed
  application`, `Application Version 1.1.0`, `Accelerometer is sampling ...`,
  NVM3 and settings loads, `BLE SM: configure`) with no reset cause printed.
  Lines 1-4 also show the banner twice at the very start. Whether 287 was a
  reflash, a power cycle or an unexpected reset is not in the log. f.6.
- The firmware of that run printed nothing at a STOP_ALL, so the STOP taps of
  the bench result are visible only as short holds followed at once by a new
  hold: lines 143-145 (`seq=89: 7 refreshes`, `seq=95: 4 refreshes`,
  `seq=101: 4 refreshes`), 273-276, 328-330, 380-381. Matching these to STOP
  taps rests on Andrew's account (recorded so in DECISIONS #49).

**B1. Roll sign.** BLE_GATT_DESIGN §6: the `** PROVISIONAL **` marker on
offset 4 is removed, and the "Roll — PROVISIONAL" paragraph is replaced by
"Roll — SPECIFICATION. `POSITIVE = LEFT raised`. Bench-confirmed 2026-11-02"
with the record: the app, decoding the wire roll as-is
(`kRollWireSign = +1`), showed its bubble and words on the side actually
raised. The four-direction table is kept as the check to repeat after any
change to the mounting or the tilt path. **Evidence is the user's report**: the
UART log has no tilt values (no line in it prints roll or pitch). No DECISIONS
entry calls roll unconfirmed (d.2 #12: the only roll-sign matches outside
BLE_GATT_DESIGN are in SESSION_LOG, which is append-only and not edited);
BLE_GATT_DESIGN §6 was the only place. VERIFIED (grep) for "no other place";
the sign itself is the user's observation.

**B2. LEVELED / "Bed is level" (REGRESSION 6.1 / 6.8, DECISIONS #46).**
VERIFIED from the log. **Six** successful app auto-levels, not four as the
prompt says (d.2 #14, #17: six `Bed is level` lines, six `-> 1` lines):

| Lines | Outcome | `-> 1` | then | `hold end op=0x03` | `-> 0` at the next motion |
|---|---|---|---|---|---|
| 156-172 | ROLL then PITCH | 169 | `Exit auto-level` 170, `Bed is level` 171 | 172 (`seq=200: 82 refreshes`) | 173 (next app move, hold end 174) |
| 186-200 | PITCH only | 196 | `Exit auto-level` 197, `ROLL is disabled and Pitch is level` 198, `Bed is level` 199 | 200 | 201 (next app AUTO LEVEL press: `Step 1` 202, released after 8 refreshes, 203) |
| 209-223 | ROLL only | 219 | `Exit auto-level` 220, `PITCH is disabled and ROLL is level` 221, `Bed is level` 222 | 223 | 224 (next move) |
| 230-252 | ROLL then PITCH | 245 | 246, 247 | 252 | 253 (next move, 254) |
| 343-364 | ROLL then PITCH | 361 | 362, 363 | 364 | 365 (next move) |
| 397-417 | ROLL then PITCH | 410 | 411, 412 | 417 | - (no motion follows in the log) |

All six are app auto-levels (each ends with an `op=0x03` hold-end line; no
`SW: 0x0002` in the log - physical AUTO LEVEL is `SWITCH_AUTO_LEVEL 0x0002`,
`app.h:116`). Twelve `LEVELED` lines in all, all in these pairs (#17). No
disconnect during a run: the six `connection_closed` lines (91, 255, 262, 336,
418, 425) all read `reason=0x1013` and fall between runs (#14). Recorded in
DECISIONS #46 "Bench result (2026-11-02)" and REGRESSION §6 "Recorded result":
6.1 (prints), 6.8 and 6.10b pass for the app path; not covered: the physical
button (6.1 and 6.10a as written), 6.10d-6.10f.

**B3. #48 / 5.A1 with an app that ends its hold at the first 0x06.**
VERIFIED from the log, lines 313-323 (quoted in DECISIONS #48 "Bench result"):

```
313  Tripped on steady!
314  Securit margin: 550
315  Motor 0 Delta Current:  0
316  Motor 1 Delta Current:  2
317  Motor 2 Delta Current:  549
318  Motor 3 Delta Current:  -24
319  BLE: COLLISION_STOP seq=94 op=0x01 (latched)
320  BLE: hold end op=0x01 seq=94: 21 refreshes, longest refresh gap 121 ms, longest between passes 0 ms
321  Once
322  BLE: collision latch cleared (stop), 1 repeats refused
323  FAILSAFE: motors energised with nothing commanding - stopped
```

Reading: the summed-rise element tripped (positive deltas 0 + 2 + 549 = 551 >
550, `motor_current_functions.c:343-353`); the 0x06 went out with the hold's
seq 94, which matches the hold-end line; `Once` is the collision block's first
pass (`app.c:588-597`); the app released with MOVE_STOP after one refused
repeat (`stop`, K = 1); then the fail-safe line. The worst case (an app that
keeps repeating) is not bench-tested. The move was a RIGHT move, raising:
`Max Current` lines 309-312 read 4 / 19 / 1123 / 724 (outputs C and D carry
load = the RIGHT pair, `dual_motor_control.c:389-407`), and the margin 550 is
the raise margin (`Loaded Raise Collision Sensitivity: 550`, line 293; lower
275, line 294; the margin selection is in `motor_current_functions.c` above
`:343`).

**B4. PRE_SHIP F26 - FAILSAFE observed.** Line 323, after the trip burst. The
row now records the observation; the log has no timestamps, so the length of
the reverse is still unknown, and the row stays open for the design ruling.

**B5. Collision location, first data point.** The whole rise was on motor 2
(output C): `MOTORC = OUTPUT2` (`dual_motor_control.c:37`), and output C is in
the RIGHT pair (`:389-407`) and the FOOT pair (`:408-426`), i.e. the
foot-right corner; the other deltas were 0 / 2 / -24. Andrew confirms he
pushed on the foot-right area (his report). **Where recorded:** PRE_SHIP.md,
the "Also recorded in the repo - not yet marked pre-ship (reviewer to decide)"
table, new last row. Reason: there are no Phase 1b notes, heat-map or
last-trip record anywhere in the repo (d.0 P: grep for `heat`, `Phase 1b`,
`last trip`, `collision location` over `docs/` found only the Prompt AC
handoff's B1(c) "Last Trip" characteristic option and the prompt itself); the
row points to AC B1(c) for the design options. f.7.

**B6. Sleep veto #45.** All **62** `BLE: hold end` lines (55 `op=0x01`, 7
`op=0x03`) read `longest between passes 0 ms` (d.2 #14: `grep -c 'hold end'`
= 62; the same lines without `longest between passes 0 ms` = 0; #17: 55 of
op 0x01). Longest refresh gap per hold 41-195 ms (#17; the 195 ms is the AUTO
LEVEL hold at line 364). No `WATCHDOG STOP`. Recorded in DECISIONS #45
"Bench result (2026-11-02, Galaxy S23)".

**B7. Rulings** - recorded in the SESSION_LOG entry: AG f.1 accepted; f.2
WITHDRAWN, replaced by Part A; f.3 moot; f.4 folded into F26 (done in
`169b2f7`: the row is now "What can cut the collision reverse short: (1) the
fail-safe; (2) any app release during the reverse"); f.5 moot; f.6 done in A2.

**B8. Not tested yet** (recorded in REGRESSION and the SESSION_LOG):
physical BED LIGHTS (REGRESSION §8, 8.a-8.d) - the log has no `SW: 0x0008`
(`SWITCH_BED_LIGHTS 0x0008`, `app.h:118`; d.2 #14 count 0); 5.A1 with a
repeating app; 5.A3 (and 5.A2, 5.A4-5.A6).

---

### b.C Part C - report: live level, self-clearing LEVELED, the auto-level fill (no code)

The user wants (1) the level display to stay live while the app is open, (2)
"Level reached" to clear by itself if the bed is tilted by hand, (3) the app's
"Hold to level" button to fill while the bed takes its first reading. All
file:line references are to `169b2f7` (unchanged from `b848e48` for every
file below except `ble_bed_service.c`). [V] = VERIFIED (code read this
session, or a recorded bench result cited with its source); [A] = ASSERTED
(reasoned, not observed).

#### C1. Tilt today

**The read.** [V] `read_accel()` (`auto_level.c:241-297`) calls
`accel_get_xyz_raw()` (`mc3479.c:237-259`): one I2C write-read - 1 register
byte out, 6 bytes in - through `I2CSPM_Transfer()` on `sl_i2cspm_on_board`
(`mc3479.c:21`, `:170`), the bus the motor/LED expander also uses (DECISIONS
#27 rule 2; `exp_board.c` polls motor faults on it every 200 ms,
`exp_board.c:27`, `:354`). The bus runs in Standard mode, **100 kbit/s**
(`config/sl_i2cspm_on_board_config.h:44-49`, `SPEED_MODE 0` = "Standard mode
(100kbit/s)").

- **Time per read:** [A] wire time ≈ 9 bytes × 9 clocks + start / repeated
  start / stop ≈ 85 bit times ≈ **0.85 ms** at 100 kHz. [V, recorded] the
  measured **maximum** was **~1.4 ms** (DECISIONS #30, bench instrumentation
  on the transfer path, 2026-07-17; not re-measured).
- **Blocking:** [V, recorded] yes - `I2CSPM_Transfer()` polls to completion
  (DECISIONS #30, which read `sl_i2cspm.c`; not re-read this session).
- **Rate:** [V] the I2C transfer runs at most once per 10 ms
  (`ACCEL_I2C_MIN_INTERVAL_MS`, `auto_level.c:36`; gate `:245-249`), matching
  the MC3479's 100 Hz output (1000 Hz internal, decimate by 10, internal LPF
  ~80 Hz: `mc3479.c:339-355`). The angle math (one sqrt, two atan2, floats,
  `:251-284`) runs on every call, on the cached sample between reads.
- **Context:** [V] main loop only. `accel_running_average()`
  (`auto_level.c:333-349`) is called from the auto-level block every pass
  (`app.c:627`) and from the settings zero-level capture
  (`settings_mode.c:322`); `tilt_telemetry_tick()` (`auto_level.c:447-485`)
  is called every pass from `app.c:769`. Nothing reads the accelerometer from
  a stack callback: REFRESH_TILT only arms a timer from the main-loop handler
  (`ble_bed_service.c:805`), and T1 is a timer restart in
  `update_status_subscriber_count()` (`:477-478`).

**The filter.** [V] Each pass: LPF with `accel_alpha` 0.1
(`auto_level.c:94`, `:235-237`), into a 130-slot running average
(`NUM_ACCEL_READINGS`, `:60`), minus the user's stored baseline
(`accel_filter_update()`, `:307-331`). Two independent instances, auto-level's
and telemetry's (DECISIONS #31/#33). Both count **passes**, not ms (DECISIONS
#29, PRE_SHIP F15): [V, recorded] ~300 ms window at the auto-level loop rate
of 2.31 ms/pass (#29); [A] the loop rate during an idle telemetry burst has
never been measured, so the telemetry window there is unknown.

**When telemetry samples.** [V] `tilt_telemetry_tick()` reads only if a
connection is subscribed to Bed Status (gate `:461`), auto-level and the
settings capture are not running (`:473`), and the window is open - a motor
energised or a burst running (`:478`). A burst is 500 ms
(`TILT_TELEMETRY_BURST_MS`, `:58`), armed (extended, never stacked,
`:406-411`) by T1 (first subscriber, `ble_bed_service.c:477-478`), T2
(REFRESH_TILT, `:805`) and T3 (motors any-on -> all-off,
`auto_level.c:465-467`). Sleep is vetoed during a burst (`app.c:556`) and
while a motor is on (`app.c:512-516`). **So while the app is open and nothing
moves, the accelerometer is read only for 500 ms after the app subscribes,
after a REFRESH_TILT, or after a move ends; the rest of the time Bed Status
carries the last value of that window.** [V]

**How tilt reaches the app.** [V] Bed Status offsets 4-7 (`i16` × 100,
deviation from the baseline; pitch negated once at the wire,
`bed_status.c:140`; roll as-is). `bed_status_get()` ships auto-level's chain
during auto-level / settings, otherwise telemetry's once it has produced a
value, otherwise auto-level's last-known (`bed_status.c:113-125`). Notified at
1 Hz idle and 10 Hz while `command` is MOVE or AUTO_LEVEL
(`ble_bed_service.c:151-152`, `:1091`), and at once on a change of flags,
source, command, command detail or a motor status byte
(`status_flags_changed()`, `:1101-1112`) - **tilt is not one of them**, so a
tilt change alone waits for the next periodic notify. The attribute value is
written only when notifying (`publish_status()`, `:1114`), so a GATT read
returns the last notified value. **`tilt_fresh` (bit 3) is always 0**: the
tick passes `false` (`:1189`). REFRESH_TILT (opcode 0x07): any role, settings
mode included, arguments 0/0, arms a burst, answered 0x00 (`:797-808`).

**What DECISIONS #27 / #30 forbid, and why.** [V, the decision text]

- #27, rule 1: **no blocking call (I2C, NVM3, any wait) inside a Bluetooth
  stack event callback** (`sl_bt_on_event()` -> `ble_bed_service_on_event()`
  and anything they call synchronously). The callback sets a flag; a
  main-loop tick does the work. Why: blocking work reached from a stack
  callback was a confirmed mechanism of this project's `reason=0x1008`
  supervision-timeout drops (#26/#27; the Command handler's comment,
  `ble_bed_service.c:1433-1442` at `169b2f7`).
- #27, rule 2: **no new per-tick blocking I2C transaction on
  `sl_i2cspm_on_board`** without accounting for what already runs on it in
  the states that matter (auto-level, zero-level capture); gate a second
  consumer the way the 200 ms fault poll is gated.
- #30: the confirmed root cause of the auto-level link drop - an
  **unconditional per-pass accel I2C read during the motor-on EM0 free-spin**
  (~60 % bus occupancy) starved the radio past its 1000 ms supervision timeout;
  A/B-confirmed; fixed by limiting the read to the sensor's 100 Hz output
  (~13 % occupancy). A bus-hang hypothesis was refuted (no transfer failures,
  max ~1.4 ms). #30 also records what it did not fix: the filter is still
  pass-based (#29).

So any live stream must read from the main loop, never a callback, and must
keep the on-board bus load bounded - well under the ~13 % that #30 showed is
safe during auto-level.

#### C2. Options for a live tilt stream while a phone is subscribed

"Idle" below = subscribed, no motor energised, not auto-level, not settings.
During a move the existing path already samples every pass and notifies at
10 Hz; during auto-level / settings, auto-level's chain is shipped at 10 Hz
(auto-level) - [V] C1. **Every option below changes only the idle case**, and
every option must stand down while a motor is on, during auto-level and in
settings (two consumers on the bus per pass is what #33 gate 2c avoids).

**Option A - keep the burst window open for as long as a phone is
subscribed** (in firmware: re-arm the burst while subscribed; or A', with no
firmware change to sampling: the app sends REFRESH_TILT every ~400 ms while
its level screen is visible).

- Where the read runs: unchanged - `tilt_telemetry_tick()`, every pass.
- Cost per pass: [A] the angle math every pass; the I2C read every 10 ms
  (≤1.4 ms each, [V, recorded] #30) = **~13 % bus occupancy**, the
  configuration #30 bench-proved does not drop the link during auto-level.
- Link and sleep: the CPU is held in EM0 for the whole time a phone is
  subscribed (the burst veto, `app.c:556`). [A] Link: #45 showed staying awake
  during holds helped the link, so no harm expected, but continuous EM0 while
  merely subscribed is new. Sleep / F11: idle current while a phone is
  subscribed becomes EM0 current; the F11 idle-current measurement must then
  be taken with no phone subscribed, and the result says nothing about the
  subscribed case. If the phone keeps the subscription with the app in the
  background, the bed stays in EM0 for that time too.
- Filter: unchanged, shared with auto-level (#33). But [A] the idle loop rate
  is unmeasured and probably faster than auto-level's 2.31 ms/pass (no motor
  supervision), so the 130-pass window would be shorter than 300 ms and
  mostly repeats of the same 10 ms sample: a jumpier number (#29).
- Display rate: still the 1 Hz idle notify unless the idle cadence is raised
  (a small change in `restart_status_notify_timer()`'s interval choice).
  A' alone gives a fresh value every second, no faster.
- `tilt_fresh` could be set while the window is open.

**Option B - a decimated idle sampler with a time-based filter** (recommended).

- Where the read runs: a periodic sleeptimer (e.g. 100 ms = 10 Hz) whose
  callback only sets a flag (ISR context, no I2C); `tilt_telemetry_tick()`, in
  the main loop, does one `read_accel()` per flag and one filter update. [A]
  The CPU sleeps between samples: the timer interrupt brings the super loop
  round (the 1 Hz status notify relies on the same mechanism today and is
  bench-proven - Stage B telemetry, MIGRATION_STATUS).
- Cost: [A] per sample one I2C read (~0.85 ms wire time, ≤1.4 ms measured
  max) plus the float math and one wake. At 10 Hz: ≤14 ms of bus time per
  second, **~1.4 % occupancy** (vs ~13 % during auto-level). At 20 Hz, ~2.8 %.
- Filter: **a new, millisecond-defined filter** on its own storage, e.g. an
  exponential average with time constant τ (α computed from the measured dt
  and τ), τ ≈ 0.5 s to start, bench-tuned; seeded from the first sample when
  the idle window opens. The existing pass-based filter cannot be reused at
  10 Hz: its 130 slots would span 13 s and its LPF (α 0.1 per sample) about
  1 s - the sluggish display #32 warned about. This partly reverses #33's
  "same filter in both chains" for the idle chain, so it needs its own
  DECISIONS entry - or F15 (both chains to milliseconds) is done first and the
  chains share again.
- Display: notify at 2 Hz or 5 Hz while idle and subscribed (10 Hz moving,
  unchanged). [A] Radio: one 20-byte notification per subscriber per period;
  at the log's connection interval (24 × 1.25 ms = 30 ms, line 15) 5 Hz uses
  about one connection event in seven - small. 2 Hz gives a visibly stepping
  bubble unless the app animates; 5 Hz looks live. The update lag is set by τ,
  not by the notify rate.
- `tilt_fresh` = 1 while the last idle sample is younger than, say, 3 sample
  periods (300 ms at 10 Hz); 0 otherwise - finally implementing PRE_SHIP F16.
- Sleep / F11: [A] 10 short wakes per second plus the notify wakes, each
  ~1.5-2 ms in EM0. Per F11 the device probably sits in EM1 between events
  anyway, so the added energy is small; measure F11 with and without a
  subscribed phone.
- Moves and auto-level: unchanged; the idle sampler stops when a motor turns
  on (the per-pass chain takes over) and auto-level / settings ship their own
  chain. At the end of a move the T3 burst runs (per-pass chain); the idle
  filter should be seeded from the burst's final value when idle resumes, so
  the number does not jump.

**Option C - send raw samples and filter in the app.** The firmware does B's
sampling without a filter; the app smooths. [A] Simpler firmware, but two
places to tune, the app cannot recover what was never sampled, and auto-level's
own decisions still use the firmware filter (#32 discussed this).

**Recommendation: Option B, 10 Hz sampling, τ ≈ 0.5 s, notify at 5 Hz while
idle and subscribed, `tilt_fresh` implemented.** A' (the app sending
REFRESH_TILT periodically) is a zero-firmware way to try a 1 Hz live display
on the bench first, if the reviewer wants to see the user's reaction before
the firmware work.

Risks of B: (1) a periodic wake next to the radio's schedule while idle has
never been on the bench - #33 deferred idle sampling for exactly that reason;
bench it with `BLE_DEBUG_WATCHDOG_LOG` 1 and REGRESSION 6.9 / §5; (2) the read
blocks the main loop for up to ~1.4 ms in an idle pass (no motor runs then, by
the gate; the switch sampler's 10 ms cadence absorbs it); (3) two filter
algorithms: a small step in the shown angle when a move starts or ends (seed to
avoid it); (4) the moving and auto-level chains stay pass-based (F15 still
open); (5) a phone that stays subscribed in the background keeps the stream
running (the app should unsubscribe, or the firmware could drop to 1 Hz after
a while); (6) a new filter needs its own bench acceptance (noise at rest, lag
when tilted by hand).

#### C3. A self-clearing LEVELED bit

**The tolerances auto-level uses** [V] (units: degrees × 100,
`SCALE_FACTOR 100`, `bed_types.h:14`; values on auto-level's averaged,
baseline-relative angle):

| Constant | Value | Scaled | File:line | Used for |
|---|---|---|---|---|
| `TOLERANCE` | 0.01° | 1 | `auto_level.c:61`, `:84` | an axis is "level" (`check_level()`, `:536-604`): the levelling of an axis succeeds at \|angle\| ≤ 0.01° |
| `CLOSE_TOLERANCE` | 0.1° | 10 | `:62`, `:85` | at Step 1 an axis within 0.1° is disabled - "no levelling needed" (`axis_disabled = close_to_level \|\| level`, `:798`) |
| `PULSE_START_TOLERANCE` | 0.3° | 30 | `:63`, `:86` | inside 0.3° the axis is driven in 75 ms pulses (`:66-67`, `auto_level_single_axis()` `:804`) |

The scaled values are the double products truncated to `int16_t` at
file-scope initialisation; 0.01×100, 0.1×100 and 0.3×100 are exactly 1.0,
10.0 and 30.0 in IEEE double (d.2 #19), so they are 1, 10, 30. [A] that GCC
folds them identically.

So bit 4 = 1 means: at the end of the run each axis was either within 0.1° at
Step 1 or was driven to within 0.01° (on the running average, with the motors
stopping on that reading; any overshoot or settling after the stop is not
checked).

**Proposal** (needs C2's live idle tilt):

- **Clear bit 4 when the live, fresh, idle tilt shows |roll| or |pitch| >
  0.3° continuously for 2 s.** 0.3° is the existing pulse threshold - far
  enough off that auto-level would treat it as needing correction - and three
  times the 0.1° "no levelling needed" band; the 2 s (milliseconds, #2) rejects
  transients (a person sitting down, the frame settling after the run).
  Hysteresis is built in: the success bands are 0.01° / 0.1°, the exit band is
  0.3°, and bit 4 cannot re-set from tilt (below), so it cannot flicker. Bench
  first: record the idle stream's peak-to-peak at rest and set the exit band
  at least three times that, and no lower than ~0.2°.
- Evaluate only on a converged value: the idle filter must have run ≥ 3τ since
  the idle window opened (C5: the telemetry filter carries old history into a
  new window).
- Clear through the existing `app_set_leveled(false)` (`bed_actions.c:54-63`),
  which already prints `LEVELED (Bed Status bit 4) -> 0` and is notified at
  once (byte 0). Main loop, no I2C beyond C2's read.
- While no phone is subscribed nothing samples, so bit 4 cannot clear; it is
  re-evaluated when a phone subscribes (T1 burst) - acceptable, no one sees it
  meanwhile. **Without C2** the same rule could run only at the end of each
  500 ms burst: "Level reached" would clear at the next app open or REFRESH_TILT,
  not live.
- **Should bit 4 ever SET without an auto-level run? Recommendation: no.**
  Bit 4 means "an auto-level run ended successfully and nothing has moved
  since" (#46), and the app uses it for "levelled - you can let go" during the
  hold. Setting it from tilt would make a bed that is within 0.1° by chance look
  auto-levelled. If the user wants "Level" shown without a run, either the app
  computes it from the live angles (on the wire, with `tilt_fresh`) or a
  separate flag (bit 5, IN_LEVEL_BAND: enter ≤ 0.1° for 2 s, exit > 0.3° for
  2 s) is added. Reviewer's call (f.8).

#### C4. The auto-level start delay

**Sequence, app press** [V code; timings A]:

1. Pass N: `ble_bed_service_tick()` (last in the pass, `app.c:773`) processes
   the first AUTO_LEVEL_START -> `bed_auto_level_start()` ->
   `app_auto_level_start()` (`bed_actions.c:183-210`: LEVELED cleared,
   `active_switch = SWITCH_AUTO_LEVEL_VALUE`, `first_check = true`,
   `reset_level_data()`) and `app_apply_auto_level_start_outputs()` (red LED,
   motors off). Result `{seq, 0x00}`.
2. Pass N+1: the auto-level block (`app.c:626`) runs
   `accel_running_average()` and, because `first_check`,
   `steady_accel_read_both_axes()` (`app.c:631`, `auto_level.c:785-801`),
   which starts a **1500 ms** one-shot (`STEADY_ACCEL_READ_TIMER_TIME`,
   `auto_level.c:65`, `:789`) and prints `Step 1: ROLL and PITCH check`.
   Every pass meanwhile refreshes auto-level's 130-slot buffer ([A] ~130 passes
   ≈ 200-300 ms at the #29 loop rates, so the buffer is fully refreshed long
   before 1.5 s; the 1.5 s is mechanical-settling margin, as #33 notes).
3. The pass after the timer fires: `check_level()` on both axes,
   `axis_disabled[]` = within 0.1°, `first_check = false`
   (`auto_level.c:791-799`).
4. Next pass: both disabled -> `Bed is disabled, no leveling required`, LEVELED
   set, **no motion** (`app.c:642-657`); otherwise the current axis (ROLL, or
   PITCH if ROLL is disabled, `app.c:637-639`) -> `Step 2` ->
   `auto_level_single_axis()`: if |angle| > 0.3° -> `level_axis()` turns the
   motors on at once (`Leveling...`, `auto_level.c:750-765`); if 0.1° < |angle|
   ≤ 0.3° -> `Latch`, `level_axis_pulse()` first starts the **350 ms** off timer
   (`SNEAK_MOTOR_OFF_TIME`, `:66`, `:694-699`), so the first pulse comes ~350 ms
   later (`Pulse on...`).

**So: first motion ≈ 1.5 s + 2-3 passes after the start is processed; + 0.35 s
if the first axis starts within 0.3°; none if both axes are within 0.1°.**
The physical press is the same from the pass after `switch_input_tick()`
delivers it (`app.c:741`, after the auto-level block).

**Does it vary?** [A] The 1.5 s is a sleeptimer (fixed, milliseconds, #2). The
passes add milliseconds: the CPU is held awake during an app hold (#45) and
during a physical hold (the switch sampler session, `app.c:518-521`). On the app
side add the BLE latency of the write (next connection event, ≤30 ms at the
log's interval) and of the notification back. Expect 1.5 s ± ~50 ms at the
app. The pulse case and the "both within 0.1°" case vary with where the bed
starts. Not measured: the log has no timestamps.

A harmless quirk [A]: in the pass where the 1.5 s timer has fired,
`steady_accel_read_both_axes()` checks "timer not running" before the timeout
flag (`auto_level.c:787-793`), so it starts a second 1.5 s timer; its flag is
read only while `first_check` is true, and `reset_level_data()` stops it at the
next start or stop (`bed_actions.c:202`, `:216`). No effect seen; not changed.

**Does Bed Status tell the app the phase?** [V] No phase field. Through the
whole run `command` = AUTO_LEVEL (`bed_status.c:31-32`); the per-motor `on`
bits are all 0 during Step 1, 1 while levelling, toggling during pulses, and
all 0 in the 1000 ms pause between axes (`ACCEL_TIMER_DURATION`, `app.c:85`,
`:702-707`); bit 4 sets at success. Motor status bytes are compared by
`status_flags_changed()`, so the first motor-on is notified at once. [A] The
app can therefore detect the **end** of Step 1 as "the first motor-on, or
LEVELED, since this hold started" - unambiguous, because the between-axes pause
only comes after motion - but it cannot see progress inside Step 1, and the
pulse case adds 0.35 s of no motion after Step 1 ends.

**What the app needs to fill accurately:**

- **A fixed duration** - fill over 1.5 s from the first `0x00` to its
  AUTO_LEVEL_START. No firmware or protocol change; accuracy about ±50 ms; the
  constant lives in two repos (document it in BLE_GATT_DESIGN §4), and F15
  (pass counts -> ms) or a retune could change it silently.
- **A phase bit** - Bed Status flag bit 5 = AUTO_LEVEL_SETTLING
  (`auto_level_active && first_check`), notified at once (byte 0). Exact
  start and end, robust to a retune; additive within v1 (bits 5-7 reserved).
  The app still needs a duration to animate the fill: animate over 1.5 s and
  snap to full when the bit clears.

**Recommendation:** document the 1.5 s now (the app can use it at once), and
add the phase bit if the reviewer wants the fill exact and robust to a future
change of the 1.5 s.

#### C5. Other things in the code that bear on these, and the protocol additions

- [V] **The telemetry filter is never reset.** `telemetry_filter`
  (`auto_level.c:388`, static) keeps its 130 slots and LPF accumulator from the
  previous window, possibly hours old; a new burst blends old and new samples
  for its first ~130 passes. [A] For a live stream: seed the idle filter at
  window open; for C3: evaluate only after convergence. Auto-level's own
  filter is also not reset at a run start (`app_auto_level_start()` does not
  touch it), but Step 1's 1.5 s refreshes it.
- [V] **The baseline (set point).** `baseline_roll_angle` /
  `baseline_pitch_angle` from NVM3, range ±1500 = ±15° (`bed_settings.c:35-36`),
  captured in settings zero-level as `returnAccelValue()` - auto-level's
  average, internal sign convention (`settings_mode.c:347`, `:358`). Every
  angle on the wire and every "level" decision is relative to it; entering
  settings already clears bit 4 (#46). A live stream and a C3 band use the same
  baseline-relative value.
- [V] **Sign:** pitch is negated exactly once, at `bed_status.c:140`; roll is
  shipped as computed (now specification, B1). A new idle chain must produce
  the internal convention like the others so that the one negation stays the
  only one.
- [V] **Tilt is not a "state" field** for the notify logic (`:1101-1112`), so
  a live display needs a cadence change (or a dead-band trigger), not just more
  sampling.
- [V, recorded] **Pass-based constants** (`NUM_ACCEL_READINGS`, `accel_alpha`;
  #29, F15) - the idle loop rate is unmeasured.
- [V] **The sensor:** ±2 g, 100 Hz output, internal LPF ~80 Hz
  (`mc3479.c:339-355`). Its noise at rest has not been measured; it sets the
  practical floor for C3's exit band and for how still the live number looks.
- [V] **Other users of the on-board bus:** the motor/LED expander and the
  200 ms fault poll (`exp_board.c:27`, `:354`). Not checked this session:
  which bus the UI boards use.

**Protocol additions each option would need** (all within v1, pre-ship, per
BLE_GATT_DESIGN §6 "Protocol version and this change"):

| Option | Wire change | Notes |
|---|---|---|
| C2 A / A' | none in layout; the §6 cadence table changes if the idle notify goes to 2-5 Hz | A' needs only the app to send REFRESH_TILT (existing opcode 0x07) |
| C2 B (recommended) | `tilt_fresh` (bit 3) becomes live - already specified in §6; idle cadence 2/5 Hz in the §6 table | no new bytes; the app's `TODO(tilt-fresh)` ungating applies |
| C2 C | as B, with raw values on offsets 4-7 while idle | a change of what offsets 4-7 mean while idle - avoid |
| C3 clear-on-tilt | bit 4's "Cleared when" list gains "live tilt leaves the band" | a change of meaning of an existing bit - allowed pre-ship |
| C3 optional "level without a run" | new flag bit 5 IN_LEVEL_BAND | additive |
| C4 fixed duration | none; document "first motion ≥ 1.5 s after the first accepted AUTO_LEVEL_START" in §4 | - |
| C4 phase bit | new flag bit 5 (or 6) AUTO_LEVEL_SETTLING | additive; with C3's optional bit, two of the three reserved bits |

---

## c. Diffs

`git show` of the two work commits, complete and not truncated (the docs diffs total 634 lines; included in full rather than cut at 400). Not repeated: the new prompt file in `48c5cf6` (149 lines, written by the reviewer; it is the committed `docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md`). The `docs/SESSION_LOG.md` diff is omitted under CLAUDE.md's standing exception (it ships with this file).

### c.1 Source diff - `git show 48c5cf6 -- ble_bed_service.c` (complete; the only source change in the session)

````diff
commit 48c5cf6660fcf31b37aafc364c19cc135b74cd2e
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Tue Nov 3 08:12:35 2026 -0800

    STOP waits for release: STOP_ALL latches app starts, STOP_LATCHED 0x07 (DECISIONS #49, Prompt AH part A)
    
    Bench 2026-11-02: holding HEAD up in the app and tapping STOP stopped the bed
    only until the held button's next repeat. Now any STOP_ALL (control or
    viewer) sets an app start latch with reason STOP after its existing stop
    actions and 0x00 result; every app MOVE_START / AUTO_LEVEL_START is then
    refused with the new result 0x07 STOP_LATCHED until the app releases
    (MOVE_STOP, AUTO_LEVEL_STOP, control disconnect, 300 ms silence).
    
    - app_collision_latched (#48) becomes app_start_latch: NONE/COLLISION/STOP.
      COLLISION behaviour unchanged; it is never downgraded to STOP.
    - latch_app_starts() is the only way to set it and always calls
      note_app_commanding() (AG f.6): trip, STOP_ALL, start refusal.
    - STOP_ALL no longer clears the latch from either role (withdraws AG f.2).
    - Bench prints: "BLE: STOP_ALL latch set (conn=N)"; the cleared line is now
      "BLE: start latch (<set reason>) cleared (<reason>), K repeats refused".
    - Docs: BLE_GATT_DESIGN §4/§7 (row 0x07, 0x06 release list), DECISIONS #49,
      REGRESSION 5.A1-5.A6 updated and new §5b.
    
    ble_bed_service.c only; no GATT/config/autogen change; protocol v1 unchanged.
    Builds clean with BLE_DEBUG_WATCHDOG_LOG 1 and 0; host test passes. Not run
    on the bench.
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

diff --git a/ble_bed_service.c b/ble_bed_service.c
index f3f3685..29bcfee 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -176,6 +176,7 @@ void ble_bed_service_encode_status(const bed_status_t *in, bool tilt_fresh, uint
 #define CMD_RESULT_REJECTED_INVALID           0x04
 #define CMD_RESULT_WATCHDOG_STOP              0x05
 #define CMD_RESULT_COLLISION_STOP             0x06 // DECISIONS #48
+#define CMD_RESULT_STOP_LATCHED               0x07 // DECISIONS #49
 #define CMD_RESULT_LEN                        2
 
 // Dead-man watchdog (BLE_GATT_DESIGN.md #5). Milliseconds via sleeptimer,
@@ -281,12 +282,13 @@ static uint8_t  wd_diag_hold_end_seq = 0;
 static uint16_t wd_diag_hold_end_refreshes = 0;
 static uint16_t wd_diag_hold_end_longest_gap_ms = 0;
 static uint32_t wd_diag_hold_end_longest_between_ticks = 0;
-// Collision latch (DECISIONS #48): motion starts refused with COLLISION_STOP
-// since the app last released, and the "latch cleared" line, latched by
-// note_app_stopped_commanding() and printed by ble_bed_service_tick() once
-// the stop that cleared it has been done.
-static uint16_t wd_diag_collision_refused = 0;
+// App start latch (DECISIONS #48, #49): motion starts refused with
+// COLLISION_STOP or STOP_LATCHED since the app last released, and the "latch
+// cleared" line, latched by note_app_stopped_commanding() and printed by
+// ble_bed_service_tick() once the stop that cleared it has been done.
+static uint16_t wd_diag_latch_refused = 0;
 static bool        wd_diag_latch_clear_pending = false;
+static const char *wd_diag_latch_clear_set_reason = "";
 static const char *wd_diag_latch_clear_reason = "";
 static uint16_t    wd_diag_latch_clear_refused = 0;
 
@@ -330,18 +332,26 @@ static bed_direction_t app_move_dir    = BED_DIR_UP;
 // so calling it ten times a second would mean levelling never progressed.
 static bool app_auto_level_active = false;
 
-// Collision latch (DECISIONS #48). Set when a collision trips during an app
-// hold, while the user is still holding the app button, and when a start is
-// refused because a collision sequence is running (amended by Prompt AG).
-// Every MOVE_START and AUTO_LEVEL_START is then answered COLLISION_STOP and
-// starts nothing, until the app releases - an explicit stop, the control
-// phone's STOP_ALL, the control-phone disconnect, or MOVE_WATCHDOG_MS of
-// silence, i.e. every caller of note_app_stopped_commanding(). A viewer's
-// STOP_ALL does not clear it. Always set together with note_app_commanding(),
-// so the silence timeout is running whenever it is set. Without it the held
-// button's next repeat re-energised the same pair, into the obstruction,
-// during the collision sequence.
-static bool app_collision_latched = false;
+// App start latch, with the reason it was set. While set, every MOVE_START
+// and AUTO_LEVEL_START is refused and starts nothing, until the app releases
+// - MOVE_STOP, AUTO_LEVEL_STOP, the control-phone disconnect, or
+// MOVE_WATCHDOG_MS of silence, i.e. every caller of
+// note_app_stopped_commanding(). STOP_ALL never clears it.
+//   COLLISION (DECISIONS #48, amended by Prompt AG): a collision tripped
+//     during an app hold, or a start was refused because a collision sequence
+//     is running. Answered COLLISION_STOP. Without it the held button's next
+//     repeat re-energised the same pair, into the obstruction.
+//   STOP (DECISIONS #49): a STOP_ALL from either connection. Answered
+//     STOP_LATCHED. Without it the held button's next repeat restarted the
+//     bed right after the STOP.
+// Set ONLY through latch_app_starts(), which also starts the silence timeout,
+// so the latch can never be left set with nothing to clear it.
+typedef enum {
+  APP_LATCH_NONE = 0,
+  APP_LATCH_COLLISION,
+  APP_LATCH_STOP
+} app_start_latch_t;
+static app_start_latch_t app_start_latch = APP_LATCH_NONE;
 
 // Whether the app is still asking for motion, for the two-source inhibit
 // (#36). Refreshed by every MOVE_START/AUTO_LEVEL_START, INCLUDING refused
@@ -561,7 +571,8 @@ static void move_watchdog_cancel(void)
 }
 
 // The app has issued a motion command. Called for every MOVE_START and
-// AUTO_LEVEL_START, accepted or refused (#36 - see app_commanding).
+// AUTO_LEVEL_START, accepted or refused (#36 - see app_commanding), and by
+// latch_app_starts().
 static void note_app_commanding(void)
 {
   app_commanding = true;
@@ -569,24 +580,39 @@ static void note_app_commanding(void)
                               + sl_sleeptimer_ms_to_tick(MOVE_WATCHDOG_MS);
 }
 
+// Sets the app start latch (#48, #49) and restarts the silence timeout in the
+// same call. The latch is set ONLY here: with note_app_stopped_commanding()
+// the only place app_commanding is cleared, and it clears the latch too,
+// app_commanding is true whenever the latch is set, so the latch always clears
+// by silence MOVE_WATCHDOG_MS after the app's last start, refusal or STOP_ALL
+// (Prompt AG b.3). A COLLISION latch is never downgraded to STOP.
+static void latch_app_starts(app_start_latch_t reason)
+{
+  if (app_start_latch != APP_LATCH_COLLISION) {
+    app_start_latch = reason;
+  }
+  note_app_commanding();
+}
+
 // The app has stopped asking for motion - an explicit stop, a disconnect, or
 // the silence timeout below. Releases its half of the two-source inhibit, and
-// the collision latch (#48). reason is only for the bench print.
+// the app start latch (#48, #49). reason is only for the bench print.
 static void note_app_stopped_commanding(const char *reason)
 {
   app_commanding = false;
   bed_note_source_released(BED_SOURCE_APP);
 #if BLE_DEBUG_WATCHDOG_LOG
-  if (app_collision_latched) {
+  if (app_start_latch != APP_LATCH_NONE) {
     wd_diag_latch_clear_pending = true;
+    wd_diag_latch_clear_set_reason = (app_start_latch == APP_LATCH_STOP) ? "stop" : "collision";
     wd_diag_latch_clear_reason = reason;
-    wd_diag_latch_clear_refused = wd_diag_collision_refused;
+    wd_diag_latch_clear_refused = wd_diag_latch_refused;
   }
-  wd_diag_collision_refused = 0;
+  wd_diag_latch_refused = 0;
 #else
   (void)reason;
 #endif
-  app_collision_latched = false;
+  app_start_latch = APP_LATCH_NONE;
 }
 
 // Main-loop context. The dead-man period doing its second job: no motion
@@ -620,13 +646,12 @@ static void service_move_watchdog(void)
     // The user is still holding the app button, so latch (#48): the repeats
     // that follow are refused and start nothing. NOT note_app_stopped_
     // commanding() - the app has not released, and its half of the #36
-    // latch stays as it is. note_app_commanding() restarts the silence
+    // latch stays as it is. latch_app_starts() restarts the silence
     // timeout, so the latch clears by silence even if no repeat follows.
     move_watchdog_cancel();
     app_move_active = false;
     app_auto_level_active = false;
-    app_collision_latched = true;
-    note_app_commanding();
+    latch_app_starts(APP_LATCH_COLLISION);
     send_command_result(control_connection, last_move_seq, CMD_RESULT_COLLISION_STOP);
 #if BLE_DEBUG_WATCHDOG_LOG
     // After the motors are off and the result is sent.
@@ -718,22 +743,23 @@ static void process_pending_command(void)
     move_watchdog_cancel();
     app_move_active = false;
     app_auto_level_active = false;
-    if (conn == control_connection || !app_collision_latched) {
-      note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
-    } else {
-      // A viewer's STOP_ALL during the collision latch (#48, amended by
-      // Prompt AG). It stops the bed and releases the app's half of the #36
-      // latch as before, but it is not the control user's release, so the
-      // collision latch stays. app_commanding is left set (the latch is only
-      // ever set with it set), so the silence timeout keeps running from the
-      // control app's last repeat.
-      bed_note_source_released(BED_SOURCE_APP);
-    }
+    bed_note_source_released(BED_SOURCE_APP); // never inhibited: releases the app's half of the #36 latch
     app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
 
     // Accepted from ANY role - control or viewer. DECISIONS #12's
     // stop-from-anyone rule, and #4's explicit carve-out.
     send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
+
+    // Stop latch (#49). The app may still be holding a button and repeating
+    // its start; that is not a new press. Refuse app starts until the app
+    // releases (an explicit stop, a disconnect, or silence). From either
+    // role, and it never clears the latch: a COLLISION latch stays COLLISION.
+    latch_app_starts(APP_LATCH_STOP);
+#if BLE_DEBUG_WATCHDOG_LOG
+    // After the stop and the result.
+    printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
+           (app_start_latch == APP_LATCH_COLLISION) ? ", kept as collision" : "");
+#endif
   }
 
   if (!command_pending) {
@@ -795,27 +821,37 @@ static void process_pending_command(void)
     return;
   }
 
-  // Collision latch (#48). Motion-starting opcodes only, and before the #36
-  // check so the app is told "collision", not "wall switch". Refused while
-  // latched (a collision during this app hold, button not yet released), and
-  // also while ANY collision sequence runs - a quick release and press inside
-  // the sequence, or a press during a physical-controller collision, would
-  // otherwise re-energise a pair while current supervision is off
-  // (switch_input_tick() requires !any_collision). A refusal also latches
-  // (Prompt AG), so a hold that met a collision must be released before a
-  // new start is accepted, even once the sequence has ended.
+  // App start latch (#48, #49). Motion-starting opcodes only, and before the
+  // #36 check so the app is told "collision" or "stopped", not "wall switch".
+  //
+  // STOP (#49): a STOP_ALL since the app last released - answered
+  // STOP_LATCHED.
+  //
+  // COLLISION (#48): a collision during this app hold, button not yet
+  // released - and also while ANY collision sequence runs: a quick release
+  // and press inside the sequence, or a press during a physical-controller
+  // collision, would otherwise re-energise a pair while current supervision is
+  // off (switch_input_tick() requires !any_collision). A refusal also latches
+  // (Prompt AG), so a hold that met a collision must be released before a new
+  // start is accepted, even once the sequence has ended. Answered
+  // COLLISION_STOP.
   //
-  // note_app_commanding() as in the #36 refusal: a refused repeat still means
-  // the user is holding, so the silence timeout must not release the latch.
-  // No bed_control call, no last_move_seq update, no watchdog arm.
+  // latch_app_starts() restarts the silence timeout as in the #36 refusal: a
+  // refused repeat still means the user is holding, so the silence timeout
+  // must not release the latch. No bed_control call, no last_move_seq update,
+  // no watchdog arm.
   if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
-      && (app_collision_latched || any_collision)) {
-    app_collision_latched = true;
-    note_app_commanding();
-    send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);
+      && (app_start_latch != APP_LATCH_NONE || any_collision)) {
+    if (app_start_latch == APP_LATCH_STOP) {
+      latch_app_starts(APP_LATCH_STOP);
+      send_command_result(conn, seq, CMD_RESULT_STOP_LATCHED);
+    } else {
+      latch_app_starts(APP_LATCH_COLLISION);
+      send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);
+    }
 #if BLE_DEBUG_WATCHDOG_LOG
-    if (wd_diag_collision_refused < 0xFFFF) {
-      wd_diag_collision_refused++;
+    if (wd_diag_latch_refused < 0xFFFF) {
+      wd_diag_latch_refused++;
     }
 #endif
     return;
@@ -1132,12 +1168,13 @@ void ble_bed_service_tick(void)
            (unsigned)wd_diag_hold_end_refreshes, (unsigned)wd_diag_hold_end_longest_gap_ms,
            (unsigned long)sl_sleeptimer_tick_to_ms(wd_diag_hold_end_longest_between_ticks));
   }
-  // The collision latch (#48) was released above; the stop that released it
-  // is done.
+  // The app start latch (#48, #49) was released above; the stop that released
+  // it is done.
   if (wd_diag_latch_clear_pending) {
     wd_diag_latch_clear_pending = false;
-    printf("BLE: collision latch cleared (%s), %u repeats refused\r\n",
-           wd_diag_latch_clear_reason, (unsigned)wd_diag_latch_clear_refused);
+    printf("BLE: start latch (%s) cleared (%s), %u repeats refused\r\n",
+           wd_diag_latch_clear_set_reason, wd_diag_latch_clear_reason,
+           (unsigned)wd_diag_latch_clear_refused);
   }
 #endif
 
````

### c.2 Part A docs - `git show 48c5cf6 -- docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/REGRESSION.md` (complete)

````diff
commit 48c5cf6660fcf31b37aafc364c19cc135b74cd2e
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Tue Nov 3 08:12:35 2026 -0800

    STOP waits for release: STOP_ALL latches app starts, STOP_LATCHED 0x07 (DECISIONS #49, Prompt AH part A)
    
    Bench 2026-11-02: holding HEAD up in the app and tapping STOP stopped the bed
    only until the held button's next repeat. Now any STOP_ALL (control or
    viewer) sets an app start latch with reason STOP after its existing stop
    actions and 0x00 result; every app MOVE_START / AUTO_LEVEL_START is then
    refused with the new result 0x07 STOP_LATCHED until the app releases
    (MOVE_STOP, AUTO_LEVEL_STOP, control disconnect, 300 ms silence).
    
    - app_collision_latched (#48) becomes app_start_latch: NONE/COLLISION/STOP.
      COLLISION behaviour unchanged; it is never downgraded to STOP.
    - latch_app_starts() is the only way to set it and always calls
      note_app_commanding() (AG f.6): trip, STOP_ALL, start refusal.
    - STOP_ALL no longer clears the latch from either role (withdraws AG f.2).
    - Bench prints: "BLE: STOP_ALL latch set (conn=N)"; the cleared line is now
      "BLE: start latch (<set reason>) cleared (<reason>), K repeats refused".
    - Docs: BLE_GATT_DESIGN §4/§7 (row 0x07, 0x06 release list), DECISIONS #49,
      REGRESSION 5.A1-5.A6 updated and new §5b.
    
    ble_bed_service.c only; no GATT/config/autogen change; protocol v1 unchanged.
    Builds clean with BLE_DEBUG_WATCHDOG_LOG 1 and 0; host test passes. Not run
    on the bench.
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

diff --git a/docs/BLE_GATT_DESIGN.md b/docs/BLE_GATT_DESIGN.md
index 44ffc5e..eef9b82 100644
--- a/docs/BLE_GATT_DESIGN.md
+++ b/docs/BLE_GATT_DESIGN.md
@@ -99,7 +99,7 @@ offset 3  seq      u8   rolling per-connection sequence number
 | 0x03 | AUTO_LEVEL_START | — | — | `bed_auto_level_start(BED_SOURCE_APP)`. **Hold-to-run** (DECISIONS #36), matching the physical auto-level button: **repeated every 100 ms while held**, and it arms and refreshes the same 300 ms dead-man watchdog a MOVE_START does. A repeat must not re-enter the verb — that would reset the levelling state machine. Release sends AUTO_LEVEL_STOP, which stops immediately. |
 | 0x04 | AUTO_LEVEL_STOP | — | — | `bed_auto_level_stop(BED_SOURCE_APP)`. |
 | 0x05 | LIGHTS_SET | 0 off / 1 on | — | `bed_lights_set(BED_SOURCE_APP, …)`. Latched. |
-| 0x06 | STOP_ALL | — | — | Explicit full stop (maps to the `app_stop_all()` path: motors + auto-level + LEDs). The app's big red button. Always accepted from **any** role, control or viewer — this is the BLE embodiment of the stop-from-anyone rule (#12). |
+| 0x06 | STOP_ALL | — | — | Explicit full stop (maps to the `app_stop_all()` path: motors + auto-level + LEDs). The app's big red button. Always accepted from **any** role, control or viewer — this is the BLE embodiment of the stop-from-anyone rule (#12). Then latches app motion starts until the app releases (DECISIONS #49; §7, result 0x07). |
 | 0x07 | REFRESH_TILT | 0 | 0 | Opens a tilt sampling burst (DECISIONS #33, trigger T2) so the app can pull a fresh angle on demand. **Commands no motion, cancels no motion.** Accepted from **any** role and in settings mode — see the arbitration note. Both args must be 0; anything else is REJECTED_INVALID. |
 
 **Implementation status: all seven opcodes are implemented** (Stage C,
@@ -368,6 +368,7 @@ Notify, 2 bytes: `{ seq u8, result u8 }`, echoing the command's seq.
 | 0x04 | REJECTED_INVALID | malformed / unknown opcode |
 | 0x05 | WATCHDOG_STOP | connection stalled; movement stopped |
 | 0x06 | COLLISION_STOP | Collision: the app ends its hold and shows the collision screen (see below) |
+| 0x07 | STOP_LATCHED | "Stopped - release and press again" (see below) |
 
 **COLLISION_STOP (0x06), DECISIONS #48.** Sent in two cases:
 
@@ -387,14 +388,14 @@ Notify, 2 bytes: `{ seq u8, result u8 }`, echoing the command's seq.
    press made while any collision sequence runs - must be released before a
    new start is accepted, even after the sequence has ended.
 
-The latch clears when the app releases: MOVE_STOP, AUTO_LEVEL_STOP, STOP_ALL
-**from the control connection**, the control connection dropping while the
-app holds the bed, or `MOVE_WATCHDOG_MS` (300 ms) with no MOVE_START /
-AUTO_LEVEL_START from the app. **A viewer's STOP_ALL does not clear it**: it
-stops the bed as always (and is answered 0x00), but the control user may still
-be holding, so the latch then clears by the control app's own release or 300
-ms after its last MOVE_START / AUTO_LEVEL_START. A press after the release,
-once the collision sequence has ended, is a normal start. Checked after the viewer and settings-mode rejections and before
+The latch clears when the app releases: MOVE_STOP, AUTO_LEVEL_STOP, the
+control connection dropping while the app holds the bed, or `MOVE_WATCHDOG_MS`
+(300 ms) with no MOVE_START / AUTO_LEVEL_START (accepted or refused) and no
+STOP_ALL from the app. **STOP_ALL does not clear it, from either connection**
+(DECISIONS #49; before #49 the control connection's STOP_ALL did): it stops the
+bed as always and is answered 0x00, and the latch stays a collision latch, so
+the held button's repeats keep getting 0x06. A press after the release, once
+the collision sequence has ended, is a normal start. Checked after the viewer and settings-mode rejections and before
 the #36 two-source check, so a viewer still gets 0x02, settings mode 0x03, and
 the app is told "collision", not "wall switch".
 
@@ -404,8 +405,40 @@ the finger; a new press is a new hold. The firmware does not rely on this:
 the repeats of any hold that met a collision are refused until it releases,
 including a new press made while the collision sequence is still running.
 
-**Protocol version: additive within v1, no version change.** The code was
-unused and the byte layout is unchanged; a client that does not know 0x06
+**STOP_LATCHED (0x07), DECISIONS #49.** A STOP_ALL - from the control or a
+viewer connection - stops the bed (answered 0x00 to the sender, as always) and
+then sets the firmware's **stop latch**. While it is set, every MOVE_START and
+AUTO_LEVEL_START from the control connection is answered 0x07, echoing that
+command's own seq, and starts nothing; it does not arm the watchdog. The user
+may still have a finger on a held app button, and the app keeps repeating its
+start while the finger is down: those repeats are not a new press. Sent only
+in answer to a start; a STOP_ALL itself is answered 0x00.
+
+The stop latch clears exactly like the collision latch: MOVE_STOP,
+AUTO_LEVEL_STOP, the control connection dropping while the app holds the bed,
+or 300 ms with no MOVE_START / AUTO_LEVEL_START (accepted or refused) and no
+STOP_ALL from the app. A further STOP_ALL does not clear it. So:
+
+- a held app button after a STOP stays stopped until the finger is lifted,
+  then a fresh press is a normal start;
+- a press made within 300 ms of a STOP tapped with nothing held is refused
+  (0x07) and must be released and pressed again; a press after that is a
+  normal start.
+
+If the collision latch (0x06) is already set when a STOP_ALL arrives, it stays
+a collision latch and starts keep getting 0x06. While the stop latch is set,
+a start is answered 0x07 even if a collision sequence is running (Bed Status
+bit 0). Checked in the same place as 0x06: after the viewer and settings-mode
+rejections, before the #36 two-source check.
+
+App behaviour: on 0x07, end the hold (stop repeating; a MOVE_STOP /
+AUTO_LEVEL_STOP is fine) and show "Stopped - release and press again" until
+the user lifts the finger; a new press is a new hold. On its own STOP button
+the app should also end every hold it is running. The firmware does not rely
+on either: the repeats of a held button are refused until it releases.
+
+**Protocol version: additive within v1, no version change.** The codes were
+unused and the byte layout is unchanged; a client that does not know 0x06 or 0x07
 gets a result code it has no entry for, as for any future code. The
 Protocol Version value stays `01`; the after-first-ship rule in §6 is
 unchanged by this. The app repository's copy of this document was written
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 3436774..0fc4b0c 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2609,3 +2609,96 @@ sequence.
 
 **Status:** builds clean (no warnings) with `BLE_DEBUG_WATCHDOG_LOG` 1 and 0;
 host test unchanged and passes. **Not run on the bench.** REGRESSION 5.A3-5.A6.
+
+## 49. STOP waits for release: a STOP_ALL latches app motion starts (STOP_LATCHED 0x07) until the app releases
+
+Date: 2026-11-03. Reviewer's Prompt AH part A, from Andrew's bench run of
+2026-11-02. **Withdraws the reviewer's ruling on the Prompt AG handoff f.2**
+("a STOP_ALL from the control connection still clears the latch, as now";
+the control phone's STOP_ALL after the collision sequence let a held repeat
+restart the move).
+
+### The bench result
+
+2026-11-02, Andrew, firmware `b848e48`, Galaxy S23 on app `4e66328`, log
+`PuTTY Logs\round3_bench_2026-11-02.log`: holding HEAD up in the app and
+tapping the app's STOP stopped the bed only briefly; the held button's next
+repeat started it again. The firmware had no STOP_ALL print, so the log shows
+each STOP only as a short hold followed at once by a new hold, e.g. lines
+143-145 (`hold end op=0x01 seq=89: 7 refreshes`, then `seq=95: 4 refreshes`,
+`seq=101: 4 refreshes`) and 328-330. Matching those lines to STOP taps rests
+on Andrew's account.
+
+From code (before this entry): STOP_ALL cleared `app_move_active`, so the held
+button's next MOVE_START repeat (every 100 ms) found no move active and called
+`bed_move_start()` + `app_apply_move_outputs()` - a fresh start. An AUTO LEVEL
+hold restarted from Step 1 the same way.
+
+### Decision (user requirement)
+
+**After a STOP the bed stays stopped, and no app motion is accepted until
+every app button has been released.**
+
+- **One app start latch with a reason** (`ble_bed_service.c` only).
+  `app_collision_latched` (#48) became `app_start_latch`: NONE, COLLISION or
+  STOP. COLLISION behaves exactly as #48 + its Prompt AG amendment.
+- **One helper sets it**, `latch_app_starts(reason)`, which also calls
+  `note_app_commanding()` (Prompt AG handoff f.6, folded in). It never
+  downgrades COLLISION to STOP. Callers - the only places the latch is set:
+  the collision trip in `service_move_watchdog()` (COLLISION), STOP_ALL
+  (STOP), and the start refusal in `process_pending_command()` (re-asserts the
+  current reason; COLLISION when only `any_collision` refused it). So the
+  latch is never set without the silence timeout running (Prompt AG b.3).
+- **Any STOP_ALL, from the control connection or a viewer, sets the latch with
+  reason STOP**, after its existing stop actions and its 0x00 result. If the
+  latch is already COLLISION it stays COLLISION. **STOP_ALL no longer clears
+  the latch from either role.** Everything else STOP_ALL does is unchanged:
+  accepted from any role (#12), the watchdog cancelled, both app flags
+  cleared, the app's half of the #36 latch released
+  (`bed_note_source_released(BED_SOURCE_APP)`, now called directly for both
+  roles), `app_stop_all()`, 0x00 to the sender.
+- **While latched with reason STOP**, every MOVE_START / AUTO_LEVEL_START from
+  the control connection is refused with the new Command Result **0x07
+  STOP_LATCHED** (its own seq): it starts nothing, arms no watchdog, does not
+  update `last_move_seq`, and restarts the silence timeout. Reason COLLISION
+  still answers 0x06. The `|| any_collision` term is unchanged: with no latch
+  set, a start during any collision sequence is refused 0x06 and latches
+  COLLISION. With the stop latch set, a start is answered 0x07 even during a
+  collision sequence (the STOP check comes first).
+- **The latch clears only through `note_app_stopped_commanding()`**: MOVE_STOP,
+  AUTO_LEVEL_STOP, the control-phone disconnect stop, and the silence timeout
+  (300 ms after the app's last start, refusal or STOP_ALL). The watchdog
+  expiry also calls it, but cannot run while latched. STOP_ALL is no longer a
+  caller.
+- Bench prints behind `BLE_DEBUG_WATCHDOG_LOG`, main-loop context, after the
+  stop and the result: `BLE: STOP_ALL latch set (conn=N)` at every STOP_ALL
+  (with `, kept as collision` appended when the latch was already COLLISION);
+  the "latch cleared" line now names the reason it was set:
+  `BLE: start latch (stop|collision) cleared (stop|disconnect|silence), K
+  repeats refused` (it was `BLE: collision latch cleared (...)`). K counts
+  0x06 and 0x07 refusals since the app last released.
+- Additive within protocol v1 (BLE_GATT_DESIGN §7 row 0x07); the Protocol
+  Version is unchanged. No GATT, `config/` or `autogen/` change.
+
+### Why STOP_ALL no longer clears the latch
+
+A STOP_ALL is not a release. The user can tap STOP with one finger while
+another is still on a held button, and the app keeps repeating that button's
+start; the only evidence that the buttons are released is a stop write for the
+hold, the phone going away, or the repeats ending. Clearing the latch at the
+STOP_ALL is what let the held repeat restart the bed - after an ordinary hold
+(the bench result) and after a collision (Prompt AG f.2, now withdrawn). The
+viewer case was already handled this way for the collision latch (Prompt AG
+f.1); it now holds for both roles and both reasons.
+
+The app side - STOP ends every hold in the app - comes in a separate app
+prompt. The firmware does not rely on it.
+
+### Status
+
+Builds clean (no warnings) with `BLE_DEBUG_WATCHDOG_LOG` 1 and 0; the host
+test (`test/bed_control_test.c`) is unchanged and passes (it compiles
+bed_control.c only and cannot reach this code). **Not run on the bench.**
+REGRESSION §5 (5.A1-5.A6 updated) and §5b (new). What happens in each case,
+and the open points, are in the Prompt AH handoff
+(`fw_HANDOFF_2026-11-03_01_...`, b.A).
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index ce8de8a..6eb3ae8 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -193,7 +193,10 @@ pair is enough. With an app that does not yet handle COLLISION_STOP (0x06)
 it keeps repeating while the finger is down, which is the case these items
 test. With an app that does handle it, the hold ends at the first 0x06 and
 the latch clears at once (`stop`) or 300 ms later (`silence`); the motors
-must do the same in both cases.
+must do the same in both cases. Since DECISIONS #49 the "latch cleared" line
+reads `BLE: start latch (collision) cleared (<reason>), K repeats refused`
+(before: `BLE: collision latch cleared (...)`), and every STOP_ALL prints
+`BLE: STOP_ALL latch set (conn=N)`.
 
 - [ ] 5.A1 Collision during an app **MOVE** hold (B1). Raise into the
       obstruction from the app, keep the finger down ~3 s after the trip,
@@ -204,60 +207,124 @@ must do the same in both cases.
       `FAILSAFE: motors energised with nothing commanding - stopped` line
       during it - ASSERTED, see #48); everything off at ~1.5 s. **The pair
       does not re-energise toward the obstruction at any point while the
-      finger is held.** On release: `BLE: collision latch cleared (stop), K
-      repeats refused` with K ≈ 10 per second held. The new press moves
+      finger is held.** On release: `BLE: start latch (collision) cleared
+      (stop), K repeats refused` with K ≈ 10 per second held. The new press moves
       normally (no 0x06). App: 0x06 for the held button (if it shows result
       codes); Bed Status bit 0 set for ~1.5 s.
 - [ ] 5.A2 Collision during an app **AUTO LEVEL** hold (B2). Obstruct a
       levelling pair, keep holding ~3 s after the trip, release, press again
       (obstruction removed). Observe: trip lines, `BLE: COLLISION_STOP seq=N
       op=0x03 (latched)`, `BLE: hold end op=0x03 ...`, `Once`; LED blink, **no
-      reverse and no motion at all** until the release; `BLE: collision latch
-      cleared (stop), K repeats refused`. The new press starts a fresh
+      reverse and no motion at all** until the release; `BLE: start latch
+      (collision) cleared (stop), K repeats refused`. The new press starts a fresh
       auto-level run (`Step 1` prints).
 - [ ] 5.A3 Release and re-press inside the collision sequence (B3). As 5.A1,
       but release ~0.5 s after the trip, press the **same** button again at
       once, and keep holding ~2 s past the end of the blink. Observe: `BLE:
-      collision latch cleared (stop), K repeats refused` at the release (K
+      start latch (collision) cleared (stop), K repeats refused` at the release (K
       about 4-5); **nothing moves toward the obstruction while the LEDs
       blink** (the reverse at blink 2 may still run, with the `FAILSAFE`
       line); **after the sequence ends the held re-press still starts
       nothing** - its repeats keep getting 0x06 (the refusal latched, DECISIONS
-      #48 as amended by Prompt AG). At the second release: `BLE: collision
-      latch cleared (stop), K repeats refused`, K about 10 per second of the
-      re-press. A fresh press after that moves normally.
-- [ ] 5.A4 STOP_ALL from the **control** phone during the latch (B5). As 5.A1,
-      but while still holding after the trip press the app's STOP (STOP_ALL)
-      - once inside the blink sequence (~0.5 s), once after it (~2 s).
-      App: 0x00 for the STOP_ALL. **Inside the sequence:** `BLE: collision
-      latch cleared (stop_all), K repeats refused` at the STOP_ALL; the held
-      button's next repeat is refused and re-latches, so **nothing moves,
-      during or after the sequence**, until the held button is released
-      (`BLE: collision latch cleared (stop), K repeats refused` then). **After
-      the sequence:** `cleared (stop_all)` at the STOP_ALL, and an app still
-      repeating the held move **starts the move again** on its next repeat
-      (as a STOP_ALL during any ordinary hold; supervision is running).
+      #48 as amended by Prompt AG). At the second release: `BLE: start
+      latch (collision) cleared (stop), K repeats refused`, K about 10 per
+      second of the re-press. A fresh press after that moves normally.
+- [ ] 5.A4 STOP_ALL from the **control** phone during the latch (B5;
+      changed by DECISIONS #49). As 5.A1, but while still holding after the
+      trip press the app's STOP (STOP_ALL) - once inside the blink sequence
+      (~0.5 s), once after it (~2 s). App: 0x00 for the STOP_ALL. Both times:
+      `BLE: STOP_ALL latch set (conn=N), kept as collision` and **no `latch
+      cleared` line at the STOP_ALL**; the held button's repeats keep getting
+      0x06 and **nothing moves, during or after the sequence**, until the held
+      button is released (`BLE: start latch (collision) cleared (stop), K
+      repeats refused` then, K counting every refusal since the trip). A
+      fresh press after that moves normally. (Before #49, a STOP_ALL after the
+      sequence cleared the latch and the next held repeat restarted the move.)
 - [ ] 5.A5 Physical-controller collision unchanged: raise into the obstruction
       from the switch panel with the app connected and idle. No `BLE:
       COLLISION_STOP` line, no Command Result; Bed Status bit 0 set for
       ~1.5 s. Then repeat with an app MOVE press made during the blink and
       held ~2 s past its end: 0x06 for every repeat, **during and after the
       sequence**, nothing starts from the app; on its release `BLE:
-      collision latch cleared (stop), K repeats refused`. A fresh app press
+      start latch (collision) cleared (stop), K repeats refused`. A fresh app press
       after that moves normally. (If the panel is moving the bed when the app
       button is released, the app's MOVE_STOP stops it - #12, unchanged.)
-- [ ] 5.A6 STOP_ALL from the **viewer** phone during the latch (Prompt AG).
-      Second phone connected as VIEWER. As 5.A1 (control phone holding the
-      move into the obstruction); while the control finger stays down, press
-      STOP (STOP_ALL) on the viewer - once inside the blink sequence, once
-      after it. Observe: viewer gets 0x00; **no `latch cleared` line at the
-      viewer's STOP_ALL**; the control phone's held repeats keep getting
-      0x06 and **nothing restarts, during or after the sequence**. On the
-      control release: `BLE: collision latch cleared (stop), K repeats
-      refused`, K counting every refusal since the trip. Optional: after the
-      viewer's STOP_ALL, turn off the control phone's Bluetooth while its
-      finger is still down: expect `BLE: collision latch cleared (silence)`
-      ~300 ms after its last repeat (not `disconnect`), and nothing moves.
+- [ ] 5.A6 STOP_ALL from the **viewer** phone during the latch (Prompt AG;
+      DECISIONS #49). Second phone connected as VIEWER. As 5.A1 (control
+      phone holding the move into the obstruction); while the control finger
+      stays down, press STOP (STOP_ALL) on the viewer - once inside the blink
+      sequence, once after it. Observe: viewer gets 0x00; `BLE: STOP_ALL latch
+      set (conn=V), kept as collision` (V = the viewer's handle) and **no
+      `latch cleared` line at the viewer's STOP_ALL**; the control phone's held
+      repeats keep getting 0x06 and **nothing restarts, during or after the
+      sequence**. On the control release: `BLE: start latch (collision)
+      cleared (stop), K repeats refused`, K counting every refusal since the
+      trip. Optional: after the viewer's STOP_ALL, turn off the control
+      phone's Bluetooth while its finger is still down: expect `BLE: start
+      latch (collision) cleared (silence)` ~300 ms after its last repeat (not
+      `disconnect`), and nothing moves.
+
+## 5b. App STOP latch (DECISIONS #49)
+
+App connected as CONTROL, `BLE_DEBUG_WATCHDOG_LOG` 1, UART logged, no
+obstruction. "STOP" = the app's STOP button (STOP_ALL). Items 5b.1-5b.6 are
+the cases the Prompt AH handoff (b.A.6) works through from code. An app that
+does not yet end its holds on STOP keeps repeating the held button, which is
+the case 5b.1 tests; an app that does (the next app prompt) is 5b.2. **In
+every item: after the STOP nothing moves until the held button is released
+and pressed again.**
+
+- [ ] 5b.1 App holding HEAD up; tap STOP with another finger; keep the HEAD
+      finger down ~2 s; release; press HEAD up again. Observe: the bed stops
+      at the STOP and **stays stopped** while the finger is down; `BLE:
+      STOP_ALL latch set (conn=N)` then `BLE: hold end op=0x01 ...`; App:
+      0x00 for the STOP, 0x07 (STOP_LATCHED) for every held repeat (about 10
+      per second). On release: `BLE: start latch (stop) cleared (stop), K
+      repeats refused`, K about 20 for 2 s. The fresh press moves normally
+      (0x00). (2026-11-02, before #49: the bed restarted on the next repeat -
+      the bench result in DECISIONS #49.)
+- [ ] 5b.2 As 5b.1 with an app that sends MOVE_STOP right after its STOP_ALL
+      and ends the hold. Observe: `STOP_ALL latch set`, then at once `BLE:
+      start latch (stop) cleared (stop), 0 repeats refused`; nothing moves
+      while the finger stays down (the app sends nothing more); a fresh press
+      moves normally. If the app instead keeps repeating after its MOVE_STOP,
+      the next repeat **starts the move** - record it (the firmware cannot tell
+      a repeat after a MOVE_STOP from a new press; Prompt AH handoff f).
+- [ ] 5b.3 Nothing held: (a) tap STOP, press HEAD up within ~0.2 s and hold
+      1 s, release; (b) tap STOP again, wait at least 1 s, press HEAD up.
+      Observe (a): 0x07 for the press and its repeats, nothing moves; at the
+      release `BLE: start latch (stop) cleared (stop), K repeats refused`; a
+      fresh press then moves. (b): `BLE: start latch (stop) cleared (silence),
+      0 repeats refused` about 300 ms after the STOP; the press moves normally
+      (0x00). If (b)'s press is refused with 0x07, record it (Prompt AH
+      handoff f: in a main-loop pass the command is processed before the
+      silence check).
+- [ ] 5b.4 App AUTO LEVEL hold from off-level, tap STOP mid-run, keep
+      holding ~2 s, release, press AUTO LEVEL again. Observe: auto-level stops
+      at the STOP (motors off, red LED off); `STOP_ALL latch set` and `BLE:
+      hold end op=0x03 ...`; 0x07 for every held repeat; **no `Step 1` line
+      while the finger is down** (before #49 the next repeat restarted the run
+      from Step 1); at the release `BLE: start latch (stop) cleared (stop), K
+      repeats refused`; the fresh press starts a new run (`Step 1`). If the
+      bed was LEVELED, no `LEVELED` line at the STOP.
+- [ ] 5b.5 Second phone connected as VIEWER. Control phone holding a move;
+      tap STOP on the viewer. Observe: the bed stops; viewer gets 0x00; `BLE:
+      STOP_ALL latch set (conn=V)`; the control phone gets 0x07 for every held
+      repeat and **nothing restarts**; on the control release `BLE: start
+      latch (stop) cleared (stop), K repeats refused`. Optional: lift the
+      control finger with the control phone's Bluetooth turned off (no stop
+      write reaches the bed): `... cleared (silence)` about 300 ms after its
+      last repeat, nothing moves.
+- [ ] 5b.6 Hold a **physical** move button (e.g. HEAD UP) and tap STOP in the
+      app while still holding it ~2 s; release; press it again. Expected from
+      code: the bed stops at the STOP and **does not restart while the button
+      is still held** (no new press edge, no `SW:` line until the release);
+      App: 0x00; `BLE: STOP_ALL latch set (conn=N)`; at the release an
+      `SW: ... up` line and nothing moves; the next physical press moves
+      normally. Repeat holding the physical AUTO LEVEL button: auto-level
+      stops and does not restart (no `Step 1`) until released and pressed
+      again.
+- [ ] 5b.7 BLE link stays up (no disconnect) through 5b.1-5b.6.
 
 ## 6. Auto-level
 
````

### c.3 Part B docs - `git show 169b2f7` (complete)

````diff
commit 169b2f718028c36eca01e7d026abf97abce15933
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Tue Nov 3 08:15:18 2026 -0800

    Bench record of 2026-11-02 and rulings (Prompt AH part B, docs only)
    
    From Andrew's run on b848e48, Galaxy S23 on app 4e66328,
    round3_bench_2026-11-02.log:
    - B1 BLE_GATT_DESIGN §6: roll sign (positive = LEFT raised) is specification,
      bench-confirmed by the app's bubble and words; PROVISIONAL marker removed.
      No DECISIONS entry called it unconfirmed.
    - B2 DECISIONS #46 / REGRESSION §6: six successful app auto-levels (lines
      169-417) print LEVELED -> 1, Exit auto-level, Bed is level; -> 0 at the
      next motion start. 6.1 prints, 6.8, 6.10b pass for the app path.
    - B3 DECISIONS #48 / REGRESSION §5: 5.A1 passes for an app that ends its
      hold at the first 0x06 (lines 313-323). Repeating-app case not tested.
    - B4/B7 PRE_SHIP F26: FAILSAFE observed after the app collision (line 323);
      row widened to "what can cut the reverse short" (AG f.4 folded in).
    - B5 PRE_SHIP: collision-location first data point (whole rise on output C,
      foot-right), new row in "Also recorded".
    - B6 DECISIONS #45: all 62 hold-end lines read "longest between passes 0 ms".
    - B8 REGRESSION §8: physical BED LIGHTS checks not yet tested (no SW: 0x0008).
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

diff --git a/docs/BLE_GATT_DESIGN.md b/docs/BLE_GATT_DESIGN.md
index eef9b82..f9ff270 100644
--- a/docs/BLE_GATT_DESIGN.md
+++ b/docs/BLE_GATT_DESIGN.md
@@ -223,7 +223,6 @@ offset 3   command_detail   u8   bits0-2 target (0-4), bit3 direction (0 down/1
                                  valid only when command == MOVE
 offset 4   roll_scaled      i16  ×100, deviation from level baseline
                                  POSITIVE = LEFT side raised
-                                 ** PROVISIONAL - see "Tilt sign conventions" **
 offset 6   pitch_scaled     i16  ×100, deviation from level baseline
                                  POSITIVE = HEAD raised
 offset 8   motor[0..3]      4 × { status u8, current_ma u16 }  = 12 B
@@ -282,17 +281,20 @@ The conversion is therefore applied exactly once, at the wire boundary, in
 `bed_status.c`'s `bed_status_get()`. Clients decode the wire value as-is; no
 client should apply a sign transform of its own.
 
-**Roll — PROVISIONAL, pending bench confirmation. `POSITIVE = LEFT raised`.**
+**Roll — SPECIFICATION. `POSITIVE = LEFT raised`.** Bench-confirmed
+2026-11-02 (Prompt AH part B1).
 
-This is **not** established. It is read off the frame diagram comment in
-`auto_level.c` (above `set_auto_level_motor_direction()`), which is a
-description of the intended mounting, not a measurement. The bench run that
-found the pitch sign wrong did not report on roll at all, and silence is not
-confirmation — the same run would have looked identical whether roll was right
-or wrong, because nobody checked it.
+It was first read off the frame diagram comment in `auto_level.c` (above
+`set_auto_level_motor_direction()`), a description of the intended mounting,
+and was carried as provisional until a bench check. On 2026-11-02 (Andrew,
+firmware `b848e48`, Galaxy S23 on app `4e66328`) the app, decoding the wire
+roll as-is (`kRollWireSign = +1` in the app), showed its level bubble and its
+words on the side that was actually raised. That observation is the user's
+report: the UART log of the run (`PuTTY Logs\round3_bench_2026-11-02.log`) carries no tilt values.
+Clients decode the wire value as-is, as for pitch.
 
-Treat the roll line as a hypothesis until a bench acceptance run confirms all
-four directions:
+The check to repeat after any change to the accelerometer mounting or to the
+tilt path:
 
 | Action | Expected wire value |
 |---|---|
@@ -301,10 +303,6 @@ four directions:
 | Raise LEFT | roll POSITIVE |
 | Raise RIGHT | roll NEGATIVE |
 
-Once that passes, delete the PROVISIONAL marker on offset 4 and this
-paragraph, and the roll line becomes specification like pitch. Until then, do
-not build a client behaviour that depends on roll's sign.
-
 ### Protocol version and this change
 
 The pitch convention above **changed within protocol v1**. The byte layout did
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 0fc4b0c..8292ab6 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2253,6 +2253,17 @@ that produced the original stalls, and a desk bed at desk range (the van test
 at a realistic distance is in `docs/PRE_SHIP.md`). "Bed is level" (REGRESSION
 6.1) printed in none of the twelve runs; see the Prompt AC handoff, QUESTIONS.
 
+### Bench result (2026-11-02, Galaxy S23)
+
+Recorded 2026-11-03 (Prompt AH part B6). Andrew's bench run, firmware
+`b848e48` (`BLE_DEBUG_WATCHDOG_LOG` 1), Galaxy S23 on app `4e66328`, log
+`PuTTY Logs\round3_bench_2026-11-02.log` (425 lines). **VERIFIED (user's bench run).** All
+**62** `BLE: hold end` lines (55 `op=0x01`, 7 `op=0x03`) read `longest between
+passes 0 ms` (`grep -c 'hold end'` = 62; the same lines without `longest
+between passes 0 ms` = 0). Longest refresh gap per hold: 41-195 ms (the 195 ms
+is the AUTO LEVEL hold at line 364). No `BLE: WATCHDOG STOP` in the log. The
+mechanism question above is unchanged.
+
 ## 46. Bed Status flag bit 4 = LEVELED; "Bed is level" prints on every successful auto-level end
 
 Date: 2026-11-02. Reviewer's instruction (Prompt AE, part B), from the Prompt
@@ -2326,6 +2337,33 @@ Bluetooth stack callback.
 
 Builds clean (no warnings). **Not run on the bench.**
 
+### Bench result (2026-11-02)
+
+Recorded 2026-11-03 (Prompt AH part B2). Andrew's bench run, firmware
+`b848e48`, Galaxy S23 on app `4e66328`, log `PuTTY Logs\round3_bench_2026-11-02.log`.
+**VERIFIED (user's bench run)** for app auto-levels; the physical AUTO LEVEL
+button was not used in this run (no `SW: 0x0002` line).
+
+- **Six** successful app auto-levels (the prompt said four), each printing
+  `LEVELED (Bed Status bit 4) -> 1`, then `Exit auto-level`, then the
+  outcome's own line if any, then `Bed is level`, then its `BLE: hold end
+  op=0x03` line: lines 169-172 (both axes), 196-200 (`ROLL is disabled and
+  Pitch is level`), 219-223 (`PITCH is disabled and ROLL is level`), 245-252
+  (both axes), 361-364 (both axes), 410-417 (both axes). Both one-axis
+  outcomes are covered. `Bed is disabled, no leveling required` did not occur.
+- **`-> 0` at the next motion start:** line 173 (next app move, hold end at
+  174), 201 (next app AUTO LEVEL press: `Step 1` at 202, a run released after
+  8 refreshes at 203 without levelling), 224 (next move), 253 (next move, 254),
+  365 (next move). The last `-> 1` (410) has no following motion in the log.
+- No `-> 0` or `-> 1` line anywhere else (12 `LEVELED` lines in all, `grep
+  -n`).
+- No disconnect during any auto-level run: the six `connection_closed` lines
+  (91, 255, 262, 336, 418, 425) all read `reason=0x1013` and fall outside the
+  runs.
+
+REGRESSION 6.1 (the prints), 6.8 and 6.10b pass for the app path. Not
+covered: the physical button (6.10a), 6.10d, 6.10e, 6.10f.
+
 ## 47. BED LIGHTS toggles the lights on the release of a short press
 
 Date: 2026-11-02. User decision 2026-11-02, implemented per the reviewer's
@@ -2610,6 +2648,39 @@ sequence.
 **Status:** builds clean (no warnings) with `BLE_DEBUG_WATCHDOG_LOG` 1 and 0;
 host test unchanged and passes. **Not run on the bench.** REGRESSION 5.A3-5.A6.
 
+### Bench result (2026-11-02)
+
+Recorded 2026-11-03 (Prompt AH part B3). Andrew's bench run, firmware
+`b848e48` (#48 as amended by Prompt AG), Galaxy S23 on app `4e66328`, log
+`PuTTY Logs\round3_bench_2026-11-02.log`. **VERIFIED (user's bench run) for an app that ends its
+hold at the first 0x06.** One collision during an app MOVE hold (a RIGHT move:
+the hold's `Max Current` lines 309-312 read 4 / 19 / 1123 / 724, outputs C and
+D), lines 313-323:
+
+```
+313  Tripped on steady!
+314  Securit margin: 550
+315  Motor 0 Delta Current:  0
+316  Motor 1 Delta Current:  2
+317  Motor 2 Delta Current:  549
+318  Motor 3 Delta Current:  -24
+319  BLE: COLLISION_STOP seq=94 op=0x01 (latched)
+320  BLE: hold end op=0x01 seq=94: 21 refreshes, longest refresh gap 121 ms, longest between passes 0 ms
+321  Once
+322  BLE: collision latch cleared (stop), 1 repeats refused
+323  FAILSAFE: motors energised with nothing commanding - stopped
+```
+
+The trip (summed positive rise 0 + 2 + 549 = 551 > margin 550,
+`motor_current_functions.c:345-353`), the 0x06 with the hold's seq, the hold
+end in the same burst, `Once`, then the app's release (`stop`) after one
+refused repeat - as REGRESSION 5.A1 expects for an app that handles 0x06. The
+`FAILSAFE` line is PRE_SHIP F26 (observed; the log has no timestamps, so the
+length of the reverse is not known). **Not yet bench-tested:** the worst case,
+an app that keeps repeating while the finger is down (5.A1 as written), and
+5.A3-5.A6. The print text has since changed (`BLE: start latch (collision)
+cleared (...)`, #49).
+
 ## 49. STOP waits for release: a STOP_ALL latches app motion starts (STOP_LATCHED 0x07) until the app releases
 
 Date: 2026-11-03. Reviewer's Prompt AH part A, from Andrew's bench run of
diff --git a/docs/PRE_SHIP.md b/docs/PRE_SHIP.md
index a6e589e..79bf953 100644
--- a/docs/PRE_SHIP.md
+++ b/docs/PRE_SHIP.md
@@ -47,7 +47,7 @@ File:line references are to the firmware at `6796e6b` unless marked.
 | F23 | Auto-level uses the raise margin for the lowering motors | `is_raising` is forced true for auto-level (`bed_actions.c:170`; `auto_level.c:771`), so the two lowering motors are judged against the raise margin | Claude decides, Claude Code changes | Decision (keep with a recorded reason, or per-direction margins in auto-level); then REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 5; reviewer ruling, Prompt AE part E |
 | F24 | Unconditional console prints to review before release | With every flag at 0 the firmware still prints `BLE SM: configure/...` (`ble_bed_service.c:1089, 1101, 1109`), `connection_parameters` (`:1270`), `Max Current for Motor` (`motor_current_functions.c:198`), `Once`, and the auto-level step lines | Claude decides, Claude Code changes | List each print with keep / flag / remove; change; check a release-build log | Prompt AC handoff QUESTIONS 12; reviewer ruling, Prompt AE part E |
 | F25 | Hold 9 (2026-11-01 bench): the HEAD pair apparently did not run during PITCH levelling | `prompt_aa_bench.log` lines 201-204 show outputs 0 and 3 at 1 and 12 mA while FOOT ran; cause not established (an inferred limit refusing the pair, `dual_motor_control.c:353, 361`, is one candidate) | Andrew + Claude | A bench look with the limit prints enabled: repeat PITCH levelling from a HEAD-high start and record which pairs run and any limit lines | Prompt AC handoff B1(d), QUESTIONS 6; reviewer ruling, Prompt AE part E |
-| F26 | The runaway fail-safe cuts the collision reverse to ~150-200 ms after an app collision, and after a physical collision once the button is released | During the reverse both app motion flags are false and no switch is down, so `motion_failsafe_tick()` de-energises it after 3 samples of 50 ms (`switch_input.c:804-896`) and prints `FAILSAFE: ...`; the reverse is meant to relieve pressure on the obstruction (ASSERTED from code, not seen) | Andrew + Claude | First: observe the `FAILSAFE` line and the length of the reverse on the bench (REGRESSION 5.A1); then a design ruling (accept, or e.g. the fail-safe stands down while `any_collision` is set) | Prompt AF handoff b.11.1, QUESTIONS f.3; Prompt AG C4 |
+| F26 | What can cut the collision reverse short: (1) the runaway fail-safe, after an app collision and after a physical collision once the button is released; (2) any app release during the reverse (MOVE_STOP / AUTO_LEVEL_STOP / STOP_ALL) | (1) During the reverse both app motion flags are false and no switch is down, so `motion_failsafe_tick()` de-energises it after 3 samples of 50 ms (`switch_input.c:804-896`) and prints `FAILSAFE: ...`. (2) The app's stop verbs call `app_apply_stop_outputs()` / `app_stop_all()`, which turn every motor off. The reverse is meant to relieve pressure on the obstruction. **Observed 2026-11-02:** `FAILSAFE: motors energised with nothing commanding - stopped` after an app collision (`round3_bench_2026-11-02.log` line 323, after the trip at 313-322); the log has no timestamps, so how long the reverse ran is not known | Andrew + Claude | Bench: the length of the reverse (5.A1). Then a design ruling covering both (accept, or e.g. the fail-safe stands down while `any_collision` is set, and app stops do not cut the reverse) | Prompt AF handoff b.11.1, QUESTIONS f.3; Prompt AG C4; Prompt AG handoff f.4 folded in by the reviewer (Prompt AH B4, B7) |
 | F27 | A BED LIGHTS press stops an app move but does not latch the #36 inhibit | Lights are not motion and never latch (`bed_control.c:153-164`), so the app's next repeat can restart the move briefly while BED LIGHTS is held | Claude | First: the bench record REGRESSION 8.d; then a ruling | Prompt AE handoff QUESTIONS 6; Prompt AG C4 |
 | F28 | A collision flagged during a BED LIGHTS hold toggles `bedlights_on` at the release without writing the output | Not expected to happen (no motor runs during a BED LIGHTS hold), but the flag and the amber/lighting output would then disagree | Claude | Review before release | Prompt AE handoff QUESTIONS 8; Prompt AG C4 |
 
@@ -93,3 +93,4 @@ pre-ship item until the reviewer says so.
 | The lower-collision "OFF" rung (750) is not actually off | SETPOINTS_INVENTORY.md Note A |
 | Auto-level's pulsed ("sneak") phase runs with no current supervision. **Now row F21 (2026-11-02)** | Prompt AC handoff B1(d) (`auto_level.c:679-738`, `:770-781`) |
 | "Bed is level" never prints: `bed_level` is never set true. **Fixed 2026-11-02 (Prompt AE part B, DECISIONS #46); bench check REGRESSION 6.1 / 6.10** | Prompt AC handoff B8 (`auto_level.c:166,603`; `app.c:681`) |
+| Collision location (Phase 1b: per-motor share of the rise at a trip, a "last trip" record, a heat map of where collisions happen). **First data point, 2026-11-02:** an app RIGHT-up hold tripped `Tripped on steady!` with the whole rise on motor 2 (output C, in both the RIGHT and FOOT pairs, i.e. foot-right): deltas 0 / 2 / 549 / -24 for outputs A-D, margin 550 (`round3_bench_2026-11-02.log` lines 313-318); Andrew confirms he pushed on the foot-right area. The data already exists at the trip (`average_current_delta_single[]`); options for sending it are in the Prompt AC handoff B1(c). No Phase 1b record existed in this repo before this row (Prompt AH B5) | Prompt AC handoff B1(c); Prompt AH B5 |
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index 6eb3ae8..c1abc9b 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -264,6 +264,17 @@ reads `BLE: start latch (collision) cleared (<reason>), K repeats refused`
       latch (collision) cleared (silence)` ~300 ms after its last repeat (not
       `disconnect`), and nothing moves.
 
+**Recorded result, 2026-11-02 (firmware `b848e48`, `BLE_DEBUG_WATCHDOG_LOG`
+1, Galaxy S23 on app `4e66328`, log `round3_bench_2026-11-02.log`).**
+VERIFIED (user's bench run). **5.A1 passes for an app that ends its hold at
+the first 0x06**: lines 313-323 - `Tripped on steady!`, the four deltas,
+`BLE: COLLISION_STOP seq=94 op=0x01 (latched)`, `BLE: hold end op=0x01
+seq=94 ...`, `Once`, `BLE: collision latch cleared (stop), 1 repeats refused`
+(the pre-#49 text), `FAILSAFE: motors energised with nothing commanding -
+stopped` (PRE_SHIP F26). Full record: DECISIONS #48 "Bench result
+(2026-11-02)". **Not yet tested:** 5.A1 with an app that keeps repeating,
+5.A2-5.A6.
+
 ## 5b. App STOP latch (DECISIONS #49)
 
 App connected as CONTROL, `BLE_DEBUG_WATCHDOG_LOG` 1, UART logged, no
@@ -421,6 +432,18 @@ open question, Prompt AC handoff.
         at entry.
   - [ ] 6.10f With it set, STOP_ALL from the app: no LEVELED line (it stays 1).
 
+**Recorded result, 2026-11-02 (firmware `b848e48`, Galaxy S23 on app
+`4e66328`, log `round3_bench_2026-11-02.log`).** VERIFIED (user's bench run).
+Six successful **app** auto-levels, each printing `LEVELED (Bed Status bit 4)
+-> 1`, `Exit auto-level`, the outcome line if any, `Bed is level`: lines
+169-171, 196-199 (ROLL disabled), 219-222 (PITCH disabled), 245-247, 361-363,
+410-412. `-> 0` at the next motion start (173, 201, 224, 253, 365). No
+disconnect during any run. **6.1 (prints), 6.8 and 6.10b pass for the app
+path**; 6.10c is seen for app moves and an app AUTO LEVEL press. Every hold
+line reads `longest between passes 0 ms` (62 of 62; DECISIONS #45). Not
+covered: the physical AUTO LEVEL button (6.1 and 6.10a as written), 6.2-6.7,
+6.10d-6.10f. Full record: DECISIONS #46 "Bench result (2026-11-02)".
+
 ## 7. Settings mode
 
 - [ ] ⚡ 7.1 Hold BED_LIGHTS 5 s: amber LED fast-blinks — settings mode active.
@@ -546,6 +569,10 @@ bench run), except where marked as the user's report.
       every LED off and then amber back on, as app MOVE_STOP already does
       (DECISIONS #36 "The indicator blip").
 
+**Not yet tested (2026-11-02 bench):** the physical BED LIGHTS checks
+above, 8.a-8.d. That run used the app's Lights button only; its log has no
+`SW: 0x0008` (BED LIGHTS) line.
+
 ## 9. Cross-checks & UART health
 
 - [ ] No unexpected resets during the entire run (watch for repeated welcome
````

## d. Raw output of commands run

### d.1 How this log was produced

From d.2 #1 onward every shell command went through a logging wrapper
(`$S/run.sh`, below; `$S` = the session scratchpad), which runs the command in
the repo root and appends the command and its complete output, with the exit
code, to `cmdlog.txt`. d.2 is that file, unedited. The commands before the
wrapper existed, and a few run outside it, are in d.0 with their output.

**File-content reads are listed, not reproduced.** Many commands only printed
the contents of committed files (`cat`, `sed -n`, `head`, `tail`, and greps
whose output is lines of a file) to read them. Their output is the file
itself at the commit stated, so it is listed in d.0 with the file and range and
the marker *[file content, not reproduced]*; the facts taken from them are
cited by file:line in b and e. The bench log (`round3_bench_2026-11-02.log`)
is the reviewer's own upload; its cited lines are quoted in b.B. Everything
else - git, builds, sizes, the host test, counts, failures - is reproduced in
full. No output contained a secret; nothing is redacted. The Windows tools
(`arm-none-eabi-size`, ninja, the host-test executable, Python) end their lines
with CR LF; in this file those 28 line ends were normalised to LF, like every
other file in the repo. No other byte was changed.

```sh
#!/bin/sh
# Session command logger: runs "$*" in the repo root, prints and appends command + output to cmdlog.txt
S="C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/1d9c595f-3b1f-4e33-8117-f5eb676e543e/scratchpad"
N=$(( $(cat "$S/n.txt" 2>/dev/null || echo 0) + 1 )); echo $N > "$S/n.txt"
cd "C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller" || exit 99
{ echo "### #$N \$ $*"; sh -c "$*" 2>&1; echo "[exit $?]"; echo; } | tee -a "$S/cmdlog.txt"
```

### d.0 Commands run before the logging wrapper, and outside it

In the order run. `P n` numbers are used in e.

- **P1** `cat docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md && date && git log --oneline -3 && git status`
  - the prompt: *[file content, not reproduced - the committed prompt file]*
  - then:
    ```
    Tue Nov  3 08:00:49 PST 2026
    b848e48 Handoff 2026-11-02 03: Prompt AG (collision latch follow-ups: viewer STOP_ALL, refusals latch, trip arms silence)
    3d7d8e6 Collision latch follow-ups: viewer STOP_ALL keeps it, refusals latch, trip arms silence (DECISIONS #48, Prompt AG)
    b1991ed Handoff 2026-11-02 02: Prompt AF (collision latch on the app path, COLLISION_STOP 0x06)
    On branch main
    Your branch is up to date with 'origin/main'.

    Changes not staged for commit:
      (use "git add <file>..." to update what will be committed)
      (use "git restore <file>..." to discard changes in working directory)
    	modified:   .projectlinkstore

    Untracked files:
      (use "git add <file>..." to include in what will be committed)
    	docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md

    no changes added to commit (use "git add" and/or "git commit -a")
    ```
- **P2** `git fetch origin 2>&1; git rev-parse HEAD origin/main; ls; ls docs docs/handoffs | tail -30; ls "PuTTY Logs" 2>&1 | tail; ls ../"PuTTY Logs" 2>&1 | tail`
  ```
  b848e485dd87dea810f0ba06a7c569265a2d5990
  b848e485dd87dea810f0ba06a7c569265a2d5990
  AGENTS.md
  CLAUDE.md
  adc_currents.c
  adc_currents.h
  app.c
  app.h
  app_bm.c
  auto_level.c
  auto_level.h
  autogen
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
  cmake_gcc
  config
  debug_capture.h
  docs
  dual_motor_control.c
  dual_motor_control.h
  exp_board.c
  exp_board.h
  exp_ui.c
  exp_ui.h
  image
  main.c
  mc3479.c
  mc3479.h
  motor_current_functions.c
  motor_current_functions.h
  nvm3_functions.c
  nvm3_functions.h
  pi4ioe5v6416.h
  readme.md
  settings_mode.c
  settings_mode.h
  sl_gatt_service_device_information_override.c
  switch_input.c
  switch_input.h
  test
  tilt_bed_controller.pintool
  tilt_bed_controller.slcp
  tilt_bed_controller.slps
  tilt_bed_controller_project.pdf
  version.h
  vscode.conf
  SETPOINTS_INVENTORY.md
  SWITCH_SAMPLER_DESIGN.md
  handoffs
  prompts

  docs/handoffs:
  README.md
  fw_HANDOFF_2026-08-02_01_pitch-sign-tilt-sampling-plan.md
  fw_HANDOFF_2026-08-02_02_handoff-artifact-process-change.md
  fw_HANDOFF_2026-08-02_03_pitch-sign-item1-and-review-rulings.md
  fw_HANDOFF_2026-08-02_04_tilt-telemetry-and-command-write.md
  fw_HANDOFF_2026-08-02_05_stage-c-app-drives-bed.md
  fw_HANDOFF_2026-08-02_06_stutter-root-cause-and-arbitration.md
  fw_HANDOFF_2026-08-04_01_holdtorun-autolevel-and-conflict-latch.md
  fw_HANDOFF_2026-08-05_01_split-active-switch-and-runaway.md
  fw_HANDOFF_2026-08-05_02_double-press-misfire-and-failsafe.md
  fw_HANDOFF_2026-08-05_03_runaway-provocation.md
  fw_HANDOFF_2026-10-17_01_git-push-housekeeping.md
  fw_HANDOFF_2026-10-17_02_failsafe-hardware-read-and-double-press.md
  fw_HANDOFF_2026-10-17_03_single-switch-sampler-plan.md
  fw_HANDOFF_2026-10-17_04_status-check-only.md
  fw_HANDOFF_2026-10-17_05_one-switch-reader-sampler.md
  fw_HANDOFF_2026-10-18_01_switch-followups-settings-exit-pairing-window.md
  fw_HANDOFF_2026-10-18_02_bonding-policy-refused-close-settings-holds.md
  fw_HANDOFF_2026-10-23_01_watchdog-diagnostics-and-followups.md
  fw_HANDOFF_2026-11-01_01_sleep-veto-during-app-holds.md
  fw_HANDOFF_2026-11-01_02_aa-bench-preship-feature-report.md
  fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md
  fw_HANDOFF_2026-11-02_02_collision-latch-app-hold.md
  fw_HANDOFF_2026-11-02_03_collision-latch-followups.md
  ls: cannot access 'PuTTY Logs': No such file or directory
  autolevel_stall_bench.log
  prompt_aa_bench.log
  prompt_q_bench.log
  prompt_v_bench.log
  prompt_w_bench.log
  provoke_runaway.log
  round3_bench_2026-11-02.log
  sampler_bench.log
  ```
  (`git fetch` printed nothing. The `ls: cannot access` line is the failed
  first guess at the log folder, inside the repo; the folder is one level up.)
- **P3** `wc -l docs/*.md *.c *.h && cat docs/MIGRATION_STATUS.md | head -150` - line counts (e.g. `ble_bed_service.c` 1485, `DECISIONS.md` 2611, `SESSION_LOG.md` 1666, total 17188) and *[file content, not reproduced - MIGRATION_STATUS.md]*
- **P4** `grep -n "^## \|^### " docs/DECISIONS.md | tail -30` - *[file content (headings), not reproduced]*
- **P5** `sed -n 2256,2611p docs/DECISIONS.md` - *[file content, not reproduced - DECISIONS #46-#48 at b848e48]*
- **P6** `wc -l` and `grep -n "^#"` on the AG handoff - `2067` lines; *[file content (headings), not reproduced]*
- **P7** `sed -n 1,340p` of the AG handoff; **P8** `sed -n 340,480p; 592,690p; 1920,2067p` of it - *[file content, not reproduced]*
- **P9-P14** `sed -n 1,140p`, `280,720p`, `720,1000p`, `1000,1200p` of `ble_bed_service.c`, `cat ble_bed_service.h`, `grep -n "CMD_RESULT_\|stop_all_pending\|..." ble_bed_service.c`, `sed -n 160,280p; 1380,1440p` - *[file content, not reproduced - ble_bed_service.c/.h at b848e48]*
- **P15** `cat bed_control.c; sed -n 240,415p bed_actions.c`; **P16** `sed -n 225,245p bed_actions.c; grep -n "switch_held\|..." switch_input.c`; **P17** `sed -n 130,180p; 510,720p switch_input.c`; **P18** `sed -n 323,450p; 800,1031p switch_input.c`; **P19** `grep -n "auto_level_active\s*=\|..." *.c; sed -n 130,225p bed_actions.c` - *[file content, not reproduced]*
- **P20** `wc -l round3_bench_2026-11-02.log && file ... && grep -n -a "STOP_ALL\|LEVELED\|..." ...` - `425 round3_bench_2026-11-02.log`; `round3_bench_2026-11-02.log: ASCII text, with CRLF line terminators`; the grep lines are lines of the bench log *[the reviewer's upload; cited lines quoted in b.B]*. Then the whole log was read with the Read tool (not a shell command).
- **P21** `grep -n "^#" docs/BLE_GATT_DESIGN.md; grep -n -i "provisional\|roll sign\|kRollWireSign\|..." docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/REGRESSION.md docs/PRE_SHIP.md | head -40` - headings *[not reproduced]*; the provisional grep printed:
  ```
  docs/BLE_GATT_DESIGN.md:225:                                 POSITIVE = LEFT side raised
  docs/BLE_GATT_DESIGN.md:226:                                 ** PROVISIONAL - see "Tilt sign conventions" **
  docs/BLE_GATT_DESIGN.md:285:**Roll — PROVISIONAL, pending bench confirmation. `POSITIVE = LEFT raised`.**
  docs/BLE_GATT_DESIGN.md:301:| Raise LEFT | roll POSITIVE |
  docs/BLE_GATT_DESIGN.md:304:Once that passes, delete the PROVISIONAL marker on offset 4 and this
  docs/DECISIONS.md:17:  threshold. Provisional starting values from bench data (captured with an
  docs/DECISIONS.md:38:invalidate without any code appearing to change. This is why the provisional
  docs/DECISIONS.md:39:values in decision 1 (per-250-samples) are explicitly provisional.
  docs/DECISIONS.md:72:in the new detector come from calibrated-timebase data, not the provisional
  docs/DECISIONS.md:715:**Hang hypothesis explicitly refuted, not just unconfirmed.** Before landing
  ```
- **P22** `sed -n 207,420p docs/BLE_GATT_DESIGN.md`; **P23** `grep -n "^#\|5\.A[0-9]\|..." docs/REGRESSION.md`; **P24** `sed -n 1,35p; 172,356p; 454,482p docs/REGRESSION.md`; **P25** `cat docs/PRE_SHIP.md` - *[file content, not reproduced]*
- **P26** `grep -rn -i "heat.map\|heat-map\|heatmap\|phase 1b\|last.trip\|collision location" docs/*.md docs/handoffs/*.md docs/prompts/*.md | head -30`:
  ```
  docs/handoffs/fw_HANDOFF_2026-11-01_02_aa-bench-preship-feature-report.md:242:3. **A new characteristic** ("Last Trip", Read + Notify, e.g. UUID
  docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md:103:B5. The collision location idea has a first data point: in that trip the
  docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md:106:    this to the Phase 1b notes wherever the heat-map / last-trip record is
  ```
- **P27** `grep -n "^#"` and `sed -n 200,280p` of the Prompt AC handoff, `grep -n -i "phase 1\|1b\|location\|which motor\|where.*collision" docs/SESSION_LOG.md | tail -20` - *[file content, not reproduced]*; the SESSION_LOG grep printed two lines (`39:- \`1bfebd8\` (2026-07-17) — DECISIONS.md #29: ...`, `99:  1b2f8ee "350 samples within 303 ms" ...`), neither about a collision location.
- **P28** `grep -rn -i "phase 1\b\|phase 1a\|phase 1b\|phase 2\b" docs/SESSION_LOG.md docs/PRE_SHIP.md docs/DECISIONS.md docs/prompts/*.md | tail -20; grep -n -i "Last Trip\|B1(c)\|share of the rise" docs/SESSION_LOG.md docs/prompts/*.md | head`:
  ```
  docs/SESSION_LOG.md:1543:  (Phase 2); the held sensitivity button's repeat-stepping every 2 s stays.
  docs/prompts/PROMPT_AE_collision-result-leveled-bit-lights-release.md:1:# Prompt AE - Phase 1a: collision result, "levelled" status bit, amber fix, lights on release
  docs/prompts/PROMPT_AE_collision-result-leveled-bit-lights-release.md:117:ranges; Q10 erase-needs-reboot is fixed with the settings work (Phase 2),
  docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md:106:    this to the Phase 1b notes wherever the heat-map / last-trip record is
  docs/SESSION_LOG.md:1520:- All B1(c) / B3 options and their costs; the B7 timing estimates; that holding a sensitivity button > 4 s steps again; the amber-off-after-STOP_ALL behaviour on the bed; why the HEAD pair did not run in hold 9 (lines 201-204).
  ```
- **P29** `sed -n 1,30p; 105,125p` of the Prompt AE file - *[file content, not reproduced]*
- **P30** `cat app_bm.c main.c | grep -v ...; sed -n 500,575p app.c; grep -n "ble_bed_service_tick\|..." app.c`; **P31** `cat autogen/sl_power_manager_handler.c | head -120; ls autogen | head -50`; **P32** `sed -n 575,792p app.c` - *[file content, not reproduced]*
- (Edits to `ble_bed_service.c` with the editor tool; not shell commands.)
- **P33** creating `run.sh` and `build.sh` (heredocs), then `ls ~/.silabs/slt/installs/conan/p | grep -i "ninja\|cmake"; echo "$S"`:
  ```
  cmake94b5e5c99cc1e
  cmakefa35ab0687064
  ninja1a38fc85adcf7
  ninja6c55a7e9bcbcf
  C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/1d9c595f-3b1f-4e33-8117-f5eb676e543e/scratchpad
  ```
- **P34** `SIZE=$(find ~/.silabs /c/Users/adunc/.silabs -name "arm-none-eabi-size*" | head -1); echo "SIZE=$SIZE"` (same line as d.2 #4):
  `SIZE=/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe`
- **P35** `grep -n -A6 "### #13\|#13 " <AG handoff>; grep -n "bed_control_test" <AG handoff>; ls test` - *[file content, not reproduced]*; `ls test` printed `bed_control_test.c`.
- **P36** (Part A docs) `grep -n "STOP_ALL" docs/REGRESSION.md docs/BLE_GATT_DESIGN.md | grep -v ...`, `sed -n 95,178p docs/BLE_GATT_DESIGN.md` - *[file content, not reproduced]*
- **P37** the BLE_GATT_DESIGN Part A edit, a Python heredoc doing four exact-match replacements (each asserted to match once), then `grep -n "authoritative\|0x07" docs/BLE_GATT_DESIGN.md | head -20` - printed lines of the edited file *[file content, not reproduced; the change is c.2]*; then `sed -n 386,450p` to review it *[not reproduced]*.
- **P38** `tail -c 300 docs/DECISIONS.md | od -c | tail -5; head -12 docs/DECISIONS.md` *[file content]*; `cat >> docs/DECISIONS.md <<'EOF' ... EOF; tail -5 docs/DECISIONS.md` - appended #49 *[the change is c.2]*.
- **P39 - a failure.** The first attempt at the REGRESSION Part A edit, a Python heredoc inside the shell command:
  ```
  /usr/bin/bash: -c: line 103: unexpected EOF while looking for matching `''
  ```
  exit 2; nothing was written (the edit script never ran). The edit was then
  written to `$S/edit_regression_a.py` and run as **P40** `python -I edit_regression_a.py && grep -n "collision latch cleared\|^## 5b\|5b\.[0-9]\|5\.A[1-6] " docs/REGRESSION.md`, which printed `ok` and lines of the edited file *[not reproduced; the change is c.2]*.
- d.2 #10-#11 (commit A), #12, then **P41** `grep -n -i "pitch sign\|tilt sign\|sign convention\|wire sign\|POSITIVE = HEAD" docs/DECISIONS.md | head; grep -n "^## 3[1-3]\." docs/DECISIONS.md`:
  ```
  743:## 31. Auto-level's sample buffer must never be fed by the telemetry sampler — the two tilt values are deliberately separate
  802:## 32. Telemetry tilt needs its own filter — open design question against Item 2
  851:## 33. Live tilt telemetry — separate sampler, four triggers, and the first Command write path
  ```
  (the first grep matched nothing in DECISIONS.md).
- **P42** `sed -n 1,25p docs/SESSION_LOG.md; tail -60 docs/SESSION_LOG.md` - *[file content, not reproduced]*
- d.2 #13-#17, then **P43** `grep -n "^## 46\.\|^## 47\.\|^## 49\.\|..." docs/DECISIONS.md docs/REGRESSION.md; sed -n 2203,2256p docs/DECISIONS.md; grep -n "roll" bed_status.c | head` - *[file content, not reproduced]*
- **P44** `sed -i 's/motor_current_functions.c:345-352/motor_current_functions.c:345-353/' "$P" && python -I "$P" && git diff --stat && for f in docs/*.md; do ...CR count...; done; echo done` (`$P` = `$S/edit_docs_b.py`, the Part B doc edits):
  ```
  ok
  warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
  warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
  warning: in the working copy of 'docs/PRE_SHIP.md', LF will be replaced by CRLF the next time Git touches it
  warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
   .projectlinkstore       |  2 +-
   docs/BLE_GATT_DESIGN.md | 26 +++++++++---------
   docs/DECISIONS.md       | 71 +++++++++++++++++++++++++++++++++++++++++++++++++
   docs/PRE_SHIP.md        |  3 ++-
   docs/REGRESSION.md      | 27 +++++++++++++++++++
   5 files changed, 113 insertions(+), 16 deletions(-)
  done
  ```
  (no file printed a CR count, i.e. every `docs/*.md` has 0 CR.)
- **P45** `git diff docs/BLE_GATT_DESIGN.md docs/PRE_SHIP.md | head -90` - the diff, *[reproduced in full as part of c.3]*
- d.2 #18 (commit B). Then the Part C reads: **P46** `cat auto_level.h; sed -n 1,200p auto_level.c`; **P47** `sed -n 200,640p auto_level.c`; **P48** `sed -n 640,846p auto_level.c; grep -n "SCALE_FACTOR" *.h` (`bed_types.h:14:#define SCALE_FACTOR 100`); **P49** `grep -n "accel_get_xyz_raw" -A25 mc3479.c; grep -rn "I2CSPM.*SPEED\|..." config/sl_i2cspm_*config.h; sed -n 95,151p bed_status.c; grep -n "STATUS_NOTIFY..." ble_bed_service.c`; **P50** `sed -n 36,52p config/sl_i2cspm_on_board_config.h; grep -n "MC3479_transaction" -A22 mc3479.c; grep -n "ODR\|init_accel\|..." mc3479.c`; **P51** `sed -n 160,175p mc3479.c; grep -n "^## 1[7-9]\.\|..." docs/DECISIONS.md`; **P52** `sed -n 230,245p; 348,358p; 486,571p docs/DECISIONS.md`; **P53** `sed -n 631,742p docs/DECISIONS.md`; **P54** `sed -n 802,1000p docs/DECISIONS.md` - *[file content, not reproduced]*
- d.2 #19-#21, then **P55** `sed -n 205,225p; 370,390p app.c; grep -n "AUTO_LEVEL_SWITCH_TIME\|..." app.c; grep -n "AUTO_LEVEL\b\|BED_COMMAND_AUTO_LEVEL" bed_status.c` - *[file content, not reproduced]*
- d.2 #22-#24 (build after the commits; diffs saved for c - `git show 48c5cf6 -- ble_bed_service.c > c1.diff` etc., then `wc -l`: `279 c1.diff`, `390 c2.diff`, `244 c3.diff`, `913 total`; date; status). SESSION_LOG entry appended (`cat >> docs/SESSION_LOG.md <<'EOF'`), no output.
- d.2 #25-#26 (checking this file's citations). **P56 - a failure.** A Python heredoc to correct eleven citations in this file's draft stopped at its own check, before writing anything:
  ```
  Traceback (most recent call last):
    File "<stdin>", line 18, in <module>
  AssertionError: (0, 'turns\n   the motors on at once (`Leveling...`, `auto_level.c')
  ```
  The same command's `sed -n 54,64p bed_actions.c` printed *[file content,
  not reproduced]*. **P57** `grep -n ":650\|1146-1159\|..." h_part1.md`
  (lines of this draft) and **P58** eleven line-addressed `sed -i`
  substitutions on the draft, then a `grep -c` of the new citations, which
  printed `11`.
- The handoff was then assembled from the draft parts, the saved diffs and
  `cmdlog.txt` (`cat`), committed with the SESSION_LOG entry, and pushed; that
  output is reported to the user, not here (g).

### d.2 Session command log (the wrapper, numbered)

````text
### #1 $ git rev-parse HEAD origin/main HEAD^ && git status --short
b848e485dd87dea810f0ba06a7c569265a2d5990
b848e485dd87dea810f0ba06a7c569265a2d5990
3d7d8e6532ba299e9e08d29bc3861dfcef386ae7
 M .projectlinkstore
 M ble_bed_service.c
?? docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md
[exit 0]

### #2 $ grep -n 'app_collision_latched\|wd_diag_collision_refused\|app_start_latch\|latch_app_starts\|note_app_stopped_commanding(\|note_app_commanding()\|app_commanding = false\|CMD_RESULT_STOP_LATCHED\|STOP_ALL latch set\|start latch (' ble_bed_service.c
179:#define CMD_RESULT_STOP_LATCHED               0x07 // DECISIONS #49
285:// App start latch (DECISIONS #48, #49): motion starts refused with
287:// cleared" line, latched by note_app_stopped_commanding() and printed by
339:// note_app_stopped_commanding(). STOP_ALL never clears it.
347:// Set ONLY through latch_app_starts(), which also starts the silence timeout,
353:} app_start_latch_t;
354:static app_start_latch_t app_start_latch = APP_LATCH_NONE;
361:static bool     app_commanding = false;
582:// Sets the app start latch (#48, #49) and restarts the silence timeout in the
583:// same call. The latch is set ONLY here: with note_app_stopped_commanding()
588:static void latch_app_starts(app_start_latch_t reason)
590:  if (app_start_latch != APP_LATCH_COLLISION) {
591:    app_start_latch = reason;
593:  note_app_commanding();
598:// the app start latch (#48, #49). reason is only for the bench print.
599:static void note_app_stopped_commanding(const char *reason)
601:  app_commanding = false;
604:  if (app_start_latch != APP_LATCH_NONE) {
606:    wd_diag_latch_clear_set_reason = (app_start_latch == APP_LATCH_STOP) ? "stop" : "collision";
614:  app_start_latch = APP_LATCH_NONE;
627:    note_app_stopped_commanding("silence");
648:    // latch stays as it is. latch_app_starts() restarts the silence
653:    latch_app_starts(APP_LATCH_COLLISION);
694:  note_app_stopped_commanding("watchdog");
756:    latch_app_starts(APP_LATCH_STOP);
759:    printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
760:           (app_start_latch == APP_LATCH_COLLISION) ? ", kept as collision" : "");
823:  // App start latch (#48, #49). Motion-starting opcodes only, and before the
838:  // latch_app_starts() restarts the silence timeout as in the #36 refusal: a
843:      && (app_start_latch != APP_LATCH_NONE || any_collision)) {
844:    if (app_start_latch == APP_LATCH_STOP) {
845:      latch_app_starts(APP_LATCH_STOP);
846:      send_command_result(conn, seq, CMD_RESULT_STOP_LATCHED);
848:      latch_app_starts(APP_LATCH_COLLISION);
863:  // Note note_app_commanding() BEFORE the refusal. A refused repeat still
871:    note_app_commanding();
886:      note_app_commanding();
924:      note_app_stopped_commanding("stop");
937:      note_app_commanding();
963:      note_app_stopped_commanding("stop");
1148:    note_app_stopped_commanding("disconnect"); // the app is gone - release its half of the latch
1170:  // The app start latch (#48, #49) was released above; the stop that released
1174:    printf("BLE: start latch (%s) cleared (%s), %u repeats refused\r\n",
[exit 0]

### #3 $ ls -la cmake_gcc/build/base/*.out 2>/dev/null || find cmake_gcc/build -name '*.out' | head
-rw-r--r-- 1 adunc 197610 2089340 Nov  2 09:46 cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

### #4 $ '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
   text	   data	    bss	    dec	    hex	filename
 245620	    784	  72476	 318880	  4dda0	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

### #5 $ grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h && sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/1d9c595f-3b1f-4e33-8117-f5eb676e543e/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
36:#define BLE_DEBUG_WATCHDOG_LOG 1
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 245732	    788	  72472	 318992	  4de10	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

### #6 $ sed -i 's/^#define BLE_DEBUG_WATCHDOG_LOG 1$/#define BLE_DEBUG_WATCHDOG_LOG 0/' ble_bed_service.h && grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h && sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/1d9c595f-3b1f-4e33-8117-f5eb676e543e/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
36:#define BLE_DEBUG_WATCHDOG_LOG 0
[1/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[2/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 243956	    780	  72480	 317216	  4d720	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]

### #7 $ sed -i 's/^#define BLE_DEBUG_WATCHDOG_LOG 0$/#define BLE_DEBUG_WATCHDOG_LOG 1/' ble_bed_service.h && grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h && sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/1d9c595f-3b1f-4e33-8117-f5eb676e543e/scratchpad/build.sh' && '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out && git status --short
36:#define BLE_DEBUG_WATCHDOG_LOG 1
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 245732	    788	  72472	 318992	  4de10	cmake_gcc/build/base/tilt_bed_controller.out
 M .projectlinkstore
 M ble_bed_service.c
?? docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md
[exit 0]

### #8 $ which gcc; cd test && gcc -std=c99 -Wall -I.. -o 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/1d9c595f-3b1f-4e33-8117-f5eb676e543e/scratchpad/bed_control_test.exe' bed_control_test.c ../bed_control.c && 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/1d9c595f-3b1f-4e33-8117-f5eb676e543e/scratchpad/bed_control_test.exe' | tail -8
/c/MinGW/bin/gcc
PASS: scenario 5f: app still refused while it alone is still holding
PASS: scenario 5g: latch clears once BOTH sources have released
PASS: scenario 5h: a fresh press after both released starts normally
PASS: scenario 6a: inhibited
PASS: scenario 6b: bed_move_stop still works while inhibited
PASS: scenario 6c: lights are not motion and are not inhibited

0 check(s) failed
[exit 0]

### #9 $ grep -n 'if (stop_all_pending)\|bed_note_source_released(BED_SOURCE_APP); // never\|app_stop_all();  \|latch_app_starts(APP_LATCH\|static void latch_app_starts\|static void note_app_stopped_commanding\|static void service_app_commanding\|if (conn != control_connection)\|if (app_in_settings_mode())\|if (bed_conflict_inhibited()\|note_app_stopped_commanding("\|if (bed_active_source() == BED_SOURCE_APP)\|process_pending_command();\|service_move_watchdog();\|service_app_commanding();\|printf("BLE: STOP_ALL\|printf("BLE: start latch\|CMD_RESULT_STOP_LATCHED\|typedef enum\|move_watchdog_arm();' ble_bed_service.c
179:#define CMD_RESULT_STOP_LATCHED               0x07 // DECISIONS #49
349:typedef enum {
589:static void latch_app_starts(app_start_latch_t reason)
600:static void note_app_stopped_commanding(const char *reason)
621:static void service_app_commanding(void)
628:    note_app_stopped_commanding("silence");
654:    latch_app_starts(APP_LATCH_COLLISION);
695:  note_app_stopped_commanding("watchdog");
738:  if (stop_all_pending) {
746:    bed_note_source_released(BED_SOURCE_APP); // never inhibited: releases the app's half of the #36 latch
747:    app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
757:    latch_app_starts(APP_LATCH_STOP);
760:    printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
814:  if (conn != control_connection) {
819:  if (app_in_settings_mode()) {
846:      latch_app_starts(APP_LATCH_STOP);
847:      send_command_result(conn, seq, CMD_RESULT_STOP_LATCHED);
849:      latch_app_starts(APP_LATCH_COLLISION);
870:  if (bed_conflict_inhibited()
914:      move_watchdog_arm();
925:      note_app_stopped_commanding("stop");
954:      move_watchdog_arm();
964:      note_app_stopped_commanding("stop");
1047:    if (bed_active_source() == BED_SOURCE_APP) {
1149:    note_app_stopped_commanding("disconnect"); // the app is gone - release its half of the latch
1157:  process_pending_command();
1158:  service_move_watchdog();
1159:  service_app_commanding();
1175:    printf("BLE: start latch (%s) cleared (%s), %u repeats refused\r\n",
[exit 0]

### #10 $ for f in ble_bed_service.c docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/REGRESSION.md; do printf '%s CR=' $f; tr -cd '\r' < $f | wc -c; done; git diff --stat; grep -n 'define BED_BENCH_PROVOKE_RUNAWAY\|define BLE_BENCH_WIPE_BONDS\|define BED_CAPTURE_MOTOR_CURRENTS\|define BLE_DEBUG_WATCHDOG_LOG' switch_input.c ble_bed_service.c debug_capture.h ble_bed_service.h
ble_bed_service.c CR=0
docs/BLE_GATT_DESIGN.md CR=0
docs/DECISIONS.md CR=0
docs/REGRESSION.md CR=0
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
 .projectlinkstore       |   2 +-
 ble_bed_service.c       | 155 ++++++++++++++++++++++++++++++------------------
 docs/BLE_GATT_DESIGN.md |  55 +++++++++++++----
 docs/DECISIONS.md       |  93 +++++++++++++++++++++++++++++
 docs/REGRESSION.md      | 133 ++++++++++++++++++++++++++++++-----------
 5 files changed, 334 insertions(+), 104 deletions(-)
switch_input.c:98:#define BED_BENCH_PROVOKE_RUNAWAY 0
ble_bed_service.c:70:#define BLE_BENCH_WIPE_BONDS 0
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
[exit 0]

### #11 $ git add ble_bed_service.c docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/REGRESSION.md docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md && git status --short && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/1d9c595f-3b1f-4e33-8117-f5eb676e543e/scratchpad/msgA.txt' && git log --oneline -1 && git show --stat HEAD | tail -8
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  ble_bed_service.c
M  docs/BLE_GATT_DESIGN.md
M  docs/DECISIONS.md
M  docs/REGRESSION.md
A  docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md
48c5cf6 STOP waits for release: STOP_ALL latches app starts, STOP_LATCHED 0x07 (DECISIONS #49, Prompt AH part A)
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

 ble_bed_service.c                                  | 155 +++++++++++++--------
 docs/BLE_GATT_DESIGN.md                            |  55 ++++++--
 docs/DECISIONS.md                                  |  93 +++++++++++++
 docs/REGRESSION.md                                 | 133 +++++++++++++-----
 .../PROMPT_AH_stop-latch-and-live-level-report.md  | 149 ++++++++++++++++++++
 5 files changed, 482 insertions(+), 103 deletions(-)
[exit 0]

### #12 $ grep -n -i 'roll' docs/DECISIONS.md | grep -i 'sign\|provisional\|confirm\|LEFT\|positive' ; grep -n -i 'roll.*sign\|sign.*roll\|kRollWireSign' docs/*.md | grep -v '^docs/DECISIONS.md'
699:## 30. Auto-level's BLE-drop bug — root cause confirmed by controlled A/B, fixed
706:(`connection_closed reason=0x1008`). Confirmed by a controlled A/B on a
docs/BLE_GATT_DESIGN.md:290:found the pitch sign wrong did not report on roll at all, and silence is not
docs/BLE_GATT_DESIGN.md:306:not build a client behaviour that depends on roll's sign.
docs/SESSION_LOG.md:451:  it. The bench run that caught the pitch sign never checked roll, and that
docs/SESSION_LOG.md:474:- **...that roll's sign is established.** It is a hypothesis with a marker on
[exit 0]

### #13 $ grep -rn 'define SWITCH_BED_LIGHTS\b\|define SWITCH_AUTO_LEVEL\b\|define SWITCH_UP_ALL\b\|define SWITCH_DOWN_ALL\b\|define SWITCH_UP_HEAD\b\|define SWITCH_DOWN_HEAD\b' *.h; grep -n 'case HEAD\|case LEFT\|case RIGHT\|case FOOT\|motor_state\[[0-3]\]\|MOTOR_[A-D]\|set_motor_on(' dual_motor_control.c | sed -n 1,60p
app.h:115:#define SWITCH_UP_ALL      0x0001
app.h:116:#define SWITCH_AUTO_LEVEL  0x0002
app.h:117:#define SWITCH_DOWN_ALL    0x0004
app.h:118:#define SWITCH_BED_LIGHTS  0x0008
351:    case HEAD:
354:          set_motor_on(MOTORA);
357:          set_motor_on(MOTORD);
362:          set_motor_on(MOTORA);
365:          set_motor_on(MOTORD);
370:    case LEFT:
373:          set_motor_on(MOTORA);
376:          set_motor_on(MOTORB);
381:          set_motor_on(MOTORA);
384:          set_motor_on(MOTORB);
389:    case RIGHT:
392:          set_motor_on(MOTORC);
395:          set_motor_on(MOTORD);
400:          set_motor_on(MOTORC);
403:          set_motor_on(MOTORD);
408:    case FOOT:
411:          set_motor_on(MOTORB);
414:          set_motor_on(MOTORC);
419:          set_motor_on(MOTORB);
422:          set_motor_on(MOTORC);
433:    case HEAD:
440:    case LEFT:
447:    case RIGHT:
454:    case FOOT:
467:    case HEAD:
474:    case LEFT:
481:    case RIGHT:
488:    case FOOT:
501:    case HEAD:
508:    case LEFT:
515:    case RIGHT:
522:    case FOOT:
[exit 0]

### #14 $ grep -rn 'MOTORA\b.*[0-9]\|MOTORB\b.*[0-9]\|MOTORC\b.*[0-9]\|MOTORD\b.*[0-9]' *.h | head; grep -n 'Delta Current\|Tripped on steady\|Securit margin' motor_current_functions.c; grep -c 'hold end' '../PuTTY Logs/round3_bench_2026-11-02.log'; grep 'hold end' '../PuTTY Logs/round3_bench_2026-11-02.log' | grep -vc 'longest between passes 0 ms'; grep -c 'SW: 0x0008' '../PuTTY Logs/round3_bench_2026-11-02.log'; grep -n 'Bed is level\|connection_closed\|WATCHDOG\|Step 1' '../PuTTY Logs/round3_bench_2026-11-02.log'
358:                printf("Tripped on steady!\r\n");
359:                printf("Securit margin: %d\r\n", security_margin);
362:                printf("Motor %d Delta Current:  %ld\r\n", i, average_current_delta_single[i]);
62
0
0
91:BLE: connection_closed conn=1 reason=0x1013
156:Step 1: ROLL and PITCH check
171:Bed is level
186:Step 1: ROLL and PITCH check
199:Bed is level
202:Step 1: ROLL and PITCH check
209:Step 1: ROLL and PITCH check
222:Bed is level
230:Step 1: ROLL and PITCH check
247:Bed is level
255:BLE: connection_closed conn=2 reason=0x1013
262:BLE: connection_closed conn=3 reason=0x1013
336:BLE: connection_closed conn=1 reason=0x1013
343:Step 1: ROLL and PITCH check
363:Bed is level
397:Step 1: ROLL and PITCH check
412:Bed is level
418:BLE: connection_closed conn=2 reason=0x1013
425:BLE: connection_closed conn=3 reason=0x1013
[exit 0]

### #15 $ grep -rn -B2 -A6 'MOTORA' *.h | head -30; sed -n 340,372p motor_current_functions.c
dual_motor_control.h-20-
dual_motor_control.h-21-// Motor assignment variables
dual_motor_control.h:22:extern uint8_t MOTORA;
dual_motor_control.h-23-extern uint8_t MOTORB;
dual_motor_control.h-24-extern uint8_t MOTORC;
dual_motor_control.h-25-extern uint8_t MOTORD;
dual_motor_control.h-26-
dual_motor_control.h-27-// Auto_level motors on or off status
dual_motor_control.h-28-extern bool auto_level_motors_on;
            security_margin = raise_collision_current_security_margin;
        }
        // Step 1: Calculate individual deltas and total delta sum for either steady or fast collision detection
        if (dynamic_collision_detection_active){
            for (uint8_t i = 0; i < NUM_MOTORS; i++) {
                average_current_delta_single[i] = (average_current_single_reading[i] - max_current_threshold_set[i]);
                if (average_current_delta_single[i] > 0) {
                    average_current_delta_sum += average_current_delta_single[i];
                }
            }
        }

        // Step 2: Check for collision based on total of all currents
        if (average_current_delta_sum > (security_margin)) {
#if BED_CAPTURE_MOTOR_CURRENTS
            printf("#COLLISION,%lu\r\n", (unsigned long)capture_now_ms());
#else
            if (dynamic_collision_detection_active){
                printf("Tripped on steady!\r\n");
                printf("Securit margin: %d\r\n", security_margin);
            }
            for (uint8_t i = 0; i < NUM_MOTORS; i++) {
                printf("Motor %d Delta Current:  %ld\r\n", i, average_current_delta_single[i]);
            }
#endif

            set_all_motors_off();
            any_collision = true;

            // Reset motor readings
            reset_ignore_data_points_count();
            reset_motor_current_data();
        }
[exit 0]

### #16 $ grep -n 'uint8_t MOTOR[A-D]' dual_motor_control.c; grep -n -i 'corner\|head.left\|foot.right\|output C\|motor C' docs/HARDWARE_CONFIG.md | head
35:uint8_t MOTORA = OUTPUT0;
36:uint8_t MOTORB = OUTPUT1;
37:uint8_t MOTORC = OUTPUT2;
38:uint8_t MOTORD = OUTPUT3;
25:| IADC (emlib IADC peripheral) | (singleton, no instance) | Motor current sensing (`adc_currents.c`) |
103:reality, and re-run the REGRESSION.md §2b per-motor checks.
[exit 0]

### #17 $ L='../PuTTY Logs/round3_bench_2026-11-02.log'; grep -c 'hold end op=0x01' "$L"; grep -n 'hold end op=0x03' "$L"; grep -n 'LEVELED' "$L"; grep -o 'longest refresh gap [0-9]* ms' "$L" | sort -t' ' -k4 -n | sed -n '1p;$p'
55
172:BLE: hold end op=0x03 seq=200: 82 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
200:BLE: hold end op=0x03 seq=32: 56 refreshes, longest refresh gap 150 ms, longest between passes 0 ms
203:BLE: hold end op=0x03 seq=42: 8 refreshes, longest refresh gap 120 ms, longest between passes 0 ms
223:BLE: hold end op=0x03 seq=96: 38 refreshes, longest refresh gap 120 ms, longest between passes 0 ms
252:BLE: hold end op=0x03 seq=161: 39 refreshes, longest refresh gap 122 ms, longest between passes 0 ms
364:BLE: hold end op=0x03 seq=72: 71 refreshes, longest refresh gap 195 ms, longest between passes 0 ms
417:BLE: hold end op=0x03 seq=180: 37 refreshes, longest refresh gap 151 ms, longest between passes 0 ms
169:LEVELED (Bed Status bit 4) -> 1
173:LEVELED (Bed Status bit 4) -> 0
196:LEVELED (Bed Status bit 4) -> 1
201:LEVELED (Bed Status bit 4) -> 0
219:LEVELED (Bed Status bit 4) -> 1
224:LEVELED (Bed Status bit 4) -> 0
245:LEVELED (Bed Status bit 4) -> 1
253:LEVELED (Bed Status bit 4) -> 0
361:LEVELED (Bed Status bit 4) -> 1
365:LEVELED (Bed Status bit 4) -> 0
410:LEVELED (Bed Status bit 4) -> 1
longest refresh gap 41 ms
longest refresh gap 195 ms
[exit 0]

### #18 $ git add docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/PRE_SHIP.md docs/REGRESSION.md && git status --short && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/1d9c595f-3b1f-4e33-8117-f5eb676e543e/scratchpad/msgB.txt' && git log --oneline -3 && git show --stat HEAD | tail -6
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/PRE_SHIP.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  docs/BLE_GATT_DESIGN.md
M  docs/DECISIONS.md
M  docs/PRE_SHIP.md
M  docs/REGRESSION.md
169b2f7 Bench record of 2026-11-02 and rulings (Prompt AH part B, docs only)
48c5cf6 STOP waits for release: STOP_ALL latches app starts, STOP_LATCHED 0x07 (DECISIONS #49, Prompt AH part A)
b848e48 Handoff 2026-11-02 03: Prompt AG (collision latch follow-ups: viewer STOP_ALL, refusals latch, trip arms silence)

 docs/BLE_GATT_DESIGN.md | 26 +++++++++---------
 docs/DECISIONS.md       | 71 +++++++++++++++++++++++++++++++++++++++++++++++++
 docs/PRE_SHIP.md        |  3 ++-
 docs/REGRESSION.md      | 27 +++++++++++++++++++
 4 files changed, 112 insertions(+), 15 deletions(-)
[exit 0]

### #19 $ grep -n 'AUTO_LEVEL_TIMER\|auto_level_oneshot\|app_reset_auto_level_timer' app.c bed_actions.c | head; grep -n 'define AUTO_LEVEL\|TIMER_TIME\|_MS ' app.c | head; sed -n 60,101p mc3479.c; sed -n 339,355p mc3479.c; grep -n '1500\|clamp\|BASELINE' bed_settings.c | head; grep -n 'baseline_.*_angle = \|returnAccelValue' settings_mode.c app.c nvm3_functions.c | head; python -c 'import struct;print(int(0.01*100), int(0.1*100), int(0.3*100), repr(0.01*100), repr(0.1*100), repr(0.3*100))'
app.c:216:static void start_auto_level_oneshot_timer()
app.c:378:void app_reset_auto_level_timer(void)
app.c:706:                start_auto_level_oneshot_timer();
bed_actions.c:200:  app_reset_auto_level_timer();
bed_actions.c:215:  app_reset_auto_level_timer();
530:  // Bounded: the watchdog disarms within MOVE_WATCHDOG_MS of the app's last

/*
 * Low pass filter bandwidth and decimation local - only used for initialization
 *
 * G-range setting is in upper nibble of register and the LPF set call assumes
 * entire register is written with a default range of 2g with and LPF enabled.
 *
 * Datasheet Section 12.13, pg 54 - for register and low pass filter settings
 */

typedef enum {
  MC3479_LPF_BW_1   = 0x09, // Fc = IDR / 4.255
  MC3479_LPF_BW_2   = 0x0A, // Fc = IDR / 6
  MC3479_LPF_BW_3   = 0x0B, // Fc = IDR / 12
  MC3479_LPF_BW_5   = 0x0D  // Fc = IDR / 16
} MC3479_lpf_bandwidth_t;

/*
 * FIFO settings in upper nibble of register and the decimation set call assumes
 * entire register is written with the power-on reset FIFO settings of 0000bbbb.
 *
 * Datasheet Section 12.23, pg. 65 - for register and decimation settings
 */

typedef enum {
  MC3479_DECIMATE_NONE   = 0x00,
  MC3479_DECIMATE_2      = 0x01,
  MC3479_DECIMATE_4      = 0x02,
  MC3479_DECIMATE_5      = 0x03,
  MC3479_DECIMATE_8      = 0x04,
  MC3479_DECIMATE_10     = 0x05,
  MC3479_DECIMATE_16     = 0x06,
  MC3479_DECIMATE_20     = 0x07,
  MC3479_DECIMATE_40     = 0x08,
  MC3479_DECIMATE_67     = 0x09,
  MC3479_DECIMATE_80     = 0x0A,
  MC3479_DECIMATE_100    = 0x0B,
  MC3479_DECIMATE_200    = 0x0C,
  MC3479_DECIMATE_250    = 0x0D,
  MC3479_DECIMATE_500    = 0x0E,
  MC3479_DECIMATE_1000   = 0x0F
} MC3479_decimation_t;
void init_accel()
{
  /*
   * Accelerometer power up defaults to 2g range in STANDBY mode.
   * All other functionality (sampling rate, filtering, FIFO, interrupts, etc.)
   * must be set.
   *
   * Set internal sample rate to maximum, decimate by 10 for 100 Hz output rate,
   * and set low pass filtering bandwidth to approximately 80 Hz. Can also
   * configure FIFO or motion sensing registers.
   */

  accel_set_sample_rate(MC3479_SAMPLE_RATE_1000HZ); // Maximum internal sample rate
  accel_set_decimation(MC3479_DECIMATE_10); // Output registers update at 100 Hz

  accel_set_lpf_bw(MC3479_LPF_BW_3); // Filter corner at 1000/12 ~= 80 Hz
}
35:  { CUSTOM_ROLL_ANGLE_KEY,           0,   -1500, 1500,  &baseline_roll_angle,                     "baseline roll angle"    },
36:  { CUSTOM_PITCH_ANGLE_KEY,          0,   -1500, 1500,  &baseline_pitch_angle,                    "baseline pitch angle"   },
settings_mode.c:347:            if (!bed_settings_set(BED_SETTING_BASELINE_ROLL_ANGLE, returnAccelValue(ROLL))) {
settings_mode.c:358:            if (!bed_settings_set(BED_SETTING_BASELINE_PITCH_ANGLE, returnAccelValue(PITCH))) {
settings_mode.c:374:                printf("Stored ROLL value is: %d\r\n", returnAccelValue(ROLL));
settings_mode.c:375:                printf("Stored PITCH value is: %d\r\n", returnAccelValue(PITCH));
1 10 30 1.0 10.0 30.0
[exit 0]

### #20 $ grep -n 'accel_running_average()\|read_accel()\|tilt_telemetry_tick(\|tilt_telemetry_arm_burst()' *.c; grep -rn 'define MC3479_I2C_DEVICE\|define MC3479_I2C_BUS_ADDRESS' *.h *.c; grep -n 'encode_status(&status' ble_bed_service.c; grep -n 'auto_level_timer_timeout = false' app.c bed_actions.c
app.c:554:  // - tilt_telemetry_tick() will not arm a burst otherwise - so an idle bed
app.c:627:      accel_running_average(); // Accel data is always being read, filtered, and averaged during auto leveling
app.c:767:  //     running, because those already call accel_running_average() every
app.c:769:  tilt_telemetry_tick(ble_bed_service_status_subscribed(),
auto_level.c:241:void read_accel(){
auto_level.c:292:  // caller filters into its OWN state (DECISIONS #31) - read_accel() is now
auto_level.c:301:// read_accel() last produced) and the user's stored baseline; writes nothing
auto_level.c:304:// This is a line-for-line lift of what accel_running_average() did inline
auto_level.c:333:void accel_running_average(){
auto_level.c:338:      read_accel();
auto_level.c:447:void tilt_telemetry_tick(bool status_subscribed, bool other_sampler_active)
auto_level.c:466:    tilt_telemetry_arm_burst(); // T3
auto_level.c:470:  // accel_running_average() every pass; running this as well would put two
auto_level.c:482:  read_accel();
auto_level.c:506:// do not "correct" it here (see the warning at read_accel()).
auto_level.c:786:  //  accel_running_average();
bed_status.c:99:  //       Those paths call accel_running_average() every pass and the
ble_bed_service.c:38:#include "auto_level.h"              // tilt_telemetry_arm_burst() - triggers T1 and T2
ble_bed_service.c:478:    tilt_telemetry_arm_burst();
ble_bed_service.c:805:    tilt_telemetry_arm_burst();
exp_board.c:338:// accelerometer's (auto_level.c's accel_running_average() does one I2C
settings_mode.c:322:            accel_running_average();
mc3479.c:21:#define MC3479_I2C_DEVICE                (sl_i2cspm_on_board)  // I2C device used to control the MC3479 Tri-Axial Accelerometer
mc3479.c:22:#define MC3479_I2C_BUS_ADDRESS           0x4C               // I2C bus address
1189:  ble_bed_service_encode_status(&status, false, packed); // tilt_fresh: Stage B5
app.c:137:static bool auto_level_timer_timeout = false;
[exit 0]

### #21 $ grep -n 'define ACCEL_I2C_MIN_INTERVAL_MS\|define TILT_TELEMETRY_BURST_MS\|define NUM_ACCEL_READINGS\|define TOLERANCE\|define CLOSE_TOLERANCE\|define PULSE_START_TOLERANCE\|define STEADY_ACCEL_READ_TIMER_TIME\|define SNEAK_MOTOR\|^int16_t tolerance\|^int16_t close_tolerance\|^int16_t pulse_start\|accel_alpha = \|^void read_accel\|^static void accel_filter_update\|^void accel_running_average\|^void tilt_telemetry_tick\|^void tilt_telemetry_arm_burst\|^void check_level\|^void steady_accel_read_both_axes\|^void auto_level_single_axis\|^void level_axis_pulse\|^void level_axis \|start_steady_accel_read_oneshot_timer();\|first_check = false;\|axis_disabled\[i\] = \|if (!motors_on && !tilt_burst_running)\|if (other_sampler_active)\|if (!status_subscribed)' auto_level.c; grep -n 'define ACCEL_TIMER_DURATION\|^bool app_is_ok_to_sleep\|tilt_telemetry_burst_active()) {\|ble_bed_service_app_hold_active()) {\|if ((auto_level_active) && (!any_collision)\|steady_accel_read_both_axes();\|Bed is disabled, no leveling\|auto_level_single_axis(auto_level_current_axis)\|start_auto_level_oneshot_timer();\|settings_mode_tick();\|switch_input_tick();\|motion_failsafe_tick();\|tilt_telemetry_tick(\|bed_control_tick();\|ble_bed_service_tick();' app.c; grep -n 'first_check = true\|app_set_leveled(false); // a new run\|reset_level_data();' bed_actions.c; grep -n 'BED_COMMAND_AUTO_LEVEL\|case SWITCH_AUTO_LEVEL_VALUE' bed_status.c; grep -n 'define STATUS_NOTIFY\|^static bool status_flags_changed\|^static void publish_status\|tilt_telemetry_arm_burst();\|if (previous == 0 && count > 0)\|if (status_notify_due || status_flags_changed' ble_bed_service.c; grep -n 'sample_motor_faults_tick\|FAULT_POLL\|200' exp_board.c | head -5
36:#define ACCEL_I2C_MIN_INTERVAL_MS 10 // Matches the MC3479's 100 Hz output rate (decimate-by-10, init_accel()) - sampling faster only returns duplicate data
58:#define TILT_TELEMETRY_BURST_MS 500
60:#define NUM_ACCEL_READINGS 130 // Number of accelerometer samples to average
61:#define TOLERANCE 0.01 // Tolerance auto-level code reaches before determining bed is level
62:#define CLOSE_TOLERANCE 0.1 // Auto-level won't begin leveling if within this tolerance
63:#define PULSE_START_TOLERANCE 0.3 // Auto-level will begin pulsing at this tolerance to not overshoot
65:#define STEADY_ACCEL_READ_TIMER_TIME 1500 // Time in ms
66:#define SNEAK_MOTOR_OFF_TIME 350  // Time in ms
67:#define SNEAK_MOTOR_ON_TIME 75 // Time in ms
84:int16_t tolerance = TOLERANCE * SCALE_FACTOR;  // Tolerance in degrees * SCALE FACTOR for determining levelness
85:int16_t close_tolerance = CLOSE_TOLERANCE * SCALE_FACTOR; // Tolerance of beginning sneak leveling to adjust slowly
86:int16_t pulse_start_tolerance = PULSE_START_TOLERANCE * SCALE_FACTOR;
94:static float accel_alpha = 0.1; // LPF adjustment value
164:bool first_check = false;
241:void read_accel(){
307:static void accel_filter_update(accel_filter_state_t *st)
333:void accel_running_average(){
406:void tilt_telemetry_arm_burst(void)
447:void tilt_telemetry_tick(bool status_subscribed, bool other_sampler_active)
461:  if (!status_subscribed) {
473:  if (other_sampler_active) {
478:  if (!motors_on && !tilt_burst_running) {
536:void check_level(axis_t axis){
618:      axis_disabled[i] = false;
679:void level_axis_pulse (axis_t axis){
740:void level_axis (axis_t axis){
785:void steady_accel_read_both_axes(){
789:        start_steady_accel_read_oneshot_timer();
793:        first_check = false;
798:            axis_disabled[i] = ((level_status[i].close_to_level) || (level_status[i].level));
804:void auto_level_single_axis(axis_t axis){
85:#define ACCEL_TIMER_DURATION 1000 // Time in ms
510:bool app_is_ok_to_sleep(void)
532:  if (ble_bed_service_app_hold_active()) {
554:  // - tilt_telemetry_tick() will not arm a burst otherwise - so an idle bed
556:  if (tilt_telemetry_burst_active()) {
626:  if ((auto_level_active) && (!any_collision) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
631:          steady_accel_read_both_axes();
650:              printf("Bed is disabled, no leveling required\r\n");
670:          auto_level_single_axis(auto_level_current_axis); // Checks level based on accel read, sets motor directions, and levels current axis
706:                start_auto_level_oneshot_timer();
734:  settings_mode_tick();
741:  switch_input_tick();
758:  motion_failsafe_tick();
769:  tilt_telemetry_tick(ble_bed_service_status_subscribed(),
772:  bed_control_tick();
773:  ble_bed_service_tick();
185:  app_set_leveled(false); // a new run, either source (#46)
190:  first_check = true;
202:  reset_level_data();
216:  reset_level_data();
31:    case SWITCH_AUTO_LEVEL_VALUE:
32:      *command = BED_COMMAND_AUTO_LEVEL;
151:#define STATUS_NOTIFY_IDLE_MS   1000
152:#define STATUS_NOTIFY_MOVING_MS  100
477:  if (previous == 0 && count > 0) {
478:    tilt_telemetry_arm_burst();
805:    tilt_telemetry_arm_burst();
1101:static bool status_flags_changed(const uint8_t *new_packed, const uint8_t *old_packed)
1114:static void publish_status(const uint8_t *packed, bool moving)
1191:  if (status_notify_due || status_flags_changed(packed, last_sent_status)) {
25:// - see sample_motor_faults_tick() below for why). Milliseconds via
27:#define MOTOR_FAULT_SAMPLE_INTERVAL_MS       200
34:// get_motor_faults()), updated only from sample_motor_faults_tick() in
350:// At 200 ms, the moving-state BLE status notify cadence (100 ms, #6) means
354:void sample_motor_faults_tick(void)
[exit 0]

### #22 $ sh 'C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/1d9c595f-3b1f-4e33-8117-f5eb676e543e/scratchpad/build.sh'
ninja: no work to do.
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
[exit 0]

### #23 $ date; git log --oneline -4; git status
Tue Nov  3 08:19:16 PST 2026
169b2f7 Bench record of 2026-11-02 and rulings (Prompt AH part B, docs only)
48c5cf6 STOP waits for release: STOP_ALL latches app starts, STOP_LATCHED 0x07 (DECISIONS #49, Prompt AH part A)
b848e48 Handoff 2026-11-02 03: Prompt AG (collision latch follow-ups: viewer STOP_ALL, refusals latch, trip arms silence)
3d7d8e6 Collision latch follow-ups: viewer STOP_ALL keeps it, refusals latch, trip arms silence (DECISIONS #48, Prompt AG)
On branch main
Your branch is ahead of 'origin/main' by 2 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
[exit 0]

### #24 $ git status
On branch main
Your branch is ahead of 'origin/main' by 2 commits.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

no changes added to commit (use "git add" and/or "git commit -a")
[exit 0]

### #25 $ sed -n '650p;743p;797,808p;1091p;1114p;1146,1149p;1157,1159p;1170,1177p;1395,1405p' ble_bed_service.c; sed -n '386p' auto_level.c; sed -n '693,697p;747,749p' auto_level.c; sed -n '512,521p;589,591p;637,640p;702,707p' app.c; sed -n '239,241p;266p;212,214p;43,47p' bed_actions.c; sed -n '400,421p' switch_input.c | head -3; sed -n '831,838p' switch_input.c; sed -n '934p' switch_input.c
    // timeout, so the latch clears by silence even if no repeat follows.
    move_watchdog_cancel();
  // REFRESH_TILT is not a motion command: accepted from either role and in
  // settings mode, where the sampler stands down and it is harmlessly
  // absorbed. Handled before the arbitration gates for that reason (#4).
  if (opcode == CMD_OPCODE_REFRESH_TILT) {
    if (arg0 != 0 || arg1 != 0) {
      send_command_result(conn, seq, CMD_RESULT_REJECTED_INVALID);
      return;
    }
    tilt_telemetry_arm_burst();
    send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
    return;
  }
  uint32_t interval_ms = moving ? STATUS_NOTIFY_MOVING_MS : STATUS_NOTIFY_IDLE_MS;
static void publish_status(const uint8_t *packed, bool moving)
    move_watchdog_cancel();
    app_move_active = false;
    app_auto_level_active = false;
    note_app_stopped_commanding("disconnect"); // the app is gone - release its half of the latch
  process_pending_command();
  service_move_watchdog();
  service_app_commanding();
  }
  // The app start latch (#48, #49) was released above; the stop that released
  // it is done.
  if (wd_diag_latch_clear_pending) {
    wd_diag_latch_clear_pending = false;
    printf("BLE: start latch (%s) cleared (%s), %u repeats refused\r\n",
           wd_diag_latch_clear_set_reason, wd_diag_latch_clear_reason,
           (unsigned)wd_diag_latch_clear_refused);
      // sl_bt_api.h: interval x 1.25 ms; latency = connection intervals the
      // peripheral may skip; timeout = supervision timeout x 10 ms. Latency
      // above 0 would delay the phone's Command writes.
      printf("BLE: connection_parameters conn=%u interval=%u latency=%u timeout=%u security_mode=%u\r\n",
             params->connection, params->interval, params->latency, params->timeout,
             params->security_mode);
#else
      printf("BLE: connection_parameters conn=%u interval=%u security_mode=%u\r\n",
             params->connection, params->interval, params->security_mode);
#endif
      if (params->security_mode != sl_bt_connection_mode1_level1) {
 ******************************************************************************/
                start_stop_sneak_oneshot_timer();
            }
        } else {
            sl_sleeptimer_is_timer_running(&sneak_oneshot_timer, &is_sneak_timer_running);
            if (!is_sneak_timer_running){
      };
      return;
  }
  for (uint8_t i = 0; i < NUM_MOTORS; i++) {
    if (motor_state[i].motor_on) {
      return false;  // A motor is running - keep the supervision loop fast.
    }
  }

  // The switch sampler is polled from the main loop (10 ms cadence), so a
  // sampling session needs the loop awake (DECISIONS.md #41).
  if (switch_sampler_active()) {
    return false;
      active_switch_during_collision = active_switch;
      auto_level_active = false;
      auto_level_stop_blink_LED(LED_RED);
          }
          if (axis_disabled[ROLL]){
              auto_level_current_axis = PITCH;
          }

            // Otherwise, switch axis and level
            sl_sleeptimer_is_timer_running(&auto_level_timer, &is_auto_level_timer_running);
            if (!is_auto_level_timer_running){
                start_auto_level_oneshot_timer();
            }
// Bench print: one line on every change of the LEVELED flag (Bed Status flag
// bit 4), so the bench log shows the bit without a phone. Fires only on a
// change, never per pass. Pre-ship: 0 (PRE_SHIP.md).
#define BED_DEBUG_LEVELED_LOG 1

void app_auto_level_stop(void)
{
  auto_level_active = false;
void app_stop_all(void)
{
  if (auto_level_active) {
  active_switch = OFF;
      uint16_t bit = (uint16_t)(1u << b);
      if ((SWITCH_INPUT_MASK & bit) == 0) {
        continue;
  bool any_switch_down = switch_state_fresh() &&
                         (switch_sampler_down() != 0);

  if (any_switch_down) {
    failsafe_quiet_samples = 0;
    failsafe_tripped = false;
    return;
  }
  if ((active_switch != OFF) && !any_collision && (!auto_level_active) && (!in_settings_mode)){
[exit 0]

### #26 $ grep -n 'THIS HANDLER DOES NO WORK\|any future\|opcode added to this switch must obey\|if (disconnect_stop_pending) {\|^void ble_bed_service_tick' ble_bed_service.c; grep -n 'static accel_filter_state_t telemetry_filter\|start_sneak_oneshot_timer();\|Leveling...\|set_dual_motors_on(LEFT);' auto_level.c; grep -n 'else if (((axis_disabled\[ROLL\]) && (axis_disabled\[PITCH\])))\|first_print\[3\] = false;\|if (!is_LED_timer_running){\|printf("Step 1' app.c; grep -n '^void app_set_leveled\|^bool app_leveled' bed_actions.c; grep -n 'pending_press |= bit' switch_input.c
1140:void ble_bed_service_tick(void)
1144:  if (disconnect_stop_pending) {
1433:    // THIS HANDLER DOES NO WORK. It copies four bytes, sets a flag, and
1442:    // opcode added to this switch must obey the same rule.
388:static accel_filter_state_t telemetry_filter = {0};
698:                start_sneak_oneshot_timer();
713:                set_dual_motors_on(LEFT);
753:          printf("Leveling...\r\n");
759:              set_dual_motors_on(LEFT);
333:  if (!is_LED_timer_running){
588:    if (!is_LED_timer_running){
634:              printf("Step 1: ROLL and PITCH check\r\n");
642:      else if (((axis_disabled[ROLL]) && (axis_disabled[PITCH]))){ // No leveling required, exit immediately
655:              first_print[3] = false;
54:void app_set_leveled(bool leveled)
65:bool app_leveled(void)
419:            pending_press |= bit;
[exit 0]

````

### d.3 Build method

`$S/build.sh` (same tools and preset as the AF and AG sessions):

```sh
#!/bin/sh
# Session build wrapper: Silicon Labs ninja + cmake on PATH, build the default preset.
P=~/.silabs/slt/installs/conan/p
export PATH="$P/ninja1a38fc85adcf7/p:$P/cmakefa35ab0687064/p/bin:$PATH"
S="C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/1d9c595f-3b1f-4e33-8117-f5eb676e543e/scratchpad"
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
full build output contains "warning" or "error". Each build's whole output was
under 25 lines, so the tail in d.2 is the complete build output.

## e. VERIFIED vs ASSERTED

Nothing in this session ran on the bed. VERIFIED means a command output
(section d), a line of code read, or a line of the 2026-11-02 bench log (the
user's run, not this session's); it never means "observed on hardware by this
session". `#n` = d.2, `P n` = d.0.

### e.1 VERIFIED, with evidence

| # | Claim | Evidence |
|---|---|---|
| V1 | Start state as the prompt requires: HEAD `b848e48` = `origin/main` after `git fetch`; `.projectlinkstore` modified, the prompt untracked, nothing else | P1, P2, #1 |
| V2 | Builds exit 0 with no warning/error line: after the edit (`BLE_DEBUG_WATCHDOG_LOG` 1), at 0, back at 1; after both commits no work | #5, #6, #7, #22 |
| V3 | Size before 245620 / 784 / 72476 (= AG record); after 245732 / 788 / 72472 (text +112, data +4, bss -4); at 0: 243956 / 780 / 72480 | #4, #5, #6, #7 |
| V4 | `BLE_DEBUG_WATCHDOG_LOG` back at 1; `ble_bed_service.h` not changed by the session | #7 (`36:#define BLE_DEBUG_WATCHDOG_LOG 1`; `git status --short` lists no `.h`), #10; c.1 has no `.h` hunk |
| V5 | Host test passes, `0 check(s) failed`; `bed_control.c` unchanged | #8; c.1 |
| V6 | `BED_BENCH_PROVOKE_RUNAWAY` 0, `BLE_BENCH_WIPE_BONDS` 0, `BED_CAPTURE_MOTOR_CURRENTS` 0 | #10 (`switch_input.c:98`, `ble_bed_service.c:70`, `debug_capture.h:21`) |
| V7 | Only `ble_bed_service.c` changed among sources; no `config/`, `autogen/`, GATT or Protocol Version change; commit A = the source, three docs, the prompt; commit B = four docs | #11, #18 (`git show --stat`); c.1-c.3 |
| V8 | The latch is a three-value enum and is assigned only in `latch_app_starts()` (`:591`) and `note_app_stopped_commanding()` (`:614`, NONE) besides its initialiser (`:354`); no `app_collision_latched` left | #2 (every `app_start_latch` line; no `app_collision_latched` match) |
| V9 | `latch_app_starts()` always calls `note_app_commanding()` and never overwrites COLLISION; callers `:654` (trip), `:757` (STOP_ALL), `:846` (STOP refusal), `:849` (COLLISION refusal) | #2, #9; c.1 |
| V10 | STOP_ALL no longer calls `note_app_stopped_commanding()`; it calls `bed_note_source_released(BED_SOURCE_APP)`, `app_stop_all()`, sends 0x00, then sets STOP and prints | #9 (`:746`, `:747`, `:757`, `:760`); c.1 |
| V11 | Callers of `note_app_stopped_commanding()`: `:628` silence, `:695` watchdog, `:925` MOVE_STOP, `:964` AUTO_LEVEL_STOP, `:1149` disconnect - five | #2, #9 |
| V12 | Refusal order: viewer `:814`, settings `:819`, start latch `:843-858`, #36 `:870` | #9; c.1 |
| V13 | In a pass, `process_pending_command()` (`:1157`) runs before `service_app_commanding()` (`:1159`) | #9, #25 |
| V14 | The disconnect stop runs only if the closed link was control and `bed_active_source() == BED_SOURCE_APP` (`:1047`); `app_stop_all()` sets `active_switch = OFF` (`bed_actions.c:266`) | #9, #25; `bed_control.c:166-171` (P15) |
| V15 | A6.6 from code: edges only (`switch_input.c:400-421`, `pending_press` at `:419`); press arms need `switch_held == OFF`; supervision needs `active_switch != OFF` (`:934`); the fail-safe stands down while a switch is down (`:831-838`); `app_stop_all()` does not touch `switch_held` / `switch_driving` (file-static in switch_input.c, `:156-157`) | P16-P18, #25, #26 |
| V16 | Bench log: six successful app auto-levels with `LEVELED -> 1`, `Exit auto-level`, `Bed is level` (lines in b.B), both one-axis outcomes; twelve LEVELED lines; `-> 0` at 173, 201, 224, 253, 365 | #14, #17 (and the full read, P20) |
| V17 | Bench log: 62 hold-end lines, 55 `op=0x01`, 7 `op=0x03`, all `longest between passes 0 ms`; refresh gap 41-195 ms; no `SW: 0x0008`; six `connection_closed` lines, all `reason=0x1013` | #14, #17 |
| V18 | Bench log lines 313-323 as quoted in b.B; the trip sum 551 > 550 (`motor_current_functions.c:343-353`); output C = `OUTPUT2`, in the RIGHT and FOOT pairs | P20, #15, #16, #13 (`dual_motor_control.c:351-422`) |
| V19 | `SWITCH_UP_ALL 0x0001`, `SWITCH_AUTO_LEVEL 0x0002`, `SWITCH_BED_LIGHTS 0x0008` | #13 (`app.h:115-118`) |
| V20 | No DECISIONS entry calls the roll sign unconfirmed; BLE_GATT_DESIGN §6 was the only place | P21, #12, P41 |
| V21 | No Phase 1b / heat-map / last-trip record in the repo before this session | P26, P28 |
| V22 | C: accel read path, rate limit, bus and speed, callers, burst triggers and gates, notify cadence, `tilt_fresh` 0, the tolerance constants and their scaled values, the 1500 ms / 350 ms / 1000 ms timers, the auto-level sequence | file:line in b.C (P46-P55, #19-#21, #25, #26); scaled values #19 |
| V23 | Edited files have LF endings (0 CR) | #10 (Part A files), P44 (all `docs/*.md`) |

### e.2 ASSERTED (reasoned from code, compiles, not observed)

- **A1.** Every row of the b.A.6 tables: the Command Results, their order, the
  motors and the console lines - in particular that a held app button after a
  STOP stays stopped until released, that an AUTO LEVEL hold does not restart,
  that a viewer's STOP_ALL also latches the control app, and that a STOP_ALL
  during a collision latch keeps it COLLISION and nothing restarts after the
  sequence.
- **A2.** A held physical button does not restart after an app STOP_ALL
  (A6.6) - from code; not observed.
- **A3.** A press ≥300 ms after a STOP with nothing held is accepted only if a
  pass ran after the deadline (A6.3 (b), f.2); how often an idle connected bed
  runs a pass is not measured.
- **A4.** The second `bed_note_source_released(BED_SOURCE_APP)` at the later
  silence clear changes nothing in bed_control (AG b.1.1 argument, re-used).
- **A5.** The bss -4 B is layout, not a new variable.
- **A6.** B5's "foot-right" corner: from the pairing code plus Andrew's
  report; motor indices A-D are physical outputs, and BLE_GATT_DESIGN §6 says
  the corner mapping is not calibrated yet.
- **A7.** B1's roll sign: the user's report of the app; nothing in the UART
  log.
- **A8.** Part C's costs, timings and recommendations: I2C wire time (~0.85 ms;
  the 1.4 ms max is a recorded measurement, not re-measured), bus occupancy
  at 10/20 Hz, the radio and sleep effects, the 1.5 s ± ~50 ms fill estimate,
  the steady-read timer quirk being harmless, the idle loop rate being faster
  than auto-level's.
- **A9.** App behaviour (keeps repeating / ends the hold / sends MOVE_STOP
  after STOP_ALL) is modelled, not read: the app repository was not looked at.

---

## f. QUESTIONS FOR REVIEW

**f.1 STOP latch vs collision - which code wins?** (b.A.4) With the latch STOP
and a collision sequence running, I answer **0x07** and keep STOP, following
A4's wording ("while latched with reason STOP, every ... start is refused with
0x07"). The other reading of "the `|| any_collision` term is unchanged" would
answer 0x06 and upgrade the latch to COLLISION. Both refuse and both clear the
same way; only the code the app shows differs. Swapping is a two-line change
(test COLLISION / `any_collision` first). Which do you want?

**f.2 The silence clear runs after the command in the same pass.** (b.A.6
A6.3 (b)) In `ble_bed_service_tick()` the pending command is processed
(`:1157`) before `service_app_commanding()` (`:1159`). If no main-loop pass
ran between the 300 ms deadline and a new press, that press is refused (0x07,
or 0x06 for a COLLISION latch) and latches for its whole hold. This is
pre-existing (the #48 latch and the #36 silence release have the same order)
but the STOP latch makes it user-visible: tap STOP, wait half a second, press -
refused if the bed ran no pass in between. With a phone subscribed a pass runs
at least once a second (the idle status notify) plus BLE events; nothing
measured. **Proposed fix, not made** (the prompt says to report, not
improvise): call `service_app_commanding()` once more at the top of
`ble_bed_service_tick()`, before `process_pending_command()`. Effect: "300 ms
since the app's last start" is judged at the moment the next command is
processed. Trade-off: a held repeat that arrives >300 ms after the previous one
(a link stall) is then always treated as a new press - which is already what
happens whenever a pass ran in the gap, and what #36 already assumes. Make the
change?

**f.3 A STOP with nothing held latches for 300 ms.** (A6.3 (a)) "Any STOP_ALL
sets the latch", so a press made within 300 ms of a STOP tap is refused and must
be released and pressed again. Intended?

**f.4 An app that sends MOVE_STOP after its STOP_ALL and keeps repeating
restarts the bed.** (A6.2) The MOVE_STOP is a release by A5's definition, so
the next repeat is a normal start. The firmware cannot tell it from a new
press. The planned app ends its hold on STOP, so this needs the app prompt to
say: after STOP, stop repeating until the finger lifts, and send MOVE_STOP /
AUTO_LEVEL_STOP only at the lift (or immediately, but then stop repeating).
Accept that the firmware relies on the app for this one case?

**f.5 The print suffix.** `BLE: STOP_ALL latch set (conn=N)` gains `, kept as
collision` when the latch was already COLLISION - not in A7's text; added so
5.A4 / 5.A6 can tell the cases apart. Keep?

**f.6 The firmware restarted at log line 287.** Banner, NVM3 and settings loads,
`BLE SM: configure` - no reset cause printed. Was that a reflash or power cycle
by Andrew? If not, it is an unexplained reset and worth a look (a reset-cause
print at boot would answer it next time).

**f.7 B5 location and B2 count.** No Phase 1b notes existed, so the collision
location data point is a new row in PRE_SHIP's "Also recorded in the repo -
not yet marked pre-ship" table. Move it if Phase 1b has a home elsewhere. B2:
the log has six successful app auto-levels, not four; I recorded six.

**f.8 Part C rulings needed:** (a) C2 - Option B (10 Hz idle sampler, ms-based
filter, 5 Hz idle notify, `tilt_fresh`), or try A' first; (b) C3 - the exit band
(0.3°, 2 s) and whether bit 4 may ever set without a run (I recommend not; a
separate IN_LEVEL_BAND bit if wanted); (c) C4 - document the 1.5 s now, add an
AUTO_LEVEL_SETTLING bit, or both.

**f.9 B1 evidence.** The record says the app's bubble and words were correct
with `kRollWireSign = +1`. I recorded the roll line as specification on that
report. Was RIGHT-raised (roll negative) seen too, or only one side? If only
one, the four-direction table is the check to finish.

---

## g. NOT DONE

- **No bench run** - the user drives the bench (CLAUDE.md). REGRESSION §5b and
  the changed 5.A4 / 5.A6 are for the next bench session.
- **No host-test cases** for the latch: it lives in `ble_bed_service.c`, which
  the host test cannot compile (SDK headers, stack events, sleeptimer) - the
  same reason as AF / AG. Not asked in this prompt.
- **f.2's ordering fix** - proposed, not made (report, don't improvise).
- **Part C** is a report only, as asked; no code.
- **The app side** (STOP ends every hold, 0x07 handling, its copy of
  BLE_GATT_DESIGN §7) - a separate app prompt.
- **Not reproduced in d:** the contents of committed files and of the bench log
  read during the session (d.1 explains); the 149-line prompt file's diff
  (it is the committed `docs/prompts/PROMPT_AH_stop-latch-and-live-level-report.md`,
  written by the reviewer).
- **The commit of this file with the SESSION_LOG entry, and the `git push`**
  run after this file is written, so their output cannot appear here; they are
  reported to the user. A push failure would be reported in a follow-up commit
  to this file, without merge, rebase or reset (CLAUDE.md "Pushing").
- Nothing else that was asked was left out.
