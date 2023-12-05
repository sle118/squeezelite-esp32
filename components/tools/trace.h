/* 
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
 
#pragma once

#ifdef ENABLE_MEMTRACE
void memtrace_print_delta(const char * msg, const char * tag, const char * function);
#define MEMTRACE_PRINT_DELTA() memtrace_print_delta(NULL,TAG,__FUNCTION__);
#define MEMTRACE_PRINT_DELTA_MESSAGE(x) memtrace_print_delta(x,TAG,__FUNCTION__);
#else
#define MEMTRACE_PRINT_DELTA()
#define MEMTRACE_PRINT_DELTA_MESSAGE(x) ESP_LOGD(TAG,"%s",x);
#endif