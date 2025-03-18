/*
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 *  (c) C. Rohs 2020 added support for the tas5713 (eg. HiFiBerry AMP+)
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "adac.h"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(*array))
#define TAS5713 (0x36 >> 1) /* i2c address of TAS5713 */

// TAS5713 I2C-bus register addresses

#define TAS5713_CLOCK_CTRL 0x00
#define TAS5713_DEVICE_ID 0x01
#define TAS5713_ERROR_STATUS 0x02
#define TAS5713_SYSTEM_CTRL1 0x03
#define TAS5713_SERIAL_DATA_INTERFACE 0x04
#define TAS5713_SYSTEM_CTRL2 0x05
#define TAS5713_SOFT_MUTE 0x06
#define TAS5713_VOL_MASTER 0x07
#define TAS5713_VOL_CH1 0x08
#define TAS5713_VOL_CH2 0x09
#define TAS5713_VOL_HEADPHONE 0x0A
#define TAS5713_OSC_TRIM 0x1B

static const char TAG[] = "TAS5713";

static bool init(sys_dac_config *config, i2s_config_t *i2s_config, bool *mck);
static void speaker(bool active) { };
static void headset(bool active) { } ;
static bool volume(unsigned left, unsigned right);
static void power(adac_power_e mode) { };

const struct adac_s dac_tas5713 = {sys_dac_models_TAS5713, init, adac_deinit, power, speaker, headset, volume};

struct tas5713_cmd_s {
    uint8_t reg;
    uint8_t value;
};

// matching orders
typedef enum {
    TAS57_ACTIVE = 0,
    TAS57_STANDBY,
    TAS57_DOWN,
    TAS57_ANALOGUE_OFF,
    TAS57_ANALOGUE_ON,
    TAS57_VOLUME
} dac_cmd_e;

/****************************************************************************************
 * init
 */
static bool init(sys_dac_config *config, i2s_config_t *i2s_config, bool *mck) {	 
	/* find if there is a tas5713 attached. Reg 0 should read non-zero but not 255 if so */
	adac_init(config);
    if (adac_read_byte(TAS5713, 0x00) == 255) {
        ESP_LOGW(TAG, "No TAS5713 detected");
        adac_deinit();
        return 0;
    }

    ESP_LOGI(TAG, "TAS5713 found");

    /* do the init sequence */
    esp_err_t res = adac_write_byte(TAS5713, TAS5713_OSC_TRIM, 0x00); /* a delay is required after this */
    vTaskDelay(50 / portTICK_PERIOD_MS); 
    res |= adac_write_byte(TAS5713, TAS5713_SERIAL_DATA_INTERFACE, 0x03); /* I2S  LJ 16 bit */
    res |= adac_write_byte(TAS5713, TAS5713_SYSTEM_CTRL2, 0x00); /* exit all channel shutdown */
    res |= adac_write_byte(TAS5713, TAS5713_SOFT_MUTE, 0x00);    /* unmute */
    res |= adac_write_byte(TAS5713, TAS5713_VOL_MASTER, 0x20);
    res |= adac_write_byte(TAS5713, TAS5713_VOL_CH1, 0x30);
    res |= adac_write_byte(TAS5713, TAS5713_VOL_CH2, 0x30);
    res |= adac_write_byte(TAS5713, TAS5713_VOL_HEADPHONE, 0xFF);
    
    /* The tas5713 typically has the mclk connected to the sclk. In this
       configuration, mclk must be a multiple of the sclk. The lowest workable
       multiple is 64x. To achieve this,  32 bits per channel on must be sent
       over I2S. Reconfigure the I2S for that here, and expand the I2S stream
       when it is sent */
    i2s_config->bits_per_sample = 32;

    if (res != ESP_OK) {
        ESP_LOGE(TAG, "could not intialize TAS5713 %d", res);
        return false;
    }
	
	return true;
}

/****************************************************************************************
 * change volume
 */
static bool volume(unsigned left, unsigned right) { 
	return false; 
}
