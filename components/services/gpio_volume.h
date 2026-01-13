#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	GPIO_VOLUME_MODE_LEDBAR,
	GPIO_VOLUME_MODE_BINARY,
	GPIO_VOLUME_MODE_LATCHING
} gpio_volume_mode_t;

typedef struct {
	int dacmaxvol;
	int lsb0;
	int lsb1;
	int high0;
	int high1;
	int width;
	int time_ms;
	bool loud;
	bool highON;
	bool lowON;
	gpio_volume_mode_t mode;
} gpio_volume_cfg_t;

bool gpio_volume_init(const char *cfg);
void gpio_volume_apply_startup_volume(unsigned gain);
void gpio_volume_update(unsigned gain);
int  gpio_volume_fixed(void);
