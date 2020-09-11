/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "esp_system.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
typedef struct {
	int width;
	int height;
	int RST_pin;
	int i2c_system_port;
	int address;
	int CS_pin;
	int speed;
	const char *drivername;
	const char *type;
	bool hflip;
	bool vflip;
	bool rotate;
} display_config_t;
const display_config_t * config_display_get();
esp_err_t 					config_i2c_set(const i2c_config_t * config, int port);
esp_err_t 					config_spi_set(const spi_bus_config_t * config, int host, int dc);
const i2c_config_t * 		config_i2c_get(int * i2c_port);
const spi_bus_config_t * 	config_spi_get(spi_host_device_t * spi_host);
void 						parse_set_GPIO(void (*cb)(int gpio, char *value));

typedef struct {
	bool fixed;
	char * name;
	char * group;
	int gpio;
} gpio_entry_t;
esp_err_t 					free_gpio_entry( gpio_entry_t ** gpio);
gpio_entry_t * 				get_gpio_by_name(char * name,char * group, bool refresh);
gpio_entry_t * 				get_gpio_by_no(int gpionum, bool refresh);
cJSON * 					get_gpio_list();
