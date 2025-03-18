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
#ifndef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#endif

#include "Locking.h"
#include "MessageDefinition.pb.h"
#include "accessors.h"
#include "configuration.pb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "network_manager.h"
#include "pb.h"
#include "pb_decode.h" // Nanopb header for decoding (deserialization)
#include "pb_encode.h" // Nanopb header for encoding (serialization)
#include "tools.h"
#include "tools_spiffs_utils.h"
#include "bootstate.h"
#include "State.pb.h"

#ifdef __cplusplus
// #include <functional>
#include <sstream>
#include <vector>

namespace System {
template <typename T> struct has_target_implementation : std::false_type {};

class FileNotFoundException : public std::runtime_error {
  public:
    explicit FileNotFoundException(const std::string& message) : std::runtime_error(message) {}
};
class DecodeError : public std::runtime_error {
  public:
    explicit DecodeError(const std::string& message) : std::runtime_error(message) {}
};
class IPBBase {
  public:
    virtual void CommitChanges() = 0;
    virtual ~IPBBase() {}
};
class PBHelper : public IPBBase {

  protected:
    const pb_msgdesc_t* _fields;
    EventGroupHandle_t _group;
    std::string name;
    std::string filename;
    static const int MaxDelay = 1000;
    Locking _lock;
    size_t _datasize;    
    bool _no_save;

  public:
    enum class Flags { COMMITTED = BIT0, LOAD = BIT1 };
    bool SetGroupBit(Flags flags, bool flag);
    static const char* PROTOTAG;
    std::string& GetName() { return name; }
    const char* GetCName() { return name.c_str(); }
    static std::string GetDefFileName(std::string name){
        return std::string(spiffs_base_path) + "/data/def_" + name + ".bin";
    }
    std::string GetDefFileName(){
        return GetDefFileName(this->name);
    }
    size_t GetDataSize(){
        return  _datasize;
    }
    PBHelper(std::string name, const pb_msgdesc_t* fields, size_t defn_size, size_t datasize, bool no_save = false)
        : _fields(fields), _group(xEventGroupCreate()), name(std::move(name)), _lock(this->name),_datasize(datasize), _no_save(no_save) {
        sys_message_def definition = sys_message_def_init_default;
        bool savedef = false;
        ESP_LOGD(PROTOTAG,"Getting definition file name");
        auto deffile = GetDefFileName();
        ESP_LOGD(PROTOTAG,"Instantiating with definition size %d and data size %d", defn_size,datasize);
        
        try {
            PBHelper::LoadFile(deffile, &sys_message_def_msg, static_cast<void*>(&definition));
            if (definition.data->size != defn_size || definition.datasize != _datasize) {
                ESP_LOGW(PROTOTAG, "Structure definition %s has changed", this->name.c_str());
                if (!is_recovery_running) {
                    savedef = true;
                    pb_release(&sys_message_def_msg, &definition);
                } else {
                    ESP_LOGW(PROTOTAG, "Using existing definition for recovery");
                    _fields = reinterpret_cast<const pb_msgdesc_t*>(definition.data->bytes);
                    _datasize = definition.datasize;
                }
            }
        } catch (const FileNotFoundException& e) {
            savedef = true;
        }

        if (savedef) {
            ESP_LOGW(PROTOTAG, "Saving definition for structure %s", this->name.c_str());
            auto data = (pb_bytes_array_t*)malloc_init_external(sizeof(pb_bytes_array_t)+defn_size);
            memcpy(&data->bytes, fields, defn_size);
            data->size = defn_size;
            definition.data = data;
            definition.datasize = _datasize;
            ESP_LOGD(PROTOTAG,"Committing structure with %d bytes ",data->size);
            PBHelper::CommitFile(deffile, &sys_message_def_msg, &definition);
            ESP_LOGD(PROTOTAG,"Releasing memory");
            free(data);
        }
    }
    void ResetModified();
    static void SyncCommit(void* protoWrapper);
    static void CommitFile(const std::string& filename, const pb_msgdesc_t* fields, const void* src_struct);
    static bool IsDataDifferent(const pb_msgdesc_t* fields, const void* src_struct, const void* other_struct);

    static void CopyStructure(const void* src_data, const pb_msgdesc_t* fields, void* target_data);
    static void LoadFile(const std::string& filename, const pb_msgdesc_t* fields, void* target_data, bool noinit = false);
    static std::vector<pb_byte_t> EncodeData(const pb_msgdesc_t* fields, const void* src_struct);
    static void DecodeData(std::vector<pb_byte_t> data, const pb_msgdesc_t* fields, void* target, bool noinit = false);
    bool FileExists(std::string filename);
    bool FileExists();
    bool IsLoading();
    void SetLoading(bool active);
    bool WaitForCommit(uint8_t retries );
    void RaiseChangedAsync();
    const std::string& GetFileName();
    bool HasChanges();
};
template <typename T> class PB : public PBHelper {
  private:
    T *_root;

    // Generic _setTarget implementation
    void _setTarget(std::string target, std::false_type) { ESP_LOGE(PROTOTAG, "Setting target not implemented for %s", name.c_str()); }

    // Special handling for sys_config
    void _setTarget(std::string target, std::true_type) {
        if (_root->target) {
            free(_root->target);
        }
        _root->target = strdup_psram(target.c_str());
    }
    std::string _getTargetName(std::false_type) { return ""; }
    std::string _getTargetName(std::true_type) { return STR_OR_BLANK(_root->target); }

  public:
    // Accessor for the underlying structure
    T& Root() { return *_root; }
    // Const accessor for the underlying structure

    const T& Root() const { return *_root; }
    T* get()  { return _root; }
    const T* get() const { return (const T*)_root; }

    // Constructor
    explicit PB(std::string name, const pb_msgdesc_t* fields, size_t defn_size, bool no_save = false) : 
        PBHelper(std::move(name), fields,defn_size, sizeof(T), no_save) {
        ESP_LOGD(PROTOTAG, "Instantiating PB class for %s with data size %d", this->name.c_str(), sizeof(T));
        ResetModified();
        filename = std::string(spiffs_base_path) + "/data/" + this->name + ".bin";
        _root = (T*)(malloc_init_external(_datasize));
        memset(_root, 0x00, sizeof(_datasize));
    }

    std::string GetTargetName() { return _getTargetName(has_target_implementation<T>{}); }
    void SetTarget(std::string targetname, bool skip_commit = false) {
        std::string newtarget = trim(targetname);
        std::string currenttarget = trim(GetTargetName());
        ESP_LOGD(PROTOTAG, "SetTarget called with %s", newtarget.c_str());
        if (newtarget == currenttarget && !newtarget.empty()) {
            ESP_LOGD(PROTOTAG, "Target name %s not changed for %s", currenttarget.c_str(), name.c_str());
        } else if (newtarget.empty() && !currenttarget.empty()) {
            ESP_LOGW(PROTOTAG, "Target name %s was removed for %s ", currenttarget.c_str(), name.c_str());
        }
        ESP_LOGI(PROTOTAG, "Setting target %s for %s", newtarget.c_str(), name.c_str());
        _setTarget(newtarget, has_target_implementation<T>{});
        if (!skip_commit) {
            ESP_LOGD(PROTOTAG, "Raising changed flag to commit new target name.");
            RaiseChangedAsync();
        } else {
            SetGroupBit(Flags::COMMITTED, false);
        }
    }
    std::string GetTargetFileName() {
        if (GetTargetName().empty()) {
            return "";
        }
        auto target_name = GetTargetName();
        return std::string(spiffs_base_path) + "/targets/" + toLowerStr(target_name) + "/" + name + ".bin";
    }
    void Reinitialize(bool skip_target = false, bool commit = false, std::string target_name = "") {
        ESP_LOGW(PROTOTAG, "Initializing %s", name.c_str());
        pb_istream_t stream = PB_ISTREAM_EMPTY;
        // initialize blank structure by
        // decoding a dummy stream
        pb_decode(&stream, _fields, _root);
        SetLoading(true);
        try {
            std::string fullpath = std::string(spiffs_base_path) + "/defaults/" + this->name + ".bin";
            ESP_LOGD(PROTOTAG, "Attempting to load defaults file for %s", fullpath.c_str());
            PBHelper::LoadFile(fullpath.c_str(), _fields, static_cast<void*>(_root), true);
        } catch (FileNotFoundException&) {
            ESP_LOGW(PROTOTAG, "No defaults found for %s", name.c_str());
        } catch (std::runtime_error& e) {
            ESP_LOGE(PROTOTAG, "Error loading Target %s overrides file: %s", GetTargetName().c_str(), e.what());
        }
        SetLoading(false);
        if (!skip_target) {
            if (!target_name.empty()) {
                SetTarget(target_name, true);
            }
            LoadTargetValues();
        }
        if (commit) {
            CommitChanges();
        }
    }
    void LoadFile(bool skip_target = false, bool noinit = false) {
        SetLoading(true);
        PBHelper::LoadFile(filename, _fields, static_cast<void*>(_root), noinit);
        SetLoading(false);
        if (!skip_target) {
            LoadTargetValues();
        }
    }
    void LoadTargetValues() {
        ESP_LOGD(PROTOTAG, "Loading target %s values for %s", GetTargetName().c_str(), name.c_str());
        if (GetTargetFileName().empty()) {
            ESP_LOGD(PROTOTAG, "No target file to load for %s", name.c_str());
            return;
        }
        try {
            // T old;
            // CopyTo(old);
            ESP_LOGI(PROTOTAG, "Loading target %s values for %s", GetTargetName().c_str(), name.c_str());
            PBHelper::LoadFile(GetTargetFileName(), _fields, static_cast<void*>(_root), true);
            // don't commit the values here, as it doesn't work well with
            // repeated values
            // if (*this != old) {
            //     ESP_LOGI(PROTOTAG, "Changes detected from target values.");
            //     RaiseChangedAsync();
            // }
            SetGroupBit(Flags::COMMITTED, false);

        } catch (FileNotFoundException&) {
            ESP_LOGD(PROTOTAG, "Target %s overrides file not found for %s", GetTargetName().c_str(), name.c_str());
        } catch (std::runtime_error& e) {
            ESP_LOGE(PROTOTAG, "Error loading Target %s overrides file: %s", GetTargetName().c_str(), e.what());
        }
    }
    void CommitChanges() override {
        ESP_LOGI(PROTOTAG, "Committing %s to flash.", name.c_str());
        if (!_lock.Lock()) {
            ESP_LOGE(PROTOTAG, "Unable to lock config for commit ");
            return;
        }
        ESP_LOGV(PROTOTAG, "Config Locked. Committing");
        try {
            CommitFile(filename, _fields, _root);
        } catch (...) {
        }
        ResetModified();
        _lock.Unlock();
        ESP_LOGI(PROTOTAG, "Done committing %s to flash.", name.c_str());
    }
    bool Lock() { return _lock.Lock(); }
    void Unlock() { return _lock.Unlock(); }
    std::vector<pb_byte_t> Encode() {
        auto data = std::vector<pb_byte_t>();
        if (!_lock.Lock()) {
            throw std::runtime_error("Unable to lock object");
        }
        data = EncodeData(_fields, this->_root);
        _lock.Unlock();
        return data;
    }

    void CopyTo(T& target_data) {
        if (!_lock.Lock()) {
            ESP_LOGE(PROTOTAG, "Lock failed for %s", name.c_str());
            throw std::runtime_error("Lock failed ");
        }
        CopyStructure(_root, _fields, &target_data);
        _lock.Unlock();
    }
    void CopyFrom(const T& source_data) {
        if (!_lock.Lock()) {
            ESP_LOGE(PROTOTAG, "Lock failed for %s", name.c_str());
            throw std::runtime_error("Lock failed ");
        }
        CopyStructure(&source_data, _fields, _root);
        _lock.Unlock();
    }

    bool operator!=(const T& other) const { return IsDataDifferent(_fields, _root, &other); }
    bool operator==(const T& other) const { return !IsDataDifferent(_fields, _root, &other); }
    void DecodeData(const std::vector<pb_byte_t> data, bool noinit = false) { PBHelper::DecodeData(data, _fields, (void*)_root, noinit); }

    ~PB() { vEventGroupDelete(_group); };
};
template <> struct has_target_implementation<sys_config> : std::true_type {};

template <> struct has_target_implementation<sys_state_data> : std::true_type {};

} // namespace PlatformConfig
extern "C" {
#endif
bool proto_load_file(const char* filename, const pb_msgdesc_t* fields, void* target_data, bool noinit);

#ifdef __cplusplus
}
#endif