/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "platform_config.h"
#include "accessors.h"
#include "globdefs.h"
#include "display.h"
#include "display.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "stdbool.h"
#include "driver/adc.h"

static const char *TAG = "services";
static const char *i2c_name="I2C";
static const char *spi_name="SPI";
static cJSON * gpio_list=NULL;
#define min(a,b) (((a) < (b)) ? (a) : (b))
#ifndef QUOTE
	#define QUOTE(name) #name
#endif
#ifndef STR
	#define STR(macro)  QUOTE(macro)
#endif

/****************************************************************************************
 * 
 */
esp_err_t config_i2c_set(const i2c_config_t * config, int port){
	int buffer_size=255;
	char * config_buffer=calloc(buffer_size,1);
	if(config_buffer)  {
		snprintf(config_buffer,buffer_size,"scl=%u sda=%u speed=%u port=%u",config->scl_io_num,config->sda_io_num,config->master.clk_speed,port);
		ESP_LOGI(TAG,"Updating i2c configuration to %s",config_buffer);
		config_set_value(NVS_TYPE_STR, "i2c_config", config_buffer);
		free(config_buffer);
	}
	return ESP_OK;
}
/****************************************************************************************
 *
 */
esp_err_t config_spi_set(const spi_bus_config_t * config, int host, int dc){
	int buffer_size=255;
	char * config_buffer=calloc(buffer_size,1);
	if(config_buffer)  {
		snprintf(config_buffer,buffer_size,"data=%u,clk=%u,dc=%u,host=%u",config->mosi_io_num,config->sclk_io_num,dc,host);
		ESP_LOGI(TAG,"Updating SPI configuration to %s",config_buffer);
		config_set_value(NVS_TYPE_STR, "spi_config", config_buffer);
		free(config_buffer);
	}
	return ESP_OK;
}

/****************************************************************************************
 * 
 */
const display_config_t * config_display_get(){
	static display_config_t dstruct;
	char *config = config_alloc_get(NVS_TYPE_STR, "display_config");
	if (!config) {
		return NULL;
	}

	char * p=NULL;

	if ((p = strcasestr(config, "driver")) != NULL){
		dstruct.drivername = display_conf_get_driver_name(strchr(p, '=') + 1);
	}

	dstruct.drivername=dstruct.drivername?dstruct.drivername:"SSD1306";
	if ((p = strcasestr(config, "width")) != NULL) dstruct.width = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "height")) != NULL) dstruct.height = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "reset")) != NULL) dstruct.RST_pin = atoi(strchr(p, '=') + 1);
	dstruct.i2c_system_port=i2c_system_port;
	if (strstr(config, "I2C") ) dstruct.type=i2c_name;
	if ((p = strcasestr(config, "address")) != NULL) dstruct.address = atoi(strchr(p, '=') + 1);
	if (strstr(config, "SPI") ) dstruct.type=spi_name;
	if ((p = strcasestr(config, "cs")) != NULL) dstruct.CS_pin = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "speed")) != NULL) dstruct.speed = atoi(strchr(p, '=') + 1);
	dstruct.hflip= strcasestr(config, "HFlip") ? true : false;
	dstruct.vflip= strcasestr(config, "VFlip") ? true : false;
	dstruct.rotate= strcasestr(config, "rotate") ? true : false;
	return &dstruct;
}

/****************************************************************************************
 * 
 */
const i2c_config_t * config_i2c_get(int * i2c_port) {
	char *nvs_item, *p;
	static i2c_config_t i2c = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = -1,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_io_num = -1,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 0,
	};

	i2c.master.clk_speed = i2c_system_speed;
	
	nvs_item = config_alloc_get(NVS_TYPE_STR, "i2c_config");
	if (nvs_item) {
		if ((p = strcasestr(nvs_item, "scl")) != NULL) i2c.scl_io_num = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(nvs_item, "sda")) != NULL) i2c.sda_io_num = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(nvs_item, "speed")) != NULL) i2c.master.clk_speed = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(nvs_item, "port")) != NULL) i2c_system_port = atoi(strchr(p, '=') + 1);
		free(nvs_item);
	}
	if(i2c_port) *i2c_port=i2c_system_port;
	return &i2c;
}

/****************************************************************************************
 * 
 */
const spi_bus_config_t * config_spi_get(spi_host_device_t * spi_host) {
	char *nvs_item, *p;
	static spi_bus_config_t spi = {
		.mosi_io_num = -1,
        .sclk_io_num = -1,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };

	nvs_item = config_alloc_get_str("spi_config", CONFIG_SPI_CONFIG, NULL);
	if (nvs_item) {
		if ((p = strcasestr(nvs_item, "data")) != NULL) spi.mosi_io_num = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(nvs_item, "clk")) != NULL) spi.sclk_io_num = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(nvs_item, "dc")) != NULL) spi_system_dc_gpio = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(nvs_item, "host")) != NULL) spi_system_host = atoi(strchr(p, '=') + 1);
		free(nvs_item);
	}
	if(spi_host) *spi_host = spi_system_host;
	return &spi;
}

/****************************************************************************************
 * 
 */
void parse_set_GPIO(void (*cb)(int gpio, char *value)) {
	char *nvs_item, *p, type[16];
	int gpio;
	
	if ((nvs_item = config_alloc_get(NVS_TYPE_STR, "set_GPIO")) == NULL) return;
	
	p = nvs_item;
	
	do {
		if (sscanf(p, "%d=%15[^,]", &gpio, type) > 0) cb(gpio, type);
		p = strchr(p, ',');
	} while (p++);
	
	free(nvs_item);
}	

/****************************************************************************************
 *
 */
cJSON * get_gpio_entry(const char * name, const char * prefix, int gpio, bool fixed){
	cJSON * entry = cJSON_CreateObject();
	cJSON_AddNumberToObject(entry,"gpio",gpio);
	cJSON_AddStringToObject(entry,"name",name);
	cJSON_AddStringToObject(entry,"group",prefix);
	cJSON_AddBoolToObject(entry,"fixed",fixed);
	return entry;
}

/****************************************************************************************
 *
 */
cJSON * get_GPIO_list() {
	cJSON * list = cJSON_CreateArray();
	char *nvs_item, *p, type[16];
	int gpio;

	if ((nvs_item = config_alloc_get(NVS_TYPE_STR, "set_GPIO")) == NULL) return list;

	p = nvs_item;

	do {
		if (sscanf(p, "%d=%15[^,]", &gpio, type) > 0 && (GPIO_IS_VALID_GPIO(gpio) ||  gpio==GPIO_NUM_NC)){
			cJSON_AddItemToArray(list,get_gpio_entry(type,"gpio", gpio, false));
		}
		p = strchr(p, ',');
	} while (p++);

	free(nvs_item);

	return list;
}

/****************************************************************************************
 *
 */
cJSON * get_GPIO_from_string(const char * nvs_item, const char * prefix, cJSON * list, bool fixed){
	cJSON * llist = list;
	int gpio=0,offset=0,soffset=0,ret1=0,sret=0;

	if(!llist){
		llist = cJSON_CreateArray();
	}
	const char  *p=NULL;
	char type[16];
	int slen=strlen(nvs_item)+1;
	char * buf1=malloc(slen);
	char * buf2=malloc(slen);
	ESP_LOGD(TAG,"Parsing string %s",nvs_item);
	p = strchr(nvs_item, ':');
	p=p?p+1:nvs_item;
	while((((ret1=sscanf(p, "%[^=]=%d%n", type,&gpio,&offset)) ==2) || ((sret=sscanf(p, "%[^=]=%[^, ],%n", buf1,buf2,&soffset)) > 0 )) && (offset || soffset)){
		if(ret1==2 && (GPIO_IS_VALID_GPIO(gpio) ||  gpio==GPIO_NUM_NC)){
			cJSON_AddItemToArray(list,get_gpio_entry(type,prefix,gpio,fixed));
			p+=offset;
		} else {
			p+=soffset;
		}

		while(*p==' ' || *p==',') p++;
		gpio=-1;
	}
	free(buf1);
	free(buf2);
	return llist;

}

/****************************************************************************************
 *
 */
cJSON * get_GPIO_from_nvs(const char * item, const char * prefix, cJSON * list, bool fixed){
	char * nvs_item=NULL;
	cJSON * llist=list;
	if ((nvs_item = config_alloc_get(NVS_TYPE_STR, item)) == NULL) return list;
	llist = get_GPIO_from_string(nvs_item,prefix,list, fixed);
	free(nvs_item);
	return llist;
}

/****************************************************************************************
 *
 */
esp_err_t get_gpio_structure(cJSON * gpio_entry, gpio_entry_t ** gpio){
	esp_err_t err = ESP_OK;
	*gpio = malloc(sizeof(gpio_entry_t));
	//gpio,name,fixed
	cJSON * val = cJSON_GetObjectItem(gpio_entry,"gpio");
	if(val){
		(*gpio)->gpio= (int)val->valuedouble;
	}
	else {
		ESP_LOGE(TAG,"gpio pin not found");
		err=ESP_FAIL;
	}
	val = cJSON_GetObjectItem(gpio_entry,"name");
	if(val){
		(*gpio)->name= strdup(cJSON_GetStringValue(val));
	}
	else {
		ESP_LOGE(TAG,"gpio name value not found");
		err=ESP_FAIL;
	}
	val = cJSON_GetObjectItem(gpio_entry,"group");
	if(val){
		(*gpio)->group= strdup(cJSON_GetStringValue(val));
	}
	else {
		ESP_LOGE(TAG,"gpio group value not found");
		err=ESP_FAIL;
	}
	val = cJSON_GetObjectItem(gpio_entry,"fixed");
	if(val){
		(*gpio)->fixed= cJSON_IsTrue(val);
	}
	else {
		ESP_LOGE(TAG,"gpio fixed indicator not found");
		err=ESP_FAIL;
	}

	return err;
}

/****************************************************************************************
 *
 */
esp_err_t free_gpio_entry( gpio_entry_t ** gpio) {
	if(* gpio){
		free((* gpio)->name);
		free((* gpio)->group);
		free(* gpio);
		* gpio=NULL;
		return ESP_OK;
	}
	return ESP_FAIL;
}

/****************************************************************************************
 *
 */
gpio_entry_t * get_gpio_by_no(int gpionum, bool refresh){
	cJSON * gpio_header=NULL;
	gpio_entry_t * gpio=NULL;
	if(refresh){
			get_gpio_list();
	}
	cJSON_ArrayForEach(gpio_header,gpio_list)
	{
		if(get_gpio_structure(gpio_header, &gpio)==ESP_OK && gpio->gpio==gpionum){
			ESP_LOGD(TAG,"Found GPIO: %s=%d %s", gpio->name,gpio->gpio,gpio->fixed?"(FIXED)":"(VARIABLE)");
		}
	}
	return gpio;
}

/****************************************************************************************
 *
 */
gpio_entry_t * get_gpio_by_name(char * name,char * group, bool refresh){
	cJSON * gpio_header=NULL;
	if(refresh){
		get_gpio_list();
	}
	gpio_entry_t * gpio=NULL;
	cJSON_ArrayForEach(gpio_header,gpio_list)
	{
		if(get_gpio_structure(gpio_header, &gpio)==ESP_OK && strcasecmp(gpio->name,name)&& strcasecmp(gpio->group,group)){
			ESP_LOGD(TAG,"Found GPIO: %s=%d %s", gpio->name,gpio->gpio,gpio->fixed?"(FIXED)":"(VARIABLE)");
		}
	}
	return gpio;
}

/****************************************************************************************
 *
 */
cJSON * get_gpio_list() {
	gpio_num_t gpio_num;
	if(gpio_list){
		cJSON_free(gpio_list);
	}
	gpio_list = get_GPIO_list();

#ifndef CONFIG_BAT_LOCKED
	char *bat_config = config_alloc_get_default(NVS_TYPE_STR, "bat_config", NULL, 0);
	if (bat_config) {
		char *p;
		int channel;
		if ((p = strcasestr(bat_config, "channel") ) != NULL)
		{
			channel = atoi(strchr(p, '=') + 1);
			if(channel != -1){
				if(adc1_pad_get_io_num(channel,&gpio_num )==ESP_OK){
					cJSON_AddItemToArray(gpio_list,get_gpio_entry("bat","",gpio_num,false));
				}
			}
		}
		free(bat_config);
	}
#else
		if(adc1_pad_get_io_num(CONFIG_BAT_CHANNEL,&gpio_num )==ESP_OK){
			cJSON_AddItemToArray(gpio_list,get_gpio_entry("bat","",gpio_num,true));
		}
#endif
	gpio_list = get_GPIO_from_nvs("i2c_config","i2c", gpio_list, false);
	gpio_list = get_GPIO_from_nvs("spi_config","spi", gpio_list, false);

	char *spdif_config = config_alloc_get_str("spdif_config", CONFIG_SPDIF_CONFIG, "bck=" STR(CONFIG_SPDIF_BCK_IO)
											  ",ws=" STR(CONFIG_SPDIF_WS_IO) ",do=" STR(CONFIG_SPDIF_DO_IO));

	gpio_list=get_GPIO_from_string(spdif_config,"spdif", gpio_list, (strlen(CONFIG_SPDIF_CONFIG)>0 || CONFIG_SPDIF_DO_IO>0 ));
	char *dac_config = config_alloc_get_str("dac_config", CONFIG_DAC_CONFIG, "model=i2s,bck=" STR(CONFIG_I2S_BCK_IO)
											",ws=" STR(CONFIG_I2S_WS_IO) ",do=" STR(CONFIG_I2S_DO_IO)
											",sda=" STR(CONFIG_I2C_SDA) ",scl=" STR(CONFIG_I2C_SCL)
											",mute=" STR(CONFIG_MUTE_GPIO));

	gpio_list=get_GPIO_from_string(dac_config,"dac", gpio_list, (strlen(CONFIG_DAC_CONFIG)>0 || CONFIG_I2S_DO_IO>0 ));
	free(spdif_config);
	free(dac_config);
	return gpio_list;
}
