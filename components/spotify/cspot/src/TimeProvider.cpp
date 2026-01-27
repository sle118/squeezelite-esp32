#include "TimeProvider.h"

#include "BellLogger.h"  // for AbstractLogger
#include "Logger.h"      // for CSPOT_LOG
#include "Utils.h"       // for extract, getCurrentTimestamp
#ifndef _WIN32
#include <arpa/inet.h>
#endif

using namespace cspot;

TimeProvider::TimeProvider() {}

void TimeProvider::syncWithPingPacket(const std::vector<uint8_t>& pongPacket) {
  CSPOT_LOG(debug, "Time synced with spotify servers");
  // Spotify's timestamp is in seconds since unix time - convert to millis.
  uint64_t remoteTimestamp =
      ((uint64_t)ntohl(extract<uint32_t>(pongPacket, 0))) * 1000;
  this->timestampDiff = remoteTimestamp - getCurrentTimestamp();
}

unsigned long long TimeProvider::getSyncedTimestamp() {
  return getCurrentTimestamp() + this->timestampDiff;
}