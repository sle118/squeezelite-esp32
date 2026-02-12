#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "Locking.h"
#include "cpp_tools.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "PBW.h"
#include "accessors.h"
#include "esp_log.h"
#include "network_manager.h"
#include "pb.h"
#include "pb_decode.h" // Nanopb header for decoding (deserialization)
#include "pb_encode.h" // Nanopb header for encoding (serialization)
#include "tools.h"
#include <functional>
#include <sstream>
#include <vector>

namespace System {
const char* PBHelper::PROTOTAG = "PB";

void PBHelper::ResetModified() {
    ESP_LOGD(PROTOTAG, "Resetting the global commit flag for %s.", name.c_str());
    SetGroupBit(Flags::COMMITTED, true);
}

bool PBHelper::SetGroupBit(Flags flags, bool flag) {
    bool result = true;
    int bit_num = static_cast<int>(flags);
    int curFlags = xEventGroupGetBits(_group);
    if((curFlags & static_cast<int>(Flags::LOAD)) && flags == Flags::COMMITTED) {
        ESP_LOGD(PROTOTAG, "Loading %s, ignoring changes", name.c_str());
        result = false;
    }
    if(result) {
        if((curFlags & bit_num) == flag) {
            ESP_LOGV(PROTOTAG, "Flag %d already %s", bit_num, flag ? "Set" : "Cleared");
            result = false;
        }
    }
    if(result) {
        ESP_LOGV(PROTOTAG, "%s Flag %d ", flag ? "Setting" : "Clearing", bit_num);
        if(!flag) {
            xEventGroupClearBits(_group, bit_num);
        } else {
            xEventGroupSetBits(_group, bit_num);
        }
    }
    return result;
}

void PBHelper::SyncCommit(void* protoWrapper) {
    IPBBase* protoWrapperBase = reinterpret_cast<IPBBase*>(protoWrapper);
    if(protoWrapperBase) { protoWrapperBase->CommitChanges(); }
}
std::vector<pb_byte_t> PBHelper::EncodeData(const pb_msgdesc_t* fields, const void* src_struct) {
    size_t datasz;
    ESP_LOGV(PROTOTAG, "EncodeData: getting size");
    if(!pb_get_encoded_size(&datasz, fields, src_struct)) { throw std::runtime_error("Failed to get encoded size."); }
    ESP_LOGV(PROTOTAG, "EncodeData: size: %d. Encoding", datasz);
    std::vector<pb_byte_t> data(datasz);
    pb_ostream_t stream = pb_ostream_from_buffer(data.data(), datasz);
    if(!pb_encode(&stream, fields, src_struct)) { throw std::runtime_error("Failed to encode data."); }
    ESP_LOGV(PROTOTAG, "EncodeData: Done");
    return data;
}

void PBHelper::DecodeData(std::vector<pb_byte_t> data, const pb_msgdesc_t* fields, void* target, bool noinit) {
    pb_istream_t stream = pb_istream_from_buffer((uint8_t*)data.data(), data.size());
    bool result = false;

    // Decode the Protocol Buffers message
    if(noinit) {
        ESP_LOGD(PROTOTAG, "Decoding WITHOUT initialization");
        result = pb_decode_noinit(&stream, fields, data.data());
    } else {
        ESP_LOGD(PROTOTAG, "Decoding WITH initialization");
        result = pb_decode(&stream, fields, target);
    }
    if(!result) { throw std::runtime_error(std::string("Failed to decode settings: %s", PB_GET_ERROR(&stream))); }
    ESP_LOGD(PROTOTAG, "Data decoded");
}
void PBHelper::CommitFile(const std::string& filename, const pb_msgdesc_t* fields, const void* src_struct) {
    size_t datasz = 0;
    ESP_LOGD(PROTOTAG, "Committing data to file File %s", filename.c_str());

    if(!pb_get_encoded_size(&datasz, fields, src_struct)) { throw std::runtime_error("Failed to get encoded size."); }

    if(datasz == 0) {
        ESP_LOGW(PROTOTAG, "File %s not written. Data size is zero", filename.c_str());
        return;
    }

    ESP_LOGD(PROTOTAG, "Committing to file %s", filename.c_str());
    if(!src_struct) { throw std::runtime_error("Null pointer received."); }
    FILE* file = fopen(filename.c_str(), "wb");
    if(file == nullptr) { throw std::runtime_error(std::string("Error opening file ") + filename.c_str()); }
    pb_ostream_t filestream = PB_OSTREAM_SIZING;
    filestream.callback = &out_file_binding;
    filestream.state = file;
    filestream.max_size = SIZE_MAX;
    ESP_LOGD(PROTOTAG, "Starting file encode for %s", filename.c_str());
    if(!pb_encode(&filestream, fields, (void*)src_struct)) {
        fclose(file);
        throw std::runtime_error("Encoding file failed");
    }
    ESP_LOGD(PROTOTAG, "Encoded size: %d", filestream.bytes_written);
    if(filestream.bytes_written == 0) { ESP_LOGW(PROTOTAG, "Empty structure for file %s", filename.c_str()); }
    fclose(file);
}

bool PBHelper::IsDataDifferent(const pb_msgdesc_t* fields, const void* src_struct, const void* other_struct) {
    bool changed = false;
    try {
        ESP_LOGV(PROTOTAG, "Encoding Source data");
        auto src_data = EncodeData(fields, src_struct);
        ESP_LOGV(PROTOTAG, "Encoding Compared data");
        auto other_data = EncodeData(fields, other_struct);
        if(src_data.size() != other_data.size()) {
            ESP_LOGD(PROTOTAG, "IsDataDifferent: source and target size different: [%d!=%d]", src_data.size(), other_data.size());
            changed = true;
        } else if(src_data != other_data) {
            ESP_LOGD(PROTOTAG, "IsDataDifferent: source and target not the same");
            changed = true;
        }
        if(changed && esp_log_level_get(PROTOTAG) >= ESP_LOG_DEBUG) {
            ESP_LOGD(PROTOTAG, "Source data: ");
            dump_data((const uint8_t*)src_data.data(), src_data.size());
            ESP_LOGD(PROTOTAG, "Compared data: ");
            dump_data((const uint8_t*)other_data.data(), src_data.size());
        }
    } catch(const std::runtime_error& e) { throw std::runtime_error(std::string("Comparison failed: ") + e.what()); }
    ESP_LOGD(PROTOTAG, "IsDataDifferent: %s", changed ? "TRUE" : "FALSE");
    return changed;
}

void PBHelper::CopyStructure(const void* src_data, const pb_msgdesc_t* fields, void* target_data) {
    try {
        auto src = EncodeData(fields, src_data);
        ESP_LOGD(PROTOTAG, "Encoded structure to copy has %d bytes", src.size());
        DecodeData(src, fields, target_data, false);
    } catch(const std::runtime_error& e) { throw std::runtime_error(std::string("Copy failed: ") + e.what()); }
}
void PBHelper::LoadFile(const std::string& filename, const pb_msgdesc_t* fields, void* target_data, bool noinit) {
    struct stat fileInformation;
    if(!get_file_info(&fileInformation, filename.c_str()) || fileInformation.st_size == 0) { throw FileNotFoundException("filename"); }

    FILE* file = fopen(filename.c_str(), "rb");
    ESP_LOGI(PROTOTAG, "Loading file %s", filename.c_str());
    if(file == nullptr) {
        int errNum = errno;
        ESP_LOGE(PROTOTAG, "Unable to open file: %s. Error: %s", filename.c_str(), strerror(errNum));
        throw std::runtime_error("Unable to open file: " + filename + ". Error: " + strerror(errNum));
    }
    pb_istream_t filestream = PB_ISTREAM_EMPTY;
    filestream.callback = &in_file_binding;
    filestream.state = file;
    filestream.bytes_left = fileInformation.st_size;

    ESP_LOGV(PROTOTAG, "Starting decode.");
    bool result = false;
    if(noinit) {
        ESP_LOGV(PROTOTAG, "Decoding WITHOUT initialization");
        result = pb_decode_noinit(&filestream, fields, target_data);
    } else {
        ESP_LOGV(PROTOTAG, "Decoding WITH initialization");
        result = pb_decode(&filestream, fields, target_data);
    }
    fclose(file);
    if(!result) { throw System::DecodeError(PB_GET_ERROR(&filestream)); }
    ESP_LOGV(PROTOTAG, "Decode done.");
}

bool PBHelper::FileExists(std::string filename) {
    struct stat fileInformation;
    ESP_LOGD(PROTOTAG, "Checking if file %s exists", filename.c_str());
    return get_file_info(&fileInformation, filename.c_str()) && fileInformation.st_size > 0;
}
bool PBHelper::FileExists() { return FileExists(filename); }
bool PBHelper::IsLoading() { return xEventGroupGetBits(_group) & static_cast<int>(Flags::LOAD); }
void PBHelper::SetLoading(bool active) { SetGroupBit(Flags::LOAD, active); }

bool PBHelper::WaitForCommit(uint8_t retries = 2) {
    auto remain = retries;
    auto bits = xEventGroupGetBits(_group);
    bool commit_pending = HasChanges();
    ESP_LOGD(PROTOTAG, "Entering WaitForCommit bits: %d, changes? %s", bits, commit_pending ? "YES" : "NO");
    while(commit_pending && remain-- > 0) {
        ESP_LOGD(PROTOTAG, "Waiting for config commit ...");
        auto bits = xEventGroupWaitBits(_group, static_cast<int>(Flags::COMMITTED), pdFALSE, pdTRUE, (MaxDelay * 2) / portTICK_PERIOD_MS);
        commit_pending = !(bits & static_cast<int>(Flags::COMMITTED));
        ESP_LOGD(PROTOTAG, "WaitForCommit bits: %d, changes? %s", bits, commit_pending ? "YES" : "NO");
        if(commit_pending) {
            ESP_LOGW(PROTOTAG, "Timeout waiting for config commit for [%s]", name.c_str());
        } else {
            ESP_LOGI(PROTOTAG, "Changes to %s committed", name.c_str());
        }
    }
    return !commit_pending;
}

void PBHelper::RaiseChangedAsync() {
    if(_no_save) { ESP_LOGD(PROTOTAG, "Ignoring changes for %s, as it is marked not to be saved", name.c_str()); }
    ESP_LOGI(PROTOTAG, "Changes made to %s", name.c_str());
    if(IsLoading()) {
        ESP_LOGD(PROTOTAG, "Ignoring raise modified during load of %s", name.c_str());
    } else {
        SetGroupBit(Flags::COMMITTED, false);
        network_async_commit_protowrapper(static_cast<void*>(static_cast<IPBBase*>(this)));
    }
}
const std::string& PBHelper::GetFileName() { return filename; }
bool PBHelper::HasChanges() { return !(xEventGroupGetBits(_group) & static_cast<int>(Flags::COMMITTED)); }
} // namespace System

bool proto_load_file(const char* filename, const pb_msgdesc_t* fields, void* target_data, bool noinit = false) {
    try {
        ESP_LOGI(System::PBHelper::PROTOTAG, "Loading file %s", filename);
        System::PBHelper::LoadFile(std::string(filename), fields, target_data, noinit);
    } catch(const std::exception& e) {
        if(!noinit) {
            // initialize the structure
            pb_istream_t stream = pb_istream_from_buffer(nullptr, 0);
            pb_decode(&stream, fields, target_data);
        }
        ESP_LOGE(System::PBHelper::PROTOTAG, "Error loading file %s: %s", filename, e.what());
        return false;
    }
    return true;
}
