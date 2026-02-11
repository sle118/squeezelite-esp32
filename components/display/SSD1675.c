/**
 * Copyright (c) 2017-2018 Tara Keeling
 *				 2020 Philippe G.
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include <esp_log.h>

#include "gds.h"
#include "gds_private.h"

static char TAG[] = "SSD1675";

const unsigned char EPD_lut_full_update[] = {
    0x80,0x60,0x40,0x00,0x00,0x00,0x00,             //LUT0: BB:     VS 0 ~7
    0x10,0x60,0x20,0x00,0x00,0x00,0x00,             //LUT1: BW:     VS 0 ~7
    0x80,0x60,0x40,0x00,0x00,0x00,0x00,             //LUT2: WB:     VS 0 ~7
    0x10,0x60,0x20,0x00,0x00,0x00,0x00,             //LUT3: WW:     VS 0 ~7
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,             //LUT4: VCOM:   VS 0 ~7
    0x03,0x03,0x00,0x00,0x02,                       // TP0 A~D RP0
    0x09,0x09,0x00,0x00,0x02,                       // TP1 A~D RP1
    0x03,0x03,0x00,0x00,0x02,                       // TP2 A~D RP2
    0x00,0x00,0x00,0x00,0x00,                       // TP3 A~D RP3
    0x00,0x00,0x00,0x00,0x00,                       // TP4 A~D RP4
    0x00,0x00,0x00,0x00,0x00,                       // TP5 A~D RP5
    0x00,0x00,0x00,0x00,0x00,                       // TP6 A~D RP6
    0x15,0x41,0xA8,0x32,0x30,0x0A,
};

const unsigned char EPD_lut_partial_update[]= { //20 bytes
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,             //LUT0: BB:     VS 0 ~7
    0x80,0x00,0x00,0x00,0x00,0x00,0x00,             //LUT1: BW:     VS 0 ~7
    0x40,0x00,0x00,0x00,0x00,0x00,0x00,             //LUT2: WB:     VS 0 ~7
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,             //LUT3: WW:     VS 0 ~7
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,             //LUT4: VCOM:   VS 0 ~7
    0x0A,0x00,0x00,0x00,0x00,                       // TP0 A~D RP0
    0x00,0x00,0x00,0x00,0x00,                       // TP1 A~D RP1
    0x00,0x00,0x00,0x00,0x00,                       // TP2 A~D RP2
    0x00,0x00,0x00,0x00,0x00,                       // TP3 A~D RP3
    0x00,0x00,0x00,0x00,0x00,                       // TP4 A~D RP4
    0x00,0x00,0x00,0x00,0x00,                       // TP5 A~D RP5
    0x00,0x00,0x00,0x00,0x00,                       // TP6 A~D RP6
    0x15,0x41,0xA8,0x32,0x30,0x0A,
};

struct PrivateSpace {
	int	ReadyPin;
	uint16_t Height;
};

// Functions are not declared to minimize # of lines

void WaitReady( struct GDS_Device* Device) {
	struct PrivateSpace *Private = (struct PrivateSpace*) Device->Private;
	if (Private->ReadyPin >= 0) {
		int count = 4*1000;
		while (gpio_get_level( Private->ReadyPin ) && count) {
			vTaskDelay( pdMS_TO_TICKS(100) );
			count -= 100;
		}	
	} else {
		vTaskDelay( pdMS_TO_TICKS(2000) );
	}	
}	

static void WriteByte( struct GDS_Device* Device, uint8_t Data ) {
	Device->WriteData( Device, &Data, 1 );
}

static void SetColumnAddress( struct GDS_Device* Device, uint8_t Start, uint8_t End ) {
	// start might be greater than end if we decrement
	Device->WriteCommand( Device, 0x44 );
	Device->WriteData( Device, &Start, 1 );
	Device->WriteData( Device, &End, 1 );
	
	// we obviously want to start ... from the start
	Device->WriteCommand( Device, 0x4e );	
	WriteByte( Device, Start );
}
static void SetRowAddress( struct GDS_Device* Device, uint16_t Start, uint16_t End ) {
	// start might be greater than end if we decrement
	Device->WriteCommand( Device, 0x45 );	
	WriteByte( Device, Start );
	WriteByte( Device, Start >> 8 );
	WriteByte( Device, End );
	WriteByte( Device, End >> 8 );
	
	// we obviously want to start ... from the start
	Device->WriteCommand( Device, 0x4f );	
	WriteByte( Device, Start );
	WriteByte( Device, Start >> 8 );
}

static void Update( struct GDS_Device* Device ) {
	uint8_t *iptr = Device->Framebuffer;
	
	Device->WriteCommand( Device, 0x24 );
	
	// this is awfully slow, but e-ink are slow anyway ...
	for (int i = Device->FramebufferSize; --i >= 0;) {
		WriteByte( Device, ~*iptr++ );
	}	
	
	Device->WriteCommand( Device, 0x22 ); 
    WriteByte( Device, 0xC7);
    Device->WriteCommand( Device, 0X20 );

	WaitReady( Device );
}

// remember that for these ELD drivers W and H are "inverted"
static inline void _DrawPixel( struct GDS_Device* Device, int X, int Y, int Color ) {
    uint32_t YBit = ( Y & 0x07 );
    Y>>= 3;

    uint8_t* FBOffset = Device->Framebuffer + ( ( Y * Device->Width ) + X );
    *FBOffset = ( Color == GDS_COLOR_BLACK ) ? *FBOffset & ~BIT( 7-YBit ) : *FBOffset | BIT( 7-YBit );
}	

static void ClearWindow( struct GDS_Device* Device, int x1, int y1, int x2, int y2, int Color ) {
	for (int r = y1; r <= y2; r++) {
		for (int c = x1; c <= x2; c++) {
			_DrawPixel( Device, c, r, Color );
		}	
	}
}

static void DrawBitmapCBR(struct GDS_Device* Device, uint8_t *Data, int Width, int Height, int Color ) {
	if (!Height) Height = Device->Height;
	if (!Width) Width = Device->Width;
	
	// just do row/column swap
	for (int r = 0; r < Height; r++) {
		uint8_t *optr = Device->Framebuffer + r*Device->Width, *iptr = Data + r;
		for (int c = Width; --c >= 0;) {
			*optr++ = *iptr;
			iptr += Height;
		}	
	}
}

static bool Init( struct GDS_Device* Device ) {
	struct PrivateSpace *Private = (struct PrivateSpace*) Device->Private;
	
	// need to re-adjust framebuffer because these are non % 8 
	Private->Height = Device->Height;
	if (Device->Height & 0x07) Device->Height = ((Device->Height >> 3) + 1) << 3;
	
	Device->FramebufferSize = Device->Width * Device->Height / 8;
	Device->Framebuffer = calloc(1, Device->FramebufferSize);
	NullCheck( Device->Framebuffer, return false );
	
	if (Private->ReadyPin >= 0) {
		esp_rom_gpio_pad_select_gpio( Private->ReadyPin );
		gpio_set_pull_mode( Private->ReadyPin, GPIO_PULLUP_ONLY);
		gpio_set_direction( Private->ReadyPin, GPIO_MODE_INPUT );
	}
	
	// soft reset	
	vTaskDelay(pdMS_TO_TICKS( 2000 ));
    Device->WriteCommand( Device, 0x12 ); 	
	WaitReady( Device );
	
	Device->WriteCommand( Device, 0x74 ); 			
    WriteByte( Device, 0x54 );
	Device->WriteCommand( Device, 0x7e ); 			
    WriteByte( Device, 0x3B );
	
	Device->WriteCommand( Device, 0x3c );	
    WriteByte( Device, 0x03 );
	
	Device->WriteCommand( Device, 0x2c );	
    WriteByte( Device, 0x55 );

	Device->WriteCommand( Device, 0x03 );	
	WriteByte( Device, EPD_lut_full_update[70] );
	
	Device->WriteCommand( Device, 0x04 );	
	WriteByte( Device, EPD_lut_full_update[71] );
	WriteByte( Device, EPD_lut_full_update[72] );
	WriteByte( Device, EPD_lut_full_update[73] );
	
	Device->WriteCommand( Device, 0x3a );	
	WriteByte( Device, EPD_lut_full_update[74] );
	Device->WriteCommand( Device, 0x3b );	
	WriteByte( Device, EPD_lut_full_update[75] );
	
	Device->WriteCommand( Device, 0X32 );	
	for (int i = 0; i < 70; i++) {
		WriteByte( Device, EPD_lut_full_update[i] );
	}	

	// now deal with funny X/Y layout (W and H are "inverted")
	Device->WriteCommand( Device, 0x01 );
    WriteByte( Device, Device->Width - 1 );
    WriteByte( Device, (Device->Width - 1) >> 8 );
    WriteByte( Device, (0 << 0) );		

	/* 
	 Start from 0, Ymax, incX, decY. Starting from X=Height would be difficult
	 as we would hit the extra bits added because height % 8 != 0 and they are
	 not written by the DrawPixel. By starting from X=0 we are aligned and 
	 doing incY is like a clockwise 90° rotation (vs otherwise we would virtually 
	 do a counter-clockwise rotation but again have the "border" issue.
	*/ 
	Device->WriteCommand( Device, 0x11 ); 			
	WriteByte( Device, (1 << 2) | (0 << 1) | (1 << 0));
		
	// must be in order with increment/decrement i.e start might be > end if we decrement
	SetColumnAddress( Device, 0x0, (Device->Height >> 3) - 1 );
	SetRowAddress ( Device, Device->Width - 1, 0 );

	WaitReady( Device );
	
	Update( Device );
	
	return true;
}

static const struct GDS_Device SSD1675 = {
    .DrawBitmapCBR = DrawBitmapCBR,
    .ClearWindow = ClearWindow,
    .DrawPixelFast = _DrawPixel,
    .Update = Update,
    .Init = Init,
    .Mode = GDS_MONO,
    .Depth = 1,
    .Alloc = GDS_ALLOC_NONE,
};

struct GDS_Device* SSD1675_Detect(sys_display_config* Driver, struct GDS_Device* Device) {
    if (Driver->common.driver != sys_display_drivers_SSD1675) return NULL;

    if (!Device) Device = calloc(1, sizeof(struct GDS_Device));
    *Device = SSD1675;
    struct PrivateSpace* Private = (struct PrivateSpace*)Device->Private;
    Private->ReadyPin = -1;
    if (Driver->common.ready >= 0) {
        Private->ReadyPin = Driver->common.ready;
    }

    ESP_LOGI(TAG, "SSD1675 driver with ready GPIO %d", Private->ReadyPin);

    return Device;
}
