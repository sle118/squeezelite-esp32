/* Console example — various system commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "cmd_ota.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_spi_flash.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "sdkconfig.h"
#include "platform_console.h"
#include "messaging.h"

static const char* TAG = "ota";
extern esp_err_t start_ota(const char* bin_url);
static struct {
    struct arg_str* url;
    struct arg_end* end;
} ota_args;
/* 'heap' command prints minumum heap size */
static int perform_ota_update(int argc, char** argv) {
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&ota_args);
    if(nerrors != 0) { return 1; }

    const char* url = ota_args.url->sval[0];

    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Starting ota: %s", url);
    start_ota(url);

    if(err != ESP_OK) {
        ESP_LOGE(TAG, "%s", esp_err_to_name(err));
        return 1;
    }

    return 0;
}

void register_ota_cmd() {
    ota_args.url = arg_str1(NULL, NULL, "<url>", "url of the binary app file");
    ota_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {.command = "update", .help = "Update from URL", .hint = NULL, .func = &perform_ota_update, .argtable = &ota_args};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
