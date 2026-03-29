/*
 * Software crossover LR4 @2500Hz for Squeezelite-ESP32
 *
 * Linkwitz-Riley 4th order (24dB/oct) crossover at 2500Hz.
 * Two cascaded Butterworth 2nd-order IIR sections per band.
 *
 * Signal chain:
 *   Stereo in -> mono (L+R)/2 -> LP cascade -> R out (woofer/CHA)
 *                               -> HP cascade -> L out (tweeter/CHB)
 *
 * Note: TAS5805M on Louder ESP32 maps I2S L->CHB, I2S R->CHA.
 * So LP goes to R (woofer) and HP goes to L (tweeter).
 *
 * Biquad: Direct Form II Transposed, float32, ESP32 FPU.
 * Coefficients: Bristow-Johnson Audio EQ Cookbook, pre-computed for 44.1/48kHz.
 */

#include <stdint.h>
#include "esp_log.h"
#include "sw_crossover.h"

static const char TAG[] = "sw_crossover";

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} biquad_t;

static inline float biquad_process(biquad_t *f, float x) {
    float y = f->b0 * x + f->z1;
    f->z1   = f->b1 * x - f->a1 * y + f->z2;
    f->z2   = f->b2 * x - f->a2 * y;
    return y;
}

static void biquad_set(biquad_t *f, float b0, float b1, float b2, float a1, float a2) {
    f->b0 = b0; f->b1 = b1; f->b2 = b2;
    f->a1 = a1; f->a2 = a2;
    f->z1 = 0.0f; f->z2 = 0.0f;
}

static biquad_t lp[2];
static biquad_t hp[2];
static int current_rate;

/* BW2 @2500Hz, fs=44100Hz */
#define LP_B0_44  0.0251761146f
#define LP_B1_44  0.0503522291f
#define LP_B2_44  0.0251761146f
#define A1_44    -1.5036953413f
#define A2_44     0.6043997995f
#define HP_B0_44  0.7770237852f
#define HP_B1_44 -1.5540475704f
#define HP_B2_44  0.7770237852f

/* BW2 @2500Hz, fs=48000Hz */
#define LP_B0_48  0.0216207184f
#define LP_B1_48  0.0432414368f
#define LP_B2_48  0.0216207184f
#define A1_48    -1.5431211312f
#define A2_48     0.6296040047f
#define HP_B0_48  0.7931812840f
#define HP_B1_48 -1.5863625680f
#define HP_B2_48  0.7931812840f

void sw_crossover_init(int sample_rate) {
    if (sample_rate == current_rate) return;
    current_rate = sample_rate;

    if (sample_rate == 48000) {
        biquad_set(&lp[0], LP_B0_48, LP_B1_48, LP_B2_48, A1_48, A2_48);
        biquad_set(&lp[1], LP_B0_48, LP_B1_48, LP_B2_48, A1_48, A2_48);
        biquad_set(&hp[0], HP_B0_48, HP_B1_48, HP_B2_48, A1_48, A2_48);
        biquad_set(&hp[1], HP_B0_48, HP_B1_48, HP_B2_48, A1_48, A2_48);
    } else {
        biquad_set(&lp[0], LP_B0_44, LP_B1_44, LP_B2_44, A1_44, A2_44);
        biquad_set(&lp[1], LP_B0_44, LP_B1_44, LP_B2_44, A1_44, A2_44);
        biquad_set(&hp[0], HP_B0_44, HP_B1_44, HP_B2_44, A1_44, A2_44);
        biquad_set(&hp[1], HP_B0_44, HP_B1_44, HP_B2_44, A1_44, A2_44);
    }

    ESP_LOGI(TAG, "LR4 @2500Hz initialized for %dHz", sample_rate);
}

void sw_crossover_process(uint8_t *buf, int frames) {
    if (!current_rate) sw_crossover_init(44100);

    int16_t *s = (int16_t *)buf;

    for (int i = 0; i < frames; i++) {
        float mono = (float)((int32_t)s[i * 2] + (int32_t)s[i * 2 + 1]) * 0.5f;

        float lo = biquad_process(&lp[0], mono);
        lo       = biquad_process(&lp[1], lo);

        float hi = biquad_process(&hp[0], mono);
        hi       = biquad_process(&hp[1], hi);

        int32_t lo_i = (int32_t)lo;
        int32_t hi_i = (int32_t)hi;
        if (lo_i >  32767) lo_i =  32767;
        if (lo_i < -32768) lo_i = -32768;
        if (hi_i >  32767) hi_i =  32767;
        if (hi_i < -32768) hi_i = -32768;

        s[i * 2]     = (int16_t)lo_i;   /* L -> woofer (physical cable on R) */
        s[i * 2 + 1] = (int16_t)hi_i;   /* R -> tweeter (physical cable on L) */
    }
}
