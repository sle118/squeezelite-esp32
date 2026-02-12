#ifdef NETWORK_WIFI_LOG_LEVEL
#define LOG_LOCAL_LEVEL NETWORK_WIFI_LOG_LEVEL
#endif
#include "network_wifi.h"
#include "Config.h"
#include "WifiList.h"
#include "accessors.h"
#include "bootstate.h"
#include "cJSON.h"
#include "dns_server.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "lwip/sockets.h"
#include "messaging.h"
#include "network_status.h"
#include "platform_esp32.h"
#include "tools.h"
#include "trace.h"
#include <string.h>
#include "esp_sntp.h"

WifiList* wifi_config_aps;
EXT_RAM_ATTR WifiList scan_results_aps("scan_results");

static void network_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static const char* get_disconnect_code_desc(uint8_t reason);
esp_err_t network_wifi_get_blob(void* target, size_t size, const char* key);

static const char TAG[] = "network_wifi";
esp_netif_t* wifi_netif;
esp_netif_t* wifi_ap_netif;
extern sys_status_data status;
static void time_sync_notification_cb(struct timeval* tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    wifi_config_aps->UpdateFromClock();
    scan_results_aps.UpdateFromClock();
}
static bool is_initialized() { return (platform != NULL) && platform->has_net; }

// void network_wifi_update_connected() {
//     wifi_config_t config;
//     esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &config);
//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "Unable to update connected STA: %s", esp_err_to_name(err));
//         return;
//     }

//     auto sta = wifi_config_aps->AddUpdate(&config.sta);
//     ESP_LOGD(TAG, "Added/Updated STA %s to credentials list, password: %s", sta.ssid, sta.password);
//     wifi_config_aps->PrintWifiSTAEntry(sta);
//     if (sys_status_obj.Lock()) {
//         sta.connected = true;
//         auto status = sys_status_obj.get();
//         status.has_net = true;
//         status.net.has_wifi = true;
//         memcpy(&status.net.wifi.connected_sta, &sta, sizeof(status.net.wifi.connected_sta));
//         status.net.wifi.has_connected_sta = true;
//         sys_status_obj.Unlock();
//     }
//     config_raise_changed(false);
// }
size_t network_wifi_get_known_count() {
    size_t result = 0;
    if(!is_initialized()) {
        ESP_LOGE(TAG, "Config not initialized");
        return 0;
    }
    result = wifi_config_aps->GetCount();
    ESP_LOGD(TAG, "SSID Count from config: %d", result);

    return result;
}

const wifi_sta_config_t* network_wifi_get_active_config() {
    static wifi_config_t config;
    esp_err_t err = ESP_OK;
    memset(&config, 0x00, sizeof(config));
    if((err = esp_wifi_get_config(WIFI_IF_STA, &config)) == ESP_OK) {
        return &config.sta;
    } else {
        ESP_LOGD(TAG, "Could not get wifi STA config: %s", esp_err_to_name(err));
    }
    return nullptr;
}

bool network_wifi_is_known_ap(const char* ssid) { return wifi_config_aps->Exists(ssid); }

esp_netif_t* network_wifi_get_interface() { return wifi_netif; }
esp_netif_t* network_wifi_get_ap_interface() { return wifi_ap_netif; }
esp_err_t network_wifi_set_sta_mode() {
    if(!wifi_netif) {
        ESP_LOGE(TAG, "Wifi not initialized. Cannot set sta mode");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGD(TAG, "Set Mode to STA");
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Error setting mode to STA: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Starting wifi");
        err = esp_wifi_start();
        if(err != ESP_OK) { ESP_LOGE(TAG, "Error starting wifi: %s", esp_err_to_name(err)); }
    }
    ESP_LOGD(TAG, "Done setting wifi mode to STA");
    return err;
}
esp_netif_t* network_wifi_start() {
    esp_err_t err = ESP_OK;
    if(!wifi_netif) {
        wifi_netif = esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "Error initializing wifi: %s", esp_err_to_name(err));
            return wifi_netif;
        }
        // network_wifi_register_handlers();
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &network_wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    }
    network_wifi_set_sta_mode();
    network_set_hostname(wifi_netif);
    return wifi_netif;
}
void destroy_network_wifi() {}

bool network_wifi_sta_config_changed() { return wifi_config_aps->Update(network_wifi_get_active_config()); }
esp_err_t network_wifi_set_connected(bool connected) {
    wifi_config_t config;
    bool state_changed = false;
    bool config_changed = false;
    ESP_LOGD(TAG, "Resetting connected list");
    if(wifi_config_aps->Lock()) {
        wifi_config_aps->ResetConnected();
        wifi_config_aps->Unlock();
    }
    ESP_LOGD(TAG, "Erasing connected sta object");
    if(sys_status_obj.get()->net.wifi.has_connected_sta && sys_status_obj.Lock()) {
        pb_release(&sys_net_wifi_entry_msg, &sys_status_obj.get()->net.wifi.connected_sta);
        sys_status_obj.get()->net.has_wifi = true;
        sys_status_obj.get()->has_net = true;
        sys_status_obj.get()->net.wifi.has_connected_sta = false;
        sys_status_obj.Unlock();
    }
    if(stateWrapper.get()->has_connected_sta && stateWrapper.Lock()) {
        pb_release(&sys_net_wifi_entry_msg, stateWrapper.get());
        stateWrapper.get()->has_connected_sta = false;
        stateWrapper.Unlock();
        state_changed = true;
    }

    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &config);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Error retrieving wifi config");
    } else {
        if(!connected) {
            ESP_LOGI(TAG, "Connected list was reset");
        } else {
            ESP_LOGI(TAG, "Updating connected sta to %s", WifiList::GetSSID(&config.sta).c_str());
            ESP_LOGD(TAG, "Locking credentials manager");
            if(wifi_config_aps->Lock()) {
                ESP_LOGD(TAG, "Adding/updating credentials");
                wifi_config_aps->AddUpdate(&config.sta).connected = true;
                ESP_LOGD(TAG, "Unlocking credentials manager");
                config_changed = true;
                wifi_config_aps->Unlock();
            }
        }
    }
    if(config_changed) {
        ESP_LOGD(TAG, "Raising changed event on the config wrapper object");
        configWrapper.RaiseChangedAsync();
    }
    if(state_changed) {
        ESP_LOGD(TAG, "Raising changed event on the state wrapper object");
        stateWrapper.RaiseChangedAsync();
    }
    return err;
}
static void network_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if(event_base != WIFI_EVENT) return;
    switch(event_id) {

    case WIFI_EVENT_WIFI_READY:
        ESP_LOGD(TAG, "WIFI_EVENT_WIFI_READY");
        break;

    case WIFI_EVENT_SCAN_DONE:
        ESP_LOGD(TAG, "WIFI_EVENT_SCAN_DONE");
        network_async_scan_done();
        break;

    case WIFI_EVENT_STA_AUTHMODE_CHANGE:
        ESP_LOGD(TAG, "WIFI_EVENT_STA_AUTHMODE_CHANGE");
        break;

    case WIFI_EVENT_AP_START:
        ESP_LOGD(TAG, "WIFI_EVENT_AP_START");
        break;

    case WIFI_EVENT_AP_STOP:
        ESP_LOGD(TAG, "WIFI_EVENT_AP_STOP");
        break;

    case WIFI_EVENT_AP_PROBEREQRECVED: {
        wifi_event_ap_probe_req_rx_t* s = (wifi_event_ap_probe_req_rx_t*)event_data;
        char* mac = alloc_get_formatted_mac_string(s->mac);
        if(mac) { ESP_LOGD(TAG, "WIFI_EVENT_AP_PROBEREQRECVED. RSSI: %d, MAC: %s", s->rssi, STR_OR_BLANK(mac)); }
        FREE_AND_NULL(mac);
    } break;
    case WIFI_EVENT_STA_WPS_ER_SUCCESS:
        ESP_LOGD(TAG, "WIFI_EVENT_STA_WPS_ER_SUCCESS");
        break;
    case WIFI_EVENT_STA_WPS_ER_FAILED:
        ESP_LOGD(TAG, "WIFI_EVENT_STA_WPS_ER_FAILED");
        break;
    case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
        ESP_LOGD(TAG, "WIFI_EVENT_STA_WPS_ER_TIMEOUT");
        break;
    case WIFI_EVENT_STA_WPS_ER_PIN:
        ESP_LOGD(TAG, "WIFI_EVENT_STA_WPS_ER_PIN");
        break;
    case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t* stac = (wifi_event_ap_staconnected_t*)event_data;
        char* mac = alloc_get_formatted_mac_string(stac->mac);
        if(mac) { ESP_LOGD(TAG, "WIFI_EVENT_AP_STACONNECTED. aid: %d, mac: %s", stac->aid, STR_OR_BLANK(mac)); }
        FREE_AND_NULL(mac);
    } break;
    case WIFI_EVENT_AP_STADISCONNECTED:
        ESP_LOGD(TAG, "WIFI_EVENT_AP_STADISCONNECTED");
        break;

    case WIFI_EVENT_STA_START:
        ESP_LOGD(TAG, "WIFI_EVENT_STA_START");
        break;

    case WIFI_EVENT_STA_STOP:
        ESP_LOGD(TAG, "WIFI_EVENT_STA_STOP");
        break;

    case WIFI_EVENT_STA_CONNECTED: {
        ESP_LOGD(TAG, "WIFI_EVENT_STA_CONNECTED. ");
        network_async(EN_CONNECTED);

    } break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        if(is_restarting()) {
            ESP_LOGD(TAG, "Disconnect event ignored while restarting");
            return;
        }
        wifi_event_sta_disconnected_t* s = (wifi_event_sta_disconnected_t*)event_data;
        char* bssid = alloc_get_formatted_mac_string(s->bssid);
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED. From BSSID: %s, reason code: %d (%s)", STR_OR_BLANK(bssid), s->reason,
            get_disconnect_code_desc(s->reason));
        FREE_AND_NULL(bssid);

        if(s->reason == WIFI_REASON_ROAMING) {
            ESP_LOGI(TAG, "WiFi Roaming to new access point");
        } else {
            network_async_lost_connection((wifi_event_sta_disconnected_t*)event_data);
            network_wifi_set_connected(false);
        }
    } break;

    default:
        break;
    }
}

void network_wifi_global_init() {
    ESP_LOGD(TAG, "running network_wifi_global_init");
    assert(platform != nullptr);
    wifi_config_aps = reinterpret_cast<WifiList*>(platform->net.credentials);
    assert(wifi_config_aps != nullptr);
    sys_status_obj.get()->has_net = true;
    sys_status_obj.get()->net.has_wifi = true;
    sys_status_obj.get()->net.wifi.scan_result = reinterpret_cast<sys_net_wifi_entry*>(&scan_results_aps);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
}

esp_netif_t* network_wifi_config_ap() {
    esp_netif_ip_info_t info;
    esp_err_t err = ESP_OK;
    wifi_config_t ap_config;
    memset(&ap_config, 0x00, sizeof(ap_config));

    ESP_LOGI(TAG, "Configuring Access Point.");
    if(!wifi_ap_netif) { wifi_ap_netif = esp_netif_create_default_wifi_ap(); }
    if(!is_initialized()) {
        ESP_LOGE(TAG, "Config not initialized");
        return wifi_ap_netif;
    }
    if(platform->has_net && platform->net.has_ap && platform->net.ap.has_ip) {
        inet_pton(AF_INET, platform->net.ap.ip.ip, (ip4_addr_t*)&info.ip);           /* access point is on a static IP */
        inet_pton(AF_INET, platform->net.ap.ip.gw, (ip4_addr_t*)&info.gw);           /* access point is on a static IP */
        inet_pton(AF_INET, platform->net.ap.ip.netmask, (ip4_addr_t*)&info.netmask); /* access point is on a static IP */
    } else {
        ESP_LOGE(TAG, "Access point not configured. Bailing out");
        return nullptr;
    }
    ap_config.ap.max_connection = platform->net.ap.max_connection > 0 ? platform->net.ap.max_connection : 4;
    ap_config.ap.beacon_interval = platform->net.ap.beacon_interval > 0 ? platform->net.ap.beacon_interval : 100;
    ap_config.ap.ssid_hidden = platform->net.ap.hidden;
    ap_config.ap.authmode = WifiList::GetESPAuthMode(platform->net.ap.auth_mode);
    ap_config.ap.channel = platform->net.ap.channel;
    /* In order to change the IP info structure, we have to first stop
     * the DHCP server on the new interface
     */
    network_start_stop_dhcps(wifi_ap_netif, false);
    ESP_LOGD(TAG, "Setting tcp_ip info for access point");
    if((err = esp_netif_set_ip_info(wifi_ap_netif, &info)) != ESP_OK) {
        ESP_LOGE(TAG, "Setting tcp_ip info for interface TCPIP_ADAPTER_IF_AP. Error %s", esp_err_to_name(err));
        return wifi_ap_netif;
    }
    network_start_stop_dhcps(wifi_ap_netif, true);

    strlcpy((char*)ap_config.ap.ssid, platform->names.wifi_ap_name, sizeof(ap_config.ap.ssid));
    strlcpy((char*)ap_config.ap.password, STR_OR_BLANK(platform->net.ap.password), sizeof(ap_config.ap.password));
    ESP_LOGI(TAG, "AP SSID: %s, PASSWORD: %s Auth Mode: %d SSID: %s Max Connections: %d Beacon interval: %d", (char*)ap_config.ap.ssid,
        (char*)ap_config.ap.password, ap_config.ap.authmode, ap_config.ap.ssid_hidden ? "HIDDEN" : "VISIBLE", ap_config.ap.max_connection,
        ap_config.ap.beacon_interval);

    const char* msg = "Setting wifi mode as WIFI_MODE_APSTA";
    ESP_LOGD(TAG, "%s", msg);
    if((err = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) {
        ESP_LOGE(TAG, "%s. Error %s", msg, esp_err_to_name(err));
        return wifi_ap_netif;
    }
    msg = "Setting wifi AP configuration for WIFI_IF_AP";
    ESP_LOGD(TAG, "%s", msg);
    if((err = esp_wifi_set_config(WIFI_IF_AP, &ap_config)) != ESP_OK) /* stop AP DHCP server */
    {
        ESP_LOGE(TAG, "%s . Error %s", msg, esp_err_to_name(err));
        return wifi_ap_netif;
    }

    msg = "Setting wifi bandwidth";
    ESP_LOGD(TAG, "%s (%d)", msg, DEFAULT_AP_BANDWIDTH);
    if((err = esp_wifi_set_bandwidth(WIFI_IF_AP, DEFAULT_AP_BANDWIDTH)) != ESP_OK) /* stop AP
    DHCP server */
    {
        ESP_LOGE(TAG, "%s failed. Error %s", msg, esp_err_to_name(err));
        return wifi_ap_netif;
    }

    msg = "Setting wifi power save";
    ESP_LOGD(TAG, "%s (%s)", msg, sys_net_config_ps_types_name(platform->net.power_save_mode));

    if((err = esp_wifi_set_ps((wifi_ps_type_t)platform->net.power_save_mode)) != ESP_OK) /* stop AP DHCP server */
    {
        ESP_LOGE(TAG, "%s failed. Error %s", msg, esp_err_to_name(err));
        return wifi_ap_netif;
    }
    ESP_LOGD(TAG, "Done configuring Soft Access Point");
    return wifi_ap_netif;
}

void network_wifi_filter_unique(wifi_ap_record_t* aplist, uint16_t* aps) {
    int total_unique;
    wifi_ap_record_t* first_free;
    total_unique = *aps;

    first_free = NULL;

    for(int i = 0; i < *aps - 1; i++) {
        wifi_ap_record_t* ap = &aplist[i];

        /* skip the previously removed APs */
        if(ap->ssid[0] == 0) continue;

        /* remove the identical SSID+authmodes */
        for(int j = i + 1; j < *aps; j++) {
            wifi_ap_record_t* ap1 = &aplist[j];
            if((strcmp((const char*)ap->ssid, (const char*)ap1->ssid) == 0) &&
                (ap->authmode == ap1->authmode)) { /* same SSID, different auth mode is skipped */
                /* save the rssi for the display */
                if((ap1->rssi) > (ap->rssi)) ap->rssi = ap1->rssi;
                /* clearing the record */
                memset(ap1, 0, sizeof(wifi_ap_record_t));
            }
        }
    }
    /* reorder the list so APs follow each other in the list */
    for(int i = 0; i < *aps; i++) {
        wifi_ap_record_t* ap = &aplist[i];
        /* skipping all that has no name */
        if(ap->ssid[0] == 0) {
            /* mark the first free slot */
            if(first_free == NULL) first_free = ap;
            total_unique--;
            continue;
        }
        if(first_free != NULL) {
            memcpy(first_free, ap, sizeof(wifi_ap_record_t));
            memset(ap, 0, sizeof(wifi_ap_record_t));
            /* find the next free slot */
            for(int j = 0; j < *aps; j++) {
                if(aplist[j].ssid[0] == 0) {
                    first_free = &aplist[j];
                    break;
                }
            }
        }
    }
    /* update the length of the list */
    *aps = total_unique;
}

bool network_wifi_update_scan_list(wifi_ap_record_t* accessp_records, uint16_t count) {
    bool changed = false;
    // reset RSSI values. This will help when choosing the
    // strongest signal to connect to
    wifi_config_aps->ResetRSSI();
    // clear current access point list
    scan_results_aps.Clear();
    if(count > 0) {
        WifiList::PrintWifiSTAEntryTitle();
        ESP_LOGD(TAG, "Found %d AP(s) during scan:", count);
        for(int i = 0; i < count; i++) {
            auto& entry = scan_results_aps.AddUpdate(&accessp_records[i]);
            scan_results_aps.UpdateLastSeen(&accessp_records[i]);

            if(wifi_config_aps->Exists(&accessp_records[i]) && configWrapper.Lock()) {
                ESP_LOGD(TAG, "AP with known password. updating last seen");
                changed = wifi_config_aps->Update(&accessp_records[i]) || changed;
                changed = wifi_config_aps->UpdateLastSeen(&accessp_records[i]) || changed;
                configWrapper.Unlock();
            }
            ESP_LOGV(TAG, "Address of entry is %p", static_cast<void*>(&entry));

            if(LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG) { WifiList::PrintWifiSTAEntry(entry); }
        }

    } else {
        ESP_LOGI(TAG, "No nearby access point found");
    }
    if(changed) { config_raise_changed(false); }
    return true;
}
esp_err_t wifi_scan_done() {
    esp_err_t err = ESP_OK;
    wifi_ap_record_t* accessp_records = NULL;
    ESP_LOGD(TAG, "Getting AP list records");
    uint16_t ap_num = platform->net.max_ap_num;
    if((err = esp_wifi_scan_get_ap_num(&ap_num)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to retrieve scan results count. Error %s", esp_err_to_name(err));
        return err;
    }
    if(ap_num <= 0) {
        ESP_LOGI(TAG, "No AP found during scan");
        return err;
    }
    accessp_records = (wifi_ap_record_t*)malloc_init_external(sizeof(wifi_ap_record_t) * ap_num);
    if(!accessp_records) {
        ESP_LOGE(TAG, "Alloc failed for scan results");
        return ESP_FAIL;
    }
    if((err = esp_wifi_scan_get_ap_records(&ap_num, accessp_records)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to retrieve scan results list. Error %s", esp_err_to_name(err));
    } else {
        /* make sure the http server isn't trying to access the list while it gets refreshed */
        ESP_LOGD(TAG, "Retrieving scan list");
        /* Will remove the duplicate SSIDs from the list and update ap_num */
        network_wifi_filter_unique(accessp_records, &ap_num);

        if(!network_wifi_update_scan_list(accessp_records, ap_num)) {
            ESP_LOGE(TAG, "could not get access to json mutex in wifi_scan");
            err = ESP_FAIL;
        }
        ESP_LOGD(TAG, "Done retrieving scan list");
    }
    free(accessp_records);
    return err;
}
bool is_wifi_up() { return wifi_netif != NULL; }
esp_err_t network_wifi_start_scan() {
    wifi_scan_config_t scan_config = {.ssid = 0, .bssid = 0, .channel = 0, .show_hidden = true, .scan_type = WIFI_SCAN_TYPE_ACTIVE};
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Initiating wifi network scan");
    if(!is_wifi_up()) {
        messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Wifi not started. Cannot scan");
        return ESP_FAIL;
    }
    /* if a scan is already in progress this message is simply ignored thanks to the
     * WIFI_MANAGER_SCAN_BIT uxBit */
    if((err = esp_wifi_scan_start(&scan_config, false)) != ESP_OK) {
        ESP_LOGW(TAG, "Unable to start scan; %s ", esp_err_to_name(err));
        //						set_status_message(WARNING, "Wifi Connecting. Cannot start scan.");
        messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Scanning failed: %s", esp_err_to_name(err));
    }
    return err;
}

bool network_wifi_is_ap_mode() {
    wifi_mode_t mode;
    /* update config to latest and attempt connection */
    return esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_AP;
}
bool network_wifi_is_sta_mode() {
    wifi_mode_t mode;
    /* update config to latest and attempt connection */
    return esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_STA;
}
bool network_wifi_is_ap_sta_mode() {
    wifi_mode_t mode;
    /* update config to latest and attempt connection */
    return esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_APSTA;
}

esp_err_t network_wifi_connect(const char* ssid, const char* password) {
    esp_err_t err = ESP_OK;
    wifi_config_t config;
    bool changed = false;

    memset(&config, 0x00, sizeof(config));
    ESP_LOGD(TAG, "network_wifi_connect");
    network_wifi_set_connected(false);

    if(!is_wifi_up()) {
        messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Wifi not started. Cannot connect");
        return ESP_FAIL;
    }
    if(!ssid || !password || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Cannot connect wifi. wifi config is null!");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_mode_t wifi_mode;
    err = esp_wifi_get_mode(&wifi_mode);
    if(err == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "Wifi not initialized. Attempting to start sta mode");
        network_wifi_start();
    } else if(err != ESP_OK) {
        ESP_LOGE(TAG, "Could not retrieve wifi mode : %s", esp_err_to_name(err));
    } else if(wifi_mode != WIFI_MODE_STA && wifi_mode != WIFI_MODE_APSTA) {
        ESP_LOGD(TAG, "Changing wifi mode to STA");
        err = network_wifi_set_sta_mode();
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "Could not set mode to STA.  Cannot connect to SSID %s", ssid);
            return err;
        }
    }
    // copy configuration and connect
    strlcpy((char*)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    if(password) { strlcpy((char*)config.sta.password, password, sizeof(config.sta.password)); }

    config.sta.scan_method = platform->net.wifi_connect_fast_scan ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN;
    auto existing = wifi_config_aps->Get(&config.sta);
    if(existing != nullptr) {
        if(existing->channel > 0 && existing->channel <= 13) {
            ESP_LOGD(TAG, "Trying to connect on channel %d", existing->channel);
            config.sta.channel = existing->channel;
        } else {
            ESP_LOGD(TAG, "Access point known, but no channel defined. Scanning");
        }
    }

    // First Disconnect
    esp_wifi_disconnect();

    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    if((err = esp_wifi_set_config(WIFI_IF_STA, &config)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA configuration. Error %s", esp_err_to_name(err));
    }
    if(err == ESP_OK) {
        ESP_LOGI(TAG, "Wifi Connecting to %s...", ssid);
        if((err = esp_wifi_connect()) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initiate wifi connection. Error %s", esp_err_to_name(err));
        } else {
            if(wifi_config_aps->Lock()) {
                ESP_LOGD(TAG, "Storing/updating credentials entry");
                if(wifi_config_aps->Exists(&config.sta)) {
                    ESP_LOGD(TAG, "Existing entry. Updating it");
                    changed = wifi_config_aps->Update(&config.sta);
                } else {
                    ESP_LOGD(TAG, "New entry. Adding it");
                    wifi_config_aps->AddUpdate(&config.sta);
                }
                auto entry = wifi_config_aps->Get(&config.sta);

                if(entry != nullptr) {
                    wifi_config_aps->UpdateLastTry(&config.sta);
                    WifiList::PrintWifiSTAEntryTitle();
                    WifiList::PrintWifiSTAEntry(*entry);
                }
                wifi_config_aps->Unlock();
            }

            if(changed) { configWrapper.RaiseChangedAsync(); }
        }
    }
    return err;
}

esp_err_t network_wifi_connect_ssid(const char* ssid) {
    sys_net_wifi_entry* item = wifi_config_aps->Get(std::string(ssid));
    if(item != nullptr) {
        return network_wifi_connect(item->ssid, item->password);
    } else {
        ESP_LOGE(TAG, "Attempting to connect to an unknown ssid: %s", ssid);
    }
    return ESP_FAIL;
}
esp_err_t network_wifi_connect_active_ssid() {
    sys_net_wifi_entry* connected = NULL;
    if(sys_state->has_connected_sta && strlen(sys_state->connected_sta.ssid) > 0) {
        connected = &sys_state->connected_sta;

        if(LOG_LOCAL_LEVEL >= ESP_LOG_INFO) {
            ESP_LOGI(TAG, "Connecting to active access point");
            WifiList::PrintWifiSTAEntryTitle();
            WifiList::PrintWifiSTAEntry(sys_state->connected_sta);
        }
        return network_wifi_connect(connected->ssid, connected->password);
    }
    return ESP_ERR_NOT_FOUND;
}
esp_err_t network_wifi_add_ssid(const char* ssid, const char* password) {
    bool changed = false;
    auto conn = wifi_config_aps->Get(ssid);
    if(conn != nullptr) {
        ESP_LOGW(TAG, "Existing ssid %s. Updating password", ssid);
        if(strcmp(conn->password, password) != 0) {
            strncpy(conn->password, password, sizeof(conn->password));
            changed = true;
        } else {
            ESP_LOGI(TAG, "No change to ssid %s", password);
        }
    } else {
        ESP_LOGI(TAG, "Adding new ssid %s with password %s to credentials", ssid, STR_OR_BLANK(password));
        auto seen = scan_results_aps.Get(ssid);
        if(seen) {
            ESP_LOGD(TAG, "Adding with extra info from scan");
            wifi_config_aps->AddUpdate(seen, STR_OR_BLANK(password));
            changed = true;
        } else {
            ESP_LOGD(TAG, "Adding without extra info from scan");
            wifi_config_aps->AddUpdate(ssid, STR_OR_BLANK(password));
            changed = true;
        }
    }

    if(changed) { config_raise_changed(false); }
    return ESP_OK;
}
esp_err_t network_wifi_remove_ssid(const char* ssid) {
    wifi_config_t config;

    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &config);
    if(err != ESP_OK) { ESP_LOGE(TAG, "Error retrieving wifi config"); }
    if((network_wifi_is_sta_mode() || network_wifi_is_ap_sta_mode()) && WifiList::GetSSID(&config.sta) == ssid) {
        ESP_LOGI(TAG, "Disconnecting from deleted ssid: %s", ssid);
        esp_wifi_disconnect();
    }
    if(wifi_config_aps->RemoveCredential(std::string(ssid))) {
        ESP_LOGI(TAG, "SSID %s was removed", ssid);
        config_raise_changed(false);
    } else {
        ESP_LOGI(TAG, "SSID %s could not be removed", ssid);
    }

    return ESP_OK;
}
void network_wifi_activate_strongest_ssid() {
    sys_state->has_connected_sta = true;
    auto strongest = wifi_config_aps->GetStrongestSTA();
    if(LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG) {
        ESP_LOGD(TAG, "Setting strongest AP as active");
        WifiList::PrintWifiSTAEntryTitle();
        WifiList::PrintWifiSTAEntry(strongest);
    }

    WifiList::Update(sys_state->connected_sta, strongest);
}
void network_wifi_clear_config() {
    /* erase configuration */
    wifi_config_aps->RemoveCredential(network_wifi_get_active_config());
    esp_err_t err = ESP_OK;
    if((err = esp_wifi_disconnect()) != ESP_OK) { ESP_LOGW(TAG, "Could not disconnect from deleted network : %s", esp_err_to_name(err)); }
}
void print_wifi_ap_status(const wifi_sta_config_t* ap_status) {
    if(ap_status == NULL) {
        printf("AP status is NULL\n");
        return;
    }

    printf("SSID: %s\n", ap_status->ssid);
    printf("Password: %s\n", ap_status->password);

    const char* scan_method = (ap_status->scan_method == WIFI_FAST_SCAN) ? "Fast Scan" : "All Channel Scan";
    printf("Scan Method: %s\n", scan_method);

    printf("BSSID Set: %s\n", ap_status->bssid_set ? "Yes" : "No");
    printf("BSSID: %02x:%02x:%02x:%02x:%02x:%02x\n", ap_status->bssid[0], ap_status->bssid[1], ap_status->bssid[2], ap_status->bssid[3],
        ap_status->bssid[4], ap_status->bssid[5]);

    printf("Channel: %d\n", ap_status->channel);
    printf("Listen Interval: %d\n", ap_status->listen_interval);

    const char* sort_method = (ap_status->sort_method == WIFI_CONNECT_AP_BY_SIGNAL) ? "By Signal" : "By Security";
    printf("Sort Method: %s\n", sort_method);

    printf("RSSI Threshold: %d\n", ap_status->threshold.rssi);
    printf("Auth Mode Threshold: %d\n", ap_status->threshold.authmode);

    printf("PMF Capable: %s\n", ap_status->pmf_cfg.capable ? "Yes" : "No");
    printf("PMF Required: %s\n", ap_status->pmf_cfg.required ? "Yes" : "No");

    // Add other fields as needed
}
void network_wifi_get_status() {
    wifi_config_t config;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &config);
    if(err == ESP_OK) {
        print_wifi_ap_status(&config.sta);
    } else {
        ESP_LOGE(TAG, "Error getting wifi status: %s", esp_err_to_name(err));
    }
    if(wifi_config_aps->GetCount() > 0 || scan_results_aps.GetCount() > 0) { WifiList::PrintWifiSTAEntryTitle(); }
    ESP_LOGI(TAG, "Known access points:");
    for(int i = 0; i < wifi_config_aps->GetCount(); i++) {
        const auto entry = wifi_config_aps->GetIndex(i);
        if(entry != nullptr) { WifiList::PrintWifiSTAEntry(*entry); }
    }
    if(scan_results_aps.GetCount() > 0) {
        ESP_LOGI(TAG, "List of nearby access points");
        for(int i = 0; i < scan_results_aps.GetCount(); i++) {
            const auto entry = scan_results_aps.GetIndex(i);
            if(entry != nullptr) { WifiList::PrintWifiSTAEntry(*entry); }
        }
    }
}

const char* get_disconnect_code_desc(uint8_t reason) {
    switch(reason) {
        ENUM_TO_STRING(WIFI_REASON_UNSPECIFIED);
        ENUM_TO_STRING(WIFI_REASON_AUTH_EXPIRE);
        ENUM_TO_STRING(WIFI_REASON_AUTH_LEAVE);
        ENUM_TO_STRING(WIFI_REASON_ASSOC_EXPIRE);
        ENUM_TO_STRING(WIFI_REASON_ASSOC_TOOMANY);
        ENUM_TO_STRING(WIFI_REASON_NOT_AUTHED);
        ENUM_TO_STRING(WIFI_REASON_NOT_ASSOCED);
        ENUM_TO_STRING(WIFI_REASON_ASSOC_LEAVE);
        ENUM_TO_STRING(WIFI_REASON_ASSOC_NOT_AUTHED);
        ENUM_TO_STRING(WIFI_REASON_DISASSOC_PWRCAP_BAD);
        ENUM_TO_STRING(WIFI_REASON_DISASSOC_SUPCHAN_BAD);
        ENUM_TO_STRING(WIFI_REASON_IE_INVALID);
        ENUM_TO_STRING(WIFI_REASON_MIC_FAILURE);
        ENUM_TO_STRING(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT);
        ENUM_TO_STRING(WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT);
        ENUM_TO_STRING(WIFI_REASON_IE_IN_4WAY_DIFFERS);
        ENUM_TO_STRING(WIFI_REASON_GROUP_CIPHER_INVALID);
        ENUM_TO_STRING(WIFI_REASON_PAIRWISE_CIPHER_INVALID);
        ENUM_TO_STRING(WIFI_REASON_AKMP_INVALID);
        ENUM_TO_STRING(WIFI_REASON_UNSUPP_RSN_IE_VERSION);
        ENUM_TO_STRING(WIFI_REASON_INVALID_RSN_IE_CAP);
        ENUM_TO_STRING(WIFI_REASON_802_1X_AUTH_FAILED);
        ENUM_TO_STRING(WIFI_REASON_CIPHER_SUITE_REJECTED);
        ENUM_TO_STRING(WIFI_REASON_INVALID_PMKID);
        ENUM_TO_STRING(WIFI_REASON_BEACON_TIMEOUT);
        ENUM_TO_STRING(WIFI_REASON_NO_AP_FOUND);
        ENUM_TO_STRING(WIFI_REASON_AUTH_FAIL);
        ENUM_TO_STRING(WIFI_REASON_ASSOC_FAIL);
        ENUM_TO_STRING(WIFI_REASON_HANDSHAKE_TIMEOUT);
        ENUM_TO_STRING(WIFI_REASON_CONNECTION_FAIL);
        ENUM_TO_STRING(WIFI_REASON_AP_TSF_RESET);
        ENUM_TO_STRING(WIFI_REASON_ROAMING);
    }
    return "";
}
