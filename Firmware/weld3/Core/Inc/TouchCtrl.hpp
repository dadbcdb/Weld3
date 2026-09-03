/*
 * TouchCtrl.hpp
 *
 *  Created on: 2023. 7. 17.
 *      Author: dadbc
 */

#pragma once

#include "MyTypeDef.h"
#include "main.h"

#define FT5336_CHIP_ID_REG          0xA8


int32_t I2C4_WriteReg(uint16_t DevAddr, uint16_t Reg, uint16_t MemAddSize, uint8_t *pData, uint16_t Length);
int32_t I2C4_ReadReg(uint16_t DevAddr, uint16_t Reg, uint16_t MemAddSize, uint8_t *pData, uint16_t Length);

class CTouchState
{
public:
	U32  TouchDetected;
	U32  TouchX;
	U32  TouchY;
};

void GetTouchState(CTouchState &ts);


#ifdef __cplusplus
extern "C" {
#endif

void TouchTest(void);

#ifdef  __cplusplus
}
#endif
