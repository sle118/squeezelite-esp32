/* 
 *  Squeezelite for esp32
 *
 *  (c) Wizmo 2021
 * 		Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "adac.h"

static const char TAG[] = "WM8978";

static void speaker(bool active) { }
static void headset(bool active) { }
static bool volume(unsigned left, unsigned right) { return false; }
static void power(adac_power_e mode);
static bool init(char *config, int i2c_port_num, i2s_config_t *i2s_config, bool *mck);

static esp_err_t i2c_write_shadow(uint8_t reg, uint16_t val);
static uint16_t i2c_read_shadow(uint8_t reg);

static int WM8978;

const struct adac_s dac_wm8978 = { sys_DACModelEnum_WM8978, init, adac_deinit, power, speaker, headset, volume };

// initiation table for non-readbale 9-bit i2c registers
static uint16_t WM8978_REGVAL_TBL[58] =	{
		0X0000, 0X0000, 0X0000, 0X0000, 0X0050, 0X0000, 0X0140, 0X0000,
		0X0000, 0X0000, 0X0000, 0X00FF, 0X00FF, 0X0000, 0X0100, 0X00FF,
		0X00FF, 0X0000, 0X012C, 0X002C, 0X002C, 0X002C, 0X002C, 0X0000,
		0X0032, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000, 0X0000,
		0X0038, 0X000B, 0X0032, 0X0000, 0X0008, 0X000C, 0X0093, 0X00E9,
		0X0000, 0X0000, 0X0000, 0X0000, 0X0003, 0X0010, 0X0010, 0X0100,
		0X0100, 0X0002, 0X0001, 0X0001, 0X0039, 0X0039, 0X0039, 0X0039,
		0X0001, 0X0001
};

/****************************************************************************************
 * init
 */
static bool init(char *config, int i2c_port, i2s_config_t *i2s_config, bool *mck) {	 
	WM8978 = adac_init(config, i2c_port);
	
	if (!WM8978) WM8978 = 0x1a;
	ESP_LOGI(TAG, "WM8978 detected @%d", WM8978);
    
    *mck = true;
    
	// init sequence
	i2c_write_shadow(0, 0);
	i2c_write_shadow(4, 16);
	i2c_write_shadow(6, 0);
	i2c_write_shadow(10, 8);
	i2c_write_shadow(43, 16);
	i2c_write_shadow(49, 102);
	
	return true;
}	

/****************************************************************************************
 * power
 */
static void power(adac_power_e mode) {
	uint16_t *data, off[] = {0, 0, 0}, on[] = {11, 384, 111};
	data = (mode == ADAC_STANDBY || mode == ADAC_OFF) ? off : on;
	i2c_write_shadow(1, data[0]);
	i2c_write_shadow(2, data[1]);
	i2c_write_shadow(3, data[2]);
}

/****************************************************************************************
 *  Write with custom reg/value structure
 */
 static esp_err_t i2c_write_shadow(uint8_t reg, uint16_t val) {
	WM8978_REGVAL_TBL[reg] = val;
	reg = (reg << 1) | ((val >> 8) & 0x01);
    val &= 0xff;  
	return adac_write_byte(WM8978, reg, val);
}

/****************************************************************************************
 *  Return local register value
 */
static uint16_t i2c_read_shadow(uint8_t reg) {
	return WM8978_REGVAL_TBL[reg];
}
