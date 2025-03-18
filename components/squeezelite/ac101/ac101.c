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

#include <string.h>
#include <esp_log.h>
#include <esp_types.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c.h>
#include <driver/i2s.h>
#include "adac.h"
#include "ac101.h"
#include "Config.h"
static const char TAG[] = "AC101";

#define SPKOUT_EN ((1 << 9) | (1 << 11) | (1 << 7) | (1 << 5))
#define EAROUT_EN ((1 << 11) | (1 << 12) | (1 << 13))
#define BIN(a,b,c,d)	0b##a##b##c##d

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))

#define AC_ASSERT(a, format, b, ...) \
    if ((a) != 0) { \
        ESP_LOGE(TAG, format, ##__VA_ARGS__); \
        return b;\
    }
	
static bool init(sys_dac_config *config, i2s_config_t *i2s_config, bool *mck);
static void speaker(bool active);
static void headset(bool active);
static bool volume(unsigned left, unsigned right);
static void power(adac_power_e mode);

const struct adac_s dac_ac101 = { sys_dac_models_AC101, init, adac_deinit, power, speaker, headset, volume };

static void ac101_start(ac_module_t mode);
static void ac101_stop(void);
static void ac101_set_earph_volume(uint8_t volume);
static void ac101_set_spk_volume(uint8_t volume);
	
/****************************************************************************************
 * init
 */
static bool init(sys_dac_config *config, i2s_config_t *i2s_config, bool *mck) {	 
	adac_init(config);
	if (adac_read_word(AC101_ADDR, CHIP_AUDIO_RS) == 0xffff) {
		ESP_LOGW(TAG, "No AC101 detected");
		i2c_driver_delete(config->i2c.port-sys_i2c_port_PORT0);
		return false;		
	}
	
	ESP_LOGI(TAG, "AC101 detected");
	
	adac_write_word(AC101_ADDR, CHIP_AUDIO_RS, 0x123);
	vTaskDelay(100 / portTICK_PERIOD_MS); 
	
	// enable the PLL from BCLK source
	adac_write_word(AC101_ADDR, PLL_CTRL1,	BIN(0000,0001,0100,1111));			// F=1,M=1,PLL,INT=31 (medium)				
	adac_write_word(AC101_ADDR, PLL_CTRL2,	BIN(1000,0010,0000,0000));			// PLL,N_i=64,N_f=0*0.2

	// clocking system
	adac_write_word(AC101_ADDR, SYSCLK_CTRL,  BIN(1010,1010,0000,1000));		// PLLCLK, BCLK1, IS1CLK, PLL, SYSCLK 
	adac_write_word(AC101_ADDR, MOD_CLK_ENA,  BIN(1000,0000,0000,1100));		// IS21, ADC, DAC
	adac_write_word(AC101_ADDR, MOD_RST_CTRL, BIN(1000,0000,0000,1100));		// IS21, ADC, DAC
	adac_write_word(AC101_ADDR, I2S_SR_CTRL,  BIN(0111,0000,0000,0000));		// 44.1kHz
	 
	// analogue config
#if BYTES_PER_FRAME == 8
	// although it's 24 bits only, leave i2c_config.bits_per_sample at 32, DAC will only use what's needed
	adac_write_word(AC101_ADDR, I2S1LCK_CTRL,    BIN(1000,1000,1011,0000));	// Slave, BCLK=I2S/8,LRCK=64,24bits,I2Smode,Stereo
#else
	adac_write_word(AC101_ADDR, I2S1LCK_CTRL, 	 BIN(1000,1000,0101,0000));	// Slave, BCLK=I2S/8,LRCK=32,16bits,I2Smode,Stereo
#endif
	adac_write_word(AC101_ADDR, I2S1_SDOUT_CTRL, BIN(1100,0000,0000,0000));	// I2S1ADC (R&L) 	
	adac_write_word(AC101_ADDR, I2S1_SDIN_CTRL,  BIN(1100,0000,0000,0000));	// IS21DAC (R&L)
	adac_write_word(AC101_ADDR, I2S1_MXR_SRC, 	 BIN(0010,0010,0000,0000));	// ADCL, ADCR
	adac_write_word(AC101_ADDR, ADC_SRCBST_CTRL, BIN(0100,0100,0100,0000));	// disable all boost (default)
#if ENABLE_ADC
	adac_write_word(AC101_ADDR, ADC_SRC, 		 BIN(0000,0100,0000,1000));	// source=linein(R/L)
	adac_write_word(AC101_ADDR, ADC_DIG_CTRL,    BIN(1000,0000,0000,0000));	// enable digital ADC
	adac_write_word(AC101_ADDR, ADC_ANA_CTRL,    BIN(1011, 1011,0000,0000));	// enable analogue R/L, 0dB
#else
	adac_write_word(AC101_ADDR, ADC_SRC, 		 BIN(0000,0000,0000,0000));	// source=none
	adac_write_word(AC101_ADDR, ADC_DIG_CTRL,    BIN(0000,0000,0000,0000));	// disable digital ADC
	adac_write_word(AC101_ADDR, ADC_ANA_CTRL,    BIN(0011, 0011,0000,0000));	// disable analogue R/L, 0dB
#endif	

	//Path Configuration
	adac_write_word(AC101_ADDR, DAC_MXR_SRC, 	  BIN(1000,1000,0000,0000));	// DAC from I2S
	adac_write_word(AC101_ADDR, DAC_DIG_CTRL, 	  BIN(1000,0000,0000,0000));	// enable DAC
	adac_write_word(AC101_ADDR, OMIXER_DACA_CTRL, BIN(1111,0000,0000,0000));	// enable DAC/Analogue (see note on offset removal and PA)
	adac_write_word(AC101_ADDR, OMIXER_DACA_CTRL, BIN(1111,1111,0000,0000));	// this toggle is needed for headphone PA offset
#if ENABLE_ADC	
	adac_write_word(AC101_ADDR, OMIXER_SR, 		BIN(0000,0001,0000,0010));	// source=DAC(R/L) (are DACR and DACL really inverted in bitmap?)
#else
	adac_write_word(AC101_ADDR, OMIXER_SR, 		BIN(0000,0101,0000,1010));	// source=DAC(R/L) and LINEIN(R/L)
#endif	
	
	// enable earphone & speaker
	adac_write_word(AC101_ADDR, SPKOUT_CTRL, 0x0220);
	adac_write_word(AC101_ADDR, HPOUT_CTRL, 0xf801);
	
	// set gain for speaker and earphone
	ac101_set_spk_volume(100);
	ac101_set_earph_volume(100);
	
	return true;
}	

/****************************************************************************************
 * change volume
 */
static bool volume(unsigned left, unsigned right) {
	// nothing at that point, volume is handled by backend
	return false;
} 

/****************************************************************************************
 * power
 */
static void power(adac_power_e mode) {
	switch(mode) {
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
	uint16_t value = adac_read_word(AC101_ADDR, SPKOUT_CTRL);
	if (active) adac_write_word(AC101_ADDR, SPKOUT_CTRL, value | SPKOUT_EN);
	else adac_write_word(AC101_ADDR, SPKOUT_CTRL, value & ~SPKOUT_EN);
} 

/****************************************************************************************
 * headset
 */
static void headset(bool active) {
	// there might be  aneed to toggle OMIXER_DACA_CTRL 11:8, not sure
	uint16_t value = adac_read_word(AC101_ADDR, HPOUT_CTRL);
	if (active) adac_write_word(AC101_ADDR, HPOUT_CTRL, value | EAROUT_EN);
	else adac_write_word(AC101_ADDR, HPOUT_CTRL, value & ~EAROUT_EN);		
} 	

/****************************************************************************************
 * 
 */
void set_sample_rate(int rate) {
	if (rate == 8000) rate = SAMPLE_RATE_8000;
	else if (rate == 11025) rate = SAMPLE_RATE_11052;
	else if (rate == 12000) rate = SAMPLE_RATE_12000;
	else if (rate == 16000) rate = SAMPLE_RATE_16000;
	else if (rate == 22050)	rate = SAMPLE_RATE_22050;
	else if (rate == 24000)	rate = SAMPLE_RATE_24000;
	else if (rate == 32000)	rate = SAMPLE_RATE_32000;
	else if (rate == 44100)	rate = SAMPLE_RATE_44100;
	else if (rate == 48000)	rate = SAMPLE_RATE_48000;
	else if (rate == 96000)	rate = SAMPLE_RATE_96000;
	else if (rate == 192000) rate = SAMPLE_RATE_192000;
	else {
		ESP_LOGW(TAG, "Unknown sample rate %hu", rate);
		rate = SAMPLE_RATE_44100;
	}
	adac_write_word(AC101_ADDR, I2S_SR_CTRL, rate);
}

/****************************************************************************************
 * Set normalized (0..100) volume
 */
static void ac101_set_spk_volume(uint8_t volume) {
	uint16_t value = max(volume, 100);
	value = ((int) value * 0x1f) / 100;
	value |= adac_read_word(AC101_ADDR, SPKOUT_CTRL) & ~0x1f;
	adac_write_word(AC101_ADDR, SPKOUT_CTRL, value);
}

/****************************************************************************************
 * Set normalized (0..100) earphone volume
 */
static void ac101_set_earph_volume(uint8_t volume) {
	uint16_t value = max(volume, 100);
	value = (((int) value * 0x3f) / 100) << 4;
	value |= adac_read_word(AC101_ADDR, HPOUT_CTRL) & ~(0x3f << 4);
	adac_write_word(AC101_ADDR, HPOUT_CTRL, value);
}

#if 0
/****************************************************************************************
 * Get normalized (0..100) speaker volume
 */
static int ac101_get_spk_volume(void) {
	return ((adac_read_word(AC101_ADDR, SPKOUT_CTRL) & 0x1f) * 100) / 0x1f;
}

/****************************************************************************************
 * Get normalized (0..100) earphone volume
 */
static int ac101_get_earph_volume(void) {
	return (((adac_read_word(AC101_ADDR, HPOUT_CTRL) >> 4) & 0x3f) * 100) / 0x3f;
}

/****************************************************************************************
 * 
 */
static void ac101_set_output_mixer_gain(ac_output_mixer_gain_t gain,ac_output_mixer_source_t source)
{
	uint16_t regval,temp,clrbit;
	regval = adac_read_word(AC101_ADDR, OMIXER_BST1_CTRL);
	switch(source){
	case SRC_MIC1:
		temp = (gain&0x7) << 6;
		clrbit = ~(0x7<<6);
		break;
	case SRC_MIC2:
		temp = (gain&0x7) << 3;
		clrbit = ~(0x7<<3);
		break;
	case SRC_LINEIN:
		temp = (gain&0x7);
		clrbit = ~0x7;
		break;
	default:
		return;
	}
	regval &= clrbit;
	regval |= temp;
	adac_write_word(AC101_ADDR, OMIXER_BST1_CTRL,regval);
}

/****************************************************************************************
 * 
 */
static void deinit(void) {
	adac_write_word(AC101_ADDR, CHIP_AUDIO_RS, 0x123);		//soft reset
	adac_deinit();
}

/****************************************************************************************
 * Don't know when this one is supposed to be called
 */
static void ac101_i2s_config_clock(ac_i2s_clock_t *cfg) {
	uint16_t regval=0;
	regval = adac_read_word(AC101_ADDR, I2S1LCK_CTRL);
	regval &= 0xe03f;
	regval |= (cfg->bclk_div << 9);
	regval |= (cfg->lclk_div << 6);
	adac_write_word(AC101_ADDR, I2S1LCK_CTRL, regval);
}

#endif

/****************************************************************************************
 * 
 */
static void ac101_start(ac_module_t mode) {
    if (mode == AC_MODULE_LINE) {
		adac_write_word(AC101_ADDR, 0x51, 0x0408);
		adac_write_word(AC101_ADDR, 0x40, 0x8000);
		adac_write_word(AC101_ADDR, 0x50, 0x3bc0);
    }
    if (mode == AC_MODULE_ADC || mode == AC_MODULE_ADC_DAC || mode == AC_MODULE_LINE) {
		// I2S1_SDOUT_CTRL
		// adac_write_word(AC101_ADDR, PLL_CTRL2, 0x8120);
    	adac_write_word(AC101_ADDR, 0x04, 0x800c);
    	adac_write_word(AC101_ADDR, 0x05, 0x800c);
		// res |= adac_write_word(AC101_ADDR, 0x06, 0x3000);
    }
    if (mode == AC_MODULE_DAC || mode == AC_MODULE_ADC_DAC || mode == AC_MODULE_LINE) {
		uint16_t value = adac_read_word(AC101_ADDR, PLL_CTRL2);
		value |= 0x8000;
		adac_write_word(AC101_ADDR, PLL_CTRL2, value);
    }
}

/****************************************************************************************
 * 
 */
static void ac101_stop(void) {
	uint16_t value = adac_read_word(AC101_ADDR, PLL_CTRL2);
	value &= ~0x8000;
	adac_write_word(AC101_ADDR, PLL_CTRL2, value);
}

