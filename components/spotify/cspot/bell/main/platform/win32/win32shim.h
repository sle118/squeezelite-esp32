#pragma once

#include <winsock2.h>

#define SHUT_RDWR SD_BOTH
#define ssize_t SSIZE_T

#define strcasecmp stricmp
#define strncasecmp _strnicmp
#define bzero(p, n) memset(p, 0, n)
#define usleep(x) Sleep((x) / 1000)

inline size_t read(int sock, char* buf, size_t n) {
  return recv(sock, buf, n, 0);
}
inline int write(int sock, const char* buf, size_t n) {
  return send(sock, buf, n, 0);
}
