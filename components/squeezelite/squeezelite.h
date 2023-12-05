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
 * Additions (c) Paul Hermann, 2015-2017 under the same license terms
 *   -Control of Raspberry pi GPIO for amplifier power
 *   -Launch script on power status change from LMS
 */

// make may define: PORTAUDIO, SELFPIPE, RESAMPLE, RESAMPLE_MP, VISEXPORT, GPIO, IR, DSD, LINKALL to influence build

#define MAJOR_VERSION "1"
#define MINOR_VERSION "0"
#define MICRO_VERSION ""

#if defined(CUSTOM_VERSION)
#define VERSION "v" MAJOR_VERSION "." MINOR_VERSION "-" MICRO_VERSION STR(CUSTOM_VERSION)
#else
#define VERSION "v" MAJOR_VERSION "." MINOR_VERSION "-" MICRO_VERSION
#endif


#if !defined(MODEL_NAME)
#define MODEL_NAME SqueezeLite
#endif

#define QUOTE(name) #name
#define STR(macro)  QUOTE(macro)
#define MODEL_NAME_STRING STR(MODEL_NAME)

// build detection 
#if defined (EMBEDDED)
#undef EMBEDDED
#define EMBEDDED  1
#elif defined(linux)
#define LINUX     1
#define OSX       0
#define WIN       0
#define FREEBSD   0
#elif defined (__APPLE__)
#define LINUX     0
#define OSX       1
#define WIN       0
#define FREEBSD   0
#elif defined (_MSC_VER)
#define LINUX     0
#define OSX       0
#define WIN       1
#define FREEBSD   0
#elif defined(__FreeBSD__)
#define LINUX     0
#define OSX       0
#define WIN       0
#define FREEBSD   1
#elif defined (__sun)
#define SUN       1
#define LINUX     1
#define PORTAUDIO 1
#define PA18API   1
#define OSX       0
#define WIN       0
#else
#error unknown target
#endif

#if !EMBEDDED
#if LINUX && !defined(PORTAUDIO)
#define ALSA      1
#define PORTAUDIO 0
#else
#define ALSA      0
#define PORTAUDIO 1
#endif
#endif

#if !defined(LOOPBACK)
#if SUN
#define EVENTFD   0
#define WINEVENT  0
#define SELFPIPE  1
#elif LINUX && !defined(SELFPIPE)
#define EVENTFD   1
#define SELFPIPE  0
#define WINEVENT  0
#endif
#if (LINUX && !EVENTFD) || OSX || FREEBSD
#define EVENTFD   0
#define SELFPIPE  1
#define WINEVENT  0
#endif
#if WIN
#define EVENTFD   0
#define SELFPIPE  0
#define WINEVENT  1
#endif
#else
#define EVENTFD   0
#define SELFPIPE  0
#define WINEVENT  0
#undef LOOPBACK
#define LOOPBACK  1
#endif

#if defined(RESAMPLE) || defined(RESAMPLE_MP)
#undef  RESAMPLE
#define RESAMPLE  1 // resampling
#define PROCESS   1 // any sample processing (only resampling at present)
#elif defined(RESAMPLE16)
#undef RESAMPLE16
#define RESAMPLE16	1
#define PROCESS		1
#else
#define RESAMPLE  0
#define PROCESS   0
#endif
#if defined(RESAMPLE_MP)
#undef RESAMPLE_MP
#define RESAMPLE_MP 1
#else
#define RESAMPLE_MP 0
#endif

#if defined(FFMPEG)
#undef FFMPEG
#define FFMPEG    1
#else
#define FFMPEG    0
#endif

#if (LINUX || OSX) && defined(VISEXPORT)
#undef VISEXPORT
#define VISEXPORT 1 // visulizer export support uses linux shared memory
#else
#define VISEXPORT 0
#endif

#if LINUX && defined(IR)
#undef IR
#define IR 1
#else
#define IR 0
#endif

#if defined(DSD)
#undef DSD
#define DSD       1
#define IF_DSD(x) { x }
#else
#undef DSD
#define DSD       0
#define IF_DSD(x)
#endif

#if defined(LINKALL)
#undef LINKALL
#define LINKALL   1 // link all libraries at build time - requires all to be available at run time
#else
#define LINKALL   0
#endif

#if defined (USE_SSL)
#undef USE_SSL
#define USE_SSL 1
#else
#define USE_SSL 0
#endif

#if defined (NO_SSLSYM)
#undef NO_SSLSYM
#define NO_SSLSYM 1
#else
#define NO_SSLSYM 0
#endif

#if !LINKALL

// dynamically loaded libraries at run time

#if LINUX
#define LIBFLAC "libFLAC.so.8"
#define LIBMAD  "libmad.so.0"
#define LIBMPG "libmpg123.so.0"
#define LIBVORBIS "libvorbisfile.so.3"
#define LIBOPUS "libopusfile.so.0"
#define LIBTREMOR "libvorbisidec.so.1"
#define LIBFAAD "libfaad.so.2"
#define LIBAVUTIL   "libavutil.so.%d"
#define LIBAVCODEC  "libavcodec.so.%d"
#define LIBAVFORMAT "libavformat.so.%d"
#define LIBSOXR "libsoxr.so.0"
#define LIBLIRC "liblirc_client.so.0"
#endif

#if OSX
#define LIBFLAC "libFLAC.8.dylib"
#define LIBMAD  "libmad.0.dylib"
#define LIBMPG "libmpg123.0.dylib"
#define LIBVORBIS "libvorbisfile.3.dylib"
#define LIBTREMOR "libvorbisidec.1.dylib"
#define LIBOPUS "libopusfile.0.dylib"
#define LIBFAAD "libfaad.2.dylib"
#define LIBAVUTIL   "libavutil.%d.dylib"
#define LIBAVCODEC  "libavcodec.%d.dylib"
#define LIBAVFORMAT "libavformat.%d.dylib"
#define LIBSOXR "libsoxr.0.dylib"
#endif

#if WIN
#define LIBFLAC "libFLAC.dll"
#define LIBMAD  "libmad-0.dll"
#define LIBMPG "libmpg123-0.dll"
#define LIBVORBIS "libvorbisfile.dll"
#define LIBOPUS "libopusfile-0.dll"
#define LIBTREMOR "libvorbisidec.dll"
#define LIBFAAD "libfaad2.dll"
#define LIBAVUTIL   "avutil-%d.dll"
#define LIBAVCODEC  "avcodec-%d.dll"
#define LIBAVFORMAT "avformat-%d.dll"
#define LIBSOXR "libsoxr.dll"
#endif

#if FREEBSD
#define LIBFLAC "libFLAC.so.8"
#define LIBMAD  "libmad.so.0"
#define LIBMPG "libmpg123.so.0"
#define LIBVORBIS "libvorbisfile.so.3"
#define LIBTREMOR "libvorbisidec.so.1"
#define LIBOPUS "libopusfile.so.1"
#define LIBFAAD "libfaad.so.2"
#define LIBAVUTIL   "libavutil.so.%d"
#define LIBAVCODEC  "libavcodec.so.%d"
#define LIBAVFORMAT "libavformat.so.%d"
#define LIBSOXR "libsoxr.so.0"
#endif

#endif // !LINKALL

// config options
#if !EMBEDDED
#define STREAMBUF_SIZE (2 * 1024 * 1024)
#define OUTPUTBUF_SIZE (44100 * 8 * 10)
#else
#define STREAMBUF_SIZE (480 * 1024)
#define OUTPUTBUF_SIZE (1450 * 1024)
#endif
#define OUTPUTBUF_SIZE_CROSSFADE (OUTPUTBUF_SIZE * 12 / 10)

#define MAX_HEADER 4096 // do not reduce as icy-meta max is 4080

#if ALSA
#define ALSA_BUFFER_TIME  40
#define ALSA_PERIOD_COUNT 4
#define OUTPUT_RT_PRIORITY 45
#endif

#define SL_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)

#if SUN || OSXPPC
#undef SL_LITTLE_ENDIAN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>

#if LINUX || OSX || FREEBSD || EMBEDDED
#include <unistd.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/poll.h>
#if !LINKALL
#include <dlfcn.h>
#endif
#include <pthread.h>
#include <signal.h>
#if SUN
#include <sys/types.h>
#endif /* SUN */
#if !OSX
#define thread_t pthread_t;
#endif
#define closesocket(s) close(s)
#define last_error() errno
#define ERROR_WOULDBLOCK EWOULDBLOCK
#if !EMBEDDED
#define STREAM_THREAD_STACK_SIZE  64 * 1024
#define DECODE_THREAD_STACK_SIZE 128 * 1024
#define OUTPUT_THREAD_STACK_SIZE  64 * 1024
#define IR_THREAD_STACK_SIZE      64 * 1024
#ifdef SUN
typedef uint8_t  u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;
#else
typedef u_int8_t  u8_t;
typedef u_int16_t u16_t;
typedef u_int32_t u32_t;
typedef u_int64_t u64_t;
#endif /* SUN */
typedef int16_t   s16_t;
typedef int32_t   s32_t;
typedef int64_t   s64_t;
#endif

#define mutex_type pthread_mutex_t
#define mutex_create(m) pthread_mutex_init(&m, NULL)
#if !EMBEDDED
#define mutex_create_p(m) pthread_mutexattr_t attr; pthread_mutexattr_init(&attr); pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT); pthread_mutex_init(&m, &attr); pthread_mutexattr_destroy(&attr)
#endif
#define mutex_lock(m) pthread_mutex_lock(&m)
#define mutex_unlock(m) pthread_mutex_unlock(&m)
#define mutex_destroy(m) pthread_mutex_destroy(&m)
#define thread_type pthread_t
#if !EMBEDDED
#define pthread_create_name(t,a,f,p,n) pthread_create(t,a,f,p)
#endif
#endif

#if WIN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>

#define STREAM_THREAD_STACK_SIZE (1024 * 64)
#define DECODE_THREAD_STACK_SIZE (1024 * 128)
#define OUTPUT_THREAD_STACK_SIZE (1024 * 64)

typedef unsigned __int8  u8_t;
typedef unsigned __int16 u16_t;
typedef unsigned __int32 u32_t;
typedef unsigned __int64 u64_t;
typedef __int16 s16_t;
typedef __int32 s32_t;
typedef __int64 s64_t;

typedef BOOL bool;
#define true TRUE
#define false FALSE

#define inline __inline

#define mutex_type HANDLE
#define mutex_create(m) m = CreateMutex(NULL, FALSE, NULL)
#define mutex_create_p mutex_create
#define mutex_lock(m) WaitForSingleObject(m, INFINITE)
#define mutex_unlock(m) ReleaseMutex(m)
#define mutex_destroy(m) CloseHandle(m)
#define thread_type HANDLE

#define usleep(x) Sleep(x/1000)
#define sleep(x) Sleep(x*1000)
#define last_error() WSAGetLastError()
#define ERROR_WOULDBLOCK WSAEWOULDBLOCK
#define open _open
#define read _read
#define snprintf _snprintf

#define in_addr_t u32_t
#define socklen_t int
#define ssize_t int

#define RTLD_NOW 0

#endif

// logging
typedef enum { lERROR = 0, lWARN, lINFO, lDEBUG, lSDEBUG } log_level;

const char *logtime(void);
void logprint(const char *fmt, ...);

#define LOG_WARN(fmt, ...)  if (loglevel >= lWARN)  logprint("%s %s:%d " fmt "\n", logtime(), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  if (loglevel >= lINFO)  logprint("%s %s:%d " fmt "\n", logtime(), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) if (loglevel >= lDEBUG) logprint("%s %s:%d " fmt "\n", logtime(), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_SDEBUG(fmt, ...) if (loglevel >= lSDEBUG) logprint("%s %s:%d " fmt "\n", logtime(), __FUNCTION__, __LINE__, ##__VA_ARGS__)
	
typedef uint32_t frames_t;
typedef int sockfd;
	
#if EMBEDDED
#include "embedded.h"
#endif

#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) logprint("%s %s:%d " fmt "\n", logtime(), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#if !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

#if EVENTFD
#include <sys/eventfd.h>
#define event_event int
#define event_handle struct pollfd
#define wake_create(e) e = eventfd(0, 0)
#define wake_signal(e) eventfd_write(e, 1)
#define wake_clear(e) eventfd_t val; eventfd_read(e, &val)
#define wake_close(e) close(e)
#endif

#if SELFPIPE
#define event_handle struct pollfd
#define event_event struct wake
#define wake_create(e) pipe(e.fds); set_nonblock(e.fds[0]); set_nonblock(e.fds[1])
#define wake_signal(e) write(e.fds[1], ".", 1)
#define wake_clear(e) char c[10]; read(e, &c, 10)
#define wake_close(e) close(e.fds[0]); close(e.fds[1])
struct wake { 
	int fds[2];
};
#endif

#if LOOPBACK
#define event_handle struct pollfd
#define event_event struct wake
#define wake_create(e) _wake_create(&e)
#define wake_signal(e) send(e.fds[1], ".", 1, 0)
#define wake_clear(e) char c; recv(e, &c, 1, 0)
#define wake_close(e) closesocket(e.mfds); closesocket(e.fds[0]); closesocket(e.fds[1])
struct wake {
	int mfds;
	int fds[2];
};
void _wake_create(event_event*);
#endif

#if WINEVENT
#define event_event HANDLE
#define event_handle HANDLE
#define wake_create(e) e = CreateEvent(NULL, FALSE, FALSE, NULL)
#define wake_signal(e) SetEvent(e)
#define wake_close(e) CloseHandle(e)
#endif

#ifndef EXT_BSS
#define EXT_BSS
#endif

// printf/scanf formats for u64_t
#if (LINUX && __WORDSIZE == 64) || (FREEBSD && __LP64__)
#define FMT_u64 "%lu"
#define FMT_x64 "%lx"
#elif __GLIBC_HAVE_LONG_LONG || defined __GNUC__ || WIN || SUN
#define FMT_u64 "%llu"
#define FMT_x64 "%llx"
#else
#error can not support u64_t
#endif

#define MAX_SILENCE_FRAMES 2048

#define FIXED_ONE 	0x10000

#ifndef BYTES_PER_FRAME
#define BYTES_PER_FRAME 8
#endif

#if BYTES_PER_FRAME == 8
#define ISAMPLE_T 		s32_t
#else
#define ISAMPLE_T		s16_t
#endif

#define min(a,b) (((a) < (b)) ? (a) : (b))

// utils.c (non logging)
typedef enum { EVENT_TIMEOUT = 0, EVENT_READ, EVENT_WAKE } event_type;
#if WIN && USE_SSL
char* strcasestr(const char *haystack, const char *needle);
#endif

char *next_param(char *src, char c);
u32_t gettime_ms(void);
void get_mac(u8_t *mac);
void set_nonblock(sockfd s);
int connect_timeout(sockfd sock, const struct sockaddr *addr, socklen_t addrlen, int timeout);
void server_addr(char *server, in_addr_t *ip_ptr, unsigned *port_ptr);
void set_readwake_handles(event_handle handles[], sockfd s, event_event e);
event_type wait_readwake(event_handle handles[], int timeout);
void packN(u32_t *dest, u32_t val);
void packn(u16_t *dest, u16_t val);
u32_t unpackN(u32_t *src);
u16_t unpackn(u16_t *src);
#if OSX
void set_nosigpipe(sockfd s);
#else
#define set_nosigpipe(s)
#endif
#if SUN
void init_daemonize(void);
int daemon(int,int);
#endif
#if WIN
void winsock_init(void);
void winsock_close(void);
void *dlopen(const char *filename, int flag);
void *dlsym(void *handle, const char *symbol);
char *dlerror(void);
int poll(struct pollfd *fds, unsigned long numfds, int timeout);
#endif
#if LINUX || FREEBSD
void touch_memory(u8_t *buf, size_t size);
#endif

// buffer.c
struct buffer {
	u8_t *buf;
	u8_t *readp;
	u8_t *writep;
	u8_t *wrap;
	size_t size;
	size_t base_size;
	size_t true_size;
	mutex_type mutex;
};

// _* called with mutex locked
unsigned _buf_used(struct buffer *buf);
unsigned _buf_space(struct buffer *buf);
unsigned _buf_cont_read(struct buffer *buf);
unsigned _buf_cont_write(struct buffer *buf);
void _buf_inc_readp(struct buffer *buf, unsigned by);
void _buf_inc_writep(struct buffer *buf, unsigned by);
void buf_flush(struct buffer *buf);
void _buf_flush(struct buffer *buf);
void _buf_unwrap(struct buffer *buf, size_t cont);
void buf_adjust(struct buffer *buf, size_t mod);
void _buf_resize(struct buffer *buf, size_t size);
size_t _buf_limit(struct buffer *buf, size_t limit);
void buf_init(struct buffer *buf, size_t size);
void buf_destroy(struct buffer *buf);

// slimproto.c
void slimproto(log_level level, char *server, u8_t mac[6], const char *name, const char *namefile, const char *modelname, int maxSampleRate);
void slimproto_stop(void);
void wake_controller(void);
void slimproto_send_packet(u8_t *packet, size_t len);
#define send_packet(p, s) slimproto_send_packet(p,s)

// stream.c
typedef enum { STOPPED = 0, DISCONNECT, STREAMING_WAIT,
			   STREAMING_BUFFERING, STREAMING_FILE, STREAMING_HTTP, SEND_HEADERS, RECV_HEADERS } stream_state;
typedef enum { DISCONNECT_OK = 0, LOCAL_DISCONNECT = 1, REMOTE_DISCONNECT = 2, UNREACHABLE = 3, TIMEOUT = 4 } disconnect_code;

struct streamstate {
	stream_state state;
	disconnect_code disconnect;
	char *header;
	size_t header_len;
	bool sent_headers;
	bool cont_wait;
	u64_t bytes;
	unsigned threshold;
	u32_t meta_interval;
	u32_t meta_next;
	u32_t meta_left;
	bool  meta_send;
};

void stream_init(log_level level, unsigned stream_buf_size);
void stream_close(void);
void stream_file(const char *header, size_t header_len, unsigned threshold);
void stream_sock(u32_t ip, u16_t port, const char *header, size_t header_len, unsigned threshold, bool cont_wait);
bool stream_disconnect(void);

// decode.c
typedef enum { DECODE_STOPPED = 0, DECODE_READY, DECODE_RUNNING, DECODE_COMPLETE, DECODE_ERROR } decode_state;

struct decodestate {
	decode_state state;
	bool new_stream;
	mutex_type mutex;
#if PROCESS
	bool direct;
	bool process;
#endif
};

#if PROCESS
struct processstate {
	u8_t *inbuf, *outbuf;
	unsigned max_in_frames, max_out_frames;
	unsigned in_frames, out_frames;
	unsigned in_sample_rate, out_sample_rate;
	unsigned long total_in, total_out;
};
#endif

struct codec {
	char id;
	char *types;
	unsigned min_read_bytes;
	unsigned min_space;
	void (*open)(u8_t sample_size, u8_t sample_rate, u8_t channels, u8_t endianness);
	void (*close)(void);
	decode_state (*decode)(void);
};

void decode_init(log_level level, const char *include_codecs, const char *exclude_codecs);
void decode_close(void);
void decode_flush(bool close);
unsigned decode_newstream(unsigned sample_rate, unsigned supported_rates[]);
void codec_open(u8_t format, u8_t sample_size, u8_t sample_rate, u8_t channels, u8_t endianness);

#if PROCESS
// process.c
void process_samples(void);
void process_drain(void);
void process_flush(void);
unsigned process_newstream(bool *direct, unsigned raw_sample_rate, unsigned supported_rates[]);
void process_init(char *opt);
#endif

#if RESAMPLE || RESAMPLE16
// resample.c
void resample_samples(struct processstate *process);
bool resample_drain(struct processstate *process);
bool resample_newstream(struct processstate *process, unsigned raw_sample_rate, unsigned supported_rates[]);
void resample_flush(void);
bool resample_init(char *opt);
#endif

// output.c output_alsa.c output_pa.c output_pack.c
typedef enum { OUTPUT_OFF = -1, OUTPUT_STOPPED = 0, OUTPUT_BUFFER, OUTPUT_RUNNING, 
			   OUTPUT_PAUSE_FRAMES, OUTPUT_SKIP_FRAMES, OUTPUT_START_AT } output_state;

#if DSD
typedef enum { PCM, DOP, DSD_U8, DSD_U16_LE, DSD_U32_LE, DSD_U16_BE, DSD_U32_BE, DOP_S24_LE, DOP_S24_3LE } dsd_format;
typedef enum { S32_LE, S24_LE, S24_3LE, S16_LE, U8, U16_LE, U16_BE, U32_LE, U32_BE } output_format;
#else
typedef enum { S32_LE, S24_LE, S24_3LE, S16_LE, S24_BE, S24_3BE, S16_BE, S8_BE } output_format;
#endif

typedef enum { FADE_INACTIVE = 0, FADE_DUE, FADE_ACTIVE } fade_state;
typedef enum { FADE_UP = 1, FADE_DOWN, FADE_CROSS } fade_dir;
typedef enum { FADE_NONE = 0, FADE_CROSSFADE, FADE_IN, FADE_OUT, FADE_INOUT } fade_mode;

#define MONO_RIGHT	0x02
#define MONO_LEFT	0x01
#define MAX_SUPPORTED_SAMPLERATES 18
#define TEST_RATES = { 768000, 705600, 384000, 352800, 192000, 176400, 96000, 88200, 48000, 44100, 32000, 24000, 22500, 16000, 12000, 11025, 8000, 0 }

struct outputstate {
	output_state state;
	output_format format;
	u8_t  channels;            
	const char *device;
	int external;
#if ALSA
	unsigned buffer;
	unsigned period;
#endif
	bool  track_started; 
#if PORTAUDIO
	bool  pa_reopen;
	unsigned latency;
	int pa_hostapi_option;
#endif
	int (* write_cb)(frames_t out_frames, bool silence, s32_t gainL, s32_t gainR, u8_t flags, s32_t cross_gain_in, s32_t cross_gain_out, ISAMPLE_T **cross_ptr);
	unsigned start_frames;
	unsigned frames_played;
	unsigned frames_played_dmp;// frames played at the point delay is measured
	unsigned current_sample_rate;
	unsigned supported_rates[MAX_SUPPORTED_SAMPLERATES]; // ordered largest first so [0] is max_rate
	unsigned default_sample_rate;
	bool error_opening;
	unsigned device_frames;
	unsigned frames_in_process;
	u32_t updated;
	u32_t track_start_time;
	u32_t current_replay_gain;
	union {
		u32_t pause_frames;
		u32_t skip_frames;
		u32_t start_at;
	};
	unsigned next_sample_rate; // set in decode thread
	u8_t  *track_start;        // set in decode thread
	u32_t gainL;               // set by slimproto
	u32_t gainR;               // set by slimproto
	bool  invert;              // set by slimproto
	u32_t next_replay_gain;    // set by slimproto
	unsigned threshold;        // set by slimproto
	fade_state fade;
	u8_t *fade_start;
	u8_t *fade_end;
	fade_dir fade_dir;
	fade_mode fade_mode;       // set by slimproto
	unsigned fade_secs;        // set by slimproto
	unsigned rate_delay;
	bool delay_active;
	u32_t stop_time;
	u32_t idle_to;
#if DSD
	dsd_format next_fmt;       // set in decode thread
	dsd_format outfmt;
	dsd_format dsdfmt;	       // set in dsd_init - output for DSD: DOP, DSD_U8, ...
	unsigned dsd_delay;		   // set in dsd_init - delay in ms switching to/from dop
#endif
};

void output_init_common(log_level level, const char *device, unsigned output_buf_size, unsigned rates[], unsigned idle);
void output_close_common(void);
void output_flush(void);
// _* called with mutex locked
frames_t _output_frames(frames_t avail);
void _checkfade(bool);

// output_alsa.c
#if ALSA
void list_devices(void);
void list_mixers(const char *output_device);
void set_volume(unsigned left, unsigned right);
bool test_open(const char *device, unsigned rates[], bool userdef_rates);
void output_init_alsa(log_level level, const char *device, unsigned output_buf_size, char *params, unsigned rates[], unsigned rate_delay, unsigned rt_priority, unsigned idle, char *mixer_device, char *volume_mixer, bool mixer_unmute, bool mixer_linear);
void output_close_alsa(void);
#endif

// output_pa.c
#if PORTAUDIO
void list_devices(void);
void set_volume(unsigned left, unsigned right);
bool test_open(const char *device, unsigned rates[], bool userdef_rates);
void output_init_pa(log_level level, const char *device, unsigned output_buf_size, char *params, unsigned rates[], unsigned rate_delay, unsigned idle);
void output_close_pa(void);
void _pa_open(void);
#endif

// output_embedded.c
#if EMBEDDED
void set_volume(unsigned left, unsigned right);
bool test_open(const char *device, unsigned rates[], bool userdef_rates);
void output_init_embedded();
void output_close_embedded(void);
#else 
// output_stdout.c
void output_init_stdout(log_level level, unsigned output_buf_size, char *params, unsigned rates[], unsigned rate_delay);
void output_close_stdout(void);
#endif

// output_pack.c
void _scale_and_pack_frames(void *outputptr, s32_t *inputptr, frames_t cnt, s32_t gainL, s32_t gainR, u8_t flags, output_format format);
void _apply_cross(struct buffer *outputbuf, frames_t out_frames, s32_t cross_gain_in, s32_t cross_gain_out, ISAMPLE_T **cross_ptr);
void _apply_gain(struct buffer *outputbuf, frames_t count, s32_t gainL, s32_t gainR, u8_t flags);
s32_t gain(s32_t gain, s32_t sample);
s32_t to_gain(float f);

// output_vis.c
#if VISEXPORT
void _vis_export(struct buffer *outputbuf, struct outputstate *output, frames_t out_frames, bool silence);
void output_vis_init(log_level level, u8_t *mac);
void vis_stop(void);
#else
#define _vis_export(...)
#define vis_stop()
#endif

// dop.c
#if DSD
bool is_stream_dop(u8_t *lptr, u8_t *rptr, int step, frames_t frames);
void update_dop(u32_t *ptr, frames_t frames, bool invert);
void dsd_silence_frames(u32_t *ptr, frames_t frames);
void dsd_invert(u32_t *ptr, frames_t frames);
void dsd_init(dsd_format format, unsigned delay);
#endif

// codecs
#define MAX_CODECS 9

struct codec *register_flac(void);
struct codec *register_pcm(void);
struct codec *register_mad(void);
struct codec *register_mpg(void);
struct codec *register_vorbis(void);
struct codec *register_faad(void);
struct codec *register_helixaac(void);
struct codec *register_dsd(void);
struct codec *register_alac(void);
struct codec *register_ff(const char *codec);
struct codec *register_opus(void);

//gpio.c
#if GPIO
void relay( int state);
void relay_script(int state);
int gpio_pin;
bool gpio_active_low;
bool gpio_active;
char *power_script;
//  my amp state
int ampstate;
#endif

// ir.c
#if IR
struct irstate {
	mutex_type mutex;
	u32_t code;
	u32_t ts;
};

void ir_init(log_level level, char *lircrc);
void ir_close(void);
#endif

// sslsym.c
#if USE_SSL && !LINKALL && !NO_SSLSYM
bool load_ssl_symbols(void);
void free_ssl_symbols(void);
bool ssl_loaded;
#endif

