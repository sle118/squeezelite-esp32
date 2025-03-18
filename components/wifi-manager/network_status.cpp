#ifdef NETWORK_STATUS_LOG_LEVEL
#define LOG_LOCAL_LEVEL NETWORK_STATUS_LOG_LEVEL
#endif

#include "network_status.h"
#include <string.h>
#ifdef CONFIG_BT_ENABLED
#include "bt_app_core.h"
#endif
#include "Config.h"
#include "WifiList.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "monitor.h"
#include "network_ethernet.h"
#include "network_wifi.h"
#include "platform_esp32.h"
#include "tools.h"
#include "trace.h"
#include "bootstate.h"

#ifndef CONFIG_SQUEEZELITE_ESP32_RELEASE_URL
#pragma message "Defaulting release url"
#define CONFIG_SQUEEZELITE_ESP32_RELEASE_URL "https://github.com/sle118/squeezelite-esp32/releases"
#endif
static const char TAG[] = "network_status";

sys_status_data* sys_status = nullptr;

__attribute__((section(".ext_ram.bss"))) System::PB<sys_status_data> sys_status_obj("status", &sys_status_data_msg,sizeof(sys_status_data_msg));

static void (*chained_notify)(in_addr_t, u16_t, u16_t);
static void connect_notify(in_addr_t ip, u16_t hport, u16_t cport);

#define STA_IP_LEN sizeof(char) * IP4ADDR_STRLEN_MAX
void network_status_set_static_info() {
    ESP_LOGD(TAG, "network_status_set_static_info starting");

    if (sys_status_obj.Lock()) {
        const esp_app_desc_t* desc = esp_ota_get_app_description();
        auto status_obj = sys_status_obj.get();
        status_obj->has_platform = true;
        strncpy(
            status_obj->platform.project, desc->project_name, sizeof(status_obj->platform.project));
        if (platform->target && strlen(platform->target) > 0) {
            if (status_obj->platform.target) {
                free(status_obj->platform.target);
            }
            status_obj->platform.target = strdup_psram(platform->target);
        }
        strncpy(status_obj->platform.version, desc->version, sizeof(status_obj->platform.version));
        status_obj->platform.recovery = is_recovery_running;
        status_obj->platform.depth = 16;
#if DEPTH == 16 || DEPTH == 32
        status_obj->platform.depth = DEPTH;
#endif
        status_obj->has_hw = true;
        status_obj->hw.supports_jack_inserted = platform->gpios.has_jack &&platform->gpios.jack.pin>=0;
        status_obj->hw.supports_spk_fault = platform->gpios.has_spkfault && platform->gpios.spkfault.pin>=0;
        status_obj->has_net = true;
        status_obj->net.has_wifi = true;

#ifdef CONFIG_BT_ENABLED
        if (platform->has_services && platform->services.has_bt_sink &&
            platform->services.bt_sink.enabled) {
            status_obj->has_bt = true;
        }
#endif
        ESP_LOGD(TAG, "network_status_set_static_info done");
        sys_status_obj.Unlock();
    } else {
        ESP_LOGW(TAG, "Unable to lock status json buffer. ");
    }
}

void init_network_status() {
    chained_notify = server_notify;
    server_notify = connect_notify;
    ESP_LOGD(TAG, "init_network_sys_status->Getting status object");
    sys_status = sys_status_obj.get();
    network_status_set_static_info();
}

void set_lms_server_details(in_addr_t ip, u16_t hport, u16_t cport) {
    sys_net_server* LMS = &sys_status->LMS;
    if (sys_status_obj.Lock()) {
        sys_status->has_LMS = true;
        strncpy(LMS->ip, inet_ntoa(ip), sizeof(LMS->ip));
        LMS->port = hport;
        LMS->cport = cport;
        ESP_LOGI(TAG, "LMS IP: %s, hport: %d, cport: %d", LMS->ip, LMS->port, LMS->cport);
        sys_status_obj.Unlock();
    }
}

static void connect_notify(in_addr_t ip, u16_t hport, u16_t cport) {
    set_lms_server_details(ip, hport, cport);
    if (chained_notify) (*chained_notify)(ip, hport, cport);
    network_async_update_status();
}

void network_status_set_basic_info() {
    if (sys_status_obj.Lock()) {
        network_t* nm = network_get_state_machine();
        sys_status->hw.jack_inserted = jack_inserted_svc();
        sys_status->hw.spk_fault = spkfault_svc();
        sys_status->hw.batt_voltage = battery_value_svc();
        sys_status->net.wifi.disconnect_count = nm->num_disconnect;
        sys_status->net.wifi.avg_conn_time =
            nm->num_disconnect > 0 ? (nm->total_connected_time / nm->num_disconnect) : 0;

#ifdef CONFIG_BT_ENABLED
        if (platform->has_services && platform->services.has_bt_sink &&
            platform->services.bt_sink.enabled) {
            sys_status->has_bt = true;
            sys_status->bt.bt_status = bt_app_source_get_a2d_state();
            sys_status->bt.bt_media_state = bt_app_source_get_media_state();
        }
#endif

        if (network_ethernet_enabled()) {
            sys_status->net.eth_up = network_ethernet_is_up();
        }
        ESP_LOGV(TAG, "network_status_get_basic_info done");
        sys_status_obj.Unlock();
    } else {
        ESP_LOGW(TAG, "Unable to lock status json buffer. ");
    }
}
void network_status_update_address(esp_netif_ip_info_t* ip_info) {
    if (sys_status_obj.Lock()) {
        sys_status->has_net = true;
        sys_status->net.has_ip = true;
        strncpy(sys_status->net.ip.ip, ip4addr_ntoa((ip4_addr_t*)&ip_info->ip),
            sizeof(sys_status->net.ip.ip));
        strncpy(sys_status->net.ip.netmask, ip4addr_ntoa((ip4_addr_t*)&ip_info->netmask),
            sizeof(sys_status->net.ip.netmask));
        strncpy(sys_status->net.ip.gw, ip4addr_ntoa((ip4_addr_t*)&ip_info->gw),
            sizeof(sys_status->net.ip.gw));
        sys_status_obj.Unlock();
    }
}
void network_status_update_ip_info(sys_status_reasons update_reason_code) {
    ESP_LOGV(TAG, "network_status_update_ip_info called");
    esp_netif_ip_info_t ip_info;
    if (sys_status_obj.Lock()) {
        /* generate the connection info with success */
        network_status_set_basic_info();
        sys_status->net.updt_reason = update_reason_code;
        sys_status->net.wifi.has_connected_sta = false;
        sys_status->net.wifi.connected_sta.connected = false;

        ESP_LOGD(TAG,
            "Updating ip info with reason code %s. Checking if Wifi interface is connected",
            sys_status_reasons_name(update_reason_code));
        if (network_is_interface_connected(network_wifi_get_interface()) ||
            update_reason_code == sys_status_reasons_R_FAILED_ATTEMPT) {
            sys_status->net.interface = sys_status_interfaces_IF_WIFI;

            esp_netif_get_ip_info(network_wifi_get_interface(), &ip_info);
            network_status_update_address(&ip_info);
            if (!network_wifi_is_ap_mode()) {
                /* wifi is active, and associated to an AP */
                wifi_ap_record_t ap;
                esp_wifi_sta_get_ap_info(&ap);
                WifiList::ToSTAEntry(&ap, sys_status->net.wifi.connected_sta);
                sys_status->net.wifi.connected_sta.connected = true;
                sys_status->net.has_wifi = true;
                sys_status->net.wifi.has_connected_sta = true;
            }
        }
        ESP_LOGD(TAG, "Checking if ethernet interface is connected");
        if (network_is_interface_connected(network_ethernet_get_interface())) {
            sys_status->net.interface = sys_status_interfaces_IF_ETHERNET;
            esp_netif_get_ip_info(network_ethernet_get_interface(), &ip_info);
            network_status_update_address(&ip_info);
        }
        sys_status_obj.Unlock();
    } else {
        ESP_LOGW(TAG, "Unable to lock status json buffer. ");
    }
    ESP_LOGV(TAG, "wifi_status_generate_ip_info_json done");
}
bool network_status_send_object(httpd_req_t* req) {
    try {
        auto data = sys_status_obj.Encode();
        httpd_resp_send(req, (const char*)data.data(), data.size());
        return true;
    } catch (const std::runtime_error& e) {
        std::string errdesc = (std::string("Unable to get status: ") + e.what());
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, errdesc.c_str());
    }
    return false;
}