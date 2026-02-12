/*
 * AirConnect: Chromecast & UPnP to AirPlay
 *
 * (c) Philippe 2016-2017, philippe_44@outlook.com
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 *
 */

#include "platform.h"

#ifdef WIN32
#include <iphlpapi.h>
#else
#include "esp_netif.h"
#include "tcpip_adapter_compat.h"
// IDF-V4++ #include "esp_netif.h"
#include <ctype.h>
#endif

#include <stdarg.h>

#include "pthread.h"
#include "util.h"
#include "log_util.h"

/*----------------------------------------------------------------------------*/
/* globals */
/*----------------------------------------------------------------------------*/

extern log_level util_loglevel;

/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/
static log_level* loglevel = &util_loglevel;

static char* ltrim(char* s);
static int read_line(int fd, char* line, int maxlen, int timeout);

/*----------------------------------------------------------------------------*/
/* 																			  */
/* NETWORKING utils															  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
#define MAX_INTERFACES 256
#define DEFAULT_INTERFACE 1
#if !defined(WIN32)
#define INVALID_SOCKET (-1)
#endif
in_addr_t get_localhost(char** name) {
#ifdef WIN32
    char buf[256];
    struct hostent* h = NULL;
    struct sockaddr_in LocalAddr;

    memset(&LocalAddr, 0, sizeof(LocalAddr));

    gethostname(buf, 256);
    h = gethostbyname(buf);

    if(name) *name = strdup(buf);

    if(h != NULL) {
        memcpy(&LocalAddr.sin_addr, h->h_addr_list[0], 4);
        return LocalAddr.sin_addr.s_addr;
    } else
        return INADDR_ANY;
#else
    tcpip_adapter_ip_info_t ipInfo;
    tcpip_adapter_if_t if_type = TCPIP_ADAPTER_IF_STA;

    // then get IP address
    tcpip_adapter_get_ip_info(if_type, &ipInfo);

    // we might be in AP mode
    if(ipInfo.ip.addr == INADDR_ANY) {
        if_type = TCPIP_ADAPTER_IF_AP;
        tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ipInfo);
    }

    // get hostname if required
    if(name) {
        const char* hostname;
        tcpip_adapter_get_hostname(if_type, &hostname);
        *name = strdup(hostname);
    }

    return ipInfo.ip.addr;
#endif
}

/*----------------------------------------------------------------------------*/
#ifdef WIN32
void winsock_init(void) {
    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(2, 2);
    int WSerr = WSAStartup(wVersionRequested, &wsaData);
    if(WSerr != 0) {
        LOG_ERROR("Bad winsock version", NULL);
        exit(1);
    }
}

/*----------------------------------------------------------------------------*/
void winsock_close(void) { WSACleanup(); }
#endif

/*----------------------------------------------------------------------------*/
int shutdown_socket(int sd) {
    if(sd <= 0) return -1;

#ifdef WIN32
    shutdown(sd, SD_BOTH);
#else
    shutdown(sd, SHUT_RDWR);
#endif

    LOG_DEBUG("closed socket %d", sd);

    return closesocket(sd);
}

/*----------------------------------------------------------------------------*/
int bind_socket(unsigned short* port, int mode) {
    int sock;
    socklen_t len = sizeof(struct sockaddr);
    struct sockaddr_in addr;

    if((sock = socket(AF_INET, mode, 0)) < 0) {
        LOG_ERROR("cannot create socket %d", sock);
        return sock;
    }

    /*  Populate socket address structure  */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(*port);
#ifdef SIN_LEN
    si.sin_len = sizeof(si);
#endif

    if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        closesocket(sock);
        LOG_ERROR("cannot bind socket %d", sock);
        return -1;
    }

    if(!*port) {
        getsockname(sock, (struct sockaddr*)&addr, &len);
        *port = ntohs(addr.sin_port);
    }

    LOG_DEBUG("socket binding %d on port %d", sock, *port);

    return sock;
}

/*----------------------------------------------------------------------------*/
int conn_socket(unsigned short port) {
    struct sockaddr_in addr;
    int sd;

    sd = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if(sd < 0 || connect(sd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sd);
        return -1;
    }

    LOG_DEBUG("created socket %d", sd);

    return sd;
}

/*----------------------------------------------------------------------------*/
/* 																			  */
/* SYSTEM utils															 	  */
/* 																			  */
/*----------------------------------------------------------------------------*/

#ifdef WIN32
/*----------------------------------------------------------------------------*/
void* dlopen(const char* filename, int flag) {
    SetLastError(0);
    return LoadLibrary((LPCTSTR)filename);
}

/*----------------------------------------------------------------------------*/
void* dlsym(void* handle, const char* symbol) {
    SetLastError(0);
    return (void*)GetProcAddress(handle, symbol);
}

/*----------------------------------------------------------------------------*/
char* dlerror(void) {
    static char ret[32];
    int last = GetLastError();
    if(last) {
        sprintf(ret, "code: %i", last);
        SetLastError(0);
        return ret;
    }
    return NULL;
}
#endif

/*----------------------------------------------------------------------------*/
/* 																			  */
/* STDLIB extensions													 	  */
/* 																			  */
/*----------------------------------------------------------------------------*/

#ifdef WIN32
/*---------------------------------------------------------------------------*/
char* strcasestr(const char* haystack, const char* needle) {
    size_t length_needle;
    size_t length_haystack;
    size_t i;

    if(!haystack || !needle) return NULL;

    length_needle = strlen(needle);
    length_haystack = strlen(haystack);

    if(length_haystack < length_needle) return NULL;

    length_haystack -= length_needle - 1;

    for(i = 0; i < length_haystack; i++) {
        size_t j;

        for(j = 0; j < length_needle; j++) {
            unsigned char c1;
            unsigned char c2;

            c1 = haystack[i + j];
            c2 = needle[j];
            if(toupper(c1) != toupper(c2)) goto next;
        }
        return (char*)haystack + i;
    next:;
    }

    return NULL;
}

/*---------------------------------------------------------------------------*/
char* strsep(char** stringp, const char* delim) {
    char* start = *stringp;
    char* p;

    p = (start != NULL) ? strpbrk(start, delim) : NULL;

    if(p == NULL) {
        *stringp = NULL;
    } else {
        *p = '\0';
        *stringp = p + 1;
    }

    return start;
}

/*---------------------------------------------------------------------------*/
char* strndup(const char* s, size_t n) {
    char* p = malloc(n + 1);
    strncpy(p, s, n);
    p[n] = '\0';

    return p;
}
#endif

/*----------------------------------------------------------------------------*/
char* strextract(char* s1, char* beg, char* end) {
    char *p1, *p2, *res;

    p1 = strcasestr(s1, beg);
    if(!p1) return NULL;

    p1 += strlen(beg);
    p2 = strcasestr(p1, end);
    if(!p2) return strdup(p1);

    res = malloc(p2 - p1 + 1);
    memcpy(res, p1, p2 - p1);
    res[p2 - p1] = '\0';

    return res;
}

#ifdef WIN32
/*----------------------------------------------------------------------------*/
int asprintf(char** strp, const char* fmt, ...) {
    va_list args, cp;
    int len, ret = 0;

    va_start(args, fmt);
    len = vsnprintf(NULL, 0, fmt, args);
    *strp = malloc(len + 1);

    if(*strp) ret = vsprintf(*strp, fmt, args);

    va_end(args);

    return ret;
}
#endif

/*---------------------------------------------------------------------------*/
static char* ltrim(char* s) {
    while(isspace((int)*s)) s++;
    return s;
}

/*----------------------------------------------------------------------------*/
/* 																			  */
/* HTTP management														 	  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
bool http_parse(int sock, char* method, key_data_t* rkd, char** body, int* len) {
    char line[256], *dp;
    unsigned j;
    int i, timeout = 100;

    rkd[0].key = NULL;

    if((i = read_line(sock, line, sizeof(line), timeout)) <= 0) {
        if(i < 0) { LOG_ERROR("cannot read method", NULL); }
        return false;
    }

    if(!sscanf(line, "%s", method)) {
        LOG_ERROR("missing method", NULL);
        return false;
    }

    i = *len = 0;

    while(read_line(sock, line, sizeof(line), timeout) > 0) {

        LOG_SDEBUG("sock: %u, received %s", line);

        // line folding should be deprecated
        if(i && rkd[i].key && (line[0] == ' ' || line[0] == '\t')) {
            for(j = 0; j < strlen(line); j++)
                if(line[j] != ' ' && line[j] != '\t') break;
            rkd[i].data = realloc(rkd[i].data, strlen(rkd[i].data) + strlen(line + j) + 1);
            strcat(rkd[i].data, line + j);
            continue;
        }

        dp = strstr(line, ":");

        if(!dp) {
            LOG_ERROR("Request failed, bad header", NULL);
            kd_free(rkd);
            return false;
        }

        *dp = 0;
        rkd[i].key = strdup(line);
        rkd[i].data = strdup(ltrim(dp + 1));

        if(!strcasecmp(rkd[i].key, "Content-Length")) *len = atol(rkd[i].data);

        i++;
        rkd[i].key = NULL;
    }

    if(*len) {
        int size = 0;

        *body = malloc(*len + 1);
        while(*body && size < *len) {
            int bytes = recv(sock, *body + size, *len - size, 0);
            if(bytes <= 0) break;
            size += bytes;
        }

        (*body)[*len] = '\0';

        if(!*body || size != *len) { LOG_ERROR("content length receive error %d %d", *len, size); }
    }

    return true;
}

/*----------------------------------------------------------------------------*/
static int read_line(int fd, char* line, int maxlen, int timeout) {
    int i, rval;
    int count = 0;
    struct pollfd pfds;
    char ch;

    *line = 0;
    pfds.fd = fd;
    pfds.events = POLLIN;

    for(i = 0; i < maxlen; i++) {
        if(poll(&pfds, 1, timeout))
            rval = recv(fd, &ch, 1, 0);
        else
            return 0;

        if(rval == -1) {
            if(errno == EAGAIN) return 0;
            LOG_ERROR("fd: %d read error: %s", fd, strerror(errno));
            return -1;
        }

        if(rval == 0) {
            LOG_INFO("disconnected on the other end %u", fd);
            return 0;
        }

        if(ch == '\n') {
            *line = 0;
            return count;
        }

        if(ch == '\r') continue;

        *line++ = ch;
        count++;
        if(count >= maxlen - 1) break;
    }

    *line = 0;
    return count;
}

/*----------------------------------------------------------------------------*/
char* http_send(int sock, char* method, key_data_t* rkd) {
    unsigned sent, len;
    char* resp = kd_dump(rkd);
    char* data = malloc(strlen(method) + 2 + strlen(resp) + 2 + 1);

    len = sprintf(data, "%s\r\n%s\r\n", method, resp);
    NFREE(resp);

    sent = send(sock, data, len, 0);

    if(sent != len) {
        LOG_ERROR("HTTP send() error:%s %u (strlen=%u)", data, sent, len);
        NFREE(data);
    }

    return data;
}

/*----------------------------------------------------------------------------*/
char* kd_lookup(key_data_t* kd, char* key) {
    int i = 0;
    while(kd && kd[i].key) {
        if(!strcasecmp(kd[i].key, key)) return kd[i].data;
        i++;
    }
    return NULL;
}

/*----------------------------------------------------------------------------*/
bool kd_add(key_data_t* kd, char* key, char* data) {
    int i = 0;
    while(kd && kd[i].key) i++;

    kd[i].key = strdup(key);
    kd[i].data = strdup(data);
    kd[i + 1].key = NULL;

    return NULL;
}

/*----------------------------------------------------------------------------*/
void kd_free(key_data_t* kd) {
    int i = 0;
    while(kd && kd[i].key) {
        free(kd[i].key);
        if(kd[i].data) free(kd[i].data);
        i++;
    }

    kd[0].key = NULL;
}

/*----------------------------------------------------------------------------*/
char* kd_dump(key_data_t* kd) {
    int i = 0;
    int pos = 0, size = 0;
    char* str = NULL;

    if(!kd || !kd[0].key) return strdup("\r\n");

    while(kd && kd[i].key) {
        char* buf;
        int len;

        len = asprintf(&buf, "%s: %s\r\n", kd[i].key, kd[i].data);

        while(pos + len >= size) {
            void* p = realloc(str, size + 1024);
            size += 1024;
            if(!p) {
                free(str);
                return NULL;
            }
            str = p;
        }

        memcpy(str + pos, buf, len);

        pos += len;
        free(buf);
        i++;
    }

    str[pos] = '\0';

    return str;
}

/*--------------------------------------------------------------------------*/
void free_metadata(struct metadata_s* metadata) {
    NFREE(metadata->artist);
    NFREE(metadata->album);
    NFREE(metadata->title);
    NFREE(metadata->genre);
    NFREE(metadata->path);
    NFREE(metadata->artwork);
    NFREE(metadata->remote_title);
}

/*----------------------------------------------------------------------------*/

int _fprintf(FILE* file, ...) {
    va_list args;
    char* fmt;
    int n;

    va_start(args, file);
    fmt = va_arg(args, char*);

    n = vfprintf(file, fmt, args);
    va_end(args);
    return n;
}
