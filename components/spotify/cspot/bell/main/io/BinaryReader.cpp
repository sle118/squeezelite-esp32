#include "BinaryReader.h"

#include <stdlib.h>     // for size_t
#include <cstdint>      // for uint8_t
#include <type_traits>  // for remove_extent_t

#include "ByteStream.h"  // for ByteStream

bell::BinaryReader::BinaryReader(std::shared_ptr<ByteStream> stream) {
  this->stream = stream;
}

size_t bell::BinaryReader::position() {
  return stream->position();
}

size_t bell::BinaryReader::size() {
  return stream->size();
}

void bell::BinaryReader::close() {
  stream->close();
}

void bell::BinaryReader::skip(size_t pos) {
  std::vector<uint8_t> b(pos);
  stream->read(&b[0], pos);
}

int32_t bell::BinaryReader::readInt() {
  uint8_t b[4];
  if (stream->read((uint8_t*)b, 4) != 4)
    return 0;

  return static_cast<int32_t>((b[3]) | (b[2] << 8) | (b[1] << 16) |
                              (b[0] << 24));
}

int16_t bell::BinaryReader::readShort() {
  uint8_t b[2];
  if (stream->read((uint8_t*)b, 2) != 2)
    return 0;

  return static_cast<int16_t>((b[1]) | (b[0] << 8));
}

uint32_t bell::BinaryReader::readUInt() {
  return readInt() & 0xffffffffL;
}

uint8_t bell::BinaryReader::readByte() {
  uint8_t b[1];
  if (stream->read((uint8_t*)b, 1) != 1)
    return 0;
  return b[0];
}

std::vector<uint8_t> bell::BinaryReader::readBytes(size_t size) {
  std::vector<uint8_t> data(size);
  stream->read(&data[0], size);
  return data;
}

long long bell::BinaryReader::readLong() {
  long high = readInt();
  long low = readInt();

  return static_cast<long long>(((long long)high << 32) | low);
}
