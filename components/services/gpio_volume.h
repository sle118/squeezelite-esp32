#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	GPIO_VOLUME_MODE_LEDBAR,
	GPIO_VOLUME_MODE_BINARY,
	GPIO_VOLUME_MODE_LATCHING
} gpio_volume_mode_t;

typedef struct {
	int dacmax;
	int visumax;
	int lsb0;
	int lsb0_level;
	int lsb1;
	int lsb1_level;
	int high0;
	int high0_level;
	int high1;
	int high1_level;
	int width;
	int time_ms;
	bool loud;
	gpio_volume_mode_t mode;
} gpio_volume_cfg_t;

typedef struct {
	bool active; 		// true = gpio_volume is configured
	bool dac_fixed;		// true = bypass digital gain, use external volume control
	bool visu_fixed;	// true = visualization at max volume, false = follows volume setting
} gpio_max_mode_t;

bool gpio_volume_init(const char *cfg);
void gpio_volume_apply_startup_volume(unsigned gain);
void gpio_volume_update(unsigned gain);
int  gpio_volume_fixed(void);
gpio_max_mode_t gpio_volume_get_mode(void);