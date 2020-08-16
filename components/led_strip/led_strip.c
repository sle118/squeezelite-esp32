/*  ----------------------------------------------------------------------------
    File: led_strip.c
    Author(s):  Lucas Bruder <LBruder@me.com>
    Date Created: 11/23/2016
    Last modified: 11/26/2016

    Description: LED Library for driving various led strips on ESP32.

    This library uses double buffering to display the LEDs.
    If the driver is showing buffer 1, any calls to led_strip_set_pixel_color
    will write to buffer 2. When it's time to drive the pixels on the strip, it
    refers to buffer 1. 
    When led_strip_show is called, it will switch to displaying the pixels
    from buffer 2 and will clear buffer 1. Any writes will now happen on buffer 1 
    and the task will look at buffer 2 for refreshing the LEDs
    ------------------------------------------------------------------------- */

#include "led_strip.h"
#include "freertos/task.h"

#include <string.h>

#define LED_STRIP_TASK_SIZE             (512)
#define LED_STRIP_TASK_PRIORITY         (configMAX_PRIORITIES - 1)

#define LED_STRIP_REFRESH_PERIOD_MS     (30U) // TODO: add as parameter to led_strip_init

#define LED_STRIP_NUM_RMT_ITEMS_PER_LED (24U) // Assumes 24 bit color for each led

// RMT Clock source is @ 80 MHz. Dividing it by 8 gives us 10 MHz frequency, or 100ns period.
#define LED_STRIP_RMT_CLK_DIV (8)

/****************************
        WS2812 Timing
 ****************************/
#define LED_STRIP_RMT_TICKS_BIT_1_HIGH_WS2812 9 // 900ns (900ns +/- 150ns per datasheet)
#define LED_STRIP_RMT_TICKS_BIT_1_LOW_WS2812  3 // 300ns (350ns +/- 150ns per datasheet)
#define LED_STRIP_RMT_TICKS_BIT_0_HIGH_WS2812 3 // 300ns (350ns +/- 150ns per datasheet)
#define LED_STRIP_RMT_TICKS_BIT_0_LOW_WS2812  9 // 900ns (900ns +/- 150ns per datasheet)

/****************************
        SK6812 Timing
 ****************************/
#define LED_STRIP_RMT_TICKS_BIT_1_HIGH_SK6812 6
#define LED_STRIP_RMT_TICKS_BIT_1_LOW_SK6812  6
#define LED_STRIP_RMT_TICKS_BIT_0_HIGH_SK6812 3
#define LED_STRIP_RMT_TICKS_BIT_0_LOW_SK6812  9

/****************************
        APA106 Timing
 ****************************/
#define LED_STRIP_RMT_TICKS_BIT_1_HIGH_APA106 14 // 1.36us +/- 150ns per datasheet
#define LED_STRIP_RMT_TICKS_BIT_1_LOW_APA106   3 // 350ns +/- 150ns per datasheet
#define LED_STRIP_RMT_TICKS_BIT_0_HIGH_APA106  3 // 350ns +/- 150ns per datasheet
#define LED_STRIP_RMT_TICKS_BIT_0_LOW_APA106  14 // 1.36us +/- 150ns per datasheet

// Function pointer for generating waveforms based on different LED drivers
typedef void (*led_fill_rmt_items_fn)(struct led_color_t *led_strip_buf, rmt_item32_t *rmt_items, uint32_t led_strip_length);

static inline void led_strip_fill_item_level(rmt_item32_t* item, int high_ticks, int low_ticks)
{
    item->level0 = 1;
    item->duration0 = high_ticks;
    item->level1 = 0;
    item->duration1 = low_ticks;
}

static inline void led_strip_rmt_bit_1_sk6812(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_1_HIGH_SK6812, LED_STRIP_RMT_TICKS_BIT_1_LOW_SK6812);
}

static inline void led_strip_rmt_bit_0_sk6812(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_0_HIGH_SK6812, LED_STRIP_RMT_TICKS_BIT_0_LOW_SK6812);
}

static void led_strip_fill_rmt_items_sk6812(struct led_color_t *led_strip_buf, rmt_item32_t *rmt_items, uint32_t led_strip_length)
{
    uint32_t rmt_items_index = 0;
    for (uint32_t led_index = 0; led_index < led_strip_length; led_index++) {
        struct led_color_t led_color = led_strip_buf[led_index];

        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.green >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_sk6812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_sk6812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.red >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_sk6812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_sk6812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.blue >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_sk6812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_sk6812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
    }
}

static inline void led_strip_rmt_bit_1_ws2812(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_1_HIGH_WS2812, LED_STRIP_RMT_TICKS_BIT_1_LOW_WS2812);
}

static inline void led_strip_rmt_bit_0_ws2812(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_0_HIGH_WS2812, LED_STRIP_RMT_TICKS_BIT_0_LOW_WS2812);
}

static void led_strip_fill_rmt_items_ws2812(struct led_color_t *led_strip_buf, rmt_item32_t *rmt_items, uint32_t led_strip_length)
{
    uint32_t rmt_items_index = 0;
    for (uint32_t led_index = 0; led_index < led_strip_length; led_index++) {
        struct led_color_t led_color = led_strip_buf[led_index];

        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.green >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_ws2812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_ws2812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.red >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_ws2812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_ws2812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.blue >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_ws2812(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_ws2812(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
    }
}

static inline void led_strip_rmt_bit_1_apa106(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_1_HIGH_APA106, LED_STRIP_RMT_TICKS_BIT_1_LOW_APA106);
}

static inline void led_strip_rmt_bit_0_apa106(rmt_item32_t* item)
{
    led_strip_fill_item_level(item, LED_STRIP_RMT_TICKS_BIT_0_HIGH_APA106, LED_STRIP_RMT_TICKS_BIT_0_LOW_APA106);
}

static void led_strip_fill_rmt_items_apa106(struct led_color_t *led_strip_buf, rmt_item32_t *rmt_items, uint32_t led_strip_length)
{
    uint32_t rmt_items_index = 0;
    for (uint32_t led_index = 0; led_index < led_strip_length; led_index++) {
        struct led_color_t led_color = led_strip_buf[led_index];

        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.red >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_apa106(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_apa106(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.green >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_apa106(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_apa106(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
        for (uint8_t bit = 8; bit != 0; bit--) {
            uint8_t bit_set = (led_color.blue >> (bit - 1)) & 1;
            if(bit_set) {
                led_strip_rmt_bit_1_apa106(&(rmt_items[rmt_items_index]));
            } else {
                led_strip_rmt_bit_0_apa106(&(rmt_items[rmt_items_index]));
            }
            rmt_items_index++;
        }
    }
}

static void led_strip_task(void *arg)
{
    struct led_strip_t *led_strip = (struct led_strip_t *)arg;
    led_fill_rmt_items_fn led_make_waveform = NULL;
    bool make_new_rmt_items = true;
    bool prev_showing_buf_1 = !led_strip->showing_buf_1;

    size_t num_items_malloc = (LED_STRIP_NUM_RMT_ITEMS_PER_LED * led_strip->led_strip_length);
    rmt_item32_t *rmt_items = (rmt_item32_t*) malloc(sizeof(rmt_item32_t) * num_items_malloc);
    if (!rmt_items) {
        vTaskDelete(NULL);
    }

    switch (led_strip->rgb_led_type) {
        case RGB_LED_TYPE_WS2812:
            led_make_waveform = led_strip_fill_rmt_items_ws2812;
            break;

        case RGB_LED_TYPE_SK6812:
            led_make_waveform = led_strip_fill_rmt_items_sk6812;
            break;

        case RGB_LED_TYPE_APA106:
            led_make_waveform = led_strip_fill_rmt_items_apa106;
            break;

        default:
            // Will avoid keeping it point to NULL
            led_make_waveform = led_strip_fill_rmt_items_ws2812;
            break;
    };

    for(;;) {
        rmt_wait_tx_done(led_strip->rmt_channel, portMAX_DELAY);
        xSemaphoreTake(led_strip->access_semaphore, portMAX_DELAY);

        /*
         * If buf 1 was previously being shown and now buf 2 is being shown,
         * it should update the new rmt items array. If buf 2 was previous being shown
         * and now buf 1 is being shown, it should update the new rmt items array.
         * Otherwise, no need to update the array
         */
        if ((prev_showing_buf_1 == true) && (led_strip->showing_buf_1 == false)) {
            make_new_rmt_items = true;
        } else if ((prev_showing_buf_1 == false) && (led_strip->showing_buf_1 == true)) {
            make_new_rmt_items = true;
        } else {
            make_new_rmt_items = false;
        }

        if (make_new_rmt_items) {
            if (led_strip->showing_buf_1) {
                led_make_waveform(led_strip->led_strip_buf_1, rmt_items, led_strip->led_strip_length);
            } else {
                led_make_waveform(led_strip->led_strip_buf_2, rmt_items, led_strip->led_strip_length);
            }
        }

        rmt_write_items(led_strip->rmt_channel, rmt_items, num_items_malloc, false);
        prev_showing_buf_1 = led_strip->showing_buf_1;
        xSemaphoreGive(led_strip->access_semaphore);
        vTaskDelay(LED_STRIP_REFRESH_PERIOD_MS / portTICK_PERIOD_MS);
    }

    if (rmt_items) {
        free(rmt_items);
    }
    vTaskDelete(NULL);
}

static bool led_strip_init_rmt(struct led_strip_t *led_strip)
{
    rmt_config_t rmt_cfg = {
        .rmt_mode = RMT_MODE_TX,
        .channel = led_strip->rmt_channel,
        .clk_div = LED_STRIP_RMT_CLK_DIV,
        .gpio_num = led_strip->gpio,
        .mem_block_num = 1,
        .tx_config = {
            .loop_en = false,
            .carrier_freq_hz = 100, // Not used, but has to be set to avoid divide by 0 err
            .carrier_duty_percent = 50,
            .carrier_level = RMT_CARRIER_LEVEL_LOW,
            .carrier_en = false,
            .idle_level = RMT_IDLE_LEVEL_LOW,
            .idle_output_en = true,
        }
    };

    esp_err_t cfg_ok = rmt_config(&rmt_cfg);
    if (cfg_ok != ESP_OK) {
        return false;
    }
    esp_err_t install_ok = rmt_driver_install(rmt_cfg.channel, 0, 0);
    if (install_ok != ESP_OK) {
        return false;
    }

    return true;
}

bool led_strip_init(struct led_strip_t *led_strip)
{
    TaskHandle_t led_strip_task_handle;

    if ((led_strip == NULL) ||
        (led_strip->rmt_channel == RMT_CHANNEL_MAX) ||
        (led_strip->gpio > GPIO_NUM_33) ||  // only inputs above 33
        (led_strip->led_strip_buf_1 == NULL) ||
        (led_strip->led_strip_buf_2 == NULL) ||
        (led_strip->led_strip_length == 0) ||
        (led_strip->access_semaphore == NULL)) {
        return false;
    }

    if(led_strip->led_strip_buf_1 == led_strip->led_strip_buf_2) {
        return false;
    }

    memset(led_strip->led_strip_buf_1, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);
    memset(led_strip->led_strip_buf_2, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);

    bool init_rmt = led_strip_init_rmt(led_strip);
    if (!init_rmt) {
        return false;
    }

    xSemaphoreGive(led_strip->access_semaphore);
    BaseType_t task_created = xTaskCreate(led_strip_task,
                                            "led_strip_task",
                                            LED_STRIP_TASK_SIZE,
                                            led_strip,
                                            LED_STRIP_TASK_PRIORITY,
                                            &led_strip_task_handle
                                         );

    if (!task_created) {
        return false;
    }

    return true;
}

bool led_strip_set_pixel_color(struct led_strip_t *led_strip, uint32_t pixel_num, struct led_color_t *color)
{
    bool set_led_success = true;

    if ((!led_strip) || (!color) || (pixel_num > led_strip->led_strip_length)) {
        return false;
    }

    if (led_strip->showing_buf_1) {
        led_strip->led_strip_buf_2[pixel_num] = *color;
    } else {
        led_strip->led_strip_buf_1[pixel_num] = *color;
    }

    return set_led_success;
}

bool led_strip_set_pixel_rgb(struct led_strip_t *led_strip, uint32_t pixel_num, uint8_t red, uint8_t green, uint8_t blue)
{
    bool set_led_success = true;

    if ((!led_strip) || (pixel_num > led_strip->led_strip_length)) {
        return false;
    }

    if (led_strip->showing_buf_1) {
        led_strip->led_strip_buf_2[pixel_num].red = red;
        led_strip->led_strip_buf_2[pixel_num].green = green;
        led_strip->led_strip_buf_2[pixel_num].blue = blue;
    } else {
        led_strip->led_strip_buf_1[pixel_num].red = red;
        led_strip->led_strip_buf_1[pixel_num].green = green;
        led_strip->led_strip_buf_1[pixel_num].blue = blue;
    }

    return set_led_success;
}

bool led_strip_get_pixel_color(struct led_strip_t *led_strip, uint32_t pixel_num, struct led_color_t *color)
{
    bool get_success = true;

    if ((!led_strip) ||
        (pixel_num > led_strip->led_strip_length) ||
        (!color)) {
        color = NULL;
        return false;
    }

    if (led_strip->showing_buf_1) {
        *color = led_strip->led_strip_buf_1[pixel_num];
    } else {
        *color = led_strip->led_strip_buf_2[pixel_num];
    }

    return get_success;
}

/**
 * Updates the led buffer to be shown
 */
bool led_strip_show(struct led_strip_t *led_strip)
{
    bool success = true;

    if (!led_strip) {
        return false;
    }

    xSemaphoreTake(led_strip->access_semaphore, portMAX_DELAY);
    if (led_strip->showing_buf_1) {
        led_strip->showing_buf_1 = false;
        memset(led_strip->led_strip_buf_1, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);
    } else {
        led_strip->showing_buf_1 = true;
        memset(led_strip->led_strip_buf_2, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);
    }
    xSemaphoreGive(led_strip->access_semaphore);

    return success;
}

/**
 * Clears the LED strip
 */
bool led_strip_clear(struct led_strip_t *led_strip)
{
    bool success = true;

    if (!led_strip) {
        return false;
    }

    if (led_strip->showing_buf_1) {
        memset(led_strip->led_strip_buf_2, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);
    } else {
        memset(led_strip->led_strip_buf_1, 0, sizeof(struct led_color_t) * led_strip->led_strip_length);
    }

    return success;
}
