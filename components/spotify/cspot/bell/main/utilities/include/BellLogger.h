#ifndef BELL_LOGGER_H
#define BELL_LOGGER_H

#include <stdarg.h>  // for va_end, va_list, va_start
#include <stdio.h>   // for printf, vprintf
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>  // for string, basic_string

namespace bell {

class AbstractLogger {
 public:
  bool enableSubmodule = false;
  bool enableTimestamp = false;
  bool shortTime = false;

  virtual void debug(std::string filename, int line, std::string submodule,
                     const char* format, ...) = 0;
  virtual void error(std::string filename, int line, std::string submodule,
                     const char* format, ...) = 0;
  virtual void info(std::string filename, int line, std::string submodule,
                    const char* format, ...) = 0;
};

extern bell::AbstractLogger* bellGlobalLogger;
class BellLogger : public bell::AbstractLogger {
 public:
  // static bool enableColors = true;
  void debug(std::string filename, int line, std::string submodule,
             const char* format, ...) {
    printTimestamp();

    printf(colorRed);
    printf("D ");
    if (enableSubmodule) {
      printf(colorReset);
      printf("[%s] ", submodule.c_str());
    }
    printFilename(filename);
    printf(":%d: ", line);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
  };

  void error(std::string filename, int line, std::string submodule,
             const char* format, ...) {
    printTimestamp();

    printf(colorRed);
    printf("E ");
    if (enableSubmodule) {
      printf(colorReset);
      printf("[%s] ", submodule.c_str());
    }
    printFilename(filename);
    printf(":%d: ", line);
    printf(colorRed);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
  };

  void info(std::string filename, int line, std::string submodule,
            const char* format, ...) {
    printTimestamp();

    printf(colorBlue);
    printf("I ");
    if (enableSubmodule) {
      printf(colorReset);
      printf("[%s] ", submodule.c_str());
    }
    printFilename(filename);
    printf(":%d: ", line);
    printf(colorReset);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
  };

  void printTimestamp() {
    if (enableTimestamp) {
      auto now = std::chrono::system_clock::now();
      time_t now_time = std::chrono::system_clock::to_time_t(now);
      const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now.time_since_epoch()) %
                         1000;

      printf(colorReset);
      struct tm* gmt_time;
      if (shortTime) {
        gmt_time = localtime(&now_time);
        std::cout << std::put_time(gmt_time, "[%H:%M:%S") << '.'
                  << std::setfill('0') << std::setw(3) << nowMs.count() << "] ";
      } else {
        gmt_time = gmtime(&now_time);
        std::cout << std::put_time(gmt_time, "[%Y-%m-%d %H:%M:%S") << '.'
                  << std::setfill('0') << std::setw(3) << nowMs.count() << "] ";
      }
    }
  }

  void printFilename(std::string filename) {
#ifdef _WIN32
    std::string basenameStr(filename.substr(filename.rfind("\\") + 1));
#else
    std::string basenameStr(filename.substr(filename.rfind("/") + 1));
#endif
    unsigned long hash = 5381;
    for (char const& c : basenameStr) {
      hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    printf("\033[0;%dm", allColors[hash % NColors]);

    printf("%s", basenameStr.c_str());
    printf(colorReset);
  }

 private:
  static constexpr const char* colorReset = "\033[0m";
  static constexpr const char* colorRed = "\033[0;31m";
  static constexpr const char* colorBlue = "\033[0;34m";
  static constexpr const int NColors = 15;
  static constexpr int allColors[NColors] = {31, 32, 33, 34, 35, 36, 37, 90,
                                             91, 92, 93, 94, 95, 96, 97};
};

void setDefaultLogger();
void enableSubmoduleLogging();
void enableTimestampLogging(bool local = false);
}  // namespace bell

#define BELL_LOG(type, ...)                                        \
  do {                                                             \
    bell::bellGlobalLogger->type(__FILE__, __LINE__, __VA_ARGS__); \
  } while (0)

#endif
