/*
 * nvm3_functions.c
 *
 *  Created on: Nov 24, 2024
 *      Author: aduncan
 */

#include "nvm3.h"
#include "nvm3_default.h"
#include "nvm3_functions.h"


Ecode_t storeValue(uint32_t key, int16_t value)
{
  return nvm3_writeData(nvm3_defaultHandle, key, &value, sizeof(int16_t));
}

Ecode_t readValue(uint32_t key, int16_t *value)
{
  uint32_t type;
  size_t len;
  Ecode_t status = nvm3_getObjectInfo(nvm3_defaultHandle, key, &type, &len);
  if (status != ECODE_NVM3_OK) {
    return status;
  }
  if (len != sizeof(int16_t)) {
    return ECODE_NVM3_ERR_READ_DATA_SIZE;
  }
  return nvm3_readData(nvm3_defaultHandle, key, value, len);
}

Ecode_t eraseValue(uint32_t key)
{
  Ecode_t status = nvm3_deleteObject(nvm3_defaultHandle, key);
  if (status == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    return ECODE_NVM3_OK;
  }
  return status;
}

Ecode_t eraseAllStoredValues(void)
{
  return nvm3_eraseAll(nvm3_defaultHandle);
}

Ecode_t eraseStoredCollisionThresholds(void)
{
  Ecode_t status;

  status = nvm3_deleteObject(nvm3_defaultHandle, RAISE_COLLISION_SENSITIVITY_KEY);
  if (status != ECODE_NVM3_OK && status != ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    // Handle error
    return status;
  }

  status = nvm3_deleteObject(nvm3_defaultHandle, LOWER_COLLISION_SENSITIVITY_KEY);
  if (status != ECODE_NVM3_OK && status != ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    // Handle error
    return status;
  }

  return ECODE_NVM3_OK;
}

bool isValueStored(uint32_t key)
{
  uint32_t type;
  size_t len;

  Ecode_t status = nvm3_getObjectInfo(nvm3_defaultHandle, key, &type, &len);
  return (status == ECODE_NVM3_OK);
}

///
/// /// Collision detection security margins
//#define RAISE_HIGH_SENSITIVITY 300 // Default values - safest
//#define RAISE_MED_SENSITIVITY 450
//#define RAISE_LOW_SENSITIVITY 600
//#define RAISE_OFF_SENSITIVITY 10000
//#define LOWER_HIGH_SENSITIVITY 100 // Default values - safest
//#define LOWER_MED_SENSITIVITY 250
//#define LOWER_LOW_SENSITIVITY 350
//#define LOWER_OFF_SENSITIVITY 10000
