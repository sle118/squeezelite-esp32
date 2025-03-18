#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "Config.h"
#include "PBW.h"
#include "WifiList.h"
#include "bootstate.h"
#include "DAC.pb.h"
#include "esp_log.h"
#include "esp_system.h"
#include "pb_common.h" // Nanopb header for encoding (serialization)
#include "pb_decode.h" // Nanopb header for decoding (deserialization)
#include "pb_encode.h" // Nanopb header for encoding (serialization)
#include "tools.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string.h>

static const char* TAG = "Configurator";

using namespace System;
__attribute__((section(".ext_ram.bss"))) sys_config* platform;
__attribute__((section(".ext_ram.bss"))) sys_state_data* sys_state;
__attribute__((section(".ext_ram.bss"))) sys_dac_default_sets* default_dac_sets;

__attribute__((section(".ext_ram.bss"))) System::PB<sys_config> configWrapper("config", &sys_config_msg, sizeof(sys_config_msg));
__attribute__((section(".ext_ram.bss"))) System::PB<sys_state_data> stateWrapper("state", &sys_state_data_msg, sizeof(sys_state_data_msg));
__attribute__((section(".ext_ram.bss"))) System::PB<sys_dac_default_sets> defaultSets("default_sets", &sys_dac_default_sets_msg, sizeof(sys_dac_default_sets_msg));

const int MaxDelay = 1000;
bool config_update_mac_string(const pb_msgdesc_t* desc, uint32_t field_tag, void* message) {
    pb_field_iter_t iter;
    if (pb_field_iter_begin(&iter, desc, message) && pb_field_iter_find(&iter, field_tag)) {
        if (!iter.pData) {
            ESP_LOGW(TAG, "Unable to check mac string member. Data not initialized");
            return false;
        }
        if (iter.pData) {
            auto curvalue = std::string((char*)iter.pData);
            if (curvalue.find(get_mac_str()) != std::string::npos) {
                ESP_LOGD(TAG, "Entry already has mac string: %s", curvalue.c_str());
                return true;
            }
            if (curvalue.find("@@init_from_mac@@") == std::string::npos) {
                ESP_LOGW(TAG, "Member not configured for mac address or was overwritten: %s", curvalue.c_str());
                return false;
            }
            auto newval = std::string("squeezelite-") + get_mac_str();
            if (PB_ATYPE(iter.type) == PB_ATYPE_POINTER) {
                ESP_LOGD(TAG, "Field is a pointer. Freeing previous value if any: %s", STR_OR_BLANK((char*)iter.pField));
                FREE_AND_NULL(*(char**)iter.pField);
                ESP_LOGD(TAG, "Field is a pointer. Setting new value as %s", newval.c_str());
                *(char**)iter.pField = strdup_psram(newval.c_str());
            } else if (PB_ATYPE(iter.type) == PB_ATYPE_STATIC) {
                ESP_LOGD(TAG, "Static string. Setting new value as %s from %s", newval.c_str(), STR_OR_BLANK((char*)iter.pData));
                memset((char*)iter.pData, 0x00, iter.data_size);
                strncpy((char*)iter.pData, newval.c_str(), iter.data_size);
            }
        } else {
            ESP_LOGE(TAG, "Set mac string failed: member should be initialized with default");
            return false;
        }
    }
    return true;
}
bool set_pb_string_from_mac(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    if (!stream) {
        // This is a size calculation pass, return true to indicate field presence
        return true;
    }

    // Generate the string based on MAC and prefix
    const char* prefix = reinterpret_cast<const char*>(*arg);
    char* value = alloc_get_string_with_mac(prefix && strlen(prefix) > 0 ? prefix : "squeezelite-");

    // Write the string to the stream
    if (!pb_encode_string(stream, (uint8_t*)value, strlen(value))) {
        free(value); // Free memory if encoding fails
        return false;
    }

    free(value); // Free memory after encoding
    return true;
}

bool config_erase_config() {
    // make sure the config object doesn't have
    // any pending changes to commit
    ESP_LOGW(TAG, "Erasing configuration object and rebooting");
    configWrapper.ResetModified();
    // Erase the file and reboot
    erase_path(configWrapper.GetFileName().c_str(), false);
    guided_factory();
    return true;
}
void set_mac_string() {
    auto expected = std::string("squeezelite-") + get_mac_str();
    bool changed = false;
    auto config = configWrapper.get();
    changed = config_update_mac_string(&sys_names_config_msg, sys_names_config_device_tag, &config->names) || changed;
    changed = config_update_mac_string(&sys_names_config_msg, sys_names_config_airplay_tag, &config->names) || changed;
    changed = config_update_mac_string(&sys_names_config_msg, sys_names_config_spotify_tag, &config->names) || changed;
    changed = config_update_mac_string(&sys_names_config_msg, sys_names_config_bluetooth_tag, &config->names) || changed;
    changed = config_update_mac_string(&sys_names_config_msg, sys_names_config_squeezelite_tag, &config->names) || changed;
    changed = config_update_mac_string(&sys_names_config_msg, sys_names_config_wifi_ap_name_tag, &config->names) || changed;

    if (changed) {
        ESP_LOGI(TAG, "One or more name was changed. Committing");
        configWrapper.RaiseChangedAsync();
    }
}

void config_load() {
    ESP_LOGI(TAG, "Loading configuration.");
    bool restart = false;
    sys_state = stateWrapper.get();
    platform = configWrapper.get();
    default_dac_sets = defaultSets.get();

    assert(platform != nullptr);
    assert(sys_state != nullptr);
    assert(default_dac_sets != nullptr);
    
    configWrapper.get()->net.credentials = reinterpret_cast<sys_net_wifi_entry*>(new WifiList("wifi"));
    assert(configWrapper.get()->net.credentials != nullptr);

    if (!stateWrapper.FileExists()) {
        ESP_LOGI(TAG, "State file not found or is empty. ");
        stateWrapper.Reinitialize(true);
        stateWrapper.SetTarget(CONFIG_FW_PLATFORM_NAME);
    } else {
        stateWrapper.LoadFile();
    }
    if (!configWrapper.FileExists()) {
        ESP_LOGI(TAG, "Configuration file not found or is empty. ");
        configWrapper.Reinitialize(true);
        ESP_LOGI(TAG, "Current device name after load: %s", platform->names.device);
        
        configWrapper.SetTarget(CONFIG_FW_PLATFORM_NAME,true);
        set_mac_string();
        ESP_LOGW(TAG, "Restart required after initializing configuration");
        restart = true;

    } else {
        configWrapper.LoadFile();
        if (configWrapper.GetTargetName().empty() && !std::string(CONFIG_FW_PLATFORM_NAME).empty()) {
            ESP_LOGW(TAG, "Config target is empty. Updating to %s", CONFIG_FW_PLATFORM_NAME);
            configWrapper.Reinitialize(false,true,std::string(CONFIG_FW_PLATFORM_NAME));
            ESP_LOGW(TAG, "Restart required due to target change");
            restart = true;
        }
    }
    if (!defaultSets.FileExists()) {
        ESP_LOGE(TAG, "Default Sets file not found or is empty. (%s)", defaultSets.GetFileName().c_str());
    } else {
        defaultSets.LoadFile();
    }        
    if (restart) {
        network_async_reboot(OTA);
    }
}
void config_dump_config() {
    auto serialized = configWrapper.Encode();
    dump_data(serialized.data(), serialized.size());
}

void config_set_target(const char* target_name, bool reset) {
    std::string new_target = std::string(target_name);
    bool restart = false;
    if (configWrapper.GetTargetName() != new_target) {
        ESP_LOGI(TAG, "Setting configuration target name to %s, %s", target_name, reset ? "full reset" : "reapply only");
        if (reset) {
            ESP_LOGD(TAG, "Reinitializing Config structure");
            configWrapper.Reinitialize(false,true,new_target);
        } else {
            ESP_LOGD(TAG, "Loading Config target values for %s", target_name);
            configWrapper.SetTarget(target_name,true);
            configWrapper.LoadTargetValues();
            configWrapper.RaiseChangedAsync();
        }
        restart = true;
    } else {
        ESP_LOGW(TAG, "Target name has no change");
    }
    if (stateWrapper.GetTargetName() != new_target) {
        ESP_LOGI(TAG, "Setting state target name to %s, %s", target_name, reset ? "full reset" : "reapply only");
        restart=true;
        if (reset) {
            ESP_LOGD(TAG, "Reinitializing State structure");
            stateWrapper.Reinitialize(false,true,new_target);
        } else {
            ESP_LOGD(TAG, "Loading State target values for %s", target_name);
            stateWrapper.SetTarget(target_name,true);
            stateWrapper.LoadTargetValues();
            stateWrapper.RaiseChangedAsync();
        }
    }
    ESP_LOGD(TAG, "Done updating target to %s", target_name);
    if(restart){
        network_async_reboot(RESTART);
    }
}
void config_set_target_no_reset(const char* target_name) { config_set_target(target_name, false); }
void config_set_target_reset(const char* target_name) { config_set_target(target_name, true); }
void config_raise_changed(bool sync) {
    if (sync) {
        configWrapper.CommitChanges();
    } else {
        configWrapper.RaiseChangedAsync();
    }
}
void config_commit_protowrapper(void* protoWrapper) {
    ESP_LOGD(TAG, "Committing synchronously");
    PBHelper::SyncCommit(protoWrapper);
}

void config_commit_state() { stateWrapper.CommitChanges(); }
void config_raise_state_changed() { stateWrapper.RaiseChangedAsync(); }

bool config_has_changes() { return configWrapper.HasChanges() || stateWrapper.HasChanges(); }

bool config_waitcommit() { return configWrapper.WaitForCommit(2) && stateWrapper.WaitForCommit(2); }

bool config_http_send_config(httpd_req_t* req) {
    try {
        auto data = configWrapper.Encode();
        httpd_resp_send(req, (const char*)data.data(), data.size());
        return true;
    } catch (const std::runtime_error& e) {
        std::string errdesc = (std::string("Unable to get configuration: ") + e.what());
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, errdesc.c_str());
    }
    return false;
}

bool system_set_string(const pb_msgdesc_t* desc, uint32_t field_tag, void* message, const char* value) {
    pb_field_iter_t iter;
    ESP_LOGD(TAG,"system_set_string. Getting new value");
    std::string newval = std::string(STR_OR_BLANK(value));
    ESP_LOGD(TAG,"system_set_string. Done getting new value");
    ESP_LOGD(TAG, "Setting value [%s] in message field tag %d", newval.c_str(), field_tag);
    if (pb_field_iter_begin(&iter, desc, message) && pb_field_iter_find(&iter, field_tag)) {
        std::string old= std::string((char*)iter.pData);
        if (iter.pData && old == newval) {
            ESP_LOGW(TAG, "No change, from and to values are the same: [%s]", newval.c_str());
            return false;
        }
        if (PB_ATYPE(iter.type) == PB_ATYPE_POINTER) {
            ESP_LOGD(TAG, "Field is a pointer. Freeing previous value if any: %s", STR_OR_BLANK((char*)iter.pField));
            FREE_AND_NULL(*(char**)iter.pField);
            ESP_LOGD(TAG, "Field is a pointer. Setting new value ");
            if (!newval.empty()) {
                *(char**)iter.pField = strdup_psram(newval.c_str());
            }

        } else if (PB_ATYPE(iter.type) == PB_ATYPE_STATIC) {
            ESP_LOGD(TAG, "Static string. Setting new value. Existing value: %s", (char*)iter.pData);
            memset((char*)iter.pData, 0x00, iter.data_size);
            if (!newval.empty()) {
                strncpy((char*)iter.pData, newval.c_str(), iter.data_size);
            }
        }
        ESP_LOGD(TAG, "Done setting value ");
    }
    return true;
}
