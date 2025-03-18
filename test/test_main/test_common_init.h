#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "esp_log.h"
#include "esp_system.h"
extern void common_test_init();
void start_ota_set_return(esp_err_t mockreturn);
#ifdef __cplusplus
}
#endif