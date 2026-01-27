#include "WrappedSemaphore.h"

using namespace bell;

WrappedSemaphore::WrappedSemaphore(int count) {
  this->semaphoreHandle = CreateSemaphore(NULL, 0, count, NULL);
}

WrappedSemaphore::~WrappedSemaphore() {
  CloseHandle(this->semaphoreHandle);
}

int WrappedSemaphore::wait() {
  WaitForSingleObject(this->semaphoreHandle, INFINITE);
  return 0;
}

int WrappedSemaphore::twait(long milliseconds) {
  return WaitForSingleObject(this->semaphoreHandle, milliseconds) !=
         WAIT_OBJECT_0;
}

void WrappedSemaphore::give() {
  ReleaseSemaphore(this->semaphoreHandle, 1, NULL);
}
