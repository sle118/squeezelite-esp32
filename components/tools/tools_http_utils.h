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
#include "esp_system.h"
#include "pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Type definition for a callback function used in HTTP download.
 * 
 * @param data Pointer to the downloaded data buffer.
 * @param len Length of the data buffer.
 * @param context User-defined context passed to the callback.
 */
typedef void (*http_download_cb_t)(uint8_t* data, size_t len, void* context);

/**
 * @brief Downloads data from a specified URL.
 * 
 * This function initializes an HTTP client and starts a download task.
 * It uses a callback mechanism to return the downloaded data.
 * 
 * @param url The URL from which to download data.
 * @param max The maximum size of data to download.
 * @param callback The callback function to be called with the downloaded data.
 * @param context User-defined context to be passed to the callback function.
 */
void http_download(char* url, size_t max, http_download_cb_t callback, void* context);

/**
 * @brief Decodes a URL-encoded string.
 * 
 * This function replaces percent-encoded characters in the URL with their ASCII representations.
 * Spaces encoded as '+' are also converted to space characters.
 * 
 * @param url The URL-encoded string to be decoded in place.
 */
void url_decode(char* url);

/**
 * @brief Callback function for output streaming with HTTP binding.
 * 
 * This function is designed to be used with NanoPB for streaming output data over HTTP.
 * It sends the given buffer over an HTTP connection.
 * 
 * @param stream The output stream provided by NanoPB.
 * @param buf The buffer containing data to be sent.
 * @param count The number of bytes in the buffer to be sent.
 * @return Returns true on successful transmission, false otherwise.
 */
bool out_http_binding(pb_ostream_t* stream, const uint8_t* buf, size_t count);

/**
 * @brief Callback function for input streaming with HTTP binding.
 * 
 * This function is designed to be used with NanoPB for streaming input data over HTTP.
 * It reads data into the given buffer from an HTTP connection.
 * 
 * The function is typically used as a callback in a `pb_istream_t` structure,
 * allowing NanoPB to receive data in a streaming manner from an HTTP source.
 * 
 * @param stream The input stream provided by NanoPB.
 * @param buf The buffer where data should be stored.
 * @param count The size of the buffer, indicating the maximum number of bytes to read.
 * @return Returns true on successful reception of data, false otherwise. When false
 *         is returned, it indicates an error in data reception or end of stream.
 */
bool in_http_binding(pb_istream_t* stream, pb_byte_t* buf, size_t count);

#ifdef __cplusplus
}
#endif
