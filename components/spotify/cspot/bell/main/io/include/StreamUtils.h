#pragma once

#include <cstddef>
#include <istream>
#include <streambuf>

namespace bell {
struct MemoryBuffer : std::streambuf {
  MemoryBuffer(std::byte const* base, size_t size) {
    std::byte* p(const_cast<std::byte*>(base));
    this->setg((char*)p, (char*)p, (char*)p + size);
  }
};
struct IMemoryStream : virtual MemoryBuffer, std::istream {
  IMemoryStream(std::byte const* base, size_t size)
      : MemoryBuffer(base, size),
        std::istream(static_cast<std::streambuf*>(this)) {}
};
}  // namespace bell
