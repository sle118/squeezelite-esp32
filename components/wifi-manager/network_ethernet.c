#ifdef NETWORK_ETHERNET_LOG_LEVEL
#define LOG_LOCAL_LEVEL NETWORK_ETHERNET_LOG_LEVEL
#endif
#include "network_ethernet.h"
#include "Config.h"
#include "accessors.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/timers.h"
#include "globdefs.h"
#include "messaging.h"
#include "network_status.h"
#include "tools.h"

static char TAG[] = "network_ethernet";
TimerHandle_t ETH_timer;
esp_netif_t* eth_netif = NULL;
EventGroupHandle_t ethernet_event_group;
const int LINK_UP_BIT = BIT0;

static network_ethernet_driver_t* network_driver = NULL;
extern network_ethernet_detect_func_t DM9051_Detect, W5500_Detect, LAN8720_Detect;
static network_ethernet_detect_func_t* drivers[] = {DM9051_Detect, W5500_Detect, LAN8720_Detect, NULL};
#define ETH_TIMEOUT_MS (30 * 1000)

network_ethernet_driver_t* network_ethernet_driver_autodetect() {
    sys_dev_eth_config* eth_config;
    sys_dev_eth_common* eth_common;

    if(!SYS_ETH(eth_config) || !SYS_ETH_COMMON(eth_common)) {
        ESP_LOGD(TAG, "Ethernet not configured");
        return NULL;
    }
    for(uint8_t i = _sys_dev_eth_models_MIN; i < _sys_dev_eth_models_MAX; i++) {
        network_ethernet_driver_t* found_driver = drivers[i](eth_config);
        if(found_driver) {
            ESP_LOGI(TAG, "Detected driver %s ", sys_dev_eth_models_name(eth_common->model));
            network_driver = found_driver;
            return found_driver;
        }
    }
    return NULL;
}

static void eth_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
esp_netif_t* network_ethernet_get_interface() { return eth_netif; }

bool network_ethernet_is_up() { return (xEventGroupGetBits(ethernet_event_group) & LINK_UP_BIT) != 0; }
bool network_ethernet_enabled() { return network_driver != NULL && network_driver->handle != NULL; }
bool network_ethernet_wait_for_link(uint16_t max_wait_ms) {
    if(!network_ethernet_enabled()) return false;
    bool link_up = (xEventGroupGetBits(ethernet_event_group) & LINK_UP_BIT) != 0;
    if(!link_up) {
        ESP_LOGD(TAG, "Waiting for Ethernet link to be established...");
        link_up = (xEventGroupWaitBits(ethernet_event_group, LINK_UP_BIT, pdFALSE, pdTRUE, max_wait_ms / portTICK_PERIOD_MS) & LINK_UP_BIT) != 0;
        if(!link_up) {
            ESP_LOGW(TAG, "Ethernet Link timeout.");
        } else {
            ESP_LOGI(TAG, "Ethernet Link Up!");
        }
    }
    return link_up;
}

static void ETH_Timeout(TimerHandle_t timer);
void destroy_network_ethernet() {}

static void network_ethernet_print_config(const network_ethernet_driver_t* eth_config) {
    sys_dev_eth_config* sys_eth;
    int mdc = -1, mdio = -1, rst = -1, intr = -1, cs = -1;
    uint32_t speed = 0;
    int8_t host = 0;

    if(SYS_ETH(sys_eth)) {
        rst = sys_eth->common.rst;

        if(sys_eth->which_ethType == sys_dev_eth_config_spi_tag) {
            cs = sys_eth->ethType.spi.cs;
            intr = sys_eth->ethType.spi.intr;
            speed = sys_eth->ethType.spi.speed;
            host = sys_eth->ethType.spi.host - sys_dev_common_hosts_Host0;

        } else if(sys_eth->which_ethType == sys_dev_eth_config_rmii_tag) {
            mdc = sys_eth->ethType.rmii.mdc;
            mdio = sys_eth->ethType.rmii.mdio;
        }
    }
    ESP_LOGI(TAG,
        "Ethernet config => model: %s, valid: %s, type: %s, mdc:%d, mdio:%d, rst:%d, intr:%d, "
        "cs:%d, speed:%d, host:%d",
        sys_dev_eth_models_name(eth_config->model), eth_config->valid ? "YES" : "NO", eth_config->spi ? "SPI" : "RMII", mdc, mdio, rst, intr, cs,
        speed, host);
}

void init_network_ethernet() {
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Attempting to initialize Ethernet");
    sys_dev_eth_config* sys_eth;
    if(!SYS_ETH(sys_eth)) {
        ESP_LOGD(TAG, "No ethernet configured");
        return;
    }
    network_ethernet_driver_t* driver = network_ethernet_driver_autodetect();
    if(!driver || !driver->valid) {
        ESP_LOGI(TAG, "No Ethernet configuration, or configuration invalid");
        return;
    }

    network_driver->init_config(&platform->dev.eth);
    network_ethernet_print_config(driver);

    eth_netif = esp_netif_new(network_driver->cfg_netif);
    // esp_eth_set_default_handlers(eth_netif);
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL);
    ethernet_event_group = xEventGroupCreate();
    xEventGroupClearBits(ethernet_event_group, LINK_UP_BIT);
    err = network_driver->start(NULL, sys_eth);
    if(err == ESP_OK) {
        uint8_t mac_address[6];
        esp_read_mac(mac_address, ESP_MAC_ETH);
        char* mac_string = alloc_get_formatted_mac_string(mac_address);
        ESP_LOGD(TAG, "Assigning mac address %s to ethernet interface", STR_OR_BLANK(mac_string));
        FREE_AND_NULL(mac_string);
        esp_eth_ioctl(network_driver->handle, ETH_CMD_S_MAC_ADDR, mac_address);
    }
    if(err == ESP_OK) {
        ESP_LOGD(TAG, "Attaching ethernet to network interface");
        err = esp_netif_attach(eth_netif, esp_eth_new_netif_glue(network_driver->handle));
    }
    if(err == ESP_OK) {
        ESP_LOGI(TAG, "Starting ethernet network");
        err = esp_eth_start(network_driver->handle);
    }
    if(err != ESP_OK) {
        messaging_post_message(MESSAGING_ERROR, MESSAGING_CLASS_SYSTEM, "Configuring Ethernet failed: %s", esp_err_to_name(err));
        network_driver->handle = NULL;
    }
}

void network_ethernet_start_timer() { ETH_timer = xTimerCreate("ETH check", pdMS_TO_TICKS(ETH_TIMEOUT_MS), pdFALSE, NULL, ETH_Timeout); }

/** Event handler for Ethernet events */
static void eth_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    if(event_base == ETH_EVENT) {
        esp_eth_handle_t eth_handle = *(esp_eth_handle_t*)event_data;
        switch(event_id) {
        case ETHERNET_EVENT_CONNECTED:
            xEventGroupSetBits(ethernet_event_group, LINK_UP_BIT);
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "Ethernet Link Up, HW Addr %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4],
                mac_addr[5]);
            network_async_link_up();
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Down");
            xEventGroupClearBits(ethernet_event_group, LINK_UP_BIT);
            network_async_link_down();
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet Started. Setting host name");
            network_set_hostname(eth_netif);
            network_async_success();
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet Stopped");
            break;
        default:
            break;
        }
    }
}

static void ETH_Timeout(TimerHandle_t timer) {
    (void)timer;
    network_async_fail();
}
