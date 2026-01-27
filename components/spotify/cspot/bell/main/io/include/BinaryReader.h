#pragma once

#include <stdint.h>  // for uint8_t, int16_t, int32_t, uint32_t
#include <stdlib.h>  // for size_t
#include <memory>    // for shared_ptr
#include <vector>    // for vector

namespace bell {
class ByteStream;

class BinaryReader {
  std::shared_ptr<ByteStream> stream;
  size_t currentPos = 0;

 public:
  BinaryReader(std::shared_ptr<ByteStream> stream);
  int32_t readInt();
  int16_t readShort();
  uint32_t readUInt();
  long long readLong();
  void close();
  uint8_t readByte();
  size_t size();
  size_t position();
  std::vector<uint8_t> readBytes(size_t);
  void skip(size_t);
};
}  // namespace bell