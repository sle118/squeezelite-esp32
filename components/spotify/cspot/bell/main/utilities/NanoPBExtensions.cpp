#include "NanoPBExtensions.h"

#include <optional>  // for optional
#include <string>    // for string
#include <vector>    // for vector

#include <pb_common.h>
#include <pb_encode.h>

bool bell::nanopb::encodeString(pb_ostream_t* stream, const pb_field_t* field,
                                void* const* arg) {
  auto& str = *static_cast<std::string*>(*arg);

  if (str.size() > 0) {
    if (!pb_encode_tag_for_field(stream, field)) {
      return false;
    }

    if (!pb_encode_string(stream, (uint8_t*)str.c_str(), str.size())) {
      return false;
    }
  }

  return true;
}

bool bell::nanopb::encodeBoolean(pb_ostream_t* stream, const pb_field_t* field,
                                 void* const* arg) {
  auto& boolean = *static_cast<std::optional<bool>*>(*arg);

  if (boolean.has_value()) {
    if (!pb_encode_tag_for_field(stream, field)) {
      return false;
    }

    if (!pb_encode_varint(stream, boolean.value())) {
      return false;
    }
  }

  return true;
}

bool bell::nanopb::encodeVector(pb_ostream_t* stream, const pb_field_t* field,
                                void* const* arg) {
  auto& vector = *static_cast<std::vector<uint8_t>*>(*arg);

  if (vector.size() > 0) {
    if (!pb_encode_tag_for_field(stream, field)) {
      return false;
    }

    if (!pb_encode_string(stream, vector.data(), vector.size())) {
      return false;
    }
  }

  return true;
}