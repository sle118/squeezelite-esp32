/* 
 *  (c) Chuck Rohs 2020, chuck@zethus.ca
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include <ctype.h>
#include <math.h>
#include "esp_dsp.h"
#include "squeezelite.h"
#include "slimproto.h"
#include "display.h"
#include "led_strip.h"
#include "platform_config.h"

static const char *TAG = "rgb_led_vu";

#define RGB_LED_VU_STACK_SIZE 	(3*1024)
#define VU_COUNT 48

#define RMS_LEN_BIT    6
#define RMS_LEN       (1 << RMS_LEN_BIT)

#define max(a,b) (((a) > (b)) ? (a) : (b))

static void rgb_led_vu_displayer_task(void* arg);

#define LED_STRIP_LENGTH 31U  /* should be odd, since there is one led in the center, and VUs on either side */
#define LED_STRIP_RMT_INTR_NUM 20U
#define LED_STRIP_PEAK_HOLD 10U
#define MAX_BARS 2U
#define LED_STRIP_DEFAULT_REFRESH 100U /* in milliseconds */ 
#define LED_STRIP_DEFAULT_BRIGHT  10U /* out of 255 */

static struct led_strip_t* led_strip_p;
static struct led_strip_t  led_strip_config = {
    .rgb_led_type      = RGB_LED_TYPE_WS2812,
    .rmt_channel       = RMT_CHANNEL_0,
    .rmt_interrupt_num = LED_STRIP_RMT_INTR_NUM,
    .gpio              = GPIO_NUM_21,
};

static EXT_RAM_ATTR struct {
    TaskHandle_t task;
    int          led_data_pin;
    int          hold;
    int          led_strip_length;
    uint8_t      bright;
    int          refresh;
    struct {
        int current, max;
        int limit;
    } bars[MAX_BARS];
    int n;
    int max; /* the max integer value of the vu, 47 for now */
} rgb_led_vu;

/****************************************************************************************
 * Initialize the RGB led vu meters if configured.
 */
void
rgb_led_vu_init(void)
{
	static DRAM_ATTR StaticTask_t xTaskBuffer __attribute__ ((aligned (4)));
	static EXT_RAM_ATTR StackType_t xStack[RGB_LED_VU_STACK_SIZE] __attribute__ ((aligned (4)));
	
    char* p;
    char* config = config_alloc_get_str("led_vu_config", NULL, "N/A");

    /* Try to configure the RGB VU meter */
    char* drivername = strcasestr(config, "WS2812");

    if ((p = strcasestr(config, "length")) != NULL) {
        rgb_led_vu.led_strip_length = atoi(strchr(p, '=') + 1);
    }
    if ((p = strcasestr(config, "data")) != NULL) {
        rgb_led_vu.led_data_pin = atoi(strchr(p, '=') + 1);
    }
    if ((p = strcasestr(config, "hold")) != NULL) {
        rgb_led_vu.hold = atoi(strchr(p, '=') + 1);
    } else {
        rgb_led_vu.hold = LED_STRIP_PEAK_HOLD;
    }
    if ((p = strcasestr(config, "bright")) != NULL) {
        rgb_led_vu.bright = atoi(strchr(p, '=') + 1);
    } else {
        rgb_led_vu.bright = LED_STRIP_DEFAULT_BRIGHT;
    }
    if ((p = strcasestr(config, "refresh")) != NULL) {
        rgb_led_vu.refresh = atoi(strchr(p, '=') + 1);
    } else {
        rgb_led_vu.refresh = LED_STRIP_DEFAULT_REFRESH;
    }

    /* ensure length is odd */
    if (!drivername || !(rgb_led_vu.led_strip_length % 2) ||
        !rgb_led_vu.led_data_pin || rgb_led_vu.refresh < 30) {
        ESP_LOGI(TAG, "rgb_led_vu configuration invalid");
        goto done;
    }

    rgb_led_vu.n         = MAX_BARS;
    rgb_led_vu.max       = VU_COUNT - 1;

    ESP_LOGI(TAG,
             "rgb_led_vu initializing with length=%d, data=%d, hold=%d, bright=%d, refresh=%d",
             rgb_led_vu.led_strip_length,
             rgb_led_vu.led_data_pin,
             rgb_led_vu.hold,
             rgb_led_vu.bright,
             rgb_led_vu.refresh);


    led_strip_config.access_semaphore = xSemaphoreCreateBinary();
    led_strip_config.led_strip_length = rgb_led_vu.led_strip_length;
    led_strip_config.led_strip_buf_1 =
        heap_caps_malloc(rgb_led_vu.led_strip_length * sizeof(struct led_color_t),
                         MALLOC_CAP_8BIT);
    led_strip_config.led_strip_buf_2 =
        heap_caps_malloc(rgb_led_vu.led_strip_length * sizeof(struct led_color_t),
                         MALLOC_CAP_8BIT);
    led_strip_config.gpio = rgb_led_vu.led_data_pin;

    bool led_init_ok = led_strip_init(&led_strip_config);
    if (led_init_ok) {
        led_strip_p = &led_strip_config;
        ESP_LOGI(TAG, "rgb_led_vu init successful");
    } else {
        ESP_LOGE(TAG, "rgb_led_vu init failed");
    }

    /* grab all the memory just in case. Not doing this was causing
           glitching on larger length strings */
    rmt_set_mem_block_num(RMT_CHANNEL_0, 8);

    // Create a meter thread
    rgb_led_vu.task  = xTaskCreateStatic((TaskFunction_t) rgb_led_vu_displayer_task,
                                       "rgb_led_vu_thread",
                                       RGB_LED_VU_STACK_SIZE,
                                       NULL,
                                       ESP_TASK_PRIO_MIN + 1,
                                       xStack,
                                       &xTaskBuffer);

    /* Display RGB on first three values */
    led_strip_clear(led_strip_p);
    if (rgb_led_vu.led_strip_length >= 3) {
        led_strip_set_pixel_rgb(led_strip_p, 0, 10, 0, 0);
        led_strip_set_pixel_rgb(led_strip_p, 1, 0, 10, 0);
        led_strip_set_pixel_rgb(led_strip_p, 2, 0, 0, 10);
    }
    led_strip_show(led_strip_p);
done:
    free(config);
    return;
}


/* bugs, reds missing on one side */
void display_led_vu(int left_vu_sample, int right_vu_sample) {
    static int lp     = 0;
    static int rp     = 0;
    static int decayl = 0;
    static int decayr = 0;

    int midpoint  = (rgb_led_vu.led_strip_length - 1) / 2;
    int vu_length = (rgb_led_vu.led_strip_length - 1) / 2;

    uint8_t bv = rgb_led_vu.bright;

    struct led_color_t red    = {.red = bv, .green = 0, .blue = 0};
    struct led_color_t green  = {.red = 0, .green = bv, .blue = 0};
    struct led_color_t orange = {.red = bv, .green = bv, .blue = 0};
    struct led_color_t blue   = {.red = 0, .green = bv, .blue = bv};

    /* figure out how many leds to light */
    left_vu_sample  = left_vu_sample * vu_length / VU_COUNT;
    right_vu_sample = right_vu_sample * vu_length / VU_COUNT;

    /* save the peaks */
    if (lp > left_vu_sample) {
        if (decayl > 0) {
            decayl--;
        } else {
            decayl = rgb_led_vu.hold;
            lp--;
        }
    } else {
        lp = left_vu_sample;
        decayl = rgb_led_vu.hold;
    }
    if (rp > right_vu_sample) {
        if (decayr > 0) {
            decayr--;
        } else {
            decayr = rgb_led_vu.hold;
            rp--;
        }
    } else {
        rp = right_vu_sample;
        decayr = rgb_led_vu.hold;
    }

    lp = max(lp, left_vu_sample);
    rp = max(rp, right_vu_sample);

    led_strip_clear(led_strip_p);

    /* set center led red */

    led_strip_set_pixel_color(led_strip_p, midpoint, &red);

    for (int i = 6; i < midpoint; i++) {
        led_strip_set_pixel_color(led_strip_p, i, &green);
        led_strip_set_pixel_color(led_strip_p, LED_STRIP_LENGTH - i - 1, &green);
    }
    for (int i = 3; i < 6; i++) {
        led_strip_set_pixel_color(led_strip_p,i,&orange); // orange
        led_strip_set_pixel_color(led_strip_p, LED_STRIP_LENGTH - i - 1, &orange);   //orange
    }
    for (int i = 0; i < 3; i++) {
        led_strip_set_pixel_color(led_strip_p, i, &red);  //red
        led_strip_set_pixel_color(led_strip_p, LED_STRIP_LENGTH - i - 1,  &red);  //red
    }

    /* erase left */
    left_vu_sample = (left_vu_sample > midpoint) ? 0 : (midpoint - left_vu_sample);
    for (int i = 0; i < left_vu_sample; i++) {
        led_strip_set_pixel_rgb(led_strip_p, i, 0, 0, 0);
    }

    /* erase right */
    right_vu_sample = (right_vu_sample > midpoint) ? 0 : (midpoint - right_vu_sample);
    for (int i = 0; i < right_vu_sample; i++) {
        led_strip_set_pixel_rgb(led_strip_p, LED_STRIP_LENGTH - i - 1, 0, 0, 0);
    }

    /* pop in the peaks */
    lp = (lp > midpoint) ? midpoint : lp;
    if (lp) {
        led_strip_set_pixel_color(led_strip_p, midpoint - lp, &blue);
    }
    rp = (rp > 20) ? 20 : rp;
    if (rp) {
        led_strip_set_pixel_color(led_strip_p, midpoint + rp, &blue);
    }

    led_strip_show(led_strip_p);
}

/****************************************************************************************
 * Update visualization bars
 */
static void
vu_update(void)
{
    // no need to protect against no woning the display as we are playing
    if (pthread_mutex_trylock(&visu_export.mutex)) return;

    // not enough samples
    if (visu_export.level < RMS_LEN * 2 && visu_export.running) {
        pthread_mutex_unlock(&visu_export.mutex);
        return;
    }

    // reset bars for all cases first
    for (int i = rgb_led_vu.n; --i >= 0;) { rgb_led_vu.bars[i].current = 0; }

    if (visu_export.running) {
        s16_t* iptr = visu_export.buffer;

        // calculate sum(L²+R²), try to not overflow at the expense of some
        // precision
        for (int i = RMS_LEN; --i >= 0;) {
            rgb_led_vu.bars[0].current +=
                (*iptr * *iptr + (1 << (RMS_LEN_BIT - 2))) >> (RMS_LEN_BIT - 1);
            iptr++;
            rgb_led_vu.bars[1].current +=
                (*iptr * *iptr + (1 << (RMS_LEN_BIT - 2))) >> (RMS_LEN_BIT - 1);
            iptr++;
        }

        // convert to dB (1 bit remaining for getting X²/N, 60dB dynamic
        // starting from 0dBFS = 3 bits back-off)
        for (int i = rgb_led_vu.n; --i >= 0;) {
            rgb_led_vu.bars[i].current =
                rgb_led_vu.max *
                (0.01667f * 10 *
                     log10f(0.0000001f +
                            (rgb_led_vu.bars[i].current >>
                             (visu_export.gain == FIXED_ONE ? 7 : 1))) -
                 0.2543f);
            if (rgb_led_vu.bars[i].current > rgb_led_vu.max) {
                rgb_led_vu.bars[i].current = rgb_led_vu.max;
            } else if (rgb_led_vu.bars[i].current < 0) {
                rgb_led_vu.bars[i].current = 0;
            }
        }
    }
    // we took what we want, we can release the buffer
    visu_export.level = 0;
    pthread_mutex_unlock(&visu_export.mutex);

    // don't refresh if all max are 0 (we were are somewhat idle)
    int clear = 0;
    for (int i = rgb_led_vu.n; --i >= 0;) {
        clear = max(clear, rgb_led_vu.bars[i].max);
    }
    if (clear){display_led_vu(0, 0);}
    else{display_led_vu(rgb_led_vu.bars[0].current, rgb_led_vu.bars[1].current);}
}

/****************************************************************************************
 * Meter display task
 */
static void
rgb_led_vu_displayer_task(void* args)
{
    while (1) {
        vu_update();
        vTaskDelay(rgb_led_vu.refresh / portTICK_PERIOD_MS);
    }
}
