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
#include <stdlib.h>
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
#include "ota_check.h"

static const char * TAG = "ota";
extern esp_err_t start_ota(const char * bin_url, char * bin_buffer, uint32_t length);
static struct {
    struct arg_str *url;
    struct arg_end *end;
} ota_args;
/* 'heap' command prints minumum heap size */
static int perform_ota_update(int argc, char **argv)
{
	int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&ota_args);
    if (nerrors != 0) {
        return 1;
    }

    const char *url = ota_args.url->sval[0];

    esp_err_t err=ESP_OK;
    ESP_LOGI(TAG, "Starting ota: %s", url);
    err = start_ota(url, NULL, 0);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s", esp_err_to_name(err));
        return 1;
    }

    return 0;
}

static int perform_ota_check(int argc, char **argv)
{
    (void)argc; (void)argv;
    char *js = NULL;
    esp_err_t err = ota_check_notify(&js);
    if (js) {
        printf("%s\n", js);
        ESP_LOGI(TAG, "ota_check: %s", js);
        free(js);
    }
    if (err == ESP_ERR_NOT_FOUND) return 0; // up to date
    return err == ESP_OK ? 0 : 1;
}

static struct {
    struct arg_str *url;
    struct arg_end *end;
} ota_flash_args;

static int perform_ota_flash(int argc, char **argv)
{
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr **)&ota_flash_args);
    if (nerrors != 0) return 1;
    esp_err_t err = ota_flash_deferred(ota_flash_args.url->sval[0]);
    if (err == ESP_ERR_INVALID_STATE) {
        printf("Flash deferred (notify-only mode). Set ota_allow_flash=1 to enable.\n");
        return 2;
    }
    return err == ESP_OK ? 0 : 1;
}

 void register_ota_cmd()
{
	 ota_args.url= arg_str1(NULL, NULL, "<url>", "url of the binary app file");
	 ota_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "update",
        .help = "Updates the application binary from the provided URL",
        .hint = NULL,
        .func = &perform_ota_update,
        .argtable = &ota_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );

    const esp_console_cmd_t check_cmd = {
        .command = "ota_check",
        .help = "Notify-only: check GitHub releases for newer firmware of stored variant (no flash). MQTT+LMS notify.",
        .hint = NULL,
        .func = &perform_ota_check,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&check_cmd) );

    ota_flash_args.url = arg_str1(NULL, NULL, "<url>", "firmware binary URL (deferred: needs ota_allow_flash=1)");
    ota_flash_args.end = arg_end(2);
    const esp_console_cmd_t flash_cmd = {
        .command = "ota_flash",
        .help = "Deferred external flash hook (notify-only by default).",
        .hint = NULL,
        .func = &perform_ota_flash,
        .argtable = &ota_flash_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&flash_cmd) );

    // Expose to the web UI command list (/commands.json -> Execute cards).
    cmd_to_json(&cmd);
    cmd_to_json(&check_cmd);
    cmd_to_json(&flash_cmd);
}



