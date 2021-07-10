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
#include "esp_attr.h"
#include "soc/spi_periph.h"
#include "esp_err.h"
#include "soc/rtc.h"
#include "sdkconfig.h"
#include "soc/efuse_periph.h"
#include "driver/gpio.h"
#include "driver/spi_common_internal.h"
#include "esp32/rom/efuse.h"
#include "adac.h"
#include "trace.h"
#include "monitor.h"
#include "messaging.h"


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

bool are_statistics_enabled(){
#if defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) &&  defined (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
	return true;
#endif
	return false;
}


/****************************************************************************************
 * 
 */
static char * config_spdif_get_string(){
	return config_alloc_get_str("spdif_config", CONFIG_SPDIF_CONFIG, "bck=" STR(CONFIG_SPDIF_BCK_IO)
											  ",ws=" STR(CONFIG_SPDIF_WS_IO) ",do=" STR(CONFIG_SPDIF_DO_IO));
}

/****************************************************************************************
 * 
 */
static char * get_dac_config_string(){
	return config_alloc_get_str("dac_config", CONFIG_DAC_CONFIG, "model=i2s,bck=" STR(CONFIG_I2S_BCK_IO)
											",ws=" STR(CONFIG_I2S_WS_IO) ",do=" STR(CONFIG_I2S_DO_IO)
											",sda=" STR(CONFIG_I2C_SDA) ",scl=" STR(CONFIG_I2C_SCL)
											",mute=" STR(CONFIG_MUTE_GPIO));
}

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

/****************************************************************************************
 * 
 */
bool is_spdif_config_locked(){
#if ( defined CONFIG_SPDIF_CONFIG )
	if(strlen(CONFIG_SPDIF_CONFIG) > 0){
		return true;
	}
#endif
#if defined(CONFIG_SPDIF_BCK_IO) && CONFIG_SPDIF_BCK_IO>0		
	return true;
#endif
	return false;
}

/****************************************************************************************
 * Set pin from config string
 */
static void set_i2s_pin(char *config, i2s_pin_config_t *pin_config) {
	char *p;
	pin_config->bck_io_num = pin_config->ws_io_num = pin_config->data_out_num = pin_config->data_in_num = -1; 				
	if ((p = strcasestr(config, "bck")) != NULL) pin_config->bck_io_num = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "ws")) != NULL) pin_config->ws_io_num = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "do")) != NULL) pin_config->data_out_num = atoi(strchr(p, '=') + 1);
}

/****************************************************************************************
 * Get i2s config structure from config string
 */
const i2s_platform_config_t * config_get_i2s_from_str(char * dac_config ){
	static i2s_platform_config_t i2s_dac_pin = {
		.i2c_addr = -1,
		.sda= -1,
		.scl = -1,
		.mute_gpio = -1,
		.mute_level = -1
	};
	set_i2s_pin(dac_config, &i2s_dac_pin.pin);
	strcpy(i2s_dac_pin.model, "i2s");
	char * p=NULL;

	if ((p = strcasestr(dac_config, "i2c")) != NULL) i2s_dac_pin.i2c_addr = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(dac_config, "sda")) != NULL) i2s_dac_pin.sda = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(dac_config, "scl")) != NULL) i2s_dac_pin.scl = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(dac_config, "model")) != NULL) sscanf(p, "%*[^=]=%31[^,]", i2s_dac_pin.model);
	if ((p = strcasestr(dac_config, "mute")) != NULL) {
		char mute[8] = "";
		sscanf(p, "%*[^=]=%7[^,]", mute);
		i2s_dac_pin.mute_gpio = atoi(mute);
		if ((p = strchr(mute, ':')) != NULL) i2s_dac_pin.mute_level = atoi(p + 1);
	}	
	return &i2s_dac_pin;
}

/****************************************************************************************
 * Get spdif config structure 
 */
const i2s_platform_config_t * config_spdif_get( ){
	char * spdif_config = config_spdif_get_string();
	static i2s_platform_config_t i2s_dac_config;
	memcpy(&i2s_dac_config, config_get_i2s_from_str(spdif_config), sizeof(i2s_dac_config));
	free(spdif_config);
	return &i2s_dac_config;
}

/****************************************************************************************
 * Get dac config structure 
 */
const i2s_platform_config_t * config_dac_get(){
	char * spdif_config = get_dac_config_string();
	static i2s_platform_config_t i2s_dac_config;
	memcpy(&i2s_dac_config, config_get_i2s_from_str(spdif_config), sizeof(i2s_dac_config));
	free(spdif_config);
	return &i2s_dac_config;
}

/****************************************************************************************
 * 
 */
esp_err_t config_i2c_set(const i2c_config_t * config, int port){
	int buffer_size=255;
	esp_err_t err=ESP_OK;
	char * config_buffer=calloc(buffer_size,1);
	if(config_buffer)  {
		snprintf(config_buffer,buffer_size,"scl=%u,sda=%u,speed=%u,port=%u",config->scl_io_num,config->sda_io_num,config->master.clk_speed,port);
		log_send_messaging(MESSAGING_INFO,"Updating I2C configuration to %s",config_buffer);
		err = config_set_value(NVS_TYPE_STR, "i2c_config", config_buffer);
		if(err!=ESP_OK){
			log_send_messaging(MESSAGING_ERROR,"Error: %s",esp_err_to_name(err));
		}		
		free(config_buffer);
	}
	return err;
}

/****************************************************************************************
 * 
 */
esp_err_t config_rotary_set(rotary_struct_t * config){
	int buffer_size=512;
	esp_err_t err=ESP_OK;
	char * config_buffer=calloc(buffer_size,1);
	char * config_buffer2=calloc(buffer_size,1);	
	if(config_buffer && config_buffer2)  {
		snprintf(config_buffer,buffer_size,"A=%i,B=%i",config->A, config->B);
		if(config->SW >=0 ){
			snprintf(config_buffer2,buffer_size,"%s,SW=%i",config_buffer,config->SW);
			strcpy(config_buffer,config_buffer2);
		}
		if(config->knobonly){
			strncat(config_buffer,",knobonly",buffer_size);
			if(config->timer>0){
				snprintf(config_buffer2,buffer_size,"%s=%i",config_buffer,config->timer);
				strcpy(config_buffer,config_buffer2);
			}
		}
		if(config->volume_lock){
			strncat(config_buffer,",volume",buffer_size);
		}
		if(config->longpress){
			strncat(config_buffer,",longpress",buffer_size);
		}
			log_send_messaging(MESSAGING_INFO,"Updating rotary configuration to %s",config_buffer);
		err = config_set_value(NVS_TYPE_STR, "rotary_config", config_buffer);
		if(err!=ESP_OK){
			log_send_messaging(MESSAGING_ERROR,"Error: %s",esp_err_to_name(err));
		}
	} 
	else {
		err = ESP_ERR_NO_MEM;
	}
	FREE_AND_NULL(config_buffer);
	FREE_AND_NULL(config_buffer2);	
	return err;	
}

/****************************************************************************************
 * 
 */
esp_err_t config_ledvu_set(ledvu_struct_t * config){
	int buffer_size=512;
	esp_err_t err=ESP_OK;
	char * config_buffer=calloc(buffer_size,1);
	char * config_buffer2=calloc(buffer_size,1);	
	if(config_buffer && config_buffer2)  {
		snprintf(config_buffer,buffer_size,"%s,length=%i,gpio=%i",config->type, config->length, config->gpio);
		log_send_messaging(MESSAGING_INFO,"Updating ledvu configuration to %s",config_buffer);
		err = config_set_value(NVS_TYPE_STR, "led_vu_config", config_buffer);
		if(err!=ESP_OK){
			log_send_messaging(MESSAGING_ERROR,"Error: %s",esp_err_to_name(err));
		}
	} 
	else {
		err = ESP_ERR_NO_MEM;
	}
	FREE_AND_NULL(config_buffer);
	FREE_AND_NULL(config_buffer2);	
	return err;	
}

/****************************************************************************************
 * 
 */
esp_err_t config_display_set(const display_config_t * config){
	int buffer_size=512;
	esp_err_t err=ESP_OK;
	char * config_buffer=calloc(buffer_size,1);
	char * config_buffer2=calloc(buffer_size,1);
	if(config_buffer && config_buffer2)  {
		snprintf(config_buffer,buffer_size,"%s,width=%i,height=%i",config->type,config->width,config->height);
		if(strcasecmp("I2C",config->type)==0){
			if(config->address>0 ){
				snprintf(config_buffer2,buffer_size,"%s,address=%i",config_buffer,config->address);
				strcpy(config_buffer,config_buffer2);
			}
		}
		else {
			if(config->CS_pin >=0 ){
				snprintf(config_buffer2,buffer_size,"%s,cs=%i",config_buffer,config->CS_pin);
				strcpy(config_buffer,config_buffer2);
			}		
		}
		if(config->RST_pin >=0 ){
			snprintf(config_buffer2,buffer_size,"%s,reset=%i",config_buffer,config->RST_pin);
			strcpy(config_buffer,config_buffer2);
		}		
// I2C,width=<pixels>,height=<pixels>[address=<i2c_address>][,reset=<gpio>][,HFlip][,VFlip][driver=SSD1306|SSD1326[:1|4]|SSD1327|SH1106]
// SPI,width=<pixels>,height=<pixels>,cs=<gpio>[,back=<gpio>][,reset=<gpio>][,speed=<speed>][,HFlip][,VFlip][driver=SSD1306|SSD1322|SSD1326[:1|4]|SSD1327|SH1106|SSD1675|ST7735|ST7789[,rotate]]		
		if(config->back >=0 ){
			snprintf(config_buffer2,buffer_size,"%s,back=%i",config_buffer,config->back);
			strcpy(config_buffer,config_buffer2);
		}
		if(config->speed >0 && strcasecmp("SPI",config->type)==0){
			snprintf(config_buffer2,buffer_size,"%s,speed=%i",config_buffer,config->speed);
			strcpy(config_buffer,config_buffer2);
		}
		snprintf(config_buffer2,buffer_size,"%s,driver=%s%s%s%s",config_buffer,config->drivername,config->hflip?",HFlip":"",config->vflip?",VFlip":"",config->rotate?",rotate":"");
		strcpy(config_buffer,config_buffer2);
		log_send_messaging(MESSAGING_INFO,"Updating display configuration to %s",config_buffer);
		err = config_set_value(NVS_TYPE_STR, "display_config", config_buffer);
		if(err!=ESP_OK){
			log_send_messaging(MESSAGING_ERROR,"Error: %s",esp_err_to_name(err));
		}
	} 
	else {
		err = ESP_ERR_NO_MEM;
	}
	FREE_AND_NULL(config_buffer);
	FREE_AND_NULL(config_buffer2);	
	return err;
}

/****************************************************************************************
 * 
 */
esp_err_t config_i2s_set(const i2s_platform_config_t * config, const char * nvs_name){
	int buffer_size=255;
	esp_err_t err=ESP_OK;
	char * config_buffer=calloc(buffer_size,1);
	char * config_buffer2=calloc(buffer_size,1);
	if(config_buffer && config_buffer2)  {
		snprintf(config_buffer,buffer_size,"model=%s,bck=%u,ws=%u,do=%u",config->model,config->pin.bck_io_num,config->pin.ws_io_num,config->pin.data_out_num);
		if(config->mute_gpio>=0){
			snprintf(config_buffer2,buffer_size,"%s,mute=%u:%u",config_buffer,config->mute_gpio,config->mute_level);
			strcpy(config_buffer,config_buffer2);
		}
		if(config->sda>=0){
			snprintf(config_buffer2,buffer_size,"%s,sda=%u,scl=%u",config_buffer,config->sda,config->scl);
			strcpy(config_buffer,config_buffer2);
		}
		if(config->i2c_addr>0){
			snprintf(config_buffer2,buffer_size,"%s,i2c=%u",config_buffer,config->i2c_addr);
			strcpy(config_buffer,config_buffer2);
		}
		log_send_messaging(MESSAGING_INFO,"Updating dac configuration to %s",config_buffer);
		err = config_set_value(NVS_TYPE_STR, nvs_name, config_buffer);
		if(err!=ESP_OK){
			log_send_messaging(MESSAGING_ERROR,"Error: %s",esp_err_to_name(err));
		}
	} 
	else {
		err = ESP_ERR_NO_MEM;
	}
	FREE_AND_NULL(config_buffer);
	FREE_AND_NULL(config_buffer2);	
	return err;
}

/****************************************************************************************
 * 
 */
esp_err_t config_spdif_set(const i2s_platform_config_t * config){
	int buffer_size=255;
	esp_err_t err=ESP_OK;
	char * config_buffer=calloc(buffer_size,1);
	if(config_buffer )  {
		snprintf(config_buffer,buffer_size,"bck=%u,ws=%u,do=%u",config->pin.bck_io_num,config->pin.ws_io_num,config->pin.data_out_num);
		log_send_messaging(MESSAGING_INFO,"Updating SPDIF configuration to %s",config_buffer);
		err = config_set_value(NVS_TYPE_STR, "spdif_config", config_buffer);
		if(err!=ESP_OK){
			log_send_messaging(MESSAGING_ERROR,"Error: %s",esp_err_to_name(err));
		}		
	} 
	else {
		err = ESP_ERR_NO_MEM;
	}
	FREE_AND_NULL(config_buffer);
	return err;
}

/****************************************************************************************
 *
 */
esp_err_t config_spi_set(const spi_bus_config_t * config, int host, int dc){
	int buffer_size=255;
	esp_err_t err = ESP_OK;
	char * config_buffer=calloc(buffer_size,1);
	if(config_buffer)  {
		snprintf(config_buffer,buffer_size,"data=%u,clk=%u,dc=%u,host=%u",config->mosi_io_num,config->sclk_io_num,dc,host);
		log_send_messaging(MESSAGING_INFO,"Updating SPI configuration to %s",config_buffer);
		err = config_set_value(NVS_TYPE_STR, "spi_config", config_buffer);
		if(err!=ESP_OK){
			log_send_messaging(MESSAGING_ERROR,"Error: %s",esp_err_to_name(err));
		}		
		free(config_buffer);
	}
	return err;
}

/****************************************************************************************
 * 
 */
const display_config_t * config_display_get(){
	static display_config_t dstruct = {
		.back = -1,
		.CS_pin = -1,
		.RST_pin = -1,
		.depth = -1,
		.address = 0,
		.drivername = NULL,
		.height = 0,
		.width = 0,
		.vflip = false,
		.hflip = false,
		.type = NULL,
		.speed = 0,
		.rotate = false
	};
	char *config = config_alloc_get(NVS_TYPE_STR, "display_config");
	if (!config) {
		return NULL;
	}

	char * p=NULL;

	if ((p = strcasestr(config, "driver")) != NULL){
		sscanf(p, "%*[^:]:%u", &dstruct.depth);
		dstruct.drivername = display_conf_get_driver_name(strchr(p, '=') + 1);
	}
	
	if ((p = strcasestr(config, "width")) != NULL) dstruct.width = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "height")) != NULL) dstruct.height = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "reset")) != NULL) dstruct.RST_pin = atoi(strchr(p, '=') + 1);
	if (strstr(config, "I2C") ) dstruct.type=i2c_name_type;
	if ((p = strcasestr(config, "address")) != NULL) dstruct.address = atoi(strchr(p, '=') + 1);
	if (strstr(config, "SPI") ) dstruct.type=spi_name_type;
	if ((p = strcasestr(config, "cs")) != NULL) dstruct.CS_pin = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "speed")) != NULL) dstruct.speed = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "back")) != NULL) dstruct.back = atoi(strchr(p, '=') + 1);
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
	if(i2c_port) {
#ifdef CONFIG_I2C_LOCKED
		*i2c_port= I2C_NUM_1;
#else
		*i2c_port=i2c_system_port;
#endif		
	}
	return &i2c;
}

/****************************************************************************************
 * 
 */
const gpio_with_level_t * get_gpio_struct_member(const char * nvs_item, const char * name){
	static gpio_with_level_t gpio_member={
		.gpio=-1,
		.level=0
	};
	if(!nvs_item) return &gpio_member;
	const char * p=nvs_item;
	char type[20]={0};
	int match=0;
	do {
		if ((match=sscanf(p, "%d=%19[^,:]:%d", &gpio_member.gpio, type,&gpio_member.level)) >0 && (GPIO_IS_VALID_GPIO(gpio_member.gpio) ||  gpio_member.gpio==GPIO_NUM_NC) && strcasestr(type,name)){
            return &gpio_member;
		}
		p = strchr(p, ',');
	} while (p++);
	gpio_member.gpio=-1;
	gpio_member.level=0;
    return &gpio_member;
}

#define HANDLE_GPIO_STRUCT_MEMBER(name,fixval) memcpy(&gpio_struct.name, get_gpio_struct_member(nvs_item, QUOTE(name)), sizeof(gpio_struct.name)); gpio_struct.name.fixed=fixval
#define ADD_GPIO_STRUCT_MEMBER_TO_ARRAY(array,structvar,name,type) if(((set_GPIO_struct_t *)structvar)->name.gpio>=0){cJSON_AddItemToArray(array,get_gpio_entry(QUOTE(name),type,((set_GPIO_struct_t *)structvar)->name.gpio, ((set_GPIO_struct_t *)structvar)->name.fixed));}
/****************************************************************************************
 * 
 */
const set_GPIO_struct_t * get_gpio_struct(){
	static set_GPIO_struct_t gpio_struct;
	char * nvs_item=config_alloc_get(NVS_TYPE_STR, "set_GPIO");
#ifdef CONFIG_LED_GREEN_GPIO_LEVEL
		gpio_struct.green.level = CONFIG_LED_GREEN_GPIO_LEVEL;
#endif
#ifdef CONFIG_LED_GREEN_GPIO
		gpio_struct.green.gpio = CONFIG_LED_GREEN_GPIO;
#endif
#ifdef CONFIG_LED_RED_GPIO_LEVEL
		gpio_struct.green.level = CONFIG_LED_RED_GPIO_LEVEL;
#endif
#ifdef CONFIG_LED_RED_GPIO
		gpio_struct.red.gpio = CONFIG_LED_RED_GPIO;
#endif	
	if(nvs_item){
		HANDLE_GPIO_STRUCT_MEMBER(amp,false);
#ifndef CONFIG_LED_LOCKED
		HANDLE_GPIO_STRUCT_MEMBER(green,false);
		HANDLE_GPIO_STRUCT_MEMBER(red,false);
#endif
		HANDLE_GPIO_STRUCT_MEMBER(jack,false);
		HANDLE_GPIO_STRUCT_MEMBER(spkfault,false);
		HANDLE_GPIO_STRUCT_MEMBER(vcc,false);
		HANDLE_GPIO_STRUCT_MEMBER(gnd,false);
		HANDLE_GPIO_STRUCT_MEMBER(ir,false);
		free(nvs_item);
	}

#ifdef CONFIG_LED_LOCKED
		gpio_struct.red.fixed=true;
		gpio_struct.green.fixed=true;
#endif	
#ifdef CONFIG_JACK_LOCKED 
		gpio_struct.jack.gpio=CONFIG_JACK_GPIO;
		gpio_struct.jack.fixed=true;
		gpio_struct.jack.level=CONFIG_JACK_GPIO_LEVEL;
#endif
#ifdef CONFIG_SPKFAULT_LOCKED
		gpio_struct.spkfault.gpio=CONFIG_SPKFAULT_GPIO;
		gpio_struct.spkfault.fixed=true;
		gpio_struct.spkfault.level=CONFIG_SPKFAULT_GPIO_LEVEL;
#endif
	return &gpio_struct;	
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
const rotary_struct_t * config_rotary_get() {

	static rotary_struct_t rotary={  .A = -1, .B = -1, .SW = -1, .longpress = false, .knobonly=false,.timer=0,.volume_lock=false};
	char *config = config_alloc_get_default(NVS_TYPE_STR, "rotary_config", NULL, 0);
	if (config && *config) {
		char *p;
		
		// parse config
		if ((p = strcasestr(config, "A")) != NULL) rotary.A = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(config, "B")) != NULL) rotary.B = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(config, "SW")) != NULL) rotary.SW = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(config, "knobonly")) != NULL) {
			p = strchr(p, '=');
			rotary.knobonly = true;
			rotary.timer = p ? atoi(p + 1) : 350;
			rotary.longpress = false;
		} else {
			rotary.knobonly = false;
			rotary.timer = 0;
			if ((p = strcasestr(config, "volume")) != NULL) rotary.volume_lock = true;
			if ((p = strcasestr(config, "longpress")) != NULL) rotary.longpress = true;
		}	
		free(config);
	}
	return &rotary;
}

/****************************************************************************************
 * 
 */
const ledvu_struct_t * config_ledvu_get() {

	static ledvu_struct_t ledvu={  .type = "WS2812", .gpio = -1, .length = 0};
	char *config = config_alloc_get_default(NVS_TYPE_STR, "led_vu_config", NULL, 0);
	if (config && *config) {
		char *p;
	
		// ToDo:  Add code for future support of alternate led types
		if ((p = strcasestr(config, "gpio")) != NULL) ledvu.gpio = atoi(strchr(p, '=') + 1);
		if ((p = strcasestr(config, "length")) != NULL) ledvu.length = atoi(strchr(p, '=') + 1);
		free(config);
	}
	return &ledvu;
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
cJSON * add_gpio_for_value(cJSON * list,const char * name,int gpio, const char * prefix, bool fixed){
	cJSON * llist = list?list:cJSON_CreateArray();
	if(GPIO_IS_VALID_GPIO(gpio) && gpio>0){
		cJSON_AddItemToArray(llist,get_gpio_entry(name,prefix,gpio,fixed));
	}
	return llist;
}

/****************************************************************************************
 *
 */
cJSON * add_gpio_for_name(cJSON * list,const char * nvs_entry,const char * name, const char * prefix, bool fixed){
	cJSON * llist = list?list:cJSON_CreateArray();
	char *p;
	int gpioNum=0;
	if ((p = strcasestr(nvs_entry, name)) != NULL) {
		gpioNum = atoi(strchr(p, '=') + 1);
		if(gpioNum>=0){
			cJSON_AddItemToArray(llist,get_gpio_entry(name,prefix,gpioNum,fixed));
		}
	}
	return llist;
}

/****************************************************************************************
 *
 */
cJSON * get_GPIO_nvs_list(cJSON * list) {
	cJSON * ilist = list?list:cJSON_CreateArray();
	const set_GPIO_struct_t * gpios = get_gpio_struct();
	ADD_GPIO_STRUCT_MEMBER_TO_ARRAY(ilist,gpios,vcc,"other");
	ADD_GPIO_STRUCT_MEMBER_TO_ARRAY(ilist,gpios,gnd,"other");
	ADD_GPIO_STRUCT_MEMBER_TO_ARRAY(ilist,gpios,amp,"other");
	ADD_GPIO_STRUCT_MEMBER_TO_ARRAY(ilist,gpios,ir,"other");
	ADD_GPIO_STRUCT_MEMBER_TO_ARRAY(ilist,gpios,jack,"other");
	ADD_GPIO_STRUCT_MEMBER_TO_ARRAY(ilist,gpios,green,"other");
	ADD_GPIO_STRUCT_MEMBER_TO_ARRAY(ilist,gpios,red,"other");
	ADD_GPIO_STRUCT_MEMBER_TO_ARRAY(ilist,gpios,spkfault,"other");
	return ilist;
}

/****************************************************************************************
 *
 */
cJSON * get_DAC_GPIO(cJSON * list){
	cJSON * llist = list;
	if(!llist){
		llist = cJSON_CreateArray();
	}	
	const i2s_platform_config_t * i2s_config= config_dac_get();
	if(i2s_config->pin.bck_io_num>=0){
		cJSON_AddItemToArray(llist,get_gpio_entry("bck","dac",i2s_config->pin.bck_io_num,is_dac_config_locked()));
		cJSON_AddItemToArray(llist,get_gpio_entry("ws","dac",i2s_config->pin.ws_io_num,is_dac_config_locked()));
		cJSON_AddItemToArray(llist,get_gpio_entry("do","dac",i2s_config->pin.data_out_num,is_dac_config_locked()));
		if(i2s_config->sda>=0){
			cJSON_AddItemToArray(llist,get_gpio_entry("sda","dac",i2s_config->sda,is_dac_config_locked()));
			cJSON_AddItemToArray(llist,get_gpio_entry("scl","dac",i2s_config->scl,is_dac_config_locked()));
		}
		if(i2s_config->mute_gpio>=0){
			cJSON_AddItemToArray(llist,get_gpio_entry("mute","dac",i2s_config->mute_gpio,is_dac_config_locked()));
		}
	}
	return llist;
}

/****************************************************************************************
 *
 */
cJSON * get_Display_GPIO(cJSON * list){
	cJSON * llist = list;
	if(!llist){
		llist = cJSON_CreateArray();
	}	
	const display_config_t * config= config_display_get();
	if(config->back >=0){
		cJSON_AddItemToArray(llist,get_gpio_entry("backlight","display",config->back,false));
	}
	if(config->CS_pin >=0){
		cJSON_AddItemToArray(llist,get_gpio_entry("CS","display",config->CS_pin,false));
	}	
	if(config->RST_pin >=0){
		cJSON_AddItemToArray(llist,get_gpio_entry("reset","display",config->RST_pin,false));
	}
	return llist;
}
/****************************************************************************************
 *
 */
cJSON * get_I2C_GPIO(cJSON * list){
	cJSON * llist = list;
	if(!llist){
		llist = cJSON_CreateArray();
	}	
	int port=0;
	const i2c_config_t * i2c_config = config_i2c_get(&port);
	if(i2c_config->scl_io_num>=0){
		cJSON_AddItemToArray(llist,get_gpio_entry("scl","i2c",i2c_config->scl_io_num,false));
		cJSON_AddItemToArray(llist,get_gpio_entry("sda","i2c",i2c_config->sda_io_num,false));
	}
	return llist;
}

/****************************************************************************************
 *
 */
cJSON * get_SPI_GPIO(cJSON * list){
	cJSON * llist = list;
	if(!llist){
		llist = cJSON_CreateArray();
	}	
	spi_host_device_t spi_host;
	const spi_bus_config_t * spi_config = config_spi_get(&spi_host);
	
	if(spi_config->miso_io_num>=0){
		cJSON_AddItemToArray(llist,get_gpio_entry("data","spi",spi_config->miso_io_num,false));
		cJSON_AddItemToArray(llist,get_gpio_entry("data","clk",spi_config->sclk_io_num,false));
	}
	if(spi_system_dc_gpio>0){
		cJSON_AddItemToArray(llist,get_gpio_entry("data","dc",spi_system_dc_gpio,false));
	}
	return llist;
}

/****************************************************************************************
 *
 */
cJSON * get_SPDIF_GPIO(cJSON * list, bool fixed){
	cJSON * llist = list?list:cJSON_CreateArray();
	char * spdif_config = config_spdif_get_string();
	if(spdif_config){
		llist = add_gpio_for_name(llist,spdif_config,"bck", "spdif", fixed);
		llist = add_gpio_for_name(llist,spdif_config,"ws",  "spdif", fixed);
		llist = add_gpio_for_name(llist,spdif_config,"do",  "spdif", fixed);
		free(spdif_config);	
	}	
	return llist;
}

/****************************************************************************************
 *
 */
cJSON * get_Rotary_GPIO(cJSON * list){
	cJSON * llist = list?list:cJSON_CreateArray();

	const rotary_struct_t *rotary= config_rotary_get();
	add_gpio_for_value(llist,"A",rotary->A, "rotary", false);
	add_gpio_for_value(llist,"B",rotary->B, "rotary", false);
	add_gpio_for_value(llist,"SW",rotary->SW, "rotary", false);
	return llist;
}

/****************************************************************************************
 *
 */
cJSON * get_ledvu_GPIO(cJSON * list){
	cJSON * llist = list?list:cJSON_CreateArray();

	const ledvu_struct_t *ledvu= config_ledvu_get();
	add_gpio_for_value(llist,"gpio",ledvu->gpio, "led_vu", false);
	return llist;
}

/****************************************************************************************
 *
 */
esp_err_t get_gpio_structure(cJSON * gpio_entry, gpio_entry_t ** gpio){
	esp_err_t err = ESP_OK;
	*gpio = malloc(sizeof(gpio_entry_t));
	cJSON * val = cJSON_GetObjectItem(gpio_entry,"gpio");
	if(val){
		(*gpio)->gpio= (int)val->valuedouble;
	} else {
		ESP_LOGE(TAG,"gpio pin not found");
		err=ESP_FAIL;
	}
	val = cJSON_GetObjectItem(gpio_entry,"name");
	if(val){
		(*gpio)->name= strdup(cJSON_GetStringValue(val));
	} else {
		ESP_LOGE(TAG,"gpio name value not found");
		err=ESP_FAIL;
	}
	val = cJSON_GetObjectItem(gpio_entry,"group");
	if(val){
		(*gpio)->group= strdup(cJSON_GetStringValue(val));
	} else {
		ESP_LOGE(TAG,"gpio group value not found");
		err=ESP_FAIL;
	}
	val = cJSON_GetObjectItem(gpio_entry,"fixed");
	if(val){
		(*gpio)->fixed= cJSON_IsTrue(val);
	} else {
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
	get_gpio_list(refresh);
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
	gpio_entry_t * gpio=NULL;
	get_gpio_list(refresh);
	cJSON_ArrayForEach(gpio_header,gpio_list)
	{
		if(get_gpio_structure(gpio_header, &gpio)==ESP_OK && strcasecmp(gpio->name,name)&& strcasecmp(gpio->group,group)){
			ESP_LOGD(TAG,"Found GPIO: %s=%d %s", gpio->name,gpio->gpio,gpio->fixed?"(FIXED)":"(VARIABLE)");
		}
	}
	return gpio;
}

#ifndef PICO_PSRAM_CLK_IO
#define PICO_PSRAM_CLK_IO          6
#endif
#ifndef PSRAM_SPIQ_SD0_IO
#define PSRAM_SPIQ_SD0_IO          7
#define PSRAM_SPID_SD1_IO          8
#define PSRAM_SPIWP_SD3_IO         10
#define PSRAM_SPIHD_SD2_IO         9
#define FLASH_HSPI_CLK_IO          14
#define FLASH_HSPI_CS_IO           15
#define PSRAM_HSPI_SPIQ_SD0_IO     12
#define PSRAM_HSPI_SPID_SD1_IO     13
#define PSRAM_HSPI_SPIWP_SD3_IO    2
#define PSRAM_HSPI_SPIHD_SD2_IO    4
#endif


cJSON * get_psram_gpio_list(cJSON * list){
	const char * psram_dev = "psram";
	const char * flash_dev = "flash";
	const char * clk = "clk";
	const char * cs = "cs";
	const char * spiq_sd0_io="spiq_sd0_io"; 
	const char * spid_sd1_io = "spid_sd1_io";
	const char * spiwp_sd3_io = "spiwp_sd3_io";
	const char * spihd_sd2_io = "spihd_sd2_io";
	cJSON * llist=list;
	
    uint32_t chip_ver = REG_GET_FIELD(EFUSE_BLK0_RDATA3_REG, EFUSE_RD_CHIP_VER_PKG);
    uint32_t pkg_ver = chip_ver & 0x7;
    if (pkg_ver == EFUSE_RD_CHIP_VER_PKG_ESP32D2WDQ5) {
        rtc_vddsdio_config_t cfg = rtc_vddsdio_get_config();
        if (cfg.tieh != RTC_VDDSDIO_TIEH_1_8V) {
            return llist;
        }
        cJSON_AddItemToArray(list,get_gpio_entry(clk,psram_dev,CONFIG_D2WD_PSRAM_CLK_IO,true));
        cJSON_AddItemToArray(list,get_gpio_entry(cs,psram_dev,CONFIG_D2WD_PSRAM_CS_IO,true));
    } else if ((pkg_ver == EFUSE_RD_CHIP_VER_PKG_ESP32PICOD2) || (pkg_ver == EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4)) {
        rtc_vddsdio_config_t cfg = rtc_vddsdio_get_config();
        if (cfg.tieh != RTC_VDDSDIO_TIEH_3_3V) {
            return llist;
        }
		cJSON_AddItemToArray(list,get_gpio_entry(clk,psram_dev,PICO_PSRAM_CLK_IO,true));
        cJSON_AddItemToArray(list,get_gpio_entry(cs,psram_dev,CONFIG_PICO_PSRAM_CS_IO,true));
    } else if ((pkg_ver == EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6) || (pkg_ver == EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5)){
		cJSON_AddItemToArray(list,get_gpio_entry(clk,psram_dev,CONFIG_D0WD_PSRAM_CLK_IO,true));
        cJSON_AddItemToArray(list,get_gpio_entry(cs,psram_dev,CONFIG_D0WD_PSRAM_CS_IO,true));
    } else {
        ESP_LOGW(TAG, "Cant' determine GPIOs for PSRAM chip id: %d", pkg_ver);
		cJSON_AddItemToArray(list,get_gpio_entry(clk,psram_dev,-1,true));
        cJSON_AddItemToArray(list,get_gpio_entry(cs,psram_dev,-1,true));
    }

    const uint32_t spiconfig = ets_efuse_get_spiconfig();
    if (spiconfig == EFUSE_SPICONFIG_SPI_DEFAULTS) {
		cJSON_AddItemToArray(list,get_gpio_entry(spiq_sd0_io,psram_dev,PSRAM_SPIQ_SD0_IO,true));
		cJSON_AddItemToArray(list,get_gpio_entry(spid_sd1_io,psram_dev,PSRAM_SPID_SD1_IO,true));
		cJSON_AddItemToArray(list,get_gpio_entry(spiwp_sd3_io,psram_dev,PSRAM_SPIWP_SD3_IO,true));
		cJSON_AddItemToArray(list,get_gpio_entry(spihd_sd2_io,psram_dev,PSRAM_SPIHD_SD2_IO,true));
    } else if (spiconfig == EFUSE_SPICONFIG_HSPI_DEFAULTS) {
		cJSON_AddItemToArray(list,get_gpio_entry(spiq_sd0_io,psram_dev,PSRAM_HSPI_SPIQ_SD0_IO,true));
		cJSON_AddItemToArray(list,get_gpio_entry(spid_sd1_io,psram_dev,PSRAM_HSPI_SPID_SD1_IO,true));
		cJSON_AddItemToArray(list,get_gpio_entry(spiwp_sd3_io,psram_dev,PSRAM_HSPI_SPIWP_SD3_IO,true));
		cJSON_AddItemToArray(list,get_gpio_entry(spihd_sd2_io,psram_dev,PSRAM_HSPI_SPIHD_SD2_IO,true));
    } else {
		cJSON_AddItemToArray(list,get_gpio_entry(spiq_sd0_io,psram_dev,EFUSE_SPICONFIG_RET_SPIQ(spiconfig),true));
		cJSON_AddItemToArray(list,get_gpio_entry(spid_sd1_io,psram_dev,EFUSE_SPICONFIG_RET_SPID(spiconfig),true));
		cJSON_AddItemToArray(list,get_gpio_entry(spihd_sd2_io,psram_dev,EFUSE_SPICONFIG_RET_SPIHD(spiconfig),true));
        // If flash mode is set to QIO or QOUT, the WP pin is equal the value configured in bootloader.
        // If flash mode is set to DIO or DOUT, the WP pin should config it via menuconfig.
        #if CONFIG_ESPTOOLPY_FLASHMODE_QIO || CONFIG_FLASHMODE_QOUT
		cJSON_AddItemToArray(list,get_gpio_entry(spiwp_sd3_io,psram_dev,CONFIG_BOOTLOADER_SPI_WP_PIN,true));
        #else
		cJSON_AddItemToArray(list,get_gpio_entry(spiwp_sd3_io,psram_dev,CONFIG_SPIRAM_SPIWP_SD3_PIN,true));
        #endif
	}
    if (spiconfig == EFUSE_SPICONFIG_SPI_DEFAULTS) {
		cJSON_AddItemToArray(list,get_gpio_entry(clk,flash_dev,SPI_IOMUX_PIN_NUM_CLK,true));
		cJSON_AddItemToArray(list,get_gpio_entry(cs,flash_dev,SPI_IOMUX_PIN_NUM_CS,true));
    } else if (spiconfig == EFUSE_SPICONFIG_HSPI_DEFAULTS) {
		cJSON_AddItemToArray(list,get_gpio_entry(clk,flash_dev,FLASH_HSPI_CLK_IO,true));
		cJSON_AddItemToArray(list,get_gpio_entry(cs,flash_dev,FLASH_HSPI_CS_IO,true));
    } else {
		cJSON_AddItemToArray(list,get_gpio_entry(clk,flash_dev,EFUSE_SPICONFIG_RET_SPICLK(spiconfig),true));
		cJSON_AddItemToArray(list,get_gpio_entry(cs,flash_dev,EFUSE_SPICONFIG_RET_SPICS0(spiconfig),true));
	}
    return llist;	
}

/****************************************************************************************
 *
 */
cJSON * get_gpio_list(bool refresh) {
	gpio_num_t gpio_num;
	if(gpio_list && !refresh){
		return gpio_list;
	}
	
	if(gpio_list){
		cJSON_Delete(gpio_list);
	}
	gpio_list= cJSON_CreateArray();
	
#ifndef CONFIG_BAT_LOCKED
	char *bat_config = config_alloc_get_default(NVS_TYPE_STR, "bat_config", NULL, 0);
	if (bat_config) {
		char *p;
		int channel;
		if ((p = strcasestr(bat_config, "channel") ) != NULL) {
			channel = atoi(strchr(p, '=') + 1);
			if(channel != -1){
				if(adc1_pad_get_io_num(channel,&gpio_num )==ESP_OK){
					cJSON_AddItemToArray(gpio_list,get_gpio_entry("bat","other",gpio_num,false));
				}
			}
		}
		free(bat_config);
	}
#else
		if(adc1_pad_get_io_num(CONFIG_BAT_CHANNEL,&gpio_num )==ESP_OK){
			cJSON_AddItemToArray(gpio_list,get_gpio_entry("bat","other",gpio_num,true));
		}
#endif
	gpio_list=get_GPIO_nvs_list(gpio_list);
	gpio_list=get_SPDIF_GPIO(gpio_list,is_spdif_config_locked());
	gpio_list=get_Rotary_GPIO(gpio_list);
	gpio_list=get_Display_GPIO(gpio_list);
	gpio_list=get_SPI_GPIO(gpio_list);
	gpio_list=get_I2C_GPIO(gpio_list);
	gpio_list=get_DAC_GPIO(gpio_list);
	gpio_list=get_ledvu_GPIO(gpio_list);
	gpio_list=get_psram_gpio_list(gpio_list);
	return gpio_list;
}
