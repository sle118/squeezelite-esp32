# Software Crossover (Alternative to TAS5805M Hardware DSP)

This is a CPU-based LR4 crossover implementation that runs in the ESP32's audio output
thread. It works with **any** I2S DAC, not just the TAS5805M.

## When to Use This

- Your DAC doesn't have hardware biquad support
- Book 0xAA is inaccessible on your board
- You want real-time filter parameter changes without reboot

## Trade-offs vs Hardware DSP

| Aspect          | Hardware (TAS5805M) | Software (ESP32)     |
|-----------------|--------------------|-----------------------|
| CPU usage       | 0%                 | ~10% of one core      |
| Latency         | Zero (DSP pipeline)| One buffer (~23ms)    |
| Filter quality  | 5.27 fixed-point   | 32-bit float          |
| Flexibility     | NVS reboot needed  | Runtime changeable    |
| DAC requirement | TAS5805M only      | Any I2S DAC           |

## Files

- `sw_crossover.c` — Float32 biquad IIR implementation (Direct Form II Transposed)
- `sw_crossover.h` — Public API: `init(sample_rate)` and `process(buf, frames)`

## Integration

To use, add to `output_i2s.c` after `equalizer_process()`:

```c
#include "sw_crossover.h"

// In sample rate change handler:
sw_crossover_init(output.current_sample_rate);

// In audio processing loop, after equalizer:
sw_crossover_process(obuf, oframes);
```

Add the .c file to your component's CMakeLists.txt source list.
