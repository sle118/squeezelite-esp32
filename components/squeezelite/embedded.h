#ifndef EMBEDDED_H
#define EMBEDDED_H
#include <ctype.h>
#include <inttypes.h>

/* 	must provide 
		- mutex_create_p
		- pthread_create_name
		- register_xxx (see below)
		- stack size
		- s16_t, s32_t, s64_t and u64_t
		- PLAYER_ID / custom_player_id
	can overload (use #define)
		- exit
		- gettime_ms
		- BASE_CAP
		- EXT_BSS 		
	recommended to add platform specific include(s) here
*/	

typedef int16_t   s16_t;
typedef int32_t   s32_t;
typedef int64_t   s64_t;
typedef unsigned long long u64_t;

#ifndef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN	256
#endif
#ifndef _CONST
#define _CONST
#endif

#define STREAM_THREAD_STACK_SIZE  4 * 1024
#define DECODE_THREAD_STACK_SIZE 14 * 1024
#define OUTPUT_THREAD_STACK_SIZE  4 * 1024
#define IR_THREAD_STACK_SIZE      4 * 1024

// number of times the 5s search for a server will happen before slimproto exits (0 = no limit)
#define MAX_SERVER_RETRIES	5

// or can be as simple as #define PLAYER_ID 100
#define PLAYER_ID custom_player_id
extern u8_t custom_player_id;

#if BYTES_PER_FRAME == 8
#define BASE_CAP "Model=squeezeesp32,AccuratePlayPoints=1,HasDigitalOut=1,HasPolarityInversion=1,Balance=1,Depth=32,Firmware=" VERSION 
#else
#define BASE_CAP "Model=squeezeesp32,AccuratePlayPoints=1,HasDigitalOut=1,HasPolarityInversion=1,Balance=1,Depth=16,Firmware=" VERSION 
#endif

// to force some special buffer attribute
#define EXT_BSS __attribute__((section(".ext_ram.bss"))) 

// otherwise just leave it empty
void em_logprint(const char *fmt, ...);
#define LOG_ERROR(fmt, ...) em_logprint("%s %s:%d " fmt "\n", logtime(), __FUNCTION__, __LINE__, ##__VA_ARGS__); 

// all exit() calls are made from main thread (or a function called in main thread)
void embedded_exit(int code);
#define exit(code) do { embedded_exit(code); } while (0)
#define gettime_ms _gettime_ms_
#define mutex_create_p(m) mutex_create(m)

uint32_t 	_gettime_ms_(void);

int			pthread_create_name(pthread_t *thread, _CONST pthread_attr_t  *attr, 
				   void *(*start_routine)( void * ), void *arg, char *name);

// must provide	of #define as empty macros		
int 		embedded_init(void);
void 		register_external(void);
void 		deregister_external(void);
void 		decode_restore(int external);
void        powering(bool on);
// used when other client wants to use slimproto socket to send messages
extern mutex_type slimp_mutex;
#define LOCK_P   mutex_lock(slimp_mutex)
#define UNLOCK_P mutex_unlock(slimp_mutex)

// bitmap of plugs status
#define PLUG_LINE_IN 	0x01
#define PLUG_LINE_OUT	0x02
#define PLUG_HEADPHONE	0x04
u16_t	get_RSSI(void);			// must provide or define as 0xffff
u16_t	get_plugged(void);		// must provide or define as 0x0
u16_t	get_battery(void);		// must provide 12 bits data or define as 0x0 (exact meaning is device-dependant)

// set name 
void set_name(char *name);		// can be defined as an empty macro

// to be defined to nothing if you don't want to support these
extern struct visu_export_s {
	pthread_mutex_t mutex;
	u32_t level, size, rate, gain;
	void *buffer;
	bool running;
} visu_export;
void 		output_visu_export(void *frames, frames_t out_frames, u32_t rate, bool silence, u32_t gain);
void 		output_visu_init(log_level level);
void 		output_visu_close(void);

// optional, please chain if used 
extern bool		(*slimp_handler)(u8_t *data, int len);
extern void 		(*slimp_loop)(void);
extern void 		(*server_notify)(in_addr_t ip, u16_t hport, u16_t cport);
	   
#endif // EMBEDDED_H
