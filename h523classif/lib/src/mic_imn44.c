#include "mic_imn44.h"
#include "i2s_LL.h"

/*
 * Note: i2s_LL.h must provide declarations for:
 * - I2S_Init(), FLAG_BUF_A, FLAG_BUF_B, buffer_dma_A, buffer_dma_B, MONO_PER_BUF
 * - LL_GPIO_*, LL_AHB1_GRP1_*, and __SSAT (include "main.h" if not transitively included)
 */

_Static_assert(MIC_GAIN_Q8 > 0 && MIC_GAIN_Q8 <= 32768, "MIC_GAIN must be in 0..128");
_Static_assert(MIC_LR_CHANNEL == 0 || MIC_LR_CHANNEL == 1, "MIC_LR_CHANNEL must be 0 or 1");

osThreadId_t g_mic_tid = NULL;
volatile uint32_t mic_overrun = 0;
volatile uint32_t mic_clip    = 0;

static mic_data_cb_t s_callback = NULL;
static void         *s_user_ctx = NULL;

static int32_t s_dc_q8   = 0;
static bool    s_dc_init = false;

/* Buffer allocated with static duration to prevent thread stack overhead regardless of MONO_PER_BUF */
static int16_t s_mono_buf[MONO_PER_BUF];

#if MIC_USE_LR_PIN
static void mic_pin_lr_init(void)
{
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    MIC_LR_GPIO_CLK_ENABLE();

    GPIO_InitStruct.Pin        = MIC_LR_PIN;
    GPIO_InitStruct.Mode       = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStruct.Speed      = LL_GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull       = LL_GPIO_PULL_NO;
    LL_GPIO_Init(MIC_LR_GPIO_PORT, &GPIO_InitStruct);

#if (MIC_LR_CHANNEL == 0)
    LL_GPIO_ResetOutputPin(MIC_LR_GPIO_PORT, MIC_LR_PIN);
#else
    LL_GPIO_SetOutputPin(MIC_LR_GPIO_PORT, MIC_LR_PIN);
#endif
}
#endif

static void mic_process_dma_half(const volatile int16_t *src)
{
    for (uint32_t i = 0; i < MONO_PER_BUF; i++) {
        int32_t s = src[2U * i + MIC_LR_CHANNEL];

#if MIC_DC_FILTER_EN
        if (!s_dc_init) {
            s_dc_q8   = s << 8;
            s_dc_init = true;
        }
        s_dc_q8 += ((s << 8) - s_dc_q8) >> MIC_DC_SHIFT;
        s -= (s_dc_q8 >> 8);
#endif

        int32_t v = (s * MIC_GAIN_Q8) >> 8;
        int32_t o = __SSAT(v, 16);
        if (o != v) {
            mic_clip++;
        }

        s_mono_buf[i] = (int16_t)o;
    }

    if (s_callback != NULL) {
        s_callback(s_mono_buf, MONO_PER_BUF, s_user_ctx);
    }
}

static void mic_thread(void *arg)
{
    (void)arg;

#if MIC_USE_LR_PIN
    mic_pin_lr_init();
#endif

    g_mic_tid = osThreadGetId();

    /* Initialize I2S + DMA strictly after g_mic_tid is valid */
    I2S_Init();

    for (;;) {
        uint32_t f = osThreadFlagsWait(FLAG_BUF_A | FLAG_BUF_B, osFlagsWaitAny, osWaitForever);
        if (f & osFlagsError) {
            continue;
        }

        if ((f & FLAG_BUF_A) && (f & FLAG_BUF_B)) {
            mic_overrun++;
        }

        if (f & FLAG_BUF_A) {
            mic_process_dma_half(buffer_dma_A);
        }
        if (f & FLAG_BUF_B) {
            mic_process_dma_half(buffer_dma_B);
        }
    }
}

osStatus_t mic_start(mic_data_cb_t cb, void *user_ctx)
{
    s_callback = cb;
    s_user_ctx = user_ctx;

    const osThreadAttr_t mic_attr = {
        .name       = "mic_dma",
        .priority   = MIC_THREAD_PRIORITY,
        .stack_size = MIC_THREAD_STACK_SIZE
    };

    osThreadId_t tid = osThreadNew(mic_thread, NULL, &mic_attr);
    if (tid == NULL) {
        return osError;
    }

    g_mic_tid = tid;
    return osOK;
}

void mic_reset_dsp(void)
{
    s_dc_init   = false;
    s_dc_q8     = 0;
    mic_overrun = 0;
    mic_clip    = 0;
}