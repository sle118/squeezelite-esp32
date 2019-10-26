/* 
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "platform_esp32.h"
#include "led.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "mdns.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "nvs_utilities.h"
#include "http_server.h"
#include "trace.h"
#include "wifi_manager.h"
#include "squeezelite-ota.h"
#include <math.h>

static EventGroupHandle_t wifi_event_group;
bool enable_bt_sink=false;
bool enable_airplay=false;
bool jack_mutes_amp=false;
const int CONNECTED_BIT = BIT0;
#define JOIN_TIMEOUT_MS (10000)

static const char TAG[] = "esp_app_main";
char * fwurl = NULL;

#ifdef CONFIG_SQUEEZEAMP
#define LED_GREEN_GPIO 	12
#define LED_RED_GPIO	13
#else
#define LED_GREEN_GPIO 	0
#define LED_RED_GPIO	0
#endif
static bool bWifiConnected=false;




/* brief this is an exemple of a callback that you can setup in your own app to get notified of wifi manager event */
void cb_connection_got_ip(void *pvParameter){
	ESP_LOGI(TAG, "I have a connection!");
	xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
	bWifiConnected=true;
	led_unpush(LED_GREEN);
}
void cb_connection_sta_disconnected(void *pvParameter){
	led_blink_pushed(LED_GREEN, 250, 250);
	bWifiConnected=false;
	xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
}
bool wait_for_wifi(){
	bool connected=(xEventGroupGetBits(wifi_event_group) & CONNECTED_BIT)!=0;
	if(!connected){
		ESP_LOGD(TAG,"Waiting for WiFi...");
	    connected = (xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
	                                   pdFALSE, pdTRUE, JOIN_TIMEOUT_MS / portTICK_PERIOD_MS)& CONNECTED_BIT)!=0;
	    if(!connected){
	    	ESP_LOGW(TAG,"wifi timeout.");
	    }
	    else
	    {
	    	ESP_LOGI(TAG,"WiFi Connected!");
	    }
	}
    return connected;
}
static void initialize_nvs() {
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	err = nvs_flash_init_partition(settings_partition);
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase_partition(settings_partition));
		err = nvs_flash_init_partition(settings_partition);
	}
	ESP_ERROR_CHECK(err);

}
char * process_ota_url(){
    nvs_handle nvs;
    ESP_LOGI(TAG,"Checking for update url");
    char * fwurl=get_nvs_value_alloc(NVS_TYPE_STR, "fwurl");
	if(fwurl!=NULL)
	{
		ESP_LOGD(TAG,"Deleting nvs entry for Firmware URL %s", fwurl);
		esp_err_t err = nvs_open_from_partition(settings_partition, current_namespace, NVS_READWRITE, &nvs);
		if (err == ESP_OK) {
			err = nvs_erase_key(nvs, "fwurl");
			if (err == ESP_OK) {
				ESP_LOGD(TAG,"Firmware url erased from nvs.");
				err = nvs_commit(nvs);
				if (err == ESP_OK) {
					ESP_LOGI(TAG, "Value with key '%s' erased", "fwurl");
					ESP_LOGD(TAG,"nvs erase committed.");
				}
				else
				{
					ESP_LOGE(TAG,"Unable to commit nvs erase operation. Error : %s.",esp_err_to_name(err));
				}
			}
			else
			{
				ESP_LOGE(TAG,"Error : %s. Unable to delete firmware url key.",esp_err_to_name(err));
			}
			nvs_close(nvs);
		}
		else
		{
			ESP_LOGE(TAG,"Error opening nvs: %s. Unable to delete firmware url key.",esp_err_to_name(err));
		}
	}
	return fwurl;
}

//CONFIG_SDIF_NUM=0
//CONFIG_SPDIF_BCK_IO=26
//CONFIG_SPDIF_WS_IO=25
//CONFIG_SPDIF_DO_IO=15
//CONFIG_A2DP_CONTROL_DELAY_MS=500
//CONFIG_A2DP_CONNECT_TIMEOUT_MS=1000
//CONFIG_WIFI_MANAGER_MAX_RETRY=2
u16_t get_adjusted_volume(u16_t volume){

	char * str_factor = get_nvs_value_alloc_default(NVS_TYPE_STR, "volumefactor", "3", 0);
	if(str_factor != NULL ){

		float factor = atof(str_factor);
		free(str_factor);
		return (u16_t) (65536.0f * powf( (volume/ 128.0f), factor) );
	}
	else {
		ESP_LOGW(TAG,"Error retrieving volume factor.  Returning unmodified volume level. ");
		return volume;
	}

}

void register_default_nvs(){
	nvs_value_set_default(NVS_TYPE_STR, "bt_sink_name", CONFIG_BT_NAME, 0);
	nvs_value_set_default(NVS_TYPE_STR, "bt_sink_pin", STR(CONFIG_BT_SINK_PIN), 0);
	nvs_value_set_default(NVS_TYPE_STR, "host_name", "squeezelite-esp32", 0);
	nvs_value_set_default(NVS_TYPE_STR, "release_url", SQUEEZELITE_ESP32_RELEASE_URL, 0);
	nvs_value_set_default(NVS_TYPE_STR, "ap_ip_address",CONFIG_DEFAULT_AP_IP , 0);
	nvs_value_set_default(NVS_TYPE_STR, "ap_ip_gateway",CONFIG_DEFAULT_AP_GATEWAY , 0);
	nvs_value_set_default(NVS_TYPE_STR, "ap_ip_netmask",CONFIG_DEFAULT_AP_NETMASK , 0);
	nvs_value_set_default(NVS_TYPE_STR, "ap_channel",STR(CONFIG_DEFAULT_AP_CHANNEL) , 0);
	nvs_value_set_default(NVS_TYPE_STR, "ap_ssid",CONFIG_DEFAULT_AP_SSID , 0);
	nvs_value_set_default(NVS_TYPE_STR, "ap_password", CONFIG_DEFAULT_AP_PASSWORD, 0);
	nvs_value_set_default(NVS_TYPE_STR, "airplay_name",CONFIG_AIRPLAY_NAME , 0);
	nvs_value_set_default(NVS_TYPE_STR, "airplay_port", CONFIG_AIRPLAY_PORT, 0);
	nvs_value_set_default(NVS_TYPE_STR, "a2dp_sink_name", CONFIG_A2DP_SINK_NAME, 0);
	nvs_value_set_default(NVS_TYPE_STR, "a2dp_dev_name", CONFIG_A2DP_DEV_NAME, 0);

	char * flag = get_nvs_value_alloc_default(NVS_TYPE_STR, "enable_bt_sink", STR(CONFIG_BT_SINK), 0);
	enable_bt_sink= (strcmp(flag,"1")==0 ||strcasecmp(flag,"y")==0);
	free(flag);
	flag = get_nvs_value_alloc_default(NVS_TYPE_STR, "enable_airplay", STR(CONFIG_AIRPLAY_SINK), 0);
	enable_airplay= (strcmp(flag,"1")==0 ||strcasecmp(flag,"y")==0);
	free(flag);

	flag = get_nvs_value_alloc_default(NVS_TYPE_STR, "jack_mutes_amp", "n", 0);
	jack_mutes_amp= (strcmp(flag,"1")==0 ||strcasecmp(flag,"y")==0);
	free(flag);



}

void app_main()
{
	char * fwurl = NULL;
	initialize_nvs();
	register_default_nvs();
	led_config(LED_GREEN, LED_GREEN_GPIO, 0);
	led_config(LED_RED, LED_RED_GPIO, 0);
	wifi_event_group = xEventGroupCreate();
	xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
	fwurl = process_ota_url();

	/* start the wifi manager */
	led_blink(LED_GREEN, 250, 250);
	wifi_manager_start();
	wifi_manager_set_callback(EVENT_STA_GOT_IP, &cb_connection_got_ip);
	wifi_manager_set_callback(WIFI_EVENT_STA_DISCONNECTED, &cb_connection_sta_disconnected);
	console_start();
	if(fwurl && strlen(fwurl)>0){
		while(!bWifiConnected){
			wait_for_wifi();
			taskYIELD();
		}
		ESP_LOGI(TAG,"Updating firmware from link: %s",fwurl);
		start_ota(fwurl, true);
		free(fwurl);
	}
}
