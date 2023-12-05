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
#define ENABLE_WRITE	0x5c

#define min(a,b) (((a) < (b)) ? (a) : (b))

static char TAG[] = "SSD1351";

struct PrivateSpace {
	uint8_t *iRAM, *Shadowbuffer;
	uint8_t ReMap, PageSize;
};

// Functions are not declared to minimize # of lines

static void WriteByte( struct GDS_Device* Device, uint8_t Data ) {
	Device->WriteData( Device, &Data, 1 );
}

static void SetColumnAddress( struct GDS_Device* Device, uint8_t Start, uint8_t End ) {
	Device->WriteCommand( Device, 0x15 );
	WriteByte( Device, Start );
	WriteByte( Device, End );
}
static void SetRowAddress( struct GDS_Device* Device, uint8_t Start, uint8_t End ) {
	Device->WriteCommand( Device, 0x75 );
	WriteByte( Device, Start );
	WriteByte( Device, End );
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
		SetRowAddress( Device, FirstRow, LastRow );
		SetColumnAddress( Device, FirstCol, LastCol );
		Device->WriteCommand( Device, ENABLE_WRITE );
			
		int ChunkSize = (LastCol - FirstCol + 1) * 2;
			
		// own use of IRAM has not proven to be much better than letting SPI do its copy
		if (Private->iRAM) {
			uint8_t *optr = Private->iRAM;
			for (int i = FirstRow; i <= LastRow; i++) {
				memcpy(optr, Private->Shadowbuffer + (i * Device->Width + FirstCol) * 2, ChunkSize);
				optr += ChunkSize;
				if (optr - Private->iRAM < PAGE_BLOCK && i < LastRow) continue;
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
	SetColumnAddress( Device, 0, Device->Width - 1);
	
	for (int r = 0; r < Device->Height; r += min(Private->PageSize, Device->Height - r)) {
		int Height = min(Private->PageSize, Device->Height - r);
		
		SetRowAddress( Device, r, r + Height - 1 );
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
	int FirstCol = (Device->Width * 3) / 2, LastCol = 0, FirstRow = -1, LastRow = 0;  
		
#ifdef SHADOW_BUFFER
	uint16_t *optr = (uint16_t*) Private->Shadowbuffer, *iptr = (uint16_t*) Device->Framebuffer;
	
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
		SetRowAddress( Device, FirstRow, LastRow );
		SetColumnAddress( Device, FirstCol, LastCol );
		Device->WriteCommand( Device, ENABLE_WRITE );
			
		int ChunkSize = (LastCol - FirstCol + 1) * 3;
					
		// own use of IRAM has not proven to be much better than letting SPI do its copy
		if (Private->iRAM) {
			uint8_t *optr = Private->iRAM;
			for (int i = FirstRow; i <= LastRow; i++) {
				memcpy(optr, Private->Shadowbuffer + (i * Device->Width + FirstCol) * 3, ChunkSize);
				optr += ChunkSize;
				if (optr - Private->iRAM < PAGE_BLOCK && i < LastRow) continue;
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
	SetColumnAddress( Device, 0, Device->Width - 1);
	
	for (int r = 0; r < Device->Height; r += min(Private->PageSize, Device->Height - r)) {
		int Height = min(Private->PageSize, Device->Height - r);
		
		SetRowAddress( Device, r, r + Height - 1 );
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
	Private->ReMap = Layout->HFlip ? (Private->ReMap & ~(1 << 1)) : (Private->ReMap | (1 << 1));
	Private->ReMap = Layout->VFlip ? (Private->ReMap | (1 << 4)) : (Private->ReMap & ~(1 << 4));
	Device->WriteCommand( Device, 0xA0 );
	WriteByte( Device, Private->ReMap );
    Device->WriteCommand( Device, Layout->Invert ? 0xA7 : 0xA6 );	
}	

static void DisplayOn( struct GDS_Device* Device ) { Device->WriteCommand( Device, 0xAF ); }
static void DisplayOff( struct GDS_Device* Device ) { Device->WriteCommand( Device, 0xAE ); }

static void SetContrast( struct GDS_Device* Device, uint8_t Contrast ) {
    Device->WriteCommand( Device, 0xC7 );
	WriteByte( Device, Contrast >> 4);
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

	ESP_LOGI(TAG, "SSD1351 with bit depth %u, page %u, iRAM %p", Device->Depth, Private->PageSize, Private->iRAM);
	
	// unlock (specially 0xA2)
	Device->WriteCommand( Device, 0xFD);
	WriteByte(Device, 0xB1);
		
	// set clocks
	/*
    Device->WriteCommand( Device, 0xB3 );
    WriteByte( Device, ( 0x08 << 4 ) | 0x00 );
	*/

	// need to be off and disable display RAM
	Device->DisplayOff( Device );

	// need COM split (5)
	Private->ReMap = (1 << 5);
	
	// Display Offset
    Device->WriteCommand( Device, 0xA2 );
    WriteByte( Device, 0x00 );

	// Display Start Line
    Device->WriteCommand( Device, 0xA1 );
	WriteByte( Device, 0x00 );
	
	// set flip modes & contrast
	Device->SetContrast( Device, 0x7F );
	struct GDS_Layout Layout = { };
	Device->SetLayout( Device, &Layout );
	
	// set Adressing Mode Horizontal
	Private->ReMap |= (0 << 2);
	// set screen depth (16/18)
	if (Device->Depth == 24) Private->ReMap |= (0x02 << 6);
	// write ReMap byte
	Device->WriteCommand( Device, 0xA0 );
	WriteByte( Device, Private->ReMap );		
	
	// no Display Inversion
    Device->WriteCommand( Device, 0xA6 );	
	
	// gone with the wind
	Device->DisplayOn( Device );
	Device->Update( Device );
	
	return true;
}	

static const struct GDS_Device SSD1351 = {
	.DisplayOn = DisplayOn, .DisplayOff = DisplayOff, .SetContrast = SetContrast,
	.SetLayout = SetLayout,
	.Update = Update16, .Init = Init,
	.Mode = GDS_RGB565, .Depth = 16,
};	

struct GDS_Device* SSD1351_Detect(sys_Display * Driver, struct GDS_Device* Device) {
	int Depth;
	
	if(Driver->common.driver != sys_DisplayDriverEnum_SSD1351) return NULL;
	
	if (!Device) Device = calloc(1, sizeof(struct GDS_Device));
	
	*Device = SSD1351;	
	Depth = Driver->common.bitDepth != 0?Driver->common.bitDepth:18;
	
	if (Depth == 18) {
		Device->Mode = GDS_RGB666;
		Device->Depth = 24;
		Device->Update = Update24;
	} 	
	
	return Device;
}