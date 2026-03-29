#!/usr/bin/env python3
"""
baboo v6 — TAS5805M Biquad Coefficient Calculator

Generates NVS-ready byte arrays for all crossover filters.
Format: 5.27 fixed-point, a1/a2 sign-inverted per TAS5805M convention.

Filters:
  - LR4 crossover @2500Hz (2x BW2 cascade per channel)
  - Subsonic HPF @35Hz (woofer protection)
  - BSC high-shelf @300Hz -3dB (baffle step compensation, 100mm baffle)
  - Tweeter trim -0.5dB (level alignment)

Usage:
  python coefficients.py
  python coefficients.py --fs 48000
"""

import math
import argparse
import json

SCALE = 2**27  # 5.27 fixed-point


def float_to_527(val):
    """Convert float to 5.27 fixed-point (signed 32-bit, two's complement)."""
    raw = int(round(val * SCALE))
    if raw < 0:
        raw += (1 << 32)
    return raw


def coeffs_to_nvs(b0, b1, b2, a1, a2):
    """Convert 5 float coefficients to 20-byte NVS array.
    a1 and a2 are sign-inverted for TAS5805M."""
    vals = [b0, b1, b2, -a1, -a2]
    bl = []
    for v in vals:
        raw = float_to_527(v)
        bl.extend([(raw >> 24) & 0xFF, (raw >> 16) & 0xFF,
                    (raw >> 8) & 0xFF, raw & 0xFF])
    return bl


def butterworth_2nd(fc, fs, filter_type):
    """Butterworth 2nd-order (Q=1/sqrt(2)) lowpass or highpass."""
    w0 = 2 * math.pi * fc / fs
    Q = 1.0 / math.sqrt(2.0)
    alpha = math.sin(w0) / (2 * Q)
    cos_w0 = math.cos(w0)
    a0 = 1 + alpha

    if filter_type == 'LP':
        b0 = (1 - cos_w0) / 2 / a0
        b1 = (1 - cos_w0) / a0
        b2 = (1 - cos_w0) / 2 / a0
    elif filter_type == 'HP':
        b0 = (1 + cos_w0) / 2 / a0
        b1 = -(1 + cos_w0) / a0
        b2 = (1 + cos_w0) / 2 / a0
    else:
        raise ValueError(f"Unknown filter type: {filter_type}")

    a1 = -2 * cos_w0 / a0
    a2 = (1 - alpha) / a0
    return b0, b1, b2, a1, a2


def high_shelf(fc, fs, gain_db):
    """High shelf filter (Bristow-Johnson Audio EQ Cookbook)."""
    A = 10**(gain_db / 40.0)
    w0 = 2 * math.pi * fc / fs
    alpha = math.sin(w0) / (2 * 0.707)
    cos_w0 = math.cos(w0)

    a0 = (A + 1) - (A - 1) * cos_w0 + 2 * math.sqrt(A) * alpha
    b0 = (A * ((A + 1) + (A - 1) * cos_w0 + 2 * math.sqrt(A) * alpha)) / a0
    b1 = (-2 * A * ((A - 1) + (A + 1) * cos_w0)) / a0
    b2 = (A * ((A + 1) + (A - 1) * cos_w0 - 2 * math.sqrt(A) * alpha)) / a0
    a1 = (2 * ((A - 1) - (A + 1) * cos_w0)) / a0
    a2 = ((A + 1) - (A - 1) * cos_w0 - 2 * math.sqrt(A) * alpha) / a0
    return b0, b1, b2, a1, a2


def gain_biquad(db):
    """Pure gain biquad (no filtering, just level change)."""
    g = 10**(db / 20.0)
    return g, 0.0, 0.0, 0.0, 0.0


def allpass():
    """Unity allpass (bypass) — TAS5805M default is 0x08000000."""
    return 1.0, 0.0, 0.0, 0.0, 0.0


def compute_all(fs):
    """Compute all filter coefficients for given sample rate."""
    filters = {
        'HP_BW2_2500': butterworth_2nd(2500, fs, 'HP'),
        'LP_BW2_2500': butterworth_2nd(2500, fs, 'LP'),
        'Subsonic_HP_35': butterworth_2nd(35, fs, 'HP'),
        'BSC_300_neg3dB': high_shelf(300, fs, -3.0),
        'Trim_neg0.5dB': gain_biquad(-0.5),
        'Allpass': allpass(),
    }

    result = {}
    for name, (b0, b1, b2, a1, a2) in filters.items():
        nvs = coeffs_to_nvs(b0, b1, b2, a1, a2)
        result[name] = {
            'floats': {'b0': b0, 'b1': b1, 'b2': b2, 'a1': a1, 'a2': a2},
            'nvs': nvs,
        }
    return result


def print_all(fs, coeffs):
    """Pretty-print all coefficients."""
    print(f"\n{'='*60}")
    print(f"  baboo v6 — TAS5805M Coefficients @ {fs}Hz")
    print(f"  Format: 5.27 fixed-point, a1/a2 sign-inverted")
    print(f"{'='*60}\n")

    for name, data in coeffs.items():
        f = data['floats']
        nvs = data['nvs']
        print(f"  {name}:")
        print(f"    b0={f['b0']:+.10f}  b1={f['b1']:+.10f}  b2={f['b2']:+.10f}")
        print(f"    a1={f['a1']:+.10f}  a2={f['a2']:+.10f}")
        print(f"    NVS: [{','.join(str(b) for b in nvs)}]")
        print()


# ── Biquad slot allocation ──
SLOT_MAP = """
  TWEETER (Left channel TAS5805M = I2S R = physical cable L):
    BQ1  HP BW2 @2500Hz #1    Page 0x24 (36)  reg 0x18 (24)
    BQ2  HP BW2 @2500Hz #2    Page 0x24 (36)  reg 0x2C (44)
    BQ3  BSC @300Hz -3dB      Page 0x24 (36)  reg 0x40 (64)
    BQ4  Trim -0.5dB          Page 0x24 (36)  reg 0x54 (84)
    BQ5-15 Allpass             (default, not written)

  WOOFER (Right channel TAS5805M = I2S L = physical cable R):
    BQ1  LP BW2 @2500Hz #1    Page 0x26 (38)  reg 0x54 (84)
    BQ2  LP BW2 @2500Hz #2    Page 0x26 (38)  reg 0x68 (104)
    BQ3  Subsonic HPF @35Hz   Page 0x26 (38)  reg 0x7C (124)
    BQ4  BSC @300Hz -3dB      Page 0x27 (39)  reg 0x18 (24)
    BQ5-15 Allpass             (default, not written)
"""


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='baboo v6 coefficient calculator')
    parser.add_argument('--fs', type=int, default=44100, help='Sample rate (default: 44100)')
    parser.add_argument('--json', action='store_true', help='Output as JSON')
    args = parser.parse_args()

    coeffs = compute_all(args.fs)

    if args.json:
        output = {}
        for name, data in coeffs.items():
            output[name] = data['nvs']
        print(json.dumps(output, indent=2))
    else:
        print_all(args.fs, coeffs)
        print("  SLOT ALLOCATION:")
        print(SLOT_MAP)
