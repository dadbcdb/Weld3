/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h7xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "xprintf.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */
typedef struct
{
  uint32_t exc_return;
  uint32_t sp;
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t pc;
  uint32_t xpsr;
  uint32_t cfsr;
  uint32_t hfsr;
  uint32_t dfsr;
  uint32_t afsr;
  uint32_t mmfar;
  uint32_t bfar;
} HardFaultInfo_t;

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
volatile HardFaultInfo_t g_hardFaultInfo;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern ETH_HandleTypeDef heth;
extern DMA2D_HandleTypeDef hdma2d;
extern LTDC_HandleTypeDef hltdc;
extern TIM_HandleTypeDef htim23;

/* USER CODE BEGIN EV */
extern ADC_HandleTypeDef hadc1;
/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  uint32_t excReturn;
  __asm volatile ("mov %0, lr" : "=r" (excReturn));
  uint32_t *stack = (uint32_t *)((excReturn & (1UL << 2)) ? __get_PSP() : __get_MSP());

  /* When the extended floating-point frame is present, the core frame follows it. */
  if ((excReturn & (1UL << 4)) == 0U)
  {
    stack += 18;
  }

  g_hardFaultInfo.exc_return = excReturn;
  g_hardFaultInfo.sp = (uint32_t)stack;
  g_hardFaultInfo.r0 = stack[0];
  g_hardFaultInfo.r1 = stack[1];
  g_hardFaultInfo.r2 = stack[2];
  g_hardFaultInfo.r3 = stack[3];
  g_hardFaultInfo.r12 = stack[4];
  g_hardFaultInfo.lr = stack[5];
  g_hardFaultInfo.pc = stack[6];
  g_hardFaultInfo.xpsr = stack[7];
  g_hardFaultInfo.cfsr = SCB->CFSR;
  g_hardFaultInfo.hfsr = SCB->HFSR;
  g_hardFaultInfo.dfsr = SCB->DFSR;
  g_hardFaultInfo.afsr = SCB->AFSR;
  g_hardFaultInfo.mmfar = SCB->MMFAR;
  g_hardFaultInfo.bfar = SCB->BFAR;

  __disable_irq();
  xprintf("\r\n[FAULT] HardFault PC=%08lX LR=%08lX SP=%08lX EXC=%08lX\r\n",
          g_hardFaultInfo.pc, g_hardFaultInfo.lr,
          g_hardFaultInfo.sp, g_hardFaultInfo.exc_return);
  xprintf("[FAULT] CFSR=%08lX HFSR=%08lX MMFAR=%08lX BFAR=%08lX\r\n",
          g_hardFaultInfo.cfsr, g_hardFaultInfo.hfsr,
          g_hardFaultInfo.mmfar, g_hardFaultInfo.bfar);

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32H7xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h7xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles Ethernet global interrupt.
  */
void ETH_IRQHandler(void)
{
  /* USER CODE BEGIN ETH_IRQn 0 */

  /* USER CODE END ETH_IRQn 0 */
  HAL_ETH_IRQHandler(&heth);
  /* USER CODE BEGIN ETH_IRQn 1 */

  /* USER CODE END ETH_IRQn 1 */
}

/**
  * @brief This function handles LTDC global interrupt.
  */
void LTDC_IRQHandler(void)
{
  /* USER CODE BEGIN LTDC_IRQn 0 */

  /* USER CODE END LTDC_IRQn 0 */
  HAL_LTDC_IRQHandler(&hltdc);
  /* USER CODE BEGIN LTDC_IRQn 1 */

  /* USER CODE END LTDC_IRQn 1 */
}

/**
  * @brief This function handles DMA2D global interrupt.
  */
void DMA2D_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2D_IRQn 0 */

  /* USER CODE END DMA2D_IRQn 0 */
  HAL_DMA2D_IRQHandler(&hdma2d);
  /* USER CODE BEGIN DMA2D_IRQn 1 */

  /* USER CODE END DMA2D_IRQn 1 */
}

/**
  * @brief This function handles TIM23 global interrupt.
  */
void TIM23_IRQHandler(void)
{
  /* USER CODE BEGIN TIM23_IRQn 0 */

  /* USER CODE END TIM23_IRQn 0 */
  HAL_TIM_IRQHandler(&htim23);
  /* USER CODE BEGIN TIM23_IRQn 1 */

  /* USER CODE END TIM23_IRQn 1 */
}

/* USER CODE BEGIN 1 */

extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim4;

void DMA1_Stream0_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_adc1);
}

void TIM4_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim4);
}

/**
  * @brief This function handles ADC1 and ADC2 global interrupts.
  */
void ADC_IRQHandler(void)
{
  HAL_ADC_IRQHandler(&hadc1);
}

/* USER CODE END 1 */
