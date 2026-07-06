/***************************************************************************//**
 * @file
 * @brief Application interface.
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

#ifndef APP_H
#define APP_H

#include <stdbool.h>

#include "bed_types.h"
#include "exp_ui.h"

/**************************************************************************//**
 * Proceed with execution. (Indicate that it is required to run the application
 * process action.)
 *****************************************************************************/
void app_proceed(void);

/**************************************************************************//**
 * Check if it is required to process with execution.
 * @return true if required, false otherwise.
 *****************************************************************************/
bool app_is_process_required(void);

/**************************************************************************//**
 * Acquire access to protected variables.
 *
 * Acquire the guard to operate on the internal state variables.
 * Guard is implemented using mutexing (RTOS).
 *
 * @note Must not be used from ISR context.
 *
 * @return true if operation was successful.
 *****************************************************************************/
bool app_mutex_acquire(void);

/**************************************************************************//**
 * Finish access to protected variables.
 *
 * Release the guard to stop working on the internal state variables.
 * Guard is implemented using mutexing (RTOS).
 *
 * @note Must not be used from ISR context.
 *****************************************************************************/
void app_mutex_release(void);

/**************************************************************************//**
 * Initialize the application.
 *
 * This function initializes the application components.
 *
 * @note Must not be used from ISR context.
 *****************************************************************************/
void app_init_bt(void);

/***************************************************************************//**
 * Initialize the bed application.
 ******************************************************************************/
void app_init(void);

/***************************************************************************//**
 * Bed application ticking function. Called every super-loop pass; unlike
 * app_proceed()/app_is_process_required() above (unused by the bed logic),
 * this runs unconditionally, matching the 4.2.2 baseline.
 ******************************************************************************/
void app_process_action(void);

/*
 * State below is defined in app.c but read and/or written from bed_actions.c
 * (C4, DECISIONS.md #12): process_outputs() and app_process_action()'s own
 * tick logic touch this state just as much as the action functions do, so it
 * stays here as plain shared extern rather than moving - matching how
 * any_collision already works between motor_current_functions.c and app.c.
 */
extern active_switch_t active_switch; // Determines which switch is active
extern bool is_raising; // Used to set specific collision threshold based on whether motors are raising or lowering
extern bool auto_level_active; // Tracks whether autolevel is running
extern bool first_print[10];

// in_settings_mode is now defined in settings_mode.c - see settings_mode.h.

// switch_pressed/app_ui_interrupt are now defined in switch_input.c - see
// switch_input.h. settings_mode.c reads (never writes) both via that header.

// Switch masks shared with settings_mode.c's settings_mode_process_outputs(),
// which switches on the same app_ui_interrupt bits (the single-side switch
// masks stay private to switch_input.c - only these four are settings-mode
// buttons).
#define SWITCH_UP_ALL      0x0001
#define SWITCH_AUTO_LEVEL  0x0002
#define SWITCH_DOWN_ALL    0x0004
#define SWITCH_BED_LIGHTS  0x0008

// Resets the auto-level timer that bed_actions.c's moved functions used to
// touch directly via its app.c-private handle. app_reset_double_press_timer()
// moved to switch_input.h - switch_input.c now owns double_press_error_timer.
void app_reset_auto_level_timer(void);

// LED helpers bed_actions.c needs (auto-level stop, forced stop). Implemented
// in app.c - deliberately not split into their own module; is_LED_timer_running/
// LED_timer_timeout are already reused between the 200ms and 500ms LED timers,
// and untangling that is out of scope for this reorganization.
void set_all_LED_off(void);
void auto_level_stop_blink_LED(led_color_t LED_color);

// Settings-mode LED helper - non-static: called by settings_mode.c
void fast_blink_LED(led_color_t LED_color);

#endif  // APP_H
