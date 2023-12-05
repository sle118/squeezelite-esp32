#pragma once

#ifdef __cplusplus
#include "esp_log.h"
#include "tools.h"
#include <cJSON.h>
#include <ctime>
#include <list>
#include <map>
#include <stdio.h>
#include <string.h>
#include <string>
#include <time.h>

namespace Metrics {
struct StrCompare {
    bool operator()(const char* a, const char* b) const { return strcmp(a, b) < 0; }
};

class Event {

  public:
    std::map<char*, char*, StrCompare> properties;
    Event& add_property(const char* name, const char* value);
    bool has_property_value(const char* name, const char* value) const;
    void remove_property(const char* name, const char* value);
    cJSON* properties_to_json();
    cJSON* to_json(const char* distinct_id);
    void free_json();
    void update_time();
    explicit Event(const char* name) {
        _name = strdup_psram(name);
        memset(&_time, 0x00, sizeof(_time));
    }
    const char* get_name() const { return _name; }
    ~Event() {
        FREE_AND_NULL(_name);

        // Iterate through the map and free the elements
        for (auto& kv : properties) {
            free((void*)kv.first);
            free(kv.second);
        }
        properties.clear(); // Clear the map after freeing memory
        FREE_AND_NULL(_json);
    }
  private:
    char* _name = nullptr;
    std::time_t _time;
    cJSON* _json = nullptr;    
};

} // namespace Metrics
#endif
