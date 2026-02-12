/*
Copyright (c) 2017-2021 Sebastien L
*/

#ifdef NETWORK_MANAGER_LOG_LEVEL
#define LOG_LOCAL_LEVEL NETWORK_MANAGER_LOG_LEVEL
#endif
#include "network_manager.h"
#include "network_ethernet.h"
#include "network_status.h"
#include "network_wifi.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

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

#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/netdb.h"
#include "mdns.h"
#include "messaging.h"

#include "tools.h"
#include "trace.h"

#include "Config.h"
#include "accessors.h"
#include "esp_err.h"
#include "http_server_handlers.h"
#include "network_manager.h"

QueueHandle_t network_queue;
BaseType_t network_task_handle;
static const char TAG[] = "network";
static TaskHandle_t task_network_manager = NULL;
RTC_NOINIT_ATTR static bool s_wifi_prioritized;

typedef struct network_callback {
    network_status_reached_cb cb;
    nm_state_t state;
    int sub_state;
    const char* from;
    SLIST_ENTRY(network_callback)
    next; //!< next callback
} network_callback_t;

/** linked list of command structures */
static SLIST_HEAD(cb_list, network_callback) s_cb_list;

network_t NM;

//! Create and initialize the array of state machines.
state_machine_t* const SM[] = {(state_machine_t*)&NM};
static void network_timer_cb(TimerHandle_t timer_id);
int get_root_id(const state_t* state);
const state_t* get_root(const state_t* const state);
static void network_task(void* pvParameters);

void network_start_stop_dhcp_client(esp_netif_t* netif, bool start) {
    tcpip_adapter_dhcp_status_t status;
    esp_err_t err = ESP_OK;
    ESP_LOGD(TAG, "Checking if DHCP client for STA interface is running");
    if(!netif) {
        ESP_LOGE(TAG, "Invalid adapter. Cannot start/stop dhcp. ");
        return;
    }
    if((err = esp_netif_dhcpc_get_status(netif, &status)) != ESP_OK) {
        ESP_LOGE(TAG, "Error retrieving dhcp status : %s", esp_err_to_name(err));
        return;
    }
    switch(status) {
    case ESP_NETIF_DHCP_STARTED:
        if(start) {
            ESP_LOGD(TAG, "DHCP client already started");
        } else {
            ESP_LOGI(TAG, "Stopping DHCP client");
            err = esp_netif_dhcpc_stop(netif);
            if(err != ESP_OK) { ESP_LOGE(TAG, "Error stopping DHCP Client : %s", esp_err_to_name(err)); }
        }
        break;
    case ESP_NETIF_DHCP_STOPPED:
        if(start) {
            ESP_LOGI(TAG, "ESP_NETIF_DHCP_STOPPED Starting DHCP client");
            err = esp_netif_dhcpc_start(netif);
            if(err != ESP_OK) { ESP_LOGE(TAG, "Error stopping DHCP Client : %s", esp_err_to_name(err)); }
        } else {
            ESP_LOGI(TAG, "DHCP client already started");
        }
        break;
    case ESP_NETIF_DHCP_INIT:
        if(start) {
            ESP_LOGI(TAG, "ESP_NETIF_DHCP_INIT: Starting DHCP client");
            err = esp_netif_dhcpc_start(netif);
            if(err != ESP_OK) { ESP_LOGE(TAG, "Error stopping DHCP Client : %s", esp_err_to_name(err)); }
        } else {
            ESP_LOGI(TAG, "Stopping DHCP client");
            err = esp_netif_dhcpc_stop(netif);
            if(err != ESP_OK) { ESP_LOGE(TAG, "Error stopping DHCP Client : %s", esp_err_to_name(err)); }
        }
        break;

    default:
        ESP_LOGW(TAG, "Unknown DHCP status");
        break;
    }
}
void network_start_stop_dhcps(esp_netif_t* netif, bool start) {
    tcpip_adapter_dhcp_status_t status;
    esp_err_t err = ESP_OK;
    ESP_LOGD(TAG, "Checking if DHCP server is running");
    if(!netif) {
        ESP_LOGE(TAG, "Invalid adapter. Cannot start/stop dhcp server. ");
        return;
    }
    if((err = esp_netif_dhcps_get_status(netif, &status)) != ESP_OK) {
        ESP_LOGE(TAG, "Error retrieving dhcp server status : %s", esp_err_to_name(err));
        return;
    }
    switch(status) {
    case ESP_NETIF_DHCP_STARTED:
        if(start) {
            ESP_LOGD(TAG, "DHCP server already started");
        } else {
            ESP_LOGI(TAG, "Stopping DHCP server");
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
        }
        break;
    case ESP_NETIF_DHCP_STOPPED:
        if(start) {
            ESP_LOGI(TAG, "Starting DHCP server");
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));
        } else {
            ESP_LOGI(TAG, "DHCP server already stopped");
        }
        break;
    case ESP_NETIF_DHCP_INIT:
        if(start) {
            ESP_LOGI(TAG, "Starting DHCP server");
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));
        } else {
            ESP_LOGI(TAG, "Stopping DHCP server");
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
        }
        break;

    default:
        ESP_LOGW(TAG, "Unknown DHCP status");
        break;
    }
}
/*********************************************************************************************
 * String conversion routines
 */

#define ADD_ROOT(name, ...) CASE_TO_STR(name);
#define ADD_ROOT_LEAF(name, ...) CASE_TO_STR(name);
#define ADD_LEAF(name, ...) CASE_TO_STR(name);
#define ADD_EVENT(name) CASE_TO_STR(name);
#define ADD_FIRST_EVENT(name) CASE_TO_STR(name);
static const char* state_to_string(const state_t* state) {
    if(!state) { return ""; }
    switch(state->Parent ? state->Parent->Id : state->Id) {
        ALL_NM_STATE
    default:
        break;
    }
    return "Unknown";
}
static const char* wifi_state_to_string(mn_wifi_active_state_t state) {
    switch(state) {
        ALL_WIFI_STATE(, )
    default:
        break;
    }
    return "Unknown";
}
static const char* eth_state_to_string(mn_eth_active_state_t state) {
    switch(state) {
        ALL_ETH_STATE(, )
    default:
        break;
    }
    return "Unknown";
}
static const char* wifi_configuring_state_to_string(mn_wifi_configuring_state_t state) {
    switch(state) {
        ALL_WIFI_CONFIGURING_STATE(, )
    default:
        break;
    }
    return "Unknown";
}
static const char* sub_state_to_string(const state_t* state) {
    if(!state) { return "N/A"; }
    int root_id = get_root_id(state);
    switch(root_id) {
    case NETWORK_ETH_ACTIVE_STATE:
        return eth_state_to_string(state->Id);
        break;
    case NETWORK_WIFI_ACTIVE_STATE:
        return wifi_state_to_string(state->Id);
    case NETWORK_WIFI_CONFIGURING_ACTIVE_STATE:
        return wifi_configuring_state_to_string(state->Id);
    default:
        break;
    }
    return "*";
}

const char* network_event_to_string(network_event_t state) {
    switch(state) {
        ALL_NM_EVENTS

    default:
        break;
    }
    return "Unknown";
}

#undef ADD_EVENT
#undef ADD_FIRST_EVENT
#undef ADD_ROOT
#undef ADD_ROOT_LEAF
#undef ADD_LEAF

typedef struct {
    int parent_state;
    int sub_state_last;
} max_sub_states_t;
static const max_sub_states_t state_max[] = {{.parent_state = NETWORK_INSTANTIATED_STATE, .sub_state_last = 0},
    {.parent_state = NETWORK_ETH_ACTIVE_STATE, .sub_state_last = TOTAL_ETH_ACTIVE_STATE - 1},
    {.parent_state = NETWORK_WIFI_ACTIVE_STATE, .sub_state_last = TOTAL_WIFI_ACTIVE_STATE - 1},
    {.parent_state = WIFI_CONFIGURING_STATE, .sub_state_last = TOTAL_WIFI_CONFIGURING_STATE - 1},
    {.parent_state = WIFI_CONFIGURING_STATE, .sub_state_last = TOTAL_WIFI_CONFIGURING_STATE - 1}, {.parent_state = -1}};

void check_queue() {
    if(!network_queue) {
        ESP_LOGD(TAG, " Creating message queue");
        network_queue = xQueueCreate(6, sizeof(queue_message));
    }
}
void network_initialize_task() {
    if(cold_boot) {
        ESP_LOGI(TAG, "Setting wifi priotitized flag to false");
        s_wifi_prioritized = false;
    }
    check_queue();
    ESP_LOGD(TAG, "Creating network manager task");
    network_task_handle = xTaskCreate(&network_task, "network", 4096 * 2, NULL, ESP_TASK_TIMER_PRIO, &task_network_manager);
}

static void event_logger(uint32_t state_machine, uint32_t state, uint32_t event) {
    ESP_LOGD(TAG, "Handling network manager event state Id %d->[%s]", state, network_event_to_string(event));
}
static const char* get_state_machine_result_string(state_machine_result_t result) {
    switch(result) {
    case EVENT_HANDLED:
        return "EVENT_HANDLED";
    case EVENT_UN_HANDLED:
        return "EVENT_UN_HANDLED";
    case TRIGGERED_TO_SELF:
        return "TRIGGERED_TO_SELF";
    }
    return "Unknown";
}
static void result_logger(uint32_t state, state_machine_result_t result) {
    ESP_LOGD(TAG, "Network Manager Result: %s, New State id: %d", get_state_machine_result_string(result), state);
}

static void network_task(void* pvParameters) {
    queue_message msg;
    BaseType_t xStatus;
    network_initialize_state_machine_globals();
    initialize_network_handlers((state_machine_t*)&NM);

    // make sure that the start event processes before everything
    // else, as some critical initialization happen there.
    network_async_front(EN_START);
    /* main processing loop */
    for(;;) {
        xStatus = xQueueReceive(network_queue, &msg, portMAX_DELAY);

        if(xStatus == pdPASS) {
            // pass the event to the sync processor
            NM.event_parameters = &msg;
            NM.Machine.Event = msg.trigger;
            if(dispatch_event(SM, 1, event_logger, result_logger) == EVENT_UN_HANDLED) {
                network_manager_format_from_to_states(
                    ESP_LOG_ERROR, "Unhandled Event", NULL, NM.Machine.State, msg.trigger, false, "network manager");
            }
        } /* end of if status=pdPASS */
    } /* end of for loop */

    vTaskDelete(NULL);
}

int get_max_substate(nm_state_t state) {
    for(int i = 0; state_max[i].parent_state != -1; i++) {
        if(state_max[i].parent_state == state) { return state_max[i].sub_state_last; }
    }
    return -1;
}
esp_err_t network_register_state_callback(nm_state_t state, int sub_state, const char* from, network_status_reached_cb cb) {
    network_callback_t* item = NULL;
    if(!cb) { return ESP_ERR_INVALID_ARG; }
    item = calloc(1, sizeof(*item));
    if(item == NULL) { return ESP_ERR_NO_MEM; }
    if(sub_state != -1 && sub_state > get_max_substate(state)) {
        // sub state has to be valid
        return ESP_ERR_INVALID_ARG;
    }

    item->state = state;
    item->cb = cb;
    item->from = from;
    item->sub_state = sub_state;
    network_callback_t* last = SLIST_FIRST(&s_cb_list);
    if(last == NULL) {
        SLIST_INSERT_HEAD(&s_cb_list, item, next);
    } else {
        network_callback_t* it;
        while((it = SLIST_NEXT(last, next)) != NULL) { last = it; }
        SLIST_INSERT_AFTER(last, item, next);
    }
    return ESP_OK;
}
const state_t* get_root(const state_t* const state) {
    if(!state) return NULL;
    return state->Parent == NULL ? state : get_root(state->Parent);
}
int get_root_id(const state_t* state) {
    if(!state) return -1;
    return state->Parent == NULL ? state->Id : get_root_id(state->Parent);
}

static bool is_root_state(const state_t* state) { return state->Parent == NULL; }
static bool is_current_state(const state_t* state, nm_state_t state_id, int sub_state_id) {
    return get_root(state)->Id == state_id && (sub_state_id == -1 || (!is_root_state(state) && state->Id == sub_state_id));
}

void network_execute_cb(state_machine_t* const state_machine, const char* caller) {
    network_callback_t* it;
    SLIST_FOREACH(it, &s_cb_list, next) {
        if(is_current_state(state_machine->State, it->state, it->sub_state)) {
            char* cb_prefix = messaging_alloc_format_string("BEGIN Executing Callback %s", it->from);
            NETWORK_DEBUG_STATE_MACHINE(true, STR_OR_BLANK(cb_prefix), state_machine, false, STR_OR_BLANK(caller));
            FREE_AND_NULL(cb_prefix);
            it->cb((nm_state_t)get_root(state_machine->State)->Id, is_root_state(state_machine->State) ? -1 : state_machine->State->Id);
            cb_prefix = messaging_alloc_format_string("END Executing Callback %s", it->from);
            NETWORK_DEBUG_STATE_MACHINE(false, STR_OR_BLANK(cb_prefix), state_machine, false, STR_OR_BLANK(caller));
            FREE_AND_NULL(cb_prefix);
        }
    }
}

bool network_is_wifi_prioritized() {
    sys_dev_eth_common* common = NULL;
    bool result = s_wifi_prioritized;
    bool valid_model = SYS_ETH_COMMON(common) && common->model != sys_dev_eth_models_NONE;
    if(result) {
        result = network_wifi_get_known_count() > 0 || !valid_model;
        ESP_LOGD(TAG, "Wifi is prioritized with %d known access points.%s %s", network_wifi_get_known_count(),
            valid_model ? " And a valid ethernet adapter" : "", result ? "Wifi prioritized" : "Ethernet prioritized");
    }
    return result;
}

void network_prioritize_wifi(bool activate) {
    if(s_wifi_prioritized == activate) return;
    s_wifi_prioritized = activate;
    ESP_LOGI(TAG, "Wifi is %sprioritized", activate ? "" : "not ");
}

void network_manager_format_state_machine(
    esp_log_level_t level, const char* prefix, state_machine_t* state_machine, bool show_source, const char* caller) {
    state_t const* source_state = NULL;
    state_t const* current_state = NULL;
    network_event_t event = -1;

    if(state_machine) {
        source_state = ((network_t*)state_machine)->source_state;
        current_state = state_machine->State;
        event = state_machine->Event;
        network_manager_format_from_to_states(level, prefix, source_state, current_state, event, show_source, caller);
    } else {
        ESP_LOG_LEVEL(level, TAG, "%s - %s -> [%s]", STR_OR_BLANK(caller), prefix, network_event_to_string(event));
    }
}
void network_manager_format_from_to_states(esp_log_level_t level, const char* prefix, const state_t* from_state, const state_t* current_state,
    network_event_t event, bool show_source, const char* caller) {
    const char* source_state = "";
    const char* source_sub_state = "";
    const char* state = "N/A";
    const char* sub_state = "N/A";

    if(current_state) {
        state = state_to_string(current_state);
        sub_state = sub_state_to_string(current_state);
    }
    if(!from_state) {
        source_state = "N/A";
    } else {
        source_state = state_to_string(from_state);
        source_sub_state = sub_state_to_string(from_state);
    }
    if(show_source) {
        ESP_LOG_LEVEL(level, TAG, "%s %s %s(%s)->%s(%s) [%s]", STR_OR_BLANK(caller), prefix, source_state, source_sub_state, state, sub_state,
            network_event_to_string(event));
    } else {
        ESP_LOG_LEVEL(level, TAG, "%s %s %s(%s) [%s]", STR_OR_BLANK(caller), prefix, state, sub_state, network_event_to_string(event));
    }
}
#define xSafeQueueSendToBack(xQueue, pvItemToQueue, xTicksToWait)                                                                                    \
    check_queue();                                                                                                                                   \
    xQueueSendToBack(xQueue, pvItemToQueue, xTicksToWait);
#define xSafeQueueSendToFront(xQueue, pvItemToQueue, xTicksToWait)                                                                                   \
    check_queue();                                                                                                                                   \
    xQueueSendToFront(xQueue, pvItemToQueue, xTicksToWait);
void network_async(network_event_t trigger) {
    queue_message msg;
    memset(&msg, 0x00, sizeof(msg));
    msg.trigger = trigger;
    ESP_LOGD(TAG, "Posting event %s directly", network_event_to_string(trigger));
    xSafeQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}
void network_async_got_ip(network_event_t trigger, ip_event_got_ip_t* event_data) {
    queue_message msg;
    memset(&msg, 0x00, sizeof(msg));
    msg.trigger = trigger;
    msg.ctx.got_ip_event_data = (ip_event_got_ip_t*)clone_obj_psram(event_data, sizeof(ip_event_got_ip_t));
    ESP_LOGD(TAG, "Posting event %s", network_event_to_string(trigger));
    xSafeQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}
void network_async_front(network_event_t trigger) {
    queue_message msg;
    memset(&msg, 0x00, sizeof(msg));
    msg.trigger = trigger;
    ESP_LOGD(TAG, "Posting event %s directly to front of the queue", network_event_to_string(trigger));
    xSafeQueueSendToFront(network_queue, &msg, portMAX_DELAY);
}

void network_async_commit_protowrapper(void* protoWrapperBase) {
    queue_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.trigger = EN_COMMIT_CHANGES;
    msg.ctx.strval = (char*)protoWrapperBase; // Cast to void pointer
    ESP_LOGD(TAG, "Posting event %s", network_event_to_string(msg.trigger));
    xSafeQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}

void network_async_fail() { network_async(EN_FAIL); }
void network_async_success() { network_async(EN_SUCCESS); }
void network_async_connected() { network_async(EN_CONNECTED); }
void network_async_link_up() { network_async(EN_LINK_UP); }
void network_async_link_down() { network_async(EN_LINK_DOWN); }
void network_async_configure() { network_async(EN_CONFIGURE); }
void network_async_eth_got_ip() { network_async(EN_ETH_GOT_IP); }
void network_async_timer() { network_async(EN_TIMER); }
void network_async_scan() { network_async(EN_SCAN); }

void network_async_update_status() { network_async(EN_UPDATE_STATUS); }

void network_async_delete() { network_async(EN_DELETE); }

void network_async_scan_done() { network_async(EN_SCAN_DONE); }
void network_async_delete_connection(const char* ssid) {
    queue_message msg;
    memset(&msg, 0x00, sizeof(msg));
    msg.trigger = EN_REMOVE;
    msg.ctx.credentials.ssid = strdup_psram(ssid);
    ESP_LOGD(TAG, "Posting event %s for ssid %s", network_event_to_string(msg.trigger), ssid);
    xSafeQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}
void network_async_connect(const char* ssid, const char* password) {
    queue_message msg;
    memset(&msg, 0x00, sizeof(msg));
    msg.trigger = EN_CONNECT_NEW;
    msg.ctx.credentials.ssid = strdup_psram(ssid);
    if(password && strlen(password) > 0) { msg.ctx.credentials.password = strdup_psram(password); }
    ESP_LOGD(TAG, "Posting event %s", network_event_to_string(msg.trigger));
    xSafeQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}
void network_async_add(const char* ssid, const char* password) {
    queue_message msg;
    memset(&msg, 0x00, sizeof(msg));
    msg.trigger = EN_ADD;
    msg.ctx.credentials.ssid = strdup_psram(ssid);
    msg.ctx.credentials.password = strdup_psram(STR_OR_BLANK(password));
    ESP_LOGD(TAG, "Posting event %s", network_event_to_string(msg.trigger));
    xSafeQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}
void network_async_lost_connection(wifi_event_sta_disconnected_t* disconnected_event) {
    queue_message msg;
    memset(&msg, 0x00, sizeof(msg));
    msg.trigger = EN_LOST_CONNECTION;
    ESP_LOGD(TAG, "Posting event %s", network_event_to_string(msg.trigger));
    msg.ctx.disconnected_event = clone_obj_psram(disconnected_event, sizeof(wifi_event_sta_disconnected_t));
    if(msg.ctx.disconnected_event) {
        xSafeQueueSendToBack(network_queue, &msg, portMAX_DELAY);
    } else {
        ESP_LOGE(TAG, "Unable to post lost connection event.");
    }
}
void network_async_reboot(reboot_type_t rtype) {
    queue_message msg;
    memset(&msg, 0x00, sizeof(msg));
    msg.trigger = EN_REBOOT;
    msg.ctx.rtype = rtype;
    ESP_LOGD(TAG, "Posting event %s - type %d", network_event_to_string(msg.trigger), rtype);
    xSafeQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}
void network_async_callback_withret(void* ctx, network_manager_ret_cb_t cb) {
    queue_message msg;
    memset(&msg, 0x00, sizeof(msg));
    msg.trigger = EN_EXECUTE_CALLBACK;
    msg.ctx.cb_ctx.ret_cb = cb;
    msg.ctx.cb_ctx.cb = NULL;
    msg.ctx.cb_ctx.ctx = ctx;
    ESP_LOGD(TAG, "Posting event %s %s", network_event_to_string(msg.trigger), "with return");
    xSafeQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}
void network_async_callback(void* ctx, network_manager_cb_t cb) {
    queue_message msg;
    memset(&msg, 0x00, sizeof(msg));
    msg.trigger = EN_EXECUTE_CALLBACK;
    msg.ctx.cb_ctx.cb = cb;
    msg.ctx.cb_ctx.ret_cb = NULL;
    msg.ctx.cb_ctx.ctx = ctx;
    ESP_LOGD(TAG, "Posting event %s %s", network_event_to_string(msg.trigger), "without return");
    xSafeQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}

void network_reboot_ota(char* url) {
    queue_message msg;
    memset(&msg, 0x00, sizeof(msg));

    if(url == NULL) {
        msg.trigger = EN_REBOOT;
        msg.ctx.rtype = OTA;
        ESP_LOGD(TAG, "Posting event %s - type %d", network_event_to_string(msg.trigger), msg.ctx.rtype);
    } else {
        msg.trigger = EN_REBOOT_URL;
        ESP_LOGD(TAG, "Posting event %s - type reboot URL", network_event_to_string(msg.trigger));
        msg.ctx.strval = strdup_psram(url);
    }

    xSafeQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}

network_t* network_get_state_machine() { return &NM; }

static void network_timer_cb(TimerHandle_t timer_id) { network_async_timer(); }
esp_netif_t* network_get_active_interface() {
    if(NM.wifi_ap_netif && (network_wifi_is_ap_mode() || network_wifi_is_ap_sta_mode())) {
        return NM.wifi_ap_netif;
    } else if(NM.wifi_netif && network_wifi_is_sta_mode()) {
        return NM.wifi_netif;
    }
    return NM.eth_netif;
}
bool network_is_interface_connected(esp_netif_t* interface) {
    esp_err_t err = ESP_OK;
    tcpip_adapter_ip_info_t ipInfo;
    if(!interface) { return false; }
    err = network_get_ip_info_for_netif(interface, &ipInfo);
    if(err != ESP_OK) { ESP_LOGD(TAG, "network_get_ip_info_for_netif returned %s", esp_err_to_name(err)); }
    return ((err == ESP_OK) && (ipInfo.ip.addr != IPADDR_ANY));
}
static esp_netif_t* get_connected_interface() {
    esp_netif_t* interface = NULL;
    for(int i = 0; i < 4; i++) {
        switch(i) {
        case 0:
            // try the active interface
            interface = network_get_active_interface();
            break;
        case 1:
            interface = NM.wifi_ap_netif;
            break;
        case 2:
            interface = NM.wifi_netif;
            break;
        case 3:
            interface = NM.eth_netif;
            break;
        default:
            break;
        }
        if(interface && network_is_interface_connected(interface)) {
            ESP_LOGD(TAG, "Found connected interface in iteration #%d", i);
            return interface;
        }
    }
    ESP_LOGD(TAG, "No connected interface found");
    return NULL;
}
esp_err_t network_get_ip_info_for_netif(esp_netif_t* netif, tcpip_adapter_ip_info_t* ipInfo) {
    esp_netif_ip_info_t loc_ip_info;
    ESP_LOGD(TAG, "%s Getting IP info", "network_get_ip_info_for_netif");
    if(!ipInfo) {
        ESP_LOGE(TAG, "Invalid pointer for ipInfo");
        return ESP_ERR_INVALID_ARG;
    }
    if(!netif) {
        ESP_LOGE(TAG, "Invalid pointer for netif");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGD(TAG, "%s Pointers were valid", "network_get_ip_info_for_netif");
    memset(ipInfo, 0x00, sizeof(tcpip_adapter_ip_info_t));
    esp_err_t err = esp_netif_get_ip_info(netif, &loc_ip_info);
    if(err == ESP_OK) {
        ESP_LOGD(TAG, "%s Setting ip4 address", "network_get_ip_info_for_netif");
        ip4_addr_set(&(ipInfo->ip), &loc_ip_info.ip);
        ip4_addr_set(&(ipInfo->gw), &loc_ip_info.gw);
        ip4_addr_set(&(ipInfo->netmask), &loc_ip_info.netmask);
    }
    return err;
}
esp_err_t network_get_ip_info(tcpip_adapter_ip_info_t* ipInfo) {
    esp_netif_t* netif = get_connected_interface();
    if(netif) { return network_get_ip_info_for_netif(netif, ipInfo); }
    return ESP_FAIL;
}

esp_err_t network_get_hostname(const char** hostname) { return esp_netif_get_hostname(get_connected_interface(), hostname); }

void network_set_timer(uint16_t duration, const char* tag) {
    if(duration > 0) {
        if(tag) {
            ESP_LOGD(TAG, "Setting timer tag to %s", tag);
            NM.timer_tag = strdup_psram(tag);
        }
        if(!NM.state_timer) {
            ESP_LOGD(TAG, "Starting %s timer with period of %u ms.", STR_OR_ALT(NM.timer_tag, "anonymous"), duration);
            NM.state_timer = xTimerCreate("background STA", pdMS_TO_TICKS(duration), pdFALSE, NULL, network_timer_cb);
        } else {
            ESP_LOGD(TAG, "Changing %s timer period to %u ms.", STR_OR_ALT(NM.timer_tag, "anonymous"), duration);
            xTimerChangePeriod(NM.state_timer, pdMS_TO_TICKS(duration), portMAX_DELAY);
        }
        xTimerStart(NM.state_timer, portMAX_DELAY);
    } else if(NM.state_timer) {
        ESP_LOGD(TAG, "Stopping timer %s", STR_OR_ALT(NM.timer_tag, "anonymous"));
        xTimerStop(NM.state_timer, portMAX_DELAY);
        FREE_AND_NULL(NM.timer_tag);
    }
}
void network_ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ip_event_got_ip_t* s = NULL;
    if(event_base != IP_EVENT) return;
    switch(event_id) {
    case IP_EVENT_ETH_GOT_IP:
    case IP_EVENT_STA_GOT_IP:
        s = (ip_event_got_ip_t*)event_data;
        network_async_got_ip(event_id == IP_EVENT_ETH_GOT_IP ? EN_ETH_GOT_IP : EN_GOT_IP, s);
        break;
    case IP_EVENT_STA_LOST_IP:
        ESP_LOGD(TAG, "IP_EVENT_STA_LOST_IP");
        break;
    case IP_EVENT_AP_STAIPASSIGNED:
        ESP_LOGD(TAG, "IP_EVENT_AP_STAIPASSIGNED");
        break;
    case IP_EVENT_GOT_IP6:
        ESP_LOGD(TAG, "IP_EVENT_GOT_IP6");
        break;
    default:
        break;
    }
}
void network_set_hostname(esp_netif_t* interface) {
    esp_err_t err;
    ESP_LOGD(TAG, "network_set_hostname. Retrieving host name from config structure");
    char* temp = NULL;

    if(!platform || !platform->has_names || strlen(platform->names.device) == 0) {
        temp = alloc_get_fallback_unique_name();
        ESP_LOGE(TAG, "Device name not set. Falling back to %s", temp);
    }

    ESP_LOGD(TAG, "Setting host name to : %s", temp ? temp : platform->names.device);
    if((err = esp_netif_set_hostname(interface, temp ? temp : platform->names.device)) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set host name. Error: %s", esp_err_to_name(err));
    }
    if(temp) free(temp);
}
