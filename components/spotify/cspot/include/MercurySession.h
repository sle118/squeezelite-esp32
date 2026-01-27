#pragma once

#include <atomic>         // for atomic
#include <cstdint>        // for uint8_t, uint64_t, uint32_t
#include <functional>     // for function
#include <memory>         // for shared_ptr
#include <mutex>          // for mutex
#include <string>         // for string
#include <unordered_map>  // for unordered_map
#include <vector>         // for vector

#include "BellTask.h"             // for Task
#include "Packet.h"               // for Packet
#include "Queue.h"                // for Queue
#include "Session.h"              // for Session
#include "protobuf/mercury.pb.h"  // for Header

namespace cspot {
class TimeProvider;

class MercurySession : public bell::Task, public cspot::Session {
 public:
  MercurySession(std::shared_ptr<cspot::TimeProvider> timeProvider);
  ~MercurySession();
  typedef std::vector<std::vector<uint8_t>> DataParts;

  struct Response {
    Header mercuryHeader;
    uint8_t flags;
    DataParts parts;
    uint64_t sequenceId;
    bool fail;
  };

  typedef std::function<void(Response&)> ResponseCallback;
  typedef std::function<void(bool, const std::vector<uint8_t>&)>
      AudioKeyCallback;
  typedef std::function<void()> ConnectionEstabilishedCallback;

  enum class RequestType : uint8_t {
    SUB = 0xb3,
    UNSUB = 0xb4,
    SUBRES = 0xb5,
    SEND = 0xb2,
    GET = 0xFF,  // Shitty workaround, it's value is actually same as SEND
    PING = 0x04,
    PONG_ACK = 0x4a,
    AUDIO_CHUNK_REQUEST_COMMAND = 0x08,
    AUDIO_CHUNK_SUCCESS_RESPONSE = 0x09,
    AUDIO_CHUNK_FAILURE_RESPONSE = 0x0A,
    AUDIO_KEY_REQUEST_COMMAND = 0x0C,
    AUDIO_KEY_SUCCESS_RESPONSE = 0x0D,
    AUDIO_KEY_FAILURE_RESPONSE = 0x0E,
    COUNTRY_CODE_RESPONSE = 0x1B,
  };

  std::unordered_map<RequestType, std::string> RequestTypeMap = {
      {RequestType::GET, "GET"},
      {RequestType::SEND, "SEND"},
      {RequestType::SUB, "SUB"},
      {RequestType::UNSUB, "UNSUB"},
  };

  void handlePacket();

  uint64_t executeSubscription(RequestType type, const std::string& uri,
                               ResponseCallback callback,
                               ResponseCallback subscription, DataParts& parts);
  uint64_t executeSubscription(RequestType type, const std::string& uri,
                               ResponseCallback callback,
                               ResponseCallback subscription) {
    DataParts parts = {};
    return this->executeSubscription(type, uri, callback, subscription, parts);
  }

  uint64_t execute(RequestType type, const std::string& uri,
                   ResponseCallback callback) {
    return this->executeSubscription(type, uri, callback, nullptr);
  }

  uint64_t execute(RequestType type, const std::string& uri,
                   ResponseCallback callback, DataParts& parts) {
    return this->executeSubscription(type, uri, callback, nullptr, parts);
  }

  void unregister(uint64_t sequenceId);

  void unregisterAudioKey(uint32_t sequenceId);

  uint32_t requestAudioKey(const std::vector<uint8_t>& trackId,
                           const std::vector<uint8_t>& fileId,
                           AudioKeyCallback audioCallback);

  std::string getCountryCode();

  void disconnect();

  void setConnectedHandler(ConnectionEstabilishedCallback callback);

  bool triggerTimeout() override;

 private:
  const int PING_TIMEOUT_MS = 2 * 60 * 1000 + 5000;

  std::shared_ptr<cspot::TimeProvider> timeProvider;
  Header tempMercuryHeader = {};
  ConnectionEstabilishedCallback connectionReadyCallback = nullptr;

  bell::Queue<cspot::Packet> packetQueue;

  void runTask() override;
  void reconnect();

  std::unordered_map<uint64_t, ResponseCallback> callbacks;
  std::unordered_map<std::string, ResponseCallback> subscriptions;
  std::unordered_map<uint32_t, AudioKeyCallback> audioKeyCallbacks;

  uint64_t sequenceId = 1;
  uint32_t audioKeySequence = 1;

  unsigned long long timestampDiff;
  unsigned long long lastPingTimestamp = -1;
  std::string countryCode = "";

  std::mutex isRunningMutex;
  std::atomic<bool> isRunning = false;
  std::atomic<bool> isReconnecting = false;
  std::atomic<bool> executeEstabilishedCallback = false;

  void failAllPending();

  Response decodeResponse(const std::vector<uint8_t>& data);
};
}  // namespace cspot
