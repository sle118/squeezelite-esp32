/* 
 *  Control of LED strip within squeezelite-esp32 
 *     
 *  (c) Wizmo 2021
 * 
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include <ctype.h>

#define LED_VU_MAX    255U
#define LED_VU_BRIGHT  20U

#define led_vu_color_red(B)    led_vu_color_all(B, 0, 0)
#define led_vu_color_green(B)    led_vu_color_all(0, B, 0)
#define led_vu_color_blue(B)    led_vu_color_all(0, 0, B)
#define led_vu_color_yellow(B)    led_vu_color_all(B/2, B/2, 0)

extern struct led_strip_t* led_display;

uint16_t led_vu_string_length();
void led_vu_progress_bar(int pct, int bright);
void led_vu_display(int vu_l, int vu_r, int bright, bool comet);
void led_vu_spin_dial(int gain, int rate, bool comet);
void led_vu_spectrum(uint8_t* data, int bright, int length, int style);
void led_vu_color_all(uint8_t r, uint8_t g, uint8_t b);
void led_vu_data(uint8_t* data, uint16_t offset, uint16_t length);
void led_vu_clear();

