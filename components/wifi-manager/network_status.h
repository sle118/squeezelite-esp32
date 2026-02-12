/*
 *
 *      Sebastien L. 2023, sle118@hotmail.com
 *      Philippe G. 2023, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 *  License Overview:
 *  ----------------
 *  The MIT License is a permissive open source license. As a user of this software, you are free to:
 *  - Use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of this software.
 *  - Use the software for private, commercial, or any other purposes.
 *
 *  Conditions:
 *  - You must include the above copyright notice and this permission notice in all
 *    copies or substantial portions of the Software.
 *
 *  The MIT License offers a high degree of freedom and is well-suited for both open source and
 *  commercial applications. It places minimal restrictions on how the software can be used,
 *  modified, and redistributed. For more details on the MIT License, please refer to the link above.
 */

#pragma once
#include "network_manager.h"
#include "cJSON.h"
#include "PBW.h"
#include "Status.pb.h"
#include "esp_http_server.h"

#ifdef __cplusplus

extern System::PB<sys_status_data> sys_status_obj;

extern "C" {

#endif

/**
 * @brief Generates the connection status json: ssid and IP addresses.
 * @note This is not thread-safe and should be called only if network_status_lock_json_buffer call is successful.
 */
void network_status_update_ip_info(sys_status_reasons update_reason_code);
bool network_status_send_object(httpd_req_t* req);
void init_network_status();
void network_status_clear_ip();
void network_status_safe_reset_sta_ip();
extern sys_status_data* sys_status;
#ifdef __cplusplus
}
#endif