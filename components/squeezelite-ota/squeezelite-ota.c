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
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "string.h"
#include <stdbool.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "cmd_system.h"
#include "esp_err.h"
#include "tcpip_adapter.h"
#include "squeezelite-ota.h"
#include "nvs_utilities.h"
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>




#include "esp_image_format.h"
#include "esp_secure_boot.h"
#include "esp_flash_encrypt.h"
#include "esp_spi_flash.h"
#include "sdkconfig.h"

#include "esp_ota_ops.h"

#define OTA_FLASH_ERASE_BLOCK (1024*100)
static const char *TAG = "squeezelite-ota";
extern const uint8_t server_cert_pem_start[] asm("_binary_github_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_github_pem_end");
char * cert=NULL;

static struct {
	char status_text[81];
	uint32_t ota_actual_len;
	uint32_t ota_total_len;
	char * redirected_url;
	char * current_url;
	bool bRedirectFound;
	bool bOTAStarted;
	bool bInitialized;
	uint8_t lastpct;
	uint8_t newpct;
	struct timeval OTA_start;
	bool bOTAThreadStarted;

} ota_status;
struct timeval tv;
static esp_http_client_config_t ota_config;

extern void wifi_manager_refresh_ota_json();

void _printMemStats(){
	ESP_LOGD(TAG,"Heap internal:%zu (min:%zu) external:%zu (min:%zu)",
			heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
			heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
			heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
			heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}
void triggerStatusJsonRefresh(bool bDelay,const char * status, ...){
    va_list args;
    va_start(args, status);
    vsnprintf(ota_status.status_text,sizeof(ota_status.status_text)-1,status, args);
    va_end(args);
    _printMemStats();
	wifi_manager_refresh_ota_json();
	if(bDelay){
		ESP_LOGD(TAG,"Holding task...");
	    vTaskDelay(200 / portTICK_PERIOD_MS);  // wait here for a short amount of time.  This will help with refreshing the UI status
		ESP_LOGD(TAG,"Done holding task...");
	}
	else
	{
		ESP_LOGI(TAG,"%s",ota_status.status_text);
		taskYIELD();
	}
}
const char * ota_get_status(){
	if(!ota_status.bInitialized)
		{
			memset(ota_status.status_text, 0x00,sizeof(ota_status.status_text));
			ota_status.bInitialized = true;
		}
	return ota_status.status_text;
}
uint8_t  ota_get_pct_complete(){
	return ota_status.ota_total_len==0?0:
			(uint8_t)((float)ota_status.ota_actual_len/(float)ota_status.ota_total_len*100.0f);
}

static void __attribute__((noreturn)) task_fatal_error(void)
{
    ESP_LOGE(TAG, "Exiting task due to fatal error...");
    (void)vTaskDelete(NULL);

    while (1) {
        ;
    }
}
#define FREE_RESET(p) if(p!=NULL) { free(p); p=NULL; }
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

//	char *header_key For HTTP_EVENT_ON_HEADER event_id, it’s store current http header key
//	char *header_value For HTTP_EVENT_ON_HEADER event_id, it’s store current http header value
// --------------
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        _printMemStats();
        //strncpy(ota_status,"HTTP_EVENT_ERROR",sizeof(ota_status)-1);
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");

        if(ota_status.bOTAStarted) triggerStatusJsonRefresh(true,"Installing...");
        ota_status.ota_total_len=0;
		ota_status.ota_actual_len=0;
		ota_status.lastpct=0;
		ota_status.newpct=0;
		gettimeofday(&ota_status.OTA_start, NULL);

			break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, status_code=%d, key=%s, value=%s",esp_http_client_get_status_code(evt->client),evt->header_key, evt->header_value);
		if (strcasecmp(evt->header_key, "location") == 0) {
			FREE_RESET(ota_status.redirected_url);
        	ota_status.redirected_url=strdup(evt->header_value);
        	ESP_LOGW(TAG,"OTA will redirect to url: %s",ota_status.redirected_url);
        	ota_status.bRedirectFound= true;
        }
        if (strcasecmp(evt->header_key, "content-length") == 0) {
        	ota_status.ota_total_len = atol(evt->header_value);
        	 ESP_LOGW(TAG, "Content length found: %s, parsed to %d", evt->header_value, ota_status.ota_total_len);
        }
        break;
    case HTTP_EVENT_ON_DATA:
    	if(!ota_status.bOTAStarted)  {
    		ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, status_code=%d, len=%d",esp_http_client_get_status_code(evt->client), evt->data_len);
    	}
    	else if(ota_status.bOTAStarted && esp_http_client_get_status_code(evt->client) == 200 ){
			ota_status.ota_actual_len+=evt->data_len;
			if(ota_get_pct_complete()%5 == 0) ota_status.newpct = ota_get_pct_complete();
			if(ota_status.lastpct!=ota_status.newpct )
			{
				gettimeofday(&tv, NULL);
				uint32_t elapsed_ms= (tv.tv_sec-ota_status.OTA_start.tv_sec )*1000+(tv.tv_usec-ota_status.OTA_start.tv_usec)/1000;
				ESP_LOGI(TAG,"OTA progress : %d/%d (%d pct), %d KB/s", ota_status.ota_actual_len, ota_status.ota_total_len, ota_status.newpct, elapsed_ms>0?ota_status.ota_actual_len*1000/elapsed_ms/1024:0);
				wifi_manager_refresh_ota_json();
				ota_status.lastpct=ota_status.newpct;
			}
        }
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

esp_err_t init_config(esp_http_client_config_t * conf, const char * url){
	memset(conf, 0x00, sizeof(esp_http_client_config_t));

	conf->cert_pem =cert==NULL?(char *)server_cert_pem_start:cert;
	conf->event_handler = _http_event_handler;
	conf->buffer_size = 2048*4;
	conf->disable_auto_redirect=true;
	conf->skip_cert_common_name_check = false;
	conf->url = strdup(url);
	conf->max_redirection_count = 0;

	return ESP_OK;
}
esp_err_t _erase_last_boot_app_partition(void)
{
	uint16_t num_passes=0;
	uint16_t remain_size=0;
    const esp_partition_t *ota_partition=NULL;
    const esp_partition_t *ota_data_partition=NULL;
	esp_err_t err=ESP_OK;

    ESP_LOGI(TAG, "Looking for OTA partition.");
	esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 , NULL);
	if(it == NULL){
		ESP_LOGE(TAG,"Unable initialize partition iterator!");
	}
	else {
		ota_partition = (esp_partition_t *) esp_partition_get(it);
		if(ota_partition != NULL){
			ESP_LOGI(TAG, "Found OTA partition.");
		}
		else {
			ESP_LOGE(TAG,"OTA partition not found!  Unable update application.");
		}
		esp_partition_iterator_release(it);
	}

	it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA , NULL);
	if(it == NULL){
		ESP_LOGE(TAG,"Unable initialize partition iterator!");
	}
	else {
		ota_data_partition = (esp_partition_t *) esp_partition_get(it);

		if(ota_data_partition != NULL){
			ESP_LOGI(TAG, "Found OTA data partition.");
		}
		else {
			ESP_LOGE(TAG,"OTA data partition not found!  Unable update application.");
		}
		esp_partition_iterator_release(it);
	}

	if(ota_data_partition==NULL || ota_partition==NULL){
		return ESP_FAIL;
	}
	ESP_LOGI(TAG,"Erasing flash ");
	num_passes=ota_partition->size/OTA_FLASH_ERASE_BLOCK;
	remain_size=ota_partition->size-(num_passes*OTA_FLASH_ERASE_BLOCK);

	for(uint16_t i=0;i<num_passes;i++){
		err=esp_partition_erase_range(ota_partition, 0, ota_partition->size);
		ESP_LOGD(TAG,"Erasing flash (%u%%)",i/num_passes);
		triggerStatusJsonRefresh(i%5==0?true:false,"Erasing flash (%u/%u)",i,num_passes);
		taskYIELD();
		if(err!=ESP_OK) return err;
	}
	if(remain_size>0){
		err=esp_partition_erase_range(ota_partition, ota_partition->size-remain_size, remain_size);
		if(err!=ESP_OK) return err;
	}
	triggerStatusJsonRefresh(false,"Erasing flash (100%%)");
	taskYIELD();
	return ESP_OK;
}

void ota_task(void *pvParameter)
{
	char * passedURL=(char *)pvParameter;

	ota_status.bInitialized = true;
	ESP_LOGD(TAG, "HTTP ota Thread started");
	triggerStatusJsonRefresh(true,"Initializing...");
	ota_status.bRedirectFound=false;
	if(passedURL==NULL || strlen(passedURL)==0){
		ESP_LOGE(TAG,"HTTP OTA called without a url");
		triggerStatusJsonRefresh(true,"Updating needs a URL!");
		ota_status.bOTAThreadStarted=false;
		vTaskDelete(NULL);
		return ;
	}
	ota_status.current_url= strdup(passedURL);
	FREE_RESET(pvParameter);

	ESP_LOGW(TAG,"****************  Expecting WATCHDOG errors below during flash erase. This is OK and not to worry about **************** ");
	triggerStatusJsonRefresh(true,"Erasing OTA partition");
	esp_err_t err=_erase_last_boot_app_partition();
	if(err!=ESP_OK){
		ESP_LOGE(TAG,"Unable to erase last APP partition. Error: %s",esp_err_to_name(err));
		FREE_RESET(ota_status.current_url);
		FREE_RESET(ota_status.redirected_url);

	    vTaskDelete(NULL);
	}

	ESP_LOGI(TAG,"Calling esp_https_ota");
	init_config(&ota_config,ota_status.bRedirectFound?ota_status.redirected_url:ota_status.current_url);
	ota_status.bOTAStarted = true;
	triggerStatusJsonRefresh(true,"Starting OTA...");
	err = esp_https_ota(&ota_config);
    if (err == ESP_OK) {
    	triggerStatusJsonRefresh(true,"Success!");
        esp_restart();
    } else {
    	triggerStatusJsonRefresh(true,"Error: %s",esp_err_to_name(err));
    	wifi_manager_refresh_ota_json();
        ESP_LOGE(TAG, "Firmware upgrade failed with error : %s", esp_err_to_name(err));
        ota_status.bOTAThreadStarted=false;
    }
	FREE_RESET(ota_status.current_url);
	FREE_RESET(ota_status.redirected_url);

    vTaskDelete(NULL);
}

esp_err_t process_recovery_ota(const char * bin_url){
	int ret = 0;
    if(ota_status.bOTAThreadStarted){
		ESP_LOGE(TAG,"OTA Already started. ");
		return ESP_FAIL;
	}
	memset(&ota_status, 0x00, sizeof(ota_status));
	ota_status.bOTAThreadStarted=true;
    char * urlPtr=strdup(bin_url);
	// the first thing we need to do here is to erase the firmware url
	// to avoid a boot loop

#ifdef CONFIG_ESP32_WIFI_TASK_PINNED_TO_CORE_1
#define OTA_CORE 0
#warning "OTA will run on core 0"
#else
#warning "OTA will run on core 1"
#define OTA_CORE 1
#endif
    ESP_LOGI(TAG, "Starting ota on core %u for : %s", OTA_CORE,urlPtr);
    ret=xTaskCreatePinnedToCore(&ota_task, "ota_task", 1024*20, (void *)urlPtr, ESP_TASK_MAIN_PRIO+1, NULL, OTA_CORE);
    if (ret != pdPASS)  {
            ESP_LOGI(TAG, "create thread %s failed", "ota_task");
            return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t start_ota(const char * bin_url, bool bFromAppMain)
{
//	uint8_t * get_nvs_value_alloc_default(NVS_TYPE_BLOB, "certs", server_cert_pem_start , server_cert_pem_end-server_cert_pem_start);
#if RECOVERY_APPLICATION
	return process_recovery_ota(bin_url);
#else
		ESP_LOGW(TAG, "Called to update the firmware from url: %s",bin_url);
		store_nvs_value(NVS_TYPE_STR, "fwurl", bin_url);
		ESP_LOGW(TAG, "Rebooting to recovery to complete the installation");
	return guided_factory();
	return ESP_OK;
#endif
}
