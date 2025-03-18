/* Example test application for testable component.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "test_common_init.h"
#include "tools.h"        // Assuming tools.h contains init_spiffs and listFiles
#include "freertos/task.h"
#include "network_manager.h"
#include "messaging.h"
#include "tools_spiffs_utils.h"
#include "bootstate.h"

static const char * TAG = "test_runner";
bool spiffs_started=false;
bool dummycond=false;
static EXT_RAM_ATTR RingbufHandle_t messaging;


static void print_banner(const char* text)
{
    printf("\n#### %s #####\n\n", text);
}

void common_test_init() {
    if(!spiffs_started){
        spiffs_started = true;
        init_spiffs();
        listFiles(spiffs_base_path);
    }


}
void app_main() {
    esp_log_level_set("*", ESP_LOG_DEBUG);
    messaging_service_init();

    bootstate_handle_boot();
    
    init_spiffs();
    if (esp_log_level_get(TAG) >= ESP_LOG_DEBUG) {
        listFiles(spiffs_base_path);
    }

    // Start the network task at this point; this is critical
    // as various subsequent init steps will post events to the queue
    network_initialize_task();

    // also register a subscriber in case we need to check for results in tests
    messaging = messaging_register_subscriber(10, "test_runner");
    

  /* These are the different ways of running registered tests.
     * In practice, only one of them is usually needed.
     *
     * UNITY_BEGIN() and UNITY_END() calls tell Unity to print a summary
     * (number of tests executed/failed/ignored) of tests executed between these calls.
     */

    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();

    print_banner("Starting interactive test menu");
    /* This function will not return, and will be busy waiting for UART input.
     * Make sure that task watchdog is disabled if you use this function.
     */
    unity_run_menu();
}
