/* Console example — WiFi commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// cmd_wifi has been replaced by wifi-manager
/* Console example � WiFi commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "cmd_wifi.h"

#include <stdio.h>
#include <string.h>

#include "cmd_decl.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "led.h"
extern bool bypass_network_manager;
#define JOIN_TIMEOUT_MS (10000)
#include "platform_console.h"

// To enable wifi configuration from the command line, uncomment the line below
// define WIFI_CMDLINE 1


extern EventGroupHandle_t network_event_group;
extern const int CONNECTED_BIT;
//static const char * TAG = "cmd_wifi";
/** Arguments used by 'join' function */
static struct {
    struct arg_int *timeout;
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} join_args;





static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		led_blink_pushed(LED_GREEN, 250, 250);
        esp_wifi_connect();
        xEventGroupClearBits(network_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		led_unpush(LED_GREEN);
        xEventGroupSetBits(network_event_group, CONNECTED_BIT);
    }
}

static void initialise_wifi(void)
{
    static bool initialized = false;
    if (initialized) {
        return;
    }
    esp_netif_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    initialized = true;
	led_blink(LED_GREEN, 250, 250);
}

static void wifi_join(void *arg)
{
	const char *ssid = join_args.ssid->sval[0];
    const char *pass = join_args.password->sval[0];
	int timeout_ms = join_args.timeout->ival[0];
	
    initialise_wifi();
    wifi_config_t wifi_config = { 0 };
    strncpy((char *) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
	wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    if (pass) {
        strncpy((char *) wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
		wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';		
    }

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_connect() );

    int bits = xEventGroupWaitBits(network_event_group, CONNECTED_BIT,
                                   pdFALSE, pdTRUE, timeout_ms / portTICK_PERIOD_MS);
								   
    if (bits & CONNECTED_BIT) {
		ESP_LOGI(__func__, "Connected");	
    } else {
        ESP_LOGW(__func__, "Connection timed out");
	}
}

//static int set_auto_connect(int argc, char **argv)
//{
//    int nerrors = arg_parse(argc, argv, (void **) &join_args);
//    if (nerrors != 0) {
//        arg_print_errors(stderr, join_args.end, argv[0]);
//        return 1;
//    }
//    ESP_LOGI(__func__, "Connecting to '%s'",
//             join_args.ssid->sval[0]);
//
//    /* set default value*/
//    if (join_args.timeout->count == 0) {
//        join_args.timeout->ival[0] = JOIN_TIMEOUT_MS;
//    }
//
//    bool connected = wifi_join(join_args.ssid->sval[0],
//                               join_args.password->sval[0],
//                               join_args.timeout->ival[0]);
//    if (!connected) {
//        ESP_LOGW(__func__, "Connection timed out");
//        return 1;
//    }
//    ESP_LOGI(__func__, "Connected");
//    return 0;
//}

static int connect(int argc, char **argv)
{
	int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&join_args);
    if (nerrors != 0) {
        return 1;
    }
    ESP_LOGI(__func__, "Connecting to '%s'",
             join_args.ssid->sval[0]);

    /* set default value*/
    if (join_args.timeout->count == 0) {
        join_args.timeout->ival[0] = JOIN_TIMEOUT_MS;
    }

	// need to use that trick to make sure we use internal stack
	xTimerStart(xTimerCreate("wifi_join", 1, pdFALSE, NULL, wifi_join), portMAX_DELAY);        

    return 0;
}
void register_wifi_join()
{
    join_args.timeout = arg_int0(NULL, "timeout", "<t>", "Connection timeout, ms");
    join_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    join_args.password = arg_str0(NULL, NULL, "<pass>", "PSK of AP");
    join_args.end = arg_end(2);

    const esp_console_cmd_t join_cmd = {
        .command = "join",
        .help = "Join WiFi AP as a station",
        .hint = NULL,
        .func = &connect,
        .argtable = &join_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&join_cmd) );
}

void register_wifi()
{
    #ifdef WIFI_CMDLINE
    register_wifi_join();
    if(bypass_network_manager){
    	initialise_wifi();
    }
    #endif
}
