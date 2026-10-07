#ifdef NETWORK_WIFI_LOG_LEVEL
#define LOG_LOCAL_LEVEL NETWORK_WIFI_LOG_LEVEL
#endif
#include "network_wifi.h"
#include <string.h>
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
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_utilities.h"
#include "platform_config.h"
#include "platform_esp32.h"
#include "tools.h"
#include "trace.h"
static void network_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static char* get_disconnect_code_desc(uint8_t reason);
esp_err_t network_wifi_get_blob(void* target, size_t size, const char* key);
static inline const char* ssid_string(const wifi_sta_config_t* sta);
static inline const char* password_string(const wifi_sta_config_t* sta);
cJSON* accessp_cjson = NULL;

static const char TAG[] = "network_wifi";
const char network_wifi_nvs_namespace[] = "config";
const char ap_list_nsv_namespace[] = "aplist";
/* rrm ctx */
//Roaming support - int rrm_ctx = 0;

uint16_t ap_num = 0;

esp_netif_t* wifi_netif;
esp_netif_t* wifi_ap_netif;

wifi_ap_record_t* accessp_records = NULL;
#define UINT_TO_STRING(val)                \
    static char loc[sizeof(val) + 1];      \
    memset(loc, 0x00, sizeof(loc));        \
    strlcpy(loc, (char*)val, sizeof(loc)); \
    return loc;

static inline const char* ssid_string(const wifi_sta_config_t* sta) {
    UINT_TO_STRING(sta->ssid);
}
static inline const char* password_string(const wifi_sta_config_t* sta) {
    UINT_TO_STRING(sta->password);
}
static inline const char* ap_ssid_string(const wifi_ap_record_t* ap) {
    UINT_TO_STRING(ap->ssid);
}
typedef struct known_access_point {
    char* ssid;
    char* password;
    bool found;
    uint8_t bssid[6];          /**< MAC address of AP */
    uint8_t primary;           /**< channel of AP */
    wifi_auth_mode_t authmode; /**< authmode of AP */
    uint32_t phy_11b : 1;      /**< bit: 0 flag to identify if 11b mode is enabled or not */
    uint32_t phy_11g : 1;      /**< bit: 1 flag to identify if 11g mode is enabled or not */
    uint32_t phy_11n : 1;      /**< bit: 2 flag to identify if 11n mode is enabled or not */
    uint32_t phy_lr : 1;       /**< bit: 3 flag to identify if low rate is enabled or not */
    time_t last_try;
    SLIST_ENTRY(known_access_point)
    next;  //!< next callback
} known_access_point_t;

/** linked list of command structures */
static EXT_RAM_ATTR SLIST_HEAD(ap_list, known_access_point) s_ap_list;
known_access_point_t* network_wifi_get_ap_entry(const char* ssid) {
    known_access_point_t* it;

    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGW(TAG, "network_wifi_get_ap_entry Invalid SSID %s", !ssid ? "IS NULL" : "IS BLANK");
        return NULL;
    }

    SLIST_FOREACH(it, &s_ap_list, next) {
        ESP_LOGD(TAG, "Looking for SSID %s = %s ?", ssid, it->ssid);
        if (strcmp(it->ssid, ssid) == 0) {
            ESP_LOGD(TAG, "network_wifi_get_ap_entry SSID %s found! ", ssid);
            return it;
        }
    }
    return NULL;
}
void network_wifi_remove_ap_entry(const char* ssid) {
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "network_wifi_remove_ap_entry error empty SSID");
    }
    known_access_point_t* it = network_wifi_get_ap_entry(ssid);
    if (it) {
        ESP_LOGW(TAG, "Removing %s from known list of access points", ssid);
        FREE_AND_NULL(it->ssid);
        FREE_AND_NULL(it->password);
        SLIST_REMOVE(&s_ap_list, it, known_access_point, next);
        FREE_AND_NULL(it);
    }
}
void network_wifi_empty_known_list() {
    known_access_point_t* it;
    while ((it = SLIST_FIRST(&s_ap_list)) != NULL) {
        network_wifi_remove_ap_entry(it->ssid);
    }
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

size_t network_wifi_get_known_count() {
    size_t count = 0;
    known_access_point_t* it;
    SLIST_FOREACH(it, &s_ap_list, next) {
        count++;
    }
    return count;
}
size_t network_wifi_get_known_count_in_range() {
    size_t count = 0;
    known_access_point_t* it;
    SLIST_FOREACH(it, &s_ap_list, next) {
        if(it->found) count++;
    }
    return count;
}
esp_err_t network_wifi_add_ap(known_access_point_t* item) {
    known_access_point_t* last = SLIST_FIRST(&s_ap_list);
    if (last == NULL) {
        SLIST_INSERT_HEAD(&s_ap_list, item, next);
    } else {
        known_access_point_t* it;
        while ((it = SLIST_NEXT(last, next)) != NULL) {
            last = it;
        }
        SLIST_INSERT_AFTER(last, item, next);
    }
    return ESP_OK;
}
esp_err_t network_wifi_add_ap_copy(const known_access_point_t* known_ap) {
    known_access_point_t* item = NULL;
    esp_err_t err = ESP_OK;
    
    if (!known_ap) {
        ESP_LOGE(TAG, "Invalid access point entry");
        return ESP_ERR_INVALID_ARG;
    }
    if (!known_ap->ssid || strlen(known_ap->ssid) == 0) {
        ESP_LOGE(TAG, "Invalid access point ssid");
        return ESP_ERR_INVALID_ARG;
    }
    item = malloc_init_external(sizeof(known_access_point_t));
    if (item == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    item->ssid = strdup_psram(known_ap->ssid);
    item->password = strdup_psram(known_ap->password);
    memcpy(&item->bssid, known_ap->bssid, sizeof(item->bssid));
    item->primary = known_ap->primary;
    item->authmode = known_ap->authmode;
    item->phy_11b = known_ap->phy_11b;
    item->phy_11g = known_ap->phy_11g;
    item->phy_11n = known_ap->phy_11n;
    item->phy_lr = known_ap->phy_lr;
    err = network_wifi_add_ap(item);
    return err;
}
const wifi_ap_record_t* network_wifi_get_ssid_info(const char* ssid) {
    if (!accessp_records)
        return NULL;
    for (int i = 0; i < ap_num; i++) {
        if (strcmp(ap_ssid_string(&accessp_records[i]), ssid) == 0) {
            return &accessp_records[i];
        }
    }
    return NULL;
}
esp_err_t network_wifi_add_ap_from_sta_copy(const wifi_sta_config_t* sta) {
    known_access_point_t* item = NULL;
    esp_err_t err = ESP_OK;
    if (!sta) {
        ESP_LOGE(TAG, "Invalid access point entry");
        return ESP_ERR_INVALID_ARG;
    }
    if (!sta->ssid || strlen((char*)sta->ssid) == 0) {
        ESP_LOGE(TAG, "Invalid access point ssid");
        return ESP_ERR_INVALID_ARG;
    }
    item = malloc_init_external(sizeof(known_access_point_t));
    if (item == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    item->ssid = strdup_psram(ssid_string(sta));
    item->password = strdup_psram(password_string(sta));
    memcpy(&item->bssid, sta->bssid, sizeof(item->bssid));
    item->primary = sta->channel;
    const wifi_ap_record_t* seen = network_wifi_get_ssid_info(item->ssid);
    if (seen) {
        item->authmode = seen->authmode;
        item->phy_11b = seen->phy_11b;
        item->phy_11g = seen->phy_11g;
        item->phy_11n = seen->phy_11n;
        item->phy_lr = seen->phy_lr;
    }
    err = network_wifi_add_ap(item);
    return err;
}

bool network_wifi_is_known_ap(const char* ssid) {
    return network_wifi_get_ap_entry(ssid) != NULL;
}

static bool network_wifi_was_ssid_seen(const char* ssid) {
    if (!accessp_records || ap_num == 0 || ap_num == MAX_AP_NUM) {
        return false;
    }
    for (int i = 0; i < ap_num; i++) {
        if (strcmp(ap_ssid_string(&accessp_records[i]), ssid) == 0) {
            return true;
        }
    }
    return false;
}
void network_wifi_set_found_ap() {
    known_access_point_t* it;
    SLIST_FOREACH(it, &s_ap_list, next) {
        if (network_wifi_was_ssid_seen(it->ssid)) {
            it->found = true;
        } else {
            it->found = false;
        }
    }
}
bool network_wifi_known_ap_in_range(){
    known_access_point_t* it;
    SLIST_FOREACH(it, &s_ap_list, next) {
        if (it->found) {
            return true;
        }
    }
    return false;
}
const char * network_wifi_get_next_ap_in_range(){
    known_access_point_t* it;
    time_t last_try_min=(esp_timer_get_time() / 1000);
    SLIST_FOREACH(it, &s_ap_list, next) {
        if (it->found && it->last_try < last_try_min) {
            last_try_min = it->last_try;
        }
    }
    SLIST_FOREACH(it, &s_ap_list, next) {
        if (it->found && it->last_try == last_try_min) {
            return it->ssid;
        }
    }
    return NULL;
}

esp_err_t network_wifi_alloc_ap_json(known_access_point_t* item, char** json_string) {
    esp_err_t err = ESP_OK;
    if (!item || !json_string) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON* cjson_item = cJSON_CreateObject();
    if (!cjson_item) {
        ESP_LOGE(TAG, "Memory allocation failure. Cannot save ap json");
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(cjson_item, "ssid", item->ssid);
    cJSON_AddStringToObject(cjson_item, "pass", item->password);
    cJSON_AddNumberToObject(cjson_item, "chan", item->primary);
    cJSON_AddNumberToObject(cjson_item, "auth", item->authmode);
    char* bssid = network_manager_alloc_get_mac_string(item->bssid);
    if (bssid) {
        cJSON_AddItemToObject(cjson_item, "bssid", cJSON_CreateString(STR_OR_BLANK(bssid)));
    }
    FREE_AND_NULL(bssid);
    cJSON_AddNumberToObject(cjson_item, "b", item->phy_11b ? 1 : 0);
    cJSON_AddNumberToObject(cjson_item, "g", item->phy_11g ? 1 : 0);
    cJSON_AddNumberToObject(cjson_item, "n", item->phy_11n ? 1 : 0);
    cJSON_AddNumberToObject(cjson_item, "low_rate", item->phy_lr ? 1 : 0);

    *json_string = cJSON_PrintUnformatted(cjson_item);
    if (!*json_string) {
        ESP_LOGE(TAG, "Memory allocaiton failed. Cannot save ap entry.");
        err = ESP_ERR_NO_MEM;
    }
    cJSON_Delete(cjson_item);
    return err;
}
bool network_wifi_str2mac(const char* mac, uint8_t* values) {
    if (6 == sscanf(mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5])) {
        return true;
    } else {
        return false;
    }
}

esp_err_t network_wifi_add_json_entry(const char* json_text) {
    esp_err_t err = ESP_OK;
    known_access_point_t known_ap;
    if (!json_text || strlen(json_text) == 0) {
        ESP_LOGE(TAG, "Invalid access point json");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON* cjson_item = cJSON_Parse(json_text);
    if (!cjson_item) {
        ESP_LOGE(TAG, "Invalid JSON %s", json_text);
        return ESP_ERR_INVALID_ARG;
    }
    cJSON* value = cJSON_GetObjectItemCaseSensitive(cjson_item, "ssid");
    if (!value || !cJSON_IsString(value) || strlen(cJSON_GetStringValue(value)) == 0) {
        ESP_LOGE(TAG, "Missing ssid in : %s", json_text);
        err = ESP_ERR_INVALID_ARG;
    } else {
        if (!network_wifi_get_ap_entry(cJSON_GetStringValue(value))) {
            known_ap.ssid = strdup_psram(cJSON_GetStringValue(value));
            value = cJSON_GetObjectItemCaseSensitive(cjson_item, "pass");
            if (value && cJSON_IsString(value) && strlen(cJSON_GetStringValue(value)) > 0) {
                known_ap.password = strdup_psram(cJSON_GetStringValue(value));
            }
            value = cJSON_GetObjectItemCaseSensitive(cjson_item, "chan");
            if (value) {
                known_ap.primary = value->valueint;
            }
            value = cJSON_GetObjectItemCaseSensitive(cjson_item, "auth");
            if (value) {
                known_ap.authmode = value->valueint;
            }
            value = cJSON_GetObjectItemCaseSensitive(cjson_item, "b");
            if (value) {
                known_ap.phy_11b = value->valueint;
            }
            value = cJSON_GetObjectItemCaseSensitive(cjson_item, "g");
            if (value) {
                known_ap.phy_11g = value->valueint;
            }
            value = cJSON_GetObjectItemCaseSensitive(cjson_item, "n");
            if (value) {
                known_ap.phy_11n = value->valueint;
            }
            value = cJSON_GetObjectItemCaseSensitive(cjson_item, "low_rate");
            if (value) {
                known_ap.phy_lr = value->valueint;
            }
            value = cJSON_GetObjectItemCaseSensitive(cjson_item, "bssid");
            if (value && cJSON_IsString(value) && strlen(cJSON_GetStringValue(value)) > 0) {
                network_wifi_str2mac(cJSON_GetStringValue(value), known_ap.bssid);
            }
            err = network_wifi_add_ap_copy(&known_ap);
        } else {
            ESP_LOGE(TAG, "Duplicate ssid %s found in storage", cJSON_GetStringValue(value));
        }
    }
    cJSON_Delete(cjson_item);
    return err;
}
esp_err_t network_wifi_delete_ap(const char* key) {
    esp_err_t esp_err = ESP_OK;
    if (!key || strlen(key) == 0) {
        ESP_LOGE(TAG, "SSID Empty. Cannot remove ");
        return ESP_ERR_INVALID_ARG;
    }

    known_access_point_t* it = network_wifi_get_ap_entry(key);
    if (!it) {
        ESP_LOGE(TAG, "Unknown AP entry");
        return ESP_ERR_INVALID_ARG;
    }

    /* 
     * Check if we're deleting the active network
     */
    ESP_LOGD(TAG, "Deleting AP %s. Checking if this is the active AP", key);
    const wifi_sta_config_t* config = network_wifi_load_active_config();
    if (config && strlen(ssid_string(config)) > 0 && strcmp(ssid_string(config), it->ssid) == 0) {
        ESP_LOGD(TAG, "Confirmed %s to be the active network. Removing it from flash.", key);
        esp_err = network_wifi_erase_legacy();
        if (esp_err != ESP_OK) {
            ESP_LOGW(TAG, "Legacy network details could not be removed from flash : %s", esp_err_to_name(esp_err));
        }
    }
    ESP_LOGD(TAG, "Removing network %s from the flash AP list", key);
    esp_err = erase_nvs_for_partition(NVS_DEFAULT_PART_NAME, ap_list_nsv_namespace, it->ssid);
    if (esp_err != ESP_OK) {
        messaging_post_message(MESSAGING_ERROR, MESSAGING_CLASS_SYSTEM, "Deleting network entry %s error (%s). Error %s", key, ap_list_nsv_namespace, esp_err_to_name(esp_err));
    }
    ESP_LOGD(TAG, "Removing network %s from the known AP list", key);
    network_wifi_remove_ap_entry(it->ssid);
    return esp_err;
}

esp_err_t network_wifi_erase_legacy() {
    esp_err_t err = erase_nvs_partition(NVS_DEFAULT_PART_NAME, network_wifi_nvs_namespace);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "Erased wifi configuration. Disconnecting from network");
        if ((err = esp_wifi_disconnect()) != ESP_OK) {
            ESP_LOGW(TAG, "Could not disconnect from deleted network : %s", esp_err_to_name(err));
        }
    }
    return err;
}

esp_err_t network_wifi_erase_known_ap() {
    network_wifi_empty_known_list();
    esp_err_t err = erase_nvs_partition(NVS_DEFAULT_PART_NAME, ap_list_nsv_namespace);
    return err;
}

esp_err_t network_wifi_write_ap(const char* key, const char* value, size_t size) {
    size_t size_override = size > 0 ? size : strlen(value) + 1;
    esp_err_t esp_err = store_nvs_value_len_for_partition(NVS_DEFAULT_PART_NAME, ap_list_nsv_namespace, NVS_TYPE_BLOB, key, value, size_override);
    if (esp_err != ESP_OK) {
        messaging_post_message(MESSAGING_ERROR, MESSAGING_CLASS_SYSTEM, "%s (%s). Error %s", key, network_wifi_nvs_namespace, esp_err_to_name(esp_err));
    }
    return esp_err;
}
esp_err_t network_wifi_write_nvs(const char* key, const char* value, size_t size) {
    size_t size_override = size > 0 ? size : strlen(value) + 1;
    esp_err_t esp_err = store_nvs_value_len_for_partition(NVS_DEFAULT_PART_NAME, network_wifi_nvs_namespace, NVS_TYPE_BLOB, key, value, size_override);
    if (esp_err != ESP_OK) {
        messaging_post_message(MESSAGING_ERROR, MESSAGING_CLASS_SYSTEM, "%s (%s). Error %s", key, network_wifi_nvs_namespace, esp_err_to_name(esp_err));
    }
    return esp_err;
}

esp_err_t network_wifi_store_ap_json(known_access_point_t* item) {
    esp_err_t err = ESP_OK;
    size_t size = 0;
    char* json_string = NULL;
    const wifi_sta_config_t* sta = network_wifi_get_active_config();

    if ((err = network_wifi_alloc_ap_json(item, &json_string)) == ESP_OK) {
        // get any existing entry from the nvs and compare
        char* existing = get_nvs_value_alloc_for_partition(NVS_DEFAULT_PART_NAME, ap_list_nsv_namespace, NVS_TYPE_BLOB, item->ssid, &size);
        if (!existing || strncmp(existing, json_string, strlen(json_string)) != 0) {
            ESP_LOGI(TAG, "SSID %s was changed or is new. Committing to flash", item->ssid);
            err = network_wifi_write_ap(item->ssid, json_string, 0);
            if (sta && strlen(ssid_string(sta)) > 0 && strcmp(ssid_string(sta), item->ssid) == 0) {
                ESP_LOGI(TAG, "Committing active access point");
                err = network_wifi_write_nvs("ssid", ssid_string(sta), 0);
                if (err == ESP_OK) {
                    err = network_wifi_write_nvs("password", STR_OR_BLANK(password_string(sta)), 0);
                }
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Error committing active access point : %s", esp_err_to_name(err));
                }
            }
        }
        FREE_AND_NULL(existing);
        FREE_AND_NULL(json_string);
    }
    return err;
}

esp_netif_t* network_wifi_get_interface() {
    return wifi_netif;
}
esp_netif_t* network_wifi_get_ap_interface() {
    return wifi_ap_netif;
}
static wifi_ps_type_t network_wifi_get_ps_mode() {
    uint16_t ps_mode = DEFAULT_STA_POWER_SAVE;
    config_get_uint16t_from_str("wifi_ps", &ps_mode, DEFAULT_STA_POWER_SAVE);
    if (ps_mode > WIFI_PS_MAX_MODEM) {
        ESP_LOGW(TAG, "Invalid wifi_ps value %d, defaulting to %d", ps_mode, DEFAULT_STA_POWER_SAVE);
        ps_mode = DEFAULT_STA_POWER_SAVE;
    }
    ESP_LOGI(TAG, "WiFi power save mode: %d (0=none, 1=min_modem, 2=max_modem)", ps_mode);
    return (wifi_ps_type_t)ps_mode;
}

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
        } else {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(network_wifi_get_ps_mode()));
        }
    }
    return err;
}
esp_netif_t* network_wifi_start() {
    MEMTRACE_PRINT_DELTA_MESSAGE( "Starting wifi interface as STA mode");
    accessp_cjson = network_manager_clear_ap_list_json(&accessp_cjson);
    if (!wifi_netif) {
        MEMTRACE_PRINT_DELTA_MESSAGE("Init STA mode - creating default interface. ");
        wifi_netif = esp_netif_create_default_wifi_sta();
        MEMTRACE_PRINT_DELTA_MESSAGE("Initializing Wifi. ");
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_init(&cfg));
        MEMTRACE_PRINT_DELTA_MESSAGE("Registering wifi Handlers");
        //network_wifi_register_handlers();
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(WIFI_EVENT,
                                                                          ESP_EVENT_ANY_ID,
                                                                          &network_wifi_event_handler,
                                                                          NULL,
                                                                          NULL));
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
    cJSON_Delete(accessp_cjson);
    accessp_cjson = NULL;
}

bool network_wifi_sta_config_changed() {
    bool changed = true;
    const wifi_sta_config_t* sta = network_wifi_get_active_config();
    if (!sta || strlen(ssid_string(sta)) == 0)
        return false;

    known_access_point_t* known = network_wifi_get_ap_entry(ssid_string(sta));
    if (known && strcmp(known->ssid, ssid_string(sta)) == 0 &&
        strcmp((char*)known->password, password_string(sta)) == 0) {
        changed = false;
    } else {
        ESP_LOGI(TAG, "New network configuration found");
    }
    return changed;
}

esp_err_t network_wifi_save_sta_config() {
    esp_err_t esp_err = ESP_OK;
    known_access_point_t* item = NULL;
    MEMTRACE_PRINT_DELTA_MESSAGE("Config Save");

    const wifi_sta_config_t* sta = network_wifi_get_active_config();
    if (sta && strlen(ssid_string(sta)) > 0) {
        MEMTRACE_PRINT_DELTA_MESSAGE("Checking if current SSID is known");
        item = network_wifi_get_ap_entry(ssid_string(sta));
        if (!item) {
            ESP_LOGD(TAG,"New SSID %s found", ssid_string(sta));
            // this is a new access point. First add it to the end of the AP list
            esp_err = network_wifi_add_ap_from_sta_copy(sta);
        }
    }
    // now traverse the list and commit
    MEMTRACE_PRINT_DELTA_MESSAGE("Saving all known ap as json strings");
    known_access_point_t* it;
    SLIST_FOREACH(it, &s_ap_list, next) {
        if ((esp_err = network_wifi_store_ap_json(it)) != ESP_OK) {
            ESP_LOGW(TAG, "Error saving wifi ap entry %s : %s", it->ssid, esp_err_to_name(esp_err));
            break;
        }
    }
    return esp_err;
}

void network_wifi_load_known_access_points() {
    esp_err_t esp_err;
    size_t size = 0;
    if (network_wifi_get_known_count() > 0) {
        ESP_LOGW(TAG, "Access points already loaded");
        return;
    }
    nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, ap_list_nsv_namespace, NVS_TYPE_ANY);
    if (it == NULL) {
        ESP_LOGW(TAG, "No known access point found");
        return;
    }
    do {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (strstr(info.namespace_name, ap_list_nsv_namespace)) {
            void* value = get_nvs_value_alloc_for_partition(NVS_DEFAULT_PART_NAME, ap_list_nsv_namespace, info.type, info.key, &size);
            if (value == NULL) {
                ESP_LOGE(TAG, "nvs read failed for %s.", info.key);
            } else if ((esp_err = network_wifi_add_json_entry(value)) != ESP_OK) {
                ESP_LOGE(TAG, "Invalid entry or error for %s.", (char*)value);
            }
            FREE_AND_NULL(value);
        }
        it = nvs_entry_next(it);
    } while (it != NULL);

    return;
}

esp_err_t network_wifi_get_blob(void* target, size_t size, const char* key) {
    esp_err_t esp_err = ESP_OK;
    size_t found_size = 0;
    if (!target) {
        ESP_LOGE(TAG, "%s invalid target pointer", __FUNCTION__);
        return ESP_ERR_INVALID_ARG;
    }
    memset(target, 0x00, size);
    char* value = (char*)get_nvs_value_alloc_for_partition(NVS_DEFAULT_PART_NAME, network_wifi_nvs_namespace, NVS_TYPE_BLOB, key, &found_size);
    if (!value) {
        ESP_LOGD(TAG,"nvs key %s not found.", key);
        esp_err = ESP_FAIL;
    } else {
        memcpy((char*)target, value, size > found_size ? found_size : size);
        FREE_AND_NULL(value);
        ESP_LOGD(TAG,"Successfully loaded key %s", key);
    }
    return esp_err;
}
const wifi_sta_config_t* network_wifi_load_active_config() {
    static wifi_sta_config_t config;
    esp_err_t esp_err = ESP_OK;
    memset(&config, 0x00, sizeof(config));
    config.scan_method = WIFI_ALL_CHANNEL_SCAN;
    MEMTRACE_PRINT_DELTA_MESSAGE("Fetching wifi sta config - ssid.");
    esp_err = network_wifi_get_blob(&config.ssid, sizeof(config.ssid), "ssid");
    if (esp_err == ESP_OK && strlen((char*)config.ssid) > 0) {
        ESP_LOGD(TAG,"network_wifi_load_active_config: ssid:%s. Fetching password (if any) ", ssid_string(&config));
        if (network_wifi_get_blob(&config.password, sizeof(config.password), "password") != ESP_OK) {
            ESP_LOGW(TAG, "No wifi password found in nvs");
        }
    } else {
        if(network_wifi_get_known_count() > 0) {
            ESP_LOGW(TAG, "No wifi ssid found in nvs, but known access points found. Using first known access point.");
            known_access_point_t* ap = SLIST_FIRST(&s_ap_list);
            if (ap) {
                strncpy((char*)&config.ssid, ap->ssid, sizeof(config.ssid));
                strncpy((char*)&config.password, ap->password, sizeof(config.password));
            }
            esp_err = ESP_OK;
        } else {
            ESP_LOGW(TAG, "network manager has no previous configuration. %s", esp_err_to_name(esp_err));
            return NULL;
        }
    }
    return &config;
}
bool network_wifi_load_wifi_sta_config() {
    network_wifi_load_known_access_points();
    const wifi_sta_config_t* config = network_wifi_load_active_config();
    if (config) {
        known_access_point_t* item = network_wifi_get_ap_entry(ssid_string(config));
        if (!item) {
            ESP_LOGI(TAG, "Adding legacy/active wifi connection to the known list");
            network_wifi_add_ap_from_sta_copy(config);
        }
    }
    return config && config->ssid[0] != '\0';
}
bool network_wifi_get_config_for_ssid(wifi_config_t* config, const char* ssid) {
    known_access_point_t* item = network_wifi_get_ap_entry(ssid);
    if (!item) {
        ESP_LOGE(TAG, "Unknown ssid %s", ssid);
        return false;
    }
    memset(&config->ap, 0x00, sizeof(config->ap));
    strncpy((char*)config->ap.ssid, item->ssid, sizeof(config->ap.ssid));
    strncpy((char*)config->ap.password, item->password, sizeof(config->ap.ssid));
    config->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    return true;
}

static void network_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base != WIFI_EVENT)
        return;
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
                ESP_LOGD(TAG, "WIFI_EVENT_AP_PROBEREQRECVED. RSSI: %d, MAC: %s", s->rssi, STR_OR_BLANK(mac));
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
                ESP_LOGD(TAG, "WIFI_EVENT_AP_STACONNECTED. aid: %d, mac: %s", stac->aid, STR_OR_BLANK(mac));
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
                ESP_LOGD(TAG, "WIFI_EVENT_STA_CONNECTED. Channel: %d, Access point: %s, BSSID: %s ", s->channel, STR_OR_BLANK(ssid), (bssid));
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
            ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED. From BSSID: %s, reason code: %d (%s)", STR_OR_BLANK(bssid), s->reason, get_disconnect_code_desc(s->reason));
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

cJSON* network_wifi_get_new_array_json(cJSON** old) {
    ESP_LOGV(TAG, "network_wifi_get_new_array_json called");
    cJSON* root = *old;
    if (root != NULL) {
        cJSON_Delete(root);
        *old = NULL;
    }
    ESP_LOGV(TAG, "network_wifi_get_new_array_json done");
    return cJSON_CreateArray();
}
void network_wifi_global_init() {
    network_wifi_get_new_array_json(&accessp_cjson);
    ESP_LOGD(TAG, "Loading existing wifi configuration (if any)");
    network_wifi_load_wifi_sta_config();
}
void network_wifi_add_access_point_json(cJSON* ap_list, wifi_ap_record_t* ap_rec) {
    cJSON* ap = cJSON_CreateObject();
    if (ap == NULL) {
        ESP_LOGE(TAG, "Unable to allocate memory for access point %s", ap_rec->ssid);
        return;
    }
    cJSON* radio = cJSON_CreateObject();
    if (radio == NULL) {
        ESP_LOGE(TAG, "Unable to allocate memory for access point %s", ap_rec->ssid);
        cJSON_Delete(ap);
        return;
    }
    cJSON_AddItemToObject(ap, "ssid", cJSON_CreateString(ap_ssid_string(ap_rec)));
    cJSON_AddBoolToObject(ap, "known", network_wifi_is_known_ap(ap_ssid_string(ap_rec)));
    if (ap_rec->rssi != 0) {
        // only add the rest of the details when record doesn't come from
        // "known" access points that aren't in range
        cJSON_AddNumberToObject(ap, "chan", ap_rec->primary);
        cJSON_AddNumberToObject(ap, "rssi", ap_rec->rssi);
        cJSON_AddNumberToObject(ap, "auth", ap_rec->authmode);

        char* bssid = network_manager_alloc_get_mac_string(ap_rec->bssid);
        if (bssid) {
            cJSON_AddItemToObject(ap, "bssid", cJSON_CreateString(STR_OR_BLANK(bssid)));
        }
        FREE_AND_NULL(bssid);
        cJSON_AddNumberToObject(radio, "b", ap_rec->phy_11b ? 1 : 0);
        cJSON_AddNumberToObject(radio, "g", ap_rec->phy_11g ? 1 : 0);
        cJSON_AddNumberToObject(radio, "n", ap_rec->phy_11n ? 1 : 0);
        cJSON_AddNumberToObject(radio, "low_rate", ap_rec->phy_lr ? 1 : 0);
        cJSON_AddItemToObject(ap, "radio", radio);
    }
    cJSON_AddItemToArray(ap_list, ap);
    char* ap_json = cJSON_PrintUnformatted(ap);
    if (ap_json != NULL) {
        ESP_LOGD(TAG, "New access point found: %s", ap_json);
        free(ap_json);
    }
}
void network_wifi_generate_access_points_json(cJSON** ap_list) {
    *ap_list = network_wifi_get_new_array_json(ap_list);
    wifi_ap_record_t known_ap;
    known_access_point_t* it;
    if (*ap_list == NULL)
        return;
    for (int i = 0; i < ap_num; i++) {
        network_wifi_add_access_point_json(*ap_list, &accessp_records[i]);
    }
    SLIST_FOREACH(it, &s_ap_list, next) {
        if (!network_wifi_was_ssid_seen(it->ssid)) {
            memset(&known_ap, 0x00, sizeof(known_ap));
            strlcpy((char*)known_ap.ssid, it->ssid, sizeof(known_ap.ssid));
            ESP_LOGD(TAG, "Adding known access point that is not in range: %s", it->ssid);
            network_wifi_add_access_point_json(*ap_list, &known_ap);
        }
    }
    char* ap_list_json = cJSON_PrintUnformatted(*ap_list);
    if (ap_list_json != NULL) {
        ESP_LOGV(TAG, "Full access point list: %s", ap_list_json);
        free(ap_list_json);
    }
}
void network_wifi_set_ipv4val(const char* key, char* default_value, ip4_addr_t* target) {
    char* value = config_alloc_get_default(NVS_TYPE_STR, key, default_value, 0);
    if (value != NULL) {
        ESP_LOGD(TAG, "%s: %s", key, value);
        inet_pton(AF_INET, value, target); /* access point is on a static IP */
    }
    FREE_AND_NULL(value);
}
esp_netif_t* network_wifi_config_ap() {
    esp_netif_ip_info_t info;
    esp_err_t err = ESP_OK;
    char* value = NULL;
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = 0,
        },
    };
    ESP_LOGI(TAG, "Configuring Access Point.");
    if (!wifi_ap_netif) {
        wifi_ap_netif = esp_netif_create_default_wifi_ap();
    }

    network_wifi_set_ipv4val("ap_ip_address", DEFAULT_AP_IP, (ip4_addr_t*)&info.ip);
    network_wifi_set_ipv4val("ap_ip_gateway", CONFIG_DEFAULT_AP_GATEWAY, (ip4_addr_t*)&info.gw);
    network_wifi_set_ipv4val("ap_ip_netmask", CONFIG_DEFAULT_AP_NETMASK, (ip4_addr_t*)&info.netmask);
    /* In order to change the IP info structure, we have to first stop 
     * the DHCP server on the new interface 
    */
    network_start_stop_dhcps(wifi_ap_netif, false);
    ESP_LOGD(TAG, "Setting tcp_ip info for access point");
    if ((err = esp_netif_set_ip_info(wifi_ap_netif, &info)) != ESP_OK) {
        ESP_LOGE(TAG, "Setting tcp_ip info for interface TCPIP_ADAPTER_IF_AP. Error %s", esp_err_to_name(err));
        return wifi_ap_netif;
    }
    network_start_stop_dhcps(wifi_ap_netif, true);

    /*
		 * Set Access Point configuration
		 */
    value = config_alloc_get_default(NVS_TYPE_STR, "ap_ssid", CONFIG_DEFAULT_AP_SSID, 0);
    if (value != NULL) {
        strlcpy((char*)ap_config.ap.ssid, value, sizeof(ap_config.ap.ssid));
        ESP_LOGI(TAG, "AP SSID: %s", (char*)ap_config.ap.ssid);
    }
    FREE_AND_NULL(value);

    value = config_alloc_get_default(NVS_TYPE_STR, "ap_pwd", DEFAULT_AP_PASSWORD, 0);
    if (value != NULL) {
        strlcpy((char*)ap_config.ap.password, value, sizeof(ap_config.ap.password));
        ESP_LOGI(TAG, "AP Password: %s", (char*)ap_config.ap.password);
    }
    FREE_AND_NULL(value);

    value = config_alloc_get_default(NVS_TYPE_STR, "ap_channel", STR(CONFIG_DEFAULT_AP_CHANNEL), 0);
    if (value != NULL) {
        ESP_LOGD(TAG, "Channel: %s", value);
        ap_config.ap.channel = atoi(value);
    }
    FREE_AND_NULL(value);

    ap_config.ap.authmode = AP_AUTHMODE;
    ap_config.ap.ssid_hidden = DEFAULT_AP_SSID_HIDDEN;
    ap_config.ap.max_connection = DEFAULT_AP_MAX_CONNECTIONS;
    ap_config.ap.beacon_interval = DEFAULT_AP_BEACON_INTERVAL;

    ESP_LOGD(TAG, "Auth Mode: %d", ap_config.ap.authmode);
    ESP_LOGD(TAG, "SSID Hidden: %d", ap_config.ap.ssid_hidden);
    ESP_LOGD(TAG, "Max Connections: %d", ap_config.ap.max_connection);
    ESP_LOGD(TAG, "Beacon interval: %d", ap_config.ap.beacon_interval);

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
    if ((err = esp_wifi_set_bandwidth(WIFI_IF_AP, DEFAULT_AP_BANDWIDTH)) != ESP_OK) /* stop AP DHCP server */
    {
        ESP_LOGE(TAG, "%s failed. Error %s", msg, esp_err_to_name(err));
        return wifi_ap_netif;
    }

    msg = "Setting wifi power save";
    wifi_ps_type_t ps_mode = network_wifi_get_ps_mode();
    ESP_LOGD(TAG, "%s (%d)", msg, ps_mode);

    if ((err = esp_wifi_set_ps(ps_mode)) != ESP_OK)
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
        if (ap->ssid[0] == 0)
            continue;

        /* remove the identical SSID+authmodes */
        for (int j = i + 1; j < *aps; j++) {
            wifi_ap_record_t* ap1 = &aplist[j];
            if ((strcmp((const char*)ap->ssid, (const char*)ap1->ssid) == 0) &&
                (ap->authmode == ap1->authmode)) { /* same SSID, different auth mode is skipped */
                /* save the rssi for the display */
                if ((ap1->rssi) > (ap->rssi))
                    ap->rssi = ap1->rssi;
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
            if (first_free == NULL)
                first_free = ap;
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

char* network_status_alloc_get_ap_list_json() {
    return cJSON_PrintUnformatted(accessp_cjson);
}
cJSON* network_manager_clear_ap_list_json(cJSON** old) {
    ESP_LOGV(TAG, "network_manager_clear_ap_list_json called");
    cJSON* root = network_wifi_get_new_array_json(old);
    ESP_LOGV(TAG, "network_manager_clear_ap_list_json done");
    return root;
}

esp_err_t network_wifi_built_known_ap_list() {
    if (network_status_lock_json_buffer(pdMS_TO_TICKS(1000))) {
        ESP_LOGD(TAG,"Building known AP list");
        accessp_cjson = network_manager_clear_ap_list_json(&accessp_cjson);
        network_wifi_generate_access_points_json(&accessp_cjson);
        network_status_unlock_json_buffer();
        ESP_LOGD(TAG, "Done building ap JSON list");
    } else {
        ESP_LOGE(TAG, "Failed to lock json buffer");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t wifi_scan_done() {
    esp_err_t err = ESP_OK;
    /* As input param, it stores max AP number ap_records can hold. As output param, it receives the actual AP number this API returns.
				 * As a consequence, ap_num MUST be reset to MAX_AP_NUM at every scan */
    ESP_LOGD(TAG, "Getting AP list records");
    ap_num = MAX_AP_NUM;
    if ((err = esp_wifi_scan_get_ap_num(&ap_num)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to retrieve scan results count. Error %s", esp_err_to_name(err));
        return err;
    }
    FREE_AND_NULL(accessp_records);
    if (ap_num > 0) {
        accessp_records = (wifi_ap_record_t*)malloc_init_external(sizeof(wifi_ap_record_t) * ap_num);
        if ((err = esp_wifi_scan_get_ap_records(&ap_num, accessp_records)) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to retrieve scan results list. Error %s", esp_err_to_name(err));
            return err;
        }
        /* make sure the http server isn't trying to access the list while it gets refreshed */
        ESP_LOGD(TAG, "Preparing to build ap JSON list");
        if (network_status_lock_json_buffer(pdMS_TO_TICKS(1000))) {
            /* Will remove the duplicate SSIDs from the list and update ap_num */
            network_wifi_filter_unique(accessp_records, &ap_num);
            network_wifi_set_found_ap();
            network_wifi_generate_access_points_json(&accessp_cjson);
            network_status_unlock_json_buffer();
            ESP_LOGD(TAG, "Done building ap JSON list");
        } else {
            ESP_LOGE(TAG, "could not get access to json mutex in wifi_scan");
            err = ESP_FAIL;
        }
    } else {
        //
        ESP_LOGD(TAG, "No AP Found.  Emptying the list.");
        accessp_cjson = network_wifi_get_new_array_json(&accessp_cjson);
    }
    return err;
}
bool is_wifi_up() {
    return wifi_netif != NULL;
}
esp_err_t network_wifi_start_scan() {
    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .show_hidden = true};
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Initiating wifi network scan");
    if (!is_wifi_up()) {
        messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Wifi not started. Cannot scan");
        return ESP_FAIL;
    }
    /* if a scan is already in progress this message is simply ignored thanks to the WIFI_MANAGER_SCAN_BIT uxBit */
    if ((err = esp_wifi_scan_start(&scan_config, false)) != ESP_OK) {
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
    memset(&config, 0x00, sizeof(config));
    ESP_LOGD(TAG, "network_wifi_connect");
    if (!is_wifi_up()) {
        messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Wifi not started. Cannot connect");
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
esp_err_t network_wifi_connect_next_in_range(){
    const char * ssid = network_wifi_get_next_ap_in_range();
    if(ssid){
        return network_wifi_connect_ssid(ssid);
    }
    return ESP_FAIL;
}
esp_err_t network_wifi_connect_ssid(const char* ssid) {
    known_access_point_t* item = network_wifi_get_ap_entry(ssid);
    if (item) {
        item->last_try = (esp_timer_get_time() / 1000);
        return network_wifi_connect(item->ssid, item->password);
    }
    return ESP_FAIL;
}
esp_err_t network_wifi_connect_active_ssid() {
    const wifi_sta_config_t* config = network_wifi_load_active_config();
    if (config) {
        return network_wifi_connect(ssid_string(config), password_string(config));
    }
    return ESP_FAIL;
}
void network_wifi_clear_config() {
    /* erase configuration */
    const wifi_sta_config_t* sta = network_wifi_get_active_config();
    network_wifi_delete_ap(ssid_string(sta));
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
