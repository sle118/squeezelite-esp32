#ifdef NETWORK_HTTP_SERVER_LOG_LEVEL
#define LOG_LOCAL_LEVEL NETWORK_HTTP_SERVER_LOG_LEVEL
#endif
/*
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "sdkconfig.h"
#include "http_server_handlers.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "messaging.h"
#include "platform_esp32.h"
#include "trace.h"
#include "tools.h"
static const char TAG[] = "http_server";

EXT_RAM_ATTR static httpd_handle_t _server;
EXT_RAM_ATTR static int _port;
EXT_RAM_ATTR rest_server_context_t *rest_context;
EXT_RAM_ATTR RingbufHandle_t messaging;

httpd_handle_t http_get_server(int *port) {
	if (port) *port = _port;
	return _server;
}

void register_common_handlers(httpd_handle_t server){
	httpd_uri_t css_get = { .uri = "/css/*", .method = HTTP_GET, .handler = resource_filehandler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &css_get);
	httpd_uri_t js_get = { .uri = "/js/*", .method = HTTP_GET, .handler = resource_filehandler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &js_get);
	httpd_uri_t icon_get = { .uri = "/icons*", .method = HTTP_GET, .handler = resource_filehandler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &icon_get);
	httpd_uri_t png_get = { .uri = "/favicon*", .method = HTTP_GET, .handler = resource_filehandler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &png_get);
}
void register_regular_handlers(httpd_handle_t server){
	httpd_uri_t root_get = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &root_get);

	httpd_uri_t ap_get = { .uri = "/ap.json", .method = HTTP_GET, .handler = ap_get_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &ap_get);
	httpd_uri_t scan_get = { .uri = "/scan.json", .method = HTTP_GET, .handler = ap_scan_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &scan_get);
	httpd_uri_t config_get = { .uri = "/config.json", .method = HTTP_GET, .handler = config_get_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &config_get);
	httpd_uri_t status_get = { .uri = "/status.json", .method = HTTP_GET, .handler = status_get_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &status_get);
	httpd_uri_t messages_get = { .uri = "/messages.json", .method = HTTP_GET, .handler = messages_get_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &messages_get);

	httpd_uri_t commands_get = { .uri = "/commands.json", .method = HTTP_GET, .handler = console_cmd_get_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &commands_get);
	httpd_uri_t commands_post = { .uri = "/commands.json", .method = HTTP_POST, .handler = console_cmd_post_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &commands_post);

	httpd_uri_t config_post = { .uri = "/config.json", .method = HTTP_POST, .handler = config_post_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &config_post);
	httpd_uri_t connect_post = { .uri = "/connect.json", .method = HTTP_POST, .handler = connect_post_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &connect_post);

	httpd_uri_t reboot_ota_post = { .uri = "/reboot_ota.json", .method = HTTP_POST, .handler = reboot_ota_post_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &reboot_ota_post);

	httpd_uri_t reboot_post = { .uri = "/reboot.json", .method = HTTP_POST, .handler = reboot_post_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &reboot_post);

	httpd_uri_t recovery_post = { .uri = "/recovery.json", .method = HTTP_POST, .handler = recovery_post_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &recovery_post);

	httpd_uri_t connect_delete = { .uri = "/connect.json", .method = HTTP_DELETE, .handler = connect_delete_handler, .user_ctx = rest_context };
	httpd_register_uri_handler(server, &connect_delete);

	if(is_recovery_running){
		httpd_uri_t flash_post = { .uri = "/flash.json", .method = HTTP_POST, .handler = flash_post_handler, .user_ctx = rest_context };
		httpd_register_uri_handler(server, &flash_post);
	}
	// from https://github.com/tripflex/wifi-captive-portal/blob/master/src/mgos_wifi_captive_portal.c
	// https://unix.stackexchange.com/questions/432190/why-isnt-androids-captive-portal-detection-triggering-a-browser-window
	 // Known HTTP GET requests to check for Captive Portal

	///kindle-wifi/wifiredirect.html Kindle when requested with com.android.captiveportallogin
	///kindle-wifi/wifistub.html Kindle before requesting with captive portal login window (maybe for detection?)


	httpd_uri_t connect_redirect_1 = { .uri = "/mobile/status.php", .method = HTTP_GET, .handler = redirect_200_ev_handler, .user_ctx = rest_context };// Android 8.0 (Samsung s9+)
	httpd_register_uri_handler(server, &connect_redirect_1);
	httpd_uri_t connect_redirect_2 = { .uri = "/generate_204", .method = HTTP_GET, .handler = redirect_200_ev_handler, .user_ctx = rest_context };// Android
	httpd_register_uri_handler(server, &connect_redirect_2);
	httpd_uri_t connect_redirect_3 = { .uri = "/gen_204", .method = HTTP_GET, .handler = redirect_ev_handler, .user_ctx = rest_context };// Android 9.0
	httpd_register_uri_handler(server, &connect_redirect_3);
//	httpd_uri_t connect_redirect_4 = { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = redirect_ev_handler, .user_ctx = rest_context };// Windows
//	httpd_register_uri_handler(server, &connect_redirect_4);
	httpd_uri_t connect_redirect_5 = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = redirect_ev_handler, .user_ctx = rest_context }; // iOS 8/9
	httpd_register_uri_handler(server, &connect_redirect_5);
	httpd_uri_t connect_redirect_6 = { .uri = "/library/test/success.html", .method = HTTP_GET, .handler = redirect_ev_handler, .user_ctx = rest_context };// iOS 8/9
	httpd_register_uri_handler(server, &connect_redirect_6);
	httpd_uri_t connect_redirect_7 = { .uri = "/hotspotdetect.html", .method = HTTP_GET, .handler = redirect_ev_handler, .user_ctx = rest_context }; // iOS
	httpd_register_uri_handler(server, &connect_redirect_7);
	httpd_uri_t connect_redirect_8 = { .uri = "/success.txt", .method = HTTP_GET, .handler = redirect_ev_handler, .user_ctx = rest_context }; // OSX
	httpd_register_uri_handler(server, &connect_redirect_8);



	ESP_LOGD(TAG,"Registering default error handler for 404");
	httpd_register_err_handler(server, HTTPD_404_NOT_FOUND,&err_handler);

}

#define EMBEDDED 1
#define LINKALL 1
#define LOOPBACK 1
#include "squeezelite.h"

// --- Metrics Implementation ---
#ifdef CONFIG_SQUEEZELITE_ESP32_METRICS

extern struct outputstate output;
extern struct buffer *outputbuf;
extern struct buffer *streambuf;
extern volatile uint32_t total_underruns;
extern volatile int cached_rssi;
extern volatile uint32_t wifi_disconnects;

static esp_err_t metrics_get_handler(httpd_req_t *req)
{
    const size_t resp_size = 4096;
    char *resp_str = malloc(resp_size); // Allocate buffer for response
    if (!resp_str) return ESP_FAIL;

    char *p = resp_str;
    size_t rem = resp_size;
    int n;

    // Snapshot Output Buffer Pointers
    uint8_t *ob_readp = NULL, *ob_writep = NULL;
    size_t ob_size = 0;
    if (outputbuf) {
        ob_readp = outputbuf->readp;
        ob_writep = outputbuf->writep;
        ob_size = outputbuf->size;
    }

    // Calculate Output Buffer Fill
    unsigned output_bytes = 0;
    if (outputbuf && outputbuf->buf) {
        if (ob_writep >= ob_readp) {
            output_bytes = ob_writep - ob_readp;
        } else {
            output_bytes = ob_size - (ob_readp - ob_writep);
        }
    }
    // Use u64 for calculation to prevent overflow before division and ensure precision
    unsigned output_fill = (ob_size > 0) ? (unsigned)(((uint64_t)output_bytes * 100) / ob_size) : 0;

    // Snapshot Stream Buffer Pointers
    uint8_t *sb_readp = NULL, *sb_writep = NULL;
    size_t sb_size = 0;
    if (streambuf) {
        sb_readp = streambuf->readp;
        sb_writep = streambuf->writep;
        sb_size = streambuf->size;
    }

    // Calculate Stream Buffer Fill
    unsigned stream_bytes = 0;
    if (streambuf && streambuf->buf) {
        if (sb_writep >= sb_readp) {
            stream_bytes = sb_writep - sb_readp;
        } else {
            stream_bytes = sb_size - (sb_readp - sb_writep);
        }
    }
    unsigned stream_fill = (sb_size > 0) ? (unsigned)(((uint64_t)stream_bytes * 100) / sb_size) : 0;

    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    uint32_t free_heap = esp_get_free_heap_size();
    int64_t uptime_sec = esp_timer_get_time() / 1000000;

    // Safe buffer writing loop
    do {
        n = snprintf(p, rem, "# HELP squeezelite_output_state Decoder state (-1=OFF, 0=STOPPED, 1=BUFFER, 2=RUNNING, 3=PAUSE, 4=SKIP, 5=START_AT)\n# TYPE squeezelite_output_state gauge\nsqueezelite_output_state %d\n", output.state);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP squeezelite_frames_decoded_total Total frames decoded\n# TYPE squeezelite_frames_decoded_total counter\nsqueezelite_frames_decoded_total %u\n", output.frames_played_dmp);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP squeezelite_underruns_total Total I2S underruns/overflows\n# TYPE squeezelite_underruns_total counter\nsqueezelite_underruns_total %u\n", total_underruns);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP squeezelite_buffer_fill_percent Audio buffer fill level (0-100)\n# TYPE squeezelite_buffer_fill_percent gauge\nsqueezelite_buffer_fill_percent %u\n", output_fill);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP squeezelite_buffer_bytes_used Audio buffer used bytes\n# TYPE squeezelite_buffer_bytes_used gauge\nsqueezelite_buffer_bytes_used %u\n", output_bytes);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP squeezelite_buffer_size_bytes Audio buffer total size in bytes\n# TYPE squeezelite_buffer_size_bytes gauge\nsqueezelite_buffer_size_bytes %u\n", (unsigned)ob_size);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP squeezelite_stream_buffer_fill_percent Stream buffer fill level (0-100)\n# TYPE squeezelite_stream_buffer_fill_percent gauge\nsqueezelite_stream_buffer_fill_percent %u\n", stream_fill);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP squeezelite_stream_buffer_bytes_used Stream buffer used bytes\n# TYPE squeezelite_stream_buffer_bytes_used gauge\nsqueezelite_stream_buffer_bytes_used %u\n", stream_bytes);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP squeezelite_stream_buffer_size_bytes Stream buffer total size in bytes\n# TYPE squeezelite_stream_buffer_size_bytes gauge\nsqueezelite_stream_buffer_size_bytes %u\n", (unsigned)sb_size);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP esp32_wifi_rssi WiFi Signal Strength (dBm)\n# TYPE esp32_wifi_rssi gauge\nesp32_wifi_rssi %d\n", cached_rssi);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP esp32_wifi_disconnects_total Total WiFi disconnections\n# TYPE esp32_wifi_disconnects_total counter\nesp32_wifi_disconnects_total %u\n", wifi_disconnects);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP esp32_min_free_heap_bytes Minimum free heap size since boot\n# TYPE esp32_min_free_heap_bytes gauge\nesp32_min_free_heap_bytes %u\n", min_free_heap);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP esp32_free_heap_bytes Current free heap size\n# TYPE esp32_free_heap_bytes gauge\nesp32_free_heap_bytes %u\n", free_heap);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# HELP esp32_uptime_seconds System uptime in seconds\n# TYPE esp32_uptime_seconds gauge\nesp32_uptime_seconds %lld\n", uptime_sec);
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

        n = snprintf(p, rem, "# EOF\n");
        if (n < 0 || n >= rem) break;
        p += n; rem -= n;

    } while (0);

    httpd_resp_set_type(req, "application/openmetrics-text; version=1.0.0; charset=utf-8");
    httpd_resp_send(req, resp_str, strlen(resp_str));

    free(resp_str);
    return ESP_OK;
}
#endif
esp_err_t http_server_start()
{

	ESP_LOGI(TAG, "Initializing HTTP Server");
	MEMTRACE_PRINT_DELTA_MESSAGE("Registering messaging subscriber");
	messaging = messaging_register_subscriber(10, "http_server");
	MEMTRACE_PRINT_DELTA_MESSAGE("Allocating RAM for server context");
    rest_context = malloc_init_external( sizeof(rest_server_context_t));
    if(rest_context==NULL){
    	ESP_LOGE(TAG,"No memory for http context");
    	return ESP_FAIL;
    }

    strlcpy(rest_context->base_path, "/res/", sizeof(rest_context->base_path));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 30;
    config.max_open_sockets = 3;
	config.lru_purge_enable = true;
	config.backlog_conn = 1;
    config.uri_match_fn = httpd_uri_match_wildcard;
	config.task_priority = ESP_TASK_PRIO_MIN;
	_port = config.server_port;
    //todo:  use the endpoint below to configure session token?
    // config.open_fn

    MEMTRACE_PRINT_DELTA_MESSAGE( "Starting HTTP Server");
    esp_err_t err= httpd_start(&_server, &config);
    if(err != ESP_OK){
    	ESP_LOGE_LOC(TAG,"Start server failed");
    }
    else {
		MEMTRACE_PRINT_DELTA_MESSAGE( "HTTP Server started. Registering common handlers");
    	register_common_handlers(_server);
		MEMTRACE_PRINT_DELTA_MESSAGE("Registering regular handlers");
    	register_regular_handlers(_server);

#ifdef CONFIG_SQUEEZELITE_ESP32_METRICS
        // Register Metrics Handler
        httpd_uri_t metrics_get = { .uri = "/metrics", .method = HTTP_GET, .handler = metrics_get_handler, .user_ctx = rest_context };
	    httpd_register_uri_handler(_server, &metrics_get);
#endif

		MEMTRACE_PRINT_DELTA_MESSAGE("HTTP Server regular handlers registered");
    }

    return err;
}


/* Function to free context */
void adder_free_func(void *ctx)
{
    ESP_LOGD(TAG, "/adder Free Context function called");
    free(ctx);
}


void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}



