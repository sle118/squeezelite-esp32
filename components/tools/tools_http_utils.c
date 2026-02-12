#include "tools_http_utils.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "tools.h"
#include "esp_tls.h"
#include "pb_decode.h"
#include "pb_encode.h"

static const char* TAG = "http_utils";

/****************************************************************************************
 * URL download
 */

typedef struct {
    void* user_context;
    http_download_cb_t callback;
    size_t max, bytes;
    bool abort;
    uint8_t* data;
    esp_http_client_handle_t client;
} http_context_t;

static void http_downloader(void* arg);
static esp_err_t http_event_handler(esp_http_client_event_t* evt);

void http_download(char* url, size_t max, http_download_cb_t callback, void* context) {
    http_context_t* http_context = (http_context_t*)heap_caps_calloc(sizeof(http_context_t), 1, MALLOC_CAP_SPIRAM);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = http_context,
    };

    http_context->callback = callback;
    http_context->user_context = context;
    http_context->max = max;
    http_context->client = esp_http_client_init(&config);

    xTaskCreateEXTRAM(http_downloader, "downloader", 8 * 1024, http_context, ESP_TASK_PRIO_MIN + 1, NULL);
}

static void http_downloader(void* arg) {
    http_context_t* http_context = (http_context_t*)arg;

    esp_http_client_perform(http_context->client);
    esp_http_client_cleanup(http_context->client);

    free(http_context);
    vTaskDeleteEXTRAM(NULL);
}

static esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    http_context_t* http_context = (http_context_t*)evt->user_data;

    if(http_context->abort) return ESP_FAIL;

    switch(evt->event_id) {
    case HTTP_EVENT_ERROR:
        http_context->callback(NULL, 0, http_context->user_context);
        http_context->abort = true;
        break;
    case HTTP_EVENT_ON_HEADER:
        if(!strcasecmp(evt->header_key, "Content-Length")) {
            size_t len = atoi(evt->header_value);
            if(!len || len > http_context->max) {
                ESP_LOGI(TAG, "content-length null or too large %zu / %zu", len, http_context->max);
                http_context->abort = true;
            }
        }
        break;
    case HTTP_EVENT_ON_DATA: {
        size_t len = esp_http_client_get_content_length(evt->client);
        if(!http_context->data) {
            if((http_context->data = (uint8_t*)malloc(len)) == NULL) {
                http_context->abort = true;
                ESP_LOGE(TAG, "failed to allocate memory for output buffer %zu", len);
                return ESP_FAIL;
            }
        }
        memcpy(http_context->data + http_context->bytes, evt->data, evt->data_len);
        http_context->bytes += evt->data_len;
        break;
    }
    case HTTP_EVENT_ON_FINISH:
        http_context->callback(http_context->data, http_context->bytes, http_context->user_context);
        break;
    case HTTP_EVENT_DISCONNECTED: {
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP download disconnect %d", err);
            if(http_context->data) free(http_context->data);
            http_context->callback(NULL, 0, http_context->user_context);
            return ESP_FAIL;
        }
        break;
    }
    default:
        break;
    }

    return ESP_OK;
}

/****************************************************************************************
 * URL tools
 */

static inline char from_hex(char ch) { return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10; }

void url_decode(char* url) {
    char *p, *src = strdup(url);
    for(p = src; *src; url++) {
        *url = *src++;
        if(*url == '%') {
            *url = from_hex(*src++) << 4;
            *url |= from_hex(*src++);
        } else if(*url == '+') {
            *url = ' ';
        }
    }
    *url = '\0';
    free(p);
}

bool out_http_binding(pb_ostream_t* stream, const uint8_t* buf, size_t count) {
    httpd_req_t* req = (httpd_req_t*)stream->state;
    ESP_LOGD(TAG, "Writing %d bytes to file", count);
    return httpd_resp_send_chunk(req, (const char*)buf, count) == ESP_OK;
}

bool in_http_binding(pb_istream_t* stream, pb_byte_t* buf, size_t count) {
    httpd_req_t* req = (httpd_req_t*)stream->state;
    ESP_LOGV(TAG, "Reading %d bytes from http stream", count);
    int received = httpd_req_recv(req, (char*)buf, count);
    if(received <= 0) {
        stream->errmsg = "Not all data received";
        return false;
    }
    return received == count;
}
