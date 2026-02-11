/* 
 *  infrared receiver (using espressif's example)
 *
 *  (c) Philippe G. 2020, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/rmt.h"
#include "globdefs.h"
#include "infrared.h"

static const char* TAG = "IR";

#define IR_TOOLS_FLAGS_PROTO_EXT (1 << 0) /*!< Enable Extended IR protocol */
#define IR_TOOLS_FLAGS_INVERSE (1 << 1)   /*!< Inverse the IR signal, i.e. take high level as low, and vice versa */

static int8_t ir_gpio = -1;

/**
* @brief IR device type
*
*/
typedef void *ir_dev_t;

/**
* @brief IR parser type
*
*/
typedef struct ir_parser_s ir_parser_t;

/**
* @brief Type definition of IR parser
*
*/
struct ir_parser_s {
    /**
    * @brief Input raw data to IR parser
    *
    * @param[in] parser: Handle of IR parser
    * @param[in] raw_data: Raw data which need decoding by IR parser
    * @param[in] length: Length of raw data
    *
    * @return
    *      - ESP_OK: Input raw data successfully
    *      - ESP_ERR_INVALID_ARG: Input raw data failed because of invalid argument
    *      - ESP_FAIL: Input raw data failed because some other error occurred
    */
    esp_err_t (*input)(ir_parser_t *parser, void *raw_data, uint32_t length);

    /**
    * @brief Get the scan code after decoding of raw data
    *
    * @param[in] parser: Handle of IR parser
    * @param[out] address: Address of the scan code
    * @param[out] command: Command of the scan code
    * @param[out] repeat: Indicate if it's a repeat code
    *
    * @return
    *      - ESP_OK: Get scan code successfully
    *      - ESP_ERR_INVALID_ARG: Get scan code failed because of invalid arguments
    *      - ESP_FAIL: Get scan code failed because some error occurred
    */
    esp_err_t (*get_scan_code)(ir_parser_t *parser, uint32_t *address, uint32_t *command, bool *repeat);
};

typedef struct {
    ir_dev_t dev_hdl;   /*!< IR device handle */
    uint32_t flags;     /*!< Flags for IR parser, different flags will enable different features */
    uint32_t margin_us; /*!< Timing parameter, indicating the tolerance to environment noise */
} ir_parser_config_t;

#define IR_PARSER_DEFAULT_CONFIG(dev) \
    {                                 \
        .dev_hdl = dev,               \
        .flags = 0,                   \
        .margin_us = 200,             \
    }

ir_parser_t *ir_parser = NULL;

#define RMT_CHECK(a, str, goto_tag, ret_value, ...)                               \
    do                                                                            \
    {                                                                             \
        if (!(a))                                                                 \
        {                                                                         \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            ret = ret_value;                                                      \
            goto goto_tag;                                                        \
        }                                                                         \
    } while (0)


/****************************************************************************************
 * NEC protocol
 ****************************************************************************************/

#define NEC_DATA_FRAME_RMT_WORDS (34)
#define NEC_REPEAT_FRAME_RMT_WORDS (2)
#define NEC_LEADING_CODE_HIGH_US (9000)
#define NEC_LEADING_CODE_LOW_US (4500)
#define NEC_PAYLOAD_ONE_HIGH_US (560)
#define NEC_PAYLOAD_ONE_LOW_US (1690)
#define NEC_PAYLOAD_ZERO_HIGH_US (560)
#define NEC_PAYLOAD_ZERO_LOW_US (560)
#define NEC_REPEAT_CODE_HIGH_US (9000)
#define NEC_REPEAT_CODE_LOW_US (2250)
#define NEC_ENDING_CODE_HIGH_US (560)


typedef struct {
    ir_parser_t parent;
    uint32_t flags;
    uint32_t leading_code_high_ticks;
    uint32_t leading_code_low_ticks;
    uint32_t repeat_code_high_ticks;
    uint32_t repeat_code_low_ticks;
    uint32_t payload_logic0_high_ticks;
    uint32_t payload_logic0_low_ticks;
    uint32_t payload_logic1_high_ticks;
    uint32_t payload_logic1_low_ticks;
    uint32_t margin_ticks;
    rmt_item32_t *buffer;
    uint32_t cursor;
    uint32_t last_address;
    uint32_t last_command;
    bool repeat;
    bool inverse;
} nec_parser_t;

/****************************************************************************************
 * 
 */
static inline bool nec_check_in_range(uint32_t raw_ticks, uint32_t target_ticks, uint32_t margin_ticks) {
    return (raw_ticks < (target_ticks + margin_ticks)) && (raw_ticks > (target_ticks - margin_ticks));
}

/****************************************************************************************
 * 
 */
static bool nec_parse_head(nec_parser_t *nec_parser) {
    nec_parser->cursor = 0;
    rmt_item32_t item = nec_parser->buffer[nec_parser->cursor];
    bool ret = (item.level0 == nec_parser->inverse) && (item.level1 != nec_parser->inverse) &&
               nec_check_in_range(item.duration0, nec_parser->leading_code_high_ticks, nec_parser->margin_ticks) &&
               nec_check_in_range(item.duration1, nec_parser->leading_code_low_ticks, nec_parser->margin_ticks);
    nec_parser->cursor += 1;
    return ret;
}

/****************************************************************************************
 * 
 */
static bool nec_parse_logic0(nec_parser_t *nec_parser) {
    rmt_item32_t item = nec_parser->buffer[nec_parser->cursor];
    bool ret = (item.level0 == nec_parser->inverse) && (item.level1 != nec_parser->inverse) &&
               nec_check_in_range(item.duration0, nec_parser->payload_logic0_high_ticks, nec_parser->margin_ticks) &&
               nec_check_in_range(item.duration1, nec_parser->payload_logic0_low_ticks, nec_parser->margin_ticks);
    return ret;
}

/****************************************************************************************
 * 
 */
static bool nec_parse_logic1(nec_parser_t *nec_parser) {
    rmt_item32_t item = nec_parser->buffer[nec_parser->cursor];
    bool ret = (item.level0 == nec_parser->inverse) && (item.level1 != nec_parser->inverse) &&
               nec_check_in_range(item.duration0, nec_parser->payload_logic1_high_ticks, nec_parser->margin_ticks) &&
               nec_check_in_range(item.duration1, nec_parser->payload_logic1_low_ticks, nec_parser->margin_ticks);
    return ret;
}

/****************************************************************************************
 * 
 */
static esp_err_t nec_parse_logic(ir_parser_t *parser, bool *logic) {
    esp_err_t ret = ESP_FAIL;
    bool logic_value = false;
    nec_parser_t *nec_parser = __containerof(parser, nec_parser_t, parent);
    if (nec_parse_logic0(nec_parser)) {
        logic_value = false;
        ret = ESP_OK;
    } else if (nec_parse_logic1(nec_parser)) {
        logic_value = true;
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        *logic = logic_value;
    }
    nec_parser->cursor += 1;
    return ret;
}

/****************************************************************************************
 * 
 */
static bool nec_parse_repeat_frame(nec_parser_t *nec_parser) {
    nec_parser->cursor = 0;
    rmt_item32_t item = nec_parser->buffer[nec_parser->cursor];
    bool ret = (item.level0 == nec_parser->inverse) && (item.level1 != nec_parser->inverse) &&
               nec_check_in_range(item.duration0, nec_parser->repeat_code_high_ticks, nec_parser->margin_ticks) &&
               nec_check_in_range(item.duration1, nec_parser->repeat_code_low_ticks, nec_parser->margin_ticks);
    nec_parser->cursor += 1;
    return ret;
}

/****************************************************************************************
 * 
 */
static esp_err_t nec_parser_input(ir_parser_t *parser, void *raw_data, uint32_t length) {
    esp_err_t ret = ESP_OK;
    nec_parser_t *nec_parser = __containerof(parser, nec_parser_t, parent);
    RMT_CHECK(raw_data, "input data can't be null", err, ESP_ERR_INVALID_ARG);
    nec_parser->buffer = raw_data;
    // Data Frame costs 34 items and Repeat Frame costs 2 items
    if (length == NEC_DATA_FRAME_RMT_WORDS) {
        nec_parser->repeat = false;
    } else if (length == NEC_REPEAT_FRAME_RMT_WORDS) {
        nec_parser->repeat = true;
    } else {
        ret = ESP_FAIL;
    }
    return ret;
err:
    return ret;
}

/****************************************************************************************
 * 
 */
static esp_err_t nec_parser_get_scan_code(ir_parser_t *parser, uint32_t *address, uint32_t *command, bool *repeat) {
    esp_err_t ret = ESP_FAIL;
    uint32_t addr = 0;
    uint32_t cmd = 0;
    bool logic_value = false;
    nec_parser_t *nec_parser = __containerof(parser, nec_parser_t, parent);
    if (nec_parser->repeat) {
        if (nec_parse_repeat_frame(nec_parser)) {
            *address = nec_parser->last_address;
            *command = nec_parser->last_command;
            *repeat = true;
            ret = ESP_OK;
        }
    } else {
        if (nec_parse_head(nec_parser)) {
            // for the forgetful, need to do a bitreverse
            for (int i = 15; i >= 0; i--) {
                if (nec_parse_logic(parser, &logic_value) == ESP_OK) {
                    addr |= (logic_value << i);
                }
            }
            for (int i = 15; i >= 0; i--) {
                if (nec_parse_logic(parser, &logic_value) == ESP_OK) {
                    cmd |= (logic_value << i);
                }
            }
            *address = addr;
            *command = cmd;
            *repeat = false;
            // keep it as potential repeat code
            nec_parser->last_address = addr;
            nec_parser->last_command = cmd;
            ret = ESP_OK;
        }
    }
    return ret;
}

/****************************************************************************************
 * 
 */
ir_parser_t *ir_parser_rmt_new_nec(const ir_parser_config_t *config) {
    ir_parser_t *ret = NULL;
    nec_parser_t *nec_parser = calloc(1, sizeof(nec_parser_t));

    nec_parser->flags = config->flags;
    if (config->flags & IR_TOOLS_FLAGS_INVERSE) {
        nec_parser->inverse = true;
    }

    uint32_t counter_clk_hz = 0;
    RMT_CHECK(rmt_get_counter_clock((rmt_channel_t)config->dev_hdl, &counter_clk_hz) == ESP_OK,
              "get rmt counter clock failed", err, NULL);
    float ratio = (float)counter_clk_hz / 1e6;
    nec_parser->leading_code_high_ticks = (uint32_t)(ratio * NEC_LEADING_CODE_HIGH_US);
    nec_parser->leading_code_low_ticks = (uint32_t)(ratio * NEC_LEADING_CODE_LOW_US);
    nec_parser->repeat_code_high_ticks = (uint32_t)(ratio * NEC_REPEAT_CODE_HIGH_US);
    nec_parser->repeat_code_low_ticks = (uint32_t)(ratio * NEC_REPEAT_CODE_LOW_US);
    nec_parser->payload_logic0_high_ticks = (uint32_t)(ratio * NEC_PAYLOAD_ZERO_HIGH_US);
    nec_parser->payload_logic0_low_ticks = (uint32_t)(ratio * NEC_PAYLOAD_ZERO_LOW_US);
    nec_parser->payload_logic1_high_ticks = (uint32_t)(ratio * NEC_PAYLOAD_ONE_HIGH_US);
    nec_parser->payload_logic1_low_ticks = (uint32_t)(ratio * NEC_PAYLOAD_ONE_LOW_US);
    nec_parser->margin_ticks = (uint32_t)(ratio * config->margin_us);
    nec_parser->parent.input = nec_parser_input;
    nec_parser->parent.get_scan_code = nec_parser_get_scan_code;
    return &nec_parser->parent;
err:
    return ret;
}

/****************************************************************************************
 * RC5 protocol
 ****************************************************************************************/
 
#define RC5_MAX_FRAME_RMT_WORDS (14) // S1+S2+T+ADDR(5)+CMD(6)
#define RC5_PULSE_DURATION_US (889)

typedef struct {
    ir_parser_t parent;
    uint32_t flags;
    uint32_t pulse_duration_ticks;
    uint32_t margin_ticks;
    rmt_item32_t *buffer;
    uint32_t buffer_len;
    uint32_t last_command;
    uint32_t last_address;
    bool last_t_bit;
} rc5_parser_t;

/****************************************************************************************
 * 
 */
static inline bool rc5_check_in_range(uint32_t raw_ticks, uint32_t target_ticks, uint32_t margin_ticks) {
    return (raw_ticks < (target_ticks + margin_ticks)) && (raw_ticks > (target_ticks - margin_ticks));
}

/****************************************************************************************
 * 
 */
static esp_err_t rc5_parser_input(ir_parser_t *parser, void *raw_data, uint32_t length) {
    esp_err_t ret = ESP_OK;
    rc5_parser_t *rc5_parser = __containerof(parser, rc5_parser_t, parent);
    rc5_parser->buffer = raw_data;
    rc5_parser->buffer_len = length;
    if (length > RC5_MAX_FRAME_RMT_WORDS) {
        ret = ESP_FAIL;
    }
    return ret;
}

/****************************************************************************************
 * 
 */
static inline bool rc5_duration_one_unit(rc5_parser_t *rc5_parser, uint32_t duration) {
    return (duration < (rc5_parser->pulse_duration_ticks + rc5_parser->margin_ticks)) &&
           (duration > (rc5_parser->pulse_duration_ticks - rc5_parser->margin_ticks));
}

/****************************************************************************************
 * 
 */
static inline bool rc5_duration_two_unit(rc5_parser_t *rc5_parser, uint32_t duration) {
    return (duration < (rc5_parser->pulse_duration_ticks * 2 + rc5_parser->margin_ticks)) &&
           (duration > (rc5_parser->pulse_duration_ticks * 2 - rc5_parser->margin_ticks));
}

/****************************************************************************************
 * 
 */
static esp_err_t rc5_parser_get_scan_code(ir_parser_t *parser, uint32_t *address, uint32_t *command, bool *repeat) {
    esp_err_t ret = ESP_FAIL;
    uint32_t parse_result = 0; // 32 bit is enough to hold the parse result of one RC5 frame
    uint32_t addr = 0;
    uint32_t cmd = 0;
    bool s1 = true;
    bool s2 = true;
    bool t = false;
    bool exchange = false;
    rc5_parser_t *rc5_parser = __containerof(parser, rc5_parser_t, parent);
    for (int i = 0; i < rc5_parser->buffer_len; i++) {
        if (rc5_duration_one_unit(rc5_parser, rc5_parser->buffer[i].duration0)) {
            parse_result <<= 1;
            parse_result |= exchange;
            if (rc5_duration_two_unit(rc5_parser, rc5_parser->buffer[i].duration1)) {
                exchange = !exchange;
            }
        } else if (rc5_duration_two_unit(rc5_parser, rc5_parser->buffer[i].duration0)) {
            parse_result <<= 1;
            parse_result |= rc5_parser->buffer[i].level0;
            parse_result <<= 1;
            parse_result |= !rc5_parser->buffer[i].level0;
            if (rc5_duration_one_unit(rc5_parser, rc5_parser->buffer[i].duration1)) {
                exchange = !exchange;
            }
        } else {
            goto out;
        }
    }
    if (!(rc5_parser->flags & IR_TOOLS_FLAGS_INVERSE)) {
        parse_result = ~parse_result;
    }
    s1 = ((parse_result & 0x2000) >> 13) & 0x01;
    s2 = ((parse_result & 0x1000) >> 12) & 0x01;
    t = ((parse_result & 0x800) >> 11) & 0x01;
    // Check S1, must be 1
    if (s1) {
        if (!(rc5_parser->flags & IR_TOOLS_FLAGS_PROTO_EXT) && !s2) {
            // Not standard RC5 protocol, but S2 is 0
            goto out;
        }
        addr = (parse_result & 0x7C0) >> 6;
        cmd = (parse_result & 0x3F);
        if (!s2) {
            cmd |= 1 << 6;
        }
        *repeat = (t == rc5_parser->last_t_bit && addr == rc5_parser->last_address && cmd == rc5_parser->last_command);
        *address = addr;
        *command = cmd;
        rc5_parser->last_address = addr;
        rc5_parser->last_command = cmd;
        rc5_parser->last_t_bit = t;
        ret = ESP_OK;
    }
out:
    return ret;
}

/****************************************************************************************
 * 
 */
ir_parser_t *ir_parser_rmt_new_rc5(const ir_parser_config_t *config) {
    ir_parser_t *ret = NULL;
    rc5_parser_t *rc5_parser = calloc(1, sizeof(rc5_parser_t));

    rc5_parser->flags = config->flags;

    uint32_t counter_clk_hz = 0;
    RMT_CHECK(rmt_get_counter_clock((rmt_channel_t)config->dev_hdl, &counter_clk_hz) == ESP_OK,
              "get rmt counter clock failed", err, NULL);
    float ratio = (float)counter_clk_hz / 1e6;
    rc5_parser->pulse_duration_ticks = (uint32_t)(ratio * RC5_PULSE_DURATION_US);
    rc5_parser->margin_ticks = (uint32_t)(ratio * config->margin_us);
    rc5_parser->parent.input = rc5_parser_input;
    rc5_parser->parent.get_scan_code = rc5_parser_get_scan_code;
    return &rc5_parser->parent;
err:
    return ret;
}


/****************************************************************************************
 * 
 */
bool infrared_receive(RingbufHandle_t rb, infrared_handler handler) {
	size_t rx_size = 0;
	rmt_item32_t* item = (rmt_item32_t*) xRingbufferReceive(rb, &rx_size, 10 / portTICK_PERIOD_MS);
    bool decoded = false;
    
	if (item) {
		uint32_t addr, cmd;
        bool repeat = false;
		      
        rx_size /= 4; // one RMT = 4 Bytes
        
        if (ir_parser->input(ir_parser, item, rx_size) == ESP_OK) {
            if (ir_parser->get_scan_code(ir_parser, &addr, &cmd, &repeat) == ESP_OK) {
                decoded = true;
                handler(addr, cmd);
                ESP_LOGI(TAG, "Scan Code %s --- addr: 0x%04x cmd: 0x%04x", repeat ? "(repeat)" : "", addr, cmd);
            }
        }

        // if we have not decoded data but lenght is reasonnable, dump it
        if (!decoded && rx_size > RC5_MAX_FRAME_RMT_WORDS) {
            ESP_LOGI(TAG, "can't decode IR signal of len %d", rx_size);
            ESP_LOG_BUFFER_HEX(TAG, item, rx_size * 4);
        }

		// after parsing the data, return spaces to ringbuffer.
        vRingbufferReturnItem(rb, (void*) item);
    }
    
    return decoded;
}

/****************************************************************************************
 * 
 */
int8_t infrared_gpio(void) {
    return ir_gpio;
};    

/****************************************************************************************
 * 
 */
void infrared_init(RingbufHandle_t *rb, int gpio, infrared_mode_t mode) {  
    int rmt_channel = RMT_NEXT_RX_CHANNEL();
    rmt_config_t rmt_rx_config = RMT_DEFAULT_CONFIG_RX(gpio, rmt_channel);
    rmt_config(&rmt_rx_config);
    rmt_driver_install(rmt_rx_config.channel, 1000, 0);
    ir_parser_config_t ir_parser_config = IR_PARSER_DEFAULT_CONFIG((ir_dev_t) rmt_rx_config.channel);
    ir_parser_config.flags |= IR_TOOLS_FLAGS_PROTO_EXT; // Using extended IR protocols (both NEC and RC5 have extended version)

    ir_parser = (mode == IR_NEC) ? ir_parser_rmt_new_nec(&ir_parser_config) : ir_parser_rmt_new_rc5(&ir_parser_config);
    ir_gpio = gpio;
    
    // get RMT RX ringbuffer
    rmt_get_ringbuf_handle(rmt_channel, rb);
    rmt_rx_start(rmt_channel, 1);
    
    ESP_LOGI(TAG, "Starting Infrared Receiver mode %s on gpio %d and channel %d", mode == IR_NEC ? "nec" : "rc5", gpio, rmt_channel);
}
