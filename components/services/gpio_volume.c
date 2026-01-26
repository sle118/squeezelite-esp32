#include "gpio_volume.h"
#include "gpio_exp.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <math.h> 

static const char *TAG = "gpio_volume";

static gpio_volume_cfg_t cfg;
static bool configured = false;
static uint8_t last_volume = 0;
static unsigned last_gain = 0;

/* latching state */
static bool latching_initialized = false;

/* =========================================================
 * helpers
 * ========================================================= */

static int get_int(const char *s, const char *k, int def)
{
	char *p = strstr(s, k);
	if (!p)
		return def;
	p += strlen(k);
	if (*p != '=')
		return def;
	return atoi(p + 1);
}

static bool get_bool(const char *s, const char *k, bool def)
{
	return get_int(s, k, def ? 1 : 0) != 0;
}

static gpio_volume_mode_t get_mode(const char *s)
{
	if (strstr(s, "mode=ledbar"))
		return GPIO_VOLUME_MODE_LEDBAR;
	if (strstr(s, "mode=latching"))
		return GPIO_VOLUME_MODE_LATCHING;
	return GPIO_VOLUME_MODE_BINARY;
}

static void parse_gpio_with_level(const char *s, const char *k, int *gpio, int *level, int def_gpio, int def_level)
{
	char *p = strstr(s, k);
	if (!p) {
		*gpio = def_gpio;
		*level = def_level;
		return;
	}
	
	p += strlen(k);
	if (*p != '=') {
		*gpio = def_gpio;
		*level = def_level;
		return;
	}
	
	p++; // Skip '='
	*gpio = atoi(p);
	
	// Look for optional :level suffix
	char *colon = strchr(p, ':');
	if (colon && (colon < strchr(p, ',') || !strchr(p, ','))) {
		*level = atoi(colon + 1);
	} else {
		*level = def_level;
	}
}

static inline void gv_gpio_out(int gpio, int level)
{
	if (gpio < GPIO_NUM_MAX)
	{
		ESP_LOGE(TAG, "GPIO %d is not an expander GPIO", gpio);
		return;
	}

	esp_err_t err = gpio_exp_set_direction(gpio, GPIO_MODE_OUTPUT, NULL);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to set GPIO %d direction: %s", gpio, esp_err_to_name(err));
	}
	err = gpio_exp_set_level(gpio, level, true, NULL);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to set GPIO %d level: %s", gpio, esp_err_to_name(err));
	}

}

/* =========================================================
 * initialization
 * ========================================================= */

bool gpio_volume_init(const char *cfgstr)
{
	if (!cfgstr || !*cfgstr)
	{
		ESP_LOGI(TAG, "gpio_volume not configured");
		return false;
	}

	// memset(&cfg, 0, sizeof(cfg));

	cfg.mode = get_mode(cfgstr);

	// Parse GPIOs with optional level suffixes
	parse_gpio_with_level(cfgstr, "lsb0", &cfg.lsb0, &cfg.lsb0_level, -1, 1);
	parse_gpio_with_level(cfgstr, "lsb1", &cfg.lsb1, &cfg.lsb1_level, -1, 1);
	parse_gpio_with_level(cfgstr, "high0", &cfg.high0, &cfg.high0_level, -1, 1);
	parse_gpio_with_level(cfgstr, "high1", &cfg.high1, &cfg.high1_level, -1, 1);

	cfg.width = get_int(cfgstr, "width", 0);
	cfg.time_ms = get_int(cfgstr, "time", 10);

	cfg.dacmax = get_bool(cfgstr, "dacmax", false);
	cfg.visumax = get_bool(cfgstr, "visumax", false);
	cfg.loud = get_bool(cfgstr, "loud", true);

	ESP_LOGI(TAG,
			 "gpio_volume: mode=%d dacmax=%d visumax=%d width=%d lsb0=%d:%d lsb1=%d:%d high0=%d:%d high1=%d:%d time=%d",
			 cfg.mode, cfg.dacmax, cfg.visumax, cfg.width, 
			 cfg.lsb0, cfg.lsb0_level, cfg.lsb1, cfg.lsb1_level,
			 cfg.high0, cfg.high0_level, cfg.high1, cfg.high1_level, 
			 cfg.time_ms);

	if (cfg.width <= 0 || cfg.lsb0 < 0)
	{
		ESP_LOGE(TAG, "Invalid gpio_volume configuration");
		return false;
	}

	// --- 1. Initialize GPIOs (Select or Quiet) ---
	int inactive_level_lsb0 = !cfg.lsb0_level;
	for (int i = 0; i < cfg.width; i++) {
		int gpio = cfg.lsb0 + i;
		esp_err_t err = gpio_exp_set_direction(gpio, GPIO_MODE_OUTPUT, NULL);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Failed to set GPIO %d as output: %s", gpio, esp_err_to_name(err));
			return false;
		}
		err = gpio_exp_set_level(gpio, inactive_level_lsb0, true, NULL);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Failed to set GPIO %d to inactive level: %s", gpio, esp_err_to_name(err));
		}
		ESP_LOGD(TAG, "Initialized GPIO %d as output at inactive level %d", gpio, inactive_level_lsb0);
	}

	// --- 2. Initialize Latching-specific pins ---
	if (cfg.mode == GPIO_VOLUME_MODE_LATCHING) {
		// Mode A: Secondary bank for Loud
		if (cfg.lsb1 >= 0) {
			int inactive_level_lsb1 = !cfg.lsb1_level;
			for (int i = 0; i < cfg.width; i++) {
				int gpio = cfg.lsb1 + i;
				esp_err_t err = gpio_exp_set_direction(gpio, GPIO_MODE_OUTPUT, NULL);
				if (err != ESP_OK) {
					ESP_LOGE(TAG, "Failed to set GPIO %d as output: %s", gpio, esp_err_to_name(err));
					return false;
				}
				err = gpio_exp_set_level(gpio, inactive_level_lsb1, true, NULL);
				if (err != ESP_OK) {
					ESP_LOGE(TAG, "Failed to set GPIO %d to inactive level: %s", gpio, esp_err_to_name(err));
				}
				ESP_LOGD(TAG, "Initialized GPIO %d as output at inactive level %d", gpio, inactive_level_lsb1);
			}
		}

		// Mode B: Toggle Rails
		if (cfg.high0 >= 0) {
			gpio_exp_set_direction(cfg.high0, GPIO_MODE_OUTPUT, NULL);
			// Ensure they start in the inactive state
			gpio_exp_set_level(cfg.high0, !cfg.high0_level, true, NULL);
			ESP_LOGD(TAG, "Initialized high0 GPIO %d at inactive level %d", cfg.high0, !cfg.high0_level);
	}
		if (cfg.high1 >= 0) {
			gpio_exp_set_direction(cfg.high1, GPIO_MODE_OUTPUT, NULL);
			gpio_exp_set_level(cfg.high1, !cfg.high1_level, true, NULL);
			ESP_LOGD(TAG, "Initialized high1 GPIO %d at inactive level %d", cfg.high1, !cfg.high1_level);
		}
	}
	
	vTaskDelay(2); // Give expander time to update 

	configured = true;
	latching_initialized = false;
	ESP_LOGI(TAG, "gpio_volume initialization complete");

	return true;
}

gpio_max_mode_t gpio_volume_get_mode(void)
{
	gpio_max_mode_t mode = { false, false, false };  // Safe defaults
	
	if (configured) {
		mode.active = true;
		mode.dac_fixed = cfg.dacmax;
		mode.visu_fixed = cfg.visumax;
	}
	
	return mode;
}

/* =========================================================
 * latching mechanism
 * ========================================================= */

// Updates multiple latching relays simultaneously
static void latch_byte(uint32_t to_loud_mask, uint32_t to_quiet_mask)
{
	if (to_loud_mask == 0 && to_quiet_mask == 0) {
		return;
	}

	ESP_LOGD(TAG, "Latch byte: LoudMask=0x%x QuietMask=0x%x", to_loud_mask, to_quiet_mask);

	// Determine active/inactive levels based on configuration
	// If lowON is true (1), active level is 1. If lowON is false (0), active level is 0.
	// gpio_exp_set_level_multi takes a 'val' mask where bits match the desired level.
	
	// Create value masks for the 'active' state
	uint32_t active_vals_loud  = cfg.lsb0_level ? to_loud_mask : 0; 
	uint32_t active_vals_quiet = cfg.lsb0_level ? to_quiet_mask : 0;

	// Create value masks for the 'inactive' state (inverse of active level)
	uint32_t inactive_vals_loud  = cfg.lsb0_level ? 0 : to_loud_mask;
	uint32_t inactive_vals_quiet = cfg.lsb0_level ? 0 : to_quiet_mask;

	int64_t start_time = esp_timer_get_time();  

	if (cfg.lsb1 >= 0)
	{
		/* --- MODE A: Two separate banks (lsb0 for Quiet, lsb1 for Loud) --- */ 
		
		// Need separate active/inactive values for lsb1 based on lsb1_level
		uint32_t active_vals_loud_lsb1  = cfg.lsb1_level ? to_loud_mask : 0;
		uint32_t inactive_vals_loud_lsb1 = cfg.lsb1_level ? 0 : to_loud_mask;

		// 1. Activate relays going to quiet (lsb0)
		if (to_quiet_mask) {
			gpio_exp_set_level_multi(cfg.lsb0, to_quiet_mask, active_vals_quiet, NULL);
		}

		// 2. Activate relays going to loud (lsb1)
		if (to_loud_mask) {
			gpio_exp_set_level_multi(cfg.lsb1, to_loud_mask, active_vals_loud_lsb1, NULL);
		}

		int64_t coil_on_time = esp_timer_get_time();

		// 3. Wait pulse time, precise timing, blocking
		esp_rom_delay_us(cfg.time_ms * 1000);

		int64_t coil_off_time = esp_timer_get_time();

		// 4. Turn first set of relays off (quiet / lsb0)
		if (to_quiet_mask) {
			gpio_exp_set_level_multi(cfg.lsb0, to_quiet_mask, inactive_vals_quiet, NULL);
		}

		// 5. Turn second set of relays off (loud / lsb1)
		if (to_loud_mask) {
			gpio_exp_set_level_multi(cfg.lsb1, to_loud_mask, inactive_vals_loud_lsb1, NULL);
		}

		int64_t end_time = esp_timer_get_time();

		float coil_active_ms = (coil_off_time - coil_on_time) / 1000.0f;
		float total_ms = (end_time - start_time) / 1000.0f;
		ESP_LOGD(TAG, "Latch timing: coil_active=%.2fms (cfg=%dms) total=%.2fms", 
				 coil_active_ms, cfg.time_ms, total_ms);
	}
	else
	{
		/* --- MODE B: Toggle / Common Rails (lsb0=Select, high0/1=Direction) --- */

		int64_t quiet_coil_on = 0, quiet_coil_off = 0;
		int64_t loud_coil_on = 0, loud_coil_off = 0;

		// 1. Toggle to NOT-LOUD (high0 active, high1 inactive)
		if (to_quiet_mask)
		{
			// Set direction for Quiet
			gv_gpio_out(cfg.high0, cfg.high0_level);
			gv_gpio_out(cfg.high1, !cfg.high1_level);

			// Activate select pins for bits moving to Quiet
			gpio_exp_set_level_multi(cfg.lsb0, to_quiet_mask, active_vals_quiet, NULL);

			quiet_coil_on = esp_timer_get_time();
			esp_rom_delay_us(cfg.time_ms * 1000);
			quiet_coil_off = esp_timer_get_time(); 

			// Deactivate select pins
			gpio_exp_set_level_multi(cfg.lsb0, to_quiet_mask, inactive_vals_quiet, NULL);

			// Reset direction
			gv_gpio_out(cfg.high0, !cfg.high0_level);
		}

		// 2. Toggle to LOUD (high1 active, high0 inactive)
		if (to_loud_mask)
		{
			// Set direction for Loud
			gv_gpio_out(cfg.high1, cfg.high1_level);
			gv_gpio_out(cfg.high0, !cfg.high0_level);

			// Activate select pins for bits moving to Loud
			gpio_exp_set_level_multi(cfg.lsb0, to_loud_mask, active_vals_loud, NULL);

			loud_coil_on = esp_timer_get_time();
			esp_rom_delay_us(cfg.time_ms * 1000);
			loud_coil_off = esp_timer_get_time();

			// Deactivate select pins
			gpio_exp_set_level_multi(cfg.lsb0, to_loud_mask, inactive_vals_loud, NULL);

			// Reset direction
			gv_gpio_out(cfg.high1, !cfg.high1_level);
		}

		int64_t end_time = esp_timer_get_time();
		float total_ms = (end_time - start_time) / 1000.0f;
		
		if (to_quiet_mask && to_loud_mask) {
			float quiet_ms = (quiet_coil_off - quiet_coil_on) / 1000.0f;
			float loud_ms = (loud_coil_off - loud_coil_on) / 1000.0f;
			ESP_LOGD(TAG, "Latch timing: quiet_coil=%.2fms loud_coil=%.2fms (cfg=%dms) total=%.2fms",
					 quiet_ms, loud_ms, cfg.time_ms, total_ms);
		} else if (to_quiet_mask) {
			float quiet_ms = (quiet_coil_off - quiet_coil_on) / 1000.0f;
			ESP_LOGD(TAG, "Latch timing: quiet_coil=%.2fms (cfg=%dms) total=%.2fms",
					 quiet_ms, cfg.time_ms, total_ms);
		} else {
			float loud_ms = (loud_coil_off - loud_coil_on) / 1000.0f;
			ESP_LOGD(TAG, "Latch timing: loud_coil=%.2fms (cfg=%dms) total=%.2fms",
					 loud_ms, cfg.time_ms, total_ms);
		}
	}
}

#define VOLUME_LOW_GAIN_THRESHOLD 926
#define VOLUME_DB_PER_STEP_HIGH 0.4950495f  // 50/101
#define VOLUME_DB_PER_STEP_LOW 1.4851485f   // 150/101
#define VOLUME_LOW_GAIN_OFFSET 36.997f
#define VOLUME_LOW_STEP_OFFSET 75.5f

static uint8_t gain_to_volume(unsigned gain) {
	// 1. Convert 16-bit gain to attenuation value
	// 100 is max volume, going down 1 notch per 50/101 dB until 25
	// 25 .. 0 is 1 notch per 150/101 dB
	// https://github.com/LMS-Community/slimserver/blob/0821de50e7c3acd4f35b10aaccf99c64269e4187/Slim/Player/Squeezebox2.pm#L253

	if (gain == 0) return 0;
	if (gain > 61953) return 100;

	float dB = 20.0f * log10f((float)gain / 65536.0f);

	uint8_t steps;
	if (gain < VOLUME_LOW_GAIN_THRESHOLD ) {
		steps = (uint8_t) (-(dB + VOLUME_LOW_GAIN_OFFSET) / VOLUME_DB_PER_STEP_LOW + VOLUME_LOW_STEP_OFFSET);
	} else {
		steps = (uint8_t) (-dB / VOLUME_DB_PER_STEP_HIGH + 0.5f);
	}
	ESP_LOGD(TAG, "Update: gain=%u dB=%6.2f steps=%u", gain, dB, steps);

	return steps > 100 ? 0 : 100 - steps;
}

/* =========================================================
* Main update logic
* ========================================================= */

static esp_timer_handle_t vol_update_timer = NULL;
static unsigned pending_gain = 0;

static void vol_update_timer_cb(void* arg)
{
	// 70ms has passed since last update - apply the pending gain
	unsigned gain = pending_gain;

	uint8_t volume = gain_to_volume(gain);

	// Calculate masks
	uint32_t total_mask = (1UL << cfg.width) - 1;
	uint32_t target_bits = 0;

	if (cfg.mode == GPIO_VOLUME_MODE_LEDBAR) {
		// Calculate number of LEDs based on volume 0..100
		int num_leds = (volume * cfg.width + 50) / 100;
		if (num_leds > cfg.width) num_leds = cfg.width;
		target_bits = (1 << num_leds) - 1;
	} else {
		// BINARY or LATCHING: map volume directly to binary value
		uint32_t max_bits = (1UL << cfg.width) - 1;
		target_bits = ((uint32_t)volume * max_bits + 50) / 100;
		// target_bits = volume > max_bits ? max_bits : volume;
	}
	if (target_bits == last_volume) {
		last_gain = gain;
		return;
	}
	
	ESP_LOGI(TAG, "Update: gain=%u volume=%u target_bits=0x%02x", gain, volume, target_bits);

	if (cfg.mode == GPIO_VOLUME_MODE_LATCHING)
	{
		if (!latching_initialized) {
			last_gain = gain; 
			return;
		}
		// Calculate changed bits
		uint32_t diff = target_bits ^ last_volume;
		if (diff) {
			// Bits changing from 0 to 1 need to go LOUD
			uint32_t to_loud = diff & target_bits;
			// Bits changing from 1 to 0 need to go QUIET
			uint32_t to_quiet = diff & (~target_bits);
			latch_byte(to_loud, to_quiet);
			last_volume = target_bits;
		}
	}
	else if (cfg.mode == GPIO_VOLUME_MODE_BINARY || cfg.mode == GPIO_VOLUME_MODE_LEDBAR)
	{
		// For non-latching, we just set the levels.
		uint32_t val_mask = cfg.loud ? target_bits : ((~target_bits) & total_mask);
		gpio_exp_set_level_multi(cfg.lsb0, total_mask, val_mask, NULL);
		last_volume = target_bits;
	}

	last_gain = gain;
}

void gpio_volume_update(unsigned gain)
{
	if (!configured) return;

	if (gain == pending_gain && vol_update_timer != NULL) return; 
		// Same gain already queued, no action needed

	// Store the pending gain
	pending_gain = gain;

	// Cancel any existing timer
	if (vol_update_timer != NULL) {
		esp_timer_stop(vol_update_timer);
	} else {
		// Create timer on first use
		esp_timer_create_args_t timer_args = {
			.callback = vol_update_timer_cb,
			.name = "vol_update"
		};
		esp_timer_create(&timer_args, &vol_update_timer);
	}

	// Start/restart the timer for 70ms
	esp_timer_start_once(vol_update_timer, 70 * 1000); // microseconds

	ESP_LOGD(TAG, "Queue volume update: gain=%u (waiting 70ms)", gain);
}

void gpio_volume_apply_startup_volume(unsigned gain)
{
	if (!configured)
		return;

	// Use consistent volume calculation
	uint8_t volume = gain_to_volume(gain);
	
	uint32_t target_bits = 0;
	uint32_t total_mask = (1UL << cfg.width) - 1;

	if (cfg.mode == GPIO_VOLUME_MODE_LEDBAR) {
		int num_leds = (volume * cfg.width) / 100;
		if (num_leds > cfg.width) num_leds = cfg.width;
		target_bits = (1 << num_leds) - 1;
	} else {
		uint32_t max_bits = (1UL << cfg.width) - 1;
		target_bits = ((uint32_t)volume * max_bits + 50) / 100;
		// target_bits = volume > max_bits ? max_bits : volume;
	}

	if (cfg.mode == GPIO_VOLUME_MODE_LATCHING)
	{
		ESP_LOGI(TAG, "Syncing relays: Gain %u -> Volume %u -> Binary 0x%02x", gain, volume, target_bits);
		
		// For startup, we must assume all relays are in an unknown state.
		// We force all bits meant to be LOUD to pulse Loud,
		// and all bits meant to be QUIET (active in total_mask but 0 in target) to pulse Quiet.
		
		uint32_t to_loud = target_bits;
		uint32_t to_quiet = (~target_bits) & total_mask;
		
		latch_byte(to_loud, to_quiet);
		
		last_volume = target_bits;
		latching_initialized = true;
	}
	else
	{
		// For standard modes, just call update, forcing gain to differ slightly if needed or just resetting last_gain
		last_gain = gain + 1; // Force update
		last_volume = target_bits + 1; // Force update
		gpio_volume_update(gain);
	}
}
