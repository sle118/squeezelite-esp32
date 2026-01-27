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
 
/* 
Synchronisation is a bit of a hack with i2s. The esp32 driver is always
full when it starts, so there is a delay of the total length of buffers.
In other words, i2s_write blocks at first call, until at least one buffer
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
#include "slimproto.h"
#include "esp_pthread.h"
#include "driver/i2s.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "perf_trace.h"
#include <signal.h>
#include "adac.h"
#include "time.h"
#include "led.h"
#include "services.h"
#include "monitor.h"
#include "platform_config.h"
#include "gpio_exp.h"
#include "accessors.h"
#include "equalizer.h"
#include "globdefs.h"
#include "gpio_volume.h"

#define LOCK   mutex_lock(outputbuf->mutex)
#define UNLOCK mutex_unlock(outputbuf->mutex)

#define FRAME_BLOCK MAX_SILENCE_FRAMES
#define SPDIF_BLOCK	256

/* we produce FRAME_BLOCK (2048) per loop of the i2s thread so it's better if they fit
 * inside a set of DMA buffer nicely, i.e. DMA_BUF_FRAMES * DMA_BUF_COUNT is a multiple 
 * of FRAME_BLOCK so that each DMA buffer is filled and we fully empty a FRAME_BLOCK at 
 * each loop. Because one DMA buffer in esp32 is 4092 or below, when using 16 bits 
 * samples and 2 channels, the best multiple is 512 (512*2*2=2048) and we use 6 of these.
 * In SPDIF, as we virtually use 32 bits per sample, the next proper multiple would
 * be 256 but such DMA buffers are too small and this causes stuttering. So we will use
 * non-multiples which means that at every loop one DMA buffer will be not fully filled. 
 * At least, let's make sure it's not a too small amount of samples so 450*4*2=3600 fits 
 * nicely in one DMA buffer and 2048/450 = 4 buffers + ~1/2 buffer which is acceptable.
 */
#define DMA_BUF_FRAMES	512
#define DMA_BUF_COUNT	12

#define DMA_BUF_FRAMES_SPDIF	450
#define DMA_BUF_COUNT_SPDIF     7

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
static void (*pseudo_idle_chain)(uint32_t now);

#ifndef CONFIG_AMP_GPIO_LEVEL
#define CONFIG_AMP_GPIO_LEVEL 1
#endif

extern struct outputstate output;
extern struct buffer *streambuf;
extern struct buffer *outputbuf;
extern u8_t *silencebuf;

const struct adac_s *dac_set[] = { &dac_tas57xx, &dac_tas5713, &dac_ac101, &dac_wm8978, NULL };
const struct adac_s *adac = &dac_external;

static log_level loglevel;

static uint32_t i2s_idle_since;
static void (*pseudo_idle_chain)(uint32_t);
static bool (*slimp_handler_chain)(u8_t *data, int len);
static bool jack_mutes_amp;
static bool running, isI2SStarted, ended;
static i2s_config_t i2s_config;
static u8_t *obuf;
static frames_t oframes;
static struct {
	bool enabled;
	u8_t *buf;
} spdif;
static size_t dma_buf_frames;
static TaskHandle_t output_i2s_task;
static struct {
	int gpio, active;
} amp_control = { CONFIG_AMP_GPIO, CONFIG_AMP_GPIO_LEVEL },
  mute_control = { CONFIG_MUTE_GPIO, CONFIG_MUTE_GPIO_LEVEL };

DECLARE_ALL_MIN_MAX;

static int _i2s_write_frames(frames_t out_frames, bool silence, s32_t gainL, s32_t gainR, u8_t flags,
								s32_t cross_gain_in, s32_t cross_gain_out, ISAMPLE_T **cross_ptr);
static void output_thread_i2s(void *arg);
static void i2s_stats(uint32_t now);

static void spdif_convert(ISAMPLE_T *src, size_t frames, u32_t *dst);
static void (*jack_handler_chain)(bool inserted);

#define I2C_PORT	0

/****************************************************************************************
 * AUDO packet handler
 */
static bool handler(u8_t *data, int len){
	bool res = true;
	
	if (!strncmp((char*) data, "audo", 4)) {
		struct audo_packet *pkt = (struct audo_packet*) data;
		// 0 = headphone (internal speakers off), 1 = sub out,
		// 2 = always on (internal speakers on), 3 = always off	

		if (jack_mutes_amp != (pkt->config == 0)) {
			jack_mutes_amp = pkt->config == 0;
			config_set_value(NVS_TYPE_STR, "jack_mutes_amp", jack_mutes_amp ? "y" : "n");		
			
			if (jack_mutes_amp && jack_inserted_svc()) {
				adac->speaker(false);
				if (amp_control.gpio != -1) gpio_set_level_x(amp_control.gpio, !amp_control.active);
			} else {
				adac->speaker(true);
				if (amp_control.gpio != -1) gpio_set_level_x(amp_control.gpio, amp_control.active);
			}	
		}

		LOG_INFO("got AUDO %02x", pkt->config);
	} else {
		res = false;
	}
	
	// chain protocol handlers (bitwise or is fine)
	if (*slimp_handler_chain) res |= (*slimp_handler_chain)(data, len);
	
	return res;
}

/****************************************************************************************
 * jack insertion handler
 */
static void jack_handler(bool inserted) {
	// jack detection bounces a bit but that seems fine
	if (jack_mutes_amp) {
		LOG_INFO("switching amplifier %s", inserted ? "OFF" : "ON");
		adac->speaker(!inserted);
		if (amp_control.gpio != -1) gpio_set_level_x(amp_control.gpio, inserted ? !amp_control.active : amp_control.active);
	}
	
	// activate headset
	adac->headset(inserted);
	
	// and chain if any
	if (jack_handler_chain) (jack_handler_chain)(inserted);
}

/****************************************************************************************
 * amp GPIO
 */
#ifndef AMP_LOCKED 
static void set_amp_gpio(int gpio, char *value) {
	char *p;
	
	if (strcasestr(value, "amp")) {
		amp_control.gpio = gpio;
		if ((p = strchr(value, ':')) != NULL) amp_control.active = atoi(p + 1);
	}	
}	
#endif

/****************************************************************************************
 * Get inactivity callback
 */
static uint32_t i2s_idle_callback(void) {
    return output.state <= OUTPUT_STOPPED ? pdTICKS_TO_MS(xTaskGetTickCount()) - i2s_idle_since : 0;
}

/****************************************************************************************
 * Set pin from config string
 */
static void set_i2s_pin(char *config, i2s_pin_config_t *pin_config) {
	pin_config->bck_io_num = pin_config->ws_io_num = pin_config->data_out_num = pin_config->data_in_num = -1;
	PARSE_PARAM(config, "bck", '=', pin_config->bck_io_num);
	PARSE_PARAM(config, "ws", '=', pin_config->ws_io_num);
	PARSE_PARAM(config, "do", '=', pin_config->data_out_num);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
    pin_config->mck_io_num = strcasestr(config, "mck") ? 0 : -1;
    PARSE_PARAM(config, "mck", '=', pin_config->mck_io_num);   
#endif    
}

/* When a panic occurs during playback, the I2S interface can produce a loud noise burst.
 * This code runs just before the system panic handler to "emergency stop" the I2S iterface
 * to prevent the noise burst from happening.  Note that when this code is called the system
 * has already crashed, so no need to disable interrupts, acquire locks, or otherwise be nice.
 *
 * This code makes use of the linker --wrap feature to intercept the call to esp_panic_handler.
 */

void __real_esp_panic_handler(void*);

void __wrap_esp_panic_handler (void* info) {
    esp_rom_printf("I2S abort!\r\n");
    
    i2s_stop(CONFIG_I2S_NUM);
    
    /* Call the original panic handler function to finish processing this error */
    __real_esp_panic_handler(info);
}

/****************************************************************************************
 * Initialize the DAC output
 */
void output_init_i2s(log_level level, char *device, unsigned output_buf_size, char *params, unsigned rates[], unsigned rate_delay, unsigned idle) {
	loglevel = level;
	int silent_do = -1;
	char *p;
	esp_err_t res;

	// chain SLIMP handlers
	slimp_handler_chain = slimp_handler;
	slimp_handler = handler;	
	
	p = config_alloc_get_default(NVS_TYPE_STR, "jack_mutes_amp", "n", 0);
	jack_mutes_amp = (strcmp(p,"1") == 0 ||strcasecmp(p,"y") == 0);
	free(p);
	
#if BYTES_PER_FRAME == 8
	output.format = S32_LE;
#else
	output.format = S16_LE;
#endif

	output.write_cb = &_i2s_write_frames;
	
	obuf = malloc(FRAME_BLOCK * BYTES_PER_FRAME);
	if (!obuf) {
		LOG_ERROR("Cannot allocate i2s buffer");
		return;
	}
		
	running = true;

	// get SPDIF configuration from NVS or compile
	char *spdif_config = config_alloc_get_str("spdif_config", CONFIG_SPDIF_CONFIG, "bck=" STR(CONFIG_SPDIF_BCK_IO) 
											  ",ws=" STR(CONFIG_SPDIF_WS_IO) ",do=" STR(CONFIG_SPDIF_DO_IO));
											  
	char *dac_config = config_alloc_get_str("dac_config", CONFIG_DAC_CONFIG, "model=i2s,bck=" STR(CONFIG_I2S_BCK_IO) 
											",ws=" STR(CONFIG_I2S_WS_IO) ",do=" STR(CONFIG_I2S_DO_IO) ",mck=" STR(CONFIG_I2S_MCK_IO)
											",sda=" STR(CONFIG_I2C_SDA) ",scl=" STR(CONFIG_I2C_SCL)
											",mute=" STR(CONFIG_MUTE_GPIO));	

    i2s_pin_config_t i2s_dac_pin, i2s_spdif_pin;											
	set_i2s_pin(spdif_config, &i2s_spdif_pin);										
	set_i2s_pin(dac_config, &i2s_dac_pin);										
    
    if (i2s_dac_pin.data_out_num == -1 && i2s_spdif_pin.data_out_num == -1) {
        LOG_WARN("DAC and SPDIF not configured, NOT launching i2s thread");
        return;
    }
    
	// common I2S initialization
	i2s_config.mode = I2S_MODE_MASTER | I2S_MODE_TX;
	i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
	i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
	// in case of overflow, do not replay old buffer
	i2s_config.tx_desc_auto_clear = true;		
#ifndef CONFIG_IDF_TARGET_ESP32S3
    i2s_config.use_apll = true;
#endif 
	i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1; //Interrupt level 1
    i2s_config.dma_buf_len = DMA_BUF_FRAMES;	
	i2s_config.dma_buf_count = DMA_BUF_COUNT;
	
	if (strcasestr(device, "spdif")) {
		spdif.enabled = true;	
		if ((spdif.buf = heap_caps_malloc(SPDIF_BLOCK * 16, MALLOC_CAP_INTERNAL)) == NULL) {
			LOG_ERROR("Cannot allocate SPDIF buffer");
		}
	
		if (i2s_spdif_pin.bck_io_num == -1 || i2s_spdif_pin.ws_io_num == -1 || i2s_spdif_pin.data_out_num == -1) {
			LOG_WARN("Cannot initialize I2S for SPDIF bck:%d ws:%d do:%d", i2s_spdif_pin.bck_io_num, 
																		   i2s_spdif_pin.ws_io_num, 
																		   i2s_spdif_pin.data_out_num);
		}
									
		i2s_config.sample_rate = output.current_sample_rate * 2;
		i2s_config.bits_per_sample = 32;
		// Normally counted in frames, but 16 sample are transformed into 32 bits in spdif
		i2s_config.dma_buf_len = DMA_BUF_FRAMES_SPDIF;	
		i2s_config.dma_buf_count = DMA_BUF_COUNT_SPDIF;
		/* 
		   In DMA, we have room for (LEN * COUNT) frames of 32 bits samples that 
		   we push at sample_rate * 2. Each of these pseudo-frames is a single true
		   audio frame. So the real depth in true frames is (LEN * COUNT / 2)
		*/   
		dma_buf_frames = i2s_config.dma_buf_len * i2s_config.dma_buf_count / 2;	
		
		// silence DAC output if sharing the same ws/bck
		if (i2s_dac_pin.ws_io_num == i2s_spdif_pin.ws_io_num && i2s_dac_pin.bck_io_num == i2s_spdif_pin.bck_io_num)	silent_do = i2s_dac_pin.data_out_num;		
		
		res = i2s_driver_install(CONFIG_I2S_NUM, &i2s_config, 0, NULL);
		res |= i2s_set_pin(CONFIG_I2S_NUM, &i2s_spdif_pin);

		// max drive capability for lowest output impedance for S/PDIF
		gpio_set_drive_capability(i2s_spdif_pin.data_out_num, GPIO_DRIVE_CAP_3); 

		LOG_INFO("SPDIF using I2S bck:%d, ws:%d, do:%d", i2s_spdif_pin.bck_io_num, i2s_spdif_pin.ws_io_num, i2s_spdif_pin.data_out_num);
	} else {
		i2s_config.sample_rate = output.current_sample_rate;
		i2s_config.bits_per_sample = BYTES_PER_FRAME * 8 / 2;
		// Counted in frames (but i2s allocates a buffer <= 4092 bytes)
		i2s_config.dma_buf_len = DMA_BUF_FRAMES;	
		i2s_config.dma_buf_count = DMA_BUF_COUNT;
		dma_buf_frames = i2s_config.dma_buf_len * i2s_config.dma_buf_count;
		
		// silence SPDIF output
		silent_do = i2s_spdif_pin.data_out_num;		

		char model[32] = "i2s";
		if ((p = strcasestr(dac_config, "model")) != NULL) sscanf(p, "%*[^=]=%31[^,]", model);
		if ((p = strcasestr(dac_config, "mute")) != NULL) {
			char mute[8] = "";
			sscanf(p, "%*[^=]=%7[^,]", mute);
			mute_control.gpio = atoi(mute);
			if ((p = strchr(mute, ':')) != NULL) mute_control.active = atoi(p + 1);
		}	

        bool mck_required = false;
		for (int i = 0; adac == &dac_external && dac_set[i]; i++) if (strcasestr(dac_set[i]->model, model)) adac = dac_set[i];
		res = adac->init(dac_config, I2C_PORT, &i2s_config, &mck_required) ? ESP_OK : ESP_FAIL;
        
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 4, 0)        
        int mck_io_num = strcasestr(dac_config, "mck") || mck_required ? 0 : -1;
        PARSE_PARAM(dac_config, "mck", '=', mck_io_num);

        LOG_INFO("configuring MCLK on GPIO %d", mck_io_num);

        if (mck_io_num == GPIO_NUM_0) {
            PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
            WRITE_PERI_REG(PIN_CTRL, CONFIG_I2S_NUM == I2S_NUM_0 ? 0xFFF0 : 0xFFFF);
        } else if (mck_io_num == GPIO_NUM_1) {
            PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD_CLK_OUT3);
            WRITE_PERI_REG(PIN_CTRL, CONFIG_I2S_NUM == I2S_NUM_0 ? 0xF0F0 : 0xF0FF);
        } else if (mck_io_num == GPIO_NUM_2) {
            PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_U0RXD_CLK_OUT2);
            WRITE_PERI_REG(PIN_CTRL, CONFIG_I2S_NUM == I2S_NUM_0 ? 0xFF00 : 0xFF0F);
        } else {
            LOG_WARN("invalid MCK gpio %d", mck_io_num);
        }
#else
        if (mck_required && i2s_dac_pin.mck_io_num == -1) i2s_dac_pin.mck_io_num = 0;
        LOG_INFO("configuring MCLK on GPIO %d", i2s_dac_pin.mck_io_num);
#endif    
       
		res |= i2s_driver_install(CONFIG_I2S_NUM, &i2s_config, 0, NULL);
		res |= i2s_set_pin(CONFIG_I2S_NUM, &i2s_dac_pin);
        	
		if (res == ESP_OK && mute_control.gpio >= 0) {
			gpio_pad_select_gpio(mute_control.gpio);
			gpio_set_direction(mute_control.gpio, GPIO_MODE_OUTPUT);
			gpio_set_level(mute_control.gpio, mute_control.active);
		}		
				
		LOG_INFO("%s DAC using I2S bck:%d, ws:%d, do:%d, mute:%d:%d (res:%d)", model, i2s_dac_pin.bck_io_num, i2s_dac_pin.ws_io_num, 
																   i2s_dac_pin.data_out_num, mute_control.gpio, mute_control.active, res);
	}	
			
	free(dac_config);
	free(spdif_config);
	
	if (res != ESP_OK) {
		LOG_WARN("no DAC configured");
		return;
	}	
	
	// turn off GPIO than is not used (SPDIF of DAC DO when shared)
	if (silent_do >= 0) {
		gpio_pad_select_gpio(silent_do);
		gpio_set_direction(silent_do, GPIO_MODE_OUTPUT);
		gpio_set_level(silent_do, 0);
	}	

	LOG_INFO("Initializing I2S mode %s with rate: %d, bits per sample: %d, buffer frames: %d, number of buffers: %d ", 
			spdif.enabled ? "S/PDIF" : "normal", 
			i2s_config.sample_rate, i2s_config.bits_per_sample, i2s_config.dma_buf_len, i2s_config.dma_buf_count);
	
	i2s_stop(CONFIG_I2S_NUM);
	i2s_zero_dma_buffer(CONFIG_I2S_NUM);
	isI2SStarted=false;
    
    equalizer_set_samplerate(output.current_sample_rate);
	
	adac->power(ADAC_STANDBY);

	jack_handler_chain = jack_handler_svc;
	jack_handler_svc = jack_handler;
	
#ifndef AMP_LOCKED	
	parse_set_GPIO(set_amp_gpio);
#endif

	if (amp_control.gpio != -1) {
		gpio_pad_select_gpio_x(amp_control.gpio);
		gpio_set_direction_x(amp_control.gpio, GPIO_MODE_OUTPUT);
		gpio_set_level_x(amp_control.gpio, !amp_control.active);
		LOG_INFO("setting amplifier GPIO %d (active:%d)", amp_control.gpio, amp_control.active);	
	}	

	if (jack_mutes_amp && jack_inserted_svc()) adac->speaker(false);
	else adac->speaker(true);
	
	adac->headset(jack_inserted_svc());	
    
	// Initialize GPIO Volume Service
	char *gv_cfg = config_alloc_get(NVS_TYPE_STR, "gpio_volume");
	if (gv_cfg)
	{
		LOG_INFO("TAG", "Found gpio_volume config: %s", gv_cfg);
		if (gpio_volume_init(gv_cfg))
		{
			unsigned v = (output.gainL > output.gainR) ? output.gainL : output.gainR;
			gpio_volume_apply_startup_volume(v);
		}
		free(gv_cfg);
	}
	else
	{
		LOG_WARN("TAG", "gpio_volume key NOT FOUND in NVS");
	}

    // do we want stats
	p = config_alloc_get_default(NVS_TYPE_STR, "stats", "n", 0);
	if (p && (*p == '1' || *p == 'Y' || *p == 'y')) {
        pseudo_idle_chain = pseudo_idle_svc;
        pseudo_idle_svc = i2s_stats;
    }
    free(p);

    // register a callback for inactivity
    i2s_idle_since = pdTICKS_TO_MS(xTaskGetTickCount());    
    services_sleep_setsleeper(i2s_idle_callback);
    
	// create task as a FreeRTOS task but uses stack in internal RAM
	{
		static DRAM_ATTR StaticTask_t xTaskBuffer __attribute__ ((aligned (4)));
		static EXT_RAM_ATTR StackType_t xStack[OUTPUT_THREAD_STACK_SIZE] __attribute__ ((aligned (4)));
		output_i2s_task = xTaskCreateStaticPinnedToCore( (TaskFunction_t) output_thread_i2s, "output_i2s", OUTPUT_THREAD_STACK_SIZE, 
											  NULL, CONFIG_ESP32_PTHREAD_TASK_PRIO_DEFAULT + 10, xStack, &xTaskBuffer, 0 );
	}
}

/****************************************************************************************
 * Terminate DAC output
 */
void output_close_i2s(void) {
	LOCK;
	running = false;
	UNLOCK;
	
	while (!ended) vTaskDelay(20 / portTICK_PERIOD_MS);
	
	i2s_driver_uninstall(CONFIG_I2S_NUM);
	free(obuf);
	
	equalizer_close();
	
	adac->deinit();
}

/****************************************************************************************
 * change volume
 */
bool output_volume_i2s(unsigned left, unsigned right) {
	if (mute_control.gpio >= 0) gpio_set_level(mute_control.gpio, (left | right) ? !mute_control.active : mute_control.active);

	gpio_max_mode_t vol_mode = gpio_volume_get_mode();

	if (vol_mode.active)
		gpio_volume_update(left > right ? left : right);

	if (vol_mode.dac_fixed)
	{
		left = 65536U;
		right = 65536U;
	}

	return adac->volume(left, right);
} 

/****************************************************************************************
 * Write frames to the output buffer
 */
static int _i2s_write_frames(frames_t out_frames, bool silence, s32_t gainL, s32_t gainR, u8_t flags,
								s32_t cross_gain_in, s32_t cross_gain_out, ISAMPLE_T **cross_ptr) {

	gpio_max_mode_t vol_mode = gpio_volume_get_mode();
	
	// Default: Visu looks at the DMA buffer (obuf)
	void* visu_ptr = obuf + oframes * BYTES_PER_FRAME;
	s32_t visu_gain = 65536; 
	
	// Check if the current read chunk wraps around the end of the ring buffer
	bool buffer_wraps = (outputbuf->readp + out_frames * BYTES_PER_FRAME > outputbuf->wrap);

	if (!silence) {
		if (output.fade == FADE_ACTIVE && output.fade_dir == FADE_CROSS && *cross_ptr) {
			_apply_cross(outputbuf, out_frames, cross_gain_in, cross_gain_out, cross_ptr);
		}

		if (vol_mode.dac_fixed) {
			// DAC FIXED: Copy RAW samples to DMA buffer without attenuation
			memcpy(obuf + oframes * BYTES_PER_FRAME, outputbuf->readp, out_frames * BYTES_PER_FRAME);

			// VISU NOT FIXED: Apply gain to the source buffer
			if (!vol_mode.visu_fixed) {
				_apply_gain(outputbuf, out_frames, gainL, gainR, flags);

				// Only redirect visu to source buffer if it is contiguous (not wrapping)
				if (!buffer_wraps) {
					visu_ptr = outputbuf->readp;
					visu_gain = (gainL + gainR) / 2;
				}
			}
		} else {
			// NORMAL MODE: Apply gain first, then copy
			_apply_gain(outputbuf, out_frames, gainL, gainR, flags);
			visu_gain = (gainL + gainR) / 2;
			memcpy(obuf + oframes * BYTES_PER_FRAME, outputbuf->readp, out_frames * BYTES_PER_FRAME);
		}
	} else {
		memcpy(obuf + oframes * BYTES_PER_FRAME, silencebuf, out_frames * BYTES_PER_FRAME);
	}

	// Update Visu: skip if the buffer wraps.
	if (!buffer_wraps && (silence || _buf_used(outputbuf) >  BYTES_PER_FRAME * output.current_sample_rate / 2)) {
		output_visu_export(visu_ptr, out_frames, output.current_sample_rate, silence, visu_gain);
	}
		
	oframes += out_frames;
	
	return out_frames;
}
/****************************************************************************************
 * Main output thread
 */
static void output_thread_i2s(void *arg) {
	size_t bytes;
	frames_t iframes = FRAME_BLOCK;
	uint32_t timer_start = 0;
	int discard = 0;
	uint32_t fullness = gettime_ms();
	bool synced;
	output_state state = OUTPUT_OFF - 1;
        
	while (running) {
			
		TIME_MEASUREMENT_START(timer_start);

		LOCK;
		
		// manage led display & analogue
		if (state != output.state) {
			LOG_INFO("Output state is %d", output.state);
			if (output.state == OUTPUT_OFF) {
				led_blink(LED_GREEN, 100, 2500);
				if (amp_control.gpio != -1) gpio_set_level_x(amp_control.gpio, !amp_control.active);
				LOG_INFO("switching off amp GPIO %d", amp_control.gpio);
			} else if (output.state == OUTPUT_STOPPED) {
                i2s_idle_since = pdTICKS_TO_MS(xTaskGetTickCount());
				adac->speaker(false);
				led_blink(LED_GREEN, 200, 1000);
			} else if (output.state == OUTPUT_RUNNING) {
				if (!jack_mutes_amp || !jack_inserted_svc()) {
					if (amp_control.gpio != -1) gpio_set_level_x(amp_control.gpio, amp_control.active);
					adac->speaker(true);
				}	
				led_on(LED_GREEN);
			}	
		}
		state = output.state;
		
		if (output.state == OUTPUT_OFF) {
			UNLOCK;
			if (isI2SStarted) {
				isI2SStarted = false;
				i2s_stop(CONFIG_I2S_NUM);
				adac->power(ADAC_STANDBY);
			}
			usleep(100000);
			continue;
		} else if (output.state == OUTPUT_STOPPED) {
			synced = false;
		}
					
		oframes = 0;
		output.updated = gettime_ms();
		output.frames_played_dmp = output.frames_played;
		// try to estimate how much we have consumed from the DMA buffer (calculation is incorrect at the very beginning ...)
		output.device_frames = dma_buf_frames - ((output.updated - fullness) * output.current_sample_rate) / 1000;
        // we'll try to produce iframes if we have any, but we might return less if outpuf does not have enough
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
            discard -= min(oframes, discard);
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
			adac->power(ADAC_ON);	
            if (spdif.enabled) spdif_convert(NULL, 0, NULL);
		} 

		// this does not work well as set_sample_rates resets the fifos (and it's too early)
		if (i2s_config.sample_rate != output.current_sample_rate) {
			LOG_INFO("changing sampling rate %u to %u", i2s_config.sample_rate, output.current_sample_rate);
			if (synced) {
			/* 				
				//  can sleep for a buffer_queue - 1 and then eat a buffer (discard) if we are synced
				usleep(((DMA_BUF_COUNT - 1) * DMA_BUF_LEN * BYTES_PER_FRAME * 1000) / 44100 * 1000);
				discard = DMA_BUF_COUNT * DMA_BUF_LEN * BYTES_PER_FRAME;
			*/		
			}	
			i2s_config.sample_rate = output.current_sample_rate;
			i2s_set_sample_rates(CONFIG_I2S_NUM, spdif.enabled ? i2s_config.sample_rate * 2 : i2s_config.sample_rate);
			i2s_zero_dma_buffer(CONFIG_I2S_NUM);

            equalizer_set_samplerate(output.current_sample_rate);
		}
		
		// run equalizer
		equalizer_process(obuf, oframes * BYTES_PER_FRAME);

		// we assume that here we have been able to entirely fill the DMA buffers
		if (spdif.enabled) {
			size_t obytes, count = 0;
			bytes = 0;
			// need IRAM for speed but can't allocate a FRAME_BLOCK * 16, so process by smaller chunks
			while (count < oframes) {
				size_t chunk = min(SPDIF_BLOCK, oframes - count);
                spdif_convert((ISAMPLE_T*) obuf + count * 2, chunk, (u32_t*) spdif.buf);              
				i2s_write(CONFIG_I2S_NUM, spdif.buf, chunk * 16, &obytes, portMAX_DELAY);
				bytes += obytes / (16 / BYTES_PER_FRAME);
				count += chunk;
			}
#if BYTES_PER_FRAME == 4		
		} else if (i2s_config.bits_per_sample == 32) {  
			i2s_write_expand(CONFIG_I2S_NUM, obuf, oframes * BYTES_PER_FRAME, 16, 32, &bytes, portMAX_DELAY);
#endif			
		} else {
			i2s_write(CONFIG_I2S_NUM, obuf, oframes * BYTES_PER_FRAME, &bytes, portMAX_DELAY);
		}

		fullness = gettime_ms();

		if (bytes != oframes * BYTES_PER_FRAME) {
			LOG_WARN("I2S DMA Overflow! available bytes: %d, I2S wrote %d bytes", oframes * BYTES_PER_FRAME, bytes);
		}
		
		SET_MIN_MAX( TIME_MEASUREMENT_GET(timer_start),i2s_time);
		
	}

	if (spdif.enabled) free(spdif.buf);
	ended = true;

	vTaskDelete(NULL);	
}

/****************************************************************************************
 * stats output callback
 */
static void i2s_stats(uint32_t now) {
    static uint32_t last;
    
    // first chain to next handler
    if (pseudo_idle_chain) pseudo_idle_chain(now);
    
    // then see if we need to act
    if (output.state <= OUTPUT_STOPPED || now < last + STATS_PERIOD_MS) return;  
    last = now;

	LOG_INFO( "Output State: %d, current sample rate: %d, bytes per frame: %d", output.state, output.current_sample_rate, BYTES_PER_FRAME);
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

/****************************************************************************************
 * SPDIF support
 */
#define PREAMBLE_B  (0xE8) //11101000
#define PREAMBLE_M  (0xE2) //11100010
#define PREAMBLE_W  (0xE4) //11100100

static const u8_t VUCP24[2] = { 0xCC, 0x32 };

static const u16_t spdif_bmclookup[256] = {
	0xcccc, 0xb333, 0xd333, 0xaccc, 0xcb33, 0xb4cc, 0xd4cc, 0xab33, 
	0xcd33, 0xb2cc, 0xd2cc, 0xad33, 0xcacc, 0xb533, 0xd533, 0xaacc, 
	0xccb3, 0xb34c, 0xd34c, 0xacb3, 0xcb4c, 0xb4b3, 0xd4b3, 0xab4c, 
	0xcd4c, 0xb2b3, 0xd2b3, 0xad4c, 0xcab3, 0xb54c, 0xd54c, 0xaab3, 
	0xccd3, 0xb32c, 0xd32c, 0xacd3, 0xcb2c, 0xb4d3, 0xd4d3, 0xab2c, 
	0xcd2c, 0xb2d3, 0xd2d3, 0xad2c, 0xcad3, 0xb52c, 0xd52c, 0xaad3, 
	0xccac, 0xb353, 0xd353, 0xacac, 0xcb53, 0xb4ac, 0xd4ac, 0xab53, 
	0xcd53, 0xb2ac, 0xd2ac, 0xad53, 0xcaac, 0xb553, 0xd553, 0xaaac, 
	0xcccb, 0xb334, 0xd334, 0xaccb, 0xcb34, 0xb4cb, 0xd4cb, 0xab34, 
	0xcd34, 0xb2cb, 0xd2cb, 0xad34, 0xcacb, 0xb534, 0xd534, 0xaacb, 
	0xccb4, 0xb34b, 0xd34b, 0xacb4, 0xcb4b, 0xb4b4, 0xd4b4, 0xab4b, 
	0xcd4b, 0xb2b4, 0xd2b4, 0xad4b, 0xcab4, 0xb54b, 0xd54b, 0xaab4, 
	0xccd4, 0xb32b, 0xd32b, 0xacd4, 0xcb2b, 0xb4d4, 0xd4d4, 0xab2b, 
	0xcd2b, 0xb2d4, 0xd2d4, 0xad2b, 0xcad4, 0xb52b, 0xd52b, 0xaad4, 
	0xccab, 0xb354, 0xd354, 0xacab, 0xcb54, 0xb4ab, 0xd4ab, 0xab54, 
	0xcd54, 0xb2ab, 0xd2ab, 0xad54, 0xcaab, 0xb554, 0xd554, 0xaaab, 
	0xcccd, 0xb332, 0xd332, 0xaccd, 0xcb32, 0xb4cd, 0xd4cd, 0xab32, 
	0xcd32, 0xb2cd, 0xd2cd, 0xad32, 0xcacd, 0xb532, 0xd532, 0xaacd, 
	0xccb2, 0xb34d, 0xd34d, 0xacb2, 0xcb4d, 0xb4b2, 0xd4b2, 0xab4d, 
	0xcd4d, 0xb2b2, 0xd2b2, 0xad4d, 0xcab2, 0xb54d, 0xd54d, 0xaab2, 
	0xccd2, 0xb32d, 0xd32d, 0xacd2, 0xcb2d, 0xb4d2, 0xd4d2, 0xab2d, 
	0xcd2d, 0xb2d2, 0xd2d2, 0xad2d, 0xcad2, 0xb52d, 0xd52d, 0xaad2, 
	0xccad, 0xb352, 0xd352, 0xacad, 0xcb52, 0xb4ad, 0xd4ad, 0xab52, 
	0xcd52, 0xb2ad, 0xd2ad, 0xad52, 0xcaad, 0xb552, 0xd552, 0xaaad, 
	0xccca, 0xb335, 0xd335, 0xacca, 0xcb35, 0xb4ca, 0xd4ca, 0xab35, 
	0xcd35, 0xb2ca, 0xd2ca, 0xad35, 0xcaca, 0xb535, 0xd535, 0xaaca, 
	0xccb5, 0xb34a, 0xd34a, 0xacb5, 0xcb4a, 0xb4b5, 0xd4b5, 0xab4a, 
	0xcd4a, 0xb2b5, 0xd2b5, 0xad4a, 0xcab5, 0xb54a, 0xd54a, 0xaab5, 
	0xccd5, 0xb32a, 0xd32a, 0xacd5, 0xcb2a, 0xb4d5, 0xd4d5, 0xab2a, 
	0xcd2a, 0xb2d5, 0xd2d5, 0xad2a, 0xcad5, 0xb52a, 0xd52a, 0xaad5, 
	0xccaa, 0xb355, 0xd355, 0xacaa, 0xcb55, 0xb4aa, 0xd4aa, 0xab55, 
	0xcd55, 0xb2aa, 0xd2aa, 0xad55, 0xcaaa, 0xb555, 0xd555, 0xaaaa	
};

/* 
 SPDIF is supposed to be (before BMC encoding, from LSB to MSB)				
    0....  1...   191..  0
    BLFMRF MLFWRF MLFWRF BLFMRF (B,M,W=preamble-4, L/R=left/Right-24, F=Flags-4)
    each xLF pattern is 32 bits 
	PPPP AAAA  SSSS SSSS  SSSS SSSS  SSSS VUCP (P=preamble, A=auxiliary, S=sample-20bits, V=valid, U=user data, C=channel status, P=parity)
 After BMC encoding, each bit becomes 2 hence this becomes a 64 bits word. The parity
 is fixed by changing AAAA bits so that VUPC does not change. Then then trick is to 
 start not with a PPPP sequence but with an VUCP sequence to that the 16 bits samples
 are aligned with a BMC word boundary. Input buffer is left first => LRLR...
 The I2S interface must output first the B/M/W preamble which means that second
 32 bits words must be first and so must be marked right channel. 
*/
static void IRAM_ATTR spdif_convert(ISAMPLE_T *src, size_t frames, u32_t *dst) {
    static u8_t vu, count;    
	register u16_t hi, lo;
#if BYTES_PER_FRAME == 8
	register u16_t aux;
#endif
    
    // we assume frame == 0 as well...
    if (!src) {
        count = 0;
        vu = VUCP24[0];
    }
    
	while (frames--) {
		// start with left channel
#if BYTES_PER_FRAME == 4		
		hi = spdif_bmclookup[(u8_t)(*src >> 8)];
		lo = spdif_bmclookup[(u8_t)*src++];
		if (lo & 1) hi = ~hi;

        if (!count--) {            
			*dst++ = (vu << 24) | (PREAMBLE_B << 16) | 0xCCCC;
			count = 191;
		} else {
			*dst++ = (vu << 24) | (PREAMBLE_M << 16) | 0xCCCC;
		}
#else
		hi = spdif_bmclookup[(u8_t)(*src >> 24)];
		lo = spdif_bmclookup[(u8_t)(*src >> 16)];
		aux = spdif_bmclookup[(u8_t)(*src++ >> 8)];
		if (aux & 1) lo = ~lo;
		if (lo & 1) hi = ~hi;


        if (!count--) {
			*dst++ = (vu << 24) | (PREAMBLE_B << 16) | aux;
			count = 191;
		} else {
			*dst++ = (vu << 24) | (PREAMBLE_M << 16) | aux;
		}
#endif

        vu = VUCP24[hi & 1];
		*dst++ = ((u32_t)lo << 16) | hi;

		// then do right channel, no need to check PREAMBLE_B
#if BYTES_PER_FRAME == 4		
		hi = spdif_bmclookup[(u8_t)(*src >> 8)];
		lo = spdif_bmclookup[(u8_t)*src++];
		if (lo & 1) hi = ~hi;

		*dst++ = (vu << 24) | (PREAMBLE_W << 16) | 0xCCCC;
#else
		hi = spdif_bmclookup[(u8_t)(*src >> 24)];
		lo = spdif_bmclookup[(u8_t)(*src >> 16)];
		aux = spdif_bmclookup[(u8_t)(*src++ >> 8)];
		if (aux & 1) lo = ~lo;
		if (lo & 1) hi = ~hi;

		*dst++ = (vu << 24) | (PREAMBLE_W << 16) | aux;
#endif

        vu = VUCP24[hi & 1];
		*dst++ = ((u32_t)lo << 16) | hi;
	}
}
