/*
 * Copyright (c) 2019 David Antliff
 * Copyright 2011 Ben Buxton
 *
 * This file is part of the esp32-rotary-encoder component.
 *
 * esp32-rotary-encoder is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * esp32-rotary-encoder is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with esp32-rotary-encoder.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file rotary_encoder.c
 * @brief Driver implementation for the ESP32-compatible Incremental Rotary Encoder component.
 *
 * Based on https://github.com/buxtronix/arduino/tree/master/libraries/Rotary
 * Original header follows:
 *
 * Rotary encoder handler for arduino. v1.1
 *
 * Copyright 2011 Ben Buxton. Licenced under the GNU GPL Version 3.
 * Contact: bb@cactii.net
 *
 * A typical mechanical rotary encoder emits a two bit gray code
 * on 3 output pins. Every step in the output (often accompanied
 * by a physical 'click') generates a specific sequence of output
 * codes on the pins.
 *
 * There are 3 pins used for the rotary encoding - one common and
 * two 'bit' pins.
 *
 * The following is the typical sequence of code on the output when
 * moving from one step to the next:
 *
 *   Position   Bit1   Bit2
 *   ----------------------
 *     Step1     0      0
 *      1/4      1      0
 *      1/2      1      1
 *      3/4      0      1
 *     Step2     0      0
 *
 * From this table, we can see that when moving from one 'click' to
 * the next, there are 4 changes in the output code.
 *
 * - From an initial 0 - 0, Bit1 goes high, Bit0 stays low.
 * - Then both bits are high, halfway through the step.
 * - Then Bit1 goes low, but Bit2 stays high.
 * - Finally at the end of the step, both bits return to 0.
 *
 * Detecting the direction is easy - the table simply goes in the other
 * direction (read up instead of down).
 *
 * To decode this, we use a simple state machine. Every time the output
 * code changes, it follows state, until finally a full steps worth of
 * code is received (in the correct order). At the final 0-0, it returns
 * a value indicating a step in one direction or the other.
 *
 * It's also possible to use 'half-step' mode. This just emits an event
 * at both the 0-0 and 1-1 positions. This might be useful for some
 * encoders where you want to detect all positions.
 *
 * If an invalid state happens (for example we go from '0-1' straight
 * to '1-0'), the state machine resets to the start until 0-0 and the
 * next valid codes occur.
 *
 * The biggest advantage of using a state machine over other algorithms
 * is that this has inherent debounce built in. Other algorithms emit spurious
 * output with switch bounce, but this one will simply flip between
 * sub-states until the bounce settles, then continue along the state
 * machine.
 * A side effect of debounce is that fast rotations can cause steps to
 * be skipped. By not requiring debounce, fast rotations can be accurately
 * measured.
 * Another advantage is the ability to properly handle bad state, such
 * as due to EMI, etc.
 * It is also a lot simpler than others - a static state table and less
 * than 10 lines of logic.
 */

#include "rotary_encoder.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "gpio_exp.h"

#define TAG "rotary_encoder"

//#define ROTARY_ENCODER_DEBUG

// Use a single-item queue so that the last value can be easily overwritten by the interrupt handler
#define EVENT_QUEUE_LENGTH 1

#define TABLE_ROWS 7

#define DIR_NONE 0x0 // No complete step yet.
#define DIR_CW 0x10  // Clockwise step.
#define DIR_CCW 0x20 // Anti-clockwise step.

// Create the half-step state table (emits a code at 00 and 11)
#define R_START 0x0
#define H_CCW_BEGIN 0x1
#define H_CW_BEGIN 0x2
#define H_START_M 0x3
#define H_CW_BEGIN_M 0x4
#define H_CCW_BEGIN_M 0x5

static const uint8_t _ttable_half[TABLE_ROWS][TABLE_COLS] = {
    // 00                  01              10            11                   // BA
    {H_START_M, H_CW_BEGIN, H_CCW_BEGIN, R_START},            // R_START (00)
    {H_START_M | DIR_CCW, R_START, H_CCW_BEGIN, R_START},     // H_CCW_BEGIN
    {H_START_M | DIR_CW, H_CW_BEGIN, R_START, R_START},       // H_CW_BEGIN
    {H_START_M, H_CCW_BEGIN_M, H_CW_BEGIN_M, R_START},        // H_START_M (11)
    {H_START_M, H_START_M, H_CW_BEGIN_M, R_START | DIR_CW},   // H_CW_BEGIN_M
    {H_START_M, H_CCW_BEGIN_M, H_START_M, R_START | DIR_CCW}, // H_CCW_BEGIN_M
};

// Create the full-step state table (emits a code at 00 only)
#define F_CW_FINAL 0x1
#define F_CW_BEGIN 0x2
#define F_CW_NEXT 0x3
#define F_CCW_BEGIN 0x4
#define F_CCW_FINAL 0x5
#define F_CCW_NEXT 0x6

static const uint8_t _ttable_full[TABLE_ROWS][TABLE_COLS] = {
    // 00        01           10           11                  // BA
    {R_START, F_CW_BEGIN, F_CCW_BEGIN, R_START},           // R_START
    {F_CW_NEXT, R_START, F_CW_FINAL, R_START | DIR_CW},    // F_CW_FINAL
    {F_CW_NEXT, F_CW_BEGIN, R_START, R_START},             // F_CW_BEGIN
    {F_CW_NEXT, F_CW_BEGIN, F_CW_FINAL, R_START},          // F_CW_NEXT
    {F_CCW_NEXT, R_START, F_CCW_BEGIN, R_START},           // F_CCW_BEGIN
    {F_CCW_NEXT, F_CCW_FINAL, R_START, R_START | DIR_CCW}, // F_CCW_FINAL
    {F_CCW_NEXT, F_CCW_FINAL, F_CCW_BEGIN, R_START},       // F_CCW_NEXT
};

static uint8_t _process(rotary_encoder_info_t* info) {
    uint8_t event = 0;
    if(info != NULL) {
        // Get state of input pins.
        uint8_t pin_state = (gpio_get_level_x(info->pin_b) << 1) | gpio_get_level_x(info->pin_a);

        // Determine new state from the pins and state table.
#ifdef ROTARY_ENCODER_DEBUG
        uint8_t old_state = info->table_state;
#endif
        info->table_state = info->table[info->table_state & 0xf][pin_state];

        // Return emit bits, i.e. the generated event.
        event = info->table_state & 0x30;
#ifdef ROTARY_ENCODER_DEBUG
        ESP_EARLY_LOGD(
            TAG, "BA %d%d, state 0x%02x, new state 0x%02x, event 0x%02x", pin_state >> 1, pin_state & 1, old_state, info->table_state, event);
#endif
    }
    return event;
}

static void _isr_rotenc(void* args) {
    rotary_encoder_info_t* info = (rotary_encoder_info_t*)args;
    uint8_t event = _process(info);
    bool send_event = false;

    switch(event) {
    case DIR_CW:
        ++info->state.position;
        info->state.direction = ROTARY_ENCODER_DIRECTION_CLOCKWISE;
        send_event = true;
        break;
    case DIR_CCW:
        --info->state.position;
        info->state.direction = ROTARY_ENCODER_DIRECTION_COUNTER_CLOCKWISE;
        send_event = true;
        break;
    default:
        break;
    }

    if(send_event && info->queue) {
        rotary_encoder_event_t queue_event = {
            .state =
                {
                    .position = info->state.position,
                    .direction = info->state.direction,
                },
        };
        if(info->pin_a < GPIO_NUM_MAX) {
            BaseType_t task_woken = pdFALSE;
            xQueueOverwriteFromISR(info->queue, &queue_event, &task_woken);
            if(task_woken) { portYIELD_FROM_ISR(); }
        } else {
            xQueueOverwrite(info->queue, &queue_event);
        }
    }
}

esp_err_t rotary_encoder_init(rotary_encoder_info_t* info, gpio_num_t pin_a, gpio_num_t pin_b) {
    esp_err_t err = ESP_OK;
    if(info) {
        info->pin_a = pin_a;
        info->pin_b = pin_b;
        info->table = &_ttable_full[0]; //enable_half_step ? &_ttable_half[0] : &_ttable_full[0];
        info->table_state = R_START;
        info->state.position = 0;
        info->state.direction = ROTARY_ENCODER_DIRECTION_NOT_SET;

        // configure GPIOs
        gpio_pad_select_gpio_x(info->pin_a);
        gpio_set_pull_mode_x(info->pin_a, GPIO_PULLUP_ONLY);
        gpio_set_direction_x(info->pin_a, GPIO_MODE_INPUT);
        gpio_set_intr_type_x(info->pin_a, GPIO_INTR_ANYEDGE);

        gpio_pad_select_gpio_x(info->pin_b);
        gpio_set_pull_mode_x(info->pin_b, GPIO_PULLUP_ONLY);
        gpio_set_direction_x(info->pin_b, GPIO_MODE_INPUT);
        gpio_set_intr_type_x(info->pin_b, GPIO_INTR_ANYEDGE);

        // install interrupt handlers
        gpio_isr_handler_add_x(info->pin_a, _isr_rotenc, info);
        gpio_isr_handler_add_x(info->pin_b, _isr_rotenc, info);
    } else {
        ESP_LOGE(TAG, "info is NULL");
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}

esp_err_t rotary_encoder_enable_half_steps(rotary_encoder_info_t* info, bool enable) {
    esp_err_t err = ESP_OK;
    if(info) {
        info->table = enable ? &_ttable_half[0] : &_ttable_full[0];
        info->table_state = R_START;
    } else {
        ESP_LOGE(TAG, "info is NULL");
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}

esp_err_t rotary_encoder_flip_direction(rotary_encoder_info_t* info) {
    esp_err_t err = ESP_OK;
    if(info) {
        gpio_num_t temp = info->pin_a;
        info->pin_a = info->pin_b;
        info->pin_b = temp;
    } else {
        ESP_LOGE(TAG, "info is NULL");
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}

esp_err_t rotary_encoder_uninit(rotary_encoder_info_t* info) {
    esp_err_t err = ESP_OK;
    if(info) {
        gpio_isr_handler_remove_x(info->pin_a);
        gpio_isr_handler_remove_x(info->pin_b);
    } else {
        ESP_LOGE(TAG, "info is NULL");
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}

QueueHandle_t rotary_encoder_create_queue(void) { return xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(rotary_encoder_event_t)); }

esp_err_t rotary_encoder_set_queue(rotary_encoder_info_t* info, QueueHandle_t queue) {
    esp_err_t err = ESP_OK;
    if(info) {
        info->queue = queue;
    } else {
        ESP_LOGE(TAG, "info is NULL");
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}

esp_err_t rotary_encoder_get_state(const rotary_encoder_info_t* info, rotary_encoder_state_t* state) {
    esp_err_t err = ESP_OK;
    if(info && state) {
        // make a snapshot of the state
        state->position = info->state.position;
        state->direction = info->state.direction;
    } else {
        ESP_LOGE(TAG, "info and/or state is NULL");
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}

esp_err_t rotary_encoder_reset(rotary_encoder_info_t* info) {
    esp_err_t err = ESP_OK;
    if(info) {
        info->state.position = 0;
        info->state.direction = ROTARY_ENCODER_DIRECTION_NOT_SET;
    } else {
        ESP_LOGE(TAG, "info is NULL");
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}
