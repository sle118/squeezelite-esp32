#include "TrackReference.h"

#include "NanoPBExtensions.h"
#include "Utils.h"
#include "protobuf/spirc.pb.h"

using namespace cspot;

static constexpr auto base62Alphabet =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

TrackReference::TrackReference() : type(Type::TRACK) {}

void TrackReference::decodeURI() {
  if (gid.size() == 0) {
    // Episode GID is being fetched via base62 encoded URI
    auto idString = uri.substr(uri.find_last_of(":") + 1, uri.size());
    gid = {0};

    std::string_view alphabet(base62Alphabet);
    for (int x = 0; x < idString.size(); x++) {
      size_t d = alphabet.find(idString[x]);
      gid = bigNumMultiply(gid, 62);
      gid = bigNumAdd(gid, d);
    }

    if (uri.find("episode:") != std::string::npos) {
      type = Type::EPISODE;
    }
  }
}

bool TrackReference::operator==(const TrackReference& other) const {
  return other.gid == gid && other.uri == uri;
}

bool TrackReference::pbEncodeTrackList(pb_ostream_t* stream,
                                       const pb_field_t* field,
                                       void* const* arg) {
  auto trackQueue = *static_cast<std::vector<TrackReference>*>(*arg);
  static TrackRef msg = TrackRef_init_zero;

  // Prepare nanopb callbacks
  msg.context.funcs.encode = &bell::nanopb::encodeString;
  msg.uri.funcs.encode = &bell::nanopb::encodeString;
  msg.gid.funcs.encode = &bell::nanopb::encodeVector;
  msg.queued.funcs.encode = &bell::nanopb::encodeBoolean;

  for (auto trackRef : trackQueue) {
    if (!pb_encode_tag_for_field(stream, field)) {
      return false;
    }

    msg.gid.arg = &trackRef.gid;
    msg.uri.arg = &trackRef.uri;
    msg.context.arg = &trackRef.context;
    msg.queued.arg = &trackRef.queued;

    if (!pb_encode_submessage(stream, TrackRef_fields, &msg)) {
      return false;
    }
  }

  return true;
}

bool TrackReference::pbDecodeTrackList(pb_istream_t* stream,
                                       const pb_field_t* field, void** arg) {
  auto trackQueue = static_cast<std::vector<TrackReference>*>(*arg);

  // Push a new reference
  trackQueue->push_back(TrackReference());

  auto& track = trackQueue->back();

  bool eof = false;
  pb_wire_type_t wire_type;
  pb_istream_t substream;
  uint32_t tag;

  while (!eof) {
    if (!pb_decode_tag(stream, &wire_type, &tag, &eof)) {
      // Decoding failed and not eof
      if (!eof) {
        return false;
      }
      // EOF
    } else {
      switch (tag) {
        case TrackRef_uri_tag:
        case TrackRef_context_tag:
        case TrackRef_gid_tag: {
          // Make substream
          if (!pb_make_string_substream(stream, &substream)) {

            return false;
          }

          uint8_t* destBuffer = nullptr;

          // Handle GID
          if (tag == TrackRef_gid_tag) {
            track.gid.resize(substream.bytes_left);
            destBuffer = &track.gid[0];
          } else if (tag == TrackRef_context_tag) {
            track.context.resize(substream.bytes_left);

            destBuffer = reinterpret_cast<uint8_t*>(&track.context[0]);
          } else if (tag == TrackRef_uri_tag) {
            track.uri.resize(substream.bytes_left);

            destBuffer = reinterpret_cast<uint8_t*>(&track.uri[0]);
          }

          if (!pb_read(&substream, destBuffer, substream.bytes_left)) {
            return false;
          }

          // Close substream
          if (!pb_close_string_substream(stream, &substream)) {
            return false;
          }

          break;
        }
        case TrackRef_queued_tag: {
          uint32_t queuedValue;

          // Decode boolean
          if (!pb_decode_varint32(stream, &queuedValue)) {
            return false;
          }

          // Cast down to bool
          track.queued = (bool)queuedValue;

          break;
        }
        default:
          // Field not known, skip
          pb_skip_field(stream, wire_type);

          break;
      }
    }
  }

  // Fill in GID when only URI is provided
  track.decodeURI();

  return true;
}
