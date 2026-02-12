/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef __BT_APP_SINK_H__
#define __BT_APP_SINK_H__

#include <stdint.h>

#include "bt_app_core.h"
typedef enum {
    BT_SINK_CONNECTED,
    BT_SINK_DISCONNECTED,
    BT_SINK_AUDIO_STARTED,
    BT_SINK_AUDIO_STOPPED,
    BT_SINK_PLAY,
    BT_SINK_STOP,
    BT_SINK_PAUSE,
    BT_SINK_RATE,
    BT_SINK_VOLUME,
    BT_SINK_METADATA,
    BT_SINK_PROGRESS
} bt_sink_cmd_t;

typedef bool (*bt_cmd_vcb_t)(bt_sink_cmd_t cmd, va_list args);
typedef void (*bt_data_cb_t)(const uint8_t* data, uint32_t len);

/**
 * @brief     init sink mode (need to be provided)
 */
void bt_sink_init(bt_cmd_vcb_t cmd_cb, bt_data_cb_t data_cb);

/**
 * @brief     deinit sink mode (need to be provided)
 */
void bt_sink_deinit(void);

/**
 * @brief     force disconnection
 */
void bt_disconnect(void);

#endif /* __BT_APP_SINK_H__*/
