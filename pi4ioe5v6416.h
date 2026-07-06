/*
 * pi4ioe5v6416.h
 *
 *  Created on: Apr 16, 2023
 *      Author: Alan Jones
 */

#ifndef PI4IOE5V6416_H_
#define PI4IOE5V6416_H_

/*
 * Register definitions of PI4IOE5V6416 I2C GPIO expander.
 *
 * Port register bit ordering is:
 * MSB   Px.7 Px.6 Px.5 Px.4 Px.3 Px.2 Px.1 Px.0   LSB
 *
 * e.g. 0xF0 sets upper 4 GPIO ports to 1, and lower 4 to 0 in that register
 *
 * Most registers are in pairs - one for P0, the second for P1
 *
 * Datasheet indicates single register writes only, but sequential reads
 */

#define EXP_CMD_READ_INPUTS              0x00 // Reads input registers
#define EXP_CMD_OUTPUT                   0x02 // Reads/sets output registers when IO configured as output - 1 high, 0 low
#define EXP_CMD_POLARITY                 0x04 // Reads/sets input polarity - 0 normal, 1 inverted
#define EXP_CMD_CONFIGURE                0x06 // Reads/sets pin as input (1) or output (0)
#define EXP_CMD_OUTPUT_DRIVE             0x40 // Reads/sets drive strength 2.5 - 10 mA
#define EXP_CMD_INPUT_LATCH              0x44 // Reads/sets input latch state (1 sets latch)
#define EXP_CMD_PULL_ENABLE              0x46 // Reads/sets input pull enable (1) or disable (0)
#define EXP_CMD_PULL_UP_DOWN             0x48 // Reads/sets input as pull up (1) or down (0) when enabled
#define EXP_CMD_INTERRUPT_MASK           0x4A // Reads/sets interrupt mask bits (0 allows pin to generate interrupt)
#define EXP_CMD_READ_INTERRUPTS          0x4C // Reads source of interrupt when mask allows through (1 indicates IO source)
#define EXP_CMD_OUTPUT_TYPE              0x4F // Reads/sets either push-pull or open drain for port outputs (by port)

#endif /* PI4IOE5V6416_H_ */
