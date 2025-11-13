/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32F103 Oscilloscope - FIXED VERSION
  *
  * FIXES:
  * 1. SPI Circular DMA mode - no restart needed
  * 2. Double buffering - prevent data race
  * 3. Synchronization between ADC and SPI
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"
#include <string.h>

#define BUFFER_SIZE          256
#define TX_BYTES             (BUFFER_SIZE * 2 + 4)
#define NUM_BUFFERS          2

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;
TIM_HandleTypeDef htim1;

// Double buffering
uint16_t adc_buffer[NUM_BUFFERS][BUFFER_SIZE] __attribute__((aligned(4)));
uint8_t  tx_buffer[NUM_BUFFERS][TX_BYTES] __attribute__((aligned(4)));

// Buffer management
volatile uint8_t adc_write_idx = 0;  // ADC writes to this buffer
volatile uint8_t spi_read_idx = 0;   // SPI reads from this buffer
volatile uint8_t buffer_ready = 0;   // New buffer ready flag

volatile uint32_t adc_count = 0;
volatile uint32_t spi_count = 0;
volatile uint32_t frame_counter = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM1_Init(void);

void pack_u16_to_bytes(uint16_t *src, uint8_t *dst, uint16_t count, uint16_t frame_num)
{
  *dst++ = 0xAA;
  *dst++ = 0x55;
  *dst++ = (frame_num >> 8) & 0xFF;
  *dst++ = frame_num & 0xFF;

  for (uint16_t i = 0; i < count; i++) {
    uint16_t val = src[i];
    *dst++ = (val >> 8) & 0xFF;
    *dst++ = val & 0xFF;
  }
}

void start_adc_capture(void)
{
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buffer[adc_write_idx], BUFFER_SIZE);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
  HAL_ADC_Stop_DMA(&hadc1);

  // Pack data with current frame counter
  pack_u16_to_bytes(adc_buffer[adc_write_idx],
                    tx_buffer[adc_write_idx],
                    BUFFER_SIZE,
                    frame_counter++);

  adc_count++;

  // Mark buffer as ready
  buffer_ready = 1;

  // Switch to next buffer
  adc_write_idx = (adc_write_idx + 1) % NUM_BUFFERS;
}

// SPI callback - NOT used for restart, just for stats
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
  spi_count++;
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  // On error, just restart
  __HAL_SPI_DISABLE(&hspi1);
  __HAL_SPI_ENABLE(&hspi1);
}

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_Base_Start(&htim1);
  __HAL_TIM_MOE_ENABLE(&htim1);

  HAL_ADCEx_Calibration_Start(&hadc1);

  // LED startup blink
  for (int i = 0; i < 3; i++) {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    HAL_Delay(200);
  }

  // Initialize buffers with default data (midpoint)
  for (int buf = 0; buf < NUM_BUFFERS; buf++) {
    for (int i = 0; i < BUFFER_SIZE; i++) {
      adc_buffer[buf][i] = 2048;
    }
    pack_u16_to_bytes(adc_buffer[buf], tx_buffer[buf], BUFFER_SIZE, 0);
  }

  // Start first ADC capture
  start_adc_capture();

  uint32_t last_heartbeat = 0;
  uint8_t spi_active = 0;

  while (1)
  {
    // Heartbeat LED
    if (HAL_GetTick() - last_heartbeat > 500) {
      last_heartbeat = HAL_GetTick();
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }

    // Check if new ADC data ready
    if (buffer_ready) {
      buffer_ready = 0;

      // Update SPI buffer pointer
      spi_read_idx = (adc_write_idx == 0) ? (NUM_BUFFERS - 1) : (adc_write_idx - 1);

      // If SPI not active, start it
      if (!spi_active) {
        // Use POLLING mode for reliability
        // Master (Pi4) will clock the data out
        spi_active = 1;
      }

      // Start next ADC capture immediately
      start_adc_capture();
    }

    // Handle SPI transfer (polling mode for slave is more reliable)
    // In slave mode, we just need to have data ready in DR
    // The master will clock it out when it reads

    // Check if SPI is ready to send
    if (hspi1.State == HAL_SPI_STATE_READY) {
      // Setup DMA transfer for current buffer
      // Use TX only (we don't care about RX data)
      HAL_SPI_Transmit_DMA(&hspi1, tx_buffer[spi_read_idx], TX_BYTES);
    }
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
}

static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T1_CC1;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  HAL_ADC_Init(&hadc1);

  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_7CYCLES_5;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_SLAVE;
  hspi1.Init.Direction = SPI_DIRECTION_1LINE;  // TX only
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_HARD_INPUT;  // Use hardware NSS
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  HAL_SPI_Init(&hspi1);
}

static void MX_TIM1_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 63;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim1);

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig);
  HAL_TIM_PWM_Init(&htim1);

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig);

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 500;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);

  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig);

  HAL_TIM_MspPostInit(&htim1);
}

static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
