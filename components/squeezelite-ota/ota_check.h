#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

// Notify-only OTA update check. Never flashes.
// Finds newest GitHub release tag matching stored variant/branch and
// newer than running firmware. Publishes result to LMS messaging + MQTT.
// Returns ESP_OK and sets *out_json (caller frees, may be NULL) with
// {current, available, url, notes, variant}. ESP_ERR_NOT_FOUND = no update.
esp_err_t ota_check_notify(char **out_json);

// Start the periodic notify-only check task (once). Interval comes from NVS
// ota_check_h (hours, "0" = disabled, re-read every minute so enabling needs
// no reboot). First check runs ~60s after start when network is up.
void ota_start_periodic_check(void);

// Resolve the MQTT broker URI ("mqtt://host:port", malloc'd, caller frees)
// or NULL when disabled/unresolvable. Order: NVS mqtt_broker, mDNS _mqtt._tcp.
char * ota_mqtt_broker_resolve(void);

// Start the persistent MQTT client (once, retries until a broker resolves):
// LWT status topic, subscribes <prefix>/<host>/ota/set for remote commands:
// {"cmd":"check"} runs ota_check_notify, {"cmd":"flash","url":"..."} runs
// the deferred flash hook (still gated by NVS ota_allow_flash).
// NVS mqtt_enable "0" disables entirely.
void ota_mqtt_start(void);

// Persist running firmware identity into NVS (ota_current/ota_variant/
// ota_branch/ota_build). Safe to call repeatedly; no-ops when unchanged.
esp_err_t ota_persist_current_version(void);

// Deferred flash hook for later implementation. Gated by NVS ota_allow_flash.
// Default ("0") refuses with ESP_ERR_INVALID_STATE and logs. When enabled
// ("1") forwards to start_ota(url, NULL, 0).
esp_err_t ota_flash_deferred(const char *url);

#ifdef __cplusplus
}
#endif
