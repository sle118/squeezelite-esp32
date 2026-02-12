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

#include "network_manager.h"
#include "accessors.h"
#include <string.h>
#include "esp_netif_defaults.h"
#include "Config.h"
#include "esp_rom_gpio.h"
#ifdef __cplusplus
extern "C" {

#endif

typedef struct {
    bool valid;
    bool rmii;
    bool spi;
    sys_dev_eth_models model;
    esp_eth_handle_t handle;
    esp_netif_config_t* cfg_netif;
    spi_device_interface_config_t* devcfg;
    // This function is called when the network interface is started
    // and performs any initialization that requires a valid ethernet
    // configuration .
    void (*init_config)(sys_dev_eth_config* config);
    esp_err_t (*start)(spi_device_handle_t spi_handle, sys_dev_eth_config* config);
} network_ethernet_driver_t;
typedef network_ethernet_driver_t* network_ethernet_detect_func_t(sys_dev_eth_config* config);
network_ethernet_driver_t* network_ethernet_driver_autodetect();
void destroy_network_ethernet();
void init_network_ethernet();
bool network_ethernet_wait_for_link(uint16_t max_wait_ms);

void network_ethernet_start_timer();
bool network_ethernet_is_up();
bool network_ethernet_enabled();
esp_netif_t* network_ethernet_get_interface();
#ifdef __cplusplus
}

#endif