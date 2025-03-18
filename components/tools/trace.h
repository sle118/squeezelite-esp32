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

#ifdef __cplusplus
extern "C" {
#endif
#ifdef CONFIG_HEAP_TRACING
#define TRACE_INIT                                                                                                                                   \
    if (!is_recovery_running) {                                                                                                                      \
        ESP_ERROR_CHECK(heap_trace_init_tohost());                                                                                                   \
    }
#define TRACE_START                                                                                                                                  \
    if (!is_recovery_running) {                                                                                                                      \
        ESP_ERROR_CHECK(heap_trace_start(HEAP_TRACE_ALL));                                                                                           \
    }                                                                                                                                                \
    #define TRACE_STOP if (!is_recovery_running) { ESP_ERROR_CHECK(heap_trace_stop()); }
#else
#define TRACE_START
#define TRACE_STOP
#define TRACE_INIT
#endif

#ifdef __cplusplus
}
#endif
