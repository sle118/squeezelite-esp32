# TAS5805M 2-Way Active Crossover for Squeezelite-ESP32

A complete hardware DSP crossover implementation for the TAS5805M Class-D amplifier,
designed for 2-way active mono speakers running on the Louder ESP32 board.

No firmware modifications required — the crossover is configured entirely via the
NVS `dac_controlset` JSON parameter.

## Features

- **LR4 crossover** (Linkwitz-Riley 4th order, 24dB/oct) at 2500Hz
- **Mono downmix** via TAS5805M hardware mixer
- **Per-channel biquad EQ** using BIAMP mode (15 biquads per channel)
- **Subsonic protection** (HPF @35Hz on woofer)
- **Baffle step compensation** (high-shelf @300Hz, -3dB)
- **Tweeter level trim** (-0.5dB)
- **Pre-computed coefficients** for 44.1kHz and 48kHz sample rates
- **Zero CPU overhead** — all DSP runs in the TAS5805M hardware

## Hardware

- **Board:** [Sonocotta Louder ESP32](https://github.com/sonocotta/louder-esp32) (ESP32-WROVER + TAS5805M)
- **Woofer:** Dayton Audio ND65-4 (4Ω, 2.5" full-range)
- **Tweeter:** SB Acoustics SB65WBAC25-4 (4Ω, 2.5" wide-band)
- **Enclosure:** ~1.8L sealed, 100mm baffle width

## Quick Start

1. Flash stock [squeezelite-esp32](https://github.com/sle118/squeezelite-esp32) firmware
2. Configure DAC: `model=I2S,bck=26,ws=25,do=22,sda=21,scl=27,i2c=45`
3. Set GPIO: `33=vcc`
4. Copy the NVS controlset from [`nvs/44100hz.json`](nvs/44100hz.json) into `dac_controlset`
5. Reboot — crossover is active

## How It Works

### Signal Chain

```
I2S Input → Mono Mixer (L+R)/2 → Biquad EQ ×15/ch → DRC → Volume → Class-D Output
                                        │
                  Left ch (TAS5805M):  HP LR4 @2500Hz + BSC + Trim  → Tweeter
                  Right ch (TAS5805M): LP LR4 @2500Hz + Subsonic + BSC → Woofer
```

### TAS5805M Configuration

| Parameter     | Register | Value | Description                          |
|---------------|----------|-------|--------------------------------------|
| BIAMP mode    | 0x66     | 0x8E  | Independent 15 biquads per channel   |
| Mono mixer    | Book 0x8C, Page 0x29 | See NVS | (L+R)/2 on both channels |
| Biquad coeffs | Book 0xAA | See NVS | 5.27 fixed-point, a1/a2 inverted  |

### I2S Channel Mapping (Louder ESP32)

| I2S Channel | TAS5805M | Speaker | Crossover |
|-------------|----------|---------|-----------|
| Left        | CHB      | Tweeter | High-pass |
| Right       | CHA      | Woofer  | Low-pass  |

> **Note:** The Louder ESP32 board maps I2S L→CHB and I2S R→CHA.
> This is handled in the NVS configuration — no code changes needed.

## Biquad Slot Allocation

### Tweeter (Left channel, Book 0xAA Pages 0x24-0x26)

| Slot | Filter           | Type       | Page | Reg  |
|------|------------------|------------|------|------|
| BQ1  | HP BW2 @2500Hz   | Highpass   | 0x24 | 0x18 |
| BQ2  | HP BW2 @2500Hz   | Highpass   | 0x24 | 0x2C |
| BQ3  | BSC @300Hz -3dB  | High-shelf | 0x24 | 0x40 |
| BQ4  | Trim -0.5dB      | Gain       | 0x24 | 0x54 |
| BQ5-15 | (unused)       | Bypass     |      |      |

### Woofer (Right channel, Book 0xAA Pages 0x26-0x29)

| Slot | Filter           | Type       | Page | Reg  |
|------|------------------|------------|------|------|
| BQ1  | LP BW2 @2500Hz   | Lowpass    | 0x26 | 0x54 |
| BQ2  | LP BW2 @2500Hz   | Lowpass    | 0x26 | 0x68 |
| BQ3  | Subsonic @35Hz   | Highpass   | 0x26 | 0x7C |
| BQ4  | BSC @300Hz -3dB  | High-shelf | 0x27 | 0x18 |
| BQ5-15 | (unused)       | Bypass     |      |      |

## Technical Reference

### Book 0xAA Biquad Format

Each biquad is 20 bytes (5 coefficients × 4 bytes):

```
[B0_3, B0_2, B0_1, B0_0,   ← b0 coefficient
 B1_3, B1_2, B1_1, B1_0,   ← b1 coefficient
 B2_3, B2_2, B2_1, B2_0,   ← b2 coefficient
 A1_3, A1_2, A1_1, A1_0,   ← -a1 (sign-inverted!)
 A2_3, A2_2, A2_1, A2_0]   ← -a2 (sign-inverted!)
```

- **Format:** 5.27 signed fixed-point (coefficient × 2²⁷)
- **Sign convention:** a1 and a2 are written with inverted sign
- **Unity bypass (allpass):** `[0x08,0x00,0x00,0x00, 0x00,...,0x00]` (b0=1.0, rest=0)

### Coefficient Tables

See [`nvs/coefficients.py`](nvs/coefficients.py) to generate coefficients for custom
crossover frequencies or sample rates.

#### 44100 Hz

| Filter          | Bytes                                                                     |
|-----------------|---------------------------------------------------------------------------|
| HP BW2 @2500Hz  | 6,55,88,63,243,145,79,130,6,55,88,63,12,7,145,108,251,42,48,112         |
| LP BW2 @2500Hz  | 0,51,143,137,0,103,31,18,0,51,143,137,12,7,145,108,251,42,48,112        |
| Subsonic @35Hz  | 7,248,202,145,240,14,106,223,7,248,202,145,15,241,142,162,248,14,100,96 |
| BSC @300Hz -3dB | 5,177,119,244,244,253,22,5,5,84,130,38,15,142,116,33,248,110,123,191    |
| Trim -0.5dB     | 7,141,111,202,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0                          |

#### 48000 Hz

| Filter          | Bytes                                                                     |
|-----------------|---------------------------------------------------------------------------|
| HP BW2 @2500Hz  | 6,88,111,110,243,79,33,36,6,88,111,110,12,88,79,228,250,246,146,45      |
| LP BW2 @2500Hz  | 0,44,71,124,0,88,142,247,0,44,71,124,12,88,79,228,250,246,146,45        |
| Subsonic @35Hz  | 7,249,96,68,240,13,63,121,7,249,96,68,15,242,187,11,248,13,57,253       |
| BSC @300Hz -3dB | 5,176,217,171,244,246,125,9,5,91,64,163,15,151,172,249,248,101,187,175  |
| Trim -0.5dB     | 7,141,111,202,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0                          |

## Software Crossover (Alternative)

If you prefer a CPU-based crossover instead of TAS5805M hardware DSP, see
[`software/`](software/) for a float32 biquad implementation that runs in the
ESP32's `output_i2s` thread. This approach works with any DAC (not just TAS5805M)
but uses ~10% CPU.

## License

This crossover configuration and documentation is provided under the MIT License.
The base firmware is [squeezelite-esp32](https://github.com/sle118/squeezelite-esp32)
by sle118, also MIT licensed.
