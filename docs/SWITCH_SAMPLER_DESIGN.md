# Switch sampler design (one switch reader) - implementation spec

Note (2026-10-18): the debounce is now 2 samples (commit 47db449); the body below is the historical design.

Written by the reviewer on 2026-10-17 for the session that implements it.
Implement as specified; where the code contradicts this document, stop and
report it under QUESTIONS FOR REVIEW instead of improvising.

BED_BENCH_PROVOKE_RUNAWAY stays 0.

======================================================================
0. REPO STATE AND HOUSEKEEPING
======================================================================
Expected: HEAD 6b35be8, three commits ahead of origin/main (ae6f13a);
.projectlinkstore modified (leave it, never commit it); this file
(docs/SWITCH_SAMPLER_DESIGN.md) untracked - commit it with the code.
The 03 handoff is already committed and closed; do not edit it.
This session's handoff uses the next filename per CLAUDE.md (date from
`date`).

======================================================================
1. BACKGROUND (verified by the reviewer)
======================================================================
Three code paths read the UI expander input port: switch_input_tick's
dispatch, the double-press check, and motion_failsafe_tick via
get_ui_status_out_of_band(). Each read clears the expander's interrupt
flag, so they race. Latest symptom (bench log
C:\Users\adunc\Desktop\_Tilt-Beds\_03-Software\PuTTY Logs\prompt_q_bench.txt
lines 190-254): swallowed_change_hint carried two bits, process_inputs()
only matches single-bit masks, a two-button release was dropped, and the
fail-safe stopped the motors ~200 ms later (five times). There is no
debounce anywhere.

Hardware (from schematics): motor expander P1_5 = EN_I2C_DRIVER, which
powers only the PCA9617A redrivers and their pull-ups. The Switch board
expander (0x21), its LEDs and the interrupt line are on always-on 3V3.
Redriver EN may only change while the bus is idle. The user approves
holding the redriver on for the duration of a sampling session.

Motor gating (verified): app.c:406-417 start all four PWMs at 100% and
never stop them, so motor expander P0_0-P0_3 alone gate motion.
get_motor_enables() reads the right signal. No change to it.

======================================================================
2. DESIGN TO IMPLEMENT
======================================================================

--- 2.1 exp_ui.c / exp_ui.h: one redriver owner, one read primitive ---

a) Redriver reference count (static in exp_ui.c):
     static uint8_t redriver_holds = 0;
     void ui_redriver_acquire(void): if (redriver_holds == 0) {
         set_I2C_redriver_on(); sl_sleeptimer_delay_millisecond(2); }
       redriver_holds++;
     void ui_redriver_release(void): if (redriver_holds > 0) {
         redriver_holds--; if (redriver_holds == 0) set_I2C_redriver_off(); }
   ui_transaction() replaces its own on/delay/off with acquire before the
   transfer and release after it. Net effect: identical behaviour when
   nothing holds the redriver; no toggling and no 2 ms delay while the
   sampler holds it. Keep the existing comment about the 2 ms load-switch
   time next to the delay. All callers are main-loop context (timer
   callbacks in this firmware only set flags - confirm with a grep and
   report it), so EN only changes between transfers.

b) Board addresses move to exp_ui.h:
     #define UI_AUX_I2C_BUS_ADDRESS    0x20
     #define UI_SWITCH_I2C_BUS_ADDRESS 0x21

c) New primitive - the ONLY function that issues EXP_CMD_READ_INPUTS to
   a UI board:
     bool ui_read_inputs(uint8_t board_addr, uint16_t *status)
   sets ui_target = board_addr, does the existing WRITE_READ of
   EXP_CMD_READ_INPUTS (2 bytes), returns false on any transfer error
   (status untouched), else *status = (ports[0] << 8) | ports[1].

d) Delete get_ui_interrupt(), get_ui_interrupts(), get_ui_status() and
   ui_int_source, and their declarations. LED functions and
   init_ui_expander() are otherwise unchanged (they still write both
   boards blindly).

--- 2.2 exp_board.c / exp_board.h: motor-enable shadow ---

static uint8_t motor_enable_shadow = 0x0F;  // unknown at boot = assume on
Every function that writes EXP_CMD_OUTPUT (port 0) - set_motor_on/off,
set_all_motors_on/off, set_motor_direction_up/down,
set_all_motors_direction_up/down, init_board_expander - updates it after
the write: on success, motor_enable_shadow = written_byte & 0x0F; on
failure, motor_enable_shadow = 0x0F (unknown = assume energised).
New accessor, no I2C:
     bool motor_outputs_maybe_on(void) { return motor_enable_shadow != 0; }
List every port-0 write site in the handoff and show each updates the
shadow (grep "EXP_CMD_OUTPUT;" in exp_board.c).

--- 2.3 switch_input.c: the switch sampler ---

Constants:
  #define SWITCH_SAMPLE_MS            10
  #define SWITCH_DEBOUNCE_SAMPLES     3   // 3 identical reads = >=20 ms stable
  #define SWITCH_READ_FAIL_LIMIT      5   // 50 ms of failed reads -> all released
  #define SWITCH_STATE_STALE_MS       100 // fail-safe distrusts older data
  #define SWITCH_SAMPLER_DIAG         1   // bench prints; pre-ship: 0
SWITCH_INPUT_MASK (0xFF0F) stays. Work in "down" polarity:
down = (uint16_t)(~status) & SWITCH_INPUT_MASK.

State (static):
  bool     sampler_active;          // session running, redriver held
  volatile bool ui_wake_request;    // set by sl_button_on_change
  bool     aux_present, switch_present;
  uint16_t deb_down;                // debounced down-mask
  uint8_t  pending_count[16];       // per-bit consecutive-differ counter
  uint8_t  bounce_count[16];        // per-bit bounce tally for diag
  uint8_t  read_fail_run;
  uint32_t next_sample_tick, last_good_sample_tick;
  uint16_t pending_release, pending_press;  // accepted edges to deliver

sl_button_on_change(): for the ui_int handle only, set ui_wake_request =
true (sl_atomic_store). No I2C, nothing else.

void switch_sampler_init(void) - called from app_init() immediately after
init_ui_expander() (add the call in app.c and a prototype in
switch_input.h):
  - clear ui_wake_request; ui_redriver_acquire();
  - probe/read both boards with ui_read_inputs(): *_present = read ok;
  - deb_down = combined down of present boards (no events generated - a
    button held at power-up is never acted on: its press is never
    delivered, so its later release finds switch_held != its value and
    does nothing; state that reasoning in a comment);
  - last_good_sample_tick = now;
  - if deb_down != 0: sampler_active = true (keep the hold),
    next_sample_tick = now; else ui_redriver_release().
  This baseline read also clears any interrupt pending from boot.

static void switch_sampler_tick(void):
  if (!sampler_active):
    start if ui_wake_request is set OR the UI_INT GPIO pin reads low
    (asserted; use GPIO_PinInGet with the ui_int simple-button config
    port/pin macros - a level check, no I2C - so an edge missed for any
    reason still starts a session). Start = ui_redriver_acquire();
    re-probe any board not present (one transaction each);
    sampler_active = true; next_sample_tick = now. Otherwise return.
  if now is before next_sample_tick: return.
  next_sample_tick += ms_to_tick(SWITCH_SAMPLE_MS) (if more than one
    period behind, resync to now + period).
  CLEAR ui_wake_request BEFORE the read (an interrupt after the read sets
    it again and keeps the session alive).
  Read each present board. A board read failure: count it (print under
    DIAG); after SWITCH_READ_FAIL_LIMIT consecutive failures of every
    present board, treat raw down as 0 (fail toward OFF) and mark the
    boards not present (re-probed at the next session start).
  If at least one read succeeded: raw = OR of successful boards' down;
    last_good_sample_tick = now; read_fail_run = 0.
  Debounce each bit b of SWITCH_INPUT_MASK:
    if raw_b == deb_b: if pending_count[b] > 0 { bounce_count[b]++;
       pending_count[b] = 0; }
    else: pending_count[b]++; if pending_count[b] >= DEBOUNCE_SAMPLES:
       flip deb_b; record it in pending_press (now down) or pending_release
       (now up); DIAG print "SW: 0x%04X %s (bounces %u)" with bit mask,
       "down"/"up", bounce_count[b]; reset both counters for b.
  End the session when deb_down == 0 AND every pending_count is 0 AND
    ui_wake_request is false: sampler_active = false;
    ui_redriver_release().

Accessors (used by the fail-safe and the double-press check):
  bool switch_sampler_active(void)   // also used by app_is_ok_to_sleep
  uint16_t switch_sampler_down(void) // deb_down
  bool switch_state_fresh(void): !sampler_active ||
       (now - last_good_sample_tick) <= STALE_MS
  (When idle, deb_down is 0 by construction - a session only ends with
  everything released - so idle state is valid without a read.)

Event delivery - in switch_input_tick(), replacing the read_ui_data block:
  switch_sampler_tick();
  then deliver pending edges, one process_inputs() call per bit:
    1) every bit in pending_release, lowest bit first:
       app_ui_interrupt = bit; switch_pressed = false; process_inputs();
    2) every bit in pending_press, lowest bit first - BUT if
       count_switches_down(deb_down) >= 2 at this point, deliver NO
       presses; DIAG print "SW: %u switches down - presses ignored".
       Otherwise app_ui_interrupt = bit; switch_pressed = true;
       process_inputs();
    clear both pending masks.
  Releases go first so a release always reaches bed_move_stop() before
  anything new can start. process_inputs()/process_outputs() and
  settings_mode_process_outputs() are NOT changed: they still see exactly
  one single-bit mask per call, which they already handle.
  NOTE count_switches_down() currently takes an active-low status word;
  give it (or a new helper) the down-mask and keep the arithmetic right.

--- 2.4 Double-press check ---

Keep the existing gate exactly ((active_switch != OFF) && !any_collision
&& !auto_level_active && !in_settings_mode, and switch_sourced_move), keep
initial_switch_press -> scan_all_currents() and run_motor_supervision()
as they are. Replace the timer-driven body with: if switch_sourced_move
&& count of switch_sampler_down() >= 2 -> the existing "Double Press
Error" block unchanged (print, motors off, clear_switch_panel_state(),
active_switch = OFF, resets, LEDs off, settings_mode_cancel_entry()).
Print the down-mask in place of the old status word.
Delete: double_press_error_timer, its callback/start function,
double_press_error_timer_timeout, is_double_press_error_timer_on,
app_reset_double_press_timer() and its declaration, its 7 call sites in
bed_actions.c (lines ~57, 70, 95, 112, 184, 201, 209) and the
sl_sleeptimer_stop_timer(&double_press_error_timer) in process_inputs'
BED_LIGHTS release branch (keep settings_mode_cancel_entry() there).

--- 2.5 Motion fail-safe ---

Replace `uint16_t status = get_ui_status_out_of_band();` and the
any_switch_down line with:
  bool any_switch_down = switch_state_fresh() &&
                         (switch_sampler_down() != 0);
Stale or unknown = no switch down = fail toward OFF. Everything else in
motion_failsafe_tick() (sampling period, confirm count, motor-enable
hardware read, provoke hook, cleanup) is unchanged. Update its comments.

--- 2.6 Sleep (app.c, app_is_ok_to_sleep) ---

Add, before the existing return true:
  if (switch_sampler_active())   return false; // sampling needs the loop
  if (motor_outputs_maybe_on())  return false; // hardware may be driving
Keep the motor_state[] loop and the tilt-burst check. Comment: the shadow
check exists because set_all_motors_on() energises without setting
motor_state[] (#40), so a runaway could previously let the CPU sleep
between fail-safe samples - the suspected cause of the ALL MOTORS
fail-safe taking 271-307 ms against 214-223 ms for the HEAD pair.

--- 2.7 Delete ---

read_ui_data, process_switch_change, app_ui_status, app_ui_status_new,
app_ui_interrupt_new, app_ui_status_valid, swallowed_change_hint,
get_ui_status_out_of_band(), the "UI: recovered swallowed switch change"
print and its comment block. app_ui_interrupt and switch_pressed stay
(settings_mode.c reads them). Keep "Unrecognized switch mask" (it should
now never print). Keep the provoke hook as is.

--- 2.8 DECISIONS.md ---

Add the next-numbered entry: "One switch reader" - only the sampler reads
UI input ports; every other consumer uses its debounced state; one event
per bit, releases first; no presses while 2+ down; the redriver
reference count; the sleep veto on sampler activity and on the
motor-enable shadow; fail toward OFF on stale/failed reads. Include the
known limit: the expander can drop an edge that coincides with the ACK
of its own read; mid-session the next sample catches it; at session end
a press in that window is lost (the press does nothing - benign, never a
lost release, because sessions only end all-released); the UI_INT level
check at idle covers a stuck-asserted line.

======================================================================
3. VERIFY BEFORE HANDOFF
======================================================================
3a. Greps, raw output in the handoff:
    - EXP_CMD_READ_INPUTS in exp_ui.c: only inside ui_read_inputs().
    - ui_read_inputs( : called only from switch_input.c's sampler/init.
    - get_ui_status|get_ui_interrupt|ui_int_source|swallowed|
      double_press_error_timer|app_reset_double_press_timer: no matches
      in .c/.h.
    - set_I2C_redriver_on|set_I2C_redriver_off: only in exp_board.c and
      the acquire/release functions.
3b. Build from the command line with the project's existing
    cmake_gcc setup (the user permits it this session). Report the raw
    tail of the output and every warning in files you touched. Do not
    flash.
3c. Off-board I2C clock: cite the config file and value, and fill this
    table with estimates: on_board / off_board transactions per second
    and blocking ms per second, today vs after, for (i) idle with the
    loop awake, (ii) one button held with motors running.
3d. Section (b) of the handoff: this design as implemented, noting every
    place you deviated and why.
3e. Proposed bench checklist: the exact PuTTY lines expected for a single
    press/release, a two-button press and release, BED LIGHTS held 5 s,
    and the provoked runaway (flag 1) - the reviewer turns it into steps.

Commit the code as one commit, then the handoff + SESSION_LOG entry, then
push per the standing rule.
