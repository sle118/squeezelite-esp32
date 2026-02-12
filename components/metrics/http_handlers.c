#include "http_handlers.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "tools.h"
#include <sys/param.h>
#if CONFIG_WITH_METRICS
static const char* TAG = "metrics_http";
static char* output_buffer; // Buffer to store response of http request from
                            // event handler
static int output_len = 0;  // Stores number of bytes read
#define MAX_HTTP_OUTPUT_BUFFER 2048
// Common function signature for event handlers
typedef void (*HttpEventHandler)(esp_http_client_event_t* evt);

static void handle_http_error(esp_http_client_event_t* evt) { ESP_LOGV(TAG, "ERROR"); }

static void handle_http_connected(esp_http_client_event_t* evt) { ESP_LOGV(TAG, "ON_CONNECTED"); }

static void handle_http_header_sent(esp_http_client_event_t* evt) { ESP_LOGV(TAG, "HEADER_SENT"); }

static void handle_http_on_header(esp_http_client_event_t* evt) { ESP_LOGV(TAG, "ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value); }

static void handle_http_on_data(esp_http_client_event_t* evt) {
    ESP_LOGV(TAG, "ON_DATA, len=%d", evt->data_len);
    ESP_LOGV(TAG, "ON_DATA, len=%d", evt->data_len);
    // Clean the buffer in case of a new request
    if(output_len == 0 && evt->user_data) {
        // we are just starting to copy the output data into the use
        ESP_LOGV(TAG, "Resetting buffer");
        memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
    }
    /*
     *  Check for chunked encoding is added as the URL for chunked encoding used in this example
     * returns binary data. However, event handler can also be used in case chunked encoding is
     * used.
     */

    // If user_data buffer is configured, copy the response into the buffer
    int copy_len = 0;
    if(evt->user_data) {
        ESP_LOGV(TAG, "Not Chunked response, with user data");
        // The last byte in evt->user_data is kept for the NULL character in
        // case of out-of-bound access.
        copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
        if(copy_len) { memcpy(evt->user_data + output_len, evt->data, copy_len); }
    } else {
        int content_len = esp_http_client_get_content_length(evt->client);
        if(esp_http_client_is_chunked_response(evt->client)) { esp_http_client_get_chunk_length(evt->client, &content_len); }

        if(output_buffer == NULL) {
            // We initialize output_buffer with 0 because it is used by
            // strlen() and similar functions therefore should be null
            // terminated.
            size_t len = (content_len + 1) * sizeof(char);
            ESP_LOGV(TAG, "Init buffer %d", len);
            output_buffer = (char*)malloc_init_external(len);
            output_len = 0;
            if(output_buffer == NULL) {
                ESP_LOGE(TAG, "Buffer alloc failed.");
                return;
            }
        }
        copy_len = MIN(evt->data_len, (content_len - output_len));
        if(copy_len) { memcpy(output_buffer + output_len, evt->data, copy_len); }
    }
    output_len += copy_len;
}

static void handle_http_on_finish(esp_http_client_event_t* evt) {
    ESP_LOGD(TAG, "ON_FINISH");
    if(output_buffer != NULL) {
        ESP_LOGV(TAG, "Response: %s", output_buffer);
        free(output_buffer);
        output_buffer = NULL;
    }
    output_len = 0;
}
static void handle_http_disconnected(esp_http_client_event_t* evt) {
    ESP_LOGI(TAG, "DISCONNECTED");
    int mbedtls_err = 0;
    esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
    if(err != 0) {
        ESP_LOGI(TAG, "Last error : %s", esp_err_to_name(err));
        ESP_LOGI(TAG, "Last mbedtls err 0x%x", mbedtls_err);
    }
    if(output_buffer != NULL) {
        free(output_buffer);
        output_buffer = NULL;
    }
    output_len = 0;
}
static const HttpEventHandler eventHandlers[] = {
    handle_http_error,       // HTTP_EVENT_ERROR
    handle_http_connected,   // HTTP_EVENT_ON_CONNECTED
    handle_http_header_sent, // HTTP_EVENT_HEADERS_SENT
    handle_http_header_sent, // HTTP_EVENT_HEADER_SENT (alias for HTTP_EVENT_HEADERS_SENT)
    handle_http_on_header,   // HTTP_EVENT_ON_HEADER
    handle_http_on_data,     // HTTP_EVENT_ON_DATA
    handle_http_on_finish,   // HTTP_EVENT_ON_FINISH
    handle_http_disconnected // HTTP_EVENT_DISCONNECTED
};
esp_err_t metrics_http_event_handler(esp_http_client_event_t* evt) {

    if(evt->event_id < 0 || evt->event_id >= sizeof(eventHandlers) / sizeof(eventHandlers[0])) {
        ESP_LOGE(TAG, "Invalid event ID: %d", evt->event_id);
        return ESP_FAIL;
    }

    eventHandlers[evt->event_id](evt);

    return ESP_OK;
}
int metrics_http_post_request(const char* payload, const char* url) {
    int status_code = 0;
    esp_http_client_config_t config = {.url = url,
        .disable_auto_redirect = false,
        .event_handler = metrics_http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .user_data = NULL, // local_response_buffer,        // Pass address of
                           // local buffer to get response
        .skip_cert_common_name_check = true

    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_set_method(client, HTTP_METHOD_POST);

    if(err == ESP_OK) { err = esp_http_client_set_header(client, "Content-Type", "application/json"); }
    if(err == ESP_OK) {
        ESP_LOGV(TAG, "Setting payload: %s", payload);
        err = esp_http_client_set_post_field(client, payload, strlen(payload));
    }
    if(err == ESP_OK) { err = esp_http_client_perform(client); }
    if(err == ESP_OK) {
        status_code = esp_http_client_get_status_code(client);
        ESP_LOGD(TAG, "metrics call Status = %d, content_length = %d", esp_http_client_get_status_code(client),
            esp_http_client_get_content_length(client));

    } else {
        status_code = 500;
        ESP_LOGW(TAG, "metrics call Status failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return status_code;
}
#endif