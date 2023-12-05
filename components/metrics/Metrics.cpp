#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "Metrics.h"
#include "Batch.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tools.h"
#include <cstdarg>
#include <cstdio>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "cJSON.h"
#include "freertos/timers.h"
#include "network_manager.h"
// #include "Configurator.h"
#pragma message("fixme: search for TODO below")

static const char* TAG = "metrics";

#if CONFIG_WITH_METRICS
extern bool is_network_connected();
#define METRICS_CLIENT_ID_LEN 50
#define MAX_HTTP_RECV_BUFFER 512

static bool metrics_usage_gen = false;
static uint32_t metrics_usage_gen_time = 0;
#ifndef METRICS_API_KEY
    #pragma message "Metrics API key needs to be passed from the environment"
    #define METRICS_API_KEY "ZZZ"
#endif
static const char* metrics_api_key =  METRICS_API_KEY;
static const char* parms_str = "params";
static const char* properties_str = "properties";
static const char* user_properties_str = "user_properties";
static const char* items_str = "items";
static const char* quantity_str = "quantity";
static const char* metrics_url = "https://app.posthog.com";
static TimerHandle_t timer;
extern cJSON* get_cmd_list();
Metrics::Batch batch;

static void metrics_timer_cb(void* timer_id) {
    if (batch.has_events()) {
        if (!is_network_connected()) {
            ESP_LOGV(TAG, "Network not connected. can't flush");
        } else {
            ESP_LOGV(TAG, "Pushing events");
            batch.push();
        }
    }
    if (gettime_ms() > metrics_usage_gen_time && !metrics_usage_gen) {
        metrics_usage_gen = true;
        ESP_LOGV(TAG, "Generate command list to pull features");
        cJSON* cmdlist = get_cmd_list();
        dump_json_content("generated cmd list", cmdlist, ESP_LOG_VERBOSE);
        cJSON_Delete(cmdlist);
    }
}
void metrics_init() {
    ESP_LOGV(TAG, "Initializing metrics");
    batch.configure(metrics_api_key, metrics_url);
    if (!timer) {
        ESP_LOGE(TAG, "Metrics Timer failure");
    } else {
        ESP_LOGV(TAG, "Starting timer");
        xTimerStart(timer, portMAX_DELAY);
    }
    // set a 20 seconds delay before generating the
    // features so the system has time to boot
    metrics_usage_gen_time = gettime_ms() + 20000;
}

void metrics_event_playback(const char* source) {
    ESP_LOGV(TAG, "Playback event: %s", source);
    auto event = batch.add_event("play").add_property("source", source);
}
void metrics_event_boot(const char* partition) {
    ESP_LOGV(TAG, "Boot event %s", partition);
    auto event = batch.add_event("start");
    event.add_property("partition", partition);
}
void metrics_add_feature_variant(const char* name, const char* format, ...) {
    va_list args;
    ESP_LOGV(TAG, "Feature %s", name);
    va_start(args, format);

    // Determine the required buffer size
    int size = vsnprintf(nullptr, 0, format, args);
    va_end(args); // Reset the va_list

    // Allocate buffer and format the string
    std::vector<char> buffer(size + 1); // +1 for the null-terminator
    va_start(args, format);
    vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);

    // Now buffer.data() contains the formatted string
    batch.add_feature_variant_event(name, buffer.data());
}
void metrics_add_feature(const char* name, bool active) {
    ESP_LOGV(TAG, "Adding feature %s: %s", name, active ? "ACTIVE" : "INACTIVE");
    batch.add_remove_feature_event(name, active);
}
void metrics_event(const char* name) {
    ESP_LOGV(TAG, "Adding Event %s", name);
    batch.add_event(name);
}
#else
static const char * not_enabled =  " - (metrics not enabled, this is just marking where the call happens)";
void metrics_init(){
#pragma message("Metrics disabled")
    ESP_LOGD(TAG,"Metrics init%s",not_enabled);
}
void metrics_event_boot(const char* partition){
    ESP_LOGD(TAG,"Metrics Event Boot from partition %s%s",partition,not_enabled);
}
void metrics_event(const char* name){
    ESP_LOGD(TAG,"Metrics Event %s%s",name,not_enabled);
}
void metrics_add_feature(const char* name, bool active) {
    ESP_LOGD(TAG,"Metrics add feature %s%s%s",name,active?"ACTIVE":"INACTIVE",not_enabled);
}
void metrics_add_feature_variant(const char* name, const char* format, ...){
    va_list args;
    ESP_LOGV(TAG, "Feature %s", name);
    va_start(args, format);

    // Determine the required buffer size
    int size = vsnprintf(nullptr, 0, format, args);
    va_end(args); // Reset the va_list

    // Allocate buffer and format the string
    std::vector<char> buffer(size + 1); // +1 for the null-terminator
    va_start(args, format);
    vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);

    ESP_LOGD(TAG,"Metrics add feature %s variant %s%s",name,buffer.data(),not_enabled);
}
#endif