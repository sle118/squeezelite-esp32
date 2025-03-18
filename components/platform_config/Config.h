#pragma once
#include "State.pb.h"
#include "Status.pb.h"
#include "configuration.pb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "status.pb.h"
#include "accessors.h"
#include "esp_http_server.h"
#include "PBW.h"

#ifdef __cplusplus
extern System::PB<sys_state_data> stateWrapper;
extern System::PB<sys_config> configWrapper;
extern "C" {
#endif
#define PLATFORM_GET_PTR(base, sname)                                                              \
    {                                                                                              \
        (base && (base)->##has_##(sname) ? &(base)->sname : NULL)
#define PLATFORM_DEVICES PLATFORM_GET_PTR(platform)
void config_load();
bool config_waitcommit();
bool config_has_changes();
void config_raise_changed(bool sync);
void config_raise_state_changed();
bool config_has_changes();
bool config_waitcommit();
void config_set_target(const char* target_name);
void config_set_target_no_reset(const char* target_name);
void config_set_target_reset(const char* target_name);

void config_commit_protowrapper(void * protoWrapper);
bool config_http_send_config(httpd_req_t* req);
bool config_erase_config();
void set_mac_string();
void config_dump_config();
bool system_set_string(
    const pb_msgdesc_t* desc, uint32_t field_tag, void* message, const char* value);
extern sys_config* platform;
extern sys_state_data* sys_state;
extern sys_config* presets;
#ifdef __cplusplus
}


#endif

