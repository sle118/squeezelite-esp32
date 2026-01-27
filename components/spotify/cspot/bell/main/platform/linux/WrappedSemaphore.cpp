#include "WrappedSemaphore.h"
#include <sys/time.h>

using namespace bell;

WrappedSemaphore::WrappedSemaphore(int count) {
  sem_init(&this->semaphoreHandle, 0, 0);  // eek pointer
}

WrappedSemaphore::~WrappedSemaphore() {
  sem_destroy(&this->semaphoreHandle);
}

int WrappedSemaphore::wait() {
  sem_wait(&this->semaphoreHandle);
  return 0;
}

int WrappedSemaphore::twait(long milliseconds) {
  // wait on semaphore with timeout
  struct timespec ts;
  struct timeval tv;

  gettimeofday(&tv, 0);

  ts.tv_sec = tv.tv_sec + milliseconds / 1000;
  ts.tv_nsec = tv.tv_usec * 1000 + (milliseconds % 1000) * 1000000;
  return sem_timedwait(&this->semaphoreHandle, &ts);
}

void WrappedSemaphore::give() {
  sem_post(&this->semaphoreHandle);
}
