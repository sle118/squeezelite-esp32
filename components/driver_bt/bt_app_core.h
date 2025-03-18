/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef __BT_APP_CORE_H__
#define __BT_APP_CORE_H__
#include "Status.pb.h"
#include "esp_log.h"
#include "time.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BT_APP_CORE_TAG "BT_APP_CORE"
#define BT_APP_SIG_WORK_DISPATCH (0x01)

enum {
    BT_APP_EVT_STACK_UP = 0,
};

extern sys_status_av_states bt_app_source_get_a2d_state();
extern sys_status_media_states bt_app_source_get_media_state();
/**
 * @brief     handler for the dispatched work
 */
typedef void (*bt_app_cb_t)(uint16_t event, void* param);

/* message to be sent */
typedef struct {
    uint16_t sig;   /*!< signal to bt_app_task */
    uint16_t event; /*!< message event id */
    bt_app_cb_t cb; /*!< context switch callback */
    void* param;    /*!< parameter area needs to be last */
} bt_app_msg_t;

/**
 * @brief     parameter deep-copy function to be customized
 */
typedef void (*bt_app_copy_cb_t)(bt_app_msg_t* msg, void* p_dest, void* p_src);

/**
 * @brief     callback for startup event
 */
typedef void bt_av_hdl_stack_evt_t(uint16_t event, void* p_param);

/**
 * @brief     work dispatcher for the application task
 */
bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void* p_params, int param_len,
    bt_app_copy_cb_t p_copy_cback);

void bt_app_task_start_up(bt_av_hdl_stack_evt_t* handler);

void bt_app_task_shut_down(void);

#endif /* __BT_APP_CORE_H__ */
#ifdef __cplusplus
}
#endif