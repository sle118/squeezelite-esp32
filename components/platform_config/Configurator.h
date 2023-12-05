#pragma once
#include "State.pb.h"
#include "configuration.pb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "status.pb.h"
#include <strings.h>
#include "accessors.h"
#ifdef __cplusplus
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
#endif
#define PLATFORM_GET_PTR(base, sname)                                                              \
    {                                                                                              \
        (base && (base)->##has_##(sname) ? &(base)->sname : NULL)
#define PLATFORM_DEVICES PLATFORM_GET_PTR(platform)
void configurator_load();
bool configurator_waitcommit();
bool configurator_has_changes();
bool configurator_lock();
void configurator_unlock();
void configurator_raise_changed();
void configurator_raise_state_changed();
bool configurator_has_changes();
bool configurator_waitcommit();
void* configurator_alloc_get_config(size_t* sz);
bool configurator_parse_config(void* buffer, size_t buffer_size);
void configurator_reset_configuration();
pb_type_t configurator_get_field_type(const pb_msgdesc_t* desc, uint32_t tag);
bool configurator_set_string(
    const pb_msgdesc_t* desc, uint32_t field_tag, void* message, const char* value);
extern sys_Config* platform;
extern sys_State* sys_state;
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

namespace PlatformConfig {

class Configurator {
  private:
    static const int MaxDelay;
    static const int LockMaxWait;

    EXT_RAM_ATTR static TimerHandle_t _timer;
    EXT_RAM_ATTR static SemaphoreHandle_t _mutex;
    EXT_RAM_ATTR static SemaphoreHandle_t _state_mutex;
    EXT_RAM_ATTR static EventGroupHandle_t _group;
    bool SetGroupBit(int bit_num, bool flag);
    void ResetModified();
    void ResetStateModified();
    sys_Config _root;
    sys_State _sys_state;

  public:
    sys_Config* Root() { return &_root; }
    sys_State* RootState() { return &_sys_state; }
    bool WaitForCommit();
    bool Lock();
    bool LockState();
    void Unlock();
    void UnlockState();

    void* AllocGetConfigBuffer(size_t* sz);
    static void* AllocGetConfigBuffer(size_t* sz, sys_Config* config);
    static void ResetStructure(sys_Config* config);
    bool LoadDecodeState();

    void CommitChanges();
    bool Commit();
    static bool Commit(sys_Config* config);

    bool CommitState();
    static bool CommitState(sys_State* state);
    sys_Config* AllocDefaultStruct();

    static bool ApplyTargetSettings(sys_Config* conf_root);
    bool ApplyTargetSettings();
    bool HasStateChanges();

    bool LoadDecodeBuffer(void* buffer, size_t buffer_size);
    void InitLoadConfig(const char* filename);
    void InitLoadConfig(const char* filename, sys_Config* conf_root, bool noinit = false);
    static bool LoadDecode(
        void* buffer, size_t buffer_size, sys_Config* conf_root, bool noinit = false);

    Configurator() {
        _mutex = xSemaphoreCreateMutex();
        _state_mutex = xSemaphoreCreateMutex();
        _group = xEventGroupCreate();
    }
    void RaiseStateModified();
    void RaiseModified();
    bool HasChanges();
    ~Configurator() {}
};
} // namespace PlatformConfig
extern PlatformConfig::Configurator configurator;
#endif
