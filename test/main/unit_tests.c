/* Example test application for testable component.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "unity.h"
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
#include "network_manager.h"
#include "squeezelite-ota.h"
#include <math.h>
#include "audio_controls.h"
// #include "Configurator.h"
#pragma message("fixme: search for TODO below")
#include "telnet.h"
#include "messaging.h"
#include "gds.h"
#include "gds_default_if.h"
#include "gds_draw.h"
#include "gds_text.h"
#include "gds_font.h"
#include "display.h"
#include "accessors.h"
#include "cmd_system.h"
#include "cmd_config.h"
#include "cmd_i2ctools.h"
#include "cmd_nvs.h"
const char unknown_string_placeholder[] = "unknown";
const char null_string_placeholder[] = "null";
// as an exception _init function don't need include
extern void services_init(void);
const char * str_or_unknown(const char * str) { return (str?str:unknown_string_placeholder); }
const char * str_or_null(const char * str) { return (str?str:null_string_placeholder); }
bool is_recovery_running;
extern void initialize_console();
/* brief this is an exemple of a callback that you can setup in your own app to get notified of wifi manager event */
esp_err_t update_certificates(bool force){return ESP_OK; }
void init_commands(){
	initialize_console();
	/* Register commands */
	register_system();
	register_config_cmd();
	register_nvs();
	register_i2ctools();
#if CONFIG_WITH_CONFIG_UI
#endif    
}
void test_init()
{
	const esp_partition_t *running = esp_ota_get_running_partition();
	is_recovery_running = (running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY);
	initialize_nvs();
	config_init();
	services_init();
	init_commands();
}

static void print_banner(const char* text);

void app_main()
{
	test_init();
    print_banner("Running tests with [config] tag");
    UNITY_BEGIN();
    unity_run_tests_by_tag("[config]", false);
    UNITY_END();

    // print_banner("Running all the registered tests");
    // UNITY_BEGIN();
    // unity_run_all_tests();
    // UNITY_END();

    print_banner("Starting interactive test menu");
    /* This function will not return, and will be busy waiting for UART input.
     * Make sure that task watchdog is disabled if you use this function.
     */
    unity_run_menu();
}

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    app_main();

    return 0;
}
static void print_banner(const char* text)
{
    printf("\n#### %s #####\n\n", text);
}




