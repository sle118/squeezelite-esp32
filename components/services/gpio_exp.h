/* GDS Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"

struct gpio_exp_s;

typedef struct {
    char model[32];
    int intr;
    uint8_t count;
    uint32_t base;
    struct gpio_exp_phy_s {
        uint8_t addr;
        struct { // for I2C
            uint8_t port;
        };
        struct { // for SPI
            uint32_t speed;
            uint8_t host;
            uint8_t cs_pin;
        };
        int8_t ena_pin; // enable pin
        int8_t ena_lvl; // enable level
    } phy;
} gpio_exp_config_t;

// set <intr> to -1 and <queue> to NULL if there is no interrupt
struct gpio_exp_s* gpio_exp_create(const gpio_exp_config_t* config);
uint32_t gpio_exp_get_base(struct gpio_exp_s* expander);
struct gpio_exp_s* gpio_exp_get_expander(int gpio);
#define gpio_is_expanded(gpio) (gpio < GPIO_NUM_MAX)

/* 
 For all functions below when <expander> is provided, GPIO's can be numbered from 0. If <expander>
 is NULL, then GPIO must start from base OR be on-chip
*/
void esp_rom_gpio_pad_select_gpio_x(uint32_t iopad_num);
esp_err_t gpio_exp_set_direction(int gpio, gpio_mode_t mode, struct gpio_exp_s* expander);
esp_err_t gpio_exp_set_pull_mode(int gpio, gpio_pull_mode_t mode, struct gpio_exp_s* expander);
int gpio_exp_get_level(int gpio, int age, struct gpio_exp_s* expander);
esp_err_t gpio_exp_set_level(int gpio, int level, bool direct, struct gpio_exp_s* expander);
esp_err_t gpio_exp_isr_handler_add(int gpio, gpio_isr_t isr, uint32_t debounce, void* arg, struct gpio_exp_s* expander);
esp_err_t gpio_exp_isr_handler_remove(int gpio, struct gpio_exp_s* expander);

// unified function to use either built-in or expanded GPIO
esp_err_t gpio_set_direction_x(int gpio, gpio_mode_t mode);
esp_err_t gpio_set_pull_mode_x(int gpio, gpio_pull_mode_t mode);
int gpio_get_level_x(int gpio);
esp_err_t gpio_set_level_x(int gpio, int level);
esp_err_t gpio_isr_handler_add_x(int gpio, gpio_isr_t isr_handler, void* args);
esp_err_t gpio_isr_handler_remove_x(int gpio);
#define gpio_set_intr_type_x(gpio, type)                                                                                                             \
    do {                                                                                                                                             \
        if(gpio < GPIO_NUM_MAX) gpio_set_intr_type(gpio, type);                                                                                      \
    } while(0)
#define gpio_intr_enable_x(gpio)                                                                                                                     \
    do {                                                                                                                                             \
        if(gpio < GPIO_NUM_MAX) gpio_intr_enable(gpio);                                                                                              \
    } while(0)
#define gpio_pad_select_gpio_x(gpio)                                                                                                                 \
    do {                                                                                                                                             \
        if(gpio < GPIO_NUM_MAX) esp_rom_gpio_pad_select_gpio(gpio);                                                                                  \
    } while(0)
