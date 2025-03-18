/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "led.h"
#include "Config.h"
#include "accessors.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rmt.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "globdefs.h"
#include "gpio_exp.h"
#include "services.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_LED 8
#define BLOCKTIME 10 // up to portMAX_DELAY

#ifdef CONFIG_IDF_TARGET_ESP32S3
#define LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE
#else
#define LEDC_SPEED_MODE LEDC_HIGH_SPEED_MODE
#endif

static const char* TAG = "led";

#define RMT_CLK (40 / 2)

static int8_t led_rmt_channel = -1;
static uint32_t scale24(uint32_t bright, uint8_t);

static const struct rmt_led_param_s {
    sys_led_types type;
    uint8_t bits;
    // number of ticks in nanoseconds converted in RMT_CLK ticks
    rmt_item32_t bit_0;
    rmt_item32_t bit_1;
    uint32_t green, red;
    uint32_t (*scale)(uint32_t, uint8_t);
} rmt_led_param[] = {
    {sys_led_types_WS2812, 24, {{{350 / RMT_CLK, 1, 1000 / RMT_CLK, 0}}},
        {{{1000 / RMT_CLK, 1, 350 / RMT_CLK, 0}}}, 0xff0000, 0x00ff00, scale24},
    {.type = -1}};

static EXT_RAM_ATTR struct led_s {
    gpio_num_t gpio;
    bool on;
    uint32_t color;
    int ontime, offtime;
    int bright;
    int channel;
    const struct rmt_led_param_s* rmt;
    int pushedon, pushedoff;
    bool pushed;
    TimerHandle_t timer;
} leds[MAX_LED];

// can't use EXT_RAM_ATTR for initialized structure
static struct led_config_s {
    int gpio;
    int color;
    int bright;
    sys_led_types type;
} green = {.gpio = -1, .color = 0, .bright = -1, .type = sys_led_types_GPIO},
  red = {.gpio = -1, .color = 0, .bright = -1, .type = sys_led_types_GPIO};

static int led_max = 2;

/****************************************************************************************
 *
 */
static uint32_t scale24(uint32_t color, uint8_t scale) {
    uint32_t scaled = (((color & 0xff0000) >> 16) * scale / 100) << 16;
    scaled |= (((color & 0xff00) >> 8) * scale / 100) << 8;
    scaled |= (color & 0xff) * scale / 100;
    return scaled;
}

/****************************************************************************************
 *
 */
static void set_level(struct led_s* led, bool on) {
    if (led->rmt) {
        uint32_t data = on ? led->rmt->scale(led->color, led->bright) : 0;
        uint32_t mask = 1 << (led->rmt->bits - 1);
        rmt_item32_t buffer[led->rmt->bits];
        for (uint32_t bit = 0; bit < led->rmt->bits; bit++) {
            uint32_t set = data & mask;
            buffer[bit] = set ? led->rmt->bit_1 : led->rmt->bit_0;
            mask >>= 1;
        }
        rmt_write_items(led->channel, buffer, led->rmt->bits, false);
    } else if (led->bright < 0 || led->gpio >= GPIO_NUM_MAX) {
        gpio_set_level_x(led->gpio, on ? led->color : !led->color);
    } else {
        ledc_set_duty(
            LEDC_SPEED_MODE, led->channel, on ? led->bright : (led->color ? 0 : pwm_system.max));
        ledc_update_duty(LEDC_SPEED_MODE, led->channel);
    }
}

/****************************************************************************************
 *
 */
static void vCallbackFunction(TimerHandle_t xTimer) {
    struct led_s* led = (struct led_s*)pvTimerGetTimerID(xTimer);

    if (!led->timer) return;

    led->on = !led->on;
    ESP_EARLY_LOGV(TAG, "led vCallbackFunction setting gpio %d level %d (bright:%d)", led->gpio,
        led->on, led->bright);
    set_level(led, led->on);

    // was just on for a while
    if (!led->on && led->offtime == -1) return;

    // regular blinking
    xTimerChangePeriod(
        xTimer, (led->on ? led->ontime : led->offtime) / portTICK_RATE_MS, BLOCKTIME);
}

/****************************************************************************************
 *
 */
bool led_blink_core(int idx, int ontime, int offtime, bool pushed) {
    if (!leds[idx].gpio || leds[idx].gpio < 0) return false;

    ESP_LOGD(TAG, "led_blink_core %d on:%d off:%d, pushed:%u", idx, ontime, offtime, pushed);
    if (leds[idx].timer) {
        // normal requests waits if a pop is pending
        if (!pushed && leds[idx].pushed) {
            leds[idx].pushedon = ontime;
            leds[idx].pushedoff = offtime;
            return true;
        }
        xTimerStop(leds[idx].timer, BLOCKTIME);
    }

    // save current state if not already pushed
    if (!leds[idx].pushed) {
        leds[idx].pushedon = leds[idx].ontime;
        leds[idx].pushedoff = leds[idx].offtime;
        leds[idx].pushed = pushed;
    }

    // then set new one
    leds[idx].ontime = ontime;
    leds[idx].offtime = offtime;

    if (ontime == 0) {
        ESP_LOGD(TAG, "led %d, setting reverse level", idx);
        set_level(leds + idx, false);
    } else if (offtime == 0) {
        ESP_LOGD(TAG, "led %d, setting level", idx);
        set_level(leds + idx, true);
    } else {
        if (!leds[idx].timer) {
            ESP_LOGD(TAG, "led %d, Creating timer", idx);
            leds[idx].timer = xTimerCreate("ledTimer", ontime / portTICK_RATE_MS, pdFALSE,
                (void*)&leds[idx], vCallbackFunction);
        }
        leds[idx].on = true;
        set_level(leds + idx, true);

        ESP_LOGD(TAG, "led %d, Setting gpio %d and starting timer", idx, leds[idx].gpio);
        if (xTimerStart(leds[idx].timer, BLOCKTIME) == pdFAIL) return false;
    }

    return true;
}

/****************************************************************************************
 *
 */
bool led_brightness(int idx, int bright) {
    if (bright > 100) bright = 100;

    if (leds[idx].rmt) {
        leds[idx].bright = bright;
    } else {
        leds[idx].bright = pwm_system.max * powf(bright / 100.0, 3);
        if (!leds[idx].color) leds[idx].bright = pwm_system.max - leds[idx].bright;

        ledc_set_duty(LEDC_SPEED_MODE, leds[idx].channel, leds[idx].bright);
        ledc_update_duty(LEDC_SPEED_MODE, leds[idx].channel);
    }

    return true;
}

/****************************************************************************************
 *
 */
bool led_unpush(int idx) {
    if (!leds[idx].gpio || leds[idx].gpio < 0) return false;

    led_blink_core(idx, leds[idx].pushedon, leds[idx].pushedoff, true);
    leds[idx].pushed = false;

    return true;
}

/****************************************************************************************
 *
 */
int led_allocate(void) {
    if (led_max < MAX_LED) return led_max++;
    return -1;
}

/****************************************************************************************
 *
 */
bool led_config(int idx, sys_led_config* led_config) {
    if (!led_config->has_gpio) {
        ESP_LOGD(TAG,"No GPIO configured for %s LED",idx == LED_GREEN ? "GREEN" : "RED");
        return false;
    }
    if (led_config->gpio.pin < 0) {
        ESP_LOGD(TAG,"GPIO -1 ignored for %s LED",idx == LED_GREEN ? "GREEN" : "RED");
        return false;
    }
    if (idx >= MAX_LED) return false;
    if (led_config->brightness > 100) led_config->brightness = 100;

    leds[idx].gpio = led_config->gpio.pin;
    leds[idx].color = led_config->gpio.level;
    leds[idx].rmt = NULL;
    leds[idx].bright = -1;

    if (led_config->led_type != sys_led_types_GPIO) {
        // first make sure we have a known addressable led
        for (const struct rmt_led_param_s* p = rmt_led_param; !leds[idx].rmt && p->type >= 0; p++)
            if (p->type == led_config->led_type) leds[idx].rmt = p;
        if (!leds[idx].rmt) return false;

        if (led_rmt_channel < 0) led_rmt_channel = RMT_NEXT_TX_CHANNEL();
        leds[idx].channel = led_rmt_channel;
        leds[idx].bright = led_config->brightness > 0 ? led_config->brightness : 100;

        // set counter clock to 40MHz
        rmt_config_t config = RMT_DEFAULT_CONFIG_TX(led_config->gpio.pin, leds[idx].channel);
        config.clk_div = 2;

        rmt_config(&config);
        rmt_driver_install(config.channel, 0, 0);
    } else if (led_config->brightness < 0 || led_config->gpio.pin >= GPIO_NUM_MAX) {
        gpio_pad_select_gpio_x(led_config->gpio.pin);
        gpio_set_direction_x(led_config->gpio.pin, GPIO_MODE_OUTPUT);
    } else {
        leds[idx].channel = pwm_system.base_channel++;
        leds[idx].bright = pwm_system.max * powf(led_config->brightness / 100.0, 3);
        if (!led_config->gpio.level) leds[idx].bright = pwm_system.max - leds[idx].bright;

        ledc_channel_config_t ledc_channel = {
            .channel = leds[idx].channel,
            .duty = leds[idx].bright,
            .gpio_num = led_config->gpio.pin,
            .speed_mode = LEDC_SPEED_MODE,
            .hpoint = 0,
            .timer_sel = pwm_system.timer,
        };

        ledc_channel_config(&ledc_channel);
    }

    set_level(leds + idx, false);
    ESP_LOGI(TAG, "Configuring LED %s %d (on:%d rmt:%s %d%% )", idx == LED_GREEN ? "GREEN" : "RED",
        led_config->gpio.pin, led_config->gpio.level, sys_led_types_name(led_config->led_type),
        led_config->brightness);
    return true;
}

/****************************************************************************************
 *
 */
static void led_suspend(void) {
    ESP_LOGD(TAG,"led_suspend: turning off leds");
    led_off(LED_GREEN);
    led_off(LED_RED);
}

/****************************************************************************************
 *
 */
void set_led_gpio(int gpio, char* value) {
    struct led_config_s* config;

    if (strcasestr(value, "green"))
        config = &green;
    else if (strcasestr(value, "red"))
        config = &red;
    else
        return;

    config->gpio = gpio;
    char* p = value;
    while ((p = strchr(p, ':')) != NULL) {
        p++;
        if ((strcasestr(p, "ws2812")) != NULL)
            config->type = sys_led_types_WS2812;
        else
            config->color = atoi(p);
    }

    if (config->type != sys_led_types_GPIO) {
        for (const struct rmt_led_param_s* p = rmt_led_param; p->type >= 0; p++) {
            if (p->type == config->type) {
                if (config == &green)
                    config->color = p->green;
                else
                    config->color = p->red;
                break;
            }
        }
    }
}

void led_svc_init(void) {
    sys_gpios_config* gpios = NULL;
    bool found = false;
    
    if (!platform->has_gpios) {
        ESP_LOGI(TAG, "No LED configured");
        return;
    }
    ESP_LOGI(TAG,"Setting up leds");
    gpios = &platform->gpios;
    if (gpios->has_greenLED) {
        found = found | led_config(LED_GREEN, &gpios->greenLED);
    }
    if (gpios->has_redLED) {
        found = found | led_config(LED_RED, &gpios->redLED);
    }
    ESP_LOGD(TAG,"Done setting up leds");
    // make sure we switch off all leds (useful for gpio expanders)
    if (found) {
        ESP_LOGD(TAG,"Switching leds off");
        services_sleep_setsuspend(led_suspend);
    }
}
