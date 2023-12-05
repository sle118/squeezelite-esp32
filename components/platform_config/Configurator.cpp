#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "Configurator.h"
#include "esp_log.h"
#include "esp_system.h"
#include "pb_common.h" // Nanopb header for encoding (serialization)
#include "pb_decode.h" // Nanopb header for decoding (deserialization)
#include "pb_encode.h" // Nanopb header for encoding (serialization)
// #include "sys_options.h"
#include "tools.h"
#include <string.h>
#include <algorithm>
static const char* TAG = "Configurator";
static const char* targets_folder = "targets";
static const char* config_file_name = "settings.bin";
static const char* state_file_name = "state.bin";
__attribute__((section(".ext_ram.bss"))) PlatformConfig::Configurator configurator;
sys_Config* platform = NULL;
sys_State* sys_state = NULL;

bool set_pb_string_from_mac(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    if (!stream) {
        // This is a size calculation pass, return true to indicate field presence
        return true;
    }

    // Generate the string based on MAC and prefix
    const char* prefix = reinterpret_cast<const char*>(*arg);
    char* value = alloc_get_string_with_mac(prefix && strlen(prefix) > 0 ? prefix : "squeezelite-");

    // Write the string to the stream
    if (!pb_encode_string(stream, (uint8_t*)value, strlen(value))) {
        free(value); // Free memory if encoding fails
        return false;
    }

    free(value); // Free memory after encoding
    return true;
}

namespace PlatformConfig {

EXT_RAM_ATTR static const int NO_COMMIT_PENDING = BIT0;
EXT_RAM_ATTR static const int LOAD_BIT = BIT1;
EXT_RAM_ATTR static const int NO_STATE_COMMIT_PENDING = BIT2;

const int Configurator::MaxDelay = 1000;
const int Configurator::LockMaxWait = 20 * Configurator::MaxDelay;

EXT_RAM_ATTR TimerHandle_t Configurator::_timer;
EXT_RAM_ATTR SemaphoreHandle_t Configurator::_mutex;
EXT_RAM_ATTR SemaphoreHandle_t Configurator::_state_mutex;
EXT_RAM_ATTR EventGroupHandle_t Configurator::_group;

static void ConfiguratorCallback(TimerHandle_t xTimer) {
    static int cnt = 0, scnt = 0;
    if (configurator.HasChanges()) {
        ESP_LOGI(TAG, "Configuration has some uncommitted entries");
        configurator.CommitChanges();
    } else {
        if (++cnt >= 15) {
            ESP_LOGV(TAG, "commit timer: commit flag not set");
            cnt = 0;
        }
    }
    if (configurator.HasStateChanges()) {
        ESP_LOGI(TAG, "State has some uncommitted changes");
        configurator.CommitState();
    } else {
        if (++scnt >= 15) {
            ESP_LOGV(TAG, "commit timer: commit flag not set");
            cnt = 0;
        }
    }    
    xTimerReset(xTimer, 10);
}
void Configurator::RaiseStateModified() { SetGroupBit(NO_STATE_COMMIT_PENDING, false); }
void Configurator::RaiseModified() { SetGroupBit(NO_COMMIT_PENDING, false); }
void Configurator::ResetModified() {
    ESP_LOGV(TAG, "Resetting the global commit flag.");
    SetGroupBit(NO_COMMIT_PENDING, false);
}
void Configurator::ResetStateModified() {
    ESP_LOGV(TAG, "Resetting the state commit flag.");
    SetGroupBit(NO_STATE_COMMIT_PENDING, false);
}
bool Configurator::SetGroupBit(int bit_num, bool flag) {
    bool result = true;
    int curFlags = xEventGroupGetBits(_group);
    if ((curFlags & LOAD_BIT) && bit_num == NO_COMMIT_PENDING) {
        ESP_LOGD(TAG, "Loading config, ignoring changes");
        result = false;
    }
    if (result) {
        bool curBit = (xEventGroupGetBits(_group) & bit_num);
        if (curBit == flag) {
            ESP_LOGV(TAG, "Flag %d already %s", bit_num, flag ? "Set" : "Cleared");
            result = false;
        }
    }
    if (result) {
        ESP_LOGV(TAG, "%s Flag %d ", flag ? "Setting" : "Clearing", bit_num);
        if (!flag) {
            xEventGroupClearBits(_group, bit_num);
        } else {
            xEventGroupSetBits(_group, bit_num);
        }
    }
    return result;
}
bool Configurator::Lock() {
    ESP_LOGV(TAG, "Locking Configurator");
    if (xSemaphoreTake(_mutex, LockMaxWait) == pdTRUE) {
        ESP_LOGV(TAG, "Configurator locked!");
        return true;
    } else {
        ESP_LOGE(TAG, "Semaphore take failed. Unable to lock Configurator");
        return false;
    }
}
bool Configurator::LockState() {
    ESP_LOGV(TAG, "Locking State");
    if (xSemaphoreTake(_state_mutex, LockMaxWait) == pdTRUE) {
        ESP_LOGV(TAG, "State locked!");
        return true;
    } else {
        ESP_LOGE(TAG, "Semaphore take failed. Unable to lock State");
        return false;
    }
}
void* Configurator::AllocGetConfigBuffer(size_t* sz, sys_Config* config) {
    size_t datasz;
    pb_byte_t* data = NULL;

    if (!pb_get_encoded_size(&datasz, sys_Config_fields, (const void*)platform) || datasz <= 0) {
        return data;
    }
    data = (pb_byte_t*)malloc_init_external(datasz * sizeof(pb_byte_t));
    pb_ostream_t stream = pb_ostream_from_buffer(data, datasz);
    pb_encode(&stream, sys_Config_fields, (const void*)platform);
    if (sz) {
        *sz = datasz * sizeof(pb_byte_t);
    }
    return data;
}
void* Configurator::AllocGetConfigBuffer(size_t* sz) {
    return AllocGetConfigBuffer(sz, &this->_root);
}
bool Configurator::WaitForCommit() {
    bool commit_pending = (xEventGroupGetBits(_group) & NO_COMMIT_PENDING) == 0;
    while (commit_pending) {
        ESP_LOGW(TAG, "Waiting for config commit ...");
        commit_pending = (xEventGroupWaitBits(_group, NO_COMMIT_PENDING | NO_STATE_COMMIT_PENDING, pdFALSE, pdTRUE,
                              (MaxDelay * 2) / portTICK_PERIOD_MS) &
                             ( NO_COMMIT_PENDING | NO_STATE_COMMIT_PENDING)) == 0;
        if (commit_pending) {
            ESP_LOGW(TAG, "Timeout waiting for config commit.");
        } else {
            ESP_LOGI(TAG, "Config committed!");
        }
    }
    return !commit_pending;
}
void Configurator::CommitChanges() {
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Committing configuration to flash. Locking config object.");
    if (!Lock()) {
        ESP_LOGE(TAG, "Unable to lock config for commit ");
        return;
    }
    ESP_LOGV(TAG, "Config Locked. Committing");
    Commit(&_root);
    ResetModified();
    Unlock();
    ESP_LOGI(TAG, "Done Committing configuration to flash.");
}

bool Configurator::CommitState() {
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Committing configuration to flash. Locking config object.");
    if (!LockState()) {
        ESP_LOGE(TAG, "Unable to lock config for commit ");
        return false;
    }
    ESP_LOGV(TAG, "Config Locked. Committing");
    CommitState(&_sys_state);   
    ResetStateModified();
    Unlock();
    ESP_LOGI(TAG, "Done Committing configuration to flash.");
    return true;
}
bool Configurator::HasChanges() { return (xEventGroupGetBits(_group) & NO_COMMIT_PENDING); }
bool Configurator::HasStateChanges() { return (xEventGroupGetBits(_group) & NO_STATE_COMMIT_PENDING); }
void Configurator::Unlock() {
    ESP_LOGV(TAG, "Unlocking Configurator!");
    xSemaphoreGive(_mutex);
}
void Configurator::UnlockState() {
    ESP_LOGV(TAG, "Unlocking State!");
    xSemaphoreGive(_state_mutex);
}

void Configurator::ResetStructure(sys_Config* config) {
    if (!config) {
        return;
    }
    sys_Config blankconfig = sys_Config_init_default;
    memcpy(config, &blankconfig, sizeof(blankconfig));
}
bool Configurator::LoadDecodeBuffer(void* buffer, size_t buffer_size) {
    size_t msgsize = 0;
    size_t newsize = 0;
    sys_Config config = sys_Config_init_default;
    bool result = Configurator::LoadDecode(buffer, buffer_size, &config);
    if (result) {
        Configurator::ApplyTargetSettings(&config);
    }
    if (result) {
        void* currentbuffer = AllocGetConfigBuffer(&msgsize);
        void* newbuffer = AllocGetConfigBuffer(&newsize, &config);
        if (msgsize != newsize || !memcmp(currentbuffer, newbuffer, msgsize)) {
            ESP_LOGI(TAG, "Config change detected.");
            // todo: here we are assuming that all strings and repeated elements have fixed size
            //  and therefore size should always be the same.
            result = Configurator::LoadDecode(buffer, buffer_size, &this->_root);
            RaiseModified();
        }
        free(currentbuffer);
        free(newbuffer);
    }
    return result;
}
bool Configurator::LoadDecodeState() {
    bool result = true;
    sys_State blank_state = sys_State_init_default;
    FILE* file = open_file("rb", state_file_name);
    if (file == nullptr) {
        ESP_LOGD(TAG,"No state file found. Initializing ");
        pb_release(&sys_State_msg,(void *)&_sys_state);
        memcpy(&_sys_state, &blank_state, sizeof(sys_State));
        ESP_LOGD(TAG,"Done Initializing state");
        return true;
    }
    ESP_LOGD(TAG, "Creating binding");
    pb_istream_t filestream = {&in_file_binding,NULL,0};
    ESP_LOGD(TAG, "Starting encode");
    if (!pb_decode(&filestream, &sys_State_msg, (void*)&_sys_state)) {
        ESP_LOGE(TAG, "Decoding failed: %s\n", PB_GET_ERROR(&filestream));
        result = false;
    }

    fclose(file);
    configurator_raise_state_changed();
    ESP_LOGD(TAG, "State loaded");
    return true;
}
bool Configurator::LoadDecode(
    void* buffer, size_t buffer_size, sys_Config* conf_root, bool noinit) {
    if (!conf_root || !buffer) {
        ESP_LOGE(TAG, "Invalid arguments passed to Load");
    }
    bool result = true;
    // Prepare to read the data into the 'config' structure
    pb_istream_t stream = pb_istream_from_buffer((uint8_t*)buffer, buffer_size);

    // Decode the Protocol Buffers message
    if (noinit) {
        ESP_LOGD(TAG, "Decoding WITHOUT initialization");
        result = pb_decode_noinit(&stream, &sys_Config_msg, conf_root);
    } else {
        ESP_LOGD(TAG, "Decoding WITH initialization");
        result = pb_decode(&stream, &sys_Config_msg, conf_root);
    }
    if (!result) {
        ESP_LOGE(TAG, "Failed to decode settings: %s", PB_GET_ERROR(&stream));
        return false;
    }
    ESP_LOGD(TAG, "Settings decoded");
    return true;
}

bool Configurator::Commit(sys_Config* config) {
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration structure!");
        return false;
    }
    FILE* file = open_file("wb", config_file_name);
    bool result = true;
    if (file == nullptr) {
        return false;
    }
    ESP_LOGD(TAG, "Creating binding");
    pb_ostream_t filestream = {&out_file_binding, file, SIZE_MAX, 0};
    ESP_LOGD(TAG, "Starting encode");
    if (!pb_encode(&filestream, sys_Config_fields, (void*)config)) {
        ESP_LOGE(TAG, "Encoding failed: %s\n", PB_GET_ERROR(&filestream));
        result = false;
    }
    ESP_LOGD(TAG, "Encoded size: %d", filestream.bytes_written);
    if (filestream.bytes_written == 0) {
        ESP_LOGE(TAG, "Empty configuration!");
        ESP_LOGD(TAG, "Device name: %s", config->names.device);
    }

    fclose(file);
    return result;
}

bool Configurator::CommitState(sys_State* state) {
    if (!state) {
        ESP_LOGE(TAG, "Invalid state structure!");
        return false;
    }
    FILE* file = open_file("wb", state_file_name);
    bool result = true;
    if (file == nullptr) {
        return false;
    }
    ESP_LOGD(TAG, "Creating binding for state commit");
    pb_ostream_t filestream = {&out_file_binding, file, SIZE_MAX, 0};
    ESP_LOGD(TAG, "Starting state encode");
    if (!pb_encode(&filestream, sys_Config_fields, (void*)state)) {
        ESP_LOGE(TAG, "Encoding failed: %s\n", PB_GET_ERROR(&filestream));
        result = false;
    }
    ESP_LOGD(TAG, "Encoded size: %d", filestream.bytes_written);
    if (filestream.bytes_written == 0) {
        ESP_LOGE(TAG, "Empty state!");
    }

    fclose(file);
    return result;
}


void Configurator::InitLoadConfig(const char* filename) {
    return Configurator::InitLoadConfig(filename, &this->_root);
}
void Configurator::InitLoadConfig(const char* filename, sys_Config* conf_root, bool noinit) {
    esp_err_t err = ESP_OK;
    size_t data_length = 0;
    bool result = false;
    ESP_LOGI(TAG, "Loading settings from %s", filename);
    void* data = load_file(&data_length, filename);
    if (!data) {
        ESP_LOGW(TAG, "Config file %s was empty. ", filename);
        return;
    } else {
        result = LoadDecode(data, data_length, conf_root, noinit);
        free(data);
    }
    if (ApplyTargetSettings(conf_root)) {
        result = true;
    }
    if (result) {
        _timer = xTimerCreate(
            "configTimer", MaxDelay / portTICK_RATE_MS, pdFALSE, NULL, ConfiguratorCallback);
        if (xTimerStart(_timer, MaxDelay / portTICK_RATE_MS) != pdPASS) {
            ESP_LOGE(TAG, "config commitment timer failed to start.");
        }
    }

    return;
}
bool Configurator::ApplyTargetSettings() { return ApplyTargetSettings(&this->_root); }
bool Configurator::ApplyTargetSettings(sys_Config* conf_root) {
    size_t data_length = 0;
    bool result = false;
    std::string target_name = conf_root->target; 
    std::string target_file;

#ifdef CONFIG_FW_PLATFORM_NAME
    if( target_name.empty()){
        target_name = CONFIG_FW_PLATFORM_NAME;
    }
#endif
    target_file = target_name+ std::string(".bin");

    std::transform(target_file.begin(), target_file.end(), target_file.begin(),
        [](unsigned char c){ return std::tolower(c); });
    if (target_file.empty() || !get_file_info(NULL, targets_folder, target_file.c_str())) {
        ESP_LOGD(TAG, "Platform settings file not found: %s", target_file.c_str());
        return result;
    }

    ESP_LOGI(TAG, "Applying target %s settings", target_name.c_str());
    void* data = load_file(&data_length, targets_folder, target_file.c_str());
    if (!data) {
        ESP_LOGE(TAG, "File read fail");
        return false;
    } else {

        result = LoadDecode(data, data_length, conf_root, true);
        if (result) {
            ESP_LOGI(TAG, "Target %s settings loaded", target_name.c_str());
        }
        free(data);
    }

    return result;
}

}; // namespace PlatformConfig

void configurator_reset_configuration() {
    ESP_LOGI(TAG, "Creating default configuration file. ");
    sys_Config config = sys_Config_init_default;
    ESP_LOGD(TAG, "Device name before target settings: %s", config.names.device);
    PlatformConfig::Configurator::ApplyTargetSettings(&config);
    ESP_LOGD(TAG, "Device name after target settings: %s", config.names.device);
    ESP_LOGD(TAG, "Committing new structure");
    PlatformConfig::Configurator::Commit(&config);
}

void configurator_load() {
    struct stat fileInformation;
    ESP_LOGI(TAG, "Loading system settings file");
    ESP_LOGD(TAG, "Checking if file %s exists", config_file_name);
    bool found = get_file_info(&fileInformation, config_file_name);
    if (!found || fileInformation.st_size == 0) {
        ESP_LOGI(TAG, "Configuration file not found or is empty. ");
        configurator_reset_configuration();
    }
    configurator.InitLoadConfig(config_file_name);
    ESP_LOGD(TAG, "Assigning global config pointer");
    platform = configurator.Root();
    configurator.LoadDecodeState();
    sys_state = configurator.RootState();
}
bool configurator_lock() { return configurator.Lock(); }

void configurator_unlock() { configurator.Unlock(); }
void configurator_raise_changed() { configurator.RaiseModified(); }
void configurator_raise_state_changed() { configurator.RaiseStateModified(); }

bool configurator_has_changes() { return configurator.HasChanges(); }

bool configurator_waitcommit() { return configurator.WaitForCommit(); }

void* configurator_alloc_get_config(size_t* sz) { return configurator.AllocGetConfigBuffer(sz); }
bool configurator_parse_config(void* buffer, size_t buffer_size) {
    // Load and decode buffer. The method also applies any overlay if needed.
    return configurator.LoadDecodeBuffer(buffer, buffer_size);
}
pb_type_t configurator_get_field_type(const pb_msgdesc_t* desc, uint32_t tag) {
    pb_field_iter_t iter;
    if (pb_field_iter_begin(&iter, desc, NULL) && pb_field_iter_find(&iter, tag)) {
        /* Found our field. */
        return iter.type;
    }
    return 0;
}
bool configurator_set_string(
    const pb_msgdesc_t* desc, uint32_t field_tag, void* message, const char* value) {
    pb_field_iter_t iter;
    const char * newval = STR_OR_BLANK(value);
    ESP_LOGD(TAG, "Setting value [%s] in message field tag %d",newval , field_tag);
    if (pb_field_iter_begin(&iter, desc, message) && pb_field_iter_find(&iter, field_tag)) {
        if (iter.pData && !strcmp((char*)iter.pData, newval)) {
            ESP_LOGW(TAG, "No change, from and to values are the same: [%s]",  STR_OR_BLANK(newval));
            return false;
        }
        if (PB_ATYPE(iter.type) == PB_ATYPE_POINTER) {
            ESP_LOGD(TAG, "Field is a pointer. Freeing previous value if any");
            FREE_AND_NULL(iter.pData);
            ESP_LOGD(TAG, "Field is a pointer. Setting new value ");
            if(newval && strlen(newval)>0){
                iter.pData = strdup_psram(newval);
            }
            
        } else if (PB_ATYPE(iter.type) == PB_ATYPE_STATIC) {
            ESP_LOGD(TAG, "Static string. Setting new value");
            memset(iter.pData,0x00,iter.data_size);
            if(newval && strlen(newval)>0){
                strncpy((char*)iter.pData, newval, iter.data_size);
            }
        }
        ESP_LOGD(TAG, "Done setting value ");
    }
    return true;
}