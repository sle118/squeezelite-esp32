#pragma once

#include <pb_encode.h>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>
#include "NanoPBHelper.h"
#include "pb_decode.h"
#include "protobuf/spirc.pb.h"

namespace cspot {
struct TrackReference {
  TrackReference();

  // Resolved track GID
  std::vector<uint8_t> gid;
  std::string uri, context;
  std::optional<bool> queued;

  // Type identifier
  enum class Type { TRACK, EPISODE };

  Type type;

  void decodeURI();

  bool operator==(const TrackReference& other) const;

  /* Argument for pbEncodeTrackList: the track list together with the mutex
   * that guards it. Frames are encoded from whatever thread calls notify()
   * while the mercury thread can rebuild the list, so the encoder must hold
   * the same lock as the writers. */
  struct LockedTrackList {
    std::mutex* mutex;
    std::vector<TrackReference>* tracks;
  };

  // Encodes list of track references into a pb structure, used by nanopb.
  // *arg must point at a LockedTrackList
  static bool pbEncodeTrackList(pb_ostream_t* stream, const pb_field_t* field,
                                void* const* arg);

  static bool pbDecodeTrackList(pb_istream_t* stream, const pb_field_t* field,
                                void** arg);
};
}  // namespace cspot
