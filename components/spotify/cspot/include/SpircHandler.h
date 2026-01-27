#pragma once

#include <stdint.h>    // for uint32_t, uint8_t
#include <functional>  // for function
#include <memory>      // for shared_ptr, unique_ptr
#include <string>      // for string
#include <variant>     // for variant
#include <vector>      // for vector

#include "CDNAudioFile.h"  // for CDNTrackStream, CDNTrackStream::Track...
#include "TrackQueue.h"
#include "protobuf/spirc.pb.h"  // for MessageType

namespace cspot {
class TrackPlayer;
struct Context;

class SpircHandler {
 public:
  SpircHandler(std::shared_ptr<cspot::Context> ctx);

  enum class EventType {
    PLAY_PAUSE,
    VOLUME,
    TRACK_INFO,
    DISC,
    NEXT,
    PREV,
    SEEK,
    DEPLETED,
    FLUSH,
    PLAYBACK_START
  };

  typedef std::variant<TrackInfo, int, bool> EventData;

  struct Event {
    EventType eventType;
    EventData data;
  };

  typedef std::function<void(std::unique_ptr<Event>)> EventHandler;

  void subscribeToMercury();
  std::shared_ptr<TrackPlayer> getTrackPlayer();

  void setEventHandler(EventHandler handler);

  void setPause(bool pause);

  bool previousSong();

  bool nextSong();

  void notifyAudioReachedPlayback();
  void notifyAudioEnded();
  void updatePositionMs(uint32_t position);
  void setRemoteVolume(int volume);
  void loadTrackFromURI(const std::string& uri);
  std::shared_ptr<cspot::TrackQueue> getTrackQueue() { return trackQueue; }

  void disconnect();

 private:
  std::shared_ptr<cspot::Context> ctx;
  std::shared_ptr<cspot::TrackPlayer> trackPlayer;
  std::shared_ptr<cspot::TrackQueue> trackQueue;

  EventHandler eventHandler = nullptr;

  std::shared_ptr<cspot::PlaybackState> playbackState;

  void sendCmd(MessageType typ);

  void sendEvent(EventType type);
  void sendEvent(EventType type, EventData data);

  bool skipSong(TrackQueue::SkipDirection dir);
  void handleFrame(std::vector<uint8_t>& data);
  void notify();
};
}  // namespace cspot
