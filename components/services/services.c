/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "driver/rmt.h"
#include "platform_config.h"
#include "gpio_exp.h"
#include "battery.h"
#include "led.h"
#include "monitor.h"
#include "globdefs.h"
#include "accessors.h"
#include "messaging.h"
#include "buttons.h"
#include "services.h"

extern void battery_svc_init(void);
extern void monitor_svc_init(void);
extern void led_svc_init(void);

int i2c_system_port = I2C_SYSTEM_PORT;
int i2c_system_speed = 400000;
int spi_system_host = SPI_SYSTEM_HOST;
int spi_system_dc_gpio = -1;
int rmt_system_base_tx_channel = RMT_CHANNEL_0;
int rmt_system_base_rx_channel = RMT_CHANNEL_MAX-1;

pwm_system_t pwm_system = {
		.timer = LEDC_TIMER_0,
		.base_channel = LEDC_CHANNEL_0,
		.max = (1 << LEDC_TIMER_13_BIT),
};

static EXT_RAM_ATTR struct {
    uint64_t wake_gpio, wake_level;
    uint64_t rtc_gpio, rtc_level;
    uint32_t delay, spurious;
    float battery_level;
    int battery_count;
    void (*idle_chain)(uint32_t now);
    void (*battery_chain)(float level, int cells);
    void (*suspend[10])(void);
    uint32_t (*sleeper[10])(void);
} sleep_context;

static const char *TAG = "services";

/****************************************************************************************
 *
 */
void set_chip_power_gpio(int gpio, char *value) {
	bool parsed = true;

	// we only parse on-chip GPIOs
	if (gpio >= GPIO_NUM_MAX) return;

	if (!strcasecmp(value, "vcc") ) {
		gpio_pad_select_gpio(gpio);
		gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
		gpio_set_level(gpio, 1);
	} else if (!strcasecmp(value, "gnd")) {
		gpio_pad_select_gpio(gpio);
		gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
		gpio_set_level(gpio, 0);
	} else parsed = false;

	if (parsed) ESP_LOGI(TAG, "set GPIO %u to %s", gpio, value);
}

/****************************************************************************************
 *
 */
void set_exp_power_gpio(int gpio, char *value) {
	bool parsed = true;

	// we only parse on-chip GPIOs
	if (gpio < GPIO_NUM_MAX) return;

	if (!strcasecmp(value, "vcc") ) {
		gpio_exp_set_direction(gpio, GPIO_MODE_OUTPUT, NULL);
		gpio_exp_set_level(gpio, 1, true, NULL);
	} else if (!strcasecmp(value, "gnd")) {
		gpio_exp_set_direction(gpio, GPIO_MODE_OUTPUT, NULL);
		gpio_exp_set_level(gpio, 0, true, NULL);
	} else parsed = false;

	if (parsed) ESP_LOGI(TAG, "set expanded GPIO %u to %s", gpio, value);
}

/****************************************************************************************
 *
 */
static void sleep_gpio_handler(void *id, button_event_e event, button_press_e mode, bool long_press) {
    if (event == BUTTON_PRESSED) services_sleep_activate(SLEEP_ONGPIO);
}

/****************************************************************************************
 *
 */
static void sleep_timer(uint32_t now) {
    static EXT_RAM_ATTR uint32_t last, first;

    // first chain the calls to pseudo_idle function
    if (sleep_context.idle_chain) sleep_context.idle_chain(now);

    // we need boot time for spurious timeout calculation
    if (!first) first = now;

    // only query callbacks every 30s if we have at least one sleeper
    if (!*sleep_context.sleeper || now < last + 30*1000) return;
    last = now;

    // time to evaluate if we had spurious wake-up
    if (sleep_context.spurious && now > sleep_context.spurious + first) {
        bool spurious = true;

        // see if at least one sleeper has been awake since we started
        for (uint32_t (**sleeper)(void) = sleep_context.sleeper; *sleeper && spurious; sleeper++) {
            spurious &= (*sleeper)() >= now - first;
        }

        // no activity since we woke-up, this was a spurious one
        if (spurious) {
            ESP_LOGI(TAG, "spurious wake of %d sec, going back to sleep", (now - first) / 1000);
            services_sleep_activate(SLEEP_ONTIMER);
        }

        // resume normal work but we might have no "regular" inactivity delay
        sleep_context.spurious = 0;
        if (!sleep_context.delay) *sleep_context.sleeper = NULL;
        ESP_LOGI(TAG, "wake-up was not spurious after %d sec", (now - first) / 1000);
    }

    // we might be here because we are waiting for spurious
    if (sleep_context.delay) {
        // call all sleepers to know how long for how long they have been inactive
        for (uint32_t (**sleeper)(void) = sleep_context.sleeper; sleep_context.delay && *sleeper; sleeper++) {
            if ((*sleeper)() < sleep_context.delay) return;
        }

        // if we are here, we are ready to sleep;
        services_sleep_activate(SLEEP_ONTIMER);
    }
}

/****************************************************************************************
 *
 */
static void sleep_battery(float level, int cells) {
    // chain if any
    if (sleep_context.battery_chain) sleep_context.battery_chain(level, cells);

    // then assess if we have to stop because of low batt
    if (level < sleep_context.battery_level) {
        if (sleep_context.battery_count++ == 2) services_sleep_activate(SLEEP_ONBATTERY);
    } else {
        sleep_context.battery_count = 0;
    }
}

/****************************************************************************************
 *
 */
void services_sleep_init(void) {
    char *config = config_alloc_get(NVS_TYPE_STR, "sleep_config");
    char *p;

    // get the wake criteria
    if ((p = strcasestr(config, "wake"))) {
        char list[32] = "", item[8];
		sscanf(p, "%*[^=]=%31[^,]", list);
        p = list - 1;
        while (p++ && sscanf(p, "%7[^|]", item)) {
            int level = 0, gpio = atoi(item);
            if (!rtc_gpio_is_valid_gpio(gpio)) {
                ESP_LOGE(TAG, "invalid wake GPIO %d (not in RTC domain)", gpio);
            } else {
                sleep_context.wake_gpio |= 1LL << gpio;
            }
            if (sscanf(item, "%*[^:]:%d", &level)) sleep_context.wake_level |= level << gpio;
            p = strchr(p, '|');
        }

        // when moving to esp-idf more recent than 4.4.x, multiple gpio wake-up with level specific can be done
        if (sleep_context.wake_gpio) {
            ESP_LOGI(TAG, "Sleep wake-up gpio bitmap 0x%llx (active 0x%llx)", sleep_context.wake_gpio, sleep_context.wake_level);
        }
    }

    // do we want battery safety
    PARSE_PARAM_FLOAT(config, "batt", '=', sleep_context.battery_level);
    if (sleep_context.battery_level != 0.0) {
        sleep_context.battery_chain = battery_handler_svc;
        battery_handler_svc = sleep_battery;
        ESP_LOGI(TAG, "Sleep on battery level of %.2f", sleep_context.battery_level);
    }


    // get the rtc-pull criteria
    if ((p = strcasestr(config, "rtc"))) {
        char list[32] = "", item[8];
		sscanf(p, "%*[^=]=%31[^,]", list);
        p = list - 1;
        while (p++ && sscanf(p, "%7[^|]", item)) {
            int level = 0, gpio = atoi(item);
            if (!rtc_gpio_is_valid_gpio(gpio)) {
                ESP_LOGE(TAG, "invalid rtc GPIO %d", gpio);
            } else {
                sleep_context.rtc_gpio |= 1LL << gpio;
            }
            if (sscanf(item, "%*[^:]:%d", &level)) sleep_context.rtc_level |= level << gpio;
            p = strchr(p, '|');
        }

        // when moving to esp-idf more recent than 4.4.x, multiple gpio wake-up with level specific can be done
        if (sleep_context.rtc_gpio) {
            ESP_LOGI(TAG, "RTC forced gpio bitmap 0x%llx (active 0x%llx)", sleep_context.rtc_gpio, sleep_context.rtc_level);
        }
    }

    // get the GPIOs that activate sleep (we could check that we have a valid wake)
    if ((p = strcasestr(config, "sleep"))) {
        int gpio, level = 0;
		char sleep[8] = "";
		sscanf(p, "%*[^=]=%7[^,]", sleep);
		gpio = atoi(sleep);
        if ((p = strchr(sleep, ':')) != NULL) level = atoi(p + 1);
        ESP_LOGI(TAG, "Sleep activation gpio %d (active %d)", gpio, level);
        button_create(NULL, gpio, level ? BUTTON_HIGH : BUTTON_LOW, true, 0, sleep_gpio_handler, 0, -1);
    }

    // do we want delay sleep
    PARSE_PARAM(config, "delay", '=', sleep_context.delay);
    sleep_context.delay *= 60*1000;

    // now check why we woke-up
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "waking-up from deep sleep with cause %d", cause);

        // find the type of wake-up
        uint64_t wake_gpio;
        if (cause == ESP_SLEEP_WAKEUP_EXT0) wake_gpio = sleep_context.wake_gpio;
        else wake_gpio = esp_sleep_get_ext1_wakeup_status();

        // we might be woken up by infrared in which case we want a short sleep
        if (infrared_gpio() >= 0 && ((1LL << infrared_gpio()) & wake_gpio)) {
            sleep_context.spurious = 1;
            PARSE_PARAM(config, "spurious", '=', sleep_context.spurious);
            sleep_context.spurious *= 60*1000;
            ESP_LOGI(TAG, "spurious wake-up detection during %d sec", sleep_context.spurious / 1000);
        }
    }

    // if we have inactivity timer (user-set or because of IR wake) then active counters
    if (sleep_context.delay || sleep_context.spurious) {
        sleep_context.idle_chain = pseudo_idle_svc;
        pseudo_idle_svc = sleep_timer;
        if (sleep_context.delay) ESP_LOGI(TAG, "inactivity timer of %d minute(s)", sleep_context.delay / (60*1000));
    }
}

/****************************************************************************************
 *
 */
void services_sleep_activate(sleep_cause_e cause) {
    // call all sleep hooks that might want to do something
    for (void (**suspend)(void) = sleep_context.suspend; *suspend; suspend++) (*suspend)();

    // isolate all possible GPIOs, except the wake-up and RTC-maintaines ones
    esp_sleep_config_gpio_isolate();

    // keep RTC domain up if we need to maintain pull-up/down of some GPIO from RTC
    if (sleep_context.rtc_gpio) esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    for (int i = 0; i < GPIO_NUM_MAX; i++) {
        // must be a RTC GPIO
        if (!rtc_gpio_is_valid_gpio(i)) continue;

        // do we need to maintain a pull-up or down of that GPIO
        if ((1LL << i) & sleep_context.rtc_gpio) {
            if ((sleep_context.rtc_level >> i) & 0x01) rtc_gpio_pullup_en(i);
            else rtc_gpio_pulldown_en(i);
        // or is this not wake-up GPIO, just isolate it
        } else if (!((1LL << i) & sleep_context.wake_gpio)) {
            rtc_gpio_isolate(i);
        }
    }

    // is there just one GPIO
    if (sleep_context.wake_gpio & (sleep_context.wake_gpio - 1)) {
        ESP_LOGI(TAG, "going to sleep cause %d, wake-up on multiple GPIO, any '1' wakes up 0x%llx", cause, sleep_context.wake_gpio);
#if defined(CONFIG_IDF_TARGET_ESP32S3) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        if (!sleep_context.wake_level) esp_sleep_enable_ext1_wakeup(sleep_context.wake_gpio, ESP_EXT1_WAKEUP_ANY_LOW);
        else
#endif
        esp_sleep_enable_ext1_wakeup(sleep_context.wake_gpio, ESP_EXT1_WAKEUP_ANY_HIGH);
    } else if (sleep_context.wake_gpio) {
        int gpio = __builtin_ctzll(sleep_context.wake_gpio);
        int level = (sleep_context.wake_level >> gpio) & 0x01;
        ESP_LOGI(TAG, "going to sleep cause %d, wake-up on GPIO %d level %d", cause, gpio, level);
        esp_sleep_enable_ext0_wakeup(gpio, level);
    } else {
        ESP_LOGW(TAG, "going to sleep cause %d, no wake-up option", cause);
    }

    // we need to use a timer in case the same button is used for sleep and wake-up and it's "pressed" vs "released" selected
    if (cause == SLEEP_ONKEY) xTimerStart(xTimerCreate("sleepTimer", pdMS_TO_TICKS(1000), pdFALSE, NULL, (void (*)(void*)) esp_deep_sleep_start), 0);
    else esp_deep_sleep_start();
}


/****************************************************************************************
 *
 */
static void register_method(void **store, size_t size, void *method) {
    for (int i = 0; i < size; i++, *store++) if (!*store) {
        *store = method;
        return;
    }
}

/****************************************************************************************
 *
 */
void services_sleep_setsuspend(void (*hook)(void)) {
    register_method((void**) sleep_context.suspend, sizeof(sleep_context.suspend)/sizeof(*sleep_context.suspend), (void*) hook);
}

/****************************************************************************************
 *
 */
void services_sleep_setsleeper(uint32_t (*sleeper)(void)) {
    register_method((void**) sleep_context.sleeper, sizeof(sleep_context.sleeper)/sizeof(*sleep_context.sleeper), (void*) sleeper);
}

/****************************************************************************************
 *
 */
void services_init(void) {
	messaging_service_init();
	gpio_install_isr_service(0);

#ifdef CONFIG_I2C_LOCKED
	if (i2c_system_port == 0) {
		i2c_system_port = 1;
		ESP_LOGE(TAG, "Port 0 is reserved for internal DAC use");
	}
#endif

	// set potential power GPIO on chip first in case expanders are powered using these
	parse_set_GPIO(set_chip_power_gpio);

	// shared I2C bus
	const i2c_config_t * i2c_config = config_i2c_get(&i2c_system_port);
	ESP_LOGI(TAG,"Configuring I2C sda:%d scl:%d port:%u speed:%u", i2c_config->sda_io_num, i2c_config->scl_io_num, i2c_system_port, i2c_config->master.clk_speed);

	if (i2c_config->sda_io_num != -1 && i2c_config->scl_io_num != -1) {
		i2c_param_config(i2c_system_port, i2c_config);
		i2c_driver_install(i2c_system_port, i2c_config->mode, 0, 0, 0 );
	} else {
		i2c_system_port = -1;
		ESP_LOGW(TAG, "no I2C configured");
	}

	const spi_bus_config_t * spi_config = config_spi_get((spi_host_device_t*) &spi_system_host);
	ESP_LOGI(TAG,"Configuring SPI mosi:%d miso:%d clk:%d host:%u dc:%d", spi_config->mosi_io_num, spi_config->miso_io_num, spi_config->sclk_io_num, spi_system_host, spi_system_dc_gpio);

	if (spi_config->mosi_io_num != -1 && spi_config->sclk_io_num != -1) {
		spi_bus_initialize( spi_system_host, spi_config, SPI_DMA_CH_AUTO );
		if (spi_system_dc_gpio != -1) {
			gpio_reset_pin(spi_system_dc_gpio);
			gpio_set_direction( spi_system_dc_gpio, GPIO_MODE_OUTPUT );
			gpio_set_level( spi_system_dc_gpio, 0 );
		} else {
			ESP_LOGW(TAG, "No DC GPIO set, SPI display will not work");
		}
	} else {
		spi_system_host = -1;
		ESP_LOGW(TAG, "no SPI configured");
	}

	// create GPIO expanders
	const gpio_exp_config_t* gpio_exp_config;
	for (int count = 0; (gpio_exp_config = config_gpio_exp_get(count)); count++) gpio_exp_create(gpio_exp_config);

	// now set potential power GPIO on expander
	parse_set_GPIO(set_exp_power_gpio);

	// generate gpio_volume nvs key
	char *dummy = config_alloc_get_default(NVS_TYPE_STR, "gpio_volume", "", 0);
	if (dummy) free(dummy);

	// system-wide PWM timer configuration
	ledc_timer_config_t pwm_timer = {
		.duty_resolution = LEDC_TIMER_13_BIT,
		.freq_hz = 5000,
#ifdef CONFIG_IDF_TARGET_ESP32S3
        .speed_mode = LEDC_LOW_SPEED_MODE,
#else
		.speed_mode = LEDC_HIGH_SPEED_MODE,
#endif
		.timer_num = pwm_system.timer,
	};

	ledc_timer_config(&pwm_timer);

	led_svc_init();
	battery_svc_init();
	monitor_svc_init();
}
