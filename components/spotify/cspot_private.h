/*
 *  (c) Philippe 2020, philippe_44@outlook.com
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 *
 */

#pragma once

#include "cspot_sink.h"

struct cspot_s;

#ifdef __cplusplus
extern "C"
{
#endif

struct cspot_s*	cspot_create(const char *name, httpd_handle_t server, int port, cspot_cmd_cb_t cmd_cb, cspot_data_cb_t data_cb);
bool			cspot_cmd(struct cspot_s *ctx, cspot_event_t event, void *param);

#ifdef __cplusplus
}
#endif

