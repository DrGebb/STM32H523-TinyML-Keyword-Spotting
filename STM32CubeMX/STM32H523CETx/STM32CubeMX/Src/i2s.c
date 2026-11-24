/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2s.c
  * @brief   This file provides code for the configuration
  *          of the I2S instances.
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
#include "i2s.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* I2S1 init function */
void MX_I2S1_Init(void)
{

  /* USER CODE BEGIN I2S1_Init 0 */

  /* USER CODE END I2S1_Init 0 */

  LL_I2S_InitTypeDef I2S_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_RCC_SetSPIClockSource(LL_RCC_SPI1_CLKSOURCE_PLL2P);

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);

  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
  /**I2S1 GPIO Configuration
  PA4   ------> I2S1_WS
  PA5   ------> I2S1_CK
  PA6   ------> I2S1_SDI
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_4|LL_GPIO_PIN_5|LL_GPIO_PIN_6;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_5;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN I2S1_Init 1 */

  /* USER CODE END I2S1_Init 1 */
  I2S_InitStruct.Mode = LL_I2S_MODE_MASTER_RX;
  I2S_InitStruct.Standard = LL_I2S_STANDARD_PHILIPS;
  I2S_InitStruct.DataFormat = LL_I2S_DATAFORMAT_16B_EXTENDED;
  I2S_InitStruct.MCLKOutput = LL_I2S_MCLK_OUTPUT_DISABLE;
  I2S_InitStruct.AudioFreq = 2254;
  I2S_InitStruct.ClockPolarity = LL_I2S_POLARITY_LOW;
  LL_I2S_Init(SPI1, &I2S_InitStruct);
  /* USER CODE BEGIN I2S1_Init 2 */

  /* USER CODE END I2S1_Init 2 */

}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
