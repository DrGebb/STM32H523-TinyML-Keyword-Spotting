#include "ai_oled_shared.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <stdio.h>
#include <string.h>

#define FLAG_OLED_UPDATE    (1U << 0)
#define OLED_THREAD_STACK   1024U

static osThreadId_t       s_oled_tid = NULL;
static ai_oled_snapshot_t s_snapshot = {0};

static const char * const s_short_names[AI_NUM_CLASSES] = {
    "FIVE", "NOIS", "STOP", "UNKW", "ZERO"
};
static const char * const s_full_names[AI_NUM_CLASSES] = {
    "FIVE", "NOISE", "STOP", "UNKNOW", "ZERO"
};

/* X-coordinates of 5 columns across 128 px */
static const uint8_t s_col_x[AI_NUM_CLASSES] = { 0, 26, 52, 78, 104 };

/* Helper function to compute weighted score and peak probability for a single class */
static void calc_class_metrics(const ai_oled_snapshot_t *snap, uint32_t cls_idx,
                               float *out_w_score, float *out_peak)
{
    float w_sum  = 0.0f;
    float p_wsum = 0.0f;
    float peak   = 0.0f;

    for (uint8_t r = 0; r < snap->count; r++) {
        /* age = 0 for the most recent row (count - 1), increasing for older rows */
        uint8_t age = (uint8_t)((snap->count - 1U) - r);
        float w = 1.0f - (float)age * AI_FRAME_DECAY_STEP;
        if (w < 0.1f) {
            w = 0.1f;
        }

        float p = snap->history[r][cls_idx];
        p_wsum += w * p;
        w_sum  += w;

        if (p > peak) {
            peak = p;
        }
    }

    *out_w_score = (w_sum > 0.0f) ? (p_wsum / w_sum) : 0.0f;
    *out_peak    = peak;
}

/* ============================================================================
 * Result publication from ai_thread (atomic history shift and score aggregation)
 * ============================================================================ */
void ai_oled_publish(const float probs[AI_NUM_CLASSES], uint32_t infer_cnt)
{
    static const uint8_t target_classes[] = { AI_CLS_FIVE, AI_CLS_STOP, AI_CLS_ZERO };
    uint32_t rows = (AI_HOPS_PER_SEC <= AI_OLED_MAX_ROWS) ? AI_HOPS_PER_SEC : AI_OLED_MAX_ROWS;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if (s_snapshot.count < rows) {
        for (uint32_t c = 0; c < AI_NUM_CLASSES; c++) {
            s_snapshot.history[s_snapshot.count][c] = probs[c];
        }
        s_snapshot.count++;
    } else {
        for (uint32_t r = 0; r < (rows - 1U); r++) {
            for (uint32_t c = 0; c < AI_NUM_CLASSES; c++) {
                s_snapshot.history[r][c] = s_snapshot.history[r + 1U][c];
            }
        }
        for (uint32_t c = 0; c < AI_NUM_CLASSES; c++) {
            s_snapshot.history[rows - 1U][c] = probs[c];
        }
    }

    /* 1. Evaluate target commands (five, stop, zero) for highest score */
    int8_t best_cmd_idx    = -1;
    float  best_cmd_wscore = -1.0f;
    float  best_cmd_peak   = 0.0f;

    for (uint32_t i = 0; i < (sizeof(target_classes) / sizeof(target_classes[0])); i++) {
        uint8_t cls = target_classes[i];
        float w_score, peak;
        calc_class_metrics(&s_snapshot, cls, &w_score, &peak);

        if ((peak >= AI_CMD_PEAK_THRESH) && (w_score >= AI_CMD_WEIGHT_THRESH)) {
            if (w_score > best_cmd_wscore) {
                best_cmd_wscore = w_score;
                best_cmd_peak   = peak;
                best_cmd_idx    = (int8_t)cls;
            }
        }
    }

    /* 2. Select matched target command if thresholds met;
          otherwise select between NOISE and UNKNOW */
    if (best_cmd_idx >= 0) {
        s_snapshot.winner_idx  = (uint8_t)best_cmd_idx;
        s_snapshot.winner_peak = best_cmd_peak;
    } else {
        float noise_w, noise_peak;
        float unknw_w, unknw_peak;
        calc_class_metrics(&s_snapshot, AI_CLS_NOISE,  &noise_w, &noise_peak);
        calc_class_metrics(&s_snapshot, AI_CLS_UNKNOW, &unknw_w, &unknw_peak);

        if (noise_w >= unknw_w) {
            s_snapshot.winner_idx  = AI_CLS_NOISE;
            s_snapshot.winner_peak = noise_peak;
        } else {
            s_snapshot.winner_idx  = AI_CLS_UNKNOW;
            s_snapshot.winner_peak = unknw_peak;
        }
    }

    s_snapshot.infer_count = infer_cnt;

    if (!primask) __enable_irq();

    if (s_oled_tid != NULL) {
        osThreadFlagsSet(s_oled_tid, FLAG_OLED_UPDATE);
    }
}

void ai_oled_get_snapshot(ai_oled_snapshot_t *out)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *out = s_snapshot;
    if (!primask) __enable_irq();
}

/* ============================================================================
 * OLED frame rendering
 * ============================================================================ */
static void oled_draw_frame(const ai_oled_snapshot_t *snap)
{
    char text_buf[24];
    const uint8_t labels_y = 35;

    ssd1306_Fill(Black);

    /* 1. Render up to AI_HOPS_PER_SEC probability rows above class labels */
    for (uint8_t r = 0; r < snap->count; r++) {
        /* Align rows bottom-up immediately above the separator line (y = 33) */
        uint8_t y = (uint8_t)(32U - (snap->count - r) * 8U);

        for (uint8_t c = 0; c < AI_NUM_CLASSES; c++) {
            float p = snap->history[r][c];
            if (p >= 0.995f) {
                snprintf(text_buf, sizeof(text_buf), "1.0");
            } else {
                uint32_t pct = (uint32_t)(p * 100.0f + 0.5f);
                snprintf(text_buf, sizeof(text_buf), ".%02lu", (unsigned long)pct);
            }
            ssd1306_SetCursor((uint8_t)(s_col_x[c] + 3U), y);
            ssd1306_WriteString(text_buf, Font_6x8, White);
        }
    }

    /* 2. Class label row (below vertical midpoint) */
    ssd1306_Line(0, 33, 127, 33, White);
    for (uint8_t c = 0; c < AI_NUM_CLASSES; c++) {
        snprintf(text_buf, sizeof(text_buf), "%s", s_short_names[c]);
        ssd1306_SetCursor(s_col_x[c], labels_y);
        ssd1306_WriteString(text_buf, Font_6x8, White);
    }
    ssd1306_Line(0, 44, 127, 44, White);

    /* 3. Winning class summary over 1-second window at bottom region (Font_11x18) */
    uint32_t win_pct = (uint32_t)(snap->winner_peak * 100.0f + 0.5f);
    if (win_pct > 100U) {
        win_pct = 100U;
    }
    snprintf(text_buf, sizeof(text_buf), "%s %lu%%",
             s_full_names[snap->winner_idx], (unsigned long)win_pct);

    /* Horizontally center text string (Font_11x18 character width = 11 px) */
    uint16_t str_px = (uint16_t)strlen(text_buf) * 11U;
    uint8_t  win_x  = (str_px < 128U) ? (uint8_t)((128U - str_px) / 2U) : 0U;

    ssd1306_SetCursor(win_x, 46);
    ssd1306_WriteString(text_buf, Font_11x18, White);

    ssd1306_UpdateScreen();
}

/* ============================================================================
 * RTX5 display thread (priority below AI and microphone threads)
 * ============================================================================ */
static void oled_thread(void *arg)
{
    (void)arg;
    s_oled_tid = osThreadGetId();

    I2C_LL_Init_test();

    ssd1306_Init();
    ssd1306_Fill(Black);
    ssd1306_SetCursor(10, 24);
    ssd1306_WriteString((char *)"READY...", Font_11x18, White);
    ssd1306_UpdateScreen();

    ai_oled_snapshot_t local_snap;

    for (;;) {
        uint32_t f = osThreadFlagsWait(FLAG_OLED_UPDATE, osFlagsWaitAny, osWaitForever);
        if (f & osFlagsError) {
            continue;
        }

        ai_oled_get_snapshot(&local_snap);
        oled_draw_frame(&local_snap);
    }
}

osStatus_t oled_task_start(void)
{
    const osThreadAttr_t oled_attr = {
        .name       = "oled_ui",
        .priority   = osPriorityBelowNormal, /* Lower priority than mic_thread and ai_thread */
        .stack_size = OLED_THREAD_STACK
    };

    osThreadId_t tid = osThreadNew(oled_thread, NULL, &oled_attr);
    if (tid == NULL) {
        return osError;
    }
    s_oled_tid = tid;
    return osOK;
}