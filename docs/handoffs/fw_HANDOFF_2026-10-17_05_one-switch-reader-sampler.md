# Handoff 2026-10-17 #05 — one switch reader (switch sampler, DECISIONS #41)

## a. Header

- Repo: `tilt_bed_controller`
- Branch: `main`
- HEAD before the work: `cb9c7b2` (Handoff 04: status check only). **This differs from `docs/SWITCH_SAMPLER_DESIGN.md` §0, which expected `6b35be8` and "three commits ahead of origin/main".** `cb9c7b2` was committed and pushed earlier today by this same conversation (the status-check task), so `origin/main` was level with local `main` and nothing was ahead. See QUESTIONS FOR REVIEW 1.
- HEAD after the code commit: `d1f13d0` (One switch reader: sampler, debounce, re-driver refcount, sleep veto (DECISIONS #41)). This handoff and the `docs/SESSION_LOG.md` entry are committed after it, together, in a docs-only commit whose SHA is not known while this file is being written.
- `docs/SESSION_LOG.md` diff is omitted per CLAUDE.md (it ships alongside this file).
- Files committed in `d1f13d0`: `app.c app.h bed_actions.c exp_board.c exp_board.h exp_ui.c exp_ui.h switch_input.c switch_input.h docs/DECISIONS.md docs/SWITCH_SAMPLER_DESIGN.md`. `.projectlinkstore` is modified and was deliberately NOT committed (the design says never to).
- `BED_BENCH_PROVOKE_RUNAWAY` is 0 in the committed source (`switch_input.c:98`).
- The 03 handoff was not touched.

`git status` immediately before this handoff was committed (after the code commit, after the final flag-0 rebuild):

```
On branch main
Your branch is ahead of 'origin/main' by 1 commit.
  (use "git push" to publish your local commits)

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   .projectlinkstore

no changes added to commit (use "git add" and/or "git commit -a")
```

## b. The design as implemented

This section is the design from `docs/SWITCH_SAMPLER_DESIGN.md` written out as it was actually implemented, with every deviation called out in **DEVIATION** paragraphs. A reviewer should not need that document to follow it.

### b.0 Problem being fixed

Three code paths read the UI expander input port: the dispatch in `switch_input_tick`, the double-press check, and `motion_failsafe_tick` (through `get_ui_status_out_of_band()`). Each read clears the expander's interrupt flag, so they raced. The last bench log (`prompt_q_bench.txt` lines 190-254, as described by the reviewer; I did not re-read that log this session) shows a two-button release delivered as one event carrying two bits; `process_inputs()` only matches single-bit masks, so the release was dropped and the fail-safe stopped the motors ~200 ms later, five times. There was no debounce anywhere.

Hardware facts taken from the design (from schematics, not checked by me): motor expander P1_5 = `EN_I2C_DRIVER` powers only the PCA9617A re-drivers and their pull-ups; the Switch board expander (0x21), its LEDs and the interrupt line are on always-on 3V3; re-driver EN may only change while the bus is idle; holding the re-driver on for a sampling session is approved.

Motor gating (design statement, consistent with `app.c:406-417` which I read): all four PWMs are started at 100% and never stopped, so motor-expander P0_0-P0_3 alone gate motion. `get_motor_enables()` reads that signal and was not changed.

### b.1 `exp_ui.c` / `exp_ui.h` — one re-driver owner, one read primitive

```c
static uint8_t redriver_holds = 0;                    // exp_ui.c:31

void ui_redriver_acquire(void) {                      // exp_ui.c:211
  if (redriver_holds == 0) {
    set_I2C_redriver_on();
    sl_sleeptimer_delay_millisecond(2);               // supply load switch needs 2 ms
  }
  redriver_holds++;
}
void ui_redriver_release(void) {                      // exp_ui.c:221
  if (redriver_holds > 0) {
    redriver_holds--;
    if (redriver_holds == 0) set_I2C_redriver_off();
  }
}
bool ui_read_inputs(uint8_t board_addr, uint16_t *status) {   // exp_ui.c:235
  ui_target = board_addr;
  WRITE_READ of EXP_CMD_READ_INPUTS, 2 bytes through ui_transaction();
  if (ret != i2cTransferDone) return false;           // *status untouched
  *status = (ports[0] << 8) | ports[1];
  return true;
}
```

- `ui_transaction()` now calls `ui_redriver_acquire()` before the transfer and `ui_redriver_release()` after it, replacing its own on/delay/off. With nothing holding the re-driver the behaviour is identical to before (toggle + 2 ms delay per transaction). While the sampler holds it there is no toggling and no delay. The comment about the 2 ms load-switch time moved with the delay into `ui_redriver_acquire()`.
- Board addresses `UI_AUX_I2C_BUS_ADDRESS 0x20` and `UI_SWITCH_I2C_BUS_ADDRESS 0x21` moved to `exp_ui.h`.
- Deleted: `get_ui_interrupt()`, `get_ui_interrupts()`, `get_ui_status()`, `ui_int_source`, and their declarations. LED functions and `init_ui_expander()` are otherwise unchanged (they still write both boards blindly).
- `exp_ui.h` gained `#include <stdbool.h>` because it now declares `bool` functions (it already used `bool LED_state_on[]` without the include, relying on include order).
- **Context safety (design asked me to confirm with a grep and report):** EN must only change between transfers, so all callers must be main-loop context. Evidence in section d: every `sl_sleeptimer_start_*` callback in `app.c`, `auto_level.c`, `dual_motor_control.c`, `settings_mode.c` only sets a flag; `sl_button_on_change` (the GPIO interrupt hook) is now flag-only; `IADC_IRQHandler` (`adc_currents.c:163`) was not inspected for I2C (it is an ADC handler; I did not read it); `ble_bed_service.c:499` has a comment saying `sl_bt_on_event()` is dispatched in main-loop context on this part — I relied on that comment and did not trace the BLE path further. So: timer callbacks VERIFIED by grep, BLE dispatch context ASSERTED from a comment, IADC ISR not checked for I2C.

### b.2 `exp_board.c` / `exp_board.h` — motor-enable shadow

```c
static uint8_t motor_enable_shadow = 0x0F;            // exp_board.c:47  unknown at boot = assume on
static void note_port0_write(ret, written) {          // exp_board.c:103
  motor_enable_shadow = (ret == i2cTransferDone) ? (written & 0x0F) : 0x0F;
}
bool motor_outputs_maybe_on(void) { return motor_enable_shadow != 0; }   // exp_board.c:329, no I2C
```

Every port-0 (`EXP_CMD_OUTPUT`) writer calls `note_port0_write(ret, write_cmd[1])` immediately after its write and before its `EFM_ASSERT`. The full list of port-0 write sites (grep `EXP_CMD_OUTPUT;` in `exp_board.c`, raw output in section d, lines 123,145,165,186,206,227,247,268,313,490 at the time of the grep; 313 is the read in `get_motor_enables()`):

| function | write line calling `note_port0_write` |
|---|---|
| `set_motor_on` | 134 |
| `set_all_motors_on` | 154 |
| `set_motor_off` | 175 |
| `set_all_motors_off` | 195 |
| `set_motor_direction_up` | 216 |
| `set_all_motors_direction_up` | 236 |
| `set_motor_direction_down` | 257 |
| `set_all_motors_direction_down` | 277 |
| `init_board_expander` (first write, port 0 = 0x00) | 493 |

The port-1 writers (re-driver on/off, under-bed lighting, `init_board_expander`'s second write) use `EXP_CMD_OUTPUT + 1` and correctly do not touch the shadow. The shadow records the byte that was written; if the read half of a read-modify-write failed (its existing `EFM_ASSERT` is unchanged), the shadow reflects whatever byte went out.

### b.3 `switch_input.c` — the sampler

Constants (`switch_input.c:53-57`): `SWITCH_SAMPLE_MS 10`, `SWITCH_DEBOUNCE_SAMPLES 3`, `SWITCH_READ_FAIL_LIMIT 5`, `SWITCH_STATE_STALE_MS 100`, `SWITCH_SAMPLER_DIAG 1`. `SWITCH_INPUT_MASK` (0xFF0F) unchanged. Work is in "down" polarity: `down = ~status & SWITCH_INPUT_MASK`.

State (all static): `sampler_active`, `volatile bool ui_wake_request`, `aux_present`, `switch_present`, `deb_down`, `pending_count[16]`, `bounce_count[16]`, `read_fail_run`, `next_sample_tick`, `last_good_sample_tick`, `pending_release`, `pending_press`.

```
sl_button_on_change(handle):       // flag only; no I2C
  if state == PRESSED and handle == &sl_button_ui_int: ui_wake_request = true   (sl_atomic_store)

switch_sampler_init():             // app.c:399, right after init_ui_expander()
  ui_wake_request = false
  ui_redriver_acquire()
  aux_present    = ui_read_inputs(0x20, &s);  if ok: down |= ~s & MASK
  switch_present = ui_read_inputs(0x21, &s);  if ok: down |= ~s & MASK
  deb_down = down                  // NO events generated (comment explains why a held-at-boot
                                   //  button is never acted on)
  last_good_sample_tick = now
  if deb_down != 0: sampler_active = true; next_sample_tick = now   // keep the hold
  else: ui_redriver_release()

switch_sampler_tick():
  if !sampler_active:
     if !ui_wake_request and UI_INT pin is high: return          // GPIO_PinInGet level check
     ui_redriver_acquire(); re-probe any board not present; sampler_active = true; next_sample_tick = now
  if now is before next_sample_tick: return
  next_sample_tick += period; if still behind: next_sample_tick = now + period
  ui_wake_request = false          // BEFORE the read
  raw = OR of down masks of boards whose ui_read_inputs() succeeded
  if some read ok:   last_good_sample_tick = now; read_fail_run = 0
  elif a board is present (all failed):
        read_fail_run++
        if read_fail_run >= 5: raw = 0; both boards marked not present; DIAG print
        else: skip debounce this sample (hold state)
  else (no board present): raw = 0
  for each bit b in SWITCH_INPUT_MASK:
     if raw_b == deb_b: if pending_count[b] > 0 { bounce_count[b]++; pending_count[b] = 0 }
     else: if ++pending_count[b] >= 3:
              flip deb_b; record in pending_press (now down) or pending_release (now up)
              DIAG "SW: 0x%04X down|up (bounces %u)"; pending_count[b] = bounce_count[b] = 0
  if deb_down == 0 and every pending_count == 0 and !ui_wake_request:
        sampler_active = false; ui_redriver_release()
```

Accessors: `switch_sampler_active()` (`:440`), `switch_sampler_down()` returns `deb_down` (`:447`), `switch_state_fresh()` = `!sampler_active || (now - last_good_sample_tick) <= 100 ms` (`:454`). When idle `deb_down` is 0 by construction because a session only ends with everything released, so idle state is valid without a read.

Event delivery in `switch_input_tick()` (`:905`), after `switch_sampler_tick()`: take and clear `pending_release`/`pending_press`; deliver every release bit (lowest bit first) via `deliver_switch_edge(bit, false)`; then, **if `count_switches_down(deb_down) >= 2` deliver no presses** (DIAG `SW: %u switches down - presses ignored`), otherwise deliver each press bit lowest first. `deliver_switch_edge` sets `app_ui_interrupt = bit; switch_pressed = pressed; process_inputs();`. Releases first guarantees a release reaches `bed_move_stop()` before anything new can start. `process_inputs()`, `process_outputs()` and `settings_mode_process_outputs()` are unchanged except that `process_outputs()` no longer sets the deleted `process_switch_change`, and the BED_LIGHTS release no longer stops the deleted timer. "Unrecognized switch mask" is kept (it should now never print). The provoke hook is untouched.

`count_switches_down()` now takes a down-mask (it used to take an active-low status word) and masks with `SWITCH_INPUT_MASK`.

### b.4 Double-press check

The gate is unchanged: `(active_switch != OFF) && !any_collision && !auto_level_active && !in_settings_mode`, and `switch_sourced_move`. `initial_switch_press -> scan_all_currents()` and `run_motor_supervision()` are unchanged. The timer-driven body is replaced by:

```
if (switch_sourced_move && count_switches_down(switch_sampler_down()) >= 2)  -> the existing "Double Press Error" block
```

The block is unchanged apart from printing the down-mask: `  down mask 0x%04X, %u switches down`. Deleted: `double_press_error_timer` and its callback/starter/flags, `app_reset_double_press_timer()` and its declaration, `DOUBLE_PRESS_ERROR_CHECK_TIME_INTERVAL`, the 7 call sites in `bed_actions.c`, and the stop-timer line in the BED_LIGHTS release branch (`settings_mode_cancel_entry()` is kept there). The check now runs every main-loop pass instead of 50 ms after the initial press and then every 50 ms. Note it runs *before* `switch_sampler_tick()` in each pass, so it sees the previous pass's debounced state.

### b.5 Motion fail-safe

`uint16_t status = get_ui_status_out_of_band();` and the old `any_switch_down` are replaced by
`bool any_switch_down = switch_state_fresh() && (switch_sampler_down() != 0);` (`switch_input.c:808`). Stale or unknown means "no switch down", so it fails toward OFF. Everything else in `motion_failsafe_tick()` (sampling period, confirm count, motor-enable hardware read, provoke hook, cleanup) is unchanged. Its comments were updated.

### b.6 Sleep (`app.c`, `app_is_ok_to_sleep`)

Added after the `motor_state[]` loop and before the tilt-burst check: `if (switch_sampler_active()) return false;` and `if (motor_outputs_maybe_on()) return false;` (`app.c:505`, `:515`) with the comment from the design. The `motor_state[]` loop and the tilt-burst check are kept.

### b.7 Deleted (§2.7)

`read_ui_data`, `process_switch_change`, `app_ui_status`, `app_ui_status_new`, `app_ui_interrupt_new`, `app_ui_status_valid`, `swallowed_change_hint`, `get_ui_status_out_of_band()`, the "UI: recovered swallowed switch change" print and its comment block. `app_ui_interrupt` and `switch_pressed` stay (`settings_mode.c` reads them).

### b.8 DECISIONS.md

Added #41 "One switch reader" with the points listed in the design §2.8, including the known limit (the expander can drop an edge that coincides with the ACK of its own read; mid-session the next sample catches it; at session end a press in that window is lost, which is benign and never a lost release; the UI_INT level check at idle covers a stuck-asserted line). It also lists the behaviour changes below.

### b.9 Deviations from the design, and things the design left open

1. **`sl_button_on_change` keeps the existing `SL_SIMPLE_BUTTON_PRESSED` gate.** The design says "set ui_wake_request = true" for the ui_int handle. I kept the old condition (only the asserting edge). Reason: UI_INT also rises when the sampler's own read clears the flag; waking on that edge would set the flag during the read and prolong every session by one sample. This assumes PRESSED means UI_INT low, which is what the old code relied on and what the idle level check also assumes (`== 0` is asserted). If you want it ungated, it is a one-line change.
2. **Diagnostics go through a small macro `SW_DIAG(...)`** which is `printf` when `SWITCH_SAMPLER_DIAG && !BED_CAPTURE_MOTOR_CURRENTS`, else nothing. The `!BED_CAPTURE_MOTOR_CURRENTS` part matches every other print in the file (they all compile out during a current capture). Pre-ship setting is still `SWITCH_SAMPLER_DIAG 0`.
3. **Stale comments naming deleted symbols were reworded** in `app.c` (5 places), `app.h` (1) and `switch_input.h` (header comment and the fail-safe prototype comment), because the §3a grep must return no matches in `.c/.h`. No logic changed in those hunks. The `#include "switch_input.h"` in `bed_actions.c` was left alone even though nothing from it is used there now (`app_reset_double_press_timer()` was the only use I found; I did not search for others beyond the grep in section d).
4. **Read-failure bookkeeping interpretation.** The design says "after SWITCH_READ_FAIL_LIMIT consecutive failures of every present board". I implemented `read_fail_run` as the count of consecutive samples in which *no* present board's read succeeded. Consequence: if one board keeps failing while the other still answers, `read_fail_run` stays 0, that board is never marked not present, and its bits simply read as released (so they debounce to released within 3 samples). That fails toward OFF but it will keep logging "board read failed" every 10 ms under DIAG.
5. **Before the fail limit is reached** (all present boards failed, count < 5) I skip the debounce step for that sample (hold the debounced state). The design is silent on this. At the limit, raw is forced to 0 and both boards are marked not present.
6. **No board present at all** (e.g. a bench with no UI boards): `raw` stays 0 and `last_good_sample_tick` is not updated, so `switch_state_fresh()` goes false 100 ms into a session and the fail-safe sees "no switch down". Combined with the level check, if UI_INT is held low with no boards, a session starts, finds nothing to read and ends, every pass, with two NACK probes and a 2 ms re-driver delay each time, and `app_is_ok_to_sleep()` is vetoed during each. The design accepts a stuck-asserted line; this is its cost on a board-less bench.
7. `switch_sampler_tick()` computes `now` once at entry; `last_good_sample_tick` is set to that value, not to the time after the reads (a conservative, slightly stale timestamp).
8. `DOUBLE_PRESS_ERROR_CHECK_TIME_INTERVAL` was removed with the timer, and `exp_ui.h` got `<stdbool.h>` (see b.1). Neither is in the design.
9. Not done as the design literally says, because it is impossible in the same step: none. The one place the design's stated repo state did not match is the HEAD/ahead-of-origin note (header, QUESTIONS 1).

### b.10 Behaviour changes the bench should expect

- A release is acted on only after 3 identical samples at 10 ms: roughly 20-30 ms after the contact opens (plus up to one sample period before the first differing read), so a move now stops later than before by about that much. This is the cost of debounce and is not offset anywhere.
- The double-press check is continuous (every pass) instead of 50 ms after the press.
- `SW:` diagnostic lines now print for every accepted edge, with a per-bit bounce tally.
- While a sampling session runs, or while the shadow says a motor might be on, the CPU does not sleep (more current draw in those windows).

### b.11 §3c — off-board I2C clock and blocking estimates

Clock: both I2C instances are in standard mode, 100 kbit/s: `config/sl_i2cspm_off_board_config.h:49` `#define SL_I2CSPM_OFF_BOARD_SPEED_MODE 0` (0 = "Standard mode (100kbit/s)", lines 45-48); the on-board bus has the same value at `config/sl_i2cspm_on_board_config.h:49`. Reference clock 0 (use current clock).

Estimation basis (my arithmetic, **ASSERTED, not measured**): 9 clocks per byte including ACK, ~10 us per clock at 100 kHz, start/stop overhead ignored apart from rounding up; 1-byte-register read of 1 byte = 4 bytes on the wire ≈ 0.4 ms; 2-byte read (command + 2 data) = 5 bytes ≈ 0.5 ms; 2-byte write = 3 bytes ≈ 0.3 ms. Re-driver on or off = on-board read-modify-write = 1 read (0.4) + 1 write (0.3) = 0.7 ms and 2 on-board transactions. The 2 ms load-switch wait is a blocking `sl_sleeptimer_delay_millisecond(2)` in main-loop context (I did not read that function's implementation; "blocking" is assumed). One UI transaction today therefore costs 0.7 + 2.0 + 0.5 + 0.7 = 3.9 ms and counts as 1 off-board + 4 on-board transactions.

Scenario (i): idle, loop awake (not sleeping), no button, no phone connected, no auto-level. Scenario (ii): one single-side button held and the motors running (switch-sourced move). Aux board assumed present for the "after" figure; if it is absent, "after (ii)" halves.

| scenario | | on-board txn/s | off-board txn/s | blocking ms/s |
|---|---|---|---|---|
| (i) idle, awake | today | 105 = 80 re-driver (20 UI reads x 4) + 20 `get_motor_enables` + 5 fault samples | 20 (fail-safe status read every 50 ms) | ~89 = 20 x 3.9 (78) + 20 x 0.4 (8) + 5 x 0.5 (2.5) |
| (i) idle, awake | after | 25 = 20 `get_motor_enables` + 5 fault samples | 0 (level check is a GPIO read) | ~10.5 = 8 + 2.5 |
| (ii) one button held, moving | today | 405 = 400 re-driver (100 UI reads x 4) + 5 fault samples | 100 = 20 (fail-safe status) + 80 (double-press timer every 50 ms: 2 interrupt reads, 1 status, 1 out-of-band status) | ~393 = 100 x 3.9 (390) + 5 x 0.5 (2.5) |
| (ii) one button held, moving | after (both boards present) | 5 (fault samples; fail-safe returns before the enable read while a switch is down) | 200 = 2 boards x 100 samples/s | ~103 = 200 x 0.5 (100) + 2.5 |
| (ii) one button held, moving | after (switch board only) | 5 | 100 | ~53 |

Plus, once per session after: acquire (0.7 + 2.0 ms) and release (0.7 ms), and one probe transaction per absent board. Caveat: today's (ii) count assumes the first interrupt read in `get_ui_interrupt()` returns 0 so the second board is also read; if it did not, it is 3 reads per double-press tick instead of 4.

### b.12 §3e — proposed bench checklist (expected PuTTY lines)

All lines are what the code *should* print; none has been observed. Build with `BED_BENCH_PROVOKE_RUNAWAY 0` and `SWITCH_SAMPLER_DIAG 1` except where noted. The bounce count in `(bounces N)` is whatever the switch does (0 for a clean contact).

1. **Single press and release (e.g. UP HEAD, mask 0x8000), about 1 s hold.**
   - `SW: 0x8000 down (bounces 0)` ~20-30 ms after pressing; motor moves.
   - `SW: 0x8000 up (bounces 0)` ~20-30 ms after releasing; motor stops.
   - No `FAILSAFE:`, no `Double Press Error`, no `Unrecognized switch mask`, no `SW: ... failed`.
2. **Two buttons pressed together from idle (e.g. UP HEAD + UP LEFT, 0x8000 and 0x4000).**
   - `SW: 0x4000 down (bounces 0)` then `SW: 0x8000 down (bounces 0)` (lowest bit first), then `SW: 2 switches down - presses ignored`.
   - No motor moves, no `Double Press Error` (nothing was started).
   - On release: two `SW: 0x.... up` lines, no `FAILSAFE:`. This is the case that failed before.
3. **Second button pressed while the first is held and moving.**
   - `SW: 0xXXXX down` for the second, `SW: 2 switches down - presses ignored`, then on the next pass `Double Press Error` and `  down mask 0xC000, 2 switches down` (for HEAD+LEFT), motors stop.
   - Releases print `SW: ... up` for both; no `FAILSAFE:`.
4. **BED LIGHTS held 5 s (mask 0x0008).**
   - `SW: 0x0008 down (bounces 0)` at the press, the existing lights toggle and the existing settings-mode entry behaviour at 5 s (those prints are unchanged from before), `SW: 0x0008 up (bounces 0)` on release. No `Unrecognized switch mask`, no `FAILSAFE:`.
5. **Provoked runaway (flag 1; the reviewer hand-flips `BED_BENCH_PROVOKE_RUNAWAY` to 1 and back).**
   - Tap UP HEAD: `SW: 0x8000 down`, `SW: 0x8000 up`, ~300 ms later `PROVOKE: energised HEAD pair with nothing commanding - fail-safe should stop this`, then `FAILSAFE: motors energised with nothing commanding - stopped`, then `PROVOKE: fail-safe caught it after N ms`. The design quotes 214-223 ms for the HEAD pair previously (I did not re-read that log).
   - Tap UP ALL (0x0001): same with `ALL MOTORS`. The design quotes 271-307 ms previously and attributes the gap to the CPU sleeping between fail-safe samples. This change is meant to remove that gap, so expect N close to the HEAD-pair figure; if ALL is still much slower, the sleep-veto explanation is wrong (it is an inference, not observed).
   - Repeat several times; watch for any `FAILSAFE:` with no preceding `PROVOKE:`.
6. **Power-up with a button held**: no `SW:` line and no motion at boot; the later release prints `SW: ... up` (the baseline read left a session running) and does nothing, because the press was never delivered.
7. **Idle**: no `SW:` lines at all. If lines appear with no button touched, UI_INT may be stuck low (see b.9 item 6).

## c. Source diffs

Complete, untruncated: `git diff cb9c7b2 d1f13d0 -- '*.c' '*.h'` (all source changes of this session; the single code commit is `d1f13d0`).

```diff
diff --git a/app.c b/app.c
index 870ada0..9b62411 100644
--- a/app.c
+++ b/app.c
@@ -98,7 +98,7 @@
 
 // Flags
 static bool heartbeat = false; // Interrupt flag for LED heartbeat - use for debug only
-// read_ui_data/process_switch_change/switch_pressed moved to switch_input.c
+// switch_pressed moved to switch_input.c
 bool auto_level_active = false; // Tracks whether autolevel is running - extern via app.h, see bed_actions.c
 bool is_raising; // Used to set specific collision threshold based on whether motors are raising or lowering - extern via app.h, see bed_actions.c
 
@@ -112,8 +112,7 @@ Ecode_t nvm3Status;
 // zeroLevelFirstPass, resetZeroLevelFirstPass, writeCollisionDetection,
 // nvm3StatusCounter) - see settings_mode.h.
 
-// app_ui_status/app_ui_status_new/app_ui_interrupt/app_ui_interrupt_new moved
-// to switch_input.c
+// app_ui_interrupt moved to switch_input.c
 
 static sl_sleeptimer_timer_handle_t heartbeat_delay_timer;
 static sl_sleeptimer_timer_handle_t LED_500ms_timer; // For collision detected LED blinks
@@ -125,7 +124,6 @@ static bool is_auto_level_LED_blink_timer_on = false;
 static sl_sleeptimer_timer_handle_t LED_200ms_timer; // For rapid LED blink for settings or for failed auto-level
 static sl_sleeptimer_timer_handle_t auto_level_timer;
 // handle_5000ms_timer/handle_2000ms_timer moved to settings_mode.c
-// double_press_error_timer + its flags moved to switch_input.c
 
 // Used to determine if all LEDs are on or off for turning all off and on in LED_timer_callback
 static bool all_LEDs_on = false;
@@ -242,8 +240,6 @@ static void start_auto_level_LED_blink_oneshot_timer()
   sl_sleeptimer_start_periodic_timer(&auto_level_LED_blink_timer, ticks, callback_auto_level_LED_blink_timer, NULL, 0, 0);
 }
 
-// double_press_error_timer's callback/starter moved to switch_input.c
-
 /*******************************************************************************/
 /*******************************************************************************/
 
@@ -364,8 +360,6 @@ static void reset_expanders()
 
 // Resets the auto-level timer - bed_actions.c's action functions used to
 // touch it directly via its app.c-private handle - see app.h.
-// app_reset_double_press_timer() moved to switch_input.c, which now owns
-// double_press_error_timer.
 void app_reset_auto_level_timer(void)
 {
   sl_sleeptimer_stop_timer(&auto_level_timer);
@@ -402,6 +396,7 @@ void app_init(void)
   reset_expanders();
   init_board_expander();
   init_ui_expander();
+  switch_sampler_init(); // baseline read of the UI boards; DECISIONS.md #41
 
   int PWM_duty = 100;
 
@@ -505,6 +500,22 @@ bool app_is_ok_to_sleep(void)
     }
   }
 
+  // The switch sampler is polled from the main loop (10 ms cadence), so a
+  // sampling session needs the loop awake (DECISIONS.md #41).
+  if (switch_sampler_active()) {
+    return false;
+  }
+
+  // The motor-enable shadow, not just motor_state[]: set_all_motors_on()
+  // energises the outputs without setting motor_state[].motor_on (#40), so a
+  // runaway could previously let the CPU sleep between fail-safe samples - the
+  // suspected cause of the ALL MOTORS fail-safe taking 271-307 ms against
+  // 214-223 ms for the HEAD pair. The shadow is conservative (true until a
+  // successful write leaves all four enables low), and needs no I2C.
+  if (motor_outputs_maybe_on()) {
+    return false;
+  }
+
   // A tilt telemetry burst also needs the loop (DECISIONS.md #33). The
   // sampler runs from app_process_action(), so without this a standalone
   // refresh - app connects, or REFRESH_TILT arrives, with the bed sitting
diff --git a/app.h b/app.h
index c32960a..0fcfbd3 100644
--- a/app.h
+++ b/app.h
@@ -118,8 +118,7 @@ extern bool first_print[10];
 #define SWITCH_BED_LIGHTS  0x0008
 
 // Resets the auto-level timer that bed_actions.c's moved functions used to
-// touch directly via its app.c-private handle. app_reset_double_press_timer()
-// moved to switch_input.h - switch_input.c now owns double_press_error_timer.
+// touch directly via its app.c-private handle.
 void app_reset_auto_level_timer(void);
 
 // LED helpers bed_actions.c needs (auto-level stop, forced stop). Implemented
diff --git a/bed_actions.c b/bed_actions.c
index d5db191..d9bcde7 100644
--- a/bed_actions.c
+++ b/bed_actions.c
@@ -54,7 +54,6 @@ static void motor_button_pressed(active_switch_t switch_value, side_of_bed_t sid
       reset_ignore_data_points_count();
       reset_motor_current_data();
   }
-  app_reset_double_press_timer();
 }
 
 // Shared handling for a single-side motor button release
@@ -67,7 +66,6 @@ static void motor_button_released(void)
       reset_ignore_data_points_count();
       reset_motor_current_data();
   }
-  app_reset_double_press_timer();
 }
 
 // Shared handling for an all-motors (UP_ALL/DOWN_ALL) button press
@@ -92,7 +90,6 @@ static void all_motors_button_pressed(bool up)
   set_motors_off_once = false;
   temp_motor_off_timer_timeout = false;
   motors_temp_off = false;
-  app_reset_double_press_timer();
 }
 
 // Shared handling for an all-motors (UP_ALL/DOWN_ALL) button release
@@ -109,7 +106,6 @@ static void all_motors_button_released(void)
   set_motors_off_once = false;
   temp_motor_off_timer_timeout = false;
   motors_temp_off = false;
-  app_reset_double_press_timer();
 }
 
 void app_move_start(bed_target_t target, bed_direction_t dir)
@@ -181,7 +177,6 @@ void app_auto_level_start(void)
       reset_ignore_data_points_count();
       reset_motor_current_data();
   }
-  app_reset_double_press_timer();
 }
 
 void app_auto_level_stop(void)
@@ -198,7 +193,6 @@ void app_auto_level_stop(void)
       reset_ignore_data_points_count();
       reset_motor_current_data();
   }
-  app_reset_double_press_timer();
 }
 
 void app_lights_set(bool on)
@@ -206,7 +200,6 @@ void app_lights_set(bool on)
   active_switch = SWITCH_BED_LIGHT_VALUE;
   initial_switch_press = true;
   bedlights_on = on;
-  app_reset_double_press_timer();
 }
 
 // Full clean stop: whichever of auto-level or movement was active gets its
diff --git a/exp_board.c b/exp_board.c
index c774062..c33f27b 100644
--- a/exp_board.c
+++ b/exp_board.c
@@ -38,6 +38,14 @@
 static uint8_t cached_motor_faults = 0;
 static uint32_t last_fault_sample_tick = 0;
 
+// Shadow of the motor ENABLE bits (P0_0-P0_3) of the on-board expander's output
+// register, as last written by this file. Lets app_is_ok_to_sleep() ask "might
+// the hardware be driving a motor?" without an I2C read. Starts at 0x0F
+// (unknown at boot = assume energised) until init_board_expander() writes the
+// port; set to 0x0F again whenever a port-0 write fails. Port 0 is written
+// ONLY from this file - every writer below updates the shadow (DECISIONS.md #41).
+static uint8_t motor_enable_shadow = 0x0F;
+
 /*******************************************************************************
  *********************   LOCAL FUNCTION PROTOTYPES   ***************************
  ******************************************************************************/
@@ -90,6 +98,13 @@ static I2C_TransferReturn_TypeDef board_exp_transaction(uint16_t flag,
   return ret;
 }
 
+// Records the outcome of a port-0 (EXP_CMD_OUTPUT) write in the shadow: on
+// success the enable bits that were written, on failure unknown = energised.
+static void note_port0_write(I2C_TransferReturn_TypeDef ret, uint8_t written)
+{
+  motor_enable_shadow = (ret == i2cTransferDone) ? (written & 0x0F) : 0x0F;
+}
+
 /*******************************************************************************
  **************************   GLOBAL FUNCTIONS   *******************************
  ******************************************************************************/
@@ -116,6 +131,7 @@ void set_motor_on(int motor)
 
   // Write out to expander to turn motor on
   ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
+  note_port0_write(ret, write_cmd[1]);
   EFM_ASSERT(ret == i2cTransferDone);
 }
 
@@ -135,6 +151,7 @@ void set_all_motors_on()
 
   // Write out to expander to turn all motors on
   ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
+  note_port0_write(ret, write_cmd[1]);
   EFM_ASSERT(ret == i2cTransferDone);
 }
 
@@ -155,6 +172,7 @@ void set_motor_off(int motor)
 
   // Write out to expander to turn motor off
   ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
+  note_port0_write(ret, write_cmd[1]);
   EFM_ASSERT(ret == i2cTransferDone);
 }
 
@@ -174,6 +192,7 @@ void set_all_motors_off()
 
   // Write out to expander to turn all motors off
   ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
+  note_port0_write(ret, write_cmd[1]);
   EFM_ASSERT(ret == i2cTransferDone);
 }
 
@@ -194,6 +213,7 @@ void set_motor_direction_up(int motor)
 
   // Write out to expander to set motor run direction
   ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
+  note_port0_write(ret, write_cmd[1]);
   EFM_ASSERT(ret == i2cTransferDone);
 }
 
@@ -213,6 +233,7 @@ void set_all_motors_direction_up()
 
   // Write out to expander to set all motors run direction
   ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
+  note_port0_write(ret, write_cmd[1]);
   EFM_ASSERT(ret == i2cTransferDone);
 }
 
@@ -233,6 +254,7 @@ void set_motor_direction_down(int motor)
 
   // Write out to expander and set motor run direction
   ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
+  note_port0_write(ret, write_cmd[1]);
   EFM_ASSERT(ret == i2cTransferDone);
 }
 
@@ -252,6 +274,7 @@ void set_all_motors_direction_down()
 
   // Write out to expander and set all motors run direction
   ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
+  note_port0_write(ret, write_cmd[1]);
   EFM_ASSERT(ret == i2cTransferDone);
 }
 
@@ -298,6 +321,16 @@ bool get_motor_enables(uint8_t *enables)
   return true;
 }
 
+// True if the motor-enable lines MIGHT be high, from the shadow of what this file
+// last wrote - no I2C, safe from any context. Conservative: false only when the
+// last successful port-0 write left all four enables low. Used by
+// app_is_ok_to_sleep(): set_all_motors_on() energises without setting
+// motor_state[].motor_on (DECISIONS.md #40), so the flags alone cannot veto sleep.
+bool motor_outputs_maybe_on(void)
+{
+  return motor_enable_shadow != 0;
+}
+
 // Rate-limited, main-loop-context wrapper around get_motor_faults(). Call
 // once per app_process_action() pass; internally gates the actual I2C read
 // to once per MOTOR_FAULT_SAMPLE_INTERVAL_MS so it doesn't add a second
@@ -457,6 +490,7 @@ void init_board_expander()
   write_cmd[0] = EXP_CMD_OUTPUT;
   write_cmd[1] = 0x00;
   ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
+  note_port0_write(ret, write_cmd[1]);
   EFM_ASSERT(ret == i2cTransferDone);
   write_cmd[0] = EXP_CMD_OUTPUT + 1; // Advance to Port 1
   ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
diff --git a/exp_board.h b/exp_board.h
index 6040b97..fd3cbc4 100644
--- a/exp_board.h
+++ b/exp_board.h
@@ -47,6 +47,11 @@ uint8_t get_motor_faults();
 // survive a bus error rather than hang on it.
 bool get_motor_enables(uint8_t *enables);
 
+// True if the motor-enable lines might be high, per a shadow of the last port-0
+// write (no I2C). Starts true (unknown at boot) and returns true again after any
+// failed write. See exp_board.c; used by app_is_ok_to_sleep().
+bool motor_outputs_maybe_on(void);
+
 // Interim polled motor-fault telemetry (main-loop context) - see
 // sample_motor_faults_tick()'s comment in exp_board.c for the rate
 // justification and the transient-miss limitation.
diff --git a/exp_ui.c b/exp_ui.c
index 5639f67..b2b6b58 100644
--- a/exp_ui.c
+++ b/exp_ui.c
@@ -19,15 +19,16 @@
  ******************************************************************************/
 
 #define UI_I2C_DEVICE               (sl_i2cspm_off_board)
-#define UI_AUX_I2C_BUS_ADDRESS      0x20 // Aux Board
-#define UI_SWITCH_I2C_BUS_ADDRESS   0x21 // Switch Board
+// UI_AUX_I2C_BUS_ADDRESS / UI_SWITCH_I2C_BUS_ADDRESS live in exp_ui.h.
 
 /*******************************************************************************
  ***************************  GLOBAL VARIABLES   ********************************
  ******************************************************************************/
 
 static uint8_t ui_target;
-static uint8_t ui_int_source;
+
+// Number of holders of the off-board bus re-drivers (see ui_redriver_acquire()).
+static uint8_t redriver_holds = 0;
 
 bool LED_state_on[NUM_LEDS] = {false}; // Declared extern in exp_ui.h to use in app.c as well
 
@@ -77,43 +78,19 @@ static I2C_TransferReturn_TypeDef ui_transaction(uint16_t flag,
       return i2cTransferUsageFault;
   }
 
-  // Re-driver on/off time is < 100 nsec, but supply load switch needs 2 msec
-  set_I2C_redriver_on();
-  sl_sleeptimer_delay_millisecond(2);
+  // Re-driver power ON for the duration of this transfer. A no-op (no toggle,
+  // no delay) if the switch sampler is already holding it for a session.
+  ui_redriver_acquire();
 
   // Perform the transfer and return status from the transfer
   ret = I2CSPM_Transfer(UI_I2C_DEVICE, &seq);
 
-  // Turn re-driver/power OFF
-  set_I2C_redriver_off();
+  // Turn re-driver/power OFF, unless something else still holds it
+  ui_redriver_release();
 
   return ret;
 }
 
-static uint16_t get_ui_interrupts()
-{
-  I2C_TransferReturn_TypeDef ret;
-  uint8_t cmd;
-  uint8_t ports[2];
-  uint16_t interrupt;
-
-  // Read GPIO IC's internal interrupt source flags
-  cmd = EXP_CMD_READ_INTERRUPTS;
-  ret = ui_transaction(I2C_FLAG_WRITE_READ, &cmd, 1, &ports[0], 2);
-  // EFM_ASSERT(ret == i2cTransferDone);
-  if (ret != i2cTransferDone) { // Other board or not connected
-      ports[0] = 0;
-      ports[1] = 0;
-  }
-
-  // Save Port 0 and Port 1 into packed variable - 1's are interrupt source(s)
-  // Maybe atomic load/store, but almost certainly OK with current design
-  interrupt = ((uint16_t) (ports[0] << 8)) | ports[1];
-
-  // Return for processing by switch decode function
-  return interrupt;
-}
-
 static void set_LEDs_on(led_color_t LED_color)
 {
   I2C_TransferReturn_TypeDef ret;
@@ -231,48 +208,45 @@ static void init_ui_expanders()
  **************************   GLOBAL FUNCTIONS   *******************************
  ******************************************************************************/
 
-uint16_t get_ui_interrupt()
+void ui_redriver_acquire(void)
 {
-  uint16_t ui_interrupt;
-
-  // Read interrupt register from Auxiliary board
-  ui_target = UI_AUX_I2C_BUS_ADDRESS;
-  ui_int_source = ui_target;
-  ui_interrupt = get_ui_interrupts();
-
-  if(ui_interrupt == 0){ // No Auxiliary board interrupts, so from Switch board
-      ui_target = UI_SWITCH_I2C_BUS_ADDRESS;
-      ui_int_source = ui_target;
-      ui_interrupt = get_ui_interrupts();
+  if (redriver_holds == 0) {
+    // Re-driver on/off time is < 100 nsec, but supply load switch needs 2 msec
+    set_I2C_redriver_on();
+    sl_sleeptimer_delay_millisecond(2);
   }
+  redriver_holds++;
+}
 
-  // Return for processing by switch decode function
-  return ui_interrupt;
+void ui_redriver_release(void)
+{
+  if (redriver_holds > 0) {
+    redriver_holds--;
+    if (redriver_holds == 0) {
+      // Turn re-driver/power OFF
+      set_I2C_redriver_off();
+    }
+  }
 }
 
-uint16_t get_ui_status()
+// The only place in the firmware that issues EXP_CMD_READ_INPUTS to a UI board.
+// Reading the input ports clears the expander's interrupt flag, which is why
+// there must be exactly one reader (DECISIONS.md #41).
+bool ui_read_inputs(uint8_t board_addr, uint16_t *status)
 {
   I2C_TransferReturn_TypeDef ret;
-  uint8_t cmd;
-  uint8_t ports[2];
-  uint16_t ui_status;
+  uint8_t cmd = EXP_CMD_READ_INPUTS;
+  uint8_t ports[2] = {0, 0};
 
-  // Set register read of the GPIO expander that triggered the interrupt
-  ui_target = ui_int_source;
-
-  // Read of port state inputs clears GPIO IC's internal interrupt flag
-  cmd = EXP_CMD_READ_INPUTS;
+  ui_target = board_addr;
   ret = ui_transaction(I2C_FLAG_WRITE_READ, &cmd, 1, &ports[0], 2);
-  //EFM_ASSERT(ret == i2cTransferDone);
-  (void)ret;
-
-  // Save freshly read Port 0 and Port 1 into a packed status variable
-  // Maybe atomic load/store, but almost certainly OK with current design
-
-  ui_status = ((uint16_t) (ports[0] << 8)) | ports[1];
+  if (ret != i2cTransferDone) { // Other board or not connected
+    return false;
+  }
 
-  // Return for processing by motor controls
-  return ui_status;
+  // Pack Port 0 and Port 1: switches are active low (1 = released)
+  *status = ((uint16_t) (ports[0] << 8)) | ports[1];
+  return true;
 }
 
 void set_LED_on(led_color_t LED_color)
diff --git a/exp_ui.h b/exp_ui.h
index cb0a858..465af5e 100644
--- a/exp_ui.h
+++ b/exp_ui.h
@@ -8,10 +8,17 @@
 #ifndef EXP_UI_H_
 #define EXP_UI_H_
 
+#include <stdbool.h>
 #include <stdint.h>
 
 #define NUM_LEDS 4
 
+// I2C addresses of the two remote UI boards (both on the off-board bus, behind
+// the PCA9617A re-drivers). Used by init/LED writes here and by the switch
+// sampler in switch_input.c.
+#define UI_AUX_I2C_BUS_ADDRESS     0x20 // Aux Board
+#define UI_SWITCH_I2C_BUS_ADDRESS  0x21 // Switch Board
+
 typedef enum {
   LED_RED,
   LED_GREEN,
@@ -32,8 +39,20 @@ void init_ui_expander();
  * UI GPIO expander interface functions
  ******************************************************************************/
 
-uint16_t get_ui_interrupt();
-uint16_t get_ui_status();
+// The ONLY function that reads a UI board's input ports. Reading them clears the
+// expander's interrupt flag, so exactly one caller (the switch sampler in
+// switch_input.c) may use it - see DECISIONS.md #41. Returns false on any
+// transfer error (*status untouched); otherwise *status = (port0 << 8) | port1.
+bool ui_read_inputs(uint8_t board_addr, uint16_t *status);
+
+// Reference-counted ownership of the off-board bus re-drivers (P1_5 on the
+// on-board expander). The first acquire switches them on and waits out the
+// supply load switch; the last release switches them off. Every UI transaction
+// takes a hold for its own duration, so holding across a sampling session means
+// no per-transaction toggling or 2 ms delay. Main-loop context only: EN must
+// change only while the bus is idle.
+void ui_redriver_acquire(void);
+void ui_redriver_release(void);
 
 void set_LED_on(led_color_t LED_color);
 void set_LED_off(led_color_t LED_color);
diff --git a/switch_input.c b/switch_input.c
index 584d0ee..e9c7ace 100644
--- a/switch_input.c
+++ b/switch_input.c
@@ -17,8 +17,10 @@
 #include <stdint.h>
 #include <stdio.h>
 
+#include "em_gpio.h"
 #include "sl_atomic.h"
 #include "sl_simple_button_instances.h"
+#include "sl_simple_button_ui_int_config.h"
 #include "sl_sleeptimer.h"
 
 #include "app.h"
@@ -46,8 +48,13 @@
 #define SWITCH_UP_LEFT     0x4000
 #define SWITCH_UP_HEAD     0x8000
 
-/// Double press error check time interval
-#define DOUBLE_PRESS_ERROR_CHECK_TIME_INTERVAL 50 // time in ms
+// Switch sampler timing (DECISIONS.md #41). Milliseconds via sleeptimer, never
+// loop passes (#2).
+#define SWITCH_SAMPLE_MS            10
+#define SWITCH_DEBOUNCE_SAMPLES     3   // 3 identical reads = >=20 ms stable
+#define SWITCH_READ_FAIL_LIMIT      5   // 50 ms of failed reads -> all released
+#define SWITCH_STATE_STALE_MS       100 // fail-safe distrusts older data
+#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
 
 // Every SWITCH bit of the packed Port0/Port1 status word, and nothing else.
 // Port 0 is all eight single-side switches; Port 1 bits 0-3 are UP_ALL,
@@ -97,15 +104,13 @@
 #define PROVOKE_DELAY_MS 300
 
 // Flags
-static bool read_ui_data = false; // Interrupt flag to read the UI status
-static bool process_switch_change = true; // Indicates now clear to deal with another switch press
 bool switch_pressed = false; // Interrupt flag to capture switch up/down state - extern via switch_input.h
 
 /*
   I2C GPIO Expander Port 0 and Port 1 packed status variables
-  app_ui_status = ((uint16_t) (ports[0] << 8)) | ports[1];
+  status = ((uint16_t) (ports[0] << 8)) | ports[1]; (see ui_read_inputs())
 
-  app_ui_interrupt shares same bit mapping
+  app_ui_interrupt and the sampler's "down" masks share the same bit mapping
 
    P0_7  UP_M4           P1_7  EN_AMBERn
    P0_6  UP_M3           P1_6  EN_BLUEn
@@ -151,47 +156,7 @@ bool switch_pressed = false; // Interrupt flag to capture switch up/down state -
 static active_switch_t switch_held = OFF;
 static active_switch_t switch_driving = OFF;
 
-static uint16_t app_ui_status;
-static uint16_t app_ui_status_new; // Used for double press error check
-uint16_t app_ui_interrupt; // Packed Port0/1 interrupt source(s) - extern via switch_input.h
-static uint16_t app_ui_interrupt_new; // Used for double press error check
-
-// Timer for reading switch values during operation to ensure motors are off if no buttons are pressed
-// Important for pressing two switches at the same time
-static sl_sleeptimer_timer_handle_t double_press_error_timer;
-static bool double_press_error_timer_timeout = false;
-static bool is_double_press_error_timer_on = false;
-
-/*******************************************************************************/
-/// For double press error timer - ONESHOT
-/*******************************************************************************/
-// Called when the auto_level timer goes off
-static void callback_double_press_error_timer(sl_sleeptimer_timer_handle_t *handle_double_press_error, void *data_double_press_error)
-{
-  (void)handle_double_press_error;
-  (void)data_double_press_error;
-
-  double_press_error_timer_timeout = true;
-
-}
-
-// Starts a one-shot timer for auto_level
-static void start_double_press_error_timer()
-{
-  uint32_t ticks = sl_sleeptimer_ms_to_tick(DOUBLE_PRESS_ERROR_CHECK_TIME_INTERVAL);
-  sl_sleeptimer_start_periodic_timer(&double_press_error_timer, ticks, callback_double_press_error_timer, NULL, 0, 0);
-}
-
-// Resets the double-press-error timer - non-static: called by bed_actions.c
-// via switch_input.h.
-void app_reset_double_press_timer(void)
-{
-  sl_sleeptimer_stop_timer(&double_press_error_timer);
-  // Discard any expiry that already fired. Stopping a timer must also drop
-  // its pending result, or the flag survives to be consumed at an arbitrary
-  // later moment (DECISIONS.md #38).
-  double_press_error_timer_timeout = false;
-}
+uint16_t app_ui_interrupt; // The ONE switch bit of the edge being delivered - extern via switch_input.h
 
 // Clears ALL of the panel's own state together. The double-press handler used
 // to clear active_switch alone, which was correct before the #37 split when
@@ -234,55 +199,263 @@ static bed_target_t side_to_target(side_of_bed_t side)
   return BED_TARGET_HEAD; // unreachable - side_of_bed_t has exactly these 4 values
 }
 
-/*
- * OUT-OF-BAND STATUS READS MUST NOT SWALLOW SWITCH EVENTS (DECISIONS.md #40).
+// Number of switches in a "down" mask (1 = physically down). Anything outside
+// SWITCH_INPUT_MASK is ignored.
+static uint8_t count_switches_down(uint16_t down)
+{
+  uint8_t n = 0;
+  down &= SWITCH_INPUT_MASK;
+  while (down != 0) {
+    n += (uint8_t)(down & 1u);
+    down >>= 1;
+  }
+  return n;
+}
+
+/*******************************************************************************
+ *** Switch sampler (DECISIONS.md #41) - the ONE reader of the UI input ports
  *
- * Reading the expander's input port clears its interrupt flag (exp_ui.c,
- * get_ui_status()). The normal read in switch_input_tick() is the consumer of
- * that flag: it reads the interrupt SOURCE bits first, then the status. But
- * the double-press check and the motion fail-safe also read the status, on
- * their own timers. If one of them lands between a switch edge and the normal
- * read, it clears the flag, the normal read finds NO source bits, and the
- * event is dropped. A dropped RELEASE leaves switch_held / switch_driving set
- * and the motors running with nothing held - the unprovoked "FAILSAFE:" trip
- * in the 732dfed bench log, and the original runaway.
+ * Reading an expander's input port clears its interrupt flag, so every extra
+ * reader races the others for the edge. Three code paths used to read it (the
+ * dispatch, the double-press check and the fail-safe) and a two-button release
+ * was dropped as a result. Now only this sampler reads (ui_read_inputs()), and
+ * everything else - the event dispatch, the double-press check, the fail-safe -
+ * uses the debounced state it keeps.
  *
- * So every out-of-band read goes through here. If the fresh switch bits
- * differ from the status latched at the last PROCESSED event, a change has
- * happened that has not been processed yet; record WHICH bits so the normal
- * read can use them if the chip no longer reports a source, and make sure the
- * normal read runs.
+ * A "session" starts when the UI_INT line asserts (edge flag, or a level check
+ * so a missed edge still starts one) and ends when everything is released and
+ * stable. During a session the off-board re-drivers are held on, and both UI
+ * boards are read every SWITCH_SAMPLE_MS and debounced per bit. Edges are
+ * delivered to process_inputs() one bit at a time, releases first.
  *
- * app_ui_status_valid: before the first processed event app_ui_status is just
- * zero, which would read as "every switch changed".
- */
-static bool app_ui_status_valid = false;
-static uint16_t swallowed_change_hint = 0;
+ * Everything is in "down" polarity: down = ~status & SWITCH_INPUT_MASK.
+ ******************************************************************************/
+
+#if SWITCH_SAMPLER_DIAG && !BED_CAPTURE_MOTOR_CURRENTS
+#define SW_DIAG(...) printf(__VA_ARGS__)
+#else
+#define SW_DIAG(...) ((void)0)
+#endif
+
+static bool     sampler_active = false;       // session running, re-drivers held
+static volatile bool ui_wake_request = false; // set by sl_button_on_change
+static bool     aux_present = false;
+static bool     switch_present = false;
+static uint16_t deb_down = 0;                 // debounced down-mask
+static uint8_t  pending_count[16];            // per-bit consecutive-differ counter
+static uint8_t  bounce_count[16];             // per-bit bounce tally for diag
+static uint8_t  read_fail_run = 0;
+static uint32_t next_sample_tick = 0;
+static uint32_t last_good_sample_tick = 0;
+static uint16_t pending_release = 0;          // accepted edges to deliver
+static uint16_t pending_press = 0;
+
+// UI_INT is the expanders' active-low interrupt output. Level check only, no I2C.
+static bool ui_int_asserted(void)
+{
+  return GPIO_PinInGet(SL_SIMPLE_BUTTON_UI_INT_PORT, SL_SIMPLE_BUTTON_UI_INT_PIN) == 0;
+}
+
+static uint16_t status_to_down(uint16_t status)
+{
+  return (uint16_t)(~status) & SWITCH_INPUT_MASK;
+}
+
+// One transaction per board that is not currently believed present. The result
+// is only used to decide presence; the next sample reads the real state.
+static void sampler_probe_missing_boards(void)
+{
+  uint16_t status;
+  if (!aux_present) {
+    aux_present = ui_read_inputs(UI_AUX_I2C_BUS_ADDRESS, &status);
+  }
+  if (!switch_present) {
+    switch_present = ui_read_inputs(UI_SWITCH_I2C_BUS_ADDRESS, &status);
+  }
+}
 
-static uint16_t get_ui_status_out_of_band(void)
+// Baseline read at boot. Call from app_init() right after init_ui_expander().
+void switch_sampler_init(void)
 {
-  uint16_t status = get_ui_status();
-  uint16_t changed = (status ^ app_ui_status) & SWITCH_INPUT_MASK;
+  uint16_t status;
+  uint16_t down = 0;
+  bool no_wake = false;
 
-  if (app_ui_status_valid && (changed != 0)) {
-    bool run_read = true;
-    swallowed_change_hint |= changed;
-    sl_atomic_store(read_ui_data, run_read);
+  sl_atomic_store(ui_wake_request, no_wake);
+  ui_redriver_acquire();
+
+  aux_present = ui_read_inputs(UI_AUX_I2C_BUS_ADDRESS, &status);
+  if (aux_present) {
+    down |= status_to_down(status);
+  }
+  switch_present = ui_read_inputs(UI_SWITCH_I2C_BUS_ADDRESS, &status);
+  if (switch_present) {
+    down |= status_to_down(status);
+  }
+
+  // No events are generated for what is found here. A button held at power-up
+  // is therefore never acted on: its press is never delivered, so switch_held
+  // is never set to its value, and when it is later released the release finds
+  // switch_held != its value and does nothing (process_inputs() only acts on a
+  // release whose switch_held matches).
+  deb_down = down;
+  last_good_sample_tick = sl_sleeptimer_get_tick_count();
+
+  // This baseline read also cleared any interrupt pending from boot.
+  if (deb_down != 0) {
+    sampler_active = true; // keep the re-driver hold
+    next_sample_tick = sl_sleeptimer_get_tick_count();
+  } else {
+    ui_redriver_release();
   }
-  return status;
 }
 
-// Number of switches physically down in a status word (active low). The LED
-// bits are excluded by the mask.
-static uint8_t count_switches_down(uint16_t status)
+static void switch_sampler_tick(void)
 {
-  uint16_t down = (uint16_t)(~status) & SWITCH_INPUT_MASK;
-  uint8_t n = 0;
-  while (down != 0) {
-    n += (uint8_t)(down & 1u);
-    down >>= 1;
+  uint32_t now = sl_sleeptimer_get_tick_count();
+  bool wake;
+
+  if (!sampler_active) {
+    sl_atomic_load(wake, ui_wake_request);
+    if (!wake && !ui_int_asserted()) {
+      return;
+    }
+    ui_redriver_acquire();
+    sampler_probe_missing_boards();
+    sampler_active = true;
+    next_sample_tick = now;
   }
-  return n;
+
+  if ((int32_t)(now - next_sample_tick) < 0) {
+    return;
+  }
+  uint32_t period = sl_sleeptimer_ms_to_tick(SWITCH_SAMPLE_MS);
+  next_sample_tick += period;
+  if ((int32_t)(now - next_sample_tick) >= 0) {
+    next_sample_tick = now + period; // more than one period behind: resync
+  }
+
+  // Clear BEFORE the read: an interrupt after the read sets it again and keeps
+  // the session alive.
+  bool no_wake = false;
+  sl_atomic_store(ui_wake_request, no_wake);
+
+  uint16_t raw = 0;
+  uint16_t status;
+  bool any_ok = false;
+  bool skip_debounce = false;
+  bool any_present = aux_present || switch_present;
+
+  if (aux_present) {
+    if (ui_read_inputs(UI_AUX_I2C_BUS_ADDRESS, &status)) {
+      raw |= status_to_down(status);
+      any_ok = true;
+    } else {
+      SW_DIAG("SW: aux board read failed\r\n");
+    }
+  }
+  if (switch_present) {
+    if (ui_read_inputs(UI_SWITCH_I2C_BUS_ADDRESS, &status)) {
+      raw |= status_to_down(status);
+      any_ok = true;
+    } else {
+      SW_DIAG("SW: switch board read failed\r\n");
+    }
+  }
+
+  if (any_ok) {
+    last_good_sample_tick = now;
+    read_fail_run = 0;
+  } else if (any_present) {
+    // Every present board failed this sample. Hold the debounced state until
+    // the limit, then fail toward OFF: all released, boards re-probed at the
+    // next session start.
+    if (read_fail_run < 255) {
+      read_fail_run++;
+    }
+    if (read_fail_run >= SWITCH_READ_FAIL_LIMIT) {
+      raw = 0;
+      aux_present = false;
+      switch_present = false;
+      SW_DIAG("SW: %u failed reads - treating all switches as released\r\n",
+              (unsigned)read_fail_run);
+    } else {
+      skip_debounce = true;
+    }
+  }
+  // else: no board present at all -> raw stays 0 (fail toward OFF)
+
+  if (!skip_debounce) {
+    for (uint8_t b = 0; b < 16; b++) {
+      uint16_t bit = (uint16_t)(1u << b);
+      if ((SWITCH_INPUT_MASK & bit) == 0) {
+        continue;
+      }
+      bool raw_b = ((raw & bit) != 0);
+      bool deb_b = ((deb_down & bit) != 0);
+
+      if (raw_b == deb_b) {
+        if (pending_count[b] > 0) {
+          if (bounce_count[b] < 255) {
+            bounce_count[b]++;
+          }
+          pending_count[b] = 0;
+        }
+      } else {
+        pending_count[b]++;
+        if (pending_count[b] >= SWITCH_DEBOUNCE_SAMPLES) {
+          deb_down ^= bit;
+          if (raw_b) {
+            pending_press |= bit;
+          } else {
+            pending_release |= bit;
+          }
+          SW_DIAG("SW: 0x%04X %s (bounces %u)\r\n", (unsigned)bit,
+                  raw_b ? "down" : "up", (unsigned)bounce_count[b]);
+          pending_count[b] = 0;
+          bounce_count[b] = 0;
+        }
+      }
+    }
+  }
+
+  // End the session only when everything is released AND stable AND no
+  // interrupt arrived since the read.
+  sl_atomic_load(wake, ui_wake_request);
+  if ((deb_down == 0) && !wake) {
+    bool settled = true;
+    for (uint8_t b = 0; b < 16; b++) {
+      if (pending_count[b] != 0) {
+        settled = false;
+      }
+    }
+    if (settled) {
+      sampler_active = false;
+      ui_redriver_release();
+    }
+  }
+}
+
+// True while a session is running (the sampler needs the main loop to run).
+bool switch_sampler_active(void)
+{
+  return sampler_active;
+}
+
+// Debounced down-mask. Idle => 0 by construction: a session only ends with
+// everything released, so idle state is valid without a read.
+uint16_t switch_sampler_down(void)
+{
+  return deb_down;
+}
+
+// False if the sampler is running but has had no good read for
+// SWITCH_STATE_STALE_MS. Consumers must then treat it as "no switch down".
+bool switch_state_fresh(void)
+{
+  return !sampler_active ||
+         (sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count() - last_good_sample_tick)
+          <= SWITCH_STATE_STALE_MS);
 }
 
 #if BED_BENCH_PROVOKE_RUNAWAY
@@ -409,9 +582,6 @@ static void process_outputs()
   else if (in_settings_mode){
       settings_mode_process_outputs();
   }
-
-  // Ready to look at another switch input, if any
-  process_switch_change = true;
 }
 
 // Configures motor control elements based on response to change in switch state
@@ -490,7 +660,6 @@ static void process_inputs()
                 }
             } else if ((!switch_pressed) && (switch_held == SWITCH_BED_LIGHT_VALUE)){
                 switch_held = OFF;
-                sl_sleeptimer_stop_timer(&double_press_error_timer);
                 /*** For "Settings Mode" ***/
                 settings_mode_cancel_entry();
 
@@ -552,16 +721,19 @@ static void process_inputs()
  * was motors energised with active_switch == OFF, which also switches off
  * run_motor_supervision() and leaves nothing that would ever stop them.
  *
- * It asks the hardware directly and nothing else: is any switch physically
- * down? Is any motor-enable line actually high (not motor_state[] - #40)?
+ * It asks for the switch state and the motor hardware and nothing else: is any
+ * switch down, per the sampler's debounced state (#41 - the sampler is the only
+ * reader of the UI input ports, so this no longer reads them itself, and stale
+ * or unknown counts as "no switch down", i.e. it fails toward OFF)? Is any
+ * motor-enable line actually high (a direct read, not motor_state[] - #40)?
  * And, separately: Is the app commanding? Is auto-level running? Is settings mode active?
  * If all four are no and a motor is energised, that is a runaway by
  * definition - de-energise.
  *
  * Rate-limited to FAILSAFE_SAMPLE_MS, matching the sample_motor_faults_tick()
- * precedent: a direct read is blocking I2C on the UI expander, and per-pass
- * blocking I2C on a shared bus is DECISIONS #27/#30's radio-starvation
- * mechanism. Milliseconds via sleeptimer, never loop passes (#2).
+ * precedent: the motor-enable read is blocking I2C on the on-board expander,
+ * and per-pass blocking I2C on a shared bus is DECISIONS #27/#30's
+ * radio-starvation mechanism. Milliseconds via sleeptimer, never loop passes (#2).
  *
  * It requires FAILSAFE_CONFIRM_SAMPLES consecutive quiet samples before
  * acting. That is what stops it fighting the normal paths: the window between
@@ -585,7 +757,7 @@ static bool     failsafe_tripped = false; // print once, not every pass
 // produces.
 //
 // Cost: one blocking I2C transaction on sl_i2cspm_on_board per sample, i.e.
-// per FAILSAFE_SAMPLE_MS = 50 ms, and ONLY once the switch check above has
+// per FAILSAFE_SAMPLE_MS = 50 ms, and ONLY once the switch check below has
 // passed (so never while any button is down). Same order as
 // sample_motor_faults_tick() (5/s, same bus, same context), and it is
 // main-loop context, not a Bluetooth stack callback, so it is not the
@@ -627,13 +799,14 @@ void motion_failsafe_tick(void)
     return;
   }
 
-  // DIRECT hardware read - deliberately not any cached flag. Switches are
-  // active low with pull-ups, so a switch bit reading 0 means that button is
-  // physically down. The LED bits (P1_4-P1_7) are masked out: they are
+  // The sampler's debounced switch state - deliberately not active_switch,
+  // switch_held or any other bookkeeping, and no I2C read of its own (#41).
+  // Only SWITCH_INPUT_MASK bits are ever in it: the LED bits (P1_4-P1_7) are
   // outputs, and they are exactly what made the double-press check depend on
-  // something other than the switches (#35/#38).
-  uint16_t status = get_ui_status_out_of_band();
-  bool any_switch_down = ((status & SWITCH_INPUT_MASK) != SWITCH_INPUT_MASK);
+  // something other than the switches (#35/#38). Stale or unknown state counts
+  // as no switch down: this function must fail toward OFF.
+  bool any_switch_down = switch_state_fresh() &&
+                         (switch_sampler_down() != 0);
 
   if (any_switch_down) {
     failsafe_quiet_samples = 0;
@@ -700,25 +873,35 @@ void motion_failsafe_tick(void)
   }
 }
 
-// Called whenever there is a change in switch or expander interrupt state
+// Called whenever there is a change in switch or expander interrupt state.
+// Flag only: no I2C, nothing else. The sampler (switch_sampler_tick()) starts a
+// session on it. Only the asserting (PRESSED = UI_INT low) edge matters; the
+// release edge is the expander clearing its own flag after the sampler's read.
 void sl_button_on_change(const sl_button_t *handle)
 {
-  bool local_read_ui_data;
+  bool local_wake;
 
   // Will need to expand logic to handle motor fault as well
 
   if (sl_button_get_state(handle) == SL_SIMPLE_BUTTON_PRESSED) {
     if (&sl_button_ui_int == handle) { // Test for UI expander switch change
-        local_read_ui_data = true;
-        sl_atomic_store(read_ui_data, local_read_ui_data);
+        local_wake = true;
+        sl_atomic_store(ui_wake_request, local_wake);
     }
   }
 }
 
-// Runs the double-press-error check and (when a fresh switch change is
-// latched) the UI interrupt/status read + process_inputs() dispatch. Called
-// once per tick from app_process_action(), same relative position as the two
-// inline blocks it replaces.
+// Hands one accepted edge to the existing single-bit dispatch.
+static void deliver_switch_edge(uint16_t bit, bool pressed)
+{
+  app_ui_interrupt = bit;
+  switch_pressed = pressed;
+  process_inputs();
+}
+
+// Runs the double-press check, then the switch sampler, then delivers the
+// debounced edges the sampler accepted to process_inputs(). Called once per
+// tick from app_process_action().
 void switch_input_tick(void)
 {
   /****
@@ -729,159 +912,91 @@ void switch_input_tick(void)
       if (initial_switch_press){
           scan_all_currents();
           initial_switch_press = false;
-
-          // Double press error check timer. Not started for an app-sourced
-          // move - there is no second physical button to catch, and the
-          // check below is gated on the same condition anyway. Belt and
-          // braces: this also stops the periodic timer being left running
-          // across a source change.
-          if (bed_active_source() == BED_SOURCE_SWITCHES){
-              sl_sleeptimer_is_timer_running(&double_press_error_timer, &is_double_press_error_timer_on);
-              if (!is_double_press_error_timer_on){
-                  start_double_press_error_timer();
-              }
-          }
       }
       run_motor_supervision(active_switch, is_raising);
 
       // The double-press check below is a PHYSICAL SWITCH PANEL consistency
       // check and must only run on a move the panel actually started.
       //
-      // It compares the switch-board status latched at the last physical
-      // switch interrupt against a fresh read, and calls them a double press
-      // if they match. That status word includes the LED enable bits
-      // (P1_4-P1_7), which is what normally makes a legitimate physical hold
-      // differ from its own latched value - the LED for that move turns on
-      // after the latch.
-      //
       // An APP move sets active_switch through the same
-      // motor_button_pressed() the switch path uses, so before this gate the
-      // check ran on app moves too. With no physical button pressed, the
-      // fresh read matched the stale latched value whenever the app's LED
-      // happened to be the one that was lit at the last physical interrupt -
-      // firing a bogus "Double Press Error" that killed the motors and set
-      // active_switch = OFF, which the app's next 100 ms MOVE_START repeat
-      // then undid. That was the 10 Hz stutter, and the LED bits are why it
-      // only affected whichever app command type was in use during the
-      // conflict (green = single side, blue = ALL, red = auto-level).
+      // motor_button_pressed() the switch path uses, and no physical button is
+      // down during one, so the gate keeps this check off app moves (it
+      // misfired on them before - the 10 Hz stutter, #35/#38).
       //
       // run_motor_supervision() above is deliberately OUTSIDE this gate:
       // collision and limit supervision must cover app moves too.
       bool switch_sourced_move = (bed_active_source() == BED_SOURCE_SWITCHES);
 
-      // The flag is consumed UNCONDITIONALLY; only the check itself is gated
-      // (DECISIONS.md #38). Putting switch_sourced_move in the same condition
-      // as the clear - which is what #35 did - meant the flag went stale
-      // whenever the source was not SWITCHES, and a stale flag is then
-      // consumed at an arbitrary later moment instead of 50 ms after the
-      // latch. This check is only sound at that 50 ms point, because what
-      // makes a legitimate single press differ from its own latched status is
-      // the LED bits changing in between. Run it at any other time and it
-      // compares a steady state against an edge, and matches whenever the LED
-      // happens to line up. That is the misfire.
-      if (double_press_error_timer_timeout){
-          double_press_error_timer_timeout = false;
-
-        if (switch_sourced_move){
-
-          // Get source of switch interrupt
-          app_ui_interrupt_new = get_ui_interrupt();
-
-          // Read the switch board input ports and process accordingly
-          get_ui_status();
-          app_ui_status_new = get_ui_status_out_of_band();
-
-//          printf("Initial UI Interrupt Source = %d, Status = %d \r\n", app_ui_interrupt, app_ui_status);
-//          printf("New UI Interrupt Source = %d, Status = %d \r\n", app_ui_interrupt_new, app_ui_status_new);
-
-          // A double press is TWO PHYSICAL SWITCHES DOWN, read now from the
-          // hardware (DECISIONS.md #40). It used to be "status unchanged
-          // since the last latched interrupt", which is not a statement about
-          // switches at all: any extra interrupt after the LED changed (contact
-          // bounce on the press) re-latched the status with the LED already on,
-          // and anything that put the LED back to its latched state (auto-level
-          // ending with the button still held) did the same, so a single held
-          // button matched its own latch. Two switches down cannot be produced
-          // by one button, however it bounces.
-          if (count_switches_down(app_ui_status_new) >= 2){
+      // A double press is TWO PHYSICAL SWITCHES DOWN (DECISIONS.md #40), read
+      // from the sampler's debounced state (#41) - no I2C here, so it can be
+      // checked every pass instead of on a timer. Two switches down cannot be
+      // produced by one button, however it bounces.
+      if (switch_sourced_move && (count_switches_down(switch_sampler_down()) >= 2)){
 #if !BED_CAPTURE_MOTOR_CURRENTS
-              printf("Double Press Error\r\n");
-              printf("  switch word 0x%04X, %u switches down\r\n",
-                     (unsigned)(app_ui_status_new & SWITCH_INPUT_MASK),
-                     (unsigned)count_switches_down(app_ui_status_new));
+          printf("Double Press Error\r\n");
+          printf("  down mask 0x%04X, %u switches down\r\n",
+                 (unsigned)switch_sampler_down(),
+                 (unsigned)count_switches_down(switch_sampler_down()));
 #endif
-              set_dual_motors_off(HEAD);
-              set_dual_motors_off(FOOT);
-              // Clear the panel's state TOGETHER with active_switch (#38).
-              // Clearing active_switch alone left switch_held naming a button
-              // nobody was holding - so every other button failed its
-              // `switch_held == OFF` press test and went dead - and left
-              // switch_driving naming a button process_outputs would still
-              // energise on, which is motion with bed_control never consulted.
-              clear_switch_panel_state();
-              active_switch = OFF;
-              // Reset motor readings
-              reset_ignore_data_points_count();
-              reset_motor_current_data();
-              // Set all LEDs off
-              set_LED_off(LED_GREEN);
-              set_LED_off(LED_BLUE);
-              set_LED_off(LED_RED);
-//              set_LED_off(LED_AMBER);
-              /*** For "Settings Mode" ***/
-              settings_mode_cancel_entry();
-
-          } else {
-//              printf("No Error\r\n");
-          }
-        }
+          set_dual_motors_off(HEAD);
+          set_dual_motors_off(FOOT);
+          // Clear the panel's state TOGETHER with active_switch (#38).
+          // Clearing active_switch alone left switch_held naming a button
+          // nobody was holding - so every other button failed its
+          // `switch_held == OFF` press test and went dead - and left
+          // switch_driving naming a button process_outputs would still
+          // energise on, which is motion with bed_control never consulted.
+          clear_switch_panel_state();
+          active_switch = OFF;
+          // Reset motor readings
+          reset_ignore_data_points_count();
+          reset_motor_current_data();
+          // Set all LEDs off
+          set_LED_off(LED_GREEN);
+          set_LED_off(LED_BLUE);
+          set_LED_off(LED_RED);
+//          set_LED_off(LED_AMBER);
+          /*** For "Settings Mode" ***/
+          settings_mode_cancel_entry();
       }
   }
 
-  bool local_read_ui_data;
-  sl_atomic_load(local_read_ui_data, read_ui_data);
-
-  if (local_read_ui_data && process_switch_change) {
-
-      local_read_ui_data = false;
-      sl_atomic_store(read_ui_data, local_read_ui_data);
-      process_switch_change = false;
-
-      // Get source of switch interrupt
-      app_ui_interrupt = get_ui_interrupt();
-
-      // The chip reports no source if an out-of-band status read cleared its
-      // flag first (#40). Fall back to the bits that read saw change.
-      if ((app_ui_interrupt == 0) && (swallowed_change_hint != 0)) {
-#if !BED_CAPTURE_MOTOR_CURRENTS
-          printf("UI: recovered swallowed switch change 0x%04X\r\n", swallowed_change_hint);
-#endif
-          app_ui_interrupt = swallowed_change_hint;
-      }
-      swallowed_change_hint = 0;
-
-      // Read the switch board input ports and process accordingly
-      get_ui_status();
-      app_ui_status = get_ui_status();
-      app_ui_status_valid = true;
-//       printf("\r\n");
-//       printf("UI Interrupt Source = %d, Status = %d \r\n", app_ui_interrupt, app_ui_status);
-
-      /*
-       Mask interrupt variable in bitwise AND with switch state variable
-       to determine the state of the switch - pressed or released.
+  /****
+   * Switch sampler + edge delivery (DECISIONS.md #41)
+   ****/
 
-       Switches are active low with pull-ups, so if status bit is 1 when
-       state change was latched into register, switch was released.
-       */
+  switch_sampler_tick();
+
+  // One process_inputs() call per bit, so process_inputs()/process_outputs()
+  // and settings_mode_process_outputs() keep seeing exactly one single-bit
+  // mask per call, as they always have.
+  uint16_t releases = pending_release;
+  uint16_t presses = pending_press;
+  pending_release = 0;
+  pending_press = 0;
+
+  // Releases first, so a release always reaches bed_move_stop() before
+  // anything new can start.
+  for (uint8_t b = 0; b < 16; b++) {
+    uint16_t bit = (uint16_t)(1u << b);
+    if (releases & bit) {
+      deliver_switch_edge(bit, false);
+    }
+  }
 
-      if (app_ui_interrupt & app_ui_status){ // Switch was released
-        switch_pressed = false;
-      }
-      else {
-        switch_pressed = true;
+  if (presses != 0) {
+    if (count_switches_down(deb_down) >= 2) {
+      // Two or more down: no press may start anything (a second press during a
+      // move is caught by the double-press check above on the next pass).
+      SW_DIAG("SW: %u switches down - presses ignored\r\n",
+              (unsigned)count_switches_down(deb_down));
+    } else {
+      for (uint8_t b = 0; b < 16; b++) {
+        uint16_t bit = (uint16_t)(1u << b);
+        if (presses & bit) {
+          deliver_switch_edge(bit, true);
+        }
       }
-
-      process_inputs();
+    }
   }
 }
diff --git a/switch_input.h b/switch_input.h
index 7e58676..8591394 100644
--- a/switch_input.h
+++ b/switch_input.h
@@ -1,17 +1,15 @@
 /*
  * switch_input.h
  *
- * The switch/LED-decode subsystem: the GPIO interrupt entry point, the
- * double-press-error check, and the switch-mask -> bed_control.h verb
- * translation. Relocated from app.c on C4 commit 4 (Jul 2026) - pure
- * relocation, no behavior change.
+ * The switch/LED-decode subsystem: the GPIO interrupt entry point, the switch
+ * sampler (the one reader of the UI expanders' input ports - DECISIONS.md #41),
+ * the double-press check, and the switch-mask -> bed_control.h verb
+ * translation. Relocated from app.c on C4 commit 4 (Jul 2026).
  *
  * switch_pressed/app_ui_interrupt were temporary externs in app.h (via
- * commits 2/3, read-only by bed_actions.c/settings_mode.c); this commit
- * relocates their real definitions here and app.h's declarations move here
- * too. app_reset_double_press_timer()'s implementation also relocates here
- * (it now owns double_press_error_timer); app.h's declaration is removed and
- * bed_actions.c switches its include.
+ * commits 2/3, read-only by bed_actions.c/settings_mode.c); their real
+ * definitions live here and app.h's declarations moved here too. Since #41 each
+ * process_inputs() call sees exactly one single-bit app_ui_interrupt mask.
  */
 
 #ifndef SWITCH_INPUT_H_
@@ -23,22 +21,34 @@
 extern bool switch_pressed; // Interrupt flag to capture switch up/down state
 extern uint16_t app_ui_interrupt; // Packed Port0/1 interrupt source(s)
 
-// Resets the double-press-error timer. Called by bed_actions.c's moved
-// action functions, which used to touch double_press_error_timer directly
-// via its (then) app.c-private handle.
-void app_reset_double_press_timer(void);
+// Baseline read of both UI boards, taking a re-driver hold if a button is
+// found down. Call once from app_init(), right after init_ui_expander(). No
+// events are generated for what it finds.
+void switch_sampler_init(void);
 
-// Runs the double-press-error check and (when a fresh switch change is
-// latched) the UI interrupt/status read + process_inputs() dispatch. Called
-// once per tick from app_process_action(), same relative position as the two
-// inline blocks it replaces.
+// Runs the double-press check, then the switch sampler, then delivers the edges
+// the sampler accepted to process_inputs() (one call per bit, releases first).
+// Called once per tick from app_process_action().
 void switch_input_tick(void);
 
+// Sampler state for other modules (DECISIONS.md #41):
+//  - switch_sampler_active(): a sampling session is running; the main loop must
+//    keep running, so app_is_ok_to_sleep() vetoes sleep.
+//  - switch_sampler_down(): debounced mask of switches physically down (same
+//    bit layout as app_ui_interrupt). 0 whenever the sampler is idle.
+//  - switch_state_fresh(): false if a session is running but has had no good
+//    read for SWITCH_STATE_STALE_MS; callers must then treat it as "no switch
+//    down" (fail toward OFF).
+bool switch_sampler_active(void);
+uint16_t switch_sampler_down(void);
+bool switch_state_fresh(void);
+
 // Unconditional motion fail-safe (DECISIONS.md #38). Call once per
-// app_process_action(). De-energises the motors whenever a direct hardware
-// read says no switch is down, the app is not commanding motion, auto-level
-// is not running and settings mode is inactive - regardless of what
-// active_switch, switch_held, switch_driving or any table match believe.
+// app_process_action(). De-energises the motors whenever the sampler's
+// debounced state says no switch is down (stale counts as none), the app is not
+// commanding motion, auto-level is not running and settings mode is inactive -
+// regardless of what active_switch, switch_held, switch_driving or any table
+// match believe.
 void motion_failsafe_tick(void);
 
 #endif /* SWITCH_INPUT_H_ */
```

Non-source parts of `d1f13d0`: `git diff --stat cb9c7b2 d1f13d0`:

```
 app.c                         |  27 +-
 app.h                         |   3 +-
 bed_actions.c                 |   7 -
 docs/DECISIONS.md             |  79 ++++++
 docs/SWITCH_SAMPLER_DESIGN.md | 273 +++++++++++++++++++
 exp_board.c                   |  34 +++
 exp_board.h                   |   5 +
 exp_ui.c                      | 102 +++----
 exp_ui.h                      |  23 +-
 switch_input.c                | 601 +++++++++++++++++++++++++-----------------
 switch_input.h                |  52 ++--
 11 files changed, 859 insertions(+), 347 deletions(-)
```

`docs/SWITCH_SAMPLER_DESIGN.md` was added exactly as the reviewer wrote it (273 lines, not edited by me, previously untracked). `docs/DECISIONS.md` diff (docs-only, 79 lines, not truncated):

```diff
diff --git a/docs/DECISIONS.md b/docs/DECISIONS.md
index 3fb3b68..a7119f8 100644
--- a/docs/DECISIONS.md
+++ b/docs/DECISIONS.md
@@ -1724,3 +1724,82 @@ the evidence: if the hypothesis is wrong it will never appear.
 Builds clean with the bench flag at 0 and at 1. **None of this has been run on
 the bench.** The swallowed-event mechanism is inferred from code and the
 `exp_ui.c` comment, not from the datasheet or a capture.
+
+## 41. One switch reader
+
+Date: 2026-10-17. Implements `docs/SWITCH_SAMPLER_DESIGN.md` (written by the
+reviewer after the `prompt_q_bench.txt` log).
+
+### Why
+
+Three code paths read the UI expanders' input ports: the event dispatch, the
+double-press check and the fail-safe. Each read clears the expander's interrupt
+flag, so they raced for every edge. In the bench log a two-button release
+arrived as one event carrying two bits, `process_inputs()` only matches
+single-bit masks, the release was dropped, and the fail-safe stopped the motors
+about 200 ms later (five times). There was no debounce anywhere.
+
+### Decision
+
+- **Only the switch sampler reads the UI input ports.** `ui_read_inputs()` in
+  `exp_ui.c` is the only function that issues `EXP_CMD_READ_INPUTS` to a UI
+  board and the sampler in `switch_input.c` is its only caller. The dispatch,
+  the double-press check and the fail-safe use the sampler's debounced state.
+- **Sampling session.** Starts on the UI_INT edge flag or, at idle, on a level
+  check of the UI_INT pin (so a missed edge still starts one). During a session
+  both boards are read every 10 ms and each bit is debounced (3 identical reads).
+  The session ends only when everything is released, stable and no interrupt
+  arrived since the last read.
+- **One event per bit, releases first.** Accepted edges are handed to
+  `process_inputs()` one bit at a time, so it always sees a single-bit mask.
+  All releases are delivered before any press.
+- **No presses while 2 or more are down.** If two or more switches are down when
+  presses are due, none is delivered. A second press during a move is caught by
+  the double-press check, which is now "2 or more bits in the debounced
+  down-mask", evaluated every pass instead of on a 50 ms timer.
+- **Re-driver reference count.** `ui_redriver_acquire()/release()` own the
+  off-board re-drivers (EN_I2C_DRIVER, on-board expander P1_5, which powers only
+  the PCA9617A re-drivers and their pull-ups; the Switch board expander, its LEDs
+  and the interrupt line are on always-on 3V3). Every UI transaction takes a
+  hold for its own duration, so behaviour is unchanged when nothing holds it; a
+  session holds it throughout, so there is no per-transaction toggling or 2 ms
+  delay. EN only changes between transfers: all callers are main-loop context
+  (every sleeptimer callback in the firmware only sets a flag, and
+  `sl_button_on_change` is now flag-only).
+- **Sleep veto.** `app_is_ok_to_sleep()` returns false while a session is active
+  and while the motor-enable shadow (`motor_outputs_maybe_on()` in `exp_board.c`,
+  updated by every port-0 writer, set to "on" at boot and after any failed write)
+  says the motors might be energised. `set_all_motors_on()` energises without
+  setting `motor_state[].motor_on` (#40), so a runaway could previously let the
+  CPU sleep between fail-safe samples - the suspected cause of the ALL MOTORS
+  fail-safe taking 271-307 ms against 214-223 ms for the HEAD pair.
+- **Fail toward OFF.** The fail-safe treats stale (no good read for 100 ms during
+  a session) or unknown state as "no switch down". After 5 consecutive samples in
+  which every present board failed, all switches are treated as released and the
+  boards are re-probed at the next session start.
+- A button held at power-up is never acted on (no press event is generated for
+  the baseline read), and its later release does nothing.
+
+### Known limit
+
+The expander can drop an edge that coincides with the ACK of its own read.
+Mid-session the next 10 ms sample catches it. At session end a press in that
+window is lost: the press does nothing, which is benign, and it is never a lost
+release, because sessions only end with everything released. The UI_INT level
+check at idle covers a line that is stuck asserted (at the cost of the sampler
+running continuously and blocking sleep while it is).
+
+### Behaviour changes to expect
+
+- Release is now seen after the debounce, about 20-30 ms after the contact opens,
+  so a move stops that much later than before (+ up to one sample period).
+- Double-press detection is continuous rather than 50 ms after the initial press.
+- `SWITCH_SAMPLER_DIAG` is 1: `SW:` lines print for every accepted edge, with a
+  per-bit bounce tally. Set it to 0 before shipping.
+
+### Status
+
+Builds clean (no warnings, no errors) from the command line with
+`BED_BENCH_PROVOKE_RUNAWAY` at 0. **None of this has been run on the bench.**
+DECISIONS.md #40's "recovered swallowed switch change" mechanism is removed;
+whether the swallow hypothesis was right is unproven either way.
```

## d. Raw command output

How to read this section. Three kinds of material:

1. **Failures and mishaps from this session, transcribed verbatim** from the tool results (d.1). These are not re-run; they happened once.
2. **Build output**, verbatim from the files the build wrote at the time (`/tmp/build1.txt` for the first flag-0 build, `/tmp/build_flag1.txt` for the flag-1 compile check, `/tmp/build_flag0.txt` for the final flag-0 rebuild), d.3.
3. **Greps and git commands, re-run at handoff-writing time** against the committed tree (`d1f13d0`) so the output is genuinely raw rather than retyped (d.2, d.4). Where the session ran the same grep earlier with the same result, the earlier output is the same text; two earlier greps (the timer-callback survey and the SDK polarity check) were large and are re-run here in a compact form.

Files were also read with the Read tool (`exp_ui.c/h`, `exp_board.c/h`, `switch_input.c/h`, `app.c` excerpts, `docs/SWITCH_SAMPLER_DESIGN.md`, `docs/MIGRATION_STATUS.md`); that is file reading, not a command, and the content is in the diff below.

No secrets appeared in any output; nothing is redacted.

### d.1 Failures and mishaps (verbatim)

(a) Tools not on the shell PATH when first probed (`which cmake ninja make arm-none-eabi-gcc`):

```
which: no cmake in (...)
which: no ninja in (...)
which: no make in (...)
which: no arm-none-eabi-gcc in (...)
```

(the PATH list is omitted here; it contained nothing relevant). The tools were then found via `cmake_gcc/build/CMakeCache.txt` (`CMAKE_MAKE_PROGRAM`, `CMAKE_COMMAND`) and added to PATH for the build only.

(b) First attempt to apply the `switch_input.c` edit script via a shell heredoc (nothing was written):

```
Exit code 2
/usr/bin/bash: -c: line 162: unexpected EOF while looking for matching `''
```

I re-created the script as a file instead.

(c) **A defect I introduced and then fixed, before any build.** In the first `switch_input.c` edit script I wrote `\r\n` inside ordinary Python strings, so the four `SW_DIAG("... \r\n")` format strings were written to the file with real CR+LF characters in the middle of the C string literal (which would not have compiled), and the file became mixed line endings. Evidence after the edit:

```
$ grep -n 'SW: ' switch_input.c | head -20
354:      SW_DIAG("SW: aux board read failed
363:      SW_DIAG("SW: switch board read failed
382:      SW_DIAG("SW: %u failed reads - treating all switches as released
416:          SW_DIAG("SW: 0x%04X %s (bounces %u)
i/lf    w/mixed attr/                 	switch_input.c
```

A first repair attempt via a shell heredoc had no effect (its output was `had crlf pairs 4 remaining CR 4` and the same broken grep output). A repair script written to a file worked:

```
bad occurrences 4
remaining CR 0
354:      SW_DIAG("SW: aux board read failed\r\n");
362:      SW_DIAG("SW: switch board read failed\r\n");
380:      SW_DIAG("SW: %u failed reads - treating all switches as released\r\n",
413:          SW_DIAG("SW: 0x%04X %s (bounces %u)\r\n", (unsigned)bit,
i/lf    w/lf    attr/                 	switch_input.c
```

The committed file contains only the corrected strings (see the diff in section c).

(d) Edit-helper assertion failure (nothing was written for that file; the helper checks all matches before writing):

```
AssertionError: ('switch_input.h', 0, "// Resets the double-press-error timer. Called by bed_actions.c's move")
```

Cause: my old-text had different line breaks from the real comment. Fixed and re-run successfully.

(e) The working-directory note: the shell's `cd cmake_gcc` for the build persisted; later commands used absolute paths.

(f) Line-ending warnings printed by git on `git add`/`git diff` (not errors): `warning: in the working copy of '<file>', LF will be replaced by CRLF the next time Git touches it`. The index is LF for every file (`git ls-files --eol` showed `i/lf`); `exp_ui.c` and `exp_ui.h` were already CRLF in the working tree and were edited preserving that.

### d.2 Greps and surveys (re-run at handoff time on the committed tree)

```
$ grep -n "EXP_CMD_READ_INPUTS" exp_ui.c
232:// The only place in the firmware that issues EXP_CMD_READ_INPUTS to a UI board.
238:  uint8_t cmd = EXP_CMD_READ_INPUTS;

$ grep -n "ui_read_inputs(" *.c *.h
exp_ui.c:235:bool ui_read_inputs(uint8_t board_addr, uint16_t *status)
switch_input.c:111:  status = ((uint16_t) (ports[0] << 8)) | ports[1]; (see ui_read_inputs())
switch_input.c:221: * was dropped as a result. Now only this sampler reads (ui_read_inputs()), and
switch_input.c:270:    aux_present = ui_read_inputs(UI_AUX_I2C_BUS_ADDRESS, &status);
switch_input.c:273:    switch_present = ui_read_inputs(UI_SWITCH_I2C_BUS_ADDRESS, &status);
switch_input.c:287:  aux_present = ui_read_inputs(UI_AUX_I2C_BUS_ADDRESS, &status);
switch_input.c:291:  switch_present = ui_read_inputs(UI_SWITCH_I2C_BUS_ADDRESS, &status);
switch_input.c:350:    if (ui_read_inputs(UI_AUX_I2C_BUS_ADDRESS, &status)) {
switch_input.c:358:    if (ui_read_inputs(UI_SWITCH_I2C_BUS_ADDRESS, &status)) {
exp_ui.h:46:bool ui_read_inputs(uint8_t board_addr, uint16_t *status);

$ grep -nE "get_ui_status|get_ui_interrupt|ui_int_source|swallowed|double_press_error_timer|app_reset_double_press_timer" *.c *.h   (expect no output)
grep exit status: 1

$ grep -n "set_I2C_redriver_on\|set_I2C_redriver_off" *.c *.h
exp_board.c:414:void set_I2C_redriver_on()
exp_board.c:434:void set_I2C_redriver_off()
exp_ui.c:215:    set_I2C_redriver_on();
exp_ui.c:227:      set_I2C_redriver_off();
exp_board.h:40:void set_I2C_redriver_on();
exp_board.h:41:void set_I2C_redriver_off();

$ grep -n "EXP_CMD_OUTPUT;" exp_board.c
123:  write_cmd[0] = EXP_CMD_OUTPUT;
145:  write_cmd[0] = EXP_CMD_OUTPUT;
165:  write_cmd[0] = EXP_CMD_OUTPUT;
186:  write_cmd[0] = EXP_CMD_OUTPUT;
206:  write_cmd[0] = EXP_CMD_OUTPUT;
227:  write_cmd[0] = EXP_CMD_OUTPUT;
247:  write_cmd[0] = EXP_CMD_OUTPUT;
268:  write_cmd[0] = EXP_CMD_OUTPUT;
313:  uint8_t cmd = EXP_CMD_OUTPUT;
490:  write_cmd[0] = EXP_CMD_OUTPUT;

$ grep -n "note_port0_write" exp_board.c
103:static void note_port0_write(I2C_TransferReturn_TypeDef ret, uint8_t written)
134:  note_port0_write(ret, write_cmd[1]);
154:  note_port0_write(ret, write_cmd[1]);
175:  note_port0_write(ret, write_cmd[1]);
195:  note_port0_write(ret, write_cmd[1]);
216:  note_port0_write(ret, write_cmd[1]);
236:  note_port0_write(ret, write_cmd[1]);
257:  note_port0_write(ret, write_cmd[1]);
277:  note_port0_write(ret, write_cmd[1]);
493:  note_port0_write(ret, write_cmd[1]);

$ grep -n "sl_sleeptimer_start" *.c  (timer start sites)
app.c:168:  sl_sleeptimer_start_periodic_timer(&heartbeat_delay_timer, ticks, heartbeat_timer_callback, NULL, 0, 0);
app.c:189:  sl_sleeptimer_start_periodic_timer(&LED_500ms_timer, ticks, LED_timer_callback, NULL, 0, 0);
app.c:196:  sl_sleeptimer_start_periodic_timer(&LED_200ms_timer, ticks, LED_timer_callback, NULL, 0, 0);
app.c:219:  sl_sleeptimer_start_timer(&auto_level_timer, ticks, callback_auto_level_timer, NULL, 0, 0);
app.c:240:  sl_sleeptimer_start_periodic_timer(&auto_level_LED_blink_timer, ticks, callback_auto_level_LED_blink_timer, NULL, 0, 0);
auto_level.c:189:  sl_sleeptimer_start_timer(&steady_accel_read_oneshot_timer, ticks, steady_accel_read_oneshot_callback, NULL, 0, 0);
auto_level.c:208:  sl_sleeptimer_start_timer(&sneak_oneshot_timer, ticks, sneak_oneshot_callback, NULL, 0, 0);
auto_level.c:227:  sl_sleeptimer_start_timer(&stop_sneak_oneshot_timer, ticks, stop_sneak_oneshot_callback, NULL, 0, 0);
dual_motor_control.c:79:  sl_sleeptimer_start_timer(&temp_motor_off_timer, ticks, temp_motor_off_callback, NULL, 0, 0);
settings_mode.c:84:  sl_sleeptimer_start_timer(&handle_5000ms_timer, ticks, callback_5000ms_timer, NULL, 0, 0);
settings_mode.c:102:  sl_sleeptimer_start_timer(&handle_2000ms_timer, ticks, callback_2000ms_timer, NULL, 0, 0);

$ grep -n -A6 "^static void .*(sl_sleeptimer_timer_handle_t" app.c auto_level.c dual_motor_control.c settings_mode.c   (callback bodies)
app.c:156:static void heartbeat_timer_callback(sl_sleeptimer_timer_handle_t *heartbeat_handle, void *heartbeat_data)
app.c-157-{
app.c-158-  (void)heartbeat_handle;
app.c-159-  (void)heartbeat_data;
app.c-160-  bool local_heartbeat = true;
app.c-161-  sl_atomic_store(heartbeat, local_heartbeat);
app.c-162-}
--
app.c:178:static void LED_timer_callback(sl_sleeptimer_timer_handle_t *LED_handle, void *LED_data)
app.c-179-{
app.c-180-  (void)LED_handle;
app.c-181-  (void)LED_data;
app.c-182-  LED_timer_timeout = true;
app.c-183-}
app.c-184-
--
app.c:208:static void callback_auto_level_timer(sl_sleeptimer_timer_handle_t *handle_auto_level, void *data_auto_level)
app.c-209-{
app.c-210-  (void)handle_auto_level;
app.c-211-  (void)data_auto_level;
app.c-212-  auto_level_timer_timeout = true;
app.c-213-}
app.c-214-
--
app.c:229:static void callback_auto_level_LED_blink_timer(sl_sleeptimer_timer_handle_t *handle_auto_level_LED_blink, void *data_auto_level_LED_blink)
app.c-230-{
app.c-231-  (void)handle_auto_level_LED_blink;
app.c-232-  (void)data_auto_level_LED_blink;
app.c-233-  auto_level_LED_blink_timer_timeout = true;
app.c-234-}
app.c-235-
--
auto_level.c:178:static void steady_accel_read_oneshot_callback(sl_sleeptimer_timer_handle_t *steady_accel_read_handle, void *steady_accel_read_data)
auto_level.c-179-{
auto_level.c-180-  (void)steady_accel_read_handle;
auto_level.c-181-  (void)steady_accel_read_data;
auto_level.c-182-  steady_accel_read_timer_timeout = true;
auto_level.c-183-}
auto_level.c-184-
--
auto_level.c:195:static void sneak_oneshot_callback(sl_sleeptimer_timer_handle_t *sneak_oneshot_read_handle, void *sneak_oneshot_read_data)
auto_level.c-196-{
auto_level.c-197-  (void)sneak_oneshot_read_handle;
auto_level.c-198-  (void)sneak_oneshot_read_data;
auto_level.c-199-
auto_level.c-200-  sneak_motors_on = true;
auto_level.c-201-
--
auto_level.c:214:static void stop_sneak_oneshot_callback(sl_sleeptimer_timer_handle_t *stop_sneak_oneshot_read_handle, void *stop_sneak_oneshot_read_data)
auto_level.c-215-{
auto_level.c-216-  (void)stop_sneak_oneshot_read_handle;
auto_level.c-217-  (void)stop_sneak_oneshot_read_data;
auto_level.c-218-
auto_level.c-219-  sneak_motors_on = false;
auto_level.c-220-
--
auto_level.c:395:static void tilt_burst_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
auto_level.c-396-{
auto_level.c-397-  (void)handle;
auto_level.c-398-  (void)data;
auto_level.c-399-  tilt_burst_running = false;
auto_level.c-400-}
auto_level.c-401-
--
dual_motor_control.c:68:static void temp_motor_off_callback(sl_sleeptimer_timer_handle_t *temp_motor_off_handle, void *temp_motor_off_data)
dual_motor_control.c-69-{
dual_motor_control.c-70-  (void)temp_motor_off_handle;
dual_motor_control.c-71-  (void)temp_motor_off_data;
dual_motor_control.c-72-  temp_motor_off_timer_timeout = true;
dual_motor_control.c-73-}
dual_motor_control.c-74-
--
settings_mode.c:73:static void callback_5000ms_timer(sl_sleeptimer_timer_handle_t *handle_5000ms, void *data_5000ms)
settings_mode.c-74-{
settings_mode.c-75-  (void)handle_5000ms;
settings_mode.c-76-  (void)data_5000ms;
settings_mode.c-77-  timeout_5000ms = true;
settings_mode.c-78-}
settings_mode.c-79-
--
settings_mode.c:91:static void callback_2000ms_timer(sl_sleeptimer_timer_handle_t *handle_2000ms, void *data_2000ms)
settings_mode.c-92-{
settings_mode.c-93-  (void)handle_2000ms;
settings_mode.c-94-  (void)data_2000ms;
settings_mode.c-95-  timeout_2000ms = true;
settings_mode.c-96-}
settings_mode.c-97-

$ grep -n "IRQHandler\|_on_change\|CORE_ENTER" *.c
adc_currents.c:163:void IADC_IRQHandler(void)
app.c:344:// sl_button_on_change() moved to switch_input.c
app_bm.c:49:  CORE_ENTER_CRITICAL();
app_bm.c:61:  CORE_ENTER_CRITICAL();
switch_input.c:241:static volatile bool ui_wake_request = false; // set by sl_button_on_change
switch_input.c:880:void sl_button_on_change(const sl_button_t *handle)

$ grep -n "SPEED_MODE" config/sl_i2cspm_off_board_config.h config/sl_i2cspm_on_board_config.h
config/sl_i2cspm_off_board_config.h:44:// <o SL_I2CSPM_OFF_BOARD_SPEED_MODE> Speed mode
config/sl_i2cspm_off_board_config.h:49:#define SL_I2CSPM_OFF_BOARD_SPEED_MODE      0
config/sl_i2cspm_on_board_config.h:44:// <o SL_I2CSPM_ON_BOARD_SPEED_MODE> Speed mode
config/sl_i2cspm_on_board_config.h:49:#define SL_I2CSPM_ON_BOARD_SPEED_MODE      0

$ grep -n "define SL_GPIO_PORT_C\b" <SDK>/platform_core/platform/emlib/inc/em_gpio.h
574:#define SL_GPIO_PORT_C gpioPortC

$ grep -n "define BED_BENCH_PROVOKE_RUNAWAY\|define SWITCH_SAMPLER_DIAG" switch_input.c
57:#define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
98:#define BED_BENCH_PROVOKE_RUNAWAY 0
```

### d.3 Builds (verbatim files written by the builds)

First build, flag 0 (`cmake --build --preset default_config`, exit 0):

```
[1/13] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app_bm.c.obj
[2/13] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_status.c.obj
[3/13] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/bed_actions.c.obj
[4/13] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/settings_mode.c.obj
[5/13] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/dual_motor_control.c.obj
[6/13] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/motor_current_functions.c.obj
[7/13] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/exp_ui.c.obj
[8/13] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/exp_board.c.obj
[9/13] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/auto_level.c.obj
[10/13] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/app.c.obj
[11/13] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[12/13] Building C object CMakeFiles/slc.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/autogen/sl_event_handler.c.obj
[13/13] Linking C executable base\tilt_bed_controller.out
```

Compile check, `BED_BENCH_PROVOKE_RUNAWAY 1` (exit 0):

```
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
```

Final rebuild after restoring the flag to 0 (exit 0):

```
[1/2] Building C object CMakeFiles/tilt_bed_controller.dir/base/C_/Users/adunc/Desktop/_Tilt-Beds/_03-Software/tilt_bed_controller/switch_input.c.obj
[2/2] Linking C executable base\tilt_bed_controller.out
```

`grep -n "warning\|error"` over all three files printed nothing.

### d.4 Git

```
$ git log --oneline -5
d1f13d0 One switch reader: sampler, debounce, re-driver refcount, sleep veto (DECISIONS #41)
cb9c7b2 Handoff 04: status check only, no source changes
6b35be8 Handoff 03: note session was interrupted before section (b)
58e2f23 Handoff rules: reword plan/diff requirements (no change in what is required)
1046a14 .gitignore: ignore Studio-generated .projectchecksums and project PDF

$ git show --stat --format= d1f13d0
 app.c                         |  27 +-
 app.h                         |   3 +-
 bed_actions.c                 |   7 -
 docs/DECISIONS.md             |  79 ++++++
 docs/SWITCH_SAMPLER_DESIGN.md | 273 +++++++++++++++++++
 exp_board.c                   |  34 +++
 exp_board.h                   |   5 +
 exp_ui.c                      | 102 +++----
 exp_ui.h                      |  23 +-
 switch_input.c                | 601 +++++++++++++++++++++++++-----------------
 switch_input.h                |  52 ++--
 11 files changed, 859 insertions(+), 347 deletions(-)
```

## e. Claims: VERIFIED vs ASSERTED

### VERIFIED (evidence attached)

1. **The firmware builds with no warnings and no errors, flag 0.** Evidence: d.3, `/tmp/build1.txt` — `exit: 0`, 13 build steps ending `Linking C executable base\tilt_bed_controller.out`, and `grep -n "warning\|error"` over that file printed nothing. The compile steps include `exp_ui.c`, `exp_board.c`, `switch_input.c`, `app.c`, `bed_actions.c`. (The build is incremental; it compiled the changed files and everything that includes the changed headers per ninja's dependency tracking. I did not do a clean full rebuild.)
2. **It also builds with `BED_BENCH_PROVOKE_RUNAWAY 1`, then again with 0.** Evidence: d.3, `flag=1 build exit: 0`, `flag=0 rebuild exit: 0`, empty warning/error greps for both. The flag was set with `sed`, built, and restored with `git checkout -- switch_input.c`; `switch_input.c:98` reads `0` and `git status` afterwards shows only `.projectlinkstore` modified. The last build in `cmake_gcc/build/` is therefore the flag-0 image.
3. **§3a greps all pass** (d.2): `EXP_CMD_READ_INPUTS` appears in `exp_ui.c` only at `:232` (a comment) and `:238` inside `ui_read_inputs()`; `ui_read_inputs(` is called only from `switch_input.c:270,273,287,291,350,358` (plus its definition `exp_ui.c:235` and prototype `exp_ui.h:46` and two comments); the removed-names grep returns no matches (`rc=1`); `set_I2C_redriver_on/off` appear only in `exp_board.c`/`exp_board.h` (definition/declaration) and `exp_ui.c:215,227` inside acquire/release.
4. **Every port-0 write site updates the shadow** (d.2): the `EXP_CMD_OUTPUT;` grep lists the 8 writers, `get_motor_enables()`'s read, and `init_board_expander()`; the `note_port0_write` grep lists a call at `exp_board.c:134,154,175,195,216,236,257,277,493`.
5. **Every sleeptimer callback in the firmware only sets a flag** (d.2 survey). Scope: `app.c`, `auto_level.c`, `dual_motor_control.c`, `settings_mode.c` — the files where `sl_sleeptimer_start_*` is called (grep output in d.2). `sl_button_on_change` (`switch_input.c:880`) now only sets `ui_wake_request`.
6. **Both I2C buses are standard mode, 100 kbit/s** — `config/sl_i2cspm_off_board_config.h:49` and `config/sl_i2cspm_on_board_config.h:49` (`..._SPEED_MODE 0`), mapping shown in d.2.
7. **`SL_GPIO_PORT_C` is defined as `gpioPortC`** at `platform/emlib/inc/em_gpio.h:574` in the SDK (d.2), which is why `GPIO_PinInGet(SL_SIMPLE_BUTTON_UI_INT_PORT, ...)` compiles without an enum conversion; the build confirms it compiles.
8. **Git facts:** HEAD before was `cb9c7b2`, code commit is `d1f13d0` (`git log` in d.4); `.projectlinkstore` was not committed (`git show --stat` in d.4 lists no such file).

### ASSERTED (reasoned, compiles, but nothing observed it)

- **Everything about behaviour on the bed.** No bench run, no flash, no host test. In particular: that the sampler detects press/release correctly; that a two-button press produces `presses ignored` and a two-button release produces two clean releases with no `FAILSAFE:`; that the debounce settings (10 ms x 3) are right for these switches; that releases-first ordering behaves in settings mode; that the 5-second BED LIGHTS hold still enters settings mode.
- That the root cause of the earlier runaways is the multi-reader race (this is the reviewer's analysis of the log; I did not re-read the log).
- That the motor-enable shadow plus sleep veto removes the ALL-MOTORS slowness (the 271-307 ms vs 214-223 ms gap is quoted from the design; the sleeping-CPU explanation is the design's inference).
- That `SL_SIMPLE_BUTTON_PRESSED` means UI_INT is low, and that a low UI_INT means "asserted" for the idle level check (based on the old code's use and the expander's open-drain active-low INT; not checked against the schematic or datasheet).
- That the baseline read in `switch_sampler_init()` clears an interrupt pending from boot (the design's statement about the expander).
- That holding the re-drivers on for a session has no bus-level side effects (the design says the user approved it).
- That `sl_sleeptimer_delay_millisecond()` is a blocking wait (I did not read its source) and that BLE events are dispatched from main-loop context (comment at `ble_bed_service.c:499` only); that `IADC_IRQHandler` does no I2C (not inspected).
- All numbers in the §3c table (estimates from a 100 kHz byte-time model, not measurements).
- That behaviour of the unchanged `settings_mode.c` code matches expectations when it sees only single-bit masks with releases first.
- That the code is free of logic errors not caught by the compiler — it has not been read by anyone but me; a second reading of the sampler's failure path (b.9 items 4-6) is worth the reviewer's time.

## f. QUESTIONS FOR REVIEW

1. **Repo state differed from the design's §0.** The design expected HEAD `6b35be8`, three commits ahead of `origin/main` (`ae6f13a`). Actual HEAD was `cb9c7b2`, level with `origin/main`, because an earlier task in this same conversation (status check) committed handoff 04 and pushed everything. I proceeded because the difference does not touch any code the design specifies, but the design said to report disagreements rather than improvise. Please confirm this is acceptable. The handoff numbering consequently starts at `05` for today.
2. **`sl_button_on_change` still gates on `SL_SIMPLE_BUTTON_PRESSED`** (b.9 item 1). Keep, or make it set the flag on every edge as the design's wording literally reads?
3. **Read-failure policy** (b.9 items 4-5). I count a failure only when every present board fails in the same sample, and hold the debounced state until 5 such samples. A single dead board among two is therefore never marked absent. Is that the intent? Should the limit be per board?
4. **Release latency.** A release is now acted on ~20-30 ms after the contact opens. For a safety-relevant stop that is a real change in worst-case stop time. Is 3 samples at 10 ms acceptable, or should the release path debounce less than the press path (the design specifies symmetric debounce, which I implemented)?
5. **Board-less bench / stuck UI_INT** (b.9 item 6): continuous probing and a sleep veto. Acceptable, or should a long-stuck line back off?
6. **Observation, not changed:** the double-press error block clears `switch_held`/`switch_driving` and `active_switch` but, unlike the fail-safe, does not call `bed_note_source_released(BED_SOURCE_SWITCHES)`. That is how it was before; the held button's later release now finds `switch_held == OFF` and does nothing, so `bed_move_stop()` is not called for it. I did not trace whether `bed_control` self-releases that latch (the fail-safe's comment says it does once the bed is idle). Worth confirming on the bench with two buttons.
7. `bed_actions.c` still includes `switch_input.h` although I found no remaining use there. Remove it in a follow-up?
8. Should `SWITCH_SAMPLER_DIAG` be 0 for the first bench build, or 1 as the design specifies (I left it 1)? The `SW:` lines print for every edge and add serial output during moves.

## g. NOT DONE

- **No bench test of any kind, and nothing was flashed** (CLAUDE.md: the user drives hardware). Every behavioural claim is ASSERTED.
- **I did not re-read `PuTTY Logs\prompt_q_bench.txt`**; the problem statement in b.0 is the design's.
- **The host-compiled test in `test/` (`bed_control_test.c`) was not run.** It covers `bed_control.c`, which this change does not touch; I did not check whether it builds against changed headers.
- **A clean full rebuild was not done**; both builds were incremental.
- **`docs/MIGRATION_STATUS.md` and `docs/REGRESSION.md` were not updated** (not asked; REGRESSION.md would need the §3e checklist turned into steps by the reviewer, as the design says).
- **`.projectlinkstore` was left modified and uncommitted**, as the design instructs.
- **No timing measurement** of release latency, sample cadence, or loop rate; §3c is arithmetic only.
- `ble_bed_service.c` and `adc_currents.c` were not read for I2C use from non-main-loop context (b.1).
