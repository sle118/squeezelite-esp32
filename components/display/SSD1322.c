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
#define PAGE_BLOCK 1024

#define min(a, b) (((a) < (b)) ? (a) : (b))

static char TAG[] = "SSD1322";

struct PrivateSpace {
    uint8_t *iRAM, *Shadowbuffer;
    uint8_t ReMap, PageSize;
    uint8_t Offset;
};

// Functions are not declared to minimize # of lines

static void WriteDataByte(struct GDS_Device* Device, uint8_t Data) { Device->WriteData(Device, &Data, 1); }

static void SetColumnAddress(struct GDS_Device* Device, uint8_t Start, uint8_t End) {
    Device->WriteCommand(Device, 0x15);
    Device->WriteData(Device, &Start, 1);
    Device->WriteData(Device, &End, 1);
}
static void SetRowAddress(struct GDS_Device* Device, uint8_t Start, uint8_t End) {
    Device->WriteCommand(Device, 0x75);
    Device->WriteData(Device, &Start, 1);
    Device->WriteData(Device, &End, 1);
}

static void Update(struct GDS_Device* Device) {
    struct PrivateSpace* Private = (struct PrivateSpace*)Device->Private;

    // RAM is by columns of 4 pixels ...
    SetColumnAddress(Device, Private->Offset, Private->Offset + Device->Width / 4 - 1);

#ifdef SHADOW_BUFFER
    uint16_t *optr = (uint16_t*)Private->Shadowbuffer, *iptr = (uint16_t*)Device->Framebuffer;
    bool dirty = false;

    for(int r = 0, page = 0; r < Device->Height; r++) {
        // look for change and update shadow (cheap optimization = width always / by 2)
        for(int c = Device->Width / 2 / 2; --c >= 0;) {
            if(*optr != *iptr) {
                dirty = true;
                *optr = *iptr;
            }
            iptr++;
            optr++;
        }

        // one line done, check for page boundary
        if(++page == Private->PageSize) {
            if(dirty) {
                uint16_t *optr = (uint16_t*)Private->iRAM, *iptr = (uint16_t*)(Private->Shadowbuffer + (r - page + 1) * Device->Width / 2);
                SetRowAddress(Device, r - page + 1, r);
                for(int i = page * Device->Width / 2 / 2; --i >= 0; iptr++) *optr++ = (*iptr >> 8) | (*iptr << 8);
                //memcpy(Private->iRAM, Private->Shadowbuffer + (r - page + 1) * Device->Width / 2, page * Device->Width / 2 );
                Device->WriteCommand(Device, 0x5c);
                Device->WriteData(Device, Private->iRAM, Device->Width * page / 2);
                dirty = false;
            }
            page = 0;
        }
    }
#else
    for(int r = 0; r < Device->Height; r += Private->PageSize) {
        SetRowAddress(Device, r, r + Private->PageSize - 1);
        Device->WriteCommand(Device, 0x5c);
        if(Private->iRAM) {
            uint16_t *optr = (uint16_t*)Private->iRAM, *iptr = (uint16_t*)(Device->Framebuffer + r * Device->Width / 2);
            for(int i = Private->PageSize * Device->Width / 2 / 2; --i >= 0; iptr++) *optr++ = (*iptr >> 8) | (*iptr << 8);
            //memcpy(Private->iRAM, Device->Framebuffer + r * Device->Width / 2, Private->PageSize * Device->Width / 2 );
            Device->WriteData(Device, Private->iRAM, Private->PageSize * Device->Width / 2);
        } else {
            Device->WriteData(Device, Device->Framebuffer + r * Device->Width / 2, Private->PageSize * Device->Width / 2);
        }
    }
#endif
}

static void SetLayout(struct GDS_Device* Device, struct GDS_Layout* Layout) {
    struct PrivateSpace* Private = (struct PrivateSpace*)Device->Private;
    Private->ReMap = Layout->HFlip ? (Private->ReMap & ~(1 << 1)) : (Private->ReMap | (1 << 1));
    Private->ReMap = Layout->VFlip ? (Private->ReMap | (1 << 4)) : (Private->ReMap & ~(1 << 4));
    Device->WriteCommand(Device, 0xA0);
    Device->WriteData(Device, &Private->ReMap, 1);
    WriteDataByte(Device, 0x11);
    Device->WriteCommand(Device, Layout->Invert ? 0xA7 : 0xA6);
}

static void DisplayOn(struct GDS_Device* Device) { Device->WriteCommand(Device, 0xAF); }
static void DisplayOff(struct GDS_Device* Device) { Device->WriteCommand(Device, 0xAE); }

static void SetContrast(struct GDS_Device* Device, uint8_t Contrast) {
    Device->WriteCommand(Device, 0xC1);
    Device->WriteData(Device, &Contrast, 1);
}

static bool Init(struct GDS_Device* Device) {
    struct PrivateSpace* Private = (struct PrivateSpace*)Device->Private;

    // these displays seems to be layout centered (1 column = 4 pixels of 4 bits each, little endian)
    Private->Offset = (480 - Device->Width) / 4 / 2;

    // find a page size that is not too small is an integer of height
    Private->PageSize = min(8, PAGE_BLOCK / (Device->Width / 2));
    while(Private->PageSize && Device->Height != (Device->Height / Private->PageSize) * Private->PageSize) Private->PageSize--;

#ifdef SHADOW_BUFFER
    Private->Shadowbuffer = malloc(Device->FramebufferSize);
    memset(Private->Shadowbuffer, 0xFF, Device->FramebufferSize);
#endif
    Private->iRAM = heap_caps_malloc(Private->PageSize * Device->Width / 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

    ESP_LOGI(TAG, "SSD1322 with offset %u, page %u, iRAM %p", Private->Offset, Private->PageSize, Private->iRAM);

    // need to be off and disable display RAM
    Device->DisplayOff(Device);
    Device->WriteCommand(Device, 0xA5);

    // Display Offset
    Device->WriteCommand(Device, 0xA2);
    WriteDataByte(Device, 0);

    // Display Start Line
    Device->WriteCommand(Device, 0xA1);
    WriteDataByte(Device, 0x00);

    // set flip modes
    Private->ReMap = 0;
    struct GDS_Layout Layout = {};
    Device->SetLayout(Device, &Layout);

    // set Display Enhancement
    Device->WriteCommand(Device, 0xB4);
    WriteDataByte(Device, 0xA0);
    WriteDataByte(Device, 0xB5);

    // set Clocks
    Device->WriteCommand(Device, 0xB3);
    WriteDataByte(Device, 0xB2); // 0x91 seems to be common but is too slow for 5.5'

    // set MUX
    Device->WriteCommand(Device, 0xCA);
    WriteDataByte(Device, Device->Height - 1);

    // phase 1 & 2 period
    Device->WriteCommand(Device, 0xB1);
    WriteDataByte(Device, 0xE3); // 0xE2 was recommended

    // set pre-charge V
    Device->WriteCommand(Device, 0xBB);
    WriteDataByte(Device, 0x0F); // 0x1F causes column interferences

    // set COM deselect voltage
    Device->WriteCommand(Device, 0xBE);
    WriteDataByte(Device, 0x07);

    // no Display Inversion
    Device->WriteCommand(Device, 0xA6);

    // gone with the wind
    Device->DisplayOn(Device);
    Device->Update(Device);

    return true;
}

static const struct GDS_Device SSD1322 = {
    .DisplayOn = DisplayOn,
    .DisplayOff = DisplayOff,
    .SetContrast = SetContrast,
    .SetLayout = SetLayout,
    .Update = Update,
    .Init = Init,
    .Mode = GDS_GRAYSCALE,
    .Depth = 4,
};

struct GDS_Device* SSD1322_Detect(sys_display_config* Driver, struct GDS_Device* Device) {
    if(Driver->common.driver != sys_display_drivers_SSD1322) return NULL;

    if(!Device) Device = calloc(1, sizeof(struct GDS_Device));

    *Device = SSD1322;

    return Device;
}