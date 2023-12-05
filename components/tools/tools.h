/*
 *  Tools
 *
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#pragma once
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_spiffs.h"
#include "stdio.h"
#include "sys/stat.h"
#include "pb_common.h" // Nanopb header for encoding (serialization)
#include "pb_decode.h" // Nanopb header for decoding (deserialization)
#include "pb_encode.h" // Nanopb header for encoding (serialization)
#ifdef __cplusplus
extern "C" {
#endif

#ifndef QUOTE
#define QUOTE(name) #name
#endif

#ifndef STR
#define STR(macro) QUOTE(macro)
#endif

#ifndef STR_OR_ALT
#define STR_OR_ALT(str, alt) (str ? str : alt)
#endif

#ifndef STR_OR_BLANK
#define STR_OR_BLANK(p) p == NULL ? "" : p
#endif

#define ESP_LOG_DEBUG_EVENT(tag, e) ESP_LOGD(tag, "evt: " e)

#ifndef FREE_AND_NULL
#define FREE_AND_NULL(x)                                                                           \
    if (x) {                                                                                       \
        free(x);                                                                                   \
        x = NULL;                                                                                  \
    }
#endif

#ifndef CASE_TO_STR
#define CASE_TO_STR(x)                                                                             \
    case x:                                                                                        \
        return STR(x);                                                                             \
        break;
#endif

#define ENUM_TO_STRING(g)                                                                          \
    case g:                                                                                        \
        return STR(g);                                                                             \
        break;

void utf8_decode(char* src);
void url_decode(char* url);
void* malloc_init_external(size_t sz);
void* clone_obj_psram(void* source, size_t source_sz);
char* strdup_psram(const char* source);
const char* str_or_unknown(const char* str);
const char* str_or_null(const char* str);
void dump_json_content(const char* prefix, cJSON* json, int level);
void init_spiffs();
char * alloc_get_string_with_mac(const char * val);
const char * get_mac_str();

#define alloc_join_path(base_path, ...) _alloc_join_path(base_path,__VA_ARGS__, NULL)
char* _alloc_join_path(const char* base_path, ...);

#define get_file_info(pfileInfo, ...) _get_file_info(pfileInfo,__VA_ARGS__, NULL)

bool _get_file_info(struct stat* pfileInfo, ...);

#define load_file(sz, ...) _load_file(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, sz, __VA_ARGS__, NULL)
#define load_file_dma(sz, ...) _load_file(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA, sz, __VA_ARGS__, NULL)

void* _load_file(uint32_t memflags, size_t* sz, ...);

#define file_exists(pfileinfo,...) _file_exists(pfileInfo,__VA_ARGS__, NULL)
bool _file_exists(struct stat *fileInfo, ...);

#define open_file(mode,...) _open_file(mode,__VA_ARGS__, NULL)
FILE* _open_file( const char* mode,...);

bool in_file_binding(pb_istream_t* stream, pb_byte_t *buf, size_t count);
bool out_file_binding(pb_ostream_t* stream, const uint8_t* buf, size_t count);
bool out_http_binding(pb_ostream_t* stream, const uint8_t* buf, size_t count);

#define write_file(data,sz,...) _write_file(data,sz,__VA_ARGS__, NULL)
bool _write_file(uint8_t* data, size_t sz, ...);

void listFiles(const char *path_requested);
#ifndef gettime_ms
// body is provided somewhere else...
uint32_t _gettime_ms_(void);
#define gettime_ms _gettime_ms_
#endif

typedef void (*http_download_cb_t)(uint8_t* data, size_t len, void* context);
void http_download(char* url, size_t max, http_download_cb_t callback, void* context);

/* Use these to dynamically create tasks whose stack is on EXTRAM. Be aware that it
 * requires configNUM_THREAD_LOCAL_STORAGE_POINTERS to bet set to 2 at least (index 0
 * is used by pthread and this uses index 1, obviously
 */
BaseType_t xTaskCreateEXTRAM(TaskFunction_t pvTaskCode, const char* const pcName,
    configSTACK_DEPTH_TYPE usStackDepth, void* pvParameters, UBaseType_t uxPriority,
    TaskHandle_t* pxCreatedTask);
void vTaskDeleteEXTRAM(TaskHandle_t xTask);

extern const char unknown_string_placeholder[];

#ifndef TRACE_DEBUG
#define TRACE_DEBUG(msgformat, ... ) printf("%-30s%-5d" msgformat "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif
