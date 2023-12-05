#pragma once
#include "network_manager.h"
#include "Configurator.h"
#ifdef __cplusplus
extern "C" {

#endif
esp_netif_t * network_wifi_start();
void destroy_network_wifi(); 

/**
 * @brief Registers handler for wifi and ip events
 */
void network_wifi_register_handlers();


/**
 * @brief Clear the list of access points.
 * @note This is not thread-safe and should be called only if network_status_lock_structure call is successful.
 */
void network_wifi_clear_access_points_json();


esp_netif_t *  network_wifi_config_ap();

void network_wifi_filter_unique( wifi_ap_record_t * aplist, uint16_t * aps);
esp_err_t wifi_scan_done();
esp_err_t network_wifi_start_scan();
esp_err_t network_wifi_load_restore(queue_message *msg);
esp_err_t network_wifi_order_connect(queue_message *msg);
esp_err_t network_wifi_disconnected(queue_message *msg);
esp_err_t network_wifi_start_ap(queue_message *msg);
esp_err_t network_wifi_handle_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
bool is_wifi_up();
wifi_config_t* network_wifi_set_wifi_sta_config(const char * ssid, const char * password) ;
void network_wifi_clear_config();
esp_netif_t *network_wifi_get_interface();
esp_netif_t *network_wifi_get_ap_interface();
bool network_wifi_is_ap_sta_mode();
bool network_wifi_is_sta_mode();
bool network_wifi_is_ap_mode();
bool network_wifi_sta_config_changed();
void network_wifi_global_init();
bool network_wifi_is_known_ap(const char * ssid);
esp_err_t network_wifi_connect(const char * ssid, const char * password);
esp_err_t network_wifi_erase_legacy();
esp_err_t network_wifi_connect_ssid(const char * ssid);
esp_err_t network_wifi_connect_active_ssid();
esp_err_t network_wifi_erase_known_ap();
esp_err_t network_wifi_set_sta_mode();
size_t network_wifi_get_known_count();
size_t network_wifi_get_known_count_in_range();
esp_err_t network_wifi_connect_next_in_range();
void network_wifi_esp_sta_to_sta(wifi_ap_record_t* scan_res, sys_WifiSTAEntry* sta_entry);
sys_WifiAuthTypeEnum network_wifi_get_auth_type(const wifi_auth_mode_t mode) ;
#ifdef __cplusplus
}
#endif