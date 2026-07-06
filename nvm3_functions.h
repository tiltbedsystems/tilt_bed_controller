/*
 * nvm3_variables.h
 *
 *  Created on: Nov 24, 2024
 *      Author: Andrew Duncan
 */

#ifndef NVM3_FUNCTIONS_H_
#define NVM3_FUNCTIONS_H_

#include "nvm3_default.h"
#include "nvm3_functions.h"

#define CUSTOM_ROLL_ANGLE_KEY           1
#define CUSTOM_PITCH_ANGLE_KEY          2
#define RAISE_COLLISION_SENSITIVITY_KEY 3
#define LOWER_COLLISION_SENSITIVITY_KEY 4

Ecode_t storeValue(uint32_t key, int16_t value);
Ecode_t readValue(uint32_t key, int16_t *value);
Ecode_t eraseValue(uint32_t key);
Ecode_t eraseAllStoredValues(void);
Ecode_t eraseStoredCollisionThresholds(void);
bool isValueStored(uint32_t key);

#endif /* NVM3_FUNCTIONS_H_ */
