	/**
 *
 */
#include <stdlib.h> // Required for libtelnet.h
#include <esp_log.h>
#include "stdbool.h"
#include <lwip/def.h>
#include <lwip/sockets.h>
#include <errno.h>
#include <string.h>
#include "esp_app_trace.h"
#include "esp_attr.h"
#include "config.h"
// #include "nvs_utilities.h"

#include "platform_esp32.h"
#include "messaging.h"
#include "tools.h"
/************************************
 * Globals
 */

const static char tag[] = "messaging";
typedef struct {
	struct messaging_list_t * next;
	char * subscriber_name;
	size_t max_count;
	RingbufHandle_t buf_handle;
} messaging_list_t;
static messaging_list_t top;
#define MSG_LENGTH_AVG 1024

messaging_list_t * get_struct_ptr(messaging_handle_t handle){
	return (messaging_list_t *)handle;
}
messaging_handle_t  get_handle_ptr(messaging_list_t * handle){
	return (messaging_handle_t )handle;
}

RingbufHandle_t messaging_create_ring_buffer(uint8_t max_count){
	RingbufHandle_t buf_handle = NULL;
	StaticRingbuffer_t *buffer_struct = malloc_init_external(sizeof(StaticRingbuffer_t));
	if (buffer_struct != NULL) {
		size_t buf_size = (size_t )(sizeof(single_message_t)+8+MSG_LENGTH_AVG)*(size_t )(max_count>0?max_count:5); // no-split buffer requires an additional 8 bytes
		buf_size = buf_size - (buf_size % 4);
		uint8_t *buffer_storage = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
		if (buffer_storage== NULL) {
			ESP_LOGE(tag,"buff alloc failed");
		}
		else {
			buf_handle = xRingbufferCreateStatic(buf_size, RINGBUF_TYPE_NOSPLIT, buffer_storage, buffer_struct);
		}
	}
	else {
		ESP_LOGE(tag,"ringbuf alloc failed");
	}
	return buf_handle;
}
void messaging_fill_messages(messaging_list_t * target_subscriber){
	single_message_t * message=NULL;
    UBaseType_t uxItemsWaiting;

    vRingbufferGetInfo(top.buf_handle, NULL, NULL, NULL, NULL, &uxItemsWaiting);
    for(size_t i=0;i<uxItemsWaiting;i++){
    	message= messaging_retrieve_message(top.buf_handle);
    	if(message){
			//re-post to original queue so it is available to future subscribers
			messaging_post_to_queue(get_handle_ptr(&top), message, message->msg_size);
			// post to new subscriber
			messaging_post_to_queue(get_handle_ptr(target_subscriber) , message, message->msg_size);
			FREE_AND_NULL(message);
    	}
    }
}
messaging_handle_t messaging_register_subscriber(uint8_t max_count, char * name){
	messaging_list_t * cur=&top;
	while(cur->next){
		cur = get_struct_ptr(cur->next);
	}
	cur->next=malloc_init_external(sizeof(messaging_list_t));
	if(!cur->next){
		ESP_LOGE(tag,"subscriber alloc failed");
		return NULL;
	}
	memset(cur->next,0x00,sizeof(messaging_list_t));
	cur = get_struct_ptr(cur->next);
	cur->max_count=max_count;
	cur->subscriber_name=strdup_psram(name);
	cur->buf_handle = messaging_create_ring_buffer(max_count);
	if(cur->buf_handle){
		messaging_fill_messages(cur);
	}
	return cur->buf_handle;
}
void messaging_service_init(){
	size_t max_count=15;
	top.buf_handle = messaging_create_ring_buffer(max_count);
	if(!top.buf_handle){
		ESP_LOGE(tag, "messaging service init failed.");
	}
	else {
		top.max_count = max_count;
		top.subscriber_name = strdup_psram("messaging");
	}
	return;
}

const char * messaging_get_type_desc(messaging_types msg_type){
	switch (msg_type) {
	CASE_TO_STR(MESSAGING_INFO);
	CASE_TO_STR(MESSAGING_WARNING);
	CASE_TO_STR(MESSAGING_ERROR);
		default:
			return "Unknown";
			break;
	}
}
const char * messaging_get_class_desc(messaging_classes msg_class){
	switch (msg_class) {
	CASE_TO_STR(MESSAGING_CLASS_OTA);
	CASE_TO_STR(MESSAGING_CLASS_SYSTEM);
	CASE_TO_STR(MESSAGING_CLASS_STATS);
	CASE_TO_STR(MESSAGING_CLASS_CFGCMD);
	CASE_TO_STR(MESSAGING_CLASS_BT);
		default:
			return "Unknown";
			break;
	}
}

cJSON *  messaging_retrieve_messages(RingbufHandle_t buf_handle){
	single_message_t * message=NULL;
	cJSON * json_messages=cJSON_CreateArray();
	cJSON * json_message=NULL;
	size_t item_size;
    UBaseType_t uxItemsWaiting;
    vRingbufferGetInfo(buf_handle, NULL, NULL, NULL, NULL, &uxItemsWaiting);
	for(int i = 0;i<uxItemsWaiting;i++){
		message = (single_message_t *)xRingbufferReceive(buf_handle, &item_size, pdMS_TO_TICKS(50));
		//Check received data
		if (message== NULL) {
			ESP_LOGE(tag,"received null ptr");
		}
		else {
			json_message = cJSON_CreateObject();
			cJSON_AddStringToObject(json_message, "message", message->message);
			cJSON_AddStringToObject(json_message, "type", messaging_get_type_desc(message->type));
			cJSON_AddStringToObject(json_message, "class", messaging_get_class_desc(message->msg_class));
			cJSON_AddNumberToObject(json_message,"sent_time",message->sent_time);
			cJSON_AddNumberToObject(json_message,"current_time",esp_timer_get_time() / 1000);
			cJSON_AddItemToArray(json_messages,json_message);
			vRingbufferReturnItem(buf_handle, (void *)message);
		}
	}
	return json_messages;
}
single_message_t *  messaging_retrieve_message(RingbufHandle_t buf_handle){
	single_message_t * message=NULL;
	single_message_t * message_copy=NULL;
	size_t item_size;
    UBaseType_t uxItemsWaiting;
    vRingbufferGetInfo(buf_handle, NULL, NULL, NULL, NULL, &uxItemsWaiting);
	if(uxItemsWaiting>0){
		message = (single_message_t *)xRingbufferReceive(buf_handle, &item_size, pdMS_TO_TICKS(50));
		message_copy  = clone_obj_psram(message,item_size);
		vRingbufferReturnItem(buf_handle, (void *)message);
	}
	return message_copy;
}

esp_err_t messaging_post_to_queue(messaging_handle_t subscriber_handle, single_message_t * message, size_t message_size){
	size_t item_size=0;
	messaging_list_t * subscriber=get_struct_ptr(subscriber_handle);
	if(!subscriber->buf_handle){
		ESP_LOGE(tag,"post failed: null buffer for %s", str_or_unknown(subscriber->subscriber_name));
		return ESP_FAIL;
	}
	void * pItem=NULL;
	UBaseType_t res=pdFALSE;
	while(1){
		ESP_LOGD(tag,"Attempting to reserve %d bytes for %s",message_size, str_or_unknown(subscriber->subscriber_name));
		res =  xRingbufferSendAcquire(subscriber->buf_handle, &pItem, message_size, pdMS_TO_TICKS(50));
		if(res == pdTRUE && pItem){
			ESP_LOGD(tag,"Reserving complete for %s", str_or_unknown(subscriber->subscriber_name));
			memcpy(pItem,message,message_size);
			xRingbufferSendComplete(subscriber->buf_handle, pItem);
			break;
		}
		ESP_LOGD(tag,"Dropping for %s",str_or_unknown(subscriber->subscriber_name));
		single_message_t * dummy = (single_message_t *)xRingbufferReceive(subscriber->buf_handle, &item_size, pdMS_TO_TICKS(50));
		if (dummy== NULL) {
			ESP_LOGE(tag,"Dropping message failed");
			break;
		}
		else {
			ESP_LOGD(tag,"Dropping message of %d bytes for %s",item_size, str_or_unknown(subscriber->subscriber_name));
			vRingbufferReturnItem(subscriber->buf_handle, (void *)dummy);
		}
	}
	if (res != pdTRUE) {
		ESP_LOGE(tag,"post to %s failed",str_or_unknown(subscriber->subscriber_name));
		return ESP_FAIL;
	}
	return ESP_OK;
}
	esp_err_t messaging_type_to_err_type(messaging_types type){
		switch (type) {
		case MESSAGING_INFO:
			return ESP_LOG_INFO;
			break;
		case MESSAGING_ERROR:
			return ESP_LOG_ERROR;
			break;
		case MESSAGING_WARNING:
			return ESP_LOG_WARN;
			break;
		default:
			return ESP_LOG_DEBUG;
			break;
		}
		return ESP_LOG_DEBUG;
	}
    
void messaging_post_message(messaging_types type,messaging_classes msg_class, const char *fmt, ...){
    va_list va;
	va_start(va, fmt);
    vmessaging_post_message(type, msg_class, fmt, va);
    va_end(va);
}
    
void vmessaging_post_message(messaging_types type,messaging_classes msg_class, const char *fmt, va_list va){    
	single_message_t * message=NULL;
	size_t msg_size=0;
	size_t ln =0;
	messaging_list_t * cur=&top;
	ln = vsnprintf(NULL, 0, fmt, va)+1;
	msg_size = sizeof(single_message_t)+ln;
	message = (single_message_t *)malloc_init_external(msg_size);
	vsprintf(message->message, fmt, va);
	message->msg_size = msg_size;
	message->type = type;
	message->msg_class = msg_class;
	message->sent_time = esp_timer_get_time() / 1000;
	if(type==MESSAGING_WARNING) {
		ESP_LOGW(tag,"%s",message->message);	
	}
	else if(type==MESSAGING_ERROR) {
		ESP_LOGE(tag,"%s",message->message);	
	}
	else {
		ESP_LOGD(tag,"Post: %s",message->message);
	}

	while(cur){
		messaging_post_to_queue(get_handle_ptr(cur),  message, msg_size);
		cur = get_struct_ptr(cur->next);
	}
	FREE_AND_NULL(message);
	return;

}
char * messaging_alloc_format_string(const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	size_t ln = vsnprintf(NULL, 0, fmt, va)+1;
	char * message_txt = malloc_init_external(ln);
	if(message_txt){
		vsprintf(message_txt, fmt, va);
		va_end(va);
	}
	else{
		ESP_LOGE(tag, "Memory allocation failed while sending message");
	}
	return message_txt;
}
void log_send_messaging(messaging_types msgtype,const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	size_t ln = vsnprintf(NULL, 0, fmt, va)+1;
	char * message_txt = malloc_init_external(ln);
	if(message_txt){
		vsprintf(message_txt, fmt, va);
		va_end(va);
		ESP_LOG_LEVEL_LOCAL(messaging_type_to_err_type(msgtype),tag, "%s",message_txt);
		messaging_post_message(msgtype, MESSAGING_CLASS_SYSTEM, message_txt );
		free(message_txt);
	}
	else{
		ESP_LOGE(tag, "Memory allocation failed while sending message");
	}
}

void cmd_send_messaging(const char * cmdname,messaging_types msgtype, const char *fmt, ...){
	va_list va;
	va_start(va, fmt);
	size_t cmd_len = strlen(cmdname)+1;
	size_t ln = vsnprintf(NULL, 0, fmt, va)+1;
	char * message_txt = malloc_init_external(ln+cmd_len);
	if(message_txt){
		strcpy(message_txt,cmdname);
		strcat(message_txt,"\n");
		vsprintf((message_txt+cmd_len), fmt, va);
		va_end(va);
		ESP_LOG_LEVEL_LOCAL(messaging_type_to_err_type(msgtype),tag, "%s",message_txt);
		messaging_post_message(msgtype, MESSAGING_CLASS_CFGCMD, message_txt );
		free(message_txt);
	}
	else{
		ESP_LOGE(tag, "Memory allocation failed while sending message");
	}
}
