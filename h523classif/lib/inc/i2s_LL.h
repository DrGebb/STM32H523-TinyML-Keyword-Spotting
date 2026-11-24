#ifndef I2S_LL_H
#define I2S_LL_H

#include "RTE_Components.h"
#include CMSIS_device_header
#include "cmsis_os2.h"
#include <stdint.h>
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_spi.h"
#include "stm32h5xx_ll_gpio.h"
#include "stm32h5xx_ll_rcc.h"
#include "stm32h5xx_ll_dma.h"


// DMA block size increased from 64 to 256 mono samples (16 ms), as USB is no longer required
#define MONO_PER_BUF      256U
#define DMA_BUF_LEN       (MONO_PER_BUF * 2U)     // 512 int16_t

#define FLAG_BUF_A        (1U << 0)               // 0x01U
#define FLAG_BUF_B        (1U << 1)               // 0x02U

extern __IO int16_t buffer_dma_A[DMA_BUF_LEN];
extern __IO int16_t buffer_dma_B[DMA_BUF_LEN];
extern osThreadId_t g_mic_tid;

void I2S_Init(void);

#endif