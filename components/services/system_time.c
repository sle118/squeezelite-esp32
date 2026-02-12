/* LwIP SNTP example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "Config.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "tools.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>
static const char* TAG = "system_time";

void system_time_init(void) {
    char strftime_buf[64];
    time_t now;
    struct tm timeinfo;
    const char* timezone = platform->has_services && platform->services.timezone && strlen(platform->services.timezone) > 0
                               ? platform->services.timezone
                               : "EST5EDT,M3.2.0/2,M11.1.0";
    setenv("TZ", timezone, 1);
    tzset();
    time(&now);
    localtime_r(&now, &timeinfo);
    // Is time set? If not, tm_year will be (1970 - 1900).
    if(timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        sntp_servermode_dhcp(2);
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();

    } else {
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Current date/time is: %s for time zone %s", strftime_buf, timezone);
    }
}
