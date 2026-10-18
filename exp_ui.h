/*
 * exp_ui.h
 *
 *  Created on: Apr 16, 2023
 *      Author: Alan Jones
 */

#ifndef EXP_UI_H_
#define EXP_UI_H_

#include <stdbool.h>
#include <stdint.h>

#define NUM_LEDS 4

// I2C addresses of the two remote UI boards (both on the off-board bus, behind
// the PCA9617A re-drivers). Used by init/LED writes here and by the switch
// sampler in switch_input.c.
#define UI_AUX_I2C_BUS_ADDRESS     0x20 // Aux Board
#define UI_SWITCH_I2C_BUS_ADDRESS  0x21 // Switch Board

typedef enum {
  LED_RED,
  LED_GREEN,
  LED_BLUE,
  LED_AMBER
} led_color_t;


extern bool LED_state_on[NUM_LEDS];

/***************************************************************************//**
 * Initialize remote switch board UI GPIO expander
 ******************************************************************************/

void init_ui_expander();

/***************************************************************************//**
 * UI GPIO expander interface functions
 ******************************************************************************/

// The ONLY function that reads a UI board's input ports. Reading them clears the
// expander's interrupt flag, so exactly one caller (the switch sampler in
// switch_input.c) may use it - see DECISIONS.md #41. Returns false on any
// transfer error (*status untouched); otherwise *status = (port0 << 8) | port1.
bool ui_read_inputs(uint8_t board_addr, uint16_t *status);

// Reference-counted ownership of the off-board bus re-drivers (P1_5 on the
// on-board expander). The first acquire switches them on and waits out the
// supply load switch; the last release switches them off. Every UI transaction
// takes a hold for its own duration, so holding across a sampling session means
// no per-transaction toggling or 2 ms delay. Main-loop context only: EN must
// change only while the bus is idle.
void ui_redriver_acquire(void);
void ui_redriver_release(void);

void set_LED_on(led_color_t LED_color);
void set_LED_off(led_color_t LED_color);

#endif /* EXP_UI_H_ */
