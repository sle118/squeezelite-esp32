#pragma once
#include "Events.h"
#include <string>
#ifdef __cplusplus
namespace Metrics {
extern "C" {
#endif

#ifdef __cplusplus

class Batch {
  private:
    std::list<Event> _events;
    bool _warned = false;
    std::string _metrics_uid;
    const char* _api_key = nullptr;
    const char* _url = nullptr;
    void build_guid();
    void assign_id();

  public:
    Batch() = default;
    void configure(const char* api_key, const char* url) {
        _api_key = api_key;
        _url = url;
        assign_id();
    }
    Event& add_feature_event();
    void add_remove_feature_event(const char* name, bool active);
    Event& add_feature_variant_event(const char* const name, const char* const value);
    Event& add_event(const char* name) {
        _events.emplace_back(name);
        return _events.back();
    }

    bool has_events() const { return !_events.empty(); }
    void remove_feature_event(const char* name);
    cJSON* to_json();
    char* to_json_str();
    void push();
};
}
#endif
#ifdef __cplusplus
}
#endif
