/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_task.h"
#include "monitor.h"
#include "driver/gpio.h"
#include "buttons.h"
#include "led.h"
#include "globdefs.h"
#include "platform_config.h"
#include "accessors.h"
#include "messaging.h"
#include "cJSON.h"
#include "tools.h"

#define PSEUDO_IDLE_STACK_SIZE	(6*1024)

#define MONITOR_TIMER	(10*1000)
#define SCRATCH_SIZE	256

static const char *TAG = "monitor";

void (*pseudo_idle_svc)(uint32_t now);

void (*jack_handler_svc)(bool inserted);
bool jack_inserted_svc(void);

void (*spkfault_handler_svc)(bool inserted);
bool spkfault_svc(void);

static monitor_gpio_t jack = { CONFIG_JACK_GPIO, 0 };
static monitor_gpio_t spkfault = { CONFIG_SPKFAULT_GPIO, 0 };
static bool monitor_stats;

/****************************************************************************************
 *
 */
static void task_stats( cJSON* top ) {
#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
#pragma message("Compiled with trace facility")
	static struct {
		TaskStatus_t *tasks;
		uint32_t total, n;
	} current, previous;
    
	cJSON * tlist=cJSON_CreateArray();
	current.n = uxTaskGetNumberOfTasks();
	current.tasks = malloc_init_external( current.n * sizeof( TaskStatus_t ) );
	current.n = uxTaskGetSystemState( current.tasks, current.n, &current.total );
	cJSON_AddNumberToObject(top,"ntasks",current.n);

	char scratch[SCRATCH_SIZE] = {0};

#ifdef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
#pragma message("Compiled with runtime stats")
	uint32_t elapsed = current.total - previous.total;

	for(int i = 0, n = 0; i < current.n; i++ ) {
		for (int j = 0; j < previous.n; j++) {
			if (current.tasks[i].xTaskNumber == previous.tasks[j].xTaskNumber) {
				n += snprintf(scratch + n, SCRATCH_SIZE - n, "%16s (%u) %2u%% s:%5u", current.tasks[i].pcTaskName,
																		   current.tasks[i].eCurrentState,
																		   100 * (current.tasks[i].ulRunTimeCounter - previous.tasks[j].ulRunTimeCounter) / elapsed,
																		   current.tasks[i].usStackHighWaterMark);
				cJSON * t=cJSON_CreateObject();
				cJSON_AddNumberToObject(t,"cpu",100 * (current.tasks[i].ulRunTimeCounter - previous.tasks[j].ulRunTimeCounter) / elapsed);
				cJSON_AddNumberToObject(t,"minstk",current.tasks[i].usStackHighWaterMark);
				cJSON_AddNumberToObject(t,"bprio",current.tasks[i].uxBasePriority);
				cJSON_AddNumberToObject(t,"cprio",current.tasks[i].uxCurrentPriority);
				cJSON_AddStringToObject(t,"nme",current.tasks[i].pcTaskName);
				cJSON_AddNumberToObject(t,"st",current.tasks[i].eCurrentState);
				cJSON_AddNumberToObject(t,"num",current.tasks[i].xTaskNumber);
				cJSON_AddItemToArray(tlist,t);
				if (i % 3 == 2 || i == current.n - 1) {
					ESP_LOGI(TAG, "%s", scratch);
					n = 0;
				}
				break;
			}
		}
	}
#else
#pragma message("Compiled WITHOUT runtime stats")

	for (int i = 0, n = 0; i < current.n; i ++) {
		n += sprintf(scratch + n, "%16s s:%5u\t", current.tasks[i].pcTaskName, current.tasks[i].usStackHighWaterMark);
		cJSON * t=cJSON_CreateObject();
		cJSON_AddNumberToObject(t,"minstk",current.tasks[i].usStackHighWaterMark);
		cJSON_AddNumberToObject(t,"bprio",current.tasks[i].uxBasePriority);
		cJSON_AddNumberToObject(t,"cprio",current.tasks[i].uxCurrentPriority);
		cJSON_AddStringToObject(t,"nme",current.tasks[i].pcTaskName);
		cJSON_AddNumberToObject(t,"st",current.tasks[i].eCurrentState);
		cJSON_AddNumberToObject(t,"num",current.tasks[i].xTaskNumber);
		cJSON_AddItemToArray(tlist,t);
		if (i % 3 == 2 || i == current.n - 1) {
			ESP_LOGI(TAG, "%s", scratch);
			n = 0;
		}
	}
#endif
	cJSON_AddItemToObject(top,"tasks",tlist);
	if (previous.tasks) free(previous.tasks);
	previous = current;
#else
#pragma message("Compiled WITHOUT trace facility")
#endif
}

/****************************************************************************************
 *
 */
static void monitor_trace(uint32_t now) {
    static uint32_t last;

    if (now < last + MONITOR_TIMER) return;
    last = now;

	cJSON * top=cJSON_CreateObject();
	cJSON_AddNumberToObject(top,"free_iram",heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
	cJSON_AddNumberToObject(top,"min_free_iram",heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
	cJSON_AddNumberToObject(top,"free_spiram",heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
	cJSON_AddNumberToObject(top,"min_free_spiram",heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));

	ESP_LOGI(TAG, "Heap internal:%zu (min:%zu) external:%zu (min:%zu) dma:%zu (min:%zu)",
			heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
			heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
			heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
			heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
			heap_caps_get_free_size(MALLOC_CAP_DMA),
			heap_caps_get_minimum_free_size(MALLOC_CAP_DMA));

	task_stats(top);

	char * top_a= cJSON_PrintUnformatted(top);
	if(top_a){
		messaging_post_message(MESSAGING_INFO, MESSAGING_CLASS_STATS,top_a);
		FREE_AND_NULL(top_a);
	}

	cJSON_Delete(top);
}

/****************************************************************************************
 *
 */
static void jack_handler_default(void *id, button_event_e event, button_press_e mode, bool long_press) {
	ESP_LOGI(TAG, "Jack %s", event == BUTTON_PRESSED ? "inserted" : "removed");
	if (jack_handler_svc) (*jack_handler_svc)(event == BUTTON_PRESSED);
}

/****************************************************************************************
 *
 */
bool jack_inserted_svc (void) {
	if (jack.gpio != -1) return button_is_pressed(jack.gpio, NULL);
	else return true;
}

/****************************************************************************************
 *
 */
static void spkfault_handler_default(void *id, button_event_e event, button_press_e mode, bool long_press) {
	ESP_LOGD(TAG, "Speaker status %s", event == BUTTON_PRESSED ? "faulty" : "normal");
	if (event == BUTTON_PRESSED) led_on(LED_RED);
	else led_off(LED_RED);
	if (spkfault_handler_svc) (*spkfault_handler_svc)(event == BUTTON_PRESSED);
}

/****************************************************************************************
 *
 */
bool spkfault_svc (void) {
	return button_is_pressed(spkfault.gpio, NULL);
}

/****************************************************************************************
 *
 */
#ifndef CONFIG_JACK_LOCKED
static void set_jack_gpio(int gpio, char *value) {
	if (strcasestr(value, "jack")) {
		char *p;
		jack.gpio = gpio;
		if ((p = strchr(value, ':')) != NULL) jack.active = atoi(p + 1);
	}
	else {
		jack.gpio = -1;
	}
}
#endif

/****************************************************************************************
 *
 */
#ifndef CONFIG_SPKFAULT_LOCKED
static void set_spkfault_gpio(int gpio, char *value) {
	if (strcasestr(value, "spkfault")) {
		char *p;
		spkfault.gpio = gpio;
		if ((p = strchr(value, ':')) != NULL) spkfault.active = atoi(p + 1);
	}
	else {
		spkfault.gpio = -1;
	}
}
#endif

/****************************************************************************************
 *
 */
static void pseudo_idle(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t now = pdTICKS_TO_MS(xTaskGetTickCount());

        if (monitor_stats) monitor_trace(now);
        if (pseudo_idle_svc) pseudo_idle_svc(now);
    }
}

/****************************************************************************************
 *
 */
void monitor_svc_init(void) {
	ESP_LOGI(TAG, "Initializing monitoring");

#ifdef CONFIG_JACK_GPIO_LEVEL
	jack.active = CONFIG_JACK_GPIO_LEVEL;
#endif

#ifndef CONFIG_JACK_LOCKED
	parse_set_GPIO(set_jack_gpio);
#endif

	// re-use button management for jack handler, it's a GPIO after all
	if (jack.gpio != -1) {
		ESP_LOGI(TAG,"Adding jack (%s) detection GPIO %d", jack.active ? "high" : "low", jack.gpio);
		// Use GPIO pull so the line does not float (Muse jack detect is active-low and needs pull-up)
		button_create(NULL, jack.gpio, jack.active ? BUTTON_HIGH : BUTTON_LOW, true, 250, jack_handler_default, 0, -1);
	}

#ifdef CONFIG_SPKFAULT_GPIO_LEVEL
	spkfault.active = CONFIG_SPKFAULT_GPIO_LEVEL;
#endif

#ifndef CONFIG_SPKFAULT_LOCKED
	parse_set_GPIO(set_spkfault_gpio);
#endif

	// re-use button management for speaker fault handler, it's a GPIO after all
	if (spkfault.gpio != -1) {
		ESP_LOGI(TAG,"Adding speaker fault (%s) detection GPIO %d", spkfault.active ? "high" : "low", spkfault.gpio);
		button_create(NULL, spkfault.gpio, spkfault.active ? BUTTON_HIGH : BUTTON_LOW, false, 0, spkfault_handler_default, 0, -1);
	}

	// do we want stats
	char *p = config_alloc_get_default(NVS_TYPE_STR, "stats", "n", 0);
	monitor_stats = p && (*p == '1' || *p == 'Y' || *p == 'y');
	FREE_AND_NULL(p);

	ESP_LOGI(TAG, "Heap internal:%zu (min:%zu) external:%zu (min:%zu) dma:%zu (min:%zu)",
			heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
			heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
			heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
			heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
			heap_caps_get_free_size(MALLOC_CAP_DMA),
			heap_caps_get_minimum_free_size(MALLOC_CAP_DMA));

    // pseudo-idle callback => don't use FreeRTOS idle callbacks so we can block (should not but ...)
	StaticTask_t* xTaskBuffer = (StaticTask_t*) heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	static EXT_RAM_ATTR StackType_t xStack[PSEUDO_IDLE_STACK_SIZE] __attribute__ ((aligned (4)));
	xTaskCreateStatic( (TaskFunction_t) pseudo_idle, "pseudo_idle", PSEUDO_IDLE_STACK_SIZE,
						NULL, ESP_TASK_PRIO_MIN, xStack, xTaskBuffer );
}

/****************************************************************************************
 *
 */
 monitor_gpio_t * get_spkfault_gpio(){
	return &spkfault	;
 }

/****************************************************************************************
 *
 */
 monitor_gpio_t * get_jack_insertion_gpio(){
	return &jack;
}
