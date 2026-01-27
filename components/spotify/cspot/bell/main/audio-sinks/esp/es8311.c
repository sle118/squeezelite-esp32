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

#include "es8311.h"
#include <string.h>
#include "esp_log.h"
// #include "board.h"

/* ES8311 address
 * 0x32:CE=1;0x30:CE=0
 */
#define ES8311_ADDR 0x32

#define ES7243_ADDR 0x26

/*
* to define the clock soure of MCLK
*/
#define FROM_MCLK_PIN 0
#define FROM_SCLK_PIN 1
/*
* to define work mode(master or slave)
*/
#define MASTER_MODE 0
#define SLAVE_MODE 1
/*
* to define serial digital audio format
*/
#define I2S_FMT 0
#define LEFT_JUSTIFIED_FMT 1
#define DPS_PCM_A_FMT 2
#define DPS_PCM_B_FMT 3
/*
* to define resolution of PCM interface
*/
#define LENGTH_16BIT 0
#define LENGTH_24BIT 1
#define LENGTH_32BIT 2

/*
* codec private data
*/
struct es8311_private {
  bool dmic_enable;
  bool mclkinv;
  bool sclkinv;
  uint8_t master_slave_mode;
  uint8_t pcm_format;
  uint8_t pcm_resolution;
  uint8_t mclk_src;
};
static struct es8311_private* es8311_priv;

/*
* Clock coefficient structer
*/
struct _coeff_div {
  uint32_t mclk;    /* mclk frequency */
  uint32_t rate;    /* sample rate */
  uint8_t prediv;   /* the pre divider with range from 1 to 8 */
  uint8_t premulti; /* the pre multiplier with x1, x2, x4 and x8 selection */
  uint8_t adcdiv;   /* adcclk divider */
  uint8_t dacdiv;   /* dacclk divider */
  uint8_t fsmode;   /* double speed or single speed, =0, ss, =1, ds */
  uint8_t lrck_h;   /* adclrck divider and daclrck divider */
  uint8_t lrck_l;
  uint8_t bclkdiv; /* sclk divider */
  uint8_t adcosr;  /* adc osr */
  uint8_t dacosr;  /* dac osr */
};
/* codec hifi mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
    //mclk     rate   prediv  mult  adcdiv dacdiv fsmode lrch  lrcl  bckdiv osr
    /* 8k */
    {12288000, 8000, 0x06, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {18432000, 8000, 0x03, 0x02, 0x03, 0x03, 0x00, 0x05, 0xff, 0x18, 0x10,
     0x10},
    {16384000, 8000, 0x08, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {8192000, 8000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 8000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000, 8000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 8000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000, 8000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 8000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1024000, 8000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    /* 11.025k */
    {11289600, 11025, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {5644800, 11025, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {2822400, 11025, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1411200, 11025, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},

    /* 12k */
    {12288000, 12000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {6144000, 12000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {3072000, 12000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1536000, 12000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},

    /* 16k */
    {12288000, 16000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {18432000, 16000, 0x03, 0x02, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10,
     0x10},
    {16384000, 16000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {8192000, 16000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {6144000, 16000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {4096000, 16000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {3072000, 16000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {2048000, 16000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1536000, 16000, 0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1024000, 16000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},

    /* 22.05k */
    {11289600, 22050, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {5644800, 22050, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {2822400, 22050, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1411200, 22050, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},

    /* 24k */
    {12288000, 24000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {18432000, 24000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {6144000, 24000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {3072000, 24000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1536000, 24000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},

    /* 32k */
    {12288000, 32000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {18432000, 32000, 0x03, 0x04, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10,
     0x10},
    {16384000, 32000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {8192000, 32000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {6144000, 32000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {4096000, 32000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {3072000, 32000, 0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {2048000, 32000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1536000, 32000, 0x03, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10,
     0x10},
    {1024000, 32000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},

    /* 44.1k */
    {11289600, 44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {5644800, 44100, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {2822400, 44100, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1411200, 44100, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},

    /* 48k */
    {12288000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {18432000, 48000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {6144000, 48000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {3072000, 48000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1536000, 48000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},

    /* 64k */
    {12288000, 64000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {18432000, 64000, 0x03, 0x04, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10,
     0x10},
    {16384000, 64000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {8192000, 64000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {6144000, 64000, 0x01, 0x04, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10,
     0x10},
    {4096000, 64000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {3072000, 64000, 0x01, 0x08, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10,
     0x10},
    {2048000, 64000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1536000, 64000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0xbf, 0x03, 0x18,
     0x18},
    {1024000, 64000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10,
     0x10},

    /* 88.2k */
    {11289600, 88200, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {5644800, 88200, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {2822400, 88200, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1411200, 88200, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10,
     0x10},

    /* 96k */
    {12288000, 96000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {18432000, 96000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {6144000, 96000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {3072000, 96000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10,
     0x10},
    {1536000, 96000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10,
     0x10},
};

static char* TAG = "DRV8311";

#define ES_ASSERT(a, format, b, ...)      \
  if ((a) != 0) {                         \
    ESP_LOGE(TAG, format, ##__VA_ARGS__); \
    return b;                             \
  }

static int Es8311WriteReg(uint8_t regAdd, uint8_t data) {
  int res = 0;
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  res |= i2c_master_start(cmd);
  res |= i2c_master_write_byte(cmd, ES8311_ADDR, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_write_byte(cmd, regAdd, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_write_byte(cmd, data, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_stop(cmd);
  res |= i2c_master_cmd_begin(0, cmd, 1000 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);
  ES_ASSERT(res, "Es8311 Write Reg error", -1);
  return res;
}

int Es8311ReadReg(uint8_t regAdd) {
  uint8_t data;
  int res = 0;
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();

  res |= i2c_master_start(cmd);
  res |= i2c_master_write_byte(cmd, ES8311_ADDR, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_write_byte(cmd, regAdd, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_stop(cmd);
  res |= i2c_master_cmd_begin(0, cmd, 1000 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);

  cmd = i2c_cmd_link_create();
  res |= i2c_master_start(cmd);
  res |= i2c_master_write_byte(cmd, ES8311_ADDR | 0x01, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_read_byte(cmd, &data, 0x01 /*NACK_VAL*/);
  res |= i2c_master_stop(cmd);
  res |= i2c_master_cmd_begin(0, cmd, 1000 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);

  ES_ASSERT(res, "Es8311 Read Reg error", -1);
  return (int)data;
}

static int Es7243WriteReg(uint8_t regAdd, uint8_t data) {
  int res = 0;
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  res |= i2c_master_start(cmd);
  res |= i2c_master_write_byte(cmd, ES7243_ADDR, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_write_byte(cmd, regAdd, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_write_byte(cmd, data, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_stop(cmd);
  res |= i2c_master_cmd_begin(0, cmd, 1000 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);
  ES_ASSERT(res, "Es7243 Write Reg error", -1);
  return res;
}

int Es7243ReadReg(uint8_t regAdd) {
  uint8_t data;
  int res = 0;
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();

  res |= i2c_master_start(cmd);
  res |= i2c_master_write_byte(cmd, ES7243_ADDR, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_write_byte(cmd, regAdd, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_stop(cmd);
  res |= i2c_master_cmd_begin(0, cmd, 1000 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);

  cmd = i2c_cmd_link_create();
  res |= i2c_master_start(cmd);
  res |= i2c_master_write_byte(cmd, ES7243_ADDR | 0x01, 1 /*ACK_CHECK_EN*/);
  res |= i2c_master_read_byte(cmd, &data, 0x01 /*NACK_VAL*/);
  res |= i2c_master_stop(cmd);
  res |= i2c_master_cmd_begin(0, cmd, 1000 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);

  ES_ASSERT(res, "Es7243 Read Reg error", -1);
  return (int)data;
}

esp_err_t Es7243Init(void) {
  esp_err_t ret = ESP_OK;
  ret |= Es7243WriteReg(0x00, 0x01);
  ret |= Es7243WriteReg(0x06, 0x00);
  ret |= Es7243WriteReg(0x05, 0x1B);
  ret |= Es7243WriteReg(0x01, 0x0C);
  ret |= Es7243WriteReg(0x08, 0x43);
  ret |= Es7243WriteReg(0x05, 0x13);
  if (ret) {
    ESP_LOGE(TAG, "Es7243 initialize failed!");
    return ESP_FAIL;
  }
  return ret;
}

static int I2cInit(i2c_config_t* conf, int i2cMasterPort) {
  int res;
  res = i2c_param_config(i2cMasterPort, conf);
  res |= i2c_driver_install(i2cMasterPort, conf->mode, 0, 0, 0);
  ES_ASSERT(res, "I2cInit error", -1);
  return res;
}

/*
* look for the coefficient in coeff_div[] table
*/
static int get_coeff(uint32_t mclk, uint32_t rate) {
  for (int i = 0; i < (sizeof(coeff_div) / sizeof(coeff_div[0])); i++) {
    if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
      return i;
  }
  return -1;
}
/*
* set es8311 clock parameter and PCM/I2S interface
*/
static void es8311_pcm_hw_params(uint32_t mclk, uint32_t lrck) {
  int coeff;
  uint8_t regv, datmp;
  ESP_LOGI(TAG, "Enter into es8311_pcm_hw_params()\n");
  coeff = get_coeff(mclk, lrck);
  if (coeff < 0) {
    ESP_LOGE(TAG, "Unable to configure sample rate %dHz with %dHz MCLK\n", lrck,
             mclk);
    return;
  }

  /*
    * set clock parammeters
    */
  if (coeff >= 0) {
    regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG02) & 0x07;
    regv |= (coeff_div[coeff].prediv - 1) << 5;
    datmp = 0;
    switch (coeff_div[coeff].premulti) {
      case 1:
        datmp = 0;
        break;
      case 2:
        datmp = 1;
        break;
      case 4:
        datmp = 2;
        break;
      case 8:
        datmp = 3;
        break;
      default:
        break;
    }
#if CONFIG_ESP32_KORVO_V1_1_BOARD
    datmp = 3;
#endif
    regv |= (datmp) << 3;
    Es8311WriteReg(ES8311_CLK_MANAGER_REG02, regv);

    regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG05) & 0x00;
    regv |= (coeff_div[coeff].adcdiv - 1) << 4;
    regv |= (coeff_div[coeff].dacdiv - 1) << 0;
    Es8311WriteReg(ES8311_CLK_MANAGER_REG05, regv);

    regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG03) & 0x80;
    regv |= coeff_div[coeff].fsmode << 6;
    regv |= coeff_div[coeff].adcosr << 0;
    Es8311WriteReg(ES8311_CLK_MANAGER_REG03, regv);

    regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG04) & 0x80;
    regv |= coeff_div[coeff].dacosr << 0;
    Es8311WriteReg(ES8311_CLK_MANAGER_REG04, regv);

    regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG07) & 0xC0;
    regv |= coeff_div[coeff].lrck_h << 0;
    Es8311WriteReg(ES8311_CLK_MANAGER_REG07, regv);

    regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG08) & 0x00;
    regv |= coeff_div[coeff].lrck_l << 0;
    Es8311WriteReg(ES8311_CLK_MANAGER_REG08, regv);

    regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG06) & 0xE0;
    if (coeff_div[coeff].bclkdiv < 19) {
      regv |= (coeff_div[coeff].bclkdiv - 1) << 0;
    } else {
      regv |= (coeff_div[coeff].bclkdiv) << 0;
    }
    Es8311WriteReg(ES8311_CLK_MANAGER_REG06, regv);
  }
}
/*
* set data and clock in tri-state mode
* if tristate = 0, tri-state is disabled for normal mode
* if tristate = 1, tri-state is enabled
*/
// static void es8311_set_tristate(int tristate)
// {
//     uint8_t regv;
//     ESP_LOGI(TAG, "Enter into es8311_set_tristate(), tristate = %d\n", tristate);
//     regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG07) & 0xcf;
//     if (tristate) {
//         Es8311WriteReg(ES8311_CLK_MANAGER_REG07, regv | 0x30);
//     } else {
//         Es8311WriteReg(ES8311_CLK_MANAGER_REG07, regv);
//     }
// }
/*
* set es8311 dac mute or not
* if mute = 0, dac un-mute
* if mute = 1, dac mute
*/
static void es8311_mute(int mute) {
  uint8_t regv;
  ESP_LOGI(TAG, "Enter into es8311_mute(), mute = %d\n", mute);
  regv = Es8311ReadReg(ES8311_DAC_REG31) & 0x9f;
  if (mute) {
    Es8311WriteReg(ES8311_SYSTEM_REG12, 0x02);
    Es8311WriteReg(ES8311_DAC_REG31, regv | 0x60);
    Es8311WriteReg(ES8311_DAC_REG32, 0x00);
    Es8311WriteReg(ES8311_DAC_REG37, 0x08);
  } else {
    Es8311WriteReg(ES8311_DAC_REG31, regv);
    Es8311WriteReg(ES8311_SYSTEM_REG12, 0x00);
  }
}
/*
* set es8311 into suspend mode
*/
// static void es8311_suspend(void)
// {
//     ESP_LOGI(TAG, "Enter into es8311_suspend()\n");
//     Es8311WriteReg(ES8311_DAC_REG32, 0x00);
//     Es8311WriteReg(ES8311_ADC_REG17, 0x00);
//     Es8311WriteReg(ES8311_SYSTEM_REG0E, 0xFF);
//     Es8311WriteReg(ES8311_SYSTEM_REG12, 0x02);
//     Es8311WriteReg(ES8311_SYSTEM_REG14, 0x00);
//     Es8311WriteReg(ES8311_SYSTEM_REG0D, 0xFA);
//     Es8311WriteReg(ES8311_ADC_REG15, 0x00);
//     Es8311WriteReg(ES8311_DAC_REG37, 0x08);
//     Es8311WriteReg(ES8311_RESET_REG00, 0x00);
//     Es8311WriteReg(ES8311_RESET_REG00, 0x1F);
//     Es8311WriteReg(ES8311_CLK_MANAGER_REG01, 0x30);
//     Es8311WriteReg(ES8311_CLK_MANAGER_REG01, 0x00);
//     Es8311WriteReg(ES8311_GP_REG45, 0x01);
// }
/*
* initialize es8311 codec
*/
static void es8311_init(uint32_t mclk_freq, uint32_t lrck_freq) {
  int regv;
  Es8311WriteReg(ES8311_GP_REG45, 0x00);
  Es8311WriteReg(ES8311_CLK_MANAGER_REG01, 0x30);
  Es8311WriteReg(ES8311_CLK_MANAGER_REG02, 0x00);
  Es8311WriteReg(ES8311_CLK_MANAGER_REG03, 0x10);
  Es8311WriteReg(ES8311_ADC_REG16, 0x24);
  Es8311WriteReg(ES8311_CLK_MANAGER_REG04, 0x10);
  Es8311WriteReg(ES8311_CLK_MANAGER_REG05, 0x00);
  Es8311WriteReg(ES8311_SYSTEM_REG0B, 0x00);
  Es8311WriteReg(ES8311_SYSTEM_REG0C, 0x00);
  Es8311WriteReg(ES8311_SYSTEM_REG10, 0x1F);
  Es8311WriteReg(ES8311_SYSTEM_REG11, 0x7F);
  Es8311WriteReg(ES8311_RESET_REG00, 0x80);
  /*
    * Set Codec into Master or Slave mode
    */
  regv = Es8311ReadReg(ES8311_RESET_REG00);
  /* set master/slave audio interface */
  switch (es8311_priv->master_slave_mode) {
    case MASTER_MODE: /* MASTER MODE */
      ESP_LOGI(TAG, "ES8311 in Master mode\n");
      regv |= 0x40;
      break;
    case SLAVE_MODE: /* SLAVE MODE */
      ESP_LOGI(TAG, "ES8311 in Slave mode\n");
      regv &= 0xBF;
      break;
    default:
      regv &= 0xBF;
  }
  Es8311WriteReg(ES8311_RESET_REG00, regv);
  Es8311WriteReg(ES8311_SYSTEM_REG0D, 0x01);
  Es8311WriteReg(ES8311_CLK_MANAGER_REG01, 0x3F);
  /*
    *   select clock source for internal mclk
    */
  switch (es8311_priv->mclk_src) {
    case FROM_MCLK_PIN:
      regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG01);
      regv &= 0x7F;
      Es8311WriteReg(ES8311_CLK_MANAGER_REG01, regv);
      break;
    case FROM_SCLK_PIN:
      regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG01);
      regv |= 0x80;
      Es8311WriteReg(ES8311_CLK_MANAGER_REG01, regv);
      break;
    default:
      regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG01);
      regv &= 0x7F;
      Es8311WriteReg(ES8311_CLK_MANAGER_REG01, regv);
      break;
  }
  es8311_pcm_hw_params(lrck_freq * 256, lrck_freq);

  /*
    *   mclk inverted or not
    */
  if (es8311_priv->mclkinv == true) {
    regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG01);
    regv |= 0x40;
    Es8311WriteReg(ES8311_CLK_MANAGER_REG01, regv);
  } else {
    regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG01);
    regv &= ~(0x40);
    Es8311WriteReg(ES8311_CLK_MANAGER_REG01, regv);
  }
  /*
    *   sclk inverted or not
    */
  if (es8311_priv->sclkinv == true) {
    regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG06);
    regv |= 0x20;
    Es8311WriteReg(ES8311_CLK_MANAGER_REG06, regv);
  } else {
    regv = Es8311ReadReg(ES8311_CLK_MANAGER_REG06);
    regv &= ~(0x20);
    Es8311WriteReg(ES8311_CLK_MANAGER_REG06, regv);
  }
  Es8311WriteReg(ES8311_SYSTEM_REG14, 0x1A);
  /*
    *   pdm dmic enable or disable
    */
  if (es8311_priv->dmic_enable == true) {
    regv = Es8311ReadReg(ES8311_SYSTEM_REG14);
    regv |= 0x40;
    Es8311WriteReg(ES8311_SYSTEM_REG14, regv);
  } else {
    regv = Es8311ReadReg(ES8311_SYSTEM_REG14);
    regv &= ~(0x40);
    Es8311WriteReg(ES8311_SYSTEM_REG14, regv);
  }

  Es8311WriteReg(ES8311_SYSTEM_REG13, 0x10);
  Es8311WriteReg(ES8311_SYSTEM_REG0E, 0x02);
  Es8311WriteReg(ES8311_ADC_REG15, 0x40);
  Es8311WriteReg(ES8311_ADC_REG1B, 0x0A);
  Es8311WriteReg(ES8311_ADC_REG1C, 0x6A);
  Es8311WriteReg(ES8311_DAC_REG37, 0x48);
  Es8311WriteReg(ES8311_GPIO_REG44, 0x08);
  Es8311WriteReg(ES8311_DAC_REG32, 0xBF);

#ifdef CONFIG_USE_ES7243
  Es7243Init();
#endif
}
/*
* set codec private data and initialize codec
*/
void es8311_Codec_Startup(uint32_t mclk_freq, uint32_t lrck_freq) {
  ESP_LOGI(TAG, "Enter into es8311_Codec_Startup()\n");
  es8311_priv->dmic_enable = false;
  es8311_priv->mclkinv = false;
  es8311_priv->sclkinv = false;
  es8311_priv->pcm_format = I2S_FMT;
  es8311_priv->pcm_resolution = LENGTH_16BIT;
  es8311_priv->master_slave_mode = SLAVE_MODE;
#ifdef CONFIG_ESP32_KORVO_V1_1_BOARD
  es8311_priv->mclk_src = FROM_SCLK_PIN;
#else
  es8311_priv->mclk_src = FROM_MCLK_PIN;
#endif

  es8311_init(mclk_freq, lrck_freq);

  ESP_LOGI(TAG, "Exit es8311_Codec_Startup()\n");
}

// static int Es8311SetAdcDacVolume(int mode, int volume, int dot)
// {
//     int res = 0;
//     if ( volume < -96 || volume > 0 ) {
//         ESP_LOGI(TAG, "Warning: volume < -96! or > 0!\n");
//         if (volume < -96) {
//             volume = -96;
//         } else {
//             volume = 0;
//         }
//     }
//     dot = (dot >= 5 ? 1 : 0);
//     return res;
// }

esp_err_t Es8311GetRef(bool flag) {
  esp_err_t ret = ESP_OK;
  uint8_t regv = 0;
  if (flag) {
    regv = Es8311ReadReg(ES8311_GPIO_REG44);
    regv |= 0x50;
    ret |= Es8311WriteReg(ES8311_GPIO_REG44, regv);
  } else {
    ret |= Es8311WriteReg(ES8311_GPIO_REG44, 0x08);
  }
  return ret;
}

int Es8311Init(Es8311Config* cfg) {
  es8311_priv = calloc(1, sizeof(struct es8311_private));
  I2cInit(&cfg->i2c_cfg, cfg->i2c_port_num);  // ESP32 in master mode
  es8311_Codec_Startup(11289600, 44100);
  return 0;
}

void Es8311Uninit() {
  Es8311WriteReg(ES8311_RESET_REG00, 0x3f);
  free(es8311_priv);
  es8311_priv = NULL;
}

int Es8311ConfigFmt(ESCodecModule mode, ESCodecI2SFmt fmt) {
  int res = 0;
  uint8_t regAdc = 0, regDac = 0;
  if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
    res |= Es8311WriteReg(ES8311_ADC_REG17, 0xBF);
  }
  if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
    res |= Es8311WriteReg(ES8311_SYSTEM_REG12, 0x00);
  }
  regAdc = Es8311ReadReg(ES8311_SDPIN_REG09);
  regDac = Es8311ReadReg(ES8311_SDPOUT_REG0A);
  switch (fmt) {
    case ES_I2S_NORMAL:
      ESP_LOGI(TAG, "ES8311 in I2S Format");
      regAdc &= ~0x03;
      regDac &= ~0x03;
      break;
    case ES_I2S_LEFT:
    case ES_I2S_RIGHT:
      ESP_LOGI(TAG, "ES8311 in LJ Format");
      regAdc &= ~0x03;
      regAdc |= 0x01;
      regDac &= ~0x03;
      regDac |= 0x01;
      break;
    case ES_I2S_DSP:
      ESP_LOGI(TAG, "ES8311 in DSP Format");
      regAdc |= 0x03;
      regDac |= 0x03;
      break;
    default:
      ESP_LOGE(TAG, "Not Supported Format");
      break;
  }
  res |= Es8311WriteReg(ES8311_SDPIN_REG09, regAdc);
  res |= Es8311WriteReg(ES8311_SDPOUT_REG0A, regDac);
  return res;
}

int Es8311I2sConfigClock(ESCodecI2sClock cfg) {
  int res = 0;
  return res;
}

int Es8311SetBitsPerSample(ESCodecModule mode, BitsLength bitPerSample) {
  int res = 0;
  uint8_t reg = 0;
  int bits = (int)bitPerSample;

  if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
    reg = Es8311ReadReg(ES8311_SDPIN_REG09);
    reg = reg & 0xe3;
    res |= Es8311WriteReg(ES8311_SDPIN_REG09, reg | (bits << 2));
  }
  if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
    reg = Es8311ReadReg(ES8311_SDPOUT_REG0A);
    reg = reg & 0xe3;
    res |= Es8311WriteReg(ES8311_SDPOUT_REG0A, reg | (bits << 2));
  }
  return res;
}

int Es8311Start(ESCodecModule mode) {
  int res = 0;
  if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
    res |= Es8311WriteReg(ES8311_ADC_REG17, 0xBF);
  }
  if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
    res |= Es8311WriteReg(ES8311_SYSTEM_REG12,
                          Es8311ReadReg(ES8311_SYSTEM_REG12) & 0xfd);
  }
  return res;
}

int Es8311Stop(ESCodecModule mode) {
  int res = 0;
  if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
    res |= Es8311WriteReg(ES8311_ADC_REG17, 0x00);
  }
  if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
    res |= Es8311WriteReg(ES8311_SYSTEM_REG12,
                          Es8311ReadReg(ES8311_SYSTEM_REG12) | 0x02);
  }
  return res;
}

int Es8311SetVoiceVolume(int volume) {
  int res = 0;

  if (volume == 0) {
    volume = 1;
  }

  Es8311WriteReg(ES8311_DAC_REG32, volume);
  return res;
}

int Es8311GetVoiceVolume(int* volume) {
  int res = ESP_OK;
  int regv = Es8311ReadReg(ES8311_DAC_REG32);
  if (regv == ESP_FAIL) {
    *volume = 0;
    res = ESP_FAIL;
  } else {
    *volume = regv * 100 / 256;
  }
  ESP_LOGI(TAG, "GET: res:%d, volume:%d\n", regv, *volume);
  return res;
}

int Es8311SetVoiceMute(int enable) {
  int res = 0;
  ESP_LOGI(TAG, "Es8311SetVoiceMute volume:%d\n", enable);
  es8311_mute(enable);
  return res;
}

int Es8311GetVoiceMute(int* mute) {
  int res = -1;
  uint8_t reg = 0;
  res = Es8311ReadReg(ES8311_DAC_REG31);
  if (res != ESP_FAIL) {
    reg = (res & 0x20) >> 5;
  }
  *mute = reg;
  return res;
}

int Es8311SetMicGain(MicGain gain) {
  int res = 0;
  uint8_t gain_n = Es8311ReadReg(ES8311_ADC_REG16) & 0x07;
  gain_n |= gain / 6;
  res = Es8311WriteReg(ES8311_ADC_REG16, gain_n);  // MIC gain scale
  return res;
}

int Es8311ConfigAdcInput(AdcInput input) {
  int res = 0;
  return res;
}

int Es8311SetAdcVolume(uint8_t adc_vol) {
  int res = 0;
  res = Es8311WriteReg(ES8311_ADC_REG17, adc_vol);  // MIC ADC Volume
  return res;
}

int ES8311WriteReg(uint8_t regAdd, uint8_t data) {
  return Es8311WriteReg(regAdd, data);
}

void Es8311ReadAll() {
  for (int i = 0; i < 0x4A; i++) {
    uint8_t reg = Es8311ReadReg(i);
    // ets_printf("REG:%02x, %02x\n", reg, i);
  }
}