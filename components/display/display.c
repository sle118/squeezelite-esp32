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
#include "platform_config.h"
#include "tools.h"
#include "display.h"
#include "gds.h"
#include "gds_default_if.h"
#include "gds_draw.h"
#include "gds_text.h"
#include "gds_font.h"

static const char *TAG = "display";

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))

#define DISPLAYER_STACK_SIZE 	(3*1024)
#define SCROLLABLE_SIZE			384
#define HEADER_SIZE				64
#define	DEFAULT_SLEEP			3600

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
	uint32_t elapsed, duration;
	TickType_t tick;
} displayer;

static const char *known_drivers[] = {"SH1106",
		"SSD1306",
		"SSD1322",
		"SSD1326",
		"SSD1327",
		"SSD1675",
		"SSD1351",
		"ST77xx",
		"ILI9341",
		NULL
	};
static void displayer_task(void *args);

struct GDS_Device *display;   
extern GDS_DetectFunc SSD1306_Detect, SSD132x_Detect, SH1106_Detect, SSD1675_Detect, SSD1322_Detect, SSD1351_Detect, ST77xx_Detect;
GDS_DetectFunc *drivers[] = { SH1106_Detect, SSD1306_Detect, SSD132x_Detect, SSD1675_Detect, SSD1322_Detect, SSD1351_Detect, ST77xx_Detect, NULL };

/****************************************************************************************
 * 
 */
void display_init(char *welcome) {
	bool init = false;
	char *config = config_alloc_get_str("display_config", CONFIG_DISPLAY_CONFIG, "N/A");
	
	int width = -1, height = -1, backlight_pin = -1;
	char *p, *drivername = strstr(config, "driver");

	if ((p = strcasestr(config, "width")) != NULL) width = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "height")) != NULL) height = atoi(strchr(p, '=') + 1);
	if ((p = strcasestr(config, "back")) != NULL) backlight_pin = atoi(strchr(p, '=') + 1);	
		
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
		if ((p = strcasestr(config, "reset")) != NULL) RST_pin = atoi(strchr(p, '=') + 1);
		
		// Detect driver interface
		if (strstr(config, "I2C") && i2c_system_port != -1) {
			int address = 0x3C;
				
			if ((p = strcasestr(config, "address")) != NULL) address = atoi(strchr(p, '=') + 1);
				
			init = true;
			GDS_I2CInit( i2c_system_port, -1, -1, i2c_system_speed ) ;
			GDS_I2CAttachDevice( display, width, height, address, RST_pin, backlight_pin );
		
			ESP_LOGI(TAG, "Display is I2C on port %u", address);
		} else if (strstr(config, "SPI") && spi_system_host != -1) {
			int CS_pin = -1, speed = 0;
		
			if ((p = strcasestr(config, "cs")) != NULL) CS_pin = atoi(strchr(p, '=') + 1);
			if ((p = strcasestr(config, "speed")) != NULL) speed = atoi(strchr(p, '=') + 1);
		
			init = true;
			GDS_SPIInit( spi_system_host, spi_system_dc_gpio );
			GDS_SPIAttachDevice( display, width, height, CS_pin, RST_pin, backlight_pin, speed );
				
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
		
		GDS_SetLayout( display, strcasestr(config, "HFlip"), strcasestr(config, "VFlip"), strcasestr(config, "rotate"));
		GDS_SetFont(display, &Font_droid_sans_fallback_15x17 );
		GDS_TextPos(display, GDS_FONT_MEDIUM, GDS_TEXT_CENTERED, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, welcome);

		// start the task that will handle scrolling & counting
		displayer.mutex = xSemaphoreCreateMutex();
		displayer.by = 2;
		displayer.pause = 3600;
		displayer.speed = 33;
		displayer.task = xTaskCreateStatic( (TaskFunction_t) displayer_task, "displayer_thread", DISPLAYER_STACK_SIZE, NULL, ESP_TASK_PRIO_MIN + 1, xStack, &xTaskBuffer);
		
		// set lines for "fixed" text mode
		GDS_TextSetFontAuto(display, 1, GDS_FONT_LINE_1, -3);
		GDS_TextSetFontAuto(display, 2, GDS_FONT_LINE_2, -3);
		
		displayer.metadata_config = config_alloc_get(NVS_TYPE_STR, "metadata_config");
	}
	
	free(config);
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
			char counter[16];
			TickType_t tick = xTaskGetTickCount();
			uint32_t elapsed = (tick - displayer.tick) * portTICK_PERIOD_MS;
			
			if (elapsed >= 1000) {
				xSemaphoreTake(displayer.mutex, portMAX_DELAY);
				displayer.tick = tick;
				displayer.elapsed += elapsed / 1000;
				xSemaphoreGive(displayer.mutex);				
				if (displayer.elapsed < 3600) snprintf(counter, 16, "%5u:%02u", displayer.elapsed / 60, displayer.elapsed % 60);
				else snprintf(counter, 16, "%2u:%02u:%02u", displayer.elapsed / 3600, (displayer.elapsed % 3600) / 60, displayer.elapsed % 60);
				GDS_TextLine(display, 1, GDS_TEXT_RIGHT, (GDS_TEXT_CLEAR | GDS_TEXT_CLEAR_EOL) | GDS_TEXT_UPDATE, counter);
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
	if ((p = strcasestr(displayer.metadata_config, "speed")) != NULL) sscanf(p, "%*[^=]=%d", &displayer.speed);
	if ((p = strcasestr(displayer.metadata_config, "pause")) != NULL) sscanf(p, "%*[^=]=%d", &displayer.pause);
	
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

	if (elapsed >= 0) displayer.elapsed = elapsed / 1000;	
	if (duration >= 0) displayer.duration = duration / 1000;
	if (displayer.timer) displayer.tick = xTaskGetTickCount();
		
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
		strncpy(displayer.header, header, HEADER_SIZE);
		displayer.header[HEADER_SIZE] = '\0';
		displayer.state = DISPLAYER_ACTIVE;
		displayer.timer = false;
		displayer.refresh = true;
		displayer.string[0] = '\0';
		displayer.elapsed = displayer.duration = 0;
		displayer.offset = displayer.boundary = 0;
		display_bus(&displayer, DISPLAY_BUS_TAKE);
		vTaskResume(displayer.task);
		break;
	}	
	case DISPLAYER_SUSPEND:		
		// task will display the line 2 from beginning and suspend
		displayer.state = DISPLAYER_IDLE;
		display_bus(&displayer, DISPLAY_BUS_GIVE);
		break;		
	case DISPLAYER_SHUTDOWN:
		// let the task self-suspend (we might be doing i2c_write)
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
bool display_is_valid_driver(char * driver){
	return display_conf_get_driver_name(driver)!=NULL;
}

/****************************************************************************************
 *
 */
const char *display_conf_get_driver_name(char * driver){
	for(uint8_t i=0;known_drivers[i]!=NULL && strlen(known_drivers[i])>0;i++ ){
		if(strcasestr(driver,known_drivers[i])){
			return known_drivers[i];
		}
	}
	return NULL;
}
