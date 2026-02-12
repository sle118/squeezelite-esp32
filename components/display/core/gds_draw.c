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
#include <stdlib.h>
#include <math.h>
#include <esp_attr.h>

#include "gds_private.h"
#include "gds.h"
#include "gds_draw.h"

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

__attribute__((always_inline)) static inline void SwapInt(int* a, int* b) {
    int Temp = *b;

    *b = *a;
    *a = Temp;
}

void IRAM_ATTR GDS_DrawPixelFast(struct GDS_Device* Device, int X, int Y, int Color) { DrawPixelFast(Device, X, Y, Color); }

void IRAM_ATTR GDS_DrawPixel(struct GDS_Device* Device, int X, int Y, int Color) { DrawPixel(Device, X, Y, Color); }

void GDS_DrawHLine(struct GDS_Device* Device, int x, int y, int Width, int Color) {
    int XEnd = x + Width;

    Device->Dirty = true;

    if(x < 0) x = 0;
    if(XEnd >= Device->Width) XEnd = Device->Width - 1;

    if(y < 0)
        y = 0;
    else if(y >= Device->Height)
        y = Device->Height - 1;

    for(; x < XEnd; x++) DrawPixelFast(Device, x, y, Color);
}

void GDS_DrawVLine(struct GDS_Device* Device, int x, int y, int Height, int Color) {
    int YEnd = y + Height;

    Device->Dirty = true;

    if(x < 0) x = 0;
    if(x >= Device->Width) x = Device->Width - 1;

    if(y < 0)
        y = 0;
    else if(YEnd >= Device->Height)
        YEnd = Device->Height - 1;

    for(; y < YEnd; y++) DrawPixel(Device, x, y, Color);
}

static inline void DrawWideLine(struct GDS_Device* Device, int x0, int y0, int x1, int y1, int Color) {
    int dx = (x1 - x0);
    int dy = (y1 - y0);
    int Error = 0;
    int Incr = 1;
    int x = x0;
    int y = y0;

    if(dy < 0) {
        Incr = -1;
        dy = -dy;
    }

    Error = (dy * 2) - dx;

    for(; x < x1; x++) {
        if(IsPixelVisible(Device, x, y) == true) { DrawPixelFast(Device, x, y, Color); }

        if(Error > 0) {
            Error -= (dx * 2);
            y += Incr;
        }

        Error += (dy * 2);
    }
}

static inline void DrawTallLine(struct GDS_Device* Device, int x0, int y0, int x1, int y1, int Color) {
    int dx = (x1 - x0);
    int dy = (y1 - y0);
    int Error = 0;
    int Incr = 1;
    int x = x0;
    int y = y0;

    if(dx < 0) {
        Incr = -1;
        dx = -dx;
    }

    Error = (dx * 2) - dy;

    for(; y < y1; y++) {
        if(IsPixelVisible(Device, x, y) == true) { DrawPixelFast(Device, x, y, Color); }

        if(Error > 0) {
            Error -= (dy * 2);
            x += Incr;
        }

        Error += (dx * 2);
    }
}

void GDS_DrawLine(struct GDS_Device* Device, int x0, int y0, int x1, int y1, int Color) {
    if(x0 == x1) {
        GDS_DrawVLine(Device, x0, y0, (y1 - y0), Color);
    } else if(y0 == y1) {
        GDS_DrawHLine(Device, x0, y0, (x1 - x0), Color);
    } else {
        Device->Dirty = true;
        if(abs(x1 - x0) > abs(y1 - y0)) {
            /* Wide ( run > rise ) */
            if(x0 > x1) {
                SwapInt(&x0, &x1);
                SwapInt(&y0, &y1);
            }

            DrawWideLine(Device, x0, y0, x1, y1, Color);
        } else {
            /* Tall ( rise > run ) */
            if(y0 > y1) {
                SwapInt(&y0, &y1);
                SwapInt(&x0, &x1);
            }

            DrawTallLine(Device, x0, y0, x1, y1, Color);
        }
    }
}

void GDS_DrawBox(struct GDS_Device* Device, int x1, int y1, int x2, int y2, int Color, bool Fill) {
    int Width = (x2 - x1);
    int Height = (y2 - y1);

    Device->Dirty = true;

    if(Fill == false) {
        /* Top side */
        GDS_DrawHLine(Device, x1, y1, Width, Color);

        /* Bottom side */
        GDS_DrawHLine(Device, x1, y1 + Height, Width, Color);

        /* Left side */
        GDS_DrawVLine(Device, x1, y1, Height, Color);

        /* Right side */
        GDS_DrawVLine(Device, x1 + Width, y1, Height, Color);
    } else {
        /* Fill the box by drawing horizontal lines */
        for(; y1 <= y2; y1++) { GDS_DrawHLine(Device, x1, y1, Width, Color); }
    }
}

/****************************************************************************************
 * Process graphic display data from column-oriented data (MSbit first)
 */
void GDS_DrawBitmapCBR(struct GDS_Device* Device, uint8_t* Data, int Width, int Height, int Color) {
    if(!Height) Height = Device->Height;
    if(!Width) Width = Device->Width;

    if(Device->DrawBitmapCBR) {
        Device->DrawBitmapCBR(Device, Data, Width, Height, Color);
    } else if(Device->Depth == 1) {

        Height >>= 3;

        // need to do row/col swap and bit-reverse
        for(int r = 0; r < Height; r++) {
            uint8_t *optr = Device->Framebuffer + r * Device->Width, *iptr = Data + r;
            for(int c = Width; --c >= 0;) {
                *optr++ = BitReverseTable256[*iptr];
                iptr += Height;
            }
        }
    } else if(Device->Depth == 4) {
        uint8_t* optr = Device->Framebuffer;
        int LineLen = Device->Width >> 1;

        Height >>= 3;
        Color &= 0x0f;

        for(int i = Width * Height, r = 0, c = 0; --i >= 0;) {
            uint8_t Byte = BitReverseTable256[*Data++];
            // we need to linearize code to let compiler better optimize
            if(c & 0x01) {
                *optr = (*optr & 0x0f) | (((Byte & 0x01) * Color) << 4);
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0x0f) | (((Byte & 0x01) * Color) << 4);
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0x0f) | (((Byte & 0x01) * Color) << 4);
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0x0f) | (((Byte & 0x01) * Color) << 4);
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0x0f) | (((Byte & 0x01) * Color) << 4);
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0x0f) | (((Byte & 0x01) * Color) << 4);
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0x0f) | (((Byte & 0x01) * Color) << 4);
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0x0f) | (((Byte & 0x01) * Color) << 4);
                optr += LineLen;
            } else {
                *optr = (*optr & 0xf0) | (((Byte & 0x01) * Color));
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0xf0) | (((Byte & 0x01) * Color));
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0xf0) | (((Byte & 0x01) * Color));
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0xf0) | (((Byte & 0x01) * Color));
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0xf0) | (((Byte & 0x01) * Color));
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0xf0) | (((Byte & 0x01) * Color));
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0xf0) | (((Byte & 0x01) * Color));
                optr += LineLen;
                Byte >>= 1;
                *optr = (*optr & 0xf0) | (((Byte & 0x01) * Color));
                optr += LineLen;
            }
            // end of a column, move to next one
            if(++r == Height) {
                c++;
                r = 0;
                optr = Device->Framebuffer + (c >> 1);
            }
        }
    } else if(Device->Depth == 8) {
        uint8_t* optr = Device->Framebuffer;
        int LineLen = Device->Width;

        Height >>= 3;

        for(int i = Width * Height, r = 0, c = 0; --i >= 0;) {
            uint8_t Byte = BitReverseTable256[*Data++];

            // we need to linearize code to let compiler better optimize
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;

            // end of a column, move to next one
            if(++r == Height) {
                c++;
                r = 0;
                optr = Device->Framebuffer + c;
            }
        }
    } else if(Device->Depth == 16) {
        uint16_t* optr = (uint16_t*)Device->Framebuffer;
        int LineLen = Device->Width;

        Height >>= 3;
        Color = __builtin_bswap16(Color);

        for(int i = Width * Height, r = 0, c = 0; --i >= 0;) {
            uint8_t Byte = BitReverseTable256[*Data++];

            // we need to linearize code to let compiler better optimize
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            *optr = ((Byte & 0x01) * Color);
            optr += LineLen;

            // end of a column, move to next one
            if(++r == Height) {
                c++;
                r = 0;
                optr = (uint16_t*)Device->Framebuffer + c;
            }
        }
    } else if(Device->Depth == 24) {
        uint8_t* optr = Device->Framebuffer;
        int LineLen = Device->Width * 3;

        Height >>= 3;
        if(Device->Mode == GDS_RGB666) Color = ((Color << 4) & 0xff0000) | ((Color << 2) & 0xff00) | (Color & 0x00ff);

        for(int i = Width * Height, r = 0, c = 0; --i >= 0;) {
            uint8_t Byte = BitReverseTable256[*Data++];

// we need to linearize code to let compiler better optimize
#define SET24(O, D)                                                                                                                                  \
    O[0] = (D) >> 16;                                                                                                                                \
    O[1] = (D) >> 8;                                                                                                                                 \
    O[2] = (D);
            SET24(optr, (Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            SET24(optr, (Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            SET24(optr, (Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            SET24(optr, (Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            SET24(optr, (Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            SET24(optr, (Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            SET24(optr, (Byte & 0x01) * Color);
            optr += LineLen;
            Byte >>= 1;
            SET24(optr, (Byte & 0x01) * Color);
            optr += LineLen;

            // end of a column, move to next one
            if(++r == Height) {
                c++;
                r = 0;
                optr = Device->Framebuffer + c * 3;
            }
        }
    } else {
        Height >>= 3;

        // don't know bitdepth, use brute-force solution
        for(int i = Width * Height, r = 0, c = 0; --i >= 0;) {
            uint8_t Byte = *Data++;
            DrawPixelFast(Device, c, (r << 3) + 7, (Byte & 0x01) * Color);
            Byte >>= 1;
            DrawPixelFast(Device, c, (r << 3) + 6, (Byte & 0x01) * Color);
            Byte >>= 1;
            DrawPixelFast(Device, c, (r << 3) + 5, (Byte & 0x01) * Color);
            Byte >>= 1;
            DrawPixelFast(Device, c, (r << 3) + 4, (Byte & 0x01) * Color);
            Byte >>= 1;
            DrawPixelFast(Device, c, (r << 3) + 3, (Byte & 0x01) * Color);
            Byte >>= 1;
            DrawPixelFast(Device, c, (r << 3) + 2, (Byte & 0x01) * Color);
            Byte >>= 1;
            DrawPixelFast(Device, c, (r << 3) + 1, (Byte & 0x01) * Color);
            Byte >>= 1;
            DrawPixelFast(Device, c, (r << 3) + 0, (Byte & 0x01) * Color);
            if(++r == Height) {
                c++;
                r = 0;
            }
        }
        /* for better understanding, here is the mundane version 
		for (int x = 0; x < Width; x++) {
			for (int y = 0; y < Height; y++) {
				uint8_t Byte = *Data++;
				GDSDrawPixel4Fast( Device, x, y * 8 + 0, ((Byte >> 7) & 0x01) * Color );
				GDSDrawPixel4Fast( Device, x, y * 8 + 1, ((Byte >> 6) & 0x01) * Color );
				GDSDrawPixel4Fast( Device, x, y * 8 + 2, ((Byte >> 5) & 0x01) * Color );
				GDSDrawPixel4Fast( Device, x, y * 8 + 3, ((Byte >> 4) & 0x01) * Color );
				GDSDrawPixel4Fast( Device, x, y * 8 + 4, ((Byte >> 3) & 0x01) * Color );
				GDSDrawPixel4Fast( Device, x, y * 8 + 5, ((Byte >> 2) & 0x01) * Color );
				GDSDrawPixel4Fast( Device, x, y * 8 + 6, ((Byte >> 1) & 0x01) * Color );
				GDSDrawPixel4Fast( Device, x, y * 8 + 7, ((Byte >> 0) & 0x01) * Color );
			}
		}
		*/
    }

    Device->Dirty = true;
}
