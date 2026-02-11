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
#include <setjmp.h>
#include "squeezelite.h"
#include "pthread.h"
#include "esp_pthread.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "monitor.h"
#include "Config.h"
#include "messaging.h"
#include "gpio_exp.h"
#include "accessors.h"

static const char TAG[] = "embedded";
static sys_gpio_config * power=NULL;

extern void sb_controls_init(void);
extern bool sb_displayer_init(void);

u8_t custom_player_id = 12;

mutex_type slimp_mutex;
static jmp_buf jumpbuf;

void get_mac(u8_t mac[]) {
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

_sig_func_ptr signal(int sig, _sig_func_ptr func) {
	return NULL;
}

void em_logprint(const char *fmt, ...) {
    va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);    
    vmessaging_post_message(MESSAGING_ERROR, MESSAGING_CLASS_SYSTEM, fmt, args); 
	va_end(args);
	fflush(stderr);
}

void *audio_calloc(size_t nmemb, size_t size) {
	return calloc(nmemb, size);
}

int	pthread_create_name(pthread_t *thread, _CONST pthread_attr_t  *attr, 
				   void *(*start_routine)( void * ), void *arg, char *name) {
	esp_pthread_cfg_t cfg = esp_pthread_get_default_config(); 
	cfg.thread_name = name; 
	cfg.inherit_cfg = true; 
	esp_pthread_set_cfg(&cfg); 
	return pthread_create(thread, attr, start_routine, arg);
}

uint32_t _gettime_ms_(void) {
	return (uint32_t) (esp_timer_get_time() / 1000);
}

int embedded_init(void) {
	mutex_create(slimp_mutex);
	ESP_LOGI(TAG,"Initializing controls");
	sb_controls_init();
	ESP_LOGI(TAG,"Initializing displayer");
	custom_player_id = sb_displayer_init() ? 100 : 101;

	
	if(SYS_GPIOS_NAME(power,power) && power->pin >= 0){
		gpio_pad_select_gpio_x(power->pin);
		gpio_set_direction_x(power->pin, GPIO_MODE_OUTPUT);
		gpio_set_level_x(power->pin, !power->level);
		ESP_LOGI(TAG, "setting power GPIO %d (active:%d)", power->pin, power->level);	
	}	    
    
    return setjmp(jumpbuf);
}

void embedded_exit(int code) {
    longjmp(jumpbuf, code + 1);
}    

void powering(bool on) {
    if (SYS_GPIOS_NAME(power,power) && power->pin != -1) {
        ESP_LOGI(TAG, "powering player %s", on ? "ON" : "OFF");	
        gpio_set_level_x(power->pin, on ? power->level : !power->level);
    }

}

u16_t get_RSSI(void) {
    wifi_ap_record_t wifidata;
    esp_wifi_sta_get_ap_info(&wifidata);
	// we'll assume dBm, -30 to -100
    if (wifidata.primary != 0) return 100 + wifidata.rssi + 30;
    else return 0xffff;
}	

u16_t get_plugged(void) {
    return jack_inserted_svc() ? PLUG_HEADPHONE : 0;
}

u16_t get_battery(void) {
	return (u16_t) (battery_value_svc() * 128) & 0x0fff;
}	 

void set_name(char *name) {
	strncpy(platform->names.squeezelite,name,sizeof(platform->names.squeezelite));
}
