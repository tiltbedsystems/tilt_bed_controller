/*
 * mc3479.c
 *
 *  Created on: Mar 24, 2023
 *      Author: Alan Jones
 */

#include <stdio.h>
#include "sl_assert.h"
#include "sl_i2cspm.h"
#include "sl_i2cspm_instances.h"
#include "sl_sleeptimer.h"

#include "mc3479.h"

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/

// MC3479 Configuration Settings
#define MC3479_I2C_DEVICE                (sl_i2cspm_on_board)  // I2C device used to control the MC3479 Tri-Axial Accelerometer
#define MC3479_I2C_BUS_ADDRESS           0x4C               // I2C bus address

/*
 * MC3479 register summary in section 12.1 of datasheet, pages 40-42.
 * Only potential register functionality of interest is mapped below.
 *
 * Read registers are read only, write registers are both read and write.
 * All can be read (or written as appropriate) in STANDBY mode, but only
 * the mode register (0x07) can be written (to transition from WAKE back to
 * STANDBY) when the accelerometer is operating.
 */

#define MC3479_RD_DEVICE_STATUS     0x05 // Reads device status
#define MC3479_WR_INTERRUPT_ENABLE  0x06 // Reads/writes interrupt enables
#define MC3479_WR_MODE              0x07 // Reads/writes device operating mode
#define MC3479_WR_SAMPLE_RATE       0x08 // Reads/writes accelerometer sample rate
#define MC3479_WR_MOTION_CTRL       0x09 // Reads/writes accelerometer motion detect modes
#define MC3479_RD_STATUS            0x13 // Reads operating modes status
#define MC3479_RD_INTR_STATUS       0x14 // Reads interrupt flags status
#define MC3479_WR_RANGE_SELECT      0x20 // Reads/writes accelerometer range and low pass filtering

#define MC3479_RD_FIFO_STATUS       0x0A // Reads FIFO fill status
#define MC3479_RD_FIFO_RD_PTR       0x0B // Reads FIFO read pointer value
#define MC3479_RD_FIFO_WR_PTR       0x0C // Reads FIFO write pointer value
#define MC3479_WR_FIFO_CTRL         0x2D // Reads/writes FIFO controls
#define MC3479_WR_FIFO_CTRL2        0x30 // Reads/writes FIFO controls and output rate decimation settings
#define MC3479_WR_FIFO_THRESHOLD    0x2E // Reads/writes FIFO threshold value

#define MC3479_RD_XOUT_LSB          0x0D // Reads x-axis acceleration LSB
#define MC3479_RD_XOUT_MSB          0x0E // Reads x-axis acceleration MSB
#define MC3479_RD_YOUT_LSB          0x0F // Reads y-axis acceleration LSB
#define MC3479_RD_YOUT_MSB          0x10 // Reads y-axis acceleration MSB
#define MC3479_RD_ZOUT_LSB          0x11 // Reads z-axis acceleration LSB
#define MC3479_RD_ZOUT_MSB          0x12 // Reads z-axis acceleration MSB

/*******************************************************************************
 ******************************   STRUCTURES   *********************************
 ******************************************************************************/

/*
 * Low pass filter bandwidth and decimation local - only used for initialization
 *
 * G-range setting is in upper nibble of register and the LPF set call assumes
 * entire register is written with a default range of 2g with and LPF enabled.
 *
 * Datasheet Section 12.13, pg 54 - for register and low pass filter settings
 */

typedef enum {
  MC3479_LPF_BW_1   = 0x09, // Fc = IDR / 4.255
  MC3479_LPF_BW_2   = 0x0A, // Fc = IDR / 6
  MC3479_LPF_BW_3   = 0x0B, // Fc = IDR / 12
  MC3479_LPF_BW_5   = 0x0D  // Fc = IDR / 16
} MC3479_lpf_bandwidth_t;

/*
 * FIFO settings in upper nibble of register and the decimation set call assumes
 * entire register is written with the power-on reset FIFO settings of 0000bbbb.
 *
 * Datasheet Section 12.23, pg. 65 - for register and decimation settings
 */

typedef enum {
  MC3479_DECIMATE_NONE   = 0x00,
  MC3479_DECIMATE_2      = 0x01,
  MC3479_DECIMATE_4      = 0x02,
  MC3479_DECIMATE_5      = 0x03,
  MC3479_DECIMATE_8      = 0x04,
  MC3479_DECIMATE_10     = 0x05,
  MC3479_DECIMATE_16     = 0x06,
  MC3479_DECIMATE_20     = 0x07,
  MC3479_DECIMATE_40     = 0x08,
  MC3479_DECIMATE_67     = 0x09,
  MC3479_DECIMATE_80     = 0x0A,
  MC3479_DECIMATE_100    = 0x0B,
  MC3479_DECIMATE_200    = 0x0C,
  MC3479_DECIMATE_250    = 0x0D,
  MC3479_DECIMATE_500    = 0x0E,
  MC3479_DECIMATE_1000   = 0x0F
} MC3479_decimation_t;

/*******************************************************************************
 ***************************  LOCAL VARIABLES   ********************************
 ******************************************************************************/

/*
 * LSB's in 2g range, use a range set function to divide down as appropriate
 * Datasheet Section 4.2, pg 17 - 4g 8192, 8g 4096, 12g 2730, 16g 2048
 *
 * Can also scale by 16, 8, 4, 3, and 2 respectively to closely approximate mg's
 * for integer rather than float calculations (e.g. 1024 counts per g vs 1,000).
 */
static uint16_t bits_per_g = 16384;

/*******************************************************************************
 *********************   LOCAL FUNCTION PROTOTYPES   ***************************
 ******************************************************************************/


/***************************************************************************//**
 * Function to perform I2C transactions on the MC3479
 *
 * This function is used to perform I2C transactions on the MC3479
 * including read, write and continued write read operations.
 ******************************************************************************/
static I2C_TransferReturn_TypeDef MC3479_transaction(uint16_t flag,
                                                     uint8_t *writeCmd,
                                                     size_t writeLen,
                                                     uint8_t *readCmd,
                                                     size_t readLen)
{
  I2C_TransferSeq_TypeDef seq;
  I2C_TransferReturn_TypeDef ret;

  seq.addr = MC3479_I2C_BUS_ADDRESS << 1;
  seq.flags = flag;

  switch (flag) {
    // Send the write command from writeCmd
    case I2C_FLAG_WRITE:
      seq.buf[0].data = writeCmd;
      seq.buf[0].len  = writeLen;

      break;

    // Receive data into readCmd of readLen
    case I2C_FLAG_READ:
      seq.buf[0].data = readCmd;
      seq.buf[0].len  = readLen;

      break;

    // Send the write command from writeCmd
    // and receive data into readCmd of readLen
    case I2C_FLAG_WRITE_READ:
      seq.buf[0].data = writeCmd;
      seq.buf[0].len  = writeLen;

      seq.buf[1].data = readCmd;
      seq.buf[1].len  = readLen;

      break;

    default:
      return i2cTransferUsageFault;
  }

  // Perform the transfer and return status from the transfer
  ret = I2CSPM_Transfer(MC3479_I2C_DEVICE, &seq);

  return ret;
}


/*
 * Low pass filtering and decimation calls are local and set/forget for now.
 *
 * Note that low pass filter runs at the internal (not output) data rate.
 */

static void accel_set_lpf_bw(MC3479_lpf_bandwidth_t accel_lpf_bw)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd[2];

  // Set accel sample rate bits
  cmd[0] = MC3479_WR_RANGE_SELECT;
  cmd[1] = accel_lpf_bw;
  ret = MC3479_transaction(I2C_FLAG_WRITE, &cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

static void accel_set_decimation(MC3479_decimation_t accel_decimation)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd[2];

  // Set accel sample rate bits
  cmd[0] = MC3479_WR_FIFO_CTRL2;
  cmd[1] = accel_decimation;
  ret = MC3479_transaction(I2C_FLAG_WRITE, &cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}
/*******************************************************************************
 **************************   GLOBAL FUNCTIONS   *******************************
 ******************************************************************************/

void accel_get_xyz_g(float *accel)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd[7];
  int16_t temp;

  /*
   *  Burst read the upper/lower xyz acceleration sample value registers
   *
   *  Datasheet Section 12.10, pg 51 indicates an atomic read between I2C
   *  Start/Stop bits for individual upper/lower pairs or all three pairs if
   *  executed as burst read.
   */

  cmd[0] = MC3479_RD_XOUT_LSB;
  ret = MC3479_transaction(I2C_FLAG_WRITE_READ, &cmd[0], 1, &cmd[1], 6);
  EFM_ASSERT(ret == i2cTransferDone);

  // Format returned sensor data into X, Y, Z accelerations array in g's

  temp = ( (int16_t) cmd[2] << 8) | cmd[1]; // raw x acceleration
  accel[0] = (float) temp / bits_per_g; // format to x g's
  temp = ( (int16_t) cmd[4] << 8) | cmd[3]; // raw y acceleration
  accel[1] = (float) temp / bits_per_g; // format to y g's
  temp = ( (int16_t) cmd[6] << 8) | cmd[5]; // raw z acceleration
  accel[2] = (float) temp / bits_per_g; // format to z g's
}

void accel_get_xyz_raw(int16_t *accel)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd[7];

  /*
   *  Burst read the upper/lower xyz acceleration sample value registers
   *
   *  Datasheet Section 12.10, pg 51 indicates an atomic read between I2C
   *  Start/Stop bits for individual upper/lower pairs or all three pairs if
   *  executed as burst read.
   */

  cmd[0] = MC3479_RD_XOUT_LSB;
  ret = MC3479_transaction(I2C_FLAG_WRITE_READ, &cmd[0], 1, &cmd[1], 6);
  EFM_ASSERT(ret == i2cTransferDone);

  // Format returned sensor data into X, Y, Z accelerations array in g's

  accel[0] = ( (int16_t) cmd[2] << 8) | cmd[1]; // raw x acceleration
  accel[1] = ( (int16_t) cmd[4] << 8) | cmd[3]; // raw y acceleration
  accel[2] = ( (int16_t) cmd[6] << 8) | cmd[5]; // raw z acceleration
}

MC3479_state_t accel_get_wake_state()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd[2];
  MC3479_state_t accel_state;

  // Read accel device status register
  cmd[0] = MC3479_RD_DEVICE_STATUS;
  cmd[1] = 0x00;
  ret = MC3479_transaction(I2C_FLAG_WRITE_READ, &cmd[0], 1, &cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Mask off all but the accel's current wake/standby state in bits 0 and 1
  accel_state = (MC3479_state_t) (cmd[1] && 0x03);

  return accel_state;
}

void accel_wake()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd[2];

  // Set accel wake bits in mode register (I2C stall WDT disabled for now)
  cmd[0] = MC3479_WR_MODE;
  cmd[1] = MC3479_STATE_WAKE;
  ret = MC3479_transaction(I2C_FLAG_WRITE, &cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);

  sl_sleeptimer_delay_millisecond(2);
}

void accel_standby()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd[2];

  // Set accel standby bits in mode register (I2C stall WDT disabled for now)
  cmd[0] = MC3479_WR_MODE;
  cmd[1] = MC3479_STATE_STANDBY;
  ret = MC3479_transaction(I2C_FLAG_WRITE, &cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

/*
 * Sample rate type and functions potentially local/static if no need to ever
 * change between two or more rates (for lower power) in the application.
 */

void accel_set_sample_rate(MC3479_sample_rate_t accel_sample_rate)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd[2];

  // Set accel sample rate bits
  cmd[0] = MC3479_WR_SAMPLE_RATE;
  cmd[1] = accel_sample_rate;
  ret = MC3479_transaction(I2C_FLAG_WRITE, &cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

MC3479_sample_rate_t accel_get_sample_rate()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd[2];
  MC3479_sample_rate_t sample_rate;

  // Get accel sample rate bits
  cmd[0] = MC3479_WR_SAMPLE_RATE;
  ret = MC3479_transaction(I2C_FLAG_WRITE_READ, &cmd[0], 1, &cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Sample register read returns 0's for all but rate bits - no need to mask
  sample_rate = (MC3479_sample_rate_t) cmd[1];

  return sample_rate;
}

void init_accel()
{
  /*
   * Accelerometer power up defaults to 2g range in STANDBY mode.
   * All other functionality (sampling rate, filtering, FIFO, interrupts, etc.)
   * must be set.
   *
   * Set internal sample rate to maximum, decimate by 10 for 100 Hz output rate,
   * and set low pass filtering bandwidth to approximately 80 Hz. Can also
   * configure FIFO or motion sensing registers.
   */

  accel_set_sample_rate(MC3479_SAMPLE_RATE_1000HZ); // Maximum internal sample rate
  accel_set_decimation(MC3479_DECIMATE_10); // Output registers update at 100 Hz

  accel_set_lpf_bw(MC3479_LPF_BW_3); // Filter corner at 1000/12 ~= 80 Hz
}
