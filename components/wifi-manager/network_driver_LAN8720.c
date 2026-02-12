#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "network_ethernet.h"

static EXT_RAM_ATTR network_ethernet_driver_t LAN8720;
static EXT_RAM_ATTR esp_netif_config_t cfg_rmii;
static EXT_RAM_ATTR esp_netif_inherent_config_t esp_netif_config;
static EXT_RAM_ATTR gpio_num_t rst = -1;
static esp_err_t reset_hw(esp_eth_phy_t* phy) {
    // set reset_gpio_num to a negative value can skip hardware reset phy chip
    if(rst >= 0) {
        esp_rom_gpio_pad_select_gpio_x(rst);
        gpio_set_direction_x(rst, GPIO_MODE_OUTPUT);
        gpio_set_level_x(rst, 0);
        /* assert nRST signal on LAN87xx a little longer than the minimum specified in datasheet */
        esp_rom_delay_us(150);
        gpio_set_level_x(rst, 1);
    }
    return ESP_OK;
}
static esp_err_t start(spi_device_handle_t spi_handle, sys_dev_eth_config* ethernet_config) {
#ifdef CONFIG_ETH_PHY_INTERFACE_RMII
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    (void)spi_handle;

    esp32_emac_config.smi_gpio.mdc_num = ethernet_config->ethType.rmii.mdc;
    esp32_emac_config.smi_gpio.mdio_num = ethernet_config->ethType.rmii.mdio;
    phy_config.phy_addr = 1;
    phy_config.reset_gpio_num = ethernet_config->common.rst;
    rst = phy_config.reset_gpio_num;

    esp_eth_mac_t* mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    esp_eth_phy_t* phy = esp_eth_phy_new_lan87xx(&phy_config);
    phy->reset_hw = reset_hw;
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    return esp_eth_driver_install(&config, &LAN8720.handle);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void init_config(sys_dev_eth_config* ethernet_config) {
    esp_netif_inherent_config_t loc_esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    memcpy(&esp_netif_config, &loc_esp_netif_config, sizeof(loc_esp_netif_config));

    cfg_rmii.base = &esp_netif_config, cfg_rmii.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;

    LAN8720.cfg_netif = &cfg_rmii;
    LAN8720.start = start;
}

network_ethernet_driver_t* LAN8720_Detect(sys_dev_eth_config* ethernet_config) {
    if(ethernet_config->common.model != sys_dev_eth_models_LAN8720 || ethernet_config->which_ethType != sys_dev_eth_config_rmii_tag) return NULL;
#ifdef CONFIG_ETH_PHY_INTERFACE_RMII
    LAN8720.valid = true;
#else
    LAN8720.valid = false;
#endif
    LAN8720.rmii = true;
    LAN8720.spi = false;
    LAN8720.model = ethernet_config->common.model;
    LAN8720.init_config = init_config;
    return &LAN8720;
}
