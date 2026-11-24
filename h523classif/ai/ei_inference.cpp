#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "cmsis_os2.h"
#include <string.h>
#include "ai_oled_shared.h"

extern "C" {
    #include "mic_imn44.h"
    #include "i2s_LL.h"
}

/* ============================================================================
 * INFERENCE WINDOW CONFIGURATION
 * ============================================================================ */
/* Model window size (typically 16000 samples for 1 s @ 16 kHz) */
#define AI_WIN_LEN          EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE

/* Classification interval (sliding window hop size).
   Example: AI_WIN_LEN / 4 = every 250 ms (4 Hz).
   Set to AI_WIN_LEN for non-overlapping full-window execution. */
#define AI_HOP_SAMPLES  (AI_WIN_LEN / AI_HOPS_PER_SEC)

#define AI_THREAD_STACK     2048U   /* Minimum ~4-6 KB stack required by Edge Impulse + TFLite; do not reduce to 1024 */
#define FLAG_AI_RUN         (1U << 0)

/* ============================================================================
 * BUFFERS AND STATE
 * ============================================================================ */
typedef struct {

    int error_code;
    int dsp_time_ms;
    int class_time_ms;

    float five_prob;
    float noise_prob;
    float stop_prob;
    float unknow_prob;
    float zero_prob;

    const char *best_label;
    float       best_prob;

    /* --- Telemetry fields for Live Watch inspection --- */
    uint32_t    infer_count;     /* Expected increment: +4 per second (liveness indicator) */
    const char *last_cmd_label;  /* Last detected command (not overwritten by noise) */
    float       last_cmd_prob;   /* Probability of the last detected command */
    uint32_t    five_hits;       /* Detection count for "five" */
    uint32_t    stop_hits;       /* Detection count for "stop" */
    uint32_t    zero_hits;       /* Detection count for "zero" */

    uint32_t    skipped_hops;

} ei_debug_out_t;


volatile ei_debug_out_t ei_debug_result = {0};

/* Ring buffer for continuous recording and snapshot buffer for inference */
static int16_t  s_ring_buf[AI_WIN_LEN];
static int16_t  s_infer_buf[AI_WIN_LEN];

static uint32_t s_ring_head          = 0;
static uint32_t s_samples_collected  = 0; /* Sample count until the initial full window is accumulated */
static uint32_t s_samples_since_hop  = 0;

/* Busy flag for lock-free buffer handoff */
static volatile bool s_ai_busy = false;
static osThreadId_t  s_ai_tid  = NULL;

/* ============================================================================
 * 1. Edge Impulse callback: supplies slices from s_infer_buf (int16 -> float)
 * ============================================================================ */
static int raw_feature_get_data(size_t offset, size_t length, float *out_ptr)
{
    /* NOTE: Do not divide by 32768.0f; Edge Impulse models trained on int16
       expect the raw range -32768.0 .. +32767.0 (consistent with reference test) */
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = (float)s_infer_buf[offset + i];
    }
    return 0;
}

/* ============================================================================
 * 2. Microphone callback (executes within high-priority mic_thread context)
 * ============================================================================ */
extern "C"  void on_mic_data_ready(const int16_t *samples, uint32_t count, void *user_ctx)
{
    (void)user_ctx;

    /* 1. Copy incoming block into the ring buffer with wrap-around handling */
    uint32_t remaining = count;
    uint32_t src_idx   = 0;

    while (remaining > 0U) {
        uint32_t space_to_end = AI_WIN_LEN - s_ring_head;
        uint32_t chunk = (remaining < space_to_end) ? remaining : space_to_end;

        memcpy(&s_ring_buf[s_ring_head], &samples[src_idx], chunk * sizeof(int16_t));

        s_ring_head += chunk;
        if (s_ring_head >= AI_WIN_LEN) {
            s_ring_head = 0;
        }
        src_idx   += chunk;
        remaining -= chunk;
    }

    if (s_samples_collected < AI_WIN_LEN) {
        s_samples_collected += count;
    }
    s_samples_since_hop += count;

    /* 2. Trigger inference if a full window is accumulated and hop threshold is met */
    if ((s_samples_collected >= AI_WIN_LEN) && (s_samples_since_hop >= AI_HOP_SAMPLES)) {
        if (!s_ai_busy && (s_ai_tid != NULL)) {
            /* Linearize ring buffer in chronological order (oldest to newest) */
            uint32_t tail_len = AI_WIN_LEN - s_ring_head;
            memcpy(&s_infer_buf[0],        &s_ring_buf[s_ring_head], tail_len * sizeof(int16_t));
            if (s_ring_head > 0U) {
                memcpy(&s_infer_buf[tail_len], &s_ring_buf[0],       s_ring_head * sizeof(int16_t));
            }

            __DMB();
            s_ai_busy = true;
            s_samples_since_hop = 0;

            /* Signal inference thread */
            osThreadFlagsSet(s_ai_tid, FLAG_AI_RUN);
        } else {
            /* Inference is still active on the previous frame; skip this hop
               while microphone acquisition continues writing to the ring buffer without sample loss */
            ei_debug_result.skipped_hops++;
            s_samples_since_hop = 0;
        }
    }
}

/* ============================================================================
 * 3. Inference thread (background execution at normal priority)
 * ============================================================================ */
static void ai_thread(void *arg)
{
    (void)arg;
    s_ai_tid = osThreadGetId();

    /* Start microphone acquisition and register callback */
    mic_start(on_mic_data_ready, NULL);

    signal_t features_signal;
    features_signal.total_length = AI_WIN_LEN;
    features_signal.get_data     = &raw_feature_get_data;

    uint32_t cmd_cooldown = 0; /* Post-detection cooldown counter (in hops) */

    for (;;) {
        uint32_t f = osThreadFlagsWait(FLAG_AI_RUN, osFlagsWaitAny, osWaitForever);
        if (f & osFlagsError) {
            continue;
        }

        ei_impulse_result_t result = {0};
        EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);

        ei_debug_result.error_code = res;
        if (res == EI_IMPULSE_OK) {
            ei_debug_result.dsp_time_ms   = result.timing.dsp;
            ei_debug_result.class_time_ms = result.timing.classification;
            ei_debug_result.infer_count++;

            float       max_val   = -1.0f;
            const char *max_label = NULL;

            for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
                float val         = result.classification[i].value;
                const char *label = ei_classifier_inferencing_categories[i];

                if (val > max_val) {
                    max_val   = val;
                    max_label = label;
                }

                if      (strcmp(label, "five")   == 0) ei_debug_result.five_prob   = val;
                else if (strcmp(label, "noise")  == 0) ei_debug_result.noise_prob  = val;
                else if (strcmp(label, "stop")   == 0) ei_debug_result.stop_prob   = val;
                else if (strcmp(label, "unknow") == 0) ei_debug_result.unknow_prob = val;
                else if (strcmp(label, "zero")   == 0) ei_debug_result.zero_prob   = val;
            }

            /* Publish class probabilities for OLED display */
            float frame_probs[AI_NUM_CLASSES] = {
                ei_debug_result.five_prob,
                ei_debug_result.noise_prob,
                ei_debug_result.stop_prob,
                ei_debug_result.unknow_prob,
                ei_debug_result.zero_prob
            };

            ai_oled_publish(frame_probs, ei_debug_result.infer_count);


            ei_debug_result.best_label = max_label;
            ei_debug_result.best_prob  = max_val;

            /* Command latching logic with debounce cooldown */
            if (cmd_cooldown > 0U) {
                cmd_cooldown--;
            } else if ((max_val >= 0.70f) && (max_label != NULL)) {
                if (strcmp(max_label, "five") == 0) {
                    ei_debug_result.five_hits++;
                    ei_debug_result.last_cmd_label = max_label;
                    ei_debug_result.last_cmd_prob  = max_val;
                    cmd_cooldown = 3U; /* Cooldown: 3 * 250 ms = 750 ms */
                    /* Insert handler for "five" command here */
                } else if (strcmp(max_label, "stop") == 0) {
                    ei_debug_result.stop_hits++;
                    ei_debug_result.last_cmd_label = max_label;
                    ei_debug_result.last_cmd_prob  = max_val;
                    cmd_cooldown = 3U;
                    /* Insert handler for "stop" command here */
                } else if (strcmp(max_label, "zero") == 0) {
                    ei_debug_result.zero_hits++;
                    ei_debug_result.last_cmd_label = max_label;
                    ei_debug_result.last_cmd_prob  = max_val;
                    cmd_cooldown = 3U;
                    /* Insert handler for "zero" command here */
                }
            }
        }

        __DMB();
        s_ai_busy = false; /* Release s_infer_buf for next snapshot */
    }
}

/* ============================================================================
 * 4. Entry point invoked from main() (before or after osKernelStart)
 * ============================================================================ */
extern "C" osStatus_t ai_detector_start(void)
{
    const osThreadAttr_t ai_attr = {
        .name       = "ai_infer",
        .priority   = osPriorityNormal,  /* Must remain strictly lower than mic_thread (osPriorityHigh) */
        .stack_size = AI_THREAD_STACK
    };

    s_ai_tid = osThreadNew(ai_thread, NULL, &ai_attr);
    return (s_ai_tid != NULL) ? osOK : osError;
}