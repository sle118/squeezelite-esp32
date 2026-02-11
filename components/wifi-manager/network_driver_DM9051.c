#include "esp_eth.h"
#include "network_ethernet.h"

static EXT_RAM_ATTR network_ethernet_driver_t DM9051;
static EXT_RAM_ATTR spi_device_interface_config_t devcfg;
static EXT_RAM_ATTR esp_netif_config_t cfg_spi;
static EXT_RAM_ATTR esp_netif_inherent_config_t esp_netif_config;
static EXT_RAM_ATTR gpio_num_t rst = -1;
static esp_err_t reset_hw(esp_eth_phy_t *phy)
{
    // set reset_gpio_num to a negative value can skip hardware reset phy chip
    if (rst >= 0) {
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
#ifdef CONFIG_ETH_SPI_ETHERNET_DM9051
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    spi_host_device_t spi_host = ethernet_config->ethType.spi.host - sys_dev_common_hosts_Host0;
    eth_dm9051_config_t eth_config = ETH_DM9051_DEFAULT_CONFIG(spi_host, &devcfg);
    (void)spi_handle;
    // we assume that isr has been installed already
    eth_config.int_gpio_num = ethernet_config->ethType.spi.intr;
    phy_config.phy_addr = -1;
    phy_config.reset_gpio_num = ethernet_config->common.rst;
    rst = phy_config.reset_gpio_num;
    esp_eth_mac_t* mac = esp_eth_mac_new_dm9051(&eth_config, &mac_config);
    esp_eth_phy_t* phy = esp_eth_phy_new_dm9051(&phy_config);
    phy->reset_hw = reset_hw;
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    return esp_eth_driver_install(&config, &DM9051.handle);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void init_config(sys_dev_eth_config* ethernet_config) {
	esp_netif_inherent_config_t loc_esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    devcfg.command_bits = 1;  
    devcfg.address_bits = 7;  
    devcfg.mode = 0;
    devcfg.clock_speed_hz = ethernet_config->ethType.spi.speed > 0 ? ethernet_config->ethType.spi.speed : SPI_MASTER_FREQ_20M;  // default speed
    devcfg.queue_size = 20;
    devcfg.spics_io_num = ethernet_config->ethType.spi.cs;
    memcpy(&esp_netif_config, &loc_esp_netif_config, sizeof(loc_esp_netif_config));
    cfg_spi.base = &esp_netif_config,
    cfg_spi.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
    DM9051.cfg_netif = &cfg_spi;
    DM9051.devcfg = &devcfg;
    DM9051.start = start;
}

network_ethernet_driver_t* DM9051_Detect(sys_dev_eth_config* ethernet_config) {
    if (ethernet_config->common.model != sys_dev_eth_models_DM9051 ||
        ethernet_config->which_ethType != sys_dev_eth_config_spi_tag )
        return NULL;
    DM9051.rmii = false;
    DM9051.spi = true;
#ifdef CONFIG_ETH_SPI_ETHERNET_DM9051
    DM9051.valid = true;
#else
    DM9051.valid = false;
#endif        
    DM9051.init_config = init_config;
    DM9051.model = ethernet_config->common.model;
    return &DM9051;
}
