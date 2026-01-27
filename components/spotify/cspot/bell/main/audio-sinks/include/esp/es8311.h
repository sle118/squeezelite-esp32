/*
* ES8311.h  --  ES8311 ALSA SoC Audio Codec
*
* Authors:
*
* Based on ES8374.h by David Yang
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/

#ifndef _ES8311_H
#define _ES8311_H
#include "driver/i2c.h"
#include "esxxx_common.h"

/*
*   ES8311_REGISTER NAME_REG_REGISTER ADDRESS
*/
#define ES8311_RESET_REG00 0x00 /*reset digital,csm,clock manager etc.*/

/*
* Clock Scheme Register definition
*/
#define ES8311_CLK_MANAGER_REG01 \
  0x01 /* select clk src for mclk, enable clock for codec */
#define ES8311_CLK_MANAGER_REG02 0x02 /* clk divider and clk multiplier */
#define ES8311_CLK_MANAGER_REG03 0x03 /* adc fsmode and osr  */
#define ES8311_CLK_MANAGER_REG04 0x04 /* dac osr */
#define ES8311_CLK_MANAGER_REG05 0x05 /* clk divier for adc and dac */
#define ES8311_CLK_MANAGER_REG06 0x06 /* bclk inverter and divider */
#define ES8311_CLK_MANAGER_REG07 0x07 /* tri-state, lrck divider */
#define ES8311_CLK_MANAGER_REG08 0x08 /* lrck divider */
#define ES8311_SDPIN_REG09 0x09       /* dac serial digital port */
#define ES8311_SDPOUT_REG0A 0x0A      /* adc serial digital port */
#define ES8311_SYSTEM_REG0B 0x0B      /* system */
#define ES8311_SYSTEM_REG0C 0x0C      /* system */
#define ES8311_SYSTEM_REG0D 0x0D      /* system, power up/down */
#define ES8311_SYSTEM_REG0E 0x0E      /* system, power up/down */
#define ES8311_SYSTEM_REG0F 0x0F      /* system, low power */
#define ES8311_SYSTEM_REG10 0x10      /* system */
#define ES8311_SYSTEM_REG11 0x11      /* system */
#define ES8311_SYSTEM_REG12 0x12      /* system, Enable DAC */
#define ES8311_SYSTEM_REG13 0x13      /* system */
#define ES8311_SYSTEM_REG14 \
  0x14                         /* system, select DMIC, select analog pga gain */
#define ES8311_ADC_REG15 0x15  /* ADC, adc ramp rate, dmic sense */
#define ES8311_ADC_REG16 0x16  /* ADC */
#define ES8311_ADC_REG17 0x17  /* ADC, volume */
#define ES8311_ADC_REG18 0x18  /* ADC, alc enable and winsize */
#define ES8311_ADC_REG19 0x19  /* ADC, alc maxlevel */
#define ES8311_ADC_REG1A 0x1A  /* ADC, alc automute */
#define ES8311_ADC_REG1B 0x1B  /* ADC, alc automute, adc hpf s1 */
#define ES8311_ADC_REG1C 0x1C  /* ADC, equalizer, hpf s2 */
#define ES8311_DAC_REG31 0x31  /* DAC, mute */
#define ES8311_DAC_REG32 0x32  /* DAC, volume */
#define ES8311_DAC_REG33 0x33  /* DAC, offset */
#define ES8311_DAC_REG34 0x34  /* DAC, drc enable, drc winsize */
#define ES8311_DAC_REG35 0x35  /* DAC, drc maxlevel, minilevel */
#define ES8311_DAC_REG37 0x37  /* DAC, ramprate */
#define ES8311_GPIO_REG44 0x44 /* GPIO, dac2adc for test */
#define ES8311_GP_REG45 0x45   /* GP CONTROL */
#define ES8311_CHD1_REGFD 0xFD /* CHIP ID1 */
#define ES8311_CHD2_REGFE 0xFE /* CHIP ID2 */
#define ES8311_CHVER_REGFF 0xFF /* VERSION */
#define ES8311_CHD1_REGFD 0xFD  /* CHIP ID1 */

#define ES8311_MAX_REGISTER 0xFF

typedef struct {
  ESCodecMode esMode;
  i2c_port_t i2c_port_num;
  i2c_config_t i2c_cfg;
  DacOutput dacOutput;
  AdcInput adcInput;
} Es8311Config;

#define AUDIO_CODEC_ES8311_DEFAULT()                                        \
  {                                                                         \
      .esMode = ES_MODE_SLAVE,                                              \
      .i2c_port_num = I2C_NUM_0,                                            \
      .i2c_cfg = {.mode = I2C_MODE_MASTER,                                  \
                  .sda_io_num = IIC_DATA,                                   \
                  .scl_io_num = IIC_CLK,                                    \
                  .sda_pullup_en = GPIO_PULLUP_ENABLE,                      \
                  .scl_pullup_en = GPIO_PULLUP_ENABLE,                      \
                  .master.clk_speed = 100000},                              \
      .adcInput = ADC_INPUT_LINPUT1_RINPUT1,                                \
      .dacOutput = DAC_OUTPUT_LOUT1 | DAC_OUTPUT_LOUT2 | DAC_OUTPUT_ROUT1 | \
                   DAC_OUTPUT_ROUT2,                                        \
  };

int Es8311Init(Es8311Config* cfg);
void Es8311Uninit();
esp_err_t Es8311GetRef(bool flag);
esp_err_t Es7243Init(void);

int Es7243ReadReg(uint8_t regAdd);

int Es8311ConfigFmt(ESCodecModule mode, ESCodecI2SFmt fmt);
int Es8311I2sConfigClock(ESCodecI2sClock cfg);
int Es8311SetBitsPerSample(ESCodecModule mode, BitsLength bitPerSample);
void es8311_Codec_Startup(uint32_t mclk_freq, uint32_t lrck_freq);

int Es8311Start(ESCodecModule mode);
int Es8311Stop(ESCodecModule mode);

int Es8311SetVoiceVolume(int volume);
int Es8311GetVoiceVolume(int* volume);
int Es8311SetVoiceMute(int enable);
int Es8311GetVoiceMute(int* mute);
int Es8311SetMicGain(MicGain gain);

int Es8311ConfigAdcInput(AdcInput input);
int Es8311ConfigDacOutput(DacOutput output);

int ES8311WriteReg(uint8_t regAdd, uint8_t data);

void Es8311ReadAll();
int Es8311ReadReg(uint8_t regAdd);
#endif