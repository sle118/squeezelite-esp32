#pragma once

#if __has_include(<bit>)
#include <bit>  // for endian
#endif

#include <stdint.h>  // for int16_t, int32_t, int64_t, uint16_t, uint32_t
#include <cstddef>   // for byte
#include <iostream>  // for istream, ostream

namespace bell {
class BinaryStream {
 private:
  std::endian byteOrder;

  std::istream* istr = nullptr;
  std::ostream* ostr = nullptr;

  void ensureReadable();
  void ensureWritable();
  bool flipBytes = false;

  template <typename T>
  T swap16(T value) {
#ifdef _WIN32
    return _byteswap_ushort(value);
#else
    return __builtin_bswap16(value);
#endif
  }
  template <typename T>
  T swap32(T value) {
#ifdef _WIN32
    return _byteswap_ulong(value);
#else
    return __builtin_bswap32(value);
#endif
  }
  template <typename T>
  T swap64(T value) {
#ifdef _WIN32
    return _byteswap_uint64(value);
#else
    return __builtin_bswap64(value);
#endif
  }

 public:
  BinaryStream(std::ostream* ostr);
  BinaryStream(std::istream* istr);

  /**
   * @brief Set byte order used by stream.
   *
   * @param byteOrder stream's byteorder. Defaults to native.
   */
  void setByteOrder(std::endian byteOrder);

  // Read operations
  BinaryStream& operator>>(char& value);
  BinaryStream& operator>>(std::byte& value);
  BinaryStream& operator>>(int16_t& value);
  BinaryStream& operator>>(uint16_t& value);
  BinaryStream& operator>>(int32_t& value);
  BinaryStream& operator>>(uint32_t& value);
  BinaryStream& operator>>(int64_t& value);
  BinaryStream& operator>>(uint64_t& value);

  // Write operations
  BinaryStream& operator<<(char value);
  BinaryStream& operator<<(std::byte value);
  BinaryStream& operator<<(int16_t value);
  BinaryStream& operator<<(uint16_t value);
  BinaryStream& operator<<(int32_t value);
  BinaryStream& operator<<(uint32_t value);
  BinaryStream& operator<<(int64_t value);
  BinaryStream& operator<<(uint64_t value);
};
}  // namespace bell
