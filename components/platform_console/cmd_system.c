/* Console example — various system commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "cmd_system.h"
#include "Config.h"
#include "argtable3/argtable3.h"
#include "bootstate.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_rom_uart.h"
#include "esp_sleep.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "messaging.h"
#include "network_manager.h"
#include "platform_console.h"
#include "platform_esp32.h"
#include "sdkconfig.h"
#include "soc/rtc_cntl_reg.h"
#include "tools.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "tools_spiffs_utils.h"


#if defined(CONFIG_WITH_METRICS)
#include "Metrics.h"
#endif

#ifdef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
#pragma message("Runtime stats enabled")
#define WITH_TASKS_INFO 1
#else
#pragma message("Runtime stats disabled")
#endif
EXT_RAM_ATTR static struct {

    struct arg_str* device;
    // AirPlay device name
    struct arg_str* airplay;
    // Spotify device name
    struct arg_str* spotify;
    // Bluetooth player name advertized
    struct arg_str* bluetooth;
    // Player name reported to the Logitech Media Server
    struct arg_str* squeezelite;
    // Wifi Access Point name
    struct arg_str* wifi_ap_name;
    struct arg_lit* all;
    struct arg_end* end;
} names_args;
EXT_RAM_ATTR static struct {
    struct arg_str* name;
    struct arg_lit*reset;
    struct arg_end* end;
} target_args;

EXT_RAM_ATTR static struct {
    struct arg_str* confirm;
    struct arg_end* end;
} reset_config_args;

// Global or static scope
EXT_RAM_ATTR static struct {
    struct arg_str* path;
    struct arg_end* end;
} ls_args;
// EXT_RAM_ATTR static struct {
//     struct arg_str *component;
//     struct arg_int *level;
//     struct arg_end *end;
// } ls_level;

// Global or static scope
EXT_RAM_ATTR static struct {
    struct arg_str* path;
    struct arg_end* end;
} erase_args;

static const char* TAG = "cmd_system";

static void register_free();
static void register_setdevicename();
static void register_settarget();
static void register_heap();
static void register_dump_heap();
static void register_version();
static void register_restart();
#if CONFIG_WITH_CONFIG_UI
static void register_deep_sleep();
static void register_light_sleep();
#endif
static void register_factory_boot();
static void register_restart_ota();
// static void register_set_services();
#if WITH_TASKS_INFO
static void register_tasks();
#endif
extern BaseType_t network_manager_task;
FILE* system_open_memstream(const char* cmdname, char** buf, size_t* buf_size) {
    FILE* f = open_memstream(buf, buf_size);
    if (f == NULL) {
        cmd_send_messaging(cmdname, MESSAGING_ERROR, "Unable to open memory stream.");
    }
    return f;
}

/* 'version' command */
static int get_version(int argc, char** argv) {
    esp_chip_info_t info;
    esp_chip_info(&info);
    cmd_send_messaging(argv[0], MESSAGING_INFO,
        "IDF Version:%s\r\n"
        "Chip info:\r\n"
        "\tmodel:%s\r\n"
        "\tcores:%d\r\n"
        "\tfeature:%s%s%s%s%d%s\r\n"
        "\trevision number:%d\r\n",
        esp_get_idf_version(), info.model == CHIP_ESP32 ? "ESP32" : "Unknow", info.cores,
        info.features & CHIP_FEATURE_WIFI_BGN ? "/802.11bgn" : "",
        info.features & CHIP_FEATURE_BLE ? "/BLE" : "",
        info.features & CHIP_FEATURE_BT ? "/BT" : "",
        info.features & CHIP_FEATURE_EMB_FLASH ? "/Embedded-Flash:" : "/External-Flash:",
        spi_flash_get_chip_size() / (1024 * 1024), " MB", info.revision);
    return 0;
}

static void register_version() {
    const esp_console_cmd_t cmd = {
        .command = "version",
        .help = "Get version of chip and SDK",
        .hint = NULL,
        .func = &get_version,
    };
    cmd_to_json(&cmd);
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static int restart(int argc, char** argv) {
    simple_restart();
    return 0;
}

static int restart_factory(int argc, char** argv) {
    cmd_send_messaging(argv[0], MESSAGING_WARNING, "Booting to Recovery");
    guided_boot(ESP_PARTITION_SUBTYPE_APP_FACTORY);
    return 0; // return fail.  This should never return... we're rebooting!
}
static int restart_ota(int argc, char** argv) {
    cmd_send_messaging(argv[0], MESSAGING_WARNING, "Booting to Squeezelite");
    guided_boot(ESP_PARTITION_SUBTYPE_APP_OTA_0);
    return 0; // return fail.  This should never return... we're rebooting!
}
static void register_restart() {
    const esp_console_cmd_t cmd = {
        .command = "restart",
        .help = "Reboot system",
        .hint = NULL,
        .func = &restart,
    };
#if CONFIG_WITH_CONFIG_UI
    cmd_to_json(&cmd);
#endif
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
static void register_restart_ota() {
    const esp_console_cmd_t cmd = {
        .command = "restart_ota",
        .help = "Reboot system to Squeezelite",
        .hint = NULL,
        .func = &restart_ota,
    };
#if CONFIG_WITH_CONFIG_UI
    cmd_to_json(&cmd);
#endif
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_factory_boot() {
    const esp_console_cmd_t cmd = {
        .command = "recovery",
        .help = "Reboot system to Recovery",
        .hint = NULL,
        .func = &restart_factory,
    };
#if CONFIG_WITH_CONFIG_UI
    cmd_to_json(&cmd);
#endif
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
/** 'free' command prints available heap memory */

static int free_mem(int argc, char** argv) {
    cmd_send_messaging(argv[0], MESSAGING_INFO, "%d", esp_get_free_heap_size());
    return 0;
}

static void register_free() {
    const esp_console_cmd_t cmd = {
        .command = "free",
        .help = "Get free heap memory",
        .hint = NULL,
        .func = &free_mem,
    };
#if CONFIG_WITH_CONFIG_UI
    cmd_to_json(&cmd);
#endif

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
static int dump_heap(int argc, char** argv) {
    ESP_LOGD(TAG, "Dumping heap");
    heap_caps_dump_all();
    return 0;
}
static int dump_config(int argc, char** argv) {
    ESP_LOGD(TAG, "Dumping config object");
    config_dump_config();
    return 0;
}
/* 'heap' command prints minumum heap size */
static int heap_size(int argc, char** argv) {
    size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t allocated_internal = total_internal - free_internal;

    size_t total_external = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t free_external = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t allocated_external = total_external - free_external;

    size_t total_dma = heap_caps_get_total_size(MALLOC_CAP_DMA);
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t allocated_dma = total_dma - free_dma;

    ESP_LOGI(TAG,
        "\nType      | TOTAL   | ALLOC   | MIN     | LARGEST | FREE    | USED %%| FREE %%\n"
        "Internal  | %7zu | %7zu | %7zu | %7zu | %7zu | %4.1f%% | %4.1f%%\n"
        "External  | %7zu | %7zu | %7zu | %7zu | %7zu | %4.1f%% | %4.1f%%\n"
        "DMA       | %7zu | %7zu | %7zu | %7zu | %7zu | %4.1f%% | %4.1f%%",
        total_internal, allocated_internal, heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), free_internal,
        (allocated_internal * 100.0) / total_internal, (free_internal * 100.0) / total_internal,
        total_external, allocated_external, heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), free_external,
        (allocated_external * 100.0) / total_external, (free_external * 100.0) / total_external,
        total_dma, allocated_dma, heap_caps_get_minimum_free_size(MALLOC_CAP_DMA),
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA), free_dma,
        (allocated_dma * 100.0) / total_dma, (free_dma * 100.0) / total_dma);

    return 0;
}
cJSON* setdevicename_cb() {
    // char * default_host_name = config_alloc_get_str("host_name",NULL,"Squeezelite");
    cJSON* values = cJSON_CreateObject();
    // cJSON_AddStringToObject(values,"name",default_host_name);
    // free(default_host_name);
    // TODO: Add support for the commented code")
    return values;
}
static int setnamevar(char* nvsname, FILE* f, char* value) {
    esp_err_t err = ESP_OK;
    // if((err=config_set_value(NVS_TYPE_STR, nvsname, value))!=ESP_OK){
    // 	fprintf(f,"Unable to set %s=%s. %s\n",nvsname,value,esp_err_to_name(err));
    // }
    // TODO: Add support for the commented code")
    return err == ESP_OK ? 0 : 1;
}
typedef enum { SCANNING, PROCESSING_NAME } scanstate_t;
int set_cspot_player_name(FILE* f, const char* name) {
    int ret = 0;
    // cJSON * cspot_config = config_alloc_get_cjson("cspot_config");
    // if(cspot_config==NULL){
    //     fprintf(f,"Unable to get cspot_config\n");
    //     return 1;
    // }
    // cJSON * player_name = cJSON_GetObjectItemCaseSensitive(cspot_config,"deviceName");
    // if(player_name==NULL){
    //     fprintf(f,"Unable to get deviceName\n");
    //     ret=1;
    // }
    // if(strcmp(player_name->valuestring,name)==0){
    //     fprintf(f,"CSpot device name not changed.\n");
    //     ret=0;
    // }
    // else{
    //     cJSON_SetValuestring(player_name,name);
    //     if(setnamevar("cspot_config",f,cJSON_Print(cspot_config))!=0){
    //         fprintf(f,"Unable to set cspot_config\n");
    //         ret=1;
    //     }
    //     else{
    //         fprintf(f,"CSpot device name set to %s\n",name);
    //     }
    // }
    // cJSON_Delete(cspot_config);
    // TODO: Add support for the commented code")
    return ret;
}
int set_squeezelite_player_name(FILE* f, const char* name) {
    int nerrors = 0;
    // TODO: Add support for the commented code")
    // char * nvs_config= config_alloc_get(NVS_TYPE_STR, "autoexec1");
    // char **argv = NULL;
    // esp_err_t err=ESP_OK;
    // bool bFoundParm=false;
    // scanstate_t state=SCANNING;
    // char * newCommandLine = NULL;
    // char * parm = " -n ";
    // char * cleaned_name = strdup(name);
    // for(char * p=cleaned_name;*p!='\0';p++){
    //     if(*p == ' '){
    //         *p='_'; // no spaces allowed
    //     }
    // }
    // if(nvs_config && strlen(nvs_config)>0){
    //     // allocate enough memory to hold the new command line
    //     size_t cmdLength = strlen(nvs_config) + strlen(cleaned_name) + strlen(parm) +1 ;
    //     newCommandLine = malloc_init_external(cmdLength);
    //     ESP_LOGD(TAG,"Parsing command %s",nvs_config);
    // 	argv = (char **) malloc_init_external(22* sizeof(char *));
    // 	if (argv == NULL) {
    // 		FREE_AND_NULL(nvs_config);
    // 		return 1;
    // 	}
    // 	size_t argc = esp_console_split_argv(nvs_config, argv,22);
    // 	for(int i=0;i<argc;i++) {
    //         if(i>0){
    //             strcat(newCommandLine," ");
    //         }
    //         switch (state)
    //         {
    //         case SCANNING:
    //             strcat(newCommandLine,argv[i]);
    //             if(strcasecmp(argv[i],"--name")==0 || strcasecmp(argv[i],"-n")==0 ){
    //                 state = PROCESSING_NAME;
    //             }
    //             break;
    //         case PROCESSING_NAME:
    //             bFoundParm=true;
    //             strcat(newCommandLine,cleaned_name);
    //             state = SCANNING;
    //             break;

    //         default:
    //             break;
    //         }
    //     }
    //     if(!bFoundParm){
    //         strcat(newCommandLine,parm);
    //         strcat(newCommandLine,name);
    //     }
    //     fprintf(f,"Squeezelite player name changed to %s\n",newCommandLine);
    //     if((err=config_set_value(NVS_TYPE_STR, "autoexec1",newCommandLine))!=ESP_OK){
    //         nerrors++;
    //         fprintf(f,"Failed updating squeezelite command. %s", esp_err_to_name(err));
    //     }

    // }

    // FREE_AND_NULL(nvs_config);
    // FREE_AND_NULL(argv);
    // free(cleaned_name);
    return nerrors;
}

static int do_cat(int argc, char** argv) {
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&ls_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ls_args.end, argv[0]);
        return 1;
    }

    // List files in the provided path
    cat_file(ls_args.path->sval[0]);

    return 0;
}
static int do_ls(int argc, char** argv) {
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&ls_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ls_args.end, argv[0]);
        return 1;
    }

    // List files in the provided path
    listFiles(ls_args.path->sval[0]);

    return 0;
}
// static int do_level(int argc, char **argv)
// {
//     int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr **)&ls_level);
//     if (nerrors != 0) {
//         arg_print_errors(stderr, ls_level.end, argv[0]);
//         return 1;
//     }
//     if(ls_level.component->count >0 && ls_level.level->count == 0){
//         // loop on ls_level.component->sval[i], and call
//     }

//     return 0;
// }
static int do_erase(int argc, char** argv) {
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&erase_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, erase_args.end, argv[0]);
        return 1;
    }

    // Erase file at the provided path
    const char* path = erase_args.path->sval[0];
    if (!erase_path(path, true)) {
        // Handle error if erase_path returns false
        // For example, log an error message
        fprintf(stderr, "Failed to erase file at path: %s\n", path);
        return 1;
    }

    // Optionally, print a success message
    printf("File at path '%s' erased successfully.\n", path);

    return 0;
}

static int do_set_target(int argc, char** argv) {
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&target_args);
    if (nerrors != 0) {
        return 1;
    }
    if (target_args.name->count <= 0) {
        ESP_LOGW(TAG, "Target name must be specified. Current target: %s",
            STR_OR_ALT(platform->target, "N/A"));
        return 1;
    }
    network_async_callback((void*)target_args.name->sval[0], (network_manager_cb_t)(target_args.reset->count>0?config_set_target_reset:config_set_target_no_reset));

    return nerrors;
}

static int do_reset_config(int argc, char** argv) {

    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&reset_config_args);
    if (nerrors != 0) {
        return 1;
    }
    if (reset_config_args.confirm->count <= 0 ||
        strcmp(reset_config_args.confirm->sval[0], "YES") != 0) {
        ESP_LOGW(TAG, "Confirmation needed. Call reset_config YES to confirm");
        return 1;
    }
    set_mac_string();
    // the below call will restart to factory
    config_erase_config();
    return nerrors;
}

static void register_reset_config() {
    reset_config_args.confirm =
        arg_str0(NULL, NULL, "YES", "To execute the reset, confirm with YES");
    reset_config_args.end = arg_end(1);

    const esp_console_cmd_t reset_config = {.command = "reset_config",
        .help = "Reset the configuration and reboot to recovery",
        .hint = NULL,
        .func = &do_reset_config,
        .argtable = &reset_config_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&reset_config));
}

static int setdevicename(int argc, char** argv) {
    bool changed = false;
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&names_args);
    if (nerrors != 0) {
        return 1;
    }
    if (names_args.device->count > 0) {
        changed = changed | system_set_string(&sys_names_config_msg, sys_names_config_device_tag,
                                &platform->names, names_args.device->sval[0]);
    } else {
        ESP_LOGE(TAG, "Device name must be specified");
        return 1;
    }
    if (names_args.airplay->count > 0) {
        changed = changed | system_set_string(&sys_names_config_msg, sys_names_config_airplay_tag,
                                &platform->names, names_args.airplay->sval[0]);
    }

    if (names_args.bluetooth->count > 0) {
        changed = changed | system_set_string(&sys_names_config_msg, sys_names_config_bluetooth_tag,
                                &platform->names, names_args.bluetooth->sval[0]);
    }
    if (names_args.spotify->count > 0) {
        changed = changed | system_set_string(&sys_names_config_msg, sys_names_config_spotify_tag,
                                &platform->names, names_args.spotify->sval[0]);
    }
    if (names_args.squeezelite->count > 0) {
        changed = changed | system_set_string(&sys_names_config_msg, sys_names_config_squeezelite_tag,
                                &platform->names, names_args.squeezelite->sval[0]);
    }
    if (names_args.wifi_ap_name->count > 0) {
        changed = changed | system_set_string(&sys_names_config_msg, sys_names_config_wifi_ap_name_tag,
                                &platform->names, names_args.wifi_ap_name->sval[0]);
    }
    if (names_args.all->count > 0) {
        ESP_LOGI(TAG, "Setting all names to %s", platform->names.device);
        changed = changed | system_set_string(&sys_names_config_msg, sys_names_config_airplay_tag,
                                &platform->names, platform->names.device);
        changed = changed | system_set_string(&sys_names_config_msg, sys_names_config_bluetooth_tag,
                                &platform->names, platform->names.device);
        changed = changed | system_set_string(&sys_names_config_msg, sys_names_config_spotify_tag,
                                &platform->names, platform->names.device);
        changed = changed | system_set_string(&sys_names_config_msg, sys_names_config_squeezelite_tag,
                                &platform->names, platform->names.device);
        changed = changed | system_set_string(&sys_names_config_msg, sys_names_config_wifi_ap_name_tag,
                                &platform->names, platform->names.device);
    }
    if (changed) {
        ESP_LOGI(TAG, "Found change(s). Saving");
        config_raise_changed(false);
    } else {
        ESP_LOGW(TAG, "No change detected.");
    }

    return nerrors;
}

static void register_heap() {
    const esp_console_cmd_t heap_cmd = {
        .command = "heap",
        .help = "Get minimum size of free heap memory",
        .hint = NULL,
        .func = &heap_size,
    };
#if CONFIG_WITH_CONFIG_UI
    cmd_to_json(&heap_cmd);
#endif
    ESP_ERROR_CHECK(esp_console_cmd_register(&heap_cmd));
}

static void register_dump_heap() {
    const esp_console_cmd_t heap_cmd = {
        .command = "dump_heap",
        .help = "Dumps the content of the heap to serial output",
        .hint = NULL,
        .func = &dump_heap,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&heap_cmd));
}
static void register_dump_config() {
    const esp_console_cmd_t cmd = {
        .command = "dump_config",
        .help = "Dumps the content of the configuration object",
        .hint = NULL,
        .func = &dump_config,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
static void register_settarget() {
    target_args.name = arg_str0(NULL, NULL, STR_OR_BLANK(platform->target), "New Target Name");
    target_args.reset = arg_lit0("r","reset","Full reset before setting target (recommended)");
    target_args.end = arg_end(1);
    const esp_console_cmd_t set_target = {.command = "target",
        .help = "Set the device target Name (SqueezeAMP, SqueezeIO, Muse, etc. )",
        .hint = NULL,
        .func = &do_set_target,
        .argtable = &target_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&set_target));
}

// static void register_loglevel()
// {
//     ls_level.component = arg_str1(NULL, NULL, "<component>", "Component name for which to set log
//     level. * for all components"); ls_level.level = arg_str1(NULL, NULL, "<level: 0..5>",
//     "NONE=0, ERROR=1, WARN=2, INFO=3, DEBUG=4, VERBOSE=5"); ls_args.end = arg_end(1); const
//     esp_console_cmd_t ls_cmd = {
//         .command = "ls",
//         .help = "List files in a path",
//         .hint = NULL,
//         .func = &do_level,
//         .argtable = &ls_level
//     };

//     ESP_ERROR_CHECK(esp_console_cmd_register(&ls_cmd));
// }

static void register_ls() {
    ls_args.path = arg_str1(NULL, NULL, "<path>", "Path to list files");
    ls_args.end = arg_end(1);
    const esp_console_cmd_t ls_cmd = {.command = "ls",
        .help = "List files in a path",
        .hint = NULL,
        .func = &do_ls,
        .argtable = &ls_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ls_cmd));
}
static void register_cat() {
    ls_args.path = arg_str1(NULL, NULL, "<path>", "File name to cat");
    ls_args.end = arg_end(1);
    const esp_console_cmd_t cat_cmd = {.command = "cat",
        .help = "Dumps the content of a file to the console",
        .hint = NULL,
        .func = &do_cat,
        .argtable = &ls_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&cat_cmd));
}

static void register_erase() {
    erase_args.path = arg_str1(NULL, NULL, "<path>", "Path of the file(s) to erase");
    erase_args.end = arg_end(1);
    const esp_console_cmd_t erase_cmd = {.command = "erase",
        .help = "Erase file(s) at a given path",
        .hint = NULL,
        .func = &do_erase,
        .argtable = &erase_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&erase_cmd));
}

static void register_setdevicename() {
    char* default_host_name = platform->names.device;
    names_args.device = arg_str0("n", "device", default_host_name, "New Name");
    names_args.airplay = arg_str0("a", "airplay", default_host_name, "New Airplay Device Name");
    names_args.bluetooth = arg_str0("b", "bt", default_host_name, "New Bluetooth Device Name");
    names_args.spotify = arg_str0("s", "spotify", default_host_name, "New Spotify Device Name");
    names_args.squeezelite =
        arg_str0("l", "squeezelite", default_host_name, "New Squeezelite Player Name");
    names_args.wifi_ap_name = arg_str0("w", "wifiap", default_host_name, "New Wifi AP Name");
    names_args.all = arg_lit0(NULL, "all", "Set all names to device name");
    names_args.end = arg_end(2);
    const esp_console_cmd_t set_name = {.command = CFG_TYPE_SYST("name"),
        .help = "Device Name",
        .hint = NULL,
        .func = &setdevicename,
        .argtable = &names_args};

    ESP_ERROR_CHECK(esp_console_cmd_register(&set_name));
}
/** 'tasks' command prints the list of tasks and related information */
#if WITH_TASKS_INFO

static int tasks_info(int argc, char** argv) {
    const size_t bytes_per_task = 40; /* see vTaskList description */
    char* task_list_buffer = malloc_init_external(uxTaskGetNumberOfTasks() * bytes_per_task);
    if (task_list_buffer == NULL) {
        cmd_send_messaging(
            argv[0], MESSAGING_ERROR, "failed to allocate buffer for vTaskList output");
        return 1;
    }
    cmd_send_messaging(argv[0], MESSAGING_INFO,
        "Task Name\tStatus\tPrio\tHWM\tTask#"
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
        "\tAffinity"
#endif
        "\n");
    vTaskList(task_list_buffer);
    cmd_send_messaging(argv[0], MESSAGING_INFO, "%s", task_list_buffer);
    free(task_list_buffer);
    return 0;
}

static void register_tasks() {
    const esp_console_cmd_t cmd = {
        .command = "tasks",
        .help = "Get information about running tasks",
        .hint = NULL,
        .func = &tasks_info,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

#endif // WITH_TASKS_INFO

/** 'deep_sleep' command puts the chip into deep sleep mode */

#if CONFIG_WITH_CONFIG_UI
static struct {
    struct arg_int* wakeup_time;
    struct arg_int* wakeup_gpio_num;
    struct arg_int* wakeup_gpio_level;
    struct arg_end* end;
} deep_sleep_args;

static int deep_sleep(int argc, char** argv) {
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&deep_sleep_args);
    if (nerrors != 0) {
        return 1;
    }
    if (deep_sleep_args.wakeup_time->count) {
        uint64_t timeout = 1000ULL * deep_sleep_args.wakeup_time->ival[0];
        cmd_send_messaging(
            argv[0], MESSAGING_INFO, "Enabling timer wakeup, timeout=%lluus", timeout);
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(timeout));
    }
    if (deep_sleep_args.wakeup_gpio_num->count) {
        int io_num = deep_sleep_args.wakeup_gpio_num->ival[0];
        if (!rtc_gpio_is_valid_gpio(io_num)) {
            cmd_send_messaging(argv[0], MESSAGING_ERROR, "GPIO %d is not an RTC IO", io_num);
            return 1;
        }
        int level = 0;
        if (deep_sleep_args.wakeup_gpio_level->count) {
            level = deep_sleep_args.wakeup_gpio_level->ival[0];
            if (level != 0 && level != 1) {
                cmd_send_messaging(argv[0], MESSAGING_ERROR, "Invalid wakeup level: %d", level);
                return 1;
            }
        }
        cmd_send_messaging(argv[0], MESSAGING_INFO, "Enabling wakeup on GPIO%d, wakeup on %s level",
            io_num, level ? "HIGH" : "LOW");

        ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(1ULL << io_num, level));
    }
    rtc_gpio_isolate(GPIO_NUM_12);
    esp_deep_sleep_start();
    return 0; // this code will never run. deep sleep will cause the system to restart
}

static void register_deep_sleep() {
    deep_sleep_args.wakeup_time = arg_int0("t", "time", "<t>", "Wake up time, ms");
    deep_sleep_args.wakeup_gpio_num =
        arg_int0(NULL, "io", "<n>", "If specified, wakeup using GPIO with given number");
    deep_sleep_args.wakeup_gpio_level =
        arg_int0(NULL, "io_level", "<0|1>", "GPIO level to trigger wakeup");
    deep_sleep_args.end = arg_end(3);

    const esp_console_cmd_t cmd = {.command = "deep_sleep",
        .help = "Enter deep sleep mode. ",
        .hint = NULL,
        .func = &deep_sleep,
        .argtable = &deep_sleep_args};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
#endif

static int enable_disable(FILE* f, char* nvs_name, struct arg_lit* arg) {
    esp_err_t err = ESP_OK;
    // err= config_set_value(NVS_TYPE_STR, nvs_name, arg->count>0?"Y":"N");
    // const char * name = arg->hdr.longopts?arg->hdr.longopts:arg->hdr.glossary;

    // if(err!=ESP_OK){
    // 	fprintf(f,"Error %s %s. %s\n",arg->count>0?"Enabling":"Disabling", name,
    // esp_err_to_name(err));
    // }
    // else {
    // 	fprintf(f,"%s %s\n",arg->count>0?"Enabled":"Disabled",name);
    // }
    return err;
}

// static int do_set_services(int argc, char **argv)
// {
//     esp_err_t err = ESP_OK;
//     int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&set_services_args);
//     if (nerrors != 0) {
//         return 1;
//     }
// 	char *buf = NULL;
// 	size_t buf_size = 0;
// 	FILE *f = system_open_memstream(argv[0],&buf, &buf_size);
// 	if (f == NULL) {
// 		return 1;
// 	}

// 	nerrors += enable_disable(f,"enable_airplay",set_services_args.airplay);
// 	nerrors += enable_disable(f,"enable_bt_sink",set_services_args.btspeaker);
//     #if CONFIG_CSPOT_SINK
//     nerrors += enable_disable(f,"enable_cspot",set_services_args.cspot);
//     #endif

//     if(set_services_args.telnet->count>0){
//         // if(strcasecmp(set_services_args.telnet->sval[0],"Disabled") == 0){
//         //     err = config_set_value(NVS_TYPE_STR, "telnet_enable", "N");
//         // }
//         // else if(strcasecmp(set_services_args.telnet->sval[0],"Telnet Only") == 0){
//         //     err = config_set_value(NVS_TYPE_STR, "telnet_enable", "Y");
//         // }
//         // else if(strcasecmp(set_services_args.telnet->sval[0],"Telnet and Serial") == 0){
//         //     err = config_set_value(NVS_TYPE_STR, "telnet_enable", "D");
//         // }
//         // TODO: Add support for the commented code")
//         if(err!=ESP_OK){
//             nerrors++;
//             fprintf(f,"Error setting telnet to %s. %s\n",set_services_args.telnet->sval[0],
//             esp_err_to_name(err));
//         }
//         else {
//             fprintf(f,"Telnet service changed to %s\n",set_services_args.telnet->sval[0]);
//         }
//     }

// #if WITH_TASKS_INFO
// 	nerrors += enable_disable(f,"stats",set_services_args.stats);
// #endif
// 	if(!nerrors ){
// 		fprintf(f,"Done.\n");
// 	}
// 	fflush (f);
// 	cmd_send_messaging(argv[0],nerrors>0?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
// 	fclose(f);
// 	FREE_AND_NULL(buf);
// 	return nerrors;
// }

// cJSON * set_services_cb(){
// 	cJSON * values = cJSON_CreateObject();
// 	char * p=NULL;
//     console_set_bool_parameter(values,"enable_bt_sink",set_services_args.btspeaker);
//     console_set_bool_parameter(values,"enable_airplay",set_services_args.airplay);
//     #if CONFIG_CSPOT_SINK
//     console_set_bool_parameter(values,"enable_cspot",set_services_args.cspot);
//     #endif
//     #if WITH_TASKS_INFO
//     console_set_bool_parameter(values,"stats",set_services_args.stats);
//     #endif
// // 	if ((p = config_alloc_get(NVS_TYPE_STR, "telnet_enable")) != NULL) {
// //         if(strcasestr("YX",p)!=NULL){
// // 		    cJSON_AddStringToObject(values,set_services_args.telnet->hdr.longopts,"Telnet
// Only");
// //         }
// //         else if(strcasestr("D",p)!=NULL){
// //             cJSON_AddStringToObject(values,set_services_args.telnet->hdr.longopts,"Telnet and
// Serial");
// //         }
// //         else {
// //             cJSON_AddStringToObject(values,set_services_args.telnet->hdr.longopts,"Disabled");
// //         }
// // #if defined(CONFIG_WITH_METRICS)
// //         metrics_add_feature_variant("telnet",p);
// // #endif
// // 		FREE_AND_NULL(p);
// // 	}
// // TODO: Add support for the commented code")
// 	return values;
// }

// static void register_set_services(){
// 	set_services_args.airplay = arg_lit0(NULL, "AirPlay", "AirPlay");
//     #if CONFIG_CSPOT_SINK
//     set_services_args.cspot = arg_lit0(NULL, "cspot", "Spotify (cspot)");
//     #endif
// 	set_services_args.btspeaker = arg_lit0(NULL, "BT_Speaker", "Bluetooth Speaker");
// 	set_services_args.telnet= arg_str0("t", "telnet","Disabled|Telnet Only|Telnet and
// Serial","Telnet server. Use only for troubleshooting"); #if WITH_TASKS_INFO
// 	set_services_args.stats= arg_lit0(NULL, "stats", "System Statistics. Use only for
// troubleshooting"); #endif
//     set_services_args.end=arg_end(2);
// 	const esp_console_cmd_t cmd = {
//         .command = CFG_TYPE_SYST("services"),
//         .help = "Services",
// 		.argtable = &set_services_args,
//         .hint = NULL,
//         .func = &do_set_services,
//     };
// 	cmd_to_json_with_cb(&cmd,&set_services_cb);
//     ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
// }

#if CONFIG_WITH_CONFIG_UI
static struct {
    struct arg_int* wakeup_time;
    struct arg_int* wakeup_gpio_num;
    struct arg_int* wakeup_gpio_level;
    struct arg_end* end;
} light_sleep_args;

static int light_sleep(int argc, char** argv) {
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&light_sleep_args);
    if (nerrors != 0) {
        return 1;
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (light_sleep_args.wakeup_time->count) {
        uint64_t timeout = 1000ULL * light_sleep_args.wakeup_time->ival[0];
        cmd_send_messaging(
            argv[0], MESSAGING_INFO, "Enabling timer wakeup, timeout=%lluus", timeout);
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(timeout));
    }
    int io_count = light_sleep_args.wakeup_gpio_num->count;
    if (io_count != light_sleep_args.wakeup_gpio_level->count) {
        cmd_send_messaging(
            argv[0], MESSAGING_INFO, "Should have same number of 'io' and 'io_level' arguments");
        return 1;
    }
    for (int i = 0; i < io_count; ++i) {
        int io_num = light_sleep_args.wakeup_gpio_num->ival[i];
        int level = light_sleep_args.wakeup_gpio_level->ival[i];
        if (level != 0 && level != 1) {
            cmd_send_messaging(argv[0], MESSAGING_ERROR, "Invalid wakeup level: %d", level);
            return 1;
        }
        cmd_send_messaging(argv[0], MESSAGING_INFO, "Enabling wakeup on GPIO%d, wakeup on %s level",
            io_num, level ? "HIGH" : "LOW");

        ESP_ERROR_CHECK(
            gpio_wakeup_enable(io_num, level ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL));
    }
    if (io_count > 0) {
        ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    }
    if (CONFIG_ESP_CONSOLE_UART_NUM <= UART_NUM_1) {
        cmd_send_messaging(
            argv[0], MESSAGING_INFO, "Enabling UART wakeup (press ENTER to exit light sleep)");
        ESP_ERROR_CHECK(uart_set_wakeup_threshold(CONFIG_ESP_CONSOLE_UART_NUM, 3));
        ESP_ERROR_CHECK(esp_sleep_enable_uart_wakeup(CONFIG_ESP_CONSOLE_UART_NUM));
    }
    fflush(stdout);
    esp_rom_uart_tx_wait_idle(CONFIG_ESP_CONSOLE_UART_NUM);
    esp_light_sleep_start();
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const char* cause_str;
    switch (cause) {
    case ESP_SLEEP_WAKEUP_GPIO:
        cause_str = "GPIO";
        break;
    case ESP_SLEEP_WAKEUP_UART:
        cause_str = "UART";
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        cause_str = "timer";
        break;
    default:
        cause_str = "unknown";
        printf("%d\n", cause);
    }
    cmd_send_messaging(argv[0], MESSAGING_INFO, "Woke up from: %s", cause_str);
    return 0;
}

static void register_light_sleep() {
    light_sleep_args.wakeup_time = arg_int0("t", "time", "<t>", "Wake up time, ms");
    light_sleep_args.wakeup_gpio_num =
        arg_intn(NULL, "io", "<n>", 0, 8, "If specified, wakeup using GPIO with given number");
    light_sleep_args.wakeup_gpio_level =
        arg_intn(NULL, "io_level", "<0|1>", 0, 8, "GPIO level to trigger wakeup");
    light_sleep_args.end = arg_end(3);

    const esp_console_cmd_t cmd = {.command = "light_sleep",
        .help = "Enter light sleep mode. "
                "Two wakeup modes are supported: timer and GPIO. "
                "Multiple GPIO pins can be specified using pairs of "
                "'io' and 'io_level' arguments. "
                "Will also wake up on UART input.",
        .hint = NULL,
        .func = &light_sleep,
        .argtable = &light_sleep_args};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
#endif

void register_system() {

    // register_set_services();
    // register_loglevel();
    register_reset_config();
    register_setdevicename();
    register_free();
    register_heap();
    register_dump_heap();
    register_version();
    register_restart();
    register_factory_boot();
    register_restart_ota();
    register_ls();
    register_cat();
    register_settarget();
    register_erase();
    register_dump_config();
#if WITH_TASKS_INFO
    register_tasks();
#endif
#if CONFIG_WITH_CONFIG_UI
    register_deep_sleep();
    register_light_sleep();
#endif
}