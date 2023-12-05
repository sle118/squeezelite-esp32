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
 
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "gpio_exp.h"
#include "cJSON.h"
#include "string.h"
// #include "Configurator.h"
#pragma message("fixme: look for TODO below")
#include "adac.h"

static const char TAG[] = "DAC external";

static void speaker(bool active);
static void headset(bool active);
static bool volume(unsigned left, unsigned right) { return false; }
static void power(adac_power_e mode);
static bool init(char *config, int i2c_port_num, i2s_config_t *i2s_config, bool *mck);

static bool i2c_json_execute(char *set);

const struct adac_s dac_external = { sys_DACModelEnum_I2S, init, adac_deinit, power, speaker, headset, volume };
static cJSON *i2c_json;
static int i2c_addr;

static const struct {
	char *model;
	bool mclk;
	char *controlset;
} codecs[] = {
	{ "es8388", true,
		"{\"init\":[ 																						\
			{\"reg\":8,\"val\":0}, {\"reg\":2,\"val\":243}, {\"reg\":43,\"val\":128}, {\"reg\":0,\"val\":5}, 		\
			{\"reg\":1,\"val\":64}, {\"reg\":4,\"val\":60},"
#if BYTES_PER_FRAME == 8
		   "{\"reg\":23,\"val\":32},"
#else			
		   "{\"reg\":23,\"val\":24},"
#endif
		   "{\"reg\":24,\"val\":2},		\
			{\"reg\":26,\"val\":0}, {\"reg\":27,\"val\":0}, {\"reg\":25,\"val\":50}, {\"reg\":38,\"val\":0},		\
			{\"reg\":39,\"val\":184}, {\"reg\":42,\"val\":184}, {\"reg\":46,\"val\":30}, {\"reg\":47,\"val\":30},	\
			{\"reg\":48,\"val\":30}, {\"reg\":49,\"val\":30}, {\"reg\":2,\"val\":170}]}" },
	{ NULL, false, NULL }		
};

/****************************************************************************************
 * init
 */
static bool init(char *config, int i2c_port_num, i2s_config_t *i2s_config, bool *mck) {	 
	char *p=NULL;	
	void * dummy = &codecs;
	// i2c_addr = adac_init(config, i2c_port_num);
	// if (!i2c_addr) return true;
	
	// ESP_LOGI(TAG, "DAC on I2C @%d", i2c_addr);
	
	// p = config_alloc_get_str("dac_controlset", CONFIG_DAC_CONTROLSET, NULL);

	// if ((!p || !*p) && (p = strcasestr(config, "model")) != NULL) {
	// 	char model[32] = "";
	// 	int i;
	// 	sscanf(p, "%*[^=]=%31[^,]", model);
	// 	for (i = 0; *model && ((p = codecs[i].controlset) != NULL) && strcasecmp(codecs[i].model, model); i++);
	//  	if (p) *mck = codecs[i].mclk;
	//  }	 

	// i2c_json = cJSON_Parse(p);
	
	// if (!i2c_json) {
	// 	ESP_LOGW(TAG, "no i2c controlset found");
	// 	return true;
	// }	
		
	// if (!i2c_json_execute("init")) {	
	// 	ESP_LOGE(TAG, "could not intialize DAC");
	// 	return false;
	// }	
	// TODO: Add support for the commented code

	return true;
}	

/****************************************************************************************
 * power
 */
static void power(adac_power_e mode) {
	if (mode == ADAC_STANDBY || mode == ADAC_OFF) i2c_json_execute("poweroff");
	else i2c_json_execute("poweron");
}

/****************************************************************************************
 * speaker
 */
static void speaker(bool active) {
	if (active) i2c_json_execute("speakeron");
	else i2c_json_execute("speakeroff");
} 

/****************************************************************************************
 * headset
 */
static void headset(bool active) {
	if (active) i2c_json_execute("headseton");
	else i2c_json_execute("headsetoff");
}

/****************************************************************************************
 * 
 */
bool i2c_json_execute(char *set) {
	cJSON *json_set = cJSON_GetObjectItemCaseSensitive(i2c_json, set);
	cJSON *item;

	if (!json_set) return true;
	
	cJSON_ArrayForEach(item, json_set) {
        cJSON *action;
        
        // is this a delay
        if ((action = cJSON_GetObjectItemCaseSensitive(item, "delay")) != NULL) {
            vTaskDelay(pdMS_TO_TICKS(action->valueint));
            ESP_LOGI(TAG, "DAC waiting %d ms", action->valueint);
            continue;
        }
        
        // is this a gpio toggle
        if ((action = cJSON_GetObjectItemCaseSensitive(item, "gpio")) != NULL) {
            cJSON *level = cJSON_GetObjectItemCaseSensitive(item, "level");
            ESP_LOGI(TAG, "set GPIO %d at %d", action->valueint, level->valueint);
            gpio_set_direction_x(action->valueint, GPIO_MODE_OUTPUT);
            gpio_set_level_x(action->valueint, level->valueint);
            continue;
        }
                    
		action= cJSON_GetObjectItemCaseSensitive(item, "reg");
		cJSON *val = cJSON_GetObjectItemCaseSensitive(item, "val");
		
        // this is gpio register setting or crap
		if (cJSON_IsArray(val)) {
			cJSON *value;			
			uint8_t *data = malloc(cJSON_GetArraySize(val));
			int count = 0;
			
			if (!data) continue;
			
			cJSON_ArrayForEach(value, val) {
				data[count++] = value->valueint;		
			}
			
			adac_write(i2c_addr, action->valueint, data, count);
			free(data);			
		} else {
			cJSON *mode = cJSON_GetObjectItemCaseSensitive(item, "mode");

			if (!action || !val) continue;

			if (!mode) {
				adac_write_byte(i2c_addr, action->valueint, val->valueint);
			} else if (!strcasecmp(mode->valuestring, "or")) {
				uint8_t data = adac_read_byte(i2c_addr, action->valueint);
				data |= (uint8_t) val->valueint;
				adac_write_byte(i2c_addr, action->valueint, data);
			} else if (!strcasecmp(mode->valuestring, "and")) {
				uint8_t data = adac_read_byte(i2c_addr, action->valueint);
				data &= (uint8_t) val->valueint;
				adac_write_byte(i2c_addr, action->valueint, data);
			}
		}
	}
	
	return true;
}	
