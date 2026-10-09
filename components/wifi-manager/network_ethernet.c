#ifdef NETWORK_ETHERNET_LOG_LEVEL
#define LOG_LOCAL_LEVEL NETWORK_ETHERNET_LOG_LEVEL
#endif
#include "network_ethernet.h"
#include "freertos/timers.h"
#include "messaging.h"
#include "network_status.h"
#include "platform_config.h"
#include "tools.h"
#include "accessors.h"
#include "esp_log.h"
#include "globdefs.h"

static char TAG[] = "network_ethernet";
TimerHandle_t ETH_timer;
esp_netif_t* eth_netif = NULL;
EventGroupHandle_t ethernet_event_group;
const int LINK_UP_BIT = BIT0;

static const char* known_drivers[] = {"DM9051", "W5500", "LAN8720", NULL};
static network_ethernet_driver_t* network_driver = NULL;
extern network_ethernet_detect_func_t DM9051_Detect, W5500_Detect, LAN8720_Detect;
static network_ethernet_detect_func_t* drivers[] = {DM9051_Detect, W5500_Detect, LAN8720_Detect, NULL};
#define ETH_TIMEOUT_MS (30 * 1000)

/****************************************************************************************
 *
 */
const char* network_ethernet_conf_get_driver_name(const char* driver) {
    for (uint8_t i = 0; known_drivers[i] != NULL && strlen(known_drivers[i]) > 0; i++) {
        if (strcasestr(driver, known_drivers[i])) {
            return known_drivers[i];
        }
    }
    return NULL;
}
/****************************************************************************************
 *
 */
bool network_ethernet_is_valid_driver(const char* driver) {
    return network_ethernet_conf_get_driver_name(driver) != NULL;
}

network_ethernet_driver_t* network_ethernet_driver_autodetect(const char* Driver) {
    if (!Driver)
        return NULL;

    for (int i = 0; drivers[i]; i++) {
        network_ethernet_driver_t* found_driver = drivers[i](Driver);
        if (found_driver) {
            ESP_LOGI(TAG, "Detected driver %s ", Driver);

            network_driver = found_driver;
            return found_driver;
        }
    }
    return NULL;
}

static void eth_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
esp_netif_t *network_ethernet_get_interface(){
    return eth_netif;
}

bool network_ethernet_is_up() {
    return (xEventGroupGetBits(ethernet_event_group) & LINK_UP_BIT)!=0;
}
bool network_ethernet_enabled() {
    return  network_driver !=NULL && network_driver->handle != NULL;
}

esp_err_t network_ethernet_recover() {
    if (!network_ethernet_enabled()) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Restarting Ethernet stack after persistent TCP reconnect failures");
    esp_err_t err = esp_eth_stop(network_driver->handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_eth_stop failed: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_err_t start_err = esp_eth_start(network_driver->handle);
    if (start_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_eth_start failed: %s", esp_err_to_name(start_err));
        return start_err;
    }
    return err;
}
bool network_ethernet_wait_for_link(uint16_t max_wait_ms){
    if(!network_ethernet_enabled()) return false;
	bool link_up=(xEventGroupGetBits(ethernet_event_group) & LINK_UP_BIT)!=0;
	if(!link_up){
		ESP_LOGD(TAG,"Waiting for Ethernet link to be established...");
	    link_up = (xEventGroupWaitBits(ethernet_event_group, LINK_UP_BIT,pdFALSE, pdTRUE, max_wait_ms / portTICK_PERIOD_MS)& LINK_UP_BIT)!=0;
	    if(!link_up){
	    	ESP_LOGW(TAG,"Ethernet Link timeout.");
	    }
	    else
	    {
	    	ESP_LOGI(TAG,"Ethernet Link Up!");
	    }
	}
    return link_up;
}

static void ETH_Timeout(void* timer_id);
void destroy_network_ethernet() {
}

static void network_ethernet_print_config(const eth_config_t* eth_config) {
    	ESP_LOGI(TAG,"Ethernet config => model: %s, valid: %s, type: %s, mdc:%d, mdio:%d, rst:%d, intr:%d, cs:%d, speed:%d, host:%d",
					eth_config->model, eth_config->valid ? "YES" : "NO", eth_config->spi ? "SPI" : "RMII",	
					eth_config->mdc, eth_config->mdio,	
					eth_config->rst, eth_config->intr, eth_config->cs, eth_config->speed, eth_config->host);
}


void init_network_ethernet() {
    esp_err_t err = ESP_OK;
    eth_config_t eth;
    ESP_LOGI(TAG, "Attempting to initialize Ethernet");
    config_eth_init(&eth);
    if(!eth.valid){
        ESP_LOGI(TAG,"No Ethernet configuration, or configuration invalid");
        return;
    }

    network_driver->init_config(&eth);
    network_ethernet_print_config(&eth);

    eth_netif = esp_netif_new(network_driver->cfg_netif);
    esp_eth_set_default_handlers(eth_netif);
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL);
    ethernet_event_group = xEventGroupCreate();
	xEventGroupClearBits(ethernet_event_group, LINK_UP_BIT);
    spi_device_handle_t spi_handle = NULL;
    if (network_driver->spi) {
	    err = spi_bus_add_device(eth.host, network_driver->devcfg, &spi_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI host failed : %s", esp_err_to_name(err));
        }
    }
    if (err == ESP_OK) {
        err = network_driver->start(spi_handle,&eth);
    }
    if(err == ESP_OK){
        uint8_t mac_address[6];
        esp_read_mac(mac_address,ESP_MAC_ETH);
        char * mac_string=network_manager_alloc_get_mac_string(mac_address);
        ESP_LOGD(TAG,"Assigning mac address %s to ethernet interface", STR_OR_BLANK(mac_string));
        FREE_AND_NULL(mac_string);
        esp_eth_ioctl(network_driver->handle, ETH_CMD_S_MAC_ADDR, mac_address);
    }    
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Attaching ethernet to network interface");
        err = esp_netif_attach(eth_netif, esp_eth_new_netif_glue(network_driver->handle));
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Starting ethernet network");
        err = esp_eth_start(network_driver->handle);

    }
    if (err != ESP_OK) {
        messaging_post_message(MESSAGING_ERROR, MESSAGING_CLASS_SYSTEM, "Configuring Ethernet failed: %s", esp_err_to_name(err));
        if(spi_handle) {
            spi_bus_remove_device(spi_handle);
        }
        network_driver->handle = NULL;
    }
}

void network_ethernet_start_timer() {
    ETH_timer = xTimerCreate("ETH check", pdMS_TO_TICKS(ETH_TIMEOUT_MS), pdFALSE, NULL, ETH_Timeout);
}

/** Event handler for Ethernet events */
static void eth_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    if (event_base == ETH_EVENT) {
        esp_eth_handle_t eth_handle = *(esp_eth_handle_t*)event_data;
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                xEventGroupSetBits(ethernet_event_group, LINK_UP_BIT);
                esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
                ESP_LOGI(TAG, "");
                ESP_LOGI(TAG, "Ethernet Link Up, HW Addr %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
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

static void ETH_Timeout(void* timer_id) {
    network_async_fail();
}

