/* 
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
#include "squeezelite.h"
#include "equalizer.h"

extern struct outputstate output;
extern struct buffer *outputbuf;

static bool (*slimp_handler_chain)(u8_t *data, int len);

#define FRAME_BLOCK MAX_SILENCE_FRAMES

#define LOCK   mutex_lock(outputbuf->mutex)
#define UNLOCK mutex_unlock(outputbuf->mutex)

// output_bt.c
extern void output_init_bt(log_level level, char *device, unsigned output_buf_size, char *params, 
						  unsigned rates[], unsigned rate_delay, unsigned idle);
extern void output_close_bt(void); 

// output_i2s.c
extern void output_init_i2s(log_level level, char *device, unsigned output_buf_size, char *params, 
						  unsigned rates[], unsigned rate_delay, unsigned idle);					
extern bool output_volume_i2s(unsigned left, unsigned right); 
extern void output_close_i2s(void); 

// controls.c
extern void cli_controls_init(void);

static log_level loglevel;

static bool (*volume_cb)(unsigned left, unsigned right);
static void (*close_cb)(void);

#pragma pack(push, 1)
struct eqlz_packet {
	char  opcode[4];
};
#pragma pack(pop)

static bool handler(u8_t *data, int len){
	bool res = true;
	
	if (!strncmp((char*) data, "eqlz", 4)) {
		s8_t *gain = (s8_t*) (data + sizeof(struct eqlz_packet));
		LOG_INFO("got equalizer %d", len);
		// update will be done at next opportunity
		equalizer_update(gain);
	} else {
		res = false;
	}
	
	// chain protocol handlers (bitwise or is fine)
	if (*slimp_handler_chain) res |= (*slimp_handler_chain)(data, len);
	
	return res;
}

void output_init_embedded(log_level level, char *device, unsigned output_buf_size, char *params, 
						  unsigned rates[], unsigned rate_delay, unsigned idle) {
	loglevel = level;						
	LOG_INFO("init device: %s", device);
	
	// chain handlers
	slimp_handler_chain = slimp_handler;
	slimp_handler = handler;
	
	memset(&output, 0, sizeof(output));
	output_init_common(level, device, output_buf_size, rates, idle);
	output.start_frames = FRAME_BLOCK;
	output.rate_delay = rate_delay;
	
	if (strcasestr(device, "BT ")) {
		LOG_INFO("init Bluetooth");
		close_cb = &output_close_bt;
		output_init_bt(level, device, output_buf_size, params, rates, rate_delay, idle);
	} else {
		LOG_INFO("init I2S/SPDIF");
		close_cb = &output_close_i2s;
		volume_cb = &output_volume_i2s;
		output_init_i2s(level, device, output_buf_size, params, rates, rate_delay, idle);
	}	
	
	output_visu_init(level);
	
	LOG_INFO("init completed.");
}	

void output_close_embedded(void) {
	LOG_INFO("close output");
	if (close_cb) (*close_cb)();		
	output_close_common();
	output_visu_close();
}

void set_volume(unsigned left, unsigned right) { 
	LOG_DEBUG("setting internal gain left: %u right: %u", left, right);
	if (!volume_cb || !(*volume_cb)(left, right)) {
		LOCK;
		output.gainL = left;
		output.gainR = right;
		UNLOCK;
	} else {
		LOCK;
		output.gainL = FIXED_ONE>>1;
		output.gainR = FIXED_ONE>>1;
		UNLOCK;
	}
}

bool test_open(const char *device, unsigned rates[], bool userdef_rates) {
	memset(rates, 0, MAX_SUPPORTED_SAMPLERATES * sizeof(unsigned));
	if (!strcasecmp(device, "I2S")) {
		unsigned _rates[] = { 192000, 176400, 96000, 88200, 48000, 
							  44100, 32000, 24000, 22050, 16000, 
							  12000, 11025, 8000, 0 };	
		memcpy(rates, _rates, sizeof(_rates));
	} else if (!strcasecmp(device, "SPDIF")) {
		unsigned _rates[] = { 48000, 44100, 0 };	
		memcpy(rates, _rates, sizeof(_rates));
	} else {
		rates[0] = 44100;	
	}	
	return true;
}

char* output_state_str(void){
	output_state state;
	LOCK;
	state = output.state;
	UNLOCK;
	switch (state) {
	case OUTPUT_OFF: 			return STR(OUTPUT_OFF);
	case OUTPUT_STOPPED:		return STR(OUTPUT_STOPPED);
	case OUTPUT_BUFFER:			return STR(OUTPUT_BUFFER);
	case OUTPUT_RUNNING:		return STR(OUTPUT_RUNNING);
	case OUTPUT_PAUSE_FRAMES: 	return STR(OUTPUT_PAUSE_FRAMES);
	case OUTPUT_SKIP_FRAMES:	return STR(OUTPUT_SKIP_FRAMES);
	case OUTPUT_START_AT:		return STR(OUTPUT_START_AT);
	default:					return "OUTPUT_UNKNOWN_STATE";
	}
}

bool output_stopped(void) {
	output_state state;
	LOCK;
	state = output.state;
	UNLOCK;
	return state <= OUTPUT_STOPPED;
}	
	



