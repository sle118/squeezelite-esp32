#ifdef NETWORK_WIFI_LOG_LEVEL
#define LOG_LOCAL_LEVEL NETWORK_WIFI_LOG_LEVEL
#endif
#include "network_wifi.h"
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
#include <string.h>
#pragma message("fixme: search for TODO in the code below")
#include "platform_esp32.h"
#include "tools.h"
#include "trace.h"
#include "accessors.h"
static void network_wifi_event_handler(
    void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static char* get_disconnect_code_desc(uint8_t reason);
esp_err_t network_wifi_get_blob(void* target, size_t size, const char* key);
static inline const char* ssid_string(const wifi_sta_config_t* sta);
static inline const char* password_string(const wifi_sta_config_t* sta);
static const char* status_file_name = "status.bin";
#define MAX_CREDENTIALS sizeof(platform->net.credentials) / sizeof(sys_WifiSTAEntry)
static const char TAG[] = "network_wifi";
esp_netif_t* wifi_netif;
esp_netif_t* wifi_ap_netif;
extern sys_Status status;

#define UINT_TO_STRING(val)                                                                        \
    static char loc[sizeof(val) + 1];                                                              \
    memset(loc, 0x00, sizeof(loc));                                                                \
    strlcpy(loc, (char*)val, sizeof(loc));                                                         \
    return loc;

static inline const char* ssid_string(const wifi_sta_config_t* sta) { UINT_TO_STRING(sta->ssid); }
static inline const char* password_string(const wifi_sta_config_t* sta) {
    UINT_TO_STRING(sta->password);
}
static inline const char* ap_ssid_string(const wifi_ap_record_t* ap) { UINT_TO_STRING(ap->ssid); }
bool network_wifi_reset_rssi() {
    ESP_LOGD(TAG,"Resetting RSSI");
    if (network_status_lock_structure(pdMS_TO_TICKS(1000))) {
        for (int index = 0; index < platform->net.credentials_count - 1; index++) {
            platform->net.credentials[index].rssi = 0;
        }
        network_status_unlock_structure();
        return true;
    }
    return false;
}
bool network_wifi_reset_connected() {
    ESP_LOGD(TAG,"Resetting Connected");
    if (network_status_lock_structure(pdMS_TO_TICKS(1000))) {
        for (int index = 0; index < platform->net.credentials_count - 1; index++) {
            platform->net.credentials[index].connected = false;
        }
        network_status_unlock_structure();
        return true;
    }
    return false;
}
void network_wifi_format_bssid(char* buffer, size_t len, const uint8_t* bssid) {
    snprintf(buffer, len, "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3],
        bssid[4], bssid[5]);
}
int network_wifi_get_ap_entry_index(const char* ssid) {
    for (int i = 0; i < platform->net.credentials_count; i++) {
        if (strcmp(platform->net.credentials[i].ssid, ssid) == 0) {
            // found the SSID.
            return i;
        }
    }
    return -1;
}
sys_WifiSTAEntry * network_wifi_get_ap_entry(const char * ssid){
    sys_WifiSTAEntry * result = NULL;
    int index = network_wifi_get_ap_entry_index(ssid);
    if(index>=0 && index < platform->net.credentials_count){
        result = &platform->net.credentials[index];
    }
    return result;
}
void network_wifi_update_connected(const char * ssid){
    if(!ssid || strlen(ssid) == 0){
        ESP_LOGE(TAG, "Unable to update connected SSID. No ssid received");
        return;
    }
    sys_WifiSTAEntry * sta = network_wifi_get_ap_entry(ssid);
    if (sta && network_status_lock_structure(pdMS_TO_TICKS(1000))) {
        sta->connected = true;
        network_status_unlock_structure();
        platform->net.has_last_connected = true;
        memcpy(&platform->net.last_connected, sta,sizeof(platform->net.last_connected));
    }
    
}
size_t network_wifi_get_known_count(){
    size_t result = 0;
    for (int i = 0; i < platform->net.credentials_count; i++) {
        if(strlen(platform->net.credentials[i].ssid)>0) result ++;
    }    
    return result;

}

bool network_wifi_remove_credentials(int index) {
    int cur_index = 0;
    if (index == -1 || index >= platform->net.credentials_count) {
        ESP_LOGW(TAG, "ssid not found or erorr finding SSID");
        return false;
    }
    if (network_status_lock_structure(pdMS_TO_TICKS(1000))) {    
        for (cur_index = index; cur_index < platform->net.credentials_count - 1; cur_index++) {
            // Shift following ssid's starting at this slot.
            memcpy(&platform->net.credentials[cur_index], &platform->net.credentials[cur_index + 1],
                sizeof(sys_WifiSTAEntry));
        }
        memset(&platform->net.credentials[cur_index + 1], 0x00, sizeof(sys_WifiSTAEntry));
        platform->net.credentials_count--;
        network_status_unlock_structure();
        return true;
    }
    return false;
}


bool network_wifi_remove_ap_entry(const char* ssid) {
    int index = network_wifi_get_ap_entry_index(ssid);
    return network_wifi_remove_credentials(index);
}

void network_wifi_sort_last_seen() {
    sys_NetworkConfig* config = NULL;
    
    if (!SYS_NET(config)) return;
    if (network_status_lock_structure(pdMS_TO_TICKS(1000))) {    
        bool swapped;
        do {
            swapped = false;
            for (int i = 0; i < config->credentials_count - 1; i++) {
                if (config->credentials[i].last_seen.seconds < config->credentials[i + 1].last_seen.seconds) {
                    sys_WifiSTAEntry temp = config->credentials[i];
                    config->credentials[i] = config->credentials[i + 1];
                    config->credentials[i + 1] = temp;
                    swapped = true;
                }
            }
        } while (swapped);
        network_status_unlock_structure();
    }
}
void network_wifi_sort_strength() {
    sys_NetworkConfig* config = platform->has_net ? &platform->net : NULL;
    if (!config) return;
    if (network_status_lock_structure(pdMS_TO_TICKS(1000))) {    
        bool swapped;
        do {
            swapped = false;
            for (int i = 0; i < config->credentials_count - 1; i++) {
                if (config->credentials[i].rssi < config->credentials[i + 1].rssi) {
                    sys_WifiSTAEntry temp = config->credentials[i];
                    config->credentials[i] = config->credentials[i + 1];
                    config->credentials[i + 1] = temp;
                    swapped = true;
                }
            }
        } while (swapped);
        network_status_unlock_structure();
    }
}
void network_wifi_empty_known_list() {
    sys_WifiSTAEntry defaultWifi = sys_WifiSTAEntry_init_default;
    sys_NetworkConfig* config = platform->has_net ? &platform->net : NULL;
    if (!config) return;
    for (int i = 0; i < config->credentials_count - 1; i++) {
        memcpy(&config->credentials[i], &defaultWifi, sizeof(config->credentials[i]));
    }
    config->credentials_count = 0;
}

const wifi_sta_config_t* network_wifi_get_active_config() {
    static wifi_config_t config;
    esp_err_t err = ESP_OK;
    memset(&config, 0x00, sizeof(config));
    if ((err = esp_wifi_get_config(WIFI_IF_STA, &config)) == ESP_OK) {
        return &config.sta;
    } else {
        ESP_LOGD(TAG, "Could not get wifi STA config: %s", esp_err_to_name(err));
    }
    return NULL;
}
sys_WifiSTAEntry* network_wifi_get_free_ssid() {
    for (int i = 0; i < MAX_CREDENTIALS; i++) {
        if (strlen(platform->net.credentials[i].ssid) == 0) {
            return &platform->net.credentials[i];
        }
    }
    return NULL;
}

bool network_wifi_add_ap(sys_WifiSTAEntry* item) {
    sys_WifiSTAEntry* entry = network_wifi_get_free_ssid();
    if (!entry) {
        network_wifi_sort_last_seen();
        network_wifi_remove_credentials(MAX_CREDENTIALS - 1);
        platform->net.credentials_count--;
        entry = network_wifi_get_free_ssid();
    }
    if (!entry) {
        ESP_LOGE(TAG, "Unable to find free slot to store wifi");
        return false;
    }
    memcpy(entry, item, sizeof(sys_WifiSTAEntry));
    platform->net.credentials_count++;
    return true;
}

sys_WifiSTAEntry* network_wifi_get_ssid_info(const char* ssid) {
    if (!ssid || strlen(ssid) == 0) return NULL;
    for (int i = 0; i < status.net.wifi.scan_result_count; i++) {
        if (strcmp(status.net.wifi.scan_result[i].ssid, ssid) == 0) {
            return &status.net.wifi.scan_result[i];
        }
    }
    return NULL;
}

wifi_auth_mode_t network_wifi_get_auth_mode(sys_WifiAuthTypeEnum auth_type) {
    switch (auth_type) {
    case sys_WifiAuthTypeEnum_AUTH_OPEN:
        return WIFI_AUTH_OPEN;
    case sys_WifiAuthTypeEnum_AUTH_WEP:
        return WIFI_AUTH_WEP;
    case sys_WifiAuthTypeEnum_AUTH_WPA_PSK:
        return WIFI_AUTH_WPA_PSK;
    case sys_WifiAuthTypeEnum_AUTH_WPA2_PSK:
        return WIFI_AUTH_WPA2_PSK;
    case sys_WifiAuthTypeEnum_AUTH_WPA_WPA2_PSK:
        return WIFI_AUTH_WPA_WPA2_PSK;
    case sys_WifiAuthTypeEnum_AUTH_WPA2_ENTERPRISE:
        return WIFI_AUTH_WPA2_ENTERPRISE;
    case sys_WifiAuthTypeEnum_AUTH_WPA3_PSK:
        return WIFI_AUTH_WPA3_PSK;
    case sys_WifiAuthTypeEnum_AUTH_WPA2_WPA3_PSK:
        return WIFI_AUTH_WPA2_WPA3_PSK;
    case sys_WifiAuthTypeEnum_AUTH_WAPI_PSK:
        return WIFI_AUTH_WAPI_PSK;
    default:
        return WIFI_AUTH_OPEN; // Default case
    }
}
sys_WifiAuthTypeEnum network_wifi_get_auth_type(const wifi_auth_mode_t mode) {
    switch (mode) {
    case WIFI_AUTH_OPEN:
        return sys_WifiAuthTypeEnum_AUTH_OPEN;
    case WIFI_AUTH_WEP:
        return sys_WifiAuthTypeEnum_AUTH_WEP;
    case WIFI_AUTH_WPA_PSK:
        return sys_WifiAuthTypeEnum_AUTH_WPA_PSK;
    case WIFI_AUTH_WPA2_PSK:
        return sys_WifiAuthTypeEnum_AUTH_WPA2_PSK;
    case WIFI_AUTH_WPA_WPA2_PSK:
        return sys_WifiAuthTypeEnum_AUTH_WPA_WPA2_PSK;
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return sys_WifiAuthTypeEnum_AUTH_WPA2_ENTERPRISE;
    case WIFI_AUTH_WPA3_PSK:
        return sys_WifiAuthTypeEnum_AUTH_WPA3_PSK;
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return sys_WifiAuthTypeEnum_AUTH_WPA2_WPA3_PSK;
    case WIFI_AUTH_WAPI_PSK:
        return sys_WifiAuthTypeEnum_AUTH_WAPI_PSK;
    case WIFI_AUTH_MAX:
        return sys_WifiAuthTypeEnum_AUTH_OPEN;
    }
    return sys_WifiAuthTypeEnum_AUTH_UNKNOWN;
}

sys_WifiRadioTypesEnum network_wifi_get_radio_type(const wifi_ap_record_t* sta) {
    if (sta == NULL) {
        return sys_WifiRadioTypesEnum_PHY_UNKNOWN;
    }

    // Check each bit field and return the corresponding enum value
    if (sta->phy_11b) {
        return sys_WifiRadioTypesEnum_PHY_11B;
    } else if (sta->phy_11g) {
        return sys_WifiRadioTypesEnum_PHY_11G;
    } else if (sta->phy_11n) {
        return sys_WifiRadioTypesEnum_PHY_11N;
    } else if (sta->phy_lr) {
        return sys_WifiRadioTypesEnum_PHY_LR;
    } else if (sta->wps) {
        return sys_WifiRadioTypesEnum_PHY_WPS;
    } else if (sta->ftm_responder) {
        return sys_WifiRadioTypesEnum_PHY_FTM_RESPONDER;
    } else if (sta->ftm_initiator) {
        return sys_WifiRadioTypesEnum_PHY_FTM_INITIATOR;
    }

    return sys_WifiRadioTypesEnum_PHY_UNKNOWN;
}
esp_err_t network_wifi_add_update_ap_from_sta_copy(const wifi_sta_config_t* sta) {
    esp_err_t err = ESP_OK;
    if (!sta) {
        ESP_LOGE(TAG, "Invalid access point entry");
        return ESP_ERR_INVALID_ARG;
    }
    if (!sta->ssid || strlen((char*)sta->ssid) == 0) {
        ESP_LOGE(TAG, "Invalid access point ssid");
        return ESP_ERR_INVALID_ARG;
    }
    sys_WifiSTAEntry item = sys_WifiSTAEntry_init_default;
    strncpy(item.ssid, ssid_string(sta), sizeof(item.ssid));
    strncpy(item.password, password_string(sta), sizeof(item.ssid));
    network_wifi_format_bssid(item.bssid, sizeof(item.bssid), sta->bssid);
    item.channel = sta->channel;

    sys_WifiSTAEntry* seen = network_wifi_get_ssid_info(item.ssid);
    if (seen) {
        item.auth_type = seen->auth_type;
        item.radio_type = seen->radio_type;
    }
    err = network_wifi_add_ap(&item);
    return err;
}

bool network_wifi_is_known_ap(const char* ssid) { return network_wifi_get_ap_entry(ssid) != NULL; }

bool network_wifi_str2mac(const char* mac, uint8_t* values) {
    if (6 == sscanf(mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &values[0], &values[1], &values[2],
                 &values[3], &values[4], &values[5])) {
        return true;
    } else {
        return false;
    }
}

esp_err_t network_wifi_erase_known_ap() {
    network_wifi_empty_known_list();
    return ESP_OK;
}


esp_netif_t* network_wifi_get_interface() { return wifi_netif; }
esp_netif_t* network_wifi_get_ap_interface() { return wifi_ap_netif; }
esp_err_t network_wifi_set_sta_mode() {
    if (!wifi_netif) {
        ESP_LOGE(TAG, "Wifi not initialized. Cannot set sta mode");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGD(TAG, "Set Mode to STA");
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error setting mode to STA: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Starting wifi");
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error starting wifi: %s", esp_err_to_name(err));
        }
    }
    return err;
}
esp_netif_t* network_wifi_start() {
    MEMTRACE_PRINT_DELTA_MESSAGE("Starting wifi interface as STA mode");
    if (!wifi_netif) {
        MEMTRACE_PRINT_DELTA_MESSAGE("Init STA mode - creating default interface. ");
        wifi_netif = esp_netif_create_default_wifi_sta();
        MEMTRACE_PRINT_DELTA_MESSAGE("Initializing Wifi. ");
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_init(&cfg));
        MEMTRACE_PRINT_DELTA_MESSAGE("Registering wifi Handlers");
        // network_wifi_register_handlers();
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &network_wifi_event_handler, NULL, NULL));
        MEMTRACE_PRINT_DELTA_MESSAGE("Setting up wifi Storage");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    }
    MEMTRACE_PRINT_DELTA_MESSAGE("Setting up wifi mode as STA");
    network_wifi_set_sta_mode();
    MEMTRACE_PRINT_DELTA_MESSAGE("Setting hostname");
    network_set_hostname(wifi_netif);
    MEMTRACE_PRINT_DELTA_MESSAGE("Done starting wifi interface");
    return wifi_netif;
}
void destroy_network_wifi() {

}

bool network_wifi_sta_config_changed() {
    bool changed = true;
    const wifi_sta_config_t* sta = network_wifi_get_active_config();
    if (!sta || strlen(ssid_string(sta)) == 0) return false;

    sys_WifiSTAEntry* known = network_wifi_get_ap_entry(ssid_string(sta));
    if (known && strcmp(known->ssid, ssid_string(sta)) == 0 &&
        strcmp((char*)known->password, password_string(sta)) == 0) {
        changed = false;
    } else {
        ESP_LOGI(TAG, "New network configuration found");
    }
    return changed;
}


static void network_wifi_event_handler(
    void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base != WIFI_EVENT) return;
    switch (event_id) {

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
        char* mac = network_manager_alloc_get_mac_string(s->mac);
        if (mac) {
            ESP_LOGD(
                TAG, "WIFI_EVENT_AP_PROBEREQRECVED. RSSI: %d, MAC: %s", s->rssi, STR_OR_BLANK(mac));
        }
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
        char* mac = network_manager_alloc_get_mac_string(stac->mac);
        if (mac) {
            ESP_LOGD(
                TAG, "WIFI_EVENT_AP_STACONNECTED. aid: %d, mac: %s", stac->aid, STR_OR_BLANK(mac));
        }
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
        wifi_event_sta_connected_t* s = (wifi_event_sta_connected_t*)event_data;
        char* bssid = network_manager_alloc_get_mac_string(s->bssid);
        char* ssid = strdup_psram((char*)s->ssid);
        if (bssid && ssid) {
            ESP_LOGD(TAG, "WIFI_EVENT_STA_CONNECTED. Channel: %d, Access point: %s, BSSID: %s ",
                s->channel, STR_OR_BLANK(ssid), (bssid));
            network_wifi_update_connected(ssid);
            
        }
        FREE_AND_NULL(bssid);
        FREE_AND_NULL(ssid);
        network_async(EN_CONNECTED);

    } break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        //		    		structwifi_event_sta_disconnected_t
        //		    		Argument structure for WIFI_EVENT_STA_DISCONNECTED event
        //
        //		    		Public Members
        //
        //		    		uint8_t ssid[32]
        //		    		SSID of disconnected AP
        //
        //		    		uint8_t ssid_len
        //		    		SSID length of disconnected AP
        //
        //		    		uint8_t bssid[6]
        //		    		BSSID of disconnected AP
        //
        //		    		uint8_t reason
        //		    		reason of disconnection
        wifi_event_sta_disconnected_t* s = (wifi_event_sta_disconnected_t*)event_data;
        char* bssid = network_manager_alloc_get_mac_string(s->bssid);
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED. From BSSID: %s, reason code: %d (%s)",
            STR_OR_BLANK(bssid), s->reason, get_disconnect_code_desc(s->reason));
        FREE_AND_NULL(bssid);
        if (s->reason == WIFI_REASON_ROAMING) {
            ESP_LOGI(TAG, "WiFi Roaming to new access point");
        } else {
            network_async_lost_connection((wifi_event_sta_disconnected_t*)event_data);
        }
    } break;

    default:
        break;
    }
}
void network_wifi_write_file(const char * filename, sys_Status * status_struct){
    FILE*  file = open_file("wb", filename);
        pb_ostream_t filestream = {&out_file_binding, file, SIZE_MAX, 0};
        ESP_LOGD(TAG, "Starting encode");
        if (!pb_encode(&filestream, sys_Config_fields, (void*)&status)) {
            ESP_LOGE(TAG, "Encoding failed: %s\n", PB_GET_ERROR(&filestream));
        }
        fclose(file);
}
void network_wifi_global_init() {
    pb_istream_t istream =PB_ISTREAM_EMPTY;
    ESP_LOGD(TAG, "Loading existing wifi configuration (if any)");
    FILE* file = open_file("rb", status_file_name);
    if (!file) {
        network_wifi_write_file(status_file_name,&status);
        file = open_file("rb", status_file_name);
    }
    if (!file) {
        ESP_LOGE(TAG, "Unable to read status file");
        return;
    }

    istream.callback = &in_file_binding;
    istream.state = file;
    pb_decode(&istream, &sys_Status_msg, &status);
    fclose(file);
}

esp_netif_t* network_wifi_config_ap() {
    esp_netif_ip_info_t info;
    esp_err_t err = ESP_OK;
    wifi_config_t ap_config = {
        .ap = {.ssid_len = 0,
            .authmode = AP_AUTHMODE,
            .ssid_hidden = DEFAULT_AP_SSID_HIDDEN,
            .max_connection = DEFAULT_AP_MAX_CONNECTIONS,
            .beacon_interval = DEFAULT_AP_BEACON_INTERVAL

        },
    };
    ESP_LOGI(TAG, "Configuring Access Point.");
    if (!wifi_ap_netif) {
        wifi_ap_netif = esp_netif_create_default_wifi_ap();
    }
    if (platform->has_net && platform->net.has_ap && platform->net.ap.has_ip) {
        inet_pton(AF_INET, platform->net.ap.ip.ip,
            (ip4_addr_t*)&info.ip); /* access point is on a static IP */
        inet_pton(AF_INET, platform->net.ap.ip.gw,
            (ip4_addr_t*)&info.gw); /* access point is on a static IP */
        inet_pton(AF_INET, platform->net.ap.ip.netmask,
            (ip4_addr_t*)&info.netmask); /* access point is on a static IP */
    } else {
        inet_pton(
            AF_INET, DEFAULT_AP_IP, (ip4_addr_t*)&info.ip); /* access point is on a static IP */
        inet_pton(AF_INET, CONFIG_DEFAULT_AP_GATEWAY,
            (ip4_addr_t*)&info.gw); /* access point is on a static IP */
        inet_pton(AF_INET, CONFIG_DEFAULT_AP_NETMASK,
            (ip4_addr_t*)&info.netmask); /* access point is on a st         */
    }

    /* In order to change the IP info structure, we have to first stop
     * the DHCP server on the new interface
     */
    network_start_stop_dhcps(wifi_ap_netif, false);
    ESP_LOGD(TAG, "Setting tcp_ip info for access point");
    if ((err = esp_netif_set_ip_info(wifi_ap_netif, &info)) != ESP_OK) {
        ESP_LOGE(TAG, "Setting tcp_ip info for interface TCPIP_ADAPTER_IF_AP. Error %s",
            esp_err_to_name(err));
        return wifi_ap_netif;
    }
    network_start_stop_dhcps(wifi_ap_netif, true);

    /*
     * Set Access Point configuration
     */
    const char* ap_ssid = CONFIG_DEFAULT_AP_SSID;
    if (platform->has_names && strlen(platform->names.wifi_ap_name) > 0) {
        ap_ssid = platform->names.wifi_ap_name;
    }
    const char* ap_password = DEFAULT_AP_PASSWORD;
    ap_config.ap.channel = CONFIG_DEFAULT_AP_CHANNEL;
    if (platform->has_net && platform->net.has_ap) {
        if (strlen(platform->net.ap.password) > 0) {
            ap_password = platform->net.ap.password;
        }
        if (platform->net.ap.channel > 0) {
            ap_config.ap.channel = platform->net.ap.channel;
        }
        if (platform->net.ap.auth_mode != sys_WifiAuthTypeEnum_AUTH_UNKNOWN) {
            ap_config.ap.authmode = network_wifi_get_auth_mode(platform->net.ap.auth_mode);
        }
        ap_config.ap.ssid_hidden = platform->net.ap.hidden;
        if (platform->net.ap.max_connection > 0) {
            ap_config.ap.max_connection = platform->net.ap.max_connection;
        }
        if (platform->net.ap.beacon_interval > 0) {
            ap_config.ap.beacon_interval = platform->net.ap.beacon_interval;
        }
    }

    strlcpy((char*)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid));
    strlcpy((char*)ap_config.ap.password, ap_password, sizeof(ap_config.ap.password));
    ESP_LOGI(TAG,
        "AP SSID: %s, PASSWORD: %s Auth Mode: %d SSID: %s Max Connections: %d Beacon interval: %d",
        (char*)ap_config.ap.ssid, (char*)ap_config.ap.password, ap_config.ap.authmode,
        ap_config.ap.ssid_hidden ? "HIDDEN" : "VISIBLE", ap_config.ap.max_connection,
        ap_config.ap.beacon_interval);

    const char* msg = "Setting wifi mode as WIFI_MODE_APSTA";
    ESP_LOGD(TAG, "%s", msg);
    if ((err = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) {
        ESP_LOGE(TAG, "%s. Error %s", msg, esp_err_to_name(err));
        return wifi_ap_netif;
    }
    msg = "Setting wifi AP configuration for WIFI_IF_AP";
    ESP_LOGD(TAG, "%s", msg);
    if ((err = esp_wifi_set_config(WIFI_IF_AP, &ap_config)) != ESP_OK) /* stop AP DHCP server */
    {
        ESP_LOGE(TAG, "%s . Error %s", msg, esp_err_to_name(err));
        return wifi_ap_netif;
    }

    msg = "Setting wifi bandwidth";
    ESP_LOGD(TAG, "%s (%d)", msg, DEFAULT_AP_BANDWIDTH);
    if ((err = esp_wifi_set_bandwidth(WIFI_IF_AP, DEFAULT_AP_BANDWIDTH)) != ESP_OK) /* stop AP
    DHCP server */
    {
        ESP_LOGE(TAG, "%s failed. Error %s", msg, esp_err_to_name(err));
        return wifi_ap_netif;
    }

    msg = "Setting wifi power save";
    ESP_LOGD(TAG, "%s (%d)", msg, DEFAULT_STA_POWER_SAVE);

    if ((err = esp_wifi_set_ps(DEFAULT_STA_POWER_SAVE)) != ESP_OK) /* stop AP DHCP server */
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

    for (int i = 0; i < *aps - 1; i++) {
        wifi_ap_record_t* ap = &aplist[i];

        /* skip the previously removed APs */
        if (ap->ssid[0] == 0) continue;

        /* remove the identical SSID+authmodes */
        for (int j = i + 1; j < *aps; j++) {
            wifi_ap_record_t* ap1 = &aplist[j];
            if ((strcmp((const char*)ap->ssid, (const char*)ap1->ssid) == 0) &&
                (ap->authmode == ap1->authmode)) { /* same SSID, different auth mode is skipped */
                /* save the rssi for the display */
                if ((ap1->rssi) > (ap->rssi)) ap->rssi = ap1->rssi;
                /* clearing the record */
                memset(ap1, 0, sizeof(wifi_ap_record_t));
            }
        }
    }
    /* reorder the list so APs follow each other in the list */
    for (int i = 0; i < *aps; i++) {
        wifi_ap_record_t* ap = &aplist[i];
        /* skipping all that has no name */
        if (ap->ssid[0] == 0) {
            /* mark the first free slot */
            if (first_free == NULL) first_free = ap;
            total_unique--;
            continue;
        }
        if (first_free != NULL) {
            memcpy(first_free, ap, sizeof(wifi_ap_record_t));
            memset(ap, 0, sizeof(wifi_ap_record_t));
            /* find the next free slot */
            for (int j = 0; j < *aps; j++) {
                if (aplist[j].ssid[0] == 0) {
                    first_free = &aplist[j];
                    break;
                }
            }
        }
    }
    /* update the length of the list */
    *aps = total_unique;
}
void network_wifi_clear_scan_results() {
    if (!status.net.wifi.scan_result || status.net.wifi.scan_result_count == 0) {
        return;
    }
    free(status.net.wifi.scan_result);
    status.net.wifi.scan_result = NULL;
    status.net.wifi.scan_result_count = 0;
}
sys_WifiSTAEntry* network_wifi_alloc_scan_results(uint16_t count) {
    network_wifi_clear_scan_results();
    status.net.wifi.scan_result = malloc_init_external(count * sizeof(sys_WifiSTAEntry));
    status.net.wifi.scan_result_count = count;
    return status.net.wifi.scan_result;
}
void network_wifi_esp_sta_to_sta(wifi_ap_record_t* scan_res, sys_WifiSTAEntry* sta_entry) {
    if(!sta_entry){
        return;
    }
    sta_entry->radio_type = network_wifi_get_radio_type(scan_res);
    sta_entry->auth_type = network_wifi_get_auth_type(scan_res->authmode);
    network_wifi_format_bssid(sta_entry->bssid, sizeof(sta_entry->bssid), scan_res->bssid);
    sta_entry->channel = scan_res->primary;
    gettimeofday((struct timeval*)&sta_entry->last_seen, NULL);
    sta_entry->rssi = scan_res->rssi;
    strncpy(sta_entry->ssid, ap_ssid_string(scan_res), sizeof(sta_entry->ssid));
    // also update any existing credential entry if any 
    network_wifi_esp_sta_to_sta(scan_res,network_wifi_get_ap_entry(sta_entry->ssid));
}
bool network_wifi_update_scan_list(wifi_ap_record_t* accessp_records, uint16_t count) {
    if (!network_wifi_alloc_scan_results(count)) {
        ESP_LOGE(TAG, "Error allocating memory to store scan results");
        return false;
    }
    // reset RSSI values. This will help when choosing the 
    // strongest signal to connect to 
    network_wifi_reset_rssi();
    for (int i = 0; i < status.net.wifi.scan_result_count; i++) {
        network_wifi_esp_sta_to_sta(&accessp_records[i], &status.net.wifi.scan_result[i]);
    }
    network_wifi_sort_strength();
    return true;
}
esp_err_t wifi_scan_done() {
    esp_err_t err = ESP_OK;
    wifi_ap_record_t* accessp_records = NULL;
    /* As input param, it stores max AP number ap_records can hold. As output param, it receives the
     * actual AP number this API returns. As a consequence, ap_num MUST be reset to MAX_AP_NUM at
     * every scan */
    ESP_LOGD(TAG, "Getting AP list records");
    uint16_t ap_num = MAX_AP_NUM;
    if ((err = esp_wifi_scan_get_ap_num(&ap_num)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to retrieve scan results count. Error %s", esp_err_to_name(err));
        return err;
    }
    if (ap_num <= 0) {
        ESP_LOGI(TAG, "No AP found during scan");
        return err;
    }
    accessp_records = (wifi_ap_record_t*)malloc_init_external(sizeof(wifi_ap_record_t) * ap_num);
    if (!accessp_records) {
        ESP_LOGE(TAG, "Alloc failed for scan results");
        return ESP_FAIL;
    }
    if ((err = esp_wifi_scan_get_ap_records(&ap_num, accessp_records)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to retrieve scan results list. Error %s", esp_err_to_name(err));
    } else {
        /* make sure the http server isn't trying to access the list while it gets refreshed */
        ESP_LOGD(TAG, "Retrieving scan list");
        /* Will remove the duplicate SSIDs from the list and update ap_num */
        network_wifi_filter_unique(accessp_records, &ap_num);
        if (network_status_lock_structure(pdMS_TO_TICKS(1000))) {
            network_wifi_update_scan_list(accessp_records, ap_num);
            network_status_unlock_structure();
            ESP_LOGD(TAG, "Done retrieving scan list");
        } else {
            ESP_LOGE(TAG, "could not get access to json mutex in wifi_scan");
            err = ESP_FAIL;
        }
    }
    free(accessp_records);
    return err;
}
bool is_wifi_up() { return wifi_netif != NULL; }
esp_err_t network_wifi_start_scan() {
    wifi_scan_config_t scan_config = {.ssid = 0,
        .bssid = 0,
        .channel = 0,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .show_hidden = true};
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Initiating wifi network scan");
    if (!is_wifi_up()) {
        messaging_post_message(
            MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Wifi not started. Cannot scan");
        return ESP_FAIL;
    }
    /* if a scan is already in progress this message is simply ignored thanks to the
     * WIFI_MANAGER_SCAN_BIT uxBit */
    if ((err = esp_wifi_scan_start(&scan_config, false)) != ESP_OK) {
        ESP_LOGW(TAG, "Unable to start scan; %s ", esp_err_to_name(err));
        //						set_status_message(WARNING, "Wifi Connecting. Cannot start scan.");
        messaging_post_message(
            MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Scanning failed: %s", esp_err_to_name(err));
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
    memset(&config, 0x00, sizeof(config));
    ESP_LOGD(TAG, "network_wifi_connect");
    network_wifi_reset_connected();
    if (!is_wifi_up()) {
        messaging_post_message(
            MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Wifi not started. Cannot connect");
        return ESP_FAIL;
    }
    if (!ssid || !password || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Cannot connect wifi. wifi config is null!");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_mode_t wifi_mode;
    err = esp_wifi_get_mode(&wifi_mode);
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "Wifi not initialized. Attempting to start sta mode");
        network_wifi_start();
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not retrieve wifi mode : %s", esp_err_to_name(err));
    } else if (wifi_mode != WIFI_MODE_STA && wifi_mode != WIFI_MODE_APSTA) {
        ESP_LOGD(TAG, "Changing wifi mode to STA");
        err = network_wifi_set_sta_mode();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Could not set mode to STA.  Cannot connect to SSID %s", ssid);
            return err;
        }
    }
    // copy configuration and connect
    strlcpy((char*)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    if (password) {
        strlcpy((char*)config.sta.password, password, sizeof(config.sta.password));
    }

    // First Disconnect
    esp_wifi_disconnect();

    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    if ((err = esp_wifi_set_config(WIFI_IF_STA, &config)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA configuration. Error %s", esp_err_to_name(err));
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wifi Connecting to %s...", ssid);
        if ((err = esp_wifi_connect()) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initiate wifi connection. Error %s", esp_err_to_name(err));
        }
    }
    return err;
}

esp_err_t network_wifi_connect_ssid(const char* ssid) {
    sys_WifiSTAEntry* item = network_wifi_get_ssid_info(ssid);
    if (item) {
        gettimeofday((struct timeval*)&item->last_try, NULL);
        return network_wifi_connect(item->ssid, item->password);
    }
    return ESP_FAIL;
}
esp_err_t network_wifi_connect_active_ssid() {
    sys_WifiSTAEntry*connected = NULL;
    if(status.has_net && platform->net.has_last_connected && strlen(platform->net.last_connected.ssid)>0){
        connected = &platform->net.last_connected.ssid;
        return network_wifi_connect(connected->ssid,connected->password);
    }
    return ESP_FAIL;
}
void network_wifi_clear_config() {
    /* erase configuration */
    const wifi_sta_config_t* sta = network_wifi_get_active_config();
    network_wifi_remove_ap_entry(ssid_string(sta));
    esp_err_t err = ESP_OK;
    if ((err = esp_wifi_disconnect()) != ESP_OK) {
        ESP_LOGW(TAG, "Could not disconnect from deleted network : %s", esp_err_to_name(err));
    }
}

char* get_disconnect_code_desc(uint8_t reason) {
    switch (reason) {
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
