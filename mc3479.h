/*
 * mc3479.h
 *
 *  Created on: Mar 24, 2023
 *      Author: Alan Jones
 */

#ifndef MC3479_H_
#define MC3479_H_

typedef enum {
  MC3479_STATE_STANDBY,
  MC3479_STATE_WAKE
} MC3479_state_t;

// Sample rate bit values are for =< 4 MHz I2C or SPI, different for > 4 MHz SPI

typedef enum {
  MC3479_SAMPLE_RATE_25HZ   = 0x10,
  MC3479_SAMPLE_RATE_50HZ   = 0x11,
  MC3479_SAMPLE_RATE_62P5HZ = 0x12,
  MC3479_SAMPLE_RATE_100HZ  = 0x13,
  MC3479_SAMPLE_RATE_125HZ  = 0x14,
  MC3479_SAMPLE_RATE_250HZ  = 0x15,
  MC3479_SAMPLE_RATE_500HZ  = 0x16,
  MC3479_SAMPLE_RATE_1000HZ = 0x17
} MC3479_sample_rate_t;

// Accelerometer init function

void init_accel();

// Accelerometer register access functions

/*
 * Note: Typical wake time is (1/ODR) + 1 ms (Section 4.3, pg 18) and can range
 * from 2 (1000 Hz ODR) to over 500 ms depending on sampling rate
 */
void accel_wake();
void accel_standby();
MC3479_state_t accel_get_wake_state();

void accel_set_sample_rate(MC3479_sample_rate_t accel_sample_rate);
MC3479_sample_rate_t accel_get_sample_rate();

/*
 * Signed XYZ vectors - _g is scaled by range; _raw is ADC count w/o scaling
 *
 * May make sense to modify raw to scale by integer divisor to work with
 * pseudo-mg's and keep all calculations as integer math to avoid float penalty.
 */
void accel_get_xyz_raw(int16_t *accel);
void accel_get_xyz_g(float *accel);

#endif /* MC3479_H_ */
