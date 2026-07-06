/*
 * adc_currents.c
 *
 * Includes portions of Silicon Labs example code
 *
 * ADC takes non-blocking single-ended measurements on all motor currents.
 *
 *  Created on: Jun 4, 2023
 *      Author: Alan Jones
 */


//#include "em_device.h"
//#include "em_chip.h"
#include "em_cmu.h"
#include "em_emu.h"
#include "em_iadc.h"
#include "em_gpio.h"

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/

// Set CLK_ADC to 10MHz
#define CLK_SRC_ADC_FREQ      20000000  // CLK_SRC_ADC
#define CLK_ADC_FREQ          10000000  // CLK_ADC - 10 MHz max in normal mode

// Number of scan channels
#define NUM_INPUTS 4 // Four motor currents of interest

/*
 * Specify the IADC input using the IADC_PosInput_t typedef.  This
 * must be paired with a corresponding macro definition that allocates
 * the corresponding ABUS to the IADC.  These are...
 *
 * GPIO->ABUSALLOC |= GPIO_ABUSALLOC_AEVEN0_ADC0
 * GPIO->ABUSALLOC |= GPIO_ABUSALLOC_AODD0_ADC0
 * GPIO->BBUSALLOC |= GPIO_BBUSALLOC_BEVEN0_ADC0
 * GPIO->BBUSALLOC |= GPIO_BBUSALLOC_BODD0_ADC0
 * GPIO->CDBUSALLOC |= GPIO_CDBUSALLOC_CDEVEN0_ADC0
 * GPIO->CDBUSALLOC |= GPIO_CDBUSALLOC_CDODD0_ADC0
 *
 * ...for port A, port B, and port C/D pins, even and odd, respectively.
 *
 * Four motor current measurements, all sequential on Port A from A5 to A8.
 *
 * Since there are two even pins and two odd pins, all four can be mapped using
 * a single configuration (the analog bus maximum through the cross-point switch
 * limit is four signals for each port/bus configuration).
 *
 */
#define IADC_INPUT_0_PORT_PIN     iadcPosInputPortAPin5;  // Motor 0
#define IADC_INPUT_1_PORT_PIN     iadcPosInputPortAPin6;  // Motor 1
#define IADC_INPUT_2_PORT_PIN     iadcPosInputPortAPin7;  // Motor 2
#define IADC_INPUT_3_PORT_PIN     iadcPosInputPortAPin8;  // Motor 3

// Four analog inputs, two even pins (A6 & A8) and two odd pins (A5 & A7)
#define IADC_INPUT_0_BUS          ABUSALLOC
#define IADC_INPUT_0_BUSALLOC     GPIO_ABUSALLOC_AODD0_ADC0
#define IADC_INPUT_1_BUS          ABUSALLOC
#define IADC_INPUT_1_BUSALLOC     GPIO_ABUSALLOC_AEVEN0_ADC0
#define IADC_INPUT_2_BUS          ABUSALLOC
#define IADC_INPUT_2_BUSALLOC     GPIO_ABUSALLOC_AODD1_ADC0
#define IADC_INPUT_3_BUS          ABUSALLOC
#define IADC_INPUT_3_BUSALLOC     GPIO_ABUSALLOC_AEVEN1_ADC0

/*******************************************************************************
 ***************************   PRIVATE VARIABLES   ******************************
 ******************************************************************************/

static volatile uint32_t scanResult[NUM_INPUTS];  // IADC value converted to mA

/**************************************************************************//**
 * @brief  IADC Initializer
 *****************************************************************************/
void initIADC (void)
{
  // Declare init structs
  IADC_Init_t init = IADC_INIT_DEFAULT;
  IADC_AllConfigs_t initAllConfigs = IADC_ALLCONFIGS_DEFAULT;
  IADC_InitScan_t initScan = IADC_INITSCAN_DEFAULT;
  IADC_ScanTable_t initScanTable = IADC_SCANTABLE_DEFAULT;    // Scan Table

  // Enable IADC0 and GPIO clock branches.
  CMU_ClockEnable(cmuClock_IADC0, true);
  CMU_ClockEnable(cmuClock_GPIO, true);

  // Select clock for IADC
  CMU_ClockSelectSet(cmuClock_IADCCLK, cmuSelect_FSRCO);

  // Modify the IADC init structures and then initialize
  init.warmup = iadcWarmupKeepWarm;

  // Set the HFSCLK prescale value here
  init.srcClkPrescale = IADC_calcSrcClkPrescale(IADC0, CLK_SRC_ADC_FREQ, 0);

  // Divide CLK_SRC_ADC to set the CLK_ADC frequency
  initAllConfigs.configs[0].adcClkPrescale = IADC_calcAdcClkPrescale(IADC0,
                                             CLK_ADC_FREQ,
                                             0,
                                             iadcCfgModeNormal,
                                             init.srcClkPrescale);

  // Average conversions to reduce motor/system noise before storing in FIFO
  initAllConfigs.configs[0].digAvg = _IADC_CFG_DIGAVG_AVG8;

  // Tag FIFO entry with scan table entry id - default is no id
  initScan.showId = true;

  /*
   * Configure entries in scan table. All are single-ended from
   * port inputs for motors 0 through 3 respectively.
   */
  initScanTable.entries[0].posInput = IADC_INPUT_0_PORT_PIN;
  initScanTable.entries[0].negInput = iadcNegInputGnd;
  initScanTable.entries[0].includeInScan = true;

  initScanTable.entries[1].posInput = IADC_INPUT_1_PORT_PIN;
  initScanTable.entries[1].negInput = iadcNegInputGnd;
  initScanTable.entries[1].includeInScan = true;

  initScanTable.entries[2].posInput = IADC_INPUT_2_PORT_PIN;
  initScanTable.entries[2].negInput = iadcNegInputGnd;
  initScanTable.entries[2].includeInScan = true;

  initScanTable.entries[3].posInput = IADC_INPUT_3_PORT_PIN;
  initScanTable.entries[3].negInput = iadcNegInputGnd;
  initScanTable.entries[3].includeInScan = true;

  // Initialize IADC
  IADC_init(IADC0, &init, &initAllConfigs);

  // Initialize scan
  IADC_initScan(IADC0, &initScan, &initScanTable);

  // Allocate the analog bus for ADC0 inputs
  GPIO->IADC_INPUT_0_BUS |= IADC_INPUT_0_BUSALLOC;
  GPIO->IADC_INPUT_1_BUS |= IADC_INPUT_1_BUSALLOC;
  GPIO->IADC_INPUT_2_BUS |= IADC_INPUT_2_BUSALLOC;
  GPIO->IADC_INPUT_3_BUS |= IADC_INPUT_3_BUSALLOC;

  // Clear any previous interrupt flags
  IADC_clearInt(IADC0, _IADC_IF_MASK);

  /*
   * Note the interrupt associated with SCANFIFODVL flag in the IADC_IF
   * register is not used. Similarly, the fifoDmaWakeup member of the initScan
   * structure is left at its default setting of false, so LDMA service is not
   * requested when the FIFO holds the specified number of samples.
   */

  // Enable Scan interrupts
  IADC_enableInt(IADC0, IADC_IEN_SCANTABLEDONE);

  // Enable ADC interrupts
  NVIC_ClearPendingIRQ(IADC_IRQn);
  NVIC_EnableIRQ(IADC_IRQn);
}

/**************************************************************************//**
 * @brief  IADC interrupt handler
 *****************************************************************************/
void IADC_IRQHandler(void)
{
  IADC_Result_t result = {0, 0};

  // While the FIFO count is non-zero...
  while (IADC_getScanFifoCnt(IADC0))
  {
    // Pull a scan result from the FIFO
    result = IADC_pullScanFifoResult(IADC0);

    /*
     * Calculate the voltage to motor current conversion as follows:
     *
     * For single-ended conversions, the result can range from 0 to +Vref.
     * Vref = VBGR = 1.21V, and with analog gain = 1 the full scale input
     * value is 1.21V. Op amp outputs scale the motor currents at 200 mV/A, or
     * 6000 mA full scale. Scaling the scan result in 12 bit modes (Full Scale =
     * 0xFFF, or 4095) by 1.5 yields the measured motor currents in milli-Amps
     * to within 2.5% without resorting to floating point calculations.
     */
    scanResult[result.id] = result.data + result.data/2;
  }

  /*
   * Clear the scan table complete interrupt.  Reading FIFO results
   * does not do this automatically.
   */
  IADC_clearInt(IADC0, IADC_IF_SCANTABLEDONE);
}

/**************************************************************************//**
 * @brief  Motor current interface functions
 *****************************************************************************/
void scan_motor_currents(void)
{
  // Execute a single scan of all four motor currents
  IADC_command(IADC0, iadcCmdStartScan);
}

void get_motor_currents(uint32_t *currents)
{
  /*
   * Conversion result variable is volatile, but should be safe to read
   * using scan/get sequence since scan is only triggered by scan command.
   */
  currents[0] = scanResult[0];
  currents[1] = scanResult[1];
  currents[2] = scanResult[2];
  currents[3] = scanResult[3];
}
