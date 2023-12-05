/*
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2015, triode1@btinternet.com
 *      Ralph Irving 2015-2017, ralph_irving@hotmail.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Additions (c) Paul Hermann, 2015-2017 under the same license terms
 *   -Control of Raspberry pi GPIO for amplifier power
 *   -Launch script on power status change from LMS
 */

#include "squeezelite.h"
#include <signal.h>
#include "Configurator.h"

extern bool user_rates;
static unsigned int rates[MAX_SUPPORTED_SAMPLERATES] = {0};
sys_Squeezelite* config;
log_level loglevel = lDEBUG;
static void sighandler(int signum) {
    slimproto_stop();

    // remove ourselves in case above does not work, second SIGINT will cause non gracefull shutdown
    signal(signum, SIG_DFL);
}

unsigned int* get_rates() {
    unsigned int ref[]  TEST_RATES;
    sys_RatesOption* ratescfg = &config->rates;
    if (!config->has_rates || ((ratescfg->list_count == 0 || ratescfg->list[0] == 0) &&
                                  ratescfg->min == 0 && ratescfg->max == 0)) {
        user_rates = false;
        return rates;
    }

    if (ratescfg->list_count > 0 && ratescfg->list[0] != 0) {
        // Sort the rates from the list
        for (int i = 0; i < ratescfg->list_count && i < MAX_SUPPORTED_SAMPLERATES; ++i) {
            rates[i] = ratescfg->list[i];
        }
        // Sort logic here if needed
    } else {
        // Use min and max to determine rates
        unsigned int min = ratescfg->min;
        unsigned int max = ratescfg->max;
        if (max < min) {
            unsigned int tmp = max;
            max = min;
            min = tmp;
        }
        for (int i = 0, j = 0; i < MAX_SUPPORTED_SAMPLERATES; ++i) {
            if (ref[i] <= max && ref[i] >= min) {
                rates[j++] = ref[i];
            }
        }
    }

    user_rates = true;
    return rates;
}
log_level log_level_from_sys_level(sys_DebugLevelEnum level) {
    switch (level) {
    case sys_DebugLevelEnum_DEFAULT:
        return lWARN;
        break;
    case sys_DebugLevelEnum_INFO:
        return lINFO;
        break;
    case sys_DebugLevelEnum_ERROR:
        return lERROR;
        break;
    case sys_DebugLevelEnum_WARN:
        return lWARN;
        break;
    case sys_DebugLevelEnum_DEBUG:
        return lDEBUG;
        break;
    case sys_DebugLevelEnum_SDEBUG:
        return lSDEBUG;
        break;
    default:
        return lWARN;
    }
}
void build_codec_string(sys_CodexEnum* list, size_t count, char* buffer, size_t buf_size) {
    const char* prefix = STR(sys_CodexEnum) "_c_";
    const char* name = NULL;
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            strncat(buffer, ", ", buf_size);
        }
        name = sys_CodexEnum_name(list[i]) + strlen(prefix);
        LOG_INFO("Found codec: %s ", name);
        strncat(buffer, name, buf_size);
    }
    LOG_INFO("Codec list: %s ", buffer);
}
int squeezelite_main_start() {
    u8_t mac[6];
    unsigned output_buf_size = 0;
    char include_codecs[101] = {0};
    char exclude_codecs[101] = {0};
    config = platform->has_services && platform->services.has_squeezelite
                 ? &platform->services.squeezelite
                 : NULL;
    if (!config) {
        LOG_ERROR("Squeezelite not configured");
        return -1;
    }

    int err = embedded_init();
    if (err) return err;
    get_mac(mac);
    unsigned int * rates = get_rates();

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
    #if defined(SIGQUIT)
    	signal(SIGQUIT, sighandler);
    #endif
    #if defined(SIGHUP)
    	signal(SIGHUP, sighandler);
    #endif
    output_buf_size = config->buffers.output;

    // set the output buffer size if not specified on the command line, take account of resampling
    if (!output_buf_size) {
        output_buf_size = OUTPUTBUF_SIZE;
        if (strlen(config->resample) > 0) {
            unsigned scale = 8;
            if (rates[0]) {
                scale = rates[0] / 44100;
                if (scale > 8) scale = 8;
                if (scale < 1) scale = 1;
            }
            output_buf_size *= scale;
        }
    }
    build_codec_string(config->excluded_codex, config->excluded_codex_count, exclude_codecs,
        sizeof(exclude_codecs));
    build_codec_string(
        config->included_codex, config->included_codex, include_codecs, sizeof(include_codecs));

    unsigned int stream_buf_size =
        config->buffers.stream > 0 ? config->buffers.stream : STREAMBUF_SIZE;
    stream_init(
        log_level_from_sys_level(platform->services.squeezelite.log.stream), stream_buf_size);
    output_init_embedded();
    decode_init(log_level_from_sys_level(platform->services.squeezelite.log.decode), include_codecs,
        exclude_codecs);

#if RESAMPLE || RESAMPLE16
    if (strlen(config->resample) > 0) {
        process_init(config->resample);
    }
#endif

    if (!config->enabled) {
        LOG_ERROR("LMS is disabled");
        while (1)
            sleep(3600);
    }
    char* name = strlen(platform->names.squeezelite) > 0 ? platform->names.squeezelite
                                                         : platform->names.device;
    slimproto(log_level_from_sys_level(platform->services.squeezelite.log.slimproto),
        config->server_name_ip, mac, name, NULL, NULL, config->max_rate);

    decode_close();
    stream_close();
    output_close_embedded();
    return (0);
}
