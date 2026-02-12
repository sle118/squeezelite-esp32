/* 
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#pragma once

#include "infrared.h"

// button type (pressed = LOW or HIGH, matches GPIO level)
#define BUTTON_LOW 0
#define BUTTON_HIGH 1

typedef enum { BUTTON_PRESSED, BUTTON_RELEASED } button_event_e;
typedef enum { BUTTON_NORMAL, BUTTON_SHIFTED } button_press_e;
typedef void (*button_handler)(void* id, button_event_e event, button_press_e mode, bool long_press);

/* 
set debounce to 0 for default (50ms)
set long_press to 0 for no long-press
set shifter_gpio to -1 for no shift
NOTE: shifter buttons *must* be created before shiftee
*/

void button_create(void* client, int gpio, int type, bool pull, int debounce, button_handler handler, int long_press, int shifter_gpio);
void* button_remap(void* client, int gpio, button_handler handler, int long_press, int shifter_gpio);
void* button_get_client(int gpio);
bool button_is_pressed(int gpio, void* client);

typedef enum { ROTARY_LEFT, ROTARY_RIGHT, ROTARY_PRESSED, ROTARY_RELEASED } rotary_event_e;
typedef void (*rotary_handler)(void* id, rotary_event_e event, bool long_press);

bool create_rotary(void* id, int A, int B, int SW, int long_press, rotary_handler handler);

bool create_infrared(int gpio, infrared_handler handler, infrared_mode_t mode);
