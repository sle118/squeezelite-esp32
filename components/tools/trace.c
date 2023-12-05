#include <stdint.h>
#include "esp_system.h"
#include <string.h>
#include <stdbool.h>
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
    const char *name;
    SLIST_ENTRY(mem_usage_trace_for_thread) next;
} mem_usage_trace_for_thread_t;

static EXT_RAM_ATTR SLIST_HEAD(memtrace, mem_usage_trace_for_thread) s_memtrace;

mem_usage_trace_for_thread_t* memtrace_get_thread_entry(TaskHandle_t task)  {
    if(!task) {
        ESP_LOGE(TAG, "memtrace_get_thread_entry: task is NULL");
        return NULL;
    }
    ESP_LOGD(TAG,"Looking for task %s",STR_OR_ALT(pcTaskGetName(task ), "unknown"));
    mem_usage_trace_for_thread_t* it;
    SLIST_FOREACH(it, &s_memtrace, next) {
        if ( it->task == task ) {
            ESP_LOGD(TAG,"Found task %s",STR_OR_ALT(pcTaskGetName(task ), "unknown"));
            return it;
        }
    }
    return NULL;
}
void memtrace_add_thread_entry(TaskHandle_t task) {
    if(!task) {
        ESP_LOGE(TAG, "memtrace_get_thread_entry: task is NULL");
        return ;
    }
    mem_usage_trace_for_thread_t* it = memtrace_get_thread_entry(task);
    if (it) {
        ESP_LOGW(TAG, "memtrace_add_thread_entry: thread already in list");
        return;
    }
    it = (mem_usage_trace_for_thread_t*)malloc_init_external(sizeof(mem_usage_trace_for_thread_t));
    if (!it) {
        ESP_LOGE(TAG, "memtrace_add_thread_entry: malloc failed");
        return;
    }
    it->task = task;
    it->malloc_int_last = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    it->malloc_spiram_last = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    it->malloc_dma_last = heap_caps_get_free_size(MALLOC_CAP_DMA);
    it->name = pcTaskGetName(task);
    SLIST_INSERT_HEAD(&s_memtrace, it, next);
    return;
}
// void memtrace_print_delta(){
//     TaskHandle_t task = xTaskGetCurrentTaskHandle();
//     mem_usage_trace_for_thread_t* it = memtrace_get_thread_entry(task);
//     if (!it) {
//         memtrace_add_thread_entry(task);
//         ESP_LOGW(TAG, "memtrace_print_delta: added new entry for task %s",STR_OR_ALT(pcTaskGetName(task ), "unknown"));
//         return;
//     }
//     size_t malloc_int_delta = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) - it->malloc_int_last;
//     size_t malloc_spiram_delta = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) - it->malloc_spiram_last;
//     size_t malloc_dma_delta = heap_caps_get_free_size(MALLOC_CAP_DMA) - it->malloc_dma_last;
// 	ESP_LOGD(TAG, "Heap internal:%zu (min:%zu) external:%zu (min:%zu) dma:%zu (min:%zu)",
// 			heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
// 			heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
// 			heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
// 			heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
// 			heap_caps_get_free_size(MALLOC_CAP_DMA),
// 			heap_caps_get_minimum_free_size(MALLOC_CAP_DMA));    
//     ESP_LOGW(TAG, "memtrace_print_delta: %s: malloc_int_delta=%d, malloc_spiram_delta=%d, malloc_dma_delta=%d",
//         STR_OR_ALT(it->name, "unknown"),
//         malloc_int_delta,
//         malloc_spiram_delta,
//         malloc_dma_delta);
//     it->malloc_int_last = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
//     it->malloc_spiram_last = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
//     it->malloc_dma_last = heap_caps_get_free_size(MALLOC_CAP_DMA);
    
// }
size_t malloc_int = 0;
size_t malloc_spiram =0;
size_t malloc_dma = 0;
void memtrace_print_delta(const char * msg, const char * tag, const char * function){
    size_t malloc_int_delta = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) - malloc_int;
    size_t malloc_spiram_delta = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) - malloc_spiram;
    size_t malloc_dma_delta = heap_caps_get_free_size(MALLOC_CAP_DMA) - malloc_dma;
	ESP_LOGW(TAG, "Heap internal:%zu (min:%zu)(chg:%d)/external:%zu (min:%zu)(chg:%d)/dma:%zu (min:%zu)(chg:%d) : %s%s%s%s%s",
			heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
			heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
            malloc_int_delta,
			heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
			heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
            malloc_spiram_delta,
			heap_caps_get_free_size(MALLOC_CAP_DMA),
			heap_caps_get_minimum_free_size(MALLOC_CAP_DMA),
            malloc_dma_delta,
            STR_OR_BLANK(tag),
            tag?" ":"",
            STR_OR_BLANK(function),
            function?" ":"",
            STR_OR_BLANK(msg)            
            );    
    malloc_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    malloc_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    malloc_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
}
