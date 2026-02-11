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

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "squeezelite-ota.h"
#include "esp_eth.h"
#include "freertos/event_groups.h"
#include "hsm.h"
#include "esp_log.h"
#include "network_services.h"

#ifdef __cplusplus
extern "C" {
#endif

//! List of oven events
#define ALL_NM_EVENTS \
    ADD_FIRST_EVENT(EN_LINK_UP) \
    ADD_EVENT(EN_LINK_DOWN)\
    ADD_EVENT(EN_CONFIGURE)\
    ADD_EVENT(EN_GOT_IP)\
    ADD_EVENT(EN_ETH_GOT_IP)\
    ADD_EVENT(EN_DELETE)\
    ADD_EVENT(EN_REMOVE)\
    ADD_EVENT(EN_TIMER)\
    ADD_EVENT(EN_START)\
    ADD_EVENT(EN_SCAN)\
    ADD_EVENT(EN_FAIL)\
    ADD_EVENT(EN_SUCCESS)\
    ADD_EVENT(EN_SCAN_DONE)\
    ADD_EVENT(EN_CONNECT)\
    ADD_EVENT(EN_CONNECT_NEW)\
    ADD_EVENT(EN_ADD)\
    ADD_EVENT(EN_REBOOT)\
    ADD_EVENT(EN_REBOOT_URL)\
    ADD_EVENT(EN_LOST_CONNECTION)\
    ADD_EVENT(EN_ETHERNET_FALLBACK)\
    ADD_EVENT(EN_UPDATE_STATUS)\
    ADD_EVENT(EN_CONNECTED)\
    ADD_EVENT(EN_COMMIT_CHANGES)\
    ADD_EVENT(EN_EXECUTE_CALLBACK)
#define ADD_EVENT(name) name,
#define ADD_FIRST_EVENT(name) name=1,
typedef enum {
	ALL_NM_EVENTS
} network_event_t;
#undef ADD_EVENT
#undef ADD_FIRST_EVENT

typedef enum {
    OTA,
    RECOVERY,
    RESTART,
} reboot_type_t;

typedef void (*network_manager_cb_t)(void * ctx);
typedef esp_err_t (*network_manager_ret_cb_t)(void * ctx);

typedef struct {
    void * ctx;
    network_manager_cb_t cb;
    network_manager_ret_cb_t ret_cb;
} callback_ctx_t;
typedef struct {
    char * ssid;
    char * password;
} network_credentials_t;
typedef struct {
    network_event_t trigger;

    union 
    {
        reboot_type_t rtype;
        char* strval;
        wifi_event_sta_disconnected_t* disconnected_event;
        callback_ctx_t cb_ctx;
        network_credentials_t credentials;
        ip_event_got_ip_t* got_ip_event_data;
    } ctx;
    
    esp_netif_t *netif;
    
} queue_message;

typedef struct
{
    state_machine_t Machine;  //!< Abstract state machine
	const state_t*  source_state;
    bool ethernet_connected;
    TimerHandle_t state_timer;
    uint32_t STA_duration;
	int32_t total_connected_time;
	int64_t last_connected;
	uint16_t num_disconnect;
	uint16_t retries;
    uint16_t initial_retries;
    bool wifi_connected;
	esp_netif_t *wifi_netif;
	esp_netif_t *eth_netif;
	esp_netif_t *wifi_ap_netif;
    uint16_t sta_polling_min_ms;
    uint16_t sta_polling_max_ms;
    uint16_t ap_duration_ms;
    uint16_t eth_link_down_reboot_ms;
    uint16_t dhcp_timeout;
    uint16_t wifi_dhcp_fail_ms;    
	queue_message * event_parameters;
    char * timer_tag;
} network_t;


/*
 *  --------------------- External function prototype ---------------------
 */

void network_initialize_task();


network_t * network_get_state_machine();
void network_event_simple(network_event_t trigger);
void network_event(network_event_t trigger, void* param);
void network_async_event(network_event_t trigger, void* param);
void network_async(network_event_t trigger);
void network_async_front(network_event_t trigger);
void network_async_fail();
void network_async_success();
void network_async_link_up();
void network_async_link_down();
void network_async_configure();
void network_async_got_ip(network_event_t trigger,ip_event_got_ip_t*event_data);
void network_async_timer();
void network_async_add(const char* ssid, const char* password);
void network_async_commit_protowrapper(void* protoWrapperBase);
void network_async_scan();
void network_async_scan_done();
void network_async_connect(const char * ssid, const char * password);
void network_async_delete_connection(const char * ssid);
void network_async_lost_connection(wifi_event_sta_disconnected_t * disconnected_event);
void network_async_reboot(reboot_type_t rtype);
void network_reboot_ota(char* url);
void network_async_delete();
void network_async_update_status();
void network_async_eth_got_ip();
void network_ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
bool network_is_interface_connected(esp_netif_t * interface);
void network_check_recovery_running();
void network_async_callback(void * ctx, network_manager_cb_t cb);
void network_async_callback_withret(void * ctx, network_manager_ret_cb_t cb);
const char* network_event_to_string(network_event_t state);
void network_initialize_state_machine_globals();

/** @brief Defines the auth mode as an access point
 *  Value must be of type wifi_auth_mode_t
 *  @see esp_wifi_types.h
 *  @warning if set to WIFI_AUTH_OPEN, passowrd me be empty. See DEFAULT_AP_PASSWORD.
 */
#define AP_AUTHMODE 						WIFI_AUTH_WPA2_PSK

/** @brief Defines visibility of the access point. 0: visible AP. 1: hidden */
#define DEFAULT_AP_SSID_HIDDEN 				0

/** @brief Defines access point's name. Default value: esp32. Run 'make menuconfig' to setup your own value or replace here by a string */
#define DEFAULT_AP_SSID 					CONFIG_DEFAULT_AP_SSID



/** @brief Defines access point's bandwidth.
 *  Value: WIFI_BW_HT20 for 20 MHz  or  WIFI_BW_HT40 for 40 MHz
 *  20 MHz minimize channel interference but is not suitable for
 *  applications with high data speeds
 */
#define DEFAULT_AP_BANDWIDTH 					WIFI_BW_HT20


#define DEFAULT_AP_CHANNEL 					CONFIG_DEFAULT_AP_CHANNEL


#define DEFAULT_AP_GATEWAY					CONFIG_DEFAULT_AP_GATEWAY

#define DEFAULT_AP_NETMASK					CONFIG_DEFAULT_AP_NETMASK


void network_reboot_ota(char * url);


/**
 * @brief simplified reason codes for a lost connection.
 *
 * esp-idf maintains a big list of reason codes which in practice are useless for most typical application.
 * UPDATE_CONNECTION_OK  - Web UI expects this when attempting to connect to a new access point succeeds
 * UPDATE_FAILED_ATTEMPT - Web UI expects this when attempting to connect to a new access point fails
 * UPDATE_USER_DISCONNECT = 2,
 * UPDATE_LOST_CONNECTION = 3,
 * UPDATE_FAILED_ATTEMPT_AND_RESTORE - Web UI expects this when attempting to connect to a new access point fails and previous connection is restored
 * UPDATE_ETHERNET_CONNECTED = 5
 */






/**
 * Frees up all memory allocated by the wifi_manager and kill the task.
 */
void network_destroy();

/**
 * Filters the AP scan list to unique SSIDs
 */
void  filter_unique( wifi_ap_record_t * aplist, uint16_t * ap_num);



/**
 * @brief Start the mDNS service
 */
void network_manager_initialise_mdns();



/**
 * @brief Register a callback to a custom function when specific network manager states are reached.
 */
bool network_is_wifi_prioritized();
void network_set_timer(uint16_t duration, const char * tag);
void network_set_hostname(esp_netif_t * netif);
esp_err_t network_get_ip_info_for_netif(esp_netif_t* netif, tcpip_adapter_ip_info_t* ipInfo);
void network_start_stop_dhcp_client(esp_netif_t* netif, bool start);
void network_start_stop_dhcps(esp_netif_t* netif, bool start);
void network_prioritize_wifi(bool activate);
#define ADD_ROOT_FORWARD_DECLARATION(name, ...) ADD_STATE_FORWARD_DECLARATION_(name)
#define ADD_ROOT_LEAF_FORWARD_DECLARATION(name, ...) ADD_STATE_FORWARD_DECLARATION_(name)
#define ADD_LEAF_FORWARD_DECLARATION(name, ...) ADD_STATE_FORWARD_DECLARATION_(name)
#define ADD_STATE_FORWARD_DECLARATION_(name)                                                              \
    static state_machine_result_t name##_handler(state_machine_t* const State_Machine);       \
    static state_machine_result_t name##_entry_handler(state_machine_t* const State_Machine); \
    static state_machine_result_t name##_exit_handler(state_machine_t* const State_Machine);

void initialize_network_handlers(state_machine_t* state_machine);
void network_manager_format_from_to_states(esp_log_level_t level, const char* prefix, state_t const *  from_state, state_t const* current_state,  network_event_t event,bool show_source, const char * caller );
void network_manager_format_state_machine(esp_log_level_t level, const char* prefix, state_machine_t * state_machine, bool show_source, const char * caller) ;

#if defined(LOG_LOCAL_LEVEL) 
#if LOG_LOCAL_LEVEL >=5
#define NETWORK_PRINT_TRANSITION(begin, prefix, source,target, event, print_source,caller ) network_manager_format_from_to_states(ESP_LOG_VERBOSE, prefix, source,target, event, print_source,caller )
#define NETWORK_DEBUG_STATE_MACHINE(begin, cb_prefix,state_machine,print_from,caller) network_manager_format_state_machine(ESP_LOG_DEBUG,cb_prefix,state_machine,print_from,caller)
#define NETWORK_EXECUTE_CB(mch) network_execute_cb(mch,__FUNCTION__);
#define network_handler_entry_print(State_Machine, begin) network_manager_format_state_machine(ESP_LOG_DEBUG,begin?"ENTRY START":"ENTRY END",State_Machine,false,__FUNCTION__)
#define network_exit_handler_print(State_Machine, begin) network_manager_format_state_machine(ESP_LOG_DEBUG,begin?"EXIT START":"END END",State_Machine,false,__FUNCTION__)
#define network_handler_print(State_Machine, begin) network_manager_format_state_machine(ESP_LOG_DEBUG,begin?"HANDLER START":"HANDLER END",State_Machine,false,__FUNCTION__)

#elif LOG_LOCAL_LEVEL >= 4
#define network_handler_entry_print(State_Machine, begin) if(begin) network_manager_format_state_machine(ESP_LOG_DEBUG,begin?"BEGIN ENTRY":"END ENTRY",State_Machine,false,"")
#define network_exit_handler_print(State_Machine, begin) if(begin) network_manager_format_state_machine(ESP_LOG_DEBUG,begin?"BEGIN EXIT":"END EXIT",State_Machine,false,"")
#define network_handler_print(State_Machine, begin) if(begin) network_manager_format_state_machine(ESP_LOG_DEBUG,begin?"HANDLER START":"HANDLER END",State_Machine,false,"")

#define NETWORK_PRINT_TRANSITION(begin, prefix, source,target, event, print_source,caller ) if(begin) network_manager_format_from_to_states(ESP_LOG_DEBUG, prefix, source,target, event, print_source,caller )
#define NETWORK_EXECUTE_CB(mch) network_execute_cb(mch,__FUNCTION__);
#define NETWORK_DEBUG_STATE_MACHINE(begin, cb_prefix,state_machine,print_from,caller) if(begin) network_manager_format_state_machine(ESP_LOG_DEBUG,cb_prefix,state_machine,print_from,caller)
#endif
#endif 

#ifndef NETWORK_PRINT_TRANSITION
#define network_exit_handler_print(nm, begin)
#define network_handler_entry_print(State_Machine, begin) 
#define network_handler_print(State_Machine, begin)
#define NETWORK_EXECUTE_CB(mch) network_execute_cb(mch,NULL)
#define NETWORK_PRINT_TRANSITION(begin, prefix, source,target, event, print_source,caller ) 
#define NETWORK_DEBUG_STATE_MACHINE(begin, cb_prefix,state_machine,print_from,caller)
#endif

#ifdef __cplusplus
}
#endif

