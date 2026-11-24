#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "cmsis_os2.h"
#include <string.h>
#include "sample_audio.h"

#define AI_THREAD_STACK  6144U

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
    uint32_t    infer_count;
} ei_debug_out_t;

volatile ei_debug_out_t ei_debug_result = {0};

static int raw_feature_get_data(size_t offset, size_t length, float *out_ptr)
{
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = (float)audio_sample[offset + i];
    }
    return 0;
}

static void ai_test_thread(void *arg)
{
    (void)arg;

    /* Verify that the array size matches the expected model input window */
    if ((sizeof(audio_sample) / sizeof(audio_sample[0])) != EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        ei_debug_result.error_code = -999;
        for (;;) {
            osDelay(1000U);
        }
    }

    signal_t features_signal;
    features_signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    features_signal.get_data     = &raw_feature_get_data;

    for (;;) {
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

            ei_debug_result.best_label = max_label;
            ei_debug_result.best_prob  = max_val;
        }

        /* Repeat every 500 ms to facilitate Live Watch inspection and limit bus load */
        osDelay(500U);
    }
}

extern "C" osStatus_t ai_detector_start(void)
{
    const osThreadAttr_t ai_attr = {
        .name       = "ai_test",
        .priority   = osPriorityNormal,
        .stack_size = AI_THREAD_STACK
    };

    osThreadId_t tid = osThreadNew(ai_test_thread, NULL, &ai_attr);
    return (tid != NULL) ? osOK : osError;
}