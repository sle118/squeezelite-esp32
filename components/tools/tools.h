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
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
#include "cpp_tools.h"
extern "C" {
#endif
/**
 * @def QUOTE
 * Converts the argument name into a string.
 *
 * @param name The name to be converted into a string.
 */
#ifndef QUOTE
#define QUOTE(name) #name
#endif

/**
 * @def STR
 * Converts a macro value into a string.
 *
 * @param macro The macro whose value is to be converted into a string.
 */
#ifndef STR
#define STR(macro) QUOTE(macro)
#endif

/**
 * @def STR_OR_ALT
 * Returns the string 'str' or an alternative 'alt' if 'str' is NULL.
 *
 * @param str The string to check.
 * @param alt The alternative string to return if 'str' is NULL.
 */
#ifndef STR_OR_ALT
#ifdef __cplusplus
#define STR_OR_ALT(str, alt) (str != nullptr ? str : alt)
#else
#define STR_OR_ALT(str, alt) (str ? str : alt)
#endif

#endif

/**
 * @def STR_OR_BLANK
 * Returns the string 'p' or an empty string if 'p' is NULL.
 *
 * @param p The string to check.
 */
#ifndef STR_OR_BLANK
#ifdef __cplusplus
#define STR_OR_BLANK(p) p == nullptr ? "" : p
#else
#define STR_OR_BLANK(p) p == NULL ? "" : p
#endif
#endif

/**
 * @def ESP_LOG_DEBUG_EVENT
 * Logs a debug event with a given tag.
 *
 * @param tag The tag associated with the log message.
 * @param e The event to log.
 */
#define ESP_LOG_DEBUG_EVENT(tag, e) ESP_LOGD(tag, "evt: " e)

/**
 * @def FREE_AND_NULL
 * Frees a pointer if it's not NULL and sets it to NULL.
 *
 * @param x The pointer to free and nullify.
 */
#ifndef FREE_AND_NULL
#define FREE_AND_NULL(x)                                                                                                                             \
    if(x) {                                                                                                                                          \
        free(x);                                                                                                                                     \
        x = NULL;                                                                                                                                    \
    }
#endif

/**
 * @def CASE_TO_STR
 * A switch case that returns the string representation of a case constant.
 *
 * @param x The case constant.
 */
#ifndef CASE_TO_STR
#define CASE_TO_STR(x)                                                                                                                               \
    case x:                                                                                                                                          \
        return STR(x);                                                                                                                               \
        break;
#endif

/**
 * @def ENUM_TO_STRING
 * A switch case for an enum that returns the string representation of the enum value.
 *
 * @param g The enum value.
 */
#define ENUM_TO_STRING(g)                                                                                                                            \
    case g:                                                                                                                                          \
        return STR(g);                                                                                                                               \
        break;

/**
 * @fn void utf8_decode(char* src)
 * Decodes a string encoded in UTF-8 to CP1252 in place.
 * The result is stored in the same buffer as the input.
 *
 * @param src A pointer to the null-terminated string to be decoded.
 */
void utf8_decode(char* src);

/**
 * @fn void* malloc_init_external(size_t sz)
 * Allocates memory in PSRAM and initializes it to zero.
 *
 * This function attempts to allocate a block of memory of size 'sz' in PSRAM.
 * If allocation is successful, the memory is initialized to zero.
 *
 * @param sz The size of memory to allocate in bytes.
 * @return A pointer to the allocated memory block or NULL if allocation fails.
 */
void* malloc_init_external(size_t sz);

/**
 * @fn void* clone_obj_psram(void* source, size_t source_sz)
 * Clones an object into PSRAM.
 *
 * This function allocates memory in PSRAM and copies the contents of the
 * source object into the newly allocated memory. The size of the source
 * object is specified by 'source_sz'.
 *
 * @param source A pointer to the source object to clone.
 * @param source_sz The size of the source object in bytes.
 * @return A pointer to the cloned object in PSRAM or NULL if allocation fails.
 */
void* clone_obj_psram(void* source, size_t source_sz);

/**
 * @fn char* strdup_psram(const char* source)
 * Duplicates a string into PSRAM.
 *
 * This function allocates enough memory in PSRAM to hold a copy of 'source',
 * including the null terminator, and then copies the string into the new memory.
 *
 * @param source A pointer to the null-terminated string to duplicate.
 * @return A pointer to the duplicated string in PSRAM or NULL if allocation fails.
 */
char* strdup_psram(const char* source);
/**
 * @fn const char* get_mem_flag_desc(int flags)
 * Returns a string describing memory allocation flags.
 *
 * This function takes memory allocation flags as an argument and returns
 * a string representation of these flags. The description includes various
 * memory capabilities like EXEC, DMA, SPIRAM, etc.
 *
 * @param flags The memory allocation flags to describe.
 * @return A string representation of the memory allocation flags.
 */
const char* get_mem_flag_desc(int flags);

/**
 * @fn const char* get_mac_str()
 * Returns a string representation of the device's MAC address.
 *
 * This function reads the MAC address of the device and returns a string
 * representation of the last three bytes of the MAC address. It uses a static
 * buffer to store the MAC address string.
 *
 * @return A string representation of the device's MAC address.
 */
const char* get_mac_str();

/**
 * @fn char* alloc_get_string_with_mac(const char* val)
 * Allocates a new string which is a concatenation of the provided value and the device's MAC address.
 *
 * This function concatenates 'val' with the device's MAC address string and returns
 * a pointer to the newly allocated string.
 *
 * @param val The string to concatenate with the MAC address.
 * @return A pointer to the newly allocated string, or NULL if allocation fails.
 */
char* alloc_get_string_with_mac(const char* val);

/**
 * @fn char* alloc_get_fallback_unique_name()
 * Allocates a unique name for the device based on predefined configurations or fallbacks.
 *
 * This function creates a unique name by concatenating a base name defined by
 * configuration macros (CONFIG_LWIP_LOCAL_HOSTNAME, CONFIG_FW_PLATFORM_NAME)
 * or a fallback prefix "squeezelite-" with the device's MAC address.
 *
 * @return A pointer to the newly allocated unique name, or NULL if allocation fails.
 */
char* alloc_get_fallback_unique_name();

/**
 * @fn char* alloc_get_formatted_mac_string(uint8_t mac[6])
 * Allocates and returns a formatted MAC address string.
 *
 * This function formats a given MAC address into a human-readable string,
 * allocating the necessary memory dynamically.
 *
 * @param mac An array containing the 6-byte MAC address.
 * @return A pointer to the dynamically allocated MAC address string, or NULL if allocation fails.
 */
char* alloc_get_formatted_mac_string(uint8_t mac[6]);

/**
 * @fn const char* str_or_unknown(const char* str)
 * Returns the input string or a placeholder for unknown string.
 *
 * If the input string is not NULL, it returns the string. Otherwise,
 * it returns a predefined placeholder for unknown strings.
 *
 * @param str The input string.
 * @return The input string or a placeholder if the input is NULL.
 */
const char* str_or_unknown(const char* str);

/**
 * @fn const char* str_or_null(const char* str)
 * Returns the input string or a placeholder for null string.
 *
 * If the input string is not NULL, it returns the string. Otherwise,
 * it returns a predefined placeholder for null strings.
 *
 * @param str The input string.
 * @return The input string or a placeholder if the input is NULL.
 */
const char* str_or_null(const char* str);

/**
 * @fn esp_log_level_t get_log_level_from_char(char* level)
 * Converts a string representation of a log level to its corresponding `esp_log_level_t` enum value.
 *
 * The function supports log level strings like "NONE", "ERROR", "WARN", "INFO", "DEBUG", and "VERBOSE".
 *
 * @param level The log level as a string.
 * @return The corresponding `esp_log_level_t` value, or ESP_LOG_WARN if the string does not match any log level.
 */
esp_log_level_t get_log_level_from_char(char* level);

/**
 * @fn const char* get_log_level_desc(esp_log_level_t level)
 * Returns the string description of a given `esp_log_level_t` log level.
 *
 * @param level The log level as an `esp_log_level_t` enum.
 * @return The string description of the log level.
 */
const char* get_log_level_desc(esp_log_level_t level);

/**
 * @fn void set_log_level(char* tag, char* level)
 * Sets the log level for a specific tag.
 *
 * The log level is specified as a string and converted internally to the corresponding `esp_log_level_t` value.
 *
 * @param tag The tag for which to set the log level.
 * @param level The log level as a string.
 */
void set_log_level(char* tag, char* level);

/**
 * @fn BaseType_t xTaskCreateEXTRAM(TaskFunction_t pvTaskCode, const char* const pcName, configSTACK_DEPTH_TYPE usStackDepth, void* pvParameters,
 * UBaseType_t uxPriority, TaskHandle_t* pxCreatedTask) Creates a new task with its stack allocated in external RAM (PSRAM). Use these to dynamically
 * create tasks whose stack is on EXTRAM. Be aware that it requires configNUM_THREAD_LOCAL_STORAGE_POINTERS to bet set to 2 at least (index 0 is used
 * by pthread and this uses index 1, obviously
 *
 * @param pvTaskCode Pointer to the task entry function.
 * @param pcName Task name.
 * @param usStackDepth Stack depth in words.
 * @param pvParameters Pointer to the task's parameters.
 * @param uxPriority Task priority.
 * @param pxCreatedTask Pointer to a variable to store the task's handle.
 * @return pdPASS on success, pdFAIL on failure.
 */
BaseType_t xTaskCreateEXTRAM(TaskFunction_t pvTaskCode, const char* const pcName, configSTACK_DEPTH_TYPE usStackDepth, void* pvParameters,
    UBaseType_t uxPriority, TaskHandle_t* pxCreatedTask);

/**
 * @fn void vTaskDeleteEXTRAM(TaskHandle_t xTask)
 * Deletes a task and cleans up its allocated resources.
 * This function should be used in place of vTaskDelete for tasks created with xTaskCreateEXTRAM.
 *
 * @param xTask Handle to the task to be deleted.
 */
void vTaskDeleteEXTRAM(TaskHandle_t xTask);

/**
 * @fn void dump_json_content(const char* prefix, cJSON* json, int level)
 * Dumps the content of a cJSON object to the log.
 *
 * @param prefix Prefix for the log message.
 * @param json Pointer to the cJSON object to dump.
 * @param level Logging level for the output.
 */
void dump_json_content(const char* prefix, cJSON* json, int level);

#ifndef gettime_ms
// body is provided somewhere else...
uint32_t _gettime_ms_(void);
#define gettime_ms _gettime_ms_
#endif

/**
 * @def TRACE_DEBUG
 * A macro for debug tracing.
 *
 * This macro prints a formatted debug message to the standard output.
 * The message includes the name of the function and the line number from which
 * the macro is called, followed by a custom formatted message.
 *
 * @param msgformat A printf-style format string for the custom message.
 * @param ... Variable arguments for the format string.
 */
#ifndef TRACE_DEBUG
#define TRACE_DEBUG(msgformat, ...) printf("%-30s%-5d" msgformat "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif
