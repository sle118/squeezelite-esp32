/*
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
#include "squeezelite.h"
#include "equalizer.h"
#include "Configurator.h"
extern log_level log_level_from_sys_level(sys_DebugLevelEnum level);
static sys_Squeezelite* config = NULL;


extern unsigned int* get_rates() ;
extern struct outputstate output;
extern struct buffer* outputbuf;

static bool (*slimp_handler_chain)(u8_t* data, int len);

#define FRAME_BLOCK MAX_SILENCE_FRAMES

#define LOCK mutex_lock(outputbuf->mutex)
#define UNLOCK mutex_unlock(outputbuf->mutex)

// output_bt.c
extern void output_init_bt(unsigned rates[]);
extern void output_close_bt(void);

// output_i2s.c
extern void output_init_i2s(unsigned rates[]);
extern bool output_volume_i2s(unsigned left, unsigned right);
extern void output_close_i2s(void);

// controls.c
extern void cli_controls_init(void);

static log_level loglevel;

static bool (*volume_cb)(unsigned left, unsigned right);
static void (*close_cb)(void);

#pragma pack(push, 1)
struct eqlz_packet {
    char opcode[4];
};

struct loud_packet {
    char opcode[4];
    u8_t loudness;
};
#pragma pack(pop)

static bool handler(u8_t* data, int len) {
    bool res = true;

    if (!strncmp((char*)data, "eqlz", 4)) {
        s8_t* gain = (s8_t*)(data + sizeof(struct eqlz_packet));
        // update will be done at next opportunity
        equalizer_set_gain(gain);
    } else if (!strncmp((char*)data, "loud", 4)) {
        struct loud_packet* packet = (struct loud_packet*)data;
        // update will be done at next opportunity
        equalizer_set_loudness(packet->loudness);
    } else {
        res = false;
    }

    // chain protocol handlers (bitwise or is fine)
    if (*slimp_handler_chain) res |= (*slimp_handler_chain)(data, len);

    return res;
}

void output_init_embedded() {
    config = &platform->services.squeezelite;
    loglevel = log_level_from_sys_level(config->log.output);
    LOG_INFO("init device: %s", sys_OutputTypeEnum_name(config->output_type));

    // chain handlers
    slimp_handler_chain = slimp_handler;
    slimp_handler = handler;

    // init equalizer before backends
    equalizer_init();
    memset(&output, 0, sizeof(output));

    output_init_common(loglevel, sys_OutputTypeEnum_name(config->output_type),
        config->buffers.output, get_rates(), config->amp_gpio_timeout);
    output.start_frames = FRAME_BLOCK;
    #pragma message("Rate delay logic incomplete")
	output.rate_delay = 0;

#if CONFIG_BT_SINK
    if (config->output_type == sys_OutputTypeEnum_OUTPUT_Bluetooth) {
        LOG_INFO("init Bluetooth");
        close_cb = &output_close_bt;
        output_init_bt(get_rates());
    } else
#endif
   	{
        close_cb = &output_close_i2s;
        volume_cb = &output_volume_i2s;
        output_init_i2s(get_rates());
    }

    output_visu_init(loglevel);

    LOG_INFO("init completed.");
}

void output_close_embedded(void) {
    LOG_INFO("close output");
    if (close_cb) (*close_cb)();
    output_close_common();
    output_visu_close();
}

void set_volume(unsigned left, unsigned right) {
    LOG_DEBUG("setting internal gain left: %u right: %u", left, right);
    if (!volume_cb || !(*volume_cb)(left, right)) {
        LOCK;
        output.gainL = left;
        output.gainR = right;
        UNLOCK;
    }
    equalizer_set_volume(left, right);
}

bool test_open(const char* device, unsigned rates[], bool userdef_rates) {
    memset(rates, 0, MAX_SUPPORTED_SAMPLERATES * sizeof(unsigned));
    if (config->output_type == sys_OutputTypeEnum_OUTPUT_I2S) {
        unsigned _rates[] = {
#if BYTES_PER_FRAME == 4
            192000,
            176400,
#endif
            96000,
            88200,
            48000,
            44100,
            32000,
            24000,
            22050,
            16000,
            12000,
            11025,
            8000,
            0
        };
        memcpy(rates, _rates, sizeof(_rates));
    } else if (config->output_type == sys_OutputTypeEnum_OUTPUT_SPDIF) {
        unsigned _rates[] = {
            96000, 88200, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 0};
        memcpy(rates, _rates, sizeof(_rates));
    } else {
        rates[0] = 44100;
    }
    return true;
}

char* output_state_str(void) {
    output_state state;
    LOCK;
    state = output.state;
    UNLOCK;
    switch (state) {
    case OUTPUT_OFF:
        return STR(OUTPUT_OFF);
    case OUTPUT_STOPPED:
        return STR(OUTPUT_STOPPED);
    case OUTPUT_BUFFER:
        return STR(OUTPUT_BUFFER);
    case OUTPUT_RUNNING:
        return STR(OUTPUT_RUNNING);
    case OUTPUT_PAUSE_FRAMES:
        return STR(OUTPUT_PAUSE_FRAMES);
    case OUTPUT_SKIP_FRAMES:
        return STR(OUTPUT_SKIP_FRAMES);
    case OUTPUT_START_AT:
        return STR(OUTPUT_START_AT);
    default:
        return "OUTPUT_UNKNOWN_STATE";
    }
}

bool output_stopped(void) {
    output_state state;
    LOCK;
    state = output.state;
    UNLOCK;
    return state <= OUTPUT_STOPPED;
}
