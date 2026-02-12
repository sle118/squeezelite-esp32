/* 
 *  Squeezelite for esp32
 *
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
#include "Config.h"
#include "driver/gpio.h"

#pragma once

typedef enum { SLEEP_ONTIMER, SLEEP_ONKEY, SLEEP_ONGPIO, SLEEP_ONIR, SLEEP_ONBATTERY } sleep_cause_e;
void services_sleep_activate(sleep_cause_e cause);
void services_sleep_setsuspend(void (*hook)(void));
void services_sleep_setsleeper(uint32_t (*sleeper)(void));
void services_sleep_init(void);
void set_gpio_level(sys_gpio_config* gpio, const char* name, gpio_mode_t mode);
