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
#define PAGE_BLOCK 1024

#define min(a, b) (((a) < (b)) ? (a) : (b))

static char TAG[] = "SSD132x";

enum { SSD1326, SSD1327 };

struct PrivateSpace {
    uint8_t *iRAM, *Shadowbuffer;
    uint8_t ReMap, PageSize;
    uint8_t Model;
};

static const unsigned char BitReverseTable256[] = {0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
    0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8, 0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4,
    0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4, 0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
    0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2, 0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA,
    0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA, 0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
    0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE, 0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1,
    0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1, 0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
    0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5, 0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED,
    0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD, 0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
    0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB, 0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7,
    0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7, 0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF};

// Functions are not declared to minimize # of lines

static void SetColumnAddress(struct GDS_Device* Device, uint8_t Start, uint8_t End) {
    Device->WriteCommand(Device, 0x15);
    Device->WriteCommand(Device, Start);
    Device->WriteCommand(Device, End);
}
static void SetRowAddress(struct GDS_Device* Device, uint8_t Start, uint8_t End) {
    // can be by row, not by page (see Update Optimization)
    Device->WriteCommand(Device, 0x75);
    Device->WriteCommand(Device, Start);
    Device->WriteCommand(Device, End);
}

static void Update4(struct GDS_Device* Device) {
    struct PrivateSpace* Private = (struct PrivateSpace*)Device->Private;

    // always update by full lines
    SetColumnAddress(Device, 0, Device->Width / 2 - 1);

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
                SetRowAddress(Device, r - page + 1, r);
                // own use of IRAM has not proven to be much better than letting SPI do its copy
                if(Private->iRAM) {
                    memcpy(Private->iRAM, Private->Shadowbuffer + (r - page + 1) * Device->Width / 2, page * Device->Width / 2);
                    Device->WriteData(Device, Private->iRAM, Device->Width * page / 2);
                } else {
                    Device->WriteData(Device, Private->Shadowbuffer + (r - page + 1) * Device->Width / 2, page * Device->Width / 2);
                }
                dirty = false;
            }
            page = 0;
        }
    }
#else
    for(int r = 0; r < Device->Height; r += Private->PageSize) {
        SetRowAddress(Device, r, r + Private->PageSize - 1);
        if(Private->iRAM) {
            memcpy(Private->iRAM, Device->Framebuffer + r * Device->Width / 2, Private->PageSize * Device->Width / 2);
            Device->WriteData(Device, Private->iRAM, Private->PageSize * Device->Width / 2);
        } else {
            Device->WriteData(Device, Device->Framebuffer + r * Device->Width / 2, Private->PageSize * Device->Width / 2);
        }
    }
#endif
}

/* 
 We have to make a choice here: either we go by row one by one and send lots of 
 small packets on the serial interface or we do a page of N rows once at least
 a change has been detected. So far, choice is to go one-by-one
*/
static void Update1(struct GDS_Device* Device) {
#ifdef SHADOW_BUFFER
    struct PrivateSpace* Private = (struct PrivateSpace*)Device->Private;
    // not sure the compiler does not have to redo all calculation in for loops, so local it is
    int width = Device->Width / 8, rows = Device->Height;
    uint8_t *optr = Private->Shadowbuffer, *iptr = Device->Framebuffer;
    int CurrentRow = -1, FirstCol = -1, LastCol = -1;

    // by row, find first and last columns that have been updated
    for(int r = 0; r < rows; r++) {
        uint8_t first = 0, last;
        for(int c = 0; c < width; c++) {
            if(*iptr != *optr) {
                if(!first) first = c + 1;
                last = c;
            }
            *optr++ = *iptr++;
        }

        // now update the display by "byte rows"
        if(first--) {
            // only set column when useful, saves a fair bit of CPU
            if(first > FirstCol && first <= FirstCol + 4 && last < LastCol && last >= LastCol - 4) {
                first = FirstCol;
                last = LastCol;
            } else {
                SetColumnAddress(Device, first, last);
                FirstCol = first;
                LastCol = last;
            }

            // Set row only when needed, otherwise let auto-increment work
            if(r != CurrentRow) SetRowAddress(Device, r, Device->Height - 1);
            CurrentRow = r + 1;

            // actual write
            Device->WriteData(Device, Private->Shadowbuffer + r * width + first, last - first + 1);
        }
    }
#else
    // automatic counter and end Row/Column
    SetColumnAddress(Device, 0, Device->Width / 8 - 1);
    SetRowAddress(Device, 0, Device->Height - 1);
    Device->WriteData(Device, Device->Framebuffer, Device->FramebufferSize);
#endif
}

// in 1 bit mode, SSD1326 has a different memory map than SSD1306 and SH1106
static void IRAM_ATTR _DrawPixel1Fast(struct GDS_Device* Device, int X, int Y, int Color) {
    uint32_t XBit = (X & 0x07);
    uint8_t* FBOffset = Device->Framebuffer + ((Y * Device->Width + X) >> 3);

    if(Color == GDS_COLOR_XOR) {
        *FBOffset ^= BIT(7 - XBit);
    } else {
        // we might be able to save the 7-Xbit using BitRemap (A0 bit 2)
        *FBOffset = (Color == GDS_COLOR_BLACK) ? *FBOffset & ~BIT(XBit) : *FBOffset | BIT(XBit);
    }
}

static void ClearWindow(struct GDS_Device* Device, int x1, int y1, int x2, int y2, int Color) {
    uint8_t _Color = Color == GDS_COLOR_BLACK ? 0 : 0xff;
    int Width = Device->Width >> 3;
    uint8_t* optr = Device->Framebuffer;

    for(int r = y1; r <= y2; r++) {
        int c = x1;
        // for a row that is not on a boundary, not column opt can be done, so handle all columns on that line
        while(c & 0x07 && c <= x2) _DrawPixel1Fast(Device, c++, r, Color);
        // at this point we are aligned on column boundary
        int chunk = (x2 - c + 1) >> 3;
        memset(optr + Width * r + (c >> 3), _Color, chunk);
        c += chunk * 8;
        while(c <= x2) _DrawPixel1Fast(Device, c++, r, Color);
    }
}

static void DrawBitmapCBR(struct GDS_Device* Device, uint8_t* Data, int Width, int Height, int Color) {
    if(!Height) Height = Device->Height;
    if(!Width) Width = Device->Width;
    int DWidth = Device->Width >> 3;

    // Two consecutive bits of source data are split over two different bytes of framebuffer
    for(int c = 0; c < Width; c++) {
        uint8_t shift = c & 0x07, bit = ~(1 << shift);
        uint8_t* optr = Device->Framebuffer + (c >> 3);

        // we need to linearize code to let compiler better optimize
        for(int r = Height >> 3; --r >= 0;) {
            uint8_t Byte = BitReverseTable256[*Data++];
            *optr = (*optr & bit) | ((Byte & 0x01) << shift);
            optr += DWidth;
            Byte >>= 1;
            *optr = (*optr & bit) | ((Byte & 0x01) << shift);
            optr += DWidth;
            Byte >>= 1;
            *optr = (*optr & bit) | ((Byte & 0x01) << shift);
            optr += DWidth;
            Byte >>= 1;
            *optr = (*optr & bit) | ((Byte & 0x01) << shift);
            optr += DWidth;
            Byte >>= 1;
            *optr = (*optr & bit) | ((Byte & 0x01) << shift);
            optr += DWidth;
            Byte >>= 1;
            *optr = (*optr & bit) | ((Byte & 0x01) << shift);
            optr += DWidth;
            Byte >>= 1;
            *optr = (*optr & bit) | ((Byte & 0x01) << shift);
            optr += DWidth;
            Byte >>= 1;
            *optr = (*optr & bit) | ((Byte & 0x01) << shift);
            optr += DWidth;
        }
    }
}

static void SetLayout(struct GDS_Device* Device, struct GDS_Layout* Layout) {
    struct PrivateSpace* Private = (struct PrivateSpace*)Device->Private;
    if(Private->Model == SSD1326) {
        Private->ReMap = Layout->HFlip ? (Private->ReMap | ((1 << 0) | (1 << 2))) : (Private->ReMap & ~((1 << 0) | (1 << 2)));
        Private->ReMap = Layout->HFlip ? (Private->ReMap | (1 << 1)) : (Private->ReMap & ~(1 << 1));
    } else {
        Private->ReMap = Layout->VFlip ? (Private->ReMap | ((1 << 0) | (1 << 1))) : (Private->ReMap & ~((1 << 0) | (1 << 1)));
        Private->ReMap = Layout->VFlip ? (Private->ReMap | (1 << 4)) : (Private->ReMap & ~(1 << 4));
    }
    Device->WriteCommand(Device, 0xA0);
    Device->WriteCommand(Device, Private->ReMap);
    Device->WriteCommand(Device, Layout->Invert ? 0xA7 : 0xA4);
}

static void DisplayOn(struct GDS_Device* Device) { Device->WriteCommand(Device, 0xAF); }
static void DisplayOff(struct GDS_Device* Device) { Device->WriteCommand(Device, 0xAE); }

static void SetContrast(struct GDS_Device* Device, uint8_t Contrast) {
    Device->WriteCommand(Device, 0x81);
    Device->WriteCommand(Device, Contrast);
}

static bool Init(struct GDS_Device* Device) {
    struct PrivateSpace* Private = (struct PrivateSpace*)Device->Private;

    // find a page size that is not too small is an integer of height
    Private->PageSize = min(8, PAGE_BLOCK / (Device->Width / 2));
    while(Private->PageSize && Device->Height != (Device->Height / Private->PageSize) * Private->PageSize) Private->PageSize--;

#ifdef SHADOW_BUFFER
#ifdef USE_IRAM
    if(Device->IF == GDS_IF_SPI) {
        if(Device->Depth == 1) {
            Private->Shadowbuffer = heap_caps_malloc(Device->FramebufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        } else {
            Private->Shadowbuffer = malloc(Device->FramebufferSize);
            Private->iRAM = heap_caps_malloc(Private->PageSize * Device->Width / 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        }
    } else
#endif
        Private->Shadowbuffer = malloc(Device->FramebufferSize);
    memset(Private->Shadowbuffer, 0xFF, Device->FramebufferSize);
#else
#ifdef USE_IRAM
    if(Device->Depth == 4 && Device->IF == GDS_IF_SPI)
        Private->iRAM = heap_caps_malloc(Private->PageSize * Device->Width / 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#endif
#endif

    ESP_LOGI(TAG, "SSD1326/7 with bit depth %u, page %u, iRAM %p", Device->Depth, Private->PageSize, Private->iRAM);

    // need to be off and disable display RAM
    Device->DisplayOff(Device);
    Device->WriteCommand(Device, 0xA5);

    // need COM split (6)
    Private->ReMap = 1 << 6;
    // MUX Ratio
    Device->WriteCommand(Device, 0xA8);
    Device->WriteCommand(Device, Device->Height - 1);
    // Display Offset
    Device->WriteCommand(Device, 0xA2);
    Device->WriteCommand(Device, 0);
    // Display Start Line
    Device->WriteCommand(Device, 0xA1);
    Device->WriteCommand(Device, 0x00);
    Device->SetContrast(Device, 0x7F);
    // set flip modes
    struct GDS_Layout Layout = {};
    Device->SetLayout(Device, &Layout);
    // no Display Inversion
    Device->WriteCommand(Device, 0xA6);
    // set Clocks
    Device->WriteCommand(Device, 0xB3);
    Device->WriteCommand(Device, (0x08 << 4) | 0x00);
    // set Adressing Mode Horizontal
    Private->ReMap |= (0 << 2);
    // set monotchrome mode if required
    if(Device->Depth == 1) Private->ReMap |= (1 << 4);
    // write ReMap byte
    Device->WriteCommand(Device, 0xA0);
    Device->WriteCommand(Device, Private->ReMap);

    // gone with the wind
    Device->WriteCommand(Device, 0xA4);
    Device->DisplayOn(Device);
    Device->Update(Device);

    return true;
}

static const struct GDS_Device SSD132x = {
    .DisplayOn = DisplayOn,
    .DisplayOff = DisplayOff,
    .SetContrast = SetContrast,
    .SetLayout = SetLayout,
    .Update = Update4,
    .Init = Init,
    .Mode = GDS_GRAYSCALE,
    .Depth = 4,
};

struct GDS_Device* SSD132x_Detect(sys_display_config* Driver, struct GDS_Device* Device) {
    uint8_t Model;
    int Depth;

    if(Driver->common.driver == sys_display_drivers_SSD1326)
        Model = SSD1326;
    else if(Driver->common.driver == sys_display_drivers_SSD1327)
        Model = SSD1327;
    return NULL;

    if(!Device) Device = calloc(1, sizeof(struct GDS_Device));

    *Device = SSD132x;
    struct PrivateSpace* Private = (struct PrivateSpace*)Device->Private;
    Private->Model = Model;

    Depth = Driver->common.bitDepth != 0 ? Driver->common.bitDepth : 1;

    if(Model == SSD1326 && Depth == 1) {
        Device->Update = Update1;
        Device->DrawPixelFast = _DrawPixel1Fast;
        Device->DrawBitmapCBR = DrawBitmapCBR;
        Device->ClearWindow = ClearWindow;
        Device->Depth = 1;
        Device->Mode = GDS_MONO;
#if !defined SHADOW_BUFFER && defined USE_IRAM
        Device->Alloc = GDS_ALLOC_IRAM_SPI;
#endif
    }

    return Device;
}