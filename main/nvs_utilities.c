#include "nvs_utilities.h"

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"

const char current_namespace[] = "config";
const char settings_partition[] = "settings";
static const char * TAG = "platform_esp32";

esp_err_t store_nvs_value(nvs_type_t type, const char *key, void * data) {
	if (type == NVS_TYPE_BLOB)
		return ESP_ERR_NVS_TYPE_MISMATCH;
	return store_nvs_value_len(type, key, data,0);
}
esp_err_t store_nvs_value_len(nvs_type_t type, const char *key, void * data,
		size_t data_len) {
	esp_err_t err;
	nvs_handle nvs;

	if (type == NVS_TYPE_ANY) {
		return ESP_ERR_NVS_TYPE_MISMATCH;
	}

	err = nvs_open_from_partition(settings_partition, current_namespace, NVS_READWRITE, &nvs);
	if (err != ESP_OK) {
		return err;
	}

	if (type == NVS_TYPE_I8) {
		err = nvs_set_i8(nvs, key, *(int8_t *) data);
	} else if (type == NVS_TYPE_U8) {
		err = nvs_set_u8(nvs, key, *(uint8_t *) data);
	} else if (type == NVS_TYPE_I16) {
		err = nvs_set_i16(nvs, key, *(int16_t *) data);
	} else if (type == NVS_TYPE_U16) {
		err = nvs_set_u16(nvs, key, *(uint16_t *) data);
	} else if (type == NVS_TYPE_I32) {
		err = nvs_set_i32(nvs, key, *(int32_t *) data);
	} else if (type == NVS_TYPE_U32) {
		err = nvs_set_u32(nvs, key, *(uint32_t *) data);
	} else if (type == NVS_TYPE_I64) {
		err = nvs_set_i64(nvs, key, *(int64_t *) data);
	} else if (type == NVS_TYPE_U64) {
		err = nvs_set_u64(nvs, key, *(uint64_t *) data);
	} else if (type == NVS_TYPE_STR) {
		err = nvs_set_str(nvs, key, data);
	} else if (type == NVS_TYPE_BLOB) {
		err = nvs_set_blob(nvs, key, (void *) data, data_len);
	}
	if (err == ESP_OK) {
		err = nvs_commit(nvs);
		if (err == ESP_OK) {
			ESP_LOGI(TAG, "Value stored under key '%s'", key);
		}
	}
	nvs_close(nvs);
	return err;
}
void nvs_value_set_default(nvs_type_t type, const char *key, void * default_value, size_t blob_size) {
	free(get_nvs_value_alloc_default(type, key, default_value, blob_size));
}
void * get_nvs_value_alloc_default(nvs_type_t type, const char *key, void * default_value, size_t blob_size) {
	void * current_value = get_nvs_value_alloc(type, key);
	if(current_value == NULL && default_value != NULL){
		if(type == NVS_TYPE_BLOB && blob_size == 0){
			ESP_LOGE(TAG,"Unable to store default value for BLOB object'  blob size was not specified");
			return NULL;
		}
		else {
			esp_err_t err = store_nvs_value_len(type, key, default_value, blob_size);
			if(err!=ESP_OK){
				ESP_LOGE(TAG,"Unable to store default nvs value for key %s. Error: %s", key,esp_err_to_name(err));
				return NULL;
			}
			else{
				ESP_LOGI(TAG,"Stored new default value for key %s", key);
			}
		}
	}
	if(current_value == NULL){
		current_value = get_nvs_value_alloc(type, key);
	}
	return current_value;
}

void * get_nvs_value_alloc(nvs_type_t type, const char *key) {
	nvs_handle nvs;
	esp_err_t err;
	void * value=NULL;

	err = nvs_open_from_partition(settings_partition, current_namespace, NVS_READONLY, &nvs);
	if (err != ESP_OK) {
		ESP_LOGE(TAG,"Could not open the nvs storage.");
		return NULL;
	}

	if (type == NVS_TYPE_I8) {
		value=malloc(sizeof(int8_t));
		err = nvs_get_i8(nvs, key, (int8_t *) value);
	} else if (type == NVS_TYPE_U8) {
		value=malloc(sizeof(uint8_t));
		err = nvs_get_u8(nvs, key, (uint8_t *) value);
	} else if (type == NVS_TYPE_I16) {
		value=malloc(sizeof(int16_t));
		err = nvs_get_i16(nvs, key, (int16_t *) value);
	} else if (type == NVS_TYPE_U16) {
		value=malloc(sizeof(uint16_t));
		err = nvs_get_u16(nvs, key, (uint16_t *) value);
	} else if (type == NVS_TYPE_I32) {
		value=malloc(sizeof(int32_t));
		err = nvs_get_i32(nvs, key, (int32_t *) value);
	} else if (type == NVS_TYPE_U32) {
		value=malloc(sizeof(uint32_t));
		err = nvs_get_u32(nvs, key, (uint32_t *) value);
	} else if (type == NVS_TYPE_I64) {
		value=malloc(sizeof(int64_t));
		err = nvs_get_i64(nvs, key, (int64_t *) value);
	} else if (type == NVS_TYPE_U64) {
		value=malloc(sizeof(uint64_t));
		err = nvs_get_u64(nvs, key, (uint64_t *) value);
	} else if (type == NVS_TYPE_STR) {
		size_t len=0;
		err = nvs_get_str(nvs, key, NULL, &len);
		if (err == ESP_OK) {
			value=malloc(len);
			err = nvs_get_str(nvs, key, value, &len);
			}
	} else if (type == NVS_TYPE_BLOB) {
		size_t len;
		err = nvs_get_blob(nvs, key, NULL, &len);
		if (err == ESP_OK) {
			value=malloc(len+1);
			err = nvs_get_blob(nvs, key, value, &len);
		}
	}
	if(err!=ESP_OK){
		ESP_LOGD(TAG,"Value not found for key %s",key);
		if(value!=NULL)
			free(value);
		value=NULL;
	}
	nvs_close(nvs);
	return value;
}
esp_err_t get_nvs_value(nvs_type_t type, const char *key, void*value, const uint8_t buf_size) {
	nvs_handle nvs;
	esp_err_t err;

	err = nvs_open_from_partition(settings_partition, current_namespace, NVS_READONLY, &nvs);
	if (err != ESP_OK) {
		return err;
	}

	if (type == NVS_TYPE_I8) {
		err = nvs_get_i8(nvs, key, (int8_t *) value);
	} else if (type == NVS_TYPE_U8) {
		err = nvs_get_u8(nvs, key, (uint8_t *) value);
	} else if (type == NVS_TYPE_I16) {
		err = nvs_get_i16(nvs, key, (int16_t *) value);
	} else if (type == NVS_TYPE_U16) {
		err = nvs_get_u16(nvs, key, (uint16_t *) value);
	} else if (type == NVS_TYPE_I32) {
		err = nvs_get_i32(nvs, key, (int32_t *) value);
	} else if (type == NVS_TYPE_U32) {
		err = nvs_get_u32(nvs, key, (uint32_t *) value);
	} else if (type == NVS_TYPE_I64) {
		err = nvs_get_i64(nvs, key, (int64_t *) value);
	} else if (type == NVS_TYPE_U64) {
		err = nvs_get_u64(nvs, key, (uint64_t *) value);
	} else if (type == NVS_TYPE_STR) {
		size_t len;
		if ((err = nvs_get_str(nvs, key, NULL, &len)) == ESP_OK) {
			if (len > buf_size) {
				//ESP_LOGE("Error reading value for %s.  Buffer size: %d, Value Length: %d", key, buf_size, len);
				err = ESP_FAIL;
			} else {
				err = nvs_get_str(nvs, key, value, &len);
			}
		}
	} else if (type == NVS_TYPE_BLOB) {
		size_t len;
		if ((err = nvs_get_blob(nvs, key, NULL, &len)) == ESP_OK) {

			if (len > buf_size) {
				//ESP_LOGE("Error reading value for %s.  Buffer size: %d, Value Length: %d",
				//		key, buf_size, len);
				err = ESP_FAIL;
			} else {
				err = nvs_get_blob(nvs, key, value, &len);
			}
		}
	}

	nvs_close(nvs);
	return err;
}
esp_err_t erase_nvs(const char *key)
{
    nvs_handle nvs;

    esp_err_t err = nvs_open(current_namespace, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_erase_key(nvs, key);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Value with key '%s' erased", key);
            }
        }
        nvs_close(nvs);
    }

    return err;
}

