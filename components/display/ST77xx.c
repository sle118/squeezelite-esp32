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
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "gds.h"
#include "gds_private.h"

#define SHADOW_BUFFER
#define USE_IRAM
#define PAGE_BLOCK		2048
#define ENABLE_WRITE	0x2c

#define min(a,b) (((a) < (b)) ? (a) : (b))

static char TAG[] = "ST77xx";

enum { ST7735, ST7789 };

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
		if (FirstRow < 0 || ((((LastCol - FirstCol + 1) * 2 + 3 - 1) / 3) * (r - FirstRow + 1) * 3 < PAGE_BLOCK && r != Device->Height - 1)) continue;
		
		FirstCol = (FirstCol * 2) / 3;
		LastCol = (LastCol * 2 + 1) / 3; 
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
	
	if (Private->Model == ST7789) {
		if (Layout->Rotate) Private->Offset.Width += Layout->HFlip ? 320 - Device->Width : 0;
		else Private->Offset.Height += Layout->HFlip ? 320 - Device->Height : 0;
		Device->WriteCommand( Device, Layout->Invert ? 0x20 : 0x21 );			
	} else {
		Device->WriteCommand( Device, Layout->Invert ? 0x21 : 0x20 );	
	}		

	Private->MADCtl = Layout->HFlip ? (Private->MADCtl | (1 << 7)) : (Private->MADCtl & ~(1 << 7));
	Private->MADCtl = Layout->VFlip ? (Private->MADCtl | (1 << 6)) : (Private->MADCtl & ~(1 << 6));
	Private->MADCtl = Layout->Rotate ? (Private->MADCtl | (1 << 5)) : (Private->MADCtl & ~(1 << 5));
	Private->MADCtl = Layout->ColorSwap ? (Private->MADCtl & ~(1 << 3)) : (Private->MADCtl | (1 << 3));	

	Device->WriteCommand( Device, 0x36 );
	WriteByte( Device, Private->MADCtl );

#ifdef SHADOW_BUFFER
	// force a full refresh (almost ...)
	memset(Private->Shadowbuffer, 0xAA, Device->FramebufferSize);
#endif	
}	

static void DisplayOn( struct GDS_Device* Device ) { Device->WriteCommand( Device, 0x29 ); }
static void DisplayOff( struct GDS_Device* Device ) { Device->WriteCommand( Device, 0x28 ); }

static void SetContrast( struct GDS_Device* Device, uint8_t Contrast ) {
	Device->WriteCommand( Device, 0x51 );
	WriteByte( Device, Contrast );
	
	Device->SetContrast = NULL;
	GDS_SetContrast( Device, Contrast );
	Device->SetContrast = SetContrast;
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

	ESP_LOGI(TAG, "ST77xx with bit depth %u, offsets %hu:%hu, page %u, iRAM %p", Device->Depth, Private->Offset.Height, Private->Offset.Width, Private->PageSize, Private->iRAM);
	
	// Sleepout + Booster
	Device->WriteCommand( Device, 0x11 );
	
	// need BGR & Address Mode
	Private->MADCtl = 1 << 3;
	Device->WriteCommand( Device, 0x36 );
	WriteByte( Device, Private->MADCtl );		
	
	// set flip modes & contrast
	GDS_SetContrast( Device, 0x7f );
	struct GDS_Layout Layout = { };
	Device->SetLayout( Device, &Layout );
	
	// set screen depth (16/18)
	Device->WriteCommand( Device, 0x3A );
	if (Private->Model == ST7789) WriteByte( Device, Device->Depth == 24 ? 0x066 : 0x55 );
	else WriteByte( Device, Device->Depth == 24 ? 0x06 : 0x05 );
	
	// gone with the wind
	Device->DisplayOn( Device );
	Device->Update( Device );

	return true;
}	

static const struct GDS_Device ST77xx = {
	.DisplayOn = DisplayOn, .DisplayOff = DisplayOff,
	.SetLayout = SetLayout,
	.Update = Update16, .Init = Init,
	.Mode = GDS_RGB565, .Depth = 16,
};		

struct GDS_Device* ST77xx_Detect(sys_Display * Driver, struct GDS_Device* Device) {
	uint8_t Model;
	int Depth;
	if(Driver->common.driver == sys_DisplayDriverEnum_ST7735)  Model = ST7735;
	else if(Driver->common.driver == sys_DisplayDriverEnum_ST7789)  Model = ST7789;
	else return NULL;
		
	if (!Device) Device = calloc(1, sizeof(struct GDS_Device));
		
	*Device = ST77xx;	
	Depth = Driver->common.bitDepth != 0?Driver->common.bitDepth:18;

	struct PrivateSpace* Private = (struct PrivateSpace*) Device->Private;
	Private->Model = Model;

	if(Driver->common.has_offsets){
		Private->Offset.Height = Driver->common.offsets.height;
		Private->Offset.Width = Driver->common.offsets.width;
	}
	
	if (Depth == 18) {
		Device->Mode = GDS_RGB666;
		Device->Depth = 24;
		Device->Update = Update24;
	} 	
	
	if (Model == ST7789) Device->SetContrast = SetContrast;

	return Device;
}