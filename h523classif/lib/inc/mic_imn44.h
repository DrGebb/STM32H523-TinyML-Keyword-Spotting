#ifndef MIC_IMN44_H
#define MIC_IMN44_H


#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"
#include "cmsis_os2.h"



#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * MICROPHONE CONFIGURATION (target board / project specific)
 * ============================================================================ */


/* 1. Hardware L/R channel selection pin (1 = MCU-controlled, 0 = hardwired on PCB) */
#define MIC_USE_LR_PIN              1
#define MIC_LR_CHANNEL              0   /* 0 = Left (even DMA indices), 1 = Right (odd DMA indices) */

#if MIC_USE_LR_PIN
#define MIC_LR_GPIO_CLK_ENABLE()    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA)
#define MIC_LR_GPIO_PORT            GPIOA
#define MIC_LR_PIN                  LL_GPIO_PIN_3
#endif

/* 2. Digital front-end settings (gain and DC-blocking filter) */
#define MIC_GAIN                    14.0f
#define MIC_GAIN_Q8                 ((int32_t)(MIC_GAIN * 256.0f))

#define MIC_DC_FILTER_EN            1   /* 1 = enable DC-blocking filter, 0 = disable */
#define MIC_DC_SHIFT                7   /* Filter time constant (2^7 = 128 samples) */

/* 3. Microphone RTOS thread parameters */
#define MIC_THREAD_PRIORITY         osPriorityHigh
#define MIC_THREAD_STACK_SIZE       1024U

/* ============================================================================
 * PUBLIC MODULE INTERFACE
 * ============================================================================ */

/* Microphone thread handle — referenced in DMA1_Stream3_IRQHandler() */
extern osThreadId_t g_mic_tid;

/* Diagnostic counters (for Live Watch / debugging) */
extern volatile uint32_t mic_overrun;
extern volatile uint32_t mic_clip;

/**
 * @brief Callback function type invoked when an audio block is ready.
 * @param samples   Pointer to processed mono samples (int16_t PCM).
 * @param count     Number of samples in the block (MONO_PER_BUF).
 * @param user_ctx  User-defined context pointer (may be NULL).
 */
typedef void (*mic_data_cb_t)(const int16_t *samples, uint32_t count, void *user_ctx);

/**
 * @brief Creates the capture thread and initializes I2S + DMA.
 * @note  PLLI2S clocking (e.g., HSE_SetPLLI2S_16kHz()) must be
 *        configured prior to scheduler startup.
 */
osStatus_t mic_start(mic_data_cb_t cb, void *user_ctx);

/**
 * @brief Resets DC-blocking filter state and diagnostic counters
 *        (applicable on stream pause/restart, e.g., USB Audio).
 */
void mic_reset_dsp(void);

#ifdef __cplusplus
}
#endif




#endif