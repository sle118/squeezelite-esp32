/*
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2024
 *      Philippe G. 2024, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
#include "Config.h"
#include "squeezelite.h"
#include <signal.h>

extern bool user_rates;
static unsigned int rates[MAX_SUPPORTED_SAMPLERATES] = {0};
sys_squeezelite_config* config;
log_level loglevel = lDEBUG;
static void sighandler(int signum) {
    slimproto_stop();

    // remove ourselves in case above does not work, second SIGINT will cause non gracefull shutdown
    signal(signum, SIG_DFL);
}

unsigned int* get_rates() {
    unsigned int ref[] TEST_RATES;
    sys_squeezelite_rates_opt* ratescfg = &config->rates;
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
log_level log_level_from_sys_level(sys_squeezelite_debug_levels level) {
    switch (level) {
    case sys_squeezelite_debug_levels_DEFAULT:
        return lWARN;
        break;
    case sys_squeezelite_debug_levels_INFO:
        return lINFO;
        break;
    case sys_squeezelite_debug_levels_ERROR:
        return lERROR;
        break;
    case sys_squeezelite_debug_levels_WARN:
        return lWARN;
        break;
    case sys_squeezelite_debug_levels_DEBUG:
        return lDEBUG;
        break;
    case sys_squeezelite_debug_levels_SDEBUG:
        return lSDEBUG;
        break;
    default:
        return lWARN;
    }
}
void build_codec_string(sys_squeezelite_codecs* list, size_t count, char* buffer, size_t buf_size) {
    const char* prefix = STR(sys_squeezelite_codecs) "_c_";
    const char* name = NULL;
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            strncat(buffer, ", ", buf_size);
        }
        name = sys_squeezelite_codecs_name(list[i]) + strlen(prefix);
        LOG_INFO("Found codec: %s ", name);
        strncat(buffer, name, buf_size);
    }
    LOG_INFO("Codec list: %s ", buffer);
}

sys_squeezelite_config* get_profile(const char* name) {
    const char* resolved_name = name && strlen(name) > 0 ? name : platform->services.current_profile;
    LOG_DEBUG("get_profile called with name: %s, resolved to: %s", 
              name ? name : "NULL", 
              resolved_name ? resolved_name : "NULL or EMPTY");

    if (!platform->has_services || platform->services.squeezelite_profiles_count <= 0) {
        LOG_ERROR("No squeezelite profiles available");
        return NULL;
    }

    if (!resolved_name || strlen(resolved_name) == 0) {
        LOG_WARN("No specific profile requested, using the first available profile");
        return &platform->services.squeezelite_profiles[0].profile;
    }

    for (int i = 0; i < platform->services.squeezelite_profiles_count; i++) {
        LOG_DEBUG("Checking profile: %s", platform->services.squeezelite_profiles[i].name);
        if (strcmp(platform->services.squeezelite_profiles[i].name, resolved_name) == 0) {
            LOG_DEBUG("Profile matched: %s", platform->services.squeezelite_profiles[i].name);
            return &platform->services.squeezelite_profiles[i].profile;
        }
    }
    if(platform->services.squeezelite_profiles[0].name && strlen(platform->services.squeezelite_profiles[0].name)>0){
        LOG_WARN("Could not find profile %s. Falling back to %s", resolved_name,platform->services.squeezelite_profiles[0].name);
        return &platform->services.squeezelite_profiles[0].profile;
    }

    LOG_ERROR("Profile '%s' not found", resolved_name);
    return NULL;
}

int squeezelite_main_start() {
    u8_t mac[6];
    unsigned output_buf_size = 0;
    LOG_INFO("Starting squeezelite");
    char include_codecs[101] = {0};
    char exclude_codecs[101] = {0};
    config = get_profile(NULL); // get the active profile
    if (!config) {
        LOG_ERROR("Squeezelite not configured");
        return -2;
    }
    LOG_INFO("Running embedded init");
    int err = embedded_init();
    if (err) return err;
    get_mac(mac);
    unsigned int* rates = get_rates();

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
#if defined(SIGQUIT)
    signal(SIGQUIT, sighandler);
#endif
#if defined(SIGHUP)
    signal(SIGHUP, sighandler);
#endif
    output_buf_size = config->buffers.output*1024;

    // set the output buffer size if not specified in the parameters, take account of resampling
    if (output_buf_size == 0) {
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
    build_codec_string(config->included_codex, config->included_codex_count, include_codecs,
        sizeof(include_codecs));

    unsigned int stream_buf_size =STREAMBUF_SIZE;
    if(config->buffers.stream > 0){
        stream_buf_size = config->buffers.stream *1024;
        LOG_DEBUG("Found stream buffer configuration: %d (%d)",config->buffers.stream,stream_buf_size);
    }
    else {
        LOG_DEBUG("Using default stream buffer: %d",stream_buf_size);
    }
    stream_init(log_level_from_sys_level(config->log.stream), stream_buf_size);
    output_init_embedded();
    decode_init(log_level_from_sys_level(config->log.decode),
        strlen(include_codecs) > 0 ? include_codecs : NULL, exclude_codecs);

#if RESAMPLE || RESAMPLE16
    if (config->resample && strlen(config->resample) > 0) {
        process_init(config->resample);
    }
#endif

    if (!config->enabled) {
        LOG_WARN("LMS is disabled");
        while (1)
            sleep(3600);
    }
    char* name = strlen(platform->names.squeezelite) > 0 ? platform->names.squeezelite
                                                         : platform->names.device;
    slimproto(log_level_from_sys_level(config->log.slimproto), config->server_name_ip, mac, name,
        NULL, NULL, config->max_rate);

    decode_close();
    stream_close();
    output_close_embedded();
    return (0);
}
