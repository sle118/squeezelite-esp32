/**
 * Copyright (c) 2017-2018 Tara Keeling
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <driver/i2c.h>
#include <driver/gpio.h>
#include "gds.h"
#include "gds_err.h"
#include "gds_private.h"
#include "gds_default_if.h"

static int I2CPortNumber;
static int I2CWait;

static const int GDS_I2C_COMMAND_MODE = 0x80;
static const int GDS_I2C_DATA_MODE = 0x40;

static bool I2CDefaultWriteBytes(int Address, bool IsCommand, const uint8_t* Data, size_t DataLength);
static bool I2CDefaultWriteCommand(struct GDS_Device* Device, uint8_t Command);
static bool I2CDefaultWriteData(struct GDS_Device* Device, const uint8_t* Data, size_t DataLength);

/*
 * Initializes the i2c master with the parameters specified
 * in the component configuration in sdkconfig.h.
 * 
 * Returns true on successful init of the i2c bus.
 */
bool GDS_I2CInit(int PortNumber, int SDA, int SCL, int Speed) {
    I2CPortNumber = PortNumber;

    I2CWait = pdMS_TO_TICKS(Speed ? (250 * 250000) / Speed : 250);

    if(SDA != -1 && SCL != -1) {
        i2c_config_t Config = {0};

        Config.mode = I2C_MODE_MASTER;
        Config.sda_io_num = SDA;
        Config.sda_pullup_en = GPIO_PULLUP_ENABLE;
        Config.scl_io_num = SCL;
        Config.scl_pullup_en = GPIO_PULLUP_ENABLE;
        Config.master.clk_speed = Speed ? Speed : 400000;

        ESP_ERROR_CHECK_NONFATAL(i2c_param_config(I2CPortNumber, &Config), return false);
        ESP_ERROR_CHECK_NONFATAL(i2c_driver_install(I2CPortNumber, Config.mode, 0, 0, 0), return false);
    }

    return true;
}

/*
 * Attaches a display to the I2C bus using default communication functions.
 * 
 * Params:
 * Device: Pointer to your GDS_Device object
 * Width: Width of display
 * Height: Height of display
 * I2CAddress: Address of your display
 * RSTPin: Optional GPIO pin to use for hardware reset, if none pass -1 for this parameter.
 * 
 * Returns true on successful init of display.
 */
bool GDS_I2CAttachDevice(struct GDS_Device* Device, int Width, int Height, int I2CAddress, int RSTPin, int BacklightPin) {
    NullCheck(Device, return false);

    Device->WriteCommand = I2CDefaultWriteCommand;
    Device->WriteData = I2CDefaultWriteData;
    Device->Address = I2CAddress;
    Device->RSTPin = RSTPin;
    Device->Backlight.Pin = BacklightPin;
    Device->IF = GDS_IF_I2C;
    Device->Width = Device->TextWidth = Width;
    Device->Height = Height;

    if(RSTPin >= 0) {
        ESP_ERROR_CHECK_NONFATAL(gpio_set_direction(RSTPin, GPIO_MODE_OUTPUT), return false);
        ESP_ERROR_CHECK_NONFATAL(gpio_set_level(RSTPin, 1), return false);
        GDS_Reset(Device);
    }

    return GDS_Init(Device);
}

static bool I2CDefaultWriteBytes(int Address, bool IsCommand, const uint8_t* Data, size_t DataLength) {
    i2c_cmd_handle_t* CommandHandle = NULL;
    static uint8_t ModeByte = 0;

    NullCheck(Data, return false);

    if((CommandHandle = i2c_cmd_link_create()) != NULL) {
        ModeByte = (IsCommand == true) ? GDS_I2C_COMMAND_MODE : GDS_I2C_DATA_MODE;

        ESP_ERROR_CHECK_NONFATAL(i2c_master_start(CommandHandle), goto error);
        ESP_ERROR_CHECK_NONFATAL(i2c_master_write_byte(CommandHandle, (Address << 1) | I2C_MASTER_WRITE, true), goto error);
        ESP_ERROR_CHECK_NONFATAL(i2c_master_write_byte(CommandHandle, ModeByte, true), goto error);
        ESP_ERROR_CHECK_NONFATAL(i2c_master_write(CommandHandle, (uint8_t*)Data, DataLength, true), goto error);
        ESP_ERROR_CHECK_NONFATAL(i2c_master_stop(CommandHandle), goto error);

        ESP_ERROR_CHECK_NONFATAL(i2c_master_cmd_begin(I2CPortNumber, CommandHandle, I2CWait), goto error);
        i2c_cmd_link_delete(CommandHandle);
    }

    return true;

error:
    if(CommandHandle) i2c_cmd_link_delete(CommandHandle);
    return false;
}

static bool I2CDefaultWriteCommand(struct GDS_Device* Device, uint8_t Command) {
    uint8_t CommandByte = (uint8_t)Command;

    NullCheck(Device, return false);
    return I2CDefaultWriteBytes(Device->Address, true, (const uint8_t*)&CommandByte, 1);
}

static bool I2CDefaultWriteData(struct GDS_Device* Device, const uint8_t* Data, size_t DataLength) {
    NullCheck(Device, return false);
    NullCheck(Data, return false);

    return I2CDefaultWriteBytes(Device->Address, false, Data, DataLength);
}
