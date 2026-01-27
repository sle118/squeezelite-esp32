#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "nvs.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_pthread.h"
#include "esp_system.h"
#include "platform_config.h"
#include "audio_controls.h"
#include "display.h"
#include "accessors.h"
#include "network_services.h"
#include "http_server_handlers.h"
#include "tools.h"
#include "cspot_private.h"
#include "cspot_sink.h"

char EXT_RAM_ATTR deviceId[16];

static EXT_RAM_ATTR struct cspot_cb_s {
	cspot_cmd_vcb_t cmd;
	cspot_data_cb_t data;
} cspot_cbs;

static const char TAG[] = "cspot";
static struct cspot_s *cspot;
static cspot_cmd_vcb_t cmd_handler_chain;

static void cspot_volume_up(bool pressed) {
	if (!pressed) return;
	cspot_cmd(cspot, CSPOT_VOLUME_UP, NULL);
	ESP_LOGI(TAG, "CSpot volume up");
}

static void cspot_volume_down(bool pressed) {
	if (!pressed) return;
	cspot_cmd(cspot, CSPOT_VOLUME_DOWN, NULL);
	ESP_LOGI(TAG, "CSpot volume down");
}

static void cspot_toggle(bool pressed) {
	if (!pressed) return;
	cspot_cmd(cspot, CSPOT_TOGGLE, NULL);
	ESP_LOGI(TAG, "CSpot play/pause");
}

static void cspot_pause(bool pressed) {
	if (!pressed) return;
	cspot_cmd(cspot, CSPOT_PAUSE, NULL);
	ESP_LOGI(TAG, "CSpot pause");
}

static void cspot_play(bool pressed) {
	if (!pressed) return;
	cspot_cmd(cspot, CSPOT_PLAY, NULL);
	ESP_LOGI(TAG, "CSpot play");
}

static void cspot_stop(bool pressed) {
	if (!pressed) return;
	cspot_cmd(cspot, CSPOT_STOP, NULL);
	ESP_LOGI(TAG, "CSpot stop");
}

static void cspot_prev(bool pressed) {
	if (!pressed) return;
	cspot_cmd(cspot, CSPOT_PREV, NULL);
	ESP_LOGI(TAG, "CSpot previous");
}

static void cspot_next(bool pressed) {
	if (!pressed) return;
	cspot_cmd(cspot, CSPOT_NEXT, NULL);
	ESP_LOGI(TAG, "CSpot next");
}

const static actrls_t controls = {
	NULL,								// power
	cspot_volume_up, cspot_volume_down,	// volume up, volume down
	cspot_toggle, cspot_play,			// toggle, play
	cspot_pause, cspot_stop,			// pause, stop
	NULL, NULL,							// rew, fwd
	cspot_prev, cspot_next,				// prev, next
	NULL, NULL, NULL, NULL, // left, right, up, down
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, // pre1-10
	cspot_volume_down, cspot_volume_up, cspot_toggle// knob left, knob_right, knob push
};

/****************************************************************************************
 * Download callback
 */
void got_artwork(uint8_t* data, size_t len, void *context) {
	if (data) {
		ESP_LOGI(TAG, "got artwork of %zu bytes", len);
		displayer_artwork(data);
		free(data);
	} else {
		ESP_LOGW(TAG, "artwork error or too large %zu", len);
	}
}

/****************************************************************************************
 * Command handler
 */
static bool cmd_handler(cspot_event_t event, ...) {
	va_list args;	

	va_start(args, event);
	
	// handle audio event and stop if forbidden
	if (!cmd_handler_chain(event, args)) {
		va_end(args);
		return false;
	}

	// now handle events for display
	switch(event) {
	case CSPOT_START:
		actrls_set(controls, false, NULL, actrls_ir_action);
		displayer_control(DISPLAYER_ACTIVATE, "SPOTIFY", true);
		break;
	case CSPOT_PLAY:
		displayer_control(DISPLAYER_TIMER_RUN);
		break;		
	case CSPOT_PAUSE:
		displayer_control(DISPLAYER_TIMER_PAUSE);
		break;		
	case CSPOT_DISC:
		actrls_unset();
		displayer_control(DISPLAYER_SUSPEND);
		break;
	case CSPOT_SEEK:
		displayer_timer(DISPLAYER_ELAPSED, va_arg(args, int), -1);
		break;
	case CSPOT_TRACK_INFO: {
		uint32_t duration = va_arg(args, int), offset = va_arg(args, int);
		char *artist = va_arg(args, char*), *album = va_arg(args, char*), *title = va_arg(args, char*), *artwork = va_arg(args, char*);
		if (artwork && displayer_can_artwork()) {
			ESP_LOGI(TAG, "requesting artwork %s", artwork);
			http_download(artwork, 128*1024, got_artwork, NULL);
		}	
		displayer_metadata(artist, album, title);
		displayer_timer(DISPLAYER_ELAPSED, offset, duration);
		break;
	}	
	// nothing to do on CSPOT_FLUSH
	default: 
		break;
	}
	
	va_end(args);
	
	return true;
}

/****************************************************************************************
 * CSpot sink startup
 */
static void cspot_sink_start(nm_state_t state_id, int sub_state) {
    const char *hostname;

	cmd_handler_chain = cspot_cbs.cmd;
	network_get_hostname(&hostname);
	
	ESP_LOGI(TAG, "starting Spotify on host %s", hostname);
    
    int port;
    httpd_handle_t server = http_get_server(&port);
    
	cspot = cspot_create(hostname, server, port, cmd_handler, cspot_cbs.data);
}

/****************************************************************************************
 * CSpot sink initialization
 */
void cspot_sink_init(cspot_cmd_vcb_t cmd_cb, cspot_data_cb_t data_cb) {
	cspot_cbs.cmd = cmd_cb;
	cspot_cbs.data = data_cb;

	network_register_state_callback(NETWORK_WIFI_ACTIVE_STATE, WIFI_CONNECTED_STATE, "cspot_sink_start", cspot_sink_start);
	network_register_state_callback(NETWORK_ETH_ACTIVE_STATE, ETH_ACTIVE_CONNECTED_STATE, "cspot_sink_start", cspot_sink_start);
}

/****************************************************************************************
 * CSpot forced disconnection
 */
void cspot_disconnect(void) {
	ESP_LOGI(TAG, "forced disconnection");
	displayer_control(DISPLAYER_SHUTDOWN);
	cspot_cmd(cspot, CSPOT_DISC, NULL);
	actrls_unset();
}
