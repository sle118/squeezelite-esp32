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

#include "adac.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "squeezelite.h"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(*array))
#define TAS5713 0x36 /* i2c address of TAS5713 */

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

static bool init(int i2c_port_num, int i2s_num, i2s_config_t* config);
static void deinit(void);
static void speaker(bool active);
static void headset(bool active);
static void volume(unsigned left, unsigned right);
static void power(adac_power_e mode);

struct adac_s dac_tas5713 = {init, deinit, power, speaker, headset, volume};

struct tas5713_cmd_s {
    u8_t reg;
    u8_t value;
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

static log_level loglevel = lINFO;
static int       i2c_port;

static void tas5713_set(u8_t reg, u8_t val);
static u8_t tas5713_get(u8_t reg);

/****************************************************************************************
 * init
 */
static bool init(int i2c_port_num, int i2s_num, i2s_config_t* i2s_config) {
    i2c_port      = i2c_port_num;
    esp_err_t res = 0;
    // configure i2c
    i2c_config_t i2c_config = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = 27,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_io_num       = 26,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };

    i2c_param_config(i2c_port, &i2c_config);
    i2c_driver_install(i2c_port, I2C_MODE_MASTER, false, false, false);

    /* find if there is a tas5713 attached. Reg 0 should read non-zero if so */
    if (!tas5713_get(0x00)) {
        LOG_WARN("No TAS5713 detected");
        i2c_driver_delete(i2c_port);
        return 0;
    }

    LOG_INFO("TAS5713 DAC using I2C sda:%u, scl:%u",
             i2c_config.sda_io_num,
             i2c_config.scl_io_num);

    /* do the init sequence */
    tas5713_set(TAS5713_OSC_TRIM, 0x00); /* a delay is required after this */
    usleep(50000);
    tas5713_set(TAS5713_SERIAL_DATA_INTERFACE, 0x03); /* I2S  LJ 16 bit */
    tas5713_set(TAS5713_SYSTEM_CTRL2, 0x00); /* exit all channel shutdown */
    tas5713_set(TAS5713_SOFT_MUTE, 0x00);    /* unmute */
    tas5713_set(TAS5713_VOL_MASTER, 0x20);
    tas5713_set(TAS5713_VOL_CH1, 0x30);
    tas5713_set(TAS5713_VOL_CH2, 0x30);
    tas5713_set(TAS5713_VOL_HEADPHONE, 0xFF);
    // configure I2S pins & install driver
    i2s_pin_config_t i2s_pin_config = (i2s_pin_config_t){
        .bck_io_num   = CONFIG_I2S_BCK_IO,
        .ws_io_num    = CONFIG_I2S_WS_IO,
        .data_out_num = CONFIG_I2S_DO_IO,
        .data_in_num  = CONFIG_I2S_DI_IO,
    };

    /* The tas5713 typically has the mclk connected to the sclk. In this
       configuration, mclk must be a multiple of the sclk. The lowest workable
       multiple is 64x. To achieve this,  32 bits per channel on must be sent
       over I2S. Reconfigure the I2S for that here, and expand the I2S stream
       when it is sent */
    i2s_config->bits_per_sample = 32;

    res |= i2s_driver_install(i2s_num, i2s_config, 0, NULL);
    res |= i2s_set_pin(i2s_num, &i2s_pin_config);
    LOG_INFO("DAC using I2S bck:%d, ws:%d, do:%d",
             i2s_pin_config.bck_io_num,
             i2s_pin_config.ws_io_num,
             i2s_pin_config.data_out_num);

    if (res == ESP_OK) {
        return true;
    } else {
        LOG_ERROR("could not intialize TAS5713 %d", res);
        return false;
    }
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
    LOG_INFO("TAS5713 volume (L:%u R:%u) - not implemented", left, right);
    /*
    tas5713_set(TAS5713_VOL_CH1, ~left >> 8);
    tas5713_set(TAS5713_VOL_CH2, ~right >> 8);
    tas5713_set(TAS5713_VOL_MASTER, ~right >> 8);
    */
}

/****************************************************************************************
 * power
 */
static void power(adac_power_e mode) {
    LOG_INFO("TAS5713 power not implemented!");
    return;
}

/****************************************************************************************
 * speaker
 */
static void speaker(bool active) {
    LOG_INFO("TAS5713 power not implemented!");
}

/****************************************************************************************
 * headset
 */
static void headset(bool active) {}

/****************************************************************************************
 * DAC specific commands
 */
void tas5713_set(u8_t reg, u8_t val) {
    esp_err_t ret = ESP_OK;

    LOG_INFO("TAS5713 send  %x %x", reg, val);
    i2c_cmd_handle_t i2c_cmd = i2c_cmd_link_create();

    i2c_master_start(i2c_cmd);
    i2c_master_write_byte(i2c_cmd,
                          TAS5713 | I2C_MASTER_WRITE,
                          I2C_MASTER_NACK);
    i2c_master_write_byte(i2c_cmd, reg, I2C_MASTER_NACK);
    i2c_master_write_byte(i2c_cmd, val, I2C_MASTER_NACK);
    i2c_master_stop(i2c_cmd);
    ret = i2c_master_cmd_begin(i2c_port, i2c_cmd, 50 / portTICK_RATE_MS);

    i2c_cmd_link_delete(i2c_cmd);

    if (ret != ESP_OK) { LOG_ERROR("Could not send command to TAS5713 %d", ret); }
}

/*************************************************************************
 * Read from i2c for the tas5713. This doubles as tas5713 detect. This function
 * returns zero on error, so read register 0x00 for tas detect, which will be
 * non-zero in this application.
 */
static u8_t tas5713_get(u8_t reg) {
    int              ret;
    u8_t             data = 0;
    i2c_cmd_handle_t i2c_cmd = i2c_cmd_link_create();

    i2c_master_start(i2c_cmd);
    i2c_master_write_byte(i2c_cmd, TAS5713 | I2C_MASTER_WRITE, I2C_MASTER_NACK);
    i2c_master_write_byte(i2c_cmd, reg, I2C_MASTER_NACK);

    i2c_master_start(i2c_cmd);
    i2c_master_write_byte(i2c_cmd, TAS5713 | I2C_MASTER_READ, I2C_MASTER_NACK);
    i2c_master_read_byte(i2c_cmd, &data, I2C_MASTER_NACK);

    i2c_master_stop(i2c_cmd);
    ret = i2c_master_cmd_begin(i2c_port, i2c_cmd, 50 / portTICK_RATE_MS);
    i2c_cmd_link_delete(i2c_cmd);

    if (ret == ESP_OK) {
        LOG_INFO("TAS5713 reg 0x%x is 0x%x", reg, data);
    }
    return data;
}
