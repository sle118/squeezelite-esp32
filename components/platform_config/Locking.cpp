
#include "Locking.h"
#include "esp_log.h"
#include "tools.h"
static const char* TAG = "Locking";

using namespace System;

const int Locking::MaxDelay = 1000;
const int Locking::LockMaxWait = 20 * Locking::MaxDelay;

// C++ methods
Locking* Locking::Create(std::string name) { return new Locking(name); }

void Locking::Destroy(Locking* lock) { delete lock; }

LockingHandle* Locking_Create(const char* name) { return reinterpret_cast<LockingHandle*>(Locking::Create(std::string(name))); }

void Locking_Destroy(LockingHandle* lock) { Locking::Destroy(reinterpret_cast<Locking*>(lock)); }

bool Locking_Lock(LockingHandle* lock, TickType_t maxWait_ms) { return reinterpret_cast<Locking*>(lock)->Lock(maxWait_ms); }

void Locking_Unlock(LockingHandle* lock) { reinterpret_cast<Locking*>(lock)->Unlock(); }

bool Locking_IsLocked(LockingHandle* lock) { return reinterpret_cast<Locking*>(lock)->IsLocked(); }

bool Locking::Lock(TickType_t maxWait_ms) {
    assert(_mutex != nullptr);
    ESP_LOGV(TAG, "Locking %s", _name.c_str());
    if(xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(maxWait_ms)) == pdTRUE) {
        ESP_LOGV(TAG, "locked %s", _name.c_str());
        return true;
    } else {
        ESP_LOGE(TAG, "Unable to lock %s", _name.c_str());
        return false;
    }
}
void Locking::Unlock() {
    ESP_LOGV(TAG, "Unlocking %s", _name.c_str());
    xSemaphoreGiveRecursive(_mutex);
}
