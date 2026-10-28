#ifdef NETWORK_STATUS_LOG_LEVEL
#define LOG_LOCAL_LEVEL NETWORK_STATUS_LOG_LEVEL
#endif

#include "network_status.h"
#include <string.h>
#ifdef CONFIG_BT_ENABLED
#include "bt_app_core.h"
#endif
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lwip/inet.h"
#include "monitor.h"
#include "network_ethernet.h"
#include "network_wifi.h"
#include "platform_config.h"
#include "platform_esp32.h"
#include "tools.h"
#include "trace.h"
#ifndef CONFIG_SQUEEZELITE_ESP32_RELEASE_URL
#pragma message "Defaulting release url"
#define CONFIG_SQUEEZELITE_ESP32_RELEASE_URL "https://github.com/sle118/squeezelite-esp32/releases"
#endif
static const char TAG[] = "network_status";
SemaphoreHandle_t network_status_json_mutex = NULL;
static TaskHandle_t network_json_locked_task = NULL;
SemaphoreHandle_t network_status_ip_address_mutex = NULL;
static TaskHandle_t network_status_ip_address_locked_task = NULL;
char* release_url = NULL;
char* network_status_ip_address = NULL;
char* ip_info_json = NULL;
cJSON* ip_info_cjson = NULL;
static char lms_server_ip[IP4ADDR_STRLEN_MAX] = {0};
static uint16_t lms_server_port = 0;
static uint16_t lms_server_cport = 0;
static void (*chained_notify)(in_addr_t, u16_t, u16_t);
static void connect_notify(in_addr_t ip, u16_t hport, u16_t cport);
#define STA_IP_LEN sizeof(char) * IP4ADDR_STRLEN_MAX

void init_network_status() {
    chained_notify = server_notify;
    server_notify = connect_notify;
    ESP_LOGD(TAG, "init_network_status.  Creating mutexes");
    network_status_json_mutex = xSemaphoreCreateMutex();
    network_status_ip_address_mutex = xSemaphoreCreateMutex();
    ip_info_json = NULL;
    ESP_LOGD(TAG, "init_network_status.  Creating status json structure");
    ip_info_cjson = network_status_clear_ip_info_json(&ip_info_cjson);
    ESP_LOGD(TAG, "Getting release url ");
    char* release_url = (char*)config_alloc_get_default(NVS_TYPE_STR, "release_url", QUOTE(CONFIG_SQUEEZELITE_ESP32_RELEASE_URL), 0);
    if (release_url == NULL) {
        ESP_LOGE(TAG, "Unable to retrieve the release url from nvs");
    } else {
        ESP_LOGD(TAG, "Found release url %s", release_url);
    }
    ESP_LOGD(TAG, "About to set the STA IP String to 0.0.0.0");
    network_status_ip_address = (char*)malloc_init_external(STA_IP_LEN);
    network_status_safe_update_sta_ip_string(NULL);
}
void destroy_network_status() {
    FREE_AND_NULL(release_url);
    FREE_AND_NULL(ip_info_json);
    FREE_AND_NULL(network_status_ip_address);
    cJSON_Delete(ip_info_cjson);
    vSemaphoreDelete(network_status_json_mutex);
    network_status_json_mutex = NULL;
    vSemaphoreDelete(network_status_ip_address_mutex);
    network_status_ip_address_mutex = NULL;
    ip_info_cjson = NULL;
}
cJSON* network_status_get_new_json(cJSON** old) {
    ESP_LOGV(TAG, "network_status_get_new_json called");
    cJSON* root = *old;
    if (root != NULL) {
        cJSON_Delete(root);
        *old = NULL;
    }
    ESP_LOGV(TAG, "network_status_get_new_json done");
    return cJSON_CreateObject();
}

cJSON* network_status_clear_ip_info_json(cJSON** old) {
    ESP_LOGV(TAG, "network_status_clear_ip_info_json called");
    cJSON* root = network_status_get_basic_info(old);
    cJSON_DeleteItemFromObjectCaseSensitive(root, "ip");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "netmask");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "gw");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "rssi");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "ssid");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "eth");

    ESP_LOGV(TAG, "network_status_clear_ip_info_json done");
    return root;
}
void network_status_clear_ip() {
    if (network_status_lock_json_buffer(portMAX_DELAY)) {
        ip_info_cjson = network_status_clear_ip_info_json(&ip_info_cjson);
        network_status_unlock_json_buffer();
    }
}
char* network_status_alloc_get_ip_info_json() {
    return cJSON_PrintUnformatted(ip_info_cjson);
}

void network_status_unlock_json_buffer() {
    ESP_LOGV(TAG, "Unlocking json buffer!");
    network_json_locked_task = NULL;
    xSemaphoreGive(network_status_json_mutex);
}

bool network_status_lock_json_buffer(TickType_t xTicksToWait) {
    ESP_LOGV(TAG, "Locking json buffer");

    TaskHandle_t calling_task = xTaskGetCurrentTaskHandle();
    if (calling_task == network_json_locked_task) {
        ESP_LOGV(TAG, "json buffer already locked to current task");
        return true;
    }

    if (network_status_json_mutex) {
        if (xSemaphoreTake(network_status_json_mutex, xTicksToWait) == pdTRUE) {
            ESP_LOGV(TAG, "Json buffer locked!");
            network_json_locked_task = calling_task;
            return true;
        } else {
            ESP_LOGE(TAG, "Semaphore take failed. Unable to lock json buffer mutex");
            return false;
        }
    } else {
        ESP_LOGV(TAG, "Unable to lock json buffer mutex");
        return false;
    }
}

bool network_status_lock_sta_ip_string(TickType_t xTicksToWait) {
    TaskHandle_t calling_task = xTaskGetCurrentTaskHandle();
    if (calling_task == network_status_ip_address_locked_task) {
        ESP_LOGD(TAG, "json buffer already locked to current task ");
        return true;
    }
    if (network_status_ip_address_mutex) {
        if (xSemaphoreTake(network_status_ip_address_mutex, xTicksToWait) == pdTRUE) {
            network_status_ip_address_locked_task = calling_task;
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

void network_status_unlock_sta_ip_string() {
    network_status_ip_address_locked_task = NULL;
    xSemaphoreGive(network_status_ip_address_mutex);
}

void network_status_safe_update_sta_ip_string(esp_ip4_addr_t* ip4) {
    if (network_status_lock_sta_ip_string(portMAX_DELAY)) {
        strcpy(network_status_ip_address, ip4 != NULL ? ip4addr_ntoa((ip4_addr_t*)ip4) : "0.0.0.0");
        ESP_LOGD(TAG, "Set STA IP String to: %s", network_status_ip_address);
        network_status_unlock_sta_ip_string();
    }
}
void network_status_safe_reset_sta_ip_string() {
    if (network_status_lock_sta_ip_string(portMAX_DELAY)) {
        strcpy(network_status_ip_address, "0.0.0.0");
        ESP_LOGD(TAG, "Set STA IP String to: %s", network_status_ip_address);
        network_status_unlock_sta_ip_string();
    }
}
char* network_status_get_sta_ip_string() {
    return network_status_ip_address;
}
void set_lms_server_details(in_addr_t ip, u16_t hport, u16_t cport) {
    strncpy(lms_server_ip, inet_ntoa(ip), sizeof(lms_server_ip));
    lms_server_ip[sizeof(lms_server_ip) - 1] = '\0';
    ESP_LOGI(TAG, "LMS IP: %s, hport: %d, cport: %d", lms_server_ip, hport, cport);
    lms_server_port = hport;
    lms_server_cport = cport;
}
static void connect_notify(in_addr_t ip, u16_t hport, u16_t cport) {
    set_lms_server_details(ip, hport, cport);
    if (chained_notify)
        (*chained_notify)(ip, hport, cport);
    network_async_update_status();
}

void network_status_update_basic_info() {
    // locking happens below this level
    network_status_get_basic_info(&ip_info_cjson);
}

cJSON* network_status_update_float(cJSON** root, const char* key, float value) {
    if (network_status_lock_json_buffer(portMAX_DELAY)) {
        if (*root == NULL) {
            *root = cJSON_CreateObject();
        }

        if (key && strlen(key) != 0) {
            cJSON* cjsonvalue = cJSON_GetObjectItemCaseSensitive(*root, key);
            if (cjsonvalue) {
                cJSON_SetNumberValue(cjsonvalue, value);
            } else {
                cJSON_AddNumberToObject(*root, key, value);
            }
        }
        network_status_unlock_json_buffer();
    } else {
        ESP_LOGW(TAG, "Unable to lock status json buffer. ");
    }
    return *root;
}
cJSON* network_status_update_bool(cJSON** root, const char* key, bool value) {
    if (network_status_lock_json_buffer(portMAX_DELAY)) {
        if (*root == NULL) {
            *root = cJSON_CreateObject();
        }

        if (key && strlen(key) != 0) {
            cJSON* cjsonvalue = cJSON_GetObjectItemCaseSensitive(*root, key);
            if (cjsonvalue) {
                cjsonvalue->type = value ? cJSON_True : cJSON_False;
            } else {
                cJSON_AddBoolToObject(*root, key, value);
            }
        }
        network_status_unlock_json_buffer();
    } else {
        ESP_LOGW(TAG, "Unable to lock status json buffer. ");
    }
    return *root;
}
cJSON * network_update_cjson_string(cJSON** root, const char* key, const char* value){
    if (network_status_lock_json_buffer(portMAX_DELAY)) {
        cjson_update_string(root, key, value);
        network_status_unlock_json_buffer();
    } else {
        ESP_LOGW(TAG, "Unable to lock status json buffer. ");
    }
    return *root;
}
cJSON * network_update_cjson_number(cJSON** root, const char* key, int value){
    if (network_status_lock_json_buffer(portMAX_DELAY)) {
        cjson_update_number(root, key, value);
        network_status_unlock_json_buffer();
    } else {
        ESP_LOGW(TAG, "Unable to lock status json buffer. ");
    }
    return *root;
}
cJSON* network_status_get_basic_info(cJSON** old) {
    if (network_status_lock_json_buffer(portMAX_DELAY)) {
        network_t* nm = network_get_state_machine();
        monitor_gpio_t* mgpio = get_jack_insertion_gpio();
        const esp_app_desc_t* desc = esp_ota_get_app_description();

        *old = network_update_cjson_string(old, "project_name", desc->project_name);
#ifdef CONFIG_FW_PLATFORM_NAME
        *old = network_update_cjson_string(old, "platform_name", CONFIG_FW_PLATFORM_NAME);
#endif
        *old = network_update_cjson_string(old, "version", desc->version);
        if (release_url != NULL)
            *old = network_update_cjson_string(old, "release_url", release_url);
        *old = network_update_cjson_number(old, "recovery", is_recovery_running ? 1 : 0);
        *old = network_status_update_bool(old, "Jack", mgpio->gpio >= 0 && jack_inserted_svc());
        *old = network_status_update_float(old, "Voltage", battery_value_svc());
        *old = network_update_cjson_number(old, "disconnect_count", nm->num_disconnect);
        *old = network_status_update_float(old, "avg_conn_time", nm->num_disconnect > 0 ? (nm->total_connected_time / nm->num_disconnect) : 0);
#ifdef CONFIG_BT_ENABLED        
        *old = network_update_cjson_number(old, "bt_status", bt_app_source_get_a2d_state());
        *old = network_update_cjson_number(old, "bt_sub_status", bt_app_source_get_media_state());
#endif        
#if BYTES_PER_FRAME == 8
        *old = network_update_cjson_number(old, "depth", 32);
#else
        *old = network_update_cjson_number(old, "depth", 16);
#endif
        *old = network_update_cjson_number(old, "uptime", (int)(esp_timer_get_time() / 1000000LL));
        *old = network_update_cjson_number(old, "heap_internal", (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        *old = network_update_cjson_number(old, "heap_external", (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#if CONFIG_I2C_LOCKED
        *old = network_status_update_bool(old, "is_i2c_locked", true);
#else
        *old = network_status_update_bool(old, "is_i2c_locked", false);
#endif
        if (network_ethernet_enabled()) {
            *old = network_status_update_bool(old, "eth_up", network_ethernet_is_up());
        }
        if (lms_server_cport > 0) {
            *old = network_update_cjson_number(old, "lms_cport", lms_server_cport);
        }

        if (lms_server_port > 0) {
            *old = network_update_cjson_number(old, "lms_port", lms_server_port);
        }

        if (strlen(lms_server_ip) > 0) {
            *old = network_update_cjson_string(old, "lms_ip", lms_server_ip);
        }
        ESP_LOGV(TAG, "network_status_get_basic_info done");
        network_status_unlock_json_buffer();
    } else {
        ESP_LOGW(TAG, "Unable to lock status json buffer. ");
    }
    return *old;
}
void network_status_update_address(cJSON* root, esp_netif_ip_info_t* ip_info) {
    if (!root || !ip_info) {
        ESP_LOGE(TAG, "Cannor update IP address. JSON structure or ip_info is null");
        return;
    }
    network_update_cjson_string(&root, "ip", ip4addr_ntoa((ip4_addr_t*)&ip_info->ip));
    network_update_cjson_string(&root, "netmask", ip4addr_ntoa((ip4_addr_t*)&ip_info->netmask));
    network_update_cjson_string(&root, "gw", ip4addr_ntoa((ip4_addr_t*)&ip_info->gw));
}
void network_status_update_ip_info(update_reason_code_t update_reason_code) {
    ESP_LOGV(TAG, "network_status_update_ip_info called");
    esp_netif_ip_info_t ip_info;
    if (network_status_lock_json_buffer(portMAX_DELAY)) {
        /* generate the connection info with success */

        ip_info_cjson = network_status_get_basic_info(&ip_info_cjson);
        ip_info_cjson = network_update_cjson_number(&ip_info_cjson, "urc", (int)update_reason_code);
        ESP_LOGD(TAG,"Updating ip info with reason code %d. Checking if Wifi interface is connected",update_reason_code);
        if (network_is_interface_connected(network_wifi_get_interface()) || update_reason_code == UPDATE_FAILED_ATTEMPT ) {
            network_update_cjson_string(&ip_info_cjson, "if", "wifi");
            esp_netif_get_ip_info(network_wifi_get_interface(), &ip_info);
            network_status_update_address(ip_info_cjson, &ip_info);
            if (!network_wifi_is_ap_mode()) {
                /* wifi is active, and associated to an AP */
                wifi_ap_record_t ap;
                esp_wifi_sta_get_ap_info(&ap);
                network_update_cjson_string(&ip_info_cjson, "ssid", ((char*)ap.ssid));
                network_update_cjson_number(&ip_info_cjson, "rssi", ap.rssi);
            }

        } else {
            cJSON_DeleteItemFromObjectCaseSensitive(ip_info_cjson, "rssi");
            cJSON_DeleteItemFromObjectCaseSensitive(ip_info_cjson, "ssid");
        }
        ESP_LOGD(TAG,"Checking if ethernet interface is connected");
        if (network_is_interface_connected(network_ethernet_get_interface())) {
            network_update_cjson_string(&ip_info_cjson, "if", "eth");
            esp_netif_get_ip_info(network_ethernet_get_interface(), &ip_info);
            network_status_update_address(ip_info_cjson, &ip_info);
        } 
        network_status_unlock_json_buffer();
    } else {
        ESP_LOGW(TAG, "Unable to lock status json buffer. ");
    }
    ESP_LOGV(TAG, "wifi_status_generate_ip_info_json done");
}
