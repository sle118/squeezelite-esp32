#include <stdint.h>
#include "esp_system.h"
#include <string.h>
#include <stdbool.h>
#include <sys/queue.h>
#include "esp_log.h"
#include "freertos/xtensa_api.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "tools.h"
#include "trace.h"

static const char TAG[] = "TRACE";
typedef struct mem_usage_trace_for_thread {
    TaskHandle_t task;
    size_t malloc_int_last;
    size_t malloc_spiram_last;
    size_t malloc_dma_last;
    const char* name;
    SLIST_ENTRY(mem_usage_trace_for_thread) next;
} mem_usage_trace_for_thread_t;

static EXT_RAM_ATTR SLIST_HEAD(memtrace, mem_usage_trace_for_thread) s_memtrace;

mem_usage_trace_for_thread_t* memtrace_get_thread_entry(TaskHandle_t task) {
    if(!task) {
        ESP_LOGE(TAG, "memtrace_get_thread_entry: task is NULL");
        return NULL;
    }
    ESP_LOGD(TAG, "Looking for task %s", STR_OR_ALT(pcTaskGetName(task), "unknown"));
    mem_usage_trace_for_thread_t* it;
    SLIST_FOREACH(it, &s_memtrace, next) {
        if(it->task == task) {
            ESP_LOGD(TAG, "Found task %s", STR_OR_ALT(pcTaskGetName(task), "unknown"));
            return it;
        }
    }
    return NULL;
}
