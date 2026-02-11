/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "Config.h"
#include "accessors.h"
#include "globdefs.h"
#include "display.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "stdbool.h"
#include "driver/adc.h"
#include "esp_attr.h"
#include "soc/spi_periph.h"
#include "esp_err.h"
#include "soc/rtc.h"
#include "sdkconfig.h"
#include "soc/efuse_periph.h"
#include "driver/gpio.h"
#if CONFIG_IDF_TARGET_ESP32   
#include "esp32/rom/efuse.h"
#endif
#include "tools.h"
#include "monitor.h"
#include "messaging.h"
#include "network_ethernet.h"

static const char *TAG = "services";
const char *i2c_name_type="I2C";
const char *spi_name_type="SPI";
cJSON * gpio_list=NULL;
#define min(a,b) (((a) < (b)) ? (a) : (b))
#ifndef QUOTE
	#define QUOTE(name) #name
#endif
#ifndef STR
	#define STR(macro)  QUOTE(macro)
#endif

extern cJSON * get_gpio_list(bool refresh);
bool are_statistics_enabled(){
#if defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) &&  defined (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
	return true;
#endif
	return false;
}


/****************************************************************************************
 * 
 */
// static char * config_spdif_get_string(){
// 	// return config_alloc_get_str("spdif_config", CONFIG_SPDIF_CONFIG, "bck=" STR(CONFIG_SPDIF_BCK_IO)
// 	// 										  ",ws=" STR(CONFIG_SPDIF_WS_IO) ",do=" STR(CONFIG_SPDIF_DO_IO));
// 	config_alloc_get_str("spdif_config", CONFIG_SPDIF_CONFIG, "bck=" STR(CONFIG_SPDIF_BCK_IO)
// 											  ",ws=" STR(CONFIG_SPDIF_WS_IO) ",do=" STR(CONFIG_SPDIF_DO_IO));
// }

/****************************************************************************************
 * 
 */
// static char * get_dac_config_string(){
// 	return config_alloc_get_str("dac_config", CONFIG_DAC_CONFIG, "model=i2s,bck=" STR(CONFIG_I2S_BCK_IO)
// 											",ws=" STR(CONFIG_I2S_WS_IO) ",do=" STR(CONFIG_I2S_DO_IO)
// 											",sda=" STR(CONFIG_I2C_SDA) ",scl=" STR(CONFIG_I2C_SCL)
// 											",mute=" STR(CONFIG_MUTE_GPIO));
// }

/****************************************************************************************
 * 
 */
bool is_dac_config_locked(){
#if ( defined CONFIG_DAC_CONFIG )
	if(strlen(CONFIG_DAC_CONFIG) > 0){
		return true;
	}
#endif
#if defined(CONFIG_I2S_BCK_IO) && CONFIG_I2S_BCK_IO>0		
	return true;
#endif
	return false;
}


const sys_i2c_bus* get_i2c_bus(i2c_port_t port){
    if(platform->dev.has_i2c && port == platform->dev.i2c.port-sys_i2c_port_PORT0 && platform->dev.i2c.scl>=0){
        return &platform->dev.i2c;
    }
    if(platform->dev.has_dac && platform->dev.dac.has_i2c && (platform->dev.dac.i2c.port - sys_i2c_port_PORT0) == port && platform->dev.dac.i2c.scl>=0){
        return &platform->dev.dac.i2c;
    }
    return NULL;
}
/****************************************************************************************
 * 
 */
const i2c_config_t * config_i2c_get(sys_i2c_bus * bus) {
	 
	static i2c_config_t i2c = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = -1,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_io_num = -1,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 0,
	};

	if(bus && bus->scl >=0  && bus->sda >=0 ){
		if(bus->scl>=0){
			i2c.scl_io_num = bus->scl;
		}
		else {
			ESP_LOGE(TAG,"%s missing for i2c","SCL");
		}
		
		if(bus->sda>=0){
			i2c.sda_io_num= bus->sda;
		}
		else {
			ESP_LOGE(TAG,"%s missing for i2c","SDA");
		}
		if(bus->speed>0){
			i2c.master.clk_speed = bus->speed;
		}		
	}

	return &i2c;
}


void config_set_gpio(int * pin, sys_gpio_config * gpio,bool has_value, const char * name, bool mandatory){
	if(has_value){
		ESP_LOGD(TAG, "Setting pin %d as %s", gpio->pin, name);
		if(pin) *pin= gpio->pin;
	}
	else if(mandatory) {
		ESP_LOGE(TAG,"Pin %s has no value",name);
	}
	else {
		ESP_LOGD(TAG,"Pin %s has no value",name);
	}
}

/****************************************************************************************
 * 
 */
const spi_bus_config_t * config_spi_get(spi_host_device_t * spi_host) {
	// don't memset all to 0xff as it's more than just GPIO
	static spi_bus_config_t spi = {
		.mosi_io_num = -1,
        .sclk_io_num = -1,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };
	if(platform->has_dev && platform->dev.has_spi){
		ESP_LOGD(TAG,"SPI Configuration found");
		spi.mosi_io_num = platform->dev.spi.mosi;
		spi.miso_io_num = platform->dev.spi.miso;
		spi.sclk_io_num = platform->dev.spi.clk;
		spi_system_dc_gpio = platform->dev.spi.dc;
		// only VSPI (1) can be used as Flash and PSRAM run at 80MHz
		if(platform->dev.spi.host!=sys_dev_common_hosts_NONE){
			spi_system_host = platform->dev.spi.host-sys_dev_common_hosts_Host0;
		}
	}
	else {
		ESP_LOGI(TAG,"SPI not configured");
	}
	
	if(spi_host) *spi_host = spi_system_host;	
	return &spi;
}
