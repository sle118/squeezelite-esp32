/* cmd_i2ctools.c

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "cmd_i2ctools.h"
#include "accessors.h"
#include "adac.h"
#include "argtable3/argtable3.h"
#include "config.h"
#include "display.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "messaging.h"
// #include "Config.h"
#pragma message("fixme: look for TODO below")
#include "platform_console.h"
#include "stdio.h"
#include "string.h"
#include "tools.h"
#include "trace.h"
#include <stdio.h>

#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define WRITE_BIT I2C_MASTER_WRITE  /*!< I2C master write */
#define READ_BIT I2C_MASTER_READ    /*!< I2C master read */
#define ACK_CHECK_EN 0x1            /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0           /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                 /*!< I2C ack value */
#define NACK_VAL 0x1                /*!< I2C nack value */
extern int spi_system_host;
extern int spi_system_dc_gpio;
extern int is_output_gpio(struct arg_int* gpio, FILE* f, int* gpio_out, bool mandatory);
static const char* TAG = "cmd_i2ctools";
const char* desc_spiconfig = "SPI Bus Parameters";
const char* desc_i2c = "I2C Bus Parameters";
const char* desc_display = "Display";


static struct {
    struct arg_int* port;
    struct arg_int* chip_address;
    struct arg_int* register_address;
    struct arg_int* data_length;
    struct arg_end* end;
} i2cget_args;
static struct {
    struct arg_int* port;
    struct arg_end* end;
} i2cdetect_args;
static struct {
    struct arg_int* chip_address;
    struct arg_int* port;
    struct arg_int* register_address;
    struct arg_int* data;
    struct arg_end* end;
} i2cset_args;

#if CONFIG_WITH_CONFIG_UI
static struct {
    struct arg_int* chip_address;
    struct arg_int* size;
    struct arg_end* end;
} i2cdump_args;
#endif    

static struct {
    struct arg_int* port;
    struct arg_int* freq;
    struct arg_int* sda;
    struct arg_int* scl;
    struct arg_lit* clear;
    struct arg_end* end;
} i2cconfig_args;
#if CONFIG_WITH_CONFIG_UI
static struct {
    struct arg_int* data;
    struct arg_int* miso;
    struct arg_int* clk;
    struct arg_int* dc;
    struct arg_int* host;
    struct arg_lit* clear;
    struct arg_end* end;
} spiconfig_args;
static struct {
    struct arg_str* type;
    struct arg_str* driver;
    struct arg_int* depth;
    struct arg_int* address;
    struct arg_int* width;
    struct arg_int* height;
    struct arg_lit* rotate;
    struct arg_lit* hflip;
    struct arg_lit* vflip;
    struct arg_int* speed;
    struct arg_int* cs;
    struct arg_int* back;
    struct arg_int* reset;
    struct arg_lit* clear;
    struct arg_lit* invert;
    struct arg_int* mode;
    struct arg_end* end;
} i2cdisp_args;
#endif

bool is_i2c_started(i2c_port_t port) {
    esp_err_t ret = ESP_OK;
    ESP_LOGD(TAG, "Determining if i2c is started on port %u", port);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ret = i2c_master_start(cmd);
    if (ret == ESP_OK) {
        ret = i2c_master_write_byte(cmd, WRITE_BIT, ACK_CHECK_EN);
    }
    if (ret == ESP_OK) {
        ret = i2c_master_stop(cmd);
    }
    if (ret == ESP_OK) {
        ret = i2c_master_cmd_begin(port, cmd, 50 / portTICK_PERIOD_MS);
    }
    i2c_cmd_link_delete(cmd);
    ESP_LOGD(TAG, "i2c is %s. %s", ret != ESP_ERR_INVALID_STATE ? "started" : "not started",
        esp_err_to_name(ret));
    return (ret != ESP_ERR_INVALID_STATE);
}

typedef struct {
    uint8_t address;
    const char* description;
} i2c_db_t;

#if CONFIG_WITH_CONFIG_UI
// the list was taken from https://i2cdevices.org/addresses
// on 2020-01-16

static const i2c_db_t i2c_db[] = {{.address = 0x00, .description = "Unknown"},
    {.address = 0x01, .description = "Unknown"}, {.address = 0x02, .description = "Unknown"},
    {.address = 0x03, .description = "Unknown"}, {.address = 0x04, .description = "Unknown"},
    {.address = 0x05, .description = "Unknown"}, {.address = 0x06, .description = "Unknown"},
    {.address = 0x07, .description = "Unknown"}, {.address = 0x08, .description = "Unknown"},
    {.address = 0x0c, .description = "AK8975"}, {.address = 0x0d, .description = "AK8975"},
    {.address = 0x0e, .description = "MAG3110 AK8975 IST-8310"},
    {.address = 0x0f, .description = "AK8975"},
    {.address = 0x10, .description = "VEML7700 VML6075 VEML6075 ES8388"},
    {.address = 0x11,
        .description = "Si4713 SAA5246 SAA5243P/K SAA5243P/L SAA5243P/E SAA5243P/H  ES8388"},
    {.address = 0x12, .description = "SEN-17374"},
    {.address = 0x13, .description = "VCNL40x0 SEN-17374"},
    {.address = 0x18, .description = "MCP9808 LIS3DH LSM303 COM-15093"},
    {.address = 0x19, .description = "MCP9808 LIS3DH LSM303 COM-15093"},
    {.address = 0x1a, .description = "AC101 MCP9808"}, {.address = 0x1b, .description = "MCP9808"},
    {.address = 0x1c, .description = "MCP9808 MMA845x FXOS8700"},
    {.address = 0x1d, .description = "MCP9808 MMA845x ADXL345 FXOS8700"},
    {.address = 0x1e, .description = "HMC5883 LSM303 MCP9808 LSM303 FXOS8700"},
    {.address = 0x1f, .description = "MCP9808 FXOS8700"},
    {.address = 0x20, .description = "TCA9554 MCP23008 MA12070P MCP23017 Chirp! FXAS21002"},
    {.address = 0x21, .description = "FXAS21002 MCP23008 MCP23017 SAA4700 MA12070P TCA9554"},
    {.address = 0x22, .description = "ES8388 MCP23008 MCP23017 PCA1070 MA12070P TCA9554"},
    {.address = 0x23, .description = "MCP23008 MCP23017 SAA4700 MA12070P TCA9554"},
    {.address = 0x24, .description = "TCA9554 MCP23008 PCD3312C MCP23017 PCD3311C"},
    {.address = 0x25, .description = "TCA9554 MCP23008 PCD3312C MCP23017 PCD3311C"},
    {.address = 0x26, .description = "MCP23008 MCP23017 TCA9554"},
    {.address = 0x27, .description = "MCP23008 MCP23017 HIH6130 TCA9554"},
    {.address = 0x28, .description = "BNO055 CAP1188"},
    {.address = 0x29, .description = "BNO055 VL53L0x VL6180X CAP1188 TCS34725 TSL2591"},
    {.address = 0x2a, .description = "CAP1188"}, {.address = 0x2b, .description = "CAP1188"},
    {.address = 0x2c, .description = "CAP1188 AD5248 AD5251 AD5252 CAT5171"},
    {.address = 0x2d, .description = "CAP1188 AD5248 AD5251 AD5252 CAT5171"},
    {.address = 0x2e, .description = "AD5248 AD5251 AD5252 LPS22HB"},
    {.address = 0x2f, .description = "AD5248 AD5243 AD5251 AD5252"},
    {.address = 0x30, .description = "SAA2502"}, {.address = 0x31, .description = "SAA2502"},
    {.address = 0x33, .description = "MLX90640"},
    {.address = 0x38, .description = "FT6x06 VEML6070 BMA150 SAA1064 SEN-15892 PCF8574AP"},
    {.address = 0x39, .description = "TSL2561 APDS-9960 VEML6070 SAA1064 PCF8574AP"},
    {.address = 0x3a, .description = "PCF8577C SAA1064 PCF8574AP"},
    {.address = 0x3b, .description = "SAA1064 PCF8569 PCF8574AP"},
    {.address = 0x3c, .description = "SSD1305 SSD1306 PCF8578 PCF8569 SH1106 PCF8574AP"},
    {.address = 0x3d, .description = "SSD1305 SSD1306 PCF8578 SH1106 PCF8574AP"},
    {.address = 0x3e, .description = "PCF8574AP"}, {.address = 0x3f, .description = "PCF8574AP"},
    {.address = 0x40,
        .description = "Si7021 HTU21D-F TMP007 TMP006 PCA9685 INA219 TEA6330 TEA6300 TDA9860 "
                       "TEA6320 TDA8421 NE5751 INA260 PCF8574"},
    {.address = 0x41,
        .description = "TMP007 TDA8421 TDA8424 STMPE610 PCF8574 STMPE811 NE5751 INA260 TDA8425 "
                       "TMP006 TDA9860 PCA9685 INA219 TDA8426"},
    {.address = 0x42,
        .description = "TMP007 TDA8417 HDC1008 PCF8574 INA260 TDA8415 TMP006 PCA9685 INA219"},
    {.address = 0x43, .description = "TMP007 HDC1008 PCF8574 INA260 TMP006 PCA9685 INA219"},
    {.address = 0x44,
        .description = "TMP007 TMP006 PCA9685 INA219 STMPE610 SHT31 ISL29125 STMPE811 TDA4688 "
                       "TDA4672 TDA4780 TDA4670 TDA8442 TDA4687 TDA4671 TDA4680 INA260 PCF8574"},
    {.address = 0x45,
        .description = "TMP007 TDA7433 PCF8574 TDA8376 INA260 TMP006 PCA9685 INA219 SHT31"},
    {.address = 0x46, .description = "TMP007 PCF8574 TDA8370 INA260 TMP006 PCA9685 INA219 TDA9150"},
    {.address = 0x47, .description = "TMP007 PCF8574 INA260 TMP006 PCA9685 INA219"},
    {.address = 0x48, .description = "PCA9685 INA219 PN532 TMP102 INA260 ADS1115 PCF8574 ADS7828"},
    {.address = 0x49,
        .description = "TSL2561 PCA9685 INA219 TMP102 INA260 ADS1115 AS7262 PCF8574 ADS7828"},
    {.address = 0x4a,
        .description = "ADS7828 PCF8574 ADS1115 INA260 PCA9685 MAX44009 INA219 TMP102"},
    {.address = 0x4b,
        .description = "ADS7828 PCF8574 ADS1115 INA260 PCA9685 MAX44009 INA219 TMP102"},
    {.address = 0x4c, .description = "PCA9685 INA219 INA260 PCF8574"},
    {.address = 0x4d, .description = "PCA9685 INA219 INA260 PCF8574"},
    {.address = 0x4e, .description = "PCA9685 INA219 INA260 PCF8574"},
    {.address = 0x4f, .description = "PCA9685 INA219 INA260 PCF8574"},
    {.address = 0x50, .description = "PCA9685 MB85RC"},
    {.address = 0x51, .description = "PCA9685 MB85RC VCNL4200"},
    {.address = 0x52, .description = "PCA9685 MB85RC Nunchuck controller APDS-9250 SI1133"},
    {.address = 0x53, .description = "ADXL345 PCA9685 MB85RC"},
    {.address = 0x54, .description = "PCA9685 MB85RC"},
    {.address = 0x55, .description = "PCA9685 MB85RC MAX30101 SI1133"},
    {.address = 0x56, .description = "PCA9685 MB85RC"},
    {.address = 0x57, .description = "PCA9685 MB85RC MAX3010x"},
    {.address = 0x58, .description = "PCA9685 TPA2016 SGP30"},
    {.address = 0x59, .description = "PCA9685"},
    {.address = 0x5a, .description = "MPR121 MLX90614 CCS811 PCA9685 DRV2605"},
    {.address = 0x5b, .description = "PCA9685 CCS811 MPR121"},
    {.address = 0x5c, .description = "PCA9685 AM2315 MPR121"},
    {.address = 0x5d, .description = "PCA9685 MPR121"}, {.address = 0x5e, .description = "PCA9685"},
    {.address = 0x5f, .description = "PCA9685 HTS221"},
    {.address = 0x60,
        .description = "SI1132 Si5351A ATECC608A TSA5511 ATECC508A SAB3035 MCP4725A0 SAB3037 "
                       "PCA9685 MCP4725A1 TEA5767 MPL3115A2 MPL115A2 Si1145"},
    {.address = 0x61,
        .description = "Si5351A TSA5511 SAB3035 MCP4725A0 SAB3037 TEA6100 PCA9685 MCP4725A1"},
    {.address = 0x62,
        .description = "SCD40-D-R2 TSA5511 SAB3035 UMA1014T SAB3037 PCA9685 MCP4725A1"},
    {.address = 0x63, .description = "Si4713 TSA5511 SAB3035 UMA1014T SAB3037 PCA9685 MCP4725A1"},
    {.address = 0x64, .description = "PCA9685 MCP4725A2 MCP4725A1"},
    {.address = 0x65, .description = "PCA9685 MCP4725A2 MCP4725A1"},
    {.address = 0x66, .description = "PCA9685 MCP4725A3 IS31FL3731 MCP4725A1"},
    {.address = 0x67, .description = "PCA9685 MCP4725A3 MCP4725A1"},
    {.address = 0x68,
        .description =
            "MPU-9250 ICM-20948 MPU6050 AMG8833 DS3231 PCA9685 PCF8573 PCF8523 DS1307 ITG3200"},
    {.address = 0x69,
        .description = "MPU-9250 ICM-20948 MPU6050 AMG8833 PCA9685 PCF8573 ITG3200 SPS30"},
    {.address = 0x6a, .description = "PCA9685 L3GD20H PCF8573"},
    {.address = 0x6b, .description = "PCA9685 L3GD20H PCF8573"},
    {.address = 0x6c, .description = "PCA9685"}, {.address = 0x6d, .description = "PCA9685"},
    {.address = 0x6e, .description = "PCA9685"},
    {.address = 0x6f, .description = "PCA9685 MCP7940N"},
    {.address = 0x70, .description = "PCA9685 TCA9548 HT16K33 SHTC3"},
    {.address = 0x71, .description = "PCA9685 TCA9548 HT16K33"},
    {.address = 0x72, .description = "PCA9685 TCA9548 HT16K33"},
    {.address = 0x73, .description = "PCA9685 TCA9548 HT16K33"},
    {.address = 0x74, .description = "PCA9685 TCA9548 HT16K33"},
    {.address = 0x75, .description = "PCA9685 TCA9548 HT16K33"},
    {.address = 0x76,
        .description = "BME688 BME680 MS5611 MS5607 HT16K33 PCA9685 BME280 BMP280 TCA9548"},
    {.address = 0x77,
        .description = "PCA9685 TCA9548 HT16K33 IS31FL3731 BME280 BMP280 MS5607 BMP180 BMP085 "
                       "BMA180 MS5611 BME680 BME688"},
    {.address = 0x78, .description = "PCA9685"}, {.address = 0x79, .description = "PCA9685"},
    {.address = 0x7a, .description = "PCA9685"}, {.address = 0x7b, .description = "PCA9685"},
    {.address = 0x7c, .description = "PCA9685"}, {.address = 0x7d, .description = "PCA9685"},
    {.address = 0x7e, .description = "PCA9685"}, {.address = 0x7f, .description = "PCA9685"},
    {.address = 0, .description = NULL}};

const char* i2c_get_description(uint8_t address) {
    uint8_t i = 0;
    while (i2c_db[i].description && i2c_db[i].address != address)
        i++;
    return i2c_db[i].description ? i2c_db[i].description : "Unlisted";
}
#endif
static esp_err_t i2c_get_port(int port, i2c_port_t* i2c_port) {
    if (port >= I2C_NUM_MAX) {
        log_send_messaging(MESSAGING_ERROR, "Wrong port number: %d", port);
        return ESP_FAIL;
    }
    switch (port) {
    case 0:
        *i2c_port = I2C_NUM_0;
        break;
    case 1:
        *i2c_port = I2C_NUM_1;
        break;
    default:
        *i2c_port = I2C_NUM_0;
        break;
    }
    return ESP_OK;
}
// static esp_err_t i2c_master_driver_install(const char* cmdname) {
//     esp_err_t err = ESP_OK;
//     cmd_send_messaging(cmdname, MESSAGING_INFO, "Installing i2c driver on port %u\n", i2c_port);
//     if ((err = i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE,
//              I2C_MASTER_TX_BUF_DISABLE, 0)) != ESP_OK) {
//         cmd_send_messaging(
//             cmdname, MESSAGING_ERROR, "Driver install failed! %s\n", esp_err_to_name(err));
//     }
//     return err;
// }

// static esp_err_t i2c_master_driver_initialize(const char* cmdname, i2c_config_t* conf) {
//     esp_err_t err = ESP_OK;
//     cmd_send_messaging(cmdname, MESSAGING_INFO,
//         "Initializing i2c driver configuration.\n   mode = I2C_MODE_MASTER, \n   scl_pullup_en = "
//         "GPIO_PULLUP_ENABLE, \n   i2c port = %u, \n   sda_io_num = %u, \n   sda_pullup_en = "
//         "GPIO_PULLUP_ENABLE, \n   scl_io_num = %u, \n   scl_pullup_en = GPIO_PULLUP_ENABLE, \n   "
//         "master.clk_speed = %u\n",
//         i2c_port, conf->sda_io_num, conf->scl_io_num, conf->master.clk_speed);
//     if ((err = i2c_param_config(i2c_port, conf)) != ESP_OK) {
//         cmd_send_messaging(
//             cmdname, MESSAGING_ERROR, "i2c driver config load failed. %s\n", esp_err_to_name(err));
//     }
//     return err;
// }

#if CONFIG_WITH_CONFIG_UI
static int do_i2c_set_display(int argc, char** argv) {
    bool bHasi2cConfig = false, bHasSpiConfig = false;
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&i2cdisp_args);
    display_config_t config = {.back = -1, .CS_pin = -1, .RST_pin = -1, .depth = 0};
    char* nvs_item = config_alloc_get(NVS_TYPE_STR, "i2c_config");
    if (nvs_item && strlen(nvs_item) > 0) {
        bHasi2cConfig = true;
    }
    FREE_AND_NULL(nvs_item);

    nvs_item = config_alloc_get(NVS_TYPE_STR, "spi_config");
    if (nvs_item && strlen(nvs_item) > 0) {
        bHasSpiConfig = true;
    }
    FREE_AND_NULL(nvs_item);

    /* Check "--clear" option */
    if (i2cdisp_args.clear->count) {
        cmd_send_messaging(argv[0], MESSAGING_WARNING, "Display config cleared");
        config_set_value(NVS_TYPE_STR, "display_config", "");
        return 0;
    }
    char* buf = NULL;
    size_t buf_size = 0;
    FILE* f = open_memstream(&buf, &buf_size);
    if (f == NULL) {
        cmd_send_messaging(argv[0], MESSAGING_ERROR, "Unable to open memory stream.");
        return 1;
    }
    if (nerrors > 0) {
        arg_print_errors(f, i2cdisp_args.end, desc_display);
        fclose(f);
        return 1;
    }
    /* Check "--type" option */
    if (i2cdisp_args.type->count) {
        if (strcasecmp(i2c_name_type, i2cdisp_args.type->sval[0]) == 0) {
            config.type = i2c_name_type;
        } else {
            config.type = spi_name_type;
        }
    } else {
        config.type = i2c_name_type;
    }
    /* Check "--address" option */
    if (strcasecmp(config.type, "I2C") == 0) {
        if (i2cdisp_args.address->count > 0) {
            config.address = i2cdisp_args.address->ival[0];
        } else {
            config.address = 60;
        }
        if (!bHasi2cConfig) {
            fprintf(f, "I2C bus has to be configured first. \n");
            nerrors++;
        }
    } else {
        // SPI Bus connection
        if (!bHasSpiConfig) {
            fprintf(f, "SPI bus has to be configured first. \n");
            nerrors++;
        }
        /* Check "--speed" option */
        if (i2cdisp_args.speed->count) {
            config.speed = i2cdisp_args.speed->ival[0];
        } else {
            config.speed = 8000000;
        }
        /* Check "--cs" option */
        nerrors += is_output_gpio(i2cdisp_args.cs, f, &config.CS_pin, false);
        /* Check "--mode" option */
        if (i2cdisp_args.mode->count) {
            config.mode = i2cdisp_args.mode->ival[0];
        } else {
            config.mode = 0;
        }
    }

    nerrors += is_output_gpio(i2cdisp_args.reset, f, &config.RST_pin, false);
    /* Check "--width" option */
    if (i2cdisp_args.width->count) {
        config.width = i2cdisp_args.width->ival[0];
    }
    /* Check "--height" option */
    if (i2cdisp_args.height->count) {
        config.height = i2cdisp_args.height->ival[0];
    }

    /* Check "--driver" option */
    if (i2cdisp_args.driver->count) {
        config.drivername = display_conf_get_driver_name(i2cdisp_args.driver->sval[0]);
    }
    if (i2cdisp_args.depth->count > 0 && strcasecmp(config.drivername, "SSD1326") == 0) {
        config.depth = i2cdisp_args.depth->ival[0];
    }
    /* Check "--back" option */
    nerrors += is_output_gpio(i2cdisp_args.back, f, &config.back, false);
    if (!config.drivername || !display_is_valid_driver(config.drivername)) {
        fprintf(f, "Unsupported display driver %s\n", config.drivername);
        nerrors++;
    }
    if (!strcasestr(config.type, "I2C") && !strcasestr(config.type, "SPI")) {
        fprintf(f, "Invalid display type %s\n", config.type);
        nerrors++;
    }
    config.rotate = i2cdisp_args.rotate->count > 0;
    config.hflip = i2cdisp_args.hflip->count > 0;
    config.vflip = i2cdisp_args.vflip->count > 0;
    config.invert = i2cdisp_args.invert->count > 0;

    if (nerrors == 0) {
        fprintf(f, "Saving display configuration\n");
        esp_err_t err = config_display_set(&config);
        if (err != ESP_OK) {
            fprintf(f, "Error: %s\n", esp_err_to_name(err));
            nerrors++;
        }
    }
    if (!nerrors) {
        fprintf(f, "Done.\n");
    }
    fflush(f);
    cmd_send_messaging(argv[0], nerrors > 0 ? MESSAGING_ERROR : MESSAGING_INFO, "%s", buf);
    fclose(f);
    FREE_AND_NULL(buf);
    return nerrors;
}

static int do_spiconfig_cmd(int argc, char** argv) {
    static spi_bus_config_t spi_config = {.mosi_io_num = -1,
        .sclk_io_num = -1,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1};
    int dc = -1, host = 0;
    esp_err_t err = ESP_OK;
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&spiconfig_args);

    /* Check "--clear" option */
    if (spiconfig_args.clear->count) {
        cmd_send_messaging(argv[0], MESSAGING_WARNING, "SPI config cleared.\n");
        config_set_value(NVS_TYPE_STR, "spi_config", "");
        return 0;
    }

    char* buf = NULL;
    size_t buf_size = 0;
    FILE* f = open_memstream(&buf, &buf_size);
    if (f == NULL) {
        cmd_send_messaging(argv[0], MESSAGING_ERROR, "Unable to open memory stream.\n");
        return 1;
    }
    if (nerrors > 0) {
        arg_print_errors(f, spiconfig_args.end, desc_spiconfig);
        fclose(f);
        return 1;
    }
    /* Check "--clk" option */
    nerrors += is_output_gpio(spiconfig_args.clk, f, &spi_config.sclk_io_num, true);
    nerrors += is_output_gpio(spiconfig_args.data, f, &spi_config.mosi_io_num, true);
    nerrors += is_output_gpio(spiconfig_args.miso, f, &spi_config.miso_io_num, true);
    nerrors += is_output_gpio(spiconfig_args.dc, f, &dc, true);
    nerrors += is_output_gpio(spiconfig_args.host, f, &host, true);

    if (!nerrors) {
        fprintf(f, "Configuring SPI data=%d clock=%d host=%u dc: %d\n", spi_config.mosi_io_num,
            spi_config.sclk_io_num, host, dc);
        err = spi_bus_initialize(host, &spi_config, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            if (err == ESP_ERR_INVALID_STATE) {
                // if user is changing the host number, we need to try freeing both hosts
                if ((err = spi_bus_free(host)) != ESP_OK &&
                    (err = spi_bus_free(host == 1 ? 2 : 1)) != ESP_OK) {
                    fprintf(f,
                        "SPI bus init failed. Please clear SPI configuration, restart the device "
                        "and try again. %s\n",
                        esp_err_to_name(err));
                    nerrors++;
                } else if ((err = spi_bus_initialize(host, &spi_config, SPI_DMA_CH_AUTO)) !=
                           ESP_OK) {
                    fprintf(f, "Failed to initialize SPI Bus. %s\n", esp_err_to_name(err));
                    nerrors++;
                }
            } else {
                fprintf(f, "SPI bus initialization failed. %s\n", esp_err_to_name(err));
                nerrors++;
            }
        }
    }

    if (!nerrors) {
        fprintf(f, "Storing SPI parameters.\n");
        nerrors += (config_spi_set(&spi_config, host, dc) != ESP_OK);
    }
    if (!nerrors) {
        fprintf(f, "Done.\n");
    }
    fflush(f);
    cmd_send_messaging(argv[0], nerrors > 0 ? MESSAGING_ERROR : MESSAGING_INFO, "%s", buf);
    fclose(f);
    FREE_AND_NULL(buf);
    return nerrors;
}
#endif

static int do_i2cconfig_cmd(int argc, char** argv) {
    esp_err_t err = ESP_OK;
    // i2c_config_t conf = {.mode = I2C_MODE_MASTER,
        // .sda_io_num = 19,
        // .sda_pullup_en = GPIO_PULLUP_ENABLE,
        // .scl_io_num = 18,
        // .scl_pullup_en = GPIO_PULLUP_ENABLE,
        // .master.clk_speed = 100000};

    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&i2cconfig_args);
    /* Check "--clear" option */
    // if (i2cconfig_args.clear->count) {
    //     cmd_send_messaging(argv[0], MESSAGING_WARNING, "i2c config cleared\n");
    //     config_set_value(NVS_TYPE_STR, "i2c_config", "");
    //     return 0;
    // }
    // TODO: Add support for the commented code

//     char* buf = NULL;
//     size_t buf_size = 0;
//     FILE* f = open_memstream(&buf, &buf_size);
//     if (f == NULL) {
//         cmd_send_messaging(argv[0], MESSAGING_ERROR, "Unable to open memory stream.\n");
//         return 1;
//     }
//     if (nerrors > 0) {
//         arg_print_errors(f, i2cconfig_args.end, desc_i2c);
//         fclose(f);
//         return 1;
//     }
//     /* Check "--port" option */
//     if (i2cconfig_args.port->count) {
//         if (i2c_get_port(i2cconfig_args.port->ival[0], &i2c_port) != ESP_OK) {
//             fprintf(f, "Invalid port %u \n", i2cconfig_args.port->ival[0]);
//             nerrors++;
//         }
//     }
//     /* Check "--freq" option */
//     if (i2cconfig_args.freq->count) {
//         conf.master.clk_speed = i2cconfig_args.freq->ival[0];
//     }

//     nerrors += is_output_gpio(i2cconfig_args.sda, f, &conf.sda_io_num, true);
//     nerrors += is_output_gpio(i2cconfig_args.scl, f, &conf.scl_io_num, true);
    
// #ifdef CONFIG_I2C_LOCKED
//     if (i2c_port == I2C_NUM_0) {
//         i2c_port = I2C_NUM_1;
//         fprintf(f, "can't use i2c port 0 when locked by config. Changing to port 1.\n");
//     }
// #endif

//     if (!nerrors) {
//         fprintf(f, "Uninstalling i2c driver from port %u if needed\n", i2c_port);
//         if (is_i2c_started(i2c_port)) {
//             if ((err = i2c_driver_delete(i2c_port)) != ESP_OK) {
//                 fprintf(f, "i2c driver delete failed. %s\n", esp_err_to_name(err));
//                 nerrors++;
//             }
//         }
//     }
//     if (!nerrors) {
//         if ((err = i2c_master_driver_initialize(argv[0], &conf)) == ESP_OK) {
//             if ((err = i2c_master_driver_install(argv[0])) != ESP_OK) {
//                 nerrors++;
//             } else {
//                 fprintf(f, "i2c driver successfully started.\n");
//             }
//         } else {
//             nerrors++;
//         }
//     }
//     if (!nerrors) {
//         fprintf(f, "Storing i2c parameters.\n");
//         config_i2c_set(&conf, i2c_port);
//     }
//     if (!nerrors) {
//         fprintf(f, "Done.\n");
//     }
//     fflush(f);
//     cmd_send_messaging(argv[0], nerrors > 0 ? MESSAGING_ERROR : MESSAGING_INFO, "%s", buf);
//     fclose(f);
//     FREE_AND_NULL(buf);

    return nerrors;
}

#if CONFIG_WITH_CONFIG_UI
static int do_i2cdump_cmd(int argc, char** argv) {
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&i2cdump_args);
    if (nerrors != 0) {
        return 1;
    }

    /* Check chip address: "-c" option */
    int chip_addr = i2cdump_args.chip_address->ival[0];
    /* Check read size: "-s" option */
    int size = 1;
    if (i2cdump_args.size->count) {
        size = i2cdump_args.size->ival[0];
    }
    i2c_port_t loc_i2c_port = i2c_port;
    if (i2cset_args.port->count &&
        i2c_get_port(i2cset_args.port->ival[0], &loc_i2c_port) != ESP_OK) {
        return 1;
    }

    if (size != 1 && size != 2 && size != 4) {
        cmd_send_messaging(argv[0], MESSAGING_ERROR, "Wrong read size. Only support 1,2,4\n");
        return 1;
    }
    char* buf = NULL;
    size_t buf_size = 0;
    FILE* f = open_memstream(&buf, &buf_size);
    if (f == NULL) {
        cmd_send_messaging(argv[0], MESSAGING_ERROR, "Unable to open memory stream.\n");
        return 1;
    }

    uint8_t data_addr;
    uint8_t data[4];
    int32_t block[16];
    fprintf(f, "\n    00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f"
               "    0123456789abcdef\r\n");
    for (int i = 0; i < 128; i += 16) {
        fprintf(f, "%02x: ", i);
        for (int j = 0; j < 16; j += size) {
            fflush(stdout);
            data_addr = i + j;
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, chip_addr << 1 | WRITE_BIT, ACK_CHECK_EN);
            i2c_master_write_byte(cmd, data_addr, ACK_CHECK_EN);
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, chip_addr << 1 | READ_BIT, ACK_CHECK_EN);
            if (size > 1) {
                i2c_master_read(cmd, data, size - 1, ACK_VAL);
            }
            i2c_master_read_byte(cmd, data + size - 1, NACK_VAL);
            i2c_master_stop(cmd);
            esp_err_t ret = i2c_master_cmd_begin(loc_i2c_port, cmd, 50 / portTICK_PERIOD_MS);
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) {
                for (int k = 0; k < size; k++) {
                    fprintf(f, "%02x ", data[k]);
                    block[j + k] = data[k];
                }
            } else {
                for (int k = 0; k < size; k++) {
                    fprintf(f, "XX ");
                    block[j + k] = -1;
                }
            }
        }
        fprintf(f, "   ");
        for (int k = 0; k < 16; k++) {
            if (block[k] < 0) {
                fprintf(f, "X");
            }
            if ((block[k] & 0xff) == 0x00 || (block[k] & 0xff) == 0xff) {
                fprintf(f, ".");
            } else if ((block[k] & 0xff) < 32 || (block[k] & 0xff) >= 127) {
                fprintf(f, "?");
            } else {
                fprintf(f, "%c", block[k] & 0xff);
            }
        }
        fprintf(f, "\r\n");
    }
    // Don't stop the driver;  our firmware may be using it for screen, etc
    // i2c_driver_delete(i2c_port);
    fflush(f);
    cmd_send_messaging(argv[0], MESSAGING_INFO, "%s", buf);
    fclose(f);
    FREE_AND_NULL(buf);
    return 0;
}

static int do_i2cset_cmd(int argc, char** argv) {
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&i2cset_args);
    if (nerrors != 0) {
        return 1;
    }

    /* Check chip address: "-c" option */
    int chip_addr = i2cset_args.chip_address->ival[0];
    /* Check register address: "-r" option */
    int data_addr = 0;
    if (i2cset_args.register_address->count) {
        data_addr = i2cset_args.register_address->ival[0];
    }

    i2c_port_t loc_i2c_port = i2c_port;
    if (i2cset_args.port->count &&
        i2c_get_port(i2cset_args.port->ival[0], &loc_i2c_port) != ESP_OK) {
        return 1;
    }

    /* Check data: "-d" option */
    int len = i2cset_args.data->count;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, chip_addr << 1 | WRITE_BIT, ACK_CHECK_EN);
    if (i2cset_args.register_address->count) {
        i2c_master_write_byte(cmd, data_addr, ACK_CHECK_EN);
    }
    for (int i = 0; i < len; i++) {
        i2c_master_write_byte(cmd, i2cset_args.data->ival[i], ACK_CHECK_EN);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(loc_i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) {
        cmd_send_messaging(argv[0], MESSAGING_INFO, "i2c Write OK\n");
    } else if (ret == ESP_ERR_TIMEOUT) {
        cmd_send_messaging(argv[0], MESSAGING_WARNING, "i2c Bus is busy\n");
    } else {
        cmd_send_messaging(argv[0], MESSAGING_ERROR, "i2c Read failed\n");
    }
    // Don't stop the driver;  our firmware may be using it for screen, etc
    // i2c_driver_delete(i2c_port);
    return 0;
}
#endif

static int do_i2cget_cmd(int argc, char** argv) {
    int nerrors = arg_parse_msg(argc, argv, (struct arg_hdr**)&i2cget_args);
    if (nerrors != 0) {
        return 1;
    }

    /* Check chip address: "-c" option */
    int chip_addr = i2cget_args.chip_address->ival[0];
    /* Check register address: "-r" option */
    int data_addr = -1;
    if (i2cget_args.register_address->count) {
        data_addr = i2cget_args.register_address->ival[0];
    }
    /* Check data length: "-l" option */
    int len = 1;
    if (i2cget_args.data_length->count) {
        len = i2cget_args.data_length->ival[0];
    }
    i2c_port_t loc_i2c_port = i2cget_args.port->ival[0];
    const sys_i2c_bus*i2c = get_i2c_bus(loc_i2c_port);
    #pragma message("TODO: Fix this!")
    char* buf = NULL;
    size_t buf_size = 0;
    FILE* f = open_memstream(&buf, &buf_size);
    if (f == NULL) {
        cmd_send_messaging(argv[0], MESSAGING_ERROR, "Unable to open memory stream.\n");
        return 1;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    uint8_t* data = malloc_init_external(len);
    if (data_addr != -1) {
        i2c_master_write_byte(cmd, chip_addr << 1 | WRITE_BIT, ACK_CHECK_EN);
        i2c_master_write_byte(cmd, data_addr, ACK_CHECK_EN);
        i2c_master_start(cmd);
    }
    i2c_master_write_byte(cmd, chip_addr << 1 | READ_BIT, ACK_CHECK_EN);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data + len - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(loc_i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) {
        for (int i = 0; i < len; i++) {
            fprintf(f, "0x%02x ", data[i]);
            if ((i + 1) % 16 == 0) {
                fprintf(f, "\r\n");
            }
        }
        if (len % 16) {
            fprintf(f, "\r\n");
        }
    } else if (ret == ESP_ERR_TIMEOUT) {
        cmd_send_messaging(argv[0], MESSAGING_WARNING, "i2c Bus is busy\n");
    } else {
        cmd_send_messaging(argv[0], MESSAGING_ERROR, "i2c Read failed\n");
    }
    free(data);

    // Don't stop the driver;  our firmware may be using it for screen, etc
    // i2c_driver_delete(i2c_port);
    fflush(f);
    cmd_send_messaging(argv[0], MESSAGING_INFO, "%s", buf);
    fclose(f);
    FREE_AND_NULL(buf);
    return 0;
}
esp_err_t cmd_i2ctools_scan_bus(FILE* f, int sda, int scl) {
#ifdef CONFIG_WITH_CONFIG_UI    
    uint8_t matches[128] = {};
#endif    
    int last_match = 0;
    esp_err_t ret = ESP_OK;

    if (!GPIO_IS_VALID_GPIO(scl) || !GPIO_IS_VALID_GPIO(sda)) {
        fprintf(f, "Invalid GPIO. Cannot scan bus\n");
        return 1;
    }

    // configure i2c
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = -1,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = -1,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 250000,
    };

    i2c_config.sda_io_num = sda;
    i2c_config.scl_io_num = scl;
    // we have an I2C configured
    i2c_port_t i2c_port = 0;
    // make sure that we don't have an i2c driver running
    i2c_driver_delete(i2c_port);
    ret = i2c_param_config(i2c_port, &i2c_config);
    if (ret != ESP_OK) {
        fprintf(f, "I2C Param Config failed %s\n", esp_err_to_name(ret));
        return ret;
    }
    ret = i2c_driver_install(i2c_port, I2C_MODE_MASTER, false, false, false);
    if (ret != ESP_OK) {
        fprintf(f, "I2C driver install failed %s\n", esp_err_to_name(ret));
        return ret;
    }

#ifndef CONFIG_WITH_CONFIG_UI
    fprintf(f, "\n     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
#endif
    for (int i = 0; i < 128; i++) {

        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (i << 1) | WRITE_BIT, ACK_CHECK_EN);
        i2c_master_stop(cmd);
        ret = i2c_master_cmd_begin(i2c_port, cmd, 50 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
#ifndef CONFIG_WITH_CONFIG_UI
            fprintf(f, "%02x ", i);
#else            
            matches[++last_match - 1] = i;
#endif            
        }
#ifndef CONFIG_WITH_CONFIG_UI
        else if (ret == ESP_ERR_TIMEOUT) {
            fprintf(f, "UU ");
        } else {
            fprintf(f, "-- ");
        }
#endif
    }
    i2c_driver_delete(i2c_port);

    if (last_match) {
#if CONFIG_WITH_CONFIG_UI
        fprintf(f, "i2c device detected (names provided by https://i2cdevices.org/addresses).\n");
        for (int i = 0; i < last_match; i++) {
            fprintf(f, "%u [%02xh]- %s\n", matches[i], matches[i], i2c_get_description(matches[i]));
        }
#endif
    } else {
        fprintf(f, "No i2c devices found with scl-%d and sda-%d\n", scl, sda);
    }

    return 0;
}

static int do_i2cdetect_cmd(int argc, char** argv) {
#ifdef CONFIG_WITH_CONFIG_UI
    uint8_t matches[128] = {};
    int last_match = 0;
#endif
    esp_err_t ret = ESP_OK;
    i2c_port_t loc_i2c_port = i2cdetect_args.port->ival[0];
    
    // if (i2cset_args.port->count && i2c_get_port(i2cset_args.port->ival[0], &loc_i2c_port) !=
    // ESP_OK) { 	return 1;
    // }

    const sys_i2c_bus*i2c = get_i2c_bus(loc_i2c_port);


    uint8_t address;
    char* buf = NULL;
    size_t buf_size = 0;
    FILE* f = open_memstream(&buf, &buf_size);
    if (f == NULL) {
        cmd_send_messaging(argv[0], MESSAGING_ERROR, "Unable to open memory stream.");
        return 1;
    }

    if (!i2c) {
        fprintf(f, "I2C bus configuration invalid.\r\n");
    } else {

        fprintf(f, "\n     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        for (int i = 0; i < 128; i += 16) {
            fprintf(f, "%02x: ", i);
            for (int j = 0; j < 16; j++) {
                address = i + j;
                i2c_cmd_handle_t cmd = i2c_cmd_link_create();
                i2c_master_start(cmd);
                i2c_master_write_byte(cmd, (address << 1) | WRITE_BIT, ACK_CHECK_EN);
                i2c_master_stop(cmd);
                ret = i2c_master_cmd_begin(loc_i2c_port, cmd, 50 / portTICK_PERIOD_MS);
                i2c_cmd_link_delete(cmd);
                if (ret == ESP_OK) {
                    fprintf(f, "%02x ", address);
#ifdef CONFIG_WITH_CONFIG_UI                    
                    matches[++last_match - 1] = address;
#endif
                } else if (ret == ESP_ERR_TIMEOUT) {
                    fprintf(f, "UU ");
                } else {
                    fprintf(f, "-- ");
                }
            }

            fprintf(f, "\r\n");
        }
#if CONFIG_WITH_CONFIG_UI
        if (last_match) {
            fprintf(f, "\r\n-----------------------------------------------------------------------"
                       "-------------"
                       "\r\nDetected the following devices (names provided by "
                       "https://i2cdevices.org/addresses).");

            for (int i = 0; i < last_match; i++) {
                // printf("%02x = %s\r\n", matches[i], i2c_get_description(matches[i]));
                fprintf(f, "\r\n%u [%02xh]- %s", matches[i], matches[i],
                    i2c_get_description(matches[i]));
            }
            fprintf(f, "\r\n-----------------------------------------------------------------------"
                       "-------------\r\n");
        }
#endif
    }
    fflush(f);
    cmd_send_messaging(argv[0], MESSAGING_INFO, "%s", buf);
    fclose(f);
    FREE_AND_NULL(buf);

    return 0;
}


#if CONFIG_WITH_CONFIG_UI
static void register_i2c_set_display() {
    char* supported_drivers = display_get_supported_drivers();

    i2cdisp_args.width = arg_int1("w", "width", "<n>", "Width");
    i2cdisp_args.height = arg_int1("h", "height", "<n>", "Height");
    i2cdisp_args.address = arg_int0("a", "address", "<n>", "I2C address (default 60)");
    i2cdisp_args.reset = arg_int0(NULL, "reset", "<n>", "Reset GPIO");
    i2cdisp_args.hflip = arg_lit0(NULL, "hf", "Flip horizontally");
    i2cdisp_args.vflip = arg_lit0(NULL, "vf", "Flip vertically");
    i2cdisp_args.driver =
        arg_str0("d", "driver", supported_drivers ? supported_drivers : "<string>", "Driver");
    i2cdisp_args.cs = arg_int0("b", "cs", "<n>", "SPI Only. CS GPIO (for SPI displays)");
    i2cdisp_args.speed = arg_int0("s", "speed", "<n>",
        "SPI Only. Bus Speed (Default 8000000). SPI interface can work up to 26MHz~40MHz");
    i2cdisp_args.back = arg_int0("b", "back", "<n>", "Backlight GPIO (if applicable)");
    i2cdisp_args.depth = arg_int0("p", "depth", "-1|1|4", "Bit Depth (only for SSD1326 displays)");
    i2cdisp_args.type = arg_str0("t", "type", "<I2C|SPI>", "Interface (default I2C)");
    i2cdisp_args.rotate = arg_lit0("r", "rotate", "Rotate 180 degrees");
    i2cdisp_args.invert = arg_lit0("i", "invert", "Invert colors");
    i2cdisp_args.clear = arg_lit0(NULL, "clear", "clear configuration and return");
    i2cdisp_args.mode = arg_int0("m", "mode", "<n>", "SPI Only. Transaction Line Mode (Default 0)");
    i2cdisp_args.end = arg_end(8);
    const esp_console_cmd_t i2c_set_display = {.command = CFG_TYPE_HW("display"),
        .help = desc_display,
        .hint = NULL,
        .func = &do_i2c_set_display,
        .argtable = &i2cdisp_args};
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2c_set_display));
}
#endif
static void register_i2cdectect(void) {
    i2cdetect_args.port = arg_int1(NULL,NULL, "<0|1>", "Specify the i2c port number");
    i2cdetect_args.end = arg_end(1);
    const esp_console_cmd_t i2cdetect_cmd = {.command = "i2cdetect",
        .help = "Scan I2C bus for devices",
        .hint = NULL,
        .func = &do_i2cdetect_cmd,
        .argtable = &i2cdetect_args};
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cdetect_cmd));
}

static void register_i2cget(void) {
    i2cget_args.chip_address =
        arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
    i2cget_args.register_address = arg_int0(
        "r", "register", "<register_addr>", "Specify the address on that chip to read from");
    i2cget_args.data_length =
        arg_int0("l", "length", "<length>", "Specify the length to read from that data address");
    i2cget_args.port = arg_int1(NULL,NULL, "<0|1>", "Specify the i2c port number");
    i2cget_args.end = arg_end(1);
    const esp_console_cmd_t i2cget_cmd = {.command = "i2cget",
        .help = "Read registers visible through the I2C bus",
        .hint = NULL,
        .func = &do_i2cget_cmd,
        .argtable = &i2cget_args};
    cmd_to_json(&i2cget_cmd);
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cget_cmd));
}

#if CONFIG_WITH_CONFIG_UI
static void register_i2cset(void) {
    i2cset_args.chip_address =
        arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
    i2cset_args.register_address = arg_int0(
        "r", "register", "<register_addr>", "Specify the address on that chip to read from");
    i2cset_args.data =
        arg_intn(NULL, NULL, "<data>", 0, 256, "Specify the data to write to that data address");
    i2cset_args.port = arg_intn("p", "port", "<n>", 0, 1, "Specify the i2c port (0|2)");

    i2cset_args.end = arg_end(2);
    const esp_console_cmd_t i2cset_cmd = {.command = "i2cset",
        .help = "Set registers visible through the I2C bus",
        .hint = NULL,
        .func = &do_i2cset_cmd,
        .argtable = &i2cset_args};
    cmd_to_json(&i2cset_cmd);
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cset_cmd));
}

static void register_i2cdump(void) {
    i2cdump_args.chip_address =
        arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
    i2cdump_args.size = arg_int0("s", "size", "<size>", "Specify the size of each read");
    i2cdump_args.end = arg_end(3);
    const esp_console_cmd_t i2cdump_cmd = {.command = "i2cdump",
        .help = "Examine registers visible through the I2C bus",
        .hint = NULL,
        .func = &do_i2cdump_cmd,
        .argtable = &i2cdump_args};
    cmd_to_json(&i2cdump_cmd);
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cdump_cmd));
}
#endif

cJSON* i2config_cb() {
    cJSON* values = cJSON_CreateObject();
    const i2c_config_t* i2c = config_i2c_get(&platform->dev.i2c);
    if (i2c->scl_io_num > 0) {
        cJSON_AddNumberToObject(values, "scl", i2c->scl_io_num);
    }
    if (i2c->sda_io_num > 0) {
        cJSON_AddNumberToObject(values, "sda", i2c->sda_io_num);
    }
    if (i2c->master.clk_speed > 0) {
        cJSON_AddNumberToObject(values, "speed", i2c->master.clk_speed);
    }
    if (platform->dev.i2c.port-sys_i2c_port_PORT0 >= 0) {
        cJSON_AddNumberToObject(values, "port", platform->dev.i2c.port-sys_i2c_port_PORT0);
    }
    return values;
}
cJSON* spiconfig_cb() {
    cJSON* values = cJSON_CreateObject();
    const spi_bus_config_t* spi_config = config_spi_get(NULL);
    if (spi_config->mosi_io_num > 0) {
        cJSON_AddNumberToObject(values, "data", spi_config->mosi_io_num);
    }
    if (spi_config->sclk_io_num > 0) {
        cJSON_AddNumberToObject(values, "clk", spi_config->sclk_io_num);
    }
    if (spi_system_dc_gpio > 0) {
        cJSON_AddNumberToObject(values, "dc", spi_system_dc_gpio);
    }
    if (spi_system_host > 0) {
        cJSON_AddNumberToObject(values, "host", spi_system_host);
    }
    return values;
}
#if CONFIG_WITH_CONFIG_UI
static void register_spiconfig(void) {
    spiconfig_args.clear = arg_lit0(NULL, "clear", "Clear configuration");
    spiconfig_args.clk = arg_int0("k", "clk", "<n>", "Clock GPIO");
    spiconfig_args.data = arg_int0("d", "data", "<n>", "Data OUT GPIO");
    spiconfig_args.miso = arg_int0("d", "miso", "<n>", "Data IN GPIO");
    spiconfig_args.dc = arg_int0("c", "dc", "<n>", "DC GPIO");
    spiconfig_args.host = arg_int0("h", "host", "1|2", "SPI Host Number");
    spiconfig_args.end = arg_end(4);
    const esp_console_cmd_t spiconfig_cmd = {.command = CFG_TYPE_HW("spi"),
        .help = desc_spiconfig,
        .hint = NULL,
        .func = &do_spiconfig_cmd,
        .argtable = &spiconfig_args};
    cmd_to_json_with_cb(&spiconfig_cmd, &spiconfig_cb);
    ESP_ERROR_CHECK(esp_console_cmd_register(&spiconfig_cmd));
}
#endif
static void register_i2cconfig(void) {
    i2cconfig_args.clear = arg_lit0(NULL, "clear", "Clear configuration");
    i2cconfig_args.port = arg_int0("p", "port", "0|1", "Port");
    i2cconfig_args.freq = arg_int0("f", "speed", "int", "Frequency (Hz) e.g. 100000");
    i2cconfig_args.sda = arg_int0("d", "sda", "<n>", "SDA GPIO. e.g. 19");
    i2cconfig_args.scl = arg_int0("c", "scl", "<n>", "SCL GPIO. e.g. 18");
    i2cconfig_args.end = arg_end(4);
    const esp_console_cmd_t i2cconfig_cmd = {.command = CFG_TYPE_HW("i2c"),
        .help = desc_i2c,
        .hint = NULL,
        .func = &do_i2cconfig_cmd,
        .argtable = &i2cconfig_args};
    cmd_to_json_with_cb(&i2cconfig_cmd, &i2config_cb);
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cconfig_cmd));
}

void register_i2ctools(void) {
    register_i2cconfig();
#if CONFIG_WITH_CONFIG_UI
    register_spiconfig();
#endif
    register_i2cdectect();
    register_i2cget();
#if CONFIG_WITH_CONFIG_UI
    register_i2cset();
    register_i2cdump();
    register_i2c_set_display();
#endif
}
