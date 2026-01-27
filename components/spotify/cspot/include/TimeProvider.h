#pragma once

#include <stdint.h>  // for uint8_t
#include <vector>    // for vector

namespace cspot {
class TimeProvider {
 private:
  unsigned long long timestampDiff;

 public:
  /**
     * @brief Bypasses the need for NTP server sync by syncing with spotify's servers
     * 
     */
  TimeProvider();

  /**
     * @brief Syncs the TimeProvider with spotify server's timestamp
     * 
     * @param pongPacket pong packet containing timestamp
     */
  void syncWithPingPacket(const std::vector<uint8_t>& pongPacket);

  /**
     * @brief Get current timestamp synced with spotify servers
     * 
     * @return unsigned long long timestamp
     */
  unsigned long long getSyncedTimestamp();
};
}  // namespace cspot