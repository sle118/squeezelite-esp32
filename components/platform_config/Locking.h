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
#include "esp_attr.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LockingHandle LockingHandle;

#ifdef __cplusplus
} // extern "C"

#include <string>
namespace System {
class Locking {
  private:
    SemaphoreHandle_t _mutex;
    static const int MaxDelay;
    static const int LockMaxWait;
    std::string _name;

  public:
    Locking(std::string name) : _mutex(xSemaphoreCreateRecursiveMutex()), _name(name) {}
    bool Lock(TickType_t maxWait_ms = LockMaxWait);
    void Unlock();
    bool IsLocked() { return xSemaphoreGetMutexHolder(_mutex) != nullptr; }
    ~Locking() { vSemaphoreDelete(_mutex); }
    static Locking* Create(std::string name);
    static void Destroy(Locking* lock);
};

} // namespace System

extern "C" {
#endif

LockingHandle* Locking_Create(const char* name);
void Locking_Destroy(LockingHandle* lock);
bool Locking_Lock(LockingHandle* lock, TickType_t maxWait_ms);
void Locking_Unlock(LockingHandle* lock);
bool Locking_IsLocked(LockingHandle* lock);

#ifdef __cplusplus
}
#endif