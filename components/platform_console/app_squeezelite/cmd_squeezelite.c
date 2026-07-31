#include <stdio.h>
#include <string.h>
#include "application_name.h"
#include "esp_log.h"
#include "esp_console.h"
#include "../cmd_system.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "pthread.h"
#include "platform_esp32.h"
#include "platform_config.h"
#include "esp_app_format.h"
#include "tools.h"
#include "messaging.h"

extern esp_err_t process_recovery_ota(const char * bin_url, char * bin_buffer, uint32_t length);
static const char * TAG = "squeezelite_cmd";
#define SQUEEZELITE_THREAD_STACK_SIZE (8*1024)

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
extern void register_rotary_config(void);
extern void register_ledvu_config(void);
extern void register_nvs();

void register_optional_cmd(void) {
    #if CONFIG_WITH_CONFIG_UI	
    register_rotary_config();
#endif
    register_audio_config();
	register_ledvu_config();
	register_nvs();
}    

extern int squeezelite_main(int argc, char **argv);

static int launchsqueezelite(int argc, char **argv);

/** Arguments used by 'squeezelite' function */
static struct {
    struct arg_str *parameters;
    struct arg_end *end;
} squeezelite_args;
static struct {
	int argc;
	char ** argv;
} thread_parms ;

#define ADDITIONAL_SQUEEZELITE_ARGS 5
static void squeezelite_thread(void *arg){  
	ESP_LOGV(TAG ,"Number of args received: %u",thread_parms.argc );
	ESP_LOGV(TAG ,"Values:");
    for(int i = 0;i<thread_parms.argc; i++){
    	ESP_LOGV(TAG ,"     %s",thread_parms.argv[i]);
    }
    ESP_LOGI(TAG ,"Calling squeezelite");
    int ret = squeezelite_main(thread_parms.argc, thread_parms.argv);
        
    cmd_send_messaging("cfg-audio-tmpl",ret > 1 ?  MESSAGING_ERROR : MESSAGING_WARNING,"squeezelite exited with error code %d\n", ret);

    if (ret <= 1) {
        wait_for_commit();
        cmd_send_messaging("cfg-audio-tmpl",MESSAGING_WARNING,"Squeezelite exited (no server). Other sinks still active.\n");
        // Don't reboot - AirPlay/Spotify/BT sinks run independently
        vTaskSuspend(NULL);
    } else {
		cmd_send_messaging("cfg-audio-tmpl",MESSAGING_ERROR,"Correct command line and reboot\n");
        vTaskSuspend(NULL);
    }

	ESP_LOGV(TAG, "Exited from squeezelite's main(). Freeing argv structure.");

	for(int i=0;i<thread_parms.argc;i++) free(thread_parms.argv[i]);
	free(thread_parms.argv);
}

static int launchsqueezelite(int argc, char **argv) {
	static DRAM_ATTR StaticTask_t xTaskBuffer __attribute__ ((aligned (4)));
	static EXT_RAM_ATTR StackType_t xStack[SQUEEZELITE_THREAD_STACK_SIZE] __attribute__ ((aligned (4)));
	static bool isRunning = false;

	if (isRunning) {
		ESP_LOGE(TAG,"Squeezelite already running. Exiting!");
		return -1;
	}
	
	ESP_LOGV(TAG ,"Begin");
	isRunning = true;

	ESP_LOGV(TAG, "Parameters:");
    for(int i = 0;i<argc; i++){
    	ESP_LOGV(TAG, "     %s",argv[i]);
    }
    ESP_LOGV(TAG,"Saving args in thread structure");

    thread_parms.argc=0;
    thread_parms.argv = malloc_init_external(sizeof(char**)*(argc+ADDITIONAL_SQUEEZELITE_ARGS));

	for(int i=0;i<argc;i++){
		ESP_LOGD(TAG ,"assigning parm %u : %s",i,argv[i]);
		thread_parms.argv[thread_parms.argc++]=strdup(argv[i]);
	}

	if(argc==1){
		// There isn't a default configuration that would actually work
		// if no parameter is passed.
		ESP_LOGV(TAG ,"Adding argv value at %u. Prev value: %s",thread_parms.argc,thread_parms.argv[thread_parms.argc-1]);
		thread_parms.argv[thread_parms.argc++]=strdup("-?");
	}

	ESP_LOGD(TAG,"Starting Squeezelite Thread");
	xTaskCreateStaticPinnedToCore(squeezelite_thread, "squeezelite", SQUEEZELITE_THREAD_STACK_SIZE, 
					  NULL, CONFIG_ESP32_PTHREAD_TASK_PRIO_DEFAULT, xStack, &xTaskBuffer, CONFIG_PTHREAD_TASK_CORE_DEFAULT);
	ESP_LOGD(TAG ,"Back to console thread!");

    return 0;
}

void register_squeezelite() {
	squeezelite_args.parameters = arg_str0(NULL, NULL, "<parms>", "command line for squeezelite. -h for help, --defaults to launch with default values.");
	squeezelite_args.end = arg_end(1);
	const esp_console_cmd_t launch_squeezelite = {
		.command = "squeezelite",
		.help = "Starts squeezelite",
		.hint = NULL,
		.func = &launchsqueezelite,
		.argtable = &squeezelite_args
	};
	ESP_ERROR_CHECK( esp_console_cmd_register(&launch_squeezelite) );
}

esp_err_t start_ota(const char * bin_url, char * bin_buffer, uint32_t length) {
	if(!bin_url){
		ESP_LOGE(TAG,"missing URL parameter. Unable to start OTA");
		return ESP_ERR_INVALID_ARG;
	}
	ESP_LOGW(TAG, "Called to update the firmware from url: %s",bin_url);
	if(config_set_value(NVS_TYPE_STR, "fwurl", bin_url) != ESP_OK){
		ESP_LOGE(TAG,"Failed to save the OTA url into nvs cache");
		return ESP_FAIL;
	}

	if(!wait_for_commit()){
		ESP_LOGW(TAG,"Unable to commit configuration. ");
	}

	ESP_LOGW(TAG, "Rebooting to recovery to complete the installation");
	return guided_factory();
	return ESP_OK;
}
