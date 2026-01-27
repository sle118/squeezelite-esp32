/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "ac101.h"
#include <driver/i2c.h>
#include <driver/i2s.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include "adac.h"

const static char TAG[] = "AC101";

#define SPKOUT_EN ((1 << 9) | (1 << 11) | (1 << 7) | (1 << 5))
#define EAROUT_EN ((1 << 11) | (1 << 12) | (1 << 13))
#define BIN(a, b, c, d) 0b##a##b##c##d

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

#define AC_ASSERT(a, format, b, ...)      \
  if ((a) != 0) {                         \
    ESP_LOGE(TAG, format, ##__VA_ARGS__); \
    return b;                             \
  }

static bool init(int i2c_port_num, int i2s_num, i2s_config_t* config);
static void deinit(void);
static void speaker(bool active);
static void headset(bool active);
static void volume(unsigned left, unsigned right);
static void power(adac_power_e mode);

struct adac_s dac_a1s = {init, deinit, power, speaker, headset, volume};

static esp_err_t i2c_write_reg(uint8_t reg, uint16_t val);
static uint16_t i2c_read_reg(uint8_t reg);
static void ac101_start(ac_module_t mode);
static void ac101_stop(void);
static void ac101_set_earph_volume(uint8_t volume);
static void ac101_set_spk_volume(uint8_t volume);
static int ac101_get_spk_volume(void);

static int i2c_port;

/****************************************************************************************
 * init
 */
static bool init(int i2c_port_num, int i2s_num, i2s_config_t* i2s_config) {
  esp_err_t res = ESP_OK;

  i2c_port = i2c_port_num;

  // configure i2c
  i2c_config_t i2c_config = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = 33,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_io_num = 32,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = 250000,
  };

  i2c_param_config(i2c_port, &i2c_config);
  i2c_driver_install(i2c_port, I2C_MODE_MASTER, false, false, false);

  res = i2c_read_reg(CHIP_AUDIO_RS);

  if (!res) {
    ESP_LOGW(TAG, "No AC101 detected");
    i2c_driver_delete(i2c_port);
    return 0;
  }

  ESP_LOGI(TAG, "AC101 DAC using I2C sda:%u, scl:%u", i2c_config.sda_io_num,
           i2c_config.scl_io_num);

  res = i2c_write_reg(CHIP_AUDIO_RS, 0x123);
  // huh?
  vTaskDelay(100 / portTICK_PERIOD_MS);

  // enable the PLL from BCLK source
  i2c_write_reg(PLL_CTRL1,
                BIN(0000, 0001, 0100, 1111));  // F=1,M=1,PLL,INT=31 (medium)
  i2c_write_reg(PLL_CTRL2, BIN(1000, 0110, 0000,
                               0000));  // PLL, F=96,N_i=1024-96,F=0,N_f=0*0.2;
  // i2c_write_reg(PLL_CTRL2, BIN(1000,0011,1100,0000));

  // clocking system
  i2c_write_reg(SYSCLK_CTRL, BIN(1010, 1010, 0000,
                                 1000));  // PLLCLK, BCLK1, IS1CLK, PLL, SYSCLK
  i2c_write_reg(MOD_CLK_ENA, BIN(1000, 0000, 0000, 1100));   // IS21, ADC, DAC
  i2c_write_reg(MOD_RST_CTRL, BIN(1000, 0000, 0000, 1100));  // IS21, ADC, DAC
  i2c_write_reg(I2S_SR_CTRL, BIN(0111, 0000, 0000, 0000));   // 44.1kHz

  // analogue config
  i2c_write_reg(I2S1LCK_CTRL,
                BIN(1000, 1000, 0101,
                    0000));  // Slave, BCLK=I2S/8,LRCK=32,16bits,I2Smode, Stereo
  i2c_write_reg(I2S1_SDOUT_CTRL, BIN(1100, 0000, 0000, 0000));  // I2S1ADC (R&L)
  i2c_write_reg(I2S1_SDIN_CTRL, BIN(1100, 0000, 0000, 0000));   // IS21DAC (R&L)
  i2c_write_reg(I2S1_MXR_SRC, BIN(0010, 0010, 0000, 0000));     // ADCL, ADCR
  i2c_write_reg(ADC_SRCBST_CTRL,
                BIN(0100, 0100, 0100, 0000));  // disable all boost (default)
#if ENABLE_ADC
  i2c_write_reg(ADC_SRC, BIN(0000, 0100, 0000, 1000));  // source=linein(R/L)
  i2c_write_reg(ADC_DIG_CTRL,
                BIN(1000, 0000, 0000, 0000));  // enable digital ADC
  i2c_write_reg(ADC_ANA_CTRL,
                BIN(1011, 1011, 0000, 0000));  // enable analogue R/L, 0dB
#else
  i2c_write_reg(ADC_SRC, BIN(0000, 0000, 0000, 0000));  // source=none
  i2c_write_reg(ADC_DIG_CTRL,
                BIN(0000, 0000, 0000, 0000));  // disable digital ADC
  i2c_write_reg(ADC_ANA_CTRL,
                BIN(0011, 0011, 0000, 0000));  // disable analogue R/L, 0dB
#endif

  //Path Configuration
  i2c_write_reg(DAC_MXR_SRC, BIN(1000, 1000, 0000, 0000));   // DAC from I2S
  i2c_write_reg(DAC_DIG_CTRL, BIN(1000, 0000, 0000, 0000));  // enable DAC
  i2c_write_reg(
      OMIXER_DACA_CTRL,
      BIN(1111, 0000, 0000,
          0000));  // enable DAC/Analogue (see note on offset removal and PA)
  i2c_write_reg(OMIXER_DACA_CTRL,
                BIN(1111, 1111, 0000,
                    0000));  // this toggle is needed for headphone PA offset
#if ENABLE_ADC
  i2c_write_reg(
      OMIXER_SR,
      BIN(0000, 0001, 0000,
          0010));  // source=DAC(R/L) (are DACR and DACL really inverted in bitmap?)
#else
  i2c_write_reg(OMIXER_SR, BIN(0000, 0101, 0000,
                               1010));  // source=DAC(R/L) and LINEIN(R/L)
#endif

  // configure I2S pins & install driver
  i2s_pin_config_t i2s_pin_config = (i2s_pin_config_t){
      .bck_io_num = 27, .ws_io_num = 26, .data_out_num = 25, .data_in_num = -1};
  res |= i2s_driver_install(i2s_num, i2s_config, 0, NULL);
  res |= i2s_set_pin(i2s_num, &i2s_pin_config);

  // enable earphone & speaker
  i2c_write_reg(SPKOUT_CTRL, 0x0220);
  i2c_write_reg(HPOUT_CTRL, 0xf801);

  // set gain for speaker and earphone
  ac101_set_spk_volume(70);
  ac101_set_earph_volume(70);

  ESP_LOGI(TAG, "DAC using I2S bck:%d, ws:%d, do:%d", i2s_pin_config.bck_io_num,
           i2s_pin_config.ws_io_num, i2s_pin_config.data_out_num);

  return (res == ESP_OK);
}

/****************************************************************************************
 * init
 */
static void deinit(void) {
  i2c_driver_delete(i2c_port);
}

/****************************************************************************************
 * change volume
 */
static void volume(unsigned left, unsigned right) {
  ac101_set_earph_volume(left);
  // nothing at that point, volume is handled by backend
}

/****************************************************************************************
 * power
 */
static void power(adac_power_e mode) {
  switch (mode) {
    case ADAC_STANDBY:
    case ADAC_OFF:
      ac101_stop();
      break;
    case ADAC_ON:
      ac101_start(AC_MODULE_DAC);
      break;
    default:
      ESP_LOGW(TAG, "unknown power command");
      break;
  }
}

/****************************************************************************************
 * speaker
 */
static void speaker(bool active) {
  uint16_t value = i2c_read_reg(SPKOUT_CTRL);
  if (active)
    i2c_write_reg(SPKOUT_CTRL, value | SPKOUT_EN);
  else
    i2c_write_reg(SPKOUT_CTRL, value & ~SPKOUT_EN);
}

/****************************************************************************************
 * headset
 */
static void headset(bool active) {
  // there might be  aneed to toggle OMIXER_DACA_CTRL 11:8, not sure
  uint16_t value = i2c_read_reg(HPOUT_CTRL);
  if (active)
    i2c_write_reg(HPOUT_CTRL, value | EAROUT_EN);
  else
    i2c_write_reg(HPOUT_CTRL, value & ~EAROUT_EN);
}

/****************************************************************************************
 * 
 */
static esp_err_t i2c_write_reg(uint8_t reg, uint16_t val) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  esp_err_t ret = 0;
  uint8_t send_buff[4];
  send_buff[0] = (AC101_ADDR << 1);
  send_buff[1] = reg;
  send_buff[2] = (val >> 8) & 0xff;
  send_buff[3] = val & 0xff;
  ret |= i2c_master_start(cmd);
  ret |= i2c_master_write(cmd, send_buff, 4, ACK_CHECK_EN);
  ret |= i2c_master_stop(cmd);
  ret |= i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);
  return ret;
}

/****************************************************************************************
 * 
 */
static uint16_t i2c_read_reg(uint8_t reg) {
  uint8_t data[2] = {0};

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (AC101_ADDR << 1) | WRITE_BIT, ACK_CHECK_EN);
  i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (AC101_ADDR << 1) | READ_BIT,
                        ACK_CHECK_EN);  //check or not
  i2c_master_read(cmd, data, 2, ACK_VAL);
  i2c_master_stop(cmd);
  i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);

  return (data[0] << 8) + data[1];
  ;
}

/****************************************************************************************
 * 
 */
void set_sample_rate(int rate) {
  if (rate == 8000)
    rate = SAMPLE_RATE_8000;
  else if (rate == 11025)
    rate = SAMPLE_RATE_11052;
  else if (rate == 12000)
    rate = SAMPLE_RATE_12000;
  else if (rate == 16000)
    rate = SAMPLE_RATE_16000;
  else if (rate == 22050)
    rate = SAMPLE_RATE_22050;
  else if (rate == 24000)
    rate = SAMPLE_RATE_24000;
  else if (rate == 32000)
    rate = SAMPLE_RATE_32000;
  else if (rate == 44100)
    rate = SAMPLE_RATE_44100;
  else if (rate == 48000)
    rate = SAMPLE_RATE_48000;
  else if (rate == 96000)
    rate = SAMPLE_RATE_96000;
  else if (rate == 192000)
    rate = SAMPLE_RATE_192000;
  else {
    ESP_LOGW(TAG, "Unknown sample rate %hu", rate);
    rate = SAMPLE_RATE_44100;
  }
  i2c_write_reg(I2S_SR_CTRL, rate);
}

/****************************************************************************************
 * Get normalized (0..100) speaker volume
 */
static int ac101_get_spk_volume(void) {
  return ((i2c_read_reg(SPKOUT_CTRL) & 0x1f) * 100) / 0x1f;
}

/****************************************************************************************
 * Set normalized (0..100) volume
 */
static void ac101_set_spk_volume(uint8_t volume) {
  uint16_t value = min(volume, 100);
  value = ((int)value * 0x1f) / 100;
  value |= i2c_read_reg(SPKOUT_CTRL) & ~0x1f;
  i2c_write_reg(SPKOUT_CTRL, value);
}

/****************************************************************************************
 * Get normalized (0..100) earphone volume
 */
static int ac101_get_earph_volume(void) {
  return (((i2c_read_reg(HPOUT_CTRL) >> 4) & 0x3f) * 100) / 0x3f;
}

/****************************************************************************************
 * Set normalized (0..100) earphone volume
 */
static void ac101_set_earph_volume(uint8_t volume) {
  uint16_t value = min(volume, 255);
  value = (((int)value * 0x3f) / 255) << 4;
  value |= i2c_read_reg(HPOUT_CTRL) & ~(0x3f << 4);
  i2c_write_reg(HPOUT_CTRL, value);
}

/****************************************************************************************
 * 
 */
static void ac101_set_output_mixer_gain(ac_output_mixer_gain_t gain,
                                        ac_output_mixer_source_t source) {
  uint16_t regval, temp, clrbit;
  regval = i2c_read_reg(OMIXER_BST1_CTRL);
  switch (source) {
    case SRC_MIC1:
      temp = (gain & 0x7) << 6;
      clrbit = ~(0x7 << 6);
      break;
    case SRC_MIC2:
      temp = (gain & 0x7) << 3;
      clrbit = ~(0x7 << 3);
      break;
    case SRC_LINEIN:
      temp = (gain & 0x7);
      clrbit = ~0x7;
      break;
    default:
      return;
  }
  regval &= clrbit;
  regval |= temp;
  i2c_write_reg(OMIXER_BST1_CTRL, regval);
}

/****************************************************************************************
 * 
 */
static void ac101_start(ac_module_t mode) {
  if (mode == AC_MODULE_LINE) {
    i2c_write_reg(0x51, 0x0408);
    i2c_write_reg(0x40, 0x8000);
    i2c_write_reg(0x50, 0x3bc0);
  }
  if (mode == AC_MODULE_ADC || mode == AC_MODULE_ADC_DAC ||
      mode == AC_MODULE_LINE) {
    // I2S1_SDOUT_CTRL
    // i2c_write_reg(PLL_CTRL2, 0x8120);
    i2c_write_reg(0x04, 0x800c);
    i2c_write_reg(0x05, 0x800c);
    // res |= i2c_write_reg(0x06, 0x3000);
  }
  if (mode == AC_MODULE_DAC || mode == AC_MODULE_ADC_DAC ||
      mode == AC_MODULE_LINE) {
    uint16_t value = i2c_read_reg(PLL_CTRL2);
    value |= 0x8000;
    i2c_write_reg(PLL_CTRL2, value);
  }
}

/****************************************************************************************
 * 
 */
static void ac101_stop(void) {
  uint16_t value = i2c_read_reg(PLL_CTRL2);
  value &= ~0x8000;
  i2c_write_reg(PLL_CTRL2, value);
}

/****************************************************************************************
 * 
 */
static void ac101_deinit(void) {
  i2c_write_reg(CHIP_AUDIO_RS, 0x123);  //soft reset
}

/****************************************************************************************
 * Don't know when this one is supposed to be called
 */
static void ac101_i2s_config_clock(ac_i2s_clock_t* cfg) {
  uint16_t regval = 0;
  regval = i2c_read_reg(I2S1LCK_CTRL);
  regval &= 0xe03f;
  regval |= (cfg->bclk_div << 9);
  regval |= (cfg->lclk_div << 6);
  i2c_write_reg(I2S1LCK_CTRL, regval);
}