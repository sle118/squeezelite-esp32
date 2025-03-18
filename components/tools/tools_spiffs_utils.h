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
#include "esp_spiffs.h"
#include "esp_system.h"
#include "pb.h"        // Nanopb header for encoding (serialization)
#include "pb_decode.h" // Nanopb header for decoding (deserialization)
#include "pb_encode.h" // Nanopb header for encoding (serialization)
#include "sys/stat.h"

#include <stdlib.h>

#ifdef __cplusplus
#include <cstdarg> // for va_list, va_start, va_end
#include <list>
#include <string>
#include <vector>
/**
 * @brief Represents a file entry.
 */
typedef struct {
    char type;        /**< File type ('F' for regular file, 'D' for directory). */
    long size;        /**< File size in bytes. */
    std::string name; /**< File or directory name. */
    bool restricted;  /**< Restricted (system controled)*/
} tools_file_entry_t;

extern const std::vector<std::string> restrictedPaths;

/**
 * @brief Retrieve a list of file entries in the specified directory.
 *
 * This function collects information about files and directories in the specified
 * directory path. The caller is responsible for crafting the complete path
 * (including any necessary SPIFFS base path).
 *
 * @param path_requested The directory path for which to list files and directories.
 * @return A std::list of tools_file_entry_t representing the files and directories in the specified
 * path.
 *
 * @note The caller is responsible for adding the SPIFFS base path if needed.
 *
 * Example usage:
 * @code
 * std::string base_path = "/spiffs";
 * std::string directory = "/some_directory";
 * std::string full_path = base_path + directory;
 * std::list<tools_file_entry_t> fileList = get_files_list(full_path);
 * for (const auto& entry : fileList) {
 *     // Access entry.type, entry.size, and entry.name
 * }
 * @endcode
 */

std::list<tools_file_entry_t> get_files_list(const std::string& path_requested);

extern "C" {
#endif

void init_spiffs();
extern const char* spiffs_base_path;

/**
 * @brief Retrieves information about a file.
 *
 * This function uses the stat system call to fill a struct stat with information about the file.
 *
 * @param pfileInfo Pointer to a struct stat where file information will be stored.
 * @param filename The file path to get info for
 * @return bool True if the file information was successfully retrieved, false otherwise.
 */
bool get_file_info(struct stat* pfileInfo, const char* filename);

/**
 * @brief Loads the entire content of a file into memory.
 *
 * This function opens a file in binary read mode and loads its entire
 * content into a memory buffer. The memory for the buffer is allocated based
 * on the specified memory flags. The size of the loaded data is stored in the
 * variable pointed to by 'sz'.
 *
 * @param memflags Flags indicating the type of memory to allocate for the buffer.
 *                 This can be a combination of memory capabilities like MALLOC_CAP_SPIRAM,
 *                 MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL, MALLOC_CAP_DMA, etc.
 * @param sz Pointer to a size_t variable where the size of the loaded data will be stored.
 *           Optional if loaded size is needed.
 * @param filename The path of the file to load. The file should exist and be readable.
 * @return A pointer to the allocated memory containing the file data. Returns NULL if the
 *         file cannot be opened, if memory allocation fails, or if the file is empty.
 */
void* load_file(uint32_t memflags, size_t* sz, const char* filename);

/**
 * @brief Macro to load a file into PSRAM (Pseudo-Static Random Access Memory).
 *
 * This macro is a convenience wrapper for 'load_file' to load file data into PSRAM.
 * It is suitable for larger data that does not fit into the internal memory.
 *
 * @param pSz Pointer to a size_t variable to store the size of the loaded data.
 * @param filename The path of the file to load.
 * @return A pointer to the allocated memory in PSRAM containing the file data, or NULL on failure.
 */
#define load_file_psram(pSz, filename) load_file(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, pSz, filename)

/**
 * @brief Macro to load a file into DMA-capable internal memory.
 *
 * This macro is a convenience wrapper for 'load_file' to load file data into
 * DMA-capable internal memory. It is suitable for smaller data that needs to be
 * accessed by DMA controllers.
 *
 * @param pSz Pointer to a size_t variable to store the size of the loaded data.
 * @param filename The path of the file to load.
 * @return A pointer to the allocated memory in internal DMA-capable memory, or NULL on failure.
 */
#define load_file_dma(pSz, filename) load_file(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA, pSz, filename)

/**
 * @brief Erases files matching a specified pattern within a path.
 *
 * This function deletes files that match the given wildcard pattern in the specified path.
 * If the 'restricted' flag is set to true, the function will not delete files that match
 * any of the paths specified in the restricted paths list.
 *
 * @param path A string representing the path and wildcard pattern of files to be deleted.
 *             Example: "folder/test*.txt" will attempt to delete all test*.txt files in the
 * 'folder' directory.
 * @param restricted A boolean flag indicating whether to apply restrictions on file deletion.
 *                   If true, files matching the restricted paths will not be deleted.
 * @return Returns true if all files matching the pattern are successfully deleted or skipped (in
 * case of restrictions). Returns false if there is an error in deleting any of the files or if the
 * directory cannot be opened.
 */
bool erase_path(const char* path, bool restricted);

/**
 * @brief Checks if a given filename matches any of the restricted paths.
 *
 * This function determines whether the provided filename matches any pattern specified
 * in the list of restricted paths. It is typically used to prevent certain files or directories
 * from being modified or deleted.
 *
 * @param filename The name of the file to check against the restricted paths.
 * @return Returns true if the filename matches any of the restricted paths.
 *         Returns false otherwise.
 */
bool is_restricted_path(const char* filename);

bool in_file_binding(pb_istream_t* stream, pb_byte_t* buf, size_t count);
bool out_file_binding(pb_ostream_t* stream, const uint8_t* buf, size_t count);

/**
 * @brief Writes binary data to a file.
 *
 * This function writes a given array of bytes (data) to a file. The file path
 * is constructed from a variable number of string arguments.
 *
 * @param data Pointer to the data array to be written.
 * @param sz Size of the data array in bytes.
 * @param filename The file path to write to
 * @return bool True if the file is written successfully, false otherwise.
 *
 * @note This function initializes the SPIFFS before writing and logs errors.
 */

bool write_file(const uint8_t* data, size_t sz, const char* filename);

void listFiles(const char* path_requested);

/**
 * @brief Prints the content of a specified file.
 *
 * This function reads the content of the file specified by `filename` and prints it to the standard
 * output. Each byte of the file is checked to determine if it is a printable ASCII character. If a
 * byte is printable, it is printed as a character; otherwise, it is printed in its hexadecimal
 * representation.
 *
 * The function utilizes `load_file_psram` to load the file into memory. It is assumed that
 * `load_file_psram` handles the opening and closing of the file, as well as memory allocation and
 * error handling.
 *
 * @param filename The path of the file to be printed. It should be a null-terminated string.
 *
 * @return Returns `true` if the file is successfully read and printed. Returns `false` if the file
 *         cannot be opened, read, or if any other error occurs during processing.
 *
 * @note The function prints a hexadecimal representation (prefixed with \x) for non-printable
 * characters. For example, a byte with value 0x1F is printed as \x1F.
 *
 * @warning The function assumes that `load_file_psram` returns a `NULL` pointer if the file cannot
 * be loaded or if any error occurs. Ensure that `load_file_psram` adheres to this behavior.
 */
bool cat_file(const char* filename);

/**
 * @brief Dumps the given data to standard output.
 *
 * This function prints the provided data array to the standard output.
 * If the data is printable (as per the `isprint` standard function), it is printed
 * as a character. Otherwise, it is printed in hexadecimal format. The function
 * also checks for null data or zero length and reports it before returning false.
 *
 * @param pData Pointer to the data array to be dumped.
 * @param length The length of the data array.
 *
 * @return Returns `true` if the data is valid (not null and non-zero length), `false` otherwise.
 *
 * @note The function prints a newline character after dumping the entire data array.
 */
bool dump_data(const uint8_t* pData, size_t length);

/**
 * @brief Encodes a protobuf structure and dumps the encoded data to the console.
 *
 * This method takes a protobuf message structure, encodes it using NanoPB, and then dumps
 * the encoded data to the console. It is useful for debugging purposes to visualize
 * the encoded protobuf data.
 *
 * @param fields Pointer to the field descriptions array (generated by NanoPB).
 * @param src_struct Pointer to the structure instance to be encoded.
 *
 * @return Returns `true` if the data was successfully encoded and dumped, `false` otherwise.
 */
bool dump_structure(const pb_msgdesc_t* fields, const void* src_struct);

#ifdef __cplusplus
}
#endif
