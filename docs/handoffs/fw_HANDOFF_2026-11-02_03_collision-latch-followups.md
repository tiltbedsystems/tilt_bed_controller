# fw_HANDOFF_2026-11-02_03 — Prompt AG: collision latch follow-ups (viewer STOP_ALL, refusals latch, trip arms silence)

Firmware repo session of 2026-11-02 (system date checked with `date` at the
start, `Mon Nov  2 09:43:00 PST 2026`, and again before writing this file,
`Mon Nov  2 09:51:37 PST 2026` and `Mon Nov  2 09:52:37 PST 2026`). Prompt:
`docs/prompts/PROMPT_AG_collision-latch-followups.md` (committed in
`3d7d8e6`). It carries the reviewer's rulings on the Prompt AE and Prompt AF
handoffs.

## a. Header

| | |
|---|---|
| Repo | `tilt_bed_controller` (firmware) |
| Branch | `main` |
| HEAD before | `b1991ed` (Handoff 2026-11-02 02: Prompt AF), parent `e646c3b`, level with `origin/main` after `git fetch` - as the prompt expects |
| HEAD after the work | `3d7d8e6` (one work commit: `ble_bed_service.c`, four docs, the prompt file) |
| After that | this handoff and the session's SESSION_LOG entry, committed together, then `git push` (normal push, no force) |
| Builds | from the command line (the prompt asks for it): exit 0, no warnings, with `BLE_DEBUG_WATCHDOG_LOG` 1, 0, and 1 again (d.2 #10-#12); after the commit `ninja: no work to do` (#31) |
| Host test | 0 checks failed (#13) |
| Bench | **nothing ran on a bed in this session** |

`git status` at the end of the work (after commit `3d7d8e6` and the
SESSION_LOG entry, before this file was written; d.2 #32):

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
| A1 (f.1) | A viewer's STOP_ALL must not clear `app_collision_latched`; everything else STOP_ALL does stays; the latch must still clear by the control app's release paths, including silence 300 ms after its last repeat, even if it sends nothing more; control STOP_ALL still clears | Done, `3d7d8e6` (`ble_bed_service.c:718-731`). How: b.1.1, b.3 |
| A2 (f.2) | The `any_collision` refusal also sets the latch | Done (`:813`) |
| A3 (f.7) | The trip branch also calls `note_app_commanding()`; confirm what it changes | Done (`:629`); confirmation b.2 |
| B1-B4 | Redo AF b.6 for the changed cases; confirm B1/B2/B4; list the callers again | b.4 (callers), b.5 (tables) |
| C1 | BLE_GATT_DESIGN §7 row 0x06 | Done, no Protocol Version change |
| C2 | DECISIONS #48 "Amended by Prompt AG" paragraph | Done (appended; existing text untouched) |
| C3 | REGRESSION 5.A3/5.A4/5.A5 updated; viewer STOP_ALL item | Done; new item 5.A6 |
| C4 | PRE_SHIP rows | F26, F27, F28 |
| C5 | Rulings in the SESSION_LOG entry | Done; also b.9 |
| D1 | Builds at 1 and 0, tails, warnings, size; host test | b.8; d.2 #9-#13, #31 |
| D2 | Expected console lines for B1, B2, B3 | b.6 |
| D3 | Section d: only this session's real output, no copies of other files | d (file-content reads are listed with a marker, not pasted) |
| D4 | `git status` before commit; commit only the prompt's files | d.2 #30 |

## b. Designs and analysis

Line references are to `3d7d8e6` unless marked. `ble_bed_service.c` is the
only source file changed; every other file:line is unchanged from `b1991ed`.
Every claim is VERIFIED (code read, or a command output in d) or ASSERTED.
Nothing ran on the bed.

Terms used below (all in `ble_bed_service.c`):

- `app_collision_latched` - the collision latch from DECISIONS #48.
- `app_commanding` + `app_command_deadline_tick` - "the app is still asking
  for motion". `note_app_commanding()` sets the flag and puts the deadline
  300 ms (`MOVE_WATCHDOG_MS`) ahead; `service_app_commanding()` runs every
  tick and, if the flag is set and the deadline has passed, calls
  `note_app_stopped_commanding("silence")`. This is "the silence timer".
- `note_app_stopped_commanding(reason)` - the app's release: clears
  `app_commanding`, releases the app's half of bed_control's #36 two-source
  latch (`bed_note_source_released(BED_SOURCE_APP)`), records the debug
  "cleared" print, clears the latch.

---

### b.1 The design as implemented

All in `ble_bed_service.c`, main-loop context. No I2C added, nothing blocks,
nothing is printed from a stack callback, no `config/`, `autogen/` or GATT
change. Three edits plus comments (complete diff in c.1).

#### b.1.1 A1 - a viewer's STOP_ALL keeps the latch (`:713-737`)

Before (`b1991ed`, `:708-722`), the STOP_ALL branch of
`process_pending_command()` was, for either role:

```c
move_watchdog_cancel();
app_move_active = false;
app_auto_level_active = false;
note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
app_stop_all();
send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
```

After:

```c
move_watchdog_cancel();
app_move_active = false;
app_auto_level_active = false;
if (conn == control_connection || !app_collision_latched) {
  note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
} else {
  // A viewer's STOP_ALL during the collision latch ... the collision latch
  // stays. app_commanding is left set ..., so the silence timeout keeps
  // running from the control app's last repeat.
  bed_note_source_released(BED_SOURCE_APP);
}
app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
```

`conn` is `stop_all_conn`, the connection handle the stack callback recorded
with the write (`:1403` at `b1991ed`, unchanged). What changes, and only in
the one case "STOP_ALL from a non-control connection while latched":

| Effect | Control STOP_ALL, or any STOP_ALL with no latch | Viewer STOP_ALL while latched |
|---|---|---|
| Watchdog cancelled, `app_move_active` / `app_auto_level_active` cleared | yes (as before) | yes (as before) |
| App's half of #36 released (`bed_note_source_released(BED_SOURCE_APP)`) | yes, via `note_app_stopped_commanding()` | yes, called directly |
| `app_stop_all()` (motors, auto-level, LEDs, `active_switch = OFF`) | yes | yes |
| Result 0x00 to the sender | yes | yes |
| `app_collision_latched` cleared | yes | **no** |
| `app_commanding` cleared (stops the silence timer) | yes | **no** - the timer keeps running from the control app's last start |
| Debug: refused count reset, "cleared" print recorded | yes | **no** - the count keeps running and the eventual "cleared" line covers the whole episode |

**How the silence timer is guaranteed to be running after a viewer STOP_ALL
("the simplest way that gives that").** No new state and no change to
`service_app_commanding()`. It rests on an invariant that A2 and A3 create:

> **The latch is set only together with `note_app_commanding()`, and
> `app_commanding` is cleared only by `note_app_stopped_commanding()`, which
> also clears the latch. So whenever the latch is set, `app_commanding` is
> true and the silence deadline is at most 300 ms after the control app's
> last MOVE_START / AUTO_LEVEL_START (or the trip).**

Evidence: the latch is set at exactly two places, `:628` (trip, followed by
`note_app_commanding()` at `:629`) and `:813` (refusal, followed by
`note_app_commanding()` at `:814`) (d.2 #15). `app_commanding = false`
appears only inside `note_app_stopped_commanding()` (`:577`; d.2 #3 lists
every use of `app_commanding` - `:574` at the time of #3, before the
comment lines were added - and no other writer), which ends with
`app_collision_latched = false` (`:589`).

So the viewer branch only has to **not** call `note_app_stopped_commanding()`.
The silence timer then fires 300 ms after the control app's last start, as
the prompt asks ("silence 300 ms after its last repeat"), whether or not the
control app sends anything after the STOP_ALL. If the control app keeps
repeating, each refused repeat pushes the deadline on (`:814`), exactly as
before the STOP_ALL. The control app's own MOVE_STOP / AUTO_LEVEL_STOP
(`:889`, `:928`) and its own STOP_ALL (`:722`, `conn == control_connection`)
clear the latch as before. The control-phone disconnect: see b.7.1.

**Why `bed_note_source_released(BED_SOURCE_APP)` is still called.** "Everything
else that STOP_ALL does today stays exactly as it is." Today a viewer STOP_ALL
releases the app's half of the #36 latch; it still does. When the silence
timer later calls `note_app_stopped_commanding("silence")`, that calls
`bed_note_source_released(BED_SOURCE_APP)` a second time. That second call is
a no-op on bed_control's state, from code: it sets
`source_commanding[BED_SOURCE_APP] = false` (already false - nothing can set
it true while latched, because every MOVE_START / AUTO_LEVEL_START is refused
before any bed_control call, `:811-822`), and it clears `conflict_inhibit`
only if both sources are not commanding (`bed_control.c:44-48`).
`conflict_inhibit` is set only in `bed_move_start()` / `bed_auto_level_start()`
right after `note_commanding(src, true)` (`bed_control.c:99-106`, `:127-134`),
and every release re-checks the clear condition, so `conflict_inhibit` true
implies some source is commanding; with the app already released, that is the
switches, so the second release cannot clear it. Redundant releases already
happen today (MOVE_STOP calls `note_app_stopped_commanding()` and then
`bed_move_stop()`, which releases again, `bed_control.c:115`).

**Alternatives considered, not taken.**

1. A `bool` parameter on `note_app_stopped_commanding()` meaning "keep the
   latch". Touches all six callers for one case; and it would still have to
   leave `app_commanding` set for the silence timer, so it is the same logic
   with a wider diff.
2. Leave STOP_ALL calling `note_app_stopped_commanding()` and save/restore
   the latch around it. That would stop the silence timer (`app_commanding`
   false), so the latch would never clear by silence - the exact requirement
   A1 states - unless `service_app_commanding()` were also changed to run
   while latched; and it would record a false "cleared (stop_all)" print and
   reset the refused count.
3. Change `service_app_commanding()`'s guard to
   `if (!app_commanding && !app_collision_latched) return;`. Works, but adds a
   second way for the timer to run; the invariant above makes it unnecessary.

**The one difference from "exactly as today" in the viewer-while-latched
case** is that `app_commanding` stays true (the silence timer keeps running)
and the debug refused-count is not reset. Both are the mechanism of A1, not
side effects; raised for confirmation in f.5.

#### b.1.2 A2 - every `any_collision` refusal latches (`:811-822`)

```c
if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
    && (app_collision_latched || any_collision)) {
  app_collision_latched = true;     // new (A2)
  note_app_commanding();
  send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);
#if BLE_DEBUG_WATCHDOG_LOG
  if (wd_diag_collision_refused < 0xFFFF) {
    wd_diag_collision_refused++;
  }
#endif
  return;
}
```

Position unchanged: after the viewer check (`:788`) and the settings-mode
check (`:793`), before the #36 check (`:834`). So only the control connection
can set the latch here (a viewer's MOVE_START is answered 0x02 first). When
the latch is already set, the new line is a no-op. When it is not (a press
during a collision sequence that the app did not trip - a re-press after its
own release, an app press during a physical-controller collision, or a
repeat processed in the trip pass before `service_move_watchdog()` runs), the
press now latches, and its repeats are refused **after the sequence too**,
until the app releases. Because the refusal already calls
`note_app_commanding()`, the silence clear is armed (the invariant in b.1.1).
The refused count now always belongs to a latched episode, so the "latch
cleared" print covers these refusals (AF f.5 resolved).

#### b.1.3 A3 - the trip arms the silence timer (`:613-637`)

```c
if (any_collision) {
  move_watchdog_cancel();
  app_move_active = false;
  app_auto_level_active = false;
  app_collision_latched = true;
  note_app_commanding();            // new (A3)
  send_command_result(control_connection, last_move_seq, CMD_RESULT_COLLISION_STOP);
#if BLE_DEBUG_WATCHDOG_LOG
  printf("BLE: COLLISION_STOP seq=%u op=0x%02x (latched)\r\n", ...);
#endif
  return;
}
```

Still **not** `note_app_stopped_commanding()`. This closes the AF b.11.3
window: a trip landing in the few ms between the silence clear and the
watchdog expiry used to set the latch with `app_commanding` already false, so
silence could not clear it. Now the silence timer runs from the trip, and the
latch clears 300 ms after the trip (or after the last refused repeat, if
later).

### b.2 A3 confirmation - what `note_app_commanding()` changes

The function (`:565-570`, read in d.2 #25, unchanged by this session):

```c
static void note_app_commanding(void)
{
  app_commanding = true;
  app_command_deadline_tick = sl_sleeptimer_get_tick_count()
                              + sl_sleeptimer_ms_to_tick(MOVE_WATCHDOG_MS);
}
```

- It writes exactly two variables, both file-static in `ble_bed_service.c`:
  `app_commanding` and `app_command_deadline_tick`. It calls only two pure
  sleeptimer helpers (read the tick counter, convert ms to ticks).
- Every use of those two variables (d.2 #3): the declarations (`:348-349` at
  the time), `note_app_commanding()` itself, `note_app_stopped_commanding()`
  (clears `app_commanding`), and `service_app_commanding()` (reads both). No
  other file can see them (static).
- It calls nothing in bed_control. **"Refreshes the app's half of the #36
  latch" is true only in this sense:** the app's half is bed_control's
  `source_commanding[BED_SOURCE_APP]`; it is set true only inside
  `bed_move_start()` / `bed_auto_level_start()` (`bed_control.c:99`, `:127`)
  and cleared by `bed_note_source_released()` / the stop verbs
  (`bed_control.c:51-54`, `:115`, `:143`). `note_app_commanding()` neither sets
  nor clears it; it postpones the silence path's
  `bed_note_source_released(BED_SOURCE_APP)` to 300 ms after the call. At a
  trip the app's half is normally already true (the accepted start that armed
  the watchdog set it), so postponing its release is the "refresh". In the
  b.11.3 window it was already released by silence, and the trip's
  `note_app_commanding()` does not re-assert it.
- So: it refreshes the silence deadline and, through it, holds off the
  app's #36 release; it changes nothing else. **Confirmed from code.**

Consequence for the #36 latch: at a trip, the app's #36 half is released up to
~100 ms later than in AF when the app goes quiet at the trip (300 ms after the
trip instead of 300 ms after the last accepted repeat). While it is held,
`conflict_inhibit` cannot be newly set by the app (its starts never reach
bed_control while latched), and a physical press on an idle bed is accepted
normally (`bed_control.c:97-111`: `conflict_inhibit` false, idle bed ->
SWITCHES takes the lock). No observable difference expected; ASSERTED.

### b.3 Why A1 needs A2 and A3 (the order of the argument)

Without A3, the trip could leave the latch set with `app_commanding` false
(b.11.3), and a viewer STOP_ALL in that state would leave nothing to clear the
latch except the control app's next explicit release. Without A2, an
`any_collision` refusal would not set the latch, so the invariant would hold
vacuously there; with A2, it sets both together. So A1's "leave
`app_commanding` alone" is sufficient only because both latch-setting places
call `note_app_commanding()` - which is now true at `:628-629` and `:813-814`.
A future change that sets the latch anywhere else must keep that pairing; the
comment on `app_collision_latched` (`:333-344`) now says so.

### b.4 Every caller of `note_app_stopped_commanding()`, after the change

From `grep -n` (d.2 #15):

| # | Caller | Line | Reason printed | Runs when | Clears the latch? |
|---|---|---|---|---|---|
| 1 | `service_app_commanding()` | `:602` | `silence` | `app_commanding` and 300 ms since the last `note_app_commanding()` - the last MOVE_START / AUTO_LEVEL_START from the app (accepted or refused) or the trip | Yes. **Now always armed while latched** (b.1.1 invariant) |
| 2 | Watchdog expiry, `service_move_watchdog()` | `:670` | `watchdog` | Watchdog armed and expired | Cannot occur while latched (unchanged: the trip cancels the watchdog and every start is refused before it can re-arm it) |
| 3 | STOP_ALL, `process_pending_command()` | `:722` | `stop_all` | **Changed:** a STOP_ALL from the control connection, or from any connection when not latched. A viewer's STOP_ALL while latched takes the `else` branch (`:723-731`) and does not call it | Yes, for the control phone; **not for a viewer while latched** |
| 4 | MOVE_STOP | `:889` | `stop` | Control connection only (a viewer is rejected at `:788` first), arguments 0/0 | Yes |
| 5 | AUTO_LEVEL_STOP | `:928` | `stop` | Control connection only, arguments 0/0 | Yes |
| 6 | Control-phone disconnect stop, `ble_bed_service_tick()` | `:1113` | `disconnect` | `disconnect_stop_pending`, set only when the closed link was control **and** `bed_active_source() == BED_SOURCE_APP` (`:1011`) | Yes, when it runs. Conditional (AF f.4, accepted); after a viewer STOP_ALL it does not run - b.7.1 |

Six callers, as in AF; the only change is the condition on row 3.

### b.5 What changes for the user (ASSERTED, from code)

Notation as in AF b.6: `t` = time after the trip pass; `N` = seq of the last
accepted repeat before the trip (= `last_move_seq`). Collision sequence
timeline, unchanged (AF b.4): pass after the trip prints `Once`, LEDs on;
blink 1 at ~0.5 s; blink 2 at ~1.0 s starts the reverse (a move only); the
runaway fail-safe cuts the reverse ~150-200 ms later (AF b.11.1, PRE_SHIP
F26); at ~1.5 s all off and `any_collision` false. (i) = an app that keeps
repeating every 100 ms while the finger is down (today's app); (ii) = an app
that ends its hold at the first 0x06 (BLE_GATT_DESIGN §7).

The trip pass itself, unchanged except as noted: if a repeat is pending in
that pass, `process_pending_command()` refuses it first (0x06 with its own
seq) **and now sets the latch** (A2); then `service_move_watchdog()`'s trip
branch sends 0x06 with seq N, sets the latch (already set) and **now calls
`note_app_commanding()`** (A3). The app may receive one or two 0x06 in the
trip pass (AF b.11.4).

#### B1 (AF B3). Released and pressed again ~0.5 s after the trip, held past the end of the sequence

(i) app keeps repeating the re-press:

| t | App sends | Command Result | Motors / state |
|---|---|---|---|
| 0 | - | `{N, 0x06}` (+ one more if a repeat was pending) | all off at the trip; latched |
| 0.1-0.4 s | MOVE_START repeats | `{seq, 0x06}` each (~4) | off |
| ~0.45 s | MOVE_STOP (finger up), seq M | `{M, 0x00}`; **latch cleared, reason `stop`** | stay off. `bed_move_stop()` -> `app_move_stop()`; `app_apply_stop_outputs()` turns all LEDs off and sets `active_switch = OFF` (the blink changes phase - cosmetic, unchanged from AF) |
| ~0.5 s | new MOVE_START (same button), seq P | `{P, 0x06}` - refused by `any_collision`, **and now latched** (A2) | off |
| 0.6-1.5 s | repeats | `{seq, 0x06}` each | ~1.0 s: the reverse (from `active_switch_during_collision`, saved in the pass after the trip, `app.c:589`); ~1.15-1.2 s: cut by the fail-safe; ~1.5 s: sequence ends |
| after ~1.5 s, finger still down | repeats | **`{seq, 0x06}` each - still latched** | **stay off.** (AF: the first repeat after ~1.5 s got 0x00 and **the move started again**) |
| release | MOVE_STOP, seq Q | `{Q, 0x00}`; latch cleared, reason `stop` | stay off |
| later | fresh MOVE_START | `{seq, 0x00}` | normal move |

If that last MOVE_STOP is lost: cleared by `silence` 300 ms after the last
repeat. Nothing moves in between.

(ii) app handles 0x06: the original hold ends at the first 0x06 (MOVE_STOP ->
`stop`, or quiet -> `silence` ~300 ms after the trip). The re-press at
~0.5 s gets `{P, 0x06}`, which now latches; the app ends that hold too
(MOVE_STOP -> `stop` with K = 1, or quiet -> `silence` 300 ms after P).
Nothing starts. Motors as in (i): off, the reverse at ~1.0 s (cut), off at
~1.5 s. Difference from AF: only that the re-press now latches (and therefore
produces a "cleared" print).

#### B2 (AF B5). STOP_ALL during the latch - from the viewer phone and from the control phone, inside and after the sequence

Setup: control phone holding a MOVE into the obstruction (the trip latched);
for the viewer cases a second phone is connected as VIEWER. "Inside" = STOP_ALL
at ~0.5 s; "after" = at ~2 s.

**B2.a Control phone, inside the sequence.**

| | Command Result | Motors / state |
|---|---|---|
| STOP_ALL (control) at ~0.5 s | `{seq, 0x00}` to the control phone; **latch cleared, reason `stop_all`** (unchanged) | `app_stop_all()`: off, LEDs off (amber back if the lights are on), `active_switch = OFF`. The collision sequence continues: reverse at ~1.0 s (cut ~1.15-1.2 s), off at ~1.5 s |
| (i) next held repeat, ~0.6 s | `{seq, 0x06}` - refused by `any_collision` **and re-latched** (A2) | off |
| (i) repeats during and **after** the sequence | `{seq, 0x06}` each | **stay off.** (AF: the first repeat after ~1.5 s got 0x00 and **the move started again**) |
| (i) release (MOVE_STOP) | `{M, 0x00}`; latch cleared, reason `stop` | stay off |
| (ii) | the app ended its hold at the trip's 0x06, so no repeats; STOP_ALL clears the latch if it is still set (or finds it already cleared) | nothing moves except the reverse |

If the STOP_ALL and a repeat land in the same pass: STOP_ALL is processed
first (`:713`), clears the latch; the repeat right after it is refused by
`any_collision` and re-latches. Same outcome.

**B2.b Control phone, after the sequence.** Unchanged from AF:

| | Command Result | Motors / state |
|---|---|---|
| STOP_ALL (control) at ~2 s | `{seq, 0x00}`; latch cleared, reason `stop_all` | already off; stay off |
| (i) next held repeat | `{seq, 0x00}` - **accepted** (latch clear, `any_collision` false) | **the move starts again**, with supervision running; if toward the obstruction it trips again after supervision's blind window (PRE_SHIP F20), and B1-of-AF repeats |
| (ii) | no repeats | nothing moves |

This is the same as a STOP_ALL during any ordinary held move today (the next
repeat restarts it). The prompt keeps "a STOP_ALL from the control connection
still clears the latch, as now"; noted in f.2.

**B2.c Viewer phone, inside the sequence.** New behaviour (A1):

| | Command Result | Motors / state |
|---|---|---|
| STOP_ALL (viewer) at ~0.5 s | `{seq, 0x00}` **to the viewer**; latch **stays**; no print | `app_stop_all()` (as before): off, LEDs off, `active_switch = OFF`; the app's #36 half released. The sequence continues: reverse ~1.0 s, cut ~1.15-1.2 s, off ~1.5 s |
| (i) control repeats, during and after the sequence | `{seq, 0x06}` each, to the control phone | **stay off.** (AF: refused only until ~1.5 s, then **the move started again**) |
| (i) control release (MOVE_STOP) | `{M, 0x00}`; latch cleared, reason `stop`; K counts every refusal since the trip | stay off |
| (i) control goes quiet without a stop write | - | latch cleared by `silence` 300 ms after its last repeat |
| (ii) | the control app ended its hold at the trip's 0x06; its MOVE_STOP cleared the latch at once, or silence cleared it ~0.3 s after the trip - in both cases before a STOP_ALL at ~0.5 s, which is then an ordinary STOP_ALL (no latch: the normal path, `note_app_stopped_commanding("stop_all")`, no print) | nothing moves except the reverse |

**B2.d Viewer phone, after the sequence.**

| | Command Result | Motors / state |
|---|---|---|
| STOP_ALL (viewer) at ~2 s | `{seq, 0x00}` to the viewer; latch stays; no print | already off; stay off |
| (i) control repeats | `{seq, 0x06}` each | **stay off.** (AF: the next repeat got 0x00 and **the move started again**) |
| (i) control release | `{M, 0x00}`, `stop` | stay off |
| (ii) | latch already clear (as B2.c (ii)) | nothing moves |

#### B3. An app MOVE press during a physical-controller collision sequence

Setup: the switch panel drives a pair into the obstruction; the app is
connected as CONTROL and idle. Physical trip at t = 0 (supervision in
`switch_input_tick()`): motors off, `any_collision`. The app watchdog is not
armed, so `service_move_watchdog()` does nothing: no 0x06 at the trip, no
`BLE: COLLISION_STOP` print (unchanged from AF).

(i) app MOVE pressed at ~0.5 s and held ~2 s past the end of the sequence:

| t | App sends | Command Result | Motors / state |
|---|---|---|---|
| ~0.5 s | MOVE_START, seq P | `{P, 0x06}` - refused by `any_collision`; **now latched** (A2); silence timer armed | off. No bed_control call: no #36 effect on the physical source |
| 0.6-1.5 s | repeats | `{seq, 0x06}` each | ~1.0 s: the reverse of the panel's pair. If the panel button is still down, the fail-safe stands down (`switch_input.c:831-838`) and the reverse runs its ~500 ms; if it is up, the fail-safe cuts it after ~150-200 ms (F26). ~1.5 s: all off |
| after ~1.5 s, finger still down | repeats | **`{seq, 0x06}` each** | **app starts nothing.** (AF: the first repeat after ~1.5 s got 0x00 and **the app's move started**.) The panel works normally (its own path; the app's refusals never reach bed_control) |
| release | MOVE_STOP, seq M | `{M, 0x00}`; latch cleared, reason `stop` | see note below |
| later | fresh app press | `{seq, 0x00}` | normal move |

Note on the app's MOVE_STOP (unchanged behaviour, from code): it calls
`bed_move_stop(BED_SOURCE_APP)` and then `app_apply_stop_outputs()`. If the
panel holds the bed at that moment (during the sequence, or a panel move
after it), `arbitrate()` returns FORCE_STOP and `app_stop_movement()` stops
the bed (#12 stop-from-anyone; `bed_control.c:113-119`); and
`app_apply_stop_outputs()` turns all motors and LEDs off and sets
`active_switch = OFF` (`bed_actions.c:334-345`). So an app release during the
reverse cuts the reverse. Pre-existing, not changed here (b.7.3).

(ii) app handles 0x06: the press's first 0x06 ends its hold; MOVE_STOP ->
`stop` (K = 1), or quiet -> `silence` 300 ms after P. Nothing starts from the
app.

An AUTO_LEVEL_START during a physical collision behaves the same (refused,
latched, released by AUTO_LEVEL_STOP / silence).

#### B4. AF B1, B2 and B4 - unchanged, except as stated

- **AF B1 (MOVE hold, collision, held 3 s, released).** Command Results and
  motors unchanged for (i): every repeat is refused while latched (A2's extra
  `app_collision_latched = true` is a no-op when already latched). Changed:
  the silence deadline is now set at the trip (A3). For (i) the repeats keep
  pushing it on, so no difference. For (ii), an app that goes quiet at the
  first 0x06 now has its latch cleared by `silence` 300 ms after the trip (or
  after a refused trip-pass repeat), instead of 300 ms after the last
  accepted repeat - i.e. up to ~100 ms later; the app's #36 half is released
  at the same moment (b.2). No motor difference.
- **AF B2 (AUTO LEVEL hold).** As AF B1: unchanged except the same (ii)
  timing.
- **AF B4 (link drop during the latch).** Drop during the sequence with no
  STOP_ALL: the disconnect stop runs as before (`disconnect`). Drop after the
  sequence: `silence`, as before. Changed: (1) the silence clear after a drop
  right after the trip is 300 ms after the trip (A3); (2) a viewer promoted
  to control that presses during the remaining sequence is now latched by the
  refusal (A2) and must release; (3) new case - drop **after a viewer
  STOP_ALL**: the bed is idle (`app_stop_all()` set `active_switch = OFF`),
  so `bed_active_source()` is NONE and the disconnect stop does not run; the
  latch clears by `silence` 300 ms after the control app's last write
  (b.7.1). In every case the firmware starts nothing by itself after a
  reconnect (unchanged).

### b.6 D2 - expected console lines (BLE_DEBUG_WATCHDOG_LOG 1)

ASSERTED (from code; nothing ran). Same conventions as AF b.10: `<...>` = a
value that varies; `<TRIP LINES>` = the summed-rise block (`Tripped on
steady!`, `Securit margin: ...`, four `Motor n Delta Current: ...` lines) or
the max-load block (empty line, `TRIPPED MAX LOAD`, `Motor <m> Filtered
Current: ...`), never both in one pass (AF V17). Other bench flags as
committed: `BED_CAPTURE_MOTOR_CURRENTS` 0, `SWITCH_SAMPLER_DIAG` 1
(`switch_input.c:57`, d.2 #6), `BED_DEBUG_LEVELED_LOG` 1 (`bed_actions.c:46`,
d.2 #21). A `LEVELED (Bed Status bit 4) -> 0` line prints at a move's start
only if the bed was level before it (`bed_actions.c:143`); it is omitted
below.

**B1 - app MOVE hold into the obstruction; released at ~0.45 s, the same button
pressed again at ~0.5 s and held ~2.5 s:**

```
<TRIP LINES>
BLE: COLLISION_STOP seq=<N> op=0x01 (latched)
BLE: hold end op=0x01 seq=<N>: <R> refreshes, longest refresh gap <G> ms, longest between passes <B> ms
Once
BLE: collision latch cleared (stop), <K ~4-5> repeats refused
FAILSAFE: motors energised with nothing commanding - stopped
BLE: collision latch cleared (stop), <K2 ~25> repeats refused
```

- Lines 1-3: the trip pass; `Once`: the next pass.
- First `cleared (stop)`: the release at ~0.45 s (refusals 0.1-0.4 s, plus a
  trip-pass refusal if there was one).
- `FAILSAFE`: ~1.15-1.2 s after the trip, during the reverse (AF b.11.1).
- **No line at ~1.5 s and no move**: the re-press is latched (AF printed
  nothing here either, but the move restarted).
- Second `cleared (stop)`: at the re-press's release; K2 ≈ 10 per second of
  the re-press (counted from its first refusal at ~0.5 s).
- With an app that ends its hold at the first 0x06: line 5 becomes
  `cleared (stop), 0-1 repeats refused` or `cleared (silence), ...` ~300 ms
  after the trip, and the last line `cleared (stop), 1 repeats refused` (or
  `(silence)`) right after the re-press.

**B2 - STOP_ALL during the latch (control repeating the held move into the
obstruction, finger held ~3 s in all four cases):**

B2.a control phone, ~0.5 s:
```
<TRIP LINES>
BLE: COLLISION_STOP seq=<N> op=0x01 (latched)
BLE: hold end op=0x01 seq=<N>: ...
Once
BLE: collision latch cleared (stop_all), <K ~4-5> repeats refused
FAILSAFE: motors energised with nothing commanding - stopped
BLE: collision latch cleared (stop), <K2 ~25> repeats refused
```
The second `cleared` is at the finger release; K2 counts the repeats from
~0.6 s (re-latched) to the release. No move at ~1.5 s.

B2.b control phone, ~2 s (unchanged from AF):
```
<TRIP LINES>
BLE: COLLISION_STOP seq=<N> op=0x01 (latched)
BLE: hold end op=0x01 seq=<N>: ...
Once
FAILSAFE: motors energised with nothing commanding - stopped
BLE: collision latch cleared (stop_all), <K ~19-20> repeats refused
```
then the next held repeat restarts the move with no print; it ends with
`BLE: hold end op=0x01 ...` at the release, or with a new `<TRIP LINES>` +
`BLE: COLLISION_STOP ...` block if it reaches the obstruction again.

B2.c viewer phone, ~0.5 s, and B2.d viewer phone, ~2 s - the same lines:
```
<TRIP LINES>
BLE: COLLISION_STOP seq=<N> op=0x01 (latched)
BLE: hold end op=0x01 seq=<N>: ...
Once
FAILSAFE: motors energised with nothing commanding - stopped
BLE: collision latch cleared (stop), <K ~30> repeats refused
```
- **Nothing prints at the viewer's STOP_ALL** (the latch is not cleared, so no
  "cleared" line is recorded; `app_stop_all()` prints nothing).
- The `cleared (stop)` line is at the control phone's release; K counts every
  refusal since the trip, before and after the STOP_ALL (~10 per second
  held).
- If the control phone stops sending without a stop write (e.g. its
  Bluetooth is switched off): `BLE: collision latch cleared (silence), <K>
  repeats refused` ~300 ms after its last repeat - `silence`, not
  `disconnect` (b.7.1).

**B3 - app MOVE press during a physical-controller collision (panel button
held into the obstruction, released at some point; app pressed at ~0.5 s and
held ~2 s past the end of the sequence):**

```
SW: 0x<bit> down (bounces <n>)
<TRIP LINES>
Once
SW: 0x<bit> up (bounces <n>)
FAILSAFE: motors energised with nothing commanding - stopped
BLE: collision latch cleared (stop), <K ~35> repeats refused
```
- `SW: ... down` is the panel press (the sampler's debounced edge,
  `switch_input.c:423`).
- `SW: ... up` prints whenever the panel button is released; its position
  depends on the user.
- `FAILSAFE` prints only if the panel button is already up when the reverse
  runs (~1.0 s); if the button is still down, there is no `FAILSAFE` line and
  the reverse runs ~500 ms.
- **No `BLE: COLLISION_STOP` and no `BLE: hold end`** - the app never had an
  accepted hold.
- **No move from the app at ~1.5 s** (AF: the app's move started).
- `cleared (stop)` at the app's release; K counts its refusals from ~0.5 s.
  If the app releases during the reverse, its MOVE_STOP cuts the reverse
  (b.5 B3 note) and the `FAILSAFE` line does not appear.

### b.7 Findings while implementing (ASSERTED unless marked)

**b.7.1 After a viewer STOP_ALL, the control-phone disconnect clears the
latch by silence, not through the disconnect stop.** A viewer STOP_ALL runs
`app_stop_all()`, which sets `active_switch = OFF` (`bed_actions.c:266`), so
the bed is idle even while the collision sequence is still running (the
reverse does not set `active_switch` - d.2 #26 shows no assignment in
`dual_motor_control.c`). `bed_control_tick()` then returns the source to
NONE (`bed_control.c:166-171`), and `handle_connection_closed()` sets
`disconnect_stop_pending` only if the source is APP (`:1011`). So a control
drop after a viewer STOP_ALL leaves the latch to the silence timer, which is
running (b.1.1), and it clears ≤300 ms after the control app's last write,
printing `silence`. This is the AF f.4 situation (accepted), now reachable
also during the sequence. The prompt lists "the control-phone disconnect"
among the paths that must still clear the latch: the latch is cleared after
the disconnect, within 300 ms, by a different path. Raised in f.1.

**b.7.2 A STOP_ALL written by the control phone but processed after a
control drop + viewer promotion** is compared with the new
`control_connection` and treated as a viewer's: if latched, the latch stays,
and clears by silence (`app_commanding` is true). Needs the STOP_ALL write
and the drop to land before the same main-loop pass. Safe direction (keeps
refusing). Raised in f.3.

**b.7.3 An app release during the collision reverse cuts the reverse.**
Pre-existing (AF and before), not changed here. MOVE_STOP and AUTO_LEVEL_STOP
call `app_apply_stop_outputs()` (`set_all_motors_off()`,
`bed_actions.c:334-345`); STOP_ALL calls `app_stop_all()`; a MOVE_STOP while
the panel holds the bed also force-stops via `app_stop_movement()`
(`bed_actions.c:368-375`). So a release landing at ~1.0-1.2 s ends the
reverse at once. Together with the fail-safe (F26), the reverse after an app
collision is short whenever the user does anything. Raised in f.4.

**b.7.4 The remaining restart path** is the control phone's own STOP_ALL
after the sequence while a finger stays on the held button (B2.b) - unchanged
by design ("as now"). Raised in f.2.

### b.8 D1 - build and size

Built with the session wrapper (d.3); host test with MinGW gcc (#13).

| Build | Result | Evidence |
|---|---|---|
| Existing ELF before any build (built 09:00 by the AF session) | text 245588, data 784, bss 72476 - matches the AF record | d.2 #8, #9 |
| After the edit, `BLE_DEBUG_WATCHDOG_LOG` 1 | 1 object + link, exit 0, **no warning/error lines**; text **245620**, data 784, bss 72476 | #10 |
| `BLE_DEBUG_WATCHDOG_LOG` 0 (temporary, `sed` on `ble_bed_service.h`) | 3 objects + link, exit 0, no warnings; text 243924, data 780, bss 72480 | #11 |
| Back to 1 | 3 objects + link, exit 0, no warnings; text 245620 / 784 / 72476 (= the first); `git status --short` shows no `ble_bed_service.h` change; header line 36 reads `1` | #12 |
| After commit `3d7d8e6` | `ninja: no work to do.`, exit 0 | #31 |
| Host test `test/bed_control_test.c` | `0 check(s) failed` | #13 |

Size change from AF (`BLE_DEBUG_WATCHDOG_LOG` 1): **text +32 B, data 0,
bss 0** - flash +32 B, RAM unchanged. The full build log is in the scratchpad
(`build_out.txt`) and was overwritten by each build; the wrapper prints its
last 25 lines and every line matching `warning` or `error` (d.3), and those
are in d.2. The full logs of #10-#12 are therefore lost; their tails are
complete because each build printed fewer than 25 lines.

### b.9 C5 - rulings recorded (copied into the SESSION_LOG entry)

- **AE QUESTIONS:** 1 and 2 closed by Prompt AF; 3 accepted ("Bed is level"
  in all four success outcomes); 4 accepted (settings-mode guard on the amber
  restore); 5 accepted (a blocked 5 s settings entry still toggles at the
  release); 6 -> PRE_SHIP F27; 7 yes, REGRESSION 8.d stays record-only until
  the bench; 8 -> PRE_SHIP F28.
- **AF QUESTIONS:** f.1, f.2, f.7 fixed here (`3d7d8e6`); f.3 -> PRE_SHIP F26;
  f.4 accepted; f.5 resolved by A2; f.6 keep REGRESSION 5.A5 (kept, and
  extended); f.8 agreed - the reviewer's bench test uses a soft obstruction.

### b.10 Docs changed (complete diff in c.2)

- **BLE_GATT_DESIGN §7, row 0x06:** case 2 now says a refusal also sets the
  latch; the release list says "STOP_ALL from the control connection" and that
  a viewer's STOP_ALL stops the bed but does not clear the latch (it clears by
  the control app's release or 300 ms after its last start); the "App
  behaviour" paragraph no longer says the firmware relies on the app for a
  re-press. Protocol Version unchanged (no byte or code change).
- **DECISIONS #48:** new subsection "Amended by Prompt AG (2026-11-02)" at the
  end of the entry: the three changes, why, the invariant, what remains; the
  existing text is not rewritten (it still contains the AF wording, e.g. "from
  either role", "It does not set the latch"; the amendment says it supersedes
  it).
- **REGRESSION §5:** 5.A3 (re-press now stays refused after the sequence),
  5.A4 (now "from the control phone"; inside the sequence nothing moves even
  after it; after the sequence the restart remains), 5.A5 (an app press held
  past the end of a physical collision stays refused; #12 note), new 5.A6
  (viewer STOP_ALL during the latch, with an optional silence check).
- **PRE_SHIP:** F26 (fail-safe vs the reverse), F27 (BED LIGHTS vs #36), F28
  (collision during a BED LIGHTS hold), with owner, how closed, source.

---

## c. Diffs

`git show 3d7d8e6`, split into the source file and the docs. The prompt file
(113 lines, new) is not repeated: it is the committed
`docs/prompts/PROMPT_AG_collision-latch-followups.md`. The
`docs/SESSION_LOG.md` diff is omitted under CLAUDE.md's standing exception (it
ships with this file).

### c.1 Source diff (complete; the only source change in the session)

```diff
commit 3d7d8e6532ba299e9e08d29bc3861dfcef386ae7
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Mon Nov 2 09:50:44 2026 -0800

    Collision latch follow-ups: viewer STOP_ALL keeps it, refusals latch, trip arms silence (DECISIONS #48, Prompt AG)
    
    - A STOP_ALL from a viewer no longer clears app_collision_latched; it still
      stops everything and releases the app's half of the #36 latch. The silence
      timeout keeps running from the control app's last repeat.
    - The any_collision refusal of MOVE_START / AUTO_LEVEL_START also sets the
      latch, so any app hold that meets a collision must be released first.
    - The trip branch calls note_app_commanding(), so the silence timer always
      runs after a latch is set (closes the AF b.11.3 window).
    
    Docs: BLE_GATT_DESIGN §7 row 0x06, DECISIONS #48 amendment, REGRESSION
    5.A3-5.A6, PRE_SHIP F26-F28. Builds clean with BLE_DEBUG_WATCHDOG_LOG 1 and 0;
    not run on the bench.
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

diff --git a/ble_bed_service.c b/ble_bed_service.c
index fa49ce2..f3f3685 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -331,13 +331,16 @@ static bed_direction_t app_move_dir    = BED_DIR_UP;
 static bool app_auto_level_active = false;
 
 // Collision latch (DECISIONS #48). Set when a collision trips during an app
-// hold, while the user is still holding the app button. Every MOVE_START and
-// AUTO_LEVEL_START is then answered COLLISION_STOP and starts nothing, until
-// the app releases - an explicit stop, STOP_ALL, the control-phone
-// disconnect, or MOVE_WATCHDOG_MS of silence, i.e. every caller of
-// note_app_stopped_commanding(). Without it the held button's next repeat
-// re-energised the same pair, into the obstruction, during the collision
-// sequence.
+// hold, while the user is still holding the app button, and when a start is
+// refused because a collision sequence is running (amended by Prompt AG).
+// Every MOVE_START and AUTO_LEVEL_START is then answered COLLISION_STOP and
+// starts nothing, until the app releases - an explicit stop, the control
+// phone's STOP_ALL, the control-phone disconnect, or MOVE_WATCHDOG_MS of
+// silence, i.e. every caller of note_app_stopped_commanding(). A viewer's
+// STOP_ALL does not clear it. Always set together with note_app_commanding(),
+// so the silence timeout is running whenever it is set. Without it the held
+// button's next repeat re-energised the same pair, into the obstruction,
+// during the collision sequence.
 static bool app_collision_latched = false;
 
 // Whether the app is still asking for motion, for the two-source inhibit
@@ -617,11 +620,13 @@ static void service_move_watchdog(void)
     // The user is still holding the app button, so latch (#48): the repeats
     // that follow are refused and start nothing. NOT note_app_stopped_
     // commanding() - the app has not released, and its half of the #36
-    // latch stays as it is.
+    // latch stays as it is. note_app_commanding() restarts the silence
+    // timeout, so the latch clears by silence even if no repeat follows.
     move_watchdog_cancel();
     app_move_active = false;
     app_auto_level_active = false;
     app_collision_latched = true;
+    note_app_commanding();
     send_command_result(control_connection, last_move_seq, CMD_RESULT_COLLISION_STOP);
 #if BLE_DEBUG_WATCHDOG_LOG
     // After the motors are off and the result is sent.
@@ -713,7 +718,17 @@ static void process_pending_command(void)
     move_watchdog_cancel();
     app_move_active = false;
     app_auto_level_active = false;
-    note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
+    if (conn == control_connection || !app_collision_latched) {
+      note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
+    } else {
+      // A viewer's STOP_ALL during the collision latch (#48, amended by
+      // Prompt AG). It stops the bed and releases the app's half of the #36
+      // latch as before, but it is not the control user's release, so the
+      // collision latch stays. app_commanding is left set (the latch is only
+      // ever set with it set), so the silence timeout keeps running from the
+      // control app's last repeat.
+      bed_note_source_released(BED_SOURCE_APP);
+    }
     app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
 
     // Accepted from ANY role - control or viewer. DECISIONS #12's
@@ -786,13 +801,16 @@ static void process_pending_command(void)
   // also while ANY collision sequence runs - a quick release and press inside
   // the sequence, or a press during a physical-controller collision, would
   // otherwise re-energise a pair while current supervision is off
-  // (switch_input_tick() requires !any_collision).
+  // (switch_input_tick() requires !any_collision). A refusal also latches
+  // (Prompt AG), so a hold that met a collision must be released before a
+  // new start is accepted, even once the sequence has ended.
   //
   // note_app_commanding() as in the #36 refusal: a refused repeat still means
   // the user is holding, so the silence timeout must not release the latch.
   // No bed_control call, no last_move_seq update, no watchdog arm.
   if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
       && (app_collision_latched || any_collision)) {
+    app_collision_latched = true;
     note_app_commanding();
     send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);
 #if BLE_DEBUG_WATCHDOG_LOG
```

### c.2 Docs diff (complete; 214 lines, not truncated)

```diff
commit 3d7d8e6532ba299e9e08d29bc3861dfcef386ae7
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Mon Nov 2 09:50:44 2026 -0800

    Collision latch follow-ups: viewer STOP_ALL keeps it, refusals latch, trip arms silence (DECISIONS #48, Prompt AG)
    
    - A STOP_ALL from a viewer no longer clears app_collision_latched; it still
      stops everything and releases the app's half of the #36 latch. The silence
      timeout keeps running from the control app's last repeat.
    - The any_collision refusal of MOVE_START / AUTO_LEVEL_START also sets the
      latch, so any app hold that meets a collision must be released first.
    - The trip branch calls note_app_commanding(), so the silence timer always
      runs after a latch is set (closes the AF b.11.3 window).
    
    Docs: BLE_GATT_DESIGN §7 row 0x06, DECISIONS #48 amendment, REGRESSION
    5.A3-5.A6, PRE_SHIP F26-F28. Builds clean with BLE_DEBUG_WATCHDOG_LOG 1 and 0;
    not run on the bench.
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

diff --git a/docs/BLE_GATT_DESIGN.md b/docs/BLE_GATT_DESIGN.md
index 343feb4..44ffc5e 100644
--- a/docs/BLE_GATT_DESIGN.md
+++ b/docs/BLE_GATT_DESIGN.md
@@ -382,25 +382,27 @@ Notify, 2 bytes: `{ seq u8, result u8 }`, echoing the command's seq.
    the app has not released since) **or while any collision sequence runs**
    (Bed Status flag bit 0 set - including one from the physical controller),
    echoing that command's own seq. The command starts nothing and does not arm
-   the watchdog.
+   the watchdog. **Such a refusal also sets the latch** (DECISIONS #48,
+   amended by Prompt AG): a hold that meets a collision - including a new
+   press made while any collision sequence runs - must be released before a
+   new start is accepted, even after the sequence has ended.
 
 The latch clears when the app releases: MOVE_STOP, AUTO_LEVEL_STOP, STOP_ALL
-(from either connection), the control connection dropping while the app holds
-the bed, or `MOVE_WATCHDOG_MS` (300 ms) with no MOVE_START / AUTO_LEVEL_START
-from the app. A press after that, once the collision sequence has ended, is a
-normal start. Checked after the viewer and settings-mode rejections and before
+**from the control connection**, the control connection dropping while the
+app holds the bed, or `MOVE_WATCHDOG_MS` (300 ms) with no MOVE_START /
+AUTO_LEVEL_START from the app. **A viewer's STOP_ALL does not clear it**: it
+stops the bed as always (and is answered 0x00), but the control user may still
+be holding, so the latch then clears by the control app's own release or 300
+ms after its last MOVE_START / AUTO_LEVEL_START. A press after the release,
+once the collision sequence has ended, is a normal start. Checked after the viewer and settings-mode rejections and before
 the #36 two-source check, so a viewer still gets 0x02, settings mode 0x03, and
 the app is told "collision", not "wall switch".
 
 App behaviour: on 0x06, end the hold (stop repeating; a MOVE_STOP /
 AUTO_LEVEL_STOP is fine) and show the collision screen until the user lifts
-the finger; a new press is a new hold. The firmware does not rely on this for
-the hold that collided: its repeats are refused until it releases. It does
-rely on it for one case: a **new** press made while the collision sequence is
-still running (released and pressed again within ~1.5 s) is refused only
-until the sequence ends, because it did not set the latch - if the app keeps
-repeating that press past the end of the sequence, the next repeat starts the
-bed (with current supervision running again).
+the finger; a new press is a new hold. The firmware does not rely on this:
+the repeats of any hold that met a collision are refused until it releases,
+including a new press made while the collision sequence is still running.
 
 **Protocol version: additive within v1, no version change.** The code was
 unused and the byte layout is unchanged; a client that does not know 0x06
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index c2f7d10..3436774 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2551,3 +2551,61 @@ Builds clean (no warnings, `BLE_DEBUG_WATCHDOG_LOG` 1 and 0). The host test
 (`test/bed_control_test.c`) is unchanged and passes; it cannot reach this
 code (it compiles bed_control.c only). **Not run on the bench.** REGRESSION §5,
 items marked #48.
+
+### Amended by Prompt AG (2026-11-02)
+
+Reviewer's rulings on the Prompt AF handoff (`fw_HANDOFF_2026-11-02_02_...`,
+QUESTIONS f.1, f.2, f.7). Three changes, `ble_bed_service.c` only; the text
+above is left as written and is superseded where it says otherwise. Line
+numbers are after the change.
+
+1. **A viewer's STOP_ALL does not clear the latch (f.1).** A STOP_ALL from the
+   connection that is not the control connection still does everything it did
+   (watchdog cancelled, both app flags cleared, the app's half of the #36 latch
+   released with `bed_note_source_released(BED_SOURCE_APP)`, `app_stop_all()`,
+   0x00 to the sender), but while the latch is set it does not call
+   `note_app_stopped_commanding()` (`ble_bed_service.c:718-731`). So the latch
+   stays, and `app_commanding` stays set - the latch is only ever set together
+   with `note_app_commanding()` (changes 2 and 3) and only
+   `note_app_stopped_commanding()` clears `app_commanding`, so the silence
+   timeout keeps running from the control app's last MOVE_START /
+   AUTO_LEVEL_START and clears the latch 300 ms after it even if the control
+   app sends nothing more. Its own MOVE_STOP / AUTO_LEVEL_STOP / STOP_ALL clear
+   it as before. **Why:** the viewer's user is not the control user; their
+   STOP_ALL stopped the bed, but it is not the control user lifting the finger,
+   and clearing the latch let a still-repeating control app restart the bed.
+   A STOP_ALL from the control connection still clears the latch, and a
+   viewer's STOP_ALL with no latch set is unchanged.
+2. **Any `any_collision` refusal also latches (f.2).** The refusal in
+   `process_pending_command()` sets `app_collision_latched` (`:813`) before
+   `note_app_commanding()`. **Why:** with AF, a new press made inside the
+   collision sequence (released and pressed again within ~1.5 s), or an app
+   press during a physical-controller collision, was refused only until the
+   sequence ended; an app that kept repeating it then restarted the bed. Now
+   every app hold that meets a collision must be released before a new start
+   is accepted. The "latch cleared" bench print now covers these refusals too
+   (AF f.5 resolved).
+3. **The trip calls `note_app_commanding()` (f.7).** In the trip branch of
+   `service_move_watchdog()` (`:629`), right after the latch is set. Still not
+   `note_app_stopped_commanding()`. **Why:** AF b.11.3 - a trip in the few ms
+   between the silence clear and the watchdog expiry set the latch with
+   `app_commanding` already false, so silence could never clear it.
+   `note_app_commanding()` (`:565-570`) sets only `app_commanding` and
+   `app_command_deadline_tick`, which only `service_app_commanding()` reads;
+   it calls nothing in bed_control. It "refreshes" the app's half of the #36
+   latch only in the sense that it postpones the silence-path
+   `bed_note_source_released(BED_SOURCE_APP)` to 300 ms after the call; it
+   does not set bed_control's `source_commanding[BED_SOURCE_APP]`.
+
+**What remains, from code (ASSERTED):** a STOP_ALL from the control phone
+(e.g. a second finger) after the collision sequence, while the first finger is
+still on the held button, clears the latch and the next repeat starts the move
+(with supervision running) - the same as that STOP_ALL during any ordinary hold.
+Inside the sequence the next repeat is refused and re-latches (change 2).
+After a viewer's STOP_ALL the bed is idle, so a control-phone drop no longer
+goes through the disconnect stop (`bed_active_source()` is NONE, `:1011`); the
+latch then clears by silence, as AF already recorded for a drop after the
+sequence.
+
+**Status:** builds clean (no warnings) with `BLE_DEBUG_WATCHDOG_LOG` 1 and 0;
+host test unchanged and passes. **Not run on the bench.** REGRESSION 5.A3-5.A6.
diff --git a/docs/PRE_SHIP.md b/docs/PRE_SHIP.md
index dcb0068..a6e589e 100644
--- a/docs/PRE_SHIP.md
+++ b/docs/PRE_SHIP.md
@@ -47,6 +47,9 @@ File:line references are to the firmware at `6796e6b` unless marked.
 | F23 | Auto-level uses the raise margin for the lowering motors | `is_raising` is forced true for auto-level (`bed_actions.c:170`; `auto_level.c:771`), so the two lowering motors are judged against the raise margin | Claude decides, Claude Code changes | Decision (keep with a recorded reason, or per-direction margins in auto-level); then REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 5; reviewer ruling, Prompt AE part E |
 | F24 | Unconditional console prints to review before release | With every flag at 0 the firmware still prints `BLE SM: configure/...` (`ble_bed_service.c:1089, 1101, 1109`), `connection_parameters` (`:1270`), `Max Current for Motor` (`motor_current_functions.c:198`), `Once`, and the auto-level step lines | Claude decides, Claude Code changes | List each print with keep / flag / remove; change; check a release-build log | Prompt AC handoff QUESTIONS 12; reviewer ruling, Prompt AE part E |
 | F25 | Hold 9 (2026-11-01 bench): the HEAD pair apparently did not run during PITCH levelling | `prompt_aa_bench.log` lines 201-204 show outputs 0 and 3 at 1 and 12 mA while FOOT ran; cause not established (an inferred limit refusing the pair, `dual_motor_control.c:353, 361`, is one candidate) | Andrew + Claude | A bench look with the limit prints enabled: repeat PITCH levelling from a HEAD-high start and record which pairs run and any limit lines | Prompt AC handoff B1(d), QUESTIONS 6; reviewer ruling, Prompt AE part E |
+| F26 | The runaway fail-safe cuts the collision reverse to ~150-200 ms after an app collision, and after a physical collision once the button is released | During the reverse both app motion flags are false and no switch is down, so `motion_failsafe_tick()` de-energises it after 3 samples of 50 ms (`switch_input.c:804-896`) and prints `FAILSAFE: ...`; the reverse is meant to relieve pressure on the obstruction (ASSERTED from code, not seen) | Andrew + Claude | First: observe the `FAILSAFE` line and the length of the reverse on the bench (REGRESSION 5.A1); then a design ruling (accept, or e.g. the fail-safe stands down while `any_collision` is set) | Prompt AF handoff b.11.1, QUESTIONS f.3; Prompt AG C4 |
+| F27 | A BED LIGHTS press stops an app move but does not latch the #36 inhibit | Lights are not motion and never latch (`bed_control.c:153-164`), so the app's next repeat can restart the move briefly while BED LIGHTS is held | Claude | First: the bench record REGRESSION 8.d; then a ruling | Prompt AE handoff QUESTIONS 6; Prompt AG C4 |
+| F28 | A collision flagged during a BED LIGHTS hold toggles `bedlights_on` at the release without writing the output | Not expected to happen (no motor runs during a BED LIGHTS hold), but the flag and the amber/lighting output would then disagree | Claude | Review before release | Prompt AE handoff QUESTIONS 8; Prompt AG C4 |
 
 ## App
 
diff --git a/docs/REGRESSION.md b/docs/REGRESSION.md
index 58ff157..ce8de8a 100644
--- a/docs/REGRESSION.md
+++ b/docs/REGRESSION.md
@@ -216,26 +216,48 @@ must do the same in both cases.
       cleared (stop), K repeats refused`. The new press starts a fresh
       auto-level run (`Step 1` prints).
 - [ ] 5.A3 Release and re-press inside the collision sequence (B3). As 5.A1,
-      but release ~0.5 s after the trip and press the **same** button again at
-      once, and hold. Observe: `BLE: collision latch cleared (stop), K repeats
-      refused` at the release; **nothing moves toward the obstruction while
-      the LEDs blink** (the reverse at blink 2 may still run); at the end of
-      the sequence (~1.5 s after the trip) an app that is still repeating
-      **starts the move again** - expected under #48 (the re-press did not
-      set the latch), with supervision running, so it trips again at the
-      obstruction. Record the time from the end of the blink to the move.
-- [ ] 5.A4 STOP_ALL during the latch (B5). As 5.A1, but while still holding
-      after the trip press the app's STOP (STOP_ALL) - once inside the blink
-      sequence, once after it. Observe: `BLE: collision latch cleared
-      (stop_all), K repeats refused`; inside the sequence nothing moves
-      toward the obstruction until it ends; after it, an app still repeating
-      the held move **starts the move again** on its next repeat (as STOP_ALL
-      during any ordinary hold). App: 0x00 for the STOP_ALL.
+      but release ~0.5 s after the trip, press the **same** button again at
+      once, and keep holding ~2 s past the end of the blink. Observe: `BLE:
+      collision latch cleared (stop), K repeats refused` at the release (K
+      about 4-5); **nothing moves toward the obstruction while the LEDs
+      blink** (the reverse at blink 2 may still run, with the `FAILSAFE`
+      line); **after the sequence ends the held re-press still starts
+      nothing** - its repeats keep getting 0x06 (the refusal latched, DECISIONS
+      #48 as amended by Prompt AG). At the second release: `BLE: collision
+      latch cleared (stop), K repeats refused`, K about 10 per second of the
+      re-press. A fresh press after that moves normally.
+- [ ] 5.A4 STOP_ALL from the **control** phone during the latch (B5). As 5.A1,
+      but while still holding after the trip press the app's STOP (STOP_ALL)
+      - once inside the blink sequence (~0.5 s), once after it (~2 s).
+      App: 0x00 for the STOP_ALL. **Inside the sequence:** `BLE: collision
+      latch cleared (stop_all), K repeats refused` at the STOP_ALL; the held
+      button's next repeat is refused and re-latches, so **nothing moves,
+      during or after the sequence**, until the held button is released
+      (`BLE: collision latch cleared (stop), K repeats refused` then). **After
+      the sequence:** `cleared (stop_all)` at the STOP_ALL, and an app still
+      repeating the held move **starts the move again** on its next repeat
+      (as a STOP_ALL during any ordinary hold; supervision is running).
 - [ ] 5.A5 Physical-controller collision unchanged: raise into the obstruction
       from the switch panel with the app connected and idle. No `BLE:
       COLLISION_STOP` line, no Command Result; Bed Status bit 0 set for
-      ~1.5 s. An app MOVE press made and released during the blink gets 0x06
-      and starts nothing.
+      ~1.5 s. Then repeat with an app MOVE press made during the blink and
+      held ~2 s past its end: 0x06 for every repeat, **during and after the
+      sequence**, nothing starts from the app; on its release `BLE:
+      collision latch cleared (stop), K repeats refused`. A fresh app press
+      after that moves normally. (If the panel is moving the bed when the app
+      button is released, the app's MOVE_STOP stops it - #12, unchanged.)
+- [ ] 5.A6 STOP_ALL from the **viewer** phone during the latch (Prompt AG).
+      Second phone connected as VIEWER. As 5.A1 (control phone holding the
+      move into the obstruction); while the control finger stays down, press
+      STOP (STOP_ALL) on the viewer - once inside the blink sequence, once
+      after it. Observe: viewer gets 0x00; **no `latch cleared` line at the
+      viewer's STOP_ALL**; the control phone's held repeats keep getting
+      0x06 and **nothing restarts, during or after the sequence**. On the
+      control release: `BLE: collision latch cleared (stop), K repeats
+      refused`, K counting every refusal since the trip. Optional: after the
+      viewer's STOP_ALL, turn off the control phone's Bluetooth while its
+      finger is still down: expect `BLE: collision latch cleared (silence)`
+      ~300 ms after its last repeat (not `disconnect`), and nothing moves.
 
 ## 6. Auto-level
 
```

## d. Raw output of commands run

### d.1 How this log was produced

From the third shell command on, every repo command was run through a small
logging wrapper in the session scratchpad (`$S/r.sh`, below), which ran the
command in the repo root and appended the command and its full output
(stdout and stderr, then `[exit N]`) to `$S/cmdlog.txt`. d.2 is generated
from that log, numbered as the wrapper numbered it. `[exit N]` is the exit
status of the **last** command in each line, so where a line chains a build
and a `size`, the build's own status is the `exit N` line printed by the build
wrapper (d.3).

**Prompt AG D3 - no copies of other files.** Where a command printed the
contents of a file (`sed -n` ranges, `head`, `grep -A` context), that part of
the output is replaced by a marker
`[<file> lines <range> - file content, not reproduced (Prompt AG D3)]`.
Everything else - grep line lists, git, builds, sizes, test results, dates -
is shown as printed. The `git diff` in #14 is shown, because it is this
session's own change (it equals c.1). No secrets appear in any output;
nothing is redacted.

Paths: `$R` = the repo root
`C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller`; `$S` =
the session scratchpad
`C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad`
(the logged command lines show it expanded).

Edits made with the Edit/Write tools are not commands; their result is the
diff in section c. Read-tool reads (not shell commands), in order:
`docs/MIGRATION_STATUS.md` (whole); the AF handoff lines 1-640, 1118-1157
and 2195-end; `ble_bed_service.c` lines 160-359, 540-939 and 975-1134 at
`b1991ed`; `bed_control.c` lines 1-70 and 70-177. Their content is not
reproduced.

### d.0 Commands run before the logging wrapper, and outside it

**P1.** The prompt's text, printed first, is not repeated: it is the
committed `docs/prompts/PROMPT_AG_collision-latch-followups.md`.

```
$ cd $R && cat docs/prompts/PROMPT_AG_collision-latch-followups.md && date && git log --oneline -8 && git status
(the prompt text, then)
Mon Nov  2 09:43:00 PST 2026
b1991ed Handoff 2026-11-02 02: Prompt AF (collision latch on the app path, COLLISION_STOP 0x06)
e646c3b Collision during an app hold latches the app path: COLLISION_STOP 0x06 (DECISIONS #48)
4d1fef3 Handoff 2026-11-02 01: Prompt AE (LEVELED bit, amber after STOP_ALL, BED LIGHTS on release, PRE_SHIP rows; Part A stopped)
e3cc865 PRE_SHIP: rows F20-F25 and F17 extended (reviewer rulings on Prompt AC)
3fb1a0c BED LIGHTS toggles the lights on the release of a short press (DECISIONS #47)
f3a762e Restore the amber indicator after app_stop_all() when the lights are on
ef36909 Bed Status bit 4 = LEVELED; "Bed is level" prints on success (DECISIONS #46)
01315cd Handoff 2026-11-01 02: Prompt AC (AA bench record, PRE_SHIP.md, feature groundwork report)
On branch main
Your branch is up to date with 'origin/main'.

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/prompts/PROMPT_AG_collision-latch-followups.md

no changes added to commit (use "git add" and/or "git commit -a")
```

**P2.** (`git fetch` printed nothing; the `cat ... | head -0` printed nothing.)

```
$ cd $R && git fetch origin 2>&1; git rev-parse HEAD origin/main HEAD^; git show --stat --oneline HEAD | head; cat MIGRATION_STATUS.md 2>/dev/null | head -0; wc -l docs/handoffs/fw_HANDOFF_2026-11-02_02_collision-latch-app-hold.md ble_bed_service.c
b1991ed9da65e96eaddafb1f49bed870ac5c8bd2
b1991ed9da65e96eaddafb1f49bed870ac5c8bd2
e646c3b0228a37840380f18b79de11f7128d2e9e
b1991ed Handoff 2026-11-02 02: Prompt AF (collision latch on the app path, COLLISION_STOP 0x06)
 docs/SESSION_LOG.md                                |   37 +
 ...NDOFF_2026-11-02_02_collision-latch-app-hold.md | 2358 ++++++++++++++++++++
 2 files changed, 2395 insertions(+)
  2358 docs/handoffs/fw_HANDOFF_2026-11-02_02_collision-latch-app-hold.md
  1467 ble_bed_service.c
  3825 total
```

**P3.** `grep -n "^#" docs/handoffs/fw_HANDOFF_2026-11-02_02_collision-latch-app-hold.md`
- the AF handoff's heading lines with line numbers, used to find sections
b.3/b.6/b.11/f. [file content, not reproduced (Prompt AG D3)]

**P4.** Created `$S/r.sh` with a heredoc (`mkdir -p "$S"; cat > "$S/r.sh" <<'EOF' ... EOF; chmod +x "$S/r.sh"; echo ok`). Output:

```
ok
```

The wrapper as created:

```sh
#!/bin/sh
# Session command logger: runs "$*" in the repo root, prints and appends command + output to cmdlog.txt
S="C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad"
cd "C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller" || exit 99
n=$(( $(cat "$S/n.txt" 2>/dev/null || echo 0) + 1 )); echo $n > "$S/n.txt"
out=$(sh -c "$*" 2>&1); rc=$?
{ echo "=== #$n"; echo "\$ $*"; echo "$out"; echo "[exit $rc]"; } >> "$S/cmdlog.txt"
echo "=== #$n"; echo "$out"; echo "[exit $rc]"
```

(`$(...)` strips trailing blank lines from each output; nothing else is
changed.)

**P5.** Created `$S/build.sh` (text in d.3) with a heredoc and printed it
(`cat "$S/build.sh"`, output = the script in d.3), in the same shell line as
d.2 #8.

**P6.** Located the size tool, then #9 in the same line:

```
$ SZ=$(ls -d ~/.silabs/slt/installs/conan/p/*/p/bin/arm-none-eabi-size.exe 2>/dev/null | head -1); echo "SZ=$SZ"
SZ=/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe
```

**P7.** Appended the DECISIONS #48 amendment with a heredoc
(`cat >> docs/DECISIONS.md <<'EOF' ... EOF; tail -5 docs/DECISIONS.md`). The
output was the last five lines of the appended text, which are the last five
lines of the DECISIONS hunk in c.2. [not repeated]

**P8.** Created `$S/crcheck.sh` (a heredoc, no output) in the same line as
#29:

```bash
#!/bin/bash
# Count lines containing a carriage return in each file given.
for f in "$@"; do printf '%s CR-lines=%s total-lines=%s\n' "$f" "$(tr -cd '\r' < "$f" | wc -c)" "$(wc -l < "$f")"; done
```

**P9.** In the same line as #31, after it: saved the committed diffs for
section c.

```
$ cd $R && git show HEAD -- docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/PRE_SHIP.md docs/REGRESSION.md > "$S/docs_diff.txt" && git show HEAD -- ble_bed_service.c > "$S/src_diff.txt" && wc -l "$S/docs_diff.txt" "$S/src_diff.txt"
  214 C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/docs_diff.txt
  100 C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/src_diff.txt
  314 total
```

**P10.** `tail -40 docs/SESSION_LOG.md; date` - the last SESSION_LOG entry (the
AF one), to match its format [file content, not reproduced (Prompt AG D3)],
then:

```
Mon Nov  2 09:50:56 PST 2026
```

**P11.** Appended this session's SESSION_LOG entry with a heredoc
(`cat >> docs/SESSION_LOG.md <<'EOF' ... EOF; echo done`). Output:

```
done
```

**P12.** Before naming this file:

```
$ date; ls $R/docs/handoffs/ | grep 2026-11-02; wc -l "$S/cmdlog.txt"; grep -n '^=== #' "$S/cmdlog.txt"
Mon Nov  2 09:51:37 PST 2026
fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md
fw_HANDOFF_2026-11-02_02_collision-latch-app-hold.md
1188 C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/cmdlog.txt
(then the line numbers of the 31 "=== #n" headers in the log - session tooling, not reproduced)
```

So this file is `_03_` (01 and 02 exist for this date).

**P13.** Session tooling that read the scratchpad's own `cmdlog.txt` (`grep -n`
and `sed -n` over line ranges) to find where file-content output started and
ended inside each logged command, and `$S/gen_d.sh` (which builds d.2 from
the log with the markers above), plus `sed -i` on the scratchpad draft of
this file to correct three line numbers. Their output was excerpts of
`cmdlog.txt` itself (everything in it that is shown is shown in d.2) and line
counts of the generated text. Not reproduced.

### d.2 Session command log (the wrapper, numbered)

**#1.**

```
$ grep -n 'note_app_stopped_commanding\|note_app_commanding\|app_collision_latched\|stop_all_conn\|control_connection ==\|== control_connection\|!= control_connection' ble_bed_service.c
236:static volatile uint8_t stop_all_conn = SLOT_EMPTY;
286:// note_app_stopped_commanding() and printed by ble_bed_service_tick() once
338:// note_app_stopped_commanding(). Without it the held button's next repeat
341:static bool app_collision_latched = false;
438:  if (control_connection == SLOT_EMPTY) {
441:  } else if (viewer_connection == SLOT_EMPTY && connection != control_connection) {
562:static void note_app_commanding(void)
572:static void note_app_stopped_commanding(const char *reason)
577:  if (app_collision_latched) {
586:  app_collision_latched = false;
599:    note_app_stopped_commanding("silence");
624:    app_collision_latched = true;
665:  note_app_stopped_commanding("watchdog");
710:    uint8_t conn = stop_all_conn;
716:    note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
773:  if (conn != control_connection) {
791:  // note_app_commanding() as in the #36 refusal: a refused repeat still means
795:      && (app_collision_latched || any_collision)) {
796:    note_app_commanding();
810:  // Note note_app_commanding() BEFORE the refusal. A refused repeat still
818:    note_app_commanding();
833:      note_app_commanding();
871:      note_app_stopped_commanding("stop");
884:      note_app_commanding();
910:      note_app_stopped_commanding("stop");
966:    if (connection == control_connection) {
984:  if (connection == control_connection) {
1095:    note_app_stopped_commanding("disconnect"); // the app is gone - release its half of the latch
1367:        uint8_t role_value = (req->connection == control_connection) ? ROLE_CONTROL : ROLE_VIEWER;
1403:        stop_all_conn = av->connection;
1433:        if (cs->connection == control_connection) {
[exit 0]
```

**#2.**

```
$ grep -n 'bed_note_source_released\|conflict_inhibit\|released\[\|holding\[\|bool bed_conflict_inhibited' bed_control.c bed_control.h
bed_control.c:29: * ble_bed_service.c, which calls bed_note_source_released() for the timeout.
bed_control.c:31:static bool conflict_inhibit = false;
bed_control.c:44:  if (conflict_inhibit
bed_control.c:47:    conflict_inhibit = false;
bed_control.c:51:void bed_note_source_released(bed_source_t src)
bed_control.c:56:bool bed_conflict_inhibited(void)
bed_control.c:58:  return conflict_inhibit;
bed_control.c:101:  if (conflict_inhibit) {
bed_control.c:105:    conflict_inhibit = true;
bed_control.c:129:  if (conflict_inhibit) {
bed_control.c:133:    conflict_inhibit = true;
bed_control.h:75:bool bed_conflict_inhibited(void);
bed_control.h:81:void bed_note_source_released(bed_source_t src);
[exit 0]
```

**#3.**

```
$ grep -n 'app_commanding\b\|app_command_deadline_tick\|service_app_commanding\|any_collision' ble_bed_service.c
41:#include "motor_current_functions.h" // any_collision - cancels the move watchdog
81:  if (in->any_collision)    flags |= 0x01;
348:static bool     app_commanding = false;
349:static uint32_t app_command_deadline_tick = 0;
561:// AUTO_LEVEL_START, accepted or refused (#36 - see app_commanding).
562:static void note_app_commanding(void)
564:  app_commanding = true;
565:  app_command_deadline_tick = sl_sleeptimer_get_tick_count()
574:  app_commanding = false;
592:static void service_app_commanding(void)
594:  if (!app_commanding) {
598:  if ((int32_t)(sl_sleeptimer_get_tick_count() - app_command_deadline_tick) >= 0) {
610:  if (any_collision) {
789:  // (switch_input_tick() requires !any_collision).
791:  // note_app_commanding() as in the #36 refusal: a refused repeat still means
795:      && (app_collision_latched || any_collision)) {
796:    note_app_commanding();
810:  // Note note_app_commanding() BEFORE the refusal. A refused repeat still
818:    note_app_commanding();
833:      note_app_commanding();
884:      note_app_commanding();
1105:  service_app_commanding();
[exit 0]
```

**#4.**

```
$ sed -n 575,625p app.c; echo ----; grep -n 'printf' switch_input.c | sed -n 1,80p; echo ----; grep -n 'BED_BENCH_PROVOKE_RUNAWAY\|BLE_BENCH_WIPE_BONDS' switch_input.c ble_bed_service.c | head; grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h
[app.c lines 575-625 at b1991ed - file content, not reproduced (Prompt AG D3)]
----
245:#define SW_DIAG(...) printf(__VA_ARGS__)
509:  printf("PROVOKE: energised %s with nothing commanding - fail-safe should stop this\r\n",
718:                printf("Unrecognized switch mask: 0x%04X\r\n", app_ui_interrupt);
885:    printf("FAILSAFE: motors energised with nothing commanding - stopped\r\n");
893:      printf("PROVOKE: fail-safe caught it after %lu ms\r\n", (unsigned long)elapsed_ms);
959:          printf("Double Press Error\r\n");
960:          printf("  down mask 0x%04X, %u switches down\r\n",
----
switch_input.c:68: * Same convention as BLE_BENCH_WIPE_BONDS in ble_bed_service.c: a hardcoded,
switch_input.c:98:#define BED_BENCH_PROVOKE_RUNAWAY 0
switch_input.c:471:#if BED_BENCH_PROVOKE_RUNAWAY
switch_input.c:512:#endif // BED_BENCH_PROVOKE_RUNAWAY
switch_input.c:635:#if BED_BENCH_PROVOKE_RUNAWAY
switch_input.c:707:#if BED_BENCH_PROVOKE_RUNAWAY
switch_input.c:806:#if BED_BENCH_PROVOKE_RUNAWAY
switch_input.c:887:#if BED_BENCH_PROVOKE_RUNAWAY
ble_bed_service.c:70:#define BLE_BENCH_WIPE_BONDS 0
ble_bed_service.c:1191:#if BLE_BENCH_WIPE_BONDS
36:#define BLE_DEBUG_WATCHDOG_LOG 1
[exit 0]
```

**#5.**

```
$ sed -n 238,248p switch_input.c; grep -n 'SW_DIAG(' switch_input.c | head -20; echo ----; sed -n 812,845p switch_input.c; echo ----; sed -n 928,942p switch_input.c
[switch_input.c lines 238-248 - file content, not reproduced (Prompt AG D3)]
245:#define SW_DIAG(...) printf(__VA_ARGS__)
247:#define SW_DIAG(...) ((void)0)
364:      SW_DIAG("SW: aux board read failed\r\n");
372:      SW_DIAG("SW: switch board read failed\r\n");
390:      SW_DIAG("SW: %u failed reads - treating all switches as released\r\n",
423:          SW_DIAG("SW: 0x%04X %s (bounces %u)\r\n", (unsigned)bit,
1020:      SW_DIAG("SW: %u switches down - presses ignored\r\n",
----
[switch_input.c lines 812-845 - file content, not reproduced (Prompt AG D3)]
----
[switch_input.c lines 928-942 - file content, not reproduced (Prompt AG D3)]
[exit 0]
```

**#6.**

```
$ grep -rn 'define SWITCH_SAMPLER_DIAG\|define BED_CAPTURE_MOTOR_CURRENTS' --include=*.h --include=*.c . | grep -v cmake_gcc
./debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
./switch_input.c:57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
[exit 0]
```

**#7.**

```
$ sed -n 410,430p switch_input.c
[switch_input.c lines 410-430 - file content, not reproduced (Prompt AG D3)]
[exit 0]
```

**#8.**

```
$ ls -l cmake_gcc/build/base/*.out 2>/dev/null; find cmake_gcc/build -name '*.out' -newer docs/SESSION_LOG.md 2>/dev/null | head; find cmake_gcc/build -name 'tilt_bed_controller.out' | head -3
-rw-r--r-- 1 adunc 197610 2089248 Nov  2 09:00 cmake_gcc/build/base/tilt_bed_controller.out
cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]
```

**#9.**

```
$ '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
   text	   data	    bss	    dec	    hex	filename
 245588	    784	  72476	 318848	  4dd80	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]
```

**#10.**

```
$ sh C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/build.sh; '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 245620	    784	  72476	 318880	  4dda0	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]
```

**#11.**

```
$ sed -i 's/^#define BLE_DEBUG_WATCHDOG_LOG 1/#define BLE_DEBUG_WATCHDOG_LOG 0/' ble_bed_service.h && grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h && sh C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/build.sh; '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out
36:#define BLE_DEBUG_WATCHDOG_LOG 0
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 243924	    780	  72480	 317184	  4d700	cmake_gcc/build/base/tilt_bed_controller.out
[exit 0]
```

**#12.**

```
$ sed -i 's/^#define BLE_DEBUG_WATCHDOG_LOG 0/#define BLE_DEBUG_WATCHDOG_LOG 1/' ble_bed_service.h && grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h && sh C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/build.sh; '/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe' cmake_gcc/build/base/tilt_bed_controller.out; git status --short; file ble_bed_service.h ble_bed_service.c
36:#define BLE_DEBUG_WATCHDOG_LOG 1
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 245620	    784	  72476	 318880	  4dda0	cmake_gcc/build/base/tilt_bed_controller.out
 M .projectlinkstore
 M ble_bed_service.c
?? docs/prompts/PROMPT_AG_collision-latch-followups.md
ble_bed_service.h: C source, ASCII text
ble_bed_service.c: C source, ASCII text
[exit 0]
```

**#13.**

```
$ head -15 test/bed_control_test.c; which gcc; cd test && gcc -std=c99 -Wall -I.. -o C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/bed_control_test.exe bed_control_test.c ../bed_control.c && C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/bed_control_test.exe | tail -8
[test/bed_control_test.c lines 1-15 (head -15) - file content, not reproduced (Prompt AG D3)]
/c/MinGW/bin/gcc
PASS: scenario 5f: app still refused while it alone is still holding
PASS: scenario 5g: latch clears once BOTH sources have released
PASS: scenario 5h: a fresh press after both released starts normally
PASS: scenario 6a: inhibited
PASS: scenario 6b: bed_move_stop still works while inhibited
PASS: scenario 6c: lights are not motion and are not inhibited

0 check(s) failed
[exit 0]
```

**#14.**

```
$ git diff ble_bed_service.c
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
diff --git a/ble_bed_service.c b/ble_bed_service.c
index fa49ce2..f3f3685 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -331,13 +331,16 @@ static bed_direction_t app_move_dir    = BED_DIR_UP;
 static bool app_auto_level_active = false;
 
 // Collision latch (DECISIONS #48). Set when a collision trips during an app
-// hold, while the user is still holding the app button. Every MOVE_START and
-// AUTO_LEVEL_START is then answered COLLISION_STOP and starts nothing, until
-// the app releases - an explicit stop, STOP_ALL, the control-phone
-// disconnect, or MOVE_WATCHDOG_MS of silence, i.e. every caller of
-// note_app_stopped_commanding(). Without it the held button's next repeat
-// re-energised the same pair, into the obstruction, during the collision
-// sequence.
+// hold, while the user is still holding the app button, and when a start is
+// refused because a collision sequence is running (amended by Prompt AG).
+// Every MOVE_START and AUTO_LEVEL_START is then answered COLLISION_STOP and
+// starts nothing, until the app releases - an explicit stop, the control
+// phone's STOP_ALL, the control-phone disconnect, or MOVE_WATCHDOG_MS of
+// silence, i.e. every caller of note_app_stopped_commanding(). A viewer's
+// STOP_ALL does not clear it. Always set together with note_app_commanding(),
+// so the silence timeout is running whenever it is set. Without it the held
+// button's next repeat re-energised the same pair, into the obstruction,
+// during the collision sequence.
 static bool app_collision_latched = false;
 
 // Whether the app is still asking for motion, for the two-source inhibit
@@ -617,11 +620,13 @@ static void service_move_watchdog(void)
     // The user is still holding the app button, so latch (#48): the repeats
     // that follow are refused and start nothing. NOT note_app_stopped_
     // commanding() - the app has not released, and its half of the #36
-    // latch stays as it is.
+    // latch stays as it is. note_app_commanding() restarts the silence
+    // timeout, so the latch clears by silence even if no repeat follows.
     move_watchdog_cancel();
     app_move_active = false;
     app_auto_level_active = false;
     app_collision_latched = true;
+    note_app_commanding();
     send_command_result(control_connection, last_move_seq, CMD_RESULT_COLLISION_STOP);
 #if BLE_DEBUG_WATCHDOG_LOG
     // After the motors are off and the result is sent.
@@ -713,7 +718,17 @@ static void process_pending_command(void)
     move_watchdog_cancel();
     app_move_active = false;
     app_auto_level_active = false;
-    note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
+    if (conn == control_connection || !app_collision_latched) {
+      note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
+    } else {
+      // A viewer's STOP_ALL during the collision latch (#48, amended by
+      // Prompt AG). It stops the bed and releases the app's half of the #36
+      // latch as before, but it is not the control user's release, so the
+      // collision latch stays. app_commanding is left set (the latch is only
+      // ever set with it set), so the silence timeout keeps running from the
+      // control app's last repeat.
+      bed_note_source_released(BED_SOURCE_APP);
+    }
     app_stop_all();                // STOP_ALL keeps its full everything-off behaviour
 
     // Accepted from ANY role - control or viewer. DECISIONS #12's
@@ -786,13 +801,16 @@ static void process_pending_command(void)
   // also while ANY collision sequence runs - a quick release and press inside
   // the sequence, or a press during a physical-controller collision, would
   // otherwise re-energise a pair while current supervision is off
-  // (switch_input_tick() requires !any_collision).
+  // (switch_input_tick() requires !any_collision). A refusal also latches
+  // (Prompt AG), so a hold that met a collision must be released before a
+  // new start is accepted, even once the sequence has ended.
   //
   // note_app_commanding() as in the #36 refusal: a refused repeat still means
   // the user is holding, so the silence timeout must not release the latch.
   // No bed_control call, no last_move_seq update, no watchdog arm.
   if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
       && (app_collision_latched || any_collision)) {
+    app_collision_latched = true;
     note_app_commanding();
     send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);
 #if BLE_DEBUG_WATCHDOG_LOG
[exit 0]
```

**#15.**

```
$ grep -n 'note_app_stopped_commanding(\|note_app_commanding()\|app_collision_latched' ble_bed_service.c
286:// note_app_stopped_commanding() and printed by ble_bed_service_tick() once
339:// silence, i.e. every caller of note_app_stopped_commanding(). A viewer's
340:// STOP_ALL does not clear it. Always set together with note_app_commanding(),
344:static bool app_collision_latched = false;
575:static void note_app_stopped_commanding(const char *reason)
580:  if (app_collision_latched) {
589:  app_collision_latched = false;
602:    note_app_stopped_commanding("silence");
623:    // latch stays as it is. note_app_commanding() restarts the silence
628:    app_collision_latched = true;
629:    note_app_commanding();
670:  note_app_stopped_commanding("watchdog");
721:    if (conn == control_connection || !app_collision_latched) {
722:      note_app_stopped_commanding("stop_all"); // never inhibited, and releases the latch
808:  // note_app_commanding() as in the #36 refusal: a refused repeat still means
812:      && (app_collision_latched || any_collision)) {
813:    app_collision_latched = true;
814:    note_app_commanding();
828:  // Note note_app_commanding() BEFORE the refusal. A refused repeat still
836:    note_app_commanding();
851:      note_app_commanding();
889:      note_app_stopped_commanding("stop");
902:      note_app_commanding();
928:      note_app_stopped_commanding("stop");
1113:    note_app_stopped_commanding("disconnect"); // the app is gone - release its half of the latch
[exit 0]
```

**#16.**

```
$ grep -n '0x06\|COLLISION_STOP' docs/BLE_GATT_DESIGN.md; grep -n '^## \|^### 48\|^## 48\|#48' docs/DECISIONS.md | tail -12; grep -n '5\.A\|^## 5' docs/REGRESSION.md; grep -n '^| *F[0-9]' docs/PRE_SHIP.md | tail -8
102:| 0x06 | STOP_ALL | — | — | Explicit full stop (maps to the `app_stop_all()` path: motors + auto-level + LEDs). The app's big red button. Always accepted from **any** role, control or viewer — this is the BLE embodiment of the stop-from-anyone rule (#12). |
370:| 0x06 | COLLISION_STOP | Collision: the app ends its hold and shows the collision screen (see below) |
372:**COLLISION_STOP (0x06), DECISIONS #48.** Sent in two cases:
395:App behaviour: on 0x06, end the hold (stop repeating; a MOVE_STOP /
406:unused and the byte layout is unchanged; a client that does not know 0x06
544:   COLLISION_STOP (0x06, §7); the held button starts nothing until released
1443:## 38. The double-press misfire, the state it desynced, and the motion fail-safe
1573:## 39. Proving the motion fail-safe: a bench-only runaway provocation
1652:## 40. The fail-safe reads motor hardware; double press = two switches down; out-of-band reads must not swallow events
1730:## 41. One switch reader
1843:## 42. Pairing window: new bonds only while settings mode is active (implements #19)
1937:## 43. Bond store full: overwrite the least recently used bond; the firmware disconnects a phone whose pairing it refused
2061:## 44. STOP in settings mode cancels any hold in progress and stays in settings mode
2119:## 45. During an app hold the CPU stays in EM0
2256:## 46. Bed Status flag bit 4 = LEVELED; "Bed is level" prints on every successful auto-level end
2329:## 47. BED LIGHTS toggles the lights on the release of a short press
2422:## 48. A collision during an app hold latches the app path: COLLISION_STOP (0x06) until the app releases
2553:items marked #48.
172:## 5. Collision detection
198:- [ ] 5.A1 Collision during an app **MOVE** hold (B1). Raise into the
211:- [ ] 5.A2 Collision during an app **AUTO LEVEL** hold (B2). Obstruct a
218:- [ ] 5.A3 Release and re-press inside the collision sequence (B3). As 5.A1,
227:- [ ] 5.A4 STOP_ALL during the latch (B5). As 5.A1, but while still holding
234:- [ ] 5.A5 Physical-controller collision unchanged: raise into the obstruction
42:| F18 | "Connection stalled" shown when the physical controller takes over from the app | The app's refused repeats do not refresh the watchdog, so ~300 ms later the app gets WATCHDOG_STOP although the link is fine (`ble_bed_service.c:746-750`) | Claude decides; Claude Code (firmware or app) | A distinct result code, or app handling of FORCE_STOPPED then WATCHDOG_STOP; bench check | Prompt AA handoff QUESTIONS 5 |
43:| F19 | `BED_DEBUG_LEVELED_LOG` 1 -> 0 (`bed_actions.c`) | Bench print `LEVELED (Bed Status bit 4) -> 0/1` on every change of bit 4 | Claude Code | Change to 0, build, check the `LEVELED` lines are gone | Prompt AE part F; DECISIONS #46 |
44:| F20 | Collision detection is blind for the first ~450 supervision passes of every move | 100 passes ignored + 350 calibrating (`motor_current_functions.c:28-29`) before any collision or max-load trip can fire; roughly 0.7-1.0 s at auto-level's loop rate, never measured for manual moves | Claude (design), Claude Code | Design with F15 (pass counts -> ms, DECISIONS #2): state the blind time in ms and what covers it; change; REGRESSION §5 with a loop-rate measurement | Prompt AC handoff B1(a); reviewer ruling, Prompt AE part E |
45:| F21 | Auto-level's pulsed ("sneak") phase has no current supervision | Once `Latch` prints, `level_axis_pulse()` runs with no collision, max-load or limit inference (`auto_level.c:679-738`, `:770-771`; `switch_input.c:921`) | Claude (design), Claude Code | Design: supervise the pulse phase (or record why it is safe); change; REGRESSION §5 collision during auto-level near level | Prompt AC handoff B1(d), QUESTIONS 3; reviewer ruling, Prompt AE part E |
46:| F22 | The "continuous" re-baseline never reaches the trip test | It writes `dynamic_collision_current_threshold[]`, which nothing reads; the trip uses `max_current_threshold_set[]`; and it counts on motor 3's counter for every motor (`motor_current_functions.c:206-231`) | Claude (design), Claude Code | Decide: make it work (with F15/F20) or remove it; change; REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 4; reviewer ruling, Prompt AE part E |
47:| F23 | Auto-level uses the raise margin for the lowering motors | `is_raising` is forced true for auto-level (`bed_actions.c:170`; `auto_level.c:771`), so the two lowering motors are judged against the raise margin | Claude decides, Claude Code changes | Decision (keep with a recorded reason, or per-direction margins in auto-level); then REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 5; reviewer ruling, Prompt AE part E |
48:| F24 | Unconditional console prints to review before release | With every flag at 0 the firmware still prints `BLE SM: configure/...` (`ble_bed_service.c:1089, 1101, 1109`), `connection_parameters` (`:1270`), `Max Current for Motor` (`motor_current_functions.c:198`), `Once`, and the auto-level step lines | Claude decides, Claude Code changes | List each print with keep / flag / remove; change; check a release-build log | Prompt AC handoff QUESTIONS 12; reviewer ruling, Prompt AE part E |
49:| F25 | Hold 9 (2026-11-01 bench): the HEAD pair apparently did not run during PITCH levelling | `prompt_aa_bench.log` lines 201-204 show outputs 0 and 3 at 1 and 12 mA while FOOT ran; cause not established (an inferred limit refusing the pair, `dual_motor_control.c:353, 361`, is one candidate) | Andrew + Claude | A bench look with the limit prints enabled: repeat PITCH levelling from a HEAD-high start and record which pairs run and any limit lines | Prompt AC handoff B1(d), QUESTIONS 6; reviewer ruling, Prompt AE part E |
[exit 0]
```

**#17.**

```
$ sed -n 362,412p docs/BLE_GATT_DESIGN.md; echo =====; sed -n 538,548p docs/BLE_GATT_DESIGN.md; echo =====; sed -n 1,41p docs/PRE_SHIP.md; echo; sed -n 50,80p docs/PRE_SHIP.md
[docs/BLE_GATT_DESIGN.md lines 362-412 and 538-548, docs/PRE_SHIP.md lines 1-41 and 50-80 - file content, not reproduced (Prompt AG D3)]
[exit 0]
```

**#18.**

```
$ sed -n 2422,2560p docs/DECISIONS.md
[docs/DECISIONS.md lines 2422-2560 (#48 as committed in e646c3b) - file content, not reproduced (Prompt AG D3)]
[exit 0]
```

**#19.**

```
$ sed -n 172,250p docs/REGRESSION.md; wc -l docs/DECISIONS.md; tail -3 docs/DECISIONS.md | cat -A | tail -3
[docs/REGRESSION.md lines 172-250 - file content, not reproduced (Prompt AG D3)]
2553 docs/DECISIONS.md
(`test/bed_control_test.c`) is unchanged and passes; it cannot reach this$
code (it compiles bed_control.c only). **Not run on the bench.** REGRESSION M-BM-'5,$
items marked #48.$
[exit 0]
```

**#20.**

```
$ grep -n '^void app_move_stop\|^void app_stop_movement\|^void app_stop_all\|^bool app_bed_is_idle' bed_actions.c; sed -n "$(grep -n '^void app_move_stop' bed_actions.c | cut -d: -f1),+12p" bed_actions.c; echo ----; sed -n "$(grep -n '^void app_stop_movement' bed_actions.c | cut -d: -f1),+22p" bed_actions.c; echo ----; grep -n 'void app_apply_stop_outputs' -A14 bed_actions.c ble_bed_service.c app.c | head -20
165:void app_move_stop(void)
239:void app_stop_all(void)
368:void app_stop_movement(void)
397:bool app_bed_is_idle(void)
[bed_actions.c from app_move_stop (165) +12 lines, '----', from app_stop_movement (368) +22 lines - file content, not reproduced (Prompt AG D3)]
----
[grep -A14 output: bed_actions.c lines 334-348 (app_apply_stop_outputs) - file content, not reproduced (Prompt AG D3)]
[exit 0]
```

**#21.**

```
$ grep -n 'printf' bed_actions.c; grep -n 'BED_DEBUG_LEVELED_LOG' bed_actions.c | head -3
61:  printf("LEVELED (Bed Status bit 4) -> %u\r\n", leveled ? 1u : 0u);
46:#define BED_DEBUG_LEVELED_LOG 1
60:#if BED_DEBUG_LEVELED_LOG && !BED_CAPTURE_MOTOR_CURRENTS
[exit 0]
```

**#22.**

```
$ sed -n 50,66p bed_actions.c; grep -n 'app_set_leveled(' *.c
[bed_actions.c lines 50-66 - file content, not reproduced (Prompt AG D3)]
app.c:605:        app_set_leveled(false); // the reverse is motion (#46)
app.c:647:          app_set_leveled(true);  // Bed Status bit 4 (#46)
app.c:688:              app_set_leveled(true);  // Bed Status bit 4 (#46)
bed_actions.c:54:void app_set_leveled(bool leveled)
bed_actions.c:143:  app_set_leveled(false); // any move, either source, ALL included (#46)
bed_actions.c:185:  app_set_leveled(false); // a new run, either source (#46)
motor_current_functions.c:402:    app_set_leveled(false);
settings_mode.c:287:        app_set_leveled(false); // Bed Status bit 4 (#46)
[exit 0]
```

**#23.**

```
$ ls docs/handoffs/ | grep 2026-11-02; F=$(ls docs/handoffs/fw_HANDOFF_2026-11-02_01_*); n=$(grep -n '^## f\|QUESTIONS FOR REVIEW' $F | head -3); echo "$n"; grep -n '8\.d' docs/REGRESSION.md | head
fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md
fw_HANDOFF_2026-11-02_02_collision-latch-app-hold.md
1843:## f. QUESTIONS FOR REVIEW
446:- [ ] 8.d App holding a move (or AUTO LEVEL): press and hold BED_LIGHTS -
[exit 0]
```

**#24.**

```
$ sed -n 1843,1930p docs/handoffs/fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md; echo =====; sed -n 440,460p docs/REGRESSION.md
[docs/handoffs/fw_HANDOFF_2026-11-02_01_...md lines 1843-1930 and docs/REGRESSION.md lines 440-460 - file content, not reproduced (Prompt AG D3)]
[exit 0]
```

**#25.**

```
$ grep -n 'static void note_app_commanding\|static void note_app_stopped_commanding\|static void service_move_watchdog\|static void service_app_commanding\|if (any_collision) {\|if (stop_all_pending)\|send_command_result(conn, seq, CMD_RESULT_ACCEPTED);\|CMD_RESULT_COLLISION_STOP);\|bed_conflict_inhibited()\|if (disconnect_stop_pending)\|bed_active_source() == BED_SOURCE_APP\|process_pending_command();\|service_move_watchdog();\|service_app_commanding();' ble_bed_service.c; sed -n 563,571p ble_bed_service.c
565:static void note_app_commanding(void)
575:static void note_app_stopped_commanding(const char *reason)
595:static void service_app_commanding(void)
607:static void service_move_watchdog(void)
613:  if (any_collision) {
630:    send_command_result(control_connection, last_move_seq, CMD_RESULT_COLLISION_STOP);
713:  if (stop_all_pending) {
736:    send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
780:    send_command_result(conn, seq, CMD_RESULT_ACCEPTED);
815:    send_command_result(conn, seq, CMD_RESULT_COLLISION_STOP);
834:  if (bed_conflict_inhibited()
1011:    if (bed_active_source() == BED_SOURCE_APP) {
1108:  if (disconnect_stop_pending) {
1121:  process_pending_command();
1122:  service_move_watchdog();
1123:  service_app_commanding();
[ble_bed_service.c lines 563-571 at the working tree (note_app_commanding) - file content, not reproduced (Prompt AG D3)]
[exit 0]
```

**#26.**

```
$ grep -n 'active_switch *=' dual_motor_control.c app.c bed_actions.c | head -20; grep -n 'app_bed_is_idle' -A4 bed_actions.c | head -8
app.c:143:active_switch_t active_switch = OFF;
app.c:616:      active_switch = OFF;
app.c:626:  if ((auto_level_active) && (!any_collision) && (active_switch == SWITCH_AUTO_LEVEL_VALUE)){
bed_actions.c:73:  active_switch = switch_value;
bed_actions.c:159:      active_switch = up ? SWITCH_UP_ALL_VALUE : SWITCH_DOWN_ALL_VALUE;
bed_actions.c:187:  active_switch = SWITCH_AUTO_LEVEL_VALUE;
bed_actions.c:230:  active_switch = SWITCH_BED_LIGHT_VALUE;
bed_actions.c:266:  active_switch = OFF;
bed_actions.c:354:  active_switch = OFF;
bed_actions.c:394:  active_switch = OFF;
bed_actions.c:399:  return (active_switch == OFF) && (!in_settings_mode);
349:// app_bed_is_idle() is what lets bed_control release the source lock, so
350-// without it a single app command would leave APP holding the lock forever
351-// and the wall switch permanently force-stopped (DECISIONS.md #12).
352-void app_release_active_switch(void)
353-{
--
397:bool app_bed_is_idle(void)
398-{
[exit 0]
```

**#27.**

```
$ grep -rn 'did not set the latch\|from either connection\|does not set the latch\|starts the move again\|starts the bed' docs/*.md | grep -v '^docs/SESSION_LOG.md'; git diff --stat
docs/DECISIONS.md:2476:  `service_move_watchdog()` in the same tick). It does not set the latch.
docs/DECISIONS.md:2544:  ~1.5 s) is refused by `any_collision` but does not set the latch: if the app
docs/REGRESSION.md:238:      repeating the held move **starts the move again** on its next repeat
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/PRE_SHIP.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
 .projectlinkstore       |  2 +-
 ble_bed_service.c       | 38 +++++++++++++++++++++++---------
 docs/BLE_GATT_DESIGN.md | 26 ++++++++++++----------
 docs/DECISIONS.md       | 58 +++++++++++++++++++++++++++++++++++++++++++++++++
 docs/PRE_SHIP.md        |  3 +++
 docs/REGRESSION.md      | 56 ++++++++++++++++++++++++++++++++---------------
 6 files changed, 143 insertions(+), 40 deletions(-)
[exit 0]
```

**#28.**

```
$ for f in ble_bed_service.c docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/PRE_SHIP.md docs/REGRESSION.md docs/prompts/PROMPT_AG_collision-latch-followups.md; do printf '%s CR-lines=%s\n' $f $(grep -c $'\r' $f); done; git config --get core.autocrlf; sed -n 716,737p ble_bed_service.c
ble_bed_service.c CR-lines=1485
docs/BLE_GATT_DESIGN.md CR-lines=549
docs/DECISIONS.md CR-lines=2611
docs/PRE_SHIP.md CR-lines=95
docs/REGRESSION.md CR-lines=505
docs/prompts/PROMPT_AG_collision-latch-followups.md CR-lines=113
true
[ble_bed_service.c lines 716-737 at the working tree (the STOP_ALL branch; same text as c.1) - file content, not reproduced (Prompt AG D3)]
[exit 0]
```

**#29.**

```
$ bash C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/crcheck.sh ble_bed_service.c docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/PRE_SHIP.md docs/REGRESSION.md docs/prompts/PROMPT_AG_collision-latch-followups.md; file ble_bed_service.c docs/*.md docs/prompts/PROMPT_AG_collision-latch-followups.md | grep -v '/handoffs/'
ble_bed_service.c CR-lines=0 total-lines=1485
docs/BLE_GATT_DESIGN.md CR-lines=0 total-lines=549
docs/DECISIONS.md CR-lines=0 total-lines=2611
docs/PRE_SHIP.md CR-lines=0 total-lines=95
docs/REGRESSION.md CR-lines=0 total-lines=505
docs/prompts/PROMPT_AG_collision-latch-followups.md CR-lines=0 total-lines=113
ble_bed_service.c:                                   C source, ASCII text
docs/APP_KICKOFF_PROMPT.md:                          Unicode text, UTF-8 text
docs/APP_SETUP_GUIDE.md:                             Unicode text, UTF-8 text
docs/BLE_GATT_DESIGN.md:                             Unicode text, UTF-8 text, with very long lines (408)
docs/DECISIONS.md:                                   Unicode text, UTF-8 text
docs/HARDWARE_CONFIG.md:                             Unicode text, UTF-8 text
docs/MIGRATION_PORT_PROMPT.md:                       Unicode text, UTF-8 text
docs/MIGRATION_STATUS.md:                            Unicode text, UTF-8 text
docs/PRE_SHIP.md:                                    Unicode text, UTF-8 text, with very long lines (841)
docs/REFACTOR_PLAN.md:                               Unicode text, UTF-8 text, with very long lines (624)
docs/REGRESSION.md:                                  Unicode text, UTF-8 text
docs/REGRESSION_RECORD_D1.md:                        Unicode text, UTF-8 text
docs/REGRESSION_RECORD_PRE_STAGEC.md:                Unicode text, UTF-8 text
docs/SDK_PORTING_GUIDE.md:                           Unicode text, UTF-8 text
docs/SESSION_LOG.md:                                 Unicode text, UTF-8 text, with very long lines (504)
docs/SETPOINTS_INVENTORY.md:                         Unicode text, UTF-8 text, with very long lines (314)
docs/SWITCH_SAMPLER_DESIGN.md:                       ASCII text
docs/prompts/PROMPT_AG_collision-latch-followups.md: ASCII text
[exit 0]
```

**#30.**

```
$ git status && git add ble_bed_service.c docs/BLE_GATT_DESIGN.md docs/DECISIONS.md docs/PRE_SHIP.md docs/REGRESSION.md docs/prompts/PROMPT_AG_collision-latch-followups.md && git commit -q -F C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/msg1.txt && git log --oneline -3 && git show --stat --oneline HEAD && git status
On branch main
Your branch is up to date with 'origin/main'.

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   ble_bed_service.c
	modified:   docs/BLE_GATT_DESIGN.md
	modified:   docs/DECISIONS.md
	modified:   docs/PRE_SHIP.md
	modified:   docs/REGRESSION.md

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	docs/prompts/PROMPT_AG_collision-latch-followups.md

no changes added to commit (use "git add" and/or "git commit -a")
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/BLE_GATT_DESIGN.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/PRE_SHIP.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/REGRESSION.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/prompts/PROMPT_AG_collision-latch-followups.md', LF will be replaced by CRLF the next time Git touches it
3d7d8e6 Collision latch follow-ups: viewer STOP_ALL keeps it, refusals latch, trip arms silence (DECISIONS #48, Prompt AG)
b1991ed Handoff 2026-11-02 02: Prompt AF (collision latch on the app path, COLLISION_STOP 0x06)
e646c3b Collision during an app hold latches the app path: COLLISION_STOP 0x06 (DECISIONS #48)
3d7d8e6 Collision latch follow-ups: viewer STOP_ALL keeps it, refusals latch, trip arms silence (DECISIONS #48, Prompt AG)
 ble_bed_service.c                                  |  38 +++++--
 docs/BLE_GATT_DESIGN.md                            |  26 ++---
 docs/DECISIONS.md                                  |  58 +++++++++++
 docs/PRE_SHIP.md                                   |   3 +
 docs/REGRESSION.md                                 |  56 ++++++----
 .../prompts/PROMPT_AG_collision-latch-followups.md | 113 +++++++++++++++++++++
 6 files changed, 255 insertions(+), 39 deletions(-)
On branch main
Your branch is ahead of 'origin/main' by 1 commit.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
[exit 0]
```

**#31.**

```
$ sh C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/build.sh; grep -n 'define BED_BENCH_PROVOKE_RUNAWAY\|define BLE_BENCH_WIPE_BONDS\|define BLE_DEBUG_WATCHDOG_LOG\|define BED_CAPTURE_MOTOR_CURRENTS' switch_input.c ble_bed_service.c ble_bed_service.h debug_capture.h; git show --stat HEAD -- config autogen | tail -1
ninja: no work to do.
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
switch_input.c:98:#define BED_BENCH_PROVOKE_RUNAWAY 0
ble_bed_service.c:70:#define BLE_BENCH_WIPE_BONDS 0
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
debug_capture.h:21:#define BED_CAPTURE_MOTOR_CURRENTS 0
[exit 0]
```

**#32.**

```
$ date; git status; git log --oneline -3
Mon Nov  2 09:52:37 PST 2026
On branch main
Your branch is ahead of 'origin/main' by 1 commit.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

no changes added to commit (use "git add" and/or "git commit -a")
3d7d8e6 Collision latch follow-ups: viewer STOP_ALL keeps it, refusals latch, trip arms silence (DECISIONS #48, Prompt AG)
b1991ed Handoff 2026-11-02 02: Prompt AF (collision latch on the app path, COLLISION_STOP 0x06)
e646c3b Collision during an app hold latches the app path: COLLISION_STOP 0x06 (DECISIONS #48)
[exit 0]
```

**#33.**

```
$ grep -n 'if (conn != control_connection)\|if (app_in_settings_mode())\|if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)\|^    return;$\|app_commanding = false' ble_bed_service.c | sed -n 1,40p | awk -F: '$1>570 && $1<840'; awk 'NR>=611 && NR<=638 && /^  }|return;/ {print NR": "$0}' ble_bed_service.c; awk 'NR>=239 && NR<=270 && /active_switch = OFF|^}/ {print "bed_actions.c:"NR": "$0}' bed_actions.c
577:  app_commanding = false;
598:    return;
610:    return;
636:    return;
654:    return;
740:    return;
764:    return;
781:    return;
788:  if (conn != control_connection) {
790:    return;
793:  if (app_in_settings_mode()) {
795:    return;
811:  if ((opcode == CMD_OPCODE_MOVE_START || opcode == CMD_OPCODE_AUTO_LEVEL_START)
821:    return;
838:    return;
611:   }
636:     return;
637:   }
bed_actions.c:266:   active_switch = OFF;
bed_actions.c:267: }
[exit 0]
```

**Note on #28.** Its CR counts are **wrong**: the pattern `$'\r'` did not
survive the wrapper's `sh -c`, and the count equals each file's line count.
#29 repeats the check with `tr -cd '\r' | wc -c` and shows 0 CRs in every
edited file; `file` (also #29) reports none "with CRLF line terminators".
The `LF will be replaced by CRLF` warnings in #14, #27 and #30 come from
`core.autocrlf=true` (#28) and concern what git would write on checkout, not
the working files.

### d.3 Build method

`$S/build.sh` (same tools and preset as the AF session's wrapper):

```sh
#!/bin/sh
# Session build wrapper: Silicon Labs ninja + cmake on PATH, build the default preset.
P=~/.silabs/slt/installs/conan/p
export PATH="$P/ninja1a38fc85adcf7/p:$P/cmakefa35ab0687064/p/bin:$PATH"
cd "C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/cmake_gcc" || exit 99
cmake --build --preset default_config > C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/build_out.txt 2>&1
rc=$?
tail -25 C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/build_out.txt
echo "exit $rc"
echo "--- warnings/errors (grep -n -i 'warning\|error' over the whole output):"
grep -n -i 'warning\|error' C:/Users/adunc/AppData/Local/Temp/claude/C--Users-adunc-Desktop--Tilt-Beds--03-Software-tilt-bed-controller/f539c38e-419f-4447-88df-8e82f6ef83bf/scratchpad/build_out.txt
exit $rc
```

Nothing printed after the `--- warnings/errors` line means no line of the
full build output contains "warning" or "error".

## e. VERIFIED vs ASSERTED

Nothing in this session ran on the bed. VERIFIED below means a command output
(section d) or a line of code that was read; it never means "observed on
hardware". `#n` = d.2 command numbers; `P n` = d.0.

### e.1 VERIFIED, with evidence

| # | Claim | Evidence |
|---|---|---|
| V1 | Start state as the prompt requires: HEAD `b1991ed`, parent `e646c3b`, = `origin/main` after `git fetch`; `.projectlinkstore` modified, the prompt untracked, nothing else | P1 (`git status`), P2 (`git rev-parse HEAD origin/main HEAD^`) |
| V2 | Builds exit 0 with no `warning`/`error` line: after the edit (`BLE_DEBUG_WATCHDOG_LOG` 1), at 0, back at 1; after the commit no work | #10, #11, #12, #31 (each `exit 0`, nothing after the `---` line) |
| V3 | Size before 245588 / 784 / 72476 (= AF record); after 245620 / 784 / 72476 (+32 text); at 0: 243924 / 780 / 72480 | #9, #10, #11, #12 |
| V4 | `BLE_DEBUG_WATCHDOG_LOG` back at 1; `ble_bed_service.h` not changed | #12 (`36:#define BLE_DEBUG_WATCHDOG_LOG 1`; `git status --short` lists no `.h`), #31; c.1 has no `.h` hunk |
| V5 | Host test passes, `0 check(s) failed`; `bed_control.c` unchanged | #13; c.1 |
| V6 | `BED_BENCH_PROVOKE_RUNAWAY` 0, `BLE_BENCH_WIPE_BONDS` 0, `BED_CAPTURE_MOTOR_CURRENTS` 0 | #31 (`switch_input.c:98`, `ble_bed_service.c:70`, `debug_capture.h:21`) |
| V7 | Only `ble_bed_service.c` changed among sources; no `config/`, `autogen/`, GATT or Protocol Version change; commit contains exactly the prompt's files | #30 (`git show --stat`: one `.c`, four docs, the prompt); #31 (`git show --stat HEAD -- config autogen` printed nothing); c.1 |
| V8 | The three changes are as described in b.1: viewer branch `:721-731`; refusal sets the latch `:813` then `note_app_commanding()` `:814`; trip sets the latch `:628` then `note_app_commanding()` `:629` | c.1; #15; #28 (working tree; same as c.1) |
| V9 | The latch is set only at `:628` and `:813`; `app_commanding = false` only at `:577` inside `note_app_stopped_commanding()`, which clears the latch at `:589` - so latched implies the silence timer is running | #15 (every `app_collision_latched` line), #33 (`577:  app_commanding = false;` the only match), #3 (every use of `app_commanding`) |
| V10 | `note_app_commanding()` writes only `app_commanding` and `app_command_deadline_tick`; only `service_app_commanding()` and `note_app_stopped_commanding()` use them; it calls nothing in bed_control | `ble_bed_service.c:565-570` (#25); #3 |
| V11 | bed_control's app half (`source_commanding[]`) is set only in `bed_move_start()` / `bed_auto_level_start()` and cleared by releases and stop verbs; `conflict_inhibit` is set only right after `note_commanding(src, true)` and cleared only when both sources are not commanding | `bed_control.c:31-54`, `:97-151` (Read at `b1991ed`; file unchanged, c.1); #2 |
| V12 | Callers of `note_app_stopped_commanding()` after the change: `:602` silence, `:670` watchdog, `:722` STOP_ALL (control or no latch), `:889` MOVE_STOP, `:928` AUTO_LEVEL_STOP, `:1113` disconnect - six | #15 |
| V13 | Viewer check `:788` and settings check `:793` precede the refusal (`:811`), which precedes the #36 check (`:834`) | #33, #25 |
| V14 | The disconnect stop is set only if the closed link was control and `bed_active_source() == BED_SOURCE_APP` (`:1011`); the source returns to NONE whenever the bed is idle; `app_stop_all()` sets `active_switch = OFF` (`bed_actions.c:266`); nothing in `dual_motor_control.c` assigns `active_switch` | #25 (`1011:`), `bed_control.c:166-171` (Read); #33 (`bed_actions.c:266`); #26 |
| V15 | An app MOVE_STOP calls `app_apply_stop_outputs()`, which calls `set_all_motors_off()` and `app_release_active_switch()` | `bed_actions.c:334-345` (#20, grep -A14; content not reproduced); `ble_bed_service.c:889-892` (c.1 context and Read at `b1991ed` `:869-873`) |
| V16 | The physical prints in b.6 exist and are enabled: `SW: 0x%04X %s (bounces %u)` (`switch_input.c:423`) under `SWITCH_SAMPLER_DIAG` 1 (`:57`); `LEVELED ...` under `BED_DEBUG_LEVELED_LOG` 1; `FAILSAFE: ...` (`switch_input.c:885`) | #4, #5, #6, #7, #21, #22 |
| V17 | The fail-safe stands down while any switch is down | `switch_input.c:831-838` (#5; content not reproduced) |
| V18 | Edited files have LF endings (0 CR) | #29 (and the note on #28) |
| V19 | Original #48 text still contains the AF wording the amendment supersedes | #27 (`DECISIONS.md:2476`, `:2544`) |

### e.2 ASSERTED (reasoned from code, compiles, not observed)

- **A1.** Every row of the b.5 tables (B1, B2.a-d, B3, B4), including the
  Command Results, their order and the motors. In particular: a re-press inside
  the sequence, an app press during a physical collision, and a held control
  button after a viewer STOP_ALL start nothing until released, during and
  after the sequence.
- **A2.** Every console line in b.6, and its order.
- **A3.** The second `bed_note_source_released(BED_SOURCE_APP)` at the later
  silence clear, after a viewer STOP_ALL, changes nothing in bed_control
  (b.1.1; built from V11).
- **A4.** The silence clear after the trip is up to ~100 ms later than in AF
  for an app that goes quiet at the first 0x06, and nothing observable
  depends on that (b.2, b.5 B4).
- **A5.** The disconnect after a viewer STOP_ALL clears by silence within 300
  ms of the control app's last write (b.7.1; built from V14).
- **A6.** b.7.2 (STOP_ALL processed after a control drop + promotion is
  treated as a viewer's) and b.7.3 (an app release during the reverse cuts
  it).
- **A7.** Every AF ASSERTED item that this session did not change still
  stands (the fail-safe cutting the reverse, the `any_collision` window, the
  #45 veto being off during the latch, the reconnect timing).
- **A8.** App behaviour (both "keeps repeating" and "ends its hold at the
  first 0x06") is modelled, not read: the app repository was not looked at.

---

## f. QUESTIONS FOR REVIEW

**f.1 The control-phone disconnect after a viewer STOP_ALL clears the latch
by silence, not through the disconnect stop** (b.7.1). A1 lists "the
control-phone disconnect" among the paths that must still clear the latch.
After a viewer STOP_ALL the bed is idle, so `handle_connection_closed()` does
not set `disconnect_stop_pending`, and the latch is cleared by the silence
timer ≤300 ms after the control app's last write (printed `silence`). This is
the AF f.4 situation, which was accepted; I treated it as satisfying A1 and
did not add code. If the reviewer wants the disconnect itself to clear it, the
smallest change would be in `ble_bed_service_tick()`'s main-loop path, not the
callback: e.g. `handle_connection_closed()` also sets a flag when the closed
link was control and `app_collision_latched`, and the tick calls
`note_app_stopped_commanding("disconnect")` without `app_stop_all()`.
Not done: confirm that silence is acceptable here.

**f.2 The control phone's own STOP_ALL after the sequence still lets a held
repeat restart the move** (b.5 B2.b, b.7.4). Kept as the prompt says ("A
STOP_ALL from the control connection still clears the latch, as now"). It
needs a second finger on the control phone while the first stays on the held
button, and it equals STOP_ALL during an ordinary hold. Accept?

**f.3 A STOP_ALL written by the control phone but processed after a control
drop + viewer promotion is treated as a viewer's** (b.7.2): the latch stays
and clears by silence. Safe direction. Accept as is?

**f.4 An app release during the collision reverse cuts the reverse**
(b.7.3; pre-existing). A MOVE_STOP / AUTO_LEVEL_STOP / STOP_ALL landing at
~1.0-1.2 s turns all motors off at once. With the fail-safe (F26), the
reverse after an app collision is short whenever the user does anything.
Should this be added to F26 (one row covering "what may cut the reverse"), or
recorded separately?

**f.5 A1's "everything else stays exactly as it is".** In the viewer
STOP_ALL while latched case, two things that STOP_ALL did before are not done:
`app_commanding` is not cleared (so the silence timer keeps running - the
mechanism A1 asks for), and the debug refused-count is not reset (so the
eventual "cleared" line counts the whole episode). Everything else - the
watchdog cancel, both app flags, the app's #36 release, `app_stop_all()`, the
0x00 result - is unchanged (b.1.1 table). Confirm that reading.

**f.6 The latch-setting invariant** (b.3) is now load-bearing: A1 relies on
the latch never being set without `note_app_commanding()`. It is stated in
the comment on `app_collision_latched`. Is a comment enough, or should a
future prompt fold the two into one helper (e.g. `latch_collision()`)? Not
done here (no refactor beyond what was asked).

---

## g. NOT DONE

- **No bench run** - the user drives the bench (CLAUDE.md). REGRESSION
  5.A1-5.A6 and the b.6 lines are for the reviewer's bench steps; per the
  reviewer (AF f.8), the collision test uses a soft obstruction.
- **No host-test cases.** Not asked in this prompt; the reason they cannot be
  added to the existing test is unchanged (AF b.8: the latch is in
  `ble_bed_service.c`, which the host test cannot compile).
- **The app side**: its copy of BLE_GATT_DESIGN §7 and its handling of 0x06
  are for the next app prompt.
- **Section d does not reproduce file-content output** (Prompt AG D3); each
  such command is listed with its file and range. The full build logs of #10-#12 were
  overwritten by the next build. Their 25-line tails, which were the whole
  output of each build, are in d.2.
- **The commit of this file with the SESSION_LOG entry, and the `git push`**
  run after this file is written, so their output cannot appear here. They
  are reported to the user. A push failure would be reported in a follow-up
  commit to this file, without merge, rebase or reset (CLAUDE.md "Pushing").
- Nothing else that was asked was left out. No judgement call was taken as a
  stop: f.1 is a reading of A1 that I acted on, raised for confirmation.
