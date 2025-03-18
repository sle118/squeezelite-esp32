#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "tools_spiffs_utils.h"
#include "esp_log.h"
#include "tools.h"

#include "PBW.h"
#include <ctype.h> // For isprint()
#include <dirent.h>
#include <fnmatch.h>
#include <iomanip> // for std::setw
#include <set>
#include <sstream>
#include <sys/stat.h> // for file stat
#include <vector>

static const char* TAG = "spiffs_utils";
static bool initialized = false;
static esp_vfs_spiffs_conf_t* spiffs_conf = NULL;
const char* spiffs_base_path = "/spiffs";
// Struct to represent a file entry

// list of reserved files that the system controls
const std::vector<std::string> restrictedPaths = {
    "/spiffs/defaults/*", "/spiffs/fonts/*", "/spiffs/targets/*", "/spiffs/www/*"};

void init_spiffs() {
    if (initialized) {
        ESP_LOGV(TAG, "SPIFFS already initialized. returning");
        return;
    }
    ESP_LOGI(TAG, "Initializing the SPI File system");
    spiffs_conf = (esp_vfs_spiffs_conf_t*)malloc(sizeof(esp_vfs_spiffs_conf_t));
    spiffs_conf->base_path = spiffs_base_path;
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

bool write_file(const uint8_t* data, size_t sz, const char* filename) {
    bool result = true;
    FILE* file = NULL;
    init_spiffs();
    if (data == NULL) {
        ESP_LOGE(TAG, "Cannot write file. Data not received");
        return false;
    }
    if (sz == 0) {
        ESP_LOGE(TAG, "Cannot write file. Data length 0");
        return false;
    }
    file = fopen(filename, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Error opening %s for writing", filename);
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

void* load_file(uint32_t memflags, size_t* sz, const char* filename) {
    void* data = NULL;
    FILE* file = NULL;
    init_spiffs();
    size_t fsz = 0;
    file = fopen(filename, "rb");

    if (file == NULL) {
        return data;
    }
    fseek(file, 0, SEEK_END);
    fsz = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fsz > 0) {
        ESP_LOGD(TAG, "Allocating %d bytes to load file %s content with flags: %s ", fsz, filename,
            get_mem_flag_desc(memflags));
        data = (void*)heap_caps_calloc(1, fsz, memflags);
        if (data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes to load file %s", fsz, filename);
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
bool get_file_info(struct stat* pfileInfo, const char* filename) {
    // ensure that the spiffs is initialized
    struct stat fileInfo;
    init_spiffs();
    if (strlen(filename) == 0) {
        ESP_LOGE(TAG, "Invalid file name");
        return false;
    }
    ESP_LOGD(TAG, "Getting file info for %s", filename);
    bool result = false;
    // Use stat to fill the fileInfo structure
    if (stat(filename, &fileInfo) != 0) {
        ESP_LOGD(TAG, "File %s not found", filename);
    } else {
        result = true;
        if (pfileInfo) {
            memcpy(pfileInfo, &fileInfo, sizeof(fileInfo));
        }
        ESP_LOGD(TAG, "File %s has %lu bytes", filename, fileInfo.st_size);
    }

    return result;
}

bool is_restricted_path(const char* filename) {
    for (const auto& pattern : restrictedPaths) {
        if (fnmatch(pattern.c_str(), filename, 0) == 0) {
            return true;
        }
    }
    return false;
}

bool erase_path(const char* filename, bool restricted) {
    std::string full_path_with_wildcard = std::string(filename);
    if (full_path_with_wildcard.empty()) {
        ESP_LOGE(TAG, "Error constructing full path");
        return false;
    }

    ESP_LOGI(TAG, "Erasing file(s) matching pattern %s", full_path_with_wildcard.c_str());

    // Extract directory path and wildcard pattern
    size_t lastSlashPos = full_path_with_wildcard.find_last_of('/');
    std::string dirpath = full_path_with_wildcard.substr(0, lastSlashPos);
    std::string wildcard = full_path_with_wildcard.substr(lastSlashPos + 1);
    ESP_LOGD(TAG, "Last slash pos: %d, dirpath: %s, wildcard %s ", lastSlashPos, dirpath.c_str(),
        wildcard.c_str());
    DIR* dir = opendir(dirpath.empty() ? "." : dirpath.c_str());
    if (!dir) {
        ESP_LOGE(TAG, "Error opening directory");
        return false;
    }

    bool result = false;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (fnmatch(wildcard.c_str(), ent->d_name, 0) == 0) {
            std::string fullfilename = dirpath + "/" + ent->d_name;
            // Check if the file is restricted
            if (restricted && is_restricted_path(fullfilename.c_str())) {
                ESP_LOGW(TAG, "Skipping restricted file %s", fullfilename.c_str());
                continue;
            }
            ESP_LOGW(TAG, "Deleting file %s", fullfilename.c_str());
            if (remove(fullfilename.c_str()) != 0) {
                ESP_LOGE(TAG, "Error deleting file %s", fullfilename.c_str());
            } else {
                result = true;
            }
        } else {
            ESP_LOGV(
                TAG, "%s does not match file pattern to delete: %s", ent->d_name, wildcard.c_str());
        }
    }

    closedir(dir);
    return result;
}

// Function to format and print a file entry
void printFileEntry(const tools_file_entry_t& entry) {
    const char* suffix;
    double size;

    // Format the size
    if (entry.type == 'F') {
        if (entry.size < 1024) { // less than 1 KB
            size = entry.size;
            suffix = "  B";
            printf("%c %10.0f%s %-80s%4s\n", entry.type, size, suffix, entry.name.c_str(),
                entry.restricted ? "X" : "-");
        } else {
            if (entry.size < 1024 * 1024) { // 1 KB to <1 MB
                size = entry.size / 1024;
                suffix = " KB";
            } else { // 1 MB and above
                size = entry.size / (1024 * 1024);
                suffix = " MB";
            }
            printf("%c %10.0f%s %-80s%4s\n", entry.type, size, suffix, entry.name.c_str(),
                entry.restricted ? "X" : "-");
        }

    } else {
        printf("%c             - %-80s%4s\n", entry.type, entry.name.c_str(),
            entry.restricted ? "X" : "-");
    }
}

void listFiles(const char* path_requested_char) {
    // Ensure that the SPIFFS is initialized
    init_spiffs();
    auto path_requested = std::string(path_requested_char);
    auto filesList = get_files_list(path_requested);
    printf("---------------------------------------------------------------------------------------"
           "---------------\n");
    printf("T           SIZE NAME                                                                  "
           "           RSTR\n");
    printf("---------------------------------------------------------------------------------------"
           "---------------\n");

    uint64_t total = 0;
    int nfiles = 0;
    for (auto& e : filesList) {
        if (e.type == 'F') {
            total += e.size;
            nfiles++;
        }
        printFileEntry(e);
    }
    printf("---------------------------------------------------------------------------------------"
           "---------------\n");
    if (total > 0) {
        printf("Total : %lu  bytes in %d file(s)\n", (unsigned long)total, nfiles);
    }

    uint32_t tot = 0, used = 0;
    esp_spiffs_info(NULL, &tot, &used);
    printf("SPIFFS: free %d KB of %d KB\n", (tot - used) / 1024, tot / 1024);
    printf("---------------------------------------------------------------------------------------"
           "---------------\n");
}

bool out_file_binding(pb_ostream_t* stream, const uint8_t* buf, size_t count) {
    FILE* file = (FILE*)stream->state;
    ESP_LOGV(TAG, "Writing %d bytes to file", count);
    return fwrite(buf, 1, count, file) == count;
}
bool in_file_binding(pb_istream_t* stream, pb_byte_t* buf, size_t count) {
    FILE* file = (FILE*)stream->state;
    ESP_LOGV(TAG, "Reading %d bytes from file", count);
    return fread(buf, 1, count, file) == count;
}

// Function to list files matching the path_requested and return a std::list of FileEntry
std::list<tools_file_entry_t> get_files_list(const std::string& path_requested) {
    std::list<tools_file_entry_t> fileList;
    std::set<std::string> directoryNames;

    struct dirent* ent;
    struct stat sb;

    // Ensure that the SPIFFS is initialized
    init_spiffs();

    std::string prefix = (path_requested.back() != '/' ? path_requested + "/" : path_requested);
    DIR* dir = opendir(path_requested.c_str());
    if (!dir) {
        ESP_LOGE(TAG, "Error opening directory %s ", path_requested.c_str());
        return fileList;
    }

    while ((ent = readdir(dir)) != NULL) {
        tools_file_entry_t fileEntry;
        fileEntry.name = prefix + ent->d_name;
        fileEntry.type = (ent->d_type == DT_REG) ? 'F' : 'D';
        fileEntry.restricted = is_restricted_path(fileEntry.name.c_str());
        if (stat(fileEntry.name.c_str(), &sb) == -1) {
            ESP_LOGE(TAG, "Ignoring file %s ", fileEntry.name.c_str());
            continue;
        }
        fileEntry.size = sb.st_size;
        fileList.push_back(fileEntry);

        // Extract all parent directory names
        size_t pos = 0;
        while ((pos = fileEntry.name.find('/', pos + 1)) != std::string::npos) {
            directoryNames.insert(fileEntry.name.substr(0, pos));
        }
    }
    closedir(dir);

    // Add directories to the file list
    for (const auto& dirName : directoryNames) {
        tools_file_entry_t dirEntry;
        dirEntry.name = dirName;
        dirEntry.type = 'D'; // Mark as directory
        fileList.push_back(dirEntry);
    }

    // Sort the list by directory/file name
    fileList.sort(
        [](const tools_file_entry_t& a, const tools_file_entry_t& b) { return a.name < b.name; });

    // Remove duplicates
    fileList.unique(
        [](const tools_file_entry_t& a, const tools_file_entry_t& b) { return a.name == b.name; });

    return fileList;
}
bool cat_file(const char* filename) {
    size_t sz;
    uint8_t* content = (uint8_t*)load_file_psram(&sz, filename);

    if (content == NULL) {
        printf("Failed to load file\n");
        return false;
    }

    for (size_t i = 0; i < sz; i++) {
        if (isprint(content[i])) {
            printf("%c", content[i]); // Print as a character
        } else {
            printf("\\x%02x", content[i]); // Print as hexadecimal
        }
    }

    printf("\n"); // New line after printing the content
    free(content);
    return true;
}

bool dump_data(const uint8_t* pData, size_t length) {
    if (pData == NULL && length == 0) {
        printf("%s/%s\n", pData == nullptr ? "Invalid Data" : "Data OK",
            length == 0 ? "Invalid Length" : "Length OK");
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        if (isprint((char)pData[i])) {
            printf("%c", (char)pData[i]); // Print as a character
        } else {
            printf("\\x%02x", (char)pData[i]); // Print as hexadecimal
        }
    }

    printf("\n"); // New line after printing the content
    return true;
}

bool dump_structure(const pb_msgdesc_t* fields, const void* src_struct) {
    try {
        std::vector<pb_byte_t> encodedData = System::PBHelper::EncodeData(fields, src_struct);
        return dump_data(encodedData.data(), encodedData.size());
    } catch (const std::runtime_error& e) {
        ESP_LOGE(TAG, "Error in dump_structure: %s", e.what());
        return false;
    }
}