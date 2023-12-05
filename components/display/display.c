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
#include <arpa/inet.h>
#include "esp_log.h"
#include "globdefs.h"
#include "tools.h"
#include "accessors.h"
#include "display.h"
#include "services.h"
#include "gds.h"
#include "gds_default_if.h"
#include "gds_draw.h"
#include "gds_text.h"
#include "gds_font.h"
#include "gds_image.h"
#include "Configurator.h"
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
	sys_Metadata *metadata_config;
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

    
static void displayer_task(void *args);
static void display_sleep(void);

struct GDS_Device *display;   
extern GDS_DetectFunc SSD1306_Detect, SSD132x_Detect, SH1106_Detect, SSD1675_Detect, SSD1322_Detect, SSD1351_Detect, ST77xx_Detect, ILI9341_Detect;
GDS_DetectFunc *drivers[] = { SH1106_Detect, SSD1306_Detect, SSD132x_Detect, SSD1675_Detect, SSD1322_Detect, SSD1351_Detect, ST77xx_Detect, ILI9341_Detect, NULL };

/****************************************************************************************
 * 
 */
void display_init(char *welcome) {
	bool init = false;
	int width = -1, height = -1, backlight_pin = -1, RST_pin = -1;
	sys_Display * sys_display;
	sys_DispCommon * common;

	if(!SYS_DISPLAY(sys_display) || !SYS_DISPLAY_COMMON(common)){
		return;
	}
	// // so far so good
	if(	common->width == 0 || common->height == 0){
		ESP_LOGE(TAG,"Misconfigured display missing data");
		return;
	}	

	ESP_LOGI(TAG, "Trying to configure display type %s, driver: %s", 
				sys_DeviceTypeEnum_name(sys_display->type),
				sys_DisplayDriverEnum_name(common->driver));
	if (common->has_back && common->back.pin >= 0) {
		struct GDS_BacklightPWM PWMConfig = { .Channel = pwm_system.base_channel++, .Timer = pwm_system.timer, .Max = pwm_system.max, .Init = false	};
		display = GDS_AutoDetect(sys_display, drivers, &PWMConfig);
	} else {
		display = GDS_AutoDetect(sys_display, drivers, NULL);
	}

	if (display) {
		if(common->has_reset){
			RST_pin = common->reset.pin;
		}
		if(common->has_back){
			backlight_pin = common->back.pin;
		}		
		width = common->width;
		height = common->height;

		// Detect driver interface
		if (sys_display->which_dispType == sys_Display_i2c_tag && i2c_system_port != -1){
			int address = 0x3C;
			
			address = sys_display->dispType.i2c.address;
			init = true;
			GDS_I2CInit( i2c_system_port, -1, -1, i2c_system_speed ) ;
			GDS_I2CAttachDevice( display, width, height, address, RST_pin, backlight_pin );
		
			ESP_LOGI(TAG, "Display is I2C on port %u", address);
		} else if (sys_display->which_dispType == sys_Display_spi_tag && spi_system_host != -1) {
			int CS_pin = -1, speed = 0, mode = 0;
			if(sys_display->dispType.spi.has_cs){
				CS_pin = sys_display->dispType.spi.cs.pin;
			}		
			speed = sys_display->dispType.spi.speed;
			
			//todo: what is mode? 

			// PARSE_PARAM(config, "mode", '=', mode);

			// todo: need to handle display offsets 
		
			init = true;
			GDS_SPIInit( spi_system_host, spi_system_dc_gpio );
			GDS_SPIAttachDevice( display, width, height, CS_pin, RST_pin, backlight_pin, speed, mode );
				
			ESP_LOGI(TAG, "Display is SPI host %u with cs:%d", spi_system_host, CS_pin);
		} else {
			display = NULL;
			ESP_LOGI(TAG, "Unsupported display interface or serial link not configured");
		}
	}
	
	
	if (init) {
		static DRAM_ATTR StaticTask_t xTaskBuffer __attribute__ ((aligned (4)));
		static EXT_RAM_ATTR StackType_t xStack[DISPLAYER_STACK_SIZE] __attribute__ ((aligned (4)));
		struct GDS_Layout Layout = {
			.HFlip = platform->dev.display.common.HFlip,
			.VFlip = platform->dev.display.common.VFlip,
			.Rotate = platform->dev.display.common.rotate, 
			.Invert = platform->dev.display.common.invert,
			.ColorSwap = platform->dev.display.common.colow_swap
		};	

		GDS_SetLayout(display, &Layout);
		GDS_SetFont(display, Font_line_2);
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
		
		if(platform->has_services && platform->services.has_metadata){
			displayer.metadata_config = &platform->services.metadata;
		
			// leave room for artwork is display is horizontal-style
			if (displayer.metadata_config->has_artwork && displayer.metadata_config->artwork.enabled) {
				// todo : check for resize flag
				displayer.artwork.enable = true;
				displayer.artwork.fit = true;
				if (height <= 64 && width > height * 2)
					displayer.artwork.offset = width - height - ARTWORK_BORDER;
			}	
		}
		
        
        // and finally register ourselves to power off upon deep sleep
        services_sleep_setsuspend(display_sleep);
	}
	
	
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
	if (strlen(displayer.metadata_config->format)>0) {
		char token[16], *q;
		int space = len;
		bool skip = false;
			
		displayer.string[0] = '\0';	
		p = strchr(displayer.metadata_config->format, '=');
			
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
	if(displayer.metadata_config->speed <=0) displayer.metadata_config->speed= displayer.speed;
	if(displayer.metadata_config->pause <=0) displayer.metadata_config->pause= displayer.pause;
	displayer.metadata_config->speed = displayer.speed;
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
