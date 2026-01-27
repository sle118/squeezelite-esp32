#include "SocketStream.h"

#include <stdint.h>  // for uint8_t
#include <cstdio>    // for NULL, ssize_t

#include "TCPSocket.h"  // for TCPSocket
#include "TLSSocket.h"  // for TLSSocket

using namespace bell;

int SocketBuffer::open(const std::string& hostname, int port, bool isSSL) {
  if (internalSocket != nullptr) {
    close();
  }
  if (isSSL) {
    internalSocket = std::make_unique<bell::TLSSocket>();
  } else {
    internalSocket = std::make_unique<bell::TCPSocket>();
  }

  internalSocket->open(hostname, port);
  return 0;
}

int SocketBuffer::close() {
  if (internalSocket != nullptr && isOpen()) {
    pubsync();
    internalSocket->close();
    internalSocket = nullptr;
  }
  return 0;
}

int SocketBuffer::sync() {
  ssize_t bw, n = pptr() - pbase();
  while (n > 0) {
    bw = internalSocket->write(reinterpret_cast<uint8_t*>(pptr() - n), n);
    if (bw < 0) {
      setp(pptr() - n, obuf + bufLen);
      pbump(n);
      return -1;
    }
    n -= bw;
  }
  setp(obuf, obuf + bufLen);
  return 0;
}

SocketBuffer::int_type SocketBuffer::underflow() {
  ssize_t br = internalSocket->read(reinterpret_cast<uint8_t*>(ibuf), bufLen);
  if (br <= 0) {
    setg(NULL, NULL, NULL);
    return traits_type::eof();
  }
  setg(ibuf, ibuf, ibuf + br);
  return traits_type::to_int_type(*ibuf);
}

SocketBuffer::int_type SocketBuffer::overflow(int_type c) {
  if (sync() < 0)
    return traits_type::eof();
  if (traits_type::eq_int_type(c, traits_type::eof()))
    return traits_type::not_eof(c);
  *pptr() = traits_type::to_char_type(c);
  pbump(1);
  return c;
}

std::streamsize SocketBuffer::xsgetn(char_type* __s, std::streamsize __n) {
  const std::streamsize bn = egptr() - gptr();
  if (__n <= bn) {
    traits_type::copy(__s, gptr(), __n);
    gbump(__n);
    return __n;
  }
  traits_type::copy(__s, gptr(), bn);
  setg(NULL, NULL, NULL);
  std::streamsize remain = __n - bn;
  char_type* end = __s + __n;
  ssize_t br;
  while (remain > 0) {
    br = internalSocket->read(reinterpret_cast<uint8_t*>(end - remain), remain);
    if (br <= 0)
      return (__n - remain);
    remain -= br;
  }
  return __n;
}

std::streamsize SocketBuffer::xsputn(const char_type* __s,
                                     std::streamsize __n) {
  if (pptr() + __n <= epptr()) {
    traits_type::copy(pptr(), __s, __n);
    pbump(__n);
    return __n;
  }
  if (sync() < 0)
    return 0;
  ssize_t bw;
  std::streamsize remain = __n;
  const char_type* end = __s + __n;
  while (remain > bufLen) {
    bw = internalSocket->write((uint8_t*)(end - remain), remain);
    if (bw < 0)
      return (__n - remain);
    remain -= bw;
  }
  if (remain > 0) {
    traits_type::copy(pptr(), end - remain, remain);
    pbump(remain);
  }
  return __n;
}
