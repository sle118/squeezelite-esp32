/* 
 *  Squeezelite for esp32
 *
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#pragma once

#define SPI_SYSTEM_HOST SPI2_HOST

#define RMT_NEXT_TX_CHANNEL() rmt_system_base_tx_channel++;
#define RMT_NEXT_RX_CHANNEL() rmt_system_base_rx_channel--;

extern int spi_system_host;
extern int spi_system_dc_gpio;
extern int rmt_system_base_tx_channel;
extern int rmt_system_base_rx_channel;
typedef struct {
    int timer, base_channel, max;
} pwm_system_t;
extern pwm_system_t pwm_system;
