/* 
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <arpa/inet.h>
#include "esp_log.h"
#include "globdefs.h"
#include "platform_config.h"
#include "tools.h"
#include "display.h"
#include "services.h"
#include "gds.h"
#include "gds_default_if.h"
#include "gds_draw.h"
#include "gds_text.h"
#include "gds_font.h"
#include "gds_image.h"

static const char *TAG = "display";

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))

#define DISPLAYER_STACK_SIZE 	(3*1024)
#define SCROLLABLE_SIZE			384
#define HEADER_SIZE				64
#define	DEFAULT_SLEEP			3600
#define ARTWORK_BORDER			1

extern const uint8_t default_artwork[]   asm("_binary_note_jpg_start");

static EXT_RAM_ATTR struct {
	TaskHandle_t task;
	SemaphoreHandle_t mutex;
	int pause, speed, by;
	enum { DISPLAYER_DOWN, DISPLAYER_IDLE, DISPLAYER_ACTIVE } state;
	char header[HEADER_SIZE + 1];
	char string[SCROLLABLE_SIZE + 1];
	int offset, boundary;
	char *metadata_config;
	bool timer, refresh;
	uint32_t elapsed;
	struct {
		uint32_t value;
		char string[8]; // H:MM:SS
		bool visible;
	} duration;
	struct {
		bool enable, active;
		bool fit;
		bool updated;
		int tick;
		int offset;
	}  artwork;
	TickType_t tick;
} displayer;

static const char *known_drivers[] = {"SH1106",
        "SH1122",
		"SSD1306",
		"SSD1322",
		"SSD1326",
		"SSD1327",
		"SSD1675",
		"SSD1351",
		"ST7735",
		"ST7789",
		"ILI9341",
		NULL
	};
    
static void displayer_task(void *args);
static void display_sleep(void);

struct GDS_Device *display;   
extern GDS_DetectFunc SSD1306_Detect, SSD132x_Detect, SH1106_Detect, SH1122_Detect, SSD1675_Detect, SSD1322_Detect, SSD1351_Detect, ST77xx_Detect, ILI9341_Detect;
GDS_DetectFunc *drivers[] = { SH1106_Detect, SH1122_Detect, SSD1306_Detect, SSD132x_Detect, SSD1675_Detect, SSD1322_Detect, SSD1351_Detect, ST77xx_Detect, ILI9341_Detect, NULL };

/****************************************************************************************
 * 
 */
void display_init(char *welcome) {
	bool init = false;
	char *config = config_alloc_get_str("display_config", CONFIG_DISPLAY_CONFIG, "N/A");
	
	int width = -1, height = -1, backlight_pin = -1;
	char *drivername = strstr(config, "driver");

	PARSE_PARAM(config, "width", '=', width);
	PARSE_PARAM(config, "height", '=', height);
	PARSE_PARAM(config, "back", '=', backlight_pin);
		
	// query drivers to see if we have a match
	ESP_LOGI(TAG, "Trying to configure display with %s", config);
	if (backlight_pin >= 0) {
		struct GDS_BacklightPWM PWMConfig = { .Channel = pwm_system.base_channel++, .Timer = pwm_system.timer, .Max = pwm_system.max, .Init = false	};
		display = GDS_AutoDetect(drivername, drivers, &PWMConfig);
	} else {
		display = GDS_AutoDetect(drivername, drivers, NULL);
	}

	// so far so good
	if (display && width > 0 && height > 0) {
		int RST_pin = -1;
		PARSE_PARAM(config, "reset", '=', RST_pin);
		
		// Detect driver interface
		if (strcasestr(config, "I2C") && i2c_system_port != -1) {
			int address = 0x3C;
				
			PARSE_PARAM(config, "address", '=', address);
				
			init = true;
			GDS_I2CInit( i2c_system_port, -1, -1, i2c_system_speed ) ;
			GDS_I2CAttachDevice( display, width, height, address, RST_pin, backlight_pin );
		
			ESP_LOGI(TAG, "Display is I2C on port %u", address);
		} else if (strcasestr(config, "SPI") && spi_system_host != -1) {
			int CS_pin = -1, speed = 0, mode = 0;
		
			PARSE_PARAM(config, "cs", '=', CS_pin);
			PARSE_PARAM(config, "speed", '=', speed);
			PARSE_PARAM(config, "mode", '=', mode);
		
			init = true;
			GDS_SPIInit( spi_system_host, spi_system_dc_gpio );
			GDS_SPIAttachDevice( display, width, height, CS_pin, RST_pin, backlight_pin, speed, mode );
				
			ESP_LOGI(TAG, "Display is SPI host %u with cs:%d", spi_system_host, CS_pin);
		} else {
			display = NULL;
			ESP_LOGI(TAG, "Unsupported display interface or serial link not configured");
		}
	} else {
		display = NULL;
		ESP_LOGW(TAG, "No display driver");
	}	
	
	if (init) {
		static DRAM_ATTR StaticTask_t xTaskBuffer __attribute__ ((aligned (4)));
		static EXT_RAM_ATTR StackType_t xStack[DISPLAYER_STACK_SIZE] __attribute__ ((aligned (4)));
		struct GDS_Layout Layout = {
			.HFlip = strcasestr(config, "HFlip"), 
			.VFlip = strcasestr(config, "VFlip"), 
			.Rotate = strcasestr(config, "rotate"), 
			.Invert = strcasestr(config, "invert"),
			.ColorSwap = strcasestr(config, "cswap"),
		};	

		GDS_SetLayout(display, &Layout);
		GDS_SetFont(display, &Font_line_2);
		GDS_TextPos(display, GDS_FONT_DEFAULT, GDS_TEXT_CENTERED, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, welcome);

		// start the task that will handle scrolling & counting
		displayer.mutex = xSemaphoreCreateMutex();
		displayer.by = 2;
		displayer.pause = 3600;
		displayer.speed = 33;
		displayer.task = xTaskCreateStatic( (TaskFunction_t) displayer_task, "common_displayer", DISPLAYER_STACK_SIZE, NULL, ESP_TASK_PRIO_MIN + 1, xStack, &xTaskBuffer);
		
		// set lines for "fixed" text mode
		GDS_TextSetFontAuto(display, 1, GDS_FONT_LINE_1, -3);
		GDS_TextSetFontAuto(display, 2, GDS_FONT_LINE_2, -3);
		
		displayer.metadata_config = config_alloc_get(NVS_TYPE_STR, "metadata_config");
		
		// leave room for artwork is display is horizontal-style
		if (strcasestr(displayer.metadata_config, "artwork")) {
			displayer.artwork.enable = true;
			displayer.artwork.fit = true;
			if (height <= 64 && width > height * 2) displayer.artwork.offset = width - height - ARTWORK_BORDER;
			PARSE_PARAM(displayer.metadata_config, "artwork", ':', displayer.artwork.fit);
		}	
        
        // and finally register ourselves to power off upon deep sleep
        services_sleep_setsuspend(display_sleep);
	}
	
	free(config);
}

/****************************************************************************************
 * 
 */
static void display_sleep(void) {
    GDS_DisplayOff(display);
}

/****************************************************************************************
 * This is not thread-safe as displayer_task might be in the middle of line drawing
 * but it won't crash (I think) and making it thread-safe would be complicated for a
 * feature which is secondary (the LMS version of scrolling is thread-safe)
 */
static void displayer_task(void *args) {
	int scroll_sleep = 0, timer_sleep;
		
	while (1) {
		// suspend ourselves if nothing to do
		if (displayer.state < DISPLAYER_ACTIVE) {
			if (displayer.state == DISPLAYER_IDLE) GDS_TextLine(display, 2, 0, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, displayer.string);
			vTaskSuspend(NULL);
			scroll_sleep = 0;
			GDS_ClearExt(display, true);
			GDS_TextLine(display, 1, GDS_TEXT_LEFT, GDS_TEXT_UPDATE, displayer.header);
		} else if (displayer.refresh) {
			// little trick when switching master while in IDLE and missing it
			GDS_TextLine(display, 1, GDS_TEXT_LEFT, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, displayer.header);	
			displayer.refresh = false;			
		}
		
		// we have been waken up before our requested time
		if (scroll_sleep <= 10) {
			// something to scroll (or we'll wake-up every pause ms ... no big deal)
			if (*displayer.string && displayer.state == DISPLAYER_ACTIVE) {
				xSemaphoreTake(displayer.mutex, portMAX_DELAY);
				
				// need to work with local copies as we don't want to suspend caller
				int offset = -displayer.offset;
				char *string = strdup(displayer.string);
				scroll_sleep = displayer.offset ? displayer.speed : displayer.pause;
				displayer.offset = displayer.offset >= displayer.boundary ? 0 : (displayer.offset + min(displayer.by, displayer.boundary - displayer.offset));			
				
				xSemaphoreGive(displayer.mutex);				
				
				// now display using safe copies, can be lengthy
				GDS_TextLine(display, 2, offset, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, string);
				free(string);
			} else {
				scroll_sleep = DEFAULT_SLEEP;
			}	
		}	
		
		// handler elapsed track time
		if (displayer.timer && displayer.state == DISPLAYER_ACTIVE) {
			char line[19] = "-", *_line = line + 1; // [-]H:MM:SS / H:MM:SS
			TickType_t tick = xTaskGetTickCount();
			uint32_t elapsed = (tick - displayer.tick) * portTICK_PERIOD_MS;

			if (elapsed >= 1000) {
				xSemaphoreTake(displayer.mutex, portMAX_DELAY);
				displayer.tick = tick;
				elapsed = displayer.elapsed += elapsed / 1000;
				xSemaphoreGive(displayer.mutex);

				// when we have duration but no space, display remaining time
				if (displayer.duration.value && !displayer.duration.visible) elapsed = displayer.duration.value - elapsed;

				if (elapsed < 3600) sprintf(_line, "%u:%02u", elapsed / 60, elapsed % 60);
				else sprintf(_line, "%u:%02u:%02u", (elapsed / 3600) % 100, (elapsed % 3600) / 60, elapsed % 60);

				// concatenate if we have room for elapsed / duration
				if (displayer.duration.visible) {
					strcat(_line, "/");
					strcat(_line, displayer.duration.string);
				} else if (displayer.duration.value) {
					_line--;
				}

				// just re-write the whole line it's easier
				GDS_TextLine(display, 1, GDS_TEXT_LEFT, GDS_TEXT_CLEAR, displayer.header);	
				GDS_TextLine(display, 1, GDS_TEXT_RIGHT, GDS_TEXT_UPDATE, _line);
				
				// if we have not received artwork after 5s, display a default icon
				if (displayer.artwork.active && !displayer.artwork.updated && tick - displayer.artwork.tick > pdMS_TO_TICKS(5000)) {
					ESP_LOGI(TAG, "no artwork received, setting default");
					displayer_artwork((uint8_t*) default_artwork);
				}	
				timer_sleep = 1000;
			} else timer_sleep = max(1000 - elapsed, 0);	
		} else timer_sleep = DEFAULT_SLEEP;
		
		// then sleep the min amount of time
		int sleep = min(scroll_sleep, timer_sleep);
		ESP_LOGD(TAG, "timers s:%d t:%d", scroll_sleep, timer_sleep);
		scroll_sleep -= sleep;
		vTaskDelay(sleep / portTICK_PERIOD_MS);
	}
}	

/****************************************************************************************
 * 
 */
void displayer_artwork(uint8_t *data) {
	if (!displayer.artwork.active) return;
	
	int x = displayer.artwork.offset ? displayer.artwork.offset + ARTWORK_BORDER : 0;
	int y = x ? 0 : 32;
	GDS_ClearWindow(display, x, y, -1, -1, GDS_COLOR_BLACK);
	if (data) {
		displayer.artwork.updated = true;
		GDS_DrawJPEG(display, data, x, y, GDS_IMAGE_CENTER | (displayer.artwork.fit ? GDS_IMAGE_FIT : 0));
	} else {
		displayer.artwork.updated = false;
		displayer.artwork.tick = xTaskGetTickCount();
	}	
	
}

/****************************************************************************************
 * 
 */
bool displayer_can_artwork(void) {
	return displayer.artwork.active;
}

/****************************************************************************************
 * Local volume screen.
 *
 * Servers are free to drive the display themselves, but a minimal slimproto
 * implementation (Music Assistant, for instance) only sends the volume text once
 * when the player connects and never again, so the number stays stale on screen.
 * Drawing it here keeps the display in sync with whatever changed the volume.
 */
void displayer_volume(int volume) {
	if (!display) return;

	int width = GDS_GetWidth(display), height = GDS_GetHeight(display);
	if (width <= 0 || height <= 0) return;

	if (volume < 0) volume = 0;
	if (volume > 100) volume = 100;

	char text[16];
	snprintf(text, sizeof(text), "VOLUME %d", volume);

	xSemaphoreTake(displayer.mutex, portMAX_DELAY);

	GDS_Clear(display, GDS_COLOR_BLACK);

	// number in the middle, bar along the bottom
	GDS_TextPos(display, GDS_FONT_DEFAULT, GDS_TEXT_CENTERED, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, text);

	const int bar_height = 8, margin = 6;
	int bar_bottom = height - 4, bar_top = bar_bottom - bar_height;
	int filled = ((width - 2 * margin) * volume) / 100;

	GDS_DrawBox(display, margin, bar_top, width - margin - 1, bar_bottom, GDS_COLOR_WHITE, false);
	if (filled > 2) {
		GDS_DrawBox(display, margin + 2, bar_top + 2, margin + filled - 1, bar_bottom - 2, GDS_COLOR_WHITE, true);
	}
	GDS_Update(display);

	xSemaphoreGive(displayer.mutex);
}

/****************************************************************************************
 * 
 */
void displayer_metadata(char *artist, char *album, char *title) {
	char *string = displayer.string, *p;
	int len = SCROLLABLE_SIZE;
	
	// need a display!
	if (!display) return;
	
	// just do title if there is no config set
	if (!displayer.metadata_config) {
		strncpy(displayer.string, title ? title : "", SCROLLABLE_SIZE);
		return;
	}
	
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);
	
	// format metadata parameters and write them directly
	if ((p = strcasestr(displayer.metadata_config, "format")) != NULL) {
		char token[16], *q;
		int space = len;
		bool skip = false;
			
		displayer.string[0] = '\0';	
		p = strchr(displayer.metadata_config, '=');
			
		while (p++) {
			// find token and copy what's after when reaching last one
			if (sscanf(p, "%*[^%%]%%%[^%]%%", token) < 0) {
				q = strchr(p, ',');
				strncat(string, p, q ? min(q - p, space) : space);
				break;
			}

			// copy what's before token (be safe)
			if ((q = strchr(p, '%')) == NULL) break;
			
			// skip whatever is after a token if this token is empty
			if (!skip) {
				strncat(string, p, min(q - p, space));
				space = len - strlen(string);
			}	

			// then copy token's content
			if (!strncasecmp(q + 1, "artist", 6) && artist) strncat(string, p = artist, space);
			else if (!strncasecmp(q + 1, "album", 5) && album) strncat(string, p = album, space);
			else if (!strncasecmp(q + 1, "title", 5) && title) strncat(string, p = title, space);
			space = len - strlen(string);
				
			// flag to skip the data following an empty field
			if (*p) skip = false;
			else skip = true;

			// advance to next separator
			p = strchr(q + 1, '%');
		}
	} else {
		strncpy(string, title ? title : "", SCROLLABLE_SIZE);
	}
	
	// get optional scroll speed & pause
	PARSE_PARAM(displayer.metadata_config, "speed", '=', displayer.speed);
	PARSE_PARAM(displayer.metadata_config, "pause", '=', displayer.pause);
	
	displayer.offset = 0;	
	utf8_decode(displayer.string);
	ESP_LOGI(TAG, "playing %s", displayer.string);
	displayer.boundary = GDS_TextStretch(display, 2, displayer.string, SCROLLABLE_SIZE);
		
	xSemaphoreGive(displayer.mutex);
}	

/****************************************************************************************
 *
 */
void displayer_scroll(char *string, int speed, int pause) {
	// need a display!
	if (!display) return;
	
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);

	if (speed) displayer.speed = speed;
	if (pause) displayer.pause = pause;
	displayer.offset = 0;	
	strncpy(displayer.string, string, SCROLLABLE_SIZE);
	displayer.string[SCROLLABLE_SIZE] = '\0';
	displayer.boundary = GDS_TextStretch(display, 2, displayer.string, SCROLLABLE_SIZE);
		
	xSemaphoreGive(displayer.mutex);
}

/****************************************************************************************
 * 
 */
void displayer_timer(enum displayer_time_e mode, int elapsed, int duration) {
	// need a display!
	if (!display) return;
	
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);

	if (displayer.timer) displayer.tick = xTaskGetTickCount();
	if (elapsed >= 0) displayer.elapsed = elapsed / 1000;	
	if (duration > 0) {
		displayer.duration.visible = true;
		displayer.duration.value = duration / 1000;

		if (displayer.duration.value > 3600) sprintf(displayer.duration.string, "%u:%02u:%02u", (displayer.duration.value / 3600) % 10,
													(displayer.duration.value % 3600) / 60, displayer.duration.value % 60);
		else sprintf(displayer.duration.string, "%u:%02u", displayer.duration.value / 60, displayer.duration.value % 60);

		char *buf;
		asprintf(&buf, "%s %s/%s", displayer.header, displayer.duration.string, displayer.duration.string);
		if (GDS_GetTextWidth(display, 1, 0, buf) > GDS_GetWidth(display)) {
			ESP_LOGW(TAG, "Can't fit duration %s (%d) on screen using elapsed only", buf, GDS_GetTextWidth(display, 1, 0, buf));
			displayer.duration.visible = false;
		}
		free(buf);
	} else if (!duration) {
		displayer.duration.visible = false;
		displayer.duration.value = 0;
	}
		
	xSemaphoreGive(displayer.mutex);
}	

/****************************************************************************************
 * See above comment
 */
void displayer_control(enum displayer_cmd_e cmd, ...) {
	va_list args;
	
	if (!display) return;
	
	va_start(args, cmd);
	xSemaphoreTake(displayer.mutex, portMAX_DELAY);
		
	switch(cmd) {
	case DISPLAYER_ACTIVATE: {	
		char *header = va_arg(args, char*);
		displayer.artwork.active = displayer.artwork.enable && va_arg(args, int);
		strncpy(displayer.header, header, HEADER_SIZE);
		displayer.header[HEADER_SIZE] = '\0';
		displayer.state = DISPLAYER_ACTIVE;
		displayer.timer = false;
		displayer.refresh = true;
		displayer.string[0] = '\0';
		displayer.elapsed = displayer.duration.value = 0;
		displayer.duration.visible = false;
		displayer.offset = displayer.boundary = 0;
		display_bus(&displayer, DISPLAY_BUS_TAKE);
		if (displayer.artwork.active) GDS_SetTextWidth(display, displayer.artwork.offset);
		vTaskResume(displayer.task);
		break;
	}	
	case DISPLAYER_SUSPEND:		
		// task will display the line 2 from beginning and suspend
		displayer.state = DISPLAYER_IDLE;
		displayer_artwork(NULL);
		display_bus(&displayer, DISPLAY_BUS_GIVE);
		break;		
	case DISPLAYER_SHUTDOWN:
		// let the task self-suspend (we might be doing i2c_write)
		GDS_SetTextWidth(display, 0);
		displayer_artwork(NULL);
		displayer.state = DISPLAYER_DOWN;
		display_bus(&displayer, DISPLAY_BUS_GIVE);
		break;
	case DISPLAYER_TIMER_RUN:
		if (!displayer.timer) {
			display_bus(&displayer, DISPLAY_BUS_TAKE);
			displayer.timer = true;		
			displayer.tick = xTaskGetTickCount();		
		}	
		break;
	case DISPLAYER_TIMER_PAUSE:
		displayer.timer = false;
		break;
	default:
		break;
	}	
	
	xSemaphoreGive(displayer.mutex);
	va_end(args);
}

/****************************************************************************************
 *
 */
bool display_is_valid_driver(const char * driver){
	return display_conf_get_driver_name(driver)!=NULL;
}

/****************************************************************************************
 *
 */
const char *display_conf_get_driver_name(const char * driver){
	for(uint8_t i=0;known_drivers[i]!=NULL && strlen(known_drivers[i])>0;i++ ){
		if(strcasestr(driver,known_drivers[i])){
			return known_drivers[i];
		}
	}
	return NULL;
}

/****************************************************************************************
 *
 */
char * display_get_supported_drivers(void){
	int total_size = 1;
	char * supported_drivers=NULL;
	const char * separator = "|";
	int separator_len = strlen(separator);

	for(uint8_t i=0;known_drivers[i]!=NULL && strlen(known_drivers[i])>0;i++ ){
		total_size += strlen(known_drivers[i])+separator_len;
	}
	total_size+=2;
	supported_drivers = malloc(total_size);
	memset(supported_drivers,0x00,total_size);
	strcat(supported_drivers,"<");
	for(uint8_t i=0;known_drivers[i]!=NULL && strlen(known_drivers[i])>0;i++ ){
		supported_drivers = strcat(supported_drivers,known_drivers[i]);
		supported_drivers = strcat(supported_drivers,separator);
	}
	strcat(supported_drivers,">");
	return supported_drivers;
}
