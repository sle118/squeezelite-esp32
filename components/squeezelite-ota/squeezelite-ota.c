/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "string.h"
#include <stdbool.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "cmd_system.h"
#include "esp_err.h"
#include "squeezelite-ota.h"
#include "tcpip_adapter.h"
// IDF-V4++ #include "esp_netif.h"
#include "platform_config.h"
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "esp_secure_boot.h"
#include "esp_flash_encrypt.h"
#include "esp_spi_flash.h"
#include "sdkconfig.h"
#include "messaging.h"
#include "trace.h"
#include "esp_ota_ops.h"
#include "display.h"
#include "gds.h"
#include "gds_text.h"
#include "gds_draw.h"
#include "led_vu.h"
#include "platform_esp32.h"
#include "lwip/sockets.h"


extern const char * get_certificate();
#define IF_DISPLAY(x) if(display) { x; }

#ifdef CONFIG_ESP32_WIFI_TASK_PINNED_TO_CORE_1
#define OTA_CORE 0
#else
#define OTA_CORE 1
#endif

static const char *TAG = "squeezelite-ota";
esp_http_client_handle_t ota_http_client = NULL;
#define IMAGE_HEADER_SIZE sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t) + 1
#define BUFFSIZE 4096
#define HASH_LEN 32 /* SHA-256 digest length */
typedef struct  {
	char * url;
	char * bin;
	uint32_t length;
} ota_thread_parms_t ;
static ota_thread_parms_t ota_thread_parms;
typedef enum  {
	OTA_TYPE_HTTP,
	OTA_TYPE_BUFFER,
	OTA_TYPE_INVALID
} ota_type_t;

typedef struct  {
	size_t actual_image_len;
	float downloaded_image_len;
	float total_image_len;
	float remain_image_len;
	ota_type_t ota_type;
	char * ota_write_data;
	char * bin_data;
	bool bOTAStarted;
	size_t buffer_size;
	uint8_t lastpct;
	uint8_t newpct;
	uint8_t newdownloadpct;
	struct timeval OTA_start;
	bool bOTAThreadStarted;
    const esp_partition_t *configured;
    const esp_partition_t *running;
    const esp_partition_t * update_partition;
    const esp_partition_t* last_invalid_app ;
    const esp_partition_t * ota_partition;
} ota_status_t;

ota_status_t * ota_status;

struct timeval tv;
static esp_http_client_config_t http_client_config;


void _printMemStats(){
	ESP_LOGD(TAG,"Heap internal:%zu (min:%zu) external:%zu (min:%zu)",
			heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
			heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
			heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
			heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}
uint8_t  ota_get_pct_complete(){
	return ota_status->total_image_len==0?0:
			(uint8_t)((float)ota_status->actual_image_len/ota_status->total_image_len*100.0f);
}
uint8_t  ota_get_pct_downloaded(){
	return ota_status->total_image_len==0?0:
			(uint8_t)(ota_status->downloaded_image_len/ota_status->total_image_len*100.0f);
}
typedef struct  {
	int x1,y1,x2,y2,width,height;
} rect_t;
typedef struct _progress {
	int border_thickness;
	int sides_margin;
	int vertical_margin;
	int bar_tot_height;
	int bar_fill_height;
	rect_t border;
	rect_t filler;
} progress_t;

static progress_t * loc_displayer_get_progress_dft(){

	int start_coord_offset=0;
	static progress_t def={
		.border_thickness = 2,
		.sides_margin = 2,
		.bar_tot_height = 7,
		};
	def.bar_fill_height= def.bar_tot_height-(def.border_thickness*2);
	def.border.x1=start_coord_offset+def.sides_margin;
	IF_DISPLAY(def.border.x2=GDS_GetWidth(display)-def.sides_margin);
	// progress bar will be drawn at the bottom of the display
	IF_DISPLAY(	def.border.y2= GDS_GetHeight(display)-def.border_thickness);
	def.border.y1= def.border.y2-def.bar_tot_height;
	def.border.width=def.border.x2-def.border.x1;
	def.border.height=def.border.y2-def.border.y1;
	def.filler.x1= def.border.x1+def.border_thickness;
	def.filler.x2= def.border.x2-def.border_thickness;
	def.filler.y1= def.border.y1+def.border_thickness;
	def.filler.y2= def.border.y2-def.border_thickness;
	def.filler.width=def.filler.x2-def.filler.x1;
	def.filler.height=def.filler.y2-def.filler.y1;
	assert(def.filler.width>0);
	assert(def.filler.height>0);
	assert(def.border.width>0);
	assert(def.border.height>0);
	assert(def.border.width>def.filler.width);
	assert(def.border.height>def.filler.height);
	return &def;

}
static void loc_displayer_progressbar(uint8_t pct){
	static progress_t * progress_coordinates;
	if(display) {
		if(!progress_coordinates) progress_coordinates = loc_displayer_get_progress_dft();
		int filler_x=progress_coordinates->filler.x1+(int)((float)progress_coordinates->filler.width*(float)pct/(float)100);

		ESP_LOGD(TAG,"Drawing %d,%d,%d,%d",progress_coordinates->border.x1,progress_coordinates->border.y1,progress_coordinates->border.x2,progress_coordinates->border.y2);
		GDS_DrawBox(display,progress_coordinates->border.x1,progress_coordinates->border.y1,progress_coordinates->border.x2,progress_coordinates->border.y2,GDS_COLOR_WHITE,false);
		ESP_LOGD(TAG,"Drawing %d,%d,%d,%d",progress_coordinates->filler.x1,progress_coordinates->filler.y1,filler_x,progress_coordinates->filler.y2);
		if(filler_x > progress_coordinates->filler.x1){
			GDS_DrawBox(display,progress_coordinates->filler.x1,progress_coordinates->filler.y1,filler_x,progress_coordinates->filler.y2,GDS_COLOR_WHITE,true);
		}
		else {
			// Clear the inner box
			GDS_DrawBox(display,progress_coordinates->filler.x1,progress_coordinates->filler.y1,progress_coordinates->filler.x2,progress_coordinates->filler.y2,GDS_COLOR_BLACK,true);
		}
		ESP_LOGD(TAG,"Updating Display");
		GDS_Update(display);
	}
	if (led_display) {
		led_vu_progress_bar(pct, LED_VU_BRIGHT);
	}
}
void sendMessaging(messaging_types type,const char * fmt, ...){
    va_list args;
    cJSON * msg = cJSON_CreateObject();
    size_t str_len=0;
    char * msg_str=NULL;

    va_start(args, fmt);
    str_len = vsnprintf(NULL,0,fmt,args)+1;
    if(str_len>0){
    	msg_str = malloc(str_len);
    	vsnprintf(msg_str,str_len,fmt,args);
        if(type == MESSAGING_WARNING){
        	ESP_LOGW(TAG,"%s",msg_str);
        }
    	else if (type == MESSAGING_ERROR){
    		ESP_LOGE(TAG,"%s",msg_str);
    	}
    	else
    		ESP_LOGI(TAG,"%s",msg_str);
    }
    else {
    	ESP_LOGW(TAG, "Sending empty string message");
    }
    va_end(args);
    if(type!=MESSAGING_INFO){
    	IF_DISPLAY(GDS_TextLine(display, 2, GDS_TEXT_LEFT, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, msg_str));
    }

    cJSON_AddStringToObject(msg,"ota_dsc",str_or_unknown(msg_str));
    free(msg_str);
    cJSON_AddNumberToObject(msg,"ota_pct",	ota_get_pct_complete()	);
    char * json_msg = cJSON_PrintUnformatted(msg);
	messaging_post_message(type, MESSAGING_CLASS_OTA, json_msg);
	free(json_msg);
	cJSON_Delete(msg);
    _printMemStats();
}

static void __attribute__((noreturn)) task_fatal_error(void)
{
    ESP_LOGE(TAG, "Exiting task due to fatal error...");
    (void)vTaskDelete(NULL);

    while (1) {
        ;
    }
}

esp_err_t handle_http_on_data(esp_http_client_event_t *evt){

	int http_status= esp_http_client_get_status_code(evt->client);
	static char * recv_ptr=NULL;

	if(http_status == 200){


		if(!ota_status->bOTAStarted)
		{
			sendMessaging(MESSAGING_INFO,"Downloading firmware");
			ota_status->bOTAStarted = true;
			ota_status->total_image_len=esp_http_client_get_content_length(evt->client);
			ota_status->downloaded_image_len = 0;
			ota_status->newdownloadpct = 0;
		    ota_status->bin_data= malloc(ota_status->total_image_len);
		    if(ota_status->bin_data==NULL){
				sendMessaging(MESSAGING_ERROR,"Error: buffer alloc error");
				return ESP_FAIL;
	   	    }
		    recv_ptr=ota_status->bin_data;
		}

		// we're downloading the binary data file
		if (!esp_http_client_is_chunked_response(evt->client)) {
			memcpy(recv_ptr,evt->data,evt->data_len);
			ota_status->downloaded_image_len +=evt->data_len;
			recv_ptr+=evt->data_len;
		}
		if(ota_get_pct_downloaded()%5 == 0 && ota_get_pct_downloaded()%5!=ota_status->newdownloadpct) {
			ota_status->newdownloadpct= ota_get_pct_downloaded();
			loc_displayer_progressbar(ota_status->newdownloadpct);
			gettimeofday(&tv, NULL);
			uint32_t elapsed_ms= (tv.tv_sec-ota_status->OTA_start.tv_sec )*1000+(tv.tv_usec-ota_status->OTA_start.tv_usec)/1000;
			ESP_LOGI(TAG,"OTA download progress : %f/%f (%d pct), %f KB/s", ota_status->downloaded_image_len, ota_status->total_image_len, ota_status->newdownloadpct, elapsed_ms>0?ota_status->downloaded_image_len*1000/elapsed_ms/1024:0);
			sendMessaging(MESSAGING_INFO,"Downloading firmware %%%3d.",ota_status->newdownloadpct);
		}

	}

	return ESP_OK;
}
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
// --------------
//	Received parameters
//
//	esp_http_client_event_id_tevent_id event_id, to know the cause of the event
//	esp_http_client_handle_t client
//	esp_http_client_handle_t context

//	void *data data of the event

//	int data_len - data length of data
//	void *user_data -- user_data context, from esp_http_client_config_t user_data

//	char *header_key For HTTP_EVENT_ON_HEADER event_id, it�s store current http header key
//	char *header_value For HTTP_EVENT_ON_HEADER event_id, it�s store current http header value
// --------------
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        _printMemStats();
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        if(ota_status->bOTAStarted) sendMessaging(MESSAGING_INFO,"HTTP Connected");
        ota_status->total_image_len=0;
		ota_status->actual_image_len=0;
		ota_status->lastpct=0;
		ota_status->remain_image_len=0;
		ota_status->newpct=0;
		gettimeofday(&ota_status->OTA_start, NULL);
		break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s",evt->header_key, evt->header_value);
//		if (strcasecmp(evt->header_key, "location") == 0) {
//        	ESP_LOGW(TAG,"OTA will redirect to url: %s",evt->header_value);
//        }
//        if (strcasecmp(evt->header_key, "content-length") == 0) {
//        	ota_status->total_image_len = atol(evt->header_value);
//        	 ESP_LOGW(TAG, "Content length found: %s, parsed to %d", evt->header_value, ota_status->total_image_len);
//        }
        break;
    case HTTP_EVENT_ON_DATA:
    	return handle_http_on_data(evt);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}

esp_err_t init_config(ota_thread_parms_t * p_ota_thread_parms){
	memset(&http_client_config, 0x00, sizeof(http_client_config));
	sendMessaging(MESSAGING_INFO,"Initializing...");
	loc_displayer_progressbar(0);
	ota_status->ota_type= OTA_TYPE_INVALID;
	if(p_ota_thread_parms->url !=NULL && strlen(p_ota_thread_parms->url)>0 ){
		ota_status->ota_type= OTA_TYPE_HTTP;
	}
	else if(p_ota_thread_parms->bin!=NULL && p_ota_thread_parms->length > 0) {
		ota_status->ota_type= OTA_TYPE_BUFFER;
	}

	if(  ota_status->ota_type== OTA_TYPE_INVALID ){
		ESP_LOGE(TAG,"HTTP OTA called without a url or a binary buffer");
		return ESP_ERR_INVALID_ARG;
	}

	ota_status->buffer_size = BUFFSIZE;
	ota_status->ota_write_data = heap_caps_malloc(ota_status->buffer_size+1 , (MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
	if(ota_status->ota_write_data== NULL){
			ESP_LOGE(TAG,"Error allocating the ota buffer");
			return ESP_ERR_NO_MEM;
		}
	switch (ota_status->ota_type) {
	case OTA_TYPE_HTTP:
		http_client_config.cert_pem =get_certificate();
		http_client_config.event_handler = _http_event_handler;
		http_client_config.disable_auto_redirect=false;
		http_client_config.skip_cert_common_name_check = false;
		http_client_config.url = strdup(p_ota_thread_parms->url);
		http_client_config.max_redirection_count = 4;
		// buffer size below is for http read chunks
		http_client_config.buffer_size = 8192; //1024 ;
		http_client_config.buffer_size_tx = 8192;
		//http_client_config.timeout_ms = 5000;
		break;
	case OTA_TYPE_BUFFER:
		ota_status->bin_data = p_ota_thread_parms->bin;
		ota_status->total_image_len = p_ota_thread_parms->length;
		break;
	default:
		return ESP_FAIL;
		break;
	}

	return ESP_OK;
}
esp_partition_t * _get_ota_partition(esp_partition_subtype_t subtype){
	esp_partition_t *ota_partition=NULL;
	ESP_LOGD(TAG, "Looking for OTA partition.");

	esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, subtype , NULL);
	if(it == NULL){
		ESP_LOGE(TAG,"Unable initialize partition iterator!");
	}
	else {
		ota_partition = (esp_partition_t *) esp_partition_get(it);
		if(ota_partition != NULL){
			ESP_LOGD(TAG, "Found OTA partition: %s.",ota_partition->label);
		}
		else {
			ESP_LOGE(TAG,"OTA partition not found!  Unable update application.");
		}
		esp_partition_iterator_release(it);
	}
	return ota_partition;

}



esp_err_t _erase_last_boot_app_partition(const esp_partition_t *ota_partition)
{
	uint16_t num_passes=0;
	uint16_t remain_size=0;
	uint32_t single_pass_size=0;
	esp_err_t err=ESP_OK;

    char * ota_erase_size=config_alloc_get(NVS_TYPE_STR, "ota_erase_blk");
	if(ota_erase_size!=NULL) {
		single_pass_size = atol(ota_erase_size);
		ESP_LOGD(TAG,"OTA Erase block size is %d (from string: %s)",single_pass_size, ota_erase_size );
		free(ota_erase_size);
	}
	else {
		ESP_LOGW(TAG,"OTA Erase block config not found");
		single_pass_size = OTA_FLASH_ERASE_BLOCK;
	}

	if(single_pass_size % SPI_FLASH_SEC_SIZE !=0){
		uint32_t temp_single_pass_size = single_pass_size-(single_pass_size % SPI_FLASH_SEC_SIZE);
		ESP_LOGW(TAG,"Invalid erase block size of %u. Value should be a multiple of %d and will be adjusted to %u.", single_pass_size, SPI_FLASH_SEC_SIZE,temp_single_pass_size);
		single_pass_size=temp_single_pass_size;
	}
	ESP_LOGD(TAG,"Erasing flash partition of size %u in blocks of %d bytes", ota_partition->size, single_pass_size);
	num_passes=ota_partition->size/single_pass_size;
	remain_size=ota_partition->size-(num_passes*single_pass_size);
	ESP_LOGI(TAG,"Erasing in %d passes with blocks of %d bytes ", num_passes,single_pass_size);
	for(uint16_t i=0;i<num_passes;i++){
		ESP_LOGD(TAG,"Erasing flash (%u%%)",i/num_passes);
		ESP_LOGD(TAG,"Pass %d of %d, with chunks of %d bytes, from %d to %d", i+1, num_passes,single_pass_size,i*single_pass_size,i*single_pass_size+single_pass_size);
		err=esp_partition_erase_range(ota_partition, i*single_pass_size, single_pass_size);
		if(err!=ESP_OK) return err;
		if(i%2) {
			loc_displayer_progressbar((int)(((float)i/(float)num_passes)*100.0f));
			sendMessaging(MESSAGING_INFO,"Erasing flash (%u/%u)",i,num_passes);
		}
		vTaskDelay(100/ portTICK_PERIOD_MS);  // wait here for a short amount of time.  This will help with reducing WDT errors
	}
	if(remain_size>0){
		err=esp_partition_erase_range(ota_partition, ota_partition->size-remain_size, remain_size);

		if(err!=ESP_OK) return err;
	}
	sendMessaging(MESSAGING_INFO,"Erasing flash complete.");
	loc_displayer_progressbar(100);
	vTaskDelay(200/ portTICK_PERIOD_MS);
	return ESP_OK;
}

void ota_task_cleanup(const char * message, ...){
	ota_status->bOTAThreadStarted=false;
	loc_displayer_progressbar(0);
	if(message!=NULL){
	    va_list args;
	    va_start(args, message);
		sendMessaging(MESSAGING_ERROR,message, args);
	    va_end(args);
		
	    if (led_display) led_vu_color_red(LED_VU_BRIGHT);
	} else {
	    if (led_display) led_vu_color_green(LED_VU_BRIGHT);
	}
	FREE_RESET(ota_status->ota_write_data);
	FREE_RESET(ota_status->bin_data);
	if(ota_http_client!=NULL) {
		esp_http_client_cleanup(ota_http_client);
		ota_http_client=NULL;
	}
	ota_status->bOTAStarted = false;
	task_fatal_error();
}
esp_err_t ota_buffer_all(){
		esp_err_t err=ESP_OK;
	if (ota_status->ota_type == OTA_TYPE_HTTP){
		IF_DISPLAY(GDS_TextLine(display, 2, GDS_TEXT_LEFT, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, "Downloading file"));
		ota_http_client = esp_http_client_init(&http_client_config);
		if (ota_http_client == NULL) {
			sendMessaging(MESSAGING_ERROR,"Error: Failed to initialize HTTP connection.");
			return ESP_FAIL;
		}
	    _printMemStats();
	    err =  esp_http_client_perform(ota_http_client);
		if (err !=  ESP_OK) {
			sendMessaging(MESSAGING_ERROR,"Error: Failed to execute HTTP download. %s",esp_err_to_name(err));
			return ESP_FAIL;
		}

	    if(ota_status->total_image_len<=0){
	    	sendMessaging(MESSAGING_ERROR,"Error: Invalid image length");
	    	return ESP_FAIL;
	    }
	    sendMessaging(MESSAGING_INFO,"Download success");
	}
	else {
		gettimeofday(&ota_status->OTA_start, NULL);
	}
	ota_status->remain_image_len=ota_status->total_image_len;

	return err;
}
int ota_buffer_read(){
	int data_read=0;
	if(ota_status->remain_image_len >ota_status->buffer_size){
		data_read = ota_status->buffer_size;
	} else {
		data_read = ota_status->remain_image_len;
	}
	memcpy(ota_status->ota_write_data, &ota_status->bin_data[ota_status->actual_image_len], data_read);

	ota_status->actual_image_len += data_read;
	ota_status->remain_image_len -= data_read;
	return data_read;
}
esp_err_t ota_header_check(){
	esp_app_desc_t new_app_info;
    esp_app_desc_t running_app_info;

    ota_status->configured = esp_ota_get_boot_partition();
    ota_status->running = esp_ota_get_running_partition();
    ota_status->last_invalid_app= esp_ota_get_last_invalid_partition();
    ota_status->ota_partition = _get_ota_partition(ESP_PARTITION_SUBTYPE_APP_OTA_0);

    ESP_LOGD(TAG, "Running partition [%s] type %d subtype %d (offset 0x%08x)", ota_status->running->label, ota_status->running->type, ota_status->running->subtype, ota_status->running->address);
    if (ota_status->total_image_len > ota_status->ota_partition->size){
    	ota_task_cleanup("Error: Image size (%d) too large to fit in partition (%d).",ota_status->ota_partition->size,ota_status->total_image_len );
        return ESP_FAIL;
	}
	if(ota_status->ota_partition == NULL){
		ESP_LOGE(TAG,"Unable to locate OTA application partition. ");
        ota_task_cleanup("Error: OTA partition not found");
        return ESP_FAIL;
	}
    if (ota_status->configured != ota_status->running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x", ota_status->configured->address, ota_status->running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGD(TAG, "Next ota update partition is: [%s] subtype %d at offset 0x%x",
    		ota_status->update_partition->label, ota_status->update_partition->subtype, ota_status->update_partition->address);

    if (ota_status->total_image_len >= IMAGE_HEADER_SIZE) {
		// check current version with downloading
		memcpy(&new_app_info, &ota_status->bin_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
		ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);
		if (esp_ota_get_partition_description(ota_status->running, &running_app_info) == ESP_OK) {
			ESP_LOGD(TAG, "Running recovery version: %s", running_app_info.version);
		}
		sendMessaging(MESSAGING_INFO,"New version is : %s",new_app_info.version);
		esp_app_desc_t invalid_app_info;
		if (esp_ota_get_partition_description(ota_status->last_invalid_app, &invalid_app_info) == ESP_OK) {
			ESP_LOGD(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
		}

		if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
			ESP_LOGW(TAG, "Current running version is the same as a new.");
		}
		return ESP_OK;
    }
    else{
    	ota_task_cleanup("Error: Binary file too small");
    }
	 return ESP_FAIL;
}

void ota_task(void *pvParameter)
{
	esp_err_t err = ESP_OK;
    int data_read = 0;
    IF_DISPLAY(GDS_TextSetFont(display,2,GDS_GetHeight(display)>32?&Font_droid_sans_fallback_15x17:&Font_droid_sans_fallback_11x13,-2))
    IF_DISPLAY(	GDS_ClearExt(display, true));
	IF_DISPLAY(GDS_TextLine(display, 1, GDS_TEXT_LEFT, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, "Firmware update"));
	IF_DISPLAY(GDS_TextLine(display, 2, GDS_TEXT_LEFT, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, "Initializing"));
	loc_displayer_progressbar(0);
	ESP_LOGD(TAG, "HTTP ota Thread started");
    _printMemStats();

    ota_status->update_partition = esp_ota_get_next_update_partition(NULL);

	ESP_LOGD(TAG,"Initializing OTA configuration");
	err = init_config(pvParameter);
	if(err!=ESP_OK){
		ota_task_cleanup("Error: Failed to initialize OTA.");
		return;
	}

	_printMemStats();
	sendMessaging(MESSAGING_INFO,"Starting OTA...");
	err=ota_buffer_all();
	if(err!=ESP_OK){
		ota_task_cleanup(NULL);
		return;
	}

	if(ota_header_check()!=ESP_OK){
		ota_task_cleanup(NULL);
		return;
	}

	/* Locate and erase ota application partition */
	sendMessaging(MESSAGING_INFO,"Formatting OTA partition");
	ESP_LOGW(TAG,"****************  Expecting WATCHDOG errors below during flash erase. This is OK and not to worry about **************** ");
	IF_DISPLAY(GDS_TextLine(display, 2, GDS_TEXT_LEFT, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, "Formatting partition"));

	_printMemStats();
	err=_erase_last_boot_app_partition(ota_status->ota_partition);
	if(err!=ESP_OK){
		ota_task_cleanup("Error: Unable to erase last APP partition. (%s)",esp_err_to_name(err));
		return;
	}
	loc_displayer_progressbar(0);
	_printMemStats();


	// Call OTA Begin with a small partition size - this minimizes the time spent in erasing partition,
	// which was already done above
    esp_ota_handle_t update_handle = 0 ;
    gettimeofday(&ota_status->OTA_start, NULL);
	err = esp_ota_begin(ota_status->ota_partition, 512, &update_handle);
	if (err != ESP_OK) {
		ota_task_cleanup("esp_ota_begin failed (%s)", esp_err_to_name(err));
		return;
	}
	ESP_LOGD(TAG, "esp_ota_begin succeeded");
	IF_DISPLAY(GDS_TextLine(display, 2, GDS_TEXT_LEFT, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, "Writing image..."));
    while (ota_status->remain_image_len>0) {

    	data_read = ota_buffer_read();
        if (data_read <= 0) {
            ota_task_cleanup("Error: Data read error");
            return;
        } else if (data_read > 0) {
            err = esp_ota_write( update_handle, (const void *)ota_status->ota_write_data, data_read);
            if (err != ESP_OK) {
                ota_task_cleanup("Error: OTA Partition write failure. (%s)",esp_err_to_name(err));
                return;
            }
            ESP_LOGD(TAG, "Written image length %d", ota_status->actual_image_len);

			if(ota_get_pct_complete()%5 == 0) ota_status->newpct = ota_get_pct_complete();
			if(ota_status->lastpct!=ota_status->newpct ) {
				loc_displayer_progressbar(ota_status->newpct);
				gettimeofday(&tv, NULL);
				uint32_t elapsed_ms= (tv.tv_sec-ota_status->OTA_start.tv_sec )*1000+(tv.tv_usec-ota_status->OTA_start.tv_usec)/1000;
				ESP_LOGI(TAG,"OTA progress : %d/%.0f (%d pct), %d KB/s", ota_status->actual_image_len, ota_status->total_image_len, ota_status->newpct, elapsed_ms>0?ota_status->actual_image_len*1000/elapsed_ms/1024:0);
				sendMessaging(MESSAGING_INFO,"Writing binary file %3d %%.",ota_status->newpct);
				ota_status->lastpct=ota_status->newpct;
			}
			taskYIELD();

        } else if (data_read == 0) {
            ESP_LOGD(TAG, "End of OTA data stream");
            break;
        }
    }

    ESP_LOGI(TAG, "Total Write binary data length: %d", ota_status->actual_image_len);
    if (ota_status->total_image_len != ota_status->actual_image_len) {
        ota_task_cleanup("Error: Error in receiving complete file");
        return;
    }
    _printMemStats();
    loc_displayer_progressbar(100);
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ota_task_cleanup("Error: %s",esp_err_to_name(err));
        return;
     }
    _printMemStats();
    err = esp_ota_set_boot_partition(ota_status->ota_partition);
    if (err == ESP_OK) {
    	ESP_LOGI(TAG,"OTA Process completed successfully!");
    	sendMessaging(MESSAGING_INFO,"Success!");
    	IF_DISPLAY(GDS_TextLine(display, 2, GDS_TEXT_LEFT, GDS_TEXT_CLEAR | GDS_TEXT_UPDATE, "Success!"));
    	vTaskDelay(3500/ portTICK_PERIOD_MS);  // wait here to give the UI a chance to refresh
    	IF_DISPLAY(GDS_Clear(display,GDS_COLOR_BLACK));
        esp_restart();
    } else {
        ota_task_cleanup("Error: Unable to update boot partition [%s]",esp_err_to_name(err));
        return;
    }
    ota_task_cleanup(NULL);
    return;
}

esp_err_t process_recovery_ota(const char * bin_url, char * bin_buffer, uint32_t length){
	int ret = 0;
	uint16_t stack_size, task_priority;

	if(ota_status && ota_status->bOTAThreadStarted){
		ESP_LOGE(TAG,"OTA Already started. ");
		return ESP_FAIL;
	}
	ota_status = heap_caps_malloc(sizeof(ota_status_t) , (MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
	memset(ota_status, 0x00, sizeof(ota_status_t));
	ota_status->bOTAThreadStarted=true;

	if(bin_url){
		ota_thread_parms.url =strdup(bin_url);
		ESP_LOGD(TAG, "Starting ota on core %u for : %s", OTA_CORE,ota_thread_parms.url);
	}
	else {
		ota_thread_parms.bin = bin_buffer;
		ota_thread_parms.length = length;
		ESP_LOGD(TAG, "Starting ota on core %u for file upload", OTA_CORE);
	}

    char * num_buffer=config_alloc_get(NVS_TYPE_STR, "ota_stack");
  	if(num_buffer!=NULL) {
  		stack_size= atol(num_buffer);
  		FREE_AND_NULL(num_buffer);
  	}
  	else {
		ESP_LOGW(TAG,"OTA stack size config not found");
  		stack_size = OTA_STACK_SIZE;
  	}
  	num_buffer=config_alloc_get(NVS_TYPE_STR, "ota_prio");
	if(num_buffer!=NULL) {
		task_priority= atol(num_buffer);
		FREE_AND_NULL(num_buffer);
	}
	else {
		ESP_LOGW(TAG,"OTA task priority not found");
		task_priority= OTA_TASK_PRIOTITY;
  	}

  	ESP_LOGD(TAG,"OTA task stack size %d, priority %d (%d %s ESP_TASK_MAIN_PRIO)",stack_size , task_priority, abs(task_priority-ESP_TASK_MAIN_PRIO), task_priority-ESP_TASK_MAIN_PRIO>0?"above":"below");
//    ret=xTaskCreatePinnedToCore(&ota_task, "ota_task", stack_size , (void *)&ota_thread_parms, task_priority, NULL, OTA_CORE);
    ret=xTaskCreate(&ota_task, "ota_task", stack_size , (void *)&ota_thread_parms, task_priority, NULL);
    if (ret != pdPASS)  {
            ESP_LOGE(TAG, "create thread %s failed", "ota_task");
            return ESP_FAIL;
    }
    return ESP_OK;
}

extern void set_lms_server_details(in_addr_t ip, u16_t hport, u16_t cport);

in_addr_t discover_ota_server(int max) {
	struct sockaddr_in d;
	struct sockaddr_in s;
	char buf[32], port_d[] = "JSON", clip_d[] = "CLIP";
	struct pollfd pollinfo;
	uint8_t len;
	uint16_t hport=9000;
	uint16_t cport=9090;

	int disc_sock = socket(AF_INET, SOCK_DGRAM, 0);

	socklen_t enable = 1;
	setsockopt(disc_sock, SOL_SOCKET, SO_BROADCAST, (const void *)&enable, sizeof(enable));

	len = sprintf(buf,"e%s%c%s", port_d, '\0', clip_d) + 1;

	memset(&d, 0, sizeof(d));
	d.sin_family = AF_INET;
	d.sin_port = htons(3483);
	d.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	pollinfo.fd = disc_sock;
	pollinfo.events = POLLIN;

	do {

		ESP_LOGI(TAG,"sending LMS discovery");
		memset(&s, 0, sizeof(s));

		if (sendto(disc_sock, buf, len, 0, (struct sockaddr *)&d, sizeof(d)) < 0) {
			ESP_LOGE(TAG,"error sending discovery");
		}
		else {

			if (poll(&pollinfo, 1, 5000) == 1) {
				char readbuf[64], *p;
				socklen_t slen = sizeof(s);
				memset(readbuf, 0, sizeof(readbuf));
				recvfrom(disc_sock, readbuf, sizeof(readbuf) - 1, 0, (struct sockaddr *)&s, &slen);
				ESP_LOGI(TAG,"got response from: %s:%d - %s", inet_ntoa(s.sin_addr), ntohs(s.sin_port),readbuf);

				if ((p = strstr(readbuf, port_d)) != NULL) {
					p += strlen(port_d);
					hport = atoi(p + 1);
				}

				if ((p = strstr(readbuf, clip_d)) != NULL) {
					p += strlen(clip_d);
					cport = atoi(p + 1);
				}
				server_notify(s.sin_addr.s_addr, hport, cport);
			}
		}

	} while (s.sin_addr.s_addr == 0 && (!max || --max));

	closesocket(disc_sock);

	return s.sin_addr.s_addr;
}

