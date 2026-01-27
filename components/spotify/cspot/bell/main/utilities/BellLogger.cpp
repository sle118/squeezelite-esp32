#include "BellLogger.h"

bell::AbstractLogger* bell::bellGlobalLogger;

void bell::setDefaultLogger() {
  bell::bellGlobalLogger = new bell::BellLogger();
}

void bell::enableSubmoduleLogging() {
  bell::bellGlobalLogger->enableSubmodule = true;
}

void bell::enableTimestampLogging(bool local) {
  bell::bellGlobalLogger->enableTimestamp = true;
  bell::bellGlobalLogger->shortTime = local;
}
