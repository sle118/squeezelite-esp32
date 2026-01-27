#include "WrappedSemaphore.h"

using namespace bell;

WrappedSemaphore::WrappedSemaphore(int count) {
  semaphoreHandle = dispatch_semaphore_create(0);
}

WrappedSemaphore::~WrappedSemaphore() {
  dispatch_release(semaphoreHandle);
}

int WrappedSemaphore::wait() {

  return dispatch_semaphore_wait(semaphoreHandle, DISPATCH_TIME_FOREVER);
}

int WrappedSemaphore::twait(long milliseconds) {
  dispatch_time_t timeout =
      dispatch_time(DISPATCH_TIME_NOW, (NSEC_PER_SEC / 1000) * milliseconds);

  return dispatch_semaphore_wait(semaphoreHandle, timeout);
}

void WrappedSemaphore::give() {
  dispatch_semaphore_signal(semaphoreHandle);
}
