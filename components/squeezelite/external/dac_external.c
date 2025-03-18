/*
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "Config.h"
#include "adac.h"
#include "cJSON.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_check.h"
#include "gpio_exp.h"
#include "inttypes.h"
#include "string.h"
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char TAG[] = "DAC external";

static void speaker(bool active);
static void headset(bool active);
static bool volume(unsigned left, unsigned right) { return false; }
static void power(adac_power_e mode);
static bool init(sys_dac_config* config, i2s_config_t* i2s_config, bool* mck);

// static bool i2c_json_execute(char *set);
static bool i2c_execute_cmd(sys_dac_control_type cmd_type);

const struct adac_s dac_external = {sys_dac_models_I2S, init, adac_deinit, power, speaker, headset, volume};
static int i2c_addr;
extern sys_dac_default_sets* default_dac_sets;
static EXT_RAM_ATTR sys_dac_control_set* i2c_default_controlset = NULL;
static EXT_RAM_ATTR sys_dac_control_set* i2c_controlset = NULL;

/****************************************************************************************
 * init
 */
static bool init(sys_dac_config* config, i2s_config_t* i2s_config, bool* mck) {

#ifdef BYTES_PER_FRAME
    uint32_t bytes_per_frame = BYTES_PER_FRAME;
#else
    uint32_t bytes_per_frame = 4;
#endif
    i2c_addr = adac_init(config);
    if (!i2c_addr) return true;
    ESP_LOGI(TAG, "DAC on I2C @%d", i2c_addr);
    ESP_LOGD(TAG, "Checkinf if there's a default control set for DAC %s in the list of %d presets", sys_dac_models_name(config->model),
        default_dac_sets->sets_count);

    for (int i = 0; i < default_dac_sets->sets_count; i++) {
        ESP_LOGD(TAG, "Checkinf if preset #%d (%s[%d]) matches %s[%d]", i + 1, sys_dac_models_name(default_dac_sets->sets[i].model),
            default_dac_sets->sets[i].bytes_per_frame, sys_dac_models_name(config->model), bytes_per_frame);
        if (default_dac_sets->sets[i].bytes_per_frame == bytes_per_frame && default_dac_sets->sets[i].model == config->model) {
            if (!default_dac_sets->sets[i].valid) {
                ESP_LOGE(TAG, "DAC %s is unsupported with %d bytes per frame", sys_dac_models_name(config->model), bytes_per_frame);
                return false;
            }
            i2c_default_controlset = &default_dac_sets->sets[i].set;
        }
    }

    if (config->has_daccontrolset && config->daccontrolset.commands_count > 0) {
        i2c_controlset = &config->daccontrolset;
    }
    if (!i2c_controlset && !i2c_default_controlset) {
        ESP_LOGE(TAG, "DAC configuration invalid for %s", sys_dac_models_name(config->model));
        return false;
    }
    if (mck) {
        *mck = i2c_controlset != NULL ? i2c_controlset->mclk_needed : i2c_default_controlset->mclk_needed;
        ESP_LOGD(TAG, "Master clock is %s", (*mck) ? "NEEDED" : "NOT NEEDED");
    }
    if (!i2c_execute_cmd(sys_dac_control_type_INIT)) {
        ESP_LOGE(TAG, "could not intialize DAC");
        return false;
    }

    return true;
}

/****************************************************************************************
 * power
 */
static void power(adac_power_e mode) {
    if (mode == ADAC_STANDBY || mode == ADAC_OFF)
        i2c_execute_cmd(sys_dac_control_type_POWER_OFF);
    else
        i2c_execute_cmd(sys_dac_control_type_POWER_ON);
}

/****************************************************************************************
 * speaker
 */
static void speaker(bool active) {
    if (active)
        i2c_execute_cmd(sys_dac_control_type_SPEAKER_ON);
    else
        i2c_execute_cmd(sys_dac_control_type_SPEAKER_OFF);
}

/****************************************************************************************
 * headset
 */
static void headset(bool active) {
    if (active)
        i2c_execute_cmd(sys_dac_control_type_HEADSET_ON);
    else
        i2c_execute_cmd(sys_dac_control_type_HEADSET_OFF);
}

/****************************************************************************************
 *
 */
static const sys_dac_control_itm* i2c_get_controlset_cmd(sys_dac_control_set* set, sys_dac_control_type cmd_type, pb_size_t* items_count) {
    ESP_RETURN_ON_FALSE(set!=NULL,NULL,TAG,"Invalid set");
    ESP_RETURN_ON_FALSE(items_count!=NULL,NULL,TAG,"Invalid items count");
    ESP_LOGD(TAG,"Looking for command %s in control from a list of %d commands",sys_dac_control_type_name(cmd_type),set->commands_count);
    *items_count = 0;
    for (int i = 0; i < set->commands_count; i++) {
        if (set->commands[i].type == cmd_type) {
            *items_count = set->commands[i].items_count;
            return set->commands[i].items;
        }
    }
    ESP_LOGD(TAG,"No control set found for command %s",sys_dac_control_type_name(cmd_type));
    return NULL;
}

/****************************************************************************************
 *
 */
bool i2c_execute_cmd(sys_dac_control_type cmd_type) {
    pb_size_t items_count;
    const sys_dac_control_itm* cmd = NULL;
    esp_err_t err = ESP_OK;
    ESP_LOGD(TAG, "Getting control set for command %s", sys_dac_control_type_name(cmd_type));
    if(i2c_controlset){
        cmd = i2c_get_controlset_cmd(i2c_controlset, cmd_type, &items_count);
    }
    if (!cmd || items_count == 0) {
        ESP_LOGD(TAG, "Not found. Checking if defaults are known %s", sys_dac_control_type_name(cmd_type));
        cmd = i2c_get_controlset_cmd(i2c_default_controlset, cmd_type, &items_count);
    }
    if (!cmd || items_count == 0) {
        ESP_LOGD(TAG, "No commands for %s", sys_dac_control_type_name(cmd_type));
        return true;
    }
    for (pb_size_t i = 0; i < items_count && err==ESP_OK; i++) {
        switch (cmd->which_item_type) {
        case sys_dac_control_itm_reg_action_tag:
            ESP_LOGD(TAG, "Setting reg %d mode %s value %d ", cmd->item_type.reg_action.reg,
                sys_dac_control_mode_name(cmd->item_type.reg_action.mode), cmd->item_type.reg_action.val);
            if (cmd->item_type.reg_action.mode == sys_dac_control_mode_NOTHING) {
                err = adac_write_byte(i2c_addr, cmd->item_type.reg_action.reg, cmd->item_type.reg_action.val);
            } else if (cmd->item_type.reg_action.mode == sys_dac_control_mode_OR) {
                uint8_t data = adac_read_byte(i2c_addr, cmd->item_type.reg_action.reg);
                data |= (uint8_t)cmd->item_type.reg_action.val;
                err = adac_write_byte(i2c_addr, cmd->item_type.reg_action.reg, data);
            } else if (cmd->item_type.reg_action.mode == sys_dac_control_mode_AND) {
                uint8_t data = adac_read_byte(i2c_addr, cmd->item_type.reg_action.reg);
                data &= (uint8_t)cmd->item_type.reg_action.val;
                err = adac_write_byte(i2c_addr, cmd->item_type.reg_action.reg, data);
            }

            break;
        case sys_dac_control_itm_regs_action_tag:
            ESP_LOGD(TAG, "Setting reg %d with %d byte(s)", cmd->item_type.regs_action.reg, cmd->item_type.regs_action.vals_count);
            err = adac_write(i2c_addr, cmd->item_type.regs_action.reg, cmd->item_type.regs_action.vals, cmd->item_type.regs_action.vals_count);
            break;
        case sys_dac_control_itm_gpio_action_tag:
            ESP_LOGI(TAG, "set GPIO %d at %s", cmd->item_type.gpio_action.gpio, sys_dac_control_lvl_name(cmd->item_type.gpio_action.level));
            err = gpio_set_direction_x(cmd->item_type.gpio_action.gpio, GPIO_MODE_OUTPUT);
            if(err == ESP_OK) err = gpio_set_level_x(cmd->item_type.gpio_action.gpio, cmd->item_type.gpio_action.level - sys_dac_control_lvl_LV_0);
            break;
        case sys_dac_control_itm_delay_action_tag:
            vTaskDelay(pdMS_TO_TICKS(cmd->item_type.delay_action.delay));
            ESP_LOGI(TAG, "DAC waiting %" PRIu64 " ms", cmd->item_type.delay_action.delay);
            break;

        default:
            break;
        }
    }

    return (err==ESP_OK);
}
