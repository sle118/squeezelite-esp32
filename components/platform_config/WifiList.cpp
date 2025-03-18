#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "WifiList.h"
#include "Config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include <memory>

static const char* TAG_CRED_MANAGER = "credentials_manager";
bool sys_status_wifi_callback(pb_istream_t* istream, pb_ostream_t* ostream, const pb_field_iter_t* field) {
    return sys_net_config_callback(istream, ostream, field);
}
bool sys_net_config_callback(pb_istream_t* istream, pb_ostream_t* ostream, const pb_field_iter_t* field) {
    WifiList** managerPtr = static_cast<WifiList**>(field->pData);
    WifiList* manager = *managerPtr;

    if (istream != NULL && (field->tag == sys_net_config_credentials_tag || field->tag == sys_status_wifi_scan_result_tag)) {
        if (manager == nullptr) {
            ESP_LOGE(TAG_CRED_MANAGER, "Invalid pointer to wifi list manager");
            return false;
        }
        ESP_LOGV(TAG_CRED_MANAGER, "Decoding credentials");
        sys_net_wifi_entry entry = sys_net_wifi_entry_init_default;
        if (!pb_decode(istream, &sys_net_wifi_entry_msg, &entry)) return false;
        printf("\nFound ssid %s, password %s\n", entry.ssid, entry.password);
        try {
            manager->AddUpdate(entry); // Add to the manager
        } catch (const std::exception& e) {
            ESP_LOGE(TAG_CRED_MANAGER, "decode exception: %s", e.what());
            return false;
        }
        ESP_LOGV(TAG_CRED_MANAGER, "Credentials decoding completed");
    } else if (ostream != NULL && (field->tag == sys_net_config_credentials_tag || field->tag == sys_status_wifi_scan_result_tag)) {
        if (manager == nullptr) {
            ESP_LOGV(TAG_CRED_MANAGER, "No wifi entries manager instance. nothing to encode");
            return true;
        }
        ESP_LOGV(TAG_CRED_MANAGER, "Encoding %d access points", manager->GetCount());

        for (int i = 0; i < manager->GetCount(); i++) {
            ESP_LOGV(TAG_CRED_MANAGER, "Encoding credential #%d: SSID: %s, PASS: %s", i, manager->GetIndex(i)->ssid, manager->GetIndex(i)->password);
            if (!pb_encode_tag_for_field(ostream, field)) {
                return false;
            }
            if (!pb_encode_submessage(ostream, &sys_net_wifi_entry_msg, manager->GetIndex(i))) {
                return false;
            }
        }
        ESP_LOGV(TAG_CRED_MANAGER, "Credentials encoding completed");
    }

    return true;
}
std::string WifiList::GetBSSID(const wifi_event_sta_connected_t* evt) {
    char buffer[18]={};
    FormatBSSID(buffer, sizeof(buffer), evt->bssid);
    ESP_LOGD(TAG_CRED_MANAGER, "Formatted BSSID: %s", buffer);
    return std::string(buffer);
}
bool WifiList::OffsetTimeStamp(google_protobuf_Timestamp* ts) {
    timeval tts;
    google_protobuf_Timestamp gts;
    gettimeofday((struct timeval*)&tts, NULL);
    gts.nanos = tts.tv_usec * 1000;
    gts.seconds = tts.tv_sec;
    if (tts.tv_sec < 1704143717) {
        ESP_LOGE(TAG_CRED_MANAGER, "Error updating time stamp. Clock doesn't seem right");
        return false;
    }
    if (ts && ts->seconds < 1704143717) {
        ESP_LOGV(TAG_CRED_MANAGER, "Updating time stamp based on new clock value");
        ts->seconds = gts.seconds - ts->seconds;
        ts->nanos = gts.nanos - ts->nanos;
        return true;
    }
    ESP_LOGD(TAG_CRED_MANAGER, "Time stamp already updated. Skipping");
    return false;
}
bool WifiList::UpdateTimeStamp(google_protobuf_Timestamp* ts, bool& has_flag_val) {
    ESP_RETURN_ON_FALSE(ts != nullptr, false, TAG_CRED_MANAGER, "Null pointer!");
    bool changed = false;
    timeval tts;
    google_protobuf_Timestamp gts;
    gettimeofday((struct timeval*)&tts, NULL);
    gts.nanos = tts.tv_usec * 1000;
    gts.seconds = tts.tv_sec;
    if (!has_flag_val || gts.nanos != ts->nanos || gts.seconds != ts->seconds) {
        ts->seconds = gts.seconds;
        ts->nanos = gts.nanos;
        has_flag_val = true;
        changed = true;
    }
    return changed;
}

bool WifiList::isEmpty(const char* str, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (str[i] != '\0') {
            return false;
        }
    }
    return true;
}

bool WifiList::Update(const wifi_ap_record_t* ap, bool connected) {
    if (!Lock()) {
        throw std::runtime_error("Lock failed");
    }
    auto existing = Get(ap);
    if (!existing) {
        return false;
    }
    auto updated = ToSTAEntry(ap);
    updated.connected = connected;
    bool changed = Update(*existing, updated);
    Release(&updated);
    Unlock();
    return changed;
}
bool WifiList::Update(sys_net_wifi_entry& existingEntry, sys_net_wifi_entry& updated) {

    // Check if any relevant fields have changed
    bool hasChanged = false;
    if (!isEmpty(updated.ssid, sizeof(updated.ssid)) && memcmp(existingEntry.ssid, updated.ssid, sizeof(existingEntry.ssid)) != 0) {
        memcpy(existingEntry.ssid, updated.ssid, sizeof(existingEntry.ssid));
        hasChanged = true;
    }

    // Check and copy BSSID if the compared BSSID is not empty
    if (!isEmpty(updated.bssid, sizeof(updated.bssid)) && strcmp(updated.bssid, "00:00:00:00:00:00") != 0 &&
        memcmp(existingEntry.bssid, updated.bssid, sizeof(existingEntry.bssid)) != 0) {
        memcpy(existingEntry.bssid, updated.bssid, sizeof(existingEntry.bssid));
        hasChanged = true;
    }

    // Check and copy password if the compared password is not empty
    if (!isEmpty(updated.password, sizeof(updated.password)) &&
        memcmp(existingEntry.password, updated.password, sizeof(existingEntry.password)) != 0) {
        memcpy(existingEntry.password, updated.password, sizeof(existingEntry.password));
        hasChanged = true;
    }

    if (existingEntry.channel != updated.channel && updated.channel > 0) {
        existingEntry.channel = updated.channel;
        hasChanged = true;
    }

    if (existingEntry.auth_type != updated.auth_type && updated.auth_type != sys_net_auth_types_AUTH_UNKNOWN) {
        existingEntry.auth_type = updated.auth_type;
        hasChanged = true;
    }

    if (areRadioTypesDifferent(existingEntry.radio_type, existingEntry.radio_type_count, updated.radio_type, updated.radio_type_count) &&
        updated.radio_type_count > 0 && updated.radio_type[0] != sys_net_radio_types_UNKNOWN) {

        if (existingEntry.radio_type != nullptr) {
            // Free the old radio_type array if it exists
            delete[] existingEntry.radio_type;
        }
        // Allocate new memory and copy the updated radio types
        existingEntry.radio_type_count = updated.radio_type_count;
        existingEntry.radio_type = new sys_net_radio_types[updated.radio_type_count];
        std::copy(updated.radio_type, updated.radio_type + updated.radio_type_count, existingEntry.radio_type);
        hasChanged = true;
    }

    if (updated.has_last_try) {
        if (memcmp(&existingEntry.last_try, &updated.last_try, sizeof(existingEntry.last_try)) != 0) {
            memcpy(&existingEntry.last_try, &updated.last_try, sizeof(existingEntry.last_try));
            hasChanged = true;
        }
    }
    if (updated.has_last_seen) {
        if (memcmp(&existingEntry.last_seen, &updated.last_seen, sizeof(existingEntry.last_seen)) != 0) {
            memcpy(&existingEntry.last_seen, &updated.last_seen, sizeof(existingEntry.last_seen));
            hasChanged = true;
        }
    }
    if (existingEntry.has_last_seen != updated.has_last_seen && updated.has_last_seen) {
        existingEntry.has_last_seen = updated.has_last_seen;
        hasChanged = true;
    }
    if (existingEntry.has_last_try != updated.has_last_try && updated.has_last_try) {
        existingEntry.has_last_try = updated.has_last_try;
        hasChanged = true;
    }

    if (existingEntry.connected != updated.connected && updated.connected) {
        existingEntry.connected = updated.connected;
        hasChanged = true;
    }

    if (existingEntry.rssi != updated.rssi && updated.rssi != 0) {
        existingEntry.rssi = updated.rssi;
        hasChanged = true;
    }

    return hasChanged;
}

std::string WifiList::formatRadioTypes(const sys_net_radio_types* radioTypes, pb_size_t count) {
    std::string result;

    for (pb_size_t i = 0; i < count; ++i) {
        switch (radioTypes[i]) {
        case sys_net_radio_types_PHY_11B:
            result += "B";
            break;
        case sys_net_radio_types_PHY_11G:
            result += "G";
            break;
        case sys_net_radio_types_PHY_11N:
            result += "N";
            break;
        case sys_net_radio_types_LR:
            result += "L";
            break;
        case sys_net_radio_types_WPS:
            result += "W";
            break;
        case sys_net_radio_types_FTM_RESPONDER:
            result += "FR";
            break;
        case sys_net_radio_types_FTM_INITIATOR:
            result += "FI";
            break;
        case sys_net_radio_types_UNKNOWN:
        default:
            result += "U";
            break;
        }
        if (i < count - 1) {
            result += ",";
        }
    }

    return result;
}

bool WifiList::Update(const wifi_sta_config_t* sta, bool connected) {
    if (!sta) {
        return false; // Invalid input
    }
    if (!Lock()) {
        throw std::runtime_error("Lock failed");
    }
    sys_net_wifi_entry* existingEntry = Get(sta);

    // If the entry does not exist, nothing to update
    if (!existingEntry) {
        Unlock();
        return false;
    }
    auto updated = ToSTAEntry(sta);
    // Check if any relevant fields have changed
    bool hasChanged = false;

    if (strlen(updated.ssid) > 0 && memcmp(existingEntry->ssid, updated.ssid, sizeof(existingEntry->ssid)) != 0) {
        memcpy(existingEntry->ssid, updated.ssid, sizeof(existingEntry->ssid));
        hasChanged = true;
    }

    if (strlen(updated.bssid) > 0 && strcmp(updated.bssid, "00:00:00:00:00:00") != 0 &&
        memcmp(existingEntry->bssid, updated.bssid, sizeof(existingEntry->bssid)) != 0) {
        memcpy(existingEntry->bssid, updated.bssid, sizeof(existingEntry->bssid));
        hasChanged = true;
    }

    if (existingEntry->channel != updated.channel) {
        existingEntry->channel = updated.channel;
        hasChanged = true;
    }

    if (existingEntry->auth_type != updated.auth_type && updated.auth_type != sys_net_auth_types_AUTH_UNKNOWN) {
        existingEntry->auth_type = updated.auth_type;
        hasChanged = true;
    }

    if (areRadioTypesDifferent(existingEntry->radio_type, existingEntry->radio_type_count, updated.radio_type, updated.radio_type_count) &&
        updated.radio_type_count > 0 && updated.radio_type[0] != sys_net_radio_types_UNKNOWN) {
        // Free the old radio_type array if it exists
        delete[] existingEntry->radio_type;

        // Allocate new memory and copy the updated radio types
        existingEntry->radio_type_count = updated.radio_type_count;
        existingEntry->radio_type = new sys_net_radio_types[updated.radio_type_count];
        std::copy(updated.radio_type, updated.radio_type + updated.radio_type_count, existingEntry->radio_type);

        hasChanged = true;
    }

    if (updated.has_last_try) {
        if (memcmp(&existingEntry->last_try, &updated.last_try, sizeof(existingEntry->last_try)) != 0) {
            memcpy(&existingEntry->last_try, &updated.last_try, sizeof(existingEntry->last_try));
            hasChanged = true;
        }
    }

    if (updated.has_last_seen) {
        if (memcmp(&existingEntry->last_seen, &updated.last_seen, sizeof(existingEntry->last_seen)) != 0) {
            memcpy(&existingEntry->last_seen, &updated.last_seen, sizeof(existingEntry->last_seen));
            hasChanged = true;
        }
    }
    if (existingEntry->has_last_try != updated.has_last_try) {
        existingEntry->has_last_try = updated.has_last_try;
        hasChanged = true;
    }
    if (existingEntry->has_last_seen != updated.has_last_seen) {
        existingEntry->has_last_seen = updated.has_last_seen;
        hasChanged = true;
    }
    if (existingEntry->connected != (connected | updated.connected)) {
        existingEntry->connected = connected | updated.connected;
        hasChanged = true;
    }

    if (strlen(updated.password) == 0 && strlen(existingEntry->password) > 0) {
        ESP_LOGW(TAG_CRED_MANAGER, "Updated password is empty, while existing password is %s for %s. Ignoring.", existingEntry->password,
            existingEntry->ssid);
    } else {
        if (memcmp(existingEntry->password, updated.password, sizeof(existingEntry->password)) != 0) {
            memcpy(existingEntry->password, updated.password, sizeof(existingEntry->password));
            hasChanged = true;
        }
    }

    if (existingEntry->rssi != updated.rssi && updated.rssi != 0) {
        existingEntry->rssi = updated.rssi;
        hasChanged = true;
    }
    Release(&updated);
    Unlock();
    return hasChanged;
}
sys_net_wifi_entry WifiList::ToSTAEntry(const sys_net_wifi_entry* sta) {
  if (!sta) {
        throw std::runtime_error("Null STA entry provided");
    }
    sys_net_wifi_entry result = *sta;
    if (result.radio_type_count > 0) {
        std::unique_ptr<sys_net_radio_types[]> newRadioTypes(new sys_net_radio_types[result.radio_type_count]);
        if (!newRadioTypes) {
            throw std::runtime_error("Failed to allocate memory for radio types");
        }
        memcpy(newRadioTypes.get(), sta->radio_type, sizeof(sys_net_radio_types) * result.radio_type_count);
        result.radio_type = newRadioTypes.release();
    } else {
        result.radio_type = nullptr;
    }

    ESP_LOGD(TAG_CRED_MANAGER, "ToSTAEntry: SSID: %s, PASS: %s", result.ssid, result.password);
    return result;
}
sys_net_wifi_entry WifiList::ToSTAEntry(const wifi_sta_config_t* sta, sys_net_auth_types auth_type) {
    return ToSTAEntry(sta, GetRadioTypes(nullptr), auth_type);
}
sys_net_wifi_entry WifiList::ToSTAEntry(
    const wifi_sta_config_t* sta, const std::list<sys_net_radio_types>& radio_types, sys_net_auth_types auth_type) {
    sys_net_wifi_entry item = sys_net_wifi_entry_init_default;
    ESP_LOGD(TAG_CRED_MANAGER,"%s (sta_config)","toSTAEntry");
    auto result = ToSTAEntry(sta, item, radio_types);
    ESP_LOGV(TAG_CRED_MANAGER, "ToSTAEntry: SSID: %s, PASS: %s", result.ssid, result.password);
    return result;
}
sys_net_wifi_entry& WifiList::ToSTAEntry(const wifi_ap_record_t* ap, sys_net_wifi_entry& item) {
    if (ap) {
        auto radioTypes = GetRadioTypes(ap);
        item.radio_type_count=radioTypes.size();
        item.radio_type = new sys_net_radio_types[item.radio_type_count];
        int i = 0;
        for (const auto& type : radioTypes) {
            item.radio_type[i++] = type;
        }
        item.auth_type = GetAuthType(ap);
        FormatBSSID(ap, item);
        item.channel = ap->primary;
        item.rssi = ap->rssi;
        strncpy(item.ssid, GetSSID(ap).c_str(), sizeof(item.ssid));
    }
    return item;
}
sys_net_wifi_entry WifiList::ToSTAEntry(const wifi_ap_record_t* ap) {
    sys_net_wifi_entry item = sys_net_wifi_entry_init_default;
    return ToSTAEntry(ap, item);
}
sys_net_wifi_entry& WifiList::ToSTAEntry(
    const wifi_sta_config_t* sta, sys_net_wifi_entry& item, const std::list<sys_net_radio_types>& radio_types, sys_net_auth_types auth_type) {
    if (!sta) {
        ESP_LOGE(TAG_CRED_MANAGER, "Invalid access point entry");
        return item;
    }
    
    std::string ssid = GetSSID(sta);         // Convert SSID to std::string
    std::string password = GetPassword(sta); // Convert password to std::string

    if (ssid.empty()) {
        ESP_LOGE(TAG_CRED_MANAGER, "Invalid access point ssid");
        return item;
    }
    memset(item.ssid, 0x00, sizeof(item.ssid));
    memset(item.password, 0x00, sizeof(item.password));
    strncpy(item.ssid, ssid.c_str(), sizeof(item.ssid));             // Copy SSID
    strncpy(item.password, password.c_str(), sizeof(item.password)); // Copy password
    if (LOG_LOCAL_LEVEL > ESP_LOG_DEBUG) {
        WifiList::FormatBSSID(item.bssid, sizeof(item.bssid), sta->bssid); // Format BSSID
    }
    item.channel = sta->channel;

    item.auth_type = auth_type;

    // Handle the radio_type array
    if (item.radio_type != nullptr) {
        delete[] item.radio_type; // Free existing array if any
        item.radio_type_count = 0;
    }
    item.radio_type_count = radio_types.size();
    item.radio_type = new sys_net_radio_types[item.radio_type_count];
    int i = 0;
    for (const auto& type : radio_types) {
        item.radio_type[i++] = type;
    }

    ESP_LOGV(TAG_CRED_MANAGER, "ToSTAEntry wifi : %s, password: %s", item.ssid, item.password);
    return item;
}
bool WifiList::RemoveCredential(const wifi_sta_config_t* sta) { return RemoveCredential(GetSSID(sta)); }
bool WifiList::RemoveCredential(const std::string& ssid) {
    auto it = credentials_.find(ssid);
    if (it != credentials_.end()) {
        // Release any dynamically allocated fields in the structure
        Release(&it->second);
        // Erase the entry from the map
        credentials_.erase(it);
        return true;
    }
    return false;
}

void WifiList::Clear() {
    if (Lock()) {
        for (auto& e : credentials_) {
            Release( &e.second);
        }
        credentials_.clear();
        Unlock();
    }
}
bool WifiList::ResetRSSI() {
    if (!Lock()) {
        ESP_LOGE(TAG_CRED_MANAGER, "Unable to lock structure %s", name_.c_str());
        return false;
    }
    for (auto& e : credentials_) {
        e.second.rssi = 0;
    }
    Unlock();
    return true;
}
bool WifiList::ResetConnected() {
    if (!Lock()) {
        throw std::runtime_error("Lock failed");
    }
    for (auto& e : credentials_) {
        e.second.connected = false;
    }
    Unlock();
    return true;
}
sys_net_wifi_entry* WifiList::Get(const std::string& ssid) {
    auto it = credentials_.find(ssid);
    if (it != credentials_.end()) {
        return &(it->second);
    }
    return nullptr;
}
const sys_net_wifi_entry* WifiList::GetConnected() {
    if (!Lock()) {
        ESP_LOGE(TAG_CRED_MANAGER, "Unable to lock structure %s", name_.c_str());
        return nullptr;
    }
    for (auto& e : credentials_) {
        if (e.second.connected) {
            return &e.second;
        }
    }
    Unlock();
    return nullptr;
}
bool WifiList::SetConnected(const wifi_event_sta_connected_t* evt, bool connected) {
    auto ssid = GetSSID(evt);
    auto bssid = GetBSSID(evt);
    auto existing = Get(ssid);
    if (existing) {
        if (bssid != existing->bssid || existing->connected != connected || existing->auth_type != GetAuthType(evt->authmode) ||
            existing->channel != evt->channel) {
            ResetConnected();
            if (!Lock()) {
                throw std::runtime_error("Lock failed");
            }            
            strncpy(existing->bssid, bssid.c_str(), sizeof(existing->bssid));
            existing->connected = connected;
            existing->auth_type = GetAuthType(evt->authmode);
            existing->channel = evt->channel;
            config_raise_changed(false);
            Unlock();
            return true;
        }
    } else {
        ESP_LOGE(TAG_CRED_MANAGER, "Cannot set unknown ssid %s as connected", ssid.c_str());
    }
    return false;
}



sys_net_wifi_entry& WifiList::AddUpdate(const wifi_sta_config_t* sta, sys_net_auth_types auth_type) {
    return AddUpdate(sta, GetRadioTypes(nullptr), auth_type);
}
sys_net_wifi_entry& WifiList::AddUpdate(
    const wifi_sta_config_t* sta, const std::list<sys_net_radio_types>& radio_types, sys_net_auth_types auth_type) {
    auto ssid = GetSSID(sta);
    
    if (!Exists(sta)) {
        auto entry = ToSTAEntry(sta, radio_types, auth_type);
        ESP_LOGD(TAG_CRED_MANAGER, "Added new entry %s to list %s", ssid.c_str(), name_.c_str());
        if (!Lock()) {
            throw std::runtime_error("Lock failed");
        }
        credentials_[ssid] = entry;
        Unlock();
    } else {
        Update(sta);
    }
    ESP_LOGV(TAG_CRED_MANAGER, "AddUpdate: SSID: %s, PASS: %s", ssid.c_str(), credentials_[ssid].password);
    return credentials_[ssid];
}
bool WifiList::UpdateFromClock() {
    bool changed = false;
    if (Lock()) {

        for (auto iter = credentials_.begin(); iter != credentials_.end(); ++iter) {
            bool entrychanged = false;
            if (iter->second.has_last_seen) {
                entrychanged |= OffsetTimeStamp(&iter->second.last_seen);
            }
            if (iter->second.has_last_try) {
                entrychanged |= OffsetTimeStamp(&iter->second.last_try);
            }
            if (entrychanged) {
                ESP_LOGD(TAG_CRED_MANAGER, "Updated from clock");
                PrintWifiSTAEntry(iter->second);
            }
            changed |= entrychanged;
        }

        Unlock();
    }
    return changed;
}
void WifiList::PrintTimeStamp(const google_protobuf_Timestamp* timestamp) {
    if (timestamp == NULL) {
        printf("Timestamp is NULL\n");
        return;
    }

    char buffer[80];

    // Check for special case of time == 0
    if (timestamp->seconds == 0) {
        if (timestamp->nanos != 0) {
            printf("nanos not empty!");
        }
        snprintf(buffer, sizeof(buffer), "%-26s", "N/A");
    }
    // Check for timestamps less than 1704143717 (2024-01-01)
    else if (timestamp->seconds > 0 && timestamp->seconds < 1704143717) {
        // Convert seconds to time_t for use with localtime
        time_t rawtime = (time_t)timestamp->seconds;
        struct tm* timeinfo = localtime(&rawtime);

        strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);
    } else {
        // Convert seconds to time_t for use with localtime
        time_t rawtime = (time_t)timestamp->seconds;
        struct tm* timeinfo = localtime(&rawtime);

        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", timeinfo);
    }

    printf("%-26s", buffer);
}
bool WifiList::UpdateLastTry(const std::string ssid) {
    if (!Lock()) {
        throw std::runtime_error("Lock failed");
    }
    auto entry = Get(ssid);
    ESP_RETURN_ON_FALSE(entry != nullptr, false, TAG_CRED_MANAGER, "Unknown ssid %s", ssid.c_str());
    ESP_RETURN_ON_FALSE(entry->ssid != nullptr, false, TAG_CRED_MANAGER, "Invalid pointer!");
    bool changed = UpdateLastTry(entry);
    Unlock();
    return changed;
}
bool WifiList::UpdateLastTry(sys_net_wifi_entry* entry) {
    ESP_RETURN_ON_FALSE(entry != nullptr, false, TAG_CRED_MANAGER, "Invalid pointer!");
    ESP_RETURN_ON_FALSE(entry->ssid != nullptr, false, TAG_CRED_MANAGER, "Invalid pointer!");
    ESP_LOGV(TAG_CRED_MANAGER, "Updating last try for %s", entry->ssid);
    return UpdateTimeStamp(&entry->last_try, entry->has_last_try);
}
bool WifiList::UpdateLastTry(const wifi_sta_config_t* sta) { return UpdateLastTry(GetSSID(sta)); }
bool WifiList::UpdateLastTry(const wifi_ap_record_t* ap) { return UpdateLastTry(GetSSID(ap)); }
bool WifiList::UpdateLastSeen(const wifi_sta_config_t* sta) { return UpdateLastSeen(GetSSID(sta)); }
bool WifiList::UpdateLastSeen(const wifi_ap_record_t* ap) { return UpdateLastSeen(GetSSID(ap)); }
bool WifiList::UpdateLastSeen(const std::string ssid) {
    if (!Lock()) {
        throw std::runtime_error("Lock failed");
    }
    auto entry = Get(ssid);
    bool changed = false;
    if (entry != nullptr) {
        changed = UpdateLastSeen(entry);
    }
    Unlock();
    return changed;
}
bool WifiList::UpdateLastSeen(sys_net_wifi_entry* entry) {
    ESP_RETURN_ON_FALSE(entry != nullptr, false, TAG_CRED_MANAGER, "Invalid pointer!");
    ESP_LOGV(TAG_CRED_MANAGER, "Updating last seen for %s", entry->ssid);
    return UpdateTimeStamp(&entry->last_seen, entry->has_last_seen);
}
sys_net_wifi_entry& WifiList::AddUpdate(const wifi_ap_record_t* scan_rec) {
    auto ssid = GetSSID(scan_rec);
    if (!Exists(scan_rec)) {
        ESP_LOGV(TAG_CRED_MANAGER, "Added new entry %s to list %s", ssid.c_str(), name_.c_str());
        if (!Lock()) {
            throw std::runtime_error("Lock failed");
        }
        credentials_[ssid] = ToSTAEntry(scan_rec);
        Unlock();
    } else {
        Update(scan_rec);
    }
    return credentials_[ssid];
}
sys_net_wifi_entry& WifiList::AddUpdate(const char* ssid, const char* password) {
    if (ssid == nullptr || password == nullptr) {
        throw std::invalid_argument("SSID and password cannot be null");
    }
    // Ensure that the SSID and password are not too long
    if (std::strlen(ssid) >= sizeof(sys_net_wifi_entry::ssid) || std::strlen(password) >= sizeof(sys_net_wifi_entry::password)) {
        throw std::length_error("SSID or password is too long");
    }
    if (!Exists(ssid)) {
        if (!Lock()) {
            throw std::runtime_error("Lock failed");
        }
        sys_net_wifi_entry newEntry = sys_net_wifi_entry_init_default;
        // Copy the SSID and password into the new entry, ensuring null termination
        std::strncpy(newEntry.ssid, ssid, sizeof(newEntry.ssid) - 1);
        newEntry.ssid[sizeof(newEntry.ssid) - 1] = '\0';
        std::strncpy(newEntry.password, password, sizeof(newEntry.password) - 1);
        newEntry.password[sizeof(newEntry.password) - 1] = '\0';
        ESP_LOGV(TAG_CRED_MANAGER, "Added new entry %s to list %s", ssid, name_.c_str());
        credentials_[ssid] = newEntry;
        Unlock();
    } else {
        auto existing = Get(ssid);
        if (strncmp(existing->password, password, sizeof(existing->password)) != 0) {
            strncpy(existing->password, password, sizeof(existing->password));
            existing->password[sizeof(existing->password) - 1] = '\0';
        }
    }
    return credentials_[ssid];
}
sys_net_wifi_entry& WifiList::AddUpdate(const sys_net_wifi_entry* sta, const char* password) {
    if (sta == nullptr) {
        throw std::invalid_argument("Entry pointer cannot be null");
    }
    auto converted = ToSTAEntry(sta);
    strncpy(converted.password, password, sizeof(converted.password));        
    if (!Exists(sta->ssid)) {
        ESP_LOGD(TAG_CRED_MANAGER, "Added new entry %s to list %s", sta->ssid, name_.c_str());
        if (!Lock()) {
            throw std::runtime_error("Lock failed");
        }
        credentials_[sta->ssid] = converted;
        Unlock();
    } else {
        auto existing = Get(sta->ssid);
        Update(*existing, converted);
        // release the converted structure now
        Release(&converted);
    }
    return credentials_[sta->ssid];
}
void WifiList::PrintString(const char* pData, size_t length, const char* format) {
    std::string buffer;
    for (size_t i = 0; i < length && pData[i]; i++) {
        if (isprint((char)pData[i])) {
            buffer += (char)pData[i]; // Print as a character
        } else {
            buffer += '?';
        }
    }
    printf(format, buffer.c_str());
}
void WifiList::PrintWifiSTAEntryTitle() {
    printf("-----------------------------------------------------------------------------------------------------------------------------------------"
           "--------------------\n");
    printf("CONN SSID                PASSWORD                 BSSID               RSSI  CHAN AUTH          RADIO    LAST TRY                   LAST "
           "SEEN\n");
    printf("-----------------------------------------------------------------------------------------------------------------------------------------"
           "--------------------\n");
}

void WifiList::PrintWifiSTAEntry(const sys_net_wifi_entry& entry) {
    google_protobuf_Timestamp gts = google_protobuf_Timestamp_init_default;
    printf("%-5c", entry.connected ? 'X' : ' ');
    printf("%-20s", entry.ssid);
    PrintString(entry.password, sizeof(entry.password), "%-25s");
    PrintString(entry.bssid, sizeof(entry.bssid), "%-20s");
    printf("%-4ddB", entry.rssi);
    printf("%3u  ", static_cast<unsigned>(entry.channel));
    printf("%-14s", sys_net_auth_types_name(entry.auth_type));
    printf("%-9s", formatRadioTypes(entry.radio_type, entry.radio_type_count).c_str());
    if (entry.has_last_try) {
        PrintTimeStamp(&entry.last_try);
    } else {
        PrintTimeStamp(&gts);
    }
    if (entry.has_last_seen) {
        PrintTimeStamp(&entry.last_seen);
    } else {
        PrintTimeStamp(&gts);
    }
    printf("\n");
}
sys_net_wifi_entry* WifiList::GetIndex(size_t index) {
    if (index >= credentials_.size()) {
        return nullptr;
    }
    auto it = credentials_.begin();
    std::advance(it, index);
    return &(it->second);
}
sys_net_wifi_entry& WifiList::GetStrongestSTA() {
    if (credentials_.empty()) {
        throw std::runtime_error("No credentials available");
    }

    auto strongestIter = credentials_.begin();
    for (auto iter = credentials_.begin(); iter != credentials_.end(); ++iter) {
        if (iter->second.rssi > strongestIter->second.rssi) {
            strongestIter = iter;
        }
    }

    return strongestIter->second;
}

std::list<sys_net_radio_types> WifiList::GetRadioTypes(const wifi_ap_record_t* sta) {
    std::list<sys_net_radio_types> result;
    if (sta == nullptr) {
        result.push_back(sys_net_radio_types_UNKNOWN);
    } else {

        // Check each bit field and return the corresponding enum value
        if (sta->phy_11b) {
            result.push_back(sys_net_radio_types_PHY_11B);
        }
        if (sta->phy_11g) {
            result.push_back(sys_net_radio_types_PHY_11G);
        }
        if (sta->phy_11n) {
            result.push_back(sys_net_radio_types_PHY_11N);
        }
        if (sta->phy_lr) {
            result.push_back(sys_net_radio_types_LR);
        }
        if (sta->wps) {
            result.push_back(sys_net_radio_types_WPS);
        }
        if (sta->ftm_responder) {
            result.push_back(sys_net_radio_types_FTM_RESPONDER);
        }
        if (sta->ftm_initiator) {
            result.push_back(sys_net_radio_types_FTM_INITIATOR);
        }
    }
    return result;
}

wifi_auth_mode_t WifiList::GetESPAuthMode(sys_net_auth_types auth_type) {
    switch (auth_type) {
    case sys_net_auth_types_OPEN:
        return WIFI_AUTH_OPEN;
    case sys_net_auth_types_WEP:
        return WIFI_AUTH_WEP;
    case sys_net_auth_types_WPA_PSK:
        return WIFI_AUTH_WPA_PSK;
    case sys_net_auth_types_WPA2_PSK:
        return WIFI_AUTH_WPA2_PSK;
    case sys_net_auth_types_WPA_WPA2_PSK:
        return WIFI_AUTH_WPA_WPA2_PSK;
    case sys_net_auth_types_WPA2_ENTERPRISE:
        return WIFI_AUTH_WPA2_ENTERPRISE;
    case sys_net_auth_types_WPA3_PSK:
        return WIFI_AUTH_WPA3_PSK;
    case sys_net_auth_types_WPA2_WPA3_PSK:
        return WIFI_AUTH_WPA2_WPA3_PSK;
    case sys_net_auth_types_WAPI_PSK:
        return WIFI_AUTH_WAPI_PSK;
    default:
        return WIFI_AUTH_OPEN; // Default case
    }
}
sys_net_auth_types WifiList::GetAuthType(const wifi_ap_record_t* ap) {
    return ap ? GetAuthType(ap->authmode) : sys_net_auth_types_AUTH_UNKNOWN;
}
sys_net_auth_types WifiList::GetAuthType(const wifi_auth_mode_t mode) {

    switch (mode) {
    case WIFI_AUTH_OPEN:
        return sys_net_auth_types_OPEN;
    case WIFI_AUTH_WEP:
        return sys_net_auth_types_WEP;
    case WIFI_AUTH_WPA_PSK:
        return sys_net_auth_types_WPA_PSK;
    case WIFI_AUTH_WPA2_PSK:
        return sys_net_auth_types_WPA2_PSK;
    case WIFI_AUTH_WPA_WPA2_PSK:
        return sys_net_auth_types_WPA_WPA2_PSK;
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return sys_net_auth_types_WPA2_ENTERPRISE;
    case WIFI_AUTH_WPA3_PSK:
        return sys_net_auth_types_WPA3_PSK;
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return sys_net_auth_types_WPA2_WPA3_PSK;
    case WIFI_AUTH_WAPI_PSK:
        return sys_net_auth_types_WAPI_PSK;
    case WIFI_AUTH_MAX:
        return sys_net_auth_types_OPEN;
    }
    return sys_net_auth_types_AUTH_UNKNOWN;
}
