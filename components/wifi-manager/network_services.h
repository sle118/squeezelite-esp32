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

#include "esp_netif.h"
#include "tcpip_adapter_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADD_ROOT(name, ...) name,
#define ADD_ROOT_LEAF(name, ...) name,
#define ADD_LEAF(name, ...) name,

#define ALL_NM_STATE                                                                                                                                 \
    ADD_ROOT_LEAF(NETWORK_INSTANTIATED_STATE)                                                                                                        \
    ADD_ROOT_LEAF(NETWORK_INITIALIZING_STATE)                                                                                                        \
    ADD_ROOT(NETWORK_ETH_ACTIVE_STATE, Eth_Active_State)                                                                                             \
    ADD_ROOT(NETWORK_WIFI_ACTIVE_STATE, Wifi_Active_State)                                                                                           \
    ADD_ROOT(NETWORK_WIFI_CONFIGURING_ACTIVE_STATE, Wifi_Configuring_State)

#define ALL_ETH_STATE(PARENT, LEVEL)                                                                                                                 \
    ADD_LEAF(ETH_STARTING_STATE, PARENT, LEVEL)                                                                                                      \
    ADD_LEAF(ETH_ACTIVE_LINKUP_STATE, PARENT, LEVEL)                                                                                                 \
    ADD_LEAF(ETH_ACTIVE_LINKDOWN_STATE, PARENT, LEVEL)                                                                                               \
    ADD_LEAF(ETH_ACTIVE_CONNECTED_STATE, PARENT, LEVEL)                                                                                              \
    ADD_LEAF(ETH_CONNECTING_NEW_STATE, PARENT, LEVEL)

#define ALL_WIFI_STATE(PARENT, LEVEL)                                                                                                                \
    ADD_LEAF(WIFI_INITIALIZING_STATE, PARENT, LEVEL)                                                                                                 \
    ADD_LEAF(WIFI_CONNECTING_STATE, PARENT, LEVEL)                                                                                                   \
    ADD_LEAF(WIFI_CONNECTING_SCAN_STATE, PARENT, LEVEL)                                                                                              \
    ADD_LEAF(WIFI_CONNECTING_NEW_STATE, PARENT, LEVEL)                                                                                               \
    ADD_LEAF(WIFI_CONNECTING_NEW_FAILED_STATE, PARENT, LEVEL)                                                                                        \
    ADD_LEAF(WIFI_CONNECTED_STATE, PARENT, LEVEL)                                                                                                    \
    ADD_LEAF(WIFI_USER_DISCONNECTED_STATE, PARENT, LEVEL)                                                                                            \
    ADD_LEAF(WIFI_LOST_CONNECTION_STATE, PARENT, LEVEL)

#define ALL_WIFI_CONFIGURING_STATE(PARENT, LEVEL)                                                                                                    \
    ADD_LEAF(WIFI_CONFIGURING_STATE, PARENT, LEVEL)                                                                                                  \
    ADD_LEAF(WIFI_CONFIGURING_CONNECT_STATE, PARENT, LEVEL)                                                                                          \
    ADD_LEAF(WIFI_CONFIGURING_CONNECT_SUCCESS_STATE, PARENT, LEVEL)

typedef enum { ALL_NM_STATE TOTAL_NM_STATE } nm_state_t;
typedef enum { ALL_WIFI_STATE(, ) TOTAL_WIFI_ACTIVE_STATE } mn_wifi_active_state_t;
typedef enum { ALL_ETH_STATE(, ) TOTAL_ETH_ACTIVE_STATE } mn_eth_active_state_t;
typedef enum { ALL_WIFI_CONFIGURING_STATE(, ) TOTAL_WIFI_CONFIGURING_STATE } mn_wifi_configuring_state_t;

#undef ADD_STATE
#undef ADD_ROOT
#undef ADD_ROOT_LEAF
#undef ADD_LEAF

typedef void (*network_status_reached_cb)(nm_state_t state_id, int sub_state);

esp_err_t network_register_state_callback(nm_state_t state, int sub_state, const char* from, network_status_reached_cb cb);
esp_netif_t* network_get_active_interface();
esp_err_t network_get_hostname(const char** hostname);
esp_err_t network_get_ip_info(tcpip_adapter_ip_info_t* ipInfo);

#ifdef __cplusplus
}
#endif
