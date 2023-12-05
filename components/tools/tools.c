/*
 *  (c) Philippe G. 20201, philippe_44@outlook.com
 *	see other copyrights below
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
// #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "tools.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_task.h"
#include "esp_tls.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "esp_http_server.h"
#if CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS < 2
#error CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS must be at least 2
#endif
static bool initialized = false;
const static char TAG[] = "tools";
static esp_vfs_spiffs_conf_t* spiffs_conf = NULL;

/****************************************************************************************
 * UTF-8 tools
 */

// Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
// Copyright (c) 2017 ZephRay <zephray@outlook.com>
//
// utf8to1252 - almost equivalent to iconv -f utf-8 -t windows-1252, but better

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

static const uint8_t utf8d[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, // 00..1f
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, // 20..3f
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, // 40..5f
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, // 60..7f
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, // 80..9f
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, // a0..bf
    8, 8, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2,                                                                              // c0..df
    0xa, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x4, 0x3, 0x3, // e0..ef
    0xb, 0x6, 0x6, 0x6, 0x5, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, // f0..ff
    0x0, 0x1, 0x2, 0x3, 0x5, 0x8, 0x7, 0x1, 0x1, 0x1, 0x4, 0x6, 0x1, 0x1, 0x1, 0x1, // s0..s0
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1,
    1, // s1..s2
    1, 2, 1, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1,
    1, // s3..s4
    1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 1, 3, 1, 1, 1, 1, 1,
    1, // s5..s6
    1, 3, 1, 1, 1, 1, 1, 3, 1, 3, 1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, // s7..s8
};

static uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
    uint32_t type = utf8d[byte];

    *codep = (*state != UTF8_ACCEPT) ? (byte & 0x3fu) | (*codep << 6) : (0xff >> type) & (byte);

    *state = utf8d[256 + *state * 16 + type];
    return *state;
}

static uint8_t UNICODEtoCP1252(uint16_t chr) {
    if (chr <= 0xff)
        return (chr & 0xff);
    else {
        ESP_LOGI(TAG, "some multi-byte %hx", chr);
        switch (chr) {
        case 0x20ac:
            return 0x80;
            break;
        case 0x201a:
            return 0x82;
            break;
        case 0x0192:
            return 0x83;
            break;
        case 0x201e:
            return 0x84;
            break;
        case 0x2026:
            return 0x85;
            break;
        case 0x2020:
            return 0x86;
            break;
        case 0x2021:
            return 0x87;
            break;
        case 0x02c6:
            return 0x88;
            break;
        case 0x2030:
            return 0x89;
            break;
        case 0x0160:
            return 0x8a;
            break;
        case 0x2039:
            return 0x8b;
            break;
        case 0x0152:
            return 0x8c;
            break;
        case 0x017d:
            return 0x8e;
            break;
        case 0x2018:
            return 0x91;
            break;
        case 0x2019:
            return 0x92;
            break;
        case 0x201c:
            return 0x93;
            break;
        case 0x201d:
            return 0x94;
            break;
        case 0x2022:
            return 0x95;
            break;
        case 0x2013:
            return 0x96;
            break;
        case 0x2014:
            return 0x97;
            break;
        case 0x02dc:
            return 0x98;
            break;
        case 0x2122:
            return 0x99;
            break;
        case 0x0161:
            return 0x9a;
            break;
        case 0x203a:
            return 0x9b;
            break;
        case 0x0153:
            return 0x9c;
            break;
        case 0x017e:
            return 0x9e;
            break;
        case 0x0178:
            return 0x9f;
            break;
        default:
            return 0x00;
            break;
        }
    }
}

void utf8_decode(char* src) {
    uint32_t codep = 0, state = UTF8_ACCEPT;
    char* dst = src;

    while (src && *src) {
        if (!decode(&state, &codep, *src++)) *dst++ = UNICODEtoCP1252(codep);
    }

    *dst = '\0';
}

/****************************************************************************************
 * URL tools
 */

static inline char from_hex(char ch) { return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10; }

void url_decode(char* url) {
    char *p, *src = strdup(url);
    for (p = src; *src; url++) {
        *url = *src++;
        if (*url == '%') {
            *url = from_hex(*src++) << 4;
            *url |= from_hex(*src++);
        } else if (*url == '+') {
            *url = ' ';
        }
    }
    *url = '\0';
    free(p);
}

/****************************************************************************************
 * Memory tools
 */

void* malloc_init_external(size_t sz) {
    void* ptr = NULL;
    ptr = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ESP_LOGE(TAG, "malloc_init_external:  unable to allocate %d bytes of PSRAM!", sz);
    } else {
        memset(ptr, 0x00, sz);
    }
    return ptr;
}

void* clone_obj_psram(void* source, size_t source_sz) {
    void* ptr = NULL;
    ptr = heap_caps_malloc(source_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ESP_LOGE(TAG, "clone_obj_psram:  unable to allocate %d bytes of PSRAM!", source_sz);
    } else {
        memcpy(ptr, source, source_sz);
    }
    return ptr;
}

char* strdup_psram(const char* source) {
    void* ptr = NULL;
    size_t source_sz = strlen(source) + 1;
    ptr = heap_caps_malloc(source_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ESP_LOGE(TAG, "strdup_psram:  unable to allocate %d bytes of PSRAM! Cannot clone string %s",
            source_sz, source);
    } else {
        memset(ptr, 0x00, source_sz);
        strcpy(ptr, source);
    }
    return ptr;
}

/****************************************************************************************
 * Task manager
 */
#define TASK_TLS_INDEX 1

typedef struct {
    StaticTask_t* xTaskBuffer;
    StackType_t* xStack;
} task_context_t;

static void task_cleanup(int index, task_context_t* context) {
    free(context->xTaskBuffer);
    free(context->xStack);
    free(context);
}

BaseType_t xTaskCreateEXTRAM(TaskFunction_t pvTaskCode, const char* const pcName,
    configSTACK_DEPTH_TYPE usStackDepth, void* pvParameters, UBaseType_t uxPriority,
    TaskHandle_t* pxCreatedTask) {
    // create the worker task as a static
    task_context_t* context = calloc(1, sizeof(task_context_t));
    context->xTaskBuffer = (StaticTask_t*)heap_caps_malloc(
        sizeof(StaticTask_t), (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    context->xStack = heap_caps_malloc(usStackDepth, (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    TaskHandle_t handle = xTaskCreateStatic(pvTaskCode, pcName, usStackDepth, pvParameters,
        uxPriority, context->xStack, context->xTaskBuffer);

    // store context in TCB or free everything in case of failure
    if (!handle) {
        free(context->xTaskBuffer);
        free(context->xStack);
        free(context);
    } else {
        vTaskSetThreadLocalStoragePointerAndDelCallback(
            handle, TASK_TLS_INDEX, context, (TlsDeleteCallbackFunction_t)task_cleanup);
    }

    if (pxCreatedTask) *pxCreatedTask = handle;
    return handle != NULL ? pdPASS : pdFAIL;
}

void vTaskDeleteEXTRAM(TaskHandle_t xTask) {
    /* At this point we leverage FreeRTOS extension to have callbacks on task deletion.
     * If not, we need to have here our own deletion implementation that include delayed
     * free for when this is called with NULL (self-deletion)
     */
    vTaskDelete(xTask);
}

/****************************************************************************************
 * URL download
 */

typedef struct {
    void* user_context;
    http_download_cb_t callback;
    size_t max, bytes;
    bool abort;
    uint8_t* data;
    esp_http_client_handle_t client;
} http_context_t;

static void http_downloader(void* arg);
static esp_err_t http_event_handler(esp_http_client_event_t* evt);

void http_download(char* url, size_t max, http_download_cb_t callback, void* context) {
    http_context_t* http_context =
        (http_context_t*)heap_caps_calloc(sizeof(http_context_t), 1, MALLOC_CAP_SPIRAM);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = http_context,
    };

    http_context->callback = callback;
    http_context->user_context = context;
    http_context->max = max;
    http_context->client = esp_http_client_init(&config);

    xTaskCreateEXTRAM(
        http_downloader, "downloader", 8 * 1024, http_context, ESP_TASK_PRIO_MIN + 1, NULL);
}

static void http_downloader(void* arg) {
    http_context_t* http_context = (http_context_t*)arg;

    esp_http_client_perform(http_context->client);
    esp_http_client_cleanup(http_context->client);

    free(http_context);
    vTaskDeleteEXTRAM(NULL);
}

static esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    http_context_t* http_context = (http_context_t*)evt->user_data;

    if (http_context->abort) return ESP_FAIL;

    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        http_context->callback(NULL, 0, http_context->user_context);
        http_context->abort = true;
        break;
    case HTTP_EVENT_ON_HEADER:
        if (!strcasecmp(evt->header_key, "Content-Length")) {
            size_t len = atoi(evt->header_value);
            if (!len || len > http_context->max) {
                ESP_LOGI(TAG, "content-length null or too large %zu / %zu", len, http_context->max);
                http_context->abort = true;
            }
        }
        break;
    case HTTP_EVENT_ON_DATA: {
        size_t len = esp_http_client_get_content_length(evt->client);
        if (!http_context->data) {
            if ((http_context->data = (uint8_t*)malloc(len)) == NULL) {
                http_context->abort = true;
                ESP_LOGE(TAG, "failed to allocate memory for output buffer %zu", len);
                return ESP_FAIL;
            }
        }
        memcpy(http_context->data + http_context->bytes, evt->data, evt->data_len);
        http_context->bytes += evt->data_len;
        break;
    }
    case HTTP_EVENT_ON_FINISH:
        http_context->callback(http_context->data, http_context->bytes, http_context->user_context);
        break;
    case HTTP_EVENT_DISCONNECTED: {
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP download disconnect %d", err);
            if (http_context->data) free(http_context->data);
            http_context->callback(NULL, 0, http_context->user_context);
            return ESP_FAIL;
        }
        break;
    }
    default:
        break;
    }

    return ESP_OK;
}

void dump_json_content(const char* prefix, cJSON* json, int level) {
    if (!json) {
        ESP_LOG_LEVEL(level, TAG, "%s: empty!", prefix);
        return;
    }
    char* output = cJSON_Print(json);
    if (output) {
        ESP_LOG_LEVEL(level, TAG, "%s: \n%s", prefix, output);
    }
    FREE_AND_NULL(output);
}
void init_spiffs() {
    if (initialized) {
        ESP_LOGD(TAG, "SPIFFS already initialized. returning");
        return;
    }
    ESP_LOGI(TAG, "Initializing the SPI File system");
    spiffs_conf = (esp_vfs_spiffs_conf_t*)malloc(sizeof(esp_vfs_spiffs_conf_t));
    spiffs_conf->base_path = "/spiffs";
    spiffs_conf->partition_label = NULL;
    spiffs_conf->max_files = 5;
    spiffs_conf->format_if_mount_failed = true;
    
    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(spiffs_conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(spiffs_conf->partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get SPIFFS partition information (%s). Formatting...",
            esp_err_to_name(ret));
        esp_spiffs_format(spiffs_conf->partition_label);
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }


    initialized = true;
}

// Function to safely append a path part with '/' if needed
void append_path_part(char** dest, const char* part) {
    if ((*dest)[-1] != '/' && part[0] != '/') {
        strcat(*dest, "/");
        *dest += 1; // Move the pointer past the '/'
    }
    strcat(*dest, part);
    *dest += strlen(part);
}

// Function to calculate the total length needed for the new path
size_t calculate_total_length(const char* base_path, va_list args) {
    ESP_LOGV(TAG, "%s, Starting with base path: %s", "calculate_total_length", base_path);
    size_t length = strlen(base_path) + 1; // +1 for null terminator
    const char* part;
    va_list args_copy;
    va_copy(args_copy, args);
    while ((part = va_arg(args_copy, const char*)) != NULL) {
        ESP_LOGV(TAG, "Adding length of %s", part);
        length += strlen(part) + 1; // +1 for potential '/'
    }
    ESP_LOGV(TAG, "Done looping. calculated length: %d", length);
    va_end(args_copy);
    return length;
}

// Main function to join paths
char* __alloc_join_path(const char* base_path, va_list args) {
    size_t count = 0;
    ESP_LOGD(TAG, "Getting path length starting with %s", base_path);
    size_t total_length = calculate_total_length(base_path, args);

    // Allocate memory
    char* full_path = malloc_init_external(total_length);
    if (!full_path) {
        ESP_LOGE(TAG, "Unable to allocate memory for path");
        return NULL;
    }

    // Start constructing the path
    strcpy(full_path, base_path);
    char* current_position = full_path + strlen(full_path);

    // Append each path part
    const char* part;
    while ((part = va_arg(args, const char*)) != NULL) {
        append_path_part(&current_position, part);
    }
    *current_position = '\0'; // Null-terminate the string
    return full_path;
}
char* _alloc_join_path(const char* base_path, ...) {
    va_list args;
    ESP_LOGD(TAG, "%s", "join_path_var_parms");
    va_start(args, base_path);
    char* result = __alloc_join_path(base_path, args);
    va_end(args);
    return result;
}

FILE* __open_file(const char* mode, va_list args) {
    FILE* file = NULL;
    char* fullfilename = __alloc_join_path(spiffs_conf->base_path, args);
    if (!fullfilename) {
        ESP_LOGE(TAG, "Open file failed: unable to determine name");
    } else {
        ESP_LOGI(TAG, "Opening file %s in mode %s ", fullfilename, mode);
        file = fopen(fullfilename, mode);
    }
    if (file == NULL) {
        ESP_LOGE(TAG, "Open file failed");
    }
    if (fullfilename) free(fullfilename);
    return file;
}
FILE* _open_file(const char* mode, ...) {
    va_list args;
    FILE* file = NULL;
    va_start(args, mode);
    file = __open_file(mode, args);
    va_end(args);
    return file;
}
bool _write_file(uint8_t* data, size_t sz, ...) {
    bool result = true;
    FILE* file = NULL;
    va_list args;
    if (data == NULL) {
        ESP_LOGE(TAG, "Cannot write file. Data not received");
        return false;
    }
    if (sz == 0) {
        ESP_LOGE(TAG, "Cannot write file. Data length 0");
        return false;
    }
    va_start(args, sz);
    file = __open_file("w+", args);
    va_end(args);
    if (file == NULL) {
        return false;
    }
    size_t written = fwrite(data, 1, sz, file);
    if (written != sz) {
        ESP_LOGE(TAG, "Write error. Wrote %d bytes of %d.", written, sz);
        result = false;
    }
    fclose(file);
    return result;
}
const char* get_mem_flag_desc(int flags) {
    static char flagString[101]; 
    memset(flagString,0x00,sizeof(flagString));
    if (flags & MALLOC_CAP_EXEC) strcat(flagString, "EXEC ");
    if (flags & MALLOC_CAP_32BIT) strcat(flagString, "32BIT ");
    if (flags & MALLOC_CAP_8BIT) strcat(flagString, "8BIT ");
    if (flags & MALLOC_CAP_DMA) strcat(flagString, "DMA ");
    if (flags & MALLOC_CAP_PID2) strcat(flagString, "PID2 ");
    if (flags & MALLOC_CAP_PID3) strcat(flagString, "PID3 ");
    if (flags & MALLOC_CAP_PID4) strcat(flagString, "PID4 ");
    if (flags & MALLOC_CAP_PID5) strcat(flagString, "PID5 ");
    if (flags & MALLOC_CAP_PID6) strcat(flagString, "PID6 ");
    if (flags & MALLOC_CAP_PID7) strcat(flagString, "PID7 ");
    if (flags & MALLOC_CAP_SPIRAM) strcat(flagString, "SPIRAM ");
    if (flags & MALLOC_CAP_INTERNAL) strcat(flagString, "INTERNAL ");
    if (flags & MALLOC_CAP_DEFAULT) strcat(flagString, "DEFAULT ");
    if (flags & MALLOC_CAP_IRAM_8BIT) strcat(flagString, "IRAM_8BIT ");
    if (flags & MALLOC_CAP_RETENTION) strcat(flagString, "RETENTION ");

    return flagString;
}
void* _load_file(uint32_t memflags,size_t* sz, ...) {
    void* data = NULL;
    FILE* file = NULL;
    size_t fsz = 0;
    va_list args;
    va_start(args, sz);
    file = __open_file("rb", args);
    va_end(args);

    if (file == NULL) {
        return data;
    }
    fseek(file, 0, SEEK_END);
    fsz = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fsz > 0) {
        ESP_LOGD(TAG, "Allocating %d bytes to load file content with flags: %s ", fsz,get_mem_flag_desc(memflags));
        data = (void*)heap_caps_calloc(1, fsz, memflags); 
        if (data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes to load file", fsz);
        } else {
            fread(data, 1, fsz, file);
            if (sz) {
                *sz = fsz;
            }
        }
    } else {
        ESP_LOGW(TAG, "File is empty. Nothing to read");
    }
    fclose(file);
    return data;
}
bool _get_file_info(struct stat* pfileInfo, ...) {
    va_list args;
    struct stat fileInfo;
    va_start(args, pfileInfo);
    char* fullfilename = __alloc_join_path(spiffs_conf->base_path, args);
    va_end(args);
    ESP_LOGD(TAG, "Getting file info for %s", fullfilename);

    if (!fullfilename) {
        ESP_LOGE(TAG, "Failed to construct full file path");
        return false;
    }
    bool result = false;
    // Use stat to fill the fileInfo structure
    if (stat(fullfilename, &fileInfo) != 0) {
        ESP_LOGD(TAG, "File %s not found", fullfilename);
    } else {
        result = true;
        if (pfileInfo) {
            memcpy(pfileInfo, &fileInfo, sizeof(fileInfo));
        }
        ESP_LOGD(TAG, "File %s has %lu bytes", fullfilename, fileInfo.st_size);
    }

    free(fullfilename);
    return result;
}
#define LOCAL_MAC_SIZE 10
const char* get_mac_str() {
    uint8_t mac[6];
    static char macStr[LOCAL_MAC_SIZE + 1] = {0};
    if (macStr[0] == 0) {
        ESP_LOGD(TAG, "calling esp_read_mac");
        esp_read_mac((uint8_t*)&mac, ESP_MAC_WIFI_STA);
        ESP_LOGD(TAG, "Writing mac to string");
        snprintf(macStr, sizeof(macStr), "%x%x%x", mac[3], mac[4], mac[5]);
        ESP_LOGD(TAG, "Determined mac string: %s", macStr);
    }
    return macStr;
}
char* alloc_get_string_with_mac(const char* val) {
    uint8_t mac[6];
    char macStr[LOCAL_MAC_SIZE + 1];
    char* fullvalue = NULL;
    esp_read_mac((uint8_t*)&mac, ESP_MAC_WIFI_STA);
    snprintf(macStr, LOCAL_MAC_SIZE - 1, "-%x%x%x", mac[3], mac[4], mac[5]);
    fullvalue = (char*)malloc_init_external(strlen(val) + sizeof(macStr) + 1);
    if (fullvalue) {
        strcpy(fullvalue, val);
        strcat(fullvalue, macStr);
    } else {
        ESP_LOGE(TAG, "malloc failed for value %s", val);
    }
    return fullvalue;
}
void listFiles(const char *path_requested) {
    DIR *dir = NULL;
    char * sep="---------------------------------------------------------\n";
    struct dirent *ent;
    char type;
    char size[21];
    char tpath[255];
    struct stat sb;
    struct tm *tm_info;
    char *lpath = NULL;
    int statok;
    char * path= alloc_join_path(spiffs_conf->base_path,path_requested);

    printf("\nList of Directory [%s]\n", path);
    printf(sep);
    // Open directory
    dir = opendir(path);
    if (!dir) {
        printf("Error opening directory\n");
        free(path);
        return;
    }

    // Read directory entries
    uint64_t total = 0;
    int nfiles = 0;
    printf("T       Size Name\n");
    printf(sep);
    while ((ent = readdir(dir)) != NULL) {
        sprintf(tpath, path);
        if (path[strlen(path)-1] != '/') strcat(tpath,"/");
        strcat(tpath,ent->d_name);

        // Get file stat
        statok = stat(tpath, &sb);

        if (ent->d_type == DT_REG) {
            type = 'f';
            nfiles++;
            if (statok) strcpy(size, "       ?");
            else {
                total += sb.st_size;
                if (sb.st_size < (1024*1024)) sprintf(size,"%8d", (int)sb.st_size);
                else if ((sb.st_size/1024) < (1024*1024)) sprintf(size,"%6dKB", (int)(sb.st_size / 1024));
                else sprintf(size,"%6dMB", (int)(sb.st_size / (1024 * 1024)));
            }
        }
        else {
            type = 'd';
            strcpy(size, "       -");
        }

        printf("%c  %s  %s\r\n",
            type,
            size,
            ent->d_name
        );
        
    }
    if (total) {
        printf(sep);
    	if (total < (1024*1024)) printf("   %8d", (int)total);
    	else if ((total/1024) < (1024*1024)) printf("   %6dKB", (int)(total / 1024));
    	else printf("   %6dMB", (int)(total / (1024 * 1024)));
    	printf(" in %d file(s)\n", nfiles);
    }
    printf(sep);

    closedir(dir);

    free(lpath);
    free(path);
	uint32_t tot=0, used=0;
    esp_spiffs_info(NULL, &tot, &used);
    printf("SPIFFS: free %d KB of %d KB\n", (tot-used) / 1024, tot / 1024);
    printf(sep);
}

bool out_file_binding(pb_ostream_t* stream, const uint8_t* buf, size_t count) {
    FILE* file = (FILE*)stream->state;
    ESP_LOGD(TAG, "Writing %d bytes to file", count);
    return fwrite(buf, 1, count, file) == count;
}
bool in_file_binding(pb_istream_t* stream, pb_byte_t *buf, size_t count) {
    FILE* file = (FILE*)stream->state;
    ESP_LOGD(TAG, "Reading %d bytes from file", count);
    return  fread(buf, 1, count, file) == count;
}

bool out_http_binding(pb_ostream_t* stream, const uint8_t* buf, size_t count) {
    httpd_req_t* req = (httpd_req_t*)stream->state;
    ESP_LOGD(TAG, "Writing %d bytes to file", count);
    return  httpd_resp_send_chunk(req, (const char*)buf, count) == ESP_OK;
}
