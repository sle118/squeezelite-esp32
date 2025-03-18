/*
 *  (c) Philippe G. 20201, philippe_44@outlook.com
 *	see other copyrights below
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "tools.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#if CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS < 2
#error CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS must be at least 2
#endif

const static char TAG[] = "tools";
const char unknown_string_placeholder[] = "unknown";
const char null_string_placeholder[] = "null";

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

/**
 * @fn static uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte)
 * Decodes a single UTF-8 encoded byte.
 *
 * @param state A pointer to the current state of the UTF-8 decoder.
 * @param codep A pointer to the code point being built from the UTF-8 bytes.
 * @param byte The current byte to be decoded.
 * @return The new state after processing the byte.
 */
static uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
    uint32_t type = utf8d[byte];

    *codep = (*state != UTF8_ACCEPT) ? (byte & 0x3fu) | (*codep << 6) : (0xff >> type) & (byte);

    *state = utf8d[256 + *state * 16 + type];
    return *state;
}

/**
 * @fn static uint8_t UNICODEtoCP1252(uint16_t chr)
 * Converts a Unicode character to its corresponding CP1252 character.
 *
 * @param chr The Unicode character to be converted.
 * @return The corresponding CP1252 character or 0x00 if there is no direct mapping.
 */
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
        ESP_LOGE(TAG, "strdup_psram:  unable to allocate %d bytes of PSRAM! Cannot clone string %s", source_sz, source);
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

/**
 * @struct task_context_t
 * Structure to hold the context of a task including its task buffer and stack.
 *
 * @var StaticTask_t* xTaskBuffer
 * Pointer to the task's control block.
 * 
 * @var StackType_t* xStack
 * Pointer to the task's stack.
 */
typedef struct {
    StaticTask_t* xTaskBuffer;
    StackType_t* xStack;
} task_context_t;

/**
 * @fn static void task_cleanup(int index, task_context_t* context)
 * Cleans up the resources allocated for a task.
 * This function is intended to be used as a callback for task deletion.
 *
 * @param index The TLS index where the task's context is stored.
 * @param context Pointer to the task's context to be cleaned up.
 */
static void task_cleanup(int index, task_context_t* context) {
    free(context->xTaskBuffer);
    free(context->xStack);
    free(context);
}

BaseType_t xTaskCreateEXTRAM(TaskFunction_t pvTaskCode, const char* const pcName, configSTACK_DEPTH_TYPE usStackDepth, void* pvParameters,
    UBaseType_t uxPriority, TaskHandle_t* pxCreatedTask) {
    // create the worker task as a static
    task_context_t* context = calloc(1, sizeof(task_context_t));
    context->xTaskBuffer = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    context->xStack = heap_caps_malloc(usStackDepth, (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    TaskHandle_t handle = xTaskCreateStatic(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, context->xStack, context->xTaskBuffer);

    // store context in TCB or free everything in case of failure
    if (!handle) {
        free(context->xTaskBuffer);
        free(context->xStack);
        free(context);
    } else {
        vTaskSetThreadLocalStoragePointerAndDelCallback(handle, TASK_TLS_INDEX, context, (TlsDeleteCallbackFunction_t)task_cleanup);
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

const char* get_mem_flag_desc(int flags) {
    static char flagString[101];
    memset(flagString, 0x00, sizeof(flagString));
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
    const char* macstr = get_mac_str();
    char* fullvalue = (char*)malloc_init_external(strlen(val) + sizeof(macstr) + 1);
    if (fullvalue) {
        strcpy(fullvalue, val);
        strcat(fullvalue, macstr);
    } else {
        ESP_LOGE(TAG, "malloc failed for value %s", val);
    }
    return fullvalue;
}

char* alloc_get_fallback_unique_name() {
#ifdef CONFIG_LWIP_LOCAL_HOSTNAME
    return alloc_get_string_with_mac(CONFIG_LWIP_LOCAL_HOSTNAME "-");
#elif defined(CONFIG_FW_PLATFORM_NAME)
    return alloc_get_string_with_mac(CONFIG_FW_PLATFORM_NAME "-");
#else
    return alloc_get_string_with_mac("squeezelite-");
#endif
}

#define LOCAL_MAC_SIZE 20
char* alloc_get_formatted_mac_string(uint8_t mac[6]) {
    char* macStr = malloc_init_external(LOCAL_MAC_SIZE);
    if (macStr) {
        snprintf(macStr, LOCAL_MAC_SIZE, MACSTR, MAC2STR(mac));
    }
    return macStr;
}

const char* str_or_unknown(const char* str) { return (str ? str : unknown_string_placeholder); }
const char* str_or_null(const char* str) { return (str ? str : null_string_placeholder); }

esp_log_level_t get_log_level_from_char(char* level) {
    if (!strcasecmp(level, "NONE")) {
        return ESP_LOG_NONE;
    }
    if (!strcasecmp(level, "ERROR")) {
        return ESP_LOG_ERROR;
    }
    if (!strcasecmp(level, "WARN")) {
        return ESP_LOG_WARN;
    }
    if (!strcasecmp(level, "INFO")) {
        return ESP_LOG_INFO;
    }
    if (!strcasecmp(level, "DEBUG")) {
        return ESP_LOG_DEBUG;
    }
    if (!strcasecmp(level, "VERBOSE")) {
        return ESP_LOG_VERBOSE;
    }
    return ESP_LOG_WARN;
}
const char* get_log_level_desc(esp_log_level_t level) {
    switch (level) {
    case ESP_LOG_NONE:
        return "NONE";
        break;

    case ESP_LOG_ERROR:
        return "ERROR";
        break;

    case ESP_LOG_WARN:
        return "WARN";
        break;

    case ESP_LOG_INFO:
        return "INFO";
        break;
    case ESP_LOG_DEBUG:
        return "DEBUG";
        break;
    case ESP_LOG_VERBOSE:
        return "VERBOSE";
        break;

    default:
        break;
    }

    return "UNKNOWN";
}

void set_log_level(char* tag, char* level) { esp_log_level_set(tag, get_log_level_from_char(level)); }
