#include "../cmd_system.h"
#include "Config.h"
#include "application_name.h"
#include "argtable3/argtable3.h"
#include "esp_app_format.h"
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "messaging.h"
#include "platform_esp32.h"
#include "pthread.h"
#include "tools.h"
#include <stdio.h>
#include <string.h>
#include "bootstate.h"

extern esp_err_t process_recovery_ota(const char* bin_url, char* bin_buffer, uint32_t length);
extern int squeezelite_main_start();
extern sys_dac_default_sets* default_dac_sets;


static const char* TAG = "squeezelite_cmd";
#define SQUEEZELITE_THREAD_STACK_SIZE (8 * 1024)
#define ADDITIONAL_SQUEEZELITE_ARGS 5

const __attribute__((section(".rodata_desc"))) esp_app_desc_t esp_app_desc = {

    .magic_word = ESP_APP_DESC_MAGIC_WORD,
    .version = PROJECT_VER,
    .project_name = CONFIG_PROJECT_NAME,
    .idf_ver = IDF_VER,

#ifdef CONFIG_BOOTLOADER_APP_SECURE_VERSION
    .secure_version = CONFIG_BOOTLOADER_APP_SECURE_VERSION,
#else
    .secure_version = 0,
#endif

#ifdef CONFIG_APP_COMPILE_TIME_DATE
    .time = __TIME__,
    .date = __DATE__,
#else
    .time = "",
    .date = "",
#endif
};

extern void register_audio_config(void);
// extern void register_rotary_config(void);
extern void register_nvs();
extern cJSON* get_gpio_list_handler(bool refresh);
cJSON* get_gpio_list(bool refresh) {
#if CONFIG_WITH_CONFIG_UI
    return get_gpio_list_handler(refresh);
#else
    return cJSON_CreateArray();
#endif
}
void register_optional_cmd(void) {
    // #if CONFIG_WITH_CONFIG_UI
    //     register_rotary_config();
    // #endif
    register_audio_config();
    // register_ledvu_config();
}

static struct {
    int argc;
    char** argv;
} thread_parms;

static void squeezelite_thread(void* arg) {
    ESP_LOGI(TAG, "Calling squeezelite");
    int wait = 60;
    int ret = squeezelite_main_start();

    if (ret == -2) {
        cmd_send_messaging("cfg-audio-tmpl", MESSAGING_ERROR,
            "Squeezelite not configured. Rebooting to factory in %d sec\n", wait);
        vTaskDelay(pdMS_TO_TICKS(wait * 1000));
        guided_factory();
    } else {
        cmd_send_messaging("cfg-audio-tmpl", ret > 1 ? MESSAGING_ERROR : MESSAGING_WARNING,
            "squeezelite exited with error code %d\n", ret);
        if (ret <= 1) {

            cmd_send_messaging(
                "cfg-audio-tmpl", MESSAGING_ERROR, "Rebooting to factory in %d sec\n", wait);
            vTaskDelay(pdMS_TO_TICKS(wait * 1000));
            guided_factory();
        } else {

            cmd_send_messaging(
                "cfg-audio-tmpl", MESSAGING_ERROR, "Correct command line and reboot\n");
            vTaskSuspend(NULL);
        }
    }

    ESP_LOGV(TAG, "Exited from squeezelite's main(). Freeing argv structure.");

    for (int i = 0; i < thread_parms.argc; i++)
        free(thread_parms.argv[i]);
    free(thread_parms.argv);
}

static int launchsqueezelite(int argc, char** argv) {
    static DRAM_ATTR StaticTask_t xTaskBuffer __attribute__((aligned(4)));
    static EXT_RAM_ATTR StackType_t xStack[SQUEEZELITE_THREAD_STACK_SIZE]
        __attribute__((aligned(4)));
    static bool isRunning = false;

    if (isRunning) {
        ESP_LOGE(TAG, "Squeezelite already running. Exiting!");
        return -1;
    }

    ESP_LOGV(TAG, "Begin");
    isRunning = true;

    // load the presets here, as the squeezelite cannot access the 
    // spiffs storage
    // proto_load_file("/spiffs/presets/default_sets.bin", &sys_dac_default_sets_msg, default_dac_sets, false);

    ESP_LOGD(TAG, "Starting Squeezelite Thread");
    xTaskCreateStaticPinnedToCore(squeezelite_thread, "squeezelite", SQUEEZELITE_THREAD_STACK_SIZE,
        NULL, CONFIG_ESP32_PTHREAD_TASK_PRIO_DEFAULT, xStack, &xTaskBuffer,
        CONFIG_PTHREAD_TASK_CORE_DEFAULT);

    return 0;
}
void start_squeezelite() { launchsqueezelite(0, NULL); }
// void register_squeezelite() {
// 	squeezelite_args.parameters = arg_str0(NULL, NULL, "<parms>", "command line for squeezelite. -h
// for help, --defaults to launch with default values."); 	squeezelite_args.end = arg_end(1); 	const
// esp_console_cmd_t launch_squeezelite = { 		.command = "squeezelite", 		.help = "Starts squeezelite",
// 		.hint = NULL,
// 		.func = &launchsqueezelite,
// 		.argtable = &squeezelite_args
// 	};
// 	ESP_ERROR_CHECK( esp_console_cmd_register(&launch_squeezelite) );
// }

esp_err_t start_ota(const char* bin_url, char* bin_buffer, uint32_t length) {
    if (!bin_url || strlen(bin_url) == 0) {
        ESP_LOGE(TAG, "missing URL parameter. Unable to start OTA");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGW(TAG, "Called to update the firmware from url: %s", bin_url);
    system_set_string(&sys_state_data_msg, sys_state_data_ota_url_tag, sys_state, bin_url);
    config_raise_state_changed();

    if (!config_waitcommit()) {
        ESP_LOGW(TAG, "Unable to commit configuration. ");
    }
    ESP_LOGW(TAG, "Rebooting to recovery to complete the installation");
    return guided_factory();
    return ESP_OK;
}
