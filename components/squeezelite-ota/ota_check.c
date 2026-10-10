#include "ota_check.h"
#include "squeezelite-ota.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "mdns.h"
#include "cJSON.h"
#include "nvs_utilities.h"
#include "platform_config.h"
#include "messaging.h"
#include "tools.h"

static const char *TAG = "ota_check";
#define OTA_CHECK_BUF_MAX (24 * 1024)

static char * s_dup_range(const char *a, const char *b) {
    size_t n = (size_t)(b - a);
    char *o = malloc(n + 1);
    if (o) { memcpy(o, a, n); o[n] = 0; }
    return o;
}

// Parse "I2S-4MFlash.16.1769.master-v4.3" ->
// variant="I2S-4MFlash", build_major=16, build_minor=1769, branch="master", ver="v4.3".
// Tolerant: missing parts leave defaults.
static void parse_fw_version(const char *v, char **variant, int *bmaj, int *bmin, char **branch, char **ver) {
    if (variant) *variant = NULL;
    if (branch) *branch = NULL;
    if (ver) *ver = NULL;
    if (bmaj) *bmaj = 0;
    if (bmin) *bmin = 0;
    if (!v || !*v) return;
    const char *dot1 = strchr(v, '.');
    if (!dot1) { if (variant) *variant = strdup(v); return; }
    if (variant) *variant = s_dup_range(v, dot1);
    int a = 0, b = 0, n = 0;
    if (sscanf(dot1 + 1, "%d.%d%n", &a, &b, &n) == 2) {
        if (bmaj) *bmaj = a;
        if (bmin) *bmin = b;
        const char *rest = dot1 + 1 + n;
        if (*rest == '.') rest++;
        // rest looks like "master-v4.3" or "master"
        const char *dash = strstr(rest, "-v");
        if (dash) {
            if (branch) *branch = s_dup_range(rest, dash);
            if (ver) *ver = strdup(dash + 1); // "v4.3"
        } else if (*rest) {
            if (branch) *branch = strdup(rest);
        }
    }
}

static int build_is_newer(int cmaj, int cmin, int rmaj, int rmin) {
    if (rmaj != cmaj) return rmaj > cmaj;
    return rmin > cmin;
}

static char * nvs_str_or(const char *key, const char *dflt) {
    char *v = config_alloc_get(NVS_TYPE_STR, key);
    if (!v && dflt) v = strdup(dflt);
    return v;
}

esp_err_t ota_persist_current_version(void) {
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(esp_ota_get_running_partition(), &desc) != ESP_OK) return ESP_FAIL;
    char *variant = NULL, *branch = NULL, *ver = NULL;
    int bmaj = 0, bmin = 0;
    parse_fw_version(desc.version, &variant, &bmaj, &bmin, &branch, &ver);
    char build[32];
    snprintf(build, sizeof(build), "%d.%d", bmaj, bmin);
    char *old = config_alloc_get(NVS_TYPE_STR, "ota_current");
    bool changed = !old || strcmp(old, desc.version) != 0;
    FREE_AND_NULL(old);
    if (!changed) { free(variant); free(branch); free(ver); return ESP_OK; }
    config_set_value(NVS_TYPE_STR, "ota_current", desc.version);
    if (variant) config_set_value(NVS_TYPE_STR, "ota_variant", variant);
    if (branch) config_set_value(NVS_TYPE_STR, "ota_branch", branch);
    if (ver) config_set_value(NVS_TYPE_STR, "ota_ver", ver);
    config_set_value(NVS_TYPE_STR, "ota_build", build);
    wait_for_commit();
    ESP_LOGI(TAG, "Persisted fw identity: current=%s variant=%s build=%s branch=%s ver=%s",
        desc.version, variant ? variant : "?", build, branch ? branch : "?", ver ? ver : "?");
    free(variant); free(branch); free(ver);
    return ESP_OK;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} http_buf_t;

static esp_err_t http_evt(esp_http_client_event_t *evt) {
    http_buf_t *b = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && b) {
        size_t need = b->len + (size_t)evt->data_len + 1;
        if (need > OTA_CHECK_BUF_MAX) return ESP_FAIL;
        if (need > b->cap) {
            size_t ncap = need + 4096;
            char *nd = realloc(b->data, ncap);
            if (!nd) return ESP_FAIL;
            b->data = nd; b->cap = ncap;
        }
        memcpy(b->data + b->len, evt->data, evt->data_len);
        b->len += evt->data_len;
        b->data[b->len] = 0;
    }
    return ESP_OK;
}

// Broker resolution order: explicit NVS mqtt_broker -> mDNS _mqtt._tcp
// discovery (e.g. Mosquitto/Home Assistant advertising MQTT) -> disabled.
// Returns malloc'd "mqtt://host:port" or NULL. Caller frees.
char * ota_mqtt_broker_resolve(void) {
    char *cfg = config_alloc_get(NVS_TYPE_STR, "mqtt_broker");
    if (cfg && strlen(cfg)) return cfg; // explicit config wins
    FREE_AND_NULL(cfg);
    mdns_result_t *results = NULL;
    // Short query so a missing broker never stalls the OTA check.
    if (mdns_query_ptr("_mqtt", "_tcp", 2000, 4, &results) != ESP_OK || !results) {
        ESP_LOGD(TAG, "No mDNS _mqtt._tcp broker found; MQTT notify disabled");
        return NULL;
    }
    char *uri = NULL;
    mdns_result_t *r = results;
    // Use advertised hostname (mDNS-resolvable, survives DHCP changes).
    // Only stable mdns_result_t fields are used (hostname/port/next).
    while (r) {
        if (r->hostname && *r->hostname && r->port > 0) {
            char hostbuf[64];
            snprintf(hostbuf, sizeof(hostbuf), "%s", r->hostname);
            char *dot = strstr(hostbuf, ".local");
            if (dot) *dot = 0;
            uri = malloc(strlen(hostbuf) + 24);
            if (uri) sprintf(uri, "mqtt://%s:%u", hostbuf, r->port);
            ESP_LOGI(TAG, "mDNS MQTT broker: %s", uri ? uri : "?");
            break;
        }
        r = r->next;
    }
    mdns_query_results_free(results);
    return uri;
}

// Best-effort MQTT publish. Disabled when no broker resolves.
static void mqtt_publish_ota(const char *payload_json) {
    char *broker = ota_mqtt_broker_resolve();
    if (!broker) return;
    char *prefix = nvs_str_or("mqtt_prefix", "squeezelite");
    char *host = nvs_str_or("host_name", "squeezelite");
    // sanitize hostname for topics: cut at first space/comma
    for (char *p = host; *p; p++) if (*p == ' ' || *p == ',') *p = '_';
    char topic[160], disc_topic[180];
    snprintf(topic, sizeof(topic), "%s/%s/ota", prefix, host);
    snprintf(disc_topic, sizeof(disc_topic), "homeassistant/sensor/%s_ota/config", host);
    char *user = config_alloc_get(NVS_TYPE_STR, "mqtt_user");
    char *pass = config_alloc_get(NVS_TYPE_STR, "mqtt_pass");
    // IDF v4.4 (this project: idfv448) uses flat .uri/.username/.password
    esp_mqtt_client_config_t cfg = { 0 };
    cfg.uri = broker;
    if (user && strlen(user)) cfg.username = user;
    if (pass && strlen(pass)) cfg.password = pass;
    esp_mqtt_client_handle_t c = esp_mqtt_client_init(&cfg);
    if (!c) goto out;
    if (esp_mqtt_client_start(c) != ESP_OK) { esp_mqtt_client_destroy(c); goto out; }
    // esp-mqtt connects async with no sync-wait API; publish() only queues.
    // Give the client a fixed window to connect, publish state + discovery,
    // then flush. Best-effort: if the broker is unreachable both are dropped.
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (esp_mqtt_client_publish(c, topic, payload_json, 0, 1, 1) < 0) {
        ESP_LOGW(TAG, "MQTT publish to %s failed", topic);
    }
    // HA discovery (retained): value_template reads .available
    {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name", "Squeezelite OTA");
        cJSON_AddStringToObject(d, "stat_t", topic);
        cJSON_AddStringToObject(d, "val_tpl", "{{ value_json.available }}");
        cJSON_AddStringToObject(d, "json_attr_t", topic);
        char *ids = malloc(strlen(host) + 16);
        if (ids) { sprintf(ids, "%s_ota", host); cJSON_AddStringToObject(d, "uniq_id", ids); free(ids); }
        char *ds = cJSON_PrintUnformatted(d);
        if (ds) { esp_mqtt_client_publish(c, disc_topic, ds, 0, 1, 1); free(ds); }
        cJSON_Delete(d);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_mqtt_client_stop(c);
    esp_mqtt_client_destroy(c);
out:
    FREE_AND_NULL(broker); FREE_AND_NULL(prefix); FREE_AND_NULL(host);
    FREE_AND_NULL(user); FREE_AND_NULL(pass);
}

esp_err_t ota_check_notify(char **out_json) {
    if (out_json) *out_json = NULL;
    ota_persist_current_version();
    char *rel_api = nvs_str_or("rel_api", CONFIG_RELEASE_API);
    char *cur = nvs_str_or("ota_current", "");
    char *variant_cfg = nvs_str_or("ota_variant", "");
    char *branch_cfg = nvs_str_or("ota_branch", "master");
    char *cv = NULL, *cb = NULL, *cver = NULL;
    int cmaj = 0, cmin = 0;
    parse_fw_version(cur, &cv, &cmaj, &cmin, &cb, &cver);
    // fall back to live desc when NVS empty
    if (!cv || !*cv) {
        esp_app_desc_t d;
        if (esp_ota_get_partition_description(esp_ota_get_running_partition(), &d) == ESP_OK) {
            free(cv); free(cb); free(cver);
            parse_fw_version(d.version, &cv, &cmaj, &cmin, &cb, &cver);
            free(cur); cur = strdup(d.version);
        }
    }
    const char *want_variant = (variant_cfg && *variant_cfg) ? variant_cfg : (cv ? cv : "");
    const char *want_branch = (branch_cfg && *branch_cfg) ? branch_cfg : "";

    http_buf_t b = {0};
    esp_http_client_config_t cfg = { .url = rel_api, .event_handler = http_evt, .user_data = &b,
        .timeout_ms = 8000, .max_redirection_count = 3 };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_err_t err = cli ? esp_http_client_perform(cli) : ESP_FAIL;
    int status = cli ? esp_http_client_get_status_code(cli) : -1;
    if (cli) { esp_http_client_cleanup(cli); }
    if (err != ESP_OK || status != 200 || !b.data) {
        ESP_LOGW(TAG, "Release API fetch failed: %s status=%d", esp_err_to_name(err), status);
        messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_OTA, "OTA check failed (API unreachable)");
        err = ESP_FAIL;
        goto done;
    }
    cJSON *arr = cJSON_Parse(b.data);
    if (!arr || !cJSON_IsArray(arr)) {
        ESP_LOGW(TAG, "Release API JSON invalid");
        messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_OTA, "OTA check failed (bad JSON)");
        cJSON_Delete(arr);
        err = ESP_FAIL;
        goto done;
    }
    // Track best (newest build) candidate matching variant+branch
    const char *best_tag = NULL, *best_url = NULL, *best_notes = NULL;
    int best_maj = cmaj, best_min = cmin;
    cJSON *best_item = NULL;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, arr) {
        const char *tag = NULL;
        cJSON *t = cJSON_GetObjectItemCaseSensitive(it, "tag_name");
        if (t && cJSON_IsString(t)) tag = t->valuestring;
        if (!tag || strncmp(tag, want_variant, strlen(want_variant)) != 0) continue;
        if (want_branch[0] && !strstr(tag, want_branch)) continue;
        char *rv = NULL, *rb = NULL, *rver = NULL;
        int rmaj = 0, rmin = 0;
        parse_fw_version(tag, &rv, &rmaj, &rmin, &rb, &rver);
        bool newer = build_is_newer(cmaj, cmin, rmaj, rmin);
        // same build but newer -v suffix also counts (lexicographic)
        if (!newer && rmaj == cmaj && rmin == cmin && rver && cver && strcmp(rver, cver) > 0) newer = true;
        if (newer && (!best_item || build_is_newer(best_maj, best_min, rmaj, rmin))) {
            best_maj = rmaj; best_min = rmin;
            best_item = it;
            best_tag = tag;
            cJSON *assets = cJSON_GetObjectItemCaseSensitive(it, "assets");
            best_url = NULL; best_notes = NULL;
            if (assets && cJSON_IsArray(assets) && cJSON_GetArraySize(assets) > 0) {
                cJSON *a0 = cJSON_GetArrayItem(assets, 0);
                cJSON *u = cJSON_GetObjectItemCaseSensitive(a0, "browser_download_url");
                if (u && cJSON_IsString(u)) best_url = u->valuestring;
            }
            // fallback: tarball_url / html_url
            if (!best_url) {
                cJSON *u = cJSON_GetObjectItemCaseSensitive(it, "html_url");
                if (u && cJSON_IsString(u)) best_url = u->valuestring;
            }
            cJSON *bn = cJSON_GetObjectItemCaseSensitive(it, "body");
            if (bn && cJSON_IsString(bn)) best_notes = bn->valuestring;
        }
        free(rv); free(rb); free(rver);
    }
    {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "ota_current", cur ? cur : "");
        cJSON_AddStringToObject(msg, "ota_variant", want_variant);
        if (best_item) {
            cJSON_AddStringToObject(msg, "available", best_tag ? best_tag : "");
            cJSON_AddStringToObject(msg, "url", best_url ? best_url : "");
            cJSON_AddStringToObject(msg, "notes", best_notes ? best_notes : "");
            char *js = cJSON_PrintUnformatted(msg);
            messaging_post_message(MESSAGING_INFO, MESSAGING_CLASS_OTA,
                "OTA update available: %s (current %s)", best_tag, cur);
            cJSON *mj = cJSON_CreateObject();
            cJSON_AddStringToObject(mj, "ota_dsc", js ? js : "");
            char *mjstr = cJSON_PrintUnformatted(mj);
            if (mjstr) { messaging_post_text(MESSAGING_INFO, MESSAGING_CLASS_OTA, mjstr); free(mjstr); }
            cJSON_Delete(mj);
            mqtt_publish_ota(js ? js : "{}");
            if (out_json) *out_json = js; else free(js);
            ESP_LOGI(TAG, "Update available: %s -> %s", cur, best_tag);
        } else {
            cJSON_AddStringToObject(msg, "available", "");
            char *js = cJSON_PrintUnformatted(msg);
            ESP_LOGI(TAG, "No OTA update (current %s, variant %s)", cur, want_variant);
            messaging_post_message(MESSAGING_INFO, MESSAGING_CLASS_OTA, "OTA check: up to date (%s)", cur);
            mqtt_publish_ota(js ? js : "{}");
            if (out_json) *out_json = js; else free(js);
            err = ESP_ERR_NOT_FOUND;
        }
        cJSON_Delete(msg);
    }
    cJSON_Delete(arr);
done:
    free(b.data);
    free(rel_api); free(cur); free(variant_cfg); free(branch_cfg);
    free(cv); free(cb); free(cver);
    return err;
}

esp_err_t ota_flash_deferred(const char *url) {    if (!url || !*url) return ESP_ERR_INVALID_ARG;
    char *allow = config_alloc_get(NVS_TYPE_STR, "ota_allow_flash");
    bool ok = allow && (!strcmp(allow, "1") || !strcasecmp(allow, "y"));
    FREE_AND_NULL(allow);
    if (!ok) {
        ESP_LOGW(TAG, "Flash deferred (notify-only). Set NVS ota_allow_flash=1 to enable later. url=%s", url);
        messaging_post_message(MESSAGING_WARNING, MESSAGING_CLASS_OTA,
            "Flash deferred (notify-only). External flash command reserved for later.");
        return ESP_ERR_INVALID_STATE;
    }
    return start_ota(url, NULL, 0);
}

static bool ota_net_up(void) {
    wifi_ap_record_t ap;
    memset(&ap, 0, sizeof(ap));
    // ESP_OK only while associated to an AP (no dependency on main image,
    // so the recovery build keeps linking).
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

#define OTA_PERIODIC_STACK 8192
#define OTA_PERIODIC_PRIO 5
static void ota_periodic_task(void *arg) {
    (void)arg;
    // Grace period: let network/LMS settle before the first check.
    vTaskDelay(pdMS_TO_TICKS(60000));
    for (;;) {
        // Persistent MQTT client (LWT + remote set-topic). No-op when already
        // running or disabled; retries broker resolution until it succeeds.
        ota_mqtt_start();
        char *h = config_alloc_get(NVS_TYPE_STR, "ota_check_h");
        long hours = h ? atol(h) : 0;
        FREE_AND_NULL(h);
        if (hours <= 0) {
            // Disabled: re-read every minute so enabling needs no reboot.
            vTaskDelay(pdMS_TO_TICKS(60000));
            continue;
        }
        if (hours > 168) hours = 168; // cap: 1 week
        if (ota_net_up()) {
            esp_err_t err = ota_check_notify(NULL);
            if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
                ESP_LOGW(TAG, "Periodic OTA check failed: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGD(TAG, "Skipping periodic OTA check: no wifi association");
        }
        vTaskDelay(pdMS_TO_TICKS((uint32_t)hours * 3600UL * 1000UL));
    }
}

void ota_start_periodic_check(void) {
    static bool started = false;
    if (started) return;
    started = true;
    if (xTaskCreate(&ota_periodic_task, "ota_periodic", OTA_PERIODIC_STACK,
                    NULL, OTA_PERIODIC_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start periodic OTA check task");
        started = false;
    }
}
