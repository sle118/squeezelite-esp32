#pragma once

#include <BellLogger.h>

#define CSPOT_LOG(type, ...)                                                \
  do {                                                                      \
    bell::bellGlobalLogger->type(__FILE__, __LINE__, "cspot", __VA_ARGS__); \
  } while (0)
