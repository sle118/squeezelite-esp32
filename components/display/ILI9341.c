/**
 * Copyright (c) 2017-2018 Tara Keeling
 *				 2020 Philippe G.
 *				 2021 Mumpf and Harry1999
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "gds.h"
#include "gds_private.h"

#define SHADOW_BUFFER
#define USE_IRAM
#define PAGE_BLOCK		2048
#define ENABLE_WRITE	0x2c
#define MADCTL_MX  0x40
#define TFT_RGB_BGR  0x08

#define min(a,b) (((a) < (b)) ? (a) : (b))

static char TAG[] = "ILI9341";

enum { ILI9341, ILI9341_24 };	//ILI9341_24 for future use...

struct PrivateSpace {
	uint8_t *iRAM, *Shadowbuffer;
	struct {
		uint16_t Height, Width;
	} Offset;
	uint8_t MADCtl, PageSize;
	uint8_t Model;
};

// Functions are not declared to minimize # of lines

static void WriteByte( struct GDS_Device* Device, uint8_t Data ) {
	Device->WriteData( Device, &Data, 1 );
}

static void SetColumnAddress( struct GDS_Device* Device, uint16_t Start, uint16_t End ) {
	uint32_t Addr = __builtin_bswap16(Start) | (__builtin_bswap16(End) << 16);
	Device->WriteCommand( Device, 0x2A );
	Device->WriteData( Device, (uint8_t*) &Addr, 4 );
}

static void SetRowAddress( struct GDS_Device* Device, uint16_t Start, uint16_t End ) {
	uint32_t Addr = __builtin_bswap16(Start) | (__builtin_bswap16(End) << 16);
	Device->WriteCommand( Device, 0x2B );
	Device->WriteData( Device, (uint8_t*) &Addr, 4 );
}

static void Update16( struct GDS_Device* Device ) {
	struct PrivateSpace *Private = (struct PrivateSpace*) Device->Private;
		
#ifdef SHADOW_BUFFER
	uint32_t *optr = (uint32_t*) Private->Shadowbuffer, *iptr = (uint32_t*) Device->Framebuffer;
	int FirstCol = Device->Width / 2, LastCol = 0, FirstRow = -1, LastRow = 0;  
	
	for (int r = 0; r < Device->Height; r++) {
		// look for change and update shadow (cheap optimization = width is always a multiple of 2)
		for (int c = 0; c < Device->Width / 2; c++, iptr++, optr++) {
			if (*optr != *iptr) {
				*optr = *iptr;
				if (c < FirstCol) FirstCol = c;	
				if (c > LastCol) LastCol = c;
				if (FirstRow < 0) FirstRow = r;
				LastRow = r;
			}
		}

		// wait for a large enough window - careful that window size might increase by more than a line at once !
		if (FirstRow < 0 || ((LastCol - FirstCol + 1) * (r - FirstRow + 1) * 4 < PAGE_BLOCK && r != Device->Height - 1)) continue;
		
		FirstCol *= 2;
		LastCol = LastCol * 2 + 1;
		SetRowAddress( Device, FirstRow + Private->Offset.Height, LastRow + Private->Offset.Height);
		SetColumnAddress( Device, FirstCol + Private->Offset.Width, LastCol + Private->Offset.Width );
		Device->WriteCommand( Device, ENABLE_WRITE );
			
		int ChunkSize = (LastCol - FirstCol + 1) * 2;
			
		// own use of IRAM has not proven to be much better than letting SPI do its copy
		if (Private->iRAM) {
			uint8_t *optr = Private->iRAM;
			for (int i = FirstRow; i <= LastRow; i++) {
				memcpy(optr, Private->Shadowbuffer + (i * Device->Width + FirstCol) * 2, ChunkSize);
				optr += ChunkSize;
				if (optr - Private->iRAM <= (PAGE_BLOCK - ChunkSize) && i < LastRow) continue;
				Device->WriteData(Device, Private->iRAM, optr - Private->iRAM);
				optr = Private->iRAM;
			}
		} else for (int i = FirstRow; i <= LastRow; i++) {
			Device->WriteData( Device, Private->Shadowbuffer + (i * Device->Width + FirstCol) * 2, ChunkSize );
		}	

		FirstCol = Device->Width / 2; LastCol = 0;
		FirstRow = -1;
	}	
#else
	// always update by full lines
	SetColumnAddress( Device, Private->Offset.Width, Device->Width - 1);
	
	for (int r = 0; r < Device->Height; r += min(Private->PageSize, Device->Height - r)) {
		int Height = min(Private->PageSize, Device->Height - r);
		
		SetRowAddress( Device, Private->Offset.Height + r, Private->Offset.Height + r + Height - 1 );
		Device->WriteCommand(Device, ENABLE_WRITE);
		
		if (Private->iRAM) {
			memcpy(Private->iRAM, Device->Framebuffer + r * Device->Width * 2, Height * Device->Width * 2 );
			Device->WriteData( Device, Private->iRAM, Height * Device->Width * 2 );
		} else	{
			Device->WriteData( Device, Device->Framebuffer + r * Device->Width * 2, Height * Device->Width * 2 );
		}	
	}	
#endif	
}

static void Update24( struct GDS_Device* Device ) {
	struct PrivateSpace *Private = (struct PrivateSpace*) Device->Private;
		
#ifdef SHADOW_BUFFER
	uint16_t *optr = (uint16_t*) Private->Shadowbuffer, *iptr = (uint16_t*) Device->Framebuffer;
	int FirstCol = (Device->Width * 3) / 2, LastCol = 0, FirstRow = -1, LastRow = 0;  

	for (int r = 0; r < Device->Height; r++) {
		// look for change and update shadow (cheap optimization = width always / by 2)
		for (int c = 0; c < (Device->Width * 3) / 2; c++, optr++, iptr++) {
			if (*optr != *iptr) {
				*optr = *iptr;
				if (c < FirstCol) FirstCol = c;	
				if (c > LastCol) LastCol = c;
				if (FirstRow < 0) FirstRow = r;
				LastRow = r;
			}
		}

		// do we have enough to send (cols are divided by 3/2)
		if (FirstRow < 0 || ((((LastCol - FirstCol + 1) * 2 ) / 3) * (r - FirstRow + 1) * 4 < PAGE_BLOCK && r != Device->Height - 1)) continue;
		
		FirstCol = (FirstCol * 2) / 3;
		LastCol = (LastCol * 2 + 1 ) / 3; 
		SetRowAddress( Device, FirstRow + Private->Offset.Height, LastRow + Private->Offset.Height);
		SetColumnAddress( Device, FirstCol + Private->Offset.Width, LastCol + Private->Offset.Width );
		Device->WriteCommand( Device, ENABLE_WRITE );
			
		int ChunkSize = (LastCol - FirstCol + 1) * 3;
					
		// own use of IRAM has not proven to be much better than letting SPI do its copy
		if (Private->iRAM) {
			uint8_t *optr = Private->iRAM;
			for (int i = FirstRow; i <= LastRow; i++) {
				memcpy(optr, Private->Shadowbuffer + (i * Device->Width + FirstCol) * 3, ChunkSize);
				optr += ChunkSize;
				if (optr - Private->iRAM <= (PAGE_BLOCK - ChunkSize) && i < LastRow) continue;
				Device->WriteData(Device, Private->iRAM, optr - Private->iRAM);
				optr = Private->iRAM;
			}	
		} else for (int i = FirstRow; i <= LastRow; i++) {
			Device->WriteData( Device, Private->Shadowbuffer + (i * Device->Width + FirstCol) * 3, ChunkSize );
		}	

		FirstCol = (Device->Width * 3) / 2; LastCol = 0;
		FirstRow = -1;
	}	
#else
	// always update by full lines
	SetColumnAddress( Device, Private->Offset.Width, Device->Width - 1);
	
	for (int r = 0; r < Device->Height; r += min(Private->PageSize, Device->Height - r)) {
		int Height = min(Private->PageSize, Device->Height - r);
		
		SetRowAddress( Device, Private->Offset.Height + r, Private->Offset.Height + r + Height - 1 );
		Device->WriteCommand(Device, ENABLE_WRITE);
		
		if (Private->iRAM) {
			memcpy(Private->iRAM, Device->Framebuffer + r * Device->Width * 3, Height * Device->Width * 3 );
			Device->WriteData( Device, Private->iRAM, Height * Device->Width * 3 );
		} else	{
			Device->WriteData( Device, Device->Framebuffer + r * Device->Width * 3, Height * Device->Width * 3 );
		}	
	}	
#endif	
}

static void SetLayout( struct GDS_Device* Device, struct GDS_Layout *Layout ) { 
	struct PrivateSpace *Private = (struct PrivateSpace*) Device->Private;
	ESP_LOGI(TAG, "SetLayout 197 HFlip=%d VFlip=%d Rotate=%d (1=true)", Layout->HFlip, Layout->VFlip, Layout->Rotate);
	//        D/CX RDX WRX D17-8 D7 D6 D5 D4 D3  D2 D1 D0 HEX
	//Command   0   1   ↑    XX  0  0  1  1  0   1  1  0  36h
	//Parameter 1   1   ↑    XX  MY MX MV ML BGR MH 0  0  00
	//Orientation 0: MADCtl = 0x80  =   1000 0000 (MY=1)
	if ((Device->Height)>(Device->Width)){		//Resolution = 320x240
		Private->MADCtl = (1 << 7);				// 0x80 = default (no Rotation an no Flip)
		if (Layout->HFlip) {							//Flip Horizontal
			int a = Private->MADCtl;
			Private->MADCtl = (a ^ (1 << 7));
		}
		if (Layout->Rotate) {							//Rotate 180 degr.
			int a = Private->MADCtl;
			a = (a ^ (1 << 7));
			Private->MADCtl = (a ^ (1 << 6));
		}
		if (Layout->VFlip) {							//Flip Vertical
			int a = Private->MADCtl;
			Private->MADCtl = (a ^ (1 << 6));
		}
	} else {									//Resolution = 240x320
		Private->MADCtl = (1 << 5);				// 0x20 = default (no Rotation an no Flip)
		if (Layout->HFlip) {							//Flip Horizontal
			int a = Private->MADCtl;
			Private->MADCtl = (a ^ (1 << 6));
		}
		if (Layout->Rotate) {							//Rotate 180 degr.
			int a = Private->MADCtl;
			a = (a ^ (1 << 7));
			Private->MADCtl = (a ^ (1 << 6));
		}
		if (Layout->VFlip) {							//Flip Vertical
			int a = Private->MADCtl;
			Private->MADCtl = (a ^ (1 << 7));
		}
	}
	
	Private->MADCtl = Layout->ColorSwap ? (Private->MADCtl | (1 << 3)) : (Private->MADCtl & ~(1 << 3));

	ESP_LOGI(TAG, "SetLayout 255 Private->MADCtl=%hhu", Private->MADCtl);

	Device->WriteCommand( Device, 0x36 );
	WriteByte( Device, Private->MADCtl );
	
	Device->WriteCommand( Device, Layout->Invert ? 0x21 : 0x20 );
	

#ifdef SHADOW_BUFFER
	// force a full refresh (almost ...)
	memset(Private->Shadowbuffer, 0xAA, Device->FramebufferSize);
#endif	
}	

static void DisplayOn( struct GDS_Device* Device ) { Device->WriteCommand( Device, 0x29 ); }	//DISPON =0x29
static void DisplayOff( struct GDS_Device* Device ) { Device->WriteCommand( Device, 0x28 ); }	//DISPOFF=0x28

static void SetContrast( struct GDS_Device* Device, uint8_t Contrast ) {
	Device->WriteCommand( Device, 0x51 );
	WriteByte( Device, Contrast );
	
	Device->SetContrast = NULL;
	GDS_SetContrast( Device, Contrast );
	Device->SetContrast = SetContrast;	// 0x00 value means the lowest brightness and 0xFF value means the highest brightness.
}

static bool Init( struct GDS_Device* Device ) {
	struct PrivateSpace *Private = (struct PrivateSpace*) Device->Private;
	int Depth = (Device->Depth + 8 - 1) / 8;
	
	Private->PageSize = min(8, PAGE_BLOCK / (Device->Width * Depth));

#ifdef SHADOW_BUFFER	
	Private->Shadowbuffer = malloc( Device->FramebufferSize );	
	memset(Private->Shadowbuffer, 0xFF, Device->FramebufferSize);
#endif
#ifdef USE_IRAM
	Private->iRAM = heap_caps_malloc( (Private->PageSize + 1) * Device->Width * Depth, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA );
#endif

	ESP_LOGI(TAG, "ILI9341 with bit default-depth %u, page %u, iRAM %p", Device->Depth, Private->PageSize, Private->iRAM);
	
	// Sleepout + Booster
	Device->WriteCommand( Device, 0x11 );
			
	// set flip modes & contrast
	GDS_SetContrast( Device, 0x7f );
	struct GDS_Layout Layout = { };
	Device->SetLayout( Device, &Layout );
	
	// set screen depth (16/18) *** INTERFACE PIXEL FORMAT: 0x66=18 bit; 0x55=16 bit
	Device->WriteCommand( Device, 0x3A );
	if (Private->Model == ILI9341_24) WriteByte( Device, Device->Depth == 24 ? 0x66 : 0x55 );
	else WriteByte( Device, Device->Depth == 24 ? 0x66 : 0x55 );

	ESP_LOGI(TAG, "ILI9341_Init 312 device-depth %u, 0x66/0x55=0x%X", Device->Depth, Device->Depth == 24 ? 0x66 : 0x55);

	// no Display Inversion (INVOFF=0x20 INVON=0x21)
	Device->WriteCommand( Device, 0x20 );

	//Gamma Correction: Enable next two line and enabel one of the Test0x Section... or build you own 15 Parameter...
	Device->WriteCommand( Device, 0xF2 ); WriteByte( Device, 0x03 );	// 3Gamma Function: Disable = default (0x02), Enable (0x03)
	Device->WriteCommand( Device, 0x26 ); WriteByte( Device, 0x01 );	// Gamma curve selected (0x01, 0x02, 0x04, 0x08) - A maximum of 4 fixed gamma curves can be selected
	//Gamma Correction Test01
	Device->WriteCommand( Device, 0xE0 );								// Positive Gamma Correction (15 Parameter)
	WriteByte( Device, 0x0F ); WriteByte( Device, 0x31 ); WriteByte( Device, 0x2B ); WriteByte( Device, 0x0C ); WriteByte( Device, 0x0E );
	WriteByte( Device, 0x08 ); WriteByte( Device, 0x4E ); WriteByte( Device, 0xF1 ); WriteByte( Device, 0x37 ); WriteByte( Device, 0x07 );
	WriteByte( Device, 0x10 ); WriteByte( Device, 0x03 ); WriteByte( Device, 0x0E ); WriteByte( Device, 0x09 ); WriteByte( Device, 0x00 );
	Device->WriteCommand( Device, 0xE1 ); 								// Negative Gamma Correction (15 Parameter)
	WriteByte( Device, 0x00 ); WriteByte( Device, 0x0E ); WriteByte( Device, 0x14 ); WriteByte( Device, 0x03 ); WriteByte( Device, 0x11 );
	WriteByte( Device, 0x07 ); WriteByte( Device, 0x31 ); WriteByte( Device, 0xC1 ); WriteByte( Device, 0x48 ); WriteByte( Device, 0x08 );
	WriteByte( Device, 0x0F ); WriteByte( Device, 0x0C ); WriteByte( Device, 0x31 ); WriteByte( Device, 0x36 ); WriteByte( Device, 0x0F );
	 
	// gone with the wind
	Device->DisplayOn( Device );
	Device->Update( Device );

	return true;
}	

static const struct GDS_Device ILI9341_X = {
	.DisplayOn = DisplayOn, .DisplayOff = DisplayOff,
	.SetLayout = SetLayout,
	.Update = Update16, .Init = Init,
	.Mode = GDS_RGB565, .Depth = 16,
};		

struct GDS_Device* ILI9341_Detect(sys_Display * Driver, struct GDS_Device* Device) {
	uint8_t Model;
	int Depth=16;		// 16bit colordepth
	
	if(Driver->common.driver == sys_DisplayDriverEnum_ILI9341) Model = ILI9341;
	else if(Driver->common.driver == sys_DisplayDriverEnum_ILI9341_24) Model = ILI9341_24;
	else return NULL;
	
	if (!Device) Device = calloc(1, sizeof(struct GDS_Device));
		
	*Device = ILI9341_X;	
	Depth = Driver->common.bitDepth != 0?Driver->common.bitDepth:Depth;
	struct PrivateSpace* Private = (struct PrivateSpace*) Device->Private;
	Private->Model = Model;
	ESP_LOGI(TAG, "ILI9341_Detect 391 Driver= %s   Depth=%d", Model==ILI9341?"ILI9341":"ILI9341_24", Depth);

	if (Depth == 18) {
		Device->Mode = GDS_RGB888;
		Device->Depth = 24;
		Device->Update = Update24;
	} 	
	
	if (Model == ILI9341_24) Device->SetContrast = SetContrast;

	return Device;
}