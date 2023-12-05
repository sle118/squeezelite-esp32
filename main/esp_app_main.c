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
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include <esp_event.h>
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "mdns.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "trace.h"
#include "network_manager.h"
#include "squeezelite-ota.h"
#include <math.h>
#include "audio_controls.h"
#include "Configurator.h"
#include "telnet.h"
#include "messaging.h"
#include "gds.h"
#include "gds_default_if.h"
#include "gds_draw.h"
#include "gds_text.h"
#include "gds_font.h"
#include "led_vu.h"
#include "display.h"
#include "accessors.h"
#include "cmd_system.h"
#include "tools.h"
#if defined(CONFIG_WITH_METRICS)
#include "Metrics.h"
#endif

const char unknown_string_placeholder[] = "unknown";
const char null_string_placeholder[] = "null";
EventGroupHandle_t network_event_group;

bool bypass_network_manager=false;
const int CONNECTED_BIT = BIT0;
#define JOIN_TIMEOUT_MS (10000)
#define LOCAL_MAC_SIZE 20
static const char TAG[] = "esp_app_main";
#define DEFAULT_HOST_NAME "squeezelite"
RTC_NOINIT_ATTR uint32_t RebootCounter ;
RTC_NOINIT_ATTR uint32_t RecoveryRebootCounter ;
RTC_NOINIT_ATTR uint16_t ColdBootIndicatorFlag;
bool cold_boot=true;

#ifdef CONFIG_IDF_TARGET_ESP32S3
extern const char _ctype_[];
const char* __ctype_ptr__ = _ctype_;
#endif

static bool bNetworkConnected=false;

// as an exception _init function don't need include
extern void services_init(void);
extern void services_sleep_init(void);
extern void	display_init(char *welcome);
extern void led_vu_init(void);
extern void target_init(char *target);
extern void start_squeezelite();
const char * str_or_unknown(const char * str) { return (str?str:unknown_string_placeholder); }
const char * str_or_null(const char * str) { return (str?str:null_string_placeholder); }

bool is_recovery_running;
bool is_network_connected(){
	return bNetworkConnected;
}
void cb_connection_got_ip(nm_state_t new_state, int sub_state){
	const char *hostname;
	static ip4_addr_t ip;
	tcpip_adapter_ip_info_t ipInfo; 
	network_get_ip_info(&ipInfo);
	if (ip.addr && ipInfo.ip.addr != ip.addr) {
		ESP_LOGW(TAG, "IP change, need to reboot");
		if(!configurator_waitcommit()){
			ESP_LOGW(TAG,"Unable to commit configuration. ");
		}
		esp_restart();
	}

	// initializing mDNS
	network_get_hostname(&hostname);
    mdns_init();
    mdns_hostname_set(hostname);
	
	ESP_LOGI(TAG, "Network connected and mDNS initialized with %s", hostname);

	messaging_post_message(MESSAGING_INFO,MESSAGING_CLASS_SYSTEM,"Network connected");
	xEventGroupSetBits(network_event_group, CONNECTED_BIT);
	bNetworkConnected=true;

	led_unpush(LED_GREEN);
	if(is_recovery_running){
		// when running in recovery, send a LMS discovery message 
		// to find a running instance. This is to enable using 
		// the plugin's proxy mode for FW download and avoid
		// expired certificate issues.
		discover_ota_server(5);
	}
}
void cb_connection_sta_disconnected(nm_state_t new_state, int sub_state){
	led_blink_pushed(LED_GREEN, 250, 250);
	messaging_post_message(MESSAGING_WARNING,MESSAGING_CLASS_SYSTEM,"Wifi disconnected");
	bNetworkConnected=false;
	xEventGroupClearBits(network_event_group, CONNECTED_BIT);
}
bool wait_for_wifi(){
	bool connected=(xEventGroupGetBits(network_event_group) & CONNECTED_BIT)!=0;
	if(!connected){
		ESP_LOGD(TAG,"Waiting for Network...");
	    connected = (xEventGroupWaitBits(network_event_group, CONNECTED_BIT,
	                                   pdFALSE, pdTRUE, JOIN_TIMEOUT_MS / portTICK_PERIOD_MS)& CONNECTED_BIT)!=0;
	    if(!connected){
	    	ESP_LOGW(TAG,"Network timeout.");
	    }
	    else
	    {
	    	ESP_LOGI(TAG,"Network Connected!");
	    }
	}
    return connected;
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


uint32_t halSTORAGE_RebootCounterRead(void) { return RebootCounter ; }
uint32_t halSTORAGE_RebootCounterUpdate(int32_t xValue) { 
	if(RebootCounter >100) {
		RebootCounter = 0;
		RecoveryRebootCounter = 0;
	}
	RebootCounter = (xValue != 0) ? (RebootCounter + xValue) : 0;
	RecoveryRebootCounter  = (xValue != 0) && is_recovery_running ? (RecoveryRebootCounter + xValue) : 0;
	return RebootCounter ; 
	}

void handle_ap_connect(nm_state_t new_state, int sub_state){
	start_telnet(NULL);
}
void handle_network_up(nm_state_t new_state, int sub_state){
	halSTORAGE_RebootCounterUpdate(0);
}
esp_reset_reason_t xReason=ESP_RST_UNKNOWN;

void app_main()
{
	if(ColdBootIndicatorFlag != 0xFACE ){
		ESP_LOGI(TAG, "System is booting from power on.");
		cold_boot = true;
        ColdBootIndicatorFlag = 0xFACE;
    }
	else {
		cold_boot = false;
	}
	const esp_partition_t *running = esp_ota_get_running_partition();
	is_recovery_running = (running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY);
	xReason = esp_reset_reason();
	ESP_LOGI(TAG,"Reset reason is: %u. Running from partition %s type %s ", 	
			xReason, 
			running->label, 
			running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY?"Factory":"Application");
	if(!is_recovery_running )  {
		/* unscheduled restart (HW, Watchdog or similar) thus increment dynamic
	 	* counter then log current boot statistics as a warning */
		uint32_t Counter = halSTORAGE_RebootCounterUpdate(1) ;		// increment counter
		ESP_LOGI(TAG,"Reboot counter=%u\n", Counter) ;
		if (Counter == 5) {
			guided_factory();
		}
	}
	else {
		uint32_t Counter = halSTORAGE_RebootCounterUpdate(1) ;		// increment counter
		if(RecoveryRebootCounter==1 && Counter>=5){
			// First time we are rebooting in recovery after crashing
			messaging_post_message(MESSAGING_ERROR,MESSAGING_CLASS_SYSTEM,"System was forced into recovery mode after crash likely caused by some bad configuration\n");
		}
		ESP_LOGI(TAG,"Recovery Reboot counter=%u\n", Counter) ;
			if (RecoveryRebootCounter == 5) {
			ESP_LOGW(TAG,"System rebooted too many times. This could be an indication that configuration is corrupted. Erasing config.");
			// TODO: Add support for the commented code
			// erase_settings_partition();
			// reboot one more time
			#pragma message("Add support for erasing the configuration")
			guided_factory();
			
		}		
		if (RecoveryRebootCounter >5){
			messaging_post_message(MESSAGING_ERROR,MESSAGING_CLASS_SYSTEM,"System was forced into recovery mode after crash likely caused by some bad configuration. Configuration was reset to factory.\n");
		}	
	}


	MEMTRACE_PRINT_DELTA();
	ESP_LOGI(TAG,"Starting app_main");
	init_spiffs();
	listFiles("/");
	ESP_LOGI(TAG,"Setting up config subsystem.");
	configurator_load();	
	MEMTRACE_PRINT_DELTA();
#if defined(CONFIG_WITH_METRICS)
	ESP_LOGI(TAG,"Setting up metrics.");
	metrics_init();	
	MEMTRACE_PRINT_DELTA();
#endif
	ESP_LOGI(TAG,"Setting up telnet.");
	init_telnet(); // align on 32 bits boundaries
	MEMTRACE_PRINT_DELTA();
	ESP_LOGD(TAG,"Creating event group for wifi");
	network_event_group = xEventGroupCreate();
	ESP_LOGD(TAG,"Clearing CONNECTED_BIT from wifi group");
	xEventGroupClearBits(network_event_group, CONNECTED_BIT);
	MEMTRACE_PRINT_DELTA();
	ESP_LOGI(TAG,"Configuring services");
	services_init();
	MEMTRACE_PRINT_DELTA();
	ESP_LOGI(TAG,"Initializing display");
	display_init("SqueezeESP32");
	MEMTRACE_PRINT_DELTA();
	if(strlen(platform->target)>0){
		target_init(platform->target);
	}
	ESP_LOGI(TAG,"Initializing led_vu");
	led_vu_init();
	if(is_recovery_running) {
		ESP_LOGI(TAG,"Turning on display");
		if (display) {
			GDS_ClearExt(display, true);
			GDS_SetFont(display, Font_line_2 );
			GDS_TextPos(display, GDS_FONT_DEFAULT, GDS_TEXT_CENTERED, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, "RECOVERY");
		}
		if(led_display) {
			led_vu_color_yellow(LED_VU_BRIGHT);
		}
	}
	#if defined(CONFIG_WITH_METRICS)
	metrics_event_boot(is_recovery_running?"recovery":"ota");
	#endif

	if(!is_recovery_running){
		#pragma message("Add audio controls support")
		// ESP_LOGD(TAG,"Getting audio control mapping ");
		// if(platform->has_dev && platform->dev.buttons_count >0){
		// 	ESP_LOGD(TAG,"Initializing audio control buttons");	
		// }
		// char *actrls_config = config_alloc_get_default(NVS_TYPE_STR, "actrls_config", "", 0);
		// if (actrls_init(actrls_config) == ESP_OK) {
		
		// } else {
		// 	ESP_LOGD(TAG,"No audio control buttons");
		// }
		// if (actrls_config) free(actrls_config);
		// TODO: Add support for the commented code
	}

	/* start the wifi manager */
	ESP_LOGD(TAG,"Blinking led");
	led_blink_pushed(LED_GREEN, 250, 250);
	MEMTRACE_PRINT_DELTA();
	if(bypass_network_manager){
		ESP_LOGW(TAG,"Network manager is disabled. Use command line for wifi control.");
	}
	else {
		ESP_LOGI(TAG,"Starting Network Manager");
		network_start();
		MEMTRACE_PRINT_DELTA();
		network_register_state_callback(NETWORK_WIFI_ACTIVE_STATE,WIFI_CONNECTED_STATE, "cb_connection_got_ip", &cb_connection_got_ip);
		network_register_state_callback(NETWORK_ETH_ACTIVE_STATE,ETH_ACTIVE_CONNECTED_STATE, "cb_connection_got_ip",&cb_connection_got_ip);
		network_register_state_callback(NETWORK_WIFI_ACTIVE_STATE,WIFI_LOST_CONNECTION_STATE, "cb_connection_sta_disconnected",&cb_connection_sta_disconnected);

		/* Start the telnet service after we are certain that the network stack has been properly initialized.
		 * This can be either after we're started the AP mode, or after we've started the STA mode  */
		network_register_state_callback(NETWORK_INITIALIZING_STATE,-1, "handle_ap_connect", &handle_ap_connect);
		network_register_state_callback(NETWORK_ETH_ACTIVE_STATE,ETH_ACTIVE_LINKDOWN_STATE, "handle_network_up", &handle_network_up);
		network_register_state_callback(NETWORK_WIFI_ACTIVE_STATE,WIFI_INITIALIZING_STATE, "handle_network_up", &handle_network_up);
		MEMTRACE_PRINT_DELTA();	
	}
	if(!is_recovery_running){
		MEMTRACE_PRINT_DELTA_MESSAGE("Launching Squeezelite");
		start_squeezelite();
		MEMTRACE_PRINT_DELTA_MESSAGE("Squeezelite Started");		
	}	
	MEMTRACE_PRINT_DELTA_MESSAGE("Starting Console");
	console_start();
	MEMTRACE_PRINT_DELTA_MESSAGE("Console started");

	if(sys_state && sys_state->ota_url && strlen(sys_state->ota_url)){
		ESP_LOGD(TAG,"Found OTA URL %s",sys_state->ota_url);
		if(is_recovery_running){
			while(!bNetworkConnected){
				wait_for_wifi();
				taskYIELD();
			}
			ESP_LOGI(TAG,"Updating firmware from link: %s",sys_state->ota_url);
			#if defined(CONFIG_WITH_METRICS)
			metrics_event("fw_update");
			#endif
			start_ota(sys_state->ota_url, NULL, 0);
		}
		else {
			ESP_LOGE(TAG,"Restarted to application partition. We're not going to perform OTA!");
		}		
	}

    services_sleep_init();
	messaging_post_message(MESSAGING_INFO,MESSAGING_CLASS_SYSTEM,"System started");
}
