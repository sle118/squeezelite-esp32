/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <stdio.h>
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "Config.h"
#include "accessors.h"
#include "battery.h"
#include "buttons.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/rmt.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "esp_sleep.h"
#include "globdefs.h"
#include "gpio_exp.h"
#include "led.h"
#include "messaging.h"
#include "monitor.h"
#include "services.h"
#include "tools.h"

extern void battery_svc_init(void);
extern void monitor_svc_init(void);
extern void led_svc_init(void);

int spi_system_host = SPI_SYSTEM_HOST;
int spi_system_dc_gpio = -1;
int rmt_system_base_tx_channel = RMT_CHANNEL_0;
int rmt_system_base_rx_channel = RMT_CHANNEL_MAX - 1;

pwm_system_t pwm_system = {
    .timer = LEDC_TIMER_0,
    .base_channel = LEDC_CHANNEL_0,
    .max = (1 << LEDC_TIMER_13_BIT),
};
static sys_sleep_config* sleep_config;
static EXT_RAM_ATTR uint8_t gpio_exp_count = 0;
static EXT_RAM_ATTR bool spi_configured = false;
static EXT_RAM_ATTR bool i2c_configured = false;
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

static const char* TAG = "services";

static void deep_sleep_timer_cb(TimerHandle_t timer) {
    (void)timer;
    esp_deep_sleep_start();
}

bool are_GPIOExp_equal(const sys_exp_config* exp1, const sys_exp_config* exp2) {
    if (exp1 == NULL || exp2 == NULL) {
        return false; // Safeguard against NULL pointers
    }

    // Check if model, address, and base are the same
    if (exp1->model != exp2->model || exp1->addr != exp2->addr || exp1->base != exp2->base) {
        return false;
    }

    // Check if intr structure (pin and level) are the same
    if (exp1->intr != exp2->intr) {
        return false;
    }

    return true;
}

bool sys_dev_config_callback(pb_istream_t* istream, pb_ostream_t* ostream, const pb_field_iter_t* field) {
    ESP_LOGV(TAG, "Decoding/Encoding Devices, tag: %d", field->tag);
    sys_exp_config** pExp = (sys_exp_config**)field->pData;
    sys_exp_config* exp = NULL;

    if (istream != NULL && field->tag == sys_dev_config_gpio_exp_tag) {
        ESP_LOGD(TAG, "Decoding GPIO Expander #%d", gpio_exp_count + 1);
        sys_exp_config entry = sys_exp_config_init_default;
        if (!pb_decode(istream, &sys_exp_config_msg, &entry)) {
            return false;
        }
        if (entry.model == sys_exp_models_UNSPECIFIED_EXP) {
            ESP_LOGD(TAG, "Skipping GPIO Expander model %s", sys_exp_models_name(entry.model));
            return true;
        }
        // Don't add the expander if it was already decoded. This could
        // happen if both the configuration and the platform configuration
        // contain the definition.
        for (int i = 0; i < gpio_exp_count; i++) {
            if (are_GPIOExp_equal(&(*pExp)[i], &entry)) {
                ESP_LOGW(TAG, "GPIO Expander entry already exists, skipping addition.");
                return true; // Skip adding as it already exists
            }
        }

        gpio_exp_count++;

        *pExp = heap_caps_realloc(*pExp, sizeof(sys_exp_config) * gpio_exp_count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

        // Assert after realloc to ensure memory allocation was successful
        assert(*pExp != NULL);
        exp = (*pExp) + gpio_exp_count - 1; // Simplified pointer arithmetic
        memcpy(exp, &entry, sizeof(entry));
        ESP_LOGD(TAG, "GPIO Expander #%d model %s", gpio_exp_count, sys_exp_models_name(entry.model));

    } else if (ostream != NULL && field->tag == sys_dev_config_gpio_exp_tag) {
        ESP_LOGV(TAG, "Encoding %d GPIO Expanders", gpio_exp_count);

        for (int i = 0; i < gpio_exp_count; i++) {
            if (!pb_encode_tag_for_field(ostream, field)) {
                return false;
            }
            if (!pb_encode_submessage(ostream, &sys_exp_config_msg, &(*pExp)[i])) {
                return false;
            }
        }
        ESP_LOGV(TAG, "GPIO Expander encoding completed");
    }

    return true;
}

void set_gpio_level(sys_gpio_config* gpio, const char* name, gpio_mode_t mode) {
    ESP_LOGI(TAG, "set GPIO %u to %s, level %d", gpio->pin, name, gpio->level);
    if (gpio->pin < 0) {
        ESP_LOGW(TAG, "Invalid gpio %d for %s", gpio->pin, name);
        return;
    }
    esp_rom_gpio_pad_select_gpio(gpio->pin);
    gpio_set_direction(gpio->pin, mode);
    gpio_set_level(gpio->pin, gpio->level);
}

/****************************************************************************************
 *
 */
static void sleep_gpio_handler(void* id, button_event_e event, button_press_e mode, bool long_press) {
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
    if (!*sleep_context.sleeper || now < last + 30 * 1000) return;
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
    ESP_LOGD(TAG, "Initializing sleep services");
    if (!sys_services_config_SLEEP(sleep_config)) {
        ESP_LOGD(TAG, "No sleep service configured");
        return;
    }
    // get the wake criteria
    for (int i = 0; i < sleep_config->wake_count; i++) {
        if (!rtc_gpio_is_valid_gpio(sleep_config->wake[i].pin)) {
            ESP_LOGE(TAG, "invalid wake GPIO %d (not in RTC domain)", sleep_config->wake[i].pin);
        } else {
            sleep_context.wake_gpio |= 1LL << sleep_config->wake[i].pin;
            sleep_context.wake_gpio |= 1LL << sleep_config->wake[i].pin;
            sleep_context.wake_level |= sleep_config->wake[i].level << sleep_config->wake[i].pin;
        }
    }
    // when moving to esp-idf more recent than 4.4.x, multiple gpio wake-up with level specific can
    // be done
    if (sleep_context.wake_gpio) {
        ESP_LOGI(TAG, "Sleep wake-up gpio bitmap 0x%llx (active 0x%llx)", sleep_context.wake_gpio, sleep_context.wake_level);
    }

    // do we want battery safety
    sleep_context.battery_level = sleep_config->batt;
    if (sleep_context.battery_level != 0.0) {
        sleep_context.battery_chain = battery_handler_svc;
        battery_handler_svc = sleep_battery;
        ESP_LOGI(TAG, "Sleep on battery level of %.2f", sleep_context.battery_level);
    }
    for (int i = 0; i < sleep_config->rtc_count; i++) {
        if (!rtc_gpio_is_valid_gpio(sleep_config->rtc[i].pin)) {
            ESP_LOGE(TAG, "invalid rtc GPIO %d", sleep_config->rtc[i].pin);
        } else {
            sleep_context.rtc_gpio |= 1LL << sleep_config->rtc[i].pin;
            sleep_context.rtc_level |= sleep_config->rtc[i].level << sleep_config->rtc[i].pin;
        }
    }
    // when moving to esp-idf more recent than 4.4.x, multiple gpio wake-up with level specific can
    // be done
    if (sleep_context.rtc_gpio) {
        ESP_LOGI(TAG, "RTC forced gpio bitmap 0x%llx (active 0x%llx)", sleep_context.rtc_gpio, sleep_context.rtc_level);
    }

    // get the GPIOs that activate sleep (we could check that we have a valid wake)
    if (sleep_config->has_sleep && sleep_config->sleep.pin >= 0) {
        ESP_LOGI(TAG, "Sleep activation gpio %d (active %d)", sleep_config->sleep.pin, sleep_config->sleep.level);
        button_create(NULL, sleep_config->sleep.pin, sleep_config->sleep.level ? BUTTON_HIGH : BUTTON_LOW, true, 0, sleep_gpio_handler, 0, -1);
    }

    // do we want delay sleep

    sleep_context.delay = sleep_config->delay * 60 * 1000;

    // now check why we woke-up
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "waking-up from deep sleep with cause %d", cause);

        // find the type of wake-up
        uint64_t wake_gpio;
        if (cause == ESP_SLEEP_WAKEUP_EXT0)
            wake_gpio = sleep_context.wake_gpio;
        else
            wake_gpio = esp_sleep_get_ext1_wakeup_status();

        // we might be woken up by infrared in which case we want a short sleep
        if (infrared_gpio() >= 0 && ((1LL << infrared_gpio()) & wake_gpio)) {
            sleep_context.spurious = 1;
            if (sleep_config->spurious > 0) {
                sleep_context.spurious = sleep_config->spurious;
            }
            sleep_context.spurious *= 60 * 1000;

            ESP_LOGI(TAG, "spurious wake-up detection during %d sec", sleep_context.spurious / 1000);
        }
    }

    // if we have inactivity timer (user-set or because of IR wake) then active counters
    if (sleep_context.delay || sleep_context.spurious) {
        sleep_context.idle_chain = pseudo_idle_svc;
        pseudo_idle_svc = sleep_timer;
        if (sleep_context.delay) ESP_LOGI(TAG, "inactivity timer of %d minute(s)", sleep_context.delay / (60 * 1000));
    }
}

/****************************************************************************************
 *
 */
void services_sleep_activate(sleep_cause_e cause) {
    // call all sleep hooks that might want to do something
    for (void (**suspend)(void) = sleep_context.suspend; *suspend; suspend++)
        (*suspend)();

    // isolate all possible GPIOs, except the wake-up and RTC-maintaines ones
    esp_sleep_config_gpio_isolate();

    // keep RTC domain up if we need to maintain pull-up/down of some GPIO from RTC
    if (sleep_context.rtc_gpio) esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    for (int i = 0; i < GPIO_NUM_MAX; i++) {
        // must be a RTC GPIO
        if (!rtc_gpio_is_valid_gpio(i)) continue;

        // do we need to maintain a pull-up or down of that GPIO
        if ((1LL << i) & sleep_context.rtc_gpio) {
            if ((sleep_context.rtc_level >> i) & 0x01)
                rtc_gpio_pullup_en(i);
            else
                rtc_gpio_pulldown_en(i);
            // or is this not wake-up GPIO, just isolate it
        } else if (!((1LL << i) & sleep_context.wake_gpio)) {
            rtc_gpio_isolate(i);
        }
    }

    // is there just one GPIO
    if (sleep_context.wake_gpio & (sleep_context.wake_gpio - 1)) {
        ESP_LOGI(TAG, "going to sleep cause %d, wake-up on multiple GPIO, any '1' wakes up 0x%llx", cause, sleep_context.wake_gpio);
#if defined(CONFIG_IDF_TARGET_ESP32S3) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        if (!sleep_context.wake_level)
            esp_sleep_enable_ext1_wakeup(sleep_context.wake_gpio, ESP_EXT1_WAKEUP_ANY_LOW);
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

    // we need to use a timer in case the same button is used for sleep and wake-up and it's
    // "pressed" vs "released" selected
    if (cause == SLEEP_ONKEY)
        xTimerStart(xTimerCreate("sleepTimer", pdMS_TO_TICKS(1000), pdFALSE, NULL, deep_sleep_timer_cb), 0);
    else
        esp_deep_sleep_start();
}

/****************************************************************************************
 *
 */
static void register_method(void** store, size_t size, void* method) {
    for (int i = 0; i < size; i++, *store++)
        if (!*store) {
            *store = method;
            return;
        }
}

/****************************************************************************************
 *
 */
void services_sleep_setsuspend(void (*hook)(void)) {
    register_method((void**)sleep_context.suspend, sizeof(sleep_context.suspend) / sizeof(*sleep_context.suspend), (void*)hook);
}

/****************************************************************************************
 *
 */
void services_sleep_setsleeper(uint32_t (*sleeper)(void)) {
    register_method((void**)sleep_context.sleeper, sizeof(sleep_context.sleeper) / sizeof(*sleep_context.sleeper), (void*)sleeper);
}
void services_ports_init(void) {
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "Initializing ports");
    gpio_install_isr_service(0);
    ESP_LOGD(TAG, "Checking i2c port usage");
    if (platform->dev.dac.has_i2c && platform->dev.has_i2c && platform->dev.dac.i2c.port !=  sys_i2c_port_UNSPECIFIED &&
        platform->dev.dac.i2c.port == platform->dev.i2c.port) {
        ESP_LOGE(TAG, "Port %s is used for internal DAC use. Switching to ", sys_i2c_port_name(platform->dev.dac.i2c.port));
        platform->dev.i2c.port = platform->dev.i2c.port == sys_i2c_port_PORT0 ? sys_i2c_port_PORT1 : sys_i2c_port_PORT0;
        config_raise_changed(false);
    }

    // shared I2C bus
    ESP_LOGD(TAG, "Configuring I2C");
    const i2c_config_t* i2c_config = config_i2c_get(&platform->dev.i2c);
    ESP_LOGD(TAG, "Stored I2C configuration [sda:%d scl:%d port:%s speed:%u]", i2c_config->sda_io_num, i2c_config->scl_io_num,
        sys_i2c_port_name(platform->dev.i2c.port), i2c_config->master.clk_speed);
    if (i2c_config->sda_io_num != -1 && i2c_config->scl_io_num != -1) {
        ESP_LOGI(TAG, "Configuring I2C sda:%d scl:%d port:%s speed:%u", i2c_config->sda_io_num, i2c_config->scl_io_num,
            sys_i2c_port_name(platform->dev.i2c.port), i2c_config->master.clk_speed);
        i2c_param_config(platform->dev.i2c.port - sys_i2c_port_PORT0, i2c_config);
        if ((err = i2c_driver_install(platform->dev.i2c.port - sys_i2c_port_PORT0, i2c_config->mode, 0, 0, 0)) != ESP_OK) {
            ESP_LOGE(TAG, "Error setting up i2c: %s", esp_err_to_name(err));
        } else {
            i2c_configured = true;
        }
    } else {
        if (platform->dev.has_display && platform->dev.display.has_common && platform->dev.display.which_dispType == sys_display_config_i2c_tag) {
            ESP_LOGE(TAG, "I2C configuration missing for display %s", sys_display_drivers_name(platform->dev.display.common.driver));
        } else {
            ESP_LOGI(TAG, "Shared I2C not configured");
        }
    }

    const spi_bus_config_t* spi_config = config_spi_get((spi_host_device_t*)&spi_system_host);
    ESP_LOGD(TAG, "Stored SPI configuration[mosi:%d miso:%d clk:%d host:%u dc:%d]", spi_config->mosi_io_num, spi_config->miso_io_num,
        spi_config->sclk_io_num, spi_system_host, spi_system_dc_gpio);
    if (spi_config->mosi_io_num != -1 && spi_config->sclk_io_num != -1) {
        ESP_LOGI(TAG, "Configuring SPI mosi:%d miso:%d clk:%d host:%u dc:%d", spi_config->mosi_io_num, spi_config->miso_io_num,
            spi_config->sclk_io_num, spi_system_host, spi_system_dc_gpio);
        if ((err = spi_bus_initialize(spi_system_host, spi_config, SPI_DMA_CH_AUTO)) != ESP_OK) {
            ESP_LOGE(TAG, "Error setting up SPI bus: %s", esp_err_to_name(err));
        } else {
            spi_configured = true;
        }
        if (spi_system_dc_gpio != -1) {
            gpio_reset_pin(spi_system_dc_gpio);
            gpio_set_direction(spi_system_dc_gpio, GPIO_MODE_OUTPUT);
            gpio_set_level(spi_system_dc_gpio, 0);
        } else {
            ESP_LOGW(TAG, "No DC GPIO set, SPI display will not work");
        }
    } else {
        spi_system_host = -1;
        if (platform->dev.has_display && platform->dev.display.has_common &&
            platform->dev.display.common.driver != sys_display_drivers_UNSPECIFIED &&
            platform->dev.display.which_dispType == sys_display_config_spi_tag) {
            ESP_LOGE(TAG, "SPI bus configuration missing for display %s", sys_display_drivers_name(platform->dev.display.common.driver));
        } else {
            ESP_LOGI(TAG, "SPI bus not configured");
        }
    }
}
void services_gpio_init(void) {
    ESP_LOGI(TAG, "Initializing GPIOs");
    // set potential power GPIO on chip first in case expanders are power using these
    sys_gpio_config* gpio = NULL;
    if (SYS_GPIOS_NAME(power, gpio) && gpio->pin >= 0) {
        ESP_LOGD(TAG, "Handling power gpio");
        gpio->level = sys_gpio_lvl_HIGH;
        set_gpio_level(gpio, "power", GPIO_MODE_OUTPUT);
    } else {
        ESP_LOGD(TAG, "No power GPIO defined");
    }

    if (SYS_GPIOS_NAME(GND, gpio) && gpio->pin >= 0) {
        ESP_LOGD(TAG, "Handling GND gpio");
        gpio->level = sys_gpio_lvl_LOW;
        set_gpio_level(gpio, "GND", GPIO_MODE_OUTPUT);
    } else {
        ESP_LOGD(TAG, "No GND gpio defined");
    }

    // create GPIO expanders
    gpio_exp_config_t gpio_exp_config;
    if (platform->has_dev && gpio_exp_count > 0 && platform->dev.gpio_exp[0].model != sys_exp_models_UNSPECIFIED_EXP) {
        ESP_LOGI(TAG, "Initializing %d GPIO Expander(s)", gpio_exp_count);
        for (int count = 0; count < gpio_exp_count; count++) {
            sys_exp_config* exp = &platform->dev.gpio_exp[count];
            if (exp->model == sys_exp_models_UNSPECIFIED_EXP) {
                ESP_LOGD(TAG, "Skipping unknown model");
                continue;
            }
            gpio_exp_config.phy.ena_pin = -1;
            gpio_exp_config.base = exp->base;
            gpio_exp_config.count = exp->count;
            gpio_exp_config.phy.addr = exp->addr;
            gpio_exp_config.intr = exp->intr;
            if (exp->has_ena && exp->ena.pin >= 0) {
                gpio_exp_config.phy.ena_pin = exp->ena.pin;
                gpio_exp_config.phy.ena_lvl = exp->ena.level;
            }
            if (exp->which_ExpType == sys_exp_config_spi_tag) {
                if (!spi_configured) {
                    ESP_LOGE(TAG, "SPI bus not configured for GPIO Expander index %d (%s)", count, sys_exp_models_name(exp->model));
                    continue;
                }
                gpio_exp_config.phy.cs_pin = exp->ExpType.spi.cs;
                gpio_exp_config.phy.host =
                    (!platform->dev.has_spi || (platform->dev.has_spi  &&  platform->dev.spi.host == sys_dev_common_hosts_NONE) ? sys_dev_common_hosts_Host0 : platform->dev.spi.host) - sys_dev_common_hosts_Host0;
                gpio_exp_config.phy.speed = exp->ExpType.spi.speed > 0 ? exp->ExpType.spi.speed : 0;
            } else {
                if (!i2c_configured) {
                    ESP_LOGE(TAG, "I2C bus not configured for GPIO Expander index %d (%s)", count, sys_exp_models_name(exp->model));
                    continue;
                }
                gpio_exp_config.phy.port =
                   ((!platform->dev.has_i2c || (platform->dev.has_i2c && platform->dev.i2c.port == sys_i2c_port_UNSPECIFIED) )? sys_dev_common_ports_SYSTEM : platform->dev.i2c.port) - sys_dev_common_ports_SYSTEM ;
            }
            strncpy(gpio_exp_config.model, sys_exp_models_name(exp->model), sizeof(gpio_exp_config.model) - 1);
            gpio_exp_create(&gpio_exp_config);
        }
    } else if (gpio_exp_count > 0) {
        ESP_LOGW(TAG, "GPIO Expander count %d but none is valid", gpio_exp_count);
    }
}
/****************************************************************************************
 *
 */
void services_init(void) {
    ESP_LOGI(TAG, "Initializing services");
    esp_err_t err = ESP_OK;

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
