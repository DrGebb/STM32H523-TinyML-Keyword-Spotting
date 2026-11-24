#ifndef AI_OLED_SHARED_H
#define AI_OLED_SHARED_H

#include <stdint.h>
#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Window divisor: 2 -> every 500 ms (2 rows on OLED),
                   4 -> every 250 ms (4 rows on OLED) */
#define AI_HOPS_PER_SEC         4U
#define AI_NUM_CLASSES          5U
#define AI_OLED_MAX_ROWS        4U

/* Class indices */
#define AI_CLS_FIVE             0U
#define AI_CLS_NOISE            1U
#define AI_CLS_STOP             2U
#define AI_CLS_UNKNOW           3U
#define AI_CLS_ZERO             4U

/* === CONFIGURABLE THRESHOLDS FOR TARGET COMMANDS === */
/* 1. Minimum peak probability in at least one frame within the window (0.0 .. 1.0) */
#define AI_CMD_PEAK_THRESH      0.65f

/* 2. Minimum normalized weighted score across the window (0.0 .. 1.0).
      Reference for 4 frames with weights [0.4, 0.6, 0.8, 1.0] (weight sum = 2.8):
      a 0.85 probability in the most recent frame yields a score of ~0.30.
      For stricter detection (requiring persistence across 2 frames), set to 0.40..0.50f.
      For detecting short utterances within a single frame, set to 0.22..0.28f. */
#define AI_CMD_WEIGHT_THRESH    0.28f

/* 3. Weight decay step for older frames:
      0.20f -> weights [0.4, 0.6, 0.8, 1.0] for 4 frames; [0.8, 1.0] for 2 frames
      0.00f -> uniform weights across all frames (standard mean/sum) */
#define AI_FRAME_DECAY_STEP     0.20f

typedef struct {
    float    history[AI_OLED_MAX_ROWS][AI_NUM_CLASSES];
    uint8_t  count;          /* 1 .. AI_HOPS_PER_SEC */

    uint8_t  winner_idx;     /* Winning class index (command or fallback NOISE/UNKNOW) */
    float    winner_peak;    /* Maximum probability of the winning class across rows */
    uint32_t infer_count;
} ai_oled_snapshot_t;

void ai_oled_publish(const float probs[AI_NUM_CLASSES], uint32_t infer_cnt);
void ai_oled_get_snapshot(ai_oled_snapshot_t *out);
osStatus_t oled_task_start(void);

#ifdef __cplusplus
}
#endif

#endif /* AI_OLED_SHARED_H */