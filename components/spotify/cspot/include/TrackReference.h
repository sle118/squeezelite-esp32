#pragma once

#include <pb_encode.h>
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

  // Encodes list of track references into a pb structure, used by nanopb
  static bool pbEncodeTrackList(pb_ostream_t* stream, const pb_field_t* field,
                                void* const* arg);

  static bool pbDecodeTrackList(pb_istream_t* stream, const pb_field_t* field,
                                void** arg);
};
}  // namespace cspot
