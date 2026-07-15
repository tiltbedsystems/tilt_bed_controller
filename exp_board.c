/*
 * exp_board.c
 *
 *  Created on: Apr 16, 2023
 *      Author: Alan Jones
 */

#include <stdio.h>
#include "sl_assert.h"
#include "sl_i2cspm.h"
#include "sl_i2cspm_instances.h"
#include "sl_sleeptimer.h"

#include "pi4ioe5v6416.h"
#include "exp_board.h"

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/

#define BOARD_EXP_I2C_DEVICE                (sl_i2cspm_on_board)
#define BOARD_EXP_I2C_BUS_ADDRESS           0x20

// Motor-fault sampling interval (main-loop-context poll, not interrupt-driven
// - see sample_motor_faults_tick() below for why). Milliseconds via
// sleeptimer, never loop counts (DECISIONS.md #2).
#define MOTOR_FAULT_SAMPLE_INTERVAL_MS       200

/*******************************************************************************
 ***************************  LOCAL VARIABLES   ********************************
 ******************************************************************************/

// Last-sampled fault bits (bit i = motor i, active meaning per
// get_motor_faults()), updated only from sample_motor_faults_tick() in
// main-loop context. bed_status_get() reads this via get_cached_motor_faults()
// instead of calling get_motor_faults() directly, which would block on I2C
// from whatever context calls it.
static uint8_t cached_motor_faults = 0;
static uint32_t last_fault_sample_tick = 0;

/*******************************************************************************
 *********************   LOCAL FUNCTION PROTOTYPES   ***************************
 ******************************************************************************/

static I2C_TransferReturn_TypeDef board_exp_transaction(uint16_t flag,
                                                     uint8_t *writeCmd,
                                                     size_t writeLen,
                                                     uint8_t *readCmd,
                                                     size_t readLen)
{
  I2C_TransferSeq_TypeDef seq;
  I2C_TransferReturn_TypeDef ret;

  seq.addr = BOARD_EXP_I2C_BUS_ADDRESS << 1;
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
  ret = I2CSPM_Transfer(BOARD_EXP_I2C_DEVICE, &seq);

  return ret;
}

/*******************************************************************************
 **************************   GLOBAL FUNCTIONS   *******************************
 ******************************************************************************/

/*******************************************************************************
 * Setters for motor UP/DOWN, and motor, re-driver, under-bed lighting ON/OFF
 ******************************************************************************/

void set_motor_on(int motor)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x00;

  // Capture existing motor enables - some may already be ON
  write_cmd[0] = EXP_CMD_OUTPUT;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Shift bit to proper motor and OR with existing enable bits
  pinmask = 1 << (motor);

  write_cmd[1] |= pinmask;

  // Write out to expander to turn motor on
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

void set_all_motors_on()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x0F;

  // Capture existing port state - some bits may already be set
  write_cmd[0] = EXP_CMD_OUTPUT;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // OR pinmask with existing port bits
  write_cmd[1] |= pinmask;

  // Write out to expander to turn all motors on
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

void set_motor_off(int motor)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x00;

  // Capture existing motor enables - some may already be ON
  write_cmd[0] = EXP_CMD_OUTPUT;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Shift bit to proper motor, invert, then AND with existing enable bits
  pinmask = 1 << (motor);
  write_cmd[1] &= ~pinmask;

  // Write out to expander to turn motor off
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

void set_all_motors_off()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x0F;

  // Capture existing port state - some bits may already be set
  write_cmd[0] = EXP_CMD_OUTPUT;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Invert pinmask then AND with existing port bits
  write_cmd[1] &= ~pinmask;

  // Write out to expander to turn all motors off
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

void set_motor_direction_up(int motor)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x00;

  // Capture existing motor direction - some may already be set
  write_cmd[0] = EXP_CMD_OUTPUT;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Shift bit to proper motor and OR with existing direction bits
  pinmask = 1 << (motor + 4);
  write_cmd[1] |= pinmask;

  // Write out to expander to set motor run direction
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

void set_all_motors_direction_up()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0xF0;

  // Capture existing port state - some bits may already be set
  write_cmd[0] = EXP_CMD_OUTPUT;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // OR pinmask with existing port bits
  write_cmd[1] |= pinmask;

  // Write out to expander to set all motors run direction
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

void set_motor_direction_down(int motor)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x00;

  // Capture existing motor direction - some may already be set
  write_cmd[0] = EXP_CMD_OUTPUT;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Shift bit to proper motor, invert, then AND with existing direction bits
  pinmask = 1 << (motor + 4);
  write_cmd[1] &= ~pinmask;

  // Write out to expander and set motor run direction
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

void set_all_motors_direction_down()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0xF0;

  // Capture existing port state - some bits may already be set
  write_cmd[0] = EXP_CMD_OUTPUT;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Invert pinmask then AND with existing port bits
  write_cmd[1] &= ~pinmask;

  // Write out to expander and set all motors run direction
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

uint8_t get_motor_faults()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd;
  uint8_t ports[2];
  uint8_t faults;

  // Port 1 bits 0-3 are FAULT_MOTOR1-4 (see init_board_expander()). MP6522
  // nFAULT is open-drain, driven low on an over-current/over-voltage/
  // thermal-shutdown fault, pulled high otherwise by the pull-ups enabled in
  // init_board_expander() - it is NOT asserted during normal current-limit/
  // PWM regulation, so it won't false-trigger under ordinary high load.
  cmd = EXP_CMD_READ_INPUTS;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &cmd, 1, &ports[0], 2);
  EFM_ASSERT(ret == i2cTransferDone);

  faults = (~ports[1]) & 0x0F; // bit i = motor i's fault state, active-low

  return faults;
}

// Rate-limited, main-loop-context wrapper around get_motor_faults(). Call
// once per app_process_action() pass; internally gates the actual I2C read
// to once per MOTOR_FAULT_SAMPLE_INTERVAL_MS so it doesn't add a second
// per-tick blocking transaction on sl_i2cspm_on_board alongside the
// accelerometer's (auto_level.c's accel_running_average() does one I2C
// transaction on that same bus every super-loop pass during auto-level -
// see DECISIONS.md's motor-fault-telemetry entry for the full history).
//
// This is an INTERIM measure, not the final answer: it polls the
// expander's INSTANTANEOUS input state, so a fault shorter than roughly
// MOTOR_FAULT_SAMPLE_INTERVAL_MS can be missed entirely between samples.
// The expander has interrupt-latch registers meant to catch exactly that
// (see init_board_expander()'s "Configure latch registers once signal
// interrupt working" comment) - they are unconfigured, and fault_int's
// interrupt is not currently wired to anything (see DECISIONS.md).
//
// At 200 ms, the moving-state BLE status notify cadence (100 ms, #6) means
// up to two consecutive notifications can carry the same stale fault byte -
// acceptable for a slow-developing driver fault (over-current/over-voltage/
// thermal shutdown), not for a fast transient.
void sample_motor_faults_tick(void)
{
  uint32_t now = sl_sleeptimer_get_tick_count();

  if (sl_sleeptimer_tick_to_ms(now - last_fault_sample_tick) < MOTOR_FAULT_SAMPLE_INTERVAL_MS) {
    return;
  }

  last_fault_sample_tick = now;
  cached_motor_faults = get_motor_faults();
}

// Non-blocking accessor for the last value sample_motor_faults_tick() read.
// Safe to call from any context (e.g. bed_status_get()) - never touches I2C.
uint8_t get_cached_motor_faults(void)
{
  return cached_motor_faults;
}

void set_underbed_lighting_on()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x00;

  // Capture existing port state in event of future design changes
  write_cmd[0] = EXP_CMD_OUTPUT +1;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Shift bit to bed lighting and OR with existing direction bits
  pinmask = 1 << 6;
  write_cmd[1] |= pinmask;

  // Write out to expander to turn under-bed lighting load switch ON
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

void set_underbed_lighting_off()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x00;

  // Capture existing Port 1 state in event of future design changes
  write_cmd[0] = EXP_CMD_OUTPUT +1; // Under-bed load switch on port 1
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Shift bit to bed lighting, invert, then AND with existing Port 1 bits
  pinmask = 1 << 6;
  write_cmd[1] &= ~pinmask;

  // Write out to expander to turn under-bed lighting load switch OFF
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

//* I2C Re-driver Code
void set_I2C_redriver_on()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x00;

  // Capture existing redriver state
  write_cmd[0] = EXP_CMD_OUTPUT +1;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Shift bit to I2C redriver and OR with existing direction bits
  pinmask = 1 << 5;
  write_cmd[1] |= pinmask;

  // Write out to expander to turn I2C redriver ON
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}

void set_I2C_redriver_off()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x00;

  // Capture existing redriver state
  write_cmd[0] = EXP_CMD_OUTPUT +1;
  ret = board_exp_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  EFM_ASSERT(ret == i2cTransferDone);

  // Shift bit to I2C redriver, then AND with existing Port 1 bits
  pinmask = 1 << 5;
  write_cmd[1] &= ~pinmask;

  // Write out to expander to turn I2C redriver OFF
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
}


/*******************************************************************************
 * Initialize local board GPIO expander
 ******************************************************************************/
void init_board_expander()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];

  /*
   * Configure on-board expander for proper operation. Device resets in known
   * state at register level with all GPIO's configured as inputs with no
   * latching, no polarity inversion, output drive strength at 10 mA, no
   * interrupt masking, 100k pulls set to pull up but not enabled, and port
   * outputs as push-pull.
   *
   *  P0_0  EN_MOTOR1         P1_0  FAULT_MOTOR1
   *  P0_1  EN_MOTOR2         P1_1  FAULT_MOTOR2
   *  P0_2  EN_MOTOR3         P1_2  FAULT_MOTOR3
   *  P0_3  EN_MOTOR4         P1_3  FAULT_MOTOR4
   *  P0_4  UP_DNn_MOTOR1     P1_4  Not connected
   *  P0_5  UP_DNn_MOTOR2     P1_5  EN_I2C_DRIVER
   *  P0_6  UP_DNn_MOTOR3     P1_6  EN_LIGHTING
   *  P0_7  UP_DNn_MOTOR4     P1_7  Not connected
   *
   * Port register bit ordering is:
   * MSB   Px.7 Px.6 Px.5 Px.4 Px.3 Px.2 Px.1 Px.0   LSB
   *
   * e.g. 0xF0 sets upper 4 ports to 1, and lower 4 to 0 in that register
   *
   * Most registers are in pairs - one for P0, the second for P1
   *
   * Datasheet indicates single register writes only, but sequential reads
   */

  // Set all GPIO's low - register values have no impact on inputs
  write_cmd[0] = EXP_CMD_OUTPUT;
  write_cmd[1] = 0x00;
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
  write_cmd[0] = EXP_CMD_OUTPUT + 1; // Advance to Port 1
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);

  // Configure outputs for motor drivers, I2C re-driver, and bed lighting load switch
  write_cmd[0] = EXP_CMD_CONFIGURE; // 0 bit enables as output
  write_cmd[1] = 0x00; // All I/O's on Port 0 are outputs
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);
  write_cmd[0] = EXP_CMD_CONFIGURE + 1; // Advance to Port 1
  write_cmd[1] = 0x9F; // Only IO's 5 (re-driver) and 6 (light) on P1 are outputs
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);

  // Enable fault input pull ups
  write_cmd[0] = EXP_CMD_PULL_ENABLE + 1; // None on Port 0
  write_cmd[1] = 0x0F; // Enable on I/O's 0-3
  ret = board_exp_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  EFM_ASSERT(ret == i2cTransferDone);

  /*
   *  Configure latch registers once signal interrupt working to respond
   *  to potential motor fault from driver IC's.
   */

}

