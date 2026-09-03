/*
 * TouchCtrl.cpp
 *
 *  Created on: 2023. 7. 17.
 *      Author: dadbc
 */

#include "TouchCtrl.hpp"
#include "MyTypeDef.h"
#include "xprintf.h"
#include "main.h"

extern I2C_HandleTypeDef hi2c4;

int32_t I2C4_WriteReg(uint16_t DevAddr, uint16_t Reg, uint16_t MemAddSize, uint8_t *pData, uint16_t Length)
{
  return HAL_I2C_Mem_Write(&hi2c4, DevAddr, Reg, MemAddSize, pData, Length, 1000);

}

/**
  * @brief  Read a register of the device through BUS
  * @param  DevAddr    Device address on BUS
  * @param  MemAddSize Size of internal memory address
  * @param  Reg        The target register address to read
  * @retval BSP status
  */
int32_t I2C4_ReadReg(uint16_t DevAddr, uint16_t Reg, uint16_t MemAddSize, uint8_t *pData, uint16_t Length)
{
  return  HAL_I2C_Mem_Read(&hi2c4, DevAddr, Reg, MemAddSize, pData, Length, 1000);
}

// 01110000
void TouchTest(void)
{
	U8 pbuf[8];
/*
	if(I2C4_ReadReg(0x70, FT5336_CHIP_ID_REG, I2C_MEMADD_SIZE_8BIT, pbuf, 1) == HAL_OK){
		xprintf("id:%d\n", (int)pbuf[0]);
	}
*/

	if(I2C4_ReadReg(0x70, 0, I2C_MEMADD_SIZE_8BIT, pbuf, 7) == HAL_OK){
		CTouchState ts;
		ts.TouchDetected = pbuf[2] & 0x0f; //number of touch points
		ts.TouchX = ((pbuf[3] & 0x0f) << 8) | pbuf[4];
		ts.TouchY = ((pbuf[5] & 0x0f) << 8) | pbuf[6];
		xprintf("x:%d, y:%d, det:%d\n", ts.TouchX, ts.TouchY, ts.TouchDetected);

	}
}

static U8 pbuf[9]={0,};
void GetTouchState(CTouchState &ts)
{
	if(I2C4_ReadReg(0x70, 0, I2C_MEMADD_SIZE_8BIT, pbuf, 7) == HAL_OK){
		ts.TouchDetected = pbuf[2] & 0x0f; //number of touch points
		if(ts.TouchDetected == 15) ts.TouchDetected = 0;
		ts.TouchX = ((pbuf[3] & 0x0f) << 8) | pbuf[4];
		ts.TouchY = ((pbuf[5] & 0x0f) << 8) | pbuf[6];
		xprintf("x:%d, y:%d, det:%d\n", ts.TouchX, ts.TouchY, ts.TouchDetected);
	}
}
