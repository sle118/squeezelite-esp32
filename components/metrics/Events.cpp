#include "Events.h"
#include <algorithm>
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#if CONFIG_WITH_METRICS
static const char* const TAG = "MetricsEvent";
namespace Metrics {
Event& Event::add_property(const char* name, const char* value) {
    ESP_LOGV(TAG, "Adding property %s:%s to event %s", name, value, _name);
    char* mutable_name = strdup_psram(name); // Cast away const-ness, be careful with this
    auto elem = properties.find(mutable_name);
    FREE_AND_NULL(mutable_name)
    if(elem == properties.end()) {
        ESP_LOGV(TAG, "Adding property %s:%s to event %s", name, value, _name);
        properties.insert({strdup_psram(name), strdup_psram(value)});
    } else {
        ESP_LOGV(TAG, "Replacing value for property %s. Old: %s New: %s, Event: %s", name, elem->second, value, name);
        FREE_AND_NULL(elem->second)
        elem->second = strdup_psram(value);
    }
    return *this;
}

bool Event::has_property_value(const char* name, const char* value) const {
    ESP_LOGV(TAG, "Checking if event %s property %s has value %s", _name, name, value);
    return std::any_of(properties.begin(), properties.end(), [name, value](const std::pair<const char* const, char*>& kv) {
        ESP_LOGV(TAG, "Found property %s=%s", name, value);
        return strcmp(kv.first, name) == 0 && strcmp(kv.second, value) == 0;
    });
}

void Event::remove_property(const char* name, const char* value) {
    auto it = properties.begin();
    ESP_LOGV(TAG, "Removing event %s property %s=%s", _name, name, value);
    while(it != properties.end()) {
        if(strcmp(it->first, name) == 0 && strcmp(it->second, value)) {
            properties.erase(it);
            return;
        }
    }
    ESP_LOGV(TAG, "Property %s=%s not found.", name, value);
}
cJSON* Event::properties_to_json() {
    ESP_LOGV(TAG, "Event %s properties to json.", _name);
    const esp_app_desc_t* desc = esp_ota_get_app_description();
#ifdef CONFIG_FW_PLATFORM_NAME
    const char* platform = CONFIG_FW_PLATFORM_NAME;
#else
    const char* platform = desc->project_name;
#endif
    cJSON* prop_json = cJSON_CreateObject();
    auto it = properties.begin();

    while(it != properties.end()) {
        cJSON_AddStringToObject(prop_json, it->first, it->second);
        ++it;
    }
    cJSON_AddStringToObject(prop_json, "platform", platform);
    cJSON_AddStringToObject(prop_json, "build", desc->version);
    dump_json_content("User properties for event:", prop_json, ESP_LOG_VERBOSE);
    return prop_json;
}
cJSON* Event::to_json(const char* distinct_id) {
    // The target structure looks like this
    //  {
    //   "event": "batched_event_name_1",
    //   "properties": {
    //     "distinct_id": "user distinct id",
    //     "account_type": "pro"
    //   },
    //   "timestamp": "[optional timestamp in ISO 8601 format]"
    // }
    ESP_LOGV(TAG, "Event %s to json", _name);

    free_json();
    _json = cJSON_CreateObject();
    cJSON_AddStringToObject(_json, "name", _name);
    cJSON_AddItemToObject(_json, "properties", properties_to_json());

    char buf[26] = {};
    strftime(buf, sizeof(buf), "%FT%TZ", gmtime(&_time));
    // this will work too, if your compiler doesn't support %F or %T:
    // strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    cJSON_AddStringToObject(_json, "timestamp", buf);
    cJSON* prop_json = properties_to_json();
    cJSON_AddStringToObject(prop_json, "distinct_id", distinct_id);
    dump_json_content("Full Event:", _json, ESP_LOG_VERBOSE);
    return _json;
}
void Event::free_json() { cJSON_Delete(_json); }
void Event::update_time() {
    if(_time == 0) { _time = time(nullptr); }
}
} // namespace Metrics
#endif