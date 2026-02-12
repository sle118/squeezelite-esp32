/* cmd_i2ctools.h

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

void register_i2ctools(void);
esp_err_t cmd_i2ctools_scan_bus(FILE* f, int sda, int scl);
#ifdef __cplusplus
}
#endif
