# fw_HANDOFF_2026-11-03_03 — Prompt AK: Prompt AI follow-ups (top-of-tick silence check only when no hold is armed; conn= on the no-latch STOP_ALL print)

Firmware repo session of 2026-11-03. System date checked with `date` at the
start (`Tue Nov  3 16:53:36 PST 2026`, d.0 P1; `16:53:59`, d.2 #1) and again
before naming this file (`Tue Nov  3 17:00:10 PST 2026`, d.2 #16). `_01` and
`_02` for this date already existed (Prompt AH and Prompt AI handoffs, listed
in #16), so this is `_03`. Prompt: `docs/prompts/PROMPT_AK_ai-followups.md`
(written by the reviewer, committed in `3fc2e98`).

## a. Header

| | |
|---|---|
| Repo | `tilt_bed_controller` (firmware) |
| Branch | `main` |
| HEAD before | `08afb33` (Handoff 2026-11-03 02: Prompt AI), equal to `origin/main` after `git fetch`, as the prompt expects (d.0 P2, d.2 #1) |
| HEAD after the work | `3fc2e98` - one work commit (A1, A2, B1, B2 and the prompt file) |
| After that | this handoff and the session's SESSION_LOG entry (B3 rulings), committed together, then `git push` (normal push, no force) |
| Builds | from the command line (A3): baseline and Prompt AK, each with `BLE_DEBUG_WATCHDOG_LOG` 1 and 0, flag restored to 1; one more build after a comment-only edit - every build exit 0, no warning/error line (d.2 #6, #7, #14) |
| Host test | `0 check(s) failed` (#8) |
| Bench | **nothing ran on a bed in this session** |

`git status` at the end of the work (after `3fc2e98` and the SESSION_LOG
entry, before this file was written; d.2 #16):

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
| A1 | `if (!move_watchdog_armed) service_app_commanding();` at the top of `ble_bed_service_tick()`; later call unchanged; confirm it closes AI b.A.2 item 5 and keeps A2's purpose; state every case that differs from `08afb33` | `ble_bed_service.c:1181-1183` (`3fc2e98`); later call `:1202` unchanged. Analysis b.1. Item 5 is closed; A2's purpose is kept (b.1.4); four cases differ (b.1.3). **A narrower, older window through the unchanged later call remains** - reported, not changed (b.1.5, f.1) |
| A2 | No-latch STOP_ALL print gains the connection | `:783`: `BLE: STOP_ALL (no app hold, no latch) (conn=%u)`. b.2 |
| A3 | CLI builds at `BLE_DEBUG_WATCHDOG_LOG` 1 and 0 (back to 1); raw tails, warnings, size change; host test | d.2 #6, #7, #14 (builds), #8 (host test). b.4 |
| B1 | DECISIONS #49 "Amended by Prompt AK" for A1 | `3fc2e98`; text in c.2. It also names A2's print (one sentence; see f.3) |
| B2 | PRE_SHIP row for AI f.7 | F31, `3fc2e98`; text in c.2 |
| B3 | Rulings in the SESSION_LOG entry | Done (SESSION_LOG, this session's entry) |
| C | In the handoff: A1 analysis, raw build output, console lines for three STOP cases | b.1, d.2, b.3 |

---

## b. Designs and analysis

### b.1 A1 - the top-of-tick silence check only when no hold is armed

#### b.1.1 The change

`ble_bed_service_tick()` (`ble_bed_service.c:1168`) now reads, in order:

```c
if (!move_watchdog_armed) {      // NEW condition (Prompt AK A1)            :1181-1183
  service_app_commanding();      // Prompt AI A2's top-of-tick silence check
}
if (disconnect_stop_pending) { ... note_app_stopped_commanding("disconnect"); app_stop_all(); }   // :1187-1194
process_pending_command();       // :1200
service_move_watchdog();         // :1201
service_app_commanding();        // :1202 - the existing later call, unchanged
... hold-end / latch-cleared debug prints, telemetry ...
```

The comment above the call says why (c.1). Nothing else in the code changed
for A1.

#### b.1.2 Facts from the code that the analysis uses (all read this session)

- **F1** `service_app_commanding()` (`:629-638`) does nothing unless
  `app_commanding` is true **and** the tick count has reached
  `app_command_deadline_tick`; then it calls
  `note_app_stopped_commanding("silence")`.
- **F2** `note_app_stopped_commanding()` (`:608-624`): `app_commanding =
  false`; `bed_note_source_released(BED_SOURCE_APP)`; if a start latch is set,
  queue the `start latch (...) cleared (...)` print; latch = NONE.
- **F3** `note_app_commanding()` (`:582-587`) sets `app_commanding` and
  `deadline = now + 300 ms` (call this D_c).
- **F4** `move_watchdog_arm()` (`:518-559`) sets `move_watchdog_armed`,
  clears `move_watchdog_expired`, and restarts the 300 ms sleeptimer from now
  (expiry D_w). Its only callers are `:942` (MOVE_START accepted) and `:982`
  (AUTO_LEVEL_START accepted), each after `note_app_commanding()` in the same
  case (`:915`, `:966`). So for the refresh that a hold last had, **D_c <=
  D_w**. Between the two calls there are a few instructions for an identical
  repeat, and the motor start (`bed_move_start()` + `app_apply_move_outputs()`,
  expander I2C) for a fresh start (#3, #4 grep; `:915-942`, `:966-982`).
- **F5** `note_app_commanding()` is also called without an arm: the #36
  refusal (`:900`) and `latch_app_starts()` (`:602`). These move D_c later
  without moving D_w.
- **F6** A start latch is set only by `latch_app_starts()`, called at `:662`
  (watchdog collision branch, after `move_watchdog_cancel()` at `:659`),
  `:775` (STOP_ALL, after `move_watchdog_cancel()` at `:757`), `:874` and
  `:877` (the start refusal) (#3).
- **F7** `any_collision` is cleared only at `app.c:678`; `ble_bed_service_tick()`
  is called from `app.c:845` (#3). Nothing in the BLE tick clears it.
- **F8** `service_move_watchdog()` (`:641-727`): if not armed, return; if
  `any_collision`, cancel, clear both app flags, `latch_app_starts(COLLISION)`,
  COLLISION_STOP result, print; if the bed went idle under an app move, clear
  `app_move_active` **without** cancelling (`:672-684`); if expired, cancel,
  clear flags, `note_app_stopped_commanding("watchdog")`, `app_stop_all()`,
  WATCHDOG_STOP result, print.
- **F9** bed_control (`bed_control.c`): `source_commanding[APP]` is set true
  only by `bed_move_start()` / `bed_auto_level_start()` (`:99`, `:127`);
  `note_commanding()` clears `conflict_inhibit` when neither source is
  commanding (`:44-48`); `bed_note_source_released()` is `note_commanding(src,
  false)` (`:51-54`); `bed_lights_set()` does not touch either (`:156-164`, #4).
- **F10** The STOP_ALL branch (`:746-786`) takes `latch_stop =
  app_commanding || latch != NONE` first (`:755`), then cancels the watchdog,
  clears both app flags, `bed_note_source_released(APP)`, `app_stop_all()`,
  0x00 to the sender, and latches STOP only if `latch_stop`.
- **F11** The start refusal (`:871-886`) runs before the #36 check (`:898`)
  for MOVE_START / AUTO_LEVEL_START when a latch is set or `any_collision`.

#### b.1.3 Exactly which passes behave differently from `08afb33`, and how

**Where a difference can happen at all.** When the watchdog is not armed the
top call runs as at `08afb33`. When it is armed, `08afb33`'s call did
something only if `app_commanding` was true and the deadline had been reached
(F1). So A1 changes only a pass whose top finds **armed, `app_commanding`
true, and the deadline reached**. Call it P*.

- P* can only be **the first pass that starts at or after D_c** of an armed
  hold: the pass before it ended with the unconditional later call (`:1202`,
  both versions), which would have released the app had it run after D_c.
- The watchdog is still armed at P*'s top, because only `service_move_watchdog()`
  (or a command) cancels it, and those run later in the pass.
- P* cannot happen once a #36 refusal has followed the last accepted
  refresh: the refusal moves D_c later than D_w (F5), and the watchdog stop
  (at about D_w) cancels the hold first.
- At the end of P*, both versions have run `:1202`. So **the end-of-pass state
  can differ only through what the disconnect block, `process_pending_command()`
  and `service_move_watchdog()` do in P* while `app_commanding` is still true
  (AK) instead of already released (`08afb33`)**.

**Every case of what P* contains:**

| # | In P* | `08afb33` | Prompt AK | Differs? |
|---|---|---|---|---|
| 1 | Nothing pending, no collision | Top releases ("silence"); then, if the watchdog has expired, the watchdog stop (second release, a no-op), WATCHDOG_STOP result and print; else nothing more | If expired: the watchdog stop releases ("watchdog"), same result and print; else `:1202` releases ("silence") | **No.** Same stop, same result, same prints (no latch is set, so neither release prints a `start latch ... cleared` line) |
| 2 | Disconnect pending | Top releases ("silence"), then the disconnect block releases again and stops | The disconnect block releases ("disconnect") and stops | **No.** The reason string is printed only when a latch is set, and none is while armed (b.1.4) |
| 3 | STOP_ALL, from either role | `latch_stop` false: no latch, `BLE: STOP_ALL (no app hold, no latch)` | `latch_stop` true (F10): STOP latch, `BLE: STOP_ALL latch set (conn=N)`. The latch clears at the app's MOVE_STOP / AUTO_LEVEL_STOP (`cleared (stop)`), a disconnect, or 300 ms of silence after the STOP_ALL (`cleared (silence)`); a start before that is 0x07 and re-latches | **Yes.** The stop itself, the 0x00 to the sender, the watchdog cancel and the `hold end` line are the same. The user was holding when the link stalled, so AK treats this STOP as one during a held button (AH A6.1). |
| 4 | An identical repeat (MOVE_START with the same target/dir and `app_move_active`, or AUTO_LEVEL_START with `app_auto_level_active`), no #36 inhibit, no collision | Top releases the app in bed_control (`source_commanding[APP]` false); the repeat refreshes the watchdog; **the move continues with the app "released"** - AI b.A.2 item 5 | The repeat is processed with the app still commanding: new D_c, re-arm; `source_commanding[APP]` stays true | **Yes - the fix.** Same as before Prompt AI for this case |
| 5 | MOVE_START with a different target/dir (`app_move_active`) | Top releases APP; then `bed_move_stop(APP)` (releases again), `bed_move_start()` (sets it) | `bed_move_stop(APP)` releases, `bed_move_start()` sets | **No.** Same bed_control calls in effect, same result |
| 6 | A start (MOVE_START or AUTO_LEVEL_START) while the #36 inhibit is set from a switch conflict earlier in this hold, **the switch already released**, and no app repeat since the conflict | Top release clears the inhibit (F9) -> the start is accepted 0x00 and the bed moves | Inhibit still set -> refused 0x01 (`:898-902`, D_c moves later); the watchdog stop follows in this pass or the next (`app_stop_all()` on an idle bed, release clears the inhibit, WATCHDOG_STOP to the app); the app's next repeat (~100 ms later) is accepted | **Yes.** The stall is still treated as a release (AH f.2's trade-off); the bed starts one repeat later, after a 0x01 and a WATCHDOG_STOP. With the switch still held both versions refuse 0x01 |
| 7 | MOVE_STOP or AUTO_LEVEL_STOP | Top releases; the stop releases again | The stop releases | **No** |
| 8 | LIGHTS_SET | Top releases; lights set | Lights set (F9: no commanding/inhibit change); `:1202` or the watchdog stop releases | **No** |
| 9 | REFRESH_TILT; a viewer's motion command (refused); settings-mode or invalid-length refusals | Top releases; command handled | Command handled; `:1202` / watchdog releases | **No** |
| 10 | A collision: `any_collision` set earlier in P* by current supervision, and/or a start refused at `:871` for `any_collision` | Top releases APP in bed_control; then COLLISION latch (`app_commanding` true again, new deadline), 0x06 / COLLISION_STOP, print | COLLISION latch with `source_commanding[APP]` still true; the app's half of #36 is released when the latch clears (stop, disconnect or silence) | **Yes, in bed_control only.** Same results, latch and prints. AK matches the ordinary collision-during-hold path, whose code says the app has not released (`:654-658`) |

So the cases that differ are **3, 4, 6 and 10**, all inside the single pass
P*. Every other pass, and every pass with no armed hold, is unchanged from
`08afb33`.

**Prompt AI b.A.2's list, corrected for A1:** items 1-3 (the start latch
judged before the next command) still hold: they concern states with no armed
hold. Item 4 (#36, a late repeat after a stall) now holds once the watchdog
stop has happened, not in P*. Item 5 is closed. Item 6 (the disconnect print
says `silence`) is gone while armed; with no hold armed it can still happen
when a latch is set. Item 7 is back to the watchdog releasing first. Item 8 is
unchanged.

#### b.1.4 A2's purpose is kept

Claim: **at the start of every `ble_bed_service_tick()`, a set start latch
means the watchdog is not armed.** So whenever there is a latched release to
judge, the top call runs and judges it before the next command, as Prompt AI A2
intended.

Proof from code (F4, F6, F7, F8, F11):

1. The latch is set only by `latch_app_starts()` (F6).
2. At `:662` and `:775` the watchdog is cancelled first, in the same block.
3. At `:877` (STOP, no collision) the latch was already set when the command
   arrived. By induction the watchdog was not armed at the start of the pass,
   and nothing earlier in the pass can arm it (only an accepted start arms it,
   and this command is a refused start).
4. At `:874` (COLLISION): either the latch was already COLLISION (as in 3), or
   `any_collision` is true while the watchdog may be armed. In that case
   `service_move_watchdog()`, later in the **same** pass, sees `armed &&
   any_collision` and cancels (F8). `any_collision` cannot be cleared in
   between (F7). So by the end of the pass the watchdog is not armed.
5. While a latch is set, nothing arms the watchdog: an arm needs an accepted
   start, and a start with a latch set or `any_collision` is refused first
   (F11). The timer ISR only sets `move_watchdog_expired` (`:511-516`).
6. Between ticks nothing else touches these variables. They are static to
   `ble_bed_service.c`, and the grep in #3 shows no other writers.

Inside a pass, the latch and the armed watchdog can coexist for a moment
(step 4, from `:874` until `:1201`). That is why the code comment says "at the
start of a tick" (c.1).

#### b.1.5 What A1 does not close: a narrower, older window through the later call (reported, not changed - f.1)

The later call (`:1202`) is unconditional, as the prompt requires. It can
release the app during an armed hold whenever a pass ends inside the gap
between the silence deadline and the watchdog stop:

1. Pass P1: `service_move_watchdog()` (`:1201`) runs before the watchdog ISR
   has set `move_watchdog_expired`, and then `:1202` runs at or after D_c.
   This is possible because D_c <= D_w (F4). `:1202` releases the app:
   `source_commanding[APP]` becomes false.
2. A later pass P2, before any `service_move_watchdog()` has acted on the
   expiry: the late repeat is processed. The top call is skipped (armed), and
   `app_commanding` is false anyway. The repeat is an identical repeat, so it
   only re-arms the watchdog, which also clears a pending expired flag (F4).
   **The move continues with `source_commanding[APP]` false: the same
   consequence as Prompt AI's item 5.** A wall-switch press later in that hold
   force-stops and latches the inhibit as usual, but the inhibit clears at
   the switch release, and the app's next held repeat starts the bed.

**Width.** P1's last two calls must land in [D_c, D_w), plus the few
microseconds between the `:1201` check and `:1202`.

- After an identical repeat, D_w - D_c is the few instructions between `:915`
  and `:942` (or `:966` and `:982`). That is 0 or 1 sleeptimer tick (30.5 µs
  at 32 768 Hz; ASSERTED, the tick rate was not read this session).
- After a fresh start it is the motor start's I2C time, a few ms (not
  measured).

Prompt AI's item 5 needed only "the late repeat is processed in the first
pass after D_c". This window also needs a pass to end inside a µs- to ms-wide
gap just before it. It is much narrower, but not zero.

**Not introduced by Prompt AI or AK.** The later call and its position predate
Prompt AI: it is "the existing call", kept by AI A2 (AI handoff a., `:1193` at
`8e1242d`). AI b.A.2 item 5's sentence "Before A2 the repeat was processed
first and pushed the deadline, so the app stayed commanding" holds for P* but
misses this path.

**Options, none made** (the prompt says the later call stays as it is):

- (a) Guard the later call the same way: `if (!move_watchdog_armed)
  service_app_commanding();` at `:1202`.
- (b) Equivalently, put the guard inside `service_app_commanding()`.

Either way, while a hold is armed the release would come only from the
watchdog stop. That stop always follows: D_w >= D_c, and every arm is
preceded by a note (F4). It comes at most D_w - D_c plus one pass after the
deadline. Outside an armed hold nothing would change. Case 1 of b.1.3 would
then release as "watchdog" in every pass, and no print depends on that. With
(a), one more case changes compared with now: a STOP_ALL in **any** pass
between D_c and the watchdog stop latches, not just in P*. This is the same
reasoning as case 3.

### b.2 A2 - `conn=` on the no-latch print

`process_pending_command()`, STOP_ALL branch, `:783`:

```c
printf("BLE: STOP_ALL (no app hold, no latch) (conn=%u)\r\n", conn);
```

`conn` is the same local that the `latch set` line uses: the sender's handle,
snapshotted from `stop_all_conn` at `:748` (written by the stack callback at
`:1512`). Still behind `BLE_DEBUG_WATCHDOG_LOG`, still printed after the stop
and the result. With the flag at 0 the line does not exist (d.2 #6: the flag-0
builds compile without warnings).

### b.3 Console lines the next bench test should expect (from code; nothing observed)

`<...>` = a value. Lines that this prompt does not touch, and that can
interleave, are not listed: `Max Current for Motor ...` during a move,
`TILT idle: ...` every 10 s at rest with a phone subscribed, and `LEVELED
(Bed Status bit 4) -> 0` when a move starts on a LEVELED bed. Connection
handles: the control phone is the first connection to take a role
(`assign_role_if_needed()`, `:455-465`). Each phone's handle is in its `BLE:
connection_opened conn=<n> ...` line (`:1332`). Whether that line is behind
`BLE_DEBUG_SECURITY_LOG` was not checked; that flag is 1 (`:53`).

**1. App STOP while holding a move** (control phone holding HEAD up; STOP
tapped with another finger; HEAD finger kept down ~2 s; released). REGRESSION
5b.1.

```
BLE: STOP_ALL latch set (conn=<C>)
BLE: hold end op=0x01 seq=<S>: <R> refreshes, longest refresh gap <G> ms, longest between passes <B> ms
                                              (each held repeat: Command Result 0x07 to the phone, no console line)
BLE: start latch (stop) cleared (stop), <K> repeats refused      <- at the release (the app's MOVE_STOP); K ~ 10 per second held
```

Variants:

- An app that sends MOVE_STOP right after its STOP_ALL: `cleared (stop), 0
  repeats refused` at once.
- A release with no stop write reaching the bed: `cleared (silence)` about
  300 ms after the last repeat.
- With an AUTO LEVEL hold: `op=0x03`.
- If the STOP lands in the first pass after a link stall of 300 ms or more
  (b.1.3 case 3): the same lines. Before AK this read `BLE: STOP_ALL (no app
  hold, no latch)`. It cannot be staged on purpose.

**2. App STOP with nothing held** (no app button touched for at least 1 s, or
the last hold ended with a stop write). REGRESSION 5b.3.

```
BLE: STOP_ALL (no app hold, no latch) (conn=<C>)
```

That line only: no `hold end` line (no watchdog was armed), and no `start
latch` line. The next press is a normal start (0x00). Exception (5b.3 (c)): a
STOP within 300 ms of a release whose stop write did not reach the bed prints
`BLE: STOP_ALL latch set (conn=<C>)`, then about 300 ms later `BLE: start
latch (stop) cleared (silence), 0 repeats refused`.

**3. STOP from a second phone (viewer) while the first holds a move.**
REGRESSION 5b.5.

```
BLE: STOP_ALL latch set (conn=<V>)            <- V = the viewer's handle, not the control phone's
BLE: hold end op=0x01 seq=<S>: <R> refreshes, longest refresh gap <G> ms, longest between passes <B> ms
                                              (viewer: 0x00 for its STOP; control phone: 0x07 for every held repeat, no line; nothing restarts)
BLE: start latch (stop) cleared (stop), <K> repeats refused      <- at the control phone's release
```

With the control phone idle, the viewer's STOP prints `BLE: STOP_ALL (no app
hold, no latch) (conn=<V>)`. A control press right after it moves normally.

### b.4 A3 - builds, sizes, host test (raw output in d.2 #6, #7, #8, #14)

| Build | `BLE_DEBUG_WATCHDOG_LOG` | text / data / bss | Exit, warnings |
|---|---|---|---|
| Baseline (`08afb33`'s `ble_bed_service.c` checked out over the AK file) | 1 | 248,540 / 788 / 72,472 (= the AI handoff's final figure) | 0, none |
| Baseline | 0 | 246,652 / 780 / 72,480 | 0, none |
| Prompt AK | 0 | 246,652 / 780 / 72,480 (**+0 B**) | 0, none |
| Prompt AK, flag restored | 1 | 248,548 / 788 / 72,472 (**+8 B** text) | 0, none |
| Prompt AK after the comment-only edit (#14) | 1 | 248,548 / 788 / 72,472 | 0, none |

**Why +0 B at flag 0:** `nm -S` shows `ble_bed_service_tick` grew from
0x3f8 to 0x400 bytes (+8). The next object's alignment padding took it up, so
the total did not move. The disassembly confirms the change is compiled in:
the AK function loads `move_watchdog_armed` and `cbnz` skips the `bl
service_app_commanding`, where the baseline calls it unconditionally (#7). At
flag 1 the 8 bytes are the same guard plus the extra `printf` argument, so the
total moves.

The flag-swap scripts restored the AK source and flag 1, checked with `cmp`
and `grep` (#6, #7). Host test: `0 check(s) failed` (#8).

---

## c. Diffs

### c.1 Source diff - `git show 3fc2e98 -- ble_bed_service.c` (complete; from d.2 #16)

```diff
diff --git a/ble_bed_service.c b/ble_bed_service.c
index e8dbde0..2e6cf36 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -780,7 +780,7 @@ static void process_pending_command(void)
       printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
              (app_start_latch == APP_LATCH_COLLISION) ? ", kept as collision" : "");
     } else {
-      printf("BLE: STOP_ALL (no app hold, no latch)\r\n");
+      printf("BLE: STOP_ALL (no app hold, no latch) (conn=%u)\r\n", conn);
     }
 #endif
   }
@@ -1171,7 +1171,16 @@ void ble_bed_service_tick(void)
   // since the app's last start" is decided at the moment the next command is
   // processed - not only if some earlier pass happened to run after the
   // deadline. Also run after the commands below, as before.
-  service_app_commanding();
+  //
+  // Not while an app hold is armed (Prompt AK A1): the watchdog is the
+  // MOVE_WATCHDOG_MS judge then, and does the release and the stop itself.
+  // Judging silence here first would let a late repeat of that hold continue
+  // the move with the app already released in bed_control (#36). At the start
+  // of a tick a set start latch means the watchdog is not armed, so latched
+  // releases are still judged before the next command.
+  if (!move_watchdog_armed) {
+    service_app_commanding();
+  }
 
   // Control link dropped mid-command (BLE_GATT_DESIGN.md #5). Deferred out
   // of the disconnect callback; done here, first, before anything else.
```

### c.2 Docs diff - `git show 3fc2e98 -- docs/DECISIONS.md docs/PRE_SHIP.md` (complete; from d.2 #16)

```diff
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 74f457f..65469d7 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2896,6 +2896,30 @@ change.
 host test unchanged. **Not run on the bench.** REGRESSION §5b updated.
 What each AH A6 case does now is in the Prompt AI handoff.
 
+### Amended by Prompt AK (2026-11-03)
+
+Reviewer's ruling on the Prompt AI handoff QUESTIONS f.1: the top-of-tick
+silence check (Prompt AI item 2 above) now runs only when no app hold is armed
+- `if (!move_watchdog_armed) service_app_commanding();` (`ble_bed_service.c:1181-1183`);
+the call after `service_move_watchdog()` is unchanged (`:1202`). While a hold
+is armed the dead-man watchdog is the 300 ms judge and does the release and
+the stop itself; judging silence first is what opened Prompt AI's "one new
+case, narrow" (a late repeat of an accepted hold continuing the move with
+`source_commanding[APP]` already false in bed_control). That case is closed:
+the repeat is processed with the app still commanding, as before Prompt AI.
+Prompt AI's purpose is kept: at the start of every tick a set start latch
+means the watchdog is not armed, so latched releases are still judged before
+the next command. Other cases that change are in the same pass only (the
+first pass after the deadline of an armed hold): a STOP_ALL there latches
+(STOP) instead of not latching; a start there while the #36 inhibit is set
+from an earlier switch conflict is refused 0x01 and the watchdog stop follows,
+instead of starting at once; a collision there keeps the app's half of #36
+until the collision latch releases. The full list, and a narrower window that
+the unchanged later call still leaves, are in the Prompt AK handoff. Also (AK
+A2): the debug line is now `BLE: STOP_ALL (no app hold, no latch) (conn=N)`.
+**Status:** builds clean with `BLE_DEBUG_WATCHDOG_LOG` 1 and 0; host test
+unchanged. **Not run on the bench.**
+
 ## 50. Live idle tilt: a 10 Hz idle sampler with its own millisecond filter, 5 Hz idle notify, `tilt_fresh` live
 
 Date: 2026-11-03. Reviewer's Prompt AI part C (Option B of the Prompt AH
diff --git a/docs/PRE_SHIP.md b/docs/PRE_SHIP.md
index aead975..c889fa1 100644
--- a/docs/PRE_SHIP.md
+++ b/docs/PRE_SHIP.md
@@ -52,6 +52,7 @@ File:line references are to the firmware at `6796e6b` unless marked.
 | F28 | A collision flagged during a BED LIGHTS hold toggles `bedlights_on` at the release without writing the output | Not expected to happen (no motor runs during a BED LIGHTS hold), but the flag and the amber/lighting output would then disagree | Claude | Review before release | Prompt AE handoff QUESTIONS 8; Prompt AG C4 |
 | F29 | `TILT_IDLE_DIAG` 1 -> 0 (`auto_level.c`) | Bench print `TILT idle: N samples in T ms, roll ... pitch ...` every 10 s while the idle tilt sampler runs | Claude Code | Change to 0, build, check the `TILT idle` lines are gone. First use the lines to set `TILT_IDLE_TAU_MS` and the LEVELED exit band | Prompt AI C6; DECISIONS #50 |
 | F30 | A phone that stays subscribed to Bed Status with the app in the background keeps the 10 Hz idle sampler (and the 5 Hz notify) running | The firmware samples whenever a connection is subscribed; it cannot tell a backgrounded app. Costs the subscribed-idle current of F11 for as long as the phone stays connected | Claude Code (app) | The app unsubscribes from Bed Status when backgrounded (app prompt); the firmware needs nothing now. Bench: background the app, check the `TILT idle` lines stop | Prompt AI C5; DECISIONS #50 risk 5 |
+| F31 | The boot reset-cause print depends on GCC inlining `sl_hal_emu` functions at `-Os` | `app.c:450-451` (lines at `08afb33`) calls `sl_hal_emu_get_reset_cause()` / `sl_hal_emu_clear_reset_cause()` (inline in `sl_hal_emu.h`); `sl_hal_emu.c` is not in the build, so at a lower optimisation level (`cmake_gcc/tilt_bed_controller.cmake:365` sets `-Os`) the link fails with an undefined reference | Andrew + Claude | Add the proper component in Simplicity Studio (one that brings in `sl_hal_emu.c`) before release, or confirm the build settings are frozen | Prompt AI handoff b.A.4, QUESTIONS f.7; reviewer ruling, Prompt AK B2 |
 
 ## App
 
```

### c.3 Not reproduced

- `docs/prompts/PROMPT_AK_ai-followups.md` (58 lines, added in `3fc2e98`):
  the reviewer's own prompt, committed unchanged.
- `docs/SESSION_LOG.md`: under CLAUDE.md's standing exception (it ships with
  this file).

---

## d. Raw output of commands run

### d.1 How this log was produced

From d.2 #1 onward, shell commands went through a logging wrapper (`run.sh`
in this session's scratchpad). It runs the command in the repo root and
appends the command, its complete output and its exit code to `cmdlog.txt`.
d.2 is that file, complete and unchanged, through #18. No output contained a
secret; nothing is redacted.

Two parts of d.2 come from earlier versions of the source, not the final one:

- #6's baseline builds compile `08afb33`'s `ble_bed_service.c`, checked out
  over the AK file and restored by the script.
- #13's diff shows the code comment before the one-sentence edit that came
  after it. The committed text is #16 / c.1.

Files were also read with the editor's read tool (no shell command):

- `docs/MIGRATION_STATUS.md` (whole file)
- `ble_bed_service.c` lines 240-269, 355-384, 500-919, 905-1034, 1155-1199
  and 1174-1183
- `docs/DECISIONS.md` 2815-2902
- `docs/PRE_SHIP.md` 1-60
- `docs/REGRESSION.md` 300-374
- `docs/SESSION_LOG.md` 1716-1768

The wrapper and the scripts it ran (`$S` = the scratchpad):

```sh
#!/bin/sh
# Session command logger: runs "$*" in the repo root, prints and appends command + output to cmdlog.txt
S="C:/Users/adunc/AppData/Local/Temp/claude/a38b8cbb5c09b5e7/1ceaa776-b7e8-4d3b-88e0-6eb2948c02b5/scratchpad"
N=$(( $(cat "$S/n.txt" 2>/dev/null || echo 0) + 1 )); echo $N > "$S/n.txt"
cd "C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller" || exit 99
{ echo "### #$N \$ $*"; sh -c "$*" 2>&1; echo "[exit $?]"; echo; } | tee -a "$S/cmdlog.txt"
```

```sh
#!/bin/sh
# Session build wrapper: Silicon Labs ninja + cmake on PATH, build the default preset.
P=~/.silabs/slt/installs/conan/p
export PATH="$P/ninja1a38fc85adcf7/p:$P/cmakefa35ab0687064/p/bin:$PATH"
S="C:/Users/adunc/AppData/Local/Temp/claude/a38b8cbb5c09b5e7/1ceaa776-b7e8-4d3b-88e0-6eb2948c02b5/scratchpad"
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
whole build output contains "warning" or "error". Every build's output was
under 25 lines, so the tail is the whole output.

```sh
#!/bin/sh
# matrix.sh - Prompt AK A3: baseline (08afb33's ble_bed_service.c) and the change, each with
# BLE_DEBUG_WATCHDOG_LOG at 1 and 0; prints the flag, the build tail, warnings/errors and size. Restores the flag to 1.
S="C:/Users/adunc/AppData/Local/Temp/claude/a38b8cbb5c09b5e7/1ceaa776-b7e8-4d3b-88e0-6eb2948c02b5/scratchpad"
SZ=/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe
setf() { sed -i "s/^#define BLE_DEBUG_WATCHDOG_LOG [01]\$/#define BLE_DEBUG_WATCHDOG_LOG $1/" ble_bed_service.h; }
build() { echo "=== $1"; grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h; sh "$S/build.sh"; echo "build exit $?"; "$SZ" cmake_gcc/build/base/tilt_bed_controller.out; }
cp ble_bed_service.c "$S/ble_bed_service.c.ak"
git show HEAD:ble_bed_service.c > ble_bed_service.c
setf 1; build "BASELINE 08afb33 ble_bed_service.c, BLE_DEBUG_WATCHDOG_LOG 1"
setf 0; build "BASELINE 08afb33 ble_bed_service.c, BLE_DEBUG_WATCHDOG_LOG 0"
cp "$S/ble_bed_service.c.ak" ble_bed_service.c
setf 0; build "PROMPT AK ble_bed_service.c, BLE_DEBUG_WATCHDOG_LOG 0"
setf 1; build "PROMPT AK ble_bed_service.c, BLE_DEBUG_WATCHDOG_LOG 1 (restored)"
echo "=== restored state"; grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h; cmp ble_bed_service.c "$S/ble_bed_service.c.ak" && echo "ble_bed_service.c = Prompt AK version"; git status --short
```

```sh
#!/bin/sh
# fncheck.sh - BLE_DEBUG_WATCHDOG_LOG 0: size of ble_bed_service_tick (nm -S) and its disassembly head, baseline vs Prompt AK. Restores flag 1 and the AK file.
S="C:/Users/adunc/AppData/Local/Temp/claude/a38b8cbb5c09b5e7/1ceaa776-b7e8-4d3b-88e0-6eb2948c02b5/scratchpad"
B=/c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin
OUT=cmake_gcc/build/base/tilt_bed_controller.out
setf() { sed -i "s/^#define BLE_DEBUG_WATCHDOG_LOG [01]\$/#define BLE_DEBUG_WATCHDOG_LOG $1/" ble_bed_service.h; }
show() { echo "=== $1"; sh "$S/build.sh" > /dev/null; echo "build exit $?"; grep -c -i 'warning\|error' "$S/build_out.txt"; "$B/arm-none-eabi-nm.exe" -S $OUT | grep -i ' ble_bed_service_tick$\| service_app_commanding$'; "$B/arm-none-eabi-objdump.exe" -d --no-show-raw-insn $OUT | sed -n '/<ble_bed_service_tick>:/,/^$/p' | head -16; }
cp ble_bed_service.c "$S/ble_bed_service.c.ak"
setf 0
git show HEAD:ble_bed_service.c > ble_bed_service.c; show "BASELINE, flag 0"
cp "$S/ble_bed_service.c.ak" ble_bed_service.c;     show "PROMPT AK, flag 0"
setf 1; sh "$S/build.sh" > /dev/null; echo "restore build exit $?"; "$B/arm-none-eabi-size.exe" $OUT
grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h; cmp ble_bed_service.c "$S/ble_bed_service.c.ak" && echo "ble_bed_service.c = Prompt AK version"; git status --short
```

In `fncheck.sh` the full build output goes to `build_out.txt`. The `grep -c`
line prints how many of its lines contain "warning" or "error" (`0` both
times in #7). `HEAD` in both scripts was `08afb33` when they ran (#6 and #7
come before the commit, #15).

The commit message file `msgA.txt` used in #15 is the message shown in #16.

### d.0 Commands run outside the wrapper

In the order run. `P n` numbers are used in e.

- **P1** `cat docs/prompts/PROMPT_AK_ai-followups.md && date && git status && git diff .projectlinkstore | head -30`
  - the prompt: *[file content, not reproduced - the committed prompt file]*
  - then:
    ```
    Tue Nov  3 16:53:36 PST 2026
    On branch main
    Your branch is up to date with 'origin/main'.

    Changes not staged for commit:
      (use "git add <file>..." to update what will be committed)
      (use "git restore <file>..." to discard changes in working directory)
    	modified:   .projectlinkstore

    Untracked files:
      (use "git add <file>..." to include in what will be committed)
    	docs/prompts/PROMPT_AK_ai-followups.md

    no changes added to commit (use "git add" and/or "git commit -a")
    diff --git a/.projectlinkstore b/.projectlinkstore
    index f0a39fb..723a265 100644
    --- a/.projectlinkstore
    +++ b/.projectlinkstore
    @@ -1 +1 @@
    -{"data":[{"hit":1073741823,"alias":"Visual Studio Code","type":"EXTERNAL_IDE","data":"C:\\Users\\adunc\\Desktop\\_Tilt-Beds\\_03-Software\\tilt_bed_controller"},{"hit":2,"alias":"Software Components","type":"NAVIGATION","data":"softwarecomponents"},{"hit":0,"alias":"app.c","type":"FILE","data":"C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c"},{"hit":0,"alias":"main.c","type":"FILE","data":"C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/main.c"},{"hit":0,"alias":"readme.md","type":"FILE","data":"C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/readme.md"},{"hit":0,"alias":"Bluetooth GATT Configurator","type":"TOOL","data":"bleGattConfigurator"},{"hit":0,"alias":"Post Build Editor","type":"TOOL","data":"postbuildeditor"},{"hit":0,"alias":"Pin Tool","type":"TOOL","data":"pintool"},{"hit":0,"alias":"Memory Editor","type":"TOOL","data":"memoryeditor"},{"hit":0,"alias":"docs.silabs.com","type":"EXTERNAL_URL","data":"https://docs.silabs.com/"}]}
    \ No newline at end of file
    +{"data":[{"hit":1073741823,"alias":"Visual Studio Code","type":"EXTERNAL_IDE","data":"C:\\Users\\adunc\\Desktop\\_Tilt-Beds\\_03-Software\\tilt_bed_controller"},{"hit":4,"alias":"Software Components","type":"NAVIGATION","data":"softwarecomponents"},{"hit":3,"alias":"Bluetooth GATT Configurator","type":"TOOL","data":"bleGattConfigurator"},{"hit":1,"alias":"Pin Tool","type":"TOOL","data":"pintool"},{"hit":0,"alias":"app.c","type":"FILE","data":"C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c"},{"hit":0,"alias":"main.c","type":"FILE","data":"C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/main.c"},{"hit":0,"alias":"readme.md","type":"FILE","data":"C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/readme.md"},{"hit":0,"alias":"Post Build Editor","type":"TOOL","data":"postbuildeditor"},{"hit":0,"alias":"Memory Editor","type":"TOOL","data":"memoryeditor"},{"hit":0,"alias":"docs.silabs.com","type":"EXTERNAL_URL","data":"https://docs.silabs.com/"}]}
    \ No newline at end of file
    ```
- **P2** `git fetch origin 2>&1; git rev-parse HEAD origin/main; ls docs/handoffs/ | tail -5; wc -l docs/handoffs/fw_HANDOFF_2026-11-03_02* docs/MIGRATION_STATUS.md`
  ```
  08afb3385c4b38cd5b0a086f4829668c0c95e943
  08afb3385c4b38cd5b0a086f4829668c0c95e943
  fw_HANDOFF_2026-11-02_01_collision-result-leveled-bit-lights-release.md
  fw_HANDOFF_2026-11-02_02_collision-latch-app-hold.md
  fw_HANDOFF_2026-11-02_03_collision-latch-followups.md
  fw_HANDOFF_2026-11-03_01_stop-latch-and-live-level-report.md
  fw_HANDOFF_2026-11-03_02_latch-refinements-live-tilt.md
    4252 docs/handoffs/fw_HANDOFF_2026-11-03_02_latch-refinements-live-tilt.md
     123 docs/MIGRATION_STATUS.md
    4375 total
  ```
- **P3** `grep -n '^#\|^##\|^###\|^\*\*b\.\|^b\.\|A\.2\|A\.3\|^f\.\|^- f\.' docs/handoffs/fw_HANDOFF_2026-11-03_02_latch-refinements-live-tilt.md | head -120`
  - *[file content, not reproduced]*: 120 numbered lines of the committed AI handoff (its headings, plus `#`-lines inside its code blocks), used to find sections b.A.2, b.A.3, d, e and f.
- **P4** `sed -n '1,58p;120,264p;696,776p;2405,2510p' docs/handoffs/fw_HANDOFF_2026-11-03_02_latch-refinements-live-tilt.md`
  - *[file content, not reproduced]*: AI handoff a., b.A.2, b.A.3, b.F, d.1, d.3 and the start of d.0.
- **P5** `sed -n '4090,4252p' docs/handoffs/fw_HANDOFF_2026-11-03_02_latch-refinements-live-tilt.md`
  - *[file content, not reproduced]*: AI handoff e., f., g.
- **P6** The command that created `run.sh` and `build.sh` (heredocs, no output of their own) and ran the wrapper's #1. Its output is #1 in d.2.
- **P7** The commands that created `matrix.sh` and `fncheck.sh` (heredocs, no output of their own) and ran #6 and #7.
- **P8** `cd "C:/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller" && wc -l docs/SESSION_LOG.md && grep -n '^## ' docs/SESSION_LOG.md | tail -3 && tail -c 200 docs/SESSION_LOG.md | od -c | tail -3`
  ```
  1767 docs/SESSION_LOG.md
  1627:## 2026-11-02 — Prompt AG: collision latch follow-ups (viewer STOP_ALL, refusals latch, trip arms silence)
  1668:## 2026-11-03 — Prompt AH: STOP waits for release (stop latch), bench record of 2026-11-02, live-level report
  1716:## 2026-11-03 — Prompt AI: latch refinements, reset cause, AUTO_LEVEL_SETTLING bit, live idle tilt, self-clearing LEVELED
  0000260   e   l   i   n   e       w   a   s       n   o   t       m   e
  0000300   a   s   u   r   e   d   .  \n
  0000310
  ```
- **P9** The command that created `msgA.txt` (a heredoc, no output of its own) and ran #15.
- **P10** `cat >> docs/SESSION_LOG.md <<'EOF' ... EOF` (this session's entry; its text ships with this file) `; tail -c 100 docs/SESSION_LOG.md | od -c | tail -2; git diff --stat`
  ```
  0000140   n   g   .  \n
  0000144
  warning: in the working copy of 'docs/SESSION_LOG.md', LF will be replaced by CRLF the next time Git touches it
   .projectlinkstore   |  2 +-
   docs/SESSION_LOG.md | 48 ++++++++++++++++++++++++++++++++++++++++++++++++
   2 files changed, 49 insertions(+), 1 deletion(-)
  ```
- **P11** `sh run.sh "date; git log ...; git status; git show 3fc2e98 -- ..." > /dev/null; tail -c 12000 cmdlog.txt | head -c 3000`. This is #16, with its stdout sent to `/dev/null`; #16 is complete in d.2. The `tail | head` printed an excerpt of `cmdlog.txt` (#13's end, #14, the start of #15), all already in d.2.
- **P12** `sed -n '/^### #16 /,$p' cmdlog.txt; wc -l cmdlog.txt`. Printed #16 (in d.2) and `695 .../cmdlog.txt`.

- **P13** The command that assembled this file (this text, then `cmdlog.txt` inside a `~~~~` fence, then sections e-g) and printed its line count and `git status --short`. Run after this file's text was written, so its output is not here.

### d.2 Session command log (the wrapper, numbered, complete)

~~~~text
### #1 $ date; git fetch origin; git rev-parse HEAD origin/main; git status --short; git log --oneline -3
Tue Nov  3 16:53:59 PST 2026
08afb3385c4b38cd5b0a086f4829668c0c95e943
08afb3385c4b38cd5b0a086f4829668c0c95e943
 M .projectlinkstore
?? docs/prompts/PROMPT_AK_ai-followups.md
08afb33 Handoff 2026-11-03 02: Prompt AI (latch refinements, reset cause, bit 5, live idle tilt, self-clearing LEVELED)
05c4c2f REGRESSION items for Prompt AI (part E, docs only)
8996259 LEVELED (bit 4) clears itself when tilted off level while idle (Prompt AI part D, DECISIONS #46 amended)
[exit 0]

### #2 $ grep -n 'move_watchdog_armed\|^static void service_app_commanding\|^static void service_move_watchdog\|^static void move_watchdog_arm\|^static void move_watchdog_cancel\|^static void latch_app_starts\|^static void note_app_stopped_commanding\|^static void note_app_commanding\|^void ble_bed_service_tick\|service_app_commanding();\|STOP_ALL (no app hold\|STOP_ALL latch set\|move_watchdog_expired = \|app_commanding = ' ble_bed_service.c
250:static volatile bool move_watchdog_expired = false;  // set in timer ISR context
251:static bool    move_watchdog_armed = false;
367:static bool     app_commanding = false;
515:  move_watchdog_expired = true; // ISR context - flag only, the tick does the work
518:static void move_watchdog_arm(void)
522:  if (move_watchdog_armed) {
554:  move_watchdog_armed = true;
555:  move_watchdog_expired = false;
561:static void move_watchdog_cancel(void)
564:  if (move_watchdog_armed) {
574:  move_watchdog_armed = false;
575:  move_watchdog_expired = false;
582:static void note_app_commanding(void)
584:  app_commanding = true;
597:static void latch_app_starts(app_start_latch_t reason)
608:static void note_app_stopped_commanding(const char *reason)
610:  app_commanding = false;
629:static void service_app_commanding(void)
641:static void service_move_watchdog(void)
643:  if (!move_watchdog_armed) {
780:      printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
783:      printf("BLE: STOP_ALL (no app hold, no latch)\r\n");
1165:  return move_watchdog_armed;
1168:void ble_bed_service_tick(void)
1174:  service_app_commanding();
1193:  service_app_commanding();
[exit 0]

### #3 $ grep -n 'move_watchdog_arm()\|note_app_commanding()\|latch_app_starts(\|note_app_stopped_commanding(\|move_watchdog_cancel()' ble_bed_service.c; grep -n 'any_collision = ' *.c; grep -n 'ble_bed_service_tick()' *.c; sed -n '30,135p' bed_control.c
262:static uint32_t wd_diag_refresh_tick = 0;         // last move_watchdog_arm()
279:// Latched by move_watchdog_cancel() when it ends an armed hold; printed by
290:// cleared" line, latched by note_app_stopped_commanding() and printed by
342:// note_app_stopped_commanding(). STOP_ALL never clears it.
353:// Set ONLY through latch_app_starts(), which also starts the silence timeout,
581:// latch_app_starts().
590:// same call. The latch is set ONLY here: with note_app_stopped_commanding()
597:static void latch_app_starts(app_start_latch_t reason)
602:  note_app_commanding();
608:static void note_app_stopped_commanding(const char *reason)
636:    note_app_stopped_commanding("silence");
657:    // latch stays as it is. latch_app_starts() restarts the silence
659:    move_watchdog_cancel();
662:    latch_app_starts(APP_LATCH_COLLISION);
700:  move_watchdog_cancel();
703:  note_app_stopped_commanding("watchdog");
754:    // is set (latch_app_starts()), so the second term only states that.
757:    move_watchdog_cancel();
775:      latch_app_starts(APP_LATCH_STOP);
867:  // latch_app_starts() restarts the silence timeout as in the #36 refusal: a
874:      latch_app_starts(APP_LATCH_COLLISION);
877:      latch_app_starts(APP_LATCH_STOP);
892:  // Note note_app_commanding() BEFORE the refusal. A refused repeat still
900:    note_app_commanding();
915:      note_app_commanding();
930:          move_watchdog_cancel();
942:      move_watchdog_arm();
951:      move_watchdog_cancel();
953:      note_app_stopped_commanding("stop");
966:      note_app_commanding();
969:        move_watchdog_cancel();
982:      move_watchdog_arm();
990:      move_watchdog_cancel();
992:      note_app_stopped_commanding("stop");
1180:    move_watchdog_cancel();
1183:    note_app_stopped_commanding("disconnect"); // the app is gone - release its half of the latch
app.c:678:      any_collision = false;
bed_status.c:158:  out->any_collision = any_collision;
motor_current_functions.c:79:bool any_collision = false; // Boolean to indicate a collision has occurred on any motor
motor_current_functions.c:307:          any_collision = true;
motor_current_functions.c:323:          any_collision = true;
motor_current_functions.c:367:            any_collision = true;
app.c:845:  ble_bed_service_tick();
ble_bed_service.c:280:// ble_bed_service_tick() once the stop that ended the hold has been done.
ble_bed_service.c:291:// ble_bed_service_tick() once the stop that cleared it has been done.
ble_bed_service.c:733:// Acts on a Command write, in MAIN-LOOP context, from ble_bed_service_tick().
ble_bed_service.c:1026:// believes it's subscribed and ble_bed_service_tick() early-returns forever.
ble_bed_service.c:1556:          // Flagging due lets the next ble_bed_service_tick() publish from
 */
static bool conflict_inhibit = false;
static bool source_commanding[3] = { false, false, false }; // indexed by bed_source_t

static void note_commanding(bed_source_t src, bool commanding)
{
  if (src == BED_SOURCE_NONE) {
    return;
  }
  source_commanding[src] = commanding;

  // Cessation is the ONLY thing that clears the latch. A refused command must
  // never clear it - an app repeating MOVE_START because the user is still
  // holding is still commanding, and must keep being refused.
  if (conflict_inhibit
      && !source_commanding[BED_SOURCE_SWITCHES]
      && !source_commanding[BED_SOURCE_APP]) {
    conflict_inhibit = false;
  }
}

void bed_note_source_released(bed_source_t src)
{
  note_commanding(src, false);
}

bool bed_conflict_inhibited(void)
{
  return conflict_inhibit;
}

typedef enum {
  ARBITRATE_NORMAL,     // src holds (or acquires) the lock - proceed normally
  ARBITRATE_FORCE_STOP  // a different source holds the lock - force a clean stop instead
} arbitrate_result_t;

/*
 * Releases the lock lazily (if the bed is already idle), then either grants
 * the lock to an idle bed, confirms the calling source already holds it, or
 * signals that a different source holds it. Every verb - including the
 * stop-class ones - runs through this uniformly: a stop request from the
 * source that already holds the lock is just that source's own natural
 * stop; a stop (or any other command) from the other source becomes a full
 * stop of movement, app_stop_movement(), via ARBITRATE_FORCE_STOP (since
 * DECISIONS.md #36; it leaves the under-bed lighting alone), matching
 * DECISIONS.md #12's "any input whatsoever from the other source is
 * interpreted ONLY as an immediate clean STOP" - not a partial, verb-specific
 * stop.
 */
static arbitrate_result_t arbitrate(bed_source_t src)
{
  if (app_bed_is_idle()) {
    active_source = BED_SOURCE_NONE;
  }

  if (active_source == BED_SOURCE_NONE) {
    active_source = src;
    return ARBITRATE_NORMAL;
  }

  if (active_source == src) {
    return ARBITRATE_NORMAL;
  }

  return ARBITRATE_FORCE_STOP;
}

bool bed_move_start(bed_source_t src, bed_target_t target, bed_direction_t dir)
{
  note_commanding(src, true);

  if (conflict_inhibit) {
    return false; // latched - both sources must release first (#36)
  }
  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    conflict_inhibit = true;
    app_stop_movement();
    return false;
  }
  app_move_start(target, dir);
  return true;
}

bool bed_move_stop(bed_source_t src)
{
  note_commanding(src, false); // stops are never inhibited

  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    app_stop_movement();
    return true;
  }
  app_move_stop();
  return true;
}

bool bed_auto_level_start(bed_source_t src)
{
  note_commanding(src, true);

  if (conflict_inhibit) {
    return false; // latched (#36)
  }
  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
    conflict_inhibit = true;
    app_stop_movement();
    return false;
[exit 0]

### #4 $ grep -n 'app_commanding\b\|app_start_latch\b' ble_bed_service.c | grep -v '^ *[0-9]*: *//' ; grep -n -A14 '^bool bed_lights_set' bed_control.c; sed -n '835,850p' app.c
360:static app_start_latch_t app_start_latch = APP_LATCH_NONE;
367:static bool     app_commanding = false;
582:static void note_app_commanding(void)
584:  app_commanding = true;
599:  if (app_start_latch != APP_LATCH_COLLISION) {
600:    app_start_latch = reason;
602:  note_app_commanding();
610:  app_commanding = false;
613:  if (app_start_latch != APP_LATCH_NONE) {
615:    wd_diag_latch_clear_set_reason = (app_start_latch == APP_LATCH_STOP) ? "stop" : "collision";
623:  app_start_latch = APP_LATCH_NONE;
629:static void service_app_commanding(void)
631:  if (!app_commanding) {
755:    bool latch_stop = app_commanding || (app_start_latch != APP_LATCH_NONE);
781:             (app_start_latch == APP_LATCH_COLLISION) ? ", kept as collision" : "");
872:      && (app_start_latch != APP_LATCH_NONE || any_collision)) {
873:    if (app_start_latch == APP_LATCH_COLLISION || any_collision) {
900:    note_app_commanding();
915:      note_app_commanding();
966:      note_app_commanding();
1174:  service_app_commanding();
1193:  service_app_commanding();
156:bool bed_lights_set(bed_source_t src, bool on)
157-{
158-  if (arbitrate(src) == ARBITRATE_FORCE_STOP) {
159-    app_stop_movement();
160-    return false;
161-  }
162-  app_lights_set(on);
163-  return true;
164-}
165-
166-void bed_control_tick(void)
167-{
168-  if (app_bed_is_idle()) {
169-    active_source = BED_SOURCE_NONE;
170-  }
                      auto_level_active || in_settings_mode);

  // LEVELED (Bed Status bit 4) clears itself when the idle tilt has left the
  // level band for long enough (Prompt AI part D). Clear only - bit 4 is set
  // only by a successful auto-level (#46). Idempotent: prints once.
  if (tilt_idle_leveled_exit_due()) {
    app_set_leveled(false);
  }

  bed_control_tick();
  ble_bed_service_tick();

#if BLE_DEBUG_WATCHDOG_LOG
  ble_bed_service_debug_pass_end();
#endif
}
[exit 0]

### #5 $ printf 'CR=%s HEAD_CR=%s\n' $(tr -cd '\r' < ble_bed_service.c | wc -c) $(git show HEAD:ble_bed_service.c | tr -cd '\r' | wc -c); git diff --stat; grep -n 'define BLE_DEBUG_WATCHDOG_LOG\|define BLE_BENCH_WIPE_BONDS' ble_bed_service.h ble_bed_service.c; grep -n 'define BED_BENCH_PROVOKE_RUNAWAY' switch_input.c
CR=0 HEAD_CR=0
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
 .projectlinkstore |  2 +-
 ble_bed_service.c | 13 +++++++++++--
 2 files changed, 12 insertions(+), 3 deletions(-)
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
ble_bed_service.c:70:#define BLE_BENCH_WIPE_BONDS 0
98:#define BED_BENCH_PROVOKE_RUNAWAY 0
[exit 0]

### #6 $ sh 'C:/Users/adunc/AppData/Local/Temp/claude/a38b8cbb5c09b5e7/1ceaa776-b7e8-4d3b-88e0-6eb2948c02b5/scratchpad/matrix.sh'
=== BASELINE 08afb33 ble_bed_service.c, BLE_DEBUG_WATCHDOG_LOG 1
36:#define BLE_DEBUG_WATCHDOG_LOG 1
[1/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[3/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
build exit 0
   text	   data	    bss	    dec	    hex	filename
 248540	    788	  72472	 321800	  4e908	cmake_gcc/build/base/tilt_bed_controller.out
=== BASELINE 08afb33 ble_bed_service.c, BLE_DEBUG_WATCHDOG_LOG 0
36:#define BLE_DEBUG_WATCHDOG_LOG 0
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
build exit 0
   text	   data	    bss	    dec	    hex	filename
 246652	    780	  72480	 319912	  4e1a8	cmake_gcc/build/base/tilt_bed_controller.out
=== PROMPT AK ble_bed_service.c, BLE_DEBUG_WATCHDOG_LOG 0
36:#define BLE_DEBUG_WATCHDOG_LOG 0
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
build exit 0
   text	   data	    bss	    dec	    hex	filename
 246652	    780	  72480	 319912	  4e1a8	cmake_gcc/build/base/tilt_bed_controller.out
=== PROMPT AK ble_bed_service.c, BLE_DEBUG_WATCHDOG_LOG 1 (restored)
36:#define BLE_DEBUG_WATCHDOG_LOG 1
[1/4] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[2/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[3/4] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[4/4] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
build exit 0
   text	   data	    bss	    dec	    hex	filename
 248548	    788	  72472	 321808	  4e910	cmake_gcc/build/base/tilt_bed_controller.out
=== restored state
36:#define BLE_DEBUG_WATCHDOG_LOG 1
ble_bed_service.c = Prompt AK version
 M .projectlinkstore
 M ble_bed_service.c
?? docs/prompts/PROMPT_AK_ai-followups.md
[exit 0]

### #7 $ sh 'C:/Users/adunc/AppData/Local/Temp/claude/a38b8cbb5c09b5e7/1ceaa776-b7e8-4d3b-88e0-6eb2948c02b5/scratchpad/fncheck.sh'
=== BASELINE, flag 0
build exit 0
0
0000ab24 000003f8 T ble_bed_service_tick
0000aacc 00000028 t service_app_commanding
0000ab24 <ble_bed_service_tick>:
    ab24:	stmdb	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
    ab28:	sub	sp, #100	@ 0x64
    ab2a:	bl	aacc <service_app_commanding>
    ab2e:	ldr	r3, [pc, #584]	@ (ad78 <ble_bed_service_tick+0x254>)
    ab30:	ldrb	r2, [r3, #0]
    ab32:	cbz	r2, ab4c <ble_bed_service_tick+0x28>
    ab34:	movs	r4, #0
    ab36:	strb	r4, [r3, #0]
    ab38:	bl	a98c <move_watchdog_cancel>
    ab3c:	ldr	r3, [pc, #572]	@ (ad7c <ble_bed_service_tick+0x258>)
    ab3e:	strb	r4, [r3, #0]
    ab40:	ldr	r3, [pc, #572]	@ (ad80 <ble_bed_service_tick+0x25c>)
    ab42:	strb	r4, [r3, #0]
    ab44:	bl	aab0 <note_app_stopped_commanding.isra.0>
    ab48:	bl	a458 <app_stop_all>
=== PROMPT AK, flag 0
build exit 0
0
0000ab24 00000400 T ble_bed_service_tick
0000aacc 00000028 t service_app_commanding
0000ab24 <ble_bed_service_tick>:
    ab24:	stmdb	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
    ab28:	ldr	r3, [pc, #596]	@ (ad80 <ble_bed_service_tick+0x25c>)
    ab2a:	sub	sp, #100	@ 0x64
    ab2c:	ldrb	r3, [r3, #0]
    ab2e:	cbnz	r3, ab34 <ble_bed_service_tick+0x10>
    ab30:	bl	aacc <service_app_commanding>
    ab34:	ldr	r3, [pc, #588]	@ (ad84 <ble_bed_service_tick+0x260>)
    ab36:	ldrb	r2, [r3, #0]
    ab38:	cbz	r2, ab52 <ble_bed_service_tick+0x2e>
    ab3a:	movs	r4, #0
    ab3c:	strb	r4, [r3, #0]
    ab3e:	bl	a98c <move_watchdog_cancel>
    ab42:	ldr	r3, [pc, #580]	@ (ad88 <ble_bed_service_tick+0x264>)
    ab44:	strb	r4, [r3, #0]
    ab46:	ldr	r3, [pc, #580]	@ (ad8c <ble_bed_service_tick+0x268>)
restore build exit 0
   text	   data	    bss	    dec	    hex	filename
 248548	    788	  72472	 321808	  4e910	cmake_gcc/build/base/tilt_bed_controller.out
36:#define BLE_DEBUG_WATCHDOG_LOG 1
ble_bed_service.c = Prompt AK version
 M .projectlinkstore
 M ble_bed_service.c
?? docs/prompts/PROMPT_AK_ai-followups.md
[exit 0]

### #8 $ which gcc; cd test && gcc -std=c99 -Wall -I.. -o 'C:/Users/adunc/AppData/Local/Temp/claude/a38b8cbb5c09b5e7/1ceaa776-b7e8-4d3b-88e0-6eb2948c02b5/scratchpad/bed_control_test.exe' bed_control_test.c ../bed_control.c && 'C:/Users/adunc/AppData/Local/Temp/claude/a38b8cbb5c09b5e7/1ceaa776-b7e8-4d3b-88e0-6eb2948c02b5/scratchpad/bed_control_test.exe' | tail -4; cd ..; git diff --stat HEAD -- config autogen bed_control.c '*.btconf'; grep -n 'define BED_BENCH_PROVOKE_RUNAWAY\|define BLE_BENCH_WIPE_BONDS\|define BLE_DEBUG_WATCHDOG_LOG' switch_input.c ble_bed_service.c ble_bed_service.h
/c/MinGW/bin/gcc
PASS: scenario 6b: bed_move_stop still works while inhibited
PASS: scenario 6c: lights are not motion and are not inhibited

0 check(s) failed
switch_input.c:98:#define BED_BENCH_PROVOKE_RUNAWAY 0
ble_bed_service.c:70:#define BLE_BENCH_WIPE_BONDS 0
ble_bed_service.h:36:#define BLE_DEBUG_WATCHDOG_LOG 1
[exit 0]

### #9 $ grep -n '^## \|Amended by Prompt' docs/DECISIONS.md | tail -14; grep -n '^| F\|^## \|^|---\|^| #\|^| ID' docs/PRE_SHIP.md | tail -20; wc -l docs/DECISIONS.md docs/PRE_SHIP.md
1652:## 40. The fail-safe reads motor hardware; double press = two switches down; out-of-band reads must not swallow events
1730:## 41. One switch reader
1843:## 42. Pairing window: new bonds only while settings mode is active (implements #19)
1937:## 43. Bond store full: overwrite the least recently used bond; the firmware disconnects a phone whose pairing it refused
2061:## 44. STOP in settings mode cancels any hold in progress and stays in settings mode
2119:## 45. During an app hold the CPU stays in EM0
2267:## 46. Bed Status flag bit 4 = LEVELED; "Bed is level" prints on every successful auto-level end
2367:### Amended by Prompt AI (2026-11-03)
2410:## 47. BED LIGHTS toggles the lights on the release of a short press
2503:## 48. A collision during an app hold latches the app path: COLLISION_STOP (0x06) until the app releases
2636:### Amended by Prompt AG (2026-11-02)
2727:## 49. STOP waits for release: a STOP_ALL latches app motion starts (STOP_LATCHED 0x07) until the app releases
2820:### Amended by Prompt AI (2026-11-03)
2899:## 50. Live idle tilt: a 10 Hz idle sampler with its own millisecond filter, 5 Hz idle notify, `tilt_fresh` live
46:| F22 | The "continuous" re-baseline never reaches the trip test | It writes `dynamic_collision_current_threshold[]`, which nothing reads; the trip uses `max_current_threshold_set[]`; and it counts on motor 3's counter for every motor (`motor_current_functions.c:206-231`) | Claude (design), Claude Code | Decide: make it work (with F15/F20) or remove it; change; REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 4; reviewer ruling, Prompt AE part E |
47:| F23 | Auto-level uses the raise margin for the lowering motors | `is_raising` is forced true for auto-level (`bed_actions.c:170`; `auto_level.c:771`), so the two lowering motors are judged against the raise margin | Claude decides, Claude Code changes | Decision (keep with a recorded reason, or per-direction margins in auto-level); then REGRESSION §5 | Prompt AC handoff B1(a), QUESTIONS 5; reviewer ruling, Prompt AE part E |
48:| F24 | Unconditional console prints to review before release | With every flag at 0 the firmware still prints `BLE SM: configure/...` (`ble_bed_service.c:1089, 1101, 1109`), `connection_parameters` (`:1270`), `Max Current for Motor` (`motor_current_functions.c:198`), `Once`, the auto-level step lines, and (added 2026-11-03, Prompt AI A4) `Reset cause: 0x... (...)` at boot (`app.c:445-456`; only `BED_CAPTURE_MOTOR_CURRENTS` suppresses it) | Claude decides, Claude Code changes | List each print with keep / flag / remove; change; check a release-build log | Prompt AC handoff QUESTIONS 12; reviewer ruling, Prompt AE part E |
49:| F25 | Hold 9 (2026-11-01 bench): the HEAD pair apparently did not run during PITCH levelling | `prompt_aa_bench.log` lines 201-204 show outputs 0 and 3 at 1 and 12 mA while FOOT ran; cause not established (an inferred limit refusing the pair, `dual_motor_control.c:353, 361`, is one candidate) | Andrew + Claude | A bench look with the limit prints enabled: repeat PITCH levelling from a HEAD-high start and record which pairs run and any limit lines | Prompt AC handoff B1(d), QUESTIONS 6; reviewer ruling, Prompt AE part E |
50:| F26 | What can cut the collision reverse short: (1) the runaway fail-safe, after an app collision and after a physical collision once the button is released; (2) any app release during the reverse (MOVE_STOP / AUTO_LEVEL_STOP / STOP_ALL) | (1) During the reverse both app motion flags are false and no switch is down, so `motion_failsafe_tick()` de-energises it after 3 samples of 50 ms (`switch_input.c:804-896`) and prints `FAILSAFE: ...`. (2) The app's stop verbs call `app_apply_stop_outputs()` / `app_stop_all()`, which turn every motor off. The reverse is meant to relieve pressure on the obstruction. **Observed 2026-11-02:** `FAILSAFE: motors energised with nothing commanding - stopped` after an app collision (`round3_bench_2026-11-02.log` line 323, after the trip at 313-322); the log has no timestamps, so how long the reverse ran is not known | Andrew + Claude | Bench: the length of the reverse (5.A1). Then a design ruling covering both (accept, or e.g. the fail-safe stands down while `any_collision` is set, and app stops do not cut the reverse) | Prompt AF handoff b.11.1, QUESTIONS f.3; Prompt AG C4; Prompt AG handoff f.4 folded in by the reviewer (Prompt AH B4, B7) |
51:| F27 | A BED LIGHTS press stops an app move but does not latch the #36 inhibit | Lights are not motion and never latch (`bed_control.c:153-164`), so the app's next repeat can restart the move briefly while BED LIGHTS is held | Claude | First: the bench record REGRESSION 8.d; then a ruling | Prompt AE handoff QUESTIONS 6; Prompt AG C4 |
52:| F28 | A collision flagged during a BED LIGHTS hold toggles `bedlights_on` at the release without writing the output | Not expected to happen (no motor runs during a BED LIGHTS hold), but the flag and the amber/lighting output would then disagree | Claude | Review before release | Prompt AE handoff QUESTIONS 8; Prompt AG C4 |
53:| F29 | `TILT_IDLE_DIAG` 1 -> 0 (`auto_level.c`) | Bench print `TILT idle: N samples in T ms, roll ... pitch ...` every 10 s while the idle tilt sampler runs | Claude Code | Change to 0, build, check the `TILT idle` lines are gone. First use the lines to set `TILT_IDLE_TAU_MS` and the LEVELED exit band | Prompt AI C6; DECISIONS #50 |
54:| F30 | A phone that stays subscribed to Bed Status with the app in the background keeps the 10 Hz idle sampler (and the 5 Hz notify) running | The firmware samples whenever a connection is subscribed; it cannot tell a backgrounded app. Costs the subscribed-idle current of F11 for as long as the phone stays connected | Claude Code (app) | The app unsubscribes from Bed Status when backgrounded (app prompt); the firmware needs nothing now. Bench: background the app, check the `TILT idle` lines stop | Prompt AI C5; DECISIONS #50 risk 5 |
56:## App
60:| # | Item | Why (one line) | Owner | How it is closed | Source |
61:|---|---|---|---|---|---|
71:## Hardware / production
73:| # | Item | Why (one line) | Owner | How it is closed | Source |
74:|---|---|---|---|---|---|
79:## Business / legal
81:| # | Item | Why (one line) | Owner | How it is closed | Source |
82:|---|---|---|---|---|---|
86:## Also recorded in the repo - not yet marked pre-ship (reviewer to decide)
92:|---|---|
  3031 docs/DECISIONS.md
    98 docs/PRE_SHIP.md
  3129 total
[exit 0]

### #10 $ grep -n '^void ble_bed_service_tick\|if (!move_watchdog_armed) {\|^    service_app_commanding();\|^  service_app_commanding();\|^  process_pending_command();\|^  service_move_watchdog();\|STOP_ALL (no app hold\|STOP_ALL latch set\|bool latch_stop = ' ble_bed_service.c; grep -n 'sl_hal_emu_\|^#include "sl_hal_emu.h"\|Reset cause' app.c; grep -n -- '-Os' cmake_gcc/tilt_bed_controller.cmake; grep -c 'sl_hal_emu' cmake_gcc/tilt_bed_controller.cmake
643:  if (!move_watchdog_armed) {
755:    bool latch_stop = app_commanding || (app_start_latch != APP_LATCH_NONE);
780:      printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
783:      printf("BLE: STOP_ALL (no app hold, no latch) (conn=%u)\r\n", conn);
1168:void ble_bed_service_tick(void)
1181:  if (!move_watchdog_armed) {
1182:    service_app_commanding();
1200:  process_pending_command();
1201:  service_move_watchdog();
1202:  service_app_commanding();
47:#include "sl_hal_emu.h"   // sl_hal_emu_get_reset_cause() / sl_hal_emu_clear_reset_cause()
386:// Reset cause at boot (Prompt AI A4). EMU->RSTCAUSE bits for the EFR32BG22,
412:  printf("Reset cause: 0x%08lX (", (unsigned long)cause);
450:  uint32_t reset_cause = sl_hal_emu_get_reset_cause();
451:  sl_hal_emu_clear_reset_cause();
365:    $<$<COMPILE_LANGUAGE:C>:-Os>
381:    $<$<COMPILE_LANGUAGE:CXX>:-Os>
0
[exit 1]

### #11 $ sed -n '1203,1245p' ble_bed_service.c; grep -n -A25 '^void app_stop_all' app.c bed_actions.c | grep -n 'printf\|^.*app_stop_all\|^.*}' | head; grep -n 'printf' bed_control.c | head

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
  // The app start latch (#48, #49) was released above; the stop that released
  // it is done.
  if (wd_diag_latch_clear_pending) {
    wd_diag_latch_clear_pending = false;
    printf("BLE: start latch (%s) cleared (%s), %u repeats refused\r\n",
           wd_diag_latch_clear_set_reason, wd_diag_latch_clear_reason,
           (unsigned)wd_diag_latch_clear_refused);
  }
#endif

  if (status_subscriber_count == 0) {
    return; // #24: no subscribers - no notify traffic, no accel-sampling gate to feed yet
  }

  bed_status_t status;
  bed_status_get(&status);

  uint8_t packed[gattdb_bed_status_len];
  ble_bed_service_encode_status(&status, status.tilt_fresh, packed); // tilt_fresh: Prompt AI C4

  // Cadence (BLE_GATT_DESIGN.md #6): 10 Hz moving; 5 Hz idle while the tilt
  // is live - the idle sampler, or a burst outside settings mode (Prompt AI
  // C3); 1 Hz otherwise. A change of cadence publishes at once, so the new
  // rate starts now rather than after a 1 s timer still running from before
  // (e.g. after an app MOVE_STOP, whose settle burst starts a pass later).
  bool moving = (status.command == BED_COMMAND_MOVE) || (status.command == BED_COMMAND_AUTO_LEVEL);
  bool idle_live = tilt_idle_active()
                   || (tilt_telemetry_burst_active() && !app_in_settings_mode());
  uint32_t interval_ms = moving ? STATUS_NOTIFY_MOVING_MS
                       : (idle_live ? STATUS_NOTIFY_IDLE_LIVE_MS : STATUS_NOTIFY_IDLE_MS);

  if (status_notify_due || status_flags_changed(packed, last_sent_status)
1:bed_actions.c:264:void app_stop_all(void)
5:bed_actions.c-268-  } else if (active_switch != OFF) {
7:bed_actions.c-270-  }
11:bed_actions.c-274-  }
18:bed_actions.c-281-  }
[exit 0]

### #12 $ grep -rn 'no app hold, no latch' --include=*.md docs | grep -v 'docs/handoffs/\|docs/prompts/\|SESSION_LOG'; grep -rn 'top of .ble_bed_service_tick\|judged first' docs/BLE_GATT_DESIGN.md docs/REGRESSION.md
docs/DECISIONS.md:2886:   `BLE: STOP_ALL (no app hold, no latch)` (same flag, `BLE_DEBUG_WATCHDOG_LOG`).
docs/REGRESSION.md:319:      (a): `BLE: STOP_ALL (no app hold, no latch)`; the press **moves
docs/REGRESSION.md:324:      MOVE_STOP reached the bed, `no app hold, no latch`; if the app sends no
docs/REGRESSION.md:358:      App: 0x00; `BLE: STOP_ALL (no app hold, no latch)` (Prompt AI A3 - the
[exit 1]

### #13 $ for f in ble_bed_service.c docs/DECISIONS.md docs/PRE_SHIP.md; do printf '%s CR=%s HEAD_CR=%s\n' $f $(tr -cd '\r' < $f | wc -c) $(git show HEAD:$f | tr -cd '\r' | wc -c); done; git diff --stat; git diff ble_bed_service.c
ble_bed_service.c CR=0 HEAD_CR=0
docs/DECISIONS.md CR=0 HEAD_CR=0
docs/PRE_SHIP.md CR=0 HEAD_CR=0
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/PRE_SHIP.md', LF will be replaced by CRLF the next time Git touches it
 .projectlinkstore |  2 +-
 ble_bed_service.c | 13 +++++++++++--
 docs/DECISIONS.md | 24 ++++++++++++++++++++++++
 docs/PRE_SHIP.md  |  1 +
 4 files changed, 37 insertions(+), 3 deletions(-)
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
diff --git a/ble_bed_service.c b/ble_bed_service.c
index e8dbde0..777f24e 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -780,7 +780,7 @@ static void process_pending_command(void)
       printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
              (app_start_latch == APP_LATCH_COLLISION) ? ", kept as collision" : "");
     } else {
-      printf("BLE: STOP_ALL (no app hold, no latch)\r\n");
+      printf("BLE: STOP_ALL (no app hold, no latch) (conn=%u)\r\n", conn);
     }
 #endif
   }
@@ -1171,7 +1171,16 @@ void ble_bed_service_tick(void)
   // since the app's last start" is decided at the moment the next command is
   // processed - not only if some earlier pass happened to run after the
   // deadline. Also run after the commands below, as before.
-  service_app_commanding();
+  //
+  // Not while an app hold is armed (Prompt AK A1): the watchdog is the
+  // MOVE_WATCHDOG_MS judge then, and does the release and the stop itself.
+  // Judging silence here first would let a late repeat of that hold continue
+  // the move with the app already released in bed_control (#36). No start
+  // latch is set while the watchdog is armed, so latched releases are still
+  // judged before the next command.
+  if (!move_watchdog_armed) {
+    service_app_commanding();
+  }
 
   // Control link dropped mid-command (BLE_GATT_DESIGN.md #5). Deferred out
   // of the disconnect callback; done here, first, before anything else.
[exit 0]

### #14 $ grep -n 'define BLE_DEBUG_WATCHDOG_LOG' ble_bed_service.h; sh 'C:/Users/adunc/AppData/Local/Temp/claude/a38b8cbb5c09b5e7/1ceaa776-b7e8-4d3b-88e0-6eb2948c02b5/scratchpad/build.sh' && /c/Users/adunc/.silabs/slt/installs/conan/p/gcc-a999d2e027337f/p/bin/arm-none-eabi-size.exe cmake_gcc/build/base/tilt_bed_controller.out; tr -cd '\r' < ble_bed_service.c | wc -c
36:#define BLE_DEBUG_WATCHDOG_LOG 1
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/ble_bed_service.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
exit 0
--- warnings/errors (grep -n -i 'warning\|error' over the whole output):
   text	   data	    bss	    dec	    hex	filename
 248548	    788	  72472	 321808	  4e910	cmake_gcc/build/base/tilt_bed_controller.out
0
[exit 0]

### #15 $ git add ble_bed_service.c docs/DECISIONS.md docs/PRE_SHIP.md docs/prompts/PROMPT_AK_ai-followups.md && git status --short && git commit -q -F 'C:/Users/adunc/AppData/Local/Temp/claude/a38b8cbb5c09b5e7/1ceaa776-b7e8-4d3b-88e0-6eb2948c02b5/scratchpad/msgA.txt' && git log --oneline -2 && git show --stat HEAD | tail -6
warning: in the working copy of 'ble_bed_service.c', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/DECISIONS.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/PRE_SHIP.md', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/prompts/PROMPT_AK_ai-followups.md', LF will be replaced by CRLF the next time Git touches it
 M .projectlinkstore
M  ble_bed_service.c
M  docs/DECISIONS.md
M  docs/PRE_SHIP.md
A  docs/prompts/PROMPT_AK_ai-followups.md
3fc2e98 Top-of-tick silence check only when no app hold is armed; conn= on the no-latch STOP_ALL print (Prompt AK)
08afb33 Handoff 2026-11-03 02: Prompt AI (latch refinements, reset cause, bit 5, live idle tilt, self-clearing LEVELED)

 ble_bed_service.c                      | 13 ++++++--
 docs/DECISIONS.md                      | 24 ++++++++++++++
 docs/PRE_SHIP.md                       |  1 +
 docs/prompts/PROMPT_AK_ai-followups.md | 58 ++++++++++++++++++++++++++++++++++
 4 files changed, 94 insertions(+), 2 deletions(-)
[exit 0]

### #16 $ date; git log --date=short --format='%h %ad %s' -3; ls docs/handoffs | grep 2026-11-03; git status; git show 3fc2e98 -- ble_bed_service.c docs/DECISIONS.md docs/PRE_SHIP.md
Tue Nov  3 17:00:10 PST 2026
3fc2e98 2026-11-03 Top-of-tick silence check only when no app hold is armed; conn= on the no-latch STOP_ALL print (Prompt AK)
08afb33 2026-11-03 Handoff 2026-11-03 02: Prompt AI (latch refinements, reset cause, bit 5, live idle tilt, self-clearing LEVELED)
05c4c2f 2026-11-03 REGRESSION items for Prompt AI (part E, docs only)
fw_HANDOFF_2026-11-03_01_stop-latch-and-live-level-report.md
fw_HANDOFF_2026-11-03_02_latch-refinements-live-tilt.md
On branch main
Your branch is ahead of 'origin/main' by 1 commit.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore
	modified:   docs/SESSION_LOG.md

no changes added to commit (use "git add" and/or "git commit -a")
commit 3fc2e98be93db7bc604bfad2268bb92c8df94c39
Author: Andrew Duncan <36340909+aduncan028@users.noreply.github.com>
Date:   Tue Nov 3 16:59:37 2026 -0800

    Top-of-tick silence check only when no app hold is armed; conn= on the no-latch STOP_ALL print (Prompt AK)
    
    A1 (Prompt AI f.1): ble_bed_service_tick() now begins
    `if (!move_watchdog_armed) service_app_commanding();`. While a hold is
    armed the dead-man watchdog is the 300 ms judge; judging silence first let a
    late repeat of an accepted hold continue the move with the app already
    released in bed_control (#36). The later call is unchanged.
    A2 (Prompt AI f.9): `BLE: STOP_ALL (no app hold, no latch) (conn=N)`.
    B1: DECISIONS #49 "Amended by Prompt AK". B2: PRE_SHIP F31 (reset-cause print
    relies on GCC inlining sl_hal_emu functions at -Os).
    
    Builds clean with BLE_DEBUG_WATCHDOG_LOG 1 and 0 (restored to 1); host test
    0 failed. Not run on the bench.
    
    Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

diff --git a/ble_bed_service.c b/ble_bed_service.c
index e8dbde0..2e6cf36 100644
--- a/ble_bed_service.c
+++ b/ble_bed_service.c
@@ -780,7 +780,7 @@ static void process_pending_command(void)
       printf("BLE: STOP_ALL latch set (conn=%u)%s\r\n", conn,
              (app_start_latch == APP_LATCH_COLLISION) ? ", kept as collision" : "");
     } else {
-      printf("BLE: STOP_ALL (no app hold, no latch)\r\n");
+      printf("BLE: STOP_ALL (no app hold, no latch) (conn=%u)\r\n", conn);
     }
 #endif
   }
@@ -1171,7 +1171,16 @@ void ble_bed_service_tick(void)
   // since the app's last start" is decided at the moment the next command is
   // processed - not only if some earlier pass happened to run after the
   // deadline. Also run after the commands below, as before.
-  service_app_commanding();
+  //
+  // Not while an app hold is armed (Prompt AK A1): the watchdog is the
+  // MOVE_WATCHDOG_MS judge then, and does the release and the stop itself.
+  // Judging silence here first would let a late repeat of that hold continue
+  // the move with the app already released in bed_control (#36). At the start
+  // of a tick a set start latch means the watchdog is not armed, so latched
+  // releases are still judged before the next command.
+  if (!move_watchdog_armed) {
+    service_app_commanding();
+  }
 
   // Control link dropped mid-command (BLE_GATT_DESIGN.md #5). Deferred out
   // of the disconnect callback; done here, first, before anything else.
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 74f457f..65469d7 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -2896,6 +2896,30 @@ change.
 host test unchanged. **Not run on the bench.** REGRESSION §5b updated.
 What each AH A6 case does now is in the Prompt AI handoff.
 
+### Amended by Prompt AK (2026-11-03)
+
+Reviewer's ruling on the Prompt AI handoff QUESTIONS f.1: the top-of-tick
+silence check (Prompt AI item 2 above) now runs only when no app hold is armed
+- `if (!move_watchdog_armed) service_app_commanding();` (`ble_bed_service.c:1181-1183`);
+the call after `service_move_watchdog()` is unchanged (`:1202`). While a hold
+is armed the dead-man watchdog is the 300 ms judge and does the release and
+the stop itself; judging silence first is what opened Prompt AI's "one new
+case, narrow" (a late repeat of an accepted hold continuing the move with
+`source_commanding[APP]` already false in bed_control). That case is closed:
+the repeat is processed with the app still commanding, as before Prompt AI.
+Prompt AI's purpose is kept: at the start of every tick a set start latch
+means the watchdog is not armed, so latched releases are still judged before
+the next command. Other cases that change are in the same pass only (the
+first pass after the deadline of an armed hold): a STOP_ALL there latches
+(STOP) instead of not latching; a start there while the #36 inhibit is set
+from an earlier switch conflict is refused 0x01 and the watchdog stop follows,
+instead of starting at once; a collision there keeps the app's half of #36
+until the collision latch releases. The full list, and a narrower window that
+the unchanged later call still leaves, are in the Prompt AK handoff. Also (AK
+A2): the debug line is now `BLE: STOP_ALL (no app hold, no latch) (conn=N)`.
+**Status:** builds clean with `BLE_DEBUG_WATCHDOG_LOG` 1 and 0; host test
+unchanged. **Not run on the bench.**
+
 ## 50. Live idle tilt: a 10 Hz idle sampler with its own millisecond filter, 5 Hz idle notify, `tilt_fresh` live
 
 Date: 2026-11-03. Reviewer's Prompt AI part C (Option B of the Prompt AH
diff --git a/docs/PRE_SHIP.md b/docs/PRE_SHIP.md
index aead975..c889fa1 100644
--- a/docs/PRE_SHIP.md
+++ b/docs/PRE_SHIP.md
@@ -52,6 +52,7 @@ File:line references are to the firmware at `6796e6b` unless marked.
 | F28 | A collision flagged during a BED LIGHTS hold toggles `bedlights_on` at the release without writing the output | Not expected to happen (no motor runs during a BED LIGHTS hold), but the flag and the amber/lighting output would then disagree | Claude | Review before release | Prompt AE handoff QUESTIONS 8; Prompt AG C4 |
 | F29 | `TILT_IDLE_DIAG` 1 -> 0 (`auto_level.c`) | Bench print `TILT idle: N samples in T ms, roll ... pitch ...` every 10 s while the idle tilt sampler runs | Claude Code | Change to 0, build, check the `TILT idle` lines are gone. First use the lines to set `TILT_IDLE_TAU_MS` and the LEVELED exit band | Prompt AI C6; DECISIONS #50 |
 | F30 | A phone that stays subscribed to Bed Status with the app in the background keeps the 10 Hz idle sampler (and the 5 Hz notify) running | The firmware samples whenever a connection is subscribed; it cannot tell a backgrounded app. Costs the subscribed-idle current of F11 for as long as the phone stays connected | Claude Code (app) | The app unsubscribes from Bed Status when backgrounded (app prompt); the firmware needs nothing now. Bench: background the app, check the `TILT idle` lines stop | Prompt AI C5; DECISIONS #50 risk 5 |
+| F31 | The boot reset-cause print depends on GCC inlining `sl_hal_emu` functions at `-Os` | `app.c:450-451` (lines at `08afb33`) calls `sl_hal_emu_get_reset_cause()` / `sl_hal_emu_clear_reset_cause()` (inline in `sl_hal_emu.h`); `sl_hal_emu.c` is not in the build, so at a lower optimisation level (`cmake_gcc/tilt_bed_controller.cmake:365` sets `-Os`) the link fails with an undefined reference | Andrew + Claude | Add the proper component in Simplicity Studio (one that brings in `sl_hal_emu.c`) before release, or confirm the build settings are frozen | Prompt AI handoff b.A.4, QUESTIONS f.7; reviewer ruling, Prompt AK B2 |
 
 ## App
 
[exit 0]

### #17 $ grep -n 'printf("BLE SEC\|printf("BLE: conn\|printf("BLE: role\|role=\|CONTROL\\|VIEWER' ble_bed_service.c | grep printf | head; grep -n 'define BLE_DEBUG_SECURITY_LOG' ble_bed_service.c; grep -n 'if (disconnect_stop_pending)\|note_app_stopped_commanding("disconnect")' ble_bed_service.c; grep -n 'stop_all_conn = \|uint8_t conn = stop_all_conn' ble_bed_service.c
1332:      printf("BLE: connection_opened conn=%u bonding=0x%02x\r\n",
1353:      printf("BLE: connection_closed conn=%u reason=0x%04x\r\n",
1452:      printf("BLE: connection_parameters conn=%u interval=%u latency=%u timeout=%u security_mode=%u\r\n",
1456:      printf("BLE: connection_parameters conn=%u interval=%u security_mode=%u\r\n",
53:#define BLE_DEBUG_SECURITY_LOG 1
1187:  if (disconnect_stop_pending) {
1192:    note_app_stopped_commanding("disconnect"); // the app is gone - release its half of the latch
240:static volatile uint8_t stop_all_conn = SLOT_EMPTY;
748:    uint8_t conn = stop_all_conn;
1512:        stop_all_conn = av->connection;
[exit 0]

### #18 $ grep -n 'control_connection = \|viewer_connection = ' ble_bed_service.c; grep -n -B3 -A12 '^static bool assign_role_if_needed' ble_bed_service.c | head -30
198:static uint8_t control_connection = SLOT_EMPTY;
458:    control_connection = connection;
461:    viewer_connection = connection;
1079:    control_connection = SLOT_EMPTY;
1087:      control_connection = viewer_connection;
1089:      viewer_connection = SLOT_EMPTY;
1098:    viewer_connection = SLOT_EMPTY;
452-// just assigned the role (either branch taken) - callers use this to seed
453-// CCCD state exactly once per connection, right after the role that seed
454-// depends on (control vs. viewer) actually exists.
455:static bool assign_role_if_needed(uint8_t connection)
456-{
457-  if (control_connection == SLOT_EMPTY) {
458-    control_connection = connection;
459-    return true;
460-  } else if (viewer_connection == SLOT_EMPTY && connection != control_connection) {
461-    viewer_connection = connection;
462-    return true;
463-  }
464-  return false;
465-}
466-
467-static void update_status_subscriber_count(void)
[exit 0]

~~~~

---

## e. VERIFIED vs ASSERTED

Nothing in this session ran on a bed. VERIFIED here means a command's output
(section d) or a line of code that was read; it never means "observed on
hardware". `#n` = d.2, `P n` = d.0.

### e.1 VERIFIED, with evidence

| # | Claim | Evidence |
|---|---|---|
| V1 | Start state as the prompt requires: HEAD `08afb33` = `origin/main` after `git fetch`; `.projectlinkstore` modified, the prompt untracked, nothing else | P1, P2, #1 |
| V2 | A1 is in the code: `if (!move_watchdog_armed) { service_app_commanding(); }` at `ble_bed_service.c:1181-1183`; the later call at `:1202` is unchanged; `process_pending_command()` `:1200`, `service_move_watchdog()` `:1201` | #10, #16 (c.1) |
| V3 | A2 is in the code: `BLE: STOP_ALL (no app hold, no latch) (conn=%u)` with `conn` at `:783`; `conn = stop_all_conn` at `:748` | #10, #17, c.1 |
| V4 | Every build exit 0 with no warning/error line: baseline and AK, each at flag 1 and 0; AK after the comment edit | #6, #7 (`grep -c` = 0), #14 |
| V5 | Sizes as in b.4: AK at flag 1 is 248,548 / 788 / 72,472 (+8 B text against the baseline's 248,540); at flag 0 246,652 / 780 / 72,480 (+0 B) | #6, #14 |
| V6 | At flag 0 the guard is compiled in: `ble_bed_service_tick` 0x3f8 -> 0x400 B; AK disassembly `ldrb` of the flag then `cbnz` over `bl service_app_commanding`; baseline `bl service_app_commanding` unconditionally | #7 |
| V7 | Flags at the end: `BLE_DEBUG_WATCHDOG_LOG` 1 (`ble_bed_service.h:36`), `BLE_BENCH_WIPE_BONDS` 0 (`ble_bed_service.c:70`), `BED_BENCH_PROVOKE_RUNAWAY` 0 (`switch_input.c:98`); the AK source restored after each flag swap (`cmp`) | #6, #7, #8, #14 |
| V8 | Host test `0 check(s) failed`; no `config/`, `autogen/`, `bed_control.c` or `.btconf` change (empty `git diff --stat`) | #8 |
| V9 | No line-ending change: `ble_bed_service.c`, `DECISIONS.md` and `PRE_SHIP.md` hold 0 CR, as at HEAD | #5, #13 |
| V10 | The code facts F1-F11 of b.1.2: every `move_watchdog_arm()` (`:942`, `:982`) follows `note_app_commanding()` in the same case (`:915`, `:966`); other `note_app_commanding()` callers `:602`, `:900`; latch set only via `latch_app_starts()` at `:662`, `:775`, `:874`, `:877`; cancels at `:659`, `:757`; `any_collision` cleared only at `app.c:678`; the BLE tick called from `app.c:845`; `bed_lights_set()` does not touch commanding or the inhibit; bed_control's `note_commanding()` / `bed_move_start()` as quoted | #3, #4; `ble_bed_service.c` read 500-1034 |
| V11 | The print formats in b.3 (`hold end ...`, `start latch (%s) cleared (%s), %u repeats refused`, `STOP_ALL latch set (conn=%u)%s`) | #11, #2 (`:780`) |
| V12 | Roles: the first connection to take a role is the control connection (`assign_role_if_needed()`, `:455-465`); `BLE: connection_opened conn=%u ...` printed at `:1332` | #17, #18 |
| V13 | The commit `3fc2e98` holds exactly `ble_bed_service.c`, `docs/DECISIONS.md`, `docs/PRE_SHIP.md`, `docs/prompts/PROMPT_AK_ai-followups.md`; `.projectlinkstore` not committed | #15, #16 |
| V14 | `git status` at the end of the work is as in a. | #16 |

### e.2 ASSERTED (reasoned from code, compiles, not observed)

- **AS1.** A1 closes Prompt AI's item-5 window (b.1.3 case 4). Reasoned from
  V10; not provoked on a bed. It probably cannot be staged on purpose: it
  needs a 300 ms link stall timed to one main-loop pass.
- **AS2.** The full case list of b.1.3, in particular that only cases 3, 4, 6
  and 10 differ from `08afb33` and that they can happen only in the first
  pass after the deadline of an armed hold.
- **AS3.** The invariant of b.1.4: at the start of a tick, a set latch implies
  the watchdog is not armed. Reasoned from V10, by induction over the code
  paths read; not tested (no host test reaches `ble_bed_service.c`).
- **AS4.** The residual window of b.1.5, and its width: about one 30.5 µs
  sleeptimer tick after an identical repeat (the 32 768 Hz tick rate was not
  read this session), a few ms after a fresh start (motor-start I2C time, not
  measured). That the sleeptimer callback fires at or after D_w (sleeptimer
  internals not read).
- **AS5.** The console lines in b.3, including the order of `STOP_ALL latch
  set` before `hold end` in the same pass, and the `K ~ 10 per second`
  repeat count.
- **AS6.** That `BLE: connection_opened` prints with the current flags (which
  flag guards `:1332` was not checked).
- **AS7.** The +0 B total at flag 0 is alignment padding taking up the 8 B
  growth. The function sizes are VERIFIED (V6); the padding explanation is
  inferred, not read from the map file.

---

## f. QUESTIONS FOR REVIEW

**f.1 The later `service_app_commanding()` call still leaves a narrower #36
window - guard it too?** (b.1.5) A1 closes AI's item 5 (the top call). The
unchanged later call (`:1202`) can still release the app during an armed hold
when a pass ends between the silence deadline and the watchdog expiry, about
one sleeptimer tick wide after an identical repeat and a few ms after a fresh
start. If the late repeat is then processed before the expiry is acted on,
the move continues with `source_commanding[APP]` false: AI item 5's
consequence. This predates Prompt AI. The prompt says the later call stays as
it is, so it is not changed.

Proposal, not made: guard it the same way (`if (!move_watchdog_armed)` at
`:1202`), or put the guard inside `service_app_commanding()`. While armed,
the watchdog stop does the release within D_w - D_c plus one pass. Side
effect: a STOP_ALL anywhere in that interval latches (b.1.3 case 3's
reasoning). Change it, or accept the window?

**f.2 b.1.3 case 3 - a STOP_ALL in the first pass after a stall now latches.**
In `08afb33` it did not latch, because the top call released first. In AK the
app is still "commanding" in that pass, so the STOP latches STOP until a stop
write or 300 ms of silence. I read this as the right result, since the user
was holding when the link stalled, and as what the reviewer intends ("latched
releases are still judged" plus the AH A6.1 behaviour). Confirm.

**f.3 Docs that still quote the no-latch line without `(conn=N)`.**
REGRESSION 5b.3 (`:319`, `:324`), 5b.5 (`:345-346`, split over two lines) and 5b.6 (`:358`) quote
`BLE: STOP_ALL (no app hold, no latch)`, and so does DECISIONS #49's Prompt AI
amendment (`:2886`). These were not edited because the prompt does not ask
for it. The quoted text is a prefix of the new line, so a bench reader still
matches it. I did add one sentence naming the new line to the B1 amendment.
Edit REGRESSION, or leave it as is?

**f.4 b.1.3 case 6 - the late start after a switch conflict in a stall.**
In `08afb33` it was accepted at once. Now it is refused 0x01, then
WATCHDOG_STOP, then accepted on the next repeat. The app would show 0x01
("wall switch") and then "connection stalled" (the F18 symptom) for about
100 ms before the bed moves. Rare: it needs a switch conflict during a stall
of 300 ms or more, with the switch already released. Acceptable?

---

## g. NOT DONE

- **No bench run** - the user drives the bench (CLAUDE.md). The three STOP
  cases of b.3 are REGRESSION 5b.1, 5b.3 and 5b.5. The rest of the Prompt AI
  bench list (1.R, §5b incl. 5b.3d and 5b.8, 6.11, §6b) is still owed.
- **f.1's further guard** - proposed, not made. The prompt says the later
  call stays as it is.
- **REGRESSION text for `(conn=N)`** - not edited (f.3).
- **No host-test case for A1**: `ble_bed_service.c` needs the SDK, the
  sleeptimer and the stack, and the host test compiles `bed_control.c` only.
  Not asked.
- **Not reproduced in d:** the file contents read to cite them (d.1, P3-P5),
  and the prompt file's diff (c.3). The `docs/SESSION_LOG.md` diff is omitted
  under CLAUDE.md's standing exception.
- **The commit of this file with the SESSION_LOG entry, and the `git push`**
  run after this file is written, so their output cannot appear here. They
  are reported to the user. A push failure would be reported in a follow-up
  commit to this file, without merge, rebase or reset (CLAUDE.md "Pushing").
- Nothing else that was asked was left out.
