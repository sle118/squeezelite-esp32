#include <stdio.h>
#include <string.h>
#include "application_name.h"
#include "esp_err.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "cJSON.h"
#include "stdbool.h"
extern esp_err_t process_recovery_ota(const char * bin_url, char * bin_buffer, uint32_t length);
extern cJSON * gpio_list;
const __attribute__((section(".rodata_desc"))) esp_app_desc_t esp_app_desc = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
    .version = PROJECT_VER,
    .project_name = CONFIG_PROJECT_NAME,
    .idf_ver = IDF_VER,

#ifdef CONFIG_BOOTLOADER_APP_SECURE_VERSION
    .secure_version = CONFIG_BOOTLOADER_APP_SECURE_VERSION,
#else
    .secure_version = 0,
#endif

#ifdef CONFIG_APP_COMPILE_TIME_DATE
    .time = __TIME__,
    .date = __DATE__,
#else
    .time = "",
    .date = "",
#endif
};
cJSON * get_gpio_list(bool refresh){
    if(!gpio_list){
         gpio_list = cJSON_CreateArray();
    }
    return gpio_list;
}
void register_optional_cmd(void) {
}    

int main(int argc, char **argv){
	return 1;
}

// void register_squeezelite(){
// }

void start_squeezelite(){
}

void register_external(void) {
}

void deregister_external(void) {
}

void decode_restore(int external) {
}

esp_err_t start_ota(const char * bin_url, char * bin_buffer, uint32_t length)
{
		return process_recovery_ota(bin_url,bin_buffer,length);
}
