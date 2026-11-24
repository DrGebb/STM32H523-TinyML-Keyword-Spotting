#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ssd1306.h"
#include "ssd1306_fonts.h"

#define SPEC_BANDS          32      // 32 bands * 4 px = 128 px (full display width)
#define WF_MAX_ROWS         45      // Max history rows for full-screen waterfall
#define TRIGGER_THRESHOLD   185     // Trigger threshold (0..255, ~72% height)

typedef enum {
    DEMO_MODE_SPECTRUM = 0,
    DEMO_MODE_DUAL     = 1,
    DEMO_MODE_WATERFALL= 2,
    DEMO_MODE_COUNT
} DemoMode_t;

// Precomputed center frequencies for 32 log-spaced bands (100 Hz .. 8000 Hz)
static const uint16_t kBandFreqHz[SPEC_BANDS] = {
     100,  115,  133,  153,  176,  203,  234,  269,
     310,  357,  411,  474,  546,  628,  724,  834,
     960, 1106, 1274, 1468, 1691, 1947, 2243, 2583,
    2976, 3428, 3948, 4548, 5238, 6034, 6950, 8000
};

// 4x4 Bayer ordered dithering matrix for grayscale rendering on 1-bit OLED
static const uint8_t kBayer4x4[4][4] = {
    {  0, 128,  32, 160 },
    {192,  64, 224,  96 },
    { 48, 176,  16, 144 },
    {240, 112, 208,  80 }
};

// Static buffers to avoid overflowing the 2 KB thread stack
static uint8_t  s_peak_val[SPEC_BANDS];
static uint8_t  s_peak_hold[SPEC_BANDS];
static uint8_t  s_peak_fall[SPEC_BANDS];
static uint8_t  s_wf_buf[WF_MAX_ROWS][SPEC_BANDS];
static uint8_t  s_wf_head = 0;

// Fast integer xorshift PRNG
static uint32_t s_rng = 0x12345678u;
static inline uint8_t FastRand8(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (uint8_t)(s_rng & 0xFFu);
}

/*
 * Synthetic 100 Hz - 8 kHz spectrum generator.
 * Replace this call with real FFT band magnitudes when integrating audio input.
 */
void Demo_SimulateSpectrum(uint8_t out_bins[SPEC_BANDS], uint32_t frame) {
    static float smooth[SPEC_BANDS] = {0};

    // Sweeping fundamental with 2nd/3rd harmonics (constant offset on log scale)
    float f0 = 7.5f + 6.5f * sinf((float)frame * 0.045f);
    float f1 = f0 + 6.0f;
    float f2 = f0 + 9.5f;

    uint32_t beat_phase = frame % 36u;
    float bass_kick = (beat_phase < 6u) ? (190.0f * (1.0f - (float)beat_phase / 6.0f)) : 0.0f;

    uint32_t chirp_phase = frame % 55u;
    float hf_burst = (chirp_phase < 10u) ? 210.0f : 0.0f;
    float hf_pos = 23.0f + 5.0f * sinf((float)frame * 0.12f);

    for (uint8_t i = 0; i < SPEC_BANDS; i++) {
        float x = (float)i;

        float d0 = x - f0;
        float d1 = x - f1;
        float d2 = x - f2;
        float target = 185.0f * expf(-(d0 * d0) / 3.2f)
                     + 115.0f * expf(-(d1 * d1) / 2.5f)
                     +  65.0f * expf(-(d2 * d2) / 2.0f);

        if (i < 7) {
            target += bass_kick * (1.0f - (float)i / 7.0f);
        }

        float dh = x - hf_pos;
        target += hf_burst * expf(-(dh * dh) / 1.8f);

        float noise = (float)(FastRand8() % 28u) + 8.0f;
        target += noise * (1.0f - 0.35f * (x / 31.0f));

        if (target > 255.0f) target = 255.0f;

        // Asymmetric attack/release smoothing
        if (target > smooth[i]) {
            smooth[i] += (target - smooth[i]) * 0.65f;
        } else {
            smooth[i] += (target - smooth[i]) * 0.25f;
        }

        float v = smooth[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 255.0f) v = 255.0f;
        out_bins[i] = (uint8_t)v;
    }
}

static void Draw_FreqAxis(void) {
    ssd1306_Line(0, 55, 127, 55, White);

    // Ticks aligned with bands: 100 Hz (0), 1 kHz (16), 4 kHz (26), 8 kHz (31)
    ssd1306_Line(1,   55, 1,   57, White);
    ssd1306_Line(65,  55, 65,  57, White);
    ssd1306_Line(105, 55, 105, 57, White);
    ssd1306_Line(125, 55, 125, 57, White);

    ssd1306_SetCursor(0, 56);
    ssd1306_WriteString("100", Font_6x8, White);

    ssd1306_SetCursor(35, 56);
    ssd1306_WriteString("400", Font_6x8, White);

    ssd1306_SetCursor(60, 56);
    ssd1306_WriteString("1k", Font_6x8, White);

    ssd1306_SetCursor(82, 56);
    ssd1306_WriteString("2k", Font_6x8, White);

    ssd1306_SetCursor(100, 56);
    ssd1306_WriteString("4k", Font_6x8, White);

    ssd1306_SetCursor(115, 56);
    ssd1306_WriteString("8k", Font_6x8, White);
}

static void Draw_WaterfallArea(uint8_t y_start, uint8_t y_end, uint8_t dom_idx) {
    uint8_t rows = (uint8_t)(y_end - y_start + 1);
    if (rows > WF_MAX_ROWS) rows = WF_MAX_ROWS;

    for (uint8_t r = 0; r < rows; r++) {
        uint8_t wf_idx = (uint8_t)((s_wf_head + r) % WF_MAX_ROWS);
        uint8_t py = (uint8_t)(y_start + r);

        for (uint8_t i = 0; i < SPEC_BANDS; i++) {
            uint8_t val = s_wf_buf[wf_idx][i];
            if (val < 20) continue; // Noise floor gate

            uint8_t x0 = (uint8_t)(i * 4);
            for (uint8_t dx = 0; dx < 4; dx++) {
                uint8_t px = (uint8_t)(x0 + dx);
                if (val > kBayer4x4[py & 3u][px & 3u]) {
                    ssd1306_DrawPixel(px, py, White);
                }
            }
        }
    }

    // Dominant frequency cursor on the newest (top) row
    uint8_t mx = (uint8_t)(dom_idx * 4 + 1);
    ssd1306_DrawPixel(mx,     y_start, White);
    ssd1306_DrawPixel(mx + 1, y_start, White);
}

/*
 * Updates peak-hold/waterfall state and renders a complete frame to the SSD1306.
 */
void Spectrum_ProcessAndDraw(const uint8_t bins[SPEC_BANDS], DemoMode_t mode) {
    uint8_t dom_idx = 0;
    uint8_t max_val = 0;

    // Update peak-hold caps (hold delay + accelerating gravity) and find global peak
    for (uint8_t i = 0; i < SPEC_BANDS; i++) {
        uint8_t v = bins[i];
        if (v > max_val) {
            max_val = v;
            dom_idx = i;
        }

        if (v >= s_peak_val[i]) {
            s_peak_val[i]  = v;
            s_peak_hold[i] = 10;
            s_peak_fall[i] = 2;
        } else if (s_peak_hold[i] > 0) {
            s_peak_hold[i]--;
        } else {
            if (s_peak_val[i] > s_peak_fall[i]) {
                s_peak_val[i] = (uint8_t)(s_peak_val[i] - s_peak_fall[i]);
            } else {
                s_peak_val[i] = 0;
            }
            if (s_peak_fall[i] < 20) s_peak_fall[i] += 2;
        }
    }

    // Push current frame into the circular waterfall buffer
    s_wf_head = (s_wf_head == 0) ? (WF_MAX_ROWS - 1) : (uint8_t)(s_wf_head - 1);
    memcpy(s_wf_buf[s_wf_head], bins, SPEC_BANDS);

    int16_t db = -48;
    if (max_val >= 1) {
        db = (int16_t)(20.0f * log10f((float)max_val / 255.0f));
        if (db < -48) db = -48;
    }
    uint8_t triggered = (max_val >= TRIGGER_THRESHOLD) ? 1 : 0;

    ssd1306_Fill(Black);

    // Header bar: 21 chars max (126 px), e.g. "P:1.9k -12dB [SPEC] !"
    char hdr[24];
    char freq_str[10];
    uint16_t f_hz = kBandFreqHz[dom_idx];

    if (f_hz < 1000) {
        snprintf(freq_str, sizeof(freq_str), "%3uHz", f_hz);
    } else {
        snprintf(freq_str, sizeof(freq_str), "%u.%uk ", f_hz / 1000, (f_hz % 1000) / 100);
    }

    const char *tag = (mode == DEMO_MODE_SPECTRUM) ? "SPEC" :
                      (mode == DEMO_MODE_DUAL)     ? "DUAL" : "WATR";

    snprintf(hdr, sizeof(hdr), "P:%s %3ddB [%s]%c",
             freq_str, (int)db, tag, triggered ? '!' : ' ');

    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString(hdr, Font_6x8, White);
    ssd1306_Line(0, 9, 127, 9, White);

    if (mode == DEMO_MODE_SPECTRUM) {
        const uint8_t y_base = 54;
        const uint8_t max_h  = 43;
        uint8_t trig_y = (uint8_t)(y_base - ((uint16_t)TRIGGER_THRESHOLD * max_h) / 255u);

        for (uint8_t i = 0; i < SPEC_BANDS; i++) {
            uint8_t x = (uint8_t)(i * 4);
            uint8_t h = (uint8_t)(((uint16_t)bins[i] * max_h) / 255u);
            uint8_t ph = (uint8_t)(((uint16_t)s_peak_val[i] * max_h) / 255u);

            if (h > 0) {
                ssd1306_FillRectangle(x, (uint8_t)(y_base - h + 1), (uint8_t)(x + 2), y_base, White);
            }
            if (ph > 0) {
                uint8_t py = (uint8_t)(y_base - ph + 1);
                ssd1306_Line(x, py, (uint8_t)(x + 2), py, White);
            }

            // Dashed threshold line in the 1 px gaps between bars
            ssd1306_DrawPixel((uint8_t)(x + 3), trig_y, White);
        }

        // Dominant peak indicator above the loudest band
        uint8_t mx = (uint8_t)(dom_idx * 4);
        ssd1306_Line(mx, 10, (uint8_t)(mx + 2), 10, White);
        ssd1306_DrawPixel((uint8_t)(mx + 1), 11, White);

    } else if (mode == DEMO_MODE_DUAL) {
        // Top half: spectrum bars (y = 11..31)
        const uint8_t y_base = 31;
        const uint8_t max_h  = 21;

        for (uint8_t i = 0; i < SPEC_BANDS; i++) {
            uint8_t x = (uint8_t)(i * 4);
            uint8_t h  = (uint8_t)(((uint16_t)bins[i] * max_h) / 255u);
            uint8_t ph = (uint8_t)(((uint16_t)s_peak_val[i] * max_h) / 255u);

            if (h > 0) {
                ssd1306_FillRectangle(x, (uint8_t)(y_base - h + 1), (uint8_t)(x + 2), y_base, White);
            }
            if (ph > 0) {
                uint8_t py = (uint8_t)(y_base - ph + 1);
                ssd1306_Line(x, py, (uint8_t)(x + 2), py, White);
            }
        }

        // Bottom half: waterfall (y = 33..54)
        ssd1306_Line(0, 32, 127, 32, White);
        Draw_WaterfallArea(33, 54, dom_idx);

    } else {
        Draw_WaterfallArea(10, 54, dom_idx);
    }

    Draw_FreqAxis();

    ssd1306_UpdateScreen();
}