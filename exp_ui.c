/*
 * exp_ui.c
 *
 *  Created on: Apr 16, 2023
 *      Author: Alan Jones
 */

#include <stdio.h>
#include "sl_i2cspm.h"
#include "sl_i2cspm_instances.h"
#include "sl_sleeptimer.h"

#include "pi4ioe5v6416.h"
#include "exp_board.h"
#include "exp_ui.h"

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/

#define UI_I2C_DEVICE               (sl_i2cspm_off_board)
#define UI_AUX_I2C_BUS_ADDRESS      0x20 // Aux Board
#define UI_SWITCH_I2C_BUS_ADDRESS   0x21 // Switch Board

/*******************************************************************************
 ***************************  GLOBAL VARIABLES   ********************************
 ******************************************************************************/

static uint8_t ui_target;
static uint8_t ui_int_source;

bool LED_state_on[NUM_LEDS] = {false}; // Declared extern in exp_ui.h to use in app.c as well

/*******************************************************************************
 *********************   LOCAL FUNCTION PROTOTYPES   ***************************
 ******************************************************************************/

static I2C_TransferReturn_TypeDef ui_transaction(uint16_t flag,
                                                     uint8_t *writeCmd,
                                                     size_t writeLen,
                                                     uint8_t *readCmd,
                                                     size_t readLen)
{
  I2C_TransferSeq_TypeDef seq;
  I2C_TransferReturn_TypeDef ret;

  seq.addr = ui_target << 1;
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

  // Re-driver on/off time is < 100 nsec, but supply load switch needs 2 msec
  set_I2C_redriver_on();
  sl_sleeptimer_delay_millisecond(2);

  // Perform the transfer and return status from the transfer
  ret = I2CSPM_Transfer(UI_I2C_DEVICE, &seq);

  // Turn re-driver/power OFF
  set_I2C_redriver_off();

  return ret;
}

static uint16_t get_ui_interrupts()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd;
  uint8_t ports[2];
  uint16_t interrupt;

  // Read GPIO IC's internal interrupt source flags
  cmd = EXP_CMD_READ_INTERRUPTS;
  ret = ui_transaction(I2C_FLAG_WRITE_READ, &cmd, 1, &ports[0], 2);
  // EFM_ASSERT(ret == i2cTransferDone);
  if (ret != i2cTransferDone) { // Other board or not connected
      ports[0] = 0;
      ports[1] = 0;
  }

  // Save Port 0 and Port 1 into packed variable - 1's are interrupt source(s)
  // Maybe atomic load/store, but almost certainly OK with current design
  interrupt = ((uint16_t) (ports[0] << 8)) | ports[1];

  // Return for processing by switch decode function
  return interrupt;
}

static void set_LEDs_on(led_color_t LED_color)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x00;

  // Capture existing register state - some LED's may already be ON
  write_cmd[0] = EXP_CMD_OUTPUT+1;
  ret = ui_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  // EFM_ASSERT(ret == i2cTransferDone);

  // LED's on switch board are active low, so set GPIO low to turn ON
  // Shift bit to proper LED, invert, then AND with existing enable bits
  pinmask = 1 << (LED_color + 4);
  write_cmd[1] &= ~pinmask;

  // Write out to expander to turn LED on
  ret = ui_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  // EFM_ASSERT(ret == i2cTransferDone);
  (void)ret;
}

static void set_LEDs_off(led_color_t LED_color)
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];
  uint8_t pinmask = 0x00;

  // Capture existing register state
  write_cmd[0] = EXP_CMD_OUTPUT+1;
  ret = ui_transaction(I2C_FLAG_WRITE_READ, &write_cmd[0], 1, &write_cmd[1], 1);
  // EFM_ASSERT(ret == i2cTransferDone);

  // LED's on switch board are active low, so set GPIO high to turn OFF
  // Shift bit to proper LED and OR with existing enable bits
  pinmask = 1 << (LED_color + 4);
  write_cmd[1] |= pinmask;

  // Write out to expander to turn LED off
  ret = ui_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  // EFM_ASSERT(ret == i2cTransferDone);
  (void)ret;
}

static void init_ui_expanders()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t write_cmd[2];

  /*
   * Configure auxiliary and switch board expanders for proper operation.
   * Devices reset in known state at register level with all GPIO's configured
   * as inputs with no latching, no polarity inversion, output drive strength at
   * 10 mA, no interrupt masking, 100k pulls set to pull up but not enabled,
   * outputs as push-pull.
   *
   *  P0_0  DOWN_M1         P1_0  UP_ALL
   *  P0_1  DOWN_M2         P1_1  AUTO_LEVEL
   *  P0_2  DOWN_M3         P1_2  DOWN_ALL
   *  P0_3  DOWN_M4         P1_3  BED_LIGHTS
   *  P0_4  UP_M1           P1_4  EN_REDn
   *  P0_5  UP_M2           P1_5  EN_GREENn
   *  P0_6  UP_M3           P1_6  EN_BLUEn
   *  P0_7  UP_M4           P1_7  EN_AMBERn
   *
   * Port register bit ordering is:
   * MSB   Px.7 Px.6 Px.5 Px.4 Px.3 Px.2 Px.1 Px.0   LSB
   *
   * e.g. 0xF0 sets upper 4 IO's to 1, and lower 4 to 0 in that register
   *
   * Most registers are in pairs - one for Port 0, the second for Port 1
   *
   * Datasheet indicates single register writes only, but sequential reads
   *
   * All switches require pull-ups and all LED's are active low.
   */

  // Enable input 100k switch pullups (reset default R value on all inputs).
  write_cmd[0] = EXP_CMD_PULL_ENABLE; // Port 0
  write_cmd[1] = 0xFF; // All IO's are switch inputs
  ret = ui_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  // EFM_ASSERT(ret == i2cTransferDone);
  write_cmd[0] = EXP_CMD_PULL_ENABLE + 1; //Advance to Port 1
  write_cmd[1] = 0x0F; // IO's 0-3 are switches
  ret = ui_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  // EFM_ASSERT(ret == i2cTransferDone);

  // Set LED GPIO's high - register values have no impact on inputs
  write_cmd[0] = EXP_CMD_OUTPUT + 1; // LED's on Port 1
  write_cmd[1] = 0xF0; // IO's 4-7
  ret = ui_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  // EFM_ASSERT(ret == i2cTransferDone);

  // Configure LED outputs
  write_cmd[0] = EXP_CMD_CONFIGURE + 1; // LED's on Port 1
  write_cmd[1] = 0x0F; // IO's 4-7
  ret = ui_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  // EFM_ASSERT(ret == i2cTransferDone);

  // Input latching deliberately NOT enabled - inputs are read live, interrupts latch separately

  // Enable interrupt with switch state change - clears with port read
  write_cmd[0] = EXP_CMD_INTERRUPT_MASK; // Port 0
  write_cmd[1] = 0x00; // All IO's are switch inputs
  ret = ui_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  // EFM_ASSERT(ret == i2cTransferDone);
  write_cmd[0] = EXP_CMD_INTERRUPT_MASK + 1; //Advance to Port 1
  write_cmd[1] = 0xF0; // IO's 0-3 are switches
  ret = ui_transaction(I2C_FLAG_WRITE, &write_cmd[0], 2, NULL, 0);
  // EFM_ASSERT(ret == i2cTransferDone);
  (void)ret;
}

/*******************************************************************************
 **************************   GLOBAL FUNCTIONS   *******************************
 ******************************************************************************/

uint16_t get_ui_interrupt()
{
  uint16_t ui_interrupt;

  // Read interrupt register from Auxiliary board
  ui_target = UI_AUX_I2C_BUS_ADDRESS;
  ui_int_source = ui_target;
  ui_interrupt = get_ui_interrupts();

  if(ui_interrupt == 0){ // No Auxiliary board interrupts, so from Switch board
      ui_target = UI_SWITCH_I2C_BUS_ADDRESS;
      ui_int_source = ui_target;
      ui_interrupt = get_ui_interrupts();
  }

  // Return for processing by switch decode function
  return ui_interrupt;
}

uint16_t get_ui_status()
{
  I2C_TransferReturn_TypeDef ret;
  uint8_t cmd;
  uint8_t ports[2];
  uint16_t ui_status;

  // Set register read of the GPIO expander that triggered the interrupt
  ui_target = ui_int_source;

  // Read of port state inputs clears GPIO IC's internal interrupt flag
  cmd = EXP_CMD_READ_INPUTS;
  ret = ui_transaction(I2C_FLAG_WRITE_READ, &cmd, 1, &ports[0], 2);
  //EFM_ASSERT(ret == i2cTransferDone);
  (void)ret;

  // Save freshly read Port 0 and Port 1 into a packed status variable
  // Maybe atomic load/store, but almost certainly OK with current design

  ui_status = ((uint16_t) (ports[0] << 8)) | ports[1];

  // Return for processing by motor controls
  return ui_status;
}

void set_LED_on(led_color_t LED_color)
{
  // Blindly write LED update to both UI boards
  ui_target = UI_AUX_I2C_BUS_ADDRESS;
  set_LEDs_on(LED_color);
  ui_target = UI_SWITCH_I2C_BUS_ADDRESS;
  set_LEDs_on(LED_color);

  // Set LED state on
  LED_state_on[LED_color] = true;
}

void set_LED_off(led_color_t LED_color)
{
  // Blindly write LED update to both UI boards
  ui_target = UI_AUX_I2C_BUS_ADDRESS;
  set_LEDs_off(LED_color);
  ui_target = UI_SWITCH_I2C_BUS_ADDRESS;
  set_LEDs_off(LED_color);

  // Set LED state off
  LED_state_on[LED_color] = false;
}



/*******************************************************************************
 * Initialize remote auxiliary and switch board GPIO expanders
 ******************************************************************************/
void init_ui_expander()
{
  // Blindly write configuration data to both UI boards
  ui_target = UI_AUX_I2C_BUS_ADDRESS;
  init_ui_expanders();
  ui_target = UI_SWITCH_I2C_BUS_ADDRESS;
  init_ui_expanders();

  // Flash LED's to confirm expanders communicating
  set_LED_on(LED_RED);
  sl_sleeptimer_delay_millisecond(200);
  set_LED_off(LED_RED);
  sl_sleeptimer_delay_millisecond(50);
  set_LED_on(LED_GREEN);
  sl_sleeptimer_delay_millisecond(200);
  set_LED_off(LED_GREEN);
  sl_sleeptimer_delay_millisecond(50);
  set_LED_on(LED_BLUE);
  sl_sleeptimer_delay_millisecond(200);
  set_LED_off(LED_BLUE);
  sl_sleeptimer_delay_millisecond(50);
  set_LED_on(LED_AMBER);
  sl_sleeptimer_delay_millisecond(200);
  set_LED_off(LED_AMBER);
}
