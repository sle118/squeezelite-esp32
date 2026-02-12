/* 
 * (c) Philippe G. 2019, philippe_44@outlook.com
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 * 
 */

#ifndef _GDS_DRAW_H_
#define _GDS_DRAW_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

void IRAM_ATTR GDS_DrawPixelFast(struct GDS_Device* Device, int X, int Y, int Color);
void IRAM_ATTR GDS_DrawPixel(struct GDS_Device* Device, int X, int Y, int Color);
void GDS_DrawHLine(struct GDS_Device* Device, int x, int y, int Width, int Color);
void GDS_DrawVLine(struct GDS_Device* Device, int x, int y, int Height, int Color);
void GDS_DrawLine(struct GDS_Device* Device, int x0, int y0, int x1, int y1, int Color);
void GDS_DrawBox(struct GDS_Device* Device, int x1, int y1, int x2, int y2, int Color, bool Fill);
// draw a bitmap with source 1-bit depth organized in column and col0 = bit7 of byte 0
void GDS_DrawBitmapCBR(struct GDS_Device* Device, uint8_t* Data, int Width, int Height, int Color);

#ifdef __cplusplus
}
#endif

#endif
