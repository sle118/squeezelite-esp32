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

#define LED_VU_SCALE    100

#define led_vu_color_red()    led_vu_color_all(255, 0, 0)
#define led_vu_color_green()    led_vu_color_all(0, 255, 0)
#define led_vu_color_blue()    led_vu_color_all(0, 0, 255)
#define led_vu_color_yellow()    led_vu_color_all(128, 128, 0)

static struct led_strip_t* led_vu;

void led_vu_progress_bar(int pct);
void led_vu_display(int vu_l, int vu_r, bool comet);
void led_vu_spin_dial(int rate, int gain, bool comet);
void led_vu_spectrum(uint8_t* data, uint8_t offset, uint8_t length);
void led_vu_color_all(uint8_t r, uint8_t g, uint8_t b);
void led_vu_data(uint8_t* data, uint8_t offset, uint8_t length);
void led_vu_clear();

