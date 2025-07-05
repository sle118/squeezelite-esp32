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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "gpio_exp.h"
#include "cJSON.h"
#include "platform_config.h"
#include "adac.h"

static const char TAG[] = "DAC external";

static void speaker(bool active);
static void headset(bool active);
static bool volume(unsigned left, unsigned right) { return false; }
static void power(adac_power_e mode);
static bool init(char *config, int i2c_port_num, i2s_config_t *i2s_config, bool *mck);

static bool i2c_json_execute(char *set);

const struct adac_s dac_external = { "i2s", init, adac_deinit, power, speaker, headset, volume };
static cJSON *i2c_json;
static int i2c_addr;

static const struct {
	char *model;
	bool mclk;
	char *controlset;
} codecs[] = {
	{ "es8388", true,
		"{\"init\":[ \
			{\"reg\":8,\"val\":0}, {\"reg\":2,\"val\":243}, {\"reg\":43,\"val\":128}, {\"reg\":0,\"val\":5}, \
			{\"reg\":1,\"val\":64}, {\"reg\":4,\"val\":60},"
#if BYTES_PER_FRAME == 8
		   "{\"reg\":23,\"val\":32},"
#else
		   "{\"reg\":23,\"val\":24},"
#endif
		   "{\"reg\":24,\"val\":2}, \
			{\"reg\":26,\"val\":0}, {\"reg\":27,\"val\":0}, {\"reg\":25,\"val\":50}, {\"reg\":38,\"val\":0},		\
			{\"reg\":39,\"val\":184}, {\"reg\":42,\"val\":184}, {\"reg\":46,\"val\":30}, {\"reg\":47,\"val\":30},	\
			{\"reg\":48,\"val\":30}, {\"reg\":49,\"val\":30}, {\"reg\":2,\"val\":170}]}" },
	{ NULL, false, NULL }
};

/****************************************************************************************
 * init
 */
static bool init(char *config, int i2c_port_num, i2s_config_t *i2s_config, bool *mck) {
	char *p;

	i2c_addr = adac_init(config, i2c_port_num);

	ESP_LOGI(TAG, "DAC on I2C @%d", i2c_addr);

	p = config_alloc_get_str("dac_controlset", CONFIG_DAC_CONTROLSET, NULL);

	if ((!p || !*p) && (p = strcasestr(config, "model")) != NULL) {
		char model[32] = "";
		int i;
		sscanf(p, "%*[^=]=%31[^,]", model);
		for (i = 0; *model && ((p = codecs[i].controlset) != NULL) && strcasecmp(codecs[i].model, model); i++);
		if (p) *mck = codecs[i].mclk;
	}

	i2c_json = cJSON_Parse(p);

	if (!i2c_json) {
		ESP_LOGW(TAG, "no i2c controlset found");
		return true;
	}

	if (!i2c_json_execute("init")) {
		ESP_LOGE(TAG, "could not intialize DAC");
		return false;
	}

	return true;
}

/****************************************************************************************
 * power
 */
static void power(adac_power_e mode) {
	if (mode == ADAC_STANDBY || mode == ADAC_OFF) i2c_json_execute("poweroff");
	else i2c_json_execute("poweron");
}

/****************************************************************************************
 * speaker
 */
static void speaker(bool active) {
	if (active) i2c_json_execute("speakeron");
	else i2c_json_execute("speakeroff");
}

/****************************************************************************************
 * headset
 */
static void headset(bool active) {
	if (active) i2c_json_execute("headseton");
	else i2c_json_execute("headsetoff");
}

/****************************************************************************************
 *
 */

typedef enum {
	I2C_JSON_ARRAY_INVALID = 0,
	I2C_JSON_ARRAY_OBJECTS,
	I2C_JSON_ARRAY_FLAT
    } i2c_json_array_mode_t;

static i2c_json_array_mode_t i2c_json_array_mode(cJSON *array) {
	if (!cJSON_IsArray(array)) return I2C_JSON_ARRAY_INVALID;

	bool all_objects = true;
	bool none_objects = true;

	for (cJSON *item = array->child; item; item = item->next) {
		if (cJSON_IsObject(item)) {
			none_objects = false;
		} else {
			all_objects = false;
		}

		if (!all_objects && !none_objects) {
			return I2C_JSON_ARRAY_INVALID;
		}
	}

	return all_objects ? I2C_JSON_ARRAY_OBJECTS :
	       none_objects ? I2C_JSON_ARRAY_FLAT :
	       I2C_JSON_ARRAY_INVALID;
}

bool i2c_json_execute(char *set) {
	cJSON *json_set = cJSON_GetObjectItemCaseSensitive(i2c_json, set);
	if (!json_set) return true;

	if (!cJSON_IsArray(json_set)) {
		ESP_LOGE(TAG, "i2c_json_execute must be called on a JSON array");
		return false;
	}

	i2c_json_array_mode_t mode = i2c_json_array_mode(json_set);
	if (mode == I2C_JSON_ARRAY_FLAT) {
		return i2c_json_execute_array(json_set);
	} else if (mode == I2C_JSON_ARRAY_OBJECTS) {
		return i2c_json_execute_objects(json_set);
	} else {
		ESP_LOGE(TAG, "Mixed array elements (objects + scalars) not supported");
		return false;
	}
}

static bool i2c_json_execute_objects(cJSON *json_set) {
    if (!cJSON_IsArray(json_set)) return false;

	cJSON_ArrayForEach(item, json_set) {
        cJSON *action;

        // is this a delay
        if ((action = cJSON_GetObjectItemCaseSensitive(item, "delay")) != NULL) {
            vTaskDelay(pdMS_TO_TICKS(action->valueint));
            ESP_LOGI(TAG, "DAC waiting %d ms", action->valueint);
            continue;
        }

        // is this a gpio toggle
        if ((action = cJSON_GetObjectItemCaseSensitive(item, "gpio")) != NULL) {
            cJSON *level = cJSON_GetObjectItemCaseSensitive(item, "level");
            ESP_LOGI(TAG, "set GPIO %d at %d", action->valueint, level->valueint);
            if (action->valueint < GPIO_NUM_MAX) gpio_pad_select_gpio(action->valueint);
            gpio_set_direction_x(action->valueint, GPIO_MODE_OUTPUT);
            gpio_set_level_x(action->valueint, level->valueint);
            continue;
        }

		action= cJSON_GetObjectItemCaseSensitive(item, "reg");
		cJSON *val = cJSON_GetObjectItemCaseSensitive(item, "val");

        // this is gpio register setting or crap
		if (cJSON_IsArray(val)) {
			cJSON *value;
			uint8_t *data = malloc(cJSON_GetArraySize(val));
			int count = 0;

			if (!data) continue;

			cJSON_ArrayForEach(value, val) {
				data[count++] = value->valueint;
			}

			adac_write(i2c_addr, action->valueint, data, count);
			free(data);
		} else {
			cJSON *mode = cJSON_GetObjectItemCaseSensitive(item, "mode");

			if (!action || !val) continue;

			if (!mode) {
				adac_write_byte(i2c_addr, action->valueint, val->valueint);
			} else if (!strcasecmp(mode->valuestring, "or")) {
				uint8_t data = adac_read_byte(i2c_addr, action->valueint);
				data |= (uint8_t) val->valueint;
				adac_write_byte(i2c_addr, action->valueint, data);
			} else if (!strcasecmp(mode->valuestring, "and")) {
				uint8_t data = adac_read_byte(i2c_addr, action->valueint);
				data &= (uint8_t) val->valueint;
				adac_write_byte(i2c_addr, action->valueint, data);
			}
		}
	}

	return true;
}


typedef enum {
	CMD_NONE = 0,
	CMD_DELAY,
	CMD_GPIO,
	CMD_AND,
	CMD_OR
    } i2c_cmd_type_t;

static i2c_cmd_type_t i2c_resolve_command(const char *s) {
	if (!strcasecmp(s, "d")) return CMD_DELAY;
	if (!strcasecmp(s, "g")) return CMD_GPIO;
	if (!strcmp(s, "&")) return CMD_AND;
	if (!strcmp(s, "|")) return CMD_OR;
	return CMD_NONE;
}

/**
 * @brief Execute a flattened I2C instruction sequence using compressed JSON format.
 *
 * This function accepts an array containing a mixture of register operations,
 * delays, bitwise modifications, and burst writes in a compressed format.
 *
 * Supported syntax elements (in a single flat array):
 *
 * - Register write:
 *     [reg, val]
 *     Writes a single byte value to a register.
 *
 * - Burst write:
 *     [reg, [val1, val2, ..., valN]]
 *     Writes multiple bytes to a register in one burst. The second item must be a JSON array.
 *
 * - Delay:
 *     ["d", duration_ms]
 *     Pauses execution for the specified number of milliseconds.
 *
 * - Bitwise operations:
 *     ["&", reg, val] — Read-modify-write: AND value into register
 *     ["|", reg, val] — Read-modify-write: OR value into register
 *
 * Notes:
 * - All entries are parsed in order from the start of the array.
 * - Invalid formatting or unexpected types will cause the function to return `false`.
 * - Only numeric register addresses and values are allowed.
 *
 * Example input:
 *     [
 *       0, 128,
 *       "d", 50,
 *       "|", 2, 240,
 *       "&", 2, 15,
 *       "g", 22, 0,
 *       25, [4, 5, 6, 7],
 *       1, 0
 *     ]
 *
 * This will:
 *  - Write 128 to reg 0
 *  - Delay 50ms
 *  - OR 240 into reg 2
 *  - AND 15 into reg 2
 *  - Set GPIO 22 to level 0
 *  - Burst write [4,5,6,7] to reg 25
 *  - Write 0 to reg 1
 */

static bool i2c_json_execute_array(cJSON *array) {
    if (!cJSON_IsArray(array)) return false;

    cJSON *cursor = array->child;
    while (cursor) {
       if (cJSON_IsString(cursor)) {
            i2c_cmd_type_t cmd = i2c_resolve_command(cursor->valuestring);
            switch (cmd) {
                case CMD_DELAY: {
                    cursor = cursor->next;
                    if (!cursor || !cJSON_IsNumber(cursor)) return false;
                    vTaskDelay(pdMS_TO_TICKS(cursor->valueint));
                    ESP_LOGI(TAG, "DAC waiting %d ms", cursor->valueint);
                    cursor = cursor->next;
                    break;
                }

                case CMD_GPIO: {
                    cursor = cursor->next;
                    if (!cursor || !cJSON_IsNumber(cursor)) return false;
                    int gpio = cursor->valueint;

                    cursor = cursor->next;
                    if (!cursor || !cJSON_IsNumber(cursor)) return false;
                    int level = cursor->valueint;

                    if (gpio < GPIO_NUM_MAX) {
                        gpio_pad_select_gpio(gpio);
                        gpio_set_direction_x(gpio, GPIO_MODE_OUTPUT);
                        gpio_set_level_x(gpio, level);
                    }

                    ESP_LOGI(TAG, "Set GPIO %d to %d", gpio, level);
                    cursor = cursor->next;
                    break;
                }

                case CMD_AND:
                case CMD_OR: {
                    cursor = cursor->next;
                    if (!cursor || !cJSON_IsNumber(cursor)) return false;
                    int reg = cursor->valueint;

                    cursor = cursor->next;
                    if (!cursor || !cJSON_IsNumber(cursor)) return false;
                    int val = cursor->valueint;

                    uint8_t data = adac_read_byte(i2c_addr, reg);
                    data = (cmd == CMD_AND) ? (data & val) : (data | val);
                    adac_write_byte(i2c_addr, reg, data);
                    cursor = cursor->next;
                    break;
                }

                default:
                    return false;
            }
            continue;
        }


        // ── Burst write: reg, [val1, val2, ...] ──
        if (cJSON_IsNumber(cursor)) {
            int reg = cursor->valueint;
            cursor = cursor->next;
            if (!cursor) return false;

            if (cJSON_IsArray(cursor)) {
                int len = cJSON_GetArraySize(cursor);
                if (len == 0) return false;
                uint8_t *data = malloc(len);
                if (!data) return false;

                cJSON *val_item = cursor->child;
                for (int i = 0; i < len; i++) {
                    if (!val_item || !cJSON_IsNumber(val_item)) {
                        free(data);
                        return false;
                    }
                    data[i] = val_item->valueint;
                    val_item = val_item->next;
                }

                adac_write(i2c_addr, reg, data, len);
                free(data);
                cursor = cursor->next;
                continue;
            }

            // ── Normal register write: reg, val ──
            if (!cJSON_IsNumber(cursor)) return false;
            int val = cursor->valueint;
            adac_write_byte(i2c_addr, reg, val);
            cursor = cursor->next;
            continue;
        }
        return false;  // Unknown pattern
    }
    return true;
}
