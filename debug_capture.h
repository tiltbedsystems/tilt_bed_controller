/*
 * debug_capture.h
 *
 * Bench-only current-capture mode (refactor step C3, DECISIONS.md #5). When
 * BED_CAPTURE_MOTOR_CURRENTS is 1, motor_current_functions.c streams
 * timestamped CSV over the debug UART, and every other printf in the
 * firmware (app.c, auto_level.c, motor_current_functions.c, bed_settings.c)
 * compiles out via "#if !BED_CAPTURE_MOTOR_CURRENTS" - so a PuTTY capture
 * log is pure CSV, loadable directly into Excel with no cleanup. The one
 * exception is a "#COLLISION,<time_ms>" marker line printed if a collision
 * trips mid-capture (DECISIONS.md #4) - the leading '#' makes contaminated
 * captures easy to spot and reject.
 *
 * NEVER leave enabled (1) in a normal build - adds UART TX overhead to the
 * supervision loop and silences all other diagnostic output.
 */

#ifndef DEBUG_CAPTURE_H_
#define DEBUG_CAPTURE_H_

#define BED_CAPTURE_MOTOR_CURRENTS 0

// Zeroes the capture CSV's time_ms column to a new t=0, so each bench test
// (motor/scenario) gets its own timeline without a rebuild or power cycle.
// Called on every button press/auto-level start (app_move_start(),
// app_auto_level_start() in app.c) - reset at the START of each new test, not
// its release, so the idle time between releasing one button and pressing
// the next doesn't get baked into the next test's first timestamp.
// motor_current_functions.c owns the real clock. No-op when capture mode is
// off.
#if BED_CAPTURE_MOTOR_CURRENTS
void capture_reset_timer(void);
#else
#define capture_reset_timer() ((void)0)
#endif

#endif /* DEBUG_CAPTURE_H_ */
