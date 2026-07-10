/*
 * adc_currents.h
 *
 *
 *  Created on: Jun 4, 2023
 *      Author: Alan Jones
 */

#ifndef ADC_CURRENTS_H_
#define ADC_CURRENTS_H_

#include <stdint.h>
#include <stdbool.h>

// Initialize the Incremental Analog to Digital Converter (IADC)
void initIADC (void);

// Motor current interface calls
void scan_motor_currents(void); // Executes analog to digital conversion
void get_motor_currents(uint32_t *currents); // Returns converter current values to application

/* --- DEBUG INSTRUMENTATION (SiSDK 2026.6 IADC migration bisection) ---
 * TODO: remove once the IADC "reads exactly 0" root cause is confirmed/fixed
 * on hardware.
 */
uint32_t get_iadc_irq_count(void); // Times IADC_IRQHandler has run
uint32_t get_iadc_last_fifo_cnt(void); // Scan FIFO count seen on IRQ entry
void get_iadc_raw_currents(uint32_t *raw); // Pre-scaling raw ADC codes, NUM_INPUTS entries
uint32_t get_iadc_status_reg(void);
uint32_t get_iadc_ien_reg(void);
uint32_t get_iadc_if_reg(void);
bool get_iadc_nvic_irq_enabled(void);

#endif /* ADC_CURRENTS_H_ */
