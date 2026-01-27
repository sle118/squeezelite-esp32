#pragma once

#include <cstdint>
#include <vector>

namespace cspot {
struct Packet {
  uint8_t command;
  std::vector<uint8_t> data;
};
}  // namespace cspot