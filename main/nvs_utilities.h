#pragma once
#include "esp_err.h"
#include "nvs.h"
#include "trace.h"
#ifdef __cplusplus
extern "C" {
#endif
extern const char current_namespace[];
extern const char settings_partition[];
esp_err_t store_nvs_value_len(nvs_type_t type, const char *key, void * data, size_t data_len);
esp_err_t store_nvs_value(nvs_type_t type, const char *key, void * data);
esp_err_t get_nvs_value(nvs_type_t type, const char *key, void*value, const uint8_t buf_size);
void * get_nvs_value_alloc(nvs_type_t type, const char *key);
esp_err_t erase_nvs(const char *key);
void * get_nvs_value_alloc_default(nvs_type_t type, const char *key, void * default_value, size_t blob_size);
void nvs_value_set_default(nvs_type_t type, const char *key, void * default_value, size_t blob_size);
#ifdef __cplusplus
}
#endif
