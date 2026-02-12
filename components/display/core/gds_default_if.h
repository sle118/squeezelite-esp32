#ifndef _GDS_DEFAULT_IF_H_
#define _GDS_DEFAULT_IF_H_

#ifdef __cplusplus
extern "C" {
#endif

struct GDS_Device;

bool GDS_I2CInit(int PortNumber, int SDA, int SCL, int speed);
bool GDS_I2CAttachDevice(struct GDS_Device* Device, int Width, int Height, int I2CAddress, int RSTPin, int BacklightPin);

bool GDS_SPIInit(int SPI, int DC);
bool GDS_SPIAttachDevice(struct GDS_Device* Device, int Width, int Height, int CSPin, int RSTPin, int Speed, int BacklightPin, int Mode);

#ifdef __cplusplus
}
#endif

#endif
