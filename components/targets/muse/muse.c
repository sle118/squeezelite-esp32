/*
 YOUR LICENSE
 */
#include <string.h>
#include <esp_log.h>
#include <esp_types.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/rmt.h"
#include "globdefs.h"
#include "monitor.h"
#include "targets.h"

/////////////////////////////////////////////////////////////////
//*********************** NeoPixels  ***************************
////////////////////////////////////////////////////////////////
#define NUM_LEDS 1
#define LED_RMT_TX_GPIO 22

#define BITS_PER_LED_CMD 24
#define LED_BUFFER_ITEMS ((NUM_LEDS * BITS_PER_LED_CMD))

// These values are determined by measuring pulse timing with logic analyzer and adjusting to match datasheet.
#define T0H 14 // 0 bit high time
#define T1H 52 // 1 bit high time
#define TL 52  // low time for either bit

// sets a color based on RGB from 0..255 and a brightness in % from 0..100
#define RGB(R, G, B, BR) (((G * BR) / 100) << 16) | (((R * BR) / 100) << 8) | ((B * BR) / 100)

#define RED RGB(255, 0, 0, 10)
#define GREEN RGB(0, 255, 0, 10)
#define BLUE RGB(0, 0, 255, 10)
#define WHITE RGB(255, 255, 255, 10)
#define YELLOW RGB(255, 118, 13, 10)

struct led_state {
    uint32_t leds[NUM_LEDS];
};

static int rmt_channel;

void ws2812_control_init(void);
void ws2812_write_leds(struct led_state new_state);

///////////////////////////////////////////////////////////////////

static const char TAG[] = "muse";

static void (*battery_handler_chain)(float value, int cells);
static void battery_svc(float value, int cells);
static bool init(void);
static void set_battery_led(float value);

const struct target_s target_muse = {.model = "muse", .init = init};

static bool init(void) {
    battery_handler_chain = battery_handler_svc;
    battery_handler_svc = battery_svc;

    ws2812_control_init();
    float value = battery_value_svc();
    set_battery_led(value);

    ESP_LOGI(TAG, "Initializing for Muse %f", value);

    return true;
}

#define VGREEN 4.0
#define VRED 3.6

static void set_battery_led(float value) {
    struct led_state new_state;

    if(value > VGREEN)
        new_state.leds[0] = GREEN;
    else if(value < VRED)
        new_state.leds[0] = RED;
    else
        new_state.leds[0] = YELLOW;

    ws2812_write_leds(new_state);
}

static void battery_svc(float value, int cells) {
    set_battery_led(value);
    ESP_LOGI(TAG, "Called for battery service with %f", value);

    if(battery_handler_chain) battery_handler_chain(value, cells);
}

// This is the buffer which the hw peripheral will access while pulsing the output pin
rmt_item32_t led_data_buffer[LED_BUFFER_ITEMS];

void setup_rmt_data_buffer(struct led_state new_state);

void ws2812_control_init(void) {
    rmt_channel = RMT_NEXT_TX_CHANNEL();
    rmt_config_t config;
    config.rmt_mode = RMT_MODE_TX;
    config.channel = rmt_channel;
    config.gpio_num = LED_RMT_TX_GPIO;
    config.mem_block_num = 3;
    config.tx_config.loop_en = false;
    config.tx_config.carrier_en = false;
    config.tx_config.idle_output_en = true;
    config.tx_config.idle_level = 0;
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    ESP_LOGI(TAG, "LED wth ws2812 using gpio %d and channel %d", LED_RMT_TX_GPIO, rmt_channel);
}

void ws2812_write_leds(struct led_state new_state) {
    setup_rmt_data_buffer(new_state);
    rmt_write_items(rmt_channel, led_data_buffer, LED_BUFFER_ITEMS, false);
}

void setup_rmt_data_buffer(struct led_state new_state) {
    for(uint32_t led = 0; led < NUM_LEDS; led++) {
        uint32_t bits_to_send = new_state.leds[led];
        uint32_t mask = 1 << (BITS_PER_LED_CMD - 1);
        for(uint32_t bit = 0; bit < BITS_PER_LED_CMD; bit++) {
            uint32_t bit_is_set = bits_to_send & mask;
            led_data_buffer[led * BITS_PER_LED_CMD + bit] = bit_is_set ? (rmt_item32_t){{{T1H, 1, TL, 0}}} : (rmt_item32_t){{{T0H, 1, TL, 0}}};
            mask >>= 1;
        }
    }
}
