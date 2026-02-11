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

#include <string.h>
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "Config.h"
#include "adac.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "math.h"
#include "stdio.h"
#define CS4265_PULL_UP (0x4F)
#define CS4265_PULL_DOWN (0x4E)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

static const char TAG[] = "CS4265";

static bool init(sys_dac_config * config, i2s_config_t* i2s_config, bool* mck);
static void speaker(bool active);
static void headset(bool active);
static bool volume(unsigned left, unsigned right);
static void power(adac_power_e mode);
static esp_err_t cs4265_update_bit(uint8_t reg_no, uint8_t mask, uint8_t val);
static esp_err_t set_clock();
const struct adac_s dac_cs4265 = {
    sys_dac_models_CS4265, init, adac_deinit, power, speaker, headset, volume};

struct cs4265_cmd_s {
    uint8_t reg;
    uint8_t value;
};
struct cs4265_private {
    uint8_t format;
    uint32_t sysclk;
    i2s_config_t* i2s_config;
	i2c_port_t port;
	};
struct cs4265_private cs4265;

#define CS4265_CHIP_ID 0x1
#define CS4265_CHIP_ID_VAL 0xD0
#define CS4265_CHIP_ID_MASK 0xF0
#define CS4265_REV_ID_MASK 0x0F

#define CS4265_PWRCTL 0x02
#define CS4265_PWRCTL_PDN (1 << 0)
#define CS4265_PWRCTL_PDN_DAC (1 << 1)
#define CS4265_PWRCTL_PDN_ADC (1 << 2)
#define CS4265_PWRCTL_PDN_MIC (1 << 3)
#define CS4265_PWRCTL_FREEZE (1 << 7)
#define CS4265_PWRCTL_PDN_ALL                                                                      \
    CS4265_PWRCTL_PDN | CS4265_PWRCTL_PDN_ADC | CS4265_PWRCTL_PDN_DAC | CS4265_PWRCTL_PDN_MIC

#define CS4265_DAC_CTL 0x3
// De-Emphasis Control (Bit 1)
// The standard 50/15 i2s digital de-emphasis filter response may be implemented for a sample
// rate of 44.1 kHz when the DeEmph bit is set.  NOTE: De-emphasis is available only in Single-Speed
// Mode.
#define CS4265_DAC_CTL_DEEMPH (1 << 1)
// MUTE DAC
// The DAC outputs will mute and the MUTEC pin will become active when this bit is set. Though this
// bit is active high, it should be noted that the MUTEC pin is active low. The common mode voltage
// on the outputs will be retained when this bit is set. The muting function is effected, similar to
// attenuation changes, by the DACSoft and DACZero bits in the DAC Control 2 register.
#define CS4265_DAC_CTL_MUTE (1 << 2)
// The required relationship between LRCK, SCLK and SDIN for the DAC is defined by the DAC Digital
// Interface DAC_DIF1 DAC_DIF0 Description                                    Format Figure 0 0 Left
// Justified, up to 24-bit data (default)    0       5 0        1        I²S, up to 24-bit data 1 6
// 1        0        Right-Justified, 16-bit Data                   2       7
// 1        1        Right-Justified, 24-bit Data                   3       7
#define CS4265_DAC_CTL_DIF0 (1 << 4)
// The required relationship between LRCK, SCLK and SDIN for the DAC is defined by the DAC Digital
// Interface DAC_DIF1 DAC_DIF0 Description                                    Format Figure 0 0 Left
// Justified, up to 24-bit data (default)    0       5 0        1        I²S, up to 24-bit data 1 6
// 1        0        Right-Justified, 16-bit Data                   2       7
// 1        1        Right-Justified, 24-bit Data                   3       7
#define CS4265_DAC_CTL_DIF1 (1 << 5)

#define CS4265_ADC_CTL 0x4
#define CS4265_ADC_MASTER 1

#define CS4265_ADC_CTL_MUTE (1 << 2)
#define CS4265_ADC_DIF (1 << 4)
#define CS4265_ADC_FM (3 << 6)

// Master Clock Dividers (Bits 6:4)
// Sets the frequency of the supplied MCLK signal.
//
// MCLK Divider MCLK Freq2 MCLK Freq1 MCLK Freq0
//  ÷   1 	   0          0           0
//  ÷   1.5 	   0          0           1
//  ÷   2 	   0          1           0
//  ÷   3 	   0          1           1
//  ÷   4 	   1          0           0
//  NA          1          0           1
//  NA 		   1          1           x
#define CS4265_MCLK_FREQ 0x5
#define CS4265_MCLK_FREQ_1_0X (0b000 << 4)
#define CS4265_MCLK_FREQ_1_5X (0b001 << 4)
#define CS4265_MCLK_FREQ_2_0X (0b010 << 4)
#define CS4265_MCLK_FREQ_3_0X (0b011 << 4)
#define CS4265_MCLK_FREQ_4_0X (0b100 << 4)

#define CS4265_MCLK_FREQ_MASK (7 << 4)

#define CS4265_SIG_SEL 0x6
#define CS4265_SIG_SEL_LOOP (1 << 1)
#define CS4265_SIG_SEL_SDIN2 (1 << 7)
#define CS4265_SIG_SEL_SDIN1 (0 << 7)

// Sets the gain or attenuation for the ADC input PGA stage. The gain may be adjusted from -12 dB to
// +12 dB in 0.5 dB steps. The gain bits are in two’s complement with the Gain0 bit set for a 0.5 dB
// step. Register settings outside of the ±12 dB range are reserved and must not be used. See Table
// 13 for example settings
#define CS4265_CHB_PGA_CTL 0x7
// Sets the gain or attenuation for the ADC input PGA stage. The gain may be adjusted from -12 dB to
// +12 dB in 0.5 dB steps. The gain bits are in two’s complement with the Gain0 bit set for a 0.5 dB
// step. Register settings outside of the ±12 dB range are reserved and must not be used. See Table
// 13 for example settings
#define CS4265_CHA_PGA_CTL 0x8
// Gain[5:0]    Setting
// 101000       -12 dB
// 000000       0 dB
// 011000       +12 dB

#define CS4265_ADC_CTL2 0x9

// The digital volume control allows the user to attenuate the signal in 0.5 dB increments from 0 to
// -127 dB. The Vol0 bit activates a 0.5 dB attenuation when set, and no attenuation when cleared.
// The Vol[7:1] bits activate attenuation equal to their decimal equivalent (in dB).
// Binary Code 	Volume Setting
// 00000000 		0 dB
// 00000001 		-0.5 dB
// 00101000 		-20 dB
// 00101001 		-20.5 dB
// 11111110 		-127 dB
// 11111111 		-127.5 dB
#define CS4265_DAC_CHA_VOL 0xA
// The digital volume control allows the user to attenuate the signal in 0.5 dB increments from 0 to
// -127 dB. The Vol0 bit activates a 0.5 dB attenuation when set, and no attenuation when cleared.
// The Vol[7:1] bits activate attenuation equal to their decimal equivalent (in dB).
// Binary Code 	Volume Setting
// 00000000 		0 dB
// 00000001 		-0.5 dB
// 00101000 		-20 dB
// 00101001 		-20.5 dB
// 11111110 		-127 dB
// 11111111 		-127.5 dB
#define CS4265_DAC_CHB_VOL 0xB
#define CS4265_DAC_VOL_ATT_000_0 0b00000000
#define CS4265_DAC_VOL_ATT_000_5 0b00000001
#define CS4265_DAC_VOL_ATT_020_0 0b00101000
#define CS4265_DAC_VOL_ATT_020_5 0b00101001
#define CS4265_DAC_VOL_ATT_127_0 0b11111110
#define CS4265_DAC_VOL_ATT_127_5 0b11111111

// DAC Soft Ramp or Zero Cross Enable (Bits 7:6)
//
// Soft Ramp Enable
// Soft Ramp allows level changes, both muting and attenuation, to be implemented by incrementally
// ramping, in 1/8 dB steps, from the current level to the new level at a rate of 1 dB per 8
// left/right clock periods. See Table 17. Zero Cross Enable Zero Cross Enable dictates that
// signal-level changes, either by attenuation changes or muting, will occur on a signal zero
// crossing to minimize audible artifacts. The requested level change will occur after a timeout
// period between 512 and 1024 sample periods (10.7 ms to 21.3 ms at 48 kHz sample rate) if the
// signal does not encounter a zero crossing. The zero cross function is independently monitored and
// implemented for each channel. See Table 17. Soft Ramp and Zero Cross Enable Soft Ramp and Zero
// Cross Enable dictate that signal-level changes, either by attenuation changes or muting, will
// occur in 1/8 dB steps and be implemented on a signal zero crossing. The 1/8 dB level change will
// occur after a time-out period between 512 and 1024 sample periods (10.7 ms to 21.3 ms at 48 kHz
// sample rate) if the signal does not encounter a zero crossing. The zero cross function is
// independently monitored and implemented for each channel DACSoft DACZeroCross Mode 0 0 Changes to
// affect immediately 0 1 Zero Cross enabled 1 0 Soft Ramp enabled 1 1 Soft Ramp and Zero Cross
// enabled (default)
#define CS4265_DAC_CTL2 0xC
#define CS4265_DAC_CTL2_ZERO_CROSS_EN (uint8_t)(0b01 << 7)
#define CS4265_DAC_CTL2_SOFT_RAMP_EN (uint8_t)(0b10 << 7)
#define CS4265_DAC_CTL2_SOFT_RAMP_ZERO_CROSS_EN (uint8_t)(0b11 << 7)

#define CS4265_INT_STATUS 0xD
#define CS4265_INT_STATUS_ADC_UNDF (1 << 0)
#define CS4265_INT_STATUS_ADC_OVF (1 << 1)
#define CS4265_INT_STATUS_CLKERR (1 << 3)

#define CS4265_INT_MASK 0xE
#define CS4265_STATUS_MODE_MSB 0xF
#define CS4265_STATUS_MODE_LSB 0x10

// Transmitter Control 1 - Address 11h
#define CS4265_SPDIF_CTL1 0x11

#define CS4265_SPDIF_CTL2 0x12
// Transmitter Digital Interface Format (Bits 7:6)
// Function:
// The required relationship between LRCK, SCLK and SDIN for the transmitter is defined
// Tx_DIF1 Tx_DIF0 Description Format Figure
// 0 0 Left Justified, up to 24-bit data (default) 0 5
// 0 1 I²S, up to 24-bit data 1 6
// 1 0 Right-Justified, 16-bit Data 2 7
// 1 1 Right-Justified, 24-bit Data 3 7
#define CS4265_SPDIF_CTL2_MMTLR (1 << 0)
#define CS4265_SPDIF_CTL2_MMTCS (1 << 1)
#define CS4265_SPDIF_CTL2_MMT (1 << 2)
#define CS4265_SPDIF_CTL2_V (1 << 3)
#define CS4265_SPDIF_CTL2_TXMUTE (1 << 4)
#define CS4265_SPDIF_CTL2_TXOFF (1 << 5)
#define CS4265_SPDIF_CTL2_MUTE (1 << 4)
#define CS4265_SPDIF_CTL2_DIF (3 << 6)
#define CS4265_SPDIF_CTL2_DIF0 (1 << 6)
#define CS4265_SPDIF_CTL2_DIF1 (1 << 7)

#define CS4265_C_DATA_BUFF 0x13
#define CS4265_MAX_REGISTER 0x2A
struct cs4265_clk_para {
    uint32_t mclk;
    uint32_t rate;
    uint8_t fm_mode; /* values 1, 2, or 4 */
    uint8_t mclkdiv;
};
static const struct cs4265_clk_para clk_map_table[] = {
    /*32k*/
    {8192000, 32000, 0, 0},
    {12288000, 32000, 0, 1},
    {16384000, 32000, 0, 2},
    {24576000, 32000, 0, 3},
    {32768000, 32000, 0, 4},

    /*44.1k*/
    {11289600, 44100, 0, 0},
    {16934400, 44100, 0, 1},
    {22579200, 44100, 0, 2},
    {33868000, 44100, 0, 3},
    {45158400, 44100, 0, 4},

    /*48k*/
    {12288000, 48000, 0, 0},
    {18432000, 48000, 0, 1},
    {24576000, 48000, 0, 2},
    {36864000, 48000, 0, 3},
    {49152000, 48000, 0, 4},

    /*64k*/
    {8192000, 64000, 1, 0},
    {12288000, 64000, 1, 1},
    {16934400, 64000, 1, 2},
    {24576000, 64000, 1, 3},
    {32768000, 64000, 1, 4},

    /* 88.2k */
    {11289600, 88200, 1, 0},
    {16934400, 88200, 1, 1},
    {22579200, 88200, 1, 2},
    {33868000, 88200, 1, 3},
    {45158400, 88200, 1, 4},

    /* 96k */
    {12288000, 96000, 1, 0},
    {18432000, 96000, 1, 1},
    {24576000, 96000, 1, 2},
    {36864000, 96000, 1, 3},
    {49152000, 96000, 1, 4},

    /* 128k */
    {8192000, 128000, 2, 0},
    {12288000, 128000, 2, 1},
    {16934400, 128000, 2, 2},
    {24576000, 128000, 2, 3},
    {32768000, 128000, 2, 4},

    /* 176.4k */
    {11289600, 176400, 2, 0},
    {16934400, 176400, 2, 1},
    {22579200, 176400, 2, 2},
    {33868000, 176400, 2, 3},
    {49152000, 176400, 2, 4},

    /* 192k */
    {12288000, 192000, 2, 0},
    {18432000, 192000, 2, 1},
    {24576000, 192000, 2, 2},
    {36864000, 192000, 2, 3},
    {49152000, 192000, 2, 4},
};
static const struct cs4265_cmd_s cs4265_init_sequence[] = {
    {CS4265_PWRCTL, CS4265_PWRCTL_PDN_ADC | CS4265_PWRCTL_FREEZE | CS4265_PWRCTL_PDN_DAC |
                        CS4265_PWRCTL_PDN_MIC},
    {CS4265_DAC_CTL, CS4265_DAC_CTL_DIF0 | CS4265_DAC_CTL_MUTE},
    {CS4265_SIG_SEL, CS4265_SIG_SEL_SDIN1},         /// SDIN1
    {CS4265_SPDIF_CTL2, CS4265_SPDIF_CTL2_DIF0},    //
    {CS4265_ADC_CTL, 0x00},                         // // Set the serial audio port in slave mode
    {CS4265_MCLK_FREQ, CS4265_MCLK_FREQ_1_0X},      // // no divider
    {CS4265_CHB_PGA_CTL, 0x00},                     // // sets the gain to 0db on channel B
    {CS4265_CHA_PGA_CTL, 0x00},                     // // sets the gain to 0db on channel A
    {CS4265_ADC_CTL2, 0x19},                        //
    {CS4265_DAC_CHA_VOL, CS4265_DAC_VOL_ATT_000_0}, // Full volume out
    {CS4265_DAC_CHB_VOL, CS4265_DAC_VOL_ATT_000_0}, // // Full volume out
    {CS4265_DAC_CTL2, CS4265_DAC_CTL2_SOFT_RAMP_ZERO_CROSS_EN}, //
    {CS4265_SPDIF_CTL1, 0x00},                                  //
    {CS4265_INT_MASK, 0x00},                                    //
    {CS4265_STATUS_MODE_MSB, 0x00},                             //
    {CS4265_STATUS_MODE_LSB, 0x00},                             //
    {0xff, 0xff}};

// matching orders
typedef enum {
    cs4265_ACTIVE = 0,
    cs4265_STANDBY,
    cs4265_DOWN,
    cs4265_ANALOGUE_OFF,
    cs4265_ANALOGUE_ON,
    cs4265_VOLUME
} dac_cmd_e;

static int cs4265_addr;

static void dac_cmd(dac_cmd_e cmd, ...);
static int cs4265_detect(void);
static uint32_t calc_rnd_mclk_freq() {
    float m_scale =
        (cs4265.i2s_config->sample_rate > 96000 && cs4265.i2s_config->bits_per_sample > 16) ? 4 : 8;
    float num_channels = cs4265.i2s_config->channel_format < I2S_CHANNEL_FMT_ONLY_RIGHT ? 2 : 1;
    return (uint32_t)round(cs4265.i2s_config->bits_per_sample * i2s_get_clk(cs4265.port) *
                           m_scale * num_channels / 100) *
           100;
}
static int cs4265_get_clk_index(int mclk, int rate) {
    for (int i = 0; i < ARRAY_SIZE(clk_map_table); i++) {
        if (clk_map_table[i].rate == rate && clk_map_table[i].mclk == mclk) return i;
    }
    return -1;
}

static esp_err_t set_clock() {
    esp_err_t err = ESP_OK;
    uint32_t mclk = calc_rnd_mclk_freq();
    int index = cs4265_get_clk_index(mclk, cs4265.i2s_config->sample_rate);
    if (index >= 0) {
        ESP_LOGD(TAG, "Setting clock for mclk %u, rate %u (fm mode:%u, clk div:%u))", mclk,
            cs4265.i2s_config->sample_rate, clk_map_table[index].fm_mode,
            clk_map_table[index].mclkdiv);
        err = cs4265_update_bit(CS4265_ADC_CTL, CS4265_ADC_FM, clk_map_table[index].fm_mode << 6);
        err |= cs4265_update_bit(
            CS4265_MCLK_FREQ, CS4265_MCLK_FREQ_MASK, clk_map_table[index].mclkdiv << 4);
    } else {
        ESP_LOGE(TAG, "can't get correct mclk for ");
        return -1;
    }
    return err;
}

static void get_status() {
    uint8_t sts1 = adac_read_byte(cs4265_addr, CS4265_INT_STATUS);
    ESP_LOGD(TAG, "Status: %s", sts1 & CS4265_INT_STATUS_CLKERR ? "CLK Error" : "CLK OK");
}

/****************************************************************************************
 * init
 */
static bool init(sys_dac_config * config, i2s_config_t* i2s_config, bool* mck) {

    // find which CS4265 we are using (if any)
    cs4265_addr = adac_init(config);
    cs4265.i2s_config = i2s_config;
    if (!cs4265_addr) cs4265_addr = cs4265_detect();
    if (!cs4265_addr) {
        ESP_LOGE(TAG, "No cs4265 detected");
        adac_deinit();
        return false;
    }
	cs4265.port = config->i2c.port-sys_i2c_port_PORT0;
#if BYTES_PER_FRAME == 8
    ESP_LOGE(TAG, "The CS4265 does not support 32 bits mode. ");
    adac_deinit();
    return false;
#endif
    // we need mclk for this DAC
    *mck = true;

	// Initialize the chip
    i2c_cmd_handle_t i2c_cmd = i2c_cmd_link_create();
    for (int i = 0; cs4265_init_sequence[i].reg != 0xff; i++) {
        i2c_master_start(i2c_cmd);
        i2c_master_write_byte(i2c_cmd, (cs4265_addr << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
        i2c_master_write_byte(i2c_cmd, cs4265_init_sequence[i].reg, I2C_MASTER_NACK);
        i2c_master_write_byte(i2c_cmd, cs4265_init_sequence[i].value, I2C_MASTER_NACK);
        ESP_LOGD(
            TAG, "i2c write %x at %u", cs4265_init_sequence[i].reg, cs4265_init_sequence[i].value);
    }
	
    i2c_master_stop(i2c_cmd);
    esp_err_t res = i2c_master_cmd_begin(cs4265.port, i2c_cmd, 500 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(i2c_cmd);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "could not intialize cs4265 %d", res);
        return false;
    }

    return true;
}

static esp_err_t cs4265_update_bit(uint8_t reg_no, uint8_t mask, uint8_t val) {
    esp_err_t ret = ESP_OK;
    uint8_t old = adac_read_byte(cs4265_addr, reg_no);
    uint8_t newval = (old & ~mask) | (val & mask);
    bool change = old != newval;
    if (change) {
        ret = adac_write_byte(cs4265_addr, reg_no, newval);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG,
                "Unable to change dac register 0x%02x [0x%02x->0x%02x] from value 0x%02x, mask "
                "0x%02x  ",
                reg_no, old, newval, val, mask);
        } else {
            ESP_LOGD(TAG,
                "Changed dac register 0x%02x [0x%02x->0x%02x] from value 0x%02x, mask 0x%02x ",
                reg_no, old, newval, val, mask);
        }
    }

    return ret;
}

/****************************************************************************************
 * change volume
 */
static bool volume(unsigned left, unsigned right) { return false; }

/****************************************************************************************
 * power
 */
static void power(adac_power_e mode) {
    switch (mode) {
    case ADAC_STANDBY:
        dac_cmd(cs4265_STANDBY);
        break;
    case ADAC_ON:
        dac_cmd(cs4265_ACTIVE);
        break;
    case ADAC_OFF:
        dac_cmd(cs4265_DOWN);
        break;
    default:
        ESP_LOGW(TAG, "unknown DAC command");
        break;
    }
}

/****************************************************************************************
 * speaker
 */
static void speaker(bool active) {
    if (active)
        dac_cmd(cs4265_ANALOGUE_ON);
    else
        dac_cmd(cs4265_ANALOGUE_OFF);
}

/****************************************************************************************
 * headset
 */
static void headset(bool active) {}

/****************************************************************************************
 * DAC specific commands
 */
void dac_cmd(dac_cmd_e cmd, ...) {
    va_list args;
    esp_err_t ret = ESP_OK;

    va_start(args, cmd);

    switch (cmd) {
    case cs4265_VOLUME:
        ESP_LOGE(TAG, "DAC volume not handled yet");
        break;
    case cs4265_ACTIVE:
        ESP_LOGD(TAG, "Activating DAC");
        adac_write_byte(cs4265_addr, CS4265_PWRCTL, 0);
        cs4265_update_bit(CS4265_SPDIF_CTL2, CS4265_SPDIF_CTL2_TXOFF, 0);
        cs4265_update_bit(CS4265_SPDIF_CTL2, CS4265_SPDIF_CTL2_TXMUTE, 0);
        cs4265_update_bit(CS4265_DAC_CTL, CS4265_DAC_CTL_MUTE, 0);
        break;
    case cs4265_STANDBY:
        ESP_LOGD(TAG, "DAC Stand-by");
        cs4265_update_bit(CS4265_SPDIF_CTL2, CS4265_SPDIF_CTL2_TXOFF, CS4265_SPDIF_CTL2_TXOFF);
        cs4265_update_bit(CS4265_SPDIF_CTL2, CS4265_SPDIF_CTL2_TXMUTE, CS4265_SPDIF_CTL2_TXMUTE);
        cs4265_update_bit(CS4265_DAC_CTL, CS4265_DAC_CTL_MUTE, CS4265_DAC_CTL_MUTE);
        break;
    case cs4265_DOWN:
        ESP_LOGD(TAG, "DAC Power Down");
        adac_write_byte(cs4265_addr, CS4265_PWRCTL, CS4265_PWRCTL_PDN_ALL);
        break;
    case cs4265_ANALOGUE_OFF:
        ESP_LOGD(TAG, "DAC Analog off");
        cs4265_update_bit(CS4265_SPDIF_CTL2, CS4265_SPDIF_CTL2_TXOFF, CS4265_SPDIF_CTL2_TXOFF);
        cs4265_update_bit(CS4265_SPDIF_CTL2, CS4265_SPDIF_CTL2_TXMUTE, CS4265_SPDIF_CTL2_TXMUTE);
        cs4265_update_bit(CS4265_DAC_CTL, CS4265_DAC_CTL_MUTE, CS4265_DAC_CTL_MUTE);
        break;
    case cs4265_ANALOGUE_ON:
        ESP_LOGD(TAG, "DAC Analog on");
        adac_write_byte(cs4265_addr, CS4265_PWRCTL, 0);
        cs4265_update_bit(CS4265_SPDIF_CTL2, CS4265_SPDIF_CTL2_TXOFF, 0);
        cs4265_update_bit(CS4265_SPDIF_CTL2, CS4265_SPDIF_CTL2_TXMUTE, 0);
        cs4265_update_bit(CS4265_DAC_CTL, CS4265_DAC_CTL_MUTE, 0);
        break;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "could not use cs4265 %d", ret);
    }
    get_status();
    // now set the clock
    ret = set_clock();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "could not set the cs4265's clock %d", ret);
    }

    va_end(args);
}

/****************************************************************************************
 * TAS57 detection
 */
static int cs4265_detect(void) {
    uint8_t addr[] = {CS4265_PULL_DOWN, CS4265_PULL_UP};
    ESP_LOGD(TAG, "Detecting chip connection type/address");
    for (int i = 0; i < sizeof(addr); i++) {
        ESP_LOGI(TAG, "Looking for CS4265 @0x%x", addr[i]);
        uint8_t reg = adac_read_byte(addr[i], CS4265_CHIP_ID);
        if (reg == 255) {
            ESP_LOGD(TAG, "Nothing there");
            continue;
        }
        // found a device at that address
        ESP_LOGD(TAG, "Found a device. Check signature");
        uint8_t devid = reg & CS4265_CHIP_ID_MASK;
        if (devid != CS4265_CHIP_ID_VAL) {
            ESP_LOGE(TAG, "CS4265 Device ID (%X). Expected %X", devid, CS4265_CHIP_ID);
            return 0;
        }
        ESP_LOGI(TAG, "Found DAC @0x%x, Version %x", addr[i], reg & CS4265_REV_ID_MASK);
        return addr[i];
    }
    return 0;
}
