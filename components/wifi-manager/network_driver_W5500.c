#include "esp_eth.h"
#include "network_ethernet.h"

static EXT_RAM_ATTR network_ethernet_driver_t W5500;
static EXT_RAM_ATTR spi_device_interface_config_t devcfg;
static EXT_RAM_ATTR esp_netif_config_t cfg_spi;
static EXT_RAM_ATTR esp_netif_inherent_config_t esp_netif_config;

static esp_err_t start(spi_device_handle_t spi_handle, sys_Eth * ethernet_config) {
#ifdef CONFIG_ETH_SPI_ETHERNET_W5500
    eth_w5500_config_t eth_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    eth_config.int_gpio_num = ethernet_config->ethType.spi.has_intr?ethernet_config->ethType.spi.intr.pin:-1;
    phy_config.phy_addr = -1;  // let the system automatically find out the phy address
    phy_config.reset_gpio_num = ethernet_config->common.has_rst?ethernet_config->common.rst.pin:-1;

    esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&eth_config, &mac_config);
    esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phy_config);
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    return esp_eth_driver_install(&config, &W5500.handle);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
static void init_config(sys_Eth * ethernet_config) {
    // This function is called when the network interface is started
    // and performs any initialization that requires a valid ethernet 
    // configuration .
    esp_netif_inherent_config_t loc_esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    devcfg.command_bits = 16;  // Actually it's the address phase in W5500 SPI frame
    devcfg.address_bits = 8;   // Actually it's the control phase in W5500 SPI frame
    devcfg.mode = 0;
    devcfg.clock_speed_hz = ethernet_config->ethType.spi.speed > 0 ? ethernet_config->ethType.spi.speed : SPI_MASTER_FREQ_20M;  // default speed
    devcfg.queue_size = 20;
    devcfg.spics_io_num =  ethernet_config->ethType.spi.has_cs?ethernet_config->ethType.spi.cs.pin:-1;
    memcpy(&esp_netif_config, &loc_esp_netif_config, sizeof(loc_esp_netif_config));
    cfg_spi.base = &esp_netif_config,
    cfg_spi.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
    W5500.cfg_netif = &cfg_spi;
    W5500.devcfg = &devcfg;
    W5500.start = start;

}
network_ethernet_driver_t* W5500_Detect(sys_Eth * ethernet_config) {
    if (ethernet_config->common.model != sys_EthModelEnum_W5500 ||
        ethernet_config->which_ethType != sys_Eth_spi_tag )
        return NULL;
    W5500.init_config = init_config;        
    W5500.spi = true;
    W5500.rmii = false;
    W5500.model = ethernet_config->common.model;
#ifdef CONFIG_ETH_SPI_ETHERNET_W5500
    W5500.valid = true;
#else
    W5500.valid = false;
#endif    
    return &W5500;
}
