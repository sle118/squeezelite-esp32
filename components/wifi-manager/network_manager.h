#pragma once

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "squeezelite-ota.h"
#include "cJSON.h"
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
    ADD_EVENT(EN_TIMER)\
    ADD_EVENT(EN_START)\
    ADD_EVENT(EN_SCAN)\
    ADD_EVENT(EN_FAIL)\
    ADD_EVENT(EN_SUCCESS)\
    ADD_EVENT(EN_SCAN_DONE)\
    ADD_EVENT(EN_CONNECT)\
    ADD_EVENT(EN_CONNECT_NEW)\
    ADD_EVENT(EN_REBOOT)\
    ADD_EVENT(EN_REBOOT_URL)\
    ADD_EVENT(EN_LOST_CONNECTION)\
    ADD_EVENT(EN_ETHERNET_FALLBACK)\
    ADD_EVENT(EN_UPDATE_STATUS)\
    ADD_EVENT(EN_CONNECTED)
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

typedef struct {
    network_event_t trigger;
    char * ssid;
    char * password;
    reboot_type_t rtype;
    char* strval;
    wifi_event_sta_disconnected_t* disconnected_event;
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

void network_start();
network_t * network_get_state_machine();
void network_event_simple(network_event_t trigger);
void network_event(network_event_t trigger, void* param);
void network_async_event(network_event_t trigger, void* param);
void network_async(network_event_t trigger);
void network_async_fail();
void network_async_success();
void network_async_link_up();
void network_async_link_down();
void network_async_configure();
void network_async_got_ip();
void network_async_timer();
void network_async_start();
void network_async_scan();
void network_async_scan_done();
void network_async_connect(const char * ssid, const char * password);
void network_async_lost_connection(wifi_event_sta_disconnected_t * disconnected_event);
void network_async_reboot(reboot_type_t rtype);
void network_reboot_ota(char* url);
void network_async_delete();
void network_async_update_status();
void network_async_eth_got_ip();
void network_ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
bool network_is_interface_connected(esp_netif_t * interface);






/*
 *  --------------------- Inline functions ---------------------
 */






/**
 * @brief Defines the maximum size of a SSID name. 32 is IEEE standard.
 * @warning limit is also hard coded in wifi_config_t. Never extend this value.
 */
#define MAX_SSID_SIZE						32

/**
 * @brief Defines the maximum size of a WPA2 passkey. 64 is IEEE standard.
 * @warning limit is also hard coded in wifi_config_t. Never extend this value.
 */
#define MAX_PASSWORD_SIZE					64
#define MAX_COMMAND_LINE_SIZE				201

/**
 * @brief Defines the maximum number of access points that can be scanned.
 *
 * To save memory and avoid nasty out of memory errors,
 * we can limit the number of APs detected in a wifi scan.
 */
#define MAX_AP_NUM 							15



/**
 * @brief Defines when a connection is lost/attempt to connect is made, how many retries should be made before giving up.
 * Setting it to 2 for instance means there will be 3 attempts in total (original request + 2 retries)
 */
#define	WIFI_MANAGER_MAX_RETRY				CONFIG_WIFI_MANAGER_MAX_RETRY

/** @brief Defines the task priority of the wifi_manager.
 *
 * Tasks spawn by the manager will have a priority of WIFI_MANAGER_TASK_PRIORITY-1.
 * For this particular reason, minimum task priority is 1. It it highly not recommended to set
 * it to 1 though as the sub-tasks will now have a priority of 0 which is the priority
 * of freeRTOS' idle task.
 */
#define WIFI_MANAGER_TASK_PRIORITY			CONFIG_WIFI_MANAGER_TASK_PRIORITY

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

/** @brief Defines access point's password.
 *	@warning In the case of an open access point, the password must be a null string "" or "\0" if you want to be verbose but waste one byte.
 *	In addition, the AP_AUTHMODE must be WIFI_AUTH_OPEN
 */
#define DEFAULT_AP_PASSWORD 				CONFIG_DEFAULT_AP_PASSWORD


/** @brief Defines access point's bandwidth.
 *  Value: WIFI_BW_HT20 for 20 MHz  or  WIFI_BW_HT40 for 40 MHz
 *  20 MHz minimize channel interference but is not suitable for
 *  applications with high data speeds
 */
#define DEFAULT_AP_BANDWIDTH 					WIFI_BW_HT20

/** @brief Defines access point's channel.
 *  Channel selection is only effective when not connected to another AP.
 *  Good practice for minimal channel interference to use
 *  For 20 MHz: 1, 6 or 11 in USA and 1, 5, 9 or 13 in most parts of the world
 *  For 40 MHz: 3 in USA and 3 or 11 in most parts of the world
 */
#define DEFAULT_AP_CHANNEL 					CONFIG_DEFAULT_AP_CHANNEL



/** @brief Defines the access point's default IP address. Default: "10.10.0.1 */
#define DEFAULT_AP_IP						CONFIG_DEFAULT_AP_IP

/** @brief Defines the access point's gateway. This should be the same as your IP. Default: "10.10.0.1" */
#define DEFAULT_AP_GATEWAY					CONFIG_DEFAULT_AP_GATEWAY

/** @brief Defines the access point's netmask. Default: "255.255.255.0" */
#define DEFAULT_AP_NETMASK					CONFIG_DEFAULT_AP_NETMASK

/** @brief Defines access point's maximum number of clients. Default: 4 */
#define DEFAULT_AP_MAX_CONNECTIONS		 	CONFIG_DEFAULT_AP_MAX_CONNECTIONS

/** @brief Defines access point's beacon interval. 100ms is the recommended default. */
#define DEFAULT_AP_BEACON_INTERVAL 			CONFIG_DEFAULT_AP_BEACON_INTERVAL

/** @brief Defines if esp32 shall run both AP + STA when connected to another AP.
 *  Value: 0 will have the own AP always on (APSTA mode)
 *  Value: 1 will turn off own AP when connected to another AP (STA only mode when connected)
 *  Turning off own AP when connected to another AP minimize channel interference and increase throughput
 */
#define DEFAULT_STA_ONLY 					1

/** @brief Defines the default wifi power save mode.
 *  Value: WIFI_PS_NONE (0) for full power (wifi modem always on)
 *  Value: WIFI_PS_MIN_MODEM (1) for minimum power save (wifi modem sleeps periodically)
 *  Value: WIFI_PS_MAX_MODEM (2) for maximum power save
 *  Note: Power save is only effective when in STA only mode
 *  Can be overridden via NVS key "wifi_ps" (0, 1, or 2)
 */
#define DEFAULT_STA_POWER_SAVE 				WIFI_PS_MIN_MODEM


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
typedef enum update_reason_code_t {
	UPDATE_CONNECTION_OK = 0, // expected when 
	UPDATE_FAILED_ATTEMPT = 1,
	UPDATE_USER_DISCONNECT = 2,
	UPDATE_LOST_CONNECTION = 3,
	UPDATE_FAILED_ATTEMPT_AND_RESTORE = 4,
	UPDATE_ETHERNET_CONNECTED = 5

}update_reason_code_t;








/**
 * Frees up all memory allocated by the wifi_manager and kill the task.
 */
void network_destroy();

/**
 * Filters the AP scan list to unique SSIDs
 */
void  filter_unique( wifi_ap_record_t * aplist, uint16_t * ap_num);


char* network_status_alloc_get_ap_list_json();
cJSON * network_manager_clear_ap_list_json(cJSON **old);



/**
 * @brief A standard wifi event handler as recommended by Espressif
 */
esp_err_t network_manager_event_handler(void *ctx, system_event_t *event);



/**
 * @brief Clears the connection status json.
 * @note This is not thread-safe and should be called only if network_status_lock_json_buffer call is successful.
 */
cJSON * network_status_clear_ip_info_json(cJSON **old);
cJSON * network_status_get_new_json(cJSON **old);



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
char* network_manager_alloc_get_mac_string(uint8_t mac[6]);

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

#define NETWORK_PRINT_TRANSITION(begin, prefix, source,target, event, print_source,caller ) if(begin) network_manager_format_from_to_states(ESP_LOG_DEBUG, prefix, source,target, event, print_source,caller )#define NETWORK_EXECUTE_CB(mch) network_execute_cb(mch,__FUNCTION__);
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


