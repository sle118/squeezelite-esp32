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
 
#include <string.h> 
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "adac.h"

#define PARSE_PARAM(S,P,C,V) do {									\
	char *__p;														\
	if ((__p = strcasestr(S, P)) && (__p = strchr(__p, C))) V = atoi(__p+1); \
} while (0)

static const char TAG[] = "DAC core";
i2c_port_t i2c_port = -1;
/****************************************************************************************
 * init
 */
int adac_init(sys_dac_config *config) {	 
	int i2c_addr = 0;

	// configure i2c
	i2c_config_t i2c_config = {
			.mode = I2C_MODE_MASTER,
			.sda_io_num = -1,
			.sda_pullup_en = GPIO_PULLUP_ENABLE,
			.scl_io_num = -1,
			.scl_pullup_en = GPIO_PULLUP_ENABLE,
			.master.clk_speed = 250000,
		};
	i2c_addr = config->addr;
	if(config->has_i2c){
		i2c_config.sda_io_num = config->i2c.sda;
		i2c_config.scl_io_num = config->i2c.scl;
		i2c_config.master.clk_speed =  config->i2c.speed>0?config->i2c.speed:i2c_config.master.clk_speed;
	}

	if (i2c_config.sda_io_num == -1 || i2c_config.scl_io_num == -1) {
		ESP_LOGW(TAG, "DAC does not use i2c");
		return i2c_addr;
	}	
	i2c_port = config->i2c.port-sys_i2c_port_PORT0;
	ESP_LOGI(TAG, "DAC uses I2C port:%s, sda:%d, scl:%d", sys_i2c_port_name(config->i2c.port), i2c_config.sda_io_num, i2c_config.scl_io_num);
	
	// we have an I2C configured	
	i2c_param_config(i2c_port, &i2c_config);
	i2c_driver_install(i2c_port, I2C_MODE_MASTER, false, false, false);
	
	return i2c_addr;
}	

/****************************************************************************************
 * close
 */
void adac_deinit(void) {
	if (i2c_port != -1) i2c_driver_delete(i2c_port);
}	

/****************************************************************************************
 * 
 */
esp_err_t adac_write_byte(int i2c_addr,uint8_t reg, uint8_t val) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
	
	i2c_master_write_byte(cmd, (i2c_addr << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
	i2c_master_write_byte(cmd, reg, I2C_MASTER_NACK);
	i2c_master_write_byte(cmd, val, I2C_MASTER_NACK);
	
	i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 100 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
	
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "I2C write failed");
	}
	
    return ret;
}

/****************************************************************************************
 * 
 */
uint8_t adac_read_byte(int i2c_addr, uint8_t reg) {
	uint8_t data = 255;
	
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    
	i2c_master_write_byte(cmd, (i2c_addr << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
	i2c_master_write_byte(cmd, reg, I2C_MASTER_NACK);

	i2c_master_start(cmd);			
	i2c_master_write_byte(cmd, (i2c_addr << 1) | I2C_MASTER_READ, I2C_MASTER_NACK);
	i2c_master_read_byte(cmd, &data, I2C_MASTER_NACK);
	
    i2c_master_stop(cmd);
	esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 100 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "I2C read failed");
	}
	
	return data;
}

/****************************************************************************************
 * 
 */
uint16_t adac_read_word(int i2c_addr, uint8_t reg) {
	uint8_t data[2] = { 255, 255 };
	
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
	
    i2c_master_write_byte(cmd, (i2c_addr << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
    i2c_master_write_byte(cmd, reg, I2C_MASTER_NACK);
	
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2c_addr << 1) | I2C_MASTER_READ, I2C_MASTER_NACK);
    i2c_master_read(cmd, data, 2, I2C_MASTER_NACK);
	
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 100 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
	
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "I2C read failed");
	}

	return (data[0] << 8) | data[1];
}

/****************************************************************************************
 * 
 */
esp_err_t adac_write_word(int i2c_addr, uint8_t reg, uint16_t val) {
	uint8_t data[] = { i2c_addr << 1, reg,
	                   val >> 8, val & 0xff };
					   
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	
    i2c_master_write(cmd, data, 4, I2C_MASTER_NACK);
    
	i2c_master_stop(cmd);
	esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 100 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
	
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "I2C write failed");
	}
	
    return ret;
}

/****************************************************************************************
 * 
 */
esp_err_t adac_write(int i2c_addr, uint8_t reg, uint8_t *data, size_t count) {
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
	
	i2c_master_write_byte(cmd, (i2c_addr << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
	i2c_master_write_byte(cmd, reg, I2C_MASTER_NACK);
	i2c_master_write(cmd, data, count, I2C_MASTER_NACK);
	
	i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 200 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
	
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "I2C write failed");
	}
	
	return ret;
}	