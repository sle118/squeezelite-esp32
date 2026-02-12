/*
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include "Config.h"
#include "accessors.h"
#include "audio_controls.h"
#include "cmd_system.h"
#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "gds_draw.h"
#include "gds_font.h"
#include "gds_text.h"
#include "led.h"
#include "led_vu.h"
#include "mdns.h"
#include "messaging.h"
#include "network_manager.h"
#include "platform_esp32.h"
#include "squeezelite-ota.h"
#include "telnet.h"
#include "tools.h"
#include "trace.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bootstate.h"
#include "platform_console.h"
#include "system_time.h"

#if defined(CONFIG_WITH_METRICS)
#include "Metrics.h"
#endif
#ifdef CONFIG_HEAP_TRACING
#include "esp_heap_trace.h"
#endif

#define JOIN_TIMEOUT_MS (10000)
#define LOCAL_MAC_SIZE 20
static const char TAG[] = "esp_app_main";
#define DEFAULT_HOST_NAME "squeezelite"

#ifdef CONFIG_IDF_TARGET_ESP32S3
extern const char _ctype_[];
const char* __ctype_ptr__ = _ctype_;
#endif

// as an exception _init function don't need include
extern void services_init(void);
extern void services_ports_init(void);
extern void services_gpio_init(void);
extern void services_sleep_init(void);
extern void display_init(char* welcome);
extern void led_vu_init(void);
extern void target_init(char* target);
extern void start_squeezelite();

void cb_connection_got_ip(nm_state_t new_state, int sub_state) {
    const char* hostname;
    static ip4_addr_t ip;
    tcpip_adapter_ip_info_t ipInfo;
    network_get_ip_info(&ipInfo);
    if(ip.addr && ipInfo.ip.addr != ip.addr) {
        config_raise_changed(false);
        ESP_LOGW(TAG, "IP change, need to reboot");
        simple_restart();
    }
    system_time_init();
    // initializing mDNS
    network_get_hostname(&hostname);
    mdns_init();
    mdns_hostname_set(hostname);

    ESP_LOGI(TAG, "Network connected and mDNS initialized with %s", hostname);
    messaging_post_message(MESSAGING_INFO, MESSAGING_CLASS_SYSTEM, "Network connected");

    led_unpush(LED_GREEN);
    if(is_recovery_running) {
        // when running in recovery, send a LMS discovery message
        // to find a running instance. This is to enable using
        // the plugin's proxy mode for FW download and avoid
        // expired certificate issues.
        discover_ota_server(5);
    }
}
void cb_connection_sta_disconnected(nm_state_t new_state, int sub_state) {
    led_blink_pushed(LED_GREEN, 250, 250);
    messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_SYSTEM, "Wifi disconnected");
}

void handle_ap_connect(nm_state_t new_state, int sub_state) { start_telnet(NULL); }
void handle_network_up(nm_state_t new_state, int sub_state) { bootstate_uptate_counter(0); }
void initialize_network() {

    ESP_LOGI(TAG, "Starting Network Manager");
    TRACE_START
    network_initialize_task();

    // register main
    network_register_state_callback(NETWORK_WIFI_ACTIVE_STATE, WIFI_CONNECTED_STATE, "cb_connection_got_ip", &cb_connection_got_ip);
    network_register_state_callback(NETWORK_ETH_ACTIVE_STATE, ETH_ACTIVE_CONNECTED_STATE, "cb_connection_got_ip", &cb_connection_got_ip);

    // OTA Processing should happen when a network IP address is received
    network_register_state_callback(NETWORK_WIFI_ACTIVE_STATE, WIFI_CONNECTED_STATE, "cb_handle_ota", &cb_handle_ota);
    network_register_state_callback(NETWORK_ETH_ACTIVE_STATE, ETH_ACTIVE_CONNECTED_STATE, "cb_handle_ota", &cb_handle_ota);

    network_register_state_callback(
        NETWORK_WIFI_ACTIVE_STATE, WIFI_LOST_CONNECTION_STATE, "cb_connection_sta_disconnected", &cb_connection_sta_disconnected);

    /* Start the telnet service after we are certain that the network stack has been properly
     * initialized. This can be either after we're started the AP mode, or after we've started
     * the STA mode  */
    network_register_state_callback(NETWORK_INITIALIZING_STATE, -1, "handle_ap_connect", &handle_ap_connect);
    network_register_state_callback(NETWORK_ETH_ACTIVE_STATE, ETH_ACTIVE_LINKDOWN_STATE, "handle_network_up", &handle_network_up);
    network_register_state_callback(NETWORK_WIFI_ACTIVE_STATE, WIFI_INITIALIZING_STATE, "handle_network_up", &handle_network_up);
    TRACE_STOP;
}
void app_main() {
    /* initialize console here, as it will capture logs earlier */
    initialize_console();

    ESP_LOGI(TAG, "Setting log level to max. System compiled with max level %s", get_log_level_desc(CONFIG_LOG_MAXIMUM_LEVEL));
    // calling this will set the log level to verbose,
    // but in reality, the actual log level used is limited
    // by CONFIG_LOG_MAXIMUM_LEVEL in sdkconfig as well as
    // LOG_LOCAL_LEVEL of each component
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    messaging_service_init();

    bootstate_handle_boot();
    TRACE_INIT;
    ESP_LOGI(TAG, "Starting app_main");
    init_spiffs();
    if(esp_log_level_get(TAG) >= ESP_LOG_DEBUG) { listFiles("/spiffs"); }

    // now that the spiffs is initialized, we can load the
    // core configuration system, which is needed by
    // most of the other system components
    config_load();
    if(!platform) {
        ESP_LOGE(TAG, "Invalid configuration object. Cannot continue");
        config_erase_config();
    }

    // initialize SPI/I2C and gpios/gpio expansion modules
    // now as they may be required for hardware access by
    // ethernet, etc. later
    services_ports_init();
    services_gpio_init();

    // Start the network task at this point; this is critical
    // as various subsequent init steps will post events to the queue
    initialize_network();

#if defined(CONFIG_WITH_METRICS)
    ESP_LOGI(TAG, "Setting up metrics.");
    metrics_init();
#endif
    init_telnet(); // align on 32 bits boundaries
    services_init();
    display_init("SqueezeESP32");

    if(platform->target && strlen(platform->target) > 0) { target_init(platform->target); }
    led_vu_init();
    if(is_recovery_running) {
        if(display) {
            ESP_LOGD(TAG, "Writing to display");
            GDS_ClearExt(display, true);
            GDS_SetFont(display, Font_line_2);
            GDS_TextPos(display, GDS_FONT_DEFAULT, GDS_TEXT_CENTERED, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, "RECOVERY");
        }
        if(led_display) { led_vu_color_yellow(LED_VU_BRIGHT); }
    }
#if defined(CONFIG_WITH_METRICS)
    metrics_event_boot(is_recovery_running ? "recovery" : "ota");
#endif

    if(!is_recovery_running) {
        ESP_LOGD(TAG, "Getting audio control mapping ");
        if(platform && platform->has_dev && platform->dev.root_button_profile && strlen(platform->dev.root_button_profile) > 0) {
            ESP_LOGD(TAG, "Initializing audio control buttons type %s", platform->dev.root_button_profile);
            if(actrls_init(platform->dev.root_button_profile) == ESP_OK) {
                ESP_LOGD(TAG, "Done Initializing audio control buttons type %s", platform->dev.root_button_profile);
            } else {
                ESP_LOGD(TAG, "No audio control buttons");
            }
        }
    }

    led_blink_pushed(LED_GREEN, 250, 250);
    if(!is_recovery_running) { start_squeezelite(); }
    console_start();
    services_sleep_init();
    messaging_post_message(MESSAGING_INFO, MESSAGING_CLASS_SYSTEM, "System started");
#ifdef CONFIG_HEAP_TRACING

#endif
}
