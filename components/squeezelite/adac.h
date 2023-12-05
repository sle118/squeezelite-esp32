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

#include "freertos/FreeRTOS.h"
#include "driver/i2s.h"
#include "driver/i2c.h"
#include "Configurator.h"
typedef enum { ADAC_ON = 0, ADAC_STANDBY, ADAC_OFF } adac_power_e;

struct adac_s {
	sys_DACModelEnum model;
	bool (*init)(char *config, int i2c_port_num, i2s_config_t *i2s_config, bool *mck);
	void (*deinit)(void);
	void (*power)(adac_power_e mode);
	void (*speaker)(bool active);
	void (*headset)(bool active);
	bool (*volume)(unsigned left, unsigned right);
};

extern const struct adac_s dac_tas57xx;
extern const struct adac_s dac_tas5713;
extern const struct adac_s dac_ac101;
extern const struct adac_s dac_wm8978;
extern const struct adac_s dac_external;

int 		adac_init(char *config, int i2c_port);
void		adac_deinit(void);
esp_err_t 	adac_write(int i2c_addr, uint8_t reg, uint8_t *data, size_t count);
esp_err_t 	adac_write_byte(int i2c_addr, uint8_t reg, uint8_t val);
esp_err_t 	adac_write_word(int i2c_addr, uint8_t reg, uint16_t val);
uint8_t 	adac_read_byte(int i2c_addr, uint8_t reg);
uint16_t 	adac_read_word(int i2c_addr, uint8_t reg);
