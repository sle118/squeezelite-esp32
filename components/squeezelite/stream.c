/* 
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2015, triode1@btinternet.com
 *      Ralph Irving 2015-2017, ralph_irving@hotmail.com
 *  
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// stream thread

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "squeezelite.h"

#include <fcntl.h>

#if USE_SSL
#include "openssl/ssl.h"
#include "openssl/err.h"
#endif

#if SUN
#include <signal.h>
#endif
static log_level loglevel;

static struct buffer buf;
struct buffer *streambuf = &buf;

#define LOCK     mutex_lock(streambuf->mutex)
#define UNLOCK   mutex_unlock(streambuf->mutex)

/* 
When LMS sends a close/open sequence very quickly, the stream thread might
still be waiting in the poll() on the closed socket. It is never recommended
to have a thread closing a socket used by another thread but it works, as
opposed to an infinite select(). 
In stream_sock() a new socket is created and full OS will allocate a different
one but on RTOS and simple IP stack, the same might be re-used and that causes
an exception as a thread is already waiting on a newly allocated socket
A simple variable that forces stream_sock() to wait until we are out of poll()
is enough and much faster than a mutex 
*/
static bool polling;
static sockfd fd;

struct EXT_RAM_ATTR streamstate stream;

static EXT_RAM_ATTR struct {
    bool flac;
    u64_t serial;
	enum { OGG_OFF, OGG_SYNC, OGG_HEADER, OGG_SEGMENTS, OGG_PAGE } state;
	size_t want, miss, match;
	u8_t* data, segments[255];
#pragma pack(push, 1)    
	struct {
		char pattern[4];
		u8_t version, type;
		u64_t granule;
		u32_t serial, page, checksum;
		u8_t count;
	} header;
#pragma pack(pop)    
} ogg;

#if USE_SSL
static SSL_CTX *SSLctx;
SSL *ssl;
#endif

static inline u32_t read_le32(const void *src) {
    const u8_t *p = src;
    return (u32_t)p[0]
         | (u32_t)p[1] << 8
         | (u32_t)p[2] << 16
         | (u32_t)p[3] << 24;
}

#if !USE_SSL
#define _recv(ssl, fc, buf, n, opt) recv(fd, buf, n, opt)
#define _send(ssl, fd, buf, n, opt) send(fd, buf, n, opt)
#define _poll(ssl, pollinfo, timeout) poll(pollinfo, 1, timeout)
#define _last_error() last_error()
#else
#define _last_error() ERROR_WOULDBLOCK

static int _recv(SSL *ssl, int fd, void *buffer, size_t bytes, int options) {
	int n;
	if (!ssl) return recv(fd, buffer, bytes, options);
	n = SSL_read(ssl, (u8_t*) buffer, bytes);
	if (n <= 0 && SSL_get_error(ssl, n) == SSL_ERROR_ZERO_RETURN) return 0;
	return n;
}

static int _send(SSL *ssl, int fd, void *buffer, size_t bytes, int options) {
	int n;
	if (!ssl) return send(fd, buffer, bytes, options);
	while (1) {
		int err;
		ERR_clear_error();
		if ((n = SSL_write(ssl, (u8_t*) buffer, bytes)) >= 0) return n;
		err = SSL_get_error(ssl, n);
		if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) continue;
		LOG_INFO("SSL write error %d", err );
		return n;
	}
}

/*
can't mimic exactly poll as SSL is a real pain. Even if SSL_pending returns
0, there might be bytes to read but when select (poll) return > 0, there might
be no frame available. As well select (poll) < 0 does not mean that there is
no data pending
*/
static int _poll(SSL *ssl, struct pollfd *pollinfo, int timeout) {
	if (!ssl) return poll(pollinfo, 1, timeout);
	if (pollinfo->events & POLLIN && SSL_pending(ssl)) {
		if (pollinfo->events & POLLOUT) poll(pollinfo, 1, 0);
		pollinfo->revents = POLLIN;
		return 1;
	}
	return poll(pollinfo, 1, timeout);
}
#endif

static bool send_header(void) {
	char *ptr = stream.header;
	int len = stream.header_len;

	unsigned try = 0;
	ssize_t n;
	
	while (len) {
		n = _send(ssl, fd, ptr, len, MSG_NOSIGNAL);
		if (n <= 0) {
			if (n < 0 && _last_error() == ERROR_WOULDBLOCK && try < 10) {
				LOG_SDEBUG("retrying (%d) writing to socket", ++try);
				usleep(1000);
				continue;
			}
			LOG_INFO("failed writing to socket: %s", strerror(last_error()));
			stream.disconnect = LOCAL_DISCONNECT;
			stream.state = DISCONNECT;
			wake_controller();
			return false;
		}
		LOG_SDEBUG("wrote %d bytes to socket", n);
		ptr += n;
		len -= n;
	}
	LOG_SDEBUG("wrote header");
	return true;
}

static bool running = true;

static void _disconnect(stream_state state, disconnect_code disconnect) {
	stream.state = state;
	stream.disconnect = disconnect;
    if (ogg.state == OGG_PAGE && ogg.data) free(ogg.data);
    ogg.data = NULL;
#if USE_SSL
	if (ssl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
		ssl = NULL;
	}
#endif
	closesocket(fd);
	fd = -1;
	wake_controller();
}

static size_t memfind(const u8_t* haystack, size_t n, const char* needle, size_t len, size_t* offset) {
	size_t i;
	for (i = 0; i < n && *offset != len; i++) *offset = (haystack[i] == needle[*offset]) ? *offset + 1 : 0;
	return i;
}

/* https://xiph.org/ogg/doc/framing.html 
 * https://xiph.org/flac/ogg_mapping.html
 * https://xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-610004.2 */
 
static void stream_ogg(size_t n) {
	if (ogg.state == OGG_OFF) return;
	u8_t* p = streambuf->writep;

	while (n) {
		size_t consumed = min(ogg.miss, n);

		// copy as many bytes as possible and come back later if we do'nt have enough
		if (ogg.data) {
			memcpy(ogg.data + ogg.want - ogg.miss, p, consumed);
			ogg.miss -= consumed;
			if (ogg.miss) return;
		}

		// we have what we want, let's parse
		switch (ogg.state) {
		case OGG_SYNC: {
			ogg.miss -= consumed;
			if (consumed) break;

			// we have to memorize position in case any of last 3 bytes match...
			size_t pos = memfind(p, n, "OggS", 4, &ogg.match);
			if (ogg.match == 4) {
				consumed = pos - ogg.match;
				ogg.state = OGG_HEADER;
				ogg.miss = ogg.want = sizeof(ogg.header);
				ogg.data = (u8_t*) &ogg.header;
				ogg.match = 0;
			} else {
                if (!ogg.match) LOG_INFO("OggS not at expected position %zu/%zu", pos, n);
                LOG_INFO("OggS not at expected position %zu/%zu", pos, n);
				return;
			}
			break;
		}
		case OGG_HEADER:
			if (!memcmp(ogg.header.pattern, "OggS", 4)) {
				ogg.miss = ogg.want = ogg.header.count;
				ogg.data = ogg.segments;
				ogg.state = OGG_SEGMENTS;
			} else {
				ogg.state = OGG_SYNC;
				ogg.data = NULL;
			}
			break;
		case OGG_SEGMENTS:
			// calculate size of page using lacing values
			for (size_t i = 0; i < ogg.want; i++) ogg.miss += ogg.data[i];
			ogg.want = ogg.miss;
            
            // acquire serial number when we are looking for headers and hit a bos
			if (ogg.serial == ULLONG_MAX && (ogg.header.type & 0x02)) ogg.serial = ogg.header.serial;

			// we have overshot and missed header, reset serial number to restart search (O and -1 are le/be)
			if (ogg.header.serial == ogg.serial && ogg.header.granule && ogg.header.granule != -1) ogg.serial = ULLONG_MAX;

			// not our serial (the above protected us from granule > 0)
			if (ogg.header.serial != ogg.serial) {
				// otherwise, jump over data
				ogg.state = OGG_SYNC;
				ogg.data = NULL;
			} else {
				ogg.state = OGG_PAGE;
				ogg.data = malloc(ogg.want);
			}
			break;
		case OGG_PAGE: {
			char** tag = (char* []){ "\x3vorbis", "OpusTags", NULL };
			size_t ofs = 0;

			/* with OggFlac, we need the next page (packet) - VorbisComment is wrapped into a FLAC_METADATA
			 * and except with vorbis, comment packet starts a new page but even in vorbis, it won't span
			 * accross multiple pages */
			if (ogg.flac) ofs = 4;
			else if (!memcmp(ogg.data, "\x7f""FLAC", 5)) ogg.flac = true;
			else for (size_t n = 0; *tag; tag++, ofs = 0) if ((ofs = memfind(ogg.data, ogg.want, *tag, strlen(*tag), &n)) && n == strlen(*tag)) break;
	
			if (ofs) {			
				// u32:len,char[]:vendorId, u32:N, N x (u32:len,char[]:comment)
				char* p = (char*) ogg.data + ofs;
				p += read_le32(p) + 4;
				u32_t count = read_le32(p);
				p += 4;

				// LMS metadata format for Ogg is "Ogg", N x (u16:len,char[]:comment)
				memcpy(stream.header, "Ogg", 3);
				stream.header_len = 3;

				for (u32_t len; count--; p += len) {
					len = read_le32(p);
					p += 4;

					// only report what we use and don't overflow (network byte order)
					if (!strncasecmp(p, "TITLE=", 6) || !strncasecmp(p, "ARTIST=", 7) || !strncasecmp(p, "ALBUM=", 6)) {
						if (stream.header_len + len + 2 > MAX_HEADER) break;
						stream.header[stream.header_len++] = len >> 8;
						stream.header[stream.header_len++] = len;
						memcpy(stream.header + stream.header_len, p, len);
						stream.header_len += len;
                        LOG_INFO("metadata: %.*s", len, p);
					}
				}

                ogg.flac = false;
                ogg.serial = ULLONG_MAX;
				stream.meta_send = true;
				wake_controller();
				LOG_INFO("Ogg metadata length: %u", stream.header_len - 3);
			}
			free(ogg.data);
            ogg.data = NULL;
			ogg.state = OGG_SYNC;
			break;
		}
        default: 
            break;
		}

		p += consumed;
		n -= consumed;
	}
}

static void *stream_thread() {

	while (running) {

		struct pollfd pollinfo;
		size_t space;

		LOCK;

		space = min(_buf_space(streambuf), _buf_cont_write(streambuf));

		if (fd < 0 || !space || stream.state <= STREAMING_WAIT) {
			UNLOCK;
			usleep(space ? 100000 : 25000);
			continue;
		}

		if (stream.state == STREAMING_FILE) {

			int n = read(fd, streambuf->writep, space);
			if (n == 0) {
				LOG_INFO("end of stream");
				_disconnect(DISCONNECT, DISCONNECT_OK);
			}
			if (n > 0) {
				_buf_inc_writep(streambuf, n);
				stream.bytes += n;
				LOG_SDEBUG("streambuf read %d bytes", n);
			}
			if (n < 0) {
				LOG_WARN("error reading: %s", strerror(last_error()));
				_disconnect(DISCONNECT, REMOTE_DISCONNECT);
			}

			UNLOCK;
			continue;

		} else {

			pollinfo.fd = fd;
			pollinfo.events = POLLIN;
			if (stream.state == SEND_HEADERS) {
				pollinfo.events |= POLLOUT;
			}
		}

		UNLOCK;
		// no mutex needed - we just want to know if we are inside poll()
		polling = true;
		
		if (_poll(ssl, &pollinfo, 100)) {

			polling = false;
			LOCK;

			// check socket has not been closed while in poll
			if (fd < 0) {
				UNLOCK;
				continue;
			}

			if ((pollinfo.revents & POLLOUT) && stream.state == SEND_HEADERS) {
				if (send_header()) stream.state = RECV_HEADERS;
				stream.header_len = 0;
				UNLOCK;
				continue;
			}
					
			if (pollinfo.revents & (POLLIN | POLLHUP)) {

				// get response headers
				if (stream.state == RECV_HEADERS) {

					// read one byte at a time to catch end of header
					char c;
					static int endtok;

					int n = _recv(ssl, fd, &c, 1, 0);
					if (n <= 0) {
						if (n < 0 && _last_error() == ERROR_WOULDBLOCK) {
							UNLOCK;
							continue;
						}
						LOG_INFO("error reading headers: %s", n ? strerror(last_error()) : "closed");
						_disconnect(STOPPED, LOCAL_DISCONNECT);
						UNLOCK;
						continue;
					}

					*(stream.header + stream.header_len) = c;
					stream.header_len++;

					if (stream.header_len > MAX_HEADER - 1) {
						LOG_ERROR("received headers too long: %u", stream.header_len);
						_disconnect(DISCONNECT, LOCAL_DISCONNECT);
					}

					if (stream.header_len > 1 && (c == '\r' || c == '\n')) {
						endtok++;
						if (endtok == 4) {
							*(stream.header + stream.header_len) = '\0';
							LOG_INFO("headers: len: %d\n%s", stream.header_len, stream.header);
							stream.state = stream.cont_wait ? STREAMING_WAIT : STREAMING_BUFFERING;
							wake_controller();
						}
					} else {
						endtok = 0;
					}
				
					UNLOCK;
					continue;
				}
				
				// receive icy meta data
				if (stream.meta_interval && stream.meta_next == 0) {

					if (stream.meta_left == 0) {
						// read meta length
						u8_t c;
						int n = _recv(ssl, fd, &c, 1, 0);
						if (n <= 0) {
							if (n < 0 && _last_error() == ERROR_WOULDBLOCK) {
								UNLOCK;
								continue;
							}
							LOG_INFO("error reading icy meta: %s", n ? strerror(last_error()) : "closed");
							_disconnect(STOPPED, LOCAL_DISCONNECT);
							UNLOCK;
							continue;
						}
						stream.meta_left = 16 * c;
						stream.header_len = 0; // amount of received meta data
						// MAX_HEADER must be more than meta max of 16 * 255
					}

					if (stream.meta_left) {
						int n = _recv(ssl, fd, stream.header + stream.header_len, stream.meta_left, 0);
						if (n <= 0) {
							if (n < 0 && _last_error() == ERROR_WOULDBLOCK) {
								UNLOCK;
								continue;
							}
							LOG_INFO("error reading icy meta: %s", n ? strerror(last_error()) : "closed");
							_disconnect(STOPPED, LOCAL_DISCONNECT);
							UNLOCK;
							continue;
						}
						stream.meta_left -= n;
						stream.header_len += n;
					}
					
					if (stream.meta_left == 0) {
						if (stream.header_len) {
							*(stream.header + stream.header_len) = '\0';
							LOG_INFO("icy meta: len: %u\n%s", stream.header_len, stream.header);
							stream.meta_send = true;
							wake_controller();
						}
						stream.meta_next = stream.meta_interval;
						UNLOCK;
						continue;
					}

				// stream body into streambuf
				} else {
					int n;

					space = min(_buf_space(streambuf), _buf_cont_write(streambuf));
					if (stream.meta_interval) {
						space = min(space, stream.meta_next);
					}

					n = _recv(ssl, fd, streambuf->writep, space, 0);
					if (n == 0) {
						LOG_INFO("end of stream (%u bytes)", stream.bytes);
						_disconnect(DISCONNECT, DISCONNECT_OK);
					}
					if (n < 0 && _last_error() != ERROR_WOULDBLOCK) {
						LOG_INFO("error reading: %s", strerror(last_error()));
						_disconnect(DISCONNECT, REMOTE_DISCONNECT);
					}
					
					if (n > 0) {
                        stream_ogg(n);
						_buf_inc_writep(streambuf, n);
						stream.bytes += n;
						if (stream.meta_interval) {
							stream.meta_next -= n;
						}
					} else {
						UNLOCK;
						continue;
					}

					if (stream.state == STREAMING_BUFFERING && stream.bytes > stream.threshold) {
						stream.state = STREAMING_HTTP;
						wake_controller();
					}
				
					LOG_SDEBUG("streambuf read %d bytes", n);
				}
			}

			UNLOCK;
			
		} else {
			polling = false;
			LOG_SDEBUG("poll timeout");
		}
	}
	
#if USE_SSL	
	if (SSLctx) {
		SSL_CTX_free(SSLctx);
	}	
#endif	

	return 0;
}

static thread_type thread;

void stream_init(log_level level, unsigned stream_buf_size) {
	loglevel = level;

	LOG_INFO("init stream");
	LOG_DEBUG("streambuf size: %u", stream_buf_size);

	buf_init(streambuf, stream_buf_size);
	if (streambuf->buf == NULL) {
		LOG_ERROR("unable to malloc buffer");
		exit(2);
	}
	
#if USE_SSL
#if !LINKALL && !NO_SSLSYM
	if (ssl_loaded) {
#endif
	SSL_library_init();
	SSLctx = SSL_CTX_new(SSLv23_client_method());
	if (SSLctx == NULL) {
		LOG_ERROR("unable to allocate SSL context");
		exit(3);
	}	
	SSL_CTX_set_options(SSLctx, SSL_OP_NO_SSLv2);
#if !LINKALL && !NO_SSLSYM
	}
#endif	
	ssl = NULL;
#endif
	
#if SUN
	signal(SIGPIPE, SIG_IGN);	/* Force sockets to return -1 with EPIPE on pipe signal */
#endif
	stream.state = STOPPED;
	stream.header = malloc(MAX_HEADER);
	*stream.header = '\0';

	fd = -1;

#if LINUX || FREEBSD
	touch_memory(streambuf->buf, streambuf->size);
#endif

#if LINUX || OSX || FREEBSD || EMBEDDED
	pthread_attr_t attr;
	pthread_attr_init(&attr);
#ifdef PTHREAD_STACK_MIN	
	pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + STREAM_THREAD_STACK_SIZE);
#endif
	pthread_create_name(&thread, &attr, stream_thread, NULL, "stream");
	pthread_attr_destroy(&attr);
#endif
#if WIN
	thread = CreateThread(NULL, STREAM_THREAD_STACK_SIZE, (LPTHREAD_START_ROUTINE)&stream_thread, NULL, 0, NULL);
#endif
}

void stream_close(void) {
	LOG_INFO("close stream");
	LOCK;
	running = false;
	UNLOCK;
#if LINUX || OSX || FREEBSD || EMBEDDED
	pthread_join(thread, NULL);
#endif
	free(stream.header);
	buf_destroy(streambuf);
}

void stream_file(const char *header, size_t header_len, unsigned threshold) {
	buf_flush(streambuf);

	LOCK;

	stream.header_len = header_len;
	memcpy(stream.header, header, header_len);
	*(stream.header+header_len) = '\0';

	LOG_INFO("opening local file: %s", stream.header);

#if WIN
	fd = open(stream.header, O_RDONLY | O_BINARY);
#else
	fd = open(stream.header, O_RDONLY);
#endif

	stream.state = STREAMING_FILE;
	if (fd < 0) {
		LOG_INFO("can't open file: %s", stream.header);
		stream.state = DISCONNECT;
	}
	wake_controller();
	
	stream.cont_wait = false;
	stream.meta_interval = 0;
	stream.meta_next = 0;
	stream.meta_left = 0;
	stream.meta_send = false;
	stream.sent_headers = false;
	stream.bytes = 0;
	stream.threshold = threshold;

	UNLOCK;
}

void stream_sock(u32_t ip, u16_t port, bool use_ssl, bool use_ogg, const char *header, size_t header_len, unsigned threshold, bool cont_wait) {
	struct sockaddr_in addr;

#if EMBEDDED
	// wait till we are not polling anymore
	while (polling && running) { usleep(10000);	}	
#endif	

	int sock = socket(AF_INET, SOCK_STREAM, 0);

	if (sock < 0) {
		LOG_ERROR("failed to create socket");
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = ip;
	addr.sin_port = port;

	LOG_INFO("connecting to %s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

	set_nonblock(sock);
	set_nosigpipe(sock);

	if (connect_timeout(sock, (struct sockaddr *) &addr, sizeof(addr), 10) < 0) {
		LOG_INFO("unable to connect to server");
		LOCK;
		stream.state = DISCONNECT;
		stream.disconnect = UNREACHABLE;
		UNLOCK;
		return;
	}
	
#if USE_SSL
	if (ntohs(port) == 443) {
		char server[256], *p;

		ssl = SSL_new(SSLctx);
		SSL_set_fd(ssl, sock);

		// add SNI
		sscanf(header, "Host:%255s", server);
		if (server) {
			if ((p = strchr(server, ':')) != NULL) *p = '\0';
			SSL_set_tlsext_host_name(ssl, server);
		}
		
		while (1) {
			int status, err = 0;

			ERR_clear_error();
			status = SSL_connect(ssl);

			// successful negotiation
			if (status == 1) break;

			// error or non-blocking requires more time
			if (status < 0) {
				err = SSL_get_error(ssl, status);
				if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) continue;
			}

			LOG_WARN("unable to open SSL socket %d (%d)", status, err);
			closesocket(sock);
			SSL_free(ssl);
			ssl = NULL;
			LOCK;
			stream.state = DISCONNECT;
			stream.disconnect = UNREACHABLE;
			UNLOCK;

			return;
		}
	} else {
		ssl = NULL;	
	}
#endif

	buf_flush(streambuf);

	LOCK;

	fd = sock;
	stream.state = SEND_HEADERS;
	stream.cont_wait = cont_wait;
	stream.meta_interval = 0;
	stream.meta_next = 0;
	stream.meta_left = 0;
	stream.meta_send = false;
	stream.header_len = header_len;
	memcpy(stream.header, header, header_len);
	*(stream.header+header_len) = '\0';

	LOG_INFO("header: %s", stream.header);

	stream.sent_headers = false;
	stream.bytes = 0;
	stream.threshold = threshold;
    
    ogg.miss = ogg.match = 0;
	ogg.state = use_ogg ? OGG_SYNC : OGG_OFF;
    ogg.flac = false;
    ogg.serial = ULLONG_MAX;

	UNLOCK;
}

bool stream_disconnect(void) {
	bool disc = false;
	LOCK;
#if USE_SSL
	if (ssl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
		ssl = NULL;
	}
#endif
	if (fd != -1) {
		closesocket(fd);
		fd = -1;
		disc = true;
	}
	stream.state = STOPPED;
    if (ogg.state == OGG_PAGE && ogg.data) free(ogg.data);
    ogg.data = NULL;
	UNLOCK;
	return disc;
}
