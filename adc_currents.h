/*
 * adc_currents.h
 *
 *
 *  Created on: Jun 4, 2023
 *      Author: Alan Jones
 */

#ifndef ADC_CURRENTS_H_
#define ADC_CURRENTS_H_

// Initialize the Incremental Analog to Digital Converter (IADC)
void initIADC (void);

// Motor current interface calls
void scan_motor_currents(void); // Executes analog to digital conversion
void get_motor_currents(uint32_t *currents); // Returns converter current values to application

#endif /* ADC_CURRENTS_H_ */
