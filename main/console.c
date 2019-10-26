/* Console example

 This example code is in the Public Domain (or CC0 licensed, at your option.)

 Unless required by applicable law or agreed to in writing, this
 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 CONDITIONS OF ANY KIND, either express or implied.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pthread.h"
#include "platform_esp32.h"
#include "esp_pthread.h"
#include "cmd_decl.h"
#include "console.h"
#include "wifi_manager.h"

#include "cmd_squeezelite.h"
#include "nvs_utilities.h"
pthread_t thread_console;
static void * console_thread();
void console_start();
static const char * TAG = "console";



/* Prompt to be printed before each line.
 * This can be customized, made dynamic, etc.
 */
const char* prompt = LOG_COLOR_I "squeezelite-esp32> " LOG_RESET_COLOR;

/* Console command history can be stored to and loaded from a file.
 * The easiest way to do this is to use FATFS filesystem on top of
 * wear_levelling library.
 */

#define MOUNT_PATH "/data"
#define HISTORY_PATH MOUNT_PATH "/history.txt"
void run_command(char * line);

void process_autoexec(){
	int i=1;
	char autoexec_name[21]={0};
	char * autoexec_value=NULL;
	uint8_t autoexec_flag=0;

	char * str_flag = get_nvs_value_alloc(NVS_TYPE_STR, "autoexec");

	if(str_flag !=NULL ){
		autoexec_flag=atoi(str_flag);
		ESP_LOGI(TAG,"autoexec flag value found with value %u, from string value: %s", autoexec_flag, str_flag);
		if(autoexec_flag == 1) {
			do {
				snprintf(autoexec_name,sizeof(autoexec_name)-1,"autoexec%u",i++);
				ESP_LOGD(TAG,"Getting command name %s", autoexec_name);
				autoexec_value= get_nvs_value_alloc(NVS_TYPE_STR, autoexec_name);
				if(autoexec_value!=NULL ){
					ESP_LOGI(TAG,"Running command %s = %s", autoexec_name, autoexec_value);
					run_command(autoexec_value);
					ESP_LOGD(TAG,"Freeing memory for command %s name", autoexec_name);
					free(autoexec_value);
				}
				else {
					ESP_LOGD(TAG,"No matching command found for name %s", autoexec_name);
					break;
				}
			} while(1);
		}
		free(str_flag);
	}
	else
	{
		ESP_LOGD(TAG,"No matching command found for name autoexec. Adding default entries");
		char autoexec_dft[]="0";
		char autoexec1_dft[256]="squeezelite -o I2S -b 500:2000 -d all=info -M esp32";
		store_nvs_value(NVS_TYPE_STR,"autoexec",autoexec_dft);
		store_nvs_value(NVS_TYPE_STR,"autoexec1",autoexec1_dft);
	}
}


void initialize_console() {

	/* Disable buffering on stdin */
	setvbuf(stdin, NULL, _IONBF, 0);

	/* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
	esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
	/* Move the caret to the beginning of the next line on '\n' */
	esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

	/* Configure UART. Note that REF_TICK is used so that the baud rate remains
	 * correct while APB frequency is changing in light sleep mode.
	 */
	const uart_config_t uart_config = { .baud_rate =
			CONFIG_CONSOLE_UART_BAUDRATE, .data_bits = UART_DATA_8_BITS,
			.parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
			.use_ref_tick = true };
	ESP_ERROR_CHECK(uart_param_config(CONFIG_CONSOLE_UART_NUM, &uart_config));

	/* Install UART driver for interrupt-driven reads and writes */
	ESP_ERROR_CHECK(
			uart_driver_install(CONFIG_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));

	/* Tell VFS to use UART driver */
	esp_vfs_dev_uart_use_driver(CONFIG_CONSOLE_UART_NUM);

	/* Initialize the console */
	esp_console_config_t console_config = { .max_cmdline_args = 22,
			.max_cmdline_length = 600,
#if CONFIG_LOG_COLORS
			.hint_color = atoi(LOG_COLOR_CYAN)
#endif
			};
	ESP_ERROR_CHECK(esp_console_init(&console_config));

	/* Configure linenoise line completion library */
	/* Enable multiline editing. If not set, long commands will scroll within
	 * single line.
	 */
	linenoiseSetMultiLine(1);

	/* Tell linenoise where to get command completions and hints */
	linenoiseSetCompletionCallback(&esp_console_get_completion);
	linenoiseSetHintsCallback((linenoiseHintsCallback*) &esp_console_get_hint);

	/* Set command history size */
	linenoiseHistorySetMaxLen(100);

	/* Load command history from filesystem */
	//linenoiseHistoryLoad(HISTORY_PATH);
}

void console_start() {

	initialize_console();

	/* Register commands */
	esp_console_register_help_command();
	register_system();
	register_nvs();
#if RECOVERY_APPLICATION!=1
	register_squeezelite();
#elif RECOVERY_APPLICATION==1
	register_ota_cmd();
#else
#error "Unknown build configuration"
#endif
	register_i2ctools();
	printf("\n"
#if RECOVERY_APPLICATION
			"****************************************************************\n"
			"RECOVERY APPLICATION\n"
			"This mode is used to flash Squeezelite into the OTA partition\n"
			"****\n\n"
#endif
			"Type 'help' to get the list of commands.\n"
			"Use UP/DOWN arrows to navigate through command history.\n"
			"Press TAB when typing command name to auto-complete.\n"
			"\n"
#if !RECOVERY_APPLICATION
			"To automatically execute lines at startup:\n"
			"\tSet NVS variable autoexec (U8) = 1 to enable, 0 to disable automatic execution.\n"
			"\tSet NVS variable autoexec[1~9] (string)to a command that should be executed automatically\n"
#endif
			"\n"
			"\n");

	/* Figure out if the terminal supports escape sequences */
	int probe_status = linenoiseProbe();
	if (probe_status) { /* zero indicates success */
		printf("\n****************************\n"
				"Your terminal application does not support escape sequences.\n"
				"Line editing and history features are disabled.\n"
				"On Windows, try using Putty instead.\n"
				"****************************\n");
		linenoiseSetDumbMode(1);
#if CONFIG_LOG_COLORS
		/* Since the terminal doesn't support escape sequences,
		 * don't use color codes in the prompt.
		 */
		prompt = "squeezelite-esp32> ";
#endif //CONFIG_LOG_COLORS

	}
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.thread_name= "console";
    cfg.inherit_cfg = true;
#if RECOVERY_APPLICATION
	cfg.stack_size = 4096 ;
#endif
    esp_pthread_set_cfg(&cfg);
	pthread_attr_t attr;
	pthread_attr_init(&attr);

	pthread_create(&thread_console, &attr, console_thread, NULL);
	pthread_attr_destroy(&attr);
}
void run_command(char * line){
	/* Try to run the command */
	int ret;
	esp_err_t err = esp_console_run(line, &ret);

	if (err == ESP_ERR_NOT_FOUND) {
		printf("Unrecognized command\n");
	} else if (err == ESP_ERR_INVALID_ARG) {
		// command was empty
	} else if (err == ESP_OK && ret != ESP_OK) {
		printf("Command returned non-zero error code: 0x%x (%s)\n", ret,
				esp_err_to_name(err));
	} else if (err != ESP_OK) {
		printf("Internal error: %s\n", esp_err_to_name(err));
	}
}
static void * console_thread() {
#if !RECOVERY_APPLICATION
	process_autoexec();
#endif
	/* Main loop */
	while (1) {
		/* Get a line using linenoise.
		 * The line is returned when ENTER is pressed.
		 */
		char* line = linenoise(prompt);
		if (line == NULL) { /* Ignore empty lines */
			continue;
		}
		/* Add the command to the history */
		linenoiseHistoryAdd(line);

		/* Save command history to filesystem */
		linenoiseHistorySave(HISTORY_PATH);
		printf("\n");
		run_command(line);
		/* linenoise allocates line buffer on the heap, so need to free it */
		linenoiseFree(line);
	}
	return NULL;
}

