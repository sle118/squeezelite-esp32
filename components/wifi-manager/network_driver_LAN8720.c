#include "esp_eth.h"
#include "network_ethernet.h"

static EXT_RAM_ATTR network_ethernet_driver_t LAN8720;
static EXT_RAM_ATTR esp_netif_config_t cfg_rmii;
static EXT_RAM_ATTR esp_netif_inherent_config_t esp_netif_config;

static esp_err_t start(spi_device_handle_t spi_handle, eth_config_t* ethernet_config) {
#ifdef CONFIG_ETH_PHY_INTERFACE_RMII
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();

    mac_config.smi_mdc_gpio_num = ethernet_config->mdc;
    mac_config.smi_mdio_gpio_num = ethernet_config->mdio;
    phy_config.phy_addr = -1; // As per Espressif demo: Set to -1 to driver find the PHY address automatically.
    phy_config.reset_gpio_num = ethernet_config->rst;

    esp_eth_mac_t* mac = esp_eth_mac_new_esp32(&mac_config);
    esp_eth_phy_t* phy = esp_eth_phy_new_lan8720(&phy_config);
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    return esp_eth_driver_install(&config, &LAN8720.handle);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void init_config(eth_config_t* ethernet_config) {
	esp_netif_inherent_config_t loc_esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    memcpy(&esp_netif_config, &loc_esp_netif_config, sizeof(loc_esp_netif_config));
	
	cfg_rmii.base = &esp_netif_config,
    cfg_rmii.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
	
    LAN8720.cfg_netif = &cfg_rmii;
    LAN8720.start = start;
}

network_ethernet_driver_t* LAN8720_Detect(char* Driver) {
    if (!strcasestr(Driver, "LAN8720"))
        return NULL;
#ifdef CONFIG_ETH_PHY_INTERFACE_RMII
    LAN8720.valid = true;
#else
    LAN8720.valid = false;
#endif        
    LAN8720.rmii = true;
    LAN8720.spi = false;
    LAN8720.init_config = init_config;
    return &LAN8720;
}
