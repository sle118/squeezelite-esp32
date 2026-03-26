/*
 *  Squeezelite for esp32
 *
 *  PWM master volume backend for external analog control paths.
 *
 */

#include "pwm_mastervol.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "globdefs.h"
#include "squeezelite.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#define LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE
#else
#define LEDC_SPEED_MODE LEDC_HIGH_SPEED_MODE
#endif

static const char *TAG = "pwm_mastervol";

static struct {
	bool enabled;
	bool initialized;
	ledc_channel_t channel;
	int gpio;
	uint32_t duty_min;
	uint32_t duty_max;
} pwm_mastervol;

static uint32_t clamp_duty(uint32_t duty) {
	uint32_t duty_limit = pwm_system.max ? (uint32_t) pwm_system.max - 1 : 0;
	if (duty > duty_limit) duty = duty_limit;
	return duty;
}

static uint32_t volume_to_duty(unsigned left, unsigned right) {
	uint32_t volume = left > right ? left : right;
	uint32_t duty_span;

	if (volume > FIXED_ONE) volume = FIXED_ONE;

	if (volume == 0 || pwm_mastervol.duty_max <= pwm_mastervol.duty_min) {
		return pwm_mastervol.duty_min;
	}

	if (volume >= FIXED_ONE) {
		return pwm_mastervol.duty_max;
	}

	duty_span = pwm_mastervol.duty_max - pwm_mastervol.duty_min;
	return pwm_mastervol.duty_min + (uint32_t) (((uint64_t) duty_span * volume) / FIXED_ONE);
}

static void apply_duty(uint32_t duty) {
	if (!pwm_mastervol.enabled || !pwm_mastervol.initialized) return;

	duty = clamp_duty(duty);
	ledc_set_duty(LEDC_SPEED_MODE, pwm_mastervol.channel, duty);
	ledc_update_duty(LEDC_SPEED_MODE, pwm_mastervol.channel);
}

void pwm_mastervol_init(void) {
	uint32_t duty_min = clamp_duty(CONFIG_PWM_MASTERVOL_DUTY_MIN);
	uint32_t duty_max = clamp_duty(CONFIG_PWM_MASTERVOL_DUTY_MAX);

	if (pwm_mastervol.initialized) return;

	pwm_mastervol.enabled = false;
	pwm_mastervol.initialized = false;
	pwm_mastervol.gpio = CONFIG_PWM_MASTERVOL_GPIO;

	if (pwm_mastervol.gpio < 0) return;

	if (duty_max < duty_min) {
		uint32_t tmp = duty_min;
		duty_min = duty_max;
		duty_max = tmp;
	}

	if (pwm_system.base_channel >= LEDC_CHANNEL_MAX) {
		ESP_LOGE(TAG, "no LEDC channel available for GPIO %d", pwm_mastervol.gpio);
		return;
	}

	pwm_mastervol.channel = pwm_system.base_channel++;
	pwm_mastervol.duty_min = duty_min;
	pwm_mastervol.duty_max = duty_max;

	ledc_channel_config_t ledc_channel = {
		.channel = pwm_mastervol.channel,
		.duty = pwm_mastervol.duty_min,
		.gpio_num = pwm_mastervol.gpio,
		.speed_mode = LEDC_SPEED_MODE,
		.hpoint = 0,
		.timer_sel = pwm_system.timer,
	};

	ledc_channel_config(&ledc_channel);

	pwm_mastervol.enabled = true;
	pwm_mastervol.initialized = true;

	ESP_LOGI(TAG, "PWM master volume on GPIO %d channel %d duty %u..%u",
			 pwm_mastervol.gpio, pwm_mastervol.channel,
			 pwm_mastervol.duty_min, pwm_mastervol.duty_max);
}

void pwm_mastervol_set(unsigned left, unsigned right) {
	if (!pwm_mastervol.enabled) return;
	apply_duty(volume_to_duty(left, right));
}
