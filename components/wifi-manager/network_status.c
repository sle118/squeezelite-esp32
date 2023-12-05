#ifdef NETWORK_STATUS_LOG_LEVEL
#define LOG_LOCAL_LEVEL NETWORK_STATUS_LOG_LEVEL
#endif

#include "network_status.h"
#include <string.h>
#ifdef CONFIG_BT_ENABLED
#include "bt_app_core.h"
#endif
#include "esp_log.h"
#include "lwip/inet.h"
#include "monitor.h"
#include "network_ethernet.h"
#include "network_wifi.h"
// #include "Configurator.h"
#pragma message("fixme: search for TODO below")
#include "platform_esp32.h"
#include "tools.h"
#include "trace.h"
#ifndef CONFIG_SQUEEZELITE_ESP32_RELEASE_URL
#pragma message "Defaulting release url"
#define CONFIG_SQUEEZELITE_ESP32_RELEASE_URL "https://github.com/sle118/squeezelite-esp32/releases"
#endif
static const char TAG[] = "network_status";
sys_Status status;
SemaphoreHandle_t network_status_structure_mutex = NULL;
static TaskHandle_t network_structure_locked_task = NULL;
static void (*chained_notify)(in_addr_t, u16_t, u16_t);
static void connect_notify(in_addr_t ip, u16_t hport, u16_t cport);

#define STA_IP_LEN sizeof(char) * IP4ADDR_STRLEN_MAX

void init_network_status() {
    chained_notify = server_notify;
    server_notify = connect_notify;
    ESP_LOGD(TAG, "init_network_status.  Creating mutexes");
    network_status_structure_mutex = xSemaphoreCreateMutex();
    ESP_LOGD(TAG, "Getting release url ");
    
    }
void destroy_network_status() {
    vSemaphoreDelete(network_status_structure_mutex);
    network_status_structure_mutex = NULL;
}

void network_status_unlock_structure() {
    ESP_LOGV(TAG, "Unlocking json buffer!");
    network_structure_locked_task = NULL;
    xSemaphoreGive(network_status_structure_mutex);
}

bool network_status_lock_structure(TickType_t xTicksToWait) {
    ESP_LOGV(TAG, "Locking structure buffer");

    TaskHandle_t calling_task = xTaskGetCurrentTaskHandle();
    if (calling_task == network_structure_locked_task) {
        ESP_LOGV(TAG, "structure buffer already locked to current task");
        return true;
    }

    if (network_status_structure_mutex) {
        if (xSemaphoreTake(network_status_structure_mutex, xTicksToWait) == pdTRUE) {
            ESP_LOGV(TAG, "structure locked!");
            network_structure_locked_task = calling_task;
            return true;
        } else {
            ESP_LOGE(TAG, "Semaphore take failed. Unable to lock structure mutex");
            return false;
        }
    } else {
        ESP_LOGV(TAG, "Unable to lock structure mutex");
        return false;
    }
}

char* network_status_get_sta_ip_string() {
    return status.has_net && status.net.has_ip?status.net.ip.ip:"0.0.0.0";
}
void set_lms_server_details(in_addr_t ip, u16_t hport, u16_t cport) {
    if (network_status_lock_structure(portMAX_DELAY)) {
        status.has_LMS = true;
        strncpy(status.LMS.ip, inet_ntoa(ip), sizeof(status.LMS.ip));
        status.LMS.port = hport;
        status.LMS.cport = cport;
        ESP_LOGI(TAG, "LMS IP: %s, hport: %d, cport: %d", status.LMS.ip, status.LMS.port, status.LMS.cport);
        network_status_unlock_structure();
    }
}
static void connect_notify(in_addr_t ip, u16_t hport, u16_t cport) {
    set_lms_server_details(ip, hport, cport);
    if (chained_notify)
        (*chained_notify)(ip, hport, cport);
    network_async_update_status();
}

void network_status_set_basic_info() {
    if (network_status_lock_structure(portMAX_DELAY)) {
        network_t* nm = network_get_state_machine();
        sys_GPIO * gpio = NULL;
        const esp_app_desc_t* desc = esp_ota_get_app_description();
        status.has_platform = true;
        strncpy(status.platform.project, desc->project_name, sizeof(status.platform.project));
#ifdef CONFIG_FW_PLATFORM_NAME
        strncpy(status.platform.name, CONFIG_FW_PLATFORM_NAME, sizeof(status.platform.name));
#endif
        strncpy(status.platform.version, desc->version, sizeof(status.platform.version));
        strncpy(status.platform.version, desc->version, sizeof(status.platform.version));
        status.platform.recovery = is_recovery_running;
        status.platform.depth = 16;
#if DEPTH == 16 || DEPTH == 32
        status.platform.depth = DEPTH;
#endif           
        status.has_hw = true;
	    
	    status.hw.jack_inserted = jack_inserted_svc();
        status.hw.has_jack_inserted = SYS_GPIOS_NAME(jack,gpio) && gpio->pin>=0;
        status.hw.has_spk_fault = SYS_GPIOS_NAME(spkfault,gpio) && gpio->pin>=0;
        status.hw.spk_fault = spkfault_svc();
        status.hw.batt_voltage = battery_value_svc();
        status.has_net = true;
        status.net.has_wifi = true;
        status.net.wifi.disconnect_count = nm->num_disconnect;
        status.net.wifi.avg_conn_time =  nm->num_disconnect > 0 ? (nm->total_connected_time / nm->num_disconnect) : 0;
        
#ifdef CONFIG_BT_ENABLED        
        if(platform->has_services && platform->services.has_bt_sink && platform->services.bt_sink.enabled){
            status.has_bt = true;
            status.bt.bt_status = bt_app_source_get_a2d_state();
            status.bt.bt_media_state  = bt_app_source_get_media_state();
        }
#endif        
        
        if (network_ethernet_enabled()) {
            status.net.eth_up = network_ethernet_is_up();
        }
        ESP_LOGV(TAG, "network_status_get_basic_info done");
        network_status_unlock_structure();
    } else {
        ESP_LOGW(TAG, "Unable to lock status json buffer. ");
    }
}
void network_status_update_address(esp_netif_ip_info_t* ip_info) {
    status.has_net = true;
    status.net.has_ip = true;
    strncpy(status.net.ip.ip,ip4addr_ntoa((ip4_addr_t*)&ip_info->ip),sizeof(status.net.ip.ip));
    strncpy(status.net.ip.netmask,ip4addr_ntoa((ip4_addr_t*)&ip_info->netmask),sizeof(status.net.ip.netmask));
    strncpy(status.net.ip.gw,ip4addr_ntoa((ip4_addr_t*)&ip_info->gw),sizeof(status.net.ip.gw));
}
void network_status_update_ip_info(sys_UPDATE_REASONS update_reason_code) {
    ESP_LOGV(TAG, "network_status_update_ip_info called");
    esp_netif_ip_info_t ip_info;
    if (network_status_lock_structure(portMAX_DELAY)) {
        /* generate the connection info with success */

        network_status_set_basic_info();
        status.net.updt_reason = update_reason_code;
        status.net.wifi.has_connected_sta = false;
        status.net.wifi.connected_sta.connected = false;
        
        ESP_LOGD(TAG,"Updating ip info with reason code %d. Checking if Wifi interface is connected",update_reason_code);
        if (network_is_interface_connected(network_wifi_get_interface()) || update_reason_code == sys_UPDATE_REASONS_R_FAILED_ATTEMPT ) {
            status.net.interface  = sys_CONNECTED_IF_IF_WIFI;
            
            esp_netif_get_ip_info(network_wifi_get_interface(), &ip_info);
            network_status_update_address(&ip_info);
            if (!network_wifi_is_ap_mode()) {
                /* wifi is active, and associated to an AP */
                wifi_ap_record_t ap;
                esp_wifi_sta_get_ap_info(&ap);
                network_wifi_esp_sta_to_sta(&ap, &status.net.wifi.connected_sta);
                status.net.wifi.connected_sta.connected = true;
                status.net.has_wifi = true;
                status.net.wifi.has_connected_sta = true;
            }

        } 
        ESP_LOGD(TAG,"Checking if ethernet interface is connected");
        if (network_is_interface_connected(network_ethernet_get_interface())) {
            status.net.interface  = sys_CONNECTED_IF_IF_ETHERNET;
            esp_netif_get_ip_info(network_ethernet_get_interface(), &ip_info);
            network_status_update_address(&ip_info);
        } 
        network_status_unlock_structure();
    } else {
        ESP_LOGW(TAG, "Unable to lock status json buffer. ");
    }
    ESP_LOGV(TAG, "wifi_status_generate_ip_info_json done");
}
