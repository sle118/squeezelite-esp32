/*
 *  audio control callbacks
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "audio_controls.h"
#include "Config.h"
#include "accessors.h"
#include "buttons.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "services.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef esp_err_t(actrls_config_map_handler)(const cJSON* member, actrls_config_t* cur_config, uint32_t offset);
typedef struct {
    char* member;
    uint32_t offset;
    actrls_config_map_handler* handler;
} actrls_config_map_t;

static void actrls_parse_action(const char* name);
static esp_err_t actrls_init_profile(const char* profile_name, bool create);

static esp_err_t actrls_process_press(const sys_btns_press* action, sys_btns_press* cur_config_press);
static esp_err_t actrls_process_button(const sys_btns_btn* button, actrls_config_t* cur_config);
static void control_rotary_handler(void* client, rotary_event_e event, bool long_press);
static void rotary_timer(TimerHandle_t xTimer);

static const char* TAG = "audio controls";
static actrls_config_t* json_config;
cJSON* control_profiles = NULL;
static EXT_RAM_ATTR actrls_t default_controls, current_controls;
static actrls_hook_t *default_hook, *current_hook;
static bool default_raw_controls, current_raw_controls;
static actrls_ir_handler_t *default_ir_handler, *current_ir_handler;

static EXT_RAM_ATTR struct {
    bool long_state;
    bool volume_lock;
    TimerHandle_t timer;
    bool click_pending;
    int left_count;
} rotary;

static const struct ir_action_map_s {
    uint32_t code;
    sys_btns_actions action;
} ir_action_map[] = {
    {0x7689b04f, sys_btns_actions_B_DOWN},
    {0x7689906f, sys_btns_actions_B_LEFT},
    {0x7689d02f, sys_btns_actions_B_RIGHT},
    {0x7689e01f, sys_btns_actions_B_UP},
    {0x768900ff, sys_btns_actions_A_VOLDOWN},
    {0x7689807f, sys_btns_actions_A_VOLUP},
    {0x7689c03f, sys_btns_actions_A_PREV},
    {0x7689a05f, sys_btns_actions_A_NEXT},
    {0x768920df, sys_btns_actions_A_PAUSE},
    {0x768910ef, sys_btns_actions_A_PLAY},
    {0x00, 0x00},
};

/****************************************************************************************
 * This function can be called to map IR codes to default actions
 */
bool actrls_ir_action(uint16_t addr, uint16_t cmd) {
    uint32_t code = (addr << 16) | cmd;
    struct ir_action_map_s const* map = ir_action_map;

    while (map->code && map->code != code)
        map++;

    if (map->code && current_controls[map->action]) {
        current_controls[map->action](true);
        return true;
    } else {
        return false;
    }
}

/****************************************************************************************
 *
 */
static void ir_handler(uint16_t addr, uint16_t cmd) {
    ESP_LOGD(TAG, "recaived IR %04hx:%04hx", addr, cmd);
    if (current_ir_handler) current_ir_handler(addr, cmd);
}

/****************************************************************************************
 *
 */
esp_err_t actrls_init(const char* profile_name) {
    esp_err_t err = ESP_OK;
    sys_dev_ir* ir = NULL;
    sys_btns_rotary* dev_config = NULL;
    if (!SYS_DEV_ROTARY(dev_config)) {
        ESP_LOGD(TAG, "Rotary not configured");
        return ESP_OK;
    }

    char* p;
    int A = -1, B = -1, SW = -1, longpress = 0;
    A = dev_config->A;
    B = dev_config->B;
    SW = dev_config->SW;
    if (A >= 0 && B >= 0) {
        if (dev_config->has_knobonly && dev_config->knobonly.enable) {
            p = strchr(p, '=');

            int double_press = dev_config->knobonly.delay_ms > 0 ? dev_config->knobonly.delay_ms : 350;
            rotary.timer = xTimerCreate("knobTimer", double_press / portTICK_PERIOD_MS, pdFALSE, NULL, rotary_timer);
            longpress = 500;
            ESP_LOGI(TAG, "single knob navigation %d", double_press);
        } else {

            if (dev_config->volume) rotary.volume_lock = true;
            if (dev_config->longpress) longpress = 1000;
        }

        // create rotary (no handling of long press)
        err = create_rotary(NULL, A, B, SW, longpress, control_rotary_handler) ? ESP_OK : ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "Rotary control not configured.");
    }

    if (SYS_DEV_IR(ir) && ir->gpio >= 0 && ir->type != sys_dev_ir_types_IR_UNKNOWN) {
        ESP_LOGD(TAG, "Infrared config found on pin %d, protocol: %s", ir->gpio, sys_dev_ir_types_name(ir->type));
        if (ir->type == sys_dev_ir_types_IR_NEC) {
            create_infrared(ir->gpio, ir_handler, IR_RC5);
        } else {
            create_infrared(ir->gpio, ir_handler, IR_NEC);
        }
    }
    if (!err)
        return actrls_init_profile(profile_name, true);
    else
        return err;
    return err;
}

/****************************************************************************************
 *
 */
static const char* get_action_desc(sys_btns_action* action_detail) {
    if (action_detail->type != sys_btns_actions_REMAP) {
        return sys_btns_actions_name(action_detail->type);
    }
    return STR_OR_ALT(action_detail->profile_name, "");
}

/****************************************************************************************
 *
 */
static void control_handler(void* client, button_event_e event, button_press_e press, bool long_press) {
    actrls_config_t* key = (actrls_config_t*)client;
    sys_btns_action* action_detail;
    static sys_btns_action actionNone = sys_btns_action_init_default;

    switch (press) {
    case BUTTON_NORMAL:
        if (long_press)
            action_detail = (event == BUTTON_PRESSED ? &key->longpress.pressed : &key->longpress.released);
        else
            action_detail = (event == BUTTON_PRESSED ? &key->normal.pressed : &key->normal.released);
        break;
    case BUTTON_SHIFTED:
        if (long_press)
            action_detail = (event == BUTTON_PRESSED ? &key->longshifted.pressed : &key->longshifted.released);
        else
            action_detail = (event == BUTTON_PRESSED ? &key->shifted.pressed : &key->shifted.released);
        break;
    default:
        action_detail = &actionNone;
        break;
    }

    ESP_LOGD(TAG, "control gpio:%u press:%u long:%u event:%u action:%s", key->gpio, press, long_press, event, get_action_desc(action_detail));

    // stop here if control hook served the request
    if (current_hook && (*current_hook)(key->gpio, action_detail, event, press, long_press)) return;

    // in raw mode, we just do normal action press *and* release, there is no longpress nor shift
    if (current_raw_controls && action_detail->type != sys_btns_actions_A_SLEEP) {
        sys_btns_actions action =
            key->normal.has_pressed && key->normal.pressed.type != sys_btns_actions_A_NONE ? key->normal.pressed.type : key->normal.released.type;
        ESP_LOGD(TAG, "calling action %s in raw mode", sys_btns_actions_name(action));
        if (action != sys_btns_actions_A_NONE && current_controls[action]) current_controls[action](event == BUTTON_PRESSED);
        return;
    }

    // otherwise process using configuration
    if (action_detail->type == sys_btns_actions_REMAP) {
        // remap requested
        ESP_LOGD(TAG, "remapping buttons to profile %s", action_detail->profile_name);
        cJSON* profile_obj = cJSON_GetObjectItem(control_profiles, action_detail->profile_name);
        if (profile_obj) {
            actrls_config_t* cur_config = (actrls_config_t*)cJSON_GetStringValue(profile_obj);
            if (cur_config) {
                ESP_LOGD(TAG, "Remapping all the buttons that are found in the new profile");
                while (cur_config->gpio != -1) {
                    ESP_LOGD(TAG, "Remapping button with gpio %u", cur_config->gpio);
                    button_remap((void*)cur_config, cur_config->gpio, control_handler, cur_config->long_press, cur_config->shifter_gpio);
                    cur_config++;
                }
            } else {
                ESP_LOGE(TAG, "Profile %s exists, but is empty. Cannot remap buttons", action_detail->profile_name);
            }
        } else {
            ESP_LOGE(TAG, "Invalid profile name %s. Cannot remap buttons", action_detail->profile_name);
        }
    } else if (action_detail->type == sys_btns_actions_A_SLEEP) {
        ESP_LOGI(TAG, "special sleep button pressed");
        services_sleep_activate(SLEEP_ONKEY);
    } else if (action_detail->type != sys_btns_actions_A_NONE) {
        ESP_LOGD(TAG, "calling action %s", sys_btns_actions_name(action_detail->type));
        if (current_controls[action_detail->type]) (*current_controls[action_detail->type])(event == BUTTON_PRESSED);
    }
}

/****************************************************************************************
 *
 */
static void control_rotary_handler(void* client, rotary_event_e event, bool long_press) {
    sys_btns_actions action = sys_btns_actions_A_NONE;
    bool pressed = true;

    // in raw mode, we just pass rotary events
    if (current_raw_controls) {
        if (event == ROTARY_LEFT)
            (*current_controls[sys_btns_actions_KNOB_LEFT])(true);
        else if (event == ROTARY_RIGHT)
            (*current_controls[sys_btns_actions_KNOB_RIGHT])(true);
        else
            (*current_controls[sys_btns_actions_KNOB_PUSH])(event == ROTARY_PRESSED);
        return;
    }

    switch (event) {
    case ROTARY_LEFT:
        if (rotary.timer) {
            if (rotary.left_count) {
                action = sys_btns_actions_KNOB_LEFT;
                // need to add a left button the first time
                if (rotary.left_count == 1) (*current_controls[sys_btns_actions_KNOB_LEFT])(true);
            }
            xTimerStart(rotary.timer, 20 / portTICK_PERIOD_MS);
            rotary.left_count++;
        } else if (rotary.long_state)
            action = sys_btns_actions_A_PREV;
        else if (rotary.volume_lock)
            action = sys_btns_actions_A_VOLDOWN;
        else
            action = sys_btns_actions_KNOB_LEFT;
        break;
    case ROTARY_RIGHT:
        if (rotary.timer) {
            if (rotary.left_count == 1) {
                action = sys_btns_actions_A_PAUSE;
                rotary.left_count = 0;
                xTimerStop(rotary.timer, 0);
            } else
                action = sys_btns_actions_KNOB_RIGHT;
        } else if (rotary.long_state)
            action = sys_btns_actions_A_NEXT;
        else if (rotary.volume_lock)
            action = sys_btns_actions_A_VOLUP;
        else
            action = sys_btns_actions_KNOB_RIGHT;
        break;
    case ROTARY_PRESSED:
        if (rotary.timer) {
            if (long_press)
                action = sys_btns_actions_A_PLAY;
            else if (rotary.click_pending) {
                action = sys_btns_actions_B_LEFT;
                xTimerStop(rotary.timer, 0);
            } else
                xTimerStart(rotary.timer, 20 / portTICK_PERIOD_MS);
            rotary.click_pending = !rotary.click_pending;
        } else if (long_press)
            rotary.long_state = !rotary.long_state;
        else if (rotary.volume_lock)
            action = sys_btns_actions_A_TOGGLE;
        else
            action = sys_btns_actions_KNOB_PUSH;
        break;
    default:
        break;
    }

    if (action != sys_btns_actions_A_NONE) (*current_controls[action])(pressed);
}

/****************************************************************************************
 *
 */
static void rotary_timer(TimerHandle_t xTimer) {
    if (rotary.click_pending) {
        (*current_controls[sys_btns_actions_KNOB_PUSH])(true);
        rotary.click_pending = false;
    } else if (rotary.left_count) {
        if (rotary.left_count == 1) (*current_controls[sys_btns_actions_KNOB_LEFT])(true);
        rotary.left_count = 0;
    }
}

/****************************************************************************************
 *
 */
static void actrls_parse_action(const char* name) {
    // Check if there is a profile name that has a match
    ESP_LOGD(TAG, "unknown action %s, trying to find matching profile ", name);
    cJSON* existing = cJSON_GetObjectItem(control_profiles, name);

    if (!existing) {
        ESP_LOGD(TAG, "Loading new audio control profile with name: %s", name);
        actrls_init_profile(name, false);
    } else {
        ESP_LOGD(TAG, "Existing profile %s was referenced", name);
    }
}

/****************************************************************************************
 *
 */
static esp_err_t actrls_process_action(const sys_btns_action* action, sys_btns_action* cur_config_act) {
    bool valid_name = action->profile_name && strlen(action->profile_name) > 0;
    cur_config_act->type = action->type;
    if (valid_name) {
        cur_config_act->profile_name = strdup_psram(action->profile_name);
        if (!cur_config_act->profile_name) {
            ESP_LOGE(TAG, "Error allocating memory for action profile name %s", action->profile_name);
            return ESP_ERR_NO_MEM;
        }
    }

    if (action->type == sys_btns_actions_REMAP) {
        if (!valid_name) {
            ESP_LOGE(TAG, "Missing name for action %s", sys_btns_actions_name(action->type));
            return ESP_ERR_INVALID_ARG;
        }
        actrls_parse_action(action->profile_name);
    }
    return ESP_OK;
}
static esp_err_t actrls_process_press(const sys_btns_press* press, sys_btns_press* cur_config_press) {
    esp_err_t err = ESP_OK;
    if (press->has_pressed) {
        cur_config_press->has_pressed = true;
        err = actrls_process_action(&press->pressed, &cur_config_press->pressed);
    }
    if (err == ESP_OK && press->has_released) {
        cur_config_press->has_released = true;
        err = actrls_process_action(&press->released, &cur_config_press->released);
    }
    return err;
}
/****************************************************************************************
 *
 */
static esp_err_t actrls_process_button(const sys_btns_btn* button, actrls_config_t* cur_config) {
    esp_err_t err = ESP_OK;
    if (button->has_gpio && button->gpio.pin >= 0) {
        cur_config->type = button->gpio.level == sys_gpio_lvl_LOW ? BUTTON_LOW : BUTTON_HIGH;
        cur_config->gpio = button->gpio.pin;
    }
    cur_config->pull = button->pull;
    cur_config->debounce = button->debounce;
    cur_config->long_press = button->longduration;
    err = actrls_process_press(&button->normal, &cur_config->normal);
    if (err == ESP_OK) err = actrls_process_press(&button->longpress, &cur_config->longpress);
    if (err == ESP_OK) err = actrls_process_press(&button->shifted, &cur_config->shifted);
    if (err == ESP_OK) err = actrls_process_press(&button->longshifted, &cur_config->longshifted);
    cur_config->shifter_gpio = button->shifter;
    return err;
}

/****************************************************************************************
 *
 */
static actrls_config_t* actrls_init_alloc_structure(const sys_btns_profile* buttons, const char* name) {
    actrls_config_t* json_config = NULL;

    // Check if the main profiles array was created
    if (!control_profiles) {
        control_profiles = cJSON_CreateObject();
    }
    ESP_LOGD(TAG, "config contains %u button definitions", buttons->buttons_count);
    if (buttons->buttons_count != 0) {
        json_config = calloc(sizeof(actrls_config_t) * (buttons->buttons_count + 1), 1);
        if (json_config) {
            json_config[buttons->buttons_count].gpio = -1;
        } else {
            ESP_LOGE(TAG, "Unable to allocate memory to hold configuration for %u buttons ", buttons->buttons_count);
        }
    } else {
        ESP_LOGE(TAG, "No button found in configuration structure");
    }

    // we're cheating here; we're going to store the control profile
    // pointer as a string reference;  this will prevent cJSON
    // from trying to free the structure if we ever want to free the object
    cJSON* new_profile = cJSON_CreateStringReference((const char*)json_config);
    cJSON_AddItemToObject(control_profiles, name, new_profile);

    return json_config;
}

/****************************************************************************************
 *
 */
static void actrls_defaults(actrls_config_t* config) {
    sys_btns_press PressDefault = sys_btns_press_init_default;
    config->type = BUTTON_LOW;
    config->pull = false;
    config->debounce = 0;
    config->long_press = 0;
    config->shifter_gpio = -1;
    config->normal = PressDefault;
    config->longpress = PressDefault;
    config->shifted = PressDefault;
    config->longshifted = PressDefault;
}

/****************************************************************************************
 *
 */
static sys_btns_profile* get_profile(const char* profile_name) {
    ESP_LOGD(TAG, "Looking for profile name %s in %d profile(s)", profile_name, platform->dev.buttons_profiles_count);
    for (int i = 0; i < platform->dev.buttons_profiles_count; i++) {
        if (strcasecmp(platform->dev.buttons_profiles[i].profile_name, profile_name) == 0) {
            ESP_LOGD(TAG, "Found profile name %s", platform->dev.buttons_profiles[i].profile_name);
            return &platform->dev.buttons_profiles[i];
        } else {
            ESP_LOGD(TAG, "Profile name %s doesn't match %s", platform->dev.buttons_profiles[i].profile_name, profile_name);
        }
    }
    ESP_LOGW(TAG, "Button control profile %s not found", profile_name);
    return NULL;
}

/****************************************************************************************
 *
 */
static esp_err_t actrls_init_profile(const char* profile_name, bool create) {
    esp_err_t err = ESP_OK;
    actrls_config_t* cur_config = NULL;
    actrls_config_t* config_root = NULL;
    sys_btns_profile* config;

    if (!profile_name || strlen(profile_name) == 0) {
        ESP_LOGI(TAG, "No control button configured");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Initializing button control profile %s", profile_name);
    config = get_profile(profile_name);
    if (!config) {
        ESP_LOGE(TAG, "Invalid button control profile %s", profile_name);
        goto exit;
    }

    if (config->buttons_count == 0) {
        ESP_LOGE(TAG, "No button found %s", profile_name);
        err = ESP_FAIL;
    } else {
        ESP_LOGD(TAG, "Number of buttons: %d", config->buttons_count);
        cur_config = config_root = actrls_init_alloc_structure(config, profile_name);
        if (!cur_config) {
            ESP_LOGE(TAG, "Config buffer was empty. ");
            err = ESP_FAIL;
            goto exit;
        }
        ESP_LOGD(TAG, "Processing button definitions. ");
        for (int i = 0; i < config->buttons_count; i++) {
            ESP_LOGD(TAG, "Processing button %d of %d for profile %s. ", i + 1, config->buttons_count, profile_name);
            actrls_defaults(cur_config);
            esp_err_t loc_err = actrls_process_button(&config->buttons[i], cur_config);
            err = (err == ESP_OK) ? loc_err : err;
            if (loc_err == ESP_OK) {
                if (create)
                    button_create((void*)cur_config, cur_config->gpio, cur_config->type, cur_config->pull, cur_config->debounce, control_handler,
                        cur_config->long_press, cur_config->shifter_gpio);
            } else {
                ESP_LOGE(TAG, "Error parsing button structure.  Button will not be registered.");
            }

            cur_config++;
        }
    }
    // Now update the global json_config object.  If we are recursively processing menu structures,
    // the last init that completes will assigh the first json config object found, which will match
    // the default config from nvs.
    json_config = config_root;
exit:
    return err;
}
/****************************************************************************************
 *
 */
void actrls_set_default(const actrls_t controls, bool raw_controls, actrls_hook_t* hook, actrls_ir_handler_t* ir_handler) {
    memcpy(default_controls, controls, sizeof(actrls_t));
    memcpy(current_controls, default_controls, sizeof(actrls_t));
    default_hook = current_hook = hook;
    default_raw_controls = current_raw_controls = raw_controls;
    default_ir_handler = current_ir_handler = ir_handler;
}

/****************************************************************************************
 *
 */
void actrls_set(const actrls_t controls, bool raw_controls, actrls_hook_t* hook, actrls_ir_handler_t* ir_handler) {
    memcpy(current_controls, controls, sizeof(actrls_t));
    current_hook = hook;
    current_raw_controls = raw_controls;
    current_ir_handler = ir_handler;
}

/****************************************************************************************
 *
 */
void actrls_unset(void) {
    memcpy(current_controls, default_controls, sizeof(actrls_t));
    current_hook = default_hook;
    current_raw_controls = default_raw_controls;
    current_ir_handler = default_ir_handler;
}
