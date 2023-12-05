#pragma once

#include "network_manager.h"
#include "accessors.h"
#include <string.h>
#include "esp_netif_defaults.h"
#include "Configurator.h"
#ifdef __cplusplus
extern "C" {

#endif

typedef struct {
    bool valid;
    bool rmii;
    bool spi;
    sys_EthModelEnum model;
    esp_eth_handle_t handle;
    esp_netif_config_t * cfg_netif;
    spi_device_interface_config_t * devcfg;
    // This function is called when the network interface is started
    // and performs any initialization that requires a valid ethernet 
    // configuration .
    void (*init_config)(sys_Eth * config);
    esp_err_t (*start)(spi_device_handle_t spi_handle,sys_Eth * config);
} network_ethernet_driver_t;
typedef network_ethernet_driver_t* network_ethernet_detect_func_t(sys_Eth * config);
network_ethernet_driver_t* network_ethernet_driver_autodetect();
void destroy_network_ethernet();
void init_network_ethernet();
bool network_ethernet_wait_for_link(uint16_t max_wait_ms);

void network_ethernet_start_timer();
bool network_ethernet_is_up();
bool network_ethernet_enabled();
esp_netif_t *network_ethernet_get_interface();
#ifdef __cplusplus
}

#endif