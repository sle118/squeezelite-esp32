/*
Copyright (c) 2017-2019 Tony Pottier

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

@file http_server.c
@author Tony Pottier
@brief Defines all functions necessary for the HTTP server to run.

Contains the freeRTOS task for the HTTP listener and all necessary support
function to process requests, decode URLs, serve files, etc. etc.

@note http_server task cannot run without the wifi_manager task!
@see https://idyl.io
@see https://github.com/tonyp7/esp32-wifi-manager
*/

#include "http_server_handlers.h"

#include "esp_http_server.h"
#include "cmd_system.h"
#include <inttypes.h>
#include "squeezelite-ota.h"
#include "nvs_utilities.h"
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform_config.h"
#include "sys/param.h"
#include "esp_vfs.h"
#include "messaging.h"
#include "platform_esp32.h"
#include "trace.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "platform_console.h"
#include "accessors.h"
 
#define HTTP_STACK_SIZE	(5*1024)
const char str_na[]="N/A";
#define STR_OR_NA(s) s?s:str_na
/* @brief tag used for ESP serial console messages */
static const char TAG[] = "httpd_handlers";
/* @brief task handle for the http server */

SemaphoreHandle_t http_server_config_mutex = NULL;
extern RingbufHandle_t messaging;
#define AUTH_TOKEN_SIZE 50
typedef struct session_context {
    char * auth_token;
    bool authenticated;
    char * sess_ip_address;
    u16_t port;
} session_context_t;


union sockaddr_aligned {
	struct sockaddr     sa;
    struct sockaddr_storage st;
    struct sockaddr_in  sin;
    struct sockaddr_in6 sin6;
} aligned_sockaddr_t;
esp_err_t post_handler_buff_receive(httpd_req_t * req);
static const char redirect_payload1[]="<html><head><title>Redirecting to Captive Portal</title><meta http-equiv='refresh' content='0; url=";
static const char redirect_payload2[]="'></head><body><p>Please wait, refreshing.  If page does not refresh, click <a href='";
static const char redirect_payload3[]="'>here</a> to login.</p></body></html>";

/**
 * @brief embedded binary data.
 * @see file "component.mk"
 * @see https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#embedding-binary-data
 */
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[]   asm("_binary_style_css_end");
extern const uint8_t jquery_gz_start[] asm("_binary_jquery_min_js_gz_start");
extern const uint8_t jquery_gz_end[] asm("_binary_jquery_min_js_gz_end");
extern const uint8_t popper_gz_start[] asm("_binary_popper_min_js_gz_start");
extern const uint8_t popper_gz_end[] asm("_binary_popper_min_js_gz_end");
extern const uint8_t bootstrap_js_gz_start[] asm("_binary_bootstrap_min_js_gz_start");
extern const uint8_t bootstrap_js_gz_end[] asm("_binary_bootstrap_min_js_gz_end");
extern const uint8_t bootstrap_css_gz_start[] asm("_binary_bootstrap_min_css_gz_start");
extern const uint8_t bootstrap_css_gz_end[] asm("_binary_bootstrap_min_css_gz_end");
extern const uint8_t code_js_start[] asm("_binary_code_js_start");
extern const uint8_t code_js_end[] asm("_binary_code_js_end");
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");
esp_err_t redirect_processor(httpd_req_t *req, httpd_err_code_t error);


char * alloc_get_http_header(httpd_req_t * req, const char * key){
    char*  buf = NULL;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, key) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGD_LOC(TAG, "Found header => %s: %s",key, buf);
        }
    }
    return buf;
}


char * http_alloc_get_socket_address(httpd_req_t *req, u8_t local, in_port_t * portl) {

	socklen_t len;
	union sockaddr_aligned addr;
	len = sizeof(addr);
	ip_addr_t * ip_addr=NULL;
	char * ipstr = malloc(INET6_ADDRSTRLEN);
	memset(ipstr,0x0,INET6_ADDRSTRLEN);

	typedef int (*getaddrname_fn_t)(int s, struct sockaddr *name, socklen_t *namelen);
	getaddrname_fn_t get_addr = NULL;

	int s = httpd_req_to_sockfd(req);
	if(s == -1) {
		free(ipstr);
		return strdup("httpd_req_to_sockfd error");
	}
	ESP_LOGV_LOC(TAG,"httpd socket descriptor: %u", s);

	get_addr = local?&lwip_getsockname:&lwip_getpeername;
	if(get_addr(s, (struct sockaddr *)&addr, &len) <0){
		ESP_LOGE_LOC(TAG,"Failed to retrieve socket address");
		sprintf(ipstr,"N/A (0.0.0.%u)",local);
	}
	else {
		if (addr.sin.sin_family!= AF_INET) {
			ip_addr = (ip_addr_t *)&(addr.sin6.sin6_addr);
			inet_ntop(addr.sa.sa_family, ip_addr, ipstr, INET6_ADDRSTRLEN);
			ESP_LOGV_LOC(TAG,"Processing an IPV6 address : %s", ipstr);
			*portl =  addr.sin6.sin6_port;
			unmap_ipv4_mapped_ipv6(ip_2_ip4(ip_addr), ip_2_ip6(ip_addr));
		}
		else {
			ip_addr = (ip_addr_t *)&(addr.sin.sin_addr);
			inet_ntop(addr.sa.sa_family, ip_addr, ipstr, INET6_ADDRSTRLEN);
			ESP_LOGV_LOC(TAG,"Processing an IPV6 address : %s", ipstr);
			*portl =  addr.sin.sin_port;
		}
		inet_ntop(AF_INET, ip_addr, ipstr, INET6_ADDRSTRLEN);
		ESP_LOGV_LOC(TAG,"Retrieved ip address:port = %s:%u",ipstr, *portl);
	}
	return ipstr;
}
bool is_captive_portal_host_name(httpd_req_t *req){
	const char * host_name=NULL;
	const char * ap_host_name=NULL;
	char * ap_ip_address=NULL;
	bool request_contains_hostname = false;
	esp_err_t hn_err =ESP_OK, err=ESP_OK;
	ESP_LOGD_LOC(TAG,  "Getting adapter host name");
	if((err  = tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_STA, &host_name )) !=ESP_OK) {
		ESP_LOGE_LOC(TAG,  "Unable to get host name. Error: %s",esp_err_to_name(err));
	}
	else {
		ESP_LOGD_LOC(TAG,  "Host name is %s",host_name);
	}

   ESP_LOGD_LOC(TAG,  "Getting host name from request");
	char *req_host = alloc_get_http_header(req, "Host");

	if(tcpip_adapter_is_netif_up(TCPIP_ADAPTER_IF_AP)){
		ESP_LOGD_LOC(TAG,  "Soft AP is enabled. getting ip info");
		// Access point is up and running. Get the current IP address
		tcpip_adapter_ip_info_t ip_info;
		esp_err_t ap_ip_err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
		if(ap_ip_err != ESP_OK){
			ESP_LOGE_LOC(TAG,  "Unable to get local AP ip address. Error: %s",esp_err_to_name(ap_ip_err));
		}
		else {
			ESP_LOGD_LOC(TAG,  "getting host name for TCPIP_ADAPTER_IF_AP");
			if((hn_err  = tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_AP, &ap_host_name )) !=ESP_OK) {
				ESP_LOGE_LOC(TAG,  "Unable to get host name. Error: %s",esp_err_to_name(hn_err));
				err=err==ESP_OK?hn_err:err;
			}
			else {
				ESP_LOGD_LOC(TAG,  "Soft AP Host name is %s",ap_host_name);
			}

			ap_ip_address =  malloc(IP4ADDR_STRLEN_MAX);
			memset(ap_ip_address, 0x00, IP4ADDR_STRLEN_MAX);
			if(ap_ip_address){
				ESP_LOGD_LOC(TAG,  "Converting soft ip address to string");
				ip4addr_ntoa_r(&ip_info.ip, ap_ip_address, IP4ADDR_STRLEN_MAX);
				ESP_LOGD_LOC(TAG,"TCPIP_ADAPTER_IF_AP is up and has ip address %s ", ap_ip_address);
			}
		}

	}


    if((request_contains_hostname 		= (host_name!=NULL) && (req_host!=NULL) && strcasestr(req_host,host_name)) == true){
    	ESP_LOGD_LOC(TAG,"http request host = system host name %s", req_host);
    }
    else if((request_contains_hostname 		= (ap_host_name!=NULL) && (req_host!=NULL) && strcasestr(req_host,ap_host_name)) == true){
    	ESP_LOGD_LOC(TAG,"http request host = AP system host name %s", req_host);
    }

    FREE_AND_NULL(ap_ip_address);
    FREE_AND_NULL(req_host);

    return request_contains_hostname;
}

/* Custom function to free context */
void free_ctx_func(void *ctx)
{
	START_FREE_MEM_CHECK(ff);
	session_context_t * context = (session_context_t *)ctx;
    if(context){
    	ESP_LOGD(TAG, "Freeing up socket context");
    	FREE_AND_NULL(context->auth_token);
    	FREE_AND_NULL(context->sess_ip_address);
    	free(context);
    	CHECK_RESET_FREE_MEM_CHECK(ff,"free_ctx");
    }
}

session_context_t* get_session_context(httpd_req_t *req){
	START_FREE_MEM_CHECK(ff);
	if (! req->sess_ctx) {
		ESP_LOGD(TAG,"New connection context. Allocating session buffer");
		req->sess_ctx = malloc(sizeof(session_context_t));
		memset(req->sess_ctx,0x00,sizeof(session_context_t));
		req->free_ctx = free_ctx_func;
		// get the remote IP address only once per session
	}
	session_context_t *ctx_data = (session_context_t*)req->sess_ctx;
	FREE_AND_NULL(ctx_data->sess_ip_address);
	ctx_data->sess_ip_address = http_alloc_get_socket_address(req, 0, &ctx_data->port);
	ESP_LOGD_LOC(TAG, "serving %s to peer %s port %u", req->uri, ctx_data->sess_ip_address , ctx_data->port);
	CHECK_RESET_FREE_MEM_CHECK(ff,"get sess context");
	return (session_context_t *)req->sess_ctx;
}

bool is_user_authenticated(httpd_req_t *req){
	session_context_t *ctx_data = get_session_context(req);

	if(ctx_data->authenticated){
		ESP_LOGD_LOC(TAG,"User is authenticated.");
		return true;
	}

	ESP_LOGD(TAG,"Heap internal:%zu (min:%zu) external:%zu (min:%zu)",
				heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
				heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
				heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
				heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));

	// todo:  ask for user to authenticate
	return false;
}



/* Copies the full path into destination buffer and returns
 * pointer to requested file name */
static const char* get_path_from_uri(char *dest, const char *uri, size_t destsize)
{
    size_t pathlen = strlen(uri);
    memset(dest,0x0,destsize);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if ( pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    strlcpy(dest , uri, pathlen + 1);

    // strip trailing blanks
    char * sr = dest+pathlen;
    while(*sr== ' ') *sr-- = '\0';

    char * last_fs = strchr(dest,'/');
    if(!last_fs) ESP_LOGD_LOC(TAG,"no / found in %s", dest);
    char * p=last_fs;
    while(p && *(++p)!='\0'){
    	if(*p == '/') {
    		last_fs=p;
    	}
    }
    /* Return pointer to path, skipping the base */
    return last_fs? ++last_fs: dest;
}

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if(strlen(filename) ==0){
    	// for root page, etc.
    	return httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    } else if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    } else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    } else if (IS_FILE_EXT(filename, ".css")) {
        return httpd_resp_set_type(req, "text/css");
    } else if (IS_FILE_EXT(filename, ".js")) {
        return httpd_resp_set_type(req, "text/javascript");
    } else if (IS_FILE_EXT(filename, ".json")) {
        return httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    }

    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}
static esp_err_t set_content_type_from_req(httpd_req_t *req)
{
	char filepath[FILE_PATH_MAX];
	const char *filename = get_path_from_uri(filepath, req->uri, sizeof(filepath));
   if (!filename) {
	   ESP_LOGE_LOC(TAG, "Filename is too long");
	   /* Respond with 500 Internal Server Error */
	   httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
	   return ESP_FAIL;
   }

   /* If name has trailing '/', respond with directory contents */
   if (filename[strlen(filename) - 1] == '/' && strlen(filename)>1) {
	   httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Browsing files forbidden.");
	   return ESP_FAIL;
   }
   set_content_type_from_file(req, filename);
   return ESP_OK;
}


esp_err_t root_get_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Accept-Encoding", "identity");

    if(!is_user_authenticated(req)){
    	// todo:  send password entry page and return
    }
    const size_t file_size = (index_html_end - index_html_start);
	esp_err_t err = set_content_type_from_req(req);
	if(err == ESP_OK){
		httpd_resp_send(req, (const char *)index_html_start, file_size);
	}
	ESP_LOGD_LOC(TAG, "done serving [%s]", req->uri);
    return err;
}

esp_err_t resource_filehandler(httpd_req_t *req){
    char filepath[FILE_PATH_MAX];
   ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);

   const char *filename = get_path_from_uri(filepath, req->uri, sizeof(filepath));

   if (!filename) {
	   ESP_LOGE_LOC(TAG, "Filename is too long");
	   /* Respond with 500 Internal Server Error */
	   httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
	   return ESP_FAIL;
   }

   /* If name has trailing '/', respond with directory contents */
   if (filename[strlen(filename) - 1] == '/') {
	   httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Browsing files forbidden.");
	   return ESP_FAIL;
   }

   if(strstr(filename, "code.js")) {
	    const size_t file_size = (code_js_end - code_js_start);
	    set_content_type_from_file(req, filename);
	    httpd_resp_send(req, (const char *)code_js_start, file_size);
	} else if(strstr(filename, "style.css")) {
		set_content_type_from_file(req, filename);
	    const size_t file_size = (style_css_end - style_css_start);
	    httpd_resp_send(req, (const char *)style_css_start, file_size);
    } else if(strstr(filename, "favicon.ico")) {
		set_content_type_from_file(req, filename);
	    const size_t file_size = (favicon_ico_end - favicon_ico_start);
	    httpd_resp_send(req, (const char *)favicon_ico_start, file_size);
	} else if(strstr(filename, "jquery.js")) {
		set_content_type_from_file(req, filename);
		httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
	    const size_t file_size = (jquery_gz_end - jquery_gz_start);
	    httpd_resp_send(req, (const char *)jquery_gz_start, file_size);
	} else if(strstr(filename, "popper.js")) {
		set_content_type_from_file(req, filename);
		httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
	    const size_t file_size = (popper_gz_end - popper_gz_start);
	    httpd_resp_send(req, (const char *)popper_gz_start, file_size);
	} else if(strstr(filename, "bootstrap.js")) {
			set_content_type_from_file(req, filename);
			httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
		    const size_t file_size = (bootstrap_js_gz_end - bootstrap_js_gz_start);
		    httpd_resp_send(req, (const char *)bootstrap_js_gz_start, file_size);
	} else if(strstr(filename, "bootstrap.css")) {
			set_content_type_from_file(req, filename);
			httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
		    const size_t file_size = (bootstrap_css_gz_end - bootstrap_css_gz_start);
		    httpd_resp_send(req, (const char *)bootstrap_css_gz_start, file_size);
    } else {
	   ESP_LOGE_LOC(TAG, "Unknown resource [%s] from path [%s] ", filename,filepath);
	   /* Respond with 404 Not Found */
	   httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
	   return ESP_FAIL;
   }
   ESP_LOGD_LOC(TAG, "Resource sending complete");
   return ESP_OK;

}
esp_err_t ap_scan_handler(httpd_req_t *req){
    const char empty[] = "{}";
	ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
	wifi_manager_scan_async();
	esp_err_t err = set_content_type_from_req(req);
	if(err == ESP_OK){
		httpd_resp_send(req, (const char *)empty, HTTPD_RESP_USE_STRLEN);
	}
	return err;
}

esp_err_t console_cmd_get_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    /* if we can get the mutex, write the last version of the AP list */
	esp_err_t err = set_content_type_from_req(req);
	cJSON * cmdlist = get_cmd_list();
	char * json_buffer = cJSON_Print(cmdlist);
	if(json_buffer){
		httpd_resp_send(req, (const char *)json_buffer, HTTPD_RESP_USE_STRLEN);
		free(json_buffer);
	}
	else{
		ESP_LOGD_LOC(TAG,  "Error retrieving command json string. ");
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to format command");
	}
	cJSON_Delete(cmdlist);
	ESP_LOGD_LOC(TAG, "done serving [%s]", req->uri);
	return err;
}
esp_err_t console_cmd_post_handler(httpd_req_t *req){
	char success[]="{}";
	ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
	//bool bOTA=false;
	//char * otaURL=NULL;
	esp_err_t err = post_handler_buff_receive(req);
	if(err!=ESP_OK){
		return err;
	}
	if(!is_user_authenticated(req)){
		// todo:  redirect to login page
		// return ESP_OK;
	}
	err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}

	char *command= ((rest_server_context_t *)(req->user_ctx))->scratch;

	cJSON *root = cJSON_Parse(command);
	if(root == NULL){
		ESP_LOGE_LOC(TAG, "Parsing command. Received content was: %s",command);
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed command json.  Unable to parse content.");
		return ESP_FAIL;
	}
	char * root_str = cJSON_Print(root);
	if(root_str!=NULL){
		ESP_LOGD(TAG, "Processing command item: \n%s", root_str);
		free(root_str);
	}
	cJSON *item=cJSON_GetObjectItemCaseSensitive(root, "command");
	if(!item){
		ESP_LOGE_LOC(TAG, "Command not found. Received content was: %s",command);
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed command json.  Unable to parse content.");
		err = ESP_FAIL;
	}
	else{
		// navigate to the first child of the config structure
		run_command(cJSON_GetStringValue(item));
	}

	httpd_resp_send(req, (const char *)success, strlen(success));

	ESP_LOGD_LOC(TAG, "done serving [%s]", req->uri);
	return err;
}
esp_err_t ap_get_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    /* if we can get the mutex, write the last version of the AP list */
	esp_err_t err = set_content_type_from_req(req);
	if( err == ESP_OK && wifi_manager_lock_json_buffer(( TickType_t ) 10)){
		char *buff = wifi_manager_alloc_get_ap_list_json();
		wifi_manager_unlock_json_buffer();
		if(buff!=NULL){
			httpd_resp_send(req, (const char *)buff, HTTPD_RESP_USE_STRLEN);
			free(buff);
		}
		else {
			ESP_LOGD_LOC(TAG,  "Error retrieving ap list json string. ");
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to retrieve AP list");
		}
	}
	else {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "AP list unavailable");
		ESP_LOGE_LOC(TAG,   "GET /ap.json failed to obtain mutex");
	}
	ESP_LOGD_LOC(TAG, "done serving [%s]", req->uri);
	return err;
}

esp_err_t config_get_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
	esp_err_t err = set_content_type_from_req(req);
	if(err == ESP_OK){
		char * json = config_alloc_get_json(false);
		if(json==NULL){
			ESP_LOGD_LOC(TAG,  "Error retrieving config json string. ");
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error retrieving configuration object");
			err=ESP_FAIL;
		}
		else {
			ESP_LOGD_LOC(TAG,  "config json : %s",json );
			cJSON * gplist=get_gpio_list();
			char * gpliststr=cJSON_PrintUnformatted(gplist);
			httpd_resp_sendstr_chunk(req,"{ \"gpio\":");
			httpd_resp_sendstr_chunk(req,gpliststr);
			httpd_resp_sendstr_chunk(req,", \"config\":");
			httpd_resp_sendstr_chunk(req, (const char *)json);
			httpd_resp_sendstr_chunk(req,"}");
			httpd_resp_sendstr_chunk(req,NULL);
			free(gpliststr);
			free(json);
		}
	}
	return err;
}
esp_err_t post_handler_buff_receive(httpd_req_t * req){
    esp_err_t err = ESP_OK;

    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
    	ESP_LOGE_LOC(TAG,"Received content was too long. ");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Content too long");
        err = ESP_FAIL;
    }
    while (err == ESP_OK && cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
        	ESP_LOGE_LOC(TAG,"Not all data was received. ");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Not all data was received");
            err = ESP_FAIL;
        }
        else {
        	cur_len += received;
        }
    }

    if(err == ESP_OK) {
    	buf[total_len] = '\0';
    }
    return err;
}

esp_err_t config_post_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
	bool bOTA=false;
	char * otaURL=NULL;
    esp_err_t err = post_handler_buff_receive(req);
    if(err!=ESP_OK){
        return err;
    }
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
	err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}

    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    cJSON *root = cJSON_Parse(buf);
    if(root == NULL){
    	ESP_LOGE_LOC(TAG, "Parsing config json failed. Received content was: %s",buf);
    	httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed config json.  Unable to parse content.");
    	return ESP_FAIL;
    }

    char * root_str = cJSON_Print(root);
	if(root_str!=NULL){
		ESP_LOGD(TAG, "Processing config item: \n%s", root_str);
		free(root_str);
	}

    cJSON *item=cJSON_GetObjectItemCaseSensitive(root, "config");
    if(!item){
    	ESP_LOGE_LOC(TAG, "Parsing config json failed. Received content was: %s",buf);
    	httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed config json.  Unable to parse content.");
    	err = ESP_FAIL;
    }
    else{
    	// navigate to the first child of the config structure
    	if(item->child) item=item->child;
    }

	while (item && err == ESP_OK)
	{
		cJSON *prev_item = item;
		item=item->next;
		char * entry_str = cJSON_Print(prev_item);
		if(entry_str!=NULL){
			ESP_LOGD_LOC(TAG, "Processing config item: \n%s", entry_str);
			free(entry_str);
		}

		if(prev_item->string==NULL) {
			ESP_LOGD_LOC(TAG,"Config value does not have a name");
			httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed config json.  Value does not have a name.");
			err = ESP_FAIL;
		}
		if(err == ESP_OK){
			ESP_LOGD_LOC(TAG,"Found config value name [%s]", prev_item->string);
			nvs_type_t item_type=  config_get_item_type(prev_item);
			if(item_type!=0){
				void * val = config_safe_alloc_get_entry_value(item_type, prev_item);
				if(val!=NULL){
					if(strcmp(prev_item->string, "fwurl")==0) {
						if(item_type!=NVS_TYPE_STR){
							ESP_LOGE_LOC(TAG,"Firmware url should be type %d. Found type %d instead.",NVS_TYPE_STR,item_type );
							httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed config json.  Wrong type for firmware URL.");
							err = ESP_FAIL;
						}
						else {
							// we're getting a request to do an OTA from that URL
							ESP_LOGW_LOC(TAG,   "Found OTA request!");
							otaURL=strdup(val);
							bOTA=true;
						}
					}
					else {
						if(config_set_value(item_type, prev_item->string , val) != ESP_OK){
							ESP_LOGE_LOC(TAG,"Unable to store value for [%s]", prev_item->string);
							httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Unable to store config value");
							err = ESP_FAIL;
						}
						else {
							ESP_LOGD_LOC(TAG,"Successfully set value for [%s]",prev_item->string);
						}
					}
					free(val);
				}
				else {
					char messageBuffer[101]={};
					ESP_LOGE_LOC(TAG,"Value not found for [%s]", prev_item->string);
					snprintf(messageBuffer,sizeof(messageBuffer),"Malformed config json.  Missing value for entry %s.",prev_item->string);
					httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, messageBuffer);
					err = ESP_FAIL;
				}
			}
			else {
				ESP_LOGE_LOC(TAG,"Unable to determine the type of config value [%s]", prev_item->string);
				httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed config json.  Missing value for entry.");
				err = ESP_FAIL;
			}
		}
	}


	if(err==ESP_OK){
		httpd_resp_sendstr(req, "{ \"result\" : \"OK\" }");
		messaging_post_message(MESSAGING_INFO,MESSAGING_CLASS_SYSTEM,"Save Success");
	}
    cJSON_Delete(root);
	if(bOTA) {

		if(is_recovery_running){
			ESP_LOGW_LOC(TAG,   "Starting process OTA for url %s",otaURL);
		}
		else {
			ESP_LOGW_LOC(TAG,   "Restarting system to process OTA for url %s",otaURL);
		}

		wifi_manager_reboot_ota(otaURL);
		free(otaURL);
	}
    return err;

}
esp_err_t connect_post_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    char success[]="{}";
    char * ssid=NULL;
    char * password=NULL;
    char * host_name=NULL;

	esp_err_t err = post_handler_buff_receive(req);
	if(err!=ESP_OK){
		return err;
	}
	err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}

	char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
	cJSON *root = cJSON_Parse(buf);

	if(root==NULL){
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "JSON parsing error.");
		return ESP_FAIL;
	}

	cJSON * ssid_object = cJSON_GetObjectItem(root, "ssid");
	if(ssid_object !=NULL){
		ssid = strdup(ssid_object->valuestring);
	}
	cJSON * password_object = cJSON_GetObjectItem(root, "pwd");
	if(password_object !=NULL){
		password = strdup(password_object->valuestring);
	}
	cJSON * host_name_object = cJSON_GetObjectItem(root, "host_name");
	if(host_name_object !=NULL){
		host_name = strdup(host_name_object->valuestring);
	}
	cJSON_Delete(root);

	if(host_name!=NULL){
		if(config_set_value(NVS_TYPE_STR, "host_name", host_name) != ESP_OK){
			ESP_LOGW_LOC(TAG,  "Unable to save host name configuration");
		}
	}

	if(ssid !=NULL && strlen(ssid) <= MAX_SSID_SIZE && strlen(password) <= MAX_PASSWORD_SIZE  ){
		wifi_config_t* config = wifi_manager_get_wifi_sta_config();
		memset(config, 0x00, sizeof(wifi_config_t));
		strlcpy((char *)config->sta.ssid, ssid, sizeof(config->sta.ssid)+1);
		if(password){
			strlcpy((char *)config->sta.password, password, sizeof(config->sta.password)+1);
		}
		ESP_LOGD_LOC(TAG,   "http_server_netconn_serve: wifi_manager_connect_async() call, with ssid: %s, password: %s", config->sta.ssid, config->sta.password);
		wifi_manager_connect_async();
		httpd_resp_send(req, (const char *)success, strlen(success));
	}
	else {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed json. Missing or invalid ssid/password.");
		err = ESP_FAIL;
	}
	FREE_AND_NULL(ssid);
	FREE_AND_NULL(password);
	FREE_AND_NULL(host_name);
	return err;
}
esp_err_t connect_delete_handler(httpd_req_t *req){
	char success[]="{}";
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
	esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}
	httpd_resp_send(req, (const char *)success, strlen(success));
	wifi_manager_disconnect_async();

    return ESP_OK;
}
esp_err_t reboot_ota_post_handler(httpd_req_t *req){
	char success[]="{}";
	ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}

	httpd_resp_send(req, (const char *)success, strlen(success));
	wifi_manager_reboot(OTA);
    return ESP_OK;
}
esp_err_t reboot_post_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    char success[]="{}";
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}
	httpd_resp_send(req, (const char *)success, strlen(success));
	wifi_manager_reboot(RESTART);
	return ESP_OK;
}
esp_err_t recovery_post_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    char success[]="{}";
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}
	httpd_resp_send(req, (const char *)success, strlen(success));
	wifi_manager_reboot(RECOVERY);
	return ESP_OK;
}


esp_err_t flash_post_handler(httpd_req_t *req){
	esp_err_t err =ESP_OK;
	if(is_recovery_running){
		ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
		char success[]="File uploaded. Flashing started.";
		if(!is_user_authenticated(req)){
			// todo:  redirect to login page
			// return ESP_OK;
		}
		err = httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
		if(err != ESP_OK){
			return err;
		}
		char * binary_buffer = malloc(req->content_len);
		if(binary_buffer == NULL){
			ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
			/* Respond with 400 Bad Request */
			httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
								"Binary file too large. Unable to allocate memory!");
			return ESP_FAIL;
		}
		ESP_LOGI(TAG, "Receiving ota binary file");
		/* Retrieve the pointer to scratch buffer for temporary storage */
		char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;

		char *head=binary_buffer;
		int received;

		/* Content length of the request gives
		 * the size of the file being uploaded */
		int remaining = req->content_len;

		while (remaining > 0) {

			ESP_LOGI(TAG, "Remaining size : %d", remaining);
			/* Receive the file part by part into a buffer */
			if ((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
				if (received == HTTPD_SOCK_ERR_TIMEOUT) {
					/* Retry if timeout occurred */
					continue;
				}
				FREE_RESET(binary_buffer);
				ESP_LOGE(TAG, "File reception failed!");
				/* Respond with 500 Internal Server Error */
				httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
				err = ESP_FAIL;
				goto bail_out;
			}

			/* Write buffer content to file on storage */
			if (received ) {
				memcpy(head,buf,received );
				head+=received;
			}

			/* Keep track of remaining size of
			 * the file left to be uploaded */
			remaining -= received;
		}

		/* Close file upon upload completion */
		ESP_LOGI(TAG, "File reception complete. Invoking OTA process.");
		err = start_ota(NULL, binary_buffer, req->content_len);
		if(err!=ESP_OK){
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA processing failed");
			goto bail_out;
		}

		//todo:  handle this in ajax.  For now, just send the root page
		httpd_resp_send(req, (const char *)success, strlen(success));
	}
bail_out:

	return err;
}

char * get_ap_ip_address(){
	static char ap_ip_address[IP4ADDR_STRLEN_MAX]={};

	tcpip_adapter_ip_info_t ip_info;
	esp_err_t err=ESP_OK;
	memset(ap_ip_address, 0x00, sizeof(ap_ip_address));

	ESP_LOGD_LOC(TAG,  "checking if soft AP is enabled");
	if(tcpip_adapter_is_netif_up(TCPIP_ADAPTER_IF_AP)){
		ESP_LOGD_LOC(TAG,  "Soft AP is enabled. getting ip info");
		// Access point is up and running. Get the current IP address
		err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
		if(err != ESP_OK){
			ESP_LOGE_LOC(TAG,  "Unable to get local AP ip address. Error: %s",esp_err_to_name(err));
		}
		else {
			ESP_LOGV_LOC(TAG,  "Converting soft ip address to string");
			ip4addr_ntoa_r(&ip_info.ip, ap_ip_address, IP4ADDR_STRLEN_MAX);
			ESP_LOGD_LOC(TAG,"TCPIP_ADAPTER_IF_AP is up and has ip address %s ", ap_ip_address);
		}
	}
	else{
		ESP_LOGD_LOC(TAG,"AP Is not enabled. Returning blank string");
	}
	return ap_ip_address;
}
esp_err_t process_redirect(httpd_req_t *req, const char * status){
	const char location_prefix[] = "http://";
	char * ap_ip_address=get_ap_ip_address();
	char * remote_ip=NULL;
	in_port_t port=0;
	char *redirect_url = NULL;

	ESP_LOGD_LOC(TAG,  "Getting remote socket address");
	remote_ip = http_alloc_get_socket_address(req,0, &port);

	size_t buf_size = strlen(redirect_payload1) +strlen(redirect_payload2) + strlen(redirect_payload3) +2*(strlen(location_prefix)+strlen(ap_ip_address))+1;
	char * redirect=malloc(buf_size);

	if(strcasestr(status,"302")){
		size_t url_buf_size = strlen(location_prefix) + strlen(ap_ip_address)+1;
		redirect_url = malloc(url_buf_size);
		memset(redirect_url,0x00,url_buf_size);
		snprintf(redirect_url, buf_size,"%s%s/",location_prefix, ap_ip_address);
		ESP_LOGW_LOC(TAG,  "Redirecting host [%s] to %s (from uri %s)",remote_ip, redirect_url,req->uri);
		httpd_resp_set_hdr(req,"Location",redirect_url);
		snprintf(redirect, buf_size,"OK");
	}
	else {

		snprintf(redirect, buf_size,"%s%s%s%s%s%s%s",redirect_payload1, location_prefix, ap_ip_address,redirect_payload2, location_prefix, ap_ip_address,redirect_payload3);
		ESP_LOGW_LOC(TAG,  "Responding to host [%s] (from uri %s) with redirect html page %s",remote_ip, req->uri,redirect);
	}

	httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
	httpd_resp_set_hdr(req,"Cache-Control","no-cache");
	httpd_resp_set_status(req, status);
	httpd_resp_send(req, redirect, HTTPD_RESP_USE_STRLEN);
	FREE_AND_NULL(redirect);
	FREE_AND_NULL(redirect_url);
	FREE_AND_NULL(remote_ip);

	return ESP_OK;
}
esp_err_t redirect_200_ev_handler(httpd_req_t *req){
	ESP_LOGD_LOC(TAG,"Processing known redirect url %s",req->uri);
	process_redirect(req,"200 OK");
	return ESP_OK;
}
esp_err_t redirect_processor(httpd_req_t *req, httpd_err_code_t error){
	esp_err_t err=ESP_OK;
	const char * host_name=NULL;
	const char * ap_host_name=NULL;
	char * user_agent=NULL;
	char * remote_ip=NULL;
	char * sta_ip_address=NULL;
	char * ap_ip_address=get_ap_ip_address();
	char * socket_local_address=NULL;
	bool request_contains_hostname = false;
	bool request_contains_ap_ip_address 	= false;
	bool request_is_sta_ip_address 	= false;
	bool connected_to_ap_ip_interface 	= false;
	bool connected_to_sta_ip_interface = false;
	bool useragentiscaptivenetwork = false;

	ESP_LOGW_LOC(TAG, "Invalid URL requested: [%s]", req->uri);
    if(wifi_manager_lock_sta_ip_string(portMAX_DELAY)){
		sta_ip_address = strdup(wifi_manager_get_sta_ip_string());
		wifi_manager_unlock_sta_ip_string();
	}
	else {
    	ESP_LOGE(TAG,"Unable to obtain local IP address from WiFi Manager.");
    	httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , NULL);
	}

    in_port_t port=0;
    ESP_LOGV_LOC(TAG,  "Getting remote socket address");
    remote_ip = http_alloc_get_socket_address(req,0, &port);

    ESP_LOGV_LOC(TAG,  "Getting host name from request");
    char *req_host = alloc_get_http_header(req, "Host");

    user_agent = alloc_get_http_header(req,"User-Agent");
    if((useragentiscaptivenetwork = (user_agent!=NULL  && strcasestr(user_agent,"CaptiveNetworkSupport"))==true)){
    	ESP_LOGW_LOC(TAG,"Found user agent that supports captive networks! [%s]",user_agent);
    }

	esp_err_t hn_err = ESP_OK;
	ESP_LOGV_LOC(TAG,  "Getting adapter host name");
	if((hn_err  = tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_STA, &host_name )) !=ESP_OK) {
		ESP_LOGE_LOC(TAG,  "Unable to get host name. Error: %s",esp_err_to_name(hn_err));
		err=err==ESP_OK?hn_err:err;
	}
	else {
		ESP_LOGV_LOC(TAG,  "Host name is %s",host_name);
	}


	in_port_t loc_port=0;
	ESP_LOGV_LOC(TAG,  "Getting local socket address");
	socket_local_address= http_alloc_get_socket_address(req,1, &loc_port);



    ESP_LOGD_LOC(TAG,  "Peer IP: %s [port %u], System AP IP address: %s, System host: %s. Requested Host: [%s], uri [%s]",STR_OR_NA(remote_ip), port, STR_OR_NA(ap_ip_address), STR_OR_NA(host_name), STR_OR_NA(req_host), req->uri);
    /* captive portal functionality: redirect to access point IP for HOST that are not the access point IP OR the STA IP */
	/* determine if Host is from the STA IP address */

    if((request_contains_hostname 		= (host_name!=NULL) && (req_host!=NULL) && strcasestr(req_host,host_name)) == true){
    	ESP_LOGD_LOC(TAG,"http request host = system host name %s", req_host);
    }
    else if((request_contains_hostname 		= (ap_host_name!=NULL) && (req_host!=NULL) && strcasestr(req_host,ap_host_name)) == true){
    	ESP_LOGD_LOC(TAG,"http request host = AP system host name %s", req_host);
    }
    if((request_contains_ap_ip_address 	= (ap_ip_address!=NULL) && (req_host!=NULL) && strcasestr(req_host,ap_ip_address))== true){
    	ESP_LOGD_LOC(TAG,"http request host is access point ip address %s", req_host);
    }
    if((connected_to_ap_ip_interface 	= (ap_ip_address!=NULL) && (socket_local_address!=NULL) && strcasestr(socket_local_address,ap_ip_address))==true){
    	ESP_LOGD_LOC(TAG,"http request is connected to access point interface IP %s", ap_ip_address);
    }
    if((request_is_sta_ip_address 	= (sta_ip_address!=NULL) && (req_host!=NULL) && strcasestr(req_host,sta_ip_address))==true){
    	ESP_LOGD_LOC(TAG,"http request host is WiFi client ip address %s", req_host);
    }
    if((connected_to_sta_ip_interface = (sta_ip_address!=NULL) && (socket_local_address!=NULL) && strcasestr(sta_ip_address,socket_local_address))==true){
    	ESP_LOGD_LOC(TAG,"http request is connected to WiFi client ip address %s", sta_ip_address);
    }

   if((error == 0) || (error == HTTPD_404_NOT_FOUND && connected_to_ap_ip_interface && !(request_contains_ap_ip_address || request_contains_hostname ))) {
		process_redirect(req,"302 Found");

	}
	else {
		ESP_LOGD_LOC(TAG,"URL not found, and not processing captive portal so throw regular 404 error");
		httpd_resp_send_err(req, error, NULL);
	}

	FREE_AND_NULL(socket_local_address);

	FREE_AND_NULL(req_host);
	FREE_AND_NULL(user_agent);

    FREE_AND_NULL(sta_ip_address);
	FREE_AND_NULL(remote_ip);
	return err;

}
esp_err_t redirect_ev_handler(httpd_req_t *req){
	return redirect_processor(req,0);
}

esp_err_t messages_get_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    START_FREE_MEM_CHECK(before);
    START_FREE_MEM_CHECK(all);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    CHECK_RESET_FREE_MEM_CHECK(before, "after user auth");
    esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}
	CHECK_RESET_FREE_MEM_CHECK(before, "after set_content_type");
	cJSON * json_messages=  messaging_retrieve_messages(messaging);
	CHECK_RESET_FREE_MEM_CHECK(before, "after receiving messages");
	if(json_messages!=NULL){
		char * json_text= cJSON_Print(json_messages);
		CHECK_RESET_FREE_MEM_CHECK(before, "after json print");
		httpd_resp_send(req, (const char *)json_text, strlen(json_text));
		CHECK_RESET_FREE_MEM_CHECK(before, "after http send");
		free(json_text);
		CHECK_RESET_FREE_MEM_CHECK(before, "after free json message");
		cJSON_Delete(json_messages);
		CHECK_RESET_FREE_MEM_CHECK(before, "after free json");
	}
	else {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Unable to retrieve messages");
	}
	CHECK_RESET_FREE_MEM_CHECK(all, "before returning");
	return ESP_OK;
}

esp_err_t status_get_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}

	if(wifi_manager_lock_json_buffer(( TickType_t ) 10)) {
		char *buff = wifi_manager_alloc_get_ip_info_json();
		wifi_manager_unlock_json_buffer();
		if(buff) {
			httpd_resp_send(req, (const char *)buff, strlen(buff));
			free(buff);
		}
		else {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Empty status object");
		}
	}
	else {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Error retrieving status object");
	}

	return ESP_OK;
}


esp_err_t err_handler(httpd_req_t *req, httpd_err_code_t error){
	esp_err_t err = ESP_OK;

    if(error != HTTPD_404_NOT_FOUND){
    	err = httpd_resp_send_err(req, error, NULL);
    }
    else {
    	err = redirect_processor(req,error);
    }

	return err;
}
