#pragma once
#include "network_manager.h"
#ifdef __cplusplus
extern "C" {

#endif
esp_netif_t * network_wifi_start();
void destroy_network_wifi(); 
/**
 * @brief saves the current STA wifi config to flash ram storage.
 */
esp_err_t network_wifi_save_sta_config();


/**
 * @brief fetch a previously STA wifi config in the flash ram storage.
 * @return true if a previously saved config was found, false otherwise.
 */
bool network_wifi_load_wifi_sta_config();


/**
 * @brief Registers handler for wifi and ip events
 */
void network_wifi_register_handlers();

/**
 * @brief Generates the list of access points after a wifi scan.
 * @note This is not thread-safe and should be called only if network_status_lock_json_buffer call is successful.
 */
void network_wifi_generate_access_points_json(cJSON ** ap_list);

/**
 * @brief Clear the list of access points.
 * @note This is not thread-safe and should be called only if network_status_lock_json_buffer call is successful.
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
bool network_wifi_get_config_for_ssid(wifi_config_t* config, const char * ssid);
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
esp_err_t network_wifi_built_known_ap_list();
esp_err_t network_wifi_connect_next_in_range();
esp_err_t network_wifi_mark_ap_failed(const char *ssid);
esp_err_t network_wifi_mark_ap_success(const char *ssid);
esp_err_t network_wifi_delete_ap(const char *key);
void network_wifi_set_found_ap(void);
size_t network_wifi_get_known_count_in_range(void);
const char * network_wifi_get_next_ap_in_range(void);
const wifi_sta_config_t* network_wifi_load_active_config();
#ifdef __cplusplus
}
#endif