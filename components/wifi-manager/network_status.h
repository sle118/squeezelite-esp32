#pragma once
#include "network_manager.h"
#include "cJSON.h"
#include "Configurator.h"
#ifdef __cplusplus
extern "C" {

#endif
extern sys_Status status;
char* network_status_alloc_get_ip_info_json();
/**
 * @brief Tries to get access to json buffer mutex.
 *
 * The HTTP server can try to access the json to serve clients while the wifi manager thread can try
 * to update it. These two tasks are synchronized through a mutex.
 *
 * The mutex is used by both the access point list json and the connection status json.\n
 * These two resources should technically have their own mutex but we lose some flexibility to save
 * on memory.
 *
 * This is a simple wrapper around freeRTOS function xSemaphoreTake.
 *
 * @param xTicksToWait The time in ticks to wait for the semaphore to become available.
 * @return true in success, false otherwise.
 */
bool network_status_lock_structure(TickType_t xTicksToWait);

/**
 * @brief Releases the json buffer mutex.
 */
void network_status_unlock_structure();

/**
 * @brief Generates the connection status json: ssid and IP addresses.
 * @note This is not thread-safe and should be called only if network_status_lock_json_buffer call is successful.
 */
void network_status_update_ip_info(sys_UPDATE_REASONS update_reason_code);

void init_network_status();
void destroy_network_status();

void network_status_clear_ip();
void network_status_safe_reset_sta_ip();
#ifdef __cplusplus
}
#endif