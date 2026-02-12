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
#include "network_manager.h"
#include "Config.h"
#ifdef __cplusplus
extern "C" {

#endif
esp_netif_t* network_wifi_start();
void destroy_network_wifi();
esp_err_t network_wifi_set_connected(bool connected);

/**
 * @brief Registers handler for wifi and ip events
 */
void network_wifi_register_handlers();

/**
 * @brief Clear the list of access points.
 * @note This is not thread-safe and should be called only if network_status_lock_structure call is successful.
 */
void network_wifi_clear_access_points_json();

esp_netif_t* network_wifi_config_ap();

void network_wifi_filter_unique(wifi_ap_record_t* aplist, uint16_t* aps);
esp_err_t wifi_scan_done();
esp_err_t network_wifi_start_scan();
esp_err_t network_wifi_load_restore(queue_message* msg);
esp_err_t network_wifi_order_connect(queue_message* msg);
esp_err_t network_wifi_disconnected(queue_message* msg);
esp_err_t network_wifi_start_ap(queue_message* msg);
esp_err_t network_wifi_handle_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
bool is_wifi_up();
void network_wifi_get_status();
esp_err_t network_wifi_add_ssid(const char* ssid, const char* password);
void network_wifi_clear_config();
void network_wifi_get_status();
esp_netif_t* network_wifi_get_interface();
esp_netif_t* network_wifi_get_ap_interface();
bool network_wifi_is_ap_sta_mode();
bool network_wifi_is_sta_mode();
bool network_wifi_is_ap_mode();
bool network_wifi_sta_config_changed();
void network_wifi_global_init();
bool network_wifi_is_known_ap(const char* ssid);
esp_err_t network_wifi_connect(const char* ssid, const char* password);
esp_err_t network_wifi_connect_ssid(const char* ssid);
esp_err_t network_wifi_connect_active_ssid();
esp_err_t network_wifi_remove_ssid(const char* ssid);
esp_err_t network_wifi_set_sta_mode();
size_t network_wifi_get_known_count();
void network_wifi_activate_strongest_ssid();
sys_net_auth_types network_wifi_get_auth_type(const wifi_auth_mode_t mode);
#ifdef __cplusplus
}
#endif