/*
 *
 *      Sebastien L. 2023, sle118@hotmail.com
 *      Philippe G. 2023, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 *  License Overview:
 *  ----------------
 *  The MIT License is a permissive open source license. As a user of this software, you are free to:
 *  - Use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of this software.
 *  - Use the software for private, commercial, or any other purposes.
 *
 *  Conditions:
 *  - You must include the above copyright notice and this permission notice in all
 *    copies or substantial portions of the Software.
 *
 *  The MIT License offers a high degree of freedom and is well-suited for both open source and
 *  commercial applications. It places minimal restrictions on how the software can be used,
 *  modified, and redistributed. For more details on the MIT License, please refer to the link above.
 */
#pragma once
#include "Network.pb.h"
#include "Status.pb.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "PBW.h"
#include "pb_decode.h"
#include "pb_encode.h"
#ifdef __cplusplus
#include "Locking.h"
#include <cstring>
#include <list>
#include <map>
#include <string>

class WifiList : public System::Locking {
  public:
    WifiList(std::string name) : System::Locking(name), name_(name) {}
    ~WifiList() { Clear(); }
    static std::string toString(const uint8_t* data, size_t max_length) {
        // Find the actual length of the string up to max_length
        size_t length = strnlen(reinterpret_cast<const char*>(data), max_length);
        // Construct a std::string using the data and its length
        auto p = std::string(reinterpret_cast<const char*>(data), length);
        return p;
    }
    static void Release(sys_net_wifi_entry& entry) { Release(&entry); }
    static void Release(sys_net_wifi_entry* entry) { pb_release(&sys_net_wifi_entry_msg, entry); }
    static std::list<sys_net_radio_types> GetRadioTypes(const wifi_ap_record_t* sta);
    static wifi_auth_mode_t GetESPAuthMode(sys_net_auth_types auth_type);
    static sys_net_auth_types GetAuthType(const wifi_ap_record_t* ap);
    static sys_net_auth_types GetAuthType(const wifi_auth_mode_t mode);
    static bool areRadioTypesDifferent(const sys_net_radio_types* types1, pb_size_t count1, const sys_net_radio_types* types2, pb_size_t count2) {
        if(count1 != count2) { return true; }

        for(pb_size_t i = 0; i < count1; ++i) {
            if(types1[i] != types2[i]) { return true; }
        }

        return false;
    }
    static std::string GetPassword(const wifi_sta_config_t* config) {
        auto p = toString(config->password, sizeof(config->password));
        return p;
    }

    static std::string GetSSID(const wifi_event_sta_connected_t* evt) { return toString(evt->ssid, sizeof(evt->ssid)); }
    static std::string GetSSID(const wifi_sta_config_t* config) { return toString(config->ssid, sizeof(config->ssid)); }
    static std::string GetSSID(const wifi_ap_record_t* ap) { return toString(ap->ssid, sizeof(ap->ssid)); }
    static void FormatBSSID(char* buffer, size_t len, const uint8_t* bssid) {
        memset(buffer, 0x00, len);
        snprintf(buffer, len, "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    }
    static void FormatBSSID(const wifi_ap_record_t* ap, sys_net_wifi_entry& entry) {
        memset(entry.bssid, 0x00, sizeof(entry.bssid));
        if(!ap) return;
        FormatBSSID(entry.bssid, sizeof(entry.bssid), ap->bssid);
    }
    static std::string GetBSSID(const wifi_event_sta_connected_t* evt);
    static bool isEmpty(const char* str, size_t len);
    static void PrintString(const char* pData, size_t length, const char* format);
    static void PrintWifiSTAEntryTitle();

    sys_net_wifi_entry& GetStrongestSTA();
    bool RemoveCredential(const std::string& ssid);
    bool RemoveCredential(const wifi_sta_config_t* sta);
    void Clear();

    static sys_net_wifi_entry ToSTAEntry(const sys_net_wifi_entry* sta);
    static sys_net_wifi_entry ToSTAEntry(const wifi_sta_config_t* sta, const std::list<sys_net_radio_types>& radio_types,
        sys_net_auth_types auth_type = sys_net_auth_types_AUTH_UNKNOWN);
    static sys_net_wifi_entry ToSTAEntry(const wifi_sta_config_t* sta, sys_net_auth_types auth_type = sys_net_auth_types_AUTH_UNKNOWN);
    static sys_net_wifi_entry& ToSTAEntry(const wifi_ap_record_t* ap, sys_net_wifi_entry& item);
    static sys_net_wifi_entry ToSTAEntry(const wifi_ap_record_t* ap);
    static sys_net_wifi_entry& ToSTAEntry(const wifi_sta_config_t* sta, sys_net_wifi_entry& item, const std::list<sys_net_radio_types>& radio_types,
        sys_net_auth_types auth_type = sys_net_auth_types_AUTH_UNKNOWN);

    static void PrintTimeStamp(const google_protobuf_Timestamp* timestamp);
    static void PrintWifiSTAEntry(const sys_net_wifi_entry& entry);
    static std::string formatRadioTypes(const sys_net_radio_types* radioTypes, pb_size_t count);

    static bool OffsetTimeStamp(google_protobuf_Timestamp* ts);
    static bool UpdateTimeStamp(google_protobuf_Timestamp* ts, bool& has_flag_val);

    bool ResetRSSI();
    bool ResetConnected();
    const sys_net_wifi_entry* GetConnected();

    bool SetConnected(const wifi_event_sta_connected_t* evt, bool connected = true);
    size_t GetCount() const { return credentials_.size(); }
    sys_net_wifi_entry* GetIndex(size_t index);

    bool Exists(const std::string& ssid) { return Get(ssid) != nullptr; }
    bool Exists(const char* ssid) { return Get(ssid) != nullptr; }
    bool Exists(const wifi_ap_record_t* ap) { return ap != nullptr && Get(GetSSID(ap)) != nullptr; }
    bool Exists(const wifi_sta_config_t* sta) { return Exists(GetSSID(sta)); }

    bool Update(const wifi_sta_config_t* sta, bool connected = false);
    bool Update(const wifi_ap_record_t* ap, bool connected = false);
    static bool Update(sys_net_wifi_entry& existingEntry, sys_net_wifi_entry& compared);

    sys_net_wifi_entry* Get(const wifi_sta_config_t* sta) { return Get(GetSSID(sta)); }
    sys_net_wifi_entry* Get(const wifi_ap_record_t* ap) { return Get(GetSSID(ap)); }
    sys_net_wifi_entry* Get(const char* ssid) { return Get(std::string(ssid)); }
    sys_net_wifi_entry* Get(const std::string& ssid);

    bool UpdateFromClock();
    bool UpdateLastTry(const wifi_sta_config_t* sta);
    bool UpdateLastTry(const wifi_ap_record_t* ap);
    bool UpdateLastTry(const std::string ssid);
    bool UpdateLastTry(sys_net_wifi_entry* entry);
    bool UpdateLastSeen(const wifi_sta_config_t* sta);
    bool UpdateLastSeen(const wifi_ap_record_t* ap);
    bool UpdateLastSeen(const std::string ssid);
    bool UpdateLastSeen(sys_net_wifi_entry* entry);
    sys_net_wifi_entry& AddUpdate(const wifi_sta_config_t* sta, sys_net_auth_types auth_type = sys_net_auth_types_AUTH_UNKNOWN);
    sys_net_wifi_entry& AddUpdate(const wifi_sta_config_t* sta, const std::list<sys_net_radio_types>& radio_types,
        sys_net_auth_types auth_type = sys_net_auth_types_AUTH_UNKNOWN);
    sys_net_wifi_entry& AddUpdate(const wifi_ap_record_t* scan_rec);
    sys_net_wifi_entry& AddUpdate(const char* ssid = "", const char* password = "");
    sys_net_wifi_entry& AddUpdate(const sys_net_wifi_entry* existing, const char* password = "");
    // this one below is used by pb_decode
    void AddUpdate(const sys_net_wifi_entry& entry) {
        if(!Lock()) { throw std::runtime_error("Lock failed"); }
        credentials_[entry.ssid] = entry;
        Unlock();
    }
    using Iterator = std::map<std::string, sys_net_wifi_entry>::iterator;
    using ConstIterator = std::map<std::string, sys_net_wifi_entry>::const_iterator;
    Iterator begin() { return credentials_.begin(); }
    ConstIterator begin() const { return credentials_.begin(); }
    Iterator end() { return credentials_.end(); }
    ConstIterator end() const { return credentials_.end(); }

  private:
    static std::string FormatTimestamp(const google_protobuf_Timestamp& timestamp) {
        // Format the timestamp as needed
        // This is a placeholder implementation.
        return std::to_string(timestamp.seconds) + "s";
    }
    std::map<std::string, sys_net_wifi_entry> credentials_;
    std::string name_; // Name of the WifiCredentialsManager
};

extern "C" {
#endif
typedef struct WifiList WifiList;

#ifdef __cplusplus
}
#endif