#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "Batch.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_tls.h"

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_system.h"
#include "http_handlers.h"
#include "tools.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/param.h>
#if CONFIG_WITH_METRICS
static const char* const TAG = "MetricsBatch";
static const char* const feature_evt_name = "$feature_flag_called";
static const char* const feature_flag_name = "$feature_flag";
static const char* const feature_flag_response_name = "$feature_flag_response";

namespace Metrics {

Event& Batch::add_feature_event() { return add_event(feature_evt_name); }
void Batch::add_remove_feature_event(const char* name, bool active) {
    if(!active) {
        remove_feature_event(name);
    } else {
        add_event(feature_evt_name).add_property(feature_flag_name, name);
    }
}
Event& Batch::add_feature_variant_event(const char* const name, const char* const value) {
    return add_event(feature_evt_name).add_property(feature_flag_name, name).add_property(feature_flag_response_name, value);
}
void Batch::remove_feature_event(const char* name) {
    for(Metrics::Event& e : _events) {
        if(strcmp(e.get_name(), feature_evt_name) == 0) {
            e.remove_property(feature_flag_name, name);
            return;
        }
    }
}
cJSON* Batch::to_json() {
    cJSON* batch_json = cJSON_CreateArray();
    for(Metrics::Event& e : _events) { cJSON_AddItemToArray(batch_json, e.to_json(_metrics_uid.c_str())); }
    cJSON* message = cJSON_CreateObject();
    cJSON_AddItemToObject(message, "batch", batch_json);
    cJSON_AddStringToObject(message, "api_key", _api_key);
    return batch_json;
}
char* Batch::to_json_str() {
    cJSON* json = to_json();
    char* json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return json_str;
}

void Batch::push() {
    int status_code = 0;
    if(_metrics_uid.empty() && !_warned) {
        ESP_LOGW(TAG, "Metrics disabled; no CID found");
        _warned = true;
        return;
    }

    char* json_str = to_json_str();
    ESP_LOGV(TAG, "Metrics payload: %s", json_str);
    uint32_t start_time = gettime_ms();

    status_code = metrics_http_post_request(json_str, _url);

    if(status_code == 200 || status_code == 204) { _events.clear(); }
    FREE_AND_NULL(json_str)
    ESP_LOGD(TAG, "Total duration for metrics call: %u. ", gettime_ms() - start_time);
}

void Batch::build_guid() {
    uint8_t raw[16];
    std::ostringstream oss;
    esp_fill_random(raw, 16);
    std::for_each(std::begin(raw), std::end(raw),
        [&oss](const uint8_t& byte) { oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte); });
    _metrics_uid = oss.str();
}
void Batch::assign_id() {
    size_t size = 0;
    esp_err_t esp_err = ESP_OK;
    _metrics_uid = std::string((char*)get_nvs_value_alloc_for_partition(NVS_DEFAULT_PART_NAME, TAG, NVS_TYPE_BLOB, "cid", &size));
    if(_metrics_uid[0] == 'G') {
        ESP_LOGW(TAG, "Invalid ID. %s", _metrics_uid.c_str());
        _metrics_uid.clear();
    }
    if(_metrics_uid.empty()) {
        build_guid();
        if(_metrics_uid.empty()) {
            ESP_LOGE(TAG, "ID Failed");
            return;
        }
        ESP_LOGW(TAG, "Metrics ID: %s", _metrics_uid.c_str());
        esp_err =
            store_nvs_value_len_for_partition(NVS_DEFAULT_PART_NAME, TAG, NVS_TYPE_BLOB, "cid", _metrics_uid.c_str(), _metrics_uid.length() + 1);
        if(esp_err != ESP_OK) { ESP_LOGE(TAG, "Store ID failed: %s", esp_err_to_name(esp_err)); }
    }
}
} // namespace Metrics
#endif