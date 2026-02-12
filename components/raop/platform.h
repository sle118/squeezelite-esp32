/*
 *  platform setting definition
 *
 *  (c) Philippe, philippe_44@outlook.com
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 *
 */

#ifndef __PLATFORM_H
#define __PLATFORM_H

#ifdef WIN32
#define LINUX 0
#define WIN 1
#else
#define LINUX 1
#define WIN 0
#endif

#include <stdbool.h>
#include <signal.h>
#include <sys/stat.h>

#ifdef WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <iphlpapi.h>
#include <sys/timeb.h>

typedef unsigned __int8 u8_t;
typedef unsigned __int16 u16_t;
typedef unsigned __int32 u32_t;
typedef unsigned __int64 u64_t;
typedef __int16 s16_t;
typedef __int32 s32_t;
typedef __int64 s64_t;

#define inline __inline

int gettimeofday(struct timeval* tv, struct timezone* tz);
char* strcasestr(const char* haystack, const char* needle);

#define usleep(x) Sleep((x) / 1000)

#define sleep(x) Sleep((x) * 1000)
#define last_error() WSAGetLastError()
#define ERROR_WOULDBLOCK WSAEWOULDBLOCK
#define open _open
#define read _read
#define poll WSAPoll
#define snprintf _snprintf
#define strcasecmp stricmp
#define _random(x) random(x)
#define VALGRIND_MAKE_MEM_DEFINED(x, y)
#define S_ADDR(X) X.S_un.S_addr

#define in_addr_t u32_t
#define socklen_t int
#define ssize_t int

#define RTLD_NOW 0

#else

#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
/*
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <netdb.h>
*/
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <lwip/inet.h>
#include <pthread.h>
#include <errno.h>

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

typedef int16_t s16_t;
typedef int32_t s32_t;
typedef int64_t s64_t;
typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef unsigned long long u64_t;

#define last_error() errno
#define ERROR_WOULDBLOCK EWOULDBLOCK

char* strlwr(char* str);
#define _random(x) random()
#define closesocket(s) close(s)
#define S_ADDR(X) X.s_addr

#endif

typedef struct ntp_s {
    u32_t seconds;
    u32_t fraction;

} ntp_t;

u64_t timeval_to_ntp(struct timeval tv, struct ntp_s* ntp);
u64_t get_ntp(struct ntp_s* ntp);
// we expect somebody to provide the ms clock, system-wide
u32_t _gettime_ms_(void);
#define gettime_ms _gettime_ms_

#endif // __PLATFORM
