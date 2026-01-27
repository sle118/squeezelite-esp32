#pragma once

#include <iostream>  // for streamsize, basic_streambuf<>::int_type, ios...
#include <memory>    // for unique_ptr, operator!=
#include <string>    // for char_traits, string

#include "BellSocket.h"  // for Socket

namespace bell {
class SocketBuffer : public std::streambuf {
 private:
  std::unique_ptr<bell::Socket> internalSocket;

  static const int bufLen = 1024;
  char ibuf[bufLen], obuf[bufLen];

 public:
  SocketBuffer() { internalSocket = nullptr; }

  SocketBuffer(const std::string& hostname, int port, bool isSSL = false) {
    open(hostname, port);
  }

  int open(const std::string& hostname, int port, bool isSSL = false);

  int close();

  bool isOpen() {
    return internalSocket != nullptr && internalSocket->isOpen();
  }

  ~SocketBuffer() { close(); }

 protected:
  virtual int sync();

  virtual int_type underflow();

  virtual int_type overflow(int_type c = traits_type::eof());

  virtual std::streamsize xsgetn(char_type* __s, std::streamsize __n);

  virtual std::streamsize xsputn(const char_type* __s, std::streamsize __n);
};

class SocketStream : public std::iostream {
 private:
  SocketBuffer socketBuf;

 public:
  SocketStream() : std::iostream(&socketBuf) {}

  SocketStream(const std::string& hostname, int port, bool isSSL = false)
      : std::iostream(&socketBuf) {
    open(hostname, port, isSSL);
  }

  SocketBuffer* rdbuf() { return &socketBuf; }

  int open(const std::string& hostname, int port, bool isSSL = false) {
    int err = socketBuf.open(hostname, port, isSSL);
    if (err)
      setstate(std::ios::failbit);
    return err;
  }

  int close() { return socketBuf.close(); }

  bool isOpen() { return socketBuf.isOpen(); }
};
}  // namespace bell
