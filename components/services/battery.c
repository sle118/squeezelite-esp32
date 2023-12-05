/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/adc.h"
#include "battery.h"
#include "Configurator.h"

/* 
 There is a bug in esp32 which causes a spurious interrupt on gpio 36/39 when
 using ADC, AMP and HALL sensor. Rather than making battery aware, we just ignore
 if as the interrupt lasts 80ns and should be debounced (and the ADC read does not
 happen very often)
*/ 

#define BATTERY_TIMER	(10*1000)

static const char *TAG = "battery";

static struct {
	float sum, avg, scale;
	int count;
	sys_Battery * battery_config;
	TimerHandle_t timer;
} battery;
#define BATTERY_CHANNEL(b) (b.battery_config?b.battery_config->channel - sys_BatteryChannelEnum_CH0:-1)
#define ATTENUATION(b) (b.battery_config?b.battery_config->atten - sys_BatteryAttenEnum_ATT_0:-1)
void (*battery_handler_svc)(float value, int cells);

/****************************************************************************************
 * 
 */
float battery_value_svc(void) {
	return battery.avg;
 }
 
/****************************************************************************************
 * 
 */
uint8_t battery_level_svc(void) {
	// TODO: this is vastly incorrect
	if(!battery.battery_config){ return 0; }
	int level = battery.avg ? (battery.avg - (3.0 * battery.battery_config->cells)) / ((4.2 - 3.0) * battery.battery_config->cells) * 100 : 0;
	return level < 100 ? level : 100;
}

/****************************************************************************************
 * 
 */
static void battery_callback(TimerHandle_t xTimer) {
	if(!battery.battery_config){ return;  }
	
	battery.sum += adc1_get_raw(BATTERY_CHANNEL(battery)) * battery.battery_config->scale / 4095.0;
	if (++battery.count == 30) {
		battery.avg = battery.sum / battery.count;
		battery.sum = battery.count = 0;
		if (battery_handler_svc) (battery_handler_svc)(battery.avg, battery.battery_config->cells);
		ESP_LOGI(TAG, "Voltage %.2fV", battery.avg);
	}	
}

/****************************************************************************************
 * 
 */
void battery_svc_init(void) {
	if (SYS_DEV_BATTERY(battery.battery_config) && BATTERY_CHANNEL(battery) != -1) {
		adc1_config_width(ADC_WIDTH_BIT_12);
		adc1_config_channel_atten(BATTERY_CHANNEL(battery), ATTENUATION(battery));

		battery.avg = adc1_get_raw(BATTERY_CHANNEL(battery)) * battery.scale / 4095.0;    
		battery.timer = xTimerCreate("battery", BATTERY_TIMER / portTICK_RATE_MS, pdTRUE, NULL, battery_callback);
		xTimerStart(battery.timer, portMAX_DELAY);
		
		ESP_LOGI(TAG, "Battery measure channel: %u, scale %f, atten %d, cells %u, avg %.2fV", BATTERY_CHANNEL(battery), battery.scale, ATTENUATION(battery), battery.battery_config->cells, battery.avg);		
	} else {
		ESP_LOGI(TAG, "No battery");
	}	
}
