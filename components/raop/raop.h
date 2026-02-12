/*
 *  (c) Philippe 2020, philippe_44@outlook.com
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 *
 */

#pragma once

#include "platform.h"
#include "raop_sink.h"

struct raop_ctx_s* raop_create(uint32_t host, char* name, unsigned char mac[6], int latency, raop_cmd_cb_t cmd_cb, raop_data_cb_t data_cb);
void raop_delete(struct raop_ctx_s* ctx);
void raop_abort(struct raop_ctx_s* ctx);
bool raop_cmd(struct raop_ctx_s* ctx, raop_event_t event, void* param);
