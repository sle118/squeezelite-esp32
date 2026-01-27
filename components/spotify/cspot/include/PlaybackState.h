#pragma once

#include <stdint.h>  // for uint8_t, uint32_t
#include <memory>    // for shared_ptr
#include <string>    // for string
#include <vector>    // for vector

#include "TrackReference.h"
#include "protobuf/spirc.pb.h"  // for Frame, TrackRef, CapabilityType, Mess...

namespace cspot {
struct Context;

class PlaybackState {
 private:
  std::shared_ptr<cspot::Context> ctx;

  uint32_t seqNum = 0;
  uint8_t capabilityIndex = 0;

  std::vector<uint8_t> frameData;

  void addCapability(
      CapabilityType typ, int intValue = -1,
      std::vector<std::string> stringsValue = std::vector<std::string>());

 public:
  Frame innerFrame;
  Frame remoteFrame;

  std::vector<TrackReference> remoteTracks;

  enum class State { Playing, Stopped, Loading, Paused };

  /**
     * @brief Player state represents the current state of player.
     *
     * Responsible for keeping track of player's state. Doesn't control the playback itself.
     *
     * @param timeProvider synced time provider
     */
  PlaybackState(std::shared_ptr<cspot::Context> ctx);

  ~PlaybackState();

  /**
     * @brief Updates state according to current playback state.
     *
     * @param state playback state
     */
  void setPlaybackState(const PlaybackState::State state);

  /**
     * @brief Sets player activity
     *
     * @param isActive activity status
     */
  void setActive(bool isActive);

  /**
     * @brief Simple getter
     *
     * @return true player is active
     * @return false player is inactive
     */
  bool isActive();

  /**
     * @brief Updates local track position.
     *
     * @param position position in milliseconds
     */
  void updatePositionMs(uint32_t position);

  /**
     * @brief Sets local volume on internal state.
     *
     * @param volume volume between 0 and UINT16 max
     */
  void setVolume(uint32_t volume);

  /**
   * @brief Updates local track queue from remote data.
     */
  void syncWithRemote();

  /**
     * @brief Encodes current frame into binary data via protobuf.
     *
     * @param typ message type to include in frame type
     * @return std::vector<uint8_t> binary frame data
     */
  std::vector<uint8_t> encodeCurrentFrame(MessageType typ);

  bool decodeRemoteFrame(std::vector<uint8_t>& data);
};
}  // namespace cspot
