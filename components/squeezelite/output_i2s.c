/*
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
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
 */

/*
Synchronisation is a bit of a hack with i2s. The esp32 driver is always
full when it starts, so there is a delay of the total length of buffers.
In other words, i2w_write blocks at first call, until at least one buffer
has been written (it uses a queue with produce / consume).

The first hack is to consume that length at the beginning of tracks when
synchronization is active. It's about ~180ms @ 44.1kHz

The second hack is that we never know exactly the number of frames in the
DMA buffers when we update the output.frames_played_dmp. We assume that
after i2s_write, these buffers are always full so by measuring the gap
between time after i2s_write and update of frames_played_dmp, we have a
good idea of the error.

The third hack is when sample rate changes, buffers are reset and we also
do the change too early, but can't do that exaclty at the right time. So
there might be a pop and a de-sync when sampling rate change happens. Not
sure that using rate_delay would fix that
*/

#include "squeezelite.h"
#include "esp_pthread.h"
#include "driver/i2s.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "perf_trace.h"
#include <signal.h>
#include "time.h"
#include "led.h"
#include "audio_hal.h"
#include "ac101.h"

#define LOCK   mutex_lock(outputbuf->mutex)
#define UNLOCK mutex_unlock(outputbuf->mutex)

#define FRAME_BLOCK MAX_SILENCE_FRAMES

// Prevent compile errors if dac output is
// included in the build and not actually activated in menuconfig
#ifndef CONFIG_I2S_BCK_IO
#define CONFIG_I2S_BCK_IO -1
#endif
#ifndef CONFIG_I2S_WS_IO
#define CONFIG_I2S_WS_IO -1
#endif
#ifndef CONFIG_I2S_DO_IO
#define CONFIG_I2S_DO_IO -1
#endif
#ifndef CONFIG_I2S_NUM
#define CONFIG_I2S_NUM -1
#endif

#ifndef CONFIG_SPDIF_BCK_IO
#define CONFIG_SPDIF_BCK_IO -1
#endif
#ifndef CONFIG_SPDIF_WS_IO
#define CONFIG_SPDIF_WS_IO -1
#endif
#ifndef CONFIG_SPDIF_DO_IO
#define CONFIG_SPDIF_DO_IO -1
#endif
#ifndef CONFIG_SPDIF_NUM
#define CONFIG_SPDIF_NUM -1
#endif

typedef enum { DAC_ON = 0, DAC_OFF, DAC_POWERDOWN, DAC_VOLUME } dac_cmd_e;

// must have an integer ratio with FRAME_BLOCK (see spdif comment)
#define DMA_BUF_LEN		512
#define DMA_BUF_COUNT	12

#define DECLARE_ALL_MIN_MAX 	\
	DECLARE_MIN_MAX(o); 		\
	DECLARE_MIN_MAX(s); 		\
	DECLARE_MIN_MAX(rec); 		\
	DECLARE_MIN_MAX(i2s_time); 	\
	DECLARE_MIN_MAX(buffering);

#define RESET_ALL_MIN_MAX 		\
	RESET_MIN_MAX(o); 			\
	RESET_MIN_MAX(s); 			\
	RESET_MIN_MAX(rec);	\
	RESET_MIN_MAX(i2s_time);	\
	RESET_MIN_MAX(buffering);

#define STATS_PERIOD_MS 5000

extern struct outputstate output;
extern struct buffer *streambuf;
extern struct buffer *outputbuf;
extern u8_t *silencebuf;
extern bool jack_mutes_amp;

static log_level loglevel;
static bool running, isI2SStarted;
static i2s_config_t i2s_config;
static int bytes_per_frame;
static thread_type thread, stats_thread;
static u8_t *obuf;
static frames_t oframes;
static bool spdif;
static size_t dma_buf_frames;

DECLARE_ALL_MIN_MAX;

static int _i2s_write_frames(frames_t out_frames, bool silence, s32_t gainL, s32_t gainR,
								s32_t cross_gain_in, s32_t cross_gain_out, ISAMPLE_T **cross_ptr);
static void *output_thread_i2s();
static void *output_thread_i2s_stats();
static void dac_cmd(dac_cmd_e cmd, ...);
static void spdif_convert(ISAMPLE_T *src, size_t frames, u32_t *dst, size_t *count);

#ifdef CONFIG_SQUEEZEAMP

#define TAS575x

#undef	CONFIG_I2S_BCK_IO
#define CONFIG_I2S_BCK_IO 	33
#undef 	CONFIG_I2S_WS_IO
#define CONFIG_I2S_WS_IO	25
#undef 	CONFIG_I2S_DO_IO
#define CONFIG_I2S_DO_IO	32
#undef 	CONFIG_I2S_NUM
#define CONFIG_I2S_NUM		0

#undef	CONFIG_SPDIF_BCK_IO
#define CONFIG_SPDIF_BCK_IO 33
#undef 	CONFIG_SPDIF_WS_IO
#define CONFIG_SPDIF_WS_IO	25
#undef 	CONFIG_SPDIF_DO_IO
#define CONFIG_SPDIF_DO_IO	15
#undef 	CONFIG_SPDIF_NUM
#define CONFIG_SPDIF_NUM	0

#define I2C_PORT	0
#define I2C_ADDR	0x4c
#define VOLUME_GPIO	33
#define JACK_GPIO	34

struct tas575x_cmd_s {
	u8_t reg;
	u8_t value;
};

u8_t config_spdif_gpio = CONFIG_SPDIF_DO_IO;

static const struct tas575x_cmd_s tas575x_init_sequence[] = {
    { 0x00, 0x00 },		// select page 0
    { 0x02, 0x10 },		// standby
    { 0x0d, 0x10 },		// use SCK for PLL
    { 0x25, 0x08 },		// ignore SCK halt
	{ 0x02, 0x00 },		// restart
	{ 0xff, 0xff }		// end of table
};

static const i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 27,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = 26,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
};

static const struct tas575x_cmd_s tas575x_cmd[] = {
	{ 0x02, 0x00 },	// DAC_ON
	{ 0x02, 0x10 },	// DAC_OFF
	{ 0x02, 0x01 },	// DAC_POWERDOWN
};
#endif


/****************************************************************************************
 * Initialize the DAC output
 */
void output_init_i2s(log_level level, char *device, unsigned output_buf_size, char *params, unsigned rates[], unsigned rate_delay, unsigned idle) {
	loglevel = level;

#ifdef TAS575x
	gpio_pad_select_gpio(JACK_GPIO);
	gpio_set_direction(JACK_GPIO, GPIO_MODE_INPUT);

	adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_0);

	// init volume & mute
	gpio_pad_select_gpio(VOLUME_GPIO);
	gpio_set_direction(VOLUME_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_level(VOLUME_GPIO, 0);

	// configure i2c
	i2c_param_config(I2C_PORT, &i2c_config);
	i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, false, false, false);
	i2c_cmd_handle_t i2c_cmd = i2c_cmd_link_create();

	for (int i = 0; tas575x_init_sequence[i].reg != 0xff; i++) {
		i2c_master_start(i2c_cmd);
		i2c_master_write_byte(i2c_cmd, I2C_ADDR << 1 | I2C_MASTER_WRITE, I2C_MASTER_NACK);
		i2c_master_write_byte(i2c_cmd, tas575x_init_sequence[i].reg, I2C_MASTER_NACK);
		i2c_master_write_byte(i2c_cmd, tas575x_init_sequence[i].value, I2C_MASTER_NACK);

		LOG_DEBUG("i2c write %x at %u", tas575x_init_sequence[i].reg, tas575x_init_sequence[i].value);
	}

	i2c_master_stop(i2c_cmd);
	esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, i2c_cmd, 500 / portTICK_RATE_MS);
    i2c_cmd_link_delete(i2c_cmd);

	if (ret != ESP_OK) {
		LOG_ERROR("could not intialize TAS575x %d", ret);
	}
#endif

// Setup AC101
// TODO: This is the wrong place for it
// TODO: This should behind a build flag
audio_hal_codec_config_t audio_hal_codec_cfg = AUDIO_HAL_AC101_DEFAULT();
ac101_init(&audio_hal_codec_cfg);
ac101_config_i2s(audio_hal_codec_cfg.dac_output, &audio_hal_codec_cfg.i2s_iface);
ac101_ctrl_state(audio_hal_codec_cfg.dac_output, AUDIO_HAL_CTRL_START);
ac101_set_voice_volume(255);


#ifdef CONFIG_I2S_BITS_PER_CHANNEL
	switch (CONFIG_I2S_BITS_PER_CHANNEL) {
		case 24:
			output.format = S24_BE;
			bytes_per_frame = 2*3;
			break;
		case 16:
			output.format = S16_BE;
			bytes_per_frame = 2*2;
			break;
		case 8:
			output.format = S8_BE;
			bytes_per_frame = 2*4;
			break;
		default:
			LOG_ERROR("Unsupported bit depth %d",CONFIG_I2S_BITS_PER_CHANNEL);
			break;
	}
#else
	output.format = S16_LE;
	bytes_per_frame = 2*2;
#endif

	if (strcasestr(device, "spdif")) spdif = true;

	output.write_cb = &_i2s_write_frames;
	obuf = malloc(FRAME_BLOCK * bytes_per_frame);
	if (!obuf) {
		LOG_ERROR("Cannot allocate i2s buffer");
		return;
	}

	running=true;

	i2s_pin_config_t pin_config;

	if (spdif) {
		pin_config = (i2s_pin_config_t) { .bck_io_num = CONFIG_SPDIF_BCK_IO, .ws_io_num = CONFIG_SPDIF_WS_IO,
										  .data_out_num = CONFIG_SPDIF_DO_IO, .data_in_num = -1 //Not used
									};
		i2s_config.sample_rate = output.current_sample_rate * 2;
		i2s_config.bits_per_sample = 32;
		// Normally counted in frames, but 16 sample are transformed into 32 bits in spdif
		i2s_config.dma_buf_len = DMA_BUF_LEN / 2;
		i2s_config.dma_buf_count = DMA_BUF_COUNT * 2;
		/*
		   In DMA, we have room for (LEN * COUNT) frames of 32 bits samples that
		   we push at sample_rate * 2. Each of these peuso-frames is a single true
		   audio frame. So the real depth is true frames is (LEN * COUNT / 2)
		*/
		dma_buf_frames = DMA_BUF_COUNT * DMA_BUF_LEN / 2;
	} else {
		pin_config = (i2s_pin_config_t) { .bck_io_num = CONFIG_I2S_BCK_IO, .ws_io_num = CONFIG_I2S_WS_IO,
										.data_out_num = CONFIG_I2S_DO_IO, .data_in_num = -1 //Not used
									};
		i2s_config.sample_rate = output.current_sample_rate;
		i2s_config.bits_per_sample = bytes_per_frame * 8 / 2;
		// Counted in frames (but i2s allocates a buffer <= 4092 bytes)
		i2s_config.dma_buf_len = DMA_BUF_LEN;
		i2s_config.dma_buf_count = DMA_BUF_COUNT;
		dma_buf_frames = DMA_BUF_COUNT * DMA_BUF_LEN;
#ifdef TAS575x
		gpio_pad_select_gpio(CONFIG_SPDIF_DO_IO);
		gpio_set_direction(CONFIG_SPDIF_DO_IO, GPIO_MODE_OUTPUT);
		gpio_set_level(CONFIG_SPDIF_DO_IO, 0);
#endif
	}

	i2s_config.mode = I2S_MODE_MASTER | I2S_MODE_TX;
	i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
	i2s_config.communication_format = I2S_COMM_FORMAT_I2S| I2S_COMM_FORMAT_I2S_MSB;
	// in case of overflow, do not replay old buffer
	i2s_config.tx_desc_auto_clear = true;
	i2s_config.use_apll = true;
	i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1; //Interrupt level 1

	LOG_INFO("Initializing I2S mode %s with rate: %d, bits per sample: %d, buffer frames: %d, number of buffers: %d ",
			spdif ? "S/PDIF" : "normal",
			i2s_config.sample_rate, i2s_config.bits_per_sample, i2s_config.dma_buf_len, i2s_config.dma_buf_count);

	i2s_driver_install(CONFIG_I2S_NUM, &i2s_config, 0, NULL);
	i2s_set_pin(CONFIG_I2S_NUM, &pin_config);
	i2s_stop(CONFIG_I2S_NUM);
	i2s_zero_dma_buffer(CONFIG_I2S_NUM);
	isI2SStarted=false;

	dac_cmd(DAC_OFF);

	esp_pthread_cfg_t cfg = esp_pthread_get_default_config();

    cfg.thread_name= "output_i2s";
    cfg.inherit_cfg = false;
	cfg.prio = CONFIG_ESP32_PTHREAD_TASK_PRIO_DEFAULT + 1;
    cfg.stack_size = PTHREAD_STACK_MIN + OUTPUT_THREAD_STACK_SIZE;
    esp_pthread_set_cfg(&cfg);
	pthread_create(&thread, NULL, output_thread_i2s, NULL);

	cfg.thread_name= "output_i2s_sts";
	cfg.prio = CONFIG_ESP32_PTHREAD_TASK_PRIO_DEFAULT - 1;
    cfg.stack_size = 2048;
	esp_pthread_set_cfg(&cfg);
	pthread_create(&stats_thread, NULL, output_thread_i2s_stats, NULL);
}


/****************************************************************************************
 * Terminate DAC output
 */
void output_close_i2s(void) {
	LOCK;
	running = false;
	UNLOCK;
	pthread_join(thread, NULL);
	pthread_join(stats_thread, NULL);

	i2s_driver_uninstall(CONFIG_I2S_NUM);
	free(obuf);

#ifdef TAS575x
	i2c_driver_delete(I2C_PORT);
#endif
}

/****************************************************************************************
 * change volume
 */
bool output_volume_i2s(unsigned left, unsigned right) {
#ifdef TAS575x
	if (!spdif) gpio_set_level(VOLUME_GPIO, left || right);
#endif
 return false;
}


/****************************************************************************************
 * Write frames to the output buffer
 */
static int _i2s_write_frames(frames_t out_frames, bool silence, s32_t gainL, s32_t gainR,
								s32_t cross_gain_in, s32_t cross_gain_out, ISAMPLE_T **cross_ptr) {
#if BYTES_PER_FRAME == 8
	s32_t *optr;
#endif

	if (!silence) {
		if (output.fade == FADE_ACTIVE && output.fade_dir == FADE_CROSS && *cross_ptr) {
			_apply_cross(outputbuf, out_frames, cross_gain_in, cross_gain_out, cross_ptr);
		}

#if BYTES_PER_FRAME == 4
		if (gainL != FIXED_ONE || gainR!= FIXED_ONE) {
			_apply_gain(outputbuf, out_frames, gainL, gainR);
		}

		memcpy(obuf + oframes * bytes_per_frame, outputbuf->readp, out_frames * bytes_per_frame);
#else
		optr = (s32_t*) outputbuf->readp;
#endif
	} else {
#if BYTES_PER_FRAME == 4
		memcpy(obuf + oframes * bytes_per_frame, silencebuf, out_frames * bytes_per_frame);
#else
		optr = (s32_t*) silencebuf;
#endif
	}

#if BYTES_PER_FRAME == 8
	IF_DSD(
	if (output.outfmt == DOP) {
			update_dop((u32_t *) optr, out_frames, output.invert);
		} else if (output.outfmt != PCM && output.invert)
			dsd_invert((u32_t *) optr, out_frames);
	)

	_scale_and_pack_frames(obuf + oframes * bytes_per_frame, optr, out_frames, gainL, gainR, output.format);
#endif

	oframes += out_frames;

	return out_frames;
}

/****************************************************************************************
 * Main output thread
 */
static void *output_thread_i2s() {
	size_t count = 0, bytes;
	frames_t iframes = FRAME_BLOCK;
	uint32_t timer_start = 0;
	int discard = 0;
	uint32_t fullness = gettime_ms();
	bool synced;
	output_state state = OUTPUT_OFF;
	char *sbuf = NULL;

	// spdif needs 16 bytes per frame : 32 bits/sample, 2 channels, BMC encoded
	if (spdif && (sbuf = malloc(FRAME_BLOCK * 16)) == NULL) {
		LOG_ERROR("Cannot allocate SPDIF buffer");
	}

	while (running) {

		TIME_MEASUREMENT_START(timer_start);

		LOCK;
		if(jack_mutes_amp){
			// todo: implement some muting logic
		}
		// manage led display
		if (state != output.state) {
			LOG_INFO("Output state is %d", output.state);
			if (output.state == OUTPUT_OFF) led_blink(LED_GREEN, 100, 2500);
			else if (output.state == OUTPUT_STOPPED) led_blink(LED_GREEN, 200, 1000);
			else if (output.state == OUTPUT_RUNNING) led_on(LED_GREEN);
		}
		state = output.state;

		if (output.state == OUTPUT_OFF) {
			UNLOCK;
			if (isI2SStarted) {
				isI2SStarted = false;
				i2s_stop(CONFIG_I2S_NUM);
				if (!spdif) dac_cmd(DAC_OFF);
				count = 0;
			}
			usleep(200000);
			continue;
		} else if (output.state == OUTPUT_STOPPED) {
			synced = false;
		}

		oframes = 0;
		output.updated = gettime_ms();
		output.frames_played_dmp = output.frames_played;
		// try to estimate how much we have consumed from the DMA buffer (calculation is incorrect at the very beginning ...)
		output.device_frames = dma_buf_frames - ((output.updated - fullness) * output.current_sample_rate) / 1000;
		_output_frames( iframes );
		// oframes must be a global updated by the write callback
		output.frames_in_process = oframes;

		SET_MIN_MAX_SIZED(oframes,rec,iframes);
		SET_MIN_MAX_SIZED(_buf_used(outputbuf),o,outputbuf->size);
		SET_MIN_MAX_SIZED(_buf_used(streambuf),s,streambuf->size);
		SET_MIN_MAX( TIME_MEASUREMENT_GET(timer_start),buffering);

		/* must skip first whatever is in the pipe (but not when resuming).
		This test is incorrect when we pause a track that has just started,
		but this is higly unlikely and I don't have a better one for now */
		if (output.state == OUTPUT_START_AT) {
			discard = output.frames_played_dmp ? 0 : output.device_frames;
			synced = true;
		} else if (discard) {
			discard -= oframes;
			iframes = discard ? min(FRAME_BLOCK, discard) : FRAME_BLOCK;
			UNLOCK;
			continue;
		}

		UNLOCK;

		// now send all the data
		TIME_MEASUREMENT_START(timer_start);

		if (!isI2SStarted ) {
			isI2SStarted = true;
			LOG_INFO("Restarting I2S.");
			i2s_zero_dma_buffer(CONFIG_I2S_NUM);
			i2s_start(CONFIG_I2S_NUM);
			if (!spdif) dac_cmd(DAC_ON);
		}

		// this does not work well as set_sample_rates resets the fifos (and it's too early)
		if (i2s_config.sample_rate != output.current_sample_rate) {
			LOG_INFO("changing sampling rate %u to %u", i2s_config.sample_rate, output.current_sample_rate);
			/*
			if (synced)
				//  can sleep for a buffer_queue - 1 and then eat a buffer (discard) if we are synced
				usleep(((DMA_BUF_COUNT - 1) * DMA_BUF_LEN * bytes_per_frame * 1000) / 44100 * 1000);
				discard = DMA_BUF_COUNT * DMA_BUF_LEN * bytes_per_frame;
			}
			*/
			i2s_config.sample_rate = output.current_sample_rate;
			i2s_set_sample_rates(CONFIG_I2S_NUM, spdif ? i2s_config.sample_rate * 2 : i2s_config.sample_rate);
			i2s_zero_dma_buffer(CONFIG_I2S_NUM);
			//return;
		}

		// we assume that here we have been able to entirely fill the DMA buffers
		if (spdif) {
			spdif_convert((ISAMPLE_T*) obuf, oframes, (u32_t*) sbuf, &count);
			i2s_write(CONFIG_I2S_NUM, sbuf, oframes * 16, &bytes, portMAX_DELAY);
			bytes /= 4;
		} else {
			i2s_write(CONFIG_I2S_NUM, obuf, oframes * bytes_per_frame, &bytes, portMAX_DELAY);
		}
		fullness = gettime_ms();

		if (bytes != oframes * bytes_per_frame) {
			LOG_WARN("I2S DMA Overflow! available bytes: %d, I2S wrote %d bytes", oframes * bytes_per_frame, bytes);
		}

		SET_MIN_MAX( TIME_MEASUREMENT_GET(timer_start),i2s_time);

	}

	if (spdif) free(sbuf);

	return 0;
}

/****************************************************************************************
 * Stats output thread
 */
static void *output_thread_i2s_stats() {
	while (running) {
#ifdef TAS575x
		LOG_ERROR("Jack %d Voltage %.2fV", !gpio_get_level(JACK_GPIO), adc1_get_raw(ADC1_CHANNEL_7) / 4095. * (10+174)/10. * 1.1);
#endif
		LOCK;
		output_state state = output.state;
		UNLOCK;
		if(state>OUTPUT_STOPPED){
			LOG_INFO( "Output State: %d, current sample rate: %d, bytes per frame: %d",state,output.current_sample_rate, bytes_per_frame);
			LOG_INFO( LINE_MIN_MAX_FORMAT_HEAD1);
			LOG_INFO( LINE_MIN_MAX_FORMAT_HEAD2);
			LOG_INFO( LINE_MIN_MAX_FORMAT_HEAD3);
			LOG_INFO( LINE_MIN_MAX_FORMAT_HEAD4);
			LOG_INFO(LINE_MIN_MAX_FORMAT_STREAM, LINE_MIN_MAX_STREAM("stream",s));
			LOG_INFO(LINE_MIN_MAX_FORMAT,LINE_MIN_MAX("output",o));
			LOG_INFO(LINE_MIN_MAX_FORMAT_FOOTER);
			LOG_INFO(LINE_MIN_MAX_FORMAT,LINE_MIN_MAX("received",rec));
			LOG_INFO(LINE_MIN_MAX_FORMAT_FOOTER);
			LOG_INFO("");
			LOG_INFO("              ----------+----------+-----------+-----------+  ");
			LOG_INFO("              max (us)  | min (us) |   avg(us) |  count    |  ");
			LOG_INFO("              ----------+----------+-----------+-----------+  ");
			LOG_INFO(LINE_MIN_MAX_DURATION_FORMAT,LINE_MIN_MAX_DURATION("Buffering(us)",buffering));
			LOG_INFO(LINE_MIN_MAX_DURATION_FORMAT,LINE_MIN_MAX_DURATION("i2s tfr(us)",i2s_time));
			LOG_INFO("              ----------+----------+-----------+-----------+");
			RESET_ALL_MIN_MAX;
		}
		LOG_INFO("Heap internal:%zu (min:%zu) external:%zu (min:%zu)",
					heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
					heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
					heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
					heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
		usleep(STATS_PERIOD_MS *1000);
	}
	return NULL;
}

/****************************************************************************************
 * DAC specific commands
 */
void dac_cmd(dac_cmd_e cmd, ...) {
	va_list args;
	esp_err_t ret = ESP_OK;

	va_start(args, cmd);
#ifdef TAS575x
	i2c_cmd_handle_t i2c_cmd = i2c_cmd_link_create();

	switch(cmd) {
	case DAC_VOLUME:
		LOG_ERROR("volume not handled yet");
		break;
	default:
		i2c_master_start(i2c_cmd);
		i2c_master_write_byte(i2c_cmd, I2C_ADDR << 1 | I2C_MASTER_WRITE, I2C_MASTER_NACK);
		i2c_master_write_byte(i2c_cmd, tas575x_cmd[cmd].reg, I2C_MASTER_NACK);
		i2c_master_write_byte(i2c_cmd, tas575x_cmd[cmd].value, I2C_MASTER_NACK);
		i2c_master_stop(i2c_cmd);
		ret	= i2c_master_cmd_begin(I2C_PORT, i2c_cmd, 50 / portTICK_RATE_MS);
	}

    i2c_cmd_link_delete(i2c_cmd);

	if (ret != ESP_OK) {
		LOG_ERROR("could not intialize TAS575x %d", ret);
	}
#endif
	va_end(args);
}

/****************************************************************************************
 * SPDIF support
 */

#define PREAMBLE_B  (0xE8) //11101000
#define PREAMBLE_M  (0xE2) //11100010
#define PREAMBLE_W  (0xE4) //11100100

#define VUCP   		((0xCC) << 24)
#define VUCP_MUTE 	((0xD4) << 24)	// To mute PCM, set VUCP = invalid.

extern const u16_t spdif_bmclookup[256];

/*
 SPDIF is supposed to be (before BMC encoding, from LSB to MSB)
	PPPP AAAA  SSSS SSSS  SSSS SSSS  SSSS VUCP
 after BMC encoding, each bits becomes 2 hence this becomes a 64 bits word. The
 the trick is to start not with a PPPP sequence but with an VUCP sequence to that
 the 16 bits samples are aligned with a BMC word boundary. Note that the LSB of the
 audio is transmitted first (not the MSB) and that ESP32 libray sends R then L,
 contrary to what seems to be usually done, so (dst) order had to be changed
*/
void spdif_convert(ISAMPLE_T *src, size_t frames, u32_t *dst, size_t *count) {
	u16_t hi, lo, aux;

	// frames are 2 channels of 16 bits
	frames *= 2;

	while (frames--) {
#if BYTES_PER_FRAME == 4
		hi  = spdif_bmclookup[(u8_t)(*src >> 8)];
		lo  = spdif_bmclookup[(u8_t) *src];
#else
		hi  = spdif_bmclookup[(u8_t)(*src >> 24)];
		lo  = spdif_bmclookup[(u8_t) *src >> 16];
#endif
		lo ^= ~((s16_t)hi) >> 16;

		// 16 bits sample:
		*(dst+0) = ((u32_t)lo << 16) | hi;

		// 4 bits auxillary-audio-databits, the first used as parity
		aux = 0xb333 ^ (((u32_t)((s16_t)lo)) >> 17);

		// VUCP-Bits: Valid, Subcode, Channelstatus, Parity = 0
		// As parity is always 0, we can use fixed preambles
		if (++(*count) > 383) {
			*(dst+1) =  VUCP | (PREAMBLE_B << 16 ) | aux; //special preamble for one of 192 frames
			*count = 0;
		} else {
			*(dst+1) = VUCP | ((((*count) & 0x01) ? PREAMBLE_W : PREAMBLE_M) << 16) | aux;
		}

		src++;
		dst += 2;
	}
}

const u16_t spdif_bmclookup[256] = { //biphase mark encoded values (least significant bit first)
	0xcccc, 0x4ccc, 0x2ccc, 0xaccc, 0x34cc, 0xb4cc, 0xd4cc, 0x54cc,
	0x32cc, 0xb2cc, 0xd2cc, 0x52cc, 0xcacc, 0x4acc, 0x2acc, 0xaacc,
	0x334c, 0xb34c, 0xd34c, 0x534c, 0xcb4c, 0x4b4c, 0x2b4c, 0xab4c,
	0xcd4c, 0x4d4c, 0x2d4c, 0xad4c, 0x354c, 0xb54c, 0xd54c, 0x554c,
	0x332c, 0xb32c, 0xd32c, 0x532c, 0xcb2c, 0x4b2c, 0x2b2c, 0xab2c,
	0xcd2c, 0x4d2c, 0x2d2c, 0xad2c, 0x352c, 0xb52c, 0xd52c, 0x552c,
	0xccac, 0x4cac, 0x2cac, 0xacac, 0x34ac, 0xb4ac, 0xd4ac, 0x54ac,
	0x32ac, 0xb2ac, 0xd2ac, 0x52ac, 0xcaac, 0x4aac, 0x2aac, 0xaaac,
	0x3334, 0xb334, 0xd334, 0x5334, 0xcb34, 0x4b34, 0x2b34, 0xab34,
	0xcd34, 0x4d34, 0x2d34, 0xad34, 0x3534, 0xb534, 0xd534, 0x5534,
	0xccb4, 0x4cb4, 0x2cb4, 0xacb4, 0x34b4, 0xb4b4, 0xd4b4, 0x54b4,
	0x32b4, 0xb2b4, 0xd2b4, 0x52b4, 0xcab4, 0x4ab4, 0x2ab4, 0xaab4,
	0xccd4, 0x4cd4, 0x2cd4, 0xacd4, 0x34d4, 0xb4d4, 0xd4d4, 0x54d4,
	0x32d4, 0xb2d4, 0xd2d4, 0x52d4, 0xcad4, 0x4ad4, 0x2ad4, 0xaad4,
	0x3354, 0xb354, 0xd354, 0x5354, 0xcb54, 0x4b54, 0x2b54, 0xab54,
	0xcd54, 0x4d54, 0x2d54, 0xad54, 0x3554, 0xb554, 0xd554, 0x5554,
	0x3332, 0xb332, 0xd332, 0x5332, 0xcb32, 0x4b32, 0x2b32, 0xab32,
	0xcd32, 0x4d32, 0x2d32, 0xad32, 0x3532, 0xb532, 0xd532, 0x5532,
	0xccb2, 0x4cb2, 0x2cb2, 0xacb2, 0x34b2, 0xb4b2, 0xd4b2, 0x54b2,
	0x32b2, 0xb2b2, 0xd2b2, 0x52b2, 0xcab2, 0x4ab2, 0x2ab2, 0xaab2,
	0xccd2, 0x4cd2, 0x2cd2, 0xacd2, 0x34d2, 0xb4d2, 0xd4d2, 0x54d2,
	0x32d2, 0xb2d2, 0xd2d2, 0x52d2, 0xcad2, 0x4ad2, 0x2ad2, 0xaad2,
	0x3352, 0xb352, 0xd352, 0x5352, 0xcb52, 0x4b52, 0x2b52, 0xab52,
	0xcd52, 0x4d52, 0x2d52, 0xad52, 0x3552, 0xb552, 0xd552, 0x5552,
	0xccca, 0x4cca, 0x2cca, 0xacca, 0x34ca, 0xb4ca, 0xd4ca, 0x54ca,
	0x32ca, 0xb2ca, 0xd2ca, 0x52ca, 0xcaca, 0x4aca, 0x2aca, 0xaaca,
	0x334a, 0xb34a, 0xd34a, 0x534a, 0xcb4a, 0x4b4a, 0x2b4a, 0xab4a,
	0xcd4a, 0x4d4a, 0x2d4a, 0xad4a, 0x354a, 0xb54a, 0xd54a, 0x554a,
	0x332a, 0xb32a, 0xd32a, 0x532a, 0xcb2a, 0x4b2a, 0x2b2a, 0xab2a,
	0xcd2a, 0x4d2a, 0x2d2a, 0xad2a, 0x352a, 0xb52a, 0xd52a, 0x552a,
	0xccaa, 0x4caa, 0x2caa, 0xacaa, 0x34aa, 0xb4aa, 0xd4aa, 0x54aa,
	0x32aa, 0xb2aa, 0xd2aa, 0x52aa, 0xcaaa, 0x4aaa, 0x2aaa, 0xaaaa
};
