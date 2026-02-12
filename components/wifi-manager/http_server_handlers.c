/*
Copyright (c) 2017-2021 Sebastien L
*/
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "http_server_handlers.h"
#include "Config.h"
#include "DataRequest.pb.h"
#include "Locking.h"
#include "Status.pb.h"
#include "accessors.h"
#include "argtable3/argtable3.h"
#include "cJSON.h"
#include "cmd_system.h"
#include "esp_console.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "messaging.h"
#include "network_status.h"
#include "network_wifi.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "platform_console.h"
#include "platform_esp32.h"
#include "squeezelite-ota.h"
#include "sys/param.h"
#include "tools.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "tools_http_utils.h"
#include "bootstate.h"

#define HTTP_STACK_SIZE (5 * 1024)
const char str_na[] = "N/A";
#define STR_OR_NA(s) s ? s : str_na
/* @brief tag used for ESP serial console messages */
static const char TAG[] = "httpd_handlers";
static const char* www_dir = "/spiffs/www";

SemaphoreHandle_t http_server_config_mutex = NULL;
extern RingbufHandle_t messaging;
#define AUTH_TOKEN_SIZE 50
typedef struct session_context {
    char* auth_token;
    bool authenticated;
    char* sess_ip_address;
    u16_t port;
    SemaphoreHandle_t signal;
} session_context_t;
extern cJSON* get_gpio_list(bool refresh);

union sockaddr_aligned {
    struct sockaddr sa;
    struct sockaddr_storage st;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
} aligned_sockaddr_t;
esp_err_t post_handler_buff_receive(httpd_req_t* req);
static const char redirect_payload1[] = "<html><head><title>Redirecting to Captive "
                                        "Portal</title><meta http-equiv='refresh' content='0; url=";
static const char redirect_payload2[] = "'></head><body><p>Please wait, refreshing.  If page does not refresh, click <a href='";
static const char redirect_payload3[] = "'>here</a> to login.</p></body></html>";

/**
 * @brief embedded binary data.
 * @see file "component.mk"
 * @see
 * https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#embedding-binary-data
 */

esp_err_t redirect_processor(httpd_req_t* req, httpd_err_code_t error);

char* alloc_get_http_header(httpd_req_t* req, const char* key) {
    char* buf = NULL;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, key) + 1;
    if(buf_len > 1) {
        buf = malloc_init_external(buf_len);
        /* Copy null terminated value string into buffer */
        if(httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) { ESP_LOGD_LOC(TAG, "Found header => %s: %s", key, buf); }
    }
    return buf;
}

char* http_alloc_get_socket_address(httpd_req_t* req, u8_t local, in_port_t* portl) {

    socklen_t len;
    union sockaddr_aligned addr;
    len = sizeof(addr);
    char* ipstr = malloc_init_external(INET6_ADDRSTRLEN);
    typedef int (*getaddrname_fn_t)(int s, struct sockaddr* name, socklen_t* namelen);
    getaddrname_fn_t get_addr = NULL;

    int s = httpd_req_to_sockfd(req);
    if(s == -1) {
        free(ipstr);
        return strdup_psram("httpd_req_to_sockfd error");
    }
    ESP_LOGV_LOC(TAG, "httpd socket descriptor: %u", s);

    get_addr = local ? &lwip_getsockname : &lwip_getpeername;
    if(get_addr(s, (struct sockaddr*)&addr, &len) < 0) {
        ESP_LOGE_LOC(TAG, "Failed to retrieve socket address");
        sprintf(ipstr, "N/A (0.0.0.%u)", local);
    } else {
        if(addr.sin.sin_family == AF_INET6) {
            inet_ntop(AF_INET6, &addr.sin6.sin6_addr, ipstr, INET6_ADDRSTRLEN);
            ESP_LOGV_LOC(TAG, "Processing an IPV6 address : %s", ipstr);
            *portl = addr.sin6.sin6_port;
        } else {
            inet_ntop(AF_INET, &addr.sin.sin_addr, ipstr, INET6_ADDRSTRLEN);
            ESP_LOGV_LOC(TAG, "Processing an IPV4 address : %s", ipstr);
            *portl = addr.sin.sin_port;
        }
        ESP_LOGV_LOC(TAG, "Retrieved ip address:port = %s:%u", ipstr, *portl);
    }
    return ipstr;
}

bool is_captive_portal_host_name(httpd_req_t* req) {
    const char* host_name = NULL;
    const char* ap_host_name = NULL;
    char* ap_ip_address = NULL;
    bool request_contains_hostname = false;
    esp_err_t hn_err = ESP_OK, err = ESP_OK;
    ESP_LOGD_LOC(TAG, "Getting adapter host name");
    if((err = tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_STA, &host_name)) != ESP_OK) {
        ESP_LOGE_LOC(TAG, "Unable to get host name. Error: %s", esp_err_to_name(err));
    } else {
        ESP_LOGD_LOC(TAG, "Host name is %s", host_name);
    }

    ESP_LOGD_LOC(TAG, "Getting host name from request");
    char* req_host = alloc_get_http_header(req, "Host");

    if(tcpip_adapter_is_netif_up(TCPIP_ADAPTER_IF_AP)) {
        ESP_LOGD_LOC(TAG, "Soft AP is enabled. getting ip info");
        // Access point is up and running. Get the current IP address
        tcpip_adapter_ip_info_t ip_info;
        esp_err_t ap_ip_err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
        if(ap_ip_err != ESP_OK) {
            ESP_LOGE_LOC(TAG, "Unable to get local AP ip address. Error: %s", esp_err_to_name(ap_ip_err));
        } else {
            ESP_LOGD_LOC(TAG, "getting host name for TCPIP_ADAPTER_IF_AP");
            if((hn_err = tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_AP, &ap_host_name)) != ESP_OK) {
                ESP_LOGE_LOC(TAG, "Unable to get host name. Error: %s", esp_err_to_name(hn_err));
                err = err == ESP_OK ? hn_err : err;
            } else {
                ESP_LOGD_LOC(TAG, "Soft AP Host name is %s", ap_host_name);
            }

            ap_ip_address = malloc_init_external(IP4ADDR_STRLEN_MAX);
            memset(ap_ip_address, 0x00, IP4ADDR_STRLEN_MAX);
            if(ap_ip_address) {
                ESP_LOGD_LOC(TAG, "Converting soft ip address to string");
                esp_ip4addr_ntoa(&ip_info.ip, ap_ip_address, IP4ADDR_STRLEN_MAX);
                ESP_LOGD_LOC(TAG, "TCPIP_ADAPTER_IF_AP is up and has ip address %s ", ap_ip_address);
            }
        }
    }

    if((request_contains_hostname = (host_name != NULL) && (req_host != NULL) && strcasestr(req_host, host_name)) == true) {
        ESP_LOGD_LOC(TAG, "http request host = system host name %s", req_host);
    } else if((request_contains_hostname = (ap_host_name != NULL) && (req_host != NULL) && strcasestr(req_host, ap_host_name)) == true) {
        ESP_LOGD_LOC(TAG, "http request host = AP system host name %s", req_host);
    }

    FREE_AND_NULL(ap_ip_address);
    FREE_AND_NULL(req_host);

    return request_contains_hostname;
}

/* Custom function to free context */
void free_ctx_func(void* ctx) {
    session_context_t* context = (session_context_t*)ctx;
    if(context) {
        ESP_LOGD(TAG, "Freeing up socket context");
        FREE_AND_NULL(context->auth_token);
        FREE_AND_NULL(context->sess_ip_address);
        free(context);
    }
}

session_context_t* get_session_context(httpd_req_t* req) {
    bool newConnection = false;
    ESP_LOGD_LOC(TAG, "Getting session context for %s", req->uri);
    if(!req->sess_ctx) {
        ESP_LOGD(TAG, "New connection context. Allocating session buffer");
        req->sess_ctx = malloc_init_external(sizeof(session_context_t));
        req->free_ctx = free_ctx_func;
        newConnection = true;
        // get the remote IP address only once per session
    }
    session_context_t* ctx_data = (session_context_t*)req->sess_ctx;
    FREE_AND_NULL(ctx_data->sess_ip_address);
    ctx_data->sess_ip_address = http_alloc_get_socket_address(req, 0, &ctx_data->port);
    if(newConnection) { ESP_LOGI(TAG, "serving %s to peer %s port %u", req->uri, ctx_data->sess_ip_address, ctx_data->port); }
    return (session_context_t*)req->sess_ctx;
}
bool is_spiffs_safe_thread(httpd_req_t* req) {
    session_context_t* ctx_data = get_session_context(req);
    return ctx_data->signal != NULL;
}
void finalize_dispatch(httpd_req_t* req) {
    ESP_LOGD_LOC(TAG, "Finalizing dispatch");
    session_context_t* ctx_data = get_session_context(req);
    if(!ctx_data) {
        ESP_LOGE(TAG, "Invalid HTTP Context");
    } else {
        xSemaphoreGive(ctx_data->signal);
    }
}
void dispatch_response(httpd_req_t* req, network_manager_ret_cb_t cb) {
    session_context_t* ctx_data = get_session_context(req);
    if(!ctx_data) {
        ESP_LOGE(TAG, "Invalid HTTP Context");
        return;
    }
    ctx_data->signal = xSemaphoreCreateBinary();
    ESP_LOGD_LOC(TAG, "FIRING async task for %s", req->uri);
    network_async_callback_withret(req, cb);
    ESP_LOGD_LOC(TAG, "WAITING for async task to complete for %s", req->uri);
    if(xSemaphoreTake(ctx_data->signal, portMAX_DELAY) != pdTRUE) { ESP_LOGE(TAG, "Async http failed for %s", req->uri); }
    ESP_LOGD_LOC(TAG, "COMPLETED Async task for %s", req->uri);
    vSemaphoreDelete(ctx_data->signal);
    ctx_data->signal = NULL;
}
bool is_user_authenticated(httpd_req_t* req) {
    session_context_t* ctx_data = get_session_context(req);

    if(ctx_data->authenticated) {
        ESP_LOGD_LOC(TAG, "User is authenticated.");
        return true;
    }

    ESP_LOGD(TAG, "Heap internal:%zu (min:%zu) external:%zu (min:%zu) dma:%zu (min:%zu)", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_DMA), heap_caps_get_minimum_free_size(MALLOC_CAP_DMA));

    // todo:  ask for user to authenticate
    return false;
}

/* Copies the full path into destination buffer and returns
 * pointer to requested file name */
static const char* get_path_from_uri(char* dest, const char* uri, size_t destsize) {
    size_t pathlen = strlen(uri);
    memset(dest, 0x0, destsize);

    const char* quest = strchr(uri, '?');
    if(quest) { pathlen = MIN(pathlen, quest - uri); }
    const char* hash = strchr(uri, '#');
    if(hash) { pathlen = MIN(pathlen, hash - uri); }

    if(pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    strlcpy(dest, uri, pathlen + 1);

    // strip trailing blanks
    char* sr = dest + pathlen;
    while(*sr == ' ') *sr-- = '\0';

    char* last_fs = strchr(dest, '/');
    if(!last_fs) ESP_LOGD_LOC(TAG, "no / found in %s", dest);
    char* p = last_fs;
    while(p && *(++p) != '\0') {
        if(*p == '/') { last_fs = p; }
    }
    /* Return pointer to path, skipping the base */
    return last_fs ? ++last_fs : dest;
}
bool hasFileExtension(const char* filename, const char* ext) {
    size_t fn_len = strlen(filename);
    size_t ext_len = strlen(ext);

    if(ext_len > fn_len) { return false; }

    return strcasecmp(&filename[fn_len - ext_len], ext) == 0;
}

static esp_err_t set_content_type_from_file(httpd_req_t* req, const char* full_name) {
    char filename[strlen(full_name) + 1];
    strcpy(filename, full_name);
    if(hasFileExtension(full_name, ".gz")) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        // Remove the .gz extension for MIME type detection
        filename[strlen(full_name) - 3] = '\0';
    }

    if(hasFileExtension(filename, ".html") || hasFileExtension(filename, ".htm")) {
        return httpd_resp_set_type(req, "text/html");
    } else if(hasFileExtension(filename, ".css")) {
        return httpd_resp_set_type(req, "text/css");
    } else if(hasFileExtension(filename, ".js")) {
        return httpd_resp_set_type(req, "application/javascript");
    } else if(hasFileExtension(filename, ".json")) {
        return httpd_resp_set_type(req, "application/json");
    } else if(hasFileExtension(filename, ".xml")) {
        return httpd_resp_set_type(req, "application/xml");
    } else if(hasFileExtension(filename, ".jpg") || hasFileExtension(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if(hasFileExtension(filename, ".png")) {
        return httpd_resp_set_type(req, "image/png");
    } else if(hasFileExtension(filename, ".gif")) {
        return httpd_resp_set_type(req, "image/gif");
    } else if(hasFileExtension(filename, ".svg")) {
        return httpd_resp_set_type(req, "image/svg+xml");
    } else if(hasFileExtension(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    } else if(hasFileExtension(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if(hasFileExtension(filename, ".txt")) {
        return httpd_resp_set_type(req, "text/plain");
    } else if(hasFileExtension(filename, ".bin") || hasFileExtension(filename, ".dat")) {
        return httpd_resp_set_type(req, "application/octet-stream");
    } else if(hasFileExtension(filename, ".mp3")) {
        return httpd_resp_set_type(req, "audio/mpeg");
    } else if(hasFileExtension(filename, ".mp4")) {
        return httpd_resp_set_type(req, "video/mp4");
    } else if(hasFileExtension(filename, ".avi")) {
        return httpd_resp_set_type(req, "video/x-msvideo");
    }

    // Default MIME type for unknown files
    return httpd_resp_set_type(req, "application/octet-stream");
}

static esp_err_t set_content_type_from_req(httpd_req_t* req) {
    char filepath[FILE_PATH_MAX];
    const char* filename = get_path_from_uri(filepath, req->uri, sizeof(filepath));
    if(!filename) {
        ESP_LOGE_LOC(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* If name has trailing '/', respond with directory contents */
    if(filename[strlen(filename) - 1] == '/' && strlen(filename) > 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Browsing files forbidden.");
        return ESP_FAIL;
    }
    set_content_type_from_file(req, filename);
    return ESP_OK;
}

// esp_err_t root_get_handler(httpd_req_t *req){
// 	esp_err_t err = ESP_OK;
//     ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
//     httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
//     httpd_resp_set_hdr(req, "Accept-Encoding", "identity");

//     if(!is_user_authenticated(req)){
//     	// todo:  send password entry page and return
//     }
// 	int idx=-1;
// 	if((idx=resource_get_index("index.html"))>=0){
// 		const size_t file_size = (resource_map_end[idx] - resource_map_start[idx]);
// 		httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
// 		err = set_content_type_from_req(req);
// 		if(err == ESP_OK){
// 			httpd_resp_send(req, (const char *)resource_map_start[idx], file_size);
// 		}
// 	}
//     else{
// 		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "index.html not found");
// 	   return ESP_FAIL;
// 	}
// 	ESP_LOGD_LOC(TAG, "done serving [%s]", req->uri);
//     return err;
// }
static bool resolve_file_path(const char* uri, char* resolvedpath, size_t resolvedsize) {
    struct stat file_stat;

    // Assume the base path is the directory where files are served from
    // Generate the expected file path
    snprintf(resolvedpath, resolvedsize, "%s%s", www_dir, uri);

    // Check if file exists
    if(stat(resolvedpath, &file_stat) == 0) {
        // File exists
        return true;
    } else {
        // Check for compressed file
        strncat(resolvedpath, ".gz", resolvedsize - strlen(resolvedpath) - 1);
        if(stat(resolvedpath, &file_stat) == 0) {
            // Compressed file exists
            return true;
        }
    }

    // Neither uncompressed nor compressed file exists
    return false;
}
esp_err_t send_file_chunked(httpd_req_t* req, const char* filename) {
    struct stat file_stat = {};
    if(stat(filename, &file_stat) == -1) {
        ESP_LOGE(TAG, "Failed to stat file : %s", filename);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    FILE* fd = fopen(filename, "r");
    if(!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filename);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGD_LOC(TAG, "Setting content type for %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);
    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char* chunk = ((rest_server_context_t*)(req->user_ctx))->scratch;
    size_t chunksize;
    do {
        ESP_LOGD_LOC(TAG, "More data to send");
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
        ESP_LOGD_LOC(TAG, "Read chunk size: %d", chunksize);
        if(chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if(httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                ESP_LOGD_LOC(TAG, "Sending NULL chunk");
                httpd_resp_sendstr_chunk(req, NULL);
                ESP_LOGD_LOC(TAG, "Sending 500 internal error");
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                ESP_LOGD_LOC(TAG, "returning");
                return ESP_FAIL;
            }
        }

        /* Keep looping till the whole file is sent */
    } while(chunksize != 0);
    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete for %s", req->uri);
    /* Respond with an empty chunk to signal HTTP response completion */
    ESP_LOGD_LOC(TAG, "Closing connection");
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}
esp_err_t file_get_handler(httpd_req_t* req) {
    esp_err_t err = ESP_OK;
    char filepath[FILE_PATH_MAX];
    if(!is_spiffs_safe_thread(req)) {
        dispatch_response(req, (network_manager_ret_cb_t)file_get_handler);
        return ESP_OK;
    }

    // const char* filename = get_path_from_uri(filepath, req->uri, sizeof(filepath));
    ESP_LOGD_LOC(TAG, "Serving file from [%s]", req->uri);
    if(err == ESP_OK && req->uri[strlen(req->uri) - 1] == '/') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Browsing files forbidden.");
        err = ESP_FAIL;
    }
    if(err == ESP_OK && strlen(req->uri) != 0 && hasFileExtension(req->uri, ".map")) {
        err = httpd_resp_sendstr(req, "");
    } else {
        if(err == ESP_OK && !resolve_file_path(req->uri, filepath, sizeof(filepath))) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
            err = ESP_FAIL;
        }
        err = send_file_chunked(req, filepath);
    }
    finalize_dispatch(req);
    return err;
}

esp_err_t root_get_handler(httpd_req_t* req) {
    size_t sz;
    char filepath[FILE_PATH_MAX];
    esp_err_t err = ESP_OK;
    ESP_LOGD(TAG, "Serving [%s]", req->uri);

    if(!is_spiffs_safe_thread(req)) {
        dispatch_response(req, (network_manager_ret_cb_t)root_get_handler);
        return ESP_OK;
    }

    if(!is_user_authenticated(req)) {
        // TODO: Send password entry page and return
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Accept-Encoding", "identity");

    if(!resolve_file_path("/index.html", filepath, sizeof(filepath))) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        err = ESP_FAIL;
    }
    err = send_file_chunked(req, filepath);
    finalize_dispatch(req);
    return err;
}

esp_err_t resource_filehandler(httpd_req_t* req) {
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    esp_err_t err = file_get_handler(req);
    ESP_LOGD_LOC(TAG, "Resource sending complete");
    return err;
}

esp_err_t console_cmd_get_handler(httpd_req_t* req) {
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)) {
        // todo:  redirect to login page
        // return ESP_OK;
    }
    /* if we can get the mutex, write the last version of the AP list */
    esp_err_t err = set_content_type_from_req(req);
    cJSON* cmdlist = get_cmd_list();
    char* json_buffer = cJSON_Print(cmdlist);
    if(json_buffer) {
        httpd_resp_send(req, (const char*)json_buffer, HTTPD_RESP_USE_STRLEN);
        free(json_buffer);
    } else {
        ESP_LOGD_LOC(TAG, "Error retrieving command json string. ");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to format command");
    }
    cJSON_Delete(cmdlist);
    ESP_LOGD_LOC(TAG, "done serving [%s]", req->uri);
    return err;
}
esp_err_t console_cmd_post_handler(httpd_req_t* req) {
    char success[] = "{\"Result\" : \"Success\" }";
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    // bool bOTA=false;
    // char * otaURL=NULL;
    esp_err_t err = post_handler_buff_receive(req);
    if(err != ESP_OK) { return err; }
    if(!is_user_authenticated(req)) {
        // todo:  redirect to login page
        // return ESP_OK;
    }
    err = set_content_type_from_req(req);
    if(err != ESP_OK) { return err; }

    char* command = ((rest_server_context_t*)(req->user_ctx))->scratch;

    cJSON* root = cJSON_Parse(command);
    if(root == NULL) {
        ESP_LOGE_LOC(TAG, "Parsing command. Received content was: %s", command);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed command json.  Unable to parse content.");
        return ESP_FAIL;
    }
    char* root_str = cJSON_Print(root);
    if(root_str != NULL) {
        ESP_LOGD(TAG, "Processing command item: \n%s", root_str);
        free(root_str);
    }
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "command");
    if(!item) {
        ESP_LOGE_LOC(TAG, "Command not found. Received content was: %s", command);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed command json.  Unable to parse content.");
        err = ESP_FAIL;

    } else {
        // navigate to the first child of the config structure
        char* cmd = cJSON_GetStringValue(item);
        if(!console_push(cmd, strlen(cmd) + 1)) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to push command for execution");
        } else {
            httpd_resp_send(req, (const char*)success, strlen(success));
        }
    }

    ESP_LOGD_LOC(TAG, "done serving [%s]", req->uri);
    return err;
}

esp_err_t post_handler_buff_receive(httpd_req_t* req) {
    esp_err_t err = ESP_OK;

    int total_len = req->content_len;
    int cur_len = 0;
    char* buf = ((rest_server_context_t*)(req->user_ctx))->scratch;
    int received = 0;
    if(total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        ESP_LOGE_LOC(TAG, "Received content was too long. ");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        err = ESP_FAIL;
    }
    while(err == ESP_OK && cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if(received <= 0) {
            /* Respond with 500 Internal Server Error */
            ESP_LOGE_LOC(TAG, "Not all data was received. ");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not all data was received");
            err = ESP_FAIL;
        } else {
            cur_len += received;
        }
    }

    if(err == ESP_OK) { buf[total_len] = '\0'; }
    return err;
}
esp_err_t send_response(httpd_req_t* req, sys_request_response* response) {
    esp_err_t err = ESP_OK;
    pb_ostream_t http_stream = PB_OSTREAM_SIZING;
    http_stream.callback = &out_http_binding;
    http_stream.state = req;
    http_stream.max_size = SIZE_MAX;
    if(!pb_encode(&http_stream, &sys_request_response_msg, response)) { err = ESP_FAIL; }
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return err;
}
esp_err_t data_post_handler(httpd_req_t* req) {
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    sys_request_payload payload;
    sys_request_response response;
    response.result = sys_request_result_SUCCESS;
    response.message = "";
    char* otaURL = NULL;
    if(!is_user_authenticated(req)) {
        // todo:  redirect to login page
        // return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
    if(err != ESP_OK) { return err; }
    pb_istream_t http_stream = PB_ISTREAM_EMPTY;
    http_stream.callback = &in_http_binding;
    http_stream.state = req;
    http_stream.bytes_left = req->content_len;

    if(!pb_decode(&http_stream, &sys_request_payload_msg, &payload)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, http_stream.errmsg);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Received Payload");
    dump_structure(&sys_request_payload_msg, &payload);

    switch(payload.type) {
    case sys_request_type_CONFIG:
        if(payload.action == sys_request_action_GET) {
            if(!config_http_send_config(req)) { err = ESP_FAIL; }
        } else {
            // we are setting a config object
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not implemented");
            err = ESP_FAIL;
        }
        break;
    case sys_request_type_STATUS:
        if(payload.action == sys_request_action_GET) {
            if(!network_status_send_object(req)) { err = ESP_FAIL; }
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid request type");
            err = ESP_FAIL;
        }
        break;
    case sys_request_type_SCAN:
        if(payload.action == sys_request_action_GET) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SCAN List should be retrieved with request type STATUS");
            err = ESP_FAIL;
        } else {
            network_async_scan();
        }
        break;
    case sys_request_type_OTA:
        if(payload.which_data != sys_request_payload_URL_tag) {
            response.result = sys_request_result_ERROR;
            response.message = "Missing URL";
        } else {
            otaURL = strdup_psram(payload.data.URL);
            if(is_recovery_running) {
                ESP_LOGW_LOC(TAG, "Starting process OTA for url %s", otaURL);
            } else {
                ESP_LOGW_LOC(TAG, "Restarting system to process OTA for url %s", otaURL);
            }
            network_reboot_ota(otaURL);
        }
        break;

    default:
        break;
    }
    send_response(req, &response);

    pb_release(&sys_request_payload_msg, &payload);
    return err;
}

esp_err_t connect_post_handler(httpd_req_t* req) {
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    char success[] = "{}";
    char* ssid = NULL;
    char* password = NULL;
    char* host_name = NULL;

    esp_err_t err = post_handler_buff_receive(req);
    if(err != ESP_OK) { return err; }
    err = set_content_type_from_req(req);
    if(err != ESP_OK) { return err; }

    char* buf = ((rest_server_context_t*)(req->user_ctx))->scratch;
    if(!is_user_authenticated(req)) {
        // todo:  redirect to login page
        // return ESP_OK;
    }
    cJSON* root = cJSON_Parse(buf);

    if(root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON parsing error.");
        return ESP_FAIL;
    }

    cJSON* ssid_object = cJSON_GetObjectItem(root, "ssid");
    if(ssid_object != NULL) { ssid = strdup_psram(ssid_object->valuestring); }
    cJSON* password_object = cJSON_GetObjectItem(root, "pwd");
    if(password_object != NULL) { password = strdup_psram(password_object->valuestring); }
    cJSON* host_name_object = cJSON_GetObjectItem(root, "host_name");
    if(host_name_object != NULL) { host_name = strdup_psram(host_name_object->valuestring); }
    cJSON_Delete(root);

    // if(host_name!=NULL){
    // 	if(config_set_value(NVS_TYPE_STR, "host_name", host_name) != ESP_OK){
    // 		ESP_LOGW_LOC(TAG,  "Unable to save host name configuration");
    // 	}
    // }

#pragma message("Update this to protocol buffers")
    if(ssid != NULL && strlen(ssid) <= platform->net.max_ssid_size && strlen(password) <= platform->net.max_password_size) {
        network_async_connect(ssid, password);
        httpd_resp_send(req, (const char*)success, strlen(success));
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed json. Missing or invalid ssid/password.");
        err = ESP_FAIL;
    }
    // FREE_AND_NULL(ssid);
    // FREE_AND_NULL(password);
    // FREE_AND_NULL(host_name);
    // TODO: Add support for the commented code
    return err;
}
esp_err_t connect_delete_handler(httpd_req_t* req) {
    char success[] = "{}";
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)) {
        // todo:  redirect to login page
        // return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
    if(err != ESP_OK) { return err; }
    httpd_resp_send(req, (const char*)success, strlen(success));
    network_async_delete();

    return ESP_OK;
}
esp_err_t reboot_ota_post_handler(httpd_req_t* req) {
    char success[] = "{}";
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)) {
        // todo:  redirect to login page
        // return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
    if(err != ESP_OK) { return err; }

    httpd_resp_send(req, (const char*)success, strlen(success));
    network_async_reboot(OTA);
    return ESP_OK;
}
esp_err_t reboot_post_handler(httpd_req_t* req) {
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    char success[] = "{}";
    if(!is_user_authenticated(req)) {
        // todo:  redirect to login page
        // return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
    if(err != ESP_OK) { return err; }
    httpd_resp_send(req, (const char*)success, strlen(success));
    network_async_reboot(RESTART);
    return ESP_OK;
}
esp_err_t recovery_post_handler(httpd_req_t* req) {
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    char success[] = "{}";
    if(!is_user_authenticated(req)) {
        // todo:  redirect to login page
        // return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
    if(err != ESP_OK) { return err; }
    httpd_resp_send(req, (const char*)success, strlen(success));
    network_async_reboot(RECOVERY);
    return ESP_OK;
}

esp_err_t flash_post_handler(httpd_req_t* req) {
    esp_err_t err = ESP_OK;
    if(is_recovery_running) {
        ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
        char success[] = "File uploaded. Flashing started.";
        if(!is_user_authenticated(req)) {
            // todo:  redirect to login page
            // return ESP_OK;
        }
        err = httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
        if(err != ESP_OK) { return err; }
        char* binary_buffer = malloc_init_external(req->content_len);
        if(binary_buffer == NULL) {
            ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
            /* Respond with 400 Bad Request */
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Binary file too large. Unable to allocate memory!");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Receiving ota binary file");
        /* Retrieve the pointer to scratch buffer for temporary storage */
        char* buf = ((rest_server_context_t*)(req->user_ctx))->scratch;

        char* head = binary_buffer;
        int received;

        /* Content length of the request gives
         * the size of the file being uploaded */
        int remaining = req->content_len;

        while(remaining > 0) {

            ESP_LOGI(TAG, "Remaining size : %d", remaining);
            /* Receive the file part by part into a buffer */
            if((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
                if(received == HTTPD_SOCK_ERR_TIMEOUT) {
                    /* Retry if timeout occurred */
                    continue;
                }
                // FREE_RESET(binary_buffer);
                // TODO: Add support for the commented code
                ESP_LOGE(TAG, "File reception failed!");
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
                err = ESP_FAIL;
                goto bail_out;
            }

            /* Write buffer content to file on storage */
            if(received) {
                memcpy(head, buf, received);
                head += received;
            }

            /* Keep track of remaining size of
             * the file left to be uploaded */
            remaining -= received;
        }

        /* Close file upon upload completion */
        ESP_LOGI(TAG, "File reception complete. Invoking OTA process.");
        err = start_ota(NULL, binary_buffer, req->content_len);
        if(err != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA processing failed");
            goto bail_out;
        }

        // todo:  handle this in ajax.  For now, just send the root page
        httpd_resp_send(req, (const char*)success, strlen(success));
    }
bail_out:

    return err;
}

char* get_ap_ip_address() {
    static char ap_ip_address[IP4ADDR_STRLEN_MAX] = {};

    tcpip_adapter_ip_info_t ip_info;
    esp_err_t err = ESP_OK;
    memset(ap_ip_address, 0x00, sizeof(ap_ip_address));

    ESP_LOGD_LOC(TAG, "checking if soft AP is enabled");
    if(tcpip_adapter_is_netif_up(TCPIP_ADAPTER_IF_AP)) {
        ESP_LOGD_LOC(TAG, "Soft AP is enabled. getting ip info");
        // Access point is up and running. Get the current IP address
        err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
        if(err != ESP_OK) {
            ESP_LOGE_LOC(TAG, "Unable to get local AP ip address. Error: %s", esp_err_to_name(err));
        } else {
            ESP_LOGV_LOC(TAG, "Converting soft ip address to string");
            esp_ip4addr_ntoa(&ip_info.ip, ap_ip_address, IP4ADDR_STRLEN_MAX);
            ESP_LOGD_LOC(TAG, "TCPIP_ADAPTER_IF_AP is up and has ip address %s ", ap_ip_address);
        }
    } else {
        ESP_LOGD_LOC(TAG, "AP Is not enabled. Returning blank string");
    }
    return ap_ip_address;
}
esp_err_t process_redirect(httpd_req_t* req, const char* status) {
    const char location_prefix[] = "http://";
    char* ap_ip_address = get_ap_ip_address();
    char* remote_ip = NULL;
    in_port_t port = 0;
    char* redirect_url = NULL;

    ESP_LOGD_LOC(TAG, "Getting remote socket address");
    remote_ip = http_alloc_get_socket_address(req, 0, &port);

    size_t buf_size =
        strlen(redirect_payload1) + strlen(redirect_payload2) + strlen(redirect_payload3) + 2 * (strlen(location_prefix) + strlen(ap_ip_address)) + 1;
    char* redirect = malloc_init_external(buf_size);

    if(strcasestr(status, "302")) {
        size_t url_buf_size = strlen(location_prefix) + strlen(ap_ip_address) + 1;
        redirect_url = malloc_init_external(url_buf_size);
        memset(redirect_url, 0x00, url_buf_size);
        snprintf(redirect_url, buf_size, "%s%s/", location_prefix, ap_ip_address);
        ESP_LOGW_LOC(TAG, "Redirecting host [%s] to %s (from uri %s)", remote_ip, redirect_url, req->uri);
        httpd_resp_set_hdr(req, "Location", redirect_url);
        snprintf(redirect, buf_size, "OK");
    } else {

        snprintf(redirect, buf_size, "%s%s%s%s%s%s%s", redirect_payload1, location_prefix, ap_ip_address, redirect_payload2, location_prefix,
            ap_ip_address, redirect_payload3);
        ESP_LOGW_LOC(TAG, "Responding to host [%s] (from uri %s) with redirect html page %s", remote_ip, req->uri, redirect);
    }

    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_status(req, status);
    httpd_resp_send(req, redirect, HTTPD_RESP_USE_STRLEN);
    FREE_AND_NULL(redirect);
    FREE_AND_NULL(redirect_url);
    FREE_AND_NULL(remote_ip);

    return ESP_OK;
}
esp_err_t redirect_200_ev_handler(httpd_req_t* req) {
    ESP_LOGD_LOC(TAG, "Processing known redirect url %s", req->uri);
    process_redirect(req, "200 OK");
    return ESP_OK;
}
esp_err_t redirect_processor(httpd_req_t* req, httpd_err_code_t error) {
    esp_err_t err = ESP_OK;
    const char* host_name = NULL;
    const char* ap_host_name = NULL;
    char* user_agent = NULL;
    char* remote_ip = NULL;
    char* sta_ip_address = NULL;
    char* ap_ip_address = get_ap_ip_address();
    char* socket_local_address = NULL;
    bool request_contains_hostname = false;
    bool request_contains_ap_ip_address = false;
    bool request_is_sta_ip_address = false;
    bool connected_to_ap_ip_interface = false;
    bool connected_to_sta_ip_interface = false;
    bool useragentiscaptivenetwork = false;

    in_port_t port = 0;
    ESP_LOGV_LOC(TAG, "Getting remote socket address");
    remote_ip = http_alloc_get_socket_address(req, 0, &port);

    ESP_LOGW_LOC(TAG, "%s requested invalid URL: [%s]", remote_ip, req->uri);
    sta_ip_address = strdup_psram(sys_status->net.ip.ip);

    ESP_LOGV_LOC(TAG, "Getting host name from request");
    char* req_host = alloc_get_http_header(req, "Host");

    user_agent = alloc_get_http_header(req, "User-Agent");
    if((useragentiscaptivenetwork = (user_agent != NULL && strcasestr(user_agent, "CaptiveNetworkSupport")) == true)) {
        ESP_LOGW_LOC(TAG, "Found user agent that supports captive networks! [%s]", user_agent);
    }

    esp_err_t hn_err = ESP_OK;
    ESP_LOGV_LOC(TAG, "Getting adapter host name");
    if((hn_err = tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_STA, &host_name)) != ESP_OK) {
        ESP_LOGE_LOC(TAG, "Unable to get host name. Error: %s", esp_err_to_name(hn_err));
        err = err == ESP_OK ? hn_err : err;
    } else {
        ESP_LOGV_LOC(TAG, "Host name is %s", host_name);
    }

    in_port_t loc_port = 0;
    ESP_LOGV_LOC(TAG, "Getting local socket address");
    socket_local_address = http_alloc_get_socket_address(req, 1, &loc_port);

    ESP_LOGD_LOC(TAG,
        "Peer IP: %s [port %u], System AP IP address: %s, System host: %s. Requested Host: [%s], "
        "uri [%s]",
        STR_OR_NA(remote_ip), port, STR_OR_NA(ap_ip_address), STR_OR_NA(host_name), STR_OR_NA(req_host), req->uri);
    /* captive portal functionality: redirect to access point IP for HOST that are not the access
     * point IP OR the STA IP */
    /* determine if Host is from the STA IP address */

    if((request_contains_hostname = (host_name != NULL) && (req_host != NULL) && strcasestr(req_host, host_name)) == true) {
        ESP_LOGD_LOC(TAG, "http request host = system host name %s", req_host);
    } else if((request_contains_hostname = (ap_host_name != NULL) && (req_host != NULL) && strcasestr(req_host, ap_host_name)) == true) {
        ESP_LOGD_LOC(TAG, "http request host = AP system host name %s", req_host);
    }
    if((request_contains_ap_ip_address = (ap_ip_address != NULL) && (req_host != NULL) && strcasestr(req_host, ap_ip_address)) == true) {
        ESP_LOGD_LOC(TAG, "http request host is access point ip address %s", req_host);
    }
    if((connected_to_ap_ip_interface =
               (ap_ip_address != NULL) && (socket_local_address != NULL) && strcasestr(socket_local_address, ap_ip_address)) == true) {
        ESP_LOGD_LOC(TAG, "http request is connected to access point interface IP %s", ap_ip_address);
    }
    if((request_is_sta_ip_address = (sta_ip_address != NULL) && (req_host != NULL) && strcasestr(req_host, sta_ip_address)) == true) {
        ESP_LOGD_LOC(TAG, "http request host is WiFi client ip address %s", req_host);
    }
    if((connected_to_sta_ip_interface =
               (sta_ip_address != NULL) && (socket_local_address != NULL) && strcasestr(sta_ip_address, socket_local_address)) == true) {
        ESP_LOGD_LOC(TAG, "http request is connected to WiFi client ip address %s", sta_ip_address);
    }

    if((error == 0) ||
        (error == HTTPD_404_NOT_FOUND && connected_to_ap_ip_interface && !(request_contains_ap_ip_address || request_contains_hostname))) {
        process_redirect(req, "302 Found");

    } else {
        ESP_LOGD_LOC(TAG, "URL not found, and not processing captive portal so throw regular 404 error");
        httpd_resp_send_err(req, error, NULL);
    }

    FREE_AND_NULL(socket_local_address);

    FREE_AND_NULL(req_host);
    FREE_AND_NULL(user_agent);

    FREE_AND_NULL(sta_ip_address);
    FREE_AND_NULL(remote_ip);
    return err;
}
esp_err_t redirect_ev_handler(httpd_req_t* req) { return redirect_processor(req, 0); }

esp_err_t messages_get_handler(httpd_req_t* req) {
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)) {
        // todo:  redirect to login page
        // return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
    if(err != ESP_OK) { return err; }
    cJSON* json_messages = messaging_retrieve_messages(messaging);
    if(json_messages != NULL) {
        char* json_text = cJSON_Print(json_messages);
        httpd_resp_send(req, (const char*)json_text, strlen(json_text));
        free(json_text);
        cJSON_Delete(json_messages);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to retrieve messages");
    }
    return ESP_OK;
}

esp_err_t status_get_handler(httpd_req_t* req) {
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)) {
        // todo:  redirect to login page
        // return ESP_OK;
    }
    esp_err_t err = httpd_resp_set_type(req, "application/octet-stream");
    if(err != ESP_OK) { return err; }
    network_status_send_object(req);
    return ESP_OK;
}

esp_err_t err_handler(httpd_req_t* req, httpd_err_code_t error) {
    esp_err_t err = ESP_OK;

    if(error != HTTPD_404_NOT_FOUND) {
        err = httpd_resp_send_err(req, error, NULL);
    } else {
        err = redirect_processor(req, error);
    }

    return err;
}
