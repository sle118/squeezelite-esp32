/* 
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include <ctype.h>
#include <math.h>
#include "esp_dsp.h"
#include "squeezelite.h"
#include "slimproto.h"
#include "display.h"
#include "gds.h"
#include "gds_text.h"
#include "gds_draw.h"
#include "gds_image.h"

#pragma pack(push, 1)

struct grfb_packet {
	char  opcode[4];
	s16_t  brightness;
};

struct grfe_packet {
	char  opcode[4];
	u16_t offset;
	u8_t  transition;
	u8_t  param;
};

struct grfs_packet {
	char  opcode[4];
	u8_t  screen;	
	u8_t  direction;	// 1=left, 2=right
	u32_t pause;		// in ms	
	u32_t speed;		// in ms
	u16_t by;			// # of pixel of scroll step
	u16_t mode;			// 0=continuous, 1=once and stop, 2=once and end
	u16_t width;		// total width of animation
	u16_t offset;		// offset if multiple packets are sent
};

struct grfg_packet {
	char  opcode[4];
	u16_t  screen;	
	u16_t  width;		// # of pixels of scrollable
};

struct grfa_packet {
	char  opcode[4];
	u32_t length;
	u16_t x;
	u16_t y;	
	u32_t offset;
};

struct visu_packet {
	char  opcode[4];
	u8_t which;
	u8_t count;
	union {
		struct {
			u32_t width;
			union {
				struct {
				u32_t bars;
				u32_t spectrum_scale;
				};
				u32_t style;	
			};	
		} full;	
		struct {	
			u32_t width;
			u32_t height;
			s32_t col;
			s32_t row;	
			u32_t border;
			u32_t bars;
			u32_t spectrum_scale;
		};	
		struct {
			u32_t mono;
			u32_t bandwidth;
			u32_t preemph;	
			struct {
				u32_t pos;
				u32_t width;
				u32_t orient;
				u32_t bar_width;
				u32_t bar_space;
				u32_t clipping;
				u32_t bar_intens;
				u32_t bar_cap_intens;
			} channels[2];
		};
		struct {
			u32_t mono;
			u32_t style;
			struct {
				u32_t pos;
				u32_t width;
			} channels[2];
		} classical_vu;	
	};	
};

struct ANIC_header {
	char  opcode[4];
	u32_t length;
	u8_t mode;
};

#pragma pack(pop)

static struct {
	TaskHandle_t task;
	SemaphoreHandle_t mutex;
	int width, height;
	bool dirty;
	bool owned;
} displayer = { .dirty = true, .owned = true };	

static uint32_t *grayMap;

#define LONG_WAKE 		(10*1000)
#define SB_HEIGHT		32

// lenght are number of frames, i.e. 2 channels of 16 bits
#define	FFT_LEN_BIT	7		
#define	FFT_LEN		(1 << FFT_LEN_BIT)
#define RMS_LEN_BIT	6
#define RMS_LEN		(1 << RMS_LEN_BIT)

#define VU_WIDTH	160
#define VU_HEIGHT	SB_HEIGHT
#define VU_COUNT	48

#define DISPLAY_BW	20000

static struct scroller_s {
	// copy of grfs content
	u8_t  screen;	
	u32_t pause, speed;
	int wake;	
	u16_t mode;	
	s16_t by;
	// scroller management & sharing between grfg and scrolling task
	bool active, first, overflow;
	int scrolled;
	struct {
		u8_t *frame;
		u32_t width;
		u32_t max, size;
	} scroll;
	struct {
		u8_t *frame;
		u32_t width;
	} back;
	u8_t *frame;
	u32_t width;
} scroller;

static struct {
	u8_t *data;
	u32_t size;
	u16_t x, y;
	bool enable;
} artwork;

#define MAX_BARS	32
#define VISU_ESP32	0x10
static EXT_RAM_ATTR struct {
	int bar_gap, bar_width, bar_border;
	bool rotate;
	struct {
		int current, max;
		int limit;
	} bars[MAX_BARS];
	float spectrum_scale;
	int n, col, row, height, width, border, style, max;
	enum { VISU_BLANK, VISU_VUMETER, VISU_SPECTRUM, VISU_WAVEFORM } mode;
	int speed, wake;	
	float fft[FFT_LEN*2], samples[FFT_LEN*2], hanning[FFT_LEN];
	struct {
		u8_t *frame;
		int width;
		bool active;
	} back;		
} visu;

extern const uint8_t vu_bitmap[]   asm("_binary_vu_data_start");

#define ANIM_NONE		  0x00
#define ANIM_TRANSITION   0x01 // A transition animation has finished
#define ANIM_SCROLL_ONCE  0x02 
#define ANIM_SCREEN_1     0x04 
#define ANIM_SCREEN_2     0x08 

#define SCROLL_STACK_SIZE	(3*1024)
#define LINELEN				40

static log_level loglevel = lINFO;

static bool (*slimp_handler_chain)(u8_t *data, int len);
static void (*notify_chain)(in_addr_t ip, u16_t hport, u16_t cport);
static bool (*display_bus_chain)(void *from, enum display_bus_cmd_e cmd);

#define max(a,b) (((a) > (b)) ? (a) : (b))

static void server(in_addr_t ip, u16_t hport, u16_t cport);
static void sendSETD(u16_t width, u16_t height);
static void sendANIC(u8_t code);
static bool handler(u8_t *data, int len);
static bool display_bus_handler(void *from, enum display_bus_cmd_e cmd);
static void vfdc_handler( u8_t *_data, int bytes_read);
static void grfe_handler( u8_t *data, int len);
static void grfb_handler(u8_t *data, int len);
static void grfs_handler(u8_t *data, int len);
static void grfg_handler(u8_t *data, int len);
static void grfa_handler(u8_t *data, int len);
static void visu_handler(u8_t *data, int len);
static void displayer_task(void* arg);

/* scrolling undocumented information
	grfs	
		B: screen number
		B:1 = left, 2 = right,
		Q: scroll pause once done (ms)
		Q: scroll speed (ms)
		W: # of pixels to scroll each time
		W: 0 = continue scrolling after pause, 1 = scroll to scrollend and then stop, 2 = scroll to scrollend and then end animation (causing new update)
		W: width of total scroll area in pixels
			
	grfd
		W: screen number
		W: width of scrollable area	in pixels
	anic ( two versions, don't know what to chose)
		B: flag
			ANIM_TRANSITION (0x01) - transition animation has finished (previous use of ANIC)
			ANIM_SCREEN_1 (0x04)                           - end of first scroll on screen 1
			ANIM_SCREEN_2 (0x08)                           - end of first scroll on screen 2
			ANIM_SCROLL_ONCE (0x02) | ANIM_SCREEN_1 (0x04) - end of scroll once on screen 1
			ANIM_SCROLL_ONCE (0x02) | ANIM_SCREEN_2 (0x08) - end of scroll once on screen 2	
		- or -
			ANIM_TRANSITION   0x01 # A transition animation has finished
			ANIM_SCROLL_ONCE  0x02 # A scrollonce has finished
			ANIM_SCREEN_1     0x04 # For scrollonce only, screen 1 was scrolling
			ANIM_SCREEN_2     0x08 # For scrollonce only, screen 2 was scrolling
*/

/* classical visu not our specific version)
 Parameters for the spectrum analyzer:
   0 - Channels: stereo == 0, mono == 1
   1 - Bandwidth: 0..22050Hz == 0, 0..11025Hz == 1
   2 - Preemphasis in dB per KHz
 Left channel parameters:
   3 - Position in pixels
   4 - Width in pixels
   5 - orientation: left to right == 0, right to left == 1
   6 - Bar width in pixels
   7 - Bar spacing in pixels
   8 - Clipping: show all subbands == 0, clip higher subbands == 1
   9 - Bar intensity (greyscale): 1-3
   10 - Bar cap intensity (greyscale): 1-3
 Right channel parameters (not required for mono):
   11-18 - same as left channel parameters

 Parameters for the vumeter:
   0 - Channels: stereo == 0, mono == 1
   1 - Style: digital == 0, analog == 1
 Left channel parameters:
   2 - Position in pixels
   3 - Width in pixels
 Right channel parameters (not required for mono):
   4-5 - same as left channel parameters
*/

/****************************************************************************************
 * 
 */
bool sb_display_init(void) {
	static DRAM_ATTR StaticTask_t xTaskBuffer __attribute__ ((aligned (4)));
	static EXT_RAM_ATTR StackType_t xStack[SCROLL_STACK_SIZE] __attribute__ ((aligned (4)));
	
	// no display, just make sure we won't have requests
	if (!display || GDS_GetWidth(display) <= 0 || GDS_GetHeight(display) <= 0) {
		LOG_INFO("no display for LMS");
		return false;
	}	
	
	// inform LMS of our screen dimensions
	sendSETD(GDS_GetWidth(display), GDS_GetHeight(display));
	
	// need to force height to 32 maximum
	displayer.width = GDS_GetWidth(display);
	displayer.height = min(GDS_GetHeight(display), SB_HEIGHT);
	
	// allocate gray-color mapping if needed;
	if (GDS_GetMode(display) > GDS_GRAYSCALE) {
		grayMap = malloc(256*sizeof(*grayMap));
		for (int i = 0; i < 256; i++) grayMap[i] = GDS_GrayMap(display, i);
	}
	
	// create visu configuration
	visu.bar_gap = 1;
	visu.speed = 100;
	visu.back.frame = calloc(1, (displayer.width * displayer.height) / 8);
	dsps_fft2r_init_fc32(visu.fft, FFT_LEN);
	dsps_wind_hann_f32(visu.hanning, FFT_LEN);
		
	// create scroll management task
	displayer.mutex = xSemaphoreCreateMutex();
	displayer.task = xTaskCreateStatic( (TaskFunction_t) displayer_task, "displayer_thread", SCROLL_STACK_SIZE, NULL, ESP_TASK_PRIO_MIN + 1, xStack, &xTaskBuffer);
	
	// size scroller (width + current screen)
	scroller.scroll.max = (displayer.width * displayer.height / 8) * (15 + 1);
	scroller.scroll.frame = malloc(scroller.scroll.max);
	scroller.back.frame = malloc(displayer.width * displayer.height / 8);
	scroller.frame = malloc(displayer.width * displayer.height / 8);

	// chain handlers
	slimp_handler_chain = slimp_handler;
	slimp_handler = handler;
	
	notify_chain = server_notify;
	server_notify = server;
	
	display_bus_chain = display_bus;
	display_bus = display_bus_handler;
	
	return true;
}

/****************************************************************************************
 * Receive display bus commands
 */
static bool display_bus_handler(void *from, enum display_bus_cmd_e cmd) {
	// don't answer to own requests
	if (from == &displayer) return false ;
	
	LOG_INFO("Display bus command %d", cmd);
	
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);
	
	switch (cmd) {
	case DISPLAY_BUS_TAKE:
		displayer.owned = false;
		break;
	case DISPLAY_BUS_GIVE:
		displayer.owned = true;
		break;
	}
	
	xSemaphoreGive(displayer.mutex);
	
	// chain to rest of "bus"
	if (display_bus_chain) return (*display_bus_chain)(from, cmd);
	else return true;
}

/****************************************************************************************
 * Send ANImation Complete
 */
static void sendANIC(u8_t code) {
	struct ANIC_header pkt_header;

	memset(&pkt_header, 0, sizeof(pkt_header));
	memcpy(&pkt_header.opcode, "ANIC", 4);
	pkt_header.length = htonl(sizeof(pkt_header) - 8);
	pkt_header.mode = code;

	LOCK_P;
	send_packet((uint8_t *) &pkt_header, sizeof(pkt_header));
	UNLOCK_P;
}	
		
/****************************************************************************************
 * Send SETD for width
 */
static void sendSETD(u16_t width, u16_t height) {
	struct SETD_header pkt_header;
		
	memset(&pkt_header, 0, sizeof(pkt_header));
	memcpy(&pkt_header.opcode, "SETD", 4);

	pkt_header.id = 0xfe; // id 0xfe is width S:P:Squeezebox2
	pkt_header.length = htonl(sizeof(pkt_header) +  4 - 8);
		
	LOG_INFO("sending dimension %ux%u", width, height);	

	width = htons(width);
	height = htons(height);
		
	LOCK_P;
	send_packet((uint8_t *) &pkt_header, sizeof(pkt_header));
	send_packet((uint8_t *) &width, 2);
	send_packet((uint8_t *) &height, 2);
	UNLOCK_P;
}

/****************************************************************************************
 * 
 */
static void server(in_addr_t ip, u16_t hport, u16_t cport) {
	char msg[32];
	
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);
	
	sprintf(msg, "%s:%hu", inet_ntoa(ip), hport);
	if (displayer.owned) GDS_TextPos(display, GDS_FONT_DEFAULT, GDS_TEXT_CENTERED, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, msg);
	displayer.dirty = true;
	
	xSemaphoreGive(displayer.mutex);
		
	// inform new LMS server of our capabilities
	sendSETD(displayer.width, GDS_GetHeight(display));
	
	if (notify_chain) (*notify_chain)(ip, hport, cport);
}

/****************************************************************************************
 * Process graphic display data
 */
static bool handler(u8_t *data, int len){
	bool res = true;

	if (!strncmp((char*) data, "vfdc", 4)) {
		vfdc_handler(data, len);
	} else if (!strncmp((char*) data, "grfe", 4)) {
		grfe_handler(data, len);
	} else if (!strncmp((char*) data, "grfb", 4)) {
		grfb_handler(data, len);
	} else if (!strncmp((char*) data, "grfs", 4)) {
		grfs_handler(data, len);		
	} else if (!strncmp((char*) data, "grfg", 4)) {
		grfg_handler(data, len);
	} else if (!strncmp((char*) data, "grfa", 4)) {
		grfa_handler(data, len);		
	} else if (!strncmp((char*) data, "visu", 4)) {
		visu_handler(data, len);
	} else {
		res = false;
	}

	// chain protocol handlers (bitwise or is fine)
	if (*slimp_handler_chain) res |= (*slimp_handler_chain)(data, len);
	
	return res;
}

/****************************************************************************************
 * Change special LCD chars to something more printable on screen 
 */
static void makeprintable(unsigned char * line) {
	for (int n = 0; n < LINELEN; n++) {
		switch (line[n]) {
		case 11:		/* block */
			line[n] = '#';
			break;;
		case 16:		/* rightarrow */
			line[n] = '>';
			break;;
		case 22:		/* circle */
			line[n] = '@';
			break;;
		case 145:		/* note */
			line[n] = ' ';
			break;;
		case 152:		/* bell */
			line[n] = 'o';
			break;
		default:
			break;
		}
	}
}

/****************************************************************************************
 * Check if char is printable, or a valid symbol
 */
static bool charisok(unsigned char c) {
   switch (c) {
	  case 11:		/* block */
	  case 16:		/* rightarrow */
	  case 22:		/* circle */
	  case 145:		/* note */
	  case 152:		/* bell */
		 return true;
	 break;;
	  default:
		 return isprint(c);
   }
}

/****************************************************************************************
 * Show the display (text mode)
 */
static void show_display_buffer(char *ddram) {
	char line1[LINELEN+1];
	char *line2;

	memset(line1, 0, LINELEN+1);
	strncpy(line1, ddram, LINELEN);
	line2 = &(ddram[LINELEN]);
	line2[LINELEN] = '\0';

	/* Convert special LCD chars */
	makeprintable((unsigned char *)line1);
	makeprintable((unsigned char *)line2);

	LOG_DEBUG("\n\t%.40s\n\t%.40s", line1, line2);

	GDS_TextLine(display, 1, GDS_TEXT_LEFT, GDS_TEXT_CLEAR, line1);	
	GDS_TextLine(display, 2, GDS_TEXT_LEFT, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, line2);	
}

/****************************************************************************************
 * Process display data
 */
static void vfdc_handler( u8_t *_data, int bytes_read) {
	unsigned short *data = (unsigned short*) _data, *display_data;
	char ddram[(LINELEN + 1) * 2];
	int n, addr = 0; /* counter */

	bytes_read -= 4;
	if (bytes_read % 2) bytes_read--; /* even number of bytes */
	// if we use Noritake VFD codes, display data starts at 12
	display_data = &(data[5]); /* display data starts at byte 10 */

	memset(ddram, ' ', LINELEN * 2);

	for (n = 0; n < (bytes_read/2); n++) {
		unsigned short d; /* data element */
		unsigned char t, c;

		d = ntohs(display_data[n]);
		t = (d & 0x00ff00) >> 8; /* type of display data */
		c = (d & 0x0000ff); /* character/command */
		switch (t) {
			case 0x03: /* character */
				if (!charisok(c)) c = ' ';
				if (addr <= LINELEN * 2) {
					ddram[addr++] = c;
		}
				break;
			case 0x02: /* command */
				switch (c) {
					case 0x06: /* display clear */
						memset(ddram, ' ', LINELEN * 2);
						break;
					case 0x02: /* cursor home */
						addr = 0;
						break;
					case 0xc0: /* cursor home2 */
						addr = LINELEN;
						break;
				}
		}
	}

	show_display_buffer(ddram);
}

/****************************************************************************************
 * Display VU-Meter (lots of hard-coding)
 */
void draw_VU(struct GDS_Device * display, const uint8_t *data, int level, int x, int y, int width, bool rotate) {
	// VU data is by columns and vertical flip to allow block offset 
	data += level * VU_WIDTH * VU_HEIGHT;
	
	// adjust to current display window
	if (width > VU_WIDTH) {
		if (rotate) y += (width - VU_WIDTH) / 2;		
		else x += (width - VU_WIDTH) / 2;		
		width = VU_WIDTH;
	} else {
		data += (VU_WIDTH - width) / 2 * VU_HEIGHT;	
	}	

	if (GDS_GetMode(display) <= GDS_GRAYSCALE) {
		// this is 8 bits grayscale
		int scale = 8 - GDS_GetDepth(display);
	
		// use "fast" version as we are not beyond screen boundaries
		if (rotate) {
			for (int r = 0; r < width; r++) {
				for (int c = VU_HEIGHT; --c >= 0;) {
					GDS_DrawPixelFast(display, c + x, r + y, *data++ >> scale);
				}	
			}	
		} else {
			for (int r = 0; r < width; r++) {
				for (int c = 0; c < VU_HEIGHT; c++) {
					GDS_DrawPixelFast(display, r + x, c + y, *data++ >> scale);
				}	
			}			
		}	
	} else {
		// use "fast" version as we are not beyond screen boundaries
		if (rotate) {
			for (int r = 0; r < width; r++) {
				for (int c = VU_HEIGHT; --c >= 0;) {
					GDS_DrawPixelFast(display, c + x, r + y, grayMap[*data++]);
				}	
			}	
		} else {
			for (int r = 0; r < width; r++) {
				for (int c = 0; c < VU_HEIGHT; c++) {
					GDS_DrawPixelFast(display, r + x, c + y, grayMap[*data++]);
				}	
			}			
		}	
	}	
	
	// need to manually set dirty flag as DrawPixel does not do it
	GDS_SetDirty(display);
}

/****************************************************************************************
 * Process graphic display data
 */
static void grfe_handler( u8_t *data, int len) {
		
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);
	
	scroller.active = false;
	
	// visu has priority when full screen on small screens
	if ((visu.mode & VISU_ESP32) && !visu.col && visu.row < displayer.height) {
		xSemaphoreGive(displayer.mutex);
		return;
	}	
	
	// are we in control
	if (displayer.owned) {
		// draw new frame, it might be less than full screen (small visu)
		int width = ((len - sizeof(struct grfe_packet)) * 8) / displayer.height;

		// did we have something that might have written on the bottom of a displayer's height + display
		if (displayer.dirty || (artwork.enable && width == displayer.width && artwork.y < displayer.height)) {
			GDS_Clear(display, GDS_COLOR_BLACK);
			displayer.dirty = false;
		}	
	
		// when doing screensaver, that frame becomes a visu background
		if (!(visu.mode & VISU_ESP32)) {
			visu.back.width = width;
			memset(visu.back.frame, 0, (displayer.width * displayer.height) / 8);
			memcpy(visu.back.frame, data + sizeof(struct grfe_packet), (width * displayer.height) / 8);
			// this is a bit tricky but basically that checks if frame if full of 0
			visu.back.active = *visu.back.frame || memcmp(visu.back.frame, visu.back.frame + 1, width - 1);
		}
		
		GDS_DrawBitmapCBR(display, data + sizeof(struct grfe_packet), width, displayer.height, GDS_COLOR_WHITE);
		GDS_Update(display);
	}	
	
	xSemaphoreGive(displayer.mutex);
	
	LOG_DEBUG("grfe frame %u", len);
}	

/****************************************************************************************
 * Brightness
 */
static void grfb_handler(u8_t *data, int len) {
	struct grfb_packet *pkt = (struct grfb_packet*) data;
	
	pkt->brightness = htons(pkt->brightness);
	
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);

	// LMS driver sends 0..5 value, we assume driver is highly log
	if (pkt->brightness <= 0) {
		GDS_DisplayOff(display); 
	} else {
		GDS_DisplayOn(display);
		GDS_SetContrast(display, 255 * powf(pkt->brightness / 5.0f, 3));
	}
	
	xSemaphoreGive(displayer.mutex);
	
	LOG_INFO("brightness %hu", pkt->brightness);
}

/****************************************************************************************
 * Scroll set
 */
static void grfs_handler(u8_t *data, int len) {
	struct grfs_packet *pkt = (struct grfs_packet*) data;
	int size = len - sizeof(struct grfs_packet);
	int offset = htons(pkt->offset);
	
	LOG_DEBUG("grfs s:%u d:%u p:%u sp:%u by:%hu m:%hu w:%hu o:%hu", 
				(int) pkt->screen,
				(int) pkt->direction,	// 1=left, 2=right
				htonl(pkt->pause),		// in ms	
				htonl(pkt->speed),		// in ms
				htons(pkt->by),			// # of pixel of scroll step
				htons(pkt->mode),		// 0=continuous, 1=once and stop, 2=once and end
				htons(pkt->width),		// last column of animation that contains a "full" screen
				htons(pkt->offset)		// offset if multiple packets are sent
	);
	
	// new grfs frame, build scroller info
	if (!offset) {	
		// use the display as a general lock
		xSemaphoreTake(displayer.mutex, portMAX_DELAY);

		// copy & set scroll parameters
		scroller.screen = pkt->screen;
		scroller.pause = htonl(pkt->pause);
		scroller.speed = htonl(pkt->speed);
		scroller.mode = htons(pkt->mode);
		scroller.scroll.width = htons(pkt->width);
		scroller.first = true;
		scroller.overflow = false;
		
		// set scroller steps & beginning
		if (pkt->direction == 1) {
			scroller.scrolled = 0;
			scroller.by = htons(pkt->by);
		} else {
			scroller.scrolled = scroller.scroll.width;
			scroller.by = -htons(pkt->by);
		}	

		xSemaphoreGive(displayer.mutex);
	}	

	// copy scroll frame data (no semaphore needed)
	if (scroller.scroll.size + size < scroller.scroll.max && !scroller.overflow) {
		memcpy(scroller.scroll.frame + offset, data + sizeof(struct grfs_packet), size);
		scroller.scroll.size = offset + size;
		LOG_INFO("scroller current size %u (w:%u)", scroller.scroll.size, scroller.scroll.width);
	} else {
		LOG_INFO("scroller too large %u/%u (w:%u)", scroller.scroll.size + size, scroller.scroll.max, scroller.scroll.width);
		scroller.scroll.width = scroller.scroll.size / (displayer.height / 8) - scroller.back.width;
		scroller.overflow = true;
	}	
}

/****************************************************************************************
 * Scroll background frame update & go
 */
static void grfg_handler(u8_t *data, int len) {
	struct grfg_packet *pkt = (struct grfg_packet*) data;
	
	LOG_DEBUG("gfrg s:%hu w:%hu (len:%u)", htons(pkt->screen), htons(pkt->width), len);

	// on small screen, visu has priority when full screen	
	if ((visu.mode & VISU_ESP32) && !visu.col && visu.row < displayer.height) return;
	
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);
		
	// size of scrollable area (less than background)
	scroller.width = htons(pkt->width);
	scroller.back.width = ((len - sizeof(struct grfg_packet)) * 8) / displayer.height;
	memcpy(scroller.back.frame, data + sizeof(struct grfg_packet), len - sizeof(struct grfg_packet));
		
	// update display asynchronously (frames are organized by columns)
	memcpy(scroller.frame, scroller.back.frame, scroller.back.width * displayer.height / 8);
	for (int i = 0; i < scroller.width * displayer.height / 8; i++) scroller.frame[i] |= scroller.scroll.frame[scroller.scrolled * displayer.height / 8 + i];
	
	// can only write if we really own display
	if (displayer.owned) {
		GDS_DrawBitmapCBR(display, scroller.frame, scroller.back.width, displayer.height, GDS_COLOR_WHITE);
		GDS_Update(display);
	}	
		
	// now we can active scrolling, but only if we are not on a small screen
	if (!visu.mode || visu.col || visu.row >= displayer.height) scroller.active = true;
		
	// if we just got a content update, let the scroller manage the screen
	LOG_DEBUG("resuming scrolling task");
			
	xSemaphoreGive(displayer.mutex);
	
	// resume task once we have background, not in grfs
	vTaskResume(displayer.task);
}


/****************************************************************************************
 * Artwork
 */
static void grfa_handler(u8_t *data, int len) {
	struct grfa_packet *pkt = (struct grfa_packet*) data;
	int size = len - sizeof(struct grfa_packet);
	int offset = htonl(pkt->offset);
	int length = htonl(pkt->length);

	// when using full screen visualizer on small screen there is a brief overlay	
	artwork.enable = (length != 0);

	// just a config or an actual artwork	
	if (length < 32) {
		if (artwork.enable) {
			// this is just to specify artwork coordinates
			artwork.x = htons(pkt->x);
			artwork.y = htons(pkt->y);		
		} else if (artwork.size) GDS_ClearWindow(display, artwork.x, artwork.y, -1, -1, GDS_COLOR_BLACK);
		
		// done in any case
		return;
	}
	
	// new grfa artwork, allocate memory
	if (!offset) {	
		// same trick to clean current/previous window
		if (artwork.size) {
			GDS_ClearWindow(display, artwork.x, artwork.y, -1, -1, GDS_COLOR_BLACK);
			artwork.size = 0;
		}
		
		// now use new parameters
		artwork.x = htons(pkt->x);
		artwork.y = htons(pkt->y);
		if (artwork.data) free(artwork.data);
		artwork.data = malloc(length);
	}	
	
	// copy artwork data
	memcpy(artwork.data + offset, data + sizeof(struct grfa_packet), size);
	artwork.size += size;
	if (artwork.size == length) {
		GDS_ClearWindow(display, artwork.x, artwork.y, -1, -1, GDS_COLOR_BLACK);
		GDS_DrawJPEG(display, artwork.data, artwork.x, artwork.y, artwork.y < displayer.height ? (GDS_IMAGE_RIGHT | GDS_IMAGE_TOP) : GDS_IMAGE_CENTER);
		free(artwork.data);
		artwork.data = NULL;
	} 
	
	LOG_INFO("gfra l:%u x:%hu, y:%hu, o:%u s:%u", length, artwork.x, artwork.y, offset, size);
}

/****************************************************************************************
 * Update visualization bars
 */
static void visu_update(void) {
	// no need to protect against no woning the display as we are playing	
	if (pthread_mutex_trylock(&visu_export.mutex)) return;
	
	int mode = visu.mode & ~VISU_ESP32;
				
	// not enough samples
	if (visu_export.level < (mode == VISU_VUMETER ? RMS_LEN : FFT_LEN) * 2 && visu_export.running) {
		pthread_mutex_unlock(&visu_export.mutex);
		return;
	}
	
	// reset bars for all cases first	
	for (int i = visu.n; --i >= 0;) visu.bars[i].current = 0;
	
	if (visu_export.running) {
					
		if (mode == VISU_VUMETER) {
			s16_t *iptr = visu_export.buffer;
			
			// calculate sum(L²+R²), try to not overflow at the expense of some precision
			for (int i = RMS_LEN; --i >= 0;) {
				visu.bars[0].current += (*iptr * *iptr + (1 << (RMS_LEN_BIT - 2))) >> (RMS_LEN_BIT - 1);
				iptr++;
				visu.bars[1].current += (*iptr * *iptr + (1 << (RMS_LEN_BIT - 2))) >> (RMS_LEN_BIT - 1);
				iptr++;
			}	
		
			// convert to dB (1 bit remaining for getting X²/N, 60dB dynamic starting from 0dBFS = 3 bits back-off)
			for (int i = visu.n; --i >= 0;) {	 
				visu.bars[i].current = visu.max * (0.01667f*10*log10f(0.0000001f + (visu.bars[i].current >> (visu_export.gain == FIXED_ONE ? 7 : 1))) - 0.2543f);
				if (visu.bars[i].current > visu.max) visu.bars[i].current = visu.max;
				else if (visu.bars[i].current < 0) visu.bars[i].current = 0;
			}
		} else {
			// on xtensa/esp32 the floating point FFT takes 1/2 cycles of the fixed point
			for (int i = 0 ; i < FFT_LEN ; i++) {
				// don't normalize here, but we are due INT16_MAX and FFT_LEN / 2 / 2
				visu.samples[i * 2 + 0] = (float) (visu_export.buffer[2*i] + visu_export.buffer[2*i + 1]) * visu.hanning[i];
				visu.samples[i * 2 + 1] = 0;
			}

			// actual FFT that might be less cycle than all the crap below		
			dsps_fft2r_fc32_ae32(visu.samples, FFT_LEN);
			dsps_bit_rev_fc32_ansi(visu.samples, FFT_LEN);
			float rate = visu_export.rate;
			
			// now arrange the result with the number of bar and sampling rate (don't want DC)
			for (int i = 0, j = 1; i < visu.n && j < (FFT_LEN / 2); i++) {
				float power, count;

				// find the next point in FFT (this is real signal, so only half matters)
				for (count = 0, power = 0; j * visu_export.rate < visu.bars[i].limit * FFT_LEN && j < FFT_LEN / 2; j++, count += 1) {
					power += visu.samples[2*j] * visu.samples[2*j] + visu.samples[2*j+1] * visu.samples[2*j+1];
				}
				// due to sample rate, we have reached the end of the available spectrum
				if (j >= (FFT_LEN / 2)) {
					// normalize accumulated data
					if (count) power /= count * 2.;
				} else if (count) {
					// how much of what remains do we need to add
					float ratio = j - (visu.bars[i].limit * FFT_LEN) / rate;
					power += (visu.samples[2*j] * visu.samples[2*j] + visu.samples[2*j+1] * visu.samples[2*j+1]) * ratio;
					
					// normalize accumulated data
					power /= (count + ratio) * 2;
				} else {
					// no data for that band (sampling rate too high), just assume same as previous one
					power = (visu.samples[2*j] * visu.samples[2*j] + visu.samples[2*j+1] * visu.samples[2*j+1]) / 2.;
				}	
			
				// convert to dB and bars, same back-off
				if (power) visu.bars[i].current = visu.max * (0.01667f*10*(log10f(power) - log10f(FFT_LEN/(visu_export.gain == FIXED_ONE ? 128 : 2)*2)) - 0.2543f);
				if (visu.bars[i].current > visu.max) visu.bars[i].current = visu.max;
				else if (visu.bars[i].current < 0) visu.bars[i].current = 0;
			}	
		}
	} 
		
	// we took what we want, we can release the buffer
	visu_export.level = 0;
	pthread_mutex_unlock(&visu_export.mutex);

	// don't refresh screen if all max are 0 (we were are somewhat idle)
	int clear = 0;
	for (int i = visu.n; --i >= 0;) clear = max(clear, visu.bars[i].max);
	if (clear) GDS_ClearExt(display, false, false, visu.col, visu.row, visu.col + visu.width - 1, visu.row + visu.height - 1);
	
	// draw background if we are in screensaver mode
	if (!(visu.mode & VISU_ESP32) && visu.back.active) {
		GDS_DrawBitmapCBR(display, visu.back.frame, visu.back.width, displayer.height, GDS_COLOR_WHITE);
	}	

	if (mode != VISU_VUMETER || !visu.style) {
		// there is much more optimization to be done here, like not redrawing bars unless needed

		for (int i = visu.n; --i >= 0;) {
			// update maximum
			if (visu.bars[i].current > visu.bars[i].max) visu.bars[i].max = visu.bars[i].current;
			else if (visu.bars[i].max) visu.bars[i].max--;
			else if (!clear) continue;

			if (visu.rotate) {
				int x1 = visu.col;
				int y1 = visu.row + visu.border + visu.bar_border + i*(visu.bar_width + visu.bar_gap);

				for (int j = 0; j <= visu.bars[i].current; j += 2) 
					GDS_DrawLine(display, x1 + j, y1, x1 + j, y1 + visu.bar_width - 1, GDS_COLOR_WHITE);
			
				if (visu.bars[i].max > 2) {
					GDS_DrawLine(display, x1 + visu.bars[i].max, y1, x1 + visu.bars[i].max, y1 + visu.bar_width - 1, GDS_COLOR_WHITE);			
					if (visu.bars[i].max < visu.max - 1) GDS_DrawLine(display, x1 + visu.bars[i].max + 1, y1, x1 + visu.bars[i].max + 1, y1 + visu.bar_width - 1, GDS_COLOR_WHITE);			
				}
			} else {
				int x1 = visu.col + visu.border + visu.bar_border + i*(visu.bar_width + visu.bar_gap);
				int y1 = visu.row + visu.height - 1;
				for (int j = 0; j <= visu.bars[i].current; j += 2) 
				GDS_DrawLine(display, x1, y1 - j, x1 + visu.bar_width - 1, y1 - j, GDS_COLOR_WHITE);
			
				if (visu.bars[i].max > 2) {
					GDS_DrawLine(display, x1, y1 - visu.bars[i].max, x1 + visu.bar_width - 1, y1 - visu.bars[i].max, GDS_COLOR_WHITE);			
					if (visu.bars[i].max < visu.max - 1) GDS_DrawLine(display, x1, y1 - visu.bars[i].max + 1, x1 + visu.bar_width - 1, y1 - visu.bars[i].max + 1, GDS_COLOR_WHITE);			
				}
			}
		}
	} else if (displayer.width / 2 >  3 * VU_WIDTH / 4) {
		if (visu.rotate) {
			draw_VU(display, vu_bitmap, visu.bars[0].current, 0, visu.row, visu.height / 2, visu.rotate);
			draw_VU(display, vu_bitmap, visu.bars[1].current, 0, visu.row + visu.height / 2, visu.height / 2, visu.rotate);
		} else {
			draw_VU(display, vu_bitmap, visu.bars[0].current, 0, visu.row, visu.width / 2, visu.rotate);
			draw_VU(display, vu_bitmap, visu.bars[1].current, visu.width / 2, visu.row, visu.width / 2, visu.rotate);
		}
	} else {
		int level = (visu.bars[0].current + visu.bars[1].current) / 2;
		draw_VU(display, vu_bitmap, level, 0, visu.row, visu.rotate ? visu.height : visu.width, visu.rotate);		
	}	
}


/****************************************************************************************
 * Calculate spectrum spread
 */
static void spectrum_limits(int min, int n, int pos) {
	if (n / 2) {
		int step = ((DISPLAY_BW - min) * visu.spectrum_scale)  / (n/2);
		visu.bars[pos].limit = min + step;
		for (int i = 1; i < n/2; i++) visu.bars[pos+i].limit = visu.bars[pos+i-1].limit + step;
		spectrum_limits(visu.bars[pos + n/2 - 1].limit, n - n/2, pos + n/2);
	} else {
		visu.bars[pos].limit = DISPLAY_BW;
	}	
}

/****************************************************************************************
 * Fit visu
 */
static void visu_fit(int bars, int width, int height) {
	// try to adapt to what we have
	if ((visu.mode & ~VISU_ESP32) == VISU_SPECTRUM) {
		visu.n = bars ? bars : MAX_BARS;
		visu.max = height - 1;
		if (visu.spectrum_scale <= 0 || visu.spectrum_scale > 0.5) visu.spectrum_scale = 0.5;
		spectrum_limits(0, visu.n, 0);
	} else {
		visu.n = 2;
		visu.max = (visu.style ? VU_COUNT : height) - 1;
	}	
		
	do {
		visu.bar_width = (width - visu.border - visu.bar_gap * (visu.n - 1)) / visu.n;
		if (visu.bar_width > 0) break;
	} while (--visu.n);	
	
	visu.bar_border = (width - visu.border - (visu.bar_width + visu.bar_gap) * visu.n + visu.bar_gap) / 2;
}	

/****************************************************************************************
 * Visu packet handler
 */
static void visu_handler( u8_t *data, int len) {
	struct visu_packet *pkt = (struct visu_packet*) data;
	int bars = 0;

	LOG_DEBUG("visu %u with %u parameters", pkt->which, pkt->count);
		
	/* 
	 If width is specified, then respect all coordinates, otherwise we try to 
	 use the bottom part of the display and if it is a small display, we overwrite
	 text
	*/ 
	
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);
	visu.mode = pkt->which;
	
	// little trick to clean the taller screens when switching visu 
	if (visu.row >= displayer.height) GDS_ClearExt(display, false, true, visu.col, visu.row, visu.col + visu.width - 1, visu.row + visu.height - 1);
	
	if (visu.mode) {
		// these will be overidden if necessary
		visu.col = visu.border = 0;
		visu.rotate = false;
		
		// what type of visu
		if (visu.mode & VISU_ESP32) {
			if (pkt->count >= 4) {
				// more than 4 parameters, this is small visu, then go were we are told to
				pkt->height = htonl(pkt->height);
				pkt->row = htonl(pkt->row);
				pkt->col = htonl(pkt->col);

				visu.style = 0;
				visu.width = htonl(pkt->width);
				visu.height = pkt->height ? pkt->height : displayer.height;
				visu.col = pkt->col < 0 ? displayer.width + pkt->col : pkt->col;
				visu.row = pkt->row < 0 ? GDS_GetHeight(display) + pkt->row : pkt->row;
				visu.border =  htonl(pkt->border);
				bars = htonl(pkt->bars);
				visu.spectrum_scale = htonl(pkt->spectrum_scale) / 100.;
			} else {
				// full screen visu, try to use bottom screen if available
				visu.width = htonl(pkt->full.width);
				visu.height = GDS_GetHeight(display) > displayer.height ? GDS_GetHeight(display) - displayer.height : GDS_GetHeight(display);
				visu.row = GDS_GetHeight(display) - visu.height;			
				
				// try to estimate if we should rotate visu
				if (visu.height > displayer.height && visu.height > 2*visu.width) visu.rotate = true;
				
				// is this spectrum or analogue/digital
				if ((visu.mode & ~VISU_ESP32) == VISU_SPECTRUM) {
					bars = htonl(pkt->full.bars);
					visu.spectrum_scale = htonl(pkt->full.spectrum_scale) / 100.;
				} else {
					// select analogue/digital style
					visu.style = htonl(pkt->full.style);
				}
			}	
		} else {
			// classical (screensaver) mode, don't try to optimize screen usage & force some params
			visu.row = 0;
			visu.height = GDS_GetHeight(display);
			visu.width = displayer.width;				
			visu.spectrum_scale = 0.25;				
			if (visu.mode == VISU_SPECTRUM) {
				bars = visu.width / (htonl(pkt->channels[0].bar_width) + htonl(pkt->channels[0].bar_space));
			} else {
				visu.style = htonl(pkt->classical_vu.style);
				if (visu.style) visu.row = visu.height - VU_HEIGHT;
			}	
			if (bars > MAX_BARS) bars = MAX_BARS;
		}	
		
		// for rotate, swap width & height
		if (visu.rotate) visu_fit(bars, visu.height, visu.width);
		else visu_fit(bars, visu.width, visu.height);

		// give up if not enough space
		if (visu.bar_width < 0)	{
			visu.mode = VISU_BLANK;
			LOG_WARN("Not enough room for displaying visu");
		} else {
			// de-activate scroller if we are taking main screen
			if (visu.row < displayer.height) scroller.active = false;
			vTaskResume(displayer.task);
		}	
		visu.wake = 0;
		
		// reset bars maximum
		for (int i = visu.n; --i >= 0;) visu.bars[i].max = 0;
				
		GDS_ClearExt(display, false, true, visu.col, visu.row, visu.col + visu.width - 1, visu.row + visu.height - 1);
		
		LOG_INFO("Visualizer with %u bars of width %d:%d:%d:%d (%w:%u,h:%u,c:%u,r:%u,s:%.02f)", visu.n, visu.bar_border, visu.bar_width, visu.bar_gap, visu.border, visu.width, visu.height, visu.col, visu.row, visu.spectrum_scale);
	} else {
		LOG_INFO("Stopping visualizer");
	}	
	
	xSemaphoreGive(displayer.mutex);
}	

/****************************************************************************************
 * Scroll task
 *  - with the addition of the visualizer, it's a bit a 2-headed beast not easy to 
 * maintain, so som better separation between the visu and scroll is probably needed
  */
static void displayer_task(void *args) {
	int sleep;

	while (1) {
		xSemaphoreTake(displayer.mutex, portMAX_DELAY);
		
		// suspend ourselves if nothing to do, grfg or visu will wake us up
		if (!scroller.active && !visu.mode)  {
			xSemaphoreGive(displayer.mutex);
			vTaskSuspend(NULL);
			xSemaphoreTake(displayer.mutex, portMAX_DELAY);
			scroller.wake = visu.wake = 0;
		}	
		
		// go for long sleep when either item is disabled
		if (!visu.mode) visu.wake = LONG_WAKE;
		if (!scroller.active) scroller.wake = LONG_WAKE;
								
		// scroll required amount of columns (within the window)
		if (scroller.active && scroller.wake <= 0) {
			// by default go for the long sleep, will change below if required
			scroller.wake = LONG_WAKE;
			
			// do we have more to scroll (scroll.width is the last column from which we have a full zone)
			if (scroller.by > 0 ? (scroller.scrolled <= scroller.scroll.width) : (scroller.scrolled >= 0)) {
				memcpy(scroller.frame, scroller.back.frame, scroller.back.width * displayer.height / 8);
				for (int i = 0; i < scroller.width * displayer.height / 8; i++) scroller.frame[i] |= scroller.scroll.frame[scroller.scrolled * displayer.height / 8 + i];
				scroller.scrolled += scroller.by;
				if (displayer.owned) GDS_DrawBitmapCBR(display, scroller.frame, scroller.width, displayer.height, GDS_COLOR_WHITE);	
				
				// short sleep & don't need background update
				scroller.wake = scroller.speed;
			} else if (scroller.first || !scroller.mode) {
				// at least one round done
				scroller.first = false;
				
				// see if we need to pause or if we are done 				
				if (scroller.mode) {
					sendANIC(ANIM_SCROLL_ONCE | ANIM_SCREEN_1);
					LOG_INFO("scroll-once terminated");
				} else {
					scroller.wake = scroller.pause;
					LOG_DEBUG("scroll cycle done, pausing for %u (ms)", scroller.pause);
				}
								
				// need to reset pointers for next scroll
				scroller.scrolled = scroller.by < 0 ? scroller.scroll.width : 0;
			} 
		}

		// update visu if active
		if (visu.mode && visu.wake <= 0) {
			visu_update();
			visu.wake = 100;
		}
		
		// need to make sure we own display
		if (displayer.owned) GDS_Update(display);
		
		// release semaphore and sleep what's needed
		xSemaphoreGive(displayer.mutex);
		
		sleep = min(visu.wake, scroller.wake);
		vTaskDelay(sleep / portTICK_PERIOD_MS);
		scroller.wake -= sleep;
		visu.wake -= sleep;
	}	
}	
