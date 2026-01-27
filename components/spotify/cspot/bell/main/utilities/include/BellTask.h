#ifndef BELL_TASK_H
#define BELL_TASK_H

#include <string>

#ifdef ESP_PLATFORM
#include <esp_pthread.h>
#include <esp_task.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#elif _WIN32
#include <winsock2.h>
#else
#include <pthread.h>
#endif

#include <iostream>
#include <string>

namespace bell {
class Task {
 public:
  std::string TASK;
  int stackSize, core;
  bool runOnPSRAM;
  Task(std::string taskName, int stackSize, int priority, int core,
       bool runOnPSRAM = true) {
    this->TASK = taskName;
    this->stackSize = stackSize;
    this->core = core;
    this->runOnPSRAM = runOnPSRAM;
#ifdef ESP_PLATFORM
    this->xStack = NULL;
    this->priority = CONFIG_ESP32_PTHREAD_TASK_PRIO_DEFAULT + priority;
    if (this->priority <= ESP_TASK_PRIO_MIN)
      this->priority = ESP_TASK_PRIO_MIN + 1;
    if (runOnPSRAM) {
      this->xStack = (StackType_t*)heap_caps_malloc(
          this->stackSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
#endif
  }
  virtual ~Task() {
#ifdef ESP_PLATFORM
    if (xStack)
      heap_caps_free(xStack);
#endif
  }

  bool startTask() {
#ifdef ESP_PLATFORM
    if (runOnPSRAM) {
      xTaskBuffer = (StaticTask_t*)heap_caps_malloc(
          sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      return (xTaskCreateStaticPinnedToCore(
                  taskEntryFuncPSRAM, this->TASK.c_str(), this->stackSize, this,
                  this->priority, xStack, xTaskBuffer, this->core) != NULL);
    } else {
      printf("task on internal %s", this->TASK.c_str());
      esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
      cfg.stack_size = stackSize;
      cfg.inherit_cfg = true;
      cfg.thread_name = this->TASK.c_str();
      cfg.pin_to_core = core;
      cfg.prio = this->priority;
      esp_pthread_set_cfg(&cfg);
    }
#endif
#if _WIN32
    thread = CreateThread(NULL, stackSize,
                          (LPTHREAD_START_ROUTINE)taskEntryFunc, this, 0, NULL);
    return thread != NULL;
#else
    if (!pthread_create(&thread, NULL, taskEntryFunc, this)) {
        pthread_detach(thread);
        return true;
    }
    return false;
#endif
  }

 protected:
  virtual void runTask() = 0;

 private:
#if _WIN32
  HANDLE thread;
#else
  pthread_t thread;
#endif
#ifdef ESP_PLATFORM
  int priority;
  StaticTask_t* xTaskBuffer;
  StackType_t* xStack;

  static void taskEntryFuncPSRAM(void* This) {
    Task* self = (Task*)This;
    self->runTask();

    // TCB are cleanup in IDLE task, so give it some time
    TimerHandle_t timer =
        xTimerCreate("cleanup", pdMS_TO_TICKS(5000), pdFALSE, self->xTaskBuffer,
                     [](TimerHandle_t xTimer) {
                       heap_caps_free(pvTimerGetTimerID(xTimer));
                       xTimerDelete(xTimer, portMAX_DELAY);
                     });
    xTimerStart(timer, portMAX_DELAY);

    vTaskDelete(NULL);
  }
#endif

  static void* taskEntryFunc(void* This) {
    ((Task*)This)->runTask();
    return NULL;
  }
};
}  // namespace bell

#endif
