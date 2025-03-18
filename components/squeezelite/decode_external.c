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

#include <math.h>
#ifdef ESP_PLATFORM
#include "Services.pb.h"
#include "Squeezelite.pb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
extern sys_squeezelite_config* get_profile(const char* name);
#endif
#include "Config.h"
#include "accessors.h"
#include "squeezelite.h"

#if CONFIG_BT_SINK
#include "bt_app_sink.h"
static bool enable_bt_sink;
#endif

#if CONFIG_CSPOT_SINK
#include "cspot_sink.h"
static bool enable_cspot;
#endif

#if CONFIG_AIRPLAY_SINK
#include "raop_sink.h"
static bool enable_airplay;

#define RAOP_OUTPUT_SIZE (((RAOP_SAMPLE_RATE * BYTES_PER_FRAME * 2 * 120) / 100) & ~BYTES_PER_FRAME)
#define SYNC_WIN_SLOW 32
#define SYNC_WIN_CHECK 8
#define SYNC_WIN_FAST 2

static raop_event_t raop_state;
static sys_squeezelite_config* squeezelite;

static EXT_RAM_ATTR struct {
    bool enabled;
    int sum, count, win, errors[SYNC_WIN_SLOW];
    s32_t len;
    u32_t start_time, playtime;
} raop_sync;
#endif

static enum { SINK_RUNNING, SINK_ABORT, SINK_DISCARD } sink_state;

#define LOCK_O mutex_lock(outputbuf->mutex)
#define UNLOCK_O mutex_unlock(outputbuf->mutex)
#define LOCK_D mutex_lock(decode.mutex);
#define UNLOCK_D mutex_unlock(decode.mutex);

enum { DECODE_BT = 1, DECODE_RAOP, DECODE_CSPOT };

extern struct outputstate output;
extern struct decodestate decode;
extern struct buffer* outputbuf;
// this is the only system-wide loglevel variable
extern log_level loglevel;

/****************************************************************************************
 * Common sink data handler
 */
static uint32_t sink_data_handler(const uint8_t* data, uint32_t len, int retries) {
    size_t bytes, space;
    uint32_t written = 0;
    int wait = retries + 1;

    // would be better to lock output, but really, it does not matter
    if (!output.external) {
        LOG_SDEBUG("Cannot use external sink while LMS is controlling player");
        return 0;
    }

    LOCK_O;
    if (sink_state == SINK_ABORT) sink_state = SINK_RUNNING;

    // there will always be room at some point
    while (len && wait && sink_state == SINK_RUNNING) {
        bytes = min(_buf_space(outputbuf), _buf_cont_write(outputbuf)) / (BYTES_PER_FRAME / 4);
        bytes = min(len, bytes);
#if BYTES_PER_FRAME == 4
        memcpy(outputbuf->writep, data, bytes);
#else
        {
            s16_t* iptr = (s16_t*)data;
            ISAMPLE_T* optr = (ISAMPLE_T*)outputbuf->writep;
            size_t n = bytes / 2;
            while (n--)
                *optr++ = *iptr++ << 16;
        }
#endif
        _buf_inc_writep(outputbuf, bytes * BYTES_PER_FRAME / 4);
        space = _buf_space(outputbuf);

        len -= bytes;
        data += bytes;
        written += bytes;

        // allow i2s to empty the buffer if needed
        if (len && !space) {
            if (!retries) break;
            wait--;
            UNLOCK_O;
            usleep(50000);
            LOCK_O;
        }
    }

    if (!wait) {
        // re-align the buffer according to what we threw away
        _buf_inc_writep(outputbuf, outputbuf->size - (BYTES_PER_FRAME - (len % BYTES_PER_FRAME)));
        LOG_WARN("Waited too long, dropping frames %d", len);
    }

    UNLOCK_O;

    return written;
}

/****************************************************************************************
 * BT sink data handler
 */
#if CONFIG_BT_SINK
static void bt_sink_data_handler(const uint8_t* data, uint32_t len) {
    sink_data_handler(data, len, 10);
}

/****************************************************************************************
 * BT sink command handler
 */
static bool bt_sink_cmd_handler(bt_sink_cmd_t cmd, va_list args) {
    // don't LOCK_O as there is always a chance that LMS takes control later anyway
    if (output.external != DECODE_BT && output.state > OUTPUT_STOPPED) {
        LOG_WARN("Cannot use BT sink while LMS/AirPlay/CSpot are controlling player %d",
            output.external);
        return false;
    }

    LOCK_D;

    if (cmd != BT_SINK_VOLUME) LOCK_O;

    switch (cmd) {
    case BT_SINK_AUDIO_STARTED:
        _buf_flush(outputbuf);
        _buf_limit(outputbuf, 0);
        output.next_sample_rate = output.current_sample_rate = va_arg(args, u32_t);
        output.external = DECODE_BT;
        output.state = OUTPUT_STOPPED;
        output.frames_played = 0;
        if (decode.state != DECODE_STOPPED) decode.state = DECODE_ERROR;
        LOG_INFO("BT sink started");
        break;
    case BT_SINK_AUDIO_STOPPED:
        if (output.external == DECODE_BT) {
            if (output.state > OUTPUT_STOPPED) output.state = OUTPUT_STOPPED;
            output.external = 0;
            output.stop_time = gettime_ms();
            LOG_INFO("BT sink stopped");
        }
        break;
    case BT_SINK_PLAY:
        output.state = OUTPUT_RUNNING;
        LOG_INFO("BT play");
        break;
    case BT_SINK_STOP:
        _buf_flush(outputbuf);
        output.state = OUTPUT_STOPPED;
        output.stop_time = gettime_ms();
        sink_state = SINK_ABORT;
        LOG_INFO("BT stop");
        break;
    case BT_SINK_PAUSE:
        output.stop_time = gettime_ms();
        LOG_INFO("BT pause, just silence");
        break;
    case BT_SINK_RATE:
        output.next_sample_rate = output.current_sample_rate = va_arg(args, u32_t);
        LOG_INFO("Setting BT sample rate %u", output.next_sample_rate);
        break;
    case BT_SINK_VOLUME: {
        u32_t volume = va_arg(args, u32_t);
        volume = 65536 * powf(volume / 128.0f, 3);
        set_volume(volume, volume);
        break;
    default:
        break;
    }
    }

    if (cmd != BT_SINK_VOLUME) UNLOCK_O;
    UNLOCK_D;

    return true;
}
#endif

/****************************************************************************************
 * raop sink data handler
 */
#if CONFIG_AIRPLAY_SINK
static void raop_sink_data_handler(const uint8_t* data, uint32_t len, u32_t playtime) {

    raop_sync.playtime = playtime;
    raop_sync.len = len;

    sink_data_handler(data, len, 10);
}

/****************************************************************************************
 * AirPlay sink command handler
 */
static bool raop_sink_cmd_handler(raop_event_t event, va_list args) {
    // don't LOCK_O as there is always a chance that LMS takes control later anyway
    if (output.external != DECODE_RAOP && output.state > OUTPUT_STOPPED) {
        LOG_WARN("Cannot use Airplay sink while LMS/BT/CSpot are controlling player %d",
            output.external);
        return false;
    }

    LOCK_D;

    if (event != RAOP_VOLUME) LOCK_O;

    // this is async, so player might have been deleted
    switch (event) {
    case RAOP_TIMING: {
        if (!raop_sync.enabled || output.state != OUTPUT_RUNNING ||
            output.frames_played_dmp < output.device_frames)
            break;

        u32_t ms, now = gettime_ms();
        u32_t level = _buf_used(outputbuf);
        int error;

        // in how many ms will the most recent block play
        ms = (((s32_t)(level - raop_sync.len) / BYTES_PER_FRAME + output.device_frames +
                  output.frames_in_process) *
                 10) /
                 (RAOP_SAMPLE_RATE / 100) -
             (s32_t)(now - output.updated);

        // when outputbuf is empty, it means we have a network black-out or something
        error = level ? (raop_sync.playtime - now) - ms : 0;

        if (loglevel == lDEBUG || !level) {
            LOG_INFO("head local:%d, remote:%d (delta:%d)", ms, raop_sync.playtime - now, error);
            LOG_INFO("obuf:%u, sync_len:%u, devframes:%u, inproc:%u", _buf_used(outputbuf),
                raop_sync.len, output.device_frames, output.frames_in_process);
        }

        // calculate sum, error and update sliding window
        raop_sync.errors[raop_sync.count++ % raop_sync.win] = error;
        raop_sync.sum += error;
        error = raop_sync.sum / min(raop_sync.count, raop_sync.win);

        // wait till we have enough data or there is a strong deviation
        if ((raop_sync.count >= raop_sync.win && abs(error) > 10) ||
            (raop_sync.count >= SYNC_WIN_CHECK && abs(error) > 100)) {
            if (error < 0) {
                output.skip_frames = -(error * RAOP_SAMPLE_RATE) / 1000;
                output.state = OUTPUT_SKIP_FRAMES;
                LOG_INFO("skipping %u frames (count:%d)", output.skip_frames, raop_sync.count);
            } else {
                output.pause_frames = (error * RAOP_SAMPLE_RATE) / 1000;
                output.state = OUTPUT_PAUSE_FRAMES;
                LOG_INFO("pausing for %u frames (count: %d)", output.pause_frames, raop_sync.count);
            }

            raop_sync.sum = raop_sync.count = 0;
            memset(raop_sync.errors, 0, sizeof(raop_sync.errors));
        }

        // move to normal mode if possible
        if (raop_sync.win == 1) {
            raop_sync.win = SYNC_WIN_FAST;
            LOG_INFO(
                "backend played %u, desired %u, (delta:%d)", ms, raop_sync.playtime - now, error);
        } else if (raop_sync.win == SYNC_WIN_FAST && raop_sync.count >= SYNC_WIN_FAST &&
                   abs(error) < 10) {
            raop_sync.win = SYNC_WIN_SLOW;
            LOG_INFO("switching to slow sync mode %u", raop_sync.win);
        }

        break;
    }
    case RAOP_SETUP: {
        uint8_t** buffer = va_arg(args, uint8_t**);
        size_t* size = va_arg(args, size_t*);

        // steal buffer tail from outputbuf but do not reallocate
        *size = _buf_limit(outputbuf, RAOP_OUTPUT_SIZE);
        *buffer = outputbuf->writep + RAOP_OUTPUT_SIZE;

        output.frames_played = 0;
        output.external = DECODE_RAOP;
        output.state = OUTPUT_STOPPED;
        if (decode.state != DECODE_STOPPED) decode.state = DECODE_ERROR;
        LOG_INFO("resizing buffer %u", outputbuf->size);
        break;
    }
    case RAOP_STREAM:
        LOG_INFO("Stream", NULL);
        raop_state = event;
        raop_sync.win = 1;
        raop_sync.sum = raop_sync.count = 0;
        memset(raop_sync.errors, 0, sizeof(raop_sync.errors));
        raop_sync.enabled = !strcasestr(output.device, "BT");
        output.next_sample_rate = output.current_sample_rate = RAOP_SAMPLE_RATE;
        break;
    case RAOP_STALLED:
    case RAOP_STOP:
        output.external = 0;
        __attribute__((fallthrough));
    case RAOP_FLUSH:
        LOG_INFO("%s", event == RAOP_FLUSH ? "Flush" : "Stop");
        _buf_flush(outputbuf);
        raop_state = event;
        if (output.state > OUTPUT_STOPPED) output.state = OUTPUT_STOPPED;
        sink_state = SINK_ABORT;
        output.frames_played = 0;
        output.stop_time = gettime_ms();
        break;
    case RAOP_PLAY: {
        LOG_INFO("Play", NULL);
        if (raop_state != RAOP_PLAY) {
            output.state = OUTPUT_START_AT;
            output.start_at = va_arg(args, u32_t);
            raop_sync.start_time = output.start_at;
            LOG_INFO("Starting at %u (in %d ms)", output.start_at, output.start_at - gettime_ms());
        }
        raop_state = event;
        break;
    }
    case RAOP_VOLUME: {
        float volume = va_arg(args, double);
        LOG_INFO("Volume[0..1] %0.4f", volume);
        volume = 65536 * powf(volume, 3);
        set_volume(volume, volume);
        break;
    }
    default:
        break;
    }

    if (event != RAOP_VOLUME) UNLOCK_O;

    UNLOCK_D;
    return true;
}
#endif

/****************************************************************************************
 * cspot sink data handler
 */
#if CONFIG_CSPOT_SINK
static uint32_t cspot_sink_data_handler(const uint8_t* data, uint32_t len) {
    return sink_data_handler(data, len, 0);
}

/****************************************************************************************
 * cspot sink command handler
 */

static bool cspot_cmd_handler(cspot_event_t cmd, va_list args) {
    // don't LOCK_O as there is always a chance that LMS takes control later anyway
    if (output.external != DECODE_CSPOT && output.state > OUTPUT_STOPPED) {
        LOG_WARN("Cannot use CSpot sink while LMS/BT/Airplay are controlling player %d",
            output.external);
        return false;
    }

    LOCK_D;

    if (cmd != CSPOT_VOLUME) LOCK_O;

    switch (cmd) {
    case CSPOT_START:
        output.current_sample_rate = output.next_sample_rate = va_arg(args, u32_t);
        output.external = DECODE_CSPOT;
        output.frames_played = 0;
        // in 1/10 of seconds
        output.threshold = 25;
        output.state = OUTPUT_STOPPED;
        sink_state = SINK_ABORT;
        _buf_flush(outputbuf);
        if (decode.state != DECODE_STOPPED) decode.state = DECODE_ERROR;
        LOG_INFO("CSpot start track");
        break;
    case CSPOT_DISC:
        _buf_flush(outputbuf);
        sink_state = SINK_ABORT;
        output.external = 0;
        output.state = OUTPUT_STOPPED;
        output.stop_time = gettime_ms();
        LOG_INFO("CSpot disconnected");
        break;
    case CSPOT_PLAY:
        sink_state = SINK_RUNNING;
        output.state = OUTPUT_RUNNING;
        LOG_INFO("CSpot play");
        break;
    case CSPOT_SEEK:
        _buf_flush(outputbuf);
        sink_state = SINK_ABORT;
        LOG_INFO("CSpot seek by %d", va_arg(args, uint32_t));
        break;
    case CSPOT_FLUSH:
        _buf_flush(outputbuf);
        sink_state = SINK_DISCARD;
        output.state = OUTPUT_STOPPED;
        LOG_INFO("CSpot flush");
        break;
    case CSPOT_PAUSE:
        output.state = OUTPUT_STOPPED;
        output.stop_time = gettime_ms();
        LOG_INFO("CSpot pause");
        break;
    case CSPOT_TRACK_MARK:
        output.track_start = outputbuf->writep;
        break;
    case CSPOT_QUERY_REMAINING: {
        uint32_t* remaining = va_arg(args, uint32_t*);
        *remaining = (_buf_used(outputbuf) * 1000) / (output.current_sample_rate * BYTES_PER_FRAME);
        break;
    }
    case CSPOT_QUERY_STARTED: {
        uint32_t* started = va_arg(args, uint32_t*);
        *started = output.track_started;
        // this is a read_and_clear event
        output.track_started = false;
        break;
    }
    case CSPOT_VOLUME: {
        u32_t volume = va_arg(args, u32_t);
        LOG_INFO("CSpot volume %u", volume);
        volume = 65536 * powf(volume / 65536.0f, 4);
        set_volume(volume, volume);
        break;
    default:
        break;
    }
    }

    if (cmd != CSPOT_VOLUME) UNLOCK_O;
    UNLOCK_D;

    return true;
}
#endif

/****************************************************************************************
 * We provide the generic codec register option
 */
void register_external(void) {
    squeezelite = get_profile(NULL); // get the active profile
#if CONFIG_BT_SINK
    sys_services_bt_sink* bt_sink;
    enable_bt_sink = (sys_services_config_BTSINK(bt_sink) && bt_sink->enabled);
    if (enable_bt_sink && squeezelite) {
#pragma message("Is the BT sink logic correct?")
        if (squeezelite->output_type == sys_squeezelite_outputs_BT) {
            LOG_ERROR("BT Sink cannot be enabled with Bluetooth output");
        } else {
            bt_sink_init(bt_sink_cmd_handler, bt_sink_data_handler);
        }
    }
#endif

#if CONFIG_AIRPLAY_SINK
    sys_airplay_config* airplay;
    enable_airplay = sys_services_config_AIRPLAY(airplay) && airplay->enabled;
    if (enable_airplay) {
        raop_sink_init(raop_sink_cmd_handler, raop_sink_data_handler);
        LOG_INFO("Initializing AirPlay sink");
    }

#endif

#if CONFIG_CSPOT_SINK
    sys_spotify_config* spotify;
    enable_cspot = sys_services_config_SPOTIFY(spotify) && spotify->enabled;
    if (enable_cspot) {
        cspot_sink_init(cspot_cmd_handler, cspot_sink_data_handler);
        LOG_INFO("Initializing CSpot sink");
    }
#endif
}

void deregister_external(void) {
#if CONFIG_BT_SINK
    squeezelite = get_profile(NULL); // get the active profile
    if (squeezelite && squeezelite->output_type == sys_squeezelite_outputs_BT && enable_bt_sink) {
        bt_sink_deinit();
    }
#endif

#if CONFIG_AIRPLAY_SINK
    if (enable_airplay) {
        LOG_INFO("Stopping AirPlay sink");
        raop_sink_deinit();
    }
#endif

#if CONFIG_CSPOT_SINK
    if (enable_cspot) {
        LOG_INFO("Stopping CSpot sink");
        cspot_sink_deinit();
    }
#endif
}

void decode_restore(int external) {
    switch (external) {
#if CONFIG_BT_SINK
    case DECODE_BT:
        bt_disconnect();
        break;
#endif
#if CONFIG_AIRPLAY_SINK
    case DECODE_RAOP:
        raop_disconnect();
        break;
#endif
#if CONFIG_CSPOT_SINK
    case DECODE_CSPOT:
        cspot_disconnect();
        break;
#endif
    }
}
