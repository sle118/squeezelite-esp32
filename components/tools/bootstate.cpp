#include "bootstate.h"
#include "Config.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_spi_flash.h"
#include "messaging.h"
#include "tools.h"
static const char* TAG = "bootstate";

RTC_NOINIT_ATTR uint32_t RebootCounter;
RTC_NOINIT_ATTR uint32_t RecoveryRebootCounter;
RTC_NOINIT_ATTR uint16_t ColdBootIndicatorFlag;
EXT_RAM_ATTR bool is_recovery_running = false;
EXT_RAM_ATTR bool cold_boot = true;
EXT_RAM_ATTR esp_reset_reason_t xReason = ESP_RST_UNKNOWN;
EXT_RAM_ATTR static bool restarting = false;

uint32_t bootstate_read_counter(void) { return RebootCounter; }
uint32_t bootstate_uptate_counter(int32_t xValue) {
    if (RebootCounter > 100) {
        RebootCounter = 0;
        RecoveryRebootCounter = 0;
    }
    RebootCounter = (xValue != 0) ? (RebootCounter + xValue) : 0;
    RecoveryRebootCounter = (xValue != 0) && is_recovery_running ? (RecoveryRebootCounter + xValue) : 0;
    return RebootCounter;
}

void bootstate_handle_boot() {
    if (ColdBootIndicatorFlag != 0xFACE) {
        ESP_LOGI(TAG, "System is booting from power on.");
        cold_boot = true;
        ColdBootIndicatorFlag = 0xFACE;
    } else {
        cold_boot = false;
    }
    const esp_partition_t* running = esp_ota_get_running_partition();

    xReason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason is: %u. Running from partition %s type %s ", xReason, running->label,
        running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY ? "Factory" : "Application");
    is_recovery_running = (running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY);

    if (!is_recovery_running) {
        /* unscheduled restart (HW, Watchdog or similar) thus increment dynamic
         * counter then log current boot statistics as a warning */
        uint32_t Counter = bootstate_uptate_counter(1); // increment counter
        ESP_LOGI(TAG, "Reboot counter=%u\n", Counter);
        if (Counter == 5) {
            guided_factory();
        }
    } else {
        uint32_t Counter = bootstate_uptate_counter(1); // increment counter
        if (RecoveryRebootCounter == 1 && Counter >= 5) {
            // First time we are rebooting in recovery after crashing
            messaging_post_message(MESSAGING_ERROR, MESSAGING_CLASS_SYSTEM,
                "System was forced into recovery mode after crash likely caused by some bad "
                "configuration\n");
        }
        ESP_LOGI(TAG, "Recovery Reboot counter=%u\n", Counter);
        if (RecoveryRebootCounter == 5) {
            ESP_LOGW(TAG, "System rebooted too many times. This could be an indication that "
                          "configuration is corrupted. Erasing config.");
            if (config_erase_config()) {
                config_raise_changed(true);
                guided_factory();
            } else {
                ESP_LOGE(TAG, "Error erasing configuration");
            }
        }
        if (RecoveryRebootCounter > 5) {
            messaging_post_message(MESSAGING_ERROR, MESSAGING_CLASS_SYSTEM,
                "System was forced into recovery mode after crash likely caused by some bad "
                "configuration. Configuration was reset to factory.\n");
        }
    }
}
esp_err_t guided_boot(esp_partition_subtype_t partition_subtype) {
    if (is_recovery_running) {
        if (partition_subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            simple_restart();
        }
    } else {
        if (partition_subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            simple_restart();
        }
    }
    esp_err_t err = ESP_OK;
    const esp_partition_t* partition;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, partition_subtype, NULL);

    if (it == NULL) {
        log_send_messaging(MESSAGING_ERROR, "Reboot failed. Partitions error");
    } else {
        ESP_LOGD(TAG, "Found partition. Getting info.");
        partition = (esp_partition_t*)esp_partition_get(it);
        ESP_LOGD(TAG, "Releasing partition iterator");
        esp_partition_iterator_release(it);
        if (partition != NULL) {
            log_send_messaging(MESSAGING_INFO, "Rebooting to %s", partition->label);
            err = esp_ota_set_boot_partition(partition);
            if (err != ESP_OK) {
                log_send_messaging(MESSAGING_ERROR, "Unable to select partition for reboot: %s", esp_err_to_name(err));
            }
        } else {
            log_send_messaging(MESSAGING_ERROR, "partition type %u not found!  Unable to reboot to recovery.", partition_subtype);
        }
        ESP_LOGD(TAG, "Yielding to other processes");
        taskYIELD();
        simple_restart();
    }

    return ESP_OK;
}
esp_err_t guided_restart_ota() {
    log_send_messaging(MESSAGING_WARNING, "Booting to Squeezelite");
    guided_boot(ESP_PARTITION_SUBTYPE_APP_OTA_0);
    return ESP_FAIL; // return fail.  This should never return... we're rebooting!
}
esp_err_t guided_factory() {
    log_send_messaging(MESSAGING_WARNING, "Booting to recovery");
    guided_boot(ESP_PARTITION_SUBTYPE_APP_FACTORY);
    return ESP_FAIL; // return fail.  This should never return... we're rebooting!
}
void simple_restart() {
    restarting = true;
    log_send_messaging(MESSAGING_WARNING, "Rebooting.");

    TimerHandle_t timer = xTimerCreate("reboot", 1, pdFALSE, nullptr, [](TimerHandle_t xTimer) {
        if (!config_waitcommit()) {
            log_send_messaging(MESSAGING_WARNING, "Waiting for configuration to commit ");
            ESP_LOGD(TAG,"Queuing restart asynchronously to ensure all events are flushed.");
            network_async_reboot(RESTART);
            return;
        }
        vTaskDelay(750 / portTICK_PERIOD_MS);
        esp_restart();
        xTimerDelete(xTimer, portMAX_DELAY);
    });
    xTimerStart(timer, portMAX_DELAY);
}
bool is_restarting() { return restarting; }