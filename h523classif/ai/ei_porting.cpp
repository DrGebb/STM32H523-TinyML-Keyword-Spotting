#include "edge-impulse-sdk/porting/ei_classifier_porting.h"
#include "stm32h523xx.h"
#include "cmsis_os2.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ============================================================================
 * 1. Microsecond timer based on DWT->CYCCNT (Cortex-M33)
 * ============================================================================ */
static uint64_t s_us_total = 0;
static uint32_t s_last_cyccnt = 0;
static bool     s_dwt_init = false;

static void dwt_init_if_needed(void)
{
    if (!s_dwt_init) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
        s_last_cyccnt = DWT->CYCCNT;
        s_dwt_init = true;
    }
}

/* Returns elapsed time in milliseconds (assuming 1 RTX5 tick = 1 ms) */
uint64_t ei_read_timer_ms(void)
{
    if (osKernelGetState() == osKernelRunning) {
        return (uint64_t)osKernelGetTickCount();
    }
    return ei_read_timer_us() / 1000ULL;
}

/* High-resolution time in microseconds (supports sub-millisecond measurements at 250 MHz) */
uint64_t ei_read_timer_us(void)
{
    dwt_init_if_needed();

    uint32_t now = DWT->CYCCNT;
    uint32_t delta_cycles = now - s_last_cyccnt; /* Unsigned arithmetic handles wrap-around safely */
    uint32_t cycles_per_us = SystemCoreClock / 1000000U;

    if (cycles_per_us > 0U) {
        uint32_t delta_us = delta_cycles / cycles_per_us;
        if (delta_us > 0U) {
            s_us_total += delta_us;
            /* Advance s_last_cyccnt by exact accounted integer microseconds to preserve remainder cycles */
            s_last_cyccnt += delta_us * cycles_per_us;
        }
    }
    return s_us_total;
}

/* ============================================================================
 * 2. Sleep (non-blocking for other RTX5 threads)
 * ============================================================================ */
EI_IMPULSE_ERROR ei_sleep(int32_t time_ms)
{
    if (time_ms <= 0) {
        return EI_IMPULSE_OK;
    }

    if (osKernelGetState() == osKernelRunning) {
        osDelay((uint32_t)time_ms);
    } else {
        uint64_t start = ei_read_timer_ms();
        while ((ei_read_timer_ms() - start) < (uint64_t)time_ms) {
            __NOP();
        }
    }
    return EI_IMPULSE_OK;
}

/* ============================================================================
 * 3. Memory allocation (with accurate peak tracking and thread safety)
 *    Ensure Heap_Size in startup_stm32h523xx.s is configured to at least 0x4000.
 * ============================================================================ */
volatile size_t current_heap_usage = 0;
volatile size_t peak_heap_usage    = 0;

/* Store block size in an 8-byte header for retrieval in ei_free
   (8-byte size preserves ARM ABI 8-byte alignment) */
typedef struct {
    size_t size;
    size_t pad;
} ei_alloc_hdr_t;

void *ei_malloc(size_t size)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    ei_alloc_hdr_t *hdr = (ei_alloc_hdr_t *)malloc(size + sizeof(ei_alloc_hdr_t));
    if (hdr == NULL) {
        if (!primask) __enable_irq();
        return NULL;
    }

    hdr->size = size;
    current_heap_usage += size;
    if (current_heap_usage > peak_heap_usage) {
        peak_heap_usage = current_heap_usage;
    }

    if (!primask) __enable_irq();
    return (void *)(hdr + 1);
}

void *ei_calloc(size_t nitems, size_t size)
{
    size_t total = nitems * size;
    void *p = ei_malloc(total);
    if (p != NULL) {
        memset(p, 0, total);
    }
    return p;
}

void ei_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    ei_alloc_hdr_t *hdr = ((ei_alloc_hdr_t *)ptr) - 1;
    if (current_heap_usage >= hdr->size) {
        current_heap_usage -= hdr->size;
    } else {
        current_heap_usage = 0;
    }
    free(hdr);

    if (!primask) __enable_irq();
}

/* ============================================================================
 * 4. Logging and cancellation
 * ============================================================================ */
char ei_last_err_msg[128] = {0};

void ei_printf(const char *format, ...)
{
    (void)format;

    va_list args;
    va_start(args, format);
    vsnprintf(ei_last_err_msg, sizeof(ei_last_err_msg), format, args);
    va_end(args);
    /* If UART / RTT log output is required, uncomment vsnprintf here */
}

void ei_printf_float(float f)
{
    ei_printf("%f", f);
}

#if defined(__cplusplus) && EI_C_LINKAGE == 1
extern "C"
#endif
void ei_putchar(char c)
{
    (void)c;
}

EI_IMPULSE_ERROR ei_run_impulse_check_canceled(void)
{
    return EI_IMPULSE_OK;
}