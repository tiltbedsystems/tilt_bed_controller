/*
 * exp_ui.h
 *
 *  Created on: Apr 16, 2023
 *      Author: Alan Jones
 */

#ifndef EXP_UI_H_
#define EXP_UI_H_

#include <stdint.h>

#define NUM_LEDS 4

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

uint16_t get_ui_interrupt();
uint16_t get_ui_status();

void set_LED_on(led_color_t LED_color);
void set_LED_off(led_color_t LED_color);

#endif /* EXP_UI_H_ */
