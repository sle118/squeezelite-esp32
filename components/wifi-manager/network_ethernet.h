#pragma once

#include "network_manager.h"
#include "accessors.h"
#include <string.h>
#include "esp_netif_defaults.h"
#ifdef __cplusplus
extern "C" {

#endif

typedef struct {
    bool valid;
    bool rmii;
    bool spi;
    esp_eth_handle_t handle;
    esp_netif_config_t * cfg_netif;
    spi_device_interface_config_t * devcfg;
    // This function is called when the network interface is started
    // and performs any initialization that requires a valid ethernet 
    // configuration .
    void (*init_config)(eth_config_t * eth_config);
    esp_err_t (*start)(spi_device_handle_t spi_handle,eth_config_t *ethernet_config);
} network_ethernet_driver_t;
typedef network_ethernet_driver_t* network_ethernet_detect_func_t(const char* Driver);
network_ethernet_driver_t* network_ethernet_driver_autodetect(const char* Driver);
void destroy_network_ethernet();
void init_network_ethernet();
/* Restart the ESP-IDF Ethernet stack without rebooting the ESP32. */
esp_err_t network_ethernet_recover();
bool network_ethernet_wait_for_link(uint16_t max_wait_ms);

void network_ethernet_start_timer();
bool network_ethernet_is_up();
bool network_ethernet_enabled();
esp_netif_t *network_ethernet_get_interface();
#ifdef __cplusplus
}

#endif