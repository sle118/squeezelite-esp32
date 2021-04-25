/* 
 *  Control of LED strip within squeezelite-esp32 
 *     
 *  (c) Wizmo 2021
 *
 *  Loosely based on code by 
 *     Chuck Rohs 2020, chuck@zethus.ca
 * 
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 * ToDo:
 * Driver does support other led device. Maybe look at supporting in future. 
 * Bright supports up to 255, but expect bad behaviour above 128.
 * The VU refresh rate has been decreaced (100->75) to optimize animation of spin dial.  Could make
 *   configurable like text scrolling (or use the same value)  
 * Artwork function, but not released as very buggy and not really practical
 */

#include <ctype.h>
#include <math.h>
#include "esp_log.h"

#include "led_strip.h"
#include "platform_config.h"
#include "led_vu.h"

static const char *TAG = "led_vu";

#define LED_VU_STACK_SIZE 	(3*1024)
#define LED_VU_RMT_INTR_NUM 20U

#define LED_VU_DEFAULT_REFRESH 75U /* in milliseconds */ 
#define LED_VU_DEFAULT_BRIGHT  20U /* out of 255 */

#define LED_VU_PEAK_HOLD 6U

#define max(a,b) (((a) > (b)) ? (a) : (b))

static struct led_strip_t* led_vu = NULL;
static struct led_strip_t  led_strip_config = {
    .rgb_led_type      = RGB_LED_TYPE_WS2812,
    .rmt_channel       = RMT_CHANNEL_1,
    .rmt_interrupt_num = LED_VU_RMT_INTR_NUM,
    .gpio              = GPIO_NUM_22,
};

static struct {
	int gpio;
	int length;
	uint8_t bright;
    int vu_length;
    int vu_start_l;
    int vu_start_r;
    int vu_odd;
} strip = { .gpio = 22, .length = 19, .bright = 20 };

static int led_addr(int pos ) {
    if (pos < 0) return pos + strip.length;
    if (pos >= strip.length) return pos - strip.length;
    return pos;
}

/****************************************************************************************
 * Initialize the led vu strip if configured.
 * 
 */
void led_vu_init()
{
	
    char* p;
    char* config = config_alloc_get_str("led_vu_config", NULL, "N/A");

    // Initialize led VU strip 
    char* drivername = strcasestr(config, "WS2812");

    if ((p = strcasestr(config, "length")) != NULL) {
        strip.length = atoi(strchr(p, '=') + 1);
    }
    if ((p = strcasestr(config, "gpio")) != NULL) {
        strip.gpio = atoi(strchr(p, '=') + 1);
    }
    if ((p = strcasestr(config, "bright")) != NULL) {
        strip.bright = atoi(strchr(p, '=') + 1);
    } else {
        strip.bright = LED_VU_DEFAULT_BRIGHT;
    }
    // check for valid configuration
    if (!drivername || !strip.gpio) {
        ESP_LOGI(TAG, "led_vu configuration invalid");
        goto done;
    }
   
    // initialize vu settings
    //strip.vu_length = (strip.length % 2) ? strip.length / 2 : (strip.length  - 1) / 2;
    strip.vu_length = (strip.length  - 1) / 2;
    strip.vu_start_l  = strip.vu_length;
    strip.vu_start_r = strip.vu_start_l + 1;
    strip.vu_odd = strip.length - 1;

    // create driver configuration
    led_strip_config.access_semaphore = xSemaphoreCreateBinary();
    led_strip_config.led_strip_length = strip.length;
    led_strip_config.led_strip_working = heap_caps_malloc(strip.length * sizeof(struct led_color_t), MALLOC_CAP_8BIT);
    led_strip_config.led_strip_showing = heap_caps_malloc(strip.length * sizeof(struct led_color_t), MALLOC_CAP_8BIT);
    led_strip_config.gpio = strip.gpio;

    // initialize driver 
    bool led_init_ok = led_strip_init(&led_strip_config);
    if (led_init_ok) {
        led_vu = &led_strip_config;
        ESP_LOGI(TAG, "led_vu using gpio:%d length:%d bright:%d", strip.gpio, strip.length, strip.bright);
} else {
        ESP_LOGE(TAG, "led_vu init failed");
        goto done;
    }

    // reserver max memory for remote management systems
    rmt_set_mem_block_num(RMT_CHANNEL_1, 7);

    led_vu_clear(led_vu);

    done:
        free(config);
        return;
    }

inline bool inRange(double x, double y, double z) {
    return (x > y && x < z);
}

/****************************************************************************************
 * Turns all LEDs off (Black)
 */
void led_vu_clear() {
    if (!led_vu) return;
    led_strip_clear(led_vu);

    led_strip_show(led_vu);
}

/****************************************************************************************
 * Sets all LEDs to one color
 * r = red (0-255), g = green (0-255), b - blue (0-255)
 *      note - all colors are adjusted for brightness
 */
void led_vu_color_all(uint8_t r, uint8_t g, uint8_t b) {
    if (!led_vu) return;

    struct led_color_t color_on = {.red = strip.bright*r/255, .green = strip.bright*g/255, .blue = strip.bright*b/255}; 

    for (int i = 0 ; i < strip.length ; i ++){
        led_strip_set_pixel_color(led_vu, i, &color_on);
    }

    led_strip_show(led_vu);
}

/****************************************************************************************
 * Sets LEDs based on a data packet consiting of rgb data
 * offset - starting LED,
 * length - number of leds (3x rgb bytes) 
 * data - array of rgb values in multiples of 3 bytes
 */
void led_vu_data(uint8_t* data, uint16_t offset, uint16_t length) {
    if (!led_vu) return;

	uint8_t* p = (uint8_t*) data;									        
	for (int i = 0; i < length; i++) {					            
		led_strip_set_pixel_rgb(led_vu, i+offset, *p, *(p+1), *(p+2));
        p+=3;
	} 

    led_strip_show(led_vu);
}

/****************************************************************************************
 * Progress bar display
 * data - array of gain values(0-100)
 * offset - starting position
 * length - size of array
 */
void led_vu_spectrum(uint8_t* data, uint16_t offset, uint16_t length) {
    if (!led_vu) return;

    uint8_t* p = (uint8_t*) data;									        
	for (int i=0; i<length; i++) {
		uint8_t gain = *p;
        led_strip_set_pixel_rgb(led_vu, i+offset, gain * gain * strip.bright / 12800, 0, gain * strip.bright / 255);
        p++;
    }
    
    led_strip_show(led_vu);
 }

/****************************************************************************************
 * Progress bar display
 * pct - percentage complete (0-100)
 */
void led_vu_progress_bar(int pct) {
    if (!led_vu) return;

    // define colors
    uint8_t bv = strip.bright;
    struct led_color_t color_on   = {.red = bv, .green = 0, .blue = 0};
    struct led_color_t color_off = {.red = 0, .green = bv, .blue = 0};

    // calcuate led position
    int led_lit = strip.length * pct / LED_VU_SCALE;

    // set colors
    for (int i = 0; i < strip.length; i++) {
        led_strip_set_pixel_color(led_vu, i, (i < led_lit) ? &color_off : &color_on);
    }

    led_strip_show(led_vu);
}

/****************************************************************************************
 * Spin dial display
 * gain - brightness (0-100), rate - color change speed (0-100) 
 * comet - alternate display mode
 */
void led_vu_spin_dial(int gain, int rate, bool comet) 
{
    static int led_pos = 0;
    static uint8_t r = 0;
    static uint8_t g = 0;
    static uint8_t b = 0;
    if (!led_vu) return;

    uint8_t bv = (comet) ? strip.bright * 2: strip.bright;
    
    // calculate next color
    uint8_t step = rate / 2; // controls color change speed
    if (r == 0 && g == 0 && b == 0) {
        r = 255U; g = step; 
    } else if (b == 0) {
        g = (g > 255U-step) ? 255U : g + step;
        r = (r < step) ? 0 : r - step;
        if (r == 0) b = step;
    } else if (r == 0) {
        b = (b > 255U-step) ? 255U : b + step;
        g = (g < step) ? 0 : g- step;
        if (g == 0) r = step;
    } else { 
        r = (r > 255U-step) ? 255U : r + step;
        b = (b < step) ? 0 : b - step;
        if (r == 0) b = step;
    }

    // pulse to gain (squared for extra pulse)
    int8_t pulse = gain * gain / LED_VU_SCALE;
    uint8_t rp = bv * r * pulse / (LED_VU_SCALE * 255); 
    uint8_t gp = bv * g * pulse / (LED_VU_SCALE * 255); 
    uint8_t bp = bv * b * pulse / (LED_VU_SCALE * 255); 

    // set led color_
    led_strip_set_pixel_rgb(led_vu, led_pos, rp, gp, bp);
    if (comet) {
        led_strip_set_pixel_rgb(led_vu, led_addr(led_pos-1), rp/2, gp/2, bp/2);
        led_strip_set_pixel_rgb(led_vu, led_addr(led_pos-2), rp/4, gp/4, bp/4);
        led_strip_set_pixel_rgb(led_vu, led_addr(led_pos-3), rp/8, gp/8, bp/8);
        led_strip_set_pixel_rgb(led_vu, led_addr(led_pos-4), 0, 0, 0);
    }
    
    // next led
    led_pos = led_addr(++led_pos);

    led_strip_show(led_vu);
}

/****************************************************************************************
 * VU meter display
 * vu_l - left response (0-100), vu_r - right response (0-100)
 * comet - alternate display mode
 */
void led_vu_display(int vu_l, int vu_r, bool comet) {
    static int peak_l = 0;
    static int peak_r = 0;
    static int decay_l = 0;
    static int decay_r = 0;
    if (!led_vu) return;

    uint8_t bv = (comet) ? strip.bright * 2 : strip.bright;

    // scale vu samples to length
    vu_l  = vu_l * strip.vu_length / LED_VU_SCALE;
    vu_r = vu_r * strip.vu_length / LED_VU_SCALE;

    // calculate hold peaks
    if (peak_l > vu_l) {
        if (decay_l-- < 0) {
            decay_l = LED_VU_PEAK_HOLD;
            peak_l--;
        }
    } else {
        peak_l = vu_l;
        decay_l = LED_VU_PEAK_HOLD;
    }
    if (peak_r > vu_r) {
        if (decay_r-- < 0) {
            decay_r = LED_VU_PEAK_HOLD;
            peak_r--;
        }
    } else {
        peak_r = vu_r;
        decay_r = LED_VU_PEAK_HOLD;
    }

    // turn off all leds
    led_strip_clear(led_vu);

    // set the led bar values
    uint8_t step = bv / (strip.vu_length-1);     
    uint8_t g = bv * 2 / 3; // more red at top
    uint8_t r = 0;
    int shift = 0;
    for (int i = 0; i < strip.vu_length; i++) {
        // set left
        if (i == peak_l) {
            led_strip_set_pixel_rgb(led_vu, strip.vu_start_l - i, r, g, bv);
        } else if (i <= vu_l) {
            shift = vu_l - i; 
            if (comet)
                led_strip_set_pixel_rgb(led_vu, strip.vu_start_l - i, r>>shift, g>>shift, 0);
            else
                led_strip_set_pixel_rgb(led_vu, strip.vu_start_l - i, r, g, 0);
        }
        // set right  
        if (i == peak_r) {
            led_strip_set_pixel_rgb(led_vu, strip.vu_start_r + i, r, g, bv);
        }  else if (i <= vu_r) {
            shift = vu_r - i;
            if (comet)
                led_strip_set_pixel_rgb(led_vu, strip.vu_start_r + i, r>>shift, g>>shift, 0);
            else
                led_strip_set_pixel_rgb(led_vu, strip.vu_start_r + i, r, g, 0);
        }
        // adjust colors (with limit checks)
        r = (r > bv-step) ? bv : r + step;
        g = (g < step) ? 0 : g - step;
    }

    led_strip_show(led_vu);
}

