/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "esp_system.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "driver/spi_master.h"
#include "gpio_exp.h"
#include "I2CBus.pb.h"
#define ASSIGN_GPIO(pin, root, name, mandatory) config_set_gpio(&pin, &(root).name, (root).has_##name, #name, mandatory)

#define ASSIGN_COND_VAL_1LVL(name, targetval) ((targetval) = (platform && platform->has_##name ? &(platform->name) : NULL))
#define ASSIGN_COND_VAL_2LVL(name1, name2, targetval)                                                                                                \
    ((targetval) = (platform && platform->has_##name1 && platform->name1.has_##name2 ? &(platform->name1.name2) : NULL))
#define ASSIGN_COND_VAL_3LVL(name1, name2, name3, targetval)                                                                                         \
    ((targetval) =                                                                                                                                   \
            (platform && platform->has_##name1 && platform->name1.has_##name2 && platform->name1.name2.has_##name3 ? &(platform->name1.name2.name3)  \
                                                                                                                   : NULL))
#define ASSIGN_COND_VAL_4LVL(name1, name2, name3, name4, targetval)                                                                                  \
    ((targetval) = (platform && platform->has_##name1 && platform->name1.has_##name2 && platform->name1.name2.has_##name3 &&                         \
                            platform->name1.name2.name3.has_##name4                                                                                  \
                        ? &(platform->name1.name2.name3.name4)                                                                                       \
                        : NULL))
#define ASSIGN_COND_VAL_5LVL(name1, name2, name3, name4, name5, targetval)                                                                           \
    ((targetval) = (platform && platform->has_##name1 && platform->name1.has_##name2 && platform->name1.name2.has_##name3 &&                         \
                            platform->name1.name2.name3.has_##name4 && platform->name1.name2.name3.name4.has_##name5                                 \
                        ? &(platform->name1.name2.name3.name4.name5)                                                                                 \
                        : NULL))

#define SYS_NET(target) ASSIGN_COND_VAL_1LVL(net, target)
#define SYS_DISPLAY(target) ASSIGN_COND_VAL_2LVL(dev, display, target)
#define SYS_DEV_LEDSTRIP(target) ASSIGN_COND_VAL_2LVL(dev, led_strip, target)
#define SYS_DEV(target) ASSIGN_COND_VAL_1LVL(dev, target)
#define SYS_DEV_ROTARY(target) ASSIGN_COND_VAL_2LVL(dev, rotary, target)
#define SYS_DEV_IR(target) ASSIGN_COND_VAL_2LVL(dev, ir, target)
#define SYS_DISPLAY_COMMON(target) ASSIGN_COND_VAL_3LVL(dev, display, common, target)
#define SYS_DISPLAY_SPI(target) ASSIGN_COND_VAL_3LVL(dev, display, spi, target)
#define SYS_DISPLAY_I2C(target) ASSIGN_COND_VAL_3LVL(dev, display, i2c, target)
#define SYS_GPIOS(target) ASSIGN_COND_VAL_2LVL(dev, gpios, target)
#define SYS_ETH(target) ASSIGN_COND_VAL_2LVL(dev, eth, target)
#define SYS_ETH_COMMON(target) ASSIGN_COND_VAL_3LVL(dev, eth, common, target)

#define sys_i2c_bus(target) ASSIGN_COND_VAL_2LVL(dev, i2c, target)
#define SYS_GPIOS_NAME(name, target) ASSIGN_COND_VAL_2LVL(gpios, name, target)
#define sys_services_config(target) ASSIGN_COND_VAL_1LVL(services, target)
#define sys_services_config_SPOTIFY(target) ASSIGN_COND_VAL_2LVL(services, cspot, target)
#define sys_services_config_METADATA(target) ASSIGN_COND_VAL_2LVL(services, metadata, target)
#define sys_services_config_AIRPLAY(target) ASSIGN_COND_VAL_2LVL(services, airplay, target)
#define sys_services_config_SLEEP(target) ASSIGN_COND_VAL_2LVL(services, sleep, target)
#define sys_services_config_EQUALIZER(target) ASSIGN_COND_VAL_2LVL(services, equalizer, target)

#define sys_services_config_TELNET(target) ASSIGN_COND_VAL_2LVL(services, telnet, target)
#define sys_services_config_BTSINK(target) ASSIGN_COND_VAL_2LVL(services, bt_sink, target)

#define SYS_DEV_BATTERY(target) ASSIGN_COND_VAL_2LVL(dev, battery, target)

const i2c_config_t* config_i2c_get(sys_i2c_bus* bus);
const spi_bus_config_t* config_spi_get(spi_host_device_t* spi_host);
const gpio_exp_config_t* config_gpio_exp_get(int index);
const sys_i2c_bus* get_i2c_bus(i2c_port_t port);

bool is_dac_config_locked();
bool are_statistics_enabled();
