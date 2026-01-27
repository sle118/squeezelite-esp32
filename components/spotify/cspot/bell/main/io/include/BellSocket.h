#pragma once

#include <string>

namespace bell {
class Socket {
 public:
  Socket(){};
  virtual ~Socket() = default;

  virtual void open(const std::string& host, uint16_t port) = 0;
  virtual size_t poll() = 0;
  virtual size_t write(uint8_t* buf, size_t len) = 0;
  virtual size_t read(uint8_t* buf, size_t len) = 0;
  virtual bool isOpen() = 0;
  virtual void close() = 0;
  virtual int getFd() = 0;
};
}  // namespace bell
