
/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include "bt_app_core.h"
#include "bt_app_sink.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "Configurator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tools.h"
#include "audio_controls.h"
#include "sys/lock.h"
#include "display.h"
#include "accessors.h"

// AVRCP used transaction label
#define APP_RC_CT_TL_GET_CAPS            (0)
#define APP_RC_CT_TL_GET_META_DATA       (1)
#define APP_RC_CT_TL_RN_TRACK_CHANGE     (2)
#define APP_RC_CT_TL_RN_PLAYBACK_CHANGE  (3)
#define APP_RC_CT_TL_RN_PLAY_POS_CHANGE  (4)

static const char BT_AV_TAG[] = "BT_AV";
static const char BT_RC_TG_TAG[] = "RCTG";
static const char BT_RC_CT_TAG[] = "RCCT";

#ifndef CONFIG_BT_NAME
#define CONFIG_BT_NAME	"ESP32-BT"
#endif

static char * bt_name = NULL;

static bool (*bt_app_a2d_cmd_cb)(bt_sink_cmd_t cmd, ...);
static void (*bt_app_a2d_data_cb)(const uint8_t *data, uint32_t len);

/* handler for bluetooth stack enabled events */
static void bt_av_hdl_stack_evt(uint16_t event, void *p_param);
/* a2dp event handler */
static void bt_av_hdl_a2d_evt(uint16_t event, void *p_param);
/* avrc CT event handler */
static void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param);
/* avrc TG event handler */
static void bt_av_hdl_avrc_tg_evt(uint16_t event, void *p_param);
static void volume_set_by_local_host(int value, bool is_step);
static void bt_av_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter);

static const char *s_a2d_conn_state_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};
static const char *s_a2d_audio_state_str[] = {"Suspended", "Stopped", "Started"};
static esp_avrc_rn_evt_cap_mask_t s_avrc_peer_rn_cap;

static _lock_t s_volume_lock;
static int s_volume, abs_volume, sink_volume;
static bool s_volume_notify;
static enum { AUDIO_IDLE, AUDIO_CONNECTED, AUDIO_PLAYING } s_audio = AUDIO_IDLE;

static int s_sample_rate;
static int tl;
static bt_cmd_vcb_t cmd_handler_chain;

#define METADATA_LEN 128

static EXT_RAM_ATTR struct {
	char artist[METADATA_LEN + 1];
	char album[METADATA_LEN + 1];
	char title[METADATA_LEN + 1];
	int duration;
	bool updated;
} s_metadata;	

static void bt_volume_up(bool pressed) {
	if (!pressed) return;
	volume_set_by_local_host(+3, true);
	(*bt_app_a2d_cmd_cb)(BT_SINK_VOLUME, s_volume);
	ESP_LOGD(BT_AV_TAG, "BT volume up %u", s_volume);
}

static void bt_volume_down(bool pressed) {
	if (!pressed) return;
	volume_set_by_local_host(-3, true);
	(*bt_app_a2d_cmd_cb)(BT_SINK_VOLUME, s_volume);
}

static void bt_toggle(bool pressed) {
	if (!pressed) return;
	if (s_audio == AUDIO_PLAYING) esp_avrc_ct_send_passthrough_cmd(tl++ & 0x0f, ESP_AVRC_PT_CMD_STOP, ESP_AVRC_PT_CMD_STATE_PRESSED);
	else esp_avrc_ct_send_passthrough_cmd(tl++, ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_PRESSED);
}

static void bt_play(bool pressed) {
	if (!pressed) return;
	esp_avrc_ct_send_passthrough_cmd(tl++ & 0x0f, ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_PRESSED);
}

static void bt_pause(bool pressed) {
	if (!pressed) return;
	esp_avrc_ct_send_passthrough_cmd(tl++ & 0x0f, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STATE_PRESSED);
}

static void bt_stop(bool pressed) {
	if (!pressed) return;
	esp_avrc_ct_send_passthrough_cmd(tl++ & 0x0f, ESP_AVRC_PT_CMD_STOP, ESP_AVRC_PT_CMD_STATE_PRESSED);
}

static void bt_prev(bool pressed) {
	if (!pressed) return;
	esp_avrc_ct_send_passthrough_cmd(tl++ & 0x0f, ESP_AVRC_PT_CMD_BACKWARD, ESP_AVRC_PT_CMD_STATE_PRESSED);
}

static void bt_next(bool pressed) {
	if (!pressed) return;
	esp_avrc_ct_send_passthrough_cmd(tl++ & 0x0f, ESP_AVRC_PT_CMD_FORWARD, ESP_AVRC_PT_CMD_STATE_PRESSED);
}

const static actrls_t controls = {
	NULL, // power
	bt_volume_up, bt_volume_down,	// volume up, volume down
	bt_toggle, bt_play,	// toggle, play
	bt_pause, bt_stop,	// pause, stop
	NULL, NULL,			// rew, fwd
	bt_prev, bt_next,	// prev, next
	NULL, NULL, NULL, NULL, // left, right, up, down
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, // pre1-10
	bt_volume_down, bt_volume_up, bt_toggle// knob left, knob_right, knob push
};

/* disconnection */
void bt_disconnect(void) {
	displayer_control(DISPLAYER_SHUTDOWN);
	if (s_audio == AUDIO_PLAYING) esp_avrc_ct_send_passthrough_cmd(tl++ & 0x0f, ESP_AVRC_PT_CMD_STOP, ESP_AVRC_PT_CMD_STATE_PRESSED);
	actrls_unset();
	ESP_LOGD(BT_AV_TAG, "forced disconnection %d", s_audio);
}

/* update metadata if any */
void update_metadata(bool force) {
	if ((s_metadata.updated || force) && s_audio == AUDIO_PLAYING) {
		(*bt_app_a2d_cmd_cb)(BT_SINK_PROGRESS, -1, s_metadata.duration);
		(*bt_app_a2d_cmd_cb)(BT_SINK_METADATA, s_metadata.artist, s_metadata.album, s_metadata.title);
		s_metadata.updated = false;
	} else s_metadata.updated = force;
}	

/* command handler */
static bool cmd_handler(bt_sink_cmd_t cmd, ...) {
	va_list args;	
	
	va_start(args, cmd);
	
	// handle audio event and stop if forbidden
	if (!cmd_handler_chain(cmd, args)) {
		va_end(args);
		return false;
	}
	
	// now handle events for display
	switch(cmd) {
	case BT_SINK_AUDIO_STARTED:
		displayer_control(DISPLAYER_ACTIVATE, "BLUETOOTH", false);
		break;
	case BT_SINK_AUDIO_STOPPED:
		displayer_control(DISPLAYER_SUSPEND);
		break;		
	case BT_SINK_PLAY:
		displayer_control(DISPLAYER_TIMER_RUN);
		break;		
	case BT_SINK_STOP:		
		 // not sure of difference between pause and stop for displayer 
	case BT_SINK_PAUSE:
		displayer_control(DISPLAYER_TIMER_PAUSE);
		break;		
	case BT_SINK_METADATA: {
		char *artist = va_arg(args, char*), *album = va_arg(args, char*), *title = va_arg(args, char*);
		displayer_metadata(artist, album, title);
		break;
	}	
	case BT_SINK_PROGRESS: {
		int elapsed = va_arg(args, int), duration = va_arg(args, int);
		displayer_timer(DISPLAYER_ELAPSED, elapsed, duration);
		break;
	}	
	default: 
		break;
	}
	
	va_end(args);
	
	return true;
}

/* callback for A2DP sink */
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT: {
        bt_app_work_dispatch(bt_av_hdl_a2d_evt, event, param, sizeof(esp_a2d_cb_param_t), NULL);
        break;
    }
    case ESP_A2D_PROF_STATE_EVT: {
        ESP_LOGI(BT_AV_TAG, "Bluetooth Init complete");
        break;
    }    
    default:
        ESP_LOGE(BT_AV_TAG, "Invalid A2DP event: %d", event);
        break;
    }
}

void bt_app_alloc_meta_buffer(esp_avrc_ct_cb_param_t *param)
{
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(param);
    uint8_t *attr_text = (uint8_t *) malloc (rc->meta_rsp.attr_length + 1);
    memcpy(attr_text, rc->meta_rsp.attr_text, rc->meta_rsp.attr_length);
    attr_text[rc->meta_rsp.attr_length] = 0;

    rc->meta_rsp.attr_text = attr_text;
}

void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_METADATA_RSP_EVT:
        bt_app_alloc_meta_buffer(param);
        /* fall through */
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        bt_app_work_dispatch(bt_av_hdl_avrc_ct_evt, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL);
        break;
    }
    default:
        ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
    case ESP_AVRC_TG_REMOTE_FEATURES_EVT:
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
        bt_app_work_dispatch(bt_av_hdl_avrc_tg_evt, event, param, sizeof(esp_avrc_tg_cb_param_t), NULL);
        break;
    default:
        ESP_LOGE(BT_RC_TG_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

static void bt_av_hdl_a2d_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s evt %d", __func__, event);
    esp_a2d_cb_param_t *a2d = NULL;
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        uint8_t *bda = a2d->conn_stat.remote_bda;
        ESP_LOGD(BT_AV_TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
             s_a2d_conn_state_str[a2d->conn_stat.state], bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
			(*bt_app_a2d_cmd_cb)(BT_SINK_DISCONNECTED);
        } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED){
			abs_volume = -1;
			s_volume = sink_volume;
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
			(*bt_app_a2d_cmd_cb)(BT_SINK_CONNECTED);
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGD(BT_AV_TAG, "A2DP audio state: %s", s_a2d_audio_state_str[a2d->audio_stat.state]);

        if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state) {
			s_audio = AUDIO_CONNECTED;

			// send memorized volume for devices that can't do absolute volume
			(*bt_app_a2d_cmd_cb)(BT_SINK_VOLUME, s_volume);
			
			// verify that we can take control
			if ((*bt_app_a2d_cmd_cb)(BT_SINK_AUDIO_STARTED, s_sample_rate)) {
				
				// if PLAY is sent before AUDIO_STARTED, generate the event here
				s_audio = AUDIO_PLAYING;
				(*bt_app_a2d_cmd_cb)(BT_SINK_PLAY);
				
				// force metadata update
				update_metadata(true);
				
				actrls_set(controls, false, NULL, actrls_ir_action);
			} else {
				// if decoder is busy, stop it (would be better to not ACK this command, but don't know how)
				esp_avrc_ct_send_passthrough_cmd(tl++ & 0x0f, ESP_AVRC_PT_CMD_STOP, ESP_AVRC_PT_CMD_STATE_PRESSED);	
			}	
		} else if (ESP_A2D_AUDIO_STATE_STOPPED == a2d->audio_stat.state ||
				   ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND == a2d->audio_stat.state) {
			(*bt_app_a2d_cmd_cb)(BT_SINK_AUDIO_STOPPED);
			s_audio = AUDIO_IDLE;
			actrls_unset();
		}	
        break;
    }
    case ESP_A2D_AUDIO_CFG_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGD(BT_AV_TAG, "A2DP audio stream configuration, codec type %d", a2d->audio_cfg.mcc.type);
        // for now only SBC stream is supported
        if (a2d->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
            s_sample_rate = 16000;
            char oct0 = a2d->audio_cfg.mcc.cie.sbc[0];
            if (oct0 & (0x01 << 6)) {
                s_sample_rate = 32000;
            } else if (oct0 & (0x01 << 5)) {
                s_sample_rate = 44100;
            } else if (oct0 & (0x01 << 4)) {
                s_sample_rate = 48000;
            }
			(*bt_app_a2d_cmd_cb)(BT_SINK_RATE, s_sample_rate);
            
            ESP_LOGI(BT_AV_TAG, "Configure audio player %x-%x-%x-%x",
                     a2d->audio_cfg.mcc.cie.sbc[0],
                     a2d->audio_cfg.mcc.cie.sbc[1],
                     a2d->audio_cfg.mcc.cie.sbc[2],
                     a2d->audio_cfg.mcc.cie.sbc[3]);
            ESP_LOGI(BT_AV_TAG, "Audio player configured, sample rate=%d", s_sample_rate);
        }
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}

static void bt_av_new_track(void)
{
    // request metadata
    uint8_t attr_mask = ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST | ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_PLAYING_TIME;
    esp_avrc_ct_send_metadata_cmd(APP_RC_CT_TL_GET_META_DATA, attr_mask);

    // register notification if peer support the event_id
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_TRACK_CHANGE)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_TRACK_CHANGE, ESP_AVRC_RN_TRACK_CHANGE, 0);
    }
}

static void bt_av_playback_changed(void)
{
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_PLAY_STATUS_CHANGE)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAYBACK_CHANGE, ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
    }
}

static void bt_av_play_pos_changed(void)
{
	if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_PLAY_POS_CHANGED)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAY_POS_CHANGE, ESP_AVRC_RN_PLAY_POS_CHANGED, 10);
    }
}

static void bt_av_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter)
{
    switch (event_id) {
    case ESP_AVRC_RN_TRACK_CHANGE:
		ESP_LOGD(BT_AV_TAG, "Track changed");
        bt_av_new_track();
		(*bt_app_a2d_cmd_cb)(BT_SINK_PROGRESS, 0, 0);
        break;
    case ESP_AVRC_RN_PLAY_STATUS_CHANGE:
        ESP_LOGD(BT_AV_TAG, "Playback status changed: 0x%x", event_parameter->playback);
		if (s_audio != AUDIO_IDLE) {
			switch (event_parameter->playback) {
			case ESP_AVRC_PLAYBACK_PLAYING:
				// if decoder is busy then stop (would be better to not ACK this command, but don't know how)
				if (s_audio != AUDIO_PLAYING && !(*bt_app_a2d_cmd_cb)(BT_SINK_PLAY)) {
					ESP_LOGW(BT_AV_TAG, "Player busy with another controller");					
					esp_avrc_ct_send_passthrough_cmd(tl++ & 0x0f, ESP_AVRC_PT_CMD_STOP, ESP_AVRC_PT_CMD_STATE_PRESSED);
				} else {
					s_audio = AUDIO_PLAYING;
					update_metadata(false);
				}
				break;		
			case ESP_AVRC_PLAYBACK_PAUSED:
				s_audio = AUDIO_CONNECTED;
				(*bt_app_a2d_cmd_cb)(BT_SINK_PAUSE);
				break;
			case ESP_AVRC_PLAYBACK_STOPPED:
				s_audio = AUDIO_CONNECTED;
				(*bt_app_a2d_cmd_cb)(BT_SINK_PROGRESS, 0, -1);			
				(*bt_app_a2d_cmd_cb)(BT_SINK_STOP);
				break;
			default:
				ESP_LOGW(BT_AV_TAG, "Un-handled event");
				break;
			}	
		} else {
			ESP_LOGW(BT_AV_TAG, "Not yet in BT connected mode: 0x%x", event_parameter->playback);
		}	
        bt_av_playback_changed();
        break;
    case ESP_AVRC_RN_PLAY_POS_CHANGED:
        ESP_LOGD(BT_AV_TAG, "Play position changed: %d (ms)", event_parameter->play_pos);
		(*bt_app_a2d_cmd_cb)(BT_SINK_PROGRESS, event_parameter->play_pos, -1);
        bt_av_play_pos_changed();
        break;
    }
}

static void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_RC_CT_TAG, "%s evt %d", __func__, event);
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(p_param);
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
        uint8_t *bda = rc->conn_stat.remote_bda;
        ESP_LOGD(BT_RC_CT_TAG, "AVRC conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

        if (rc->conn_stat.connected) {
            // get remote supported event_ids of peer AVRCP Target
            esp_avrc_ct_send_get_rn_capabilities_cmd(APP_RC_CT_TL_GET_CAPS);
        } else {
            // clear peer notification capability record
            s_avrc_peer_rn_cap.bits = 0;
        }
        break;
    }
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: {
        ESP_LOGD(BT_RC_CT_TAG, "AVRC passthrough rsp: key_code 0x%x, key_state %d", rc->psth_rsp.key_code, rc->psth_rsp.key_state);
        break;
    }
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        ESP_LOGD(BT_RC_CT_TAG, "AVRC metadata rsp: attribute id 0x%x, %s", rc->meta_rsp.attr_id, rc->meta_rsp.attr_text);
		
		if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_PLAYING_TIME) s_metadata.duration = atoi((char*) rc->meta_rsp.attr_text);
		else if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_TITLE) strncpy(s_metadata.title, (char*) rc->meta_rsp.attr_text, METADATA_LEN);
		else if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_ARTIST) strncpy(s_metadata.artist, (char*) rc->meta_rsp.attr_text, METADATA_LEN);
		else if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_ALBUM) strncpy(s_metadata.album, (char*) rc->meta_rsp.attr_text, METADATA_LEN);
		update_metadata(true);
		
        free(rc->meta_rsp.attr_text);
        break;
    }
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
        ESP_LOGD(BT_RC_CT_TAG, "AVRC event notification: %d", rc->change_ntf.event_id);
        bt_av_notify_evt_handler(rc->change_ntf.event_id, &rc->change_ntf.event_parameter);
        break;
    }
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
        ESP_LOGD(BT_RC_CT_TAG, "AVRC remote features %x, TG features %x", rc->rmt_feats.feat_mask, rc->rmt_feats.tg_feat_flag);
        break;
    }
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        ESP_LOGD(BT_RC_CT_TAG, "remote rn_cap: count %d, bitmask 0x%x", rc->get_rn_caps_rsp.cap_count,
                 rc->get_rn_caps_rsp.evt_set.bits);
        s_avrc_peer_rn_cap.bits = rc->get_rn_caps_rsp.evt_set.bits;
        bt_av_new_track();
        bt_av_playback_changed();
        bt_av_play_pos_changed();
        break;
    }
    default:
        ESP_LOGE(BT_RC_CT_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}

static void volume_set_by_controller(uint8_t volume)
{
	// do not modified state volume
    _lock_acquire(&s_volume_lock);
    s_volume = abs_volume = (volume * 100) / 127;
    _lock_release(&s_volume_lock);
	(*bt_app_a2d_cmd_cb)(BT_SINK_VOLUME, s_volume);
}

static void volume_set_by_local_host(int value, bool is_step)
{
	_lock_acquire(&s_volume_lock);
	s_volume = is_step ? s_volume + value : value;
	if (s_volume > 127) s_volume = 127;
	else if (s_volume < 0) s_volume = 0;	
	if (abs_volume >= 0) abs_volume = s_volume;
	else sink_volume = s_volume;
	_lock_release(&s_volume_lock);
    if(sys_state->bt_sink_volume != s_volume){
        sys_state->bt_sink_volume = s_volume;
        configurator_raise_state_changed();
    }
	
    if (s_volume_notify) {
        esp_avrc_rn_param_t rn_param;
        rn_param.volume = s_volume;
        esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_CHANGED, &rn_param);
        s_volume_notify = false;
    }
}

static void bt_av_hdl_avrc_tg_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_RC_TG_TAG, "%s evt %d", __func__, event);
    esp_avrc_tg_cb_param_t *rc = (esp_avrc_tg_cb_param_t *)(p_param);
    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT: {
        uint8_t *bda = rc->conn_stat.remote_bda;
        ESP_LOGD(BT_RC_TG_TAG, "AVRC conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        break;
    }
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT: {
        ESP_LOGD(BT_RC_TG_TAG, "AVRC passthrough cmd: key_code 0x%x, key_state %d", rc->psth_cmd.key_code, rc->psth_cmd.key_state);
        break;
    }
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT: {
        ESP_LOGD(BT_RC_TG_TAG, "AVRC set absolute volume: %d%%", (rc->set_abs_vol.volume * 100) / 127);
        volume_set_by_controller(rc->set_abs_vol.volume);
        break;
    }
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT: {
        ESP_LOGD(BT_RC_TG_TAG, "AVRC register event notification: %d, param: 0x%x", rc->reg_ntf.event_id, rc->reg_ntf.event_parameter);
        if (rc->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
            s_volume_notify = true;
            esp_avrc_rn_param_t rn_param;
            rn_param.volume = s_volume;
            esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &rn_param);
        }
        break;
    }
    case ESP_AVRC_TG_REMOTE_FEATURES_EVT: {
        ESP_LOGD(BT_RC_TG_TAG, "AVRC remote features %x, CT features %x", rc->rmt_feats.feat_mask, rc->rmt_feats.ct_feat_flag);
        break;
    }
    default:
        ESP_LOGE(BT_RC_TG_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}

void bt_sink_init(bt_cmd_vcb_t cmd_cb, bt_data_cb_t data_cb)
{
	bt_app_a2d_cmd_cb = cmd_handler;
	cmd_handler_chain = cmd_cb;
  	bt_app_a2d_data_cb = data_cb;
    sys_BluetoothSink * bt_sink;
    if(!SYS_SERVICES_BTSINK(bt_sink)){
        return;
    }
    char pin_code[ESP_BT_PIN_CODE_LEN+1] = "1234\0";
	
	// create task and run event loop
    bt_app_task_start_up(bt_av_hdl_stack_evt);
	
	sink_volume = sys_state->bt_sink_volume;
	
    /*
     * Set default parameters for Legacy Pairing
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    strncpy(pin_code,bt_sink->pin,sizeof(pin_code));
    if(SYS_SERVICES_BTSINK(bt_sink) && strlen(bt_sink->pin)>ESP_BT_PIN_CODE_LEN){

    	ESP_LOGW(BT_AV_TAG, "BT Sink pin code [%s] too long. ", bt_sink->pin);
    	pin_code[ESP_BT_PIN_CODE_LEN] = '\0';
    	ESP_LOGW(BT_AV_TAG, "BT Sink pin truncated code [%s]. ", pin_code);
    }

    esp_bt_pin_code_t esp_pin_code;
    bool bError=false;
    memset(esp_pin_code, 0x00, sizeof(esp_pin_code) );
    ESP_LOGW(BT_AV_TAG, "BT Sink pin code is: [%s] ", pin_code);

    for(int i=0;i<strlen(pin_code);i++){
    	if(pin_code[i] < '0' || pin_code[i] > '9' ) {
    		ESP_LOGE(BT_AV_TAG,"Invalid number found in sequence");
    		bError=true;
    	}
    	esp_pin_code[i]= pin_code[i];
    }
	
    if (bError) memcpy(esp_pin_code, "1234", 4);
	esp_bt_gap_set_pin(pin_type, strlen(pin_code), esp_pin_code);
	
	free(pin_code);
}

void bt_sink_deinit(void)
{
	bt_app_task_shut_down();
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGD(BT_AV_TAG, "authentication success: %s", param->auth_cmpl.device_name);
            esp_log_buffer_hex(BT_AV_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE(BT_AV_TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;
    }

#if (CONFIG_BT_SSP_ENABLED == true)
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGD(BT_AV_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d", param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGD(BT_AV_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%d", param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGD(BT_AV_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
#endif

    default: {
        ESP_LOGD(BT_AV_TAG, "event: %d", event);
        break;
    }
    }
    return;
}

static void bt_av_hdl_stack_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s evt %d", __func__, event);
    switch (event) {
    case BT_APP_EVT_STACK_UP: {
        /* set up device name */
		esp_bt_dev_set_device_name(strlen(platform->names.bluetooth)>0?platform->names.bluetooth:platform->names.device);
        esp_bt_gap_register_callback(bt_app_gap_cb);

        /* initialize AVRCP controller */
        esp_avrc_ct_init();
        esp_avrc_ct_register_callback(bt_app_rc_ct_cb);
        /* initialize AVRCP target */
        assert (esp_avrc_tg_init() == ESP_OK);
        esp_avrc_tg_register_callback(bt_app_rc_tg_cb);

        esp_avrc_rn_evt_cap_mask_t evt_set = {0};
        esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
        assert(esp_avrc_tg_set_rn_evt_cap(&evt_set) == ESP_OK);

        /* initialize A2DP sink */
        esp_a2d_register_callback(&bt_app_a2d_cb);
        esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);
        esp_a2d_sink_init();

        /* set discoverable and connectable mode, wait to be connected */
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}




