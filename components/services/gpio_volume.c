#include "gpio_volume.h"
#include "gpio_exp.h"
#include "esp_log.h"
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

static inline void gv_gpio_out(int gpio, int level)
{
	if (gpio < GPIO_NUM_MAX)
	{
		ESP_LOGE(TAG, "GPIO %d is not an expander GPIO", gpio);
		return;
	}

	gpio_exp_set_direction(gpio, GPIO_MODE_OUTPUT, NULL);
	gpio_exp_set_level(gpio, level, true, NULL);
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

	cfg.dacmaxvol = get_int(cfgstr, "dacmaxvol", false);
	cfg.lsb0 = get_int(cfgstr, "lsb0", -1);
	cfg.lsb1 = get_int(cfgstr, "lsb1", -1);
	cfg.high0 = get_int(cfgstr, "high0", -1);
	cfg.high1 = get_int(cfgstr, "high1", -1);
	cfg.width = get_int(cfgstr, "width", 0);
	cfg.time_ms = get_int(cfgstr, "time", 5);

	cfg.loud = get_bool(cfgstr, "loud", true);
	cfg.highON = get_bool(cfgstr, "highON", true);
	cfg.lowON = get_bool(cfgstr, "lowON", true);
	cfg.mode = get_mode(cfgstr);

	ESP_LOGI(TAG,
			 "gpio_volume: mode=%d dacmaxvol=%d width=%d lsb0=%d",
			 cfg.mode, cfg.dacmaxvol, cfg.width, cfg.lsb0);

	if (cfg.width <= 0 || cfg.lsb0 < 0)
	{
		ESP_LOGE(TAG, "Invalid gpio_volume configuration");
		return false;
	}

	// --- 1. Initialize the primary bank (lsb0) ---
	for (int i = 0; i < cfg.width; i++) {
		gpio_exp_set_direction(cfg.lsb0 + i, GPIO_MODE_OUTPUT, NULL);
	}

	// --- 2. Initialize Latching-specific pins ---
	if (cfg.mode == GPIO_VOLUME_MODE_LATCHING) {
		// Mode A: Secondary bank for "Not-Loud"
		if (cfg.lsb1 >= 0) {
			for (int i = 0; i < cfg.width; i++) {
				gpio_exp_set_direction(cfg.lsb1 + i, GPIO_MODE_OUTPUT, NULL);
			}
		}
		
		// Mode B: Toggle Rails
		if (cfg.high0 >= 0) {
			gpio_exp_set_direction(cfg.high0, GPIO_MODE_OUTPUT, NULL);
			// Ensure they start in the inactive state
			gpio_exp_set_level(cfg.high0, !cfg.highON, true, NULL);
		}
		if (cfg.high1 >= 0) {
			gpio_exp_set_direction(cfg.high1, GPIO_MODE_OUTPUT, NULL);
			gpio_exp_set_level(cfg.high1, !cfg.highON, true, NULL);
		}
	}

	configured = true;
	latching_initialized = false;
	return true;
}

int gpio_volume_fixed(void)
{
	return configured ? cfg.dacmaxvol : -1;  // -1 = not configured
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
	uint32_t active_vals_loud  = cfg.lowON ? to_loud_mask : 0; 
	uint32_t active_vals_quiet = cfg.lowON ? to_quiet_mask : 0;

	// Create value masks for the 'inactive' state (inverse of active level)
	uint32_t inactive_vals_loud  = cfg.lowON ? 0 : to_loud_mask;
	uint32_t inactive_vals_quiet = cfg.lowON ? 0 : to_quiet_mask;

	if (cfg.lsb1 >= 0)
	{
		/* --- MODE A: Two separate banks (lsb0 for Loud, lsb1 for Not-Loud) --- */
		
		// 1. Activate relays going to not-loud (lsb1)
		if (to_quiet_mask) {
			gpio_exp_set_level_multi(cfg.lsb1, to_quiet_mask, active_vals_quiet, NULL);
		}

		// 2. Activate relays going to loud (lsb0)
		if (to_loud_mask) {
			gpio_exp_set_level_multi(cfg.lsb0, to_loud_mask, active_vals_loud, NULL);
		}

		// 3. Wait pulse time
		vTaskDelay(pdMS_TO_TICKS(cfg.time_ms));

		// 4. Turn first set of relays off (not-loud / lsb1)
		if (to_quiet_mask) {
			gpio_exp_set_level_multi(cfg.lsb1, to_quiet_mask, inactive_vals_quiet, NULL);
		}

		// 5. Turn second set of relays off (loud / lsb0)
		if (to_loud_mask) {
			gpio_exp_set_level_multi(cfg.lsb0, to_loud_mask, inactive_vals_loud, NULL);
		}
	}
	else
	{
		/* --- MODE B: Toggle / Common Rails (lsb0=Select, high0/1=Direction) --- */
		
		// 1. Toggle to NOT-LOUD (high0 active, high1 inactive)
		if (to_quiet_mask)
		{
			// Set direction for Quiet
			gv_gpio_out(cfg.high0, cfg.highON);
			gv_gpio_out(cfg.high1, !cfg.highON);

			// Activate select pins for bits moving to Quiet
			gpio_exp_set_level_multi(cfg.lsb0, to_quiet_mask, active_vals_quiet, NULL);
			
			vTaskDelay(pdMS_TO_TICKS(cfg.time_ms));
			
			// Deactivate select pins
			gpio_exp_set_level_multi(cfg.lsb0, to_quiet_mask, inactive_vals_quiet, NULL);

			// Reset direction
			gv_gpio_out(cfg.high0, !cfg.highON);
		}

		// 2. Toggle to LOUD (high1 active, high0 inactive)
		if (to_loud_mask)
		{
			// Set direction for Loud
			gv_gpio_out(cfg.high1, cfg.highON);
			gv_gpio_out(cfg.high0, !cfg.highON);

			// Activate select pins for bits moving to Loud
			gpio_exp_set_level_multi(cfg.lsb0, to_loud_mask, active_vals_loud, NULL);
			
			vTaskDelay(pdMS_TO_TICKS(cfg.time_ms));
			
			// Deactivate select pins
			gpio_exp_set_level_multi(cfg.lsb0, to_loud_mask, inactive_vals_loud, NULL);

			// Reset direction
			gv_gpio_out(cfg.high1, !cfg.highON);
		}
	}
}

static uint8_t gain_to_volume(unsigned gain) {
	// 1. Convert 16-bit gain to attenuation value
	// 100 is max volume, going down 1 notch per 50/101 dB until 25
	// 25 .. 0 is 1 notch per 150/101 dB
	// https://github.com/LMS-Community/slimserver/blob/0821de50e7c3acd4f35b10aaccf99c64269e4187/Slim/Player/Squeezebox2.pm#L253
	
	if (gain == 0) return 0;
	if (gain > 61953) return 100;

	float dB = 20.0f * log10f((float)gain / 65536.0f);

	uint8_t steps = (uint8_t) (-dB * 101.0f / 50.0f + 0.5f);
	if (gain < 926 ) {
		steps = (uint8_t) ( -(dB + 36.997f) * 101.0f / 150.0f + 75.5f);
	}
	ESP_LOGD(TAG, "Update: gain=%u dB=%6.2f steps=%u", gain, dB, steps);

	return steps > 100 ? 0 : 100 - steps;
}

/* =========================================================
* Main update logic
* ========================================================= */

void gpio_volume_update(unsigned gain)
{
	if (!configured || gain == last_gain)
		return;

	uint8_t volume = gain_to_volume(gain);

	// Calculate masks
	uint32_t total_mask = (1 << cfg.width) - 1;
	uint32_t target_bits = 0;

	if (cfg.mode == GPIO_VOLUME_MODE_LEDBAR) {
		// Calculate number of LEDs based on volume 0..100
		// Ensure volume 100 lights up all LEDs, volume 0 lights up 0
		int num_leds = (volume * cfg.width) / 100;
		if (num_leds > cfg.width) num_leds = cfg.width;

		target_bits = (1 << num_leds) - 1;
	} else {
		// BINARY or LATCHING: map volume directly to binary value
		uint32_t max_bits = (1UL << cfg.width) - 1;
		target_bits = ((uint32_t)volume * max_bits + 50) / 100;
		// target_bits = volume > max_bits ? max_bits : volume;
	}

	// If the target is the same as the last set volume, DO NOTHING.
	if (target_bits == last_volume) {
		last_gain = gain; // Update gain even if volume index didn't change
		return;
	}

	ESP_LOGI(TAG, "Update: gain=%u volume=%u target_bits=0x%02x", gain, volume, target_bits);

	if (cfg.mode == GPIO_VOLUME_MODE_LATCHING)
	{
		if (!latching_initialized)
			return;

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
		// Construct the value mask based on cfg.loud (polarity)

		uint32_t val_mask = 0;

		if (cfg.loud) {
			// Active High: target bits are 1
			val_mask = target_bits;
		} else {
			// Active Low: target bits are 0 (so we invert target bits, then mask)
			// But gpio_exp_set_level_multi takes 'val' as the raw level to write.
			// If bit is in target (active), we want 0. If bit is NOT in target (inactive), we want 1.
			val_mask = (~target_bits) & total_mask;
		}

		gpio_exp_set_level_multi(cfg.lsb0, total_mask, val_mask, NULL);
		last_volume = target_bits;
	}

	last_gain = gain;
}

void gpio_volume_apply_startup_volume(unsigned gain)
{
	if (!configured)
		return;

	// Use consistent volume calculation
	uint8_t volume = gain_to_volume(gain);
	
	uint32_t target_bits = 0;
	uint32_t total_mask = (1 << cfg.width) - 1;

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
