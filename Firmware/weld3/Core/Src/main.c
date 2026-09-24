/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "lwip.h"
#include "app_touchgfx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
//#include "stm32h735g_discovery_ospi.h"
#include "MainProc.h"
#include "WaveTest.h"
#include "CurrentFeedbackFilter.h"
#include "stm32h7xx_hal_ospi.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PWM_A_CHANNEL          TIM_CHANNEL_2
#define PWM_WAVE_TEST_ENABLE   1U /* Explicit single-shot mode; no boot PWM. */
#define PWM_B_CHANNEL          TIM_CHANNEL_4
#define ADC_TRIGGER_CHANNEL    TIM_CHANNEL_3
#define PWM_PERIOD_TICKS       65536U
#define PWM_TEST_INITIAL_CCR   4096U
#define PWM_OR_THRESHOLD       20000U
#define PWM_OR_TARGET          0.50f
#define PWM_OR_KP              0.20f
#define PWM_OR_KI              15.0f
#define PWM_OR_KD              0.0f /* Quantized feedback: start with PI. */
#ifndef PWM_OR_BENCH_ALLOW_ZERO
#define PWM_OR_BENCH_ALLOW_ZERO 1U /* TEST ONLY: zero feedback drives duty upward. */
#endif
#ifndef PWM_OR_BENCH_HOLD_ON_INVALID
#define PWM_OR_BENCH_HOLD_ON_INVALID 1U /* TEST ONLY: power stage disconnected. */
#endif
#define CURRENT_DMA_SAMPLES    64U
#define CURRENT_DMA_HALF       (CURRENT_DMA_SAMPLES / 2U)
#define CURRENT_CONTROL_ENABLE 0U
#define DAC_SINE_TEST_ENABLE 1U
#define DAC_SINE_HZ          1000U
#define DAC_SINE_SAMPLES     64U
#if CURRENT_SENSE_IRQ_PRIORITY >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
#error "Current acquisition IRQ must not be masked by FreeRTOS critical sections"
#endif
#if CURRENT_CONTROL_ENABLE
#error "Closed-loop control requires verified sensor scaling, limits and gate safety"
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
ADC_HandleTypeDef hadc3;

CRC_HandleTypeDef hcrc;

DAC_HandleTypeDef hdac1;

DMA2D_HandleTypeDef hdma2d;

FDCAN_HandleTypeDef hfdcan1;
FDCAN_HandleTypeDef hfdcan2;

I2C_HandleTypeDef hi2c4;

LTDC_HandleTypeDef hltdc;

OSPI_HandleTypeDef hospi1;
OSPI_HandleTypeDef hospi2;

SAI_HandleTypeDef hsai_BlockA1;
SAI_HandleTypeDef hsai_BlockB1;

SD_HandleTypeDef hsd1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim4;
TIM_HandleTypeDef htim5;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;

PCD_HandleTypeDef hpcd_USB_OTG_HS;
HCD_HandleTypeDef hhcd_USB_OTG_HS;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */
/* CubeMX does not currently emit this definition even though the preserved
 * ADC1 MSP/DMA code and IRQ handler use it. Keep it in a generated-safe area. */
DMA_HandleTypeDef hdma_adc1;
volatile uint32_t g_adc1Value;
volatile uint16_t g_primaryCurrentAdc;
volatile uint16_t g_secondaryCurrentAdc;
/* ADC2 path reserved for conditioned Rogowski feedback; normalized, not A. */
volatile float g_secondaryCurrentFiltered;
static CurrentFeedbackFilter secondaryFeedbackFilter;
volatile uint16_t g_primaryCurrentMin;
volatile uint16_t g_primaryCurrentMax;
volatile uint16_t g_secondaryCurrentMin;
volatile uint16_t g_secondaryCurrentMax;
volatile uint32_t g_currentDmaBlocks;
static TIM_HandleTypeDef htim3;
static TIM_HandleTypeDef htim6Dac;
static DMA_HandleTypeDef hdmaDac2;
volatile uint32_t g_dacSineError;
volatile uint32_t g_dacSineDmaError;
volatile uint32_t g_dacSineSampleRateHz;
/* 2048 +/- 1241 codes. RAM_D1 source; never modified after cache clean.
   First DHR value is 2048; DMA starts with the following phase sample. */
__attribute__((aligned(32))) static uint32_t dacSineTable[DAC_SINE_SAMPLES] = {
  2170U, 2290U, 2408U, 2523U, 2633U, 2737U, 2835U, 2926U,
  3007U, 3080U, 3142U, 3195U, 3236U, 3265U, 3283U, 3289U,
  3283U, 3265U, 3236U, 3195U, 3142U, 3080U, 3007U, 2926U,
  2835U, 2737U, 2633U, 2523U, 2408U, 2290U, 2170U, 2048U,
  1926U, 1806U, 1688U, 1573U, 1463U, 1359U, 1261U, 1170U,
  1089U, 1016U, 954U, 901U, 860U, 831U, 813U, 807U,
  813U, 831U, 860U, 901U, 954U, 1016U, 1089U, 1170U,
  1261U, 1359U, 1463U, 1573U, 1688U, 1806U, 1926U, 2048U,
};
/* Raw codes only. Half 0/1 denote fixed PWM half-periods, not variable ON time. */
typedef struct
{
  uint16_t primaryMean, secondaryMean;
  uint16_t primaryMin, primaryMax, secondaryMin, secondaryMax;
  uint32_t sequence;
} CurrentHalfStats;
volatile CurrentHalfStats g_currentHalf[2];
volatile uint32_t g_currentProcessingCycles;
volatile uint32_t g_currentMaxProcessingCycles;
volatile uint32_t g_currentDeadlineMisses;
volatile uint32_t g_currentSenseFault; /* 1=DMA ownership/order, 2=ADC/DMA error */
volatile uint32_t g_currentAdcError;
volatile uint32_t g_currentDmaError;
volatile uint32_t g_currentHalfBudgetCycles;
volatile uint32_t g_currentSampleRateHz;
volatile uint32_t g_currentPwmCountAtCallback;
volatile uint32_t g_currentWarmupBlocks = 2U; /* Discard startup/preload period. */
/* First failure snapshot, read after fault without breakpoints in the ISR. */
typedef struct
{
  uint32_t reason; /* 1=order, 2=entry ownership, 3=exit ownership,
                      4=processing deadline, 5=publication deadline,
                      6=PWM preload window missed */
  uint32_t offset, expectedOffset, ndtrEntry, ndtrExit;
  uint32_t elapsedCycles, budgetCycles, warmupBlocks, publishedBlocks;
  uint32_t dmaFlags, pwmCount;
} CurrentFaultDetail;
volatile CurrentFaultDetail g_currentFaultDetail;
static uint32_t currentExpectedOffset;
/* Bench-only OR occupancy feedback, NOT calibrated welding current. */
volatile float g_pwmOrMeasured;
volatile float g_pwmOrCommand = 0.25f;
volatile uint32_t g_pwmOrUpdates;
volatile uint32_t g_pwmOrFault; /* 1=acquisition, 2=no HIGH/LOW in 16 periods */
volatile uint32_t g_pwmOrInputInvalid; /* 0=mixed/unassessed, 1=all LOW, 2=all HIGH */
volatile uint32_t g_pwmOrInvalidWindows;
/* First-stop evidence survives PwmOr_Stop changing timer/GPIO registers. */
volatile uint32_t g_pwmStartupStage; /* 0=before acquisition, 1=init, 2=armed, 3=started */
volatile uintptr_t g_pwmErrorCaller; /* Error_Handler return address; resolve with ELF */
typedef struct
{
  uint32_t valid, startupStage, fault, acquisitionFault, detailReason;
  uint32_t cr1, ccer, ccmr1, ccmr2, cnt, arr, ccr2, ccr4;
  uint32_t tim3Cr1, tim3Cnt, ndtr, adc1Error, adc2Error;
  uint32_t inputMin, inputMax, highSamples, totalSamples;
} PwmStopDetail;
volatile PwmStopDetail g_pwmStopDetail;
static uint32_t pwmOrHigh, pwmOrSamples;
static float pwmOrIntegral = 0.25f, pwmOrPrevious;
__attribute__((aligned(32))) static uint32_t g_currentDmaBuffer[CURRENT_DMA_SAMPLES];
static WeldSettings waveSettings;
static WaveTestStatus waveStatus;
static WaveTestSample waveSamples[2048];
static float wavePeak;
static uint32_t waveLimit, waveRunPairs, waveLeaseTick, waveDmaTick;
static uint32_t waveInterval, waveNextLog, waveArmHalves;
static WavePidConfig wavePid = {0.20f, 15.0f, 0.0f, 0.50f, 0U};
static float waveIntegral, wavePrevious, waveOutput;
/* Feedback-only low-pass: tau=1 ms, nominal coefficient about 0.1065.
 * State is seeded from the current ADC mean at every single-shot start. */
static const float waveFilterTauSeconds = 0.001f;
static float waveFilterAlpha;
volatile float g_wavePidFiltered;
static void WaveTest_Tick(void);
static void WaveTest_Dma(void);

osThreadId_t TouchGFXTaskHandle;
const osThreadAttr_t TouchGFXTask_attributes = {
  .name = "TouchGFXTask",
  .stack_size = 1024 * 8,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_ADC3_Init(void);
static void MX_DAC1_Init(void);
static void MX_FDCAN1_Init(void);
static void MX_FDCAN2_Init(void);
static void MX_I2C4_Init(void);
static void MX_OCTOSPI1_Init(void);
static void MX_OCTOSPI2_Init(void);
static void MX_LTDC_Init(void);
static void MX_SAI1_Init(void);
static void MX_SDMMC1_SD_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM5_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_HS_PCD_Init(void);
static void MX_DMA2D_Init(void);
static void MX_CRC_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
extern void TouchGFX_Task(void *argument);
static void LCD_DelayMs(uint32_t milliseconds);
static void CurrentSense_Init(void);
static void DacSine_Start(void);
static void CurrentSense_ProcessBlock(uint32_t offset, uint32_t count);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void LCD_DelayMs(uint32_t milliseconds)
{
  uint32_t start;
  uint32_t cycles;

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  start = DWT->CYCCNT;
  cycles = (SystemCoreClock / 1000U) * milliseconds;
  while ((uint32_t)(DWT->CYCCNT - start) < cycles)
  {
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_ADC3_Init();
  MX_DAC1_Init();
  MX_FDCAN1_Init();
  MX_FDCAN2_Init();
  MX_I2C4_Init();
  MX_OCTOSPI1_Init();
  MX_OCTOSPI2_Init();
  MX_LTDC_Init();
  MX_SAI1_Init();
  MX_SDMMC1_SD_Init();
  MX_TIM1_Init();
  MX_TIM4_Init();
  MX_TIM5_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_USB_OTG_HS_PCD_Init();
  MX_DMA2D_Init();
  MX_CRC_Init();
  MX_TouchGFX_Init();
  /* Call PreOsInit function */
  MX_TouchGFX_PreOSInit();
  /* USER CODE BEGIN 2 */
  g_pwmStartupStage = 1U;
  CurrentSense_Init();
  g_pwmStartupStage = 2U;

  /* A is centered at CNT=0, B at CNT=ARR. Single-shot mode loads OFF
     compares before enabling pins; the legacy OR test starts at 12.5% each. */
  TIM4->CCR2 = PWM_WAVE_TEST_ENABLE ? 0U : PWM_TEST_INITIAL_CCR;
  TIM4->CCR4 = PWM_WAVE_TEST_ENABLE ? TIM4->ARR + 1U : TIM4->ARR - PWM_TEST_INITIAL_CCR;
  if (PWM_WAVE_TEST_ENABLE) g_pwmOrCommand = 0.0f;
  TIM4->EGR = TIM_EGR_UG;
  TIM4->SR = 0U;
  SET_BIT(TIM4->CCER, TIM_CCER_CC2E | TIM_CCER_CC4E);
  SET_BIT(TIM4->CR1, TIM_CR1_CEN); /* Also starts the waiting TIM3. */
  g_pwmStartupStage = 3U;

  /* PG3 is now owned exclusively by DMA processing, not TIM4 interrupts. */

  /* Release the LCD panel from reset, then enable it and its backlight. */
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
  LCD_DelayMs(20);
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
  LCD_DelayMs(120);
  HAL_GPIO_WritePin(LCD_DISP_GPIO_Port, LCD_DISP_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
  LCD_DelayMs(40);
#if DAC_SINE_TEST_ENABLE
  DacSine_Start();
#endif
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  TouchGFXTaskHandle = osThreadNew(TouchGFX_Task, NULL, &TouchGFXTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 110;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_OSPI|RCC_PERIPHCLK_ADC;
  PeriphClkInitStruct.PLL2.PLL2M = 5;
  PeriphClkInitStruct.PLL2.PLL2N = 80;
  PeriphClkInitStruct.PLL2.PLL2P = 5;
  PeriphClkInitStruct.PLL2.PLL2Q = 2;
  PeriphClkInitStruct.PLL2.PLL2R = 2;
  PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_2;
  PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
  PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
  PeriphClkInitStruct.OspiClockSelection = RCC_OSPICLKSOURCE_PLL2;
  PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_16B;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc1.Init.OversamplingMode = DISABLE;
  hadc1.Init.Oversampling.Ratio = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Common config
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc2.Init.Resolution = ADC_RESOLUTION_16B;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc2.Init.LowPowerAutoWait = DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc2.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  hadc2.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc2.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc2.Init.OversamplingMode = DISABLE;
  hadc2.Init.Oversampling.Ratio = 1;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

/**
  * @brief ADC3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC3_Init(void)
{

  /* USER CODE BEGIN ADC3_Init 0 */

  /* USER CODE END ADC3_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC3_Init 1 */

  /* USER CODE END ADC3_Init 1 */

  /** Common config
  */
  hadc3.Instance = ADC3;
  hadc3.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc3.Init.Resolution = ADC_RESOLUTION_12B;
  hadc3.Init.DataAlign = ADC3_DATAALIGN_RIGHT;
  hadc3.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc3.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc3.Init.LowPowerAutoWait = DISABLE;
  hadc3.Init.ContinuousConvMode = DISABLE;
  hadc3.Init.NbrOfConversion = 1;
  hadc3.Init.DiscontinuousConvMode = DISABLE;
  hadc3.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc3.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc3.Init.DMAContinuousRequests = DISABLE;
  hadc3.Init.SamplingMode = ADC_SAMPLING_MODE_NORMAL;
  hadc3.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  hadc3.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc3.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc3.Init.OversamplingMode = DISABLE;
  hadc3.Init.Oversampling.Ratio = ADC3_OVERSAMPLING_RATIO_2;
  if (HAL_ADC_Init(&hadc3) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC3_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  sConfig.OffsetSign = ADC3_OFFSET_SIGN_NEGATIVE;
  sConfig.OffsetSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc3, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC3_Init 2 */

  /* USER CODE END ADC3_Init 2 */

}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
  hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
  hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
  hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

}

/**
  * @brief DAC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC1_Init(void)
{

  /* USER CODE BEGIN DAC1_Init 0 */

  /* USER CODE END DAC1_Init 0 */

  DAC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN DAC1_Init 1 */

  /* USER CODE END DAC1_Init 1 */

  /** DAC Initialization
  */
  hdac1.Instance = DAC1;
  if (HAL_DAC_Init(&hdac1) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT2 config
  */
  sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
  sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DAC1_Init 2 */

  /* USER CODE END DAC1_Init 2 */

}

/**
  * @brief DMA2D Initialization Function
  * @param None
  * @retval None
  */
static void MX_DMA2D_Init(void)
{

  /* USER CODE BEGIN DMA2D_Init 0 */

  /* USER CODE END DMA2D_Init 0 */

  /* USER CODE BEGIN DMA2D_Init 1 */

  /* USER CODE END DMA2D_Init 1 */
  hdma2d.Instance = DMA2D;
  hdma2d.Init.Mode = DMA2D_R2M;
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB888;
  hdma2d.Init.OutputOffset = 0;
  if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DMA2D_Init 2 */

  /* USER CODE END DMA2D_Init 2 */

}

/**
  * @brief FDCAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN1_Init(void)
{

  /* USER CODE BEGIN FDCAN1_Init 0 */

  /* USER CODE END FDCAN1_Init 0 */

  /* USER CODE BEGIN FDCAN1_Init 1 */

  /* USER CODE END FDCAN1_Init 1 */
  hfdcan1.Instance = FDCAN1;
  hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan1.Init.AutoRetransmission = DISABLE;
  hfdcan1.Init.TransmitPause = DISABLE;
  hfdcan1.Init.ProtocolException = DISABLE;
  hfdcan1.Init.NominalPrescaler = 16;
  hfdcan1.Init.NominalSyncJumpWidth = 1;
  hfdcan1.Init.NominalTimeSeg1 = 1;
  hfdcan1.Init.NominalTimeSeg2 = 1;
  hfdcan1.Init.DataPrescaler = 1;
  hfdcan1.Init.DataSyncJumpWidth = 1;
  hfdcan1.Init.DataTimeSeg1 = 1;
  hfdcan1.Init.DataTimeSeg2 = 1;
  hfdcan1.Init.MessageRAMOffset = 0;
  hfdcan1.Init.StdFiltersNbr = 0;
  hfdcan1.Init.ExtFiltersNbr = 0;
  hfdcan1.Init.RxFifo0ElmtsNbr = 0;
  hfdcan1.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
  hfdcan1.Init.RxFifo1ElmtsNbr = 0;
  hfdcan1.Init.RxFifo1ElmtSize = FDCAN_DATA_BYTES_8;
  hfdcan1.Init.RxBuffersNbr = 0;
  hfdcan1.Init.RxBufferSize = FDCAN_DATA_BYTES_8;
  hfdcan1.Init.TxEventsNbr = 0;
  hfdcan1.Init.TxBuffersNbr = 0;
  hfdcan1.Init.TxFifoQueueElmtsNbr = 0;
  hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  hfdcan1.Init.TxElmtSize = FDCAN_DATA_BYTES_8;
  if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN1_Init 2 */

  /* USER CODE END FDCAN1_Init 2 */

}

/**
  * @brief FDCAN2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN2_Init(void)
{

  /* USER CODE BEGIN FDCAN2_Init 0 */

  /* USER CODE END FDCAN2_Init 0 */

  /* USER CODE BEGIN FDCAN2_Init 1 */

  /* USER CODE END FDCAN2_Init 1 */
  hfdcan2.Instance = FDCAN2;
  hfdcan2.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan2.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan2.Init.AutoRetransmission = DISABLE;
  hfdcan2.Init.TransmitPause = DISABLE;
  hfdcan2.Init.ProtocolException = DISABLE;
  hfdcan2.Init.NominalPrescaler = 16;
  hfdcan2.Init.NominalSyncJumpWidth = 1;
  hfdcan2.Init.NominalTimeSeg1 = 1;
  hfdcan2.Init.NominalTimeSeg2 = 1;
  hfdcan2.Init.DataPrescaler = 1;
  hfdcan2.Init.DataSyncJumpWidth = 1;
  hfdcan2.Init.DataTimeSeg1 = 1;
  hfdcan2.Init.DataTimeSeg2 = 1;
  hfdcan2.Init.MessageRAMOffset = 0;
  hfdcan2.Init.StdFiltersNbr = 0;
  hfdcan2.Init.ExtFiltersNbr = 0;
  hfdcan2.Init.RxFifo0ElmtsNbr = 0;
  hfdcan2.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
  hfdcan2.Init.RxFifo1ElmtsNbr = 0;
  hfdcan2.Init.RxFifo1ElmtSize = FDCAN_DATA_BYTES_8;
  hfdcan2.Init.RxBuffersNbr = 0;
  hfdcan2.Init.RxBufferSize = FDCAN_DATA_BYTES_8;
  hfdcan2.Init.TxEventsNbr = 0;
  hfdcan2.Init.TxBuffersNbr = 0;
  hfdcan2.Init.TxFifoQueueElmtsNbr = 0;
  hfdcan2.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  hfdcan2.Init.TxElmtSize = FDCAN_DATA_BYTES_8;
  if (HAL_FDCAN_Init(&hfdcan2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN2_Init 2 */

  /* USER CODE END FDCAN2_Init 2 */

}

/**
  * @brief I2C4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C4_Init(void)
{

  /* USER CODE BEGIN I2C4_Init 0 */

  /* USER CODE END I2C4_Init 0 */

  /* USER CODE BEGIN I2C4_Init 1 */

  /* USER CODE END I2C4_Init 1 */
  hi2c4.Instance = I2C4;
  hi2c4.Init.Timing = 0x60404E72;
  hi2c4.Init.OwnAddress1 = 0;
  hi2c4.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c4.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c4.Init.OwnAddress2 = 0;
  hi2c4.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c4.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c4.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c4) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c4, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c4, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C4_Init 2 */

  /* USER CODE END I2C4_Init 2 */

}

/**
  * @brief LTDC Initialization Function
  * @param None
  * @retval None
  */
static void MX_LTDC_Init(void)
{

  /* USER CODE BEGIN LTDC_Init 0 */

  /* USER CODE END LTDC_Init 0 */

  LTDC_LayerCfgTypeDef pLayerCfg = {0};

  /* USER CODE BEGIN LTDC_Init 1 */

  /* USER CODE END LTDC_Init 1 */
  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  hltdc.Init.HorizontalSync = 40;
  hltdc.Init.VerticalSync = 9;
  hltdc.Init.AccumulatedHBP = 53;
  hltdc.Init.AccumulatedVBP = 11;
  hltdc.Init.AccumulatedActiveW = 533;
  hltdc.Init.AccumulatedActiveH = 283;
  hltdc.Init.TotalWidth = 565;
  hltdc.Init.TotalHeigh = 285;
  hltdc.Init.Backcolor.Blue = 0;
  hltdc.Init.Backcolor.Green = 255;
  hltdc.Init.Backcolor.Red = 0;
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }
  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = 480;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = 272;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB888;
  pLayerCfg.Alpha = 255;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  pLayerCfg.FBStartAdress = 0x70000000;
  pLayerCfg.ImageWidth = 480;
  pLayerCfg.ImageHeight = 272;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LTDC_Init 2 */

  /* USER CODE END LTDC_Init 2 */

}

/**
  * @brief OCTOSPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_OCTOSPI1_Init(void)
{

  /* USER CODE BEGIN OCTOSPI1_Init 0 */

  /* USER CODE END OCTOSPI1_Init 0 */

  OSPIM_CfgTypeDef sOspiManagerCfg = {0};

  /* USER CODE BEGIN OCTOSPI1_Init 1 */

  /* USER CODE END OCTOSPI1_Init 1 */
  /* OCTOSPI1 parameter configuration*/
  hospi1.Instance = OCTOSPI1;
  hospi1.Init.FifoThreshold = 1;
  hospi1.Init.DualQuad = HAL_OSPI_DUALQUAD_DISABLE;
  hospi1.Init.MemoryType = HAL_OSPI_MEMTYPE_MICRON;
  hospi1.Init.DeviceSize = 26;
  hospi1.Init.ChipSelectHighTime = 1;
  hospi1.Init.FreeRunningClock = HAL_OSPI_FREERUNCLK_DISABLE;
  hospi1.Init.ClockMode = HAL_OSPI_CLOCK_MODE_0;
  hospi1.Init.WrapSize = HAL_OSPI_WRAP_NOT_SUPPORTED;
  hospi1.Init.ClockPrescaler = 2;
  hospi1.Init.SampleShifting = HAL_OSPI_SAMPLE_SHIFTING_NONE;
  hospi1.Init.DelayHoldQuarterCycle = HAL_OSPI_DHQC_DISABLE;
  hospi1.Init.ChipSelectBoundary = 0;
  hospi1.Init.DelayBlockBypass = HAL_OSPI_DELAY_BLOCK_BYPASSED;
  hospi1.Init.MaxTran = 0;
  hospi1.Init.Refresh = 0;
  if (HAL_OSPI_Init(&hospi1) != HAL_OK)
  {
    Error_Handler();
  }
  sOspiManagerCfg.ClkPort = 1;
  sOspiManagerCfg.DQSPort = 1;
  sOspiManagerCfg.NCSPort = 1;
  sOspiManagerCfg.IOLowPort = HAL_OSPIM_IOPORT_1_LOW;
  sOspiManagerCfg.IOHighPort = HAL_OSPIM_IOPORT_1_HIGH;
  if (HAL_OSPIM_Config(&hospi1, &sOspiManagerCfg, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN OCTOSPI1_Init 2 */

  /* USER CODE END OCTOSPI1_Init 2 */

}

/**
  * @brief OCTOSPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_OCTOSPI2_Init(void)
{

  /* USER CODE BEGIN OCTOSPI2_Init 0 */

  /* USER CODE END OCTOSPI2_Init 0 */

  OSPIM_CfgTypeDef sOspiManagerCfg = {0};
  OSPI_HyperbusCfgTypeDef sHyperBusCfg = {0};

  /* USER CODE BEGIN OCTOSPI2_Init 1 */
  OSPI_HyperbusCmdTypeDef sCommand = {0};
  OSPI_MemoryMappedTypeDef sMemMappedCfg = {0};
  /* USER CODE END OCTOSPI2_Init 1 */
  /* OCTOSPI2 parameter configuration*/
  hospi2.Instance = OCTOSPI2;
  hospi2.Init.FifoThreshold = 4;
  hospi2.Init.DualQuad = HAL_OSPI_DUALQUAD_DISABLE;
  hospi2.Init.MemoryType = HAL_OSPI_MEMTYPE_HYPERBUS;
  hospi2.Init.DeviceSize = 32;
  hospi2.Init.ChipSelectHighTime = 4;
  hospi2.Init.FreeRunningClock = HAL_OSPI_FREERUNCLK_DISABLE;
  hospi2.Init.ClockMode = HAL_OSPI_CLOCK_MODE_0;
  hospi2.Init.WrapSize = HAL_OSPI_WRAP_NOT_SUPPORTED;
  hospi2.Init.ClockPrescaler = 2;
  hospi2.Init.SampleShifting = HAL_OSPI_SAMPLE_SHIFTING_NONE;
  hospi2.Init.DelayHoldQuarterCycle = HAL_OSPI_DHQC_ENABLE;
  hospi2.Init.ChipSelectBoundary = 23;
  hospi2.Init.DelayBlockBypass = HAL_OSPI_DELAY_BLOCK_USED;
  hospi2.Init.MaxTran = 0;
  hospi2.Init.Refresh = 400;
  if (HAL_OSPI_Init(&hospi2) != HAL_OK)
  {
    Error_Handler();
  }
  sOspiManagerCfg.ClkPort = 2;
  sOspiManagerCfg.DQSPort = 2;
  sOspiManagerCfg.NCSPort = 2;
  sOspiManagerCfg.IOLowPort = HAL_OSPIM_IOPORT_2_LOW;
  sOspiManagerCfg.IOHighPort = HAL_OSPIM_IOPORT_2_HIGH;
  if (HAL_OSPIM_Config(&hospi2, &sOspiManagerCfg, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    Error_Handler();
  }
  sHyperBusCfg.RWRecoveryTime = 3;
  sHyperBusCfg.AccessTime = 6;
  sHyperBusCfg.WriteZeroLatency = HAL_OSPI_LATENCY_ON_WRITE;
  sHyperBusCfg.LatencyMode = HAL_OSPI_FIXED_LATENCY;
  if (HAL_OSPI_HyperbusCfg(&hospi2, &sHyperBusCfg, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN OCTOSPI2_Init 2 */
  sCommand.AddressSpace = HAL_OSPI_MEMORY_ADDRESS_SPACE;
  sCommand.AddressSize = HAL_OSPI_ADDRESS_32_BITS;
  sCommand.DQSMode = HAL_OSPI_DQS_ENABLE;
  sCommand.Address = 0;
  sCommand.NbData = 1;

  if (HAL_OSPI_HyperbusCmd(&hospi2, &sCommand, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    Error_Handler();
  }

  sMemMappedCfg.TimeOutActivation = HAL_OSPI_TIMEOUT_COUNTER_DISABLE;

  if (HAL_OSPI_MemoryMapped(&hospi2, &sMemMappedCfg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END OCTOSPI2_Init 2 */

}

/**
  * @brief SAI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SAI1_Init(void)
{

  /* USER CODE BEGIN SAI1_Init 0 */

  /* USER CODE END SAI1_Init 0 */

  /* USER CODE BEGIN SAI1_Init 1 */

  /* USER CODE END SAI1_Init 1 */
  hsai_BlockA1.Instance = SAI1_Block_A;
  hsai_BlockA1.Init.Protocol = SAI_FREE_PROTOCOL;
  hsai_BlockA1.Init.AudioMode = SAI_MODESLAVE_RX;
  hsai_BlockA1.Init.DataSize = SAI_DATASIZE_8;
  hsai_BlockA1.Init.FirstBit = SAI_FIRSTBIT_MSB;
  hsai_BlockA1.Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
  hsai_BlockA1.Init.Synchro = SAI_SYNCHRONOUS;
  hsai_BlockA1.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockA1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockA1.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockA1.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockA1.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockA1.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  hsai_BlockA1.Init.PdmInit.Activation = DISABLE;
  hsai_BlockA1.Init.PdmInit.MicPairsNbr = 0;
  hsai_BlockA1.Init.PdmInit.ClockEnable = SAI_PDM_CLOCK1_ENABLE;
  hsai_BlockA1.FrameInit.FrameLength = 8;
  hsai_BlockA1.FrameInit.ActiveFrameLength = 1;
  hsai_BlockA1.FrameInit.FSDefinition = SAI_FS_STARTFRAME;
  hsai_BlockA1.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;
  hsai_BlockA1.FrameInit.FSOffset = SAI_FS_FIRSTBIT;
  hsai_BlockA1.SlotInit.FirstBitOffset = 0;
  hsai_BlockA1.SlotInit.SlotSize = SAI_SLOTSIZE_DATASIZE;
  hsai_BlockA1.SlotInit.SlotNumber = 1;
  hsai_BlockA1.SlotInit.SlotActive = 0x00000000;
  if (HAL_SAI_Init(&hsai_BlockA1) != HAL_OK)
  {
    Error_Handler();
  }
  hsai_BlockB1.Instance = SAI1_Block_B;
  hsai_BlockB1.Init.Protocol = SAI_FREE_PROTOCOL;
  hsai_BlockB1.Init.AudioMode = SAI_MODEMASTER_TX;
  hsai_BlockB1.Init.DataSize = SAI_DATASIZE_8;
  hsai_BlockB1.Init.FirstBit = SAI_FIRSTBIT_MSB;
  hsai_BlockB1.Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
  hsai_BlockB1.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockB1.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockB1.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  hsai_BlockB1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockB1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_192K;
  hsai_BlockB1.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockB1.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockB1.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockB1.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  hsai_BlockB1.Init.PdmInit.Activation = DISABLE;
  hsai_BlockB1.Init.PdmInit.MicPairsNbr = 0;
  hsai_BlockB1.Init.PdmInit.ClockEnable = SAI_PDM_CLOCK1_ENABLE;
  hsai_BlockB1.FrameInit.FrameLength = 8;
  hsai_BlockB1.FrameInit.ActiveFrameLength = 1;
  hsai_BlockB1.FrameInit.FSDefinition = SAI_FS_STARTFRAME;
  hsai_BlockB1.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;
  hsai_BlockB1.FrameInit.FSOffset = SAI_FS_FIRSTBIT;
  hsai_BlockB1.SlotInit.FirstBitOffset = 0;
  hsai_BlockB1.SlotInit.SlotSize = SAI_SLOTSIZE_DATASIZE;
  hsai_BlockB1.SlotInit.SlotNumber = 1;
  hsai_BlockB1.SlotInit.SlotActive = 0x00000000;
  if (HAL_SAI_Init(&hsai_BlockB1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SAI1_Init 2 */

  /* USER CODE END SAI1_Init 2 */

}

/**
  * @brief SDMMC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SDMMC1_SD_Init(void)
{

  /* USER CODE BEGIN SDMMC1_Init 0 */
  /* PF5 is active-low on STM32H735G-DK. Do not stop the whole boot when no
     card is inserted. */
  if (HAL_GPIO_ReadPin(uSD_Detect_GPIO_Port, uSD_Detect_Pin) == GPIO_PIN_SET)
  {
    return;
  }
  /* USER CODE END SDMMC1_Init 0 */

  /* USER CODE BEGIN SDMMC1_Init 1 */

  /* USER CODE END SDMMC1_Init 1 */
  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
  hsd1.Init.ClockDiv = 0;
  if (HAL_SD_Init(&hsd1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SDMMC1_Init 2 */
  /* Switch to the board's 4-bit data bus only after card identification. */
  if (HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END SDMMC1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 65535;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* Bench-only center-aligned PWM: full period = 2*32768 timer ticks.
     PWM1 at zero and PWM2 at ARR give equal pulses T/2 apart. */
  htim4.Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
  htim4.Init.Period = PWM_PERIOD_TICKS / 2U;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK) Error_Handler();
  sConfigOC.OCMode = TIM_OCMODE_PWM2;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

}

/**
  * @brief TIM5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM5_Init(void)
{

  /* USER CODE BEGIN TIM5_Init 0 */

  /* USER CODE END TIM5_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM5_Init 1 */

  /* USER CODE END TIM5_Init 1 */
  htim5.Instance = TIM5;
  htim5.Init.Prescaler = 0;
  htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim5.Init.Period = 4294967295;
  htim5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim5, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim5, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM5_Init 2 */

  /* USER CODE END TIM5_Init 2 */
  HAL_TIM_MspPostInit(&htim5);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 921600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief USB_OTG_HS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_HS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_HS_PCD_Init 0 */

  /* USER CODE END USB_OTG_HS_PCD_Init 0 */

  /* USER CODE BEGIN USB_OTG_HS_PCD_Init 1 */

  /* USER CODE END USB_OTG_HS_PCD_Init 1 */
  hpcd_USB_OTG_HS.Instance = USB_OTG_HS;
  hpcd_USB_OTG_HS.Init.dev_endpoints = 9;
  hpcd_USB_OTG_HS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_HS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.phy_itface = USB_OTG_EMBEDDED_PHY;
  hpcd_USB_OTG_HS.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.vbus_sensing_enable = ENABLE;
  hpcd_USB_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_HS_PCD_Init 2 */

  /* USER CODE END USB_OTG_HS_PCD_Init 2 */

}

/**
  * @brief USB_OTG_HS Initialization Function
  * @param None
  * @retval None
  */
void MX_USB_OTG_HS_HCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_HS_HCD_Init 0 */

  /* USER CODE END USB_OTG_HS_HCD_Init 0 */

  /* USER CODE BEGIN USB_OTG_HS_HCD_Init 1 */

  /* USER CODE END USB_OTG_HS_HCD_Init 1 */
  hhcd_USB_OTG_HS.Instance = USB_OTG_HS;
  hhcd_USB_OTG_HS.Init.Host_channels = 16;
  hhcd_USB_OTG_HS.Init.speed = HCD_SPEED_FULL;
  hhcd_USB_OTG_HS.Init.dma_enable = DISABLE;
  hhcd_USB_OTG_HS.Init.phy_itface = USB_OTG_EMBEDDED_PHY;
  hhcd_USB_OTG_HS.Init.Sof_enable = DISABLE;
  hhcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  if (HAL_HCD_Init(&hhcd_USB_OTG_HS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_HS_HCD_Init 2 */

  /* USER CODE END USB_OTG_HS_HCD_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, Detectn_Pin|LCD_DISP_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, ARD_D8_Pin|STMOD_17_Pin|STMOD_19_Pin|STMOD_18_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, LCD_BL_CTRL_Pin|ARD_D7_Pin|MEMS_LED_Pin|ARD_D4_Pin
                          |ARD_D2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, USER_LED2_Pin|USER_LED1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(STMOD_20_GPIO_Port, STMOD_20_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOH, LCD_RST_Pin|USB_FS_PWR_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : Detectn_Pin LCD_DISP_Pin */
  GPIO_InitStruct.Pin = Detectn_Pin|LCD_DISP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : SAI4_D2_Pin SAI4_CK2_Pin */
  GPIO_InitStruct.Pin = SAI4_D2_Pin|SAI4_CK2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF10_SAI4;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : ARD_D8_Pin STMOD_17_Pin STMOD_19_Pin STMOD_18_Pin */
  GPIO_InitStruct.Pin = ARD_D8_Pin|STMOD_17_Pin|STMOD_19_Pin|STMOD_18_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : USB_FS_OVCR_Pin CTP_INT_Pin */
  GPIO_InitStruct.Pin = USB_FS_OVCR_Pin|CTP_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pin : Blue_button_B2_used_for_wakeup_Pin */
  GPIO_InitStruct.Pin = Blue_button_B2_used_for_wakeup_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(Blue_button_B2_used_for_wakeup_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LCD_BL_CTRL_Pin ARD_D7_Pin MEMS_LED_Pin ARD_D4_Pin
                           ARD_D2_Pin */
  GPIO_InitStruct.Pin = LCD_BL_CTRL_Pin|ARD_D7_Pin|MEMS_LED_Pin|ARD_D4_Pin
                          |ARD_D2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pin : uSD_Detect_Pin */
  GPIO_InitStruct.Pin = uSD_Detect_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(uSD_Detect_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : USER_LED2_Pin USER_LED1_Pin */
  GPIO_InitStruct.Pin = USER_LED2_Pin|USER_LED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI5_MISO_Pin */
  GPIO_InitStruct.Pin = SPI5_MISO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI5;
  HAL_GPIO_Init(SPI5_MISO_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI5_MOSI_Pin */
  GPIO_InitStruct.Pin = SPI5_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI5;
  HAL_GPIO_Init(SPI5_MOSI_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : Audio_Int_Pin */
  GPIO_InitStruct.Pin = Audio_Int_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(Audio_Int_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : STMOD_20_Pin */
  GPIO_InitStruct.Pin = STMOD_20_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(STMOD_20_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : STMOD_11_INT_Pin */
  GPIO_InitStruct.Pin = STMOD_11_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(STMOD_11_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LCD_RST_Pin USB_FS_PWR_EN_Pin */
  GPIO_InitStruct.Pin = LCD_RST_Pin|USB_FS_PWR_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

  /*AnalogSwitch Config */
  HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PC2, SYSCFG_SWITCH_PC2_CLOSE);

  /*AnalogSwitch Config */
  HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PC3, SYSCFG_SWITCH_PC3_CLOSE);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* Bench signal only: PA5/DAC1_OUT2. No connection to gate control. */
static void DacSine_Start(void)
{
  DAC_ChannelConfTypeDef channel = {0};
  TIM_MasterConfigTypeDef master = {0};
  uint32_t timerClock = HAL_RCC_GetPCLK1Freq();
  uint32_t ticks;
  if ((RCC->CFGR & RCC_CFGR_TIMPRE) != 0U) Error_Handler();
  if ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1_Msk) != RCC_D2CFGR_D2PPRE1_DIV1)
    timerClock *= 2U;
  ticks = (timerClock + DAC_SINE_HZ * DAC_SINE_SAMPLES / 2U) /
          (DAC_SINE_HZ * DAC_SINE_SAMPLES);
  if ((ticks < 2U) || (ticks > 65536U)) Error_Handler();
  if (((uintptr_t)dacSineTable < 0x24000000UL) ||
      ((uintptr_t)dacSineTable + sizeof(dacSineTable) > 0x24050000UL))
    Error_Handler();

  __HAL_RCC_TIM6_CLK_ENABLE();
  htim6Dac.Instance = TIM6;
  htim6Dac.Init.Prescaler = 0U;
  htim6Dac.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6Dac.Init.Period = ticks - 1U;
  htim6Dac.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6Dac) != HAL_OK) Error_Handler();
  master.MasterOutputTrigger = TIM_TRGO_UPDATE;
  master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6Dac, &master) != HAL_OK) Error_Handler();
  __HAL_TIM_CLEAR_FLAG(&htim6Dac, TIM_FLAG_UPDATE);
  g_dacSineSampleRateHz = timerClock / ticks;

  __HAL_RCC_DMA1_CLK_ENABLE();
  hdmaDac2.Instance = DMA1_Stream1;
  hdmaDac2.Init.Request = DMA_REQUEST_DAC1_CH2;
  hdmaDac2.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdmaDac2.Init.PeriphInc = DMA_PINC_DISABLE;
  hdmaDac2.Init.MemInc = DMA_MINC_ENABLE;
  hdmaDac2.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  hdmaDac2.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  hdmaDac2.Init.Mode = DMA_CIRCULAR;
  hdmaDac2.Init.Priority = DMA_PRIORITY_LOW;
  hdmaDac2.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdmaDac2) != HAL_OK) Error_Handler();
  __HAL_LINKDMA(&hdac1, DMA_Handle2, hdmaDac2);
  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
  HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

  /* Runtime override of generated DAC_TRIGGER_NONE, inside USER CODE. */
  channel.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  channel.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
  channel.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  channel.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
  channel.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac1, &channel, DAC_CHANNEL_2) != HAL_OK) Error_Handler();
  if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 2048U) != HAL_OK) Error_Handler();
  SCB_CleanDCache_by_Addr(dacSineTable, sizeof(dacSineTable));
  if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2, dacSineTable,
                        DAC_SINE_SAMPLES, DAC_ALIGN_12B_R) != HAL_OK) Error_Handler();
  /* Static circular waveform: only errors need interrupts, not each period. */
  __HAL_DMA_DISABLE_IT(&hdmaDac2, DMA_IT_HT | DMA_IT_TC);
  if (HAL_TIM_Base_Start(&htim6Dac) != HAL_OK) Error_Handler();
}

void DMA1_Stream1_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdmaDac2);
}

void TIM6_DAC_IRQHandler(void)
{
  /* TIM6 update IRQ is disabled; this vector services DAC underrun only. */
  HAL_DAC_IRQHandler(&hdac1);
}

void HAL_DACEx_ErrorCallbackCh2(DAC_HandleTypeDef *hdac)
{
  if (hdac->Instance == DAC1)
  {
    g_dacSineError = HAL_DAC_GetError(hdac);
    g_dacSineDmaError = HAL_DMA_GetError(&hdmaDac2);
    CLEAR_BIT(TIM6->CR1, TIM_CR1_CEN);
  }
}

void HAL_DACEx_DMAUnderrunCallbackCh2(DAC_HandleTypeDef *hdac)
{
  HAL_DACEx_ErrorCallbackCh2(hdac);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    CurrentSense_ProcessBlock(CURRENT_DMA_HALF, CURRENT_DMA_HALF);
  }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    CurrentSense_ProcessBlock(0U, CURRENT_DMA_HALF);
  }
}

static void PwmOr_Stop(void)
{
  GPIO_InitTypeDef gpio = {0};
  if (g_pwmStopDetail.valid == 0U)
  {
    g_pwmStopDetail.startupStage = g_pwmStartupStage;
    g_pwmStopDetail.fault = g_pwmOrFault;
    g_pwmStopDetail.acquisitionFault = g_currentSenseFault;
    g_pwmStopDetail.detailReason = g_currentFaultDetail.reason;
    g_pwmStopDetail.cr1 = TIM4->CR1;
    g_pwmStopDetail.ccer = TIM4->CCER;
    g_pwmStopDetail.ccmr1 = TIM4->CCMR1;
    g_pwmStopDetail.ccmr2 = TIM4->CCMR2;
    g_pwmStopDetail.cnt = TIM4->CNT;
    g_pwmStopDetail.arr = TIM4->ARR;
    g_pwmStopDetail.ccr2 = TIM4->CCR2;
    g_pwmStopDetail.ccr4 = TIM4->CCR4;
    g_pwmStopDetail.tim3Cr1 = TIM3->CR1;
    g_pwmStopDetail.tim3Cnt = TIM3->CNT;
    g_pwmStopDetail.ndtr = DMA1_Stream0->NDTR;
    g_pwmStopDetail.adc1Error = hadc1.ErrorCode;
    g_pwmStopDetail.adc2Error = hadc2.ErrorCode;
    g_pwmStopDetail.inputMin = g_primaryCurrentMin;
    g_pwmStopDetail.inputMax = g_primaryCurrentMax;
    g_pwmStopDetail.highSamples = pwmOrHigh;
    g_pwmStopDetail.totalSamples = pwmOrSamples;
    __DMB();
    g_pwmStopDetail.valid = 1U;
  }
  WaveTest_Stop(4U);
  CLEAR_BIT(TIM4->CCER, TIM_CCER_CC2E | TIM_CCER_CC4E);
  CLEAR_BIT(TIM4->CR1, TIM_CR1_CEN);
  CLEAR_BIT(TIM3->CR1, TIM_CR1_CEN);
  /* Disabled timer channels alone do not guarantee pin LOW. */
  GPIOB->BSRR = (uint32_t)GPIO_PIN_7 << 16U;
  GPIOD->BSRR = (uint32_t)GPIO_PIN_15 << 16U;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Pin = GPIO_PIN_7;
  HAL_GPIO_Init(GPIOB, &gpio);
  gpio.Pin = GPIO_PIN_15;
  HAL_GPIO_Init(GPIOD, &gpio);
}

static void PwmOr_Process(uint32_t high, uint32_t count)
{
  pwmOrHigh += high;
  pwmOrSamples += count;
  if (pwmOrSamples < 16U * CURRENT_DMA_SAMPLES) return;
  g_pwmOrMeasured = (float)pwmOrHigh / (float)pwmOrSamples;
  g_pwmOrInputInvalid = (pwmOrHigh == 0U) ? 1U :
                       ((pwmOrHigh == pwmOrSamples) ? 2U : 0U);
  if (g_pwmOrInputInvalid != 0U) ++g_pwmOrInvalidWindows;
#if PWM_OR_BENCH_ALLOW_ZERO
  /* User-requested zero-input response test. Keep LOW visible diagnostically,
     but let PI approach its existing upper bound instead of holding PWM. */
  if (pwmOrHigh == pwmOrSamples)
#else
  if ((pwmOrHigh == 0U) || (pwmOrHigh == pwmOrSamples))
#endif
  {
#if PWM_OR_BENCH_HOLD_ON_INVALID
    /* Diagnostic fallback only: provide a known waveform for probing PA0_C.
       Never integrate missing feedback up to maximum duty. */
    g_pwmOrCommand = 0.25f;
    pwmOrIntegral = 0.25f;
    pwmOrPrevious = g_pwmOrMeasured;
    pwmOrHigh = pwmOrSamples = 0U;
#else
    g_pwmOrFault = 2U;
    PwmOr_Stop();
#endif
    return;
  }
  float dt = (float)pwmOrSamples / (float)g_currentSampleRateHz;
  float error = PWM_OR_TARGET - g_pwmOrMeasured;
  float candidate = pwmOrIntegral + PWM_OR_KI * dt * error;
  float output = PWM_OR_KP * error + candidate
               - PWM_OR_KD * (g_pwmOrMeasured - pwmOrPrevious) / dt;
  /* Conditional integration avoids windup at the occupancy limits. */
  if (!((output > 0.90f && error > 0.0f) ||
        (output < 0.10f && error < 0.0f))) pwmOrIntegral = candidate;
  if (output > 0.90f) output = 0.90f;
  if (output < 0.10f) output = 0.10f;
  if (output > g_pwmOrCommand + 0.025f) output = g_pwmOrCommand + 0.025f;
  if (output < g_pwmOrCommand - 0.025f) output = g_pwmOrCommand - 0.025f;
  g_pwmOrCommand = output;
  pwmOrPrevious = g_pwmOrMeasured;
  ++g_pwmOrUpdates;
  pwmOrHigh = pwmOrSamples = 0U;
}

static void CurrentSense_RecordFault(uint32_t reason, uint32_t offset,
                                     uint32_t entry, uint32_t remaining,
                                     uint32_t start)
{
  if (g_currentSenseFault != 0U) return;
  g_currentFaultDetail.reason = reason;
  g_currentFaultDetail.offset = offset;
  g_currentFaultDetail.expectedOffset = currentExpectedOffset;
  g_currentFaultDetail.ndtrEntry = entry;
  g_currentFaultDetail.ndtrExit = remaining;
  g_currentFaultDetail.elapsedCycles = DWT->CYCCNT - start;
  g_currentFaultDetail.budgetCycles = g_currentHalfBudgetCycles;
  g_currentFaultDetail.warmupBlocks = g_currentWarmupBlocks;
  g_currentFaultDetail.publishedBlocks = g_currentDmaBlocks;
  g_currentFaultDetail.dmaFlags = DMA1->LISR;
  g_currentFaultDetail.pwmCount = TIM4->CNT;
  __DMB();
  g_currentSenseFault = 1U;
  g_pwmOrFault = 1U;
  PwmOr_Stop();
  ++g_currentDeadlineMisses;
  CLEAR_BIT(TIM3->CR1, TIM_CR1_CEN);
  ARD_D2_GPIO_Port->BSRR = (uint32_t)ARD_D2_Pin << 16U;
}

static void CurrentSense_ProcessBlock(uint32_t offset, uint32_t count)
{
  /* Priority 4: no RTOS APIs, blocking calls, allocation or display work here. */
  uint32_t primarySum = 0U, secondarySum = 0U;
  uint16_t primaryMin = UINT16_MAX, secondaryMin = UINT16_MAX;
  uint16_t primaryMax = 0U, secondaryMax = 0U;
  uint32_t i;
  uint32_t high = 0U;

  uint32_t start = DWT->CYCCNT;
  uint32_t remaining = __HAL_DMA_GET_COUNTER(&hdma_adc1);
  uint32_t entryRemaining = remaining;
  uint32_t half = offset / CURRENT_DMA_HALF;
  if ((g_currentSenseFault != 0U) || (g_pwmOrFault != 0U)) return;
  ARD_D2_GPIO_Port->BSRR = ARD_D2_Pin;
  g_currentPwmCountAtCallback = TIM4->CNT;
  /* Never process the half currently being written. Catch delayed/coalesced IRQs. */
  if ((offset != currentExpectedOffset) ||
      ((offset == 0U) && ((remaining == 0U) || (remaining > CURRENT_DMA_HALF))) ||
      ((offset != 0U) && (remaining <= CURRENT_DMA_HALF)))
  {
    CurrentSense_RecordFault((offset != currentExpectedOffset) ? 1U : 2U,
                             offset, entryRemaining, remaining, start);
    return;
  }

  SCB_InvalidateDCache_by_Addr((uint32_t *)&g_currentDmaBuffer[offset],
                              (int32_t)(count * sizeof(g_currentDmaBuffer[0])));
  for (i = offset; i < (offset + count); ++i)
  {
    uint16_t primary = (uint16_t)(g_currentDmaBuffer[i] & 0xffffU);
    uint16_t secondary = (uint16_t)(g_currentDmaBuffer[i] >> 16);
    primarySum += primary;
    if (primary >= PWM_OR_THRESHOLD) ++high;
    secondarySum += secondary;
    if (primary < primaryMin) primaryMin = primary;
    if (primary > primaryMax) primaryMax = primary;
    if (secondary < secondaryMin) secondaryMin = secondary;
    if (secondary > secondaryMax) secondaryMax = secondary;
  }
  remaining = __HAL_DMA_GET_COUNTER(&hdma_adc1);
  if (((offset == 0U) && ((remaining == 0U) || (remaining > CURRENT_DMA_HALF))) ||
      ((offset != 0U) && (remaining <= CURRENT_DMA_HALF)) ||
      ((uint32_t)(DWT->CYCCNT - start) >= g_currentHalfBudgetCycles))
  {
    CurrentSense_RecordFault(((uint32_t)(DWT->CYCCNT - start) >=
                              g_currentHalfBudgetCycles) ? 4U : 3U,
                             offset, entryRemaining, remaining, start);
    return;
  }
  currentExpectedOffset = (offset == 0U) ? CURRENT_DMA_HALF : 0U;
  if (g_currentWarmupBlocks != 0U)
  {
    --g_currentWarmupBlocks;
    ARD_D2_GPIO_Port->BSRR = (uint32_t)ARD_D2_Pin << 16U;
    return;
  }
  g_primaryCurrentAdc = (uint16_t)(primarySum / count);
  g_secondaryCurrentAdc = (uint16_t)(secondarySum / count);
  g_secondaryCurrentFiltered = CurrentFeedbackFilter_Update(
      &secondaryFeedbackFilter, g_secondaryCurrentAdc);
  g_primaryCurrentMin = primaryMin;
  g_primaryCurrentMax = primaryMax;
  g_secondaryCurrentMin = secondaryMin;
  g_secondaryCurrentMax = secondaryMax;
  g_adc1Value = (uint16_t)g_currentDmaBuffer[offset + count - 1U];
  ++g_currentHalf[half].sequence; /* Odd while writing, even after publish. */
  __DMB();
  g_currentHalf[half].primaryMean = g_primaryCurrentAdc;
  g_currentHalf[half].secondaryMean = g_secondaryCurrentAdc;
  g_currentHalf[half].primaryMin = primaryMin;
  g_currentHalf[half].primaryMax = primaryMax;
  g_currentHalf[half].secondaryMin = secondaryMin;
  g_currentHalf[half].secondaryMax = secondaryMax;
  __DMB();
  ++g_currentHalf[half].sequence;
  ++g_currentDmaBlocks;
#if PWM_WAVE_TEST_ENABLE
  (void)high;
  WaveTest_Dma();
#else
  PwmOr_Process(high, count);
#endif
  if (g_pwmOrFault == 0U)
  {
    uint32_t direction = TIM4->CR1 & TIM_CR1_DIR;
    uint32_t position = TIM4->CNT;
    if (((half == 0U) && ((direction == 0U) || (position < 2048U))) ||
        ((half != 0U) && ((direction != 0U) || (position > TIM4->ARR - 2048U))))
    {
      CurrentSense_RecordFault(6U, offset, entryRemaining,
                              __HAL_DMA_GET_COUNTER(&hdma_adc1), start);
      return;
    }
    uint32_t width = (uint32_t)(g_pwmOrCommand *
                               (float)(PWM_PERIOD_TICKS / 4U));
    /* Stage each compare just after its pulse center; it loads at the
       opposite extremum, while that output is LOW. This avoids changing
       width halfway through a pulse. A/B adopt a new command in successive
       half-periods. Never force UG while sampling. */
    if (half == 0U) TIM4->CCR4 = width ? TIM4->ARR - width : TIM4->ARR + 1U;
    else TIM4->CCR2 = width;
  }
  g_currentProcessingCycles = DWT->CYCCNT - start;
  if (g_currentProcessingCycles > g_currentMaxProcessingCycles)
    g_currentMaxProcessingCycles = g_currentProcessingCycles;
  if (g_currentProcessingCycles >= g_currentHalfBudgetCycles)
  {
    CurrentSense_RecordFault(5U, offset, entryRemaining,
                             __HAL_DMA_GET_COUNTER(&hdma_adc1), start);
  }
  ARD_D2_GPIO_Port->BSRR = (uint32_t)ARD_D2_Pin << 16U;
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    g_currentAdcError = HAL_ADC_GetError(hadc);
    g_currentDmaError = HAL_DMA_GetError(&hdma_adc1);
    g_currentSenseFault = 2U;
    g_pwmOrFault = 1U;
    PwmOr_Stop();
    CLEAR_BIT(TIM3->CR1, TIM_CR1_CEN);
    ARD_D2_GPIO_Port->BSRR = (uint32_t)ARD_D2_Pin << 16U;
  }
  /* Stops bench pins only; no verified power-stage interlock exists. */
}

static void CurrentSense_Init(void)
{
  TIM_MasterConfigTypeDef master = {0};
  TIM_SlaveConfigTypeDef slave = {0};
  TIM_OC_InitTypeDef oc = {0};
  uint32_t timerClock = HAL_RCC_GetPCLK1Freq();
  uint32_t pwmTicks = 2U * htim4.Init.Period;
  uint32_t sampleTicks = pwmTicks / CURRENT_DMA_SAMPLES;

  /* Bench debugging: halt PWM and its ADC trigger together on CPU PAUSE.
     Otherwise DMA keeps wrapping while callbacks cannot execute, and RUN
     correctly trips the stale-buffer checks. Set both freeze bits before
     either timer starts. This holds pin levels; it is NOT a gate shutdown. */
  SET_BIT(DBGMCU->APB1LFZ1,
          DBGMCU_APB1LFZ1_DBG_TIM3 | DBGMCU_APB1LFZ1_DBG_TIM4);

  if ((RCC->CFGR & RCC_CFGR_TIMPRE) != 0U ||
      (htim4.Init.Prescaler != 0U) || (pwmTicks != 65536U) ||
      (htim4.Init.CounterMode != TIM_COUNTERMODE_CENTERALIGNED1) ||
      ((TIM4->CR1 & TIM_CR1_CEN) != 0U))
    Error_Handler();

  if ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1_Msk) != RCC_D2CFGR_D2PPRE1_DIV1)
  {
    timerClock *= 2U;
  }
  g_currentSampleRateHz = timerClock / sampleTicks;
  CurrentFeedbackFilter_Init(&secondaryFeedbackFilter,
      (float)CURRENT_DMA_HALF / (float)g_currentSampleRateHz, 0.001f);
  g_currentHalfBudgetCycles = (uint32_t)(((uint64_t)SystemCoreClock *
                                           (pwmTicks / 2U)) / timerClock);
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  __HAL_RCC_TIM3_CLK_ENABLE();
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0U;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = sampleTicks - 1U;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) Error_Handler();
  /* OC4REF rises halfway through each 1024-tick sample interval.
     CC4 output is NOT enabled and no TIM3 GPIO is configured. */
  oc.OCMode = TIM_OCMODE_PWM2;
  oc.Pulse = sampleTicks / 2U;
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_4) != HAL_OK) Error_Handler();
  master.MasterOutputTrigger = TIM_TRGO_OC4REF;
  master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &master) != HAL_OK) Error_Handler();
  slave.SlaveMode = TIM_SLAVEMODE_TRIGGER;
  slave.InputTrigger = TIM_TS_ITR3; /* TIM4 TRGO -> TIM3 ITR3; bench verify. */
  if (HAL_TIM_SlaveConfigSynchro(&htim3, &slave) != HAL_OK) Error_Handler();
  /* CEN remains zero until TIM4 starts. Identical APB1 timer clocks and an
     exact 64:1 period ratio prevent phase drift; no periodic reset extra sample. */
  master.MasterOutputTrigger = TIM_TRGO_ENABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &master) != HAL_OK) Error_Handler();
  __HAL_TIM_SET_COUNTER(&htim4, 0U);
  __HAL_TIM_SET_COUNTER(&htim3, 0U);
  __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE | TIM_FLAG_CC4 | TIM_FLAG_TRIGGER);
  __HAL_TIM_DISABLE_IT(&htim4, TIM_IT_UPDATE | TIM_IT_CC3);

  /* DMA1 cannot access DTCM (the RAM-debug linker puts normal .bss there). */
  if (((uintptr_t)g_currentDmaBuffer < 0x24000000UL) ||
      (((uintptr_t)g_currentDmaBuffer + sizeof(g_currentDmaBuffer)) > 0x24050000UL))
    Error_Handler();
  /* Restore runtime acquisition settings after generated MX_ADC init.
     Requested OR input: PA0_C / Arduino A2 / ADC1_INP0.
     Keep the PA0 analogue switch OPEN to isolate PA0 / Arduino D3.
     ADC2 uses PA1_C / Arduino A3 / ADC2_INP1 for conditioned Rogowski input.
     PA1 analogue switch remains OPEN to isolate Ethernet REF_CLK on PA1. */
  ADC_ChannelConfTypeDef channel = {0};
  ADC_MultiModeTypeDef dual = {0};
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T3_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
  hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();
  hadc2.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;
  hadc2.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  if (HAL_ADC_Init(&hadc2) != HAL_OK) Error_Handler();
  channel.Channel = ADC_CHANNEL_0;
  channel.Rank = ADC_REGULAR_RANK_1;
  channel.SamplingTime = ADC_SAMPLETIME_32CYCLES_5;
  channel.SingleDiff = ADC_SINGLE_ENDED;
  channel.OffsetNumber = ADC_OFFSET_NONE;
  if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) Error_Handler();
  channel.Channel = ADC_CHANNEL_1;
  if (HAL_ADC_ConfigChannel(&hadc2, &channel) != HAL_OK) Error_Handler();
  dual.Mode = ADC_DUALMODE_REGSIMULT;
  dual.DualModeData = ADC_DUALMODEDATAFORMAT_32_10_BITS;
  dual.TwoSamplingDelay = ADC_TWOSAMPLINGDELAY_1CYCLE;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &dual) != HAL_OK) Error_Handler();
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED) != HAL_OK)
    Error_Handler();
  if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED) != HAL_OK)
    Error_Handler();

  SCB_CleanInvalidateDCache_by_Addr((uint32_t *)g_currentDmaBuffer,
                                   sizeof(g_currentDmaBuffer));
  if (HAL_ADCEx_MultiModeStart_DMA(&hadc1, g_currentDmaBuffer,
                                   CURRENT_DMA_SAMPLES) != HAL_OK) Error_Handler();
  /* Do not software-start TIM3: the later TIM4 PWM start is the only trigger. */
}

static void WaveTest_OutputsOff(void)
{
  CLEAR_BIT(TIM4->CCER, TIM_CCER_CC2E | TIM_CCER_CC4E);
  GPIOB->BSRR = (uint32_t)GPIO_PIN_7 << 16U;
  GPIOD->BSRR = (uint32_t)GPIO_PIN_15 << 16U;
  MODIFY_REG(GPIOB->MODER, 3UL << 14U, 1UL << 14U);
  MODIFY_REG(GPIOD->MODER, 3UL << 30U, 1UL << 30U);
  TIM4->CCR2 = 0U;
  TIM4->CCR4 = TIM4->ARR + 1U;
  g_pwmOrCommand = 0.0f;
}

static void WaveTest_Log(float target)
{
  if (waveStatus.count >= 2048U) return;
  WaveTestSample *p = &waveSamples[waveStatus.count];
  p->time_ms = waveStatus.elapsed_ms;
  p->target_milli = (uint32_t)(target * 1000.0f);
  p->duty_permille = waveStatus.duty_permille;
  p->adc_mean = g_primaryCurrentAdc;
  p->adc_min = g_primaryCurrentMin;
  p->adc_max = g_primaryCurrentMax;
  p->primary_filtered_micro = (uint32_t)(g_wavePidFiltered * 1000000.0f + 0.5f);
  p->secondary_adc = g_secondaryCurrentAdc;
  p->secondary_filtered_micro = (uint32_t)(g_secondaryCurrentFiltered * 1000000.0f + 0.5f);
  __DMB();
  ++waveStatus.count;
}

void WaveTest_Stop(uint32_t reason)
{
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  if (PWM_WAVE_TEST_ENABLE)
  {
    WaveTest_OutputsOff();
    if (waveStatus.active)
    {
      if (waveStatus.elapsed_ms > waveStatus.duration_ms)
        waveStatus.elapsed_ms = waveStatus.duration_ms;
      waveStatus.duty_permille = 0U;
      WaveTest_Log(0.0f);
      waveStatus.reason = reason;
      waveStatus.active = 0U;
    }
  }
  __set_PRIMASK(mask);
}

int WaveTest_Start(const WeldSettings *s, uint32_t duty_percent)
{
  if (!PWM_WAVE_TEST_ENABLE || !s || duty_percent < 1U || duty_percent > 45U)
    return 0;
  float peak = 0.0f;
  if (s->squeeze_ms > 999U || s->cool_ms[0] > 999U || s->cool_ms[1] > 999U) return 0;
  for (unsigned i = 0; i < 3; ++i)
  {
    float v = s->stage_current_a[i];
    if (!(v >= 0.0f && v <= 65535.0f) || s->stage_up_ms[i] > 500U ||
        s->stage_time_ms[i] > 999U || s->stage_down_ms[i] > 500U ||
        ((v == 0.0f) != (s->stage_time_ms[i] == 0U)) ||
        (v == 0.0f && (s->stage_up_ms[i] || s->stage_down_ms[i]))) return 0;
    if (v > peak) peak = v;
  }
  uint32_t duration = WaveTest_Duration(s);
  if (peak == 0.0f || duration == 0U || duration > 9000U) return 0;
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  if (waveStatus.active || g_currentSenseFault || g_pwmOrFault ||
      g_currentDmaBlocks == 0U || HAL_GetTick() - waveDmaTick > 3U)
  { __set_PRIMASK(mask); return 0; }
  WaveTest_OutputsOff();
  waveSettings = *s; /* Immutable for this run. No RTOS call from acquisition IRQ. */
  wavePeak = peak;
  waveIntegral = waveOutput = 0.0f;
  wavePrevious = (float)g_primaryCurrentAdc / 65535.0f;
  g_wavePidFiltered = wavePrevious;
  float filterDt = (float)CURRENT_DMA_HALF / (float)g_currentSampleRateHz;
  waveFilterAlpha = filterDt / (waveFilterTauSeconds + filterDt);
  waveLimit = duty_percent;
  waveStatus.id++;
  waveStatus.reason = 0U;
  waveStatus.elapsed_ms = waveStatus.count = waveStatus.duty_permille = 0U;
  waveStatus.duration_ms = duration;
  waveInterval = (duration + 2045U) / 2046U;
  waveNextLog = 0U;
  waveRunPairs = 0U;
  waveArmHalves = 3U; /* Let both zero-compare preloads settle before AF enable. */
  waveLeaseTick = HAL_GetTick();
  waveStatus.active = 1U;
  __set_PRIMASK(mask);
  return 1;
}

void WaveTest_KeepAlive(void) { waveLeaseTick = HAL_GetTick(); }
void WaveTest_GetStatus(WaveTestStatus *s)
{
  uint32_t mask = __get_PRIMASK(); __disable_irq();
  *s = waveStatus;
  s->adc_mean = g_primaryCurrentAdc;
  s->primary_filtered_micro = (uint32_t)(g_wavePidFiltered * 1000000.0f + 0.5f);
  s->secondary_adc = g_secondaryCurrentAdc;
  s->secondary_filtered_micro = (uint32_t)(g_secondaryCurrentFiltered * 1000000.0f + 0.5f);
  __set_PRIMASK(mask);
}
int WaveTest_GetSample(uint32_t id, uint32_t index, WaveTestSample *s)
{
  uint32_t mask = __get_PRIMASK(); __disable_irq();
  int valid = !waveStatus.active && id == waveStatus.id && index < waveStatus.count;
  if (valid) *s = waveSamples[index];
  __set_PRIMASK(mask);
  return valid;
}
void WaveTest_GetPid(WavePidConfig *config) { if (config) *config = wavePid; }
int WaveTest_SetPid(const WavePidConfig *config)
{
  if (!config || !isfinite(config->kp) || !isfinite(config->ki) || !isfinite(config->kd) ||
      !isfinite(config->target) || config->kp < 0.0f || config->kp > 100.0f ||
      config->ki < 0.0f || config->ki > 1000.0f || config->kd < 0.0f || config->kd > 100.0f ||
      config->target < 0.0f || config->target > 1.0f) return 0;
  if (waveStatus.active) return 0;
  wavePid = *config; return 1;
}

/* Normalized ADC bench feedback, not calibrated amperes. Output is per-phase
 * duty fraction. Derivative acts on measurement; integration stops at limits. */
static float WaveTest_Control(float target)
{
  if (!wavePid.enabled) return (float)waveLimit * target / wavePeak;
  float raw = (float)g_primaryCurrentAdc / 65535.0f;
  g_wavePidFiltered += waveFilterAlpha * (raw - g_wavePidFiltered);
  float measured = g_wavePidFiltered;
  float dt = (float)CURRENT_DMA_HALF / (float)g_currentSampleRateHz;
  if (target <= 0.0f) {
    waveIntegral = waveOutput = 0.0f; wavePrevious = measured; return 0.0f;
  }
  float error = target / 65535.0f - measured;
  float limit = (float)waveLimit * 0.01f;
  float integral = waveIntegral + wavePid.ki * error * dt;
  float pd = wavePid.kp * error - wavePid.kd * (measured-wavePrevious)/dt;
  float requested = pd + integral;
  if ((requested >= 0.0f && requested <= limit) ||
      (requested > limit && error < 0.0f) || (requested < 0.0f && error > 0.0f))
    waveIntegral = integral;
  float output = pd + waveIntegral;
  if (output < 0.0f) output = 0.0f;
  if (output > limit) output = limit;
  wavePrevious = measured; waveOutput = output;
  return output * 100.0f;
}

static void WaveTest_Dma(void)
{
  (void)PwmOr_Process; /* Legacy OR controller is deliberately not executed here. */
  waveDmaTick = HAL_GetTick();
  if (!waveStatus.active) return;
  if (waveArmHalves)
  {
    if (--waveArmHalves != 0U) return;
    /* AF2 mappings established by TIM4 MSP. Both active compares are zero-OFF. */
    MODIFY_REG(GPIOB->MODER, 3UL << 14U, 2UL << 14U);
    MODIFY_REG(GPIOD->MODER, 3UL << 30U, 2UL << 30U);
    SET_BIT(TIM4->CCER, TIM_CCER_CC2E | TIM_CCER_CC4E);
    float target = WaveTest_Target(&waveSettings, 0U);
    float duty = WaveTest_Control(target);
    waveStatus.duty_permille = (uint32_t)(duty * 10.0f + 0.5f);
    g_pwmOrCommand = duty * 0.02f;
    WaveTest_Log(target);
    waveNextLog = waveInterval;
    return;
  }
  /* Sequence duration follows acquired sample pairs at priority 4, rather
     than the GUI/TCP task or the lower-priority HAL millisecond tick. */
  waveRunPairs += CURRENT_DMA_HALF;
  waveStatus.elapsed_ms = (uint32_t)(((uint64_t)waveRunPairs * 1000U) /
                                    g_currentSampleRateHz);
  if (waveStatus.elapsed_ms >= waveStatus.duration_ms) { WaveTest_Stop(1U); return; }
  float target = WaveTest_Target(&waveSettings, waveStatus.elapsed_ms);
  float duty = WaveTest_Control(target);
  waveStatus.duty_permille = (uint32_t)(duty * 10.0f + 0.5f);
  g_pwmOrCommand = duty * 0.02f;
  if (waveStatus.elapsed_ms >= waveNextLog)
  { WaveTest_Log(target); waveNextLog = waveStatus.elapsed_ms + waveInterval; }
}

static void WaveTest_Tick(void)
{
  if (!PWM_WAVE_TEST_ENABLE) return;
  uint32_t mask = __get_PRIMASK(); __disable_irq();
  if (waveStatus.active)
  {
    uint32_t now = HAL_GetTick();
    if (now - waveDmaTick > 3U || g_currentSenseFault || g_pwmOrFault)
      WaveTest_Stop(4U);
    else if (now - waveLeaseTick > 1000U) WaveTest_Stop(3U);
  }
  __set_PRIMASK(mask);
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
__weak void StartDefaultTask(void *argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x08000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_1MB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x70000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512MB;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32KB;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM23 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */


  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM23)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */
  if (htim->Instance == TIM23) WaveTest_Tick();

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  if (g_pwmErrorCaller == 0U)
    g_pwmErrorCaller = (uintptr_t)__builtin_return_address(0);
  PwmOr_Stop();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
