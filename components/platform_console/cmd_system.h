/* Console example — various system commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#pragma once
#include "esp_system.h"
#ifdef __cplusplus
extern "C" {
#endif

// Register system functions
void register_system();

FILE * system_open_memstream(const char * cmdname,char **buf,size_t *buf_size);
#ifdef __cplusplus
}
#endif
