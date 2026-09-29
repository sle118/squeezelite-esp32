#ifdef NETWORK_HANDLERS_LOG_LEVEL
#define LOG_LOCAL_LEVEL NETWORK_HANDLERS_LOG_LEVEL
#endif
#include "network_manager.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network_ethernet.h"
#include "network_status.h"
#include "network_wifi.h"

#include "dns_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "platform_esp32.h"

#include "esp_netif.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "cmd_system.h"
#include "esp_app_format.h"

#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/netdb.h"
#include "mdns.h"
#include "messaging.h"

#include "platform_config.h"
#include "trace.h"

#include "accessors.h"
#include "esp_err.h"
#include "tools.h"
#include "http_server_handlers.h"
#include "network_manager.h"

static const char TAG[]="network_handlers";

EXT_RAM_ATTR static state_t network_states[TOTAL_NM_STATE];
EXT_RAM_ATTR static state_t Wifi_Active_State[TOTAL_WIFI_ACTIVE_STATE];
EXT_RAM_ATTR static state_t Eth_Active_State[TOTAL_ETH_ACTIVE_STATE];
EXT_RAM_ATTR static state_t Wifi_Configuring_State[TOTAL_WIFI_CONFIGURING_STATE];

static void network_interface_coexistence(state_machine_t* state_machine);
static state_machine_result_t local_traverse_state(state_machine_t* const state_machine,
                                                   const state_t* const target_state, const char * caller) ;
static state_machine_result_t local_switch_state(state_machine_t* state_machine,
                                                 const state_t* const target_state, const char * caller);
void network_execute_cb(state_machine_t* const state_machine, const char * caller);
int get_root_id(const state_t *  state);
const state_t* get_root( const state_t* const state);

#define ADD_ROOT(name,...) ADD_ROOT_FORWARD_DECLARATION(name,...)
#define ADD_ROOT_LEAF(name,...) ADD_ROOT_LEAF_FORWARD_DECLARATION(name,...)
#define ADD_LEAF(name,...) ADD_LEAF_FORWARD_DECLARATION(name,...)
ALL_NM_STATE
ALL_ETH_STATE(, )
ALL_WIFI_STATE(, )
ALL_WIFI_CONFIGURING_STATE(, )
#undef ADD_ROOT
#undef ADD_ROOT_LEAF
#undef ADD_LEAF

/*
 *  --------------------- Global variables ---------------------
 */
#define ADD_ROOT(NAME, CHILD) ADD_ROOT_(NAME, CHILD)
#define ADD_LEAF(NAME, PARENT, LEVEL) ADD_LEAF_(NAME, PARENT, LEVEL)
#define ADD_ROOT_LEAF(NAME) ADD_ROOT_LEAF_(NAME)

#define ADD_ROOT_(NAME, CHILD)         \
    [NAME] = {                         \
        .Handler = NAME##_handler,     \
        .Entry = NAME##_entry_handler, \
        .Exit = NAME##_exit_handler,   \
        .Level = 0,                    \
        .Parent = NULL,                \
        .Id = NAME,                    \
        .Node = CHILD,                 \
    },

#define ADD_ROOT_LEAF_(NAME)           \
    [NAME] = {                         \
        .Handler = NAME##_handler,     \
        .Entry = NAME##_entry_handler, \
        .Exit = NAME##_exit_handler,   \
        .Id = NAME,                    \
        .Parent = NULL,              \
    },

#define ADD_LEAF_(NAME, PARENT, LEVEL) \
    [NAME] = {                         \
        .Handler = NAME##_handler,     \
        .Entry = NAME##_entry_handler, \
        .Exit = NAME##_exit_handler,   \
        .Id = NAME,                    \
        .Level = LEVEL,                \
        .Parent = PARENT,              \
    },

static void network_initialize_state_machine_globals(){
    static state_t loc_network_states[] =
        {
            ALL_NM_STATE};

    static state_t loc_Wifi_Active_State[] = {
        ALL_WIFI_STATE(&network_states[NETWORK_WIFI_ACTIVE_STATE], 1)
    };
    static state_t loc_Eth_Active_State[]={
        ALL_ETH_STATE(&network_states[NETWORK_ETH_ACTIVE_STATE], 1)

    };
    static state_t loc_Wifi_Configuring_State[]={
        ALL_WIFI_CONFIGURING_STATE(&network_states[NETWORK_WIFI_CONFIGURING_ACTIVE_STATE], 1)
    };
    memcpy(&network_states,&loc_network_states,sizeof(network_states));
    memcpy(&Eth_Active_State,&loc_Eth_Active_State,sizeof(Eth_Active_State));
    memcpy(&Wifi_Active_State,&loc_Wifi_Active_State,sizeof(Wifi_Active_State));
    memcpy(&Wifi_Configuring_State,&loc_Wifi_Configuring_State,sizeof(Wifi_Configuring_State));
}
#undef ADD_ROOT
#undef ADD_ROOT_LEAF
#undef ADD_LEAF

#define HANDLE_GLOBAL_EVENT(m)                   \
    if (handle_global_event(m) == EVENT_HANDLED) \
        return EVENT_HANDLED;

static void network_connect_active_ssid(state_machine_t* const State_Machine) {
    network_t* const nm = (network_t*)State_Machine;
    if (network_wifi_connect_active_ssid() != ESP_OK) {
        ESP_LOGE(TAG, "Oups.  Something went wrong!");
        nm->wifi_connected = false;
        ESP_LOGD(TAG, "Checking if ethernet interface is connected");
        if (network_is_interface_connected(nm->eth_netif)) {
            ESP_LOGD(TAG, "Ethernet connection is found.  Try to fallback there");
            network_async(EN_ETHERNET_FALLBACK);
        } else {
            // returning to AP mode
            nm->STA_duration = nm->sta_polling_min_ms;
            ESP_LOGD(TAG, "No ethernet and no wifi configured. Go to configuration mode");
            network_async_configure();
        }
    }
}

void initialize_network_handlers(state_machine_t* state_machine){
    MEMTRACE_PRINT_DELTA();
    network_initialize_state_machine_globals();
    MEMTRACE_PRINT_DELTA();
    NETWORK_INSTANTIATED_STATE_handler(state_machine);
    MEMTRACE_PRINT_DELTA();
}
static state_machine_result_t handle_global_event(state_machine_t* state_machine) {
    network_t * net_sm= ((network_t *)state_machine);
    switch (net_sm->Machine.Event) {
        case EN_UPDATE_STATUS:
            // handle the event, but don't swicth
            MEMTRACE_PRINT_DELTA_MESSAGE("handle EN_UPDATE_STATUS - start");
            network_status_update_basic_info();
            MEMTRACE_PRINT_DELTA_MESSAGE("handle EN_UPDATE_STATUS - end");
            return EVENT_HANDLED;
            /* code */
            break;
        case EN_REBOOT:
            ESP_LOGD(TAG,"Called for reboot type %d",net_sm->event_parameters->rtype);
            switch (net_sm->event_parameters->rtype) {
                case OTA:
                    ESP_LOGD(TAG, " Calling guided_restart_ota.");
                    guided_restart_ota();
                    break;
                case RECOVERY:
                    ESP_LOGD(TAG, " Calling guided_factory.");
                    guided_factory();
                    break;
                case RESTART:
                    ESP_LOGD(TAG, " Calling simple_restart.");
                    simple_restart();
                    break;

                default:
                    break;
            }
            return EVENT_UN_HANDLED;
            break;
        case EN_REBOOT_URL:
            if (net_sm->event_parameters->strval) {
                start_ota(net_sm->event_parameters->strval, NULL, 0);
                FREE_AND_NULL(net_sm->event_parameters->strval);
            }
            return EVENT_UN_HANDLED;
            break;

        default:
            break;
    }
    return EVENT_UN_HANDLED;
}




/********************************************************************************************* 
 * INSTANTIATED_STATE
 */
static state_machine_result_t NETWORK_INSTANTIATED_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_handler_entry_print(State_Machine,true);
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t NETWORK_INSTANTIATED_STATE_handler(state_machine_t* const State_Machine) {
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_UN_HANDLED;
    network_t* const nm = (network_t *)State_Machine;
    State_Machine->State = &network_states[NETWORK_INSTANTIATED_STATE];
    State_Machine->Event = EN_START;
    config_get_uint16t_from_str("pollmx",&nm->sta_polling_max_ms,600);
    nm->sta_polling_max_ms = nm->sta_polling_max_ms * 1000;
    config_get_uint16t_from_str("apdelay",&nm->ap_duration_ms,20);
    nm->ap_duration_ms = nm->ap_duration_ms * 1000;
    config_get_uint16t_from_str("pollmin",&nm->sta_polling_min_ms,15);
    nm->sta_polling_min_ms = nm->sta_polling_min_ms*1000;
    config_get_uint16t_from_str("ethtmout",&nm->eth_link_down_reboot_ms,30);
    nm->eth_link_down_reboot_ms = nm->eth_link_down_reboot_ms*1000;
    config_get_uint16t_from_str("dhcp_tmout",&nm->dhcp_timeout,30);
    nm->dhcp_timeout = nm->dhcp_timeout*1000;
    ESP_LOGI(TAG,"Network manager configuration: polling max %d, polling min %d, ap delay %d, dhcp timeout %d, eth timeout %d",
        nm->sta_polling_max_ms,nm->sta_polling_min_ms,nm->ap_duration_ms,nm->dhcp_timeout, nm->eth_link_down_reboot_ms);
    HANDLE_GLOBAL_EVENT(State_Machine);
    if (State_Machine->Event == EN_START) {
        result= local_traverse_state(State_Machine, &network_states[NETWORK_INITIALIZING_STATE],__FUNCTION__);
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t NETWORK_INSTANTIATED_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}

/********************************************************************************************* 
 * INITIALIZING_STATE
 */
static state_machine_result_t NETWORK_INITIALIZING_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_handler_entry_print(State_Machine,true);    
    
    MEMTRACE_PRINT_DELTA_MESSAGE(" Initializing tcp_ip adapter");
    esp_netif_init();
    MEMTRACE_PRINT_DELTA_MESSAGE(" Creating the default event loop");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    MEMTRACE_PRINT_DELTA_MESSAGE("Initializing network status");
    init_network_status();
    MEMTRACE_PRINT_DELTA_MESSAGE("Loading wifi global configuration");
    network_wifi_global_init();
    MEMTRACE_PRINT_DELTA_MESSAGE(" Registering event handler");
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &network_ip_event_handler, NULL);
    MEMTRACE_PRINT_DELTA_MESSAGE(" Initializing network done. Starting http server");
    // send a message to start the connections
    http_server_start(); 
    MEMTRACE_PRINT_DELTA_MESSAGE("Executing Callback");
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}

static state_machine_result_t NETWORK_INITIALIZING_STATE_handler(state_machine_t* const State_Machine) {
     
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_UN_HANDLED;
    HANDLE_GLOBAL_EVENT(State_Machine);
    switch (State_Machine->Event) {
        case EN_START:
        if (network_is_wifi_prioritized()) {
                ESP_LOGI(TAG, "WiFi connection is prioritized. Starting WiFi");
                result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_INITIALIZING_STATE],__FUNCTION__);
            }
            else if(is_recovery_running){
                ESP_LOGI(TAG, "Running recovery. Skipping ethernet, starting WiFi");
                result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_INITIALIZING_STATE],__FUNCTION__);
            }
            else {
                result= local_traverse_state(State_Machine, &Eth_Active_State[ETH_STARTING_STATE],__FUNCTION__);
            }
            break;
        default:
            result= EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t NETWORK_INITIALIZING_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}

/********************************************************************************************* 
 * ETH_STARTING_STATE
 */
static state_machine_result_t ETH_STARTING_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_handler_entry_print(State_Machine,true);
    ESP_LOGD(TAG, "Looking for ethernet Interface");
    network_t* const nm = (network_t *)State_Machine;
    init_network_ethernet();
    if (!network_ethernet_enabled()) {
        network_async_fail();
    } else {
        nm->eth_netif = network_ethernet_get_interface();
    }
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}

static state_machine_result_t ETH_STARTING_STATE_handler(state_machine_t* const State_Machine) {
    state_machine_result_t result = EVENT_HANDLED;
    network_handler_print(State_Machine,true);
    HANDLE_GLOBAL_EVENT(State_Machine);
    switch (State_Machine->Event) {
        case EN_FAIL:
            result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_INITIALIZING_STATE],__FUNCTION__);
            break;
        case EN_SUCCESS:
            result= local_traverse_state(State_Machine, &network_states[NETWORK_ETH_ACTIVE_STATE],__FUNCTION__);
            break;
        default:
            ESP_LOGE(TAG, "No handler");
            result= EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t ETH_STARTING_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}

/********************************************************************************************* 
 * NETWORK_ETH_ACTIVE_STATE
 */
static state_machine_result_t NETWORK_ETH_ACTIVE_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_handler_entry_print(State_Machine,true);
    network_t* const nm = (network_t *)State_Machine;
    network_set_timer(nm->eth_link_down_reboot_ms,"Ethernet link not detected" );
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t NETWORK_ETH_ACTIVE_STATE_handler(state_machine_t* const State_Machine) {
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_UN_HANDLED;
    network_t* const nm = (network_t *)State_Machine;
    switch (State_Machine->Event) {
        case EN_CONNECT_NEW:
            result= local_traverse_state(State_Machine, &Eth_Active_State[ETH_CONNECTING_NEW_STATE],__FUNCTION__);
            break;
        case EN_LINK_UP:
            result= local_traverse_state(State_Machine, &Eth_Active_State[ETH_ACTIVE_LINKUP_STATE],__FUNCTION__);
            break;
        case EN_LINK_DOWN:
            result= local_traverse_state(State_Machine, &Eth_Active_State[ETH_ACTIVE_LINKDOWN_STATE],__FUNCTION__);
            break;
        case EN_ETH_GOT_IP:
            result= local_traverse_state(State_Machine, &Eth_Active_State[ETH_ACTIVE_CONNECTED_STATE],__FUNCTION__);
            break;
        case EN_ETHERNET_FALLBACK:
            result= local_traverse_state(State_Machine, &Eth_Active_State[ETH_ACTIVE_CONNECTED_STATE],__FUNCTION__);
            break;
        case EN_TIMER:
            ESP_LOGW(TAG, "Timeout %s. Rebooting to wifi",STR_OR_ALT(nm->timer_tag,"Ethernet link not detected"));
            network_prioritize_wifi(true);
            simple_restart();
            //result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_INITIALIZING_STATE],__FUNCTION__);
            break;
        case EN_SCAN:
            ESP_LOGW(TAG,"Wifi  scan cannot be executed in this state");
            network_wifi_built_known_ap_list();
            result = EVENT_HANDLED;
            break;
        case EN_DELETE: {
            ESP_LOGD(TAG, "WiFi disconnected by user");
            network_wifi_clear_config();
            network_status_update_ip_info(UPDATE_USER_DISCONNECT);
            result= EVENT_HANDLED;
        } break;
        default:
            HANDLE_GLOBAL_EVENT(State_Machine);
            result=EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t NETWORK_ETH_ACTIVE_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_set_timer(0,NULL);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}



/********************************************************************************************* 
 * ETH_CONNECTING_NEW_STATE
 */
static state_machine_result_t ETH_CONNECTING_NEW_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_t* const nm = (network_t *)State_Machine;
    network_handler_entry_print(State_Machine,true);
    network_start_stop_dhcp_client(nm->wifi_netif, true);
    network_wifi_connect(nm->event_parameters->ssid,nm->event_parameters->password);
    FREE_AND_NULL(nm->event_parameters->ssid);
    FREE_AND_NULL(nm->event_parameters->password);
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t ETH_CONNECTING_NEW_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_HANDLED;
    switch (State_Machine->Event) {
        case EN_GOT_IP:
            result= local_traverse_state(State_Machine, &network_states[WIFI_CONNECTED_STATE],__FUNCTION__);
            break;
        case EN_LOST_CONNECTION:
            network_status_update_ip_info(UPDATE_FAILED_ATTEMPT);
            messaging_post_message(MESSAGING_ERROR, MESSAGING_CLASS_SYSTEM, "Unable to connect to new WiFi access point.");
            // no existing configuration, or wifi wasn't the active connection when connection
            // attempt was made
            network_async(EN_ETHERNET_FALLBACK);
            result = EVENT_HANDLED;
            break;

        default:
            result= EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t ETH_CONNECTING_NEW_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_set_timer(0,NULL);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}

/********************************************************************************************* 
 * ETH_ACTIVE_LINKDOWN
 */
static state_machine_result_t ETH_ACTIVE_LINKDOWN_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_handler_entry_print(State_Machine,true);
    network_t* const nm = (network_t *)State_Machine;
    network_set_timer(nm->eth_link_down_reboot_ms, "Ethernet link down" );
    NETWORK_EXECUTE_CB(State_Machine);
    messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Ethernet link down.");
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t ETH_ACTIVE_LINKDOWN_STATE_handler(state_machine_t* const State_Machine) {
    network_handler_print(State_Machine,true);
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,false);
    return EVENT_UN_HANDLED;
}
static state_machine_result_t ETH_ACTIVE_LINKDOWN_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_set_timer(0,NULL);
    network_exit_handler_print(State_Machine,false);
    
    return EVENT_HANDLED;
}

/********************************************************************************************* 
 * ETH_ACTIVE_LINKUP_STATE
 */
static state_machine_result_t ETH_ACTIVE_LINKUP_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_handler_entry_print(State_Machine,true);
    network_t* const nm = (network_t *)State_Machine;
    network_set_timer(nm->dhcp_timeout, "DHCP timeout" );
    NETWORK_EXECUTE_CB(State_Machine);
    messaging_post_message(MESSAGING_INFO, MESSAGING_CLASS_SYSTEM, "Ethernet link up.");
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t ETH_ACTIVE_LINKUP_STATE_handler(state_machine_t* const State_Machine) {
    state_machine_result_t result = EVENT_UN_HANDLED;
    network_handler_print(State_Machine,true);
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t ETH_ACTIVE_LINKUP_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}

/********************************************************************************************* 
 * WIFI_UP_STATE
 */
static state_machine_result_t NETWORK_WIFI_ACTIVE_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_handler_entry_print(State_Machine,true);
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t NETWORK_WIFI_ACTIVE_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_UN_HANDLED;
    switch (State_Machine->Event)
    {
        case EN_LINK_UP:
            ESP_LOGW(TAG, "Ethernet link up in wifi mode");
            break;
        case EN_ETH_GOT_IP:
            network_interface_coexistence(State_Machine);
            break;
        case EN_GOT_IP:
            network_status_update_ip_info(UPDATE_CONNECTION_OK);
            result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_CONNECTED_STATE],__FUNCTION__);
            break;            
        case EN_SCAN:
            if (network_wifi_start_scan() == ESP_OK) {
                result= EVENT_HANDLED;
            }
            break;
        case EN_SCAN_DONE:
            if(wifi_scan_done() == ESP_OK) {
                result= EVENT_HANDLED;
            }
            break;
        case EN_CONNECT_NEW:
            result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_CONNECTING_NEW_STATE],__FUNCTION__);
            break;
        case EN_DELETE:
            result= local_traverse_state(State_Machine,&Wifi_Active_State[WIFI_USER_DISCONNECTED_STATE],__FUNCTION__);
            break;
        case EN_ETHERNET_FALLBACK:
            result= local_traverse_state(State_Machine, &Eth_Active_State[ETH_ACTIVE_CONNECTED_STATE],__FUNCTION__);
            break;            
        default:
            break;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t NETWORK_WIFI_ACTIVE_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}

/********************************************************************************************* 
 * WIFI_INITIALIZING_STATE
 */
static state_machine_result_t WIFI_INITIALIZING_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_t* const nm = (network_t *)State_Machine;
    network_handler_entry_print(State_Machine,true);
    if(!nm->wifi_netif){
        nm->wifi_netif = network_wifi_start();
    }
    if (!is_wifi_up()) {
        messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Wifi not started. Load Configuration");
        return EVENT_UN_HANDLED;
    }
    if (network_wifi_get_known_count()>0) {
        ESP_LOGI(TAG, "Existing wifi config found. Attempting to connect.");
        network_async_success();
    } else {
        /* no wifi saved: start soft AP! This is what should happen during a first run */
        ESP_LOGW(TAG, "No saved wifi. Starting AP configuration mode.");
        network_async_configure();
    }
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}

static state_machine_result_t WIFI_INITIALIZING_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_HANDLED;
    switch (State_Machine->Event) {
        case EN_CONFIGURE:
            result= local_traverse_state(State_Machine, &Wifi_Configuring_State[WIFI_CONFIGURING_STATE],__FUNCTION__);
            break;
        case EN_SUCCESS:
            result= local_switch_state(State_Machine, &Wifi_Active_State[WIFI_CONNECTING_STATE],__FUNCTION__);
            break;
        default:
            result= EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t WIFI_INITIALIZING_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}


/********************************************************************************************* 
 * WIFI_CONFIGURING_ACTIVE_STATE
 */
static state_machine_result_t NETWORK_WIFI_CONFIGURING_ACTIVE_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_handler_entry_print(State_Machine,true);
    network_t* const nm = (network_t *)State_Machine;
    nm->wifi_ap_netif = network_wifi_config_ap();
    dns_server_start(nm->wifi_ap_netif);
    network_wifi_start_scan();
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t NETWORK_WIFI_CONFIGURING_ACTIVE_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_HANDLED;
    switch (State_Machine->Event) {
        case EN_SCAN:
            if (network_wifi_start_scan() == ESP_OK) {
                result= EVENT_HANDLED;
            }
            break;
        case EN_SCAN_DONE:
            ESP_LOGD(TAG,"Network configuration active, wifi scan completed");
            if(wifi_scan_done() == ESP_OK) {
                result= EVENT_HANDLED;
            }
            break;   
        case EN_CONNECT_NEW:
            result= local_traverse_state(State_Machine, &Wifi_Configuring_State[WIFI_CONFIGURING_CONNECT_STATE],__FUNCTION__);
            break;
        case EN_LINK_UP:
            ESP_LOGW(TAG, "Ethernet link up in wifi mode");
            break;
        case EN_ETH_GOT_IP:
            network_interface_coexistence(State_Machine);
            break;
        default:
            result =EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t NETWORK_WIFI_CONFIGURING_ACTIVE_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine, true);
    /* bring down DNS hijack */
    ESP_LOGD(TAG, " Stopping DNS.");
    dns_server_stop();    
    ESP_LOGD(TAG, "Changing wifi mode to STA.");
    network_wifi_set_sta_mode();
    network_exit_handler_print(State_Machine, false);

    return EVENT_HANDLED;
}


/********************************************************************************************* 
 * WIFI_CONFIGURING_STATE
 */
static state_machine_result_t WIFI_CONFIGURING_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_handler_entry_print(State_Machine,true);
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t WIFI_CONFIGURING_STATE_handler(state_machine_t* const State_Machine) {
    network_handler_print(State_Machine,true);
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,false);
    return EVENT_UN_HANDLED;
}
static state_machine_result_t WIFI_CONFIGURING_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}

/********************************************************************************************* 
 * WIFI_CONFIGURING_CONNECT_STATE
 */
static state_machine_result_t WIFI_CONFIGURING_CONNECT_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_t* const nm = (network_t *)State_Machine;
    network_handler_entry_print(State_Machine,true);
    network_start_stop_dhcp_client(nm->wifi_netif, true);
    network_wifi_connect(nm->event_parameters->ssid,nm->event_parameters->password);
    FREE_AND_NULL(nm->event_parameters->ssid);
    FREE_AND_NULL(nm->event_parameters->password);
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t WIFI_CONFIGURING_CONNECT_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,true);
    network_t* const nm = (network_t *)State_Machine;
    state_machine_result_t result = EVENT_HANDLED;
    switch (State_Machine->Event) {
        case EN_CONNECTED:
            result=EVENT_HANDLED;
            ESP_LOGI(TAG,"Wifi was connected. Waiting for IP address");
            network_set_timer(nm->dhcp_timeout,"DHCP Timeout");
            break;
        case EN_GOT_IP:
            network_status_update_ip_info(UPDATE_CONNECTION_OK); 
            result= local_traverse_state(State_Machine, &Wifi_Configuring_State[WIFI_CONFIGURING_CONNECT_SUCCESS_STATE],__FUNCTION__);
            break;
        case EN_LOST_CONNECTION:
            if(nm->event_parameters->disconnected_event->reason == WIFI_REASON_ASSOC_LEAVE) {
                ESP_LOGI(TAG,"Wifi was disconnected from previous access point. Waiting to connect.");
            }
            else {
                network_status_update_ip_info(UPDATE_FAILED_ATTEMPT);
                result = local_traverse_state(State_Machine, &Wifi_Configuring_State[WIFI_CONFIGURING_STATE],__FUNCTION__);
            }
            break;
        case EN_TIMER:
            ESP_LOGW(TAG,"Connection timeout.  (%s)",STR_OR_ALT(nm->timer_tag, "Unknown"));
            network_status_update_ip_info(UPDATE_FAILED_ATTEMPT);
            result = local_traverse_state(State_Machine, &Wifi_Configuring_State[WIFI_CONFIGURING_STATE],__FUNCTION__);
            break;
        default:
            result= EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t WIFI_CONFIGURING_CONNECT_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);

    FREE_AND_NULL(((network_t *)State_Machine)->event_parameters->disconnected_event);
    network_set_timer(0,NULL);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}

/********************************************************************************************* 
 * WIFI_CONFIGURING_CONNECT_SUCCESS_STATE
 */
static state_machine_result_t WIFI_CONFIGURING_CONNECT_SUCCESS_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_handler_entry_print(State_Machine,true);
    network_status_update_ip_info(UPDATE_CONNECTION_OK);
    ESP_LOGD(TAG, "Saving wifi configuration.");
    network_wifi_save_sta_config();
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t WIFI_CONFIGURING_CONNECT_SUCCESS_STATE_handler(state_machine_t* const State_Machine) {
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_HANDLED;
    network_t* const nm = (network_t *)State_Machine;
    switch (State_Machine->Event) {
         case EN_UPDATE_STATUS:
            network_status_update_basic_info();
            network_set_timer(nm->ap_duration_ms,"Access point teardown"); // set a timer to tear down the AP mode
            break;
        case EN_TIMER:
            network_status_update_basic_info();
            result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_CONNECTED_STATE],__FUNCTION__);
            break;
        default:
            result= EVENT_UN_HANDLED;
    }
    // Process global handler at the end, since we want to overwrite
    // UPDATE_STATUS with our own logic above
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t WIFI_CONFIGURING_CONNECT_SUCCESS_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_set_timer(0,NULL);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}




/********************************************************************************************* 
 * WIFI_CONNECTING_STATE
 */
static state_machine_result_t WIFI_CONNECTING_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_t* const nm = (network_t *)State_Machine;
    network_handler_entry_print(State_Machine,true);
    network_start_stop_dhcp_client(nm->wifi_netif, true);
    network_connect_active_ssid(State_Machine);
    nm->STA_duration = nm->sta_polling_min_ms;
    /* create timer for background STA connection */
    network_set_timer(nm->STA_duration,"Wifi Polling timeout");    
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t WIFI_CONNECTING_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    state_machine_result_t result = EVENT_HANDLED;
    network_t* const nm = (network_t *)State_Machine;
    network_handler_print(State_Machine,true);
    switch (State_Machine->Event) {
        case EN_CONNECTED:
            // nothing to do here. Let's wait for IP address 
            break;
        case EN_TIMER:
            // try connecting again.
            // todo: implement multi-ap logic
            ESP_LOGI(TAG, "Timer: %s ",STR_OR_ALT(nm->timer_tag,"Ethernet link not detected"));
            network_connect_active_ssid(State_Machine);
            break;
        case EN_LOST_CONNECTION:
            if(nm->event_parameters->disconnected_event->reason == WIFI_REASON_ASSOC_LEAVE || nm->event_parameters->disconnected_event->reason == WIFI_REASON_AUTH_EXPIRE || nm->event_parameters->disconnected_event->reason ==  WIFI_REASON_ASSOC_EXPIRE) {
                ESP_LOGI(TAG,"Wifi was disconnected from previous access point. Waiting to connect.");
            }
            else if(nm->event_parameters->disconnected_event->reason != WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
                network_status_update_ip_info(UPDATE_FAILED_ATTEMPT);
                result = local_traverse_state(State_Machine, &Wifi_Configuring_State[WIFI_CONFIGURING_STATE],__FUNCTION__);
            }
            break;
        default:
            result = EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t WIFI_CONNECTING_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}

/********************************************************************************************* 
 * WIFI_CONNECTING_NEW_STATE
 */
static state_machine_result_t WIFI_CONNECTING_NEW_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_t* const nm = (network_t *)State_Machine;
    network_handler_entry_print(State_Machine,true);
    network_start_stop_dhcp_client(nm->wifi_netif, true);
    network_wifi_connect(nm->event_parameters->ssid,nm->event_parameters->password);
    FREE_AND_NULL(nm->event_parameters->ssid);
    FREE_AND_NULL(nm->event_parameters->password);
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t WIFI_CONNECTING_NEW_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_HANDLED;
    switch (State_Machine->Event) {
        case EN_GOT_IP:
            network_status_update_ip_info(UPDATE_CONNECTION_OK); 
            result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_CONNECTED_STATE],__FUNCTION__);
            break;
        case EN_CONNECTED:
            ESP_LOGD(TAG,"Successfully connected to the new access point. Waiting for IP Address");
            result = EVENT_HANDLED;
            break;
        case EN_LOST_CONNECTION:
            if(((network_t *)State_Machine)->event_parameters->disconnected_event->reason == WIFI_REASON_ASSOC_LEAVE){
                ESP_LOGD(TAG,"Successfully disconnected from the existing access point. ");
                return EVENT_HANDLED;
            }
            ESP_LOGW(TAG,"Trying to connect failed");
            result = local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_CONNECTING_NEW_FAILED_STATE],__FUNCTION__);
            break;

        default:
            result= EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t WIFI_CONNECTING_NEW_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_set_timer(0,NULL);
    FREE_AND_NULL(((network_t *)State_Machine)->event_parameters->disconnected_event);
    network_exit_handler_print(State_Machine,false);
    
    return EVENT_HANDLED;
}



/********************************************************************************************* 
 * WIFI_CONNECTING_NEW_FAILED_STATE
 */
static state_machine_result_t WIFI_CONNECTING_NEW_FAILED_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_t* const nm = (network_t *)State_Machine;
    network_handler_entry_print(State_Machine,true);
    if (nm->wifi_connected ) {
        // Wifi was already connected to an existing access point. Restore connection
        network_connect_active_ssid(State_Machine);      
    } 
  
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t WIFI_CONNECTING_NEW_FAILED_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_HANDLED;
    switch (State_Machine->Event) {
        case EN_GOT_IP:
            network_status_update_ip_info(UPDATE_FAILED_ATTEMPT_AND_RESTORE); 
            result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_CONNECTED_STATE],__FUNCTION__);
            break;
        case EN_CONNECTED:
            ESP_LOGD(TAG,"Successfully connected to the previous access point. Waiting for IP Address");
            result = EVENT_HANDLED;
            break;
        case EN_LOST_CONNECTION:
            network_status_update_ip_info(UPDATE_FAILED_ATTEMPT);
            messaging_post_message(MESSAGING_ERROR, MESSAGING_CLASS_SYSTEM, "Unable to fall back to previous access point.");
            result = EVENT_HANDLED;
            break;

        default:
            result= EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t WIFI_CONNECTING_NEW_FAILED_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_set_timer(0,NULL);
    FREE_AND_NULL(((network_t *)State_Machine)->event_parameters->disconnected_event);
    network_exit_handler_print(State_Machine,false);
    
    return EVENT_HANDLED;
}



/********************************************************************************************* 
 * WIFI_CONNECTED_STATE
 */
static state_machine_result_t WIFI_CONNECTED_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_t* const nm = (network_t *)State_Machine;
    network_handler_entry_print(State_Machine,true);
    nm->last_connected = esp_timer_get_time();
    // cancel timeout pulse
    network_set_timer(0,NULL);
    ESP_LOGD(TAG, "Checking if wifi config changed.");
    if (network_wifi_sta_config_changed()) {
        ESP_LOGD(TAG, "Wifi Config changed. Saving it.");
        network_wifi_save_sta_config();
    }
    ESP_LOGD(TAG, "Updating the ip info json.");
    network_interface_coexistence(State_Machine);
    nm->wifi_connected = true;
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t WIFI_CONNECTED_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_HANDLED;
    switch (State_Machine->Event) {
        case EN_LOST_CONNECTION:
            result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_LOST_CONNECTION_STATE],__FUNCTION__);
            break;
        default:
            result = EVENT_UN_HANDLED;
            break;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t WIFI_CONNECTED_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    FREE_AND_NULL(((network_t *)State_Machine)->event_parameters->disconnected_event);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}


/********************************************************************************************* 
 * WIFI_USER_DISCONNECTED_STATE
 */
static state_machine_result_t WIFI_USER_DISCONNECTED_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_handler_entry_print(State_Machine,true);
    ESP_LOGD(TAG, " WiFi disconnected by user");
    network_wifi_clear_config();
    network_status_update_ip_info(UPDATE_USER_DISCONNECT);
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t WIFI_USER_DISCONNECTED_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_handler_print(State_Machine,true);
    state_machine_result_t result = EVENT_HANDLED;
    switch (State_Machine->Event) {
        case EN_LOST_CONNECTION:
            // this is a success! we're actually asking to disconnect
            result= local_traverse_state(State_Machine, &Wifi_Configuring_State[WIFI_CONFIGURING_STATE],__FUNCTION__);
        break;
        default:
            result= EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t WIFI_USER_DISCONNECTED_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}

/********************************************************************************************* 
 * WIFI_LOST_CONNECTION_STATE
 */
static state_machine_result_t WIFI_LOST_CONNECTION_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_t* const nm = (network_t *)State_Machine;
    network_handler_entry_print(State_Machine,true);
    ESP_LOGE(TAG, " WiFi Connection lost.");
    messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "WiFi Connection lost");
    network_status_update_ip_info(UPDATE_LOST_CONNECTION);
    network_status_safe_reset_sta_ip_string();
    if (nm->last_connected > 0)
        nm->total_connected_time += ((esp_timer_get_time() - nm->last_connected) / (1000 * 1000));
    nm->last_connected = 0;
    nm->num_disconnect++;
    ESP_LOGW(TAG, " Wifi disconnected. Number of disconnects: %d, Average time connected: %d", nm->num_disconnect, nm->num_disconnect > 0 ? (nm->total_connected_time / nm->num_disconnect) : 0);
    if (nm->retries < WIFI_MANAGER_MAX_RETRY) {
        nm->retries++;
        ESP_LOGD(TAG, " Retrying connection connection, %d/%d.", nm->retries, WIFI_MANAGER_MAX_RETRY);
        network_async(EN_CONNECT);
    } else {
        /* In this scenario the connection was lost beyond repair */
        nm->retries = 0;
        ESP_LOGD(TAG,"Checking if ethernet interface is connected");
        if (network_is_interface_connected(nm->eth_netif)) {
            ESP_LOGW(TAG, "Cannot connect to Wifi. Falling back to Ethernet ");
            network_async(EN_ETHERNET_FALLBACK);
        } else {
            network_status_update_ip_info(UPDATE_LOST_CONNECTION);
            wifi_mode_t mode;
            ESP_LOGW(TAG, " All connect retry attempts failed.");

            /* put us in softAP mode first */
            esp_wifi_get_mode(&mode);
            if (WIFI_MODE_APSTA != mode) {
                 nm->STA_duration = nm->sta_polling_min_ms;
                network_async_configure();
            } else if (nm->STA_duration < nm->sta_polling_max_ms) {
                nm->STA_duration *= 1.25;
            }

            /* keep polling for existing connection */
            network_set_timer(nm->STA_duration, "Wifi Polling timeout");
            ESP_LOGD(TAG, " STA search slow polling of %d", nm->STA_duration);
        }
    }

    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t WIFI_LOST_CONNECTION_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    network_t* const nm = (network_t *)State_Machine;
    state_machine_result_t result = EVENT_HANDLED;
    network_handler_print(State_Machine,true);
    switch (State_Machine->Event) {
        case EN_CONFIGURE:
            result= local_traverse_state(State_Machine, &Wifi_Configuring_State[WIFI_CONFIGURING_STATE],__FUNCTION__);
            break;
        case EN_TIMER:
            ESP_LOGI(TAG, "Timer: %s ",STR_OR_ALT(nm->timer_tag,"Lost connection"));
            result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_LOST_CONNECTION_STATE],__FUNCTION__);
            break;
        case EN_CONNECT:
            result= local_traverse_state(State_Machine, &Wifi_Active_State[WIFI_CONNECTING_STATE],__FUNCTION__);
            break;
        default:
            result= EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t WIFI_LOST_CONNECTION_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    FREE_AND_NULL(((network_t *)State_Machine)->event_parameters->disconnected_event);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}


/********************************************************************************************* 
 * ETH_ACTIVE_CONNECTED_STATE
 */
static state_machine_result_t ETH_ACTIVE_CONNECTED_STATE_entry_handler(state_machine_t* const State_Machine) {
    network_t* const nm = (network_t *)State_Machine;
    network_handler_entry_print(State_Machine,true);
    network_status_update_ip_info(UPDATE_ETHERNET_CONNECTED);
    nm->ethernet_connected = true;
    // start a wifi Scan so web ui is populated with available entries
    NETWORK_EXECUTE_CB(State_Machine);
    network_handler_entry_print(State_Machine,false);
    return EVENT_HANDLED;
}
static state_machine_result_t ETH_ACTIVE_CONNECTED_STATE_handler(state_machine_t* const State_Machine) {
    HANDLE_GLOBAL_EVENT(State_Machine);
    state_machine_result_t result = EVENT_HANDLED;
    network_handler_print(State_Machine,true);
    switch (State_Machine->Event) {
        case EN_TIMER:
            ESP_LOGD(TAG, "Ignoring ethernet link up timer check");
            result= EVENT_HANDLED;
            break;
        default:
            result= EVENT_UN_HANDLED;
    }
    network_handler_print(State_Machine,false);
    return result;
}
static state_machine_result_t ETH_ACTIVE_CONNECTED_STATE_exit_handler(state_machine_t* const State_Machine) {
    network_exit_handler_print(State_Machine,true);
    network_exit_handler_print(State_Machine,false);
    return EVENT_HANDLED;
}

static state_machine_result_t local_switch_state(state_machine_t* state_machine,
                                                 const state_t* const target_state, const char * caller) {
    const state_t* source = state_machine->State;
    NETWORK_PRINT_TRANSITION(true, "BEGIN SWITCH", ((network_t *)state_machine)->source_state, target_state, state_machine->Event, true,caller);
    state_machine_result_t result = switch_state(state_machine, target_state);
    NETWORK_PRINT_TRANSITION( false,"BEGIN SWITCH", ((network_t *)state_machine)->source_state, target_state, state_machine->Event, true,caller);
    ((network_t *)state_machine)->source_state = source;
    return result;
}
static state_machine_result_t local_traverse_state(state_machine_t* const state_machine,
                                                   const state_t* const target_state, const char * caller) {
    const state_t *  source = state_machine->State;
    NETWORK_PRINT_TRANSITION( true,"BEGIN TRAVERSE", ((network_t *)state_machine)->source_state, target_state, state_machine->Event, true, caller);
    state_machine_result_t result = traverse_state(state_machine, target_state);
    NETWORK_PRINT_TRANSITION( false,"END TRAVERSE", ((network_t *)state_machine)->source_state, target_state, state_machine->Event, true,caller);
    ((network_t *)state_machine)->source_state = source;
    return result;
}

static void network_interface_coexistence(state_machine_t* state_machine) {
    // this function is called whenever both wifi and ethernet are
    // found to be active at the same time
    network_t* nm = (network_t *)state_machine;
    if (nm->wifi_connected && state_machine->Event == EN_ETH_GOT_IP) {
        char* eth_reboot = config_alloc_get_default(NVS_TYPE_STR, "eth_boot", "N", 0);
        network_prioritize_wifi(false);
        if (strcasecmp(eth_reboot, "N")) {
            ESP_LOGW(TAG, "Option eth_reboot set to reboot when ethernet is connected. Rebooting");
            simple_restart();
        } else {
            ESP_LOGW(TAG, "Option eth_reboot set to not reboot when ethernet is connected. Using Wifi interface until next reboot");
        }
        FREE_AND_NULL(eth_reboot);
    } else if (get_root(state_machine->State)->Id == NETWORK_ETH_ACTIVE_STATE){
        messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Wifi Connected with Ethernet active. System reload needed");
        simple_restart();
    }
}
