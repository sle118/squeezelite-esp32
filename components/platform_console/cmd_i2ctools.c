/* cmd_i2ctools.c

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
//#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include <stdio.h>
#include "cmd_i2ctools.h"
#include "argtable3/argtable3.h"
#include "driver/i2c.h"
#include "platform_console.h"
#include "esp_log.h"
#include "string.h"
#include "stdio.h"
#include "platform_config.h"
#include "accessors.h"
#include "trace.h"
#include "messaging.h"
#include "display.h"

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

static const char *TAG = "cmd_i2ctools";
#define NOT_OUTPUT "has input capabilities only"
#define NOT_GPIO "is not a GPIO"
static gpio_num_t i2c_gpio_sda = 19;
static gpio_num_t i2c_gpio_scl = 18;
static uint32_t i2c_frequency = 100000;
#ifdef CONFIG_I2C_LOCKED
static i2c_port_t i2c_port = I2C_NUM_1;
#else
static i2c_port_t i2c_port = I2C_NUM_0;
#endif

static struct {
    struct arg_int *chip_address;
    struct arg_int *register_address;
    struct arg_int *data_length;
    struct arg_end *end;
} i2cget_args;

static struct {
    struct arg_int *chip_address;
    struct arg_int *port;
    struct arg_int *register_address;
    struct arg_int *data;
    struct arg_end *end;
} i2cset_args;

static struct {
    struct arg_int *chip_address;
    struct arg_int *size;
    struct arg_end *end;
} i2cdump_args;

static struct {
    struct arg_int *port;
    struct arg_int *freq;
    struct arg_int *sda;
    struct arg_int *scl;
	struct arg_lit *load;
	struct arg_lit *clear;
    struct arg_end *end;
} i2cconfig_args;

static struct {
    struct arg_int *data;
    struct arg_int *clk;
    struct arg_int *dc;
    struct arg_int *host;
	struct arg_lit *clear;
    struct arg_end *end;
} spiconfig_args;

static struct {
    struct arg_int *port;
    struct arg_end *end;
} i2cstop_args;

static struct {
    struct arg_int *port;
    struct arg_end *end;
} i2ccheck_args;

static struct {
	struct arg_str *name;
	struct arg_str *driver;
	struct arg_int *address;
	struct arg_int *width;
	struct arg_int *height;
	struct arg_lit *rotate;
	struct arg_lit *hflip;
	struct arg_lit *vflip;
	struct arg_int *speed;
	struct arg_int *back;
	struct arg_lit *clear;
	struct arg_end *end;
} i2cdisp_args;

int is_output_gpio(struct arg_int * gpio, FILE * f, int * gpio_out){
	int res = 0;
	const char * name = gpio->hdr.longopts?gpio->hdr.longopts:gpio->hdr.glossary;
	*gpio_out=-1;
	int t_gpio=gpio->ival[0];
	if(gpio->count==0){
		fprintf(f,"Missing: %s\n", name);
		res++;
	} else  if(!GPIO_IS_VALID_OUTPUT_GPIO(t_gpio)){
		fprintf(f,"Invalid %s gpio: [%d] %s\n",name, t_gpio, GPIO_IS_VALID_GPIO(t_gpio)?NOT_OUTPUT:NOT_GPIO );
		res++;
	}
	else{
		*gpio_out = t_gpio;
	}
	return res;
}
bool is_i2c_started(i2c_port_t port){
	esp_err_t ret = ESP_OK;
	ESP_LOGD(TAG,"Determining if i2c is started on port %u", port);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ret = i2c_master_start(cmd);
    if(ret == ESP_OK){
    	ret = i2c_master_write_byte(cmd,WRITE_BIT, ACK_CHECK_EN);
    }
    if(ret == ESP_OK){
    	ret = i2c_master_stop(cmd);
    }
    if(ret == ESP_OK){
    	ret = i2c_master_cmd_begin(port, cmd, 50 / portTICK_RATE_MS);
    }
    i2c_cmd_link_delete(cmd);
    ESP_LOGD(TAG,"i2c is %s. %s",ret!=ESP_ERR_INVALID_STATE?"started":"not started", esp_err_to_name(ret));
    return (ret!=ESP_ERR_INVALID_STATE);
}

typedef struct {
	uint8_t address;
	char * description;
} i2c_db_t;


// the list was taken from https://i2cdevices.org/addresses
// on 2020-01-16
static const i2c_db_t i2c_db[] = {
		{ .address = 0x00, .description = "Unknown"},
		{ .address = 0x01, .description = "Unknown"},
		{ .address = 0x02, .description = "Unknown"},
		{ .address = 0x03, .description = "Unknown"},
		{ .address = 0x04, .description = "Unknown"},
		{ .address = 0x05, .description = "Unknown"},
		{ .address = 0x06, .description = "Unknown"},
		{ .address = 0x07, .description = "Unknown"},
		{ .address = 0x0c, .description = "AK8975"},
		{ .address = 0x0d, .description = "AK8975"},
		{ .address = 0x0e, .description = "MAG3110 AK8975 IST-8310"},
		{ .address = 0x0f, .description = "AK8975"},
		{ .address = 0x10, .description = "VEML7700 VML6075"},
		{ .address = 0x11, .description = "Si4713 SAA5246 SAA5243P/K SAA5243P/L SAA5243P/E SAA5243P/H"},
		{ .address = 0x13, .description = "VCNL40x0"},
		{ .address = 0x18, .description = "MCP9808 LIS3DH LSM303"},
		{ .address = 0x19, .description = "MCP9808 LIS3DH LSM303"},
		{ .address = 0x1a, .description = "MCP9808"},
		{ .address = 0x1b, .description = "MCP9808"},
		{ .address = 0x1c, .description = "MCP9808 MMA845x FXOS8700"},
		{ .address = 0x1d, .description = "MCP9808 MMA845x ADXL345 FXOS8700"},
		{ .address = 0x1e, .description = "MCP9808 FXOS8700 HMC5883 LSM303 LSM303"},
		{ .address = 0x1f, .description = "MCP9808 FXOS8700"},
		{ .address = 0x20, .description = "FXAS21002 MCP23008 MCP23017 Chirp!"},
		{ .address = 0x21, .description = "FXAS21002 MCP23008 MCP23017 SAA4700"},
		{ .address = 0x22, .description = "MCP23008 MCP23017 PCA1070"},
		{ .address = 0x23, .description = "MCP23008 MCP23017 SAA4700"},
		{ .address = 0x24, .description = "MCP23008 MCP23017 PCD3311C PCD3312C"},
		{ .address = 0x25, .description = "MCP23008 MCP23017 PCD3311C PCD3312C"},
		{ .address = 0x26, .description = "MCP23008 MCP23017"},
		{ .address = 0x27, .description = "MCP23008 MCP23017"},
		{ .address = 0x28, .description = "BNO055 CAP1188"},
		{ .address = 0x29, .description = "BNO055 CAP1188 TCS34725 TSL2591 VL53L0x VL6180X"},
		{ .address = 0x2a, .description = "CAP1188"},
		{ .address = 0x2b, .description = "CAP1188"},
		{ .address = 0x2c, .description = "CAP1188 AD5248 AD5251 AD5252 CAT5171"},
		{ .address = 0x2d, .description = "CAP1188 AD5248 AD5251 AD5252 CAT5171"},
		{ .address = 0x2e, .description = "AD5248 AD5251 AD5252"},
		{ .address = 0x2f, .description = "AD5248 AD5243 AD5251 AD5252"},
		{ .address = 0x30, .description = "SAA2502"},
		{ .address = 0x31, .description = "SAA2502"},
		{ .address = 0x38, .description = "FT6x06 VEML6070 BMA150 SAA1064"},
		{ .address = 0x39, .description = "TSL2561 APDS-9960 VEML6070 SAA1064"},
		{ .address = 0x3a, .description = "PCF8577C SAA1064"},
		{ .address = 0x3b, .description = "SAA1064 PCF8569"},
		{ .address = 0x3c, .description = "SSD1305 SSD1306 PCF8578 PCF8569 SH1106"},
		{ .address = 0x3d, .description = "SSD1305 SSD1306 PCF8578 SH1106"},
		{ .address = 0x40, .description = "HTU21D-F TMP007 PCA9685 NE5751 TDA8421 INA260 TEA6320 TEA6330 TMP006 TEA6300 Si7021 INA219 TDA9860"},
		{ .address = 0x41, .description = "TMP007 PCA9685 STMPE811 TDA8424 NE5751 TDA8421 INA260 STMPE610 TDA8425 TMP006 INA219 TDA9860 TDA8426"},
		{ .address = 0x42, .description = "HDC1008 TMP007 TMP006 PCA9685 INA219 TDA8415 TDA8417 INA260"},
		{ .address = 0x43, .description = "HDC1008 TMP007 TMP006 PCA9685 INA219 INA260"},
		{ .address = 0x44, .description = "TMP007 TMP006 PCA9685 INA219 STMPE610 SHT31 ISL29125 STMPE811 TDA4688 TDA4672 TDA4780 TDA4670 TDA8442 TDA4687 TDA4671 TDA4680 INA260"},
		{ .address = 0x45, .description = "TMP007 TMP006 PCA9685 INA219 SHT31 TDA8376 INA260"},
		{ .address = 0x46, .description = "TMP007 TMP006 PCA9685 INA219 TDA9150 TDA8370 INA260"},
		{ .address = 0x47, .description = "TMP007 TMP006 PCA9685 INA219 INA260"},
		{ .address = 0x48, .description = "PCA9685 INA219 PN532 TMP102 INA260 ADS1115"},
		{ .address = 0x49, .description = "TSL2561 PCA9685 INA219 TMP102 INA260 ADS1115 AS7262"},
		{ .address = 0x4a, .description = "PCA9685 INA219 TMP102 ADS1115 MAX44009 INA260"},
		{ .address = 0x4b, .description = "PCA9685 INA219 TMP102 ADS1115 MAX44009 INA260"},
		{ .address = 0x4c, .description = "PCA9685 INA219 INA260"},
		{ .address = 0x4d, .description = "PCA9685 INA219 INA260"},
		{ .address = 0x4e, .description = "PCA9685 INA219 INA260"},
		{ .address = 0x4f, .description = "PCA9685 INA219 INA260"},
		{ .address = 0x50, .description = "PCA9685 MB85RC"},
		{ .address = 0x51, .description = "PCA9685 MB85RC"},
		{ .address = 0x52, .description = "PCA9685 MB85RC Nunchuck controller APDS-9250"},
		{ .address = 0x53, .description = "ADXL345 PCA9685 MB85RC"},
		{ .address = 0x54, .description = "PCA9685 MB85RC"},
		{ .address = 0x55, .description = "PCA9685 MB85RC"},
		{ .address = 0x56, .description = "PCA9685 MB85RC"},
		{ .address = 0x57, .description = "PCA9685 MB85RC MAX3010x"},
		{ .address = 0x58, .description = "PCA9685 TPA2016 SGP30"},
		{ .address = 0x59, .description = "PCA9685"},
		{ .address = 0x5a, .description = "PCA9685 CCS811 MLX90614 DRV2605 MPR121"},
		{ .address = 0x5b, .description = "PCA9685 CCS811 MPR121"},
		{ .address = 0x5c, .description = "PCA9685 AM2315 MPR121"},
		{ .address = 0x5d, .description = "PCA9685 MPR121"},
		{ .address = 0x5e, .description = "PCA9685"},
		{ .address = 0x5f, .description = "PCA9685 HTS221"},
		{ .address = 0x60, .description = "PCA9685 MPL115A2 MPL3115A2 Si5351A Si1145 MCP4725A0 TEA5767 TSA5511 SAB3037 SAB3035 MCP4725A1"},
		{ .address = 0x61, .description = "PCA9685 Si5351A MCP4725A0 TEA6100 TSA5511 SAB3037 SAB3035 MCP4725A1"},
		{ .address = 0x62, .description = "PCA9685 MCP4725A1 TSA5511 SAB3037 SAB3035 UMA1014T"},
		{ .address = 0x63, .description = "Si4713 PCA9685 MCP4725A1 TSA5511 SAB3037 SAB3035 UMA1014T"},
		{ .address = 0x64, .description = "PCA9685 MCP4725A2 MCP4725A1"},
		{ .address = 0x65, .description = "PCA9685 MCP4725A2 MCP4725A1"},
		{ .address = 0x66, .description = "PCA9685 MCP4725A3 IS31FL3731 MCP4725A1"},
		{ .address = 0x67, .description = "PCA9685 MCP4725A3 MCP4725A1"},
		{ .address = 0x68, .description = "PCA9685 AMG8833 DS1307 PCF8523 DS3231 MPU-9250 ITG3200 PCF8573 MPU6050"},
		{ .address = 0x69, .description = "PCA9685 AMG8833 MPU-9250 ITG3200 PCF8573 SPS30 MPU6050"},
		{ .address = 0x6a, .description = "PCA9685 L3GD20H PCF8573"},
		{ .address = 0x6b, .description = "PCA9685 L3GD20H PCF8573"},
		{ .address = 0x6c, .description = "PCA9685"},
		{ .address = 0x6d, .description = "PCA9685"},
		{ .address = 0x6e, .description = "PCA9685"},
		{ .address = 0x6f, .description = "PCA9685"},
		{ .address = 0x70, .description = "PCA9685 TCA9548 HT16K33"},
		{ .address = 0x71, .description = "PCA9685 TCA9548 HT16K33"},
		{ .address = 0x72, .description = "PCA9685 TCA9548 HT16K33"},
		{ .address = 0x73, .description = "PCA9685 TCA9548 HT16K33"},
		{ .address = 0x74, .description = "PCA9685 TCA9548 HT16K33"},
		{ .address = 0x75, .description = "PCA9685 TCA9548 HT16K33"},
		{ .address = 0x76, .description = "PCA9685 TCA9548 HT16K33 BME280 BMP280 MS5607 MS5611 BME680"},
		{ .address = 0x77, .description = "PCA9685 TCA9548 HT16K33 IS31FL3731 BME280 BMP280 MS5607 BMP180 BMP085 BMA180 MS5611 BME680"},
		{ .address = 0x78, .description = "PCA9685"},
		{ .address = 0x79, .description = "PCA9685"},
		{ .address = 0x7a, .description = "PCA9685"},
		{ .address = 0x7b, .description = "PCA9685"},
		{ .address = 0x7c, .description = "PCA9685"},
		{ .address = 0x7d, .description = "PCA9685"},
		{ .address = 0x7e, .description = "PCA9685"},
		{ .address = 0x7f, .description = "PCA9685"},
		{ .address = 0, .description = NULL}
};
void i2c_load_configuration(){
	ESP_LOGD(TAG,"Loading configuration from nvs");
	const i2c_config_t * conf =  config_i2c_get((int *)&i2c_port);
	i2c_gpio_scl = conf->scl_io_num;
	i2c_gpio_sda = conf->sda_io_num;
	i2c_frequency = conf->master.clk_speed;
}

const char * i2c_get_description(uint8_t address){
	uint8_t i=0;
	while(i2c_db[i].description && i2c_db[i].address!=address) i++;
	return i2c_db[i].description?i2c_db[i].description:"Unlisted";
}

static esp_err_t i2c_get_port(int port, i2c_port_t *i2c_port)
{
    if (port >= I2C_NUM_MAX) {
    	log_send_messaging(MESSAGING_ERROR,"Wrong port number: %d", port);
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
static esp_err_t i2c_master_driver_install(){
	esp_err_t err=ESP_OK;
	ESP_LOGD(TAG,"Installing i2c driver on port %u", i2c_port);
	if((err=i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0))!=ESP_OK){
		log_send_messaging(MESSAGING_ERROR,"Driver install failed! %s", esp_err_to_name(err));
	}
	return err;
}

static esp_err_t i2c_master_driver_initialize()
{
	esp_err_t err=ESP_OK;
	i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = i2c_gpio_sda,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = i2c_gpio_scl,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = i2c_frequency
    };
    log_send_messaging(MESSAGING_INFO,"Initializing i2c driver configuration.\n   mode = I2C_MODE_MASTER, \n   scl_pullup_en = GPIO_PULLUP_ENABLE, \n   i2c port = %u, \n   sda_io_num = %u, \n   sda_pullup_en = GPIO_PULLUP_ENABLE, \n   scl_io_num = %u, \n   scl_pullup_en = GPIO_PULLUP_ENABLE, \n   master.clk_speed = %u", i2c_port, i2c_gpio_sda,i2c_gpio_scl,i2c_frequency);
    if((err=i2c_param_config(i2c_port, &conf))!=ESP_OK){
    	log_send_messaging(MESSAGING_ERROR,"i2c driver config load failed. %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t i2c_initialize_driver_from_config(){
	esp_err_t err = ESP_OK;
	ESP_LOGD(TAG,"Initializing driver from configuration.");
	i2c_load_configuration();
	if(is_i2c_started(i2c_port)){
		log_send_messaging(MESSAGING_WARNING,"Stopping i2c driver on port %u", i2c_port);
		//  stop the current driver instance
		if((err=i2c_driver_delete(i2c_port))!=ESP_OK){
			log_send_messaging(MESSAGING_ERROR,"i2c driver delete failed. %s", esp_err_to_name(err));
		}
	}
	if(err==ESP_OK){
		err = i2c_master_driver_initialize();
	}
	if(err == ESP_OK){
		err = i2c_master_driver_install();
	}
	return err;
}


static int do_i2c_stop(int argc, char **argv ){

    int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&i2cstop_args);
    if (nerrors != 0) {
        return 0;
    }
    if (i2cstop_args.port->count && i2c_get_port(i2cstop_args.port->ival[0], &i2c_port) != ESP_OK) {
        return 1;
    }
    log_send_messaging(MESSAGING_WARNING,"Stopping i2c on port %u.",i2c_port);
	i2c_driver_delete(i2c_port);
	return 0;
}
static int do_i2c_check(int argc, char **argv ){

	i2c_port_t port=0;
    int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&i2ccheck_args);
    if (nerrors != 0) {

        return 0;
    }
    port=i2c_port;

    if (i2ccheck_args.port->count && i2c_get_port(i2ccheck_args.port->ival[0], &port) != ESP_OK) {
        return 1;
    }
    bool started=is_i2c_started(port);
    log_send_messaging(MESSAGING_INFO,"i2c is %s on port %u.", started?"started":"not started",port );
	return 0;
}
static int do_i2c_show_display(int argc, char **argv){
	char * config_string = (char * )config_alloc_get(NVS_TYPE_STR, "display_config") ;
	if(config_string){
		log_send_messaging(MESSAGING_INFO,"Display configuration string is : \n"
				"display_config = \"%s\"",config_string);
		free(config_string);
	}
	else {
		log_send_messaging(MESSAGING_WARNING,"No display configuration found in nvs config display_config");
	}
	char * nvs_item = config_alloc_get(NVS_TYPE_STR, "i2c_config");
	if (nvs_item) {
		log_send_messaging(MESSAGING_INFO,"I2C configuration is: %s", nvs_item);
		free(nvs_item);
	}
	return 0;
}

static int do_i2c_set_display(int argc, char **argv)
{
	int width=0, height=0, address=60, back=-1, speed=8000000 ;
	char * name = NULL;
	char * driver= NULL;
	char config_string[200]={};
	bool bHasi2cConfig = false, bHasSpiConfig=false;
    int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&i2cdisp_args);
    if (nerrors != 0) {
        return 0;
    }

    char * nvs_item = config_alloc_get(NVS_TYPE_STR, "i2c_config");
	if (nvs_item && strlen(nvs_item)>0) {
		bHasi2cConfig=true;
	}
	FREE_AND_NULL(nvs_item);

	nvs_item = config_alloc_get(NVS_TYPE_STR, "spi_config");
	if (nvs_item && strlen(nvs_item)>0) {
		bHasSpiConfig=true;
	}
	FREE_AND_NULL(nvs_item);

		/* Check "--clear" option */
	if (i2cdisp_args.clear->count) {
		log_send_messaging(MESSAGING_WARNING,"Display config cleared");
		config_set_value(NVS_TYPE_STR, "display_config", "");
		return 0;
	}
	char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		log_send_messaging(MESSAGING_ERROR,"Unable to open memory stream.");
		return 0;
	}
	/* Check "--address" option */
	if (i2cdisp_args.address->count) {
		address=i2cdisp_args.address->ival[0];
	}

	/* Check "--width" option */
	if (i2cdisp_args.width->count) {
		width=i2cdisp_args.width->ival[0];
	}
	else {
		fprintf(f,"Missing parameter: --width\n");
		nerrors ++;
	}

	/* Check "--height" option */
	if (i2cdisp_args.height->count) {
		height=i2cdisp_args.height->ival[0];
	}
	else {
		fprintf(f,"Missing parameter: --height\n");
		nerrors ++;
	}

	/* Check "--name" option */
	if (i2cdisp_args.name->count) {
		name=strdup(i2cdisp_args.name->sval[0]);
	}

	/* Check "--driver" option */
	if (i2cdisp_args.driver->count) {
		driver=strdup(i2cdisp_args.driver->sval[0]);
	}


	/* Check "--back" option */
	nerrors +=is_output_gpio(i2cdisp_args.back,f,&back);


	if(!name) name = strdup("I2C");
	/* Check "--speed" option */
	if (i2cdisp_args.speed->count) {
		speed=i2cdisp_args.speed->ival[0];
	}
	else {
		if(strcasestr(name,"I2C")){
			speed = 250000;
		}
		else {
			speed = 8000000;
		}
	}


	if(!driver) driver = strdup("SSD1306");


	if(!display_is_valid_driver(driver)){
		fprintf(f,"Unsupported display driver %s\n",driver);
		nerrors++;
	}

	if(strcasestr(name,"I2C") && !bHasi2cConfig){
		fprintf(f,"Please configure I2C bus first. \n");
	}else if(strcasestr(name,"SPI") && !bHasSpiConfig){
		fprintf(f,"Please configure SPI bus first. \n");
	}
	else if(!strcasestr(name,"I2C") && !strcasestr(name,"SPI")){
		fprintf(f,"Invalid display type %s\n",name);
	}
	bool rotate = i2cdisp_args.rotate->count>0;

	if(nerrors==0){
		snprintf(config_string, sizeof(config_string),"%s:back=%i,speed=%i,width=%i,height=%i,address=%i,driver=%s%s%s",
				name,back,speed,width,height,address,driver,rotate || i2cdisp_args.hflip->count?",HFlip":"",rotate || i2cdisp_args.vflip->count?",VFlip":"" );
		fprintf(f,"Updating display configuration string configuration to :\n"
				"display_config = \"%s\"",config_string );
		nerrors = config_set_value(NVS_TYPE_STR, "display_config", config_string)!=ESP_OK;
	}

	FREE_AND_NULL(name);
	FREE_AND_NULL(driver);
	fflush (f);
	log_send_messaging(nerrors>0?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);
	return nerrors==0;
}


static int do_spiconfig_cmd(int argc, char **argv){
	static spi_bus_config_t spi_config = {
			.mosi_io_num = -1,
	        .sclk_io_num = -1,
	        .miso_io_num = -1,
	        .quadwp_io_num = -1,
	        .quadhd_io_num = -1
	    };

	int data, clk, dc, host = 0;
	esp_err_t err=ESP_OK;
	int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&spiconfig_args);
	if (nerrors != 0) {
		return 0;
	}

	/* Check "--clear" option */
	if (spiconfig_args.clear->count) {
		log_send_messaging(MESSAGING_WARNING,"spi config cleared");
		config_set_value(NVS_TYPE_STR, "spi_config", "");
		return 0;
	}

	char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		log_send_messaging(MESSAGING_ERROR,"Unable to open memory stream.");
		return 0;
	}
	/* Check "--clk" option */
	nerrors+=is_output_gpio(spiconfig_args.clk, f, &clk);
	nerrors+=is_output_gpio(spiconfig_args.data, f, &data);
	nerrors+=is_output_gpio(spiconfig_args.dc, f, &dc);
	nerrors+=is_output_gpio(spiconfig_args.host, f, &host);

	if(!nerrors){
		spi_config.mosi_io_num=data;
		spi_config.sclk_io_num=clk;

		fprintf(f,"Configuring SPI data:%d clk:%d host:%u dc:%d", spi_config.mosi_io_num, spi_config.sclk_io_num, host, dc);
		if((err=spi_bus_initialize( host, &spi_config, 1 ))!=ESP_OK){
			fprintf(f,"SPI bus initialization failed. %s\n", esp_err_to_name(err));
			nerrors++;
		}
	}

	if(!nerrors){
		fprintf(f,"Storing SPI parameters.\n");
		config_spi_set(&spi_config, host, dc);
	}
	fflush (f);
	log_send_messaging(nerrors>0?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);

	return nerrors==0;


}


static int do_i2cconfig_cmd(int argc, char **argv)
{
	esp_err_t err=ESP_OK;
	int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&i2cconfig_args);
    if (nerrors != 0) {
        return 0;
    }
	/* Check "--clear" option */
	if (i2cconfig_args.clear->count) {
		log_send_messaging(MESSAGING_WARNING,"i2c config cleared");
		config_set_value(NVS_TYPE_STR, "i2c_config", "");
		return 0;
	}

	char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		log_send_messaging(MESSAGING_ERROR,"Unable to open memory stream.");
		return 0;
	}

    /* Check "--load" option */
    if (i2cconfig_args.load->count) {
    	log_send_messaging(MESSAGING_WARNING,"Loading i2c config");
    	i2c_load_configuration();
    }
    else {

		/* Check "--port" option */
		if (i2cconfig_args.port->count) {
			if (i2c_get_port(i2cconfig_args.port->ival[0], &i2c_port) != ESP_OK) {
				fprintf(f,"Invalid port %u \n",i2cconfig_args.port->ival[0]);
				nerrors ++;
			}
		}
		/* Check "--freq" option */
		if (i2cconfig_args.freq->count) {
			i2c_frequency = i2cconfig_args.freq->ival[0];
		}

		nerrors +=is_output_gpio(i2cconfig_args.sda,f,&i2c_gpio_sda);
		nerrors +=is_output_gpio(i2cconfig_args.scl,f,&i2c_gpio_scl);
    }

#ifdef CONFIG_SQUEEZEAMP
	if (i2c_port == I2C_NUM_0) {
		i2c_port = I2C_NUM_1;
		fprintf(f,"can't use i2c port 0 on SqueezeAMP. Changing to port 1.\n");
	}
#endif
	if(!nerrors){
		fprintf(f,"Uninstalling i2c driver from port %u if needed\n",i2c_port);
		if(is_i2c_started(i2c_port)){
			if((err=i2c_driver_delete(i2c_port))!=ESP_OK){
				fprintf(f,"i2c driver delete failed. %s\n", esp_err_to_name(err));
				nerrors++;
			}
		}
	}
	if(!nerrors){
		fprintf(f,"Initializing driver with config scl=%u sda=%u speed=%u port=%u\n",i2c_gpio_scl,i2c_gpio_sda,i2c_frequency,i2c_port);
		if((err=i2c_master_driver_initialize())==ESP_OK){
			fprintf(f,"Initalize success.\n");
			// now start the i2c driver
			fprintf(f,"Starting the i2c driver.");
			if((err=i2c_master_driver_install())!=ESP_OK){
				fprintf(f,"I2C master driver install failed. %s\n", esp_err_to_name(err));
				nerrors++;
			}
			else
			{
				fprintf(f,"i2c driver successfully started.\n");
			}
		}
		else {

			fprintf(f,"I2C initialization failed. %s\n", esp_err_to_name(err));
			nerrors++;
		}
	}
	if(!nerrors && !i2cconfig_args.load->count){
		fprintf(f,"Storing i2c parameters.\n");
			i2c_config_t config={
				.mode = I2C_MODE_MASTER,
				.sda_io_num = i2c_gpio_sda,
				.sda_pullup_en = GPIO_PULLUP_ENABLE,
				.scl_io_num = i2c_gpio_scl,
				.scl_pullup_en = GPIO_PULLUP_ENABLE,
				.master.clk_speed = i2c_frequency
			};
			config_i2c_set(&config, i2c_port);
	}
	fflush (f);
	log_send_messaging(nerrors>0?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);

	return nerrors==0;
}

#define RUN_SHOW_ERROR(c)


static int do_i2cdump_cmd(int argc, char **argv)
{
	int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&i2cdump_args);
    if (nerrors != 0) {
        return 0;
    }

    /* Check chip address: "-c" option */
    int chip_addr = i2cdump_args.chip_address->ival[0];
    /* Check read size: "-s" option */
    int size = 1;
    if (i2cdump_args.size->count) {
        size = i2cdump_args.size->ival[0];
    }
    i2c_port_t loc_i2c_port=i2c_port;
    if (i2cset_args.port->count && i2c_get_port(i2cset_args.port->ival[0], &loc_i2c_port) != ESP_OK) {
    	return 0;
    }

    if (size != 1 && size != 2 && size != 4) {
        log_send_messaging(MESSAGING_ERROR, "Wrong read size. Only support 1,2,4");
        return 1;
    }
    i2c_load_configuration();
    if(i2c_gpio_scl==-1 ||i2c_gpio_sda ==-1){
     	log_send_messaging(MESSAGING_ERROR,"i2c set failed. i2c needs to be configured first.");
     	return 0;
    }
    char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		log_send_messaging(MESSAGING_ERROR,"Unable to open memory stream.");
		return 0;
	}

    uint8_t data_addr;
    uint8_t data[4];
    int32_t block[16];
    fprintf(f,"\n    00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f"
           "    0123456789abcdef\r\n");
    for (int i = 0; i < 128; i += 16) {
        fprintf(f,"%02x: ", i);
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
            esp_err_t ret = i2c_master_cmd_begin(loc_i2c_port, cmd, 50 / portTICK_RATE_MS);
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) {
                for (int k = 0; k < size; k++) {
                    fprintf(f,"%02x ", data[k]);
                    block[j + k] = data[k];
                }
            } else {
                for (int k = 0; k < size; k++) {
                    fprintf(f,"XX ");
                    block[j + k] = -1;
                }
            }
        }
        fprintf(f,"   ");
        for (int k = 0; k < 16; k++) {
            if (block[k] < 0) {
                fprintf(f,"X");
            }
            if ((block[k] & 0xff) == 0x00 || (block[k] & 0xff) == 0xff) {
                fprintf(f,".");
            } else if ((block[k] & 0xff) < 32 || (block[k] & 0xff) >= 127) {
                fprintf(f,"?");
            } else {
                fprintf(f,"%c", block[k] & 0xff);
            }
        }
        fprintf(f,"\r\n");
    }
    // Don't stop the driver;  our firmware may be using it for screen, etc
    //i2c_driver_delete(i2c_port);
	fflush (f);
	log_send_messaging(MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);


    return 0;
}
static int do_i2cset_cmd(int argc, char **argv)
{

	int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&i2cset_args);
    if (nerrors != 0) {
        return 0;
    }

    /* Check chip address: "-c" option */
    int chip_addr = i2cset_args.chip_address->ival[0];
    /* Check register address: "-r" option */
    int data_addr = 0;
    if (i2cset_args.register_address->count) {
        data_addr = i2cset_args.register_address->ival[0];
    }

    i2c_port_t loc_i2c_port=i2c_port;
    if (i2cset_args.port->count && i2c_get_port(i2cset_args.port->ival[0], &loc_i2c_port) != ESP_OK) {
    	return 0;
    }

    /* Check data: "-d" option */
    int len = i2cset_args.data->count;

    i2c_load_configuration();
    if(i2c_gpio_scl==-1 ||i2c_gpio_sda ==-1){
    	log_send_messaging(MESSAGING_ERROR,"i2c set failed. i2c needs to be configured first.");
    	return 0;
    }

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
    esp_err_t ret = i2c_master_cmd_begin(loc_i2c_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) {
    	log_send_messaging(MESSAGING_INFO, "i2c Write OK");
    } else if (ret == ESP_ERR_TIMEOUT) {
    	log_send_messaging(MESSAGING_WARNING, "i2c Bus is busy");
    } else {
    	log_send_messaging(MESSAGING_ERROR,"i2c Read failed");
    }
    // Don't stop the driver;  our firmware may be using it for screen, etc
    //i2c_driver_delete(i2c_port);
    return 0;
}

static int do_i2cget_cmd(int argc, char **argv)
{
    int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&i2cget_args);
    if (nerrors != 0) {
        return 0;
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
    i2c_port_t loc_i2c_port=i2c_port;
    if (i2cset_args.port->count && i2c_get_port(i2cset_args.port->ival[0], &loc_i2c_port) != ESP_OK) {
    	return 0;
    }


    i2c_load_configuration();
    if(i2c_gpio_scl==-1 ||i2c_gpio_sda ==-1){
    	log_send_messaging(MESSAGING_ERROR,"i2c set failed. i2c needs to be configured first.");
    	return 0;
    }
	char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		log_send_messaging(MESSAGING_ERROR,"Unable to open memory stream.");
		return 0;
	}
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    uint8_t *data = malloc(len);
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
    esp_err_t ret = i2c_master_cmd_begin(loc_i2c_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) {
        for (int i = 0; i < len; i++) {
            fprintf(f,"0x%02x ", data[i]);
            if ((i + 1) % 16 == 0) {
                fprintf(f,"\r\n");
            }
        }
        if (len % 16) {
            fprintf(f,"\r\n");
        }
    } else if (ret == ESP_ERR_TIMEOUT) {
    	log_send_messaging(MESSAGING_WARNING, "i2c Bus is busy");
    } else {
    	log_send_messaging(MESSAGING_ERROR,"i2c Read failed");
    }
    free(data);

    // Don't stop the driver;  our firmware may be using it for screen, etc
    //i2c_driver_delete(i2c_port);
	fflush (f);
	log_send_messaging(MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);

    return 0;
}

static int do_i2cdetect_cmd(int argc, char **argv)
{
	uint8_t matches[128]={};
	int last_match=0;
	esp_err_t ret = ESP_OK;
    i2c_load_configuration();
    if(i2c_gpio_scl==-1 ||i2c_gpio_sda ==-1){
    	log_send_messaging(MESSAGING_ERROR,"i2c set failed. i2c needs to be configured first.");
    	return 0;
    }
    i2c_port_t loc_i2c_port=i2c_port;
    if (i2cset_args.port->count && i2c_get_port(i2cset_args.port->ival[0], &loc_i2c_port) != ESP_OK) {
    	return 0;
    }

    uint8_t address;
    char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		log_send_messaging(MESSAGING_ERROR,"Unable to open memory stream.");
		return 0;
	}


    fprintf(f,"\n     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128 ; i += 16) {
        fprintf(f,"%02x: ", i);
        for (int j = 0; j < 16 ; j++) {
            address = i + j;
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (address << 1) | WRITE_BIT, ACK_CHECK_EN);
            i2c_master_stop(cmd);
            ret = i2c_master_cmd_begin(loc_i2c_port, cmd, 50 / portTICK_RATE_MS);
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) {
                fprintf(f,"%02x ", address);
                matches[++last_match-1] = address;
            } else if (ret == ESP_ERR_TIMEOUT) {
                fprintf(f,"UU ");
            } else {
                fprintf(f,"-- ");
            }
        }

        fprintf(f,"\r\n");

    }
    if(last_match) {
    	fprintf(f,"\r\n------------------------------------------------------------------------------------"
    		   "\r\nDetected the following devices (names provided by https://i2cdevices.org/addresses).");

		for(int i=0;i<last_match;i++){
			//printf("%02x = %s\r\n", matches[i], i2c_get_description(matches[i]));
			fprintf(f,"\r\n%u [%02xh]- %s", matches[i], matches[i], i2c_get_description(matches[i]));
		}
		fprintf(f,"\r\n------------------------------------------------------------------------------------\r\n");
    }

	fflush (f);
	log_send_messaging(MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);

    return 0;
}
cJSON * i2c_set_display_cb(){
	cJSON * values = cJSON_CreateObject();
	const display_config_t * conf= config_display_get();
	if(conf){
		cJSON_AddNumberToObject(values,"address",conf->address);
		cJSON_AddNumberToObject(values,"width",conf->width);
		cJSON_AddNumberToObject(values,"height",conf->height);
		cJSON_AddStringToObject(values,"type",conf->type);
		cJSON_AddStringToObject(values,"driver",conf->drivername);
		cJSON_AddBoolToObject(values,"hf",conf->hflip);
		cJSON_AddBoolToObject(values,"vf",conf->vflip);
		if(conf->vflip && conf->hflip){
			cJSON_AddBoolToObject(values,"rotate",true);
		}
		else {
			cJSON_AddBoolToObject(values,"rotate",false);
		}
	}
	return values;
}

static void register_i2c_set_display(){
	char * supported_drivers = display_get_supported_drivers();

	i2cdisp_args.address = arg_int0("a", "address", "<n>", "I2C address (default 60)");
	i2cdisp_args.width = arg_int0("w", "width", "<n>", "Width");
	i2cdisp_args.height = arg_int0("h", "height", "<n>", "Height");
	i2cdisp_args.name = arg_str0("t", "type", "<I2C|SPI>", "Interface (default I2C)");
	i2cdisp_args.driver = arg_str0("d", "driver", supported_drivers?supported_drivers:"<string>", "Driver (default SSD1306)");
	i2cdisp_args.clear = arg_lit0(NULL, "clear", "clear configuration and return");
	i2cdisp_args.hflip = arg_lit0(NULL, "hf", "Flip horizontally");
	i2cdisp_args.vflip = arg_lit0(NULL, "vf", "Flip vertically");
	i2cdisp_args.rotate = arg_lit0("r", "rotate", "Rotate 180 degrees");
	i2cdisp_args.back = arg_int0("b", "back", "<n>","Backlight GPIO (if applicable)");
	i2cdisp_args.speed = arg_int0("s", "speed", "<n>","Bus Speed (Default 8000000 for SPI, 250000 for I2C). SPI interface can work up to 26MHz~40MHz");
	i2cdisp_args.end = arg_end(8);
	const esp_console_cmd_t i2c_set_display= {
	 		.command = "setdisplay",
			.help="Display",
			.hint = NULL,
			.func = &do_i2c_set_display,
			.argtable = &i2cdisp_args
	};

	const esp_console_cmd_t i2c_show_display= {
			.command = "getdisplay",
			.help="Shows display options and global i2c configuration",
			.hint = NULL,
			.func = &do_i2c_show_display,
			.argtable = NULL
	};
	cmd_to_json_with_cb(&i2c_set_display,&i2c_set_display_cb);
	cmd_to_json(&i2c_show_display);
	ESP_ERROR_CHECK(esp_console_cmd_register(&i2c_set_display));
	ESP_ERROR_CHECK(esp_console_cmd_register(&i2c_show_display));
}
static void register_i2cdectect(void)
{
    const esp_console_cmd_t i2cdetect_cmd = {
        .command = "i2cdetect",
        .help = "Scan I2C bus for devices",
        .hint = NULL,
        .func = &do_i2cdetect_cmd,
        .argtable = NULL
    };
    cmd_to_json(&i2cdetect_cmd);
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cdetect_cmd));
}

static void register_i2cget(void)
{
    i2cget_args.chip_address = arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
    i2cget_args.register_address = arg_int0("r", "register", "<register_addr>", "Specify the address on that chip to read from");
    i2cget_args.data_length = arg_int0("l", "length", "<length>", "Specify the length to read from that data address");
    i2cget_args.end = arg_end(1);
    const esp_console_cmd_t i2cget_cmd = {
        .command = "i2cget",
        .help = "Read registers visible through the I2C bus",
        .hint = NULL,
        .func = &do_i2cget_cmd,
        .argtable = &i2cget_args
    };
    cmd_to_json(&i2cget_cmd);
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cget_cmd));
}

static void register_i2cset(void)
{
    i2cset_args.chip_address = arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
    i2cset_args.register_address = arg_int0("r", "register", "<register_addr>", "Specify the address on that chip to read from");
    i2cset_args.data = arg_intn(NULL, NULL, "<data>", 0, 256, "Specify the data to write to that data address");
    i2cset_args.port = arg_intn("p","port","<n>",0,1,"Specify the i2c port (0|2)");

    i2cset_args.end = arg_end(2);
    const esp_console_cmd_t i2cset_cmd = {
        .command = "i2cset",
        .help = "Set registers visible through the I2C bus",
        .hint = NULL,
        .func = &do_i2cset_cmd,
        .argtable = &i2cset_args
    };
    cmd_to_json(&i2cset_cmd);
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cset_cmd));
}




static void register_i2cdump(void)
{
    i2cdump_args.chip_address = arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
    i2cdump_args.size = arg_int0("s", "size", "<size>", "Specify the size of each read");
    i2cdump_args.end = arg_end(3);
    const esp_console_cmd_t i2cdump_cmd = {
        .command = "i2cdump",
        .help = "Examine registers visible through the I2C bus",
        .hint = NULL,
        .func = &do_i2cdump_cmd,
        .argtable = &i2cdump_args
    };
    cmd_to_json(&i2cdump_cmd);
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cdump_cmd));
}

static void register_i2ccheck(){
	i2ccheck_args.port = arg_int0("p", "port", "<0|1>", "Set the I2C bus port number");
	i2ccheck_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "i2ccheck",
        .help = "Check if the I2C bus is installed",
        .hint = NULL,
        .func = &do_i2c_check,
        .argtable = &i2ccheck_args
    };
    cmd_to_json(&cmd);
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}


static void register_i2cstop(){
	i2cstop_args.port = arg_int0("p", "port", "<0|1>", "I2C bus port number");
	i2cstop_args.end = arg_end(2);

    const esp_console_cmd_t i2cconfig_cmd = {
        .command = "i2cstop",
        .help = "Stop the I2C bus",
        .hint = NULL,
        .func = &do_i2c_stop,
        .argtable = &i2cstop_args
    };
    cmd_to_json(&i2cconfig_cmd);
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cconfig_cmd));
}

cJSON * i2config_cb(){
	cJSON * values = cJSON_CreateObject();
	int i2c_port;
	const i2c_config_t * i2c= config_i2c_get(&i2c_port);
	if(i2c->scl_io_num>0) {
		cJSON_AddNumberToObject(values,"scl",i2c->scl_io_num);
	}
	if(i2c->sda_io_num>0) {
		cJSON_AddNumberToObject(values,"sda",i2c->sda_io_num);
	}
	if(i2c->master.clk_speed>0) {
		cJSON_AddNumberToObject(values,"freq",i2c->master.clk_speed);
	}
	if(i2c_port>0) {
		cJSON_AddNumberToObject(values,"port",i2c_port);
	}
	return values;
}
cJSON * spiconfig_cb(){
	cJSON * values = cJSON_CreateObject();
	const spi_bus_config_t * spi_config= config_spi_get(NULL);
	if(spi_config->mosi_io_num>0){
		cJSON_AddNumberToObject(values,"data",spi_config->mosi_io_num);
	}
	if(spi_config->sclk_io_num>0){
		cJSON_AddNumberToObject(values,"clk",spi_config->sclk_io_num);
	}
	if(spi_system_dc_gpio>0){
		cJSON_AddNumberToObject(values,"dc",spi_system_dc_gpio);
	}
	if(spi_system_host>0){
		cJSON_AddNumberToObject(values,"host",spi_system_host);
	}
	return values;
}

static void register_spiconfig(void)
{
	spiconfig_args.clear = arg_lit0(NULL, "clear", "Clear configuration");
	spiconfig_args.clk = arg_int0("k", "clock", "<n>", "Clock GPIO");
	spiconfig_args.data = arg_int0("d","data", "<n>","Data GPIO");
	spiconfig_args.dc = arg_int0("c","dc", "<n>", "DC GPIO");
	spiconfig_args.host= arg_int0("h", "host", "int", "SPI Host Number");
	spiconfig_args.end = arg_end(4);
    const esp_console_cmd_t spiconfig_cmd = {
        .command = "spiconfig",
        .help = "SPI Bus Parameters",
        .hint = NULL,
        .func = &do_spiconfig_cmd,
        .argtable = &spiconfig_args
    };
    cmd_to_json_with_cb(&spiconfig_cmd,&spiconfig_cb);
    ESP_ERROR_CHECK(esp_console_cmd_register(&spiconfig_cmd));
}
static void register_i2cconfig(void)
{
	i2cconfig_args.clear = arg_lit0(NULL, "clear", "Clear configuration");
    i2cconfig_args.port = arg_int0("p", "port", "0|1", "Port");
    i2cconfig_args.freq = arg_int0("f", "freq", "int", "Frequency (Hz) e.g. 100000");
    i2cconfig_args.sda = arg_int0("d", "sda", "<n>", "SDA GPIO. e.g. 19");
    i2cconfig_args.scl = arg_int0("c", "scl", "<n>", "SCL GPIO. e.g. 18");
    i2cconfig_args.load = arg_lit0("l", "load", "Load Existing Configuration");
    i2cconfig_args.end = arg_end(4);
    const esp_console_cmd_t i2cconfig_cmd = {
        .command = "i2cconfig",
        .help = "I2C Bus Parameters",
        .hint = NULL,
        .func = &do_i2cconfig_cmd,
        .argtable = &i2cconfig_args
    };
    cmd_to_json_with_cb(&i2cconfig_cmd,&i2config_cb);
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cconfig_cmd));
}

void register_i2ctools(void)
{
    register_i2cconfig();
    register_spiconfig();
    register_i2cdectect();
    register_i2cget();
    register_i2cset();
    register_i2cdump();
    register_i2c_set_display();
    register_i2cstop();
    register_i2ccheck();
}
