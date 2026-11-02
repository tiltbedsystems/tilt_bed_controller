# fw_HANDOFF_2026-11-02_02 — Prompt AF: collision latch on the app path, COLLISION_STOP (0x06)

Firmware repo session of 2026-11-02 (system date checked with `date` at the
start, `Mon Nov  2 08:55:03 PST 2026`, and again before writing this file,
`Mon Nov  2 09:05:18 PST 2026`). Prompt:
`docs/prompts/PROMPT_AF_collision-latch-app-hold.md` (committed in
`e646c3b`). It replaces Prompt AE Part A in full.

## a. Header

| | |
|---|---|
| Repo | `tilt_bed_controller` (firmware) |
| Branch | `main` |
| HEAD before | `4d1fef3` (Handoff 2026-11-02 01: Prompt AE), parent `e3cc865`, level with `origin/main` after `git fetch` - as the prompt expects |
| HEAD after the work | `e646c3b` (one work commit: source, docs, prompt file) |
| After that | this handoff and the session's SESSION_LOG entry, committed together, then `git push` (normal push, no force) |
| Builds | from the command line (the prompt permits it): exit 0, no warnings, with `BLE_DEBUG_WATCHDOG_LOG` 1 and 0 (d.2, commands 21-22) |
| Bench | **nothing ran on a bed in this session** |

`git status` at the end of the work (after commit `e646c3b` and the
SESSION_LOG entry, before this file was written):

```
On branch main
Your branch is ahead of 'origin/main' by 1 commit.
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
| A1 | `CMD_RESULT_COLLISION_STOP 0x06`, `app_collision_latched` | Done, `e646c3b` (`ble_bed_service.c:178`, `:341`) |
| A2 | At the trip: cancel, clear both flags, latch, send 0x06 with `last_move_seq`; not `note_app_stopped_commanding()` | Done (`:610-631`) |
| A3 | Refuse MOVE_START / AUTO_LEVEL_START while `app_collision_latched \|\| any_collision`, after viewer + settings checks, before #36 | Done (`:783-804`). Window stated from code: b.4 |
| A4 | Clear the latch in `note_app_stopped_commanding()`; list every caller; confirm each is a real release, else stop | Done (`:572-587`). Six callers, b.3. **One judgement call reported, not stopped on: STOP_ALL is also accepted from a viewer** (b.3, f.1) |
| A5 | Physical controller unaffected | No change outside `ble_bed_service.c` (c.1) |
| A6 | Sleep veto off during the latch - confirm nothing moves except the reverse | b.5. Also found: the runaway fail-safe cuts that reverse short (b.11.1) |
| B1-B5 | Command Results + motors per case | b.6 (ASSERTED) |
| C | Two bench prints, behind `BLE_DEBUG_WATCHDOG_LOG` | Done; b.7 |
| D1 | BLE_GATT_DESIGN §7 row 0x06 | Done; also one line in §12 item 8 |
| D2 | DECISIONS entry | #48 |
| D3 | REGRESSION items for B1, B2, B3, B5 | §5 items 5.A1-5.A4, plus 5.A5 (physical collision unchanged; not asked - f.6) |
| D4 | Host test cases if no large new harness | **Not added** - the latch is not reachable from the existing test; what it would take: b.8 |
| D5 | Build, warnings, flash/RAM change | b.9: text +400 B, data +4 B, bss -4 B |
| E | Expected console lines for B1, B2, B3, B5 | b.10 |

## b. Designs and analysis

Line references are to `e646c3b` unless marked. `ble_bed_service.c` is the
only source file changed; every other file:line is unchanged from `4d1fef3`.
Every claim is VERIFIED (code read, or a command output in d) or ASSERTED.
Nothing ran on the bed.

---

### b.1 The defect being fixed (recap, from the AE handoff b.A.1)

- A trip - summed rise in `collision_check()` or max load in
  `max_load_check()` - calls `set_all_motors_off()` and sets
  `any_collision = true` (`motor_current_functions.c:306-307`, `:322-323`,
  `:366-367`). Both run only inside `run_motor_supervision()`, which runs in
  `switch_input_tick()` for a move (`switch_input.c:934-939`) or in
  auto-level's `level_axis()` (`auto_level.c:770-771`), both earlier in the
  main-loop pass than `ble_bed_service_tick()` (`app.c:626` auto-level block,
  `:741` `switch_input_tick()`, `:773` `ble_bed_service_tick()`).
- Before `e646c3b`, `service_move_watchdog()`'s collision branch only
  cancelled the watchdog and cleared `app_move_active`; no Command Result.
  The app repeats MOVE_START every 100 ms while the button is held; the next
  repeat found `app_move_active` false and called `bed_move_start()` and
  `app_apply_move_outputs()` (now `:844`, `:852`). Nothing on that path read
  `any_collision`, so the pair re-energised in the original direction while
  supervision was off (`switch_input.c:934` requires `!any_collision`).
  ASSERTED in the AE session and still not seen on a bed.

### b.2 The design as implemented (Option 1 with the AF rulings)

All in `ble_bed_service.c`, main-loop context. No I2C added, nothing blocks,
nothing is printed from a stack callback. No `config/`, `autogen/` or GATT
change: 0x06 is a value in the existing 2-byte Command Result notification.

**State** (`:333-341`):

```c
// Collision latch (DECISIONS #48). Set when a collision trips during an app
// hold, while the user is still holding the app button. ...
static bool app_collision_latched = false;
```

**Result code** (`:178`): `#define CMD_RESULT_COLLISION_STOP 0x06`.

**At the trip - `service_move_watchdog()` (`:604-631`).** The function
returns at once unless `move_watchdog_armed` (`:606`), and the watchdog is
armed only by an accepted MOVE_START / AUTO_LEVEL_START (`:860`, `:900`), so
this branch runs only during an app hold:

```c
if (any_collision) {
  move_watchdog_cancel();          // ends the hold; latches the "hold end" print
  app_move_active = false;
  app_auto_level_active = false;   // new (A2)
  app_collision_latched = true;    // new
  send_command_result(control_connection, last_move_seq, CMD_RESULT_COLLISION_STOP); // new
#if BLE_DEBUG_WATCHDOG_LOG
  printf("BLE: COLLISION_STOP seq=%u op=0x%02x (latched)\r\n",
         last_move_seq, wd_diag_refresh_opcode);   // new; after stop + result
#endif
  return;
}
```

`note_app_stopped_commanding()` is **not** called here (A2 ruling): the app's
`app_commanding` and its half of bed_control's #36 latch are left as they are.
No `app_stop_all()` either - the collision block in `app.c:580-620` owns the
stop and the LED sequence, as before.

**The refusal - `process_pending_command()` (`:783-804`)**, after the viewer
check (`:773`) and the settings-mode check (`:778`), before the #36 conflict
check (`:816`):

```c
if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
    && (app_collision_latched || any_collision)) {
  note_app_commanding();       // still holding: keeps the silence timer alive
  send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);  // its own seq
#if BLE_DEBUG_WATCHDOG_LOG
  if (wd_diag_collision_refused < 0xFFFF) wd_diag_collision_refused++;
#endif
  return;                      // no bed_control call, no last_move_seq, no watchdog arm
}
```

Consequences of the position, from code: a viewer still gets 0x02, settings
mode still 0x03; STOP-class opcodes, LIGHTS_SET and REFRESH_TILT are never
refused by it; a MOVE_START with out-of-range arguments is answered 0x06 rather
than 0x04 while latched (the #36 check already behaves that way, it also runs
before argument validation).

**The release - `note_app_stopped_commanding()` (`:569-587`)** now takes a
reason string (only for the print) and clears the latch:

```c
static void note_app_stopped_commanding(const char *reason)
{
  app_commanding = false;
  bed_note_source_released(BED_SOURCE_APP);
#if BLE_DEBUG_WATCHDOG_LOG
  if (app_collision_latched) {           // record the "cleared" line
    wd_diag_latch_clear_pending = true;
    wd_diag_latch_clear_reason = reason;
    wd_diag_latch_clear_refused = wd_diag_collision_refused;
  }
  wd_diag_collision_refused = 0;
#else
  (void)reason;
#endif
  app_collision_latched = false;
}
```

The signature change is the only edit at the existing call sites (each passes
its reason). No other behaviour of those paths changes.

### b.3 A4 - every caller of `note_app_stopped_commanding()`, and whether it is a release

From `grep -n note_app_stopped_commanding ble_bed_service.c` after the change
(d.2, command 16):

| # | Caller | Line | Reason printed | Runs when | A real release? |
|---|---|---|---|---|---|
| 1 | `service_app_commanding()` | `:599` | `silence` | `MOVE_WATCHDOG_MS` (300 ms) since the last MOVE_START / AUTO_LEVEL_START from the app, **refused ones included** (`note_app_commanding()` at `:796`) | Yes. A held button repeats every 100 ms, so 300 ms with no start means the finger is up or the link is gone (the same rule #36 already relies on) |
| 2 | Watchdog expiry in `service_move_watchdog()` | `:665` | `watchdog` | Watchdog armed and expired | Yes (300 ms without a refresh). **Cannot clear a set latch**: the latch is set in the same branch that cancels the watchdog (`:621-624`), and while latched every start is refused before it can re-arm it (`:795-803`). The prompt's list of reasons does not include it; it is listed for completeness |
| 3 | STOP_ALL in `process_pending_command()` | `:716` | `stop_all` | A STOP_ALL write from **either** connection, control or viewer (`:708-722`; the role is never checked, #4/#12) | From the control phone: yes - the app's own stop. **From a viewer: it is a release of the bed, not of the control user's finger** - see below |
| 4 | MOVE_STOP | `:871` | `stop` | Control connection only (a viewer is rejected at `:773` first), arguments 0/0 | Yes - the app's release of a MOVE button |
| 5 | AUTO_LEVEL_STOP | `:910` | `stop` | Control connection only, arguments 0/0 | Yes - the app's release of AUTO LEVEL |
| 6 | Control-phone disconnect stop in `ble_bed_service_tick()` | `:1095` | `disconnect` | `disconnect_stop_pending`, which `handle_connection_closed()` sets only when the closed link was the control connection **and** `bed_active_source() == BED_SOURCE_APP` (`:993`) | Yes - the app is gone. But conditional, see b.6 B4 |

**STOP_ALL from a viewer (judgement call, reported in f.1, not stopped on).**
The prompt lists STOP_ALL among the release paths it wants, and also says to
stop if a caller "would unlatch while the user may still be holding". A viewer
STOP_ALL does exactly that for the control phone's user. I did not treat this
as a stop condition because the prompt names STOP_ALL explicitly, and because
from code the consequence equals what STOP_ALL already does to an ordinary
hold today:

- inside the collision sequence, the `|| any_collision` term still refuses the
  control app's repeats, so nothing moves toward the obstruction;
- after the sequence, a control app that is still repeating gets its next
  MOVE_START accepted and the move restarts, with supervision on. Today a
  viewer STOP_ALL during any ordinary held move is followed the same way by the
  next repeat (`:708-722` clears `app_move_active`; the next repeat calls
  `bed_move_start()`).

The same applies, less plausibly, to a STOP_ALL from the control phone pressed
with a second finger while the first is still on a move button.

### b.4 A3 ruling - how long the `|| any_collision` window is, from code

- **Set**: at the trip, in pass N (b.1).
- **Sequence start**: the collision block is at the top of
  `app_process_action()` (`app.c:580`). In pass N+1 it finds the LED timer not
  running, saves `active_switch` into `active_switch_during_collision`, clears
  `auto_level_active`, starts the 500 ms **periodic** `LED_500ms_timer`
  (`app.c:186-190`, `:588-599`), prints `Once`, all LEDs on.
- **Each expiry** sets `LED_timer_timeout`; the next pass toggles all LEDs and
  increments `LED_blink_counter` (`app.c:600-608`). At 2 the reverse starts
  (`:604-606`).
- **Clear**: the pass that takes the counter to `COLLISION_BLINK_CYCLES` (3,
  `app.c:84`) runs `set_all_motors_off()`, stops the timer, LEDs off (amber
  back on if the lights are on), `active_switch = OFF`, `any_collision = false`
  (`app.c:609-618`) - in the same pass as the third toggle.

So the window is **1500 ms (three timer periods) + the time from the trip to
the start of pass N+1 + the time from the third expiry to the pass that
handles it**. Both extra terms are one pass when the CPU is awake. When it is
asleep they are the wake latency: the timer interrupt itself wakes the CPU
for the third expiry; after the trip, the motors are off and the watchdog is
cancelled, so `app_is_ok_to_sleep()` (`app.c:510-560`) may let the CPU sleep
until the next interrupt - at most about one BLE connection interval (15-30
ms, `CONN_MIN/MAX_INTERVAL` `:145-146`) with a phone connected, or one
advertising interval (30 ms, `:140`) without. ASSERTED: **~1.5 s, plus at most
a few tens of ms.** The window is the same for a physical-controller
collision, during which an app MOVE_START / AUTO_LEVEL_START is now refused
with 0x06 (A3 ruling) without setting the latch.

### b.5 A6 - the #45 sleep veto during the latch

`ble_bed_service_app_hold_active()` returns `move_watchdog_armed`
(`:1081-1084`). The trip cancels the watchdog and nothing re-arms it while
latched (b.3 row 2), so the veto is off for the whole latch. From code, what
can move during the latch:

1. **Nothing the app commands.** Every MOVE_START / AUTO_LEVEL_START is
   refused before any bed_control call (`:783-804`). LIGHTS_SET is not
   motion. STOP-class commands stop.
2. **The collision sequence's own reverse**, for a move only:
   `reverse_motor_direction_and_start(active_switch_during_collision)`
   (`app.c:606`) has cases for the eight pair values and the two ALL values;
   `SWITCH_AUTO_LEVEL_VALUE` falls to `default: break` (`dual_motor_control.c:
   300-343`) - no reverse after an auto-level collision (the known limitation,
   REGRESSION §5). The reverse keeps the CPU in EM0 without the #45 veto:
   `set_dual_motors_on()` sets `motor_state[].motor_on = true` (seen at
   `dual_motor_control.c:352-356` for HEAD), which `app_is_ok_to_sleep()`'s
   first check vetoes; an ALL reverse uses `set_all_motors_on()`, covered by the
   `motor_outputs_maybe_on()` shadow check (`app.c:542`).
3. **The physical controller**, once the sequence has ended (during it
   `process_outputs()` ignores the panel, `switch_input.c:517`). That is the
   physical source's own motion with its own vetoes (motor on, switch sampler
   active). The app latch does not refuse it (A5), and the app's refused
   repeats never reach `bed_move_start()`, so they raise no #36 conflict while
   latched.

So the veto is not needed during the latch. ASSERTED, from code. Main-loop
passes still run on every BLE connection event, so refused repeats and the
silence clear are serviced without it.

### b.6 Part B - what the held button does (ASSERTED, from code)

Notation: `t` = time after the trip pass. `N` = seq of the last accepted
repeat before the trip (= `last_move_seq`). The app is the control phone.
Each case gives (i) an app that keeps repeating while the finger is down
(today's app, which does not know 0x06 - the worst case, and what the
firmware must handle) and, where it differs, (ii) an app that handles 0x06 as
BLE_GATT_DESIGN §7 now says (ends its hold at the first 0x06).

The common start, for a move and for auto-level:

- **Trip pass (t = 0).** Supervision turns all motors off and sets
  `any_collision`. In `ble_bed_service_tick()`: if a repeat happened to be
  pending in this pass, `process_pending_command()` (`:1103`) refuses it first
  - **0x06 with that repeat's seq** (counted). Then `service_move_watchdog()`
  (`:1104`) - **0x06 with seq N**, latch set, watchdog cancelled. The app may
  therefore receive one or two 0x06 in the trip pass. No WATCHDOG_STOP follows.
- **Pass N+1.** The collision sequence starts (b.4).

#### B1. Collision during an app MOVE hold; finger down 3 s; released; fresh press after the sequence

(i) app keeps repeating:

| t | App sends | Command Result | Motors |
|---|---|---|---|
| 0 | (last repeat N already accepted) | `{N, 0x06}` (plus `{N+1, 0x06}` if a repeat was pending in the trip pass) | all off at the trip |
| 0.1 ... 3.0 s, every 100 ms | MOVE_START (same target/dir), seq N+1, N+2, ... | `{seq, 0x06}` for each - ~30 in 3 s | 0-1.0 s: off. ~1.0 s: **the reverse** (pair driven opposite to the move, `app.c:604-606`). ~1.15-1.2 s: **cut by the runaway fail-safe** (b.11.1). ~1.5 s: sequence ends, all off, `any_collision` false. 1.5-3.0 s: **off; the repeats are still refused (latched)** |
| 3.0 s | MOVE_STOP, seq M | `{M, 0x00}` - latch cleared, reason `stop` | stay off (`bed_move_stop()` on an idle bed + `app_apply_stop_outputs()`) |
| later | fresh MOVE_START | `{seq, 0x00}` | normal move |

Before `e646c3b` the row at 0.1 s would have been `0x00` and **the pair
re-energised toward the obstruction** (b.1).

If the app's MOVE_STOP is lost: the latch clears by silence 300 ms after the
last repeat (`silence`); nothing moves in between.

(ii) app handles 0x06: it stops repeating at the first 0x06. If it sends
MOVE_STOP, the latch clears at once (`stop`); if it just goes quiet, 300 ms
later (`silence`). Motors as in (i): off, the reverse at ~1.0 s (cut at
~1.15-1.2 s), off at ~1.5 s. A fresh press inside the sequence is refused by
`any_collision` (B3); after it, it moves.

#### B2. The same during an app AUTO LEVEL hold

(i) app keeps repeating:

| t | App sends | Command Result | Motors |
|---|---|---|---|
| 0 | (last AUTO_LEVEL_START N accepted) | `{N, 0x06}` (plus one more if a repeat was pending in the trip pass) | all off at the trip; `level_axis()` also clears `auto_level_motors_on` and `motor_on` (`auto_level.c:772-778`) |
| 0.1 ... 3.0 s | AUTO_LEVEL_START, every 100 ms | `{seq, 0x06}` each, ~30 | **no motion at all**: no reverse for auto-level (b.5 item 2); sequence ends ~1.5 s; `auto_level_active` was cleared by the collision block (`app.c:590`), so the auto-level loop does not resume |
| 3.0 s | AUTO_LEVEL_STOP, seq M | `{M, 0x00}`, reason `stop` | stay off |
| later | fresh AUTO_LEVEL_START | `{seq, 0x00}` | a fresh run (`app_auto_level_start()` resets the state machine) |

Difference from before `e646c3b`: the repeats were `0x00` and only re-armed the
watchdog, the bed stayed still by accident (`app_auto_level_active` stayed
set). Now they are `0x06`, and the watchdog is not armed during the latch.

(ii) app handles 0x06: as B1 (ii); the bed does not move.

#### B3. Released and pressed again quickly (~0.5 s after the trip, inside the sequence)

(i) app keeps repeating the new press:

| t | App sends | Command Result | Motors |
|---|---|---|---|
| 0 | - | `{N, 0x06}` | off |
| 0.1-0.4 s | MOVE_START repeats | `{seq, 0x06}` each (~4) | off |
| ~0.45 s | MOVE_STOP (finger up), seq M | `{M, 0x00}`; latch cleared, reason `stop` | stay off. `bed_move_stop()` (the bed is not idle yet and APP holds it) -> `app_move_stop()`; `app_apply_stop_outputs()` turns all LEDs off and sets `active_switch = OFF` - the blinking LEDs change phase (cosmetic) |
| ~0.5 s | new MOVE_START (same button), seq P | `{P, 0x06}` - refused by `any_collision`, **not latched**; counted but no print follows (b.7) | off |
| 0.6-1.5 s | repeats | `{seq, 0x06}` each | ~1.0 s: **the reverse still runs** (it uses `active_switch_during_collision`, saved at pass N+1, `app.c:589`); ~1.15-1.2 s: cut by the fail-safe; ~1.5 s: sequence ends |
| first repeat after ~1.5 s | MOVE_START | `{seq, 0x00}` | **the move starts again**, in the pressed direction, with supervision running (`any_collision` false). If that is toward the obstruction, it trips again after supervision's blind window (PRE_SHIP F20) and B1 repeats |

That last row is what A3 as written gives: the re-press did not set the latch,
so it is refused only while the sequence runs. Raised in f.2.

(ii) app handles 0x06: the re-press's first 0x06 ends the new hold; nothing
starts.

#### B4. The link drops during the latch, then reconnects

Two cases, from `handle_connection_closed()` (`:980-1022`):

- **Drop during the sequence** (t < ~1.5 s). `active_switch` still holds the
  move / auto-level value, so the bed is not idle and `bed_active_source()` is
  APP (`bed_control.c:79-83`, `:173-176`): `disconnect_stop_pending` is set
  (`:993-995`). Next tick (`:1090-1097`): `note_app_stopped_commanding(
  "disconnect")` - **latch cleared** - then `app_stop_all()`: motors off, LEDs
  off (amber if lights), `active_switch = OFF`. The collision sequence keeps
  running (`any_collision` and the timer are untouched): the reverse at ~1.0 s
  still runs (move only) and is cut by the fail-safe; all off at ~1.5 s.
- **Drop after the sequence** (t > ~1.5 s). `bed_control_tick()` (`app.c:772`)
  has already returned `active_source` to NONE (`bed_control.c:166-171`)
  because the bed is idle, so `disconnect_stop_pending` is **not** set and the
  disconnect stop does not run. The latch is cleared instead by **silence**,
  300 ms after the last repeat the bed received (`app_commanding` is true -
  the refused repeats kept it alive). Nothing is moving.

Either way the latch is clear within 300 ms of the bed's last write from the
app. A reconnect needs connection open, encryption from the stored bond and a
role (`connection_parameters` with security, `:1337-1358`) before any write is
accepted as control; not measured, but expected to take well over 300 ms. If a
MOVE_START did arrive while still latched it would be refused (0x06). After
that, **the firmware starts nothing by itself**: it moves only on a
MOVE_START / AUTO_LEVEL_START from the reconnected app, i.e. a fresh press -
or a press the app re-sends because the finger is still down after the
reconnect, which is app behaviour this repo cannot see (f.4). If a viewer was
connected it is promoted to control on the drop (`:1001-1014`); its starts
during the remaining latch would be refused.

#### B5. STOP_ALL from the app during the latch

(i) app keeps repeating the held move, STOP_ALL pressed with another finger:

| When | STOP_ALL result | Motors | Then the held repeats |
|---|---|---|---|
| Inside the sequence (e.g. t = 0.5 s) | `{seq, 0x00}`; latch cleared, reason `stop_all` | `app_stop_all()`: off, LEDs off, `active_switch = OFF`. The sequence continues: reverse at ~1.0 s (cut ~1.15-1.2 s), off at ~1.5 s | refused `0x06` (by `any_collision`) until ~1.5 s, **then the next repeat starts the move again** (`0x00`) |
| After the sequence (e.g. t = 2 s) | `{seq, 0x00}`; reason `stop_all` | already off; stays off | **the next repeat starts the move again** (`0x00`) |

That restart is the same as STOP_ALL during an ordinary hold today (b.3).
STOP_ALL is processed first in the tick (`:708`), so if a repeat is in the
shared slot in the same pass it is processed right after the STOP_ALL, with the
latch already clear.

(ii) app handles 0x06: no repeats; STOP_ALL clears the latch; nothing moves
except the reverse if still inside the sequence.

### b.7 Part C - the bench prints

Both behind `BLE_DEBUG_WATCHDOG_LOG` (`ble_bed_service.h:36`, 1 today), main
loop only, after the stop and the Command Result:

1. **At the trip**: `BLE: COLLISION_STOP seq=N op=0x0M (latched)`, printed in
   `service_move_watchdog()` right after `send_command_result()` (`:626-629`).
   N = `last_move_seq`; M = `wd_diag_refresh_opcode`, the opcode of the last
   watchdog refresh: `0x01` (MOVE_START) or `0x03` (AUTO_LEVEL_START). Printed
   before the existing `BLE: hold end ...` line for the same hold (that one is
   printed later in the same tick, `:1110-1116`).
2. **When the latch clears**: `BLE: collision latch cleared (<reason>), K
   repeats refused`. `note_app_stopped_commanding()` only records it (it runs
   *before* the stop on the MOVE_STOP / AUTO_LEVEL_STOP / STOP_ALL /
   disconnect paths); `ble_bed_service_tick()` prints it at `:1119-1123`, after
   `process_pending_command()`, `service_move_watchdog()` and
   `service_app_commanding()` have run - the same deferral the `hold end` line
   uses. Reasons: `stop`, `stop_all`, `disconnect`, `silence` (and `watchdog`,
   which cannot occur while latched, b.3).
   **K** = `wd_diag_collision_refused`: every 0x06 refusal since the app last
   released (incremented at `:799-801`, reset in every
   `note_app_stopped_commanding()` call). It includes a refusal in the trip
   pass before the latch was set. It does **not** include the trip's own 0x06.
   Refusals made while not latched (B3's re-press, a press during a physical
   collision) are counted and then dropped without a print when the app
   releases - the prompt asks for the print only "when the latch clears"
   (f.5).

No per-repeat print.

### b.8 D4 - why no host-test cases were added, and what it would take

The existing host test (`test/bed_control_test.c`) compiles
`bed_control.c` alone with stub `bed_actions.h` functions
(`gcc -std=c99 -Wall -I.. -o bed_control_test bed_control_test.c
../bed_control.c`, its header comment). The latch is entirely in
`ble_bed_service.c`, which the test does not compile and cannot without new
infrastructure. `ble_bed_service.c` includes `app_assert.h`, `gatt_db.h`,
`sl_bluetooth.h`, `sl_sleeptimer.h`, and through `ble_bed_service.h`
`sl_bt_api.h` and `bed_status.h`; plus `auto_level.h`, `bed_actions.h`,
`bed_control.h`, `motor_current_functions.h` (d.2, command 15).
Commands reach `process_pending_command()` only through
`ble_bed_service_on_event()` with an `sl_bt_msg_t`
(`sl_bt_evt_gatt_server_attribute_value`), and the silence clear and the
watchdog depend on `sl_sleeptimer_get_tick_count()` and sleeptimer callbacks.

What it would take - a second host test, not new cases in the existing one:

1. A `test/fakes/` include directory placed before the SDK on the include
   path, with minimal `sl_bt_api.h` / `sl_bluetooth.h` (the `sl_bt_msg_t`
   header + the union members the file reads: system_boot,
   connection_opened/closed, sm_*, connection_parameters,
   gatt_server_attribute_value / characteristic_status / user_read_request;
   `SL_BT_MSG_ID`; the `sl_bt_*` functions it calls, recording calls - in
   particular `sl_bt_gatt_server_send_notification()` to capture Command
   Results), `gatt_db.h` (the `gattdb_*` handles), `sl_sleeptimer.h` (a
   settable tick counter; timers recorded and fired by hand), `app_assert.h`,
   `sl_status.h`.
2. Stubs for `auto_level.h` (`tilt_telemetry_arm_burst`), `bed_status_get`,
   every `bed_actions.h` function the file calls (the existing test's stubs
   cover part of it), and a test-owned `bool any_collision`.
3. Link against the real `bed_control.c` (so the #36 interaction is real).
4. Cases: B1 (repeat during latch -> 0x06, no `bed_move_start`; MOVE_STOP ->
   0x00 and next start accepted), B3 (release + press while `any_collision`
   -> 0x06; clear `any_collision` -> next repeat accepted), silence clear
   (advance the fake tick 300 ms -> next start accepted).

Estimate: roughly 300-400 lines of fakes and test, plus the risk that the fake
`sl_bt_msg_t` diverges from the SDK's. That is the "large new harness" the
prompt says not to build, so none was added. The existing test still passes
(d.2, command 24).

### b.9 D5 - build and size

Built with the session wrapper (d.3). Results (d.2, commands 20-22 and 36):

| Build | Result |
|---|---|
| Existing ELF before any build (built 07:17 by the AE session at `e3cc865`) | text 245188, data 780, bss 72480 - matches the AE record |
| After the edit | 1 object + link, exit 0, **no warning/error lines** |
| `BLE_DEBUG_WATCHDOG_LOG` 0 (temporary, `sed` on `ble_bed_service.h`) | 3 objects + link, exit 0, no warnings (checks the `(void)reason` path) |
| Back to 1 | 3 objects + link, exit 0, no warnings; `git status` shows no `ble_bed_service.h` change |
| After commit `e646c3b` | `ninja: no work to do.`, exit 0 |

`arm-none-eabi-size` after: **text 245588 (+400), data 784 (+4), bss 72476
(-4)**. Flash use (text + data) +404 B; RAM (data + bss) unchanged in total.
The +4 data / -4 bss is `wd_diag_latch_clear_reason`, a pointer initialised to
`""` (data rather than bss).

### b.10 Part E - expected console lines (BLE_DEBUG_WATCHDOG_LOG 1)

ASSERTED (from code; nothing ran). `<...>` = a value that varies. Other bench
flags as committed: `BED_CAPTURE_MOTOR_CURRENTS` 0 (`debug_capture.h:21`), so
the human-readable trip lines print. The trip prints come from
`motor_current_functions.c` and are one of two forms, exclusive within a pass
(a max-load trip calls `reset_motor_current_data()`, which clears
`dynamic_collision_detection_active` (`:238`), so `collision_check()` in the
same `run_motor_supervision()` call does nothing):

Summed-rise trip (`collision_check()`, `:357-364`):
```
Tripped on steady!
Securit margin: <margin>
Motor 0 Delta Current:  <d0>
Motor 1 Delta Current:  <d1>
Motor 2 Delta Current:  <d2>
Motor 3 Delta Current:  <d3>
```
(`Securit` is the existing spelling.) Max-load trip (`max_load_check()`,
`:302-304` or `:318-320`): an empty line, then
```
TRIPPED MAX LOAD
Motor <m> Filtered Current:  <c>
```
Below, `<TRIP LINES>` stands for whichever block printed.

**B1 - app MOVE hold (e.g. HEAD UP into the obstruction), held ~3 s, released,
fresh press later:**
```
<TRIP LINES>
BLE: COLLISION_STOP seq=<N> op=0x01 (latched)
BLE: hold end op=0x01 seq=<N>: <R> refreshes, longest refresh gap <G> ms, longest between passes <B> ms
Once
FAILSAFE: motors energised with nothing commanding - stopped
BLE: collision latch cleared (stop), <K> repeats refused
```
- The first three blocks are the trip pass; `Once` is the next pass.
- `FAILSAFE: ...` ~1.15-1.2 s after the trip, during the reverse (b.11.1). If
  it does **not** appear, the reverse ran its full 500 ms - report it; it would
  mean b.11.1 is wrong.
- `collision latch cleared (stop)` at the release; K about 10 per second held
  after the trip (~30 for 3 s). With an app that ends its hold at the first
  0x06: `(stop)` with small K right after the trip, or `(silence)` ~300 ms
  after the trip.
- The fresh press prints nothing until it ends: then `BLE: hold end op=0x01
  ...` (no `COLLISION_STOP`).
- There is **no** `LEVELED ... -> 0` line at the trip: a move already cleared
  LEVELED when it started (`app_move_start()`, `bed_actions.c:143`).

**B2 - app AUTO LEVEL hold, collision, held ~3 s, released:**
```
Step 1: ROLL and PITCH check
<the auto-level progress lines up to the axis being driven, e.g. "Step 2: ROLL Not Level">
<TRIP LINES>
BLE: COLLISION_STOP seq=<N> op=0x03 (latched)
BLE: hold end op=0x03 seq=<N>: <R> refreshes, longest refresh gap <G> ms, longest between passes <B> ms
Once
BLE: collision latch cleared (stop), <K> repeats refused
```
No `FAILSAFE` line (no reverse, nothing energised). After the release a fresh
press prints `Step 1: ROLL and PITCH check` again.

**B3 - released at ~0.5 s and pressed again (same button), held:**
```
<TRIP LINES>
BLE: COLLISION_STOP seq=<N> op=0x01 (latched)
BLE: hold end op=0x01 seq=<N>: ...
Once
BLE: collision latch cleared (stop), <K ~4-5> repeats refused
FAILSAFE: motors energised with nothing commanding - stopped
```
Then, if the app keeps repeating the re-press past ~1.5 s, the move restarts
with no print; it ends either with `BLE: hold end op=0x01 ...` at the release
or, if it reaches the obstruction again, with a second `<TRIP LINES>` +
`BLE: COLLISION_STOP ...` block. The re-press's refusals during the blink are
not printed (b.7).

**B5 - STOP_ALL while holding, inside the sequence (~0.5 s):**
```
<TRIP LINES>
BLE: COLLISION_STOP seq=<N> op=0x01 (latched)
BLE: hold end op=0x01 seq=<N>: ...
Once
BLE: collision latch cleared (stop_all), <K> repeats refused
FAILSAFE: motors energised with nothing commanding - stopped
```
then, if the held button is still repeating, the move restarts at ~1.5 s (no
print until it ends). **STOP_ALL after the sequence (~2 s):** the same first
four blocks, then `FAILSAFE: ...` at ~1.15-1.2 s, then `BLE: collision latch
cleared (stop_all), <K ~20> repeats refused` at the STOP_ALL, and the next held
repeat restarts the move.

### b.11 Findings while implementing (ASSERTED unless marked)

**b.11.1 The runaway fail-safe cuts the collision reverse after an app
collision.** `motion_failsafe_tick()` (`switch_input.c:804-896`, called every
pass at `app.c:758`) samples every 50 ms (`FAILSAFE_SAMPLE_MS`, `:769`). It
stands down if auto-level or settings mode is active or
`ble_bed_service_app_motion_active()` is true (`:819`), or if a physical
switch is down (`:831-838`); otherwise, with a motor energised, it counts 3
samples (`FAILSAFE_CONFIRM_SAMPLES`, `:770`) and on the 4th turns all motors
off, clears panel state, `active_switch = OFF`, and prints `FAILSAFE: motors
energised with nothing commanding - stopped` (`:885`). After an app collision
both app flags are false (`app_move_active` was already cleared at the trip
before this change; `app_auto_level_active` now is too) and no physical switch
is down, so the reverse started at blink 2 is cut **~150-200 ms** after it
starts instead of running ~500 ms. The same already happens after a physical
collision once the user lets go of the button. For an app MOVE hold before
`e646c3b`, the restarted move had set `app_move_active` again, so the
fail-safe stood down. Not changed (the prompt forbids touching the collision
sequence and switch_input.c). f.3.

**b.11.2 The disconnect release is conditional** (b.6 B4). The prompt lists
"the control-phone disconnect" as a release path; it is one only while the
bed is not idle. After the sequence the latch clears by silence instead. The
outcome B4 asks for holds either way. f.4.

**b.11.3 A narrow window where the latch can outlive the silence timer.** In
MOVE_START, `note_app_commanding()` sets the silence deadline (`:833`) before
`bed_move_start()` + `app_apply_move_outputs()` (I2C) and before
`move_watchdog_arm()` (`:860`), so on a fresh start the silence deadline
falls slightly (the I2C time) before the watchdog expiry; on an identical
repeat the two are microseconds apart. If the app goes silent and a trip lands
in the pass(es) between the silence clear and the watchdog expiry, the latch
is set with `app_commanding` already false, so the silence path never clears
it; the next app press is refused (0x06) until it releases (stop / STOP_ALL /
disconnect), and that press's refusals then re-arm the silence timer. Safe
direction (refuses, never starts). Requires a trip within a few ms of a 300 ms
app silence. Not fixed - the prompt specifies A2 exactly. f.7.

**b.11.4 Two 0x06 in the trip pass** are possible (b.6, common start):
the pending repeat's own seq and then `last_move_seq`. The app should treat
any 0x06 as "end the hold".

### b.12 Options considered (the design record, from AE b.A.3; DECISIONS #48)

- **Option 1 (implemented)** - the latch above.
- **Option 2 - reuse the #36 two-source latch** (set `conflict_inhibit` at a
  trip via a new `bed_note_collision()`). Rejected: the repeats would get
  0x01 FORCE_STOPPED_OTHER_SOURCE ("wall switch is in control" - wrong); the
  latch would refuse the **physical** controller until the app released,
  changing physical behaviour; and it mixes collisions into the two-source
  mechanism the host test covers.
- **Option 3 - keep the flags set instead of clearing them.** Rejected: for
  MOVE it is Option 1 under another name, because `service_move_watchdog()`
  clears `app_move_active` when the bed goes idle at the end of the sequence
  (`:634-646`) and the restart follows; it also contradicted A2's wording.
- **Option 4 - app only.** Rejected as insufficient: a lost notification or an
  app without 0x06 handling leaves the restart in place.
- **Prompt AE A2 is withdrawn** (DECISIONS #48): clearing the flags alone
  restarts the bed (AE b.A.2).

## c. Diffs

### c.1 Source diff (complete; the only source change in the session)

`git show --format= e646c3b -- '*.c' '*.h'`:

~~~~~~ diff
diff --git a/ble_bed_service.c b/ble_bed_service.c
index 8102cd4..fa49ce2 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -175,6 +175,7 @@ void ble_bed_service_encode_status(const bed_status_t *in, bool tilt_fresh, uint
 #define CMD_RESULT_REJECTED_SETTINGS_MODE     0x03
 #define CMD_RESULT_REJECTED_INVALID           0x04
 #define CMD_RESULT_WATCHDOG_STOP              0x05
+#define CMD_RESULT_COLLISION_STOP             0x06 // DECISIONS #48
 #define CMD_RESULT_LEN                        2
 
 // Dead-man watchdog (BLE_GATT_DESIGN.md #5). Milliseconds via sleeptimer,
@@ -280,6 +281,14 @@ static uint8_t  wd_diag_hold_end_seq = 0;
 static uint16_t wd_diag_hold_end_refreshes = 0;
 static uint16_t wd_diag_hold_end_longest_gap_ms = 0;
 static uint32_t wd_diag_hold_end_longest_between_ticks = 0;
+// Collision latch (DECISIONS #48): motion starts refused with COLLISION_STOP
+// since the app last released, and the "latch cleared" line, latched by
+// note_app_stopped_commanding() and printed by ble_bed_service_tick() once
+// the stop that cleared it has been done.
+static uint16_t wd_diag_collision_refused = 0;
+static bool        wd_diag_latch_clear_pending = false;
+static const char *wd_diag_latch_clear_reason = "";
+static uint16_t    wd_diag_latch_clear_refused = 0;
 
 void ble_bed_service_debug_pass_begin(void)
 {
@@ -321,6 +330,16 @@ static bed_direction_t app_move_dir    = BED_DIR_UP;
 // so calling it ten times a second would mean levelling never progressed.
 static bool app_auto_level_active = false;
 
+// Collision latch (DECISIONS #48). Set when a collision trips during an app
+// hold, while the user is still holding the app button. Every MOVE_START and
+// AUTO_LEVEL_START is then answered COLLISION_STOP and starts nothing, until
+// the app releases - an explicit stop, STOP_ALL, the control-phone
+// disconnect, or MOVE_WATCHDOG_MS of silence, i.e. every caller of
+// note_app_stopped_commanding(). Without it the held button's next repeat
+// re-energised the same pair, into the obstruction, during the collision
+// sequence.
+static bool app_collision_latched = false;
+
 // Whether the app is still asking for motion, for the two-source inhibit
 // (#36). Refreshed by every MOVE_START/AUTO_LEVEL_START, INCLUDING refused
 // ones: a refused repeat still means the user is holding the button, so the
@@ -548,11 +567,23 @@ static void note_app_commanding(void)
 }
 
 // The app has stopped asking for motion - an explicit stop, a disconnect, or
-// the silence timeout below. Releases its half of the two-source inhibit.
-static void note_app_stopped_commanding(void)
+// the silence timeout below. Releases its half of the two-source inhibit, and
+// the collision latch (#48). reason is only for the bench print.
+static void note_app_stopped_commanding(const char *reason)
 {
   app_commanding = false;
   bed_note_source_released(BED_SOURCE_APP);
+#if BLE_DEBUG_WATCHDOG_LOG
+  if (app_collision_latched) {
+    wd_diag_latch_clear_pending = true;
+    wd_diag_latch_clear_reason = reason;
+    wd_diag_latch_clear_refused = wd_diag_collision_refused;
+  }
+  wd_diag_collision_refused = 0;
+#else
+  (void)reason;
+#endif
+  app_collision_latched = false;
 }
 
 // Main-loop context. The dead-man period doing its second job: no motion
@@ -565,7 +596,7 @@ static void service_app_commanding(void)
   }
   // Signed difference so this is correct across tick-counter wraparound.
   if ((int32_t)(sl_sleeptimer_get_tick_count() - app_command_deadline_tick) >= 0) {
-    note_app_stopped_commanding();
+    note_app_stopped_commanding("silence");
   }
 }
 
@@ -577,11 +608,26 @@ static void service_move_watchdog(void)
   }
 
   if (any_collision) {
-    // The collision handler owns the stop and its LED sequence from here.
+    // A collision tripped during an app hold (the watchdog is armed only
+    // then). The motors were turned off at the trip, earlier in this pass,
+    // and the collision handler owns the stop and its LED sequence from here.
     // Nothing left for the watchdog to guard, and letting it expire into an
     // app_stop_all() would cut across that sequence.
+    //
+    // The user is still holding the app button, so latch (#48): the repeats
+    // that follow are refused and start nothing. NOT note_app_stopped_
+    // commanding() - the app has not released, and its half of the #36
+    // latch stays as it is.
     move_watchdog_cancel();
     app_move_active = false;
+    app_auto_level_active = false;
+    app_collision_latched = true;
+    send_command_result(control_connection, last_move_seq, CMD_RESULT_COLLISION_STOP);
+#if BLE_DEBUG_WATCHDOG_LOG
+    // After the motors are off and the result is sent.
+    printf("BLE: COLLISION_STOP seq=%u op=0x%02x (latched)\r\n",
+           last_move_seq, wd_diag_refresh_opcode);
+#endif
     return;
   }
 
@@ -616,7 +662,7 @@ static void service_move_watchdog(void)
   move_watchdog_cancel();
   app_move_active = false;
   app_auto_level_active = false;   // hold-to-run auto-level is watchdogged too (#36)
-  note_app_stopped_commanding();
+  note_app_stopped_commanding("watchdog");
   app_stop_all();
   send_command_result(control_connection, last_move_seq, CMD_RESULT_WATCHDOG_STOP);
 #if BLE_DEBUG_WATCHDOG_LOG
@@ -667,7 +713,7 @@ static void process_pending_command(void)
     move_watchdog_cancel();
     app_move_active = false;
     app_auto_level_active = false;
-    note_app_stopped_commanding(); // never inhibited, and releases the latch
+    note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
     app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
 
     // Accepted from ANY role - control or viewer. DECISIONS #12's
@@ -734,6 +780,29 @@ static void process_pending_command(void)
     return;
   }
 
+  // Collision latch (#48). Motion-starting opcodes only, and before the #36
+  // check so the app is told "collision", not "wall switch". Refused while
+  // latched (a collision during this app hold, button not yet released), and
+  // also while ANY collision sequence runs - a quick release and press inside
+  // the sequence, or a press during a physical-controller collision, would
+  // otherwise re-energise a pair while current supervision is off
+  // (switch_input_tick() requires !any_collision).
+  //
+  // note_app_commanding() as in the #36 refusal: a refused repeat still means
+  // the user is holding, so the silence timeout must not release the latch.
+  // No bed_control call, no last_move_seq update, no watchdog arm.
+  if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
+      && (app_collision_latched || any_collision)) {
+    note_app_commanding();
+    send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);
+#if BLE_DEBUG_WATCHDOG_LOG
+    if (wd_diag_collision_refused < 0xFFFF) {
+      wd_diag_collision_refused++;
+    }
+#endif
+    return;
+  }
+
   // Two-source conflict inhibit (#36). Motion-starting opcodes only: stops
   // are never inhibited, LIGHTS is not motion, and REFRESH_TILT returned
   // above.
@@ -799,7 +868,7 @@ static void process_pending_command(void)
       }
       move_watchdog_cancel();
       app_move_active = false;
-      note_app_stopped_commanding();
+      note_app_stopped_commanding("stop");
       bed_move_stop(BED_SOURCE_APP); // stop-class: always reports true
       app_apply_stop_outputs();
       result = CMD_RESULT_ACCEPTED;
@@ -838,7 +907,7 @@ static void process_pending_command(void)
       }
       move_watchdog_cancel();
       app_auto_level_active = false;
-      note_app_stopped_commanding();
+      note_app_stopped_commanding("stop");
       bed_auto_level_stop(BED_SOURCE_APP); // stop-class: always reports true
       app_apply_stop_outputs();            // clears the red/green auto-level LEDs
       result = CMD_RESULT_ACCEPTED;
@@ -1023,7 +1092,7 @@ void ble_bed_service_tick(void)
     move_watchdog_cancel();
     app_move_active = false;
     app_auto_level_active = false;
-    note_app_stopped_commanding(); // the app is gone - release its half of the latch
+    note_app_stopped_commanding("disconnect"); // the app is gone - release its half of the latch
     app_stop_all();
   }
 
@@ -1045,6 +1114,13 @@ void ble_bed_service_tick(void)
            (unsigned)wd_diag_hold_end_refreshes, (unsigned)wd_diag_hold_end_longest_gap_ms,
            (unsigned long)sl_sleeptimer_tick_to_ms(wd_diag_hold_end_longest_between_ticks));
   }
+  // The collision latch (#48) was released above; the stop that released it
+  // is done.
+  if (wd_diag_latch_clear_pending) {
+    wd_diag_latch_clear_pending = false;
+    printf("BLE: collision latch cleared (%s), %u repeats refused\r\n",
+           wd_diag_latch_clear_reason, (unsigned)wd_diag_latch_clear_refused);
+  }
 #endif
 
   if (status_subscriber_count == 0) {
~~~~~~

### c.2 Docs diff (complete)

`git show --format= e646c3b -- docs/ ':!docs/prompts'`. Not repeated:
the prompt file `docs/prompts/PROMPT_AF_collision-latch-app-hold.md`
(added in the same commit, 130 lines, the reviewer's own text, unchanged),
and the `docs/SESSION_LOG.md` entry (standing exception: the log ships
with this file).

~~~~~~ diff
diff --git a/docs/BLE_GATT_DESIGN.md b/docs/BLE_GATT_DESIGN.md
index a68b038..343feb4 100644
--- a/docs/BLE_GATT_DESIGN.md
+++ b/docs/BLE_GATT_DESIGN.md
@@ -367,6 +367,48 @@ Notify, 2 bytes: `{ seq u8, result u8 }`, echoing the command's seq.
 | 0x03 | REJECTED_SETTINGS_MODE | "Bed is in settings mode" |
 | 0x04 | REJECTED_INVALID | malformed / unknown opcode |
 | 0x05 | WATCHDOG_STOP | connection stalled; movement stopped |
+| 0x06 | COLLISION_STOP | Collision: the app ends its hold and shows the collision screen (see below) |
+
+**COLLISION_STOP (0x06), DECISIONS #48.** Sent in two cases:
+
+1. **Once, at the trip**, when a collision trips during an app hold (MOVE or
+   AUTO LEVEL - the dead-man watchdog is armed), echoing `last_move_seq` (the
+   seq of the last accepted MOVE_START / AUTO_LEVEL_START of that hold). The
+   motors are already off; the collision sequence (LED blink, the reverse for a
+   move, ~1.5 s) runs as for any collision. No WATCHDOG_STOP follows: the
+   watchdog is cancelled at the trip.
+2. **In answer to every MOVE_START and AUTO_LEVEL_START** from the control
+   connection while the firmware's collision latch is set (the trip above, and
+   the app has not released since) **or while any collision sequence runs**
+   (Bed Status flag bit 0 set - including one from the physical controller),
+   echoing that command's own seq. The command starts nothing and does not arm
+   the watchdog.
+
+The latch clears when the app releases: MOVE_STOP, AUTO_LEVEL_STOP, STOP_ALL
+(from either connection), the control connection dropping while the app holds
+the bed, or `MOVE_WATCHDOG_MS` (300 ms) with no MOVE_START / AUTO_LEVEL_START
+from the app. A press after that, once the collision sequence has ended, is a
+normal start. Checked after the viewer and settings-mode rejections and before
+the #36 two-source check, so a viewer still gets 0x02, settings mode 0x03, and
+the app is told "collision", not "wall switch".
+
+App behaviour: on 0x06, end the hold (stop repeating; a MOVE_STOP /
+AUTO_LEVEL_STOP is fine) and show the collision screen until the user lifts
+the finger; a new press is a new hold. The firmware does not rely on this for
+the hold that collided: its repeats are refused until it releases. It does
+rely on it for one case: a **new** press made while the collision sequence is
+still running (released and pressed again within ~1.5 s) is refused only
+until the sequence ends, because it did not set the latch - if the app keeps
+repeating that press past the end of the sequence, the next repeat starts the
+bed (with current supervision running again).
+
+**Protocol version: additive within v1, no version change.** The code was
+unused and the byte layout is unchanged; a client that does not know 0x06
+gets a result code it has no entry for, as for any future code. The
+Protocol Version value stays `01`; the after-first-ship rule in §6 is
+unchanged by this. The app repository's copy of this document was written
+earlier and will be brought into line by the next app prompt; **this copy is
+authoritative.**
 
 Latency budget for the perceived press→motion path: connection interval
 15–30 ms (write lands next event) + one firmware tick + motor turn-on ≈
@@ -498,6 +540,8 @@ app command force-stops; STOP_ALL from either).
    unchanged; SET while moving returns BUSY; value survives power cycle.
 7. Pairing window: bond attempt outside settings mode rejected; inside, ok.
 8. Collision during app-commanded move: trip + reverse + LED sequence
-   unchanged; app sees collision flag within one notify interval.
+   unchanged; app sees collision flag within one notify interval, and gets
+   COLLISION_STOP (0x06, §7); the held button starts nothing until released
+   (DECISIONS #48, REGRESSION §5).
 9. OTA: full DFU cycle; DFU attempt while moving refused by app UI;
    power-pull mid-DFU recovers to AppLoader (re-flashable, not bricked).
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 8aa2591..c2f7d10 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2418,3 +2418,136 @@ the pass after the debounced press, as before.
 ### Status
 
 Builds clean (no warnings). **Not run on the bench.**
+
+## 48. A collision during an app hold latches the app path: COLLISION_STOP (0x06) until the app releases
+
+Date: 2026-11-02. Reviewer's Prompt AF, implementing **Option 1** of the
+Prompt AE handoff (`fw_HANDOFF_2026-11-02_01_...`, b.A.3), accepted by the
+reviewer and Andrew on 2026-11-02. **Prompt AE part A2 is withdrawn**: its
+"clear `app_move_active` and `app_auto_level_active`, so the following repeats
+start nothing" was wrong - the AE handoff (b.A.2) showed that clearing the
+flags is what lets a repeat restart the bed.
+
+### The defect (from code, not seen on a bed)
+
+A trip turns all motors off and sets `any_collision`
+(`motor_current_functions.c:306-307, 322-323, 366-367`). Before this entry,
+`service_move_watchdog()` then only cancelled the watchdog and cleared
+`app_move_active`, and sent no Command Result. The app repeats MOVE_START
+every 100 ms while its button is held, so the next repeat found
+`app_move_active` false and called `bed_move_start()` +
+`app_apply_move_outputs()`: **the same pair re-energised in the original
+direction, into the obstruction, ~100 ms after the trip**, while current
+supervision is gated off (`switch_input.c:934` requires `!any_collision`).
+Nothing on the app command path read `any_collision`. (An AUTO LEVEL hold did
+not restart, by accident: `app_auto_level_active` stayed set, so its repeats
+only refreshed the watchdog.)
+
+### Decision (Option 1)
+
+A collision latch on the app path, in `ble_bed_service.c` only.
+
+- **Result code** `CMD_RESULT_COLLISION_STOP 0x06` (BLE_GATT_DESIGN.md §7).
+- **At the trip** - the `if (any_collision)` branch of
+  `service_move_watchdog()` (`ble_bed_service.c:610-631`), which runs only
+  while the watchdog is armed, i.e. only during an app MOVE or AUTO LEVEL
+  hold: cancel the watchdog, clear `app_move_active` and
+  `app_auto_level_active`, set `app_collision_latched`, send 0x06 with
+  `last_move_seq`. `note_app_stopped_commanding()` is **not** called: the
+  user is still holding, and the app's half of the #36 latch stays as it is.
+- **Refusal** - in `process_pending_command()`, after the viewer and
+  settings-mode checks and before the #36 conflict check
+  (`ble_bed_service.c:783-805`), for MOVE_START and AUTO_LEVEL_START only:
+  if `app_collision_latched || any_collision` -> `note_app_commanding()`, send
+  0x06 with the command's own seq, return. No bed_control call, no
+  `last_move_seq` update, no watchdog arm.
+- **Release** - `note_app_stopped_commanding()` clears the latch
+  (`ble_bed_service.c:572-587`), so every existing release clears it. Its
+  callers after the change: MOVE_STOP (`:871`), AUTO_LEVEL_STOP (`:910`),
+  STOP_ALL (`:716`, from either role), the control-phone disconnect stop
+  (`:1095`), the silence timeout in `service_app_commanding()` (`:599`;
+  `MOVE_WATCHDOG_MS` = 300 ms after the last MOVE_START / AUTO_LEVEL_START,
+  refused ones included), and the watchdog expiry (`:665`), which cannot run
+  while latched because nothing arms the watchdog while latched.
+- **The `|| any_collision` term stays** (reviewer ruling). It also refuses an
+  app motion start during the collision sequence after ANY trip, including a
+  physical-controller collision with the app idle, and a repeat processed in
+  the trip pass itself (`process_pending_command()` runs before
+  `service_move_watchdog()` in the same tick). It does not set the latch.
+  Window, from code: `any_collision` is set at the trip (pass N) and cleared by
+  the collision block (`app.c:609-618`) in the first pass after the third
+  expiry of the 500 ms periodic LED timer, which that block starts at the top
+  of pass N+1 (`app.c:589-597`, `COLLISION_BLINK_CYCLES 3`). So **1500 ms,
+  plus the gap from the trip to pass N+1, plus the wake latency after the
+  third timer expiry** - each at most about one BLE connection or advertising
+  interval (15-30 ms) when the CPU sleeps between passes, less when it is
+  awake.
+- **The physical controller is not affected**: no change to switch_input.c,
+  bed_control.c, the collision sequence in app.c or the motor-current code. A
+  physical collision sends no Command Result (no command); the app learns of
+  it from Bed Status flag bit 0 as before.
+- Bench print behind `BLE_DEBUG_WATCHDOG_LOG`: `BLE: COLLISION_STOP seq=N
+  op=0x0M (latched)` at the trip, and `BLE: collision latch cleared (<reason>),
+  K repeats refused` (reason: stop / stop_all / disconnect / silence; K counts
+  the 0x06 refusals since the app last released) - both printed after the
+  stop and the Command Result, main-loop context.
+
+### Options rejected (AE handoff b.A.3)
+
+- **Option 2, reuse the #36 two-source latch** (set bed_control's
+  `conflict_inhibit` at a trip). The repeats would get 0x01
+  FORCE_STOPPED_OTHER_SOURCE, which tells the user "wall switch is in
+  control" - wrong; the latch would also refuse the **physical** controller
+  until the app released, changing physical behaviour; and it mixes a
+  collision into the two-source mechanism the host test covers.
+- **Option 3, keep the flags set instead of clearing them.** For MOVE this is
+  Option 1 under another name: `service_move_watchdog()` clears
+  `app_move_active` when the bed goes idle at the end of the sequence, and
+  the restart follows. It also contradicted A2's wording.
+- **Option 4, app only** (send 0x06, the app stops repeating). Not sufficient:
+  a lost notification, or an app that does not handle 0x06, leaves the
+  restart in place. The firmware rule is needed whatever the app does.
+
+### Interactions found while implementing (ASSERTED from code)
+
+- **Sleep veto (#45).** While latched the watchdog is not armed, so
+  `ble_bed_service_app_hold_active()` is false and the app hold no longer
+  vetoes sleep. Nothing the app commands moves under the latch. What can move
+  is the collision sequence's own reverse (a move only; auto-level has none,
+  `dual_motor_control.c:300-343`, default case), which `set_dual_motors_on()`'s
+  `motor_state[].motor_on` or the `motor_outputs_maybe_on()` shadow keep in EM0
+  (`app_is_ok_to_sleep()`, `app.c:510-560`); and the physical controller after the sequence, which has
+  its own vetoes. Main-loop passes still run on every BLE event, so refusals
+  and the silence clear are serviced.
+- **The runaway fail-safe shortens the reverse after an app collision.**
+  During the reverse `ble_bed_service_app_motion_active()` is false (both app
+  flags cleared at the trip) and no physical switch is down, so
+  `motion_failsafe_tick()` (`switch_input.c:804-896`; 50 ms samples, 3 to
+  confirm) de-energises the reverse ~150-200 ms after it starts and prints
+  `FAILSAFE: motors energised with nothing commanding - stopped`. The same
+  already happens after a physical collision once the button is released.
+  Not changed here (QUESTIONS in the AF handoff).
+- **The disconnect release is conditional.** `handle_connection_closed()`
+  sets `disconnect_stop_pending` only if `bed_active_source() ==
+  BED_SOURCE_APP` (`ble_bed_service.c:993`). During the collision sequence
+  that holds; after it, `bed_control_tick()` has returned the source to NONE,
+  so a drop then does not go through the disconnect stop and the latch clears
+  by the silence timeout instead, 300 ms after the last write the bed
+  received. Either way it is clear before a reconnect is expected to deliver a
+  command, and a command that did arrive while latched would be refused.
+- **STOP_ALL is accepted from a viewer** (#4, #12) and so clears the latch
+  even if the control phone's user is still holding. A still-repeating
+  control app would then be refused only while the sequence runs and would
+  restart after it - the same as a viewer STOP_ALL during an ordinary hold
+  today.
+- **A new press inside the sequence** (released and pressed again within
+  ~1.5 s) is refused by `any_collision` but does not set the latch: if the app
+  keeps repeating it past the end of the sequence, the next repeat starts the
+  bed, with supervision running.
+
+### Status
+
+Builds clean (no warnings, `BLE_DEBUG_WATCHDOG_LOG` 1 and 0). The host test
+(`test/bed_control_test.c`) is unchanged and passes; it cannot reach this
+code (it compiles bed_control.c only). **Not run on the bench.** REGRESSION §5,
+items marked #48.
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index 0a0a7a1..58ff157 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -187,6 +187,56 @@ log `sampler_bench.log`; full figures in DECISIONS #41 "Bench result"):**
       ceiling: verify `TRIPPED MAX LOAD` on a deliberately stalled motor
 - [ ] ⚡ BLE link stays up (no disconnect) through the collision sequence
 
+**Collisions during app holds (DECISIONS #48).** App connected as CONTROL,
+`BLE_DEBUG_WATCHDOG_LOG` 1, UART logged. Use the rigid obstruction; one
+pair is enough. With an app that does not yet handle COLLISION_STOP (0x06)
+it keeps repeating while the finger is down, which is the case these items
+test. With an app that does handle it, the hold ends at the first 0x06 and
+the latch clears at once (`stop`) or 300 ms later (`silence`); the motors
+must do the same in both cases.
+
+- [ ] 5.A1 Collision during an app **MOVE** hold (B1). Raise into the
+      obstruction from the app, keep the finger down ~3 s after the trip,
+      release, wait, press again (away from the obstruction).
+      Observe: trip lines, then **in the same burst** `BLE: COLLISION_STOP
+      seq=N op=0x01 (latched)` and `BLE: hold end op=0x01 seq=N ...`; then
+      `Once`; LED blink; the brief reverse at blink 2 (expect a
+      `FAILSAFE: motors energised with nothing commanding - stopped` line
+      during it - ASSERTED, see #48); everything off at ~1.5 s. **The pair
+      does not re-energise toward the obstruction at any point while the
+      finger is held.** On release: `BLE: collision latch cleared (stop), K
+      repeats refused` with K ≈ 10 per second held. The new press moves
+      normally (no 0x06). App: 0x06 for the held button (if it shows result
+      codes); Bed Status bit 0 set for ~1.5 s.
+- [ ] 5.A2 Collision during an app **AUTO LEVEL** hold (B2). Obstruct a
+      levelling pair, keep holding ~3 s after the trip, release, press again
+      (obstruction removed). Observe: trip lines, `BLE: COLLISION_STOP seq=N
+      op=0x03 (latched)`, `BLE: hold end op=0x03 ...`, `Once`; LED blink, **no
+      reverse and no motion at all** until the release; `BLE: collision latch
+      cleared (stop), K repeats refused`. The new press starts a fresh
+      auto-level run (`Step 1` prints).
+- [ ] 5.A3 Release and re-press inside the collision sequence (B3). As 5.A1,
+      but release ~0.5 s after the trip and press the **same** button again at
+      once, and hold. Observe: `BLE: collision latch cleared (stop), K repeats
+      refused` at the release; **nothing moves toward the obstruction while
+      the LEDs blink** (the reverse at blink 2 may still run); at the end of
+      the sequence (~1.5 s after the trip) an app that is still repeating
+      **starts the move again** - expected under #48 (the re-press did not
+      set the latch), with supervision running, so it trips again at the
+      obstruction. Record the time from the end of the blink to the move.
+- [ ] 5.A4 STOP_ALL during the latch (B5). As 5.A1, but while still holding
+      after the trip press the app's STOP (STOP_ALL) - once inside the blink
+      sequence, once after it. Observe: `BLE: collision latch cleared
+      (stop_all), K repeats refused`; inside the sequence nothing moves
+      toward the obstruction until it ends; after it, an app still repeating
+      the held move **starts the move again** on its next repeat (as STOP_ALL
+      during any ordinary hold). App: 0x00 for the STOP_ALL.
+- [ ] 5.A5 Physical-controller collision unchanged: raise into the obstruction
+      from the switch panel with the app connected and idle. No `BLE:
+      COLLISION_STOP` line, no Command Result; Bed Status bit 0 set for
+      ~1.5 s. An app MOVE press made and released during the blink gets 0x06
+      and starts nothing.
+
 ## 6. Auto-level
 
 Starting condition for 6.1: frame tilted several degrees in both roll and
~~~~~~

## d. Raw output of commands run

### d.1 How this log was produced

This session did not run its commands through a logging wrapper and saved no
log file. Every shell command it ran is listed below, **in the order run**,
numbered (section e cites these numbers). For each one:

- **Output shown** - copied from the session's own command output, unedited.
- **Output not reproduced** - said plainly where it applies. This is used only
  for long file-content reads (`sed -n` ranges, `cat`, `head` of source and
  docs files), where the output was the file's own lines. No saved copy of
  that output exists, and it is not re-created here from any other file. Every
  such read is of a file at `4d1fef3` (before the edit) unless it says `e646c3b`;
  the line ranges are given so a reader with the repo can see the same text.

Paths: `$R` = the repo root
`C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller`; `$S` =
the session scratchpad
`C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/434d4749-28cb-4360-85e6-38c8917c0bb4/scratchpad`.
Edits made with the Edit/Write tools are not commands; their result is the
diff in section c. Three Read-tool reads (not shell commands) are noted where
they happened. No secrets appear in any output; nothing is redacted.

### d.2 Session command log

**1.** The prompt's own text, printed first, is not repeated: it is the
committed `docs/prompts/PROMPT_AF_collision-latch-app-hold.md`.

```
$ cd $R && cat docs/prompts/PROMPT_AF_collision-latch-app-hold.md && date && git log --oneline -8 && git status
(the prompt text, then)
Mon Nov  2 08:55:03 PST 2026
4d1fef3 Handoff 2026-11-02 01: Prompt AE (LEVELED bit, amber after STOP_ALL, BED LIGHTS on release, PRE_SHIP rows; Part A stopped)
e3cc865 PRE_SHIP: rows F20-F25 and F17 extended (reviewer rulings on Prompt AC)
3fb1a0c BED LIGHTS toggles the lights on the release of a short press (DECISIONS #47)
f3a762e Restore the amber indicator after app_stop_all() when the lights are on
ef36909 Bed Status bit 4 = LEVELED; "Bed is level" prints on success (DECISIONS #46)
01315cd Handoff 2026-11-01 02: Prompt AC (AA bench record, PRE_SHIP.md, feature groundwork report)
fad70fc Docs: Prompt AA bench result (DECISIONS #45, REGRESSION 6.9), create PRE_SHIP.md
6796e6b Handoff 2026-11-01 01: fix one section cross-reference (d.5 -> d.4)
On branch main
Your branch is up to date with 'origin/main'.

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/prompts/PROMPT_AF_collision-latch-app-hold.md

no changes added to commit (use "git add" and/or "git commit -a")
```

**2.** (`git fetch` printed nothing)

```
$ cd $R && git fetch origin 2>&1; git rev-parse HEAD origin/main; git log -1 --format=%P HEAD; git show --stat HEAD | head -20; wc -l docs/MIGRATION_STATUS.md docs/DECISIONS.md ble_bed_service.c docs/handoffs/fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md
4d1fef35d03dc324036451ba6e0a97c5165ae5f7
4d1fef35d03dc324036451ba6e0a97c5165ae5f7
e3cc8652220d9dba7868ab3728ba81f47acb9afb
commit 4d1fef35d03dc324036451ba6e0a97c5165ae5f7
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Mon Nov 2 08:48:29 2026 -0800

    Handoff 2026-11-02 01: Prompt AE (LEVELED bit, amber after STOP_ALL, BED LIGHTS on release, PRE_SHIP rows; Part A stopped)
    
    Session log entry and the reviewer handoff for commits ef36909..e3cc865.
    Part A was not implemented: clearing the app hold flags does not stop the
    held button's repeats, so a further rule is needed; options are in the
    handoff (b.A), and Part A will come in a new prompt.
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

 docs/SESSION_LOG.md                                |   54 +
 ..._collision-result-leveled-bit-lights-release.md | 1889 ++++++++++++++++++++
 2 files changed, 1943 insertions(+)
   123 docs/MIGRATION_STATUS.md
  2420 docs/DECISIONS.md
  1391 ble_bed_service.c
  1889 docs/handoffs/fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md
  5823 total
```

**3.** Output not reproduced: the whole of `docs/MIGRATION_STATUS.md` (123
lines), then the heading lines of the AE handoff (`grep -n "^#"`, about 55
lines).

```
$ cd $R && cat docs/MIGRATION_STATUS.md; grep -n "^#" docs/handoffs/fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md | head -80
```

Read tool: the AE handoff lines 1-252 (sections a and b.A); `ble_bed_service.c`
lines 1-1156.

**4.** The two greps are shown; the trailing `sed -n 560,640p app.c` (81 lines
of `app.c`, the collision block) is not reproduced.

```
$ cd $R && grep -n "BLE_DEBUG_WATCHDOG_LOG" *.h *.c | head; echo ----; grep -n "any_collision\|app_hold_active\|app_motion_active" *.c *.h; echo ----; sed -n 560,640p app.c
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
ble_bed_service.h:38:#if BLE_DEBUG_WATCHDOG_LOG
app.c:568:#if BLE_DEBUG_WATCHDOG_LOG
app.c:775:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:249:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:250:// Watchdog-stop diagnostics (BLE_DEBUG_WATCHDOG_LOG, ble_bed_service.h).
ble_bed_service.c:482:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:525:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:609:#if BLE_DEBUG_WATCHDOG_LOG
ble_bed_service.c:622:#if BLE_DEBUG_WATCHDOG_LOG
----
app.c:532:  if (ble_bed_service_app_hold_active()) {
app.c:580:  if (any_collision){
app.c:583:    // Note: motors are shut off in "collision_check" function. All functions that are supervised with the "any_collision" flag are suspended until the flag is cleared.
app.c:587:    // (3) Turn off motors, LEDs, and reset active switch and "any_collision" flags
app.c:617:      any_collision = false;
app.c:626:  if ((auto_level_active) && (!any_collision) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
app.c:672:      } else if (!any_collision) { // Current axis is now level, switch to next axis after brief pause
auto_level.c:772:          if (any_collision){
bed_status.c:143:  out->any_collision = any_collision;
ble_bed_service.c:41:#include "motor_current_functions.h" // any_collision - cancels the move watchdog
ble_bed_service.c:81:  if (in->any_collision)    flags |= 0x01;
ble_bed_service.c:579:  if (any_collision) {
ble_bed_service.c:1007:bool ble_bed_service_app_motion_active(void)
ble_bed_service.c:1012:bool ble_bed_service_app_hold_active(void)
motor_current_functions.c:79:bool any_collision = false; // Boolean to indicate a collision has occurred on any motor
motor_current_functions.c:307:          any_collision = true;
motor_current_functions.c:323:          any_collision = true;
motor_current_functions.c:367:            any_collision = true;
motor_current_functions.c:390:    if (any_collision) {
motor_current_functions.c:401:  if (any_collision) {
switch_input.c:517:  if((!any_collision) && (!in_settings_mode)){
switch_input.c:819:  if (auto_level_active || in_settings_mode || ble_bed_service_app_motion_active()) {
switch_input.c:876:  // only runs when ble_bed_service_app_motion_active() is already false.
switch_input.c:934:  if ((active_switch != OFF) && !any_collision && (!auto_level_active) && (!in_settings_mode)){
app.h:99: * any_collision already works between motor_current_functions.c and app.c.
bed_status.h:60:  bool any_collision;
ble_bed_service.h:73:bool ble_bed_service_app_motion_active(void);
ble_bed_service.h:79:bool ble_bed_service_app_hold_active(void);
motor_current_functions.h:17:extern bool any_collision;
----
(sed -n 560,640p app.c - not reproduced)
```

**5.** The grep is shown; `sed -n 500,560p app.c` (61 lines,
`app_is_ok_to_sleep()`) and `sed -n 280,410p motor_current_functions.c` (131
lines: `max_load_check()`, `collision_check()`, `run_motor_supervision()`)
are not reproduced.

```
$ cd $R && grep -n "COLLISION_BLINK_CYCLES\|start_500ms_LED_timer\|LED_500ms_timer" *.c *.h | head -20; echo ----; sed -n 500,560p app.c; echo -----; sed -n 280,410p motor_current_functions.c
app.c:84:#define COLLISION_BLINK_CYCLES 3 // Number of times to blink LEDs for a collision
app.c:118:static sl_sleeptimer_timer_handle_t LED_500ms_timer; // For collision detected LED blinks
app.c:186:static void start_500ms_LED_timer()
app.c:189:  sl_sleeptimer_start_periodic_timer(&LED_500ms_timer, ticks, LED_timer_callback, NULL, 0, 0);
app.c:581:    sl_sleeptimer_is_timer_running(&LED_500ms_timer, &is_LED_timer_running);
app.c:592:      start_500ms_LED_timer();
app.c:609:    if (LED_blink_counter >= COLLISION_BLINK_CYCLES){
app.c:611:      sl_sleeptimer_stop_timer(&LED_500ms_timer);
----
(the two sed ranges - not reproduced)
```

**6.** Output not reproduced: `app.c` 180-195, `reverse_motor_direction_and_start()`
and its context (`grep -A30`, up to 60 lines), and the first 176 lines of
`bed_control.c` (the whole file).

```
$ cd $R && sed -n 180,195p app.c; grep -n "reverse_motor_direction_and_start" -A30 bed_actions.c dual_motor_control.c app.c | grep -v "^--$" | head -60; echo ----; cat bed_control.c | head -200
```

**7.**

```
$ cd $R && grep -n "bed_control_tick\|ble_bed_service_tick\|switch_input_tick\|settings_mode_tick" app.c; grep -n "^bool app_bed_is_idle\|^void app_stop_all\|^void app_stop_movement\|^void app_move_start\|^void app_apply_move_outputs\|^void app_apply_stop_outputs\|^void app_move_stop" bed_actions.c
734:  settings_mode_tick();
741:  switch_input_tick();
772:  bed_control_tick();
773:  ble_bed_service_tick();
139:void app_move_start(bed_target_t target, bed_direction_t dir)
165:void app_move_stop(void)
239:void app_stop_all(void)
299:void app_apply_move_outputs(bed_target_t target)
334:void app_apply_stop_outputs(void)
368:void app_stop_movement(void)
397:bool app_bed_is_idle(void)
```

**8.** Output not reproduced: `bed_actions.c` 130-410 (281 lines) and `app.c`
725-790 (66 lines, the end of the main loop).

```
$ cd $R && sed -n 130,410p bed_actions.c; sed -n 725,790p app.c
```

**9.** Output not reproduced: `motion_failsafe_tick()` (`grep -A60`), and
`switch_input.c` 505-530 and 925-945.

```
$ cd $R && grep -n "void motion_failsafe_tick" -A60 *.c | head -90; sed -n 505,530p switch_input.c; sed -n 925,945p switch_input.c
```

**10.**

```
$ cd $R && grep -n "define FAILSAFE_" switch_input.c; grep -n "^## \|^### " docs/BLE_GATT_DESIGN.md | sed -n 1,60p; grep -n "^## [0-9]*\.\|^### #\|^## #" docs/DECISIONS.md | tail -6; ls test; grep -n "^##\|^###" docs/REGRESSION.md | tail -40
769:#define FAILSAFE_SAMPLE_MS        50
770:#define FAILSAFE_CONFIRM_SAMPLES  3   // ~150 ms of agreement before acting
15:## 1. Design goals (ranked)
31:## 2. SDK and stack configuration
59:## 3. Service topology
69:### Tilt Bed Service characteristics
84:## 4. Command characteristic (0001)
178:## 5. Dead-man watchdog (new safety element, firmware-side)
207:## 6. Bed Status characteristic (0003)
237:### LEVELED (bit 4)
271:### Tilt sign conventions
308:### Protocol version and this change
358:## 7. Command Result characteristic (0002)
375:## 8. Connections, roles, bonding, fast connect
420:## 9. Settings RPC characteristic (0005)
446:## 10. OTA (D2, designed now)
462:## 11. Firmware module plan (D1)
484:## 12. Bench verification plan (append to REGRESSION.md at D1)
1843:## 42. Pairing window: new bonds only while settings mode is active (implements #19)
1937:## 43. Bond store full: overwrite the least recently used bond; the firmware disconnects a phone whose pairing it refused
2061:## 44. STOP in settings mode cancels any hold in progress and stays in settings mode
2119:## 45. During an app hold the CPU stays in EM0
2256:## 46. Bed Status flag bit 4 = LEVELED; "Bed is level" prints on every successful auto-level end
2329:## 47. BED LIGHTS toggles the lights on the release of a short press
bed_control_test.c
23:## Test record header
36:## 0. Setup / preconditions
48:## 1. Power-on & initialization
58:## 2. Manual motor control (button matrix)
84:## 2b. Per-motor PWM timer mapping (D1 migration, DECISIONS #25) — one-time at D1
98:## 3. Double-press rejection
105:## 3b. Switch sampler (DECISIONS #41)
157:## 4. Limit switch inference
172:## 5. Collision detection
190:## 6. Auto-level
285:## 7. Settings mode
382:## 8. Bed lights
410:## 9. Cross-checks & UART health
418:## Result
```

**11.** Only the last grep's output is shown. `dual_motor_control.c` 330-360,
`switch_input.c` 860-880 and `any_motor_energised_now()` (`grep -A15`) are not
reproduced.

```
$ cd $R && sed -n 330,360p dual_motor_control.c; sed -n 860,880p switch_input.c; grep -n "any_motor_energised_now" -A15 switch_input.c | head -30; grep -n -i "reverse" docs/DECISIONS.md | head -30
(the first three reads - not reproduced)
147:- **Enables targeted per-pair collision relief**: reverse only the pair
1328:Reverses the decision recorded in #35, on the test #35 itself set: *"What would
2267:(any move, ALL, a new auto-level run, the collision reverse), on a collision
2295:  the collision reverse (`app.c:605`); settings-mode entry
```

**12.** `switch_input.c` 880-905 (the fail-safe's tail and its print) is not
reproduced. The DECISIONS grep printed nothing. The REGRESSION grep is shown.

```
$ cd $R && sed -n 880,905p switch_input.c; grep -n -i "collision" docs/DECISIONS.md | grep -i "fail-safe\|failsafe\|runaway" | head; grep -n -i "collision" docs/REGRESSION.md | head -30
(switch_input.c 880-905 - not reproduced)
44:- [ ] Test obstruction available (rigid block for fast collision, pillow/box
45:      for soft collision)
172:## 5. Collision detection
180:- [ ] Collision during auto-level: motors stop, LED sequence runs
182:- [ ] ⚡ After the collision sequence completes, normal button operation
184:- [ ] Collision sensitivity sanity: at HIGH rung, the rigid-obstruction trip
188:- [ ] ⚡ BLE link stays up (no disconnect) through the collision sequence
401:- [ ] Lights state survives a motor move and a collision sequence
402:      (amber restored after collision blink)
427:| 5 Collision | | |
```

**13.** (no output)

```
$ cd $R && grep -rn -i "failsafe\|fail-safe" docs/*.md | grep -i "collision\|reverse" | head -20; grep -n "FAILSAFE" docs/PRE_SHIP.md | head
```

**14.**

```
$ cd $R && grep -rn -i "reverse" docs/SESSION_LOG.md docs/PRE_SHIP.md docs/REGRESSION.md docs/REGRESSION_RECORD_PRE_STAGEC.md | head -20; sed -n 172,190p docs/REGRESSION.md
docs/SESSION_LOG.md:117:- Known limitations carried forward: no motor reverse after a collision
docs/PRE_SHIP.md:88:| No reverse after a collision during auto-level | REGRESSION §5 note; REFACTOR_PLAN P1.5 |
docs/REGRESSION.md:175:      (3 cycles), motors briefly reverse starting at blink 2, then everything
docs/REGRESSION.md:181:      (note: no reverse in auto-level — known limitation, REFACTOR_PLAN P1.5)
docs/REGRESSION_RECORD_PRE_STAGEC.md:128:      (3 cycles), motors briefly reverse starting at blink 2, then everything
docs/REGRESSION_RECORD_PRE_STAGEC.md:134:      (note: no reverse in auto-level — known limitation, REFACTOR_PLAN P1.5)
## 5. Collision detection

- [ ] ⚡ Raise one side into a rigid obstruction: motors stop, all LEDs blink
      (3 cycles), motors briefly reverse starting at blink 2, then everything
      stops and LEDs clear
- [ ] Lower one side onto a rigid obstruction: same trip sequence
- [ ] Soft obstruction (pillow/box) while raising: trips via baseline-margin
      element (may take longer than rigid — confirm it does trip)
- [ ] Collision during auto-level: motors stop, LED sequence runs
      (note: no reverse in auto-level — known limitation, REFACTOR_PLAN P1.5)
- [ ] ⚡ After the collision sequence completes, normal button operation
      resumes without power cycle
- [ ] Collision sensitivity sanity: at HIGH rung, the rigid-obstruction trip
      is noticeably faster/lighter than at LOW rung
- [ ] (Optional, destructive-adjacent — skip in routine runs) Max-load
      ceiling: verify `TRIPPED MAX LOAD` on a deliberately stalled motor
- [ ] ⚡ BLE link stays up (no disconnect) through the collision sequence
```

**15.** `head -80 test/bed_control_test.c` (80 lines) is not reproduced. Its
header comment says the test compiles standalone with
`gcc -std=c99 -Wall -I.. -o bed_control_test bed_control_test.c ../bed_control.c`.
The rest is shown.

```
$ cd $R && head -80 test/bed_control_test.c; wc -l test/bed_control_test.c; ls test; grep -n "^#include" ble_bed_service.c ble_bed_service.h
(head -80 test/bed_control_test.c - not reproduced)
277 test/bed_control_test.c
bed_control_test.c
ble_bed_service.c:28:#include "ble_bed_service.h"
ble_bed_service.c:30:#include <stdio.h>
ble_bed_service.c:31:#include <string.h>
ble_bed_service.c:33:#include "app_assert.h"
ble_bed_service.c:34:#include "gatt_db.h"
ble_bed_service.c:35:#include "sl_bluetooth.h"            // SL_BT_CONFIG_MAX_CONNECTIONS_SUM - sizes refused_pairing_conn[]
ble_bed_service.c:36:#include "sl_sleeptimer.h"
ble_bed_service.c:38:#include "auto_level.h"              // tilt_telemetry_arm_burst() - triggers T1 and T2
ble_bed_service.c:39:#include "bed_actions.h"             // app_stop_all(), the app-path output helpers
ble_bed_service.c:40:#include "bed_control.h"             // the command verbs every opcode routes through
ble_bed_service.c:41:#include "motor_current_functions.h" // any_collision - cancels the move watchdog
ble_bed_service.h:23:#include <stdint.h>
ble_bed_service.h:24:#include <stdbool.h>
ble_bed_service.h:26:#include "sl_bt_api.h"
ble_bed_service.h:27:#include "bed_status.h"
```

Read tool: `ble_bed_service.c` lines 1157-1392.

*(The edits to `ble_bed_service.c` were made here - section c.1.)*

**16.** After the edit. The grep is the A4 caller list. The AE handoff's d.2
(`sed -n 1662,1705p`, 44 lines) is not reproduced.

```
$ cd $R && grep -n "note_app_stopped_commanding\|app_collision_latched\|CMD_RESULT_COLLISION_STOP" ble_bed_service.c; sed -n 1662,1705p docs/handoffs/fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md
178:#define CMD_RESULT_COLLISION_STOP             0x06 // DECISIONS #48
286:// note_app_stopped_commanding() and printed by ble_bed_service_tick() once
338:// note_app_stopped_commanding(). Without it the held button's next repeat
341:static bool app_collision_latched = false;
572:static void note_app_stopped_commanding(const char *reason)
577:  if (app_collision_latched) {
586:  app_collision_latched = false;
599:    note_app_stopped_commanding("silence");
624:    app_collision_latched = true;
625:    send_command_result(control_connection, last_move_seq, CMD_RESULT_COLLISION_STOP);
665:  note_app_stopped_commanding("watchdog");
716:    note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
795:      && (app_collision_latched || any_collision)) {
797:    send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);
871:      note_app_stopped_commanding("stop");
910:      note_app_stopped_commanding("stop");
1095:    note_app_stopped_commanding("disconnect"); // the app is gone - release its half of the latch
(AE handoff lines 1662-1705 - not reproduced)
```

**17.**

```
$ cd $R && ls ~/.silabs/slt/installs/conan/p/ | grep -i "ninja\|cmake\|gcc\|arm" ; ls cmake_gcc/build 2>/dev/null | head; find cmake_gcc -name "*.elf" -o -name "*.out" 2>/dev/null | head
cmake94b5e5c99cc1e
cmakefa35ab0687064
gcc-a1a188b577d6ca
gcc-a999d2e027337f
ninja1a38fc85adcf7
ninja6c55a7e9bcbcf
CMakeCache.txt
CMakeFiles
base
build-base.ninja
build.ninja
cmake_install.cmake
compile_commands.json
cmake_gcc/build/base/tilt_bed_controller.out
```

**18.** Writes the build wrapper; its final text is in d.3. The `grep` for
`CMAKE_C_COMPILER:` matched nothing.

```
$ S="$S"; mkdir -p "$S"; grep -m1 "CMAKE_C_COMPILER:" "$R/cmake_gcc/build/CMakeCache.txt"; cat > "$S/build.sh" <<'EOF'
(the wrapper script, d.3, with /tmp/af_build_out.txt as its output file)
EOF
echo written
written
```

**19.** Moves the wrapper's output file from `/tmp` into the scratchpad, and
finds `arm-none-eabi-size`.

```
$ S="$S"; sed -i "s#/tmp/af_build_out.txt#$S/build_out.txt#g" "$S/build.sh"; grep build_out "$S/build.sh" | head -2; ls -d ~/.silabs/slt/installs/conan/p/gcc-*/p/bin 2>/dev/null; ls ~/.silabs/slt/installs/conan/p/gcc-a1a188b577d6ca/p/bin 2>/dev/null | grep size
cmake --build --preset default_config > C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/434d4749-28cb-4360-85e6-38c8917c0bb4/scratchpad/build_out.txt 2>&1
tail -25 C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/434d4749-28cb-4360-85e6-38c8917c0bb4/scratchpad/build_out.txt
/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin
```

**20.** The size of the ELF the AE session left, before any build in this
session

```
$ cd $R && SZ=~/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size; ls -la cmake_gcc/build/base/tilt_bed_controller.out; echo "== size of the existing ELF, before this session's build:"; $SZ cmake_gcc/build/base/tilt_bed_controller.out
-rw-r--r-- 1 adunc 197610 2088252 Nov  2 07:17 cmake_gcc/build/base/tilt_bed_controller.out
== size of the existing ELF, before this session's build:
   text	   data	    bss	    dec	    hex	filename
 245188	    780	  72480	 318448	  4dbf0	cmake_gcc/build/base/tilt_bed_controller.out
```

**21.** Build with the edit

```
$ S="$S"; sh "$S/build.sh"; echo "== size:"; ~/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size "$R/cmake_gcc/build/base/tilt_bed_controller.out"
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
== size:
   text	   data	    bss	    dec	    hex	filename
 245588	    784	  72476	 318848	  4dd80	C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/cmake_gcc/build/base/tilt_bed_controller.out
```

**22.** `BLE_DEBUG_WATCHDOG_LOG` set to 0, build, set back to 1, build,
`git status --short` (no `ble_bed_service.h` change left)

```
$ S="$S"; cd $R && sed -n 30,40p ble_bed_service.h && sed -i 's/^#define BLE_DEBUG_WATCHDOG_LOG 1$/#define BLE_DEBUG_WATCHDOG_LOG 0/' ble_bed_service.h && grep -n "define BLE_DEBUG_WATCHDOG_LOG" ble_bed_service.h && sh "$S/build.sh"; sed -i 's/^#define BLE_DEBUG_WATCHDOG_LOG 0$/#define BLE_DEBUG_WATCHDOG_LOG 1/' ble_bed_service.h && grep -n "define BLE_DEBUG_WATCHDOG_LOG" ble_bed_service.h && sh "$S/build.sh"; git status --short
// stalled" message, Command Result WATCHDOG_STOP). When 1, ble_bed_service.c
// records refresh and Command-write ticks and app.c's app_process_action()
// records its own pass times; one short block is printed on each watchdog
// stop, and the connection_parameters print adds latency and supervision
// timeout. Measures only: nothing blocks and nothing prints per pass. Set to
// 0 before shipping.
#define BLE_DEBUG_WATCHDOG_LOG 1

#if BLE_DEBUG_WATCHDOG_LOG
// Called first and last in app_process_action(), to time each main-loop pass.
void ble_bed_service_debug_pass_begin(void);
36:#define BLE_DEBUG_WATCHDOG_LOG 0
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
36:#define BLE_DEBUG_WATCHDOG_LOG 1
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
 M .projectlinkstore
 M ble_bed_service.c
?? docs/prompts/PROMPT_AF_collision-latch-app-hold.md
```

**23.** Host compiler, run from `test/`. `which` found no `cc` and no `clang`
(its two "no ... in (PATH)" lines each carry the whole PATH, about 1,300
characters, and are shortened here to `(PATH)`). MinGW gcc was found.

```
$ cd $R/test && (which gcc cc clang 2>&1; gcc --version 2>&1 | head -1)
which: no cc in (PATH)
which: no clang in (PATH)
/c/MinGW/bin/gcc
gcc.exe (MinGW.org GCC-6.3.0-1) 6.3.0
```

**24.** Host test, built into the scratchpad

```
$ S="$S"; cd $R/test && gcc -std=c99 -Wall -I.. -o "$S/bed_control_test.exe" bed_control_test.c ../bed_control.c; echo "gcc exit $?"; "$S/bed_control_test.exe"; echo "test exit $?"
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
```

**25.** Output not reproduced: `docs/BLE_GATT_DESIGN.md` 300-376 (the protocol
version text and §7 before the edit).

```
$ cd $R && sed -n 300,376p docs/BLE_GATT_DESIGN.md
```

*(BLE_GATT_DESIGN.md edited here.)*

**26.** Output not reproduced: `docs/DECISIONS.md` 2329-2420 (#47, used as the
format for #48).

```
$ cd $R && sed -n 2329,2420p docs/DECISIONS.md
```

**27.** (after the edit)

```
$ cd $R && grep -n "static void note_app_stopped_commanding\|static void service_app_commanding\|static void service_move_watchdog\|if (any_collision) {\|Collision latch (#48)\|if (bed_conflict_inhibited()\|case CMD_OPCODE_MOVE_STOP\|case CMD_OPCODE_AUTO_LEVEL_STOP\|if (disconnect_stop_pending)\|process_pending_command();\|if (wd_diag_latch_clear_pending)\|if (stop_all_pending)\|if (bed_active_source() == BED_SOURCE_APP)\|bool ble_bed_service_app_hold_active\|static bool app_collision_latched" ble_bed_service.c
341:static bool app_collision_latched = false;
572:static void note_app_stopped_commanding(const char *reason)
592:static void service_app_commanding(void)
604:static void service_move_watchdog(void)
610:  if (any_collision) {
708:  if (stop_all_pending) {
783:  // Collision latch (#48). Motion-starting opcodes only, and before the #36
816:  if (bed_conflict_inhibited()
865:    case CMD_OPCODE_MOVE_STOP:
904:    case CMD_OPCODE_AUTO_LEVEL_STOP:
993:    if (bed_active_source() == BED_SOURCE_APP) {
1081:bool ble_bed_service_app_hold_active(void)
1090:  if (disconnect_stop_pending) {
1103:  process_pending_command();
1119:  if (wd_diag_latch_clear_pending) {
```

**28.**

```
$ cd $R && tail -5 docs/DECISIONS.md | cat -A | tail -5
  ASSERTED from code; not fixed here.$
$
### Status$
$
Builds clean (no warnings). **Not run on the bench.**$
```

**29. FAILED.** An attempt to append DECISIONS #48 with a shell heredoc. The
heredoc body (a first draft of the #48 text, about 115 lines) is not
reproduced. The entry was then added with the Edit tool instead; its final
text is in c.2. The failure left the file unchanged (command 30).

```
$ cd $R && cat >> docs/DECISIONS.md <<'EOF'
(first draft of DECISIONS #48)
EOF
tail -3 docs/DECISIONS.md
/usr/bin/bash: -c: line 119: unexpected EOF while looking for matching `''
[exit code 2]
```

**30.**

```
$ cd $R && wc -l docs/DECISIONS.md && tail -2 docs/DECISIONS.md && git diff --stat
2420 docs/DECISIONS.md

Builds clean (no warnings). **Not run on the bench.**
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
 .projectlinkstore       |  2 +-
 ble_bed_service.c       | 94 ++++++++++++++++++++++++++++++++++++++++++++-----
 docs/BLE_GATT_DESIGN.md | 46 +++++++++++++++++++++++-
 3 files changed, 131 insertions(+), 11 deletions(-)
```

**31.** Line endings of the edited files: LF, as the committed
`ble_bed_service.c` was. `core.autocrlf` is `true`, hence git's warnings. (The
`grep -c $'\r'` count of 0 agrees here, but command 49 shows that this grep
does not reliably detect CRs in this shell. `file` is the evidence.)

```
$ cd $R && file ble_bed_service.c docs/BLE_GATT_DESIGN.md docs/DECISIONS.md; git config --get core.autocrlf; git show HEAD:ble_bed_service.c | file -; grep -c $'\r' ble_bed_service.c
ble_bed_service.c:       C source, ASCII text
docs/BLE_GATT_DESIGN.md: Unicode text, UTF-8 text, with very long lines (408)
docs/DECISIONS.md:       Unicode text, UTF-8 text
true
/dev/stdin: C source, ASCII text
0
```

*(DECISIONS #48 added with the Edit tool here.)*

**32.** Checks the ranges cited in #48 (one was then corrected:
`app.c:513-541` -> `app.c:510-560`).

```
$ cd $R && grep -n "bool app_is_ok_to_sleep\|motor_outputs_maybe_on())\|return true;" app.c | sed -n 1,10p; sed -n 589,597p app.c | head -3; sed -n 609,618p app.c | sed -n '1p;10p'; sed -n 300p dual_motor_control.c; sed -n 342,344p dual_motor_control.c; sed -n 896,897p switch_input.c; sed -n 610p ble_bed_service.c; sed -n 630,632p ble_bed_service.c; sed -n 783p ble_bed_service.c; sed -n 805,806p ble_bed_service.c; sed -n 572p ble_bed_service.c; sed -n 586,588p ble_bed_service.c
510:bool app_is_ok_to_sleep(void)
542:  if (motor_outputs_maybe_on()) {
560:  return true;
      active_switch_during_collision = active_switch;
      auto_level_active = false;
      auto_level_stop_blink_LED(LED_RED);
    if (LED_blink_counter >= COLLISION_BLINK_CYCLES){

void reverse_motor_direction_and_start(active_switch_t switch_value){ ///NEED TO UPDATE BECAUSE AUTO LEVEL WONT WORK HERE
    default:
      break;
  }
  }
}
  if (any_collision) {
#endif
    return;
  }
  // Collision latch (#48). Motion-starting opcodes only, and before the #36

  // Two-source conflict inhibit (#36). Motion-starting opcodes only: stops
static void note_app_stopped_commanding(const char *reason)
  app_collision_latched = false;
}
```

**33.** Output not reproduced: REGRESSION 6.8/6.9 lines and the 6.9 block (up
to 50 lines of `docs/REGRESSION.md`, used for the item format).

```
$ cd $R && grep -n "6\.9\|6\.8\|^- \[ \] \*\*" docs/REGRESSION.md | head; sed -n '/6\.9/,/^## 7/p' docs/REGRESSION.md | head -50
```

*(REGRESSION.md edited here.)*

**34.** The source diff before committing. Its output is the
`ble_bed_service.c` diff in c.1, preceded by git's line-ending warning and
with the index line `index 8102cd4..fa49ce2 100644`. It is not repeated.

```
$ cd $R && git diff ble_bed_service.c
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
(the diff in c.1)
```

**35.** The work commit. The message body was passed on stdin; its text is
the commit message of `e646c3b`.

```
$ cd $R && git add ble_bed_service.c docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/REGRESSION.md docs/prompts/PROMPT_AF_collision-latch-app-hold.md && git commit -q -F - <<'EOF'
(commit message)
EOF
git log --oneline -3; git status --short
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/prompts/PROMPT_AF_collision-latch-app-hold.md', LF will be replaced by CRLF the next time Git touches it
e646c3b Collision during an app hold latches the app path: COLLISION_STOP 0x06 (DECISIONS #48)
4d1fef3 Handoff 2026-11-02 01: Prompt AE (LEVELED bit, amber after STOP_ALL, BED LIGHTS on release, PRE_SHIP rows; Part A stopped)
e3cc865 PRE_SHIP: rows F20-F25 and F17 extended (reviewer rulings on Prompt AC)
 M .projectlinkstore
```

**36.** The post-commit build, the size, the date for this file's name, the
existing handoffs for that date, and the commit's size.

```
$ S="$S"; sh "$S/build.sh"; ~/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size "$R/cmake_gcc/build/base/tilt_bed_controller.out"; date; cd $R && ls docs/handoffs | grep 2026-11-02; git show --stat --format= e646c3b; git show --format= e646c3b -- docs/ | wc -l
ninja: no work to do.
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 245588	    784	  72476	 318848	  4dd80	C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/cmake_gcc/build/base/tilt_bed_controller.out
Mon Nov  2 09:05:18 PST 2026
fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md
 ble_bed_service.c                                  |  94 +++++++++++++--
 docs/BLE_GATT_DESIGN.md                            |  46 ++++++-
 docs/DECISIONS.md                                  | 133 +++++++++++++++++++++
 docs/REGRESSION.md                                 |  50 ++++++++
 docs/prompts/PROMPT_AF_collision-latch-app-hold.md | 130 ++++++++++++++++++++
 5 files changed, 443 insertions(+), 10 deletions(-)
401
```

**37.** Output not reproduced: `wc -l` of `docs/SESSION_LOG.md` (1588) and
its lines 1540-1600 (the AE entry, used for the entry format).

```
$ cd $R && wc -l docs/SESSION_LOG.md && sed -n 1540,1600p docs/SESSION_LOG.md
```

**38.** The SESSION_LOG lines 1528-1540 (13 lines of the AC entry and the AE
heading) are not reproduced. The rest is shown.

```
$ cd $R && grep -n "^## \|^### " docs/SESSION_LOG.md | tail -4; sed -n 1528,1540p docs/SESSION_LOG.md; grep -n "BED_BENCH_PROVOKE_RUNAWAY\b" switch_input.c | head -2; grep -n "define BED_CAPTURE_MOTOR_CURRENTS" debug_capture.h; grep -n "define BLE_BENCH_WIPE_BONDS" ble_bed_service.c
1434:## 2026-11-01 — Prompt AA: sleep veto during app holds, hold-end line, sleep-clock report
1471:## 2026-11-01 — Record: Prompt AA bench result (Prompt AC part A)
1503:## 2026-11-01 — Prompt AC: AA bench record, PRE_SHIP.md, code report for the next features
1536:## 2026-11-02 — Prompt AE: LEVELED bit, amber after STOP_ALL, BED LIGHTS on release, PRE_SHIP rows; Part A stopped and reported
(SESSION_LOG lines 1528-1540 - not reproduced)
98:#define BED_BENCH_PROVOKE_RUNAWAY 0
471:#if BED_BENCH_PROVOKE_RUNAWAY
21:#define BED_CAPTURE_MOTOR_CURRENTS 0
70:#define BLE_BENCH_WIPE_BONDS 0
```

**39.** The SESSION_LOG ends with a newline

```
$ cd $R && tail -c 200 docs/SESSION_LOG.md | od -c | tail -3
0000260   r       Q   U   E   S   T   I   O   N   S       F   O   R    
0000300   R   E   V   I   E   W   .  \n
0000310
```

*(SESSION_LOG entry appended with the Edit tool here.)*

**40.** Output not reproduced: the AE handoff's section d opening (lines
1272-1330) and section e (60 lines from 1785), used for the format.

```
$ cd $R && sed -n 1272,1330p docs/handoffs/fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md; sed -n 1785,1889p docs/handoffs/fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md | head -60
```

**41.**

```
$ cd $R && grep -n "^void reset_motor_current_data" -A25 motor_current_functions.c
235:void reset_motor_current_data(){
236-
237-  motor_current_running_average_on = false;
238-  dynamic_collision_detection_active = false;
239-
240-
241-  for (uint8_t i=0; i < NUM_MOTORS; i++){
242-    dynamic_max_average_calibration_current[i] = 0;
243-    dynamic_collision_current_threshold[i] = 0;
244-    dynamic_reading_count[i] = 0;
245-    average_current_single_reading[i] = 0;
246-    current_running_sum[i] = 0;
247-    filtered_current[i] = 0;
248-    max_current_threshold_set[i] = 0;
249-
250-    for (uint8_t t=0; t < NUM_CURRENT_READINGS; t++){
251-        readings[i][t] = 0;
252-    }
253-  };
254-
255-}
256-
257-void reset_ignore_data_points_count(){
258-
259-  for (uint8_t i=0; i < NUM_MOTORS; i++){
260-    ignore_data_points_count[i] = 0;
```

**42.** The `git status` quoted in section a

```
$ cd $R && git status; git rev-parse HEAD
On branch main
Your branch is ahead of 'origin/main' by 1 commit.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

no changes added to commit (use "git add" and/or "git commit -a")
e646c3b0228a37840380f18b79de11f7128d2e9e
```

**43.** Line numbers for this file (at `e646c3b`)

```
$ cd $R && grep -n "if (conn != control_connection)\|if (app_in_settings_mode())\|case CMD_OPCODE_MOVE_START\|case CMD_OPCODE_AUTO_LEVEL_START\|service_move_watchdog();\|service_app_commanding();\|if (app_bed_is_idle() && app_move_active)\|if (!move_watchdog_expired)\|bed_move_start(BED_SOURCE_APP, target, dir)\|app_apply_move_outputs(target);\|move_watchdog_arm();\|if (wd_diag_hold_end_pending)\|note_app_commanding();" ble_bed_service.c; grep -n "switch_input_tick();\|motion_failsafe_tick();\|bed_control_tick();\|ble_bed_service_tick();\|if (any_collision){\|if ((auto_level_active) && (!any_collision)" app.c
634:  if (app_bed_is_idle() && app_move_active) {
648:  if (!move_watchdog_expired) {
773:  if (conn != control_connection) {
778:  if (app_in_settings_mode()) {
796:    note_app_commanding();
818:    note_app_commanding();
826:    case CMD_OPCODE_MOVE_START: {
833:      note_app_commanding();
844:        if (!bed_move_start(BED_SOURCE_APP, target, dir)) {
852:        app_apply_move_outputs(target);
860:      move_watchdog_arm();
877:    case CMD_OPCODE_AUTO_LEVEL_START:
884:      note_app_commanding();
900:      move_watchdog_arm();
1104:  service_move_watchdog();
1105:  service_app_commanding();
1110:  if (wd_diag_hold_end_pending) {
580:  if (any_collision){
626:  if ((auto_level_active) && (!any_collision) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
741:  switch_input_tick();
758:  motion_failsafe_tick();
772:  bed_control_tick();
773:  ble_bed_service_tick();
```

**44.** Checks the `ble_bed_service.c` lines cited in a draft of section b.
`:984-1022` and `:1274-1281` were wrong (corrected in command 47 to
`:980-1022` and `:1337-1358`), and so was `:620-623` (corrected to
`:621-624`).

```
$ cd $R && p(){ echo "== $1:$2"; sed -n "$2p" "$1"; }; p ble_bed_service.c 140; p ble_bed_service.c 145,146; p ble_bed_service.c 606; p ble_bed_service.c 620,623; p ble_bed_service.c 626,629; p ble_bed_service.c 634,646 | head -3; p ble_bed_service.c 795,803 | head -2; p ble_bed_service.c 799,801; p ble_bed_service.c 984,985; p ble_bed_service.c 993,995; p ble_bed_service.c 1001,1002; p ble_bed_service.c 1014,1022; p ble_bed_service.c 1110,1116 | head -2; p ble_bed_service.c 1274,1281 | head -3; p ble_bed_service.c 1081,1084
== ble_bed_service.c:140
#define ADV_INTERVAL_UNITS 48
== ble_bed_service.c:145,146
#define CONN_MIN_INTERVAL        12
#define CONN_MAX_INTERVAL        24
== ble_bed_service.c:606
  if (!move_watchdog_armed) {
== ble_bed_service.c:620,623
    // latch stays as it is.
    move_watchdog_cancel();
    app_move_active = false;
    app_auto_level_active = false;
== ble_bed_service.c:626,629
#if BLE_DEBUG_WATCHDOG_LOG
    // After the motors are off and the result is sent.
    printf("BLE: COLLISION_STOP seq=%u op=0x%02x (latched)\r\n",
           last_move_seq, wd_diag_refresh_opcode);
== ble_bed_service.c:634,646
  if (app_bed_is_idle() && app_move_active) {
    // The bed stopped underneath a move the app still believed it held - the
== ble_bed_service.c:795,803
      && (app_collision_latched || any_collision)) {
== ble_bed_service.c:799,801
    if (wd_diag_collision_refused < 0xFFFF) {
      wd_diag_collision_refused++;
    }
== ble_bed_service.c:984,985
  if (connection == control_connection) {
    // BLE_GATT_DESIGN.md #5, implemented exactly as written: the control
== ble_bed_service.c:993,995
    if (bed_active_source() == BED_SOURCE_APP) {
      disconnect_stop_pending = true;
    }
== ble_bed_service.c:1001,1002
      // Promote the surviving viewer to control (DECISIONS.md #21) - a
      // reconnect should not be required to regain control. The physical
== ble_bed_service.c:1014,1022
    }
  } else if (connection == viewer_connection) {
    viewer_connection = SLOT_EMPTY;
    viewer_status_subscribed = false;
  }

  update_status_subscriber_count();
  start_advertising(); // a slot just freed
}
== ble_bed_service.c:1110,1116
  if (wd_diag_hold_end_pending) {
== ble_bed_service.c:1274,1281
    // pairing was refused by the window below, close it here: the bed has
    // only two role slots and a phone or app that stays connected after a
== ble_bed_service.c:1081,1084
bool ble_bed_service_app_hold_active(void)
{
  return move_watchdog_armed;
}
```

**45.** Checks the other files' citations. Wrong in the draft and corrected
in command 47: the `bed_control.c` ranges, the trip-print ranges,
`bed_actions.c:142`, and two `app.c` ranges.

```
$ cd $R && grep -n "static void handle_connection_closed\|case sl_bt_evt_connection_parameters_id\|if (params->security_mode != sl_bt_connection_mode1_level1)" ble_bed_service.c; p(){ echo "== $1:$2"; sed -n "$2p" "$1"; }; p bed_control.c 76,79; p bed_control.c 178,191; p motor_current_functions.c 300,302; p motor_current_functions.c 316,318; p motor_current_functions.c 339,347; p app.c 84; p app.c 186,190; p app.c 589,608; p app.c 758; p dual_motor_control.c 352,356; p switch_input.c 769,770; p switch_input.c 819; p switch_input.c 831,838; p switch_input.c 885; p bed_actions.c 142; p auto_level.c 772,778
980:static void handle_connection_closed(uint8_t connection)
1337:    case sl_bt_evt_connection_parameters_id: {
1350:      if (params->security_mode != sl_bt_connection_mode1_level1) {
== bed_control.c:76,79
 * interpreted ONLY as an immediate clean STOP" - not a partial, verb-specific
 * stop.
 */
static arbitrate_result_t arbitrate(bed_source_t src)
== bed_control.c:178,191
== motor_current_functions.c:300,302
          printf("#COLLISION,%lu\r\n", (unsigned long)capture_now_ms());
#else
          printf("\r\n");
== motor_current_functions.c:316,318
          printf("#COLLISION,%lu\r\n", (unsigned long)capture_now_ms());
#else
          printf("\r\n");
== motor_current_functions.c:339,347
        } else { // If motor is raising, use the raising security margin
            security_margin = raise_collision_current_security_margin;
        }
        // Step 1: Calculate individual deltas and total delta sum for either steady or fast collision detection
        if (dynamic_collision_detection_active){
            for (uint8_t i = 0; i < NUM_MOTORS; i++) {
                average_current_delta_single[i] = (average_current_single_reading[i] - max_current_threshold_set[i]);
                if (average_current_delta_single[i] > 0) {
                    average_current_delta_sum += average_current_delta_single[i];
== app.c:84
#define COLLISION_BLINK_CYCLES 3 // Number of times to blink LEDs for a collision
== app.c:186,190
static void start_500ms_LED_timer()
{
  uint32_t ticks = sl_sleeptimer_ms_to_tick(500);
  sl_sleeptimer_start_periodic_timer(&LED_500ms_timer, ticks, LED_timer_callback, NULL, 0, 0);
}
== app.c:589,608
      active_switch_during_collision = active_switch;
      auto_level_active = false;
      auto_level_stop_blink_LED(LED_RED);
      start_500ms_LED_timer();
      LED_blink_counter = 0;
#if !BED_CAPTURE_MOTOR_CURRENTS
      printf("Once\r\n");
#endif

      set_all_LED_on();
    }
    if (LED_timer_timeout){
      toggle_all_LED();
      LED_timer_timeout = false;
      LED_blink_counter++;
      if (LED_blink_counter == 2){
        app_set_leveled(false); // the reverse is motion (#46)
        reverse_motor_direction_and_start(active_switch_during_collision);
      }
    }
== app.c:758
  motion_failsafe_tick();
== dual_motor_control.c:352,356
      // If trying to raise HEAD and BOTH Motor A and Motor D (HEAD) are NOT at UPPER limit switch, move UP
      if ((motor_state[MOTORA].motor_up && motor_state[MOTORD].motor_up) && !(at_limit_switch[MOTORA].upper || at_limit_switch[MOTORD].upper)){
          set_motor_on(MOTORA);
          motor_state[MOTORA].motor_on = true;

== switch_input.c:769,770
#define FAILSAFE_SAMPLE_MS        50
#define FAILSAFE_CONFIRM_SAMPLES  3   // ~150 ms of agreement before acting
== switch_input.c:819
  if (auto_level_active || in_settings_mode || ble_bed_service_app_motion_active()) {
== switch_input.c:831,838
  bool any_switch_down = switch_state_fresh() &&
                         (switch_sampler_down() != 0);

  if (any_switch_down) {
    failsafe_quiet_samples = 0;
    failsafe_tripped = false;
    return;
  }
== switch_input.c:885
    printf("FAILSAFE: motors energised with nothing commanding - stopped\r\n");
== bed_actions.c:142
  active_move_is_all = (target == BED_TARGET_ALL);
== auto_level.c:772,778
          if (any_collision){
              set_all_motors_off();
              auto_level_motors_on = false;
              for (uint8_t i=0; i < NUM_MOTORS; i++){
                  motor_state[i].motor_on = false;
              };
              return;
```

**46.**

```
$ cd $R && wc -l bed_control.c; grep -n "if (app_bed_is_idle())\|^void bed_control_tick\|^bed_source_t bed_active_source\|^static arbitrate_result_t arbitrate" bed_control.c; grep -n "Tripped on steady\|TRIPPED MAX LOAD\|Delta Current" motor_current_functions.c; grep -n "app_set_leveled(false); // any move" bed_actions.c; grep -n "if (any_collision){\|LED_blink_counter >= COLLISION_BLINK_CYCLES\|any_collision = false;" app.c
176 bed_control.c
79:static arbitrate_result_t arbitrate(bed_source_t src)
81:  if (app_bed_is_idle()) {
166:void bed_control_tick(void)
168:  if (app_bed_is_idle()) {
173:bed_source_t bed_active_source(void)
303:          printf("TRIPPED MAX LOAD\r\n");
319:          printf("TRIPPED MAX LOAD\r\n");
358:                printf("Tripped on steady!\r\n");
362:                printf("Motor %d Delta Current:  %ld\r\n", i, average_current_delta_single[i]);
143:  app_set_leveled(false); // any move, either source, ALL included (#46)
580:  if (any_collision){
609:    if (LED_blink_counter >= COLLISION_BLINK_CYCLES){
617:      any_collision = false;
```

**47.** Applies those corrections to the scratchpad draft of section b
(`$S/h_p1.md`) and shows the corrected lines.

```
$ S="$S"; f="$S/h_p1.md"; sed -i \
 -e 's/`bed_control.c:76-79`, `:186-191`/`bed_control.c:79-83`, `:173-176`/' \
 -e 's/`bed_control.c:178-184`/`bed_control.c:166-171`/' \
 -e 's/(`:984-1022`)/(`:980-1022`)/' \
 -e 's/`connection_parameters` with security, `:1274-1281`/`connection_parameters` with security, `:1337-1358`/' \
 -e 's/(`:620-623`)/(`:621-624`)/' \
 -e 's/(`collision_check()`, `:339-347`)/(`collision_check()`, `:357-364`)/' \
 -e 's/`:300-302` or `:316-318`/`:302-304` or `:318-320`/' \
 -e 's/`bed_actions.c:142`/`bed_actions.c:143`/' \
 -e 's/(`app.c:599-608`)/(`app.c:600-608`)/' \
 -e 's/`app.c:186-190`, `:589-597`/`app.c:186-190`, `:588-599`/' "$f"; grep -n "bed_control.c:\|:980-1022\|:1337-1358\|:621-624\|:357-364\|:302-304\|bed_actions.c:143\|app.c:600-608\|:588-599" "$f"
184:| 2 | Watchdog expiry in `service_move_watchdog()` | `:665` | `watchdog` | Watchdog armed and expired | Yes (300 ms without a refresh). **Cannot clear a set latch**: the latch is set in the same branch that cancels the watchdog (`:621-624`), and while latched every start is refused before it can re-arm it (`:795-803`). The prompt's list of reasons does not include it; it is listed for completeness |
216:  (`app.c:186-190`, `:588-599`), prints `Once`, all LEDs on.
218:  increments `LED_blink_counter` (`app.c:600-608`). At 2 the reverse starts
349:Two cases, from `handle_connection_closed()` (`:980-1022`):
353:  APP (`bed_control.c:79-83`, `:173-176`): `disconnect_stop_pending` is set
360:  has already returned `active_source` to NONE (`bed_control.c:166-171`)
368:role (`connection_parameters` with security, `:1337-1358`) before any write is
494:Summed-rise trip (`collision_check()`, `:357-364`):
504:`:302-304` or `:318-320`): an empty line, then
532:  LEVELED when it started (`app_move_start()`, `bed_actions.c:143`).
```

**48-56. An abandoned approach to this section.** A first version of this
section tried to re-create the long file-content outputs above by re-running
the same commands against copies of the files in scratchpad folders (`treeA`
/ `treeB` from `git archive`, then `wtA` / `wtB` copied from the working
tree), and assembled a draft of this file from them. That approach was
dropped. Those outputs were re-creations, not this session's output, and
the user asked that section d use only real outputs. Nothing from those folders
is used in this file. What those commands found, with their outputs:

- An ignored-files check found only
  `cmake_gcc/build/CMakeFiles/3.30.2/CompilerIdC/CMakeCCompilerId.c`.
- `git archive` exports came out with CRLF line endings
  (`core.autocrlf=true`), unlike the LF working copy of `docs/DECISIONS.md`.
  This showed up as `^M` in a re-run of command 28.
- A CR check with `grep -l $'\r'` printed nothing for the exports and for the
  working tree, although `file` shows CRLF files in both. **That grep does
  not detect CRs in this shell.**
- `file` lists these working-tree files as CRLF: `adc_currents.c`,
  `auto_level.c`, `bed_actions.c`, `dual_motor_control.c`, `exp_ui.c`,
  `mc3479.c`, `motor_current_functions.c`, `switch_input.c`,
  `adc_currents.h`, `auto_level.h`, `bed_types.h`, `debug_capture.h`,
  `dual_motor_control.h`, `exp_ui.h`, `mc3479.h`,
  `motor_current_functions.h`, `pi4ioe5v6416.h`, `version.h`. The files this
  session edited are LF (command 31). Their line endings were not changed.
- `git diff --stat 4d1fef3` over sources, docs and test showed exactly the
  committed `e646c3b` files plus `docs/SESSION_LOG.md` (+37); `git status`
  showed `docs/SESSION_LOG.md` modified and this file's draft untracked.
- **Refused command.** One command, which began by removing and recreating
  the two scratchpad folders `treeA` and `treeB`, was **refused by Claude
  Code's built-in safety check** (a removal whose target was a shell
  variable). It did not run, and nothing was deleted. It was **not retried**.
  The folders `treeA`, `treeB`, `wtA` and `wtB` are still in the scratchpad.
  They are unused, and they are not part of the repo or of any commit.
- Building `wtA` / `wtB` copied the repo's `*.c`, `*.h`, `docs/` and `test/`
  into those scratch folders and removed the copy of this handoff's draft
  from each copy (inside the scratchpad only; the repo was not touched).

**After this.** Commands that assemble this file from its parts, run
`git status`, commit this file with `docs/SESSION_LOG.md`, and push come after
this file's content was fixed. Their output is in the session's final report
to the user. A push failure would be reported in a follow-up commit to this
file (CLAUDE.md "Pushing").

### d.3 Build method

`$S/build.sh` (final text, after command 19):

```sh
#!/bin/sh
# Session build wrapper: Silicon Labs ninja + cmake on PATH, build the default preset.
P=~/.silabs/slt/installs/conan/p
export PATH="$P/ninja1a38fc85adcf7/p:$P/cmakefa35ab0687064/p/bin:$PATH"
cd "C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/cmake_gcc" || exit 99
cmake --build --preset default_config > C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/434d4749-28cb-4360-85e6-38c8917c0bb4/scratchpad/build_out.txt 2>&1
rc=$?
tail -25 C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/434d4749-28cb-4360-85e6-38c8917c0bb4/scratchpad/build_out.txt
echo "exit $rc"
echo "--- warnings/errors (grep -n -i 'warning\|error' over the whole output):"
grep -n -i 'warning\|error' C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/434d4749-28cb-4360-85e6-38c8917c0bb4/scratchpad/build_out.txt
exit $rc
```

The tools and preset are the same as the AE session's wrapper. Nothing printed after the
`--- warnings/errors` line means no line of the full build output contains
"warning" or "error". (The `grep -i` would match the words; it does not depend
on CR detection.)

## e. VERIFIED vs ASSERTED

Nothing in this session ran on the bed. VERIFIED below means a command output
(section d) or a line of code that was read; it never means "observed on
hardware". d.2 numbers are the command numbers in the session log. Where a
cited command's output is marked "not reproduced" in d.2 (a long file read),
the evidence is the `file:line` given in the claim. It was read in the session
at the commit stated, and it is not shown in this file.

### e.1 VERIFIED, with evidence

| # | Claim | Evidence |
|---|---|---|
| V1 | Start state as the prompt requires: HEAD `4d1fef3`, parent `e3cc865`, = `origin/main` after `git fetch`; `.projectlinkstore` modified, the prompt untracked | d.2 #1 (`git status`), #2 (`git rev-parse HEAD origin/main` both `4d1fef35...`, parent `e3cc8652...`) |
| V2 | Builds exit 0 with no `warning`/`error` line: after the edit; with `BLE_DEBUG_WATCHDOG_LOG` 0; back at 1; after the commit (no work) | d.2 #21, #22, #36 (each `exit 0`, nothing after the `---` grep line) |
| V3 | Size before text 245188 / data 780 / bss 72480 (the ELF the AE session built at 07:17); after text 245588 / data 784 / bss 72476 | d.2 #20, #21, #36 |
| V4 | `BLE_DEBUG_WATCHDOG_LOG` is back at 1 after the temporary 0; `ble_bed_service.h` not changed | d.2 #22 (`36:#define BLE_DEBUG_WATCHDOG_LOG 1`, `git status --short` lists no `.h`); c.1 has no `.h` hunk |
| V5 | Host test passes, 0 checks failed; `bed_control.c` unchanged | d.2 #24; c.1 has no `bed_control.c` hunk |
| V6 | `BED_BENCH_PROVOKE_RUNAWAY` 0, `BLE_BENCH_WIPE_BONDS` 0, `BED_CAPTURE_MOTOR_CURRENTS` 0 | d.2 #38 (`switch_input.c:98`, `ble_bed_service.c:70`, `debug_capture.h:21`) |
| V7 | Only `ble_bed_service.c` changed among sources; no `config/`, `autogen/`, GATT or Protocol Version change | d.2 #36 `git show --stat` (five paths: one `.c`, three docs, the prompt); c.1 |
| V8 | The latch code is as described in b.2: set at `:621-624` + result `:625` + print `:626-629`; refusal `:794-804` after the viewer (`:773`) and settings (`:778`) checks and before #36 (`:816`); clear in `note_app_stopped_commanding()` `:572-587` | c.1; d.2 #27, #43, #44 |
| V9 | Callers of `note_app_stopped_commanding()`: `:599` silence, `:665` watchdog, `:716` STOP_ALL, `:871` MOVE_STOP, `:910` AUTO_LEVEL_STOP, `:1095` disconnect - six, no other | d.2 #16 (grep) |
| V10 | The watchdog is armed only by an accepted MOVE_START / AUTO_LEVEL_START, so the trip branch runs only during an app hold, and nothing re-arms it while latched | `move_watchdog_arm()` calls at `:860`, `:900` only (d.2 #43); `service_move_watchdog()` returns unless armed (`:606`, d.2 #44); the refusal returns before the switch (c.1) |
| V11 | STOP_ALL is accepted from either role; MOVE_STOP / AUTO_LEVEL_STOP from a viewer are rejected before the switch | `:708-722` has no role check; viewer check at `:773` precedes the switch (d.2 #43; Read of `ble_bed_service.c` at `4d1fef3`, same code) |
| V12 | The disconnect stop runs only if the closed link was control and `bed_active_source() == BED_SOURCE_APP`; the source returns to NONE whenever the bed is idle | `ble_bed_service.c:993-995` (d.2 #44); `bed_control.c:166-171` and `:81` (d.2 #6, #46); `app_bed_is_idle()` = `active_switch == OFF && !in_settings_mode` (d.2 #8) |
| V13 | `any_collision` is cleared by the collision block in the pass that reaches `LED_blink_counter >= 3`, driven by a 500 ms periodic timer started in that block | `app.c:84`, `:186-190`, `:580-620` (d.2 #4, #5, #6) |
| V14 | No reverse after an auto-level collision; a move's reverse runs at blink 2 from the value saved when the sequence started | `dual_motor_control.c:300-343` default case (d.2 #6, #11); `app.c:589`, `:604-606` (d.2 #4) |
| V15 | The runaway fail-safe's conditions: 50 ms samples, 3 to confirm, stands down for auto-level / settings / `ble_bed_service_app_motion_active()` / a switch down; otherwise de-energises and prints `FAILSAFE: ...` | `switch_input.c:769-770`, `:804-896` (d.2 #9, #11, #12) |
| V16 | `ble_bed_service_app_motion_active()` = `app_move_active \|\| app_auto_level_active`; both are false after the trip branch | `ble_bed_service.c:1076-1079` (`:1007-1010` at `4d1fef3`, read with the Read tool; body unchanged - no c.1 hunk); c.1 trip branch clears both flags |
| V17 | A max-load trip disables the summed-rise check for the rest of that call, so the two trip print blocks never both appear in one pass | `reset_motor_current_data()` sets `dynamic_collision_detection_active = false` (`motor_current_functions.c:238`, d.2 #41); `collision_check()` body gated on it (d.2 #5) |
| V18 | The existing host test compiles only `bed_control.c`; `ble_bed_service.c` includes SDK headers | `test/bed_control_test.c` header comment (d.2 #15); `grep ^#include` (d.2 #15) |
| V19 | No doc mentions the fail-safe in connection with the collision reverse | d.2 #13 (no output) |
| V20 | Edited files keep LF endings with no CR, as before | d.2 #31 |

### e.2 ASSERTED (reasoned from code, compiles, not observed)

- **A1. The defect is fixed**: during an app MOVE hold, a trip is followed by
  0x06 and the held button's repeats re-energise nothing (b.6 B1). The
  building blocks are V8-V10; the behaviour on a bed is not observed. The
  defect itself (b.1) has also never been seen on a bed.
- A2. Every B1-B5 sequence in b.6, including the Command Results the app
  receives and their order.
- A3. The `|| any_collision` window is ~1.5 s plus at most a few tens of ms
  (b.4); the wake latency figures are from the configured intervals, not
  measured.
- A4. **The fail-safe cuts the reverse ~150-200 ms after it starts after an
  app collision** (b.11.1). Built from V14-V16; not observed.
- A5. A reconnect cannot deliver a command within 300 ms of a drop, so the
  latch is always clear before the reconnected app can command (b.6 B4).
- A6. Nothing the app commands moves under the latch; the #45 veto is not
  needed then (b.5).
- A7. The b.11.3 window (latch set with `app_commanding` already false) exists
  and is a few ms wide at most.
- A8. Every expected console line in b.10, and its order.
- A9. App behaviour on 0x06 (both "keeps repeating" and "ends its hold") is
  modelled, not read: the app repository was not looked at.

---

## f. QUESTIONS FOR REVIEW

**f.1 STOP_ALL from a viewer clears the latch (A4's stop condition).** The
prompt names STOP_ALL as a release path and also says to stop if any caller
"would unlatch while the user may still be holding". A STOP_ALL from the
**viewer** phone (or a second finger on the control phone) clears the latch
while the control user may still be holding. I implemented it as the prompt
lists it, because the outcome equals today's STOP_ALL during any ordinary
hold - inside the sequence nothing moves (`any_collision` still refuses);
after it a still-repeating control app restarts on its next repeat (b.3,
b.6 B5). If that is not acceptable, the change would be to not clear the
latch in the STOP_ALL path when `stop_all_conn != control_connection` (a
viewer's STOP_ALL still stops everything). Reviewer's call.

**f.2 A re-press inside the collision sequence (B3) starts the bed when the
sequence ends**, if the app keeps repeating it, because A3 does not latch on
an `any_collision` refusal. With a 0x06-aware app this does not happen (its
first 0x06 ends the hold). Should an `any_collision` refusal set the latch
too (one line: `app_collision_latched = true` in the refusal branch), so any
hold that meets a collision has to be released first? That would also cover
an app press during a physical-controller collision. It would make the
"cleared" print cover those refusals (f.5).

**f.3 The runaway fail-safe shortens the collision reverse** after an app
collision (b.11.1), and already does after a physical collision once the
button is let go. The reverse is meant to relieve pressure on the
obstruction (REGRESSION §5: "motors briefly reverse starting at blink 2").
Is ~150-200 ms of reverse acceptable, or should the fail-safe stand down
while `any_collision` is set (a `switch_input.c` change, outside this
prompt)? Worth confirming on the bench first: REGRESSION 5.A1 asks the
tester to look for the `FAILSAFE` line.

**f.4 The disconnect release is conditional** (b.6 B4, b.11.2). After the
sequence has ended a drop clears the latch by silence (300 ms), not by the
disconnect path, and logs `silence`. The B4 outcome holds either way. Also
for the app prompt: what the reconnected app sends if the finger is still
down after a reconnect is app behaviour this repo cannot see.

**f.5 The "cleared" print covers only a latched episode.** Refusals made
without the latch (B3's re-press, an app press during a physical collision)
are counted and dropped silently when the app releases. Should those get a
line of their own (e.g. `BLE: collision refused K starts (not latched)`)?

**f.6 REGRESSION 5.A5** (physical collision unchanged, and an app press
during it refused) was not asked for; it tests A5 and the A3 ruling. Keep or
drop.

**f.7 The b.11.3 window** - a trip in the few ms between the silence clear
and the watchdog expiry latches with `app_commanding` already false, so the
latch outlives the silence timer until the app's next release. Safe direction
(a later press is refused once, until released). Acceptable as is, or should
the trip branch call `note_app_commanding()` so the silence timer always runs
after a latch is set? (That would refresh, not clear, the #36 half - the
prompt only forbids `note_app_stopped_commanding()` there.)

**f.8 The B1 bench test hits the obstruction.** Until 5.A1 passes, the AE
warning stands: do not test a collision while holding an app MOVE button
unless the obstruction can safely take a second hit - if the latch does not
work as built, the pair drives into it again ~100 ms after the trip.

---

## g. NOT DONE

- **D4, host-test cases for B1, B3 and the silence clear: not added.** The
  latch is in `ble_bed_service.c`, which the existing test cannot compile; a
  second harness with SDK fakes would be needed (~300-400 lines). What it
  would take: b.8.
- **No bench run** - the user drives the bench (CLAUDE.md). REGRESSION
  5.A1-5.A5 and the b.10 lines are for the reviewer's bench steps.
- **The app side**: the app's handling of 0x06 and its copy of
  BLE_GATT_DESIGN §7 are for the next app prompt (the prompt says so).
- **Section d does not reproduce the output of long file-content reads.**
  No saved copy of that output exists, and the user asked that it not be
  re-created from other files. Each such command is listed with its file and
  line range (d.1).
- Nothing else that was asked was left out. The f.1 judgement call was made
  (implemented as the prompt lists STOP_ALL) rather than stopped on.
