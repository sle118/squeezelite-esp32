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
extern const char *i2c_name_type;
extern const char *spi_name_type;
typedef struct {
	int width;
	int height;
	int address;
	int RST_pin;
	bool hflip;
	bool vflip;
	const char *drivername;
	int CS_pin;
	int speed;
	int back;
	int depth;
	const char *type;
	bool rotate;
} display_config_t;

typedef struct {
	i2s_pin_config_t pin;
	char model[32];
	int mute_gpio;
	int mute_level;
	int i2c_addr;
	int sda;
	int scl;
} i2s_platform_config_t;

typedef struct {
	int gpio;
	int level;
	bool fixed;
} gpio_with_level_t;

typedef struct {
	gpio_with_level_t vcc;
	gpio_with_level_t gnd;
	gpio_with_level_t amp;
	gpio_with_level_t ir;
	gpio_with_level_t jack;
	gpio_with_level_t green;
	gpio_with_level_t red;
	gpio_with_level_t spkfault;	
} set_GPIO_struct_t;

typedef struct {
	int A;
	int B;
	int SW;
	bool knobonly;
	bool volume_lock;
	bool longpress;
	int timer;
} rotary_struct_t;

typedef struct {
	char type[16];
	int length;
	int gpio;
} ledvu_struct_t;

typedef struct {
	bool fixed;
	char * name;
	char * group;
	int gpio;
} gpio_entry_t;

const display_config_t * 	config_display_get();
esp_err_t 					config_display_set(const display_config_t * config);
esp_err_t 					config_i2c_set(const i2c_config_t * config, int port);
esp_err_t 					config_i2s_set(const i2s_platform_config_t * config, const char * nvs_name);
esp_err_t 					config_spi_set(const spi_bus_config_t * config, int host, int dc);
const i2c_config_t * 		config_i2c_get(int * i2c_port);
const spi_bus_config_t * 	config_spi_get(spi_host_device_t * spi_host);
void 						parse_set_GPIO(void (*cb)(int gpio, char *value));
const i2s_platform_config_t * 	config_dac_get();
const i2s_platform_config_t * 	config_spdif_get( );
esp_err_t 					config_spdif_set(const i2s_platform_config_t * config);
bool 						is_spdif_config_locked();
esp_err_t 					free_gpio_entry( gpio_entry_t ** gpio);
gpio_entry_t * 				get_gpio_by_name(char * name,char * group, bool refresh);
gpio_entry_t * 				get_gpio_by_no(int gpionum, bool refresh);
cJSON * 					get_gpio_list(bool refresh);
bool 						is_dac_config_locked();
bool 						are_statistics_enabled();
const rotary_struct_t * 	config_rotary_get();
esp_err_t 					config_rotary_set(rotary_struct_t * rotary);
const ledvu_struct_t * 			config_ledvu_get();
esp_err_t 				config_ledvu_set(ledvu_struct_t * rotary);
