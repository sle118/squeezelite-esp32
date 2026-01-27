/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// STOP means remove playlist, FLUSH means flush audio buffer, DISC means bye-bye
typedef enum { 	CSPOT_START, CSPOT_DISC, CSPOT_FLUSH, CSPOT_STOP, CSPOT_PLAY, CSPOT_PAUSE, CSPOT_SEEK, 
                CSPOT_NEXT, CSPOT_PREV, CSPOT_TOGGLE, 
                CSPOT_TRACK_INFO, CSPOT_TRACK_MARK,
				CSPOT_VOLUME, CSPOT_VOLUME_UP, CSPOT_VOLUME_DOWN, 
                CSPOT_BUSY, CSPOT_QUERY_STARTED, CSPOT_QUERY_REMAINING, 
} cspot_event_t;
				
typedef bool (*cspot_cmd_cb_t)(cspot_event_t event, ...);				
typedef bool (*cspot_cmd_vcb_t)(cspot_event_t event, va_list args);
typedef uint32_t (*cspot_data_cb_t)(const uint8_t *data, size_t len);

/**
 * @brief     init sink mode (need to be provided)
 */
void cspot_sink_init(cspot_cmd_vcb_t cmd_cb, cspot_data_cb_t data_cb);

/**
 * @brief     deinit sink mode (need to be provided)
 */
#define cspot_sink_deinit()

/**
 * @brief     force disconnection
 */
void cspot_disconnect(void);

#ifdef __cplusplus
}
#endif
