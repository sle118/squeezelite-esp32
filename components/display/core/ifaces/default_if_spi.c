
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
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <freertos/task.h>
#include "gds.h"
#include "gds_err.h"
#include "gds_private.h"
#include "gds_default_if.h"

static const int GDS_SPI_Command_Mode = 0;
static const int GDS_SPI_Data_Mode = 1;

static spi_host_device_t SPIHost;
static int DCPin;

static bool SPIDefaultWriteBytes(spi_device_handle_t SPIHandle, int WriteMode, const uint8_t* Data, size_t DataLength);
static bool SPIDefaultWriteCommand(struct GDS_Device* Device, uint8_t Command);
static bool SPIDefaultWriteData(struct GDS_Device* Device, const uint8_t* Data, size_t DataLength);

bool GDS_SPIInit(int SPI, int DC) {
    SPIHost = SPI;
    DCPin = DC;
    return true;
}

bool GDS_SPIAttachDevice(struct GDS_Device* Device, int Width, int Height, int CSPin, int RSTPin, int BackLightPin, int Speed, int Mode) {
    spi_device_interface_config_t SPIDeviceConfig = {};
    spi_device_handle_t SPIDevice;

    NullCheck(Device, return false);

    if(CSPin >= 0) {
        ESP_ERROR_CHECK_NONFATAL(gpio_set_direction(CSPin, GPIO_MODE_OUTPUT), return false);
        ESP_ERROR_CHECK_NONFATAL(gpio_set_level(CSPin, 0), return false);
    }

    SPIDeviceConfig.clock_speed_hz = Speed > 0 ? Speed : SPI_MASTER_FREQ_8M;
    SPIDeviceConfig.spics_io_num = CSPin;
    SPIDeviceConfig.queue_size = 1;
    SPIDeviceConfig.mode = Mode;
    SPIDeviceConfig.flags = SPI_DEVICE_NO_DUMMY;
    if(Device->SPIParams)
        Device->SPIParams(SPIDeviceConfig.clock_speed_hz, &SPIDeviceConfig.mode, &SPIDeviceConfig.cs_ena_pretrans, &SPIDeviceConfig.cs_ena_posttrans);

    ESP_ERROR_CHECK_NONFATAL(spi_bus_add_device(SPIHost, &SPIDeviceConfig, &SPIDevice), return false);

    Device->WriteCommand = SPIDefaultWriteCommand;
    Device->WriteData = SPIDefaultWriteData;
    Device->SPIHandle = SPIDevice;
    Device->RSTPin = RSTPin;
    Device->CSPin = CSPin;
    Device->Backlight.Pin = BackLightPin;
    Device->IF = GDS_IF_SPI;
    Device->Width = Device->TextWidth = Width;
    Device->Height = Height;

    if(RSTPin >= 0) {
        ESP_ERROR_CHECK_NONFATAL(gpio_set_direction(RSTPin, GPIO_MODE_OUTPUT), return false);
        ESP_ERROR_CHECK_NONFATAL(gpio_set_level(RSTPin, 0), return false);
        GDS_Reset(Device);
    }

    return GDS_Init(Device);
}

static bool SPIDefaultWriteBytes(spi_device_handle_t SPIHandle, int WriteMode, const uint8_t* Data, size_t DataLength) {
    spi_transaction_t SPITransaction = {};

    NullCheck(SPIHandle, return false);
    NullCheck(Data, return false);

    if(DataLength > 0) {
        gpio_set_level(DCPin, WriteMode);

        SPITransaction.length = DataLength * 8;

        if(DataLength <= 4) {
            SPITransaction.flags = SPI_TRANS_USE_TXDATA;
            SPITransaction.tx_data[0] = *Data++;
            SPITransaction.tx_data[1] = *Data++;
            SPITransaction.tx_data[2] = *Data++;
            SPITransaction.tx_data[3] = *Data;
        } else {
            SPITransaction.tx_buffer = Data;
        }

        // only do polling as we don't have contention on SPI (otherwise DMA for transfers > 16 bytes)
        ESP_ERROR_CHECK_NONFATAL(spi_device_polling_transmit(SPIHandle, &SPITransaction), return false);
    }

    return true;
}

static bool SPIDefaultWriteCommand(struct GDS_Device* Device, uint8_t Command) {
    static uint8_t CommandByte = 0;

    NullCheck(Device, return false);
    NullCheck(Device->SPIHandle, return false);

    CommandByte = Command;

    return SPIDefaultWriteBytes(Device->SPIHandle, GDS_SPI_Command_Mode, &CommandByte, 1);
}

static bool SPIDefaultWriteData(struct GDS_Device* Device, const uint8_t* Data, size_t DataLength) {
    NullCheck(Device, return false);
    NullCheck(Device->SPIHandle, return false);

    return SPIDefaultWriteBytes(Device->SPIHandle, GDS_SPI_Data_Mode, Data, DataLength);
}