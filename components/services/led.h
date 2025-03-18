/*
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#ifndef LED_H
#define LED_H
#include "driver/gpio.h"
#include "Config.h"

enum { LED_GREEN = 0, LED_RED };
#define led_on(idx)						led_blink_core(idx, 1, 0, false)
#define led_off(idx)					led_blink_core(idx, 0, 0, false)
#define led_blink(idx, on, off)			led_blink_core(idx, on, off, false)
#define led_blink_pushed(idx, on, off)	led_blink_core(idx, on, off, true)

// if type is LED_GPIO then color set the GPIO logic value for "on"
bool led_config(int idx, sys_led_config * led_config);
bool led_brightness(int idx, int percent);
bool led_blink_core(int idx, int ontime, int offtime, bool push);
bool led_unpush(int idx);
int  led_allocate(void);

#endif
