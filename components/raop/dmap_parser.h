#ifndef dmap_parser_h
#define dmap_parser_h
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#define DMAP_VERSION_MAJOR 1
#define DMAP_VERSION_MINOR 2
#define DMAP_VERSION_PATCH 1

#define DMAP_VERSION (DMAP_VERSION_MAJOR * 1000000 + DMAP_VERSION_MINOR * 1000 + DMAP_VERSION_PATCH)

/*
 * Callbacks invoked during parsing.
 *
 * @param ctx  The context pointer specified in the dmap_settings structure.
 * @param code The content code from the message.
 * @param name The name associated with the content code, if known. If there is
 *             no known name this parameter contains the same value as the code
 *             parameter.
 */
typedef void (*dmap_dict_cb)(void* ctx, const char* code, const char* name);
typedef void (*dmap_int32_cb)(void* ctx, const char* code, const char* name, int32_t value);
typedef void (*dmap_int64_cb)(void* ctx, const char* code, const char* name, int64_t value);
typedef void (*dmap_uint32_cb)(void* ctx, const char* code, const char* name, uint32_t value);
typedef void (*dmap_uint64_cb)(void* ctx, const char* code, const char* name, uint64_t value);
typedef void (*dmap_data_cb)(void* ctx, const char* code, const char* name, const char* buf, size_t len);

typedef struct {
    /* Callbacks to indicate the start and end of dictionary fields. */
    dmap_dict_cb on_dict_start;
    dmap_dict_cb on_dict_end;

    /* Callbacks for field data. */
    dmap_int32_cb on_int32;
    dmap_int64_cb on_int64;
    dmap_uint32_cb on_uint32;
    dmap_uint64_cb on_uint64;
    dmap_uint32_cb on_date;
    dmap_data_cb on_string;
    dmap_data_cb on_data;

    /** A context pointer passed to each callback function. */
    void* ctx;
} dmap_settings;

/**
 * Returns the library version number.
 *
 * The version number format is (major * 1000000) + (minor * 1000) + patch.
 * For example, the value for version 1.2.3 is 1002003.
 */
int dmap_version(void);

/**
 * Returns the library version as a string.
 */
const char* dmap_version_string(void);

/**
 * Returns the name associated with the provided content code, or NULL if there
 * is no known name.
 *
 * For example, if given the code "minm" this function returns "dmap.itemname".
 */
const char* dmap_name_from_code(const char* code);

/**
 * Parses a DMAP message buffer using the provided settings.
 *
 * @param settings A dmap_settings structure populated with the callbacks to
 *                 invoke during parsing.
 * @param buf      Pointer to a DMAP message buffer. The buffer must contain a
 *                 complete message.
 * @param len      The length of the DMAP message buffer.
 *
 * @return 0 if parsing was successful, or -1 if an error occurred.
 */
int dmap_parse(const dmap_settings* settings, const char* buf, size_t len);

#ifdef __cplusplus
}
#endif
#endif
