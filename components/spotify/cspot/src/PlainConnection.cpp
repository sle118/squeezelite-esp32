#include "PlainConnection.h"

#ifndef _WIN32
#include <netdb.h>  // for addrinfo, freeaddrinfo, getaddrinfo
#include <netdb.h>
#include <netinet/in.h>   // for IPPROTO_IP, IPPROTO_TCP
#include <netinet/tcp.h>  // for TCP_NODELAY
#include <sys/errno.h>    // for EAGAIN, EINTR, ETIMEDOUT, errno
#include <sys/socket.h>   // for setsockopt, connect, recv, send, shutdown
#include <sys/time.h>     // for timeval
#include <cstring>        // for memset
#include <stdexcept>      // for runtime_error
#else
#include <ws2tcpip.h>
#endif
#include "BellLogger.h"  // for AbstractLogger
#include "Logger.h"      // for CSPOT_LOG
#include "Packet.h"      // for cspot
#include "Utils.h"       // for extract, pack

using namespace cspot;

static int getErrno() {
#ifdef _WIN32
  int code = WSAGetLastError();
  if (code == WSAETIMEDOUT)
    return ETIMEDOUT;
  if (code == WSAEINTR)
    return EINTR;
  return code;
#else
  return errno;
#endif
}

PlainConnection::PlainConnection() {
  this->apSock = -1;
};

PlainConnection::~PlainConnection() {
  this->close();
};

void PlainConnection::connect(const std::string& apAddress) {
  struct addrinfo h, *airoot, *ai;
  std::string hostname = apAddress.substr(0, apAddress.find(":"));
  std::string portStr =
      apAddress.substr(apAddress.find(":") + 1, apAddress.size());
  memset(&h, 0, sizeof(h));
  h.ai_family = AF_INET;
  h.ai_socktype = SOCK_STREAM;
  h.ai_protocol = IPPROTO_IP;

  // Lookup host
  if (getaddrinfo(hostname.c_str(), portStr.c_str(), &h, &airoot)) {
    CSPOT_LOG(error, "getaddrinfo failed");
  }

  // find the right ai, connect to server
  for (ai = airoot; ai; ai = ai->ai_next) {
    if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
      continue;

    this->apSock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (this->apSock < 0)
      continue;

    if (::connect(this->apSock, (struct sockaddr*)ai->ai_addr,
                  ai->ai_addrlen) != -1) {
#ifdef _WIN32
      uint32_t tv = 3000;
#else
      struct timeval tv;
      tv.tv_sec = 3;
      tv.tv_usec = 0;
#endif
      setsockopt(this->apSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,
                 sizeof tv);
      setsockopt(this->apSock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv,
                 sizeof tv);

      int flag = 1;
      setsockopt(this->apSock, /* socket affected */
                 IPPROTO_TCP,  /* set option at TCP level */
                 TCP_NODELAY,  /* name of option */
                 (char*)&flag, /* the cast is historical cruft */
                 sizeof(int)); /* length of option value */
      break;
    }

#ifdef _WIN32
    closesocket(this->apSock);
#else
    ::close(this->apSock);
#endif
    apSock = -1;
    throw std::runtime_error("Can't connect to spotify servers");
  }

  freeaddrinfo(airoot);
  CSPOT_LOG(debug, "Connected to spotify server");
}

std::vector<uint8_t> PlainConnection::recvPacket() {
  // Read packet size
  std::vector<uint8_t> packetBuffer(4);
  readBlock(packetBuffer.data(), 4);
  uint32_t packetSize = ntohl(extract<uint32_t>(packetBuffer, 0));

  packetBuffer.resize(packetSize, 0);

  // Read actual data
  readBlock(packetBuffer.data() + 4, packetSize - 4);

  return packetBuffer;
}

std::vector<uint8_t> PlainConnection::sendPrefixPacket(
    const std::vector<uint8_t>& prefix, const std::vector<uint8_t>& data) {
  // Calculate full packet length
  uint32_t actualSize = prefix.size() + data.size() + sizeof(uint32_t);

  // Packet structure [PREFIX] + [SIZE] +  [DATA]
  auto sizeRaw = pack<uint32_t>(htonl(actualSize));
  sizeRaw.insert(sizeRaw.begin(), prefix.begin(), prefix.end());
  sizeRaw.insert(sizeRaw.end(), data.begin(), data.end());

  // Actually write it to the server
  writeBlock(sizeRaw);

  return sizeRaw;
}

void PlainConnection::readBlock(const uint8_t* dst, size_t size) {
  unsigned int idx = 0;
  ssize_t n;
  int retries = 0;

  while (idx < size) {
  READ:
    if ((n = recv(this->apSock, (char*)&dst[idx], size - idx, 0)) <= 0) {
      switch (getErrno()) {
        case EAGAIN:
        case ETIMEDOUT:
          if (timeoutHandler()) {
            CSPOT_LOG(error, "Connection lost, will need to reconnect...");
            throw std::runtime_error("Reconnection required");
          }
          goto READ;
        case EINTR:
          break;
        default:
          if (retries++ > 4)
            throw std::runtime_error("Error in read");
          goto READ;
      }
    }
    idx += n;
  }
}

size_t PlainConnection::writeBlock(const std::vector<uint8_t>& data) {
  unsigned int idx = 0;
  ssize_t n;

  int retries = 0;

  while (idx < data.size()) {
  WRITE:
    if ((n = send(this->apSock, (char*)&data[idx],
                  data.size() - idx < 64 ? data.size() - idx : 64, 0)) <= 0) {
      switch (getErrno()) {
        case EAGAIN:
        case ETIMEDOUT:
          if (timeoutHandler()) {
            throw std::runtime_error("Reconnection required");
          }
          goto WRITE;
        case EINTR:
          break;
        default:
          if (retries++ > 4)
            throw std::runtime_error("Error in write");
          goto WRITE;
      }
    }
    idx += n;
  }

  return data.size();
}

void PlainConnection::close() {
  if (this->apSock < 0)
    return;

  CSPOT_LOG(info, "Closing socket...");
  shutdown(this->apSock, SHUT_RDWR);
#ifdef _WIN32
  closesocket(this->apSock);
#else
  ::close(this->apSock);
#endif
  this->apSock = -1;
}
