#include "string.h"
#include "esp_log.h"
#include "targets.h"
static const char * TAG = "targets";
const struct target_s *target_set[] = { &target_muse, NULL };

void target_init(char *target) { 
	ESP_LOGI(TAG,"Checking if target %s needs init",target);
	for (int i = 0; *target && target_set[i]; i++) if (strcasestr(target_set[i]->model, target)) {
		ESP_LOGI(TAG,"Initializing target %s ",target);
		target_set[i]->init();
		break;
	}	
}
