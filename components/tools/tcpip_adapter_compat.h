#pragma once

#include "esp_netif.h"
#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
typedef enum { TCPIP_ADAPTER_IF_STA = 0, TCPIP_ADAPTER_IF_AP = 1, TCPIP_ADAPTER_IF_ETH = 2, TCPIP_ADAPTER_IF_MAX } tcpip_adapter_if_t;

typedef esp_netif_ip_info_t tcpip_adapter_ip_info_t;
typedef esp_netif_dhcp_status_t tcpip_adapter_dhcp_status_t;

static inline esp_netif_t* tcpip_adapter_get_netif(tcpip_adapter_if_t tcpip_if) {
    switch(tcpip_if) {
    case TCPIP_ADAPTER_IF_STA:
        return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    case TCPIP_ADAPTER_IF_AP:
        return esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    case TCPIP_ADAPTER_IF_ETH:
        return esp_netif_get_handle_from_ifkey("ETH_DEF");
    default:
        return NULL;
    }
}

static inline esp_err_t tcpip_adapter_get_hostname(tcpip_adapter_if_t tcpip_if, const char** hostname) {
    esp_netif_t* netif = tcpip_adapter_get_netif(tcpip_if);
    if(!netif) { return ESP_ERR_INVALID_ARG; }
    return esp_netif_get_hostname(netif, hostname);
}

static inline bool tcpip_adapter_is_netif_up(tcpip_adapter_if_t tcpip_if) {
    esp_netif_t* netif = tcpip_adapter_get_netif(tcpip_if);
    return netif ? esp_netif_is_netif_up(netif) : false;
}

static inline esp_err_t tcpip_adapter_get_ip_info(tcpip_adapter_if_t tcpip_if, tcpip_adapter_ip_info_t* ip_info) {
    esp_netif_t* netif = tcpip_adapter_get_netif(tcpip_if);
    if(!netif) { return ESP_ERR_INVALID_ARG; }
    return esp_netif_get_ip_info(netif, ip_info);
}
#endif
