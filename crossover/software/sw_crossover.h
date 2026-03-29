/*
 * baboo v6 — Software crossover LR4 @2500Hz
 *
 * Processes audio in-place: stereo input → mono mix → LP(L) + HP(R) output.
 * L = woofer (low-pass), R = tweeter (high-pass).
 *
 * Uses float biquad IIR (Direct Form II Transposed) on ESP32 FPU.
 * Two cascaded Butterworth 2nd-order sections per band = LR4 24dB/oct.
 */

#pragma once

void baboo_crossover_init(int sample_rate);
void baboo_crossover_process(uint8_t *buf, int frames);
