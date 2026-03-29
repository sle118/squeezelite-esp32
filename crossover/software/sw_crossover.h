/*
 * Software crossover LR4 @2500Hz for Squeezelite-ESP32
 *
 * Processes audio in-place: stereo input -> mono mix -> LP(woofer) + HP(tweeter).
 * Uses float biquad IIR (Direct Form II Transposed) on ESP32 FPU.
 * Two cascaded Butterworth 2nd-order sections per band = LR4 24dB/oct.
 *
 * Alternative to TAS5805M hardware DSP — works with any I2S DAC.
 */

#pragma once

#include <stdint.h>

void sw_crossover_init(int sample_rate);
void sw_crossover_process(uint8_t *buf, int frames);
