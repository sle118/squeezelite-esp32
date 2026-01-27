#ifndef EUPHONIUM_BELL_UTILS
#define EUPHONIUM_BELL_UTILS

#include <stdint.h>  // for int32_t, int64_t
#include <string.h>  // for NULL
#ifdef _WIN32
#include <WinSock2.h>
#else
#include <sys/time.h>  // for timeval, gettimeofday
#include <unistd.h>    // for usleep
#endif
#include <cmath>   // for floor
#include <string>  // for string

#ifdef ESP_PLATFORM
#include "esp_system.h"
#endif

namespace bell {

std::string generateRandomUUID();
void freeAndNull(void*& ptr);
std::string getMacAddress();
struct tv {
  tv() {}
  tv(timeval tv) : sec(tv.tv_sec), usec(tv.tv_usec){};
  tv(int32_t _sec, int32_t _usec) : sec(_sec), usec(_usec){};
  static tv now() {
    tv timestampNow;
#if _WIN32
    static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

    SYSTEMTIME system_time;
    FILETIME file_time;
    uint64_t time;

    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);
    time = ((uint64_t)file_time.dwLowDateTime);
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    timestampNow.sec = (long)((time - EPOCH) / 10000000L);
    timestampNow.usec = (long)(system_time.wMilliseconds * 1000);
#else
    timeval t;
    gettimeofday(&t, NULL);
    timestampNow.sec = t.tv_sec;
    timestampNow.usec = t.tv_usec;
#endif
    return timestampNow;
  }
  int32_t sec;
  int32_t usec;

  int64_t ms() {
    return (sec * (int64_t)1000) + (usec / 1000);
  }

  tv operator+(const tv& other) const {
    tv result(*this);
    result.sec += other.sec;
    result.usec += other.usec;
    if (result.usec > 1000000) {
      result.sec += result.usec / 1000000;
      result.usec %= 1000000;
    }
    return result;
  }

  tv operator/(const int& other) const {
    tv result(*this);
    int64_t millis = result.ms();
    millis = millis / other;
    result.sec = std::floor(millis / 1000.0);
    result.usec = (int32_t)((int64_t)(millis * 1000) % 1000000);
    return result;
  }

  tv operator-(const tv& other) const {
    tv result(*this);
    result.sec -= other.sec;
    result.usec -= other.usec;
    while (result.usec < 0) {
      result.sec -= 1;
      result.usec += 1000000;
    }
    return result;
  }
};
}  // namespace bell

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>

#define BELL_SLEEP_MS(ms) vTaskDelay(ms / portTICK_PERIOD_MS)
#define BELL_YIELD() taskYIELD()

#elif defined(_WIN32)
#define BELL_SLEEP_MS(ms) Sleep(ms)
#define BELL_YIELD() ;
#else

#define BELL_SLEEP_MS(ms) usleep(ms * 1000)
#define BELL_YIELD() ;

#endif
#endif
