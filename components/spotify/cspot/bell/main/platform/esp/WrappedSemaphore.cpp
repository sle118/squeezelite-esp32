#include "WrappedSemaphore.h"

/**
 * Platform semaphopre implementation for the esp-idf.
 */

using namespace bell;

WrappedSemaphore::WrappedSemaphore(int count) {
  semaphoreHandle = xSemaphoreCreateCounting(count, 0);
}

WrappedSemaphore::~WrappedSemaphore() {
  vSemaphoreDelete(semaphoreHandle);
}

int WrappedSemaphore::wait() {
  if (xSemaphoreTake(semaphoreHandle, portMAX_DELAY) == pdTRUE) {
    return 0;
  }

  return 1;
}

int WrappedSemaphore::twait(long milliseconds) {
  if (xSemaphoreTake(semaphoreHandle, milliseconds / portTICK_PERIOD_MS) ==
      pdTRUE) {
    return 0;
  }

  return 1;
}

void WrappedSemaphore::give() {

  xSemaphoreGive(semaphoreHandle);
}
