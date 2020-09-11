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
#include <esp_event.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "mdns.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "nvs_utilities.h"
#include "trace.h"
#include "wifi_manager.h"
#include "squeezelite-ota.h"
#include <math.h>
#include "audio_controls.h"
#include "platform_config.h"
#include "telnet.h"
#include "messaging.h"
#include "gds.h"
#include "gds_default_if.h"
#include "gds_draw.h"
#include "gds_text.h"
#include "gds_font.h"
#include "display.h"
#include "accessors.h"
static const char certs_namespace[] = "certificates";
static const char certs_key[] = "blob";
static const char certs_version[] = "version";
const char unknown_string_placeholder[] = "unknown";
const char null_string_placeholder[] = "null";
EventGroupHandle_t wifi_event_group;

bool bypass_wifi_manager=false;
const int CONNECTED_BIT = BIT0;
#define JOIN_TIMEOUT_MS (10000)
#define LOCAL_MAC_SIZE 20
static const char TAG[] = "esp_app_main";
#define DEFAULT_HOST_NAME "squeezelite"
char * fwurl = NULL;

static bool bWifiConnected=false;
extern const uint8_t server_cert_pem_start[] asm("_binary_github_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_github_pem_end");

// as an exception _init function don't need include
extern void services_init(void);
extern void	display_init(char *welcome);
const char * str_or_unknown(const char * str) { return (str?str:unknown_string_placeholder); }
const char * str_or_null(const char * str) { return (str?str:null_string_placeholder); }
bool is_recovery_running;
/* brief this is an exemple of a callback that you can setup in your own app to get notified of wifi manager event */
void cb_connection_got_ip(void *pvParameter){
	static ip4_addr_t ip;
	tcpip_adapter_ip_info_t ipInfo; 

	tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
	if (ip.addr && ipInfo.ip.addr != ip.addr) {
		ESP_LOGW(TAG, "IP change, need to reboot");
		if(!wait_for_commit()){
			ESP_LOGW(TAG,"Unable to commit configuration. ");
		}
		esp_restart();
	}
	ip.addr = ipInfo.ip.addr;
	ESP_LOGI(TAG, "I have a connection!");
	messaging_post_message(MESSAGING_INFO,MESSAGING_CLASS_SYSTEM,"Wifi connected");
	xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
	bWifiConnected=true;
	led_unpush(LED_GREEN);
}
void cb_connection_sta_disconnected(void *pvParameter){
	led_blink_pushed(LED_GREEN, 250, 250);
	messaging_post_message(MESSAGING_WARNING,MESSAGING_CLASS_SYSTEM,"Wifi disconnected");
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

char * process_ota_url(){
    ESP_LOGI(TAG,"Checking for update url");
    char * fwurl=config_alloc_get(NVS_TYPE_STR, "fwurl");
	if(fwurl!=NULL)
	{
		ESP_LOGD(TAG,"Deleting nvs entry for Firmware URL %s", fwurl);
		config_delete_key("fwurl");
	}
	return fwurl;
}

esp_log_level_t  get_log_level_from_char(char * level){
	if(!strcasecmp(level, "NONE"    )) { return ESP_LOG_NONE  ;}
	if(!strcasecmp(level, "ERROR"   )) { return ESP_LOG_ERROR ;}
	if(!strcasecmp(level, "WARN"    )) { return ESP_LOG_WARN  ;}
	if(!strcasecmp(level, "INFO"    )) { return ESP_LOG_INFO  ;}
	if(!strcasecmp(level, "DEBUG"   )) { return ESP_LOG_DEBUG ;}
	if(!strcasecmp(level, "VERBOSE" )) { return ESP_LOG_VERBOSE;}
	return ESP_LOG_WARN;
}
void set_log_level(char * tag, char * level){
	esp_log_level_set(tag, get_log_level_from_char(level));
}


esp_err_t update_certificates(bool force){

	nvs_handle handle;
	esp_err_t esp_err;
    esp_app_desc_t running_app_info;

	ESP_LOGI(TAG,   "About to check if certificates need to be updated in flash");
	esp_err = nvs_open_from_partition(settings_partition, certs_namespace, NVS_READWRITE, &handle);
	if (esp_err != ESP_OK) {
		LOG_SEND(MESSAGING_INFO,"Unable to update HTTPS certificates. Could not open NVS namespace %s. Error %s", certs_namespace, esp_err_to_name(esp_err));
		return esp_err;
	}

	const esp_partition_t *running = esp_ota_get_running_partition();
	ESP_LOGI(TAG, "Running partition [%s] type %d subtype %d (offset 0x%08x)", running->label, running->type, running->subtype, running->address);

	if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
		ESP_LOGI(TAG, "Running version: %s", running_app_info.version);
	}

	size_t len=0;
	char *str=NULL;
	bool changed=false;
	if ( (esp_err= nvs_get_str(handle, certs_version, NULL, &len)) == ESP_OK) {
		str=(char *)malloc(len+1);
		if(str){
			memset(str,0x00,len+1);
			if ( (esp_err = nvs_get_str(handle,  certs_version, str, &len)) == ESP_OK) {
				ESP_LOGI(TAG,"Certificate version: %s", str);
			}
		}
	}
	if(str!=NULL && running->subtype !=ESP_PARTITION_SUBTYPE_APP_FACTORY){
		// If certificates were found in nvs, only update if we're not
		// running recovery. This will prevent rolling back to an older version
		if(strcmp((char *)running_app_info.version,(char *)str )){
			// Versions are different
			ESP_LOGW(TAG,"Found a different software version. Updating certificates");
			changed=true;
		}
		free(str);
	}
	else if(str==NULL){
		ESP_LOGW(TAG,"No certificate found. Adding certificates");
		changed=true;
	}

	if(changed || force){

		esp_err = nvs_set_blob(handle, certs_key, server_cert_pem_start, (server_cert_pem_end-server_cert_pem_start));
		if(esp_err!=ESP_OK){
			log_send_messaging(MESSAGING_ERROR,"Failed to store certificate data: %s", esp_err_to_name(esp_err));
		}
		else {
			esp_err = nvs_set_str(handle,  certs_version, running_app_info.version);
			if(esp_err!=ESP_OK){
				log_send_messaging(MESSAGING_ERROR,"Unable to update HTTPS Certificates version: %s",esp_err_to_name(esp_err));
			}
			else {
				esp_err = nvs_commit(handle);
				if(esp_err!=ESP_OK){
					log_send_messaging(MESSAGING_ERROR,"Failed to commit certificates changes : %s",esp_err_to_name(esp_err));
				}
				else {
					log_send_messaging(MESSAGING_INFO,"HTTPS Certificates were updated with version: %s",running_app_info.version);
				}
			}
		}
	}

	nvs_close(handle);
	return ESP_OK;
}
const char * get_certificate(){
	nvs_handle handle;
	esp_err_t esp_err;
	char *blob =NULL;
//
	ESP_LOGD(TAG,  "Fetching certificate.");
	esp_err = nvs_open_from_partition(settings_partition, certs_namespace, NVS_READONLY, &handle);
	if(esp_err == ESP_OK){
        size_t len;
        esp_err = nvs_get_blob(handle, certs_key, NULL, &len);
        if( esp_err == ESP_OK) {
            blob = (char *) heap_caps_malloc(len+1, (MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
            if(!blob){
            	log_send_messaging(MESSAGING_ERROR,"Unable to retrieve HTTPS certificates. %s","Memory allocation failed");
        		return "";
            }
            memset(blob,0x00,len+1);
            esp_err = nvs_get_blob(handle, certs_key, blob, &len);
            if ( esp_err  == ESP_OK) {
            	ESP_LOGD(TAG,"Certificates content is %d bytes long: ", len);
            }
            else {
            	log_send_messaging(MESSAGING_ERROR,"Unable to retrieve HTTPS certificates. Get blob failed: %s", esp_err_to_name(esp_err));
            }
        }
        else{
        	log_send_messaging(MESSAGING_ERROR,"Unable to retrieve HTTPS certificates. Get blob failed: %s",esp_err_to_name(esp_err));
        }
        nvs_close(handle);
	}
	else{
    	log_send_messaging(MESSAGING_ERROR,"Unable to retrieve HTTPS certificates. NVS name space %s open failed: %s",certs_namespace, esp_err_to_name(esp_err));
	}
	return blob;
}

#define DEFAULT_NAME_WITH_MAC(var,defval) char var[strlen(defval)+sizeof(macStr)]; strcpy(var,defval); strcat(var,macStr)
void register_default_nvs(){
	uint8_t mac[6];
	char macStr[LOCAL_MAC_SIZE+1];
	char default_command_line[strlen(CONFIG_DEFAULT_COMMAND_LINE)+sizeof(macStr)];

	esp_read_mac((uint8_t *)&mac, ESP_MAC_WIFI_STA);
	snprintf(macStr, LOCAL_MAC_SIZE-1,"-%x%x%x", mac[3], mac[4], mac[5]);

	DEFAULT_NAME_WITH_MAC(default_bt_name,CONFIG_BT_NAME);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "bt_name", default_bt_name);
	config_set_default(NVS_TYPE_STR, "bt_name", default_bt_name, 0);

	DEFAULT_NAME_WITH_MAC(default_host_name,DEFAULT_HOST_NAME);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "host_name", default_host_name);
	config_set_default(NVS_TYPE_STR, "host_name", default_host_name, 0);

	DEFAULT_NAME_WITH_MAC(default_airplay_name,CONFIG_AIRPLAY_NAME);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "airplay_name",default_airplay_name);
	config_set_default(NVS_TYPE_STR, "airplay_name",default_airplay_name , 0);

	DEFAULT_NAME_WITH_MAC(default_ap_name,CONFIG_DEFAULT_AP_SSID);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ap_ssid", default_ap_name);
	config_set_default(NVS_TYPE_STR, "ap_ssid",default_ap_name , 0);

	strncpy(default_command_line, CONFIG_DEFAULT_COMMAND_LINE,sizeof(default_command_line)-1);
	strncat(default_command_line, " -n ",sizeof(default_command_line)-1);
	strncat(default_command_line, default_host_name,sizeof(default_command_line)-1);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "autoexec", "1");
	config_set_default(NVS_TYPE_STR,"autoexec","1", 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "autoexec1",default_command_line);
	config_set_default(NVS_TYPE_STR,"autoexec1",default_command_line,0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "a2dp_sink_name", CONFIG_A2DP_SINK_NAME);
	config_set_default(NVS_TYPE_STR, "a2dp_sink_name", CONFIG_A2DP_SINK_NAME, 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "bt_sink_pin", STR(CONFIG_BT_SINK_PIN));
	config_set_default(NVS_TYPE_STR, "a2dp_ctmt", STR(CONFIG_A2DP_CONNECT_TIMEOUT_MS), 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "bt_sink_pin", STR(CONFIG_A2DP_CONNECT_TIMEOUT_MS));
	config_set_default(NVS_TYPE_STR, "a2dp_ctrld", STR(CONFIG_A2DP_CONTROL_DELAY_MS), 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "bt_sink_pin", STR(CONFIG_A2DP_CONTROL_DELAY_MS));

	config_set_default(NVS_TYPE_STR, "bt_sink_pin", STR(CONFIG_BT_SINK_PIN), 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "release_url", SQUEEZELITE_ESP32_RELEASE_URL);
	config_set_default(NVS_TYPE_STR, "release_url", SQUEEZELITE_ESP32_RELEASE_URL, 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s","ap_ip_address",CONFIG_DEFAULT_AP_IP );
	config_set_default(NVS_TYPE_STR, "ap_ip_address",CONFIG_DEFAULT_AP_IP , 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ap_ip_gateway",CONFIG_DEFAULT_AP_GATEWAY );
	config_set_default(NVS_TYPE_STR, "ap_ip_gateway",CONFIG_DEFAULT_AP_GATEWAY , 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s","ap_ip_netmask",CONFIG_DEFAULT_AP_NETMASK );
	config_set_default(NVS_TYPE_STR, "ap_ip_netmask",CONFIG_DEFAULT_AP_NETMASK , 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ap_channel",STR(CONFIG_DEFAULT_AP_CHANNEL));
	config_set_default(NVS_TYPE_STR, "ap_channel",STR(CONFIG_DEFAULT_AP_CHANNEL) , 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ap_pwd", CONFIG_DEFAULT_AP_PASSWORD);
	config_set_default(NVS_TYPE_STR, "ap_pwd", CONFIG_DEFAULT_AP_PASSWORD, 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "airplay_port", CONFIG_AIRPLAY_PORT);
	config_set_default(NVS_TYPE_STR, "airplay_port", CONFIG_AIRPLAY_PORT, 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "a2dp_dev_name", CONFIG_A2DP_DEV_NAME);
	config_set_default(NVS_TYPE_STR, "a2dp_dev_name", CONFIG_A2DP_DEV_NAME, 0);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "bypass_wm", "0");
	config_set_default(NVS_TYPE_STR, "bypass_wm", "0", 0);

	ESP_LOGD(TAG,"Registering default Audio control board type %s, value ","actrls_config");
	config_set_default(NVS_TYPE_STR, "actrls_config", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "lms_ctrls_raw");
	config_set_default(NVS_TYPE_STR, "lms_ctrls_raw", "n", 0);
	
	ESP_LOGD(TAG,"Registering default Audio control board type %s, value %s", "rotary_config", CONFIG_ROTARY_ENCODER);
	config_set_default(NVS_TYPE_STR, "rotary_config", CONFIG_ROTARY_ENCODER, 0);

	char number_buffer[101] = {};
	snprintf(number_buffer,sizeof(number_buffer)-1,"%u",OTA_FLASH_ERASE_BLOCK);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ota_erase_blk", number_buffer);
	config_set_default(NVS_TYPE_STR, "ota_erase_blk", number_buffer, 0);

	snprintf(number_buffer,sizeof(number_buffer)-1,"%u",OTA_STACK_SIZE);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ota_stack", number_buffer);
	config_set_default(NVS_TYPE_STR, "ota_stack", number_buffer, 0);

	snprintf(number_buffer,sizeof(number_buffer)-1,"%d",OTA_TASK_PRIOTITY);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ota_prio", number_buffer);
	config_set_default(NVS_TYPE_STR, "ota_prio", number_buffer, 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "enable_bt_sink", STR(CONFIG_BT_SINK));
	config_set_default(NVS_TYPE_STR, "enable_bt_sink", STR(CONFIG_BT_SINK), 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "enable_airplay", STR(CONFIG_AIRPLAY_SINK));
	config_set_default(NVS_TYPE_STR, "enable_airplay", STR(CONFIG_AIRPLAY_SINK), 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "display_config", CONFIG_DISPLAY_CONFIG);
	config_set_default(NVS_TYPE_STR, "display_config", CONFIG_DISPLAY_CONFIG, 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "i2c_config", CONFIG_I2C_CONFIG);
	config_set_default(NVS_TYPE_STR, "i2c_config", CONFIG_I2C_CONFIG, 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "spi_config", CONFIG_SPI_CONFIG);
	config_set_default(NVS_TYPE_STR, "spi_config", CONFIG_SPI_CONFIG, 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "set_GPIO", CONFIG_SET_GPIO);
	config_set_default(NVS_TYPE_STR, "set_GPIO", CONFIG_SET_GPIO, 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "led_brightness");
	config_set_default(NVS_TYPE_STR, "led_brightness", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "spdif_config");
	config_set_default(NVS_TYPE_STR, "spdif_config", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "dac_config");
	config_set_default(NVS_TYPE_STR, "dac_config", "", 0);
	//todo: add dac_config for known targets
	ESP_LOGD(TAG,"Registering default value for key %s", "dac_controlset");
	config_set_default(NVS_TYPE_STR, "dac_controlset", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "jack_mutes_amp");
	config_set_default(NVS_TYPE_STR, "jack_mutes_amp", "n", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "bat_config");
	config_set_default(NVS_TYPE_STR, "bat_config", "", 0);
			
	ESP_LOGD(TAG,"Registering default value for key %s", "metadata_config");
	config_set_default(NVS_TYPE_STR, "metadata_config", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "telnet_enable");
	config_set_default(NVS_TYPE_STR, "telnet_enable", "", 0);

	ESP_LOGD(TAG,"Registering default value for key %s", "telnet_buffer");
	config_set_default(NVS_TYPE_STR, "telnet_buffer", "40000", 0);

	ESP_LOGD(TAG,"Registering default value for key %s", "telnet_block");
	config_set_default(NVS_TYPE_STR, "telnet_block", "500", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "stats");
	config_set_default(NVS_TYPE_STR, "stats", "n", 0);
	wait_for_commit();
	ESP_LOGD(TAG,"Done setting default values in nvs.");
}

void app_main()
{
	const esp_partition_t *running = esp_ota_get_running_partition();
	is_recovery_running = (running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY);

	char * fwurl = NULL;
	ESP_LOGI(TAG,"Starting app_main");
	initialize_nvs();
	ESP_LOGI(TAG,"Setting up telnet.");
	init_telnet(); // align on 32 bits boundaries

	ESP_LOGI(TAG,"Setting up config subsystem.");
	config_init();

	ESP_LOGD(TAG,"Creating event group for wifi");
	wifi_event_group = xEventGroupCreate();
	ESP_LOGD(TAG,"Clearing CONNECTED_BIT from wifi group");
	xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);

	ESP_LOGI(TAG,"Registering default values");
	register_default_nvs();

	ESP_LOGI(TAG,"Configuring services");
	services_init();

	ESP_LOGI(TAG,"Initializing display");
	display_init("SqueezeESP32");

	if(is_recovery_running && display){
		GDS_ClearExt(display, true);
		GDS_SetFont(display, &Font_droid_sans_fallback_15x17 );
		GDS_TextPos(display, GDS_FONT_MEDIUM, GDS_TEXT_CENTERED, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, "RECOVERY");
	}


	ESP_LOGI(TAG,"Checking if certificates need to be updated");
	update_certificates(false);


	ESP_LOGD(TAG,"Getting firmware OTA URL (if any)");
	fwurl = process_ota_url();

	ESP_LOGD(TAG,"Getting value for WM bypass, nvs 'bypass_wm'");
	char * bypass_wm = config_alloc_get_default(NVS_TYPE_STR, "bypass_wm", "0", 0);
	if(bypass_wm==NULL)
	{
		ESP_LOGE(TAG, "Unable to retrieve the Wifi Manager bypass flag");
		bypass_wifi_manager = false;
	}
	else {
		bypass_wifi_manager=(strcmp(bypass_wm,"1")==0 ||strcasecmp(bypass_wm,"y")==0);
	}

	ESP_LOGD(TAG,"Getting audio control mapping ");
	char *actrls_config = config_alloc_get_default(NVS_TYPE_STR, "actrls_config", NULL, 0);
	if (actrls_init(actrls_config) == ESP_OK) {
		ESP_LOGD(TAG,"Initializing audio control buttons type %s", actrls_config);	
	} else {
		ESP_LOGD(TAG,"No audio control buttons");
	}
	if (actrls_config) free(actrls_config);

	/* start the wifi manager */
	ESP_LOGD(TAG,"Blinking led");
	led_blink_pushed(LED_GREEN, 250, 250);

	if(bypass_wifi_manager){
		ESP_LOGW(TAG,"*******************************************************************************************");
		ESP_LOGW(TAG,"* wifi manager is disabled. Please use wifi commands to connect to your wifi access point.");
		ESP_LOGW(TAG,"*******************************************************************************************");
	}
	else {
		ESP_LOGI(TAG,"Starting Wifi Manager");
		wifi_manager_start();
		wifi_manager_set_callback(EVENT_STA_GOT_IP, &cb_connection_got_ip);
		wifi_manager_set_callback(EVENT_STA_DISCONNECTED, &cb_connection_sta_disconnected);
		/* Start the telnet service after we are certain that the network stack has been properly initialized.
		 * This can be either after we're started the AP mode, or after we've started the STA mode  */
		wifi_manager_set_callback(ORDER_START_AP, &start_telnet);
		wifi_manager_set_callback(ORDER_CONNECT_STA, &start_telnet);
	}
	console_start();
	if(fwurl && strlen(fwurl)>0){
		if(is_recovery_running){
			while(!bWifiConnected){
				wait_for_wifi();
				taskYIELD();
			}
			ESP_LOGI(TAG,"Updating firmware from link: %s",fwurl);
			start_ota(fwurl, NULL, 0);
		}
		else {
			ESP_LOGE(TAG,"Restarted to application partition. We're not going to perform OTA!");
		}
		free(fwurl);
	}
	messaging_post_message(MESSAGING_INFO,MESSAGING_CLASS_SYSTEM,"System started");
}
