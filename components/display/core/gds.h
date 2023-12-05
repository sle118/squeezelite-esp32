#ifndef _GDS_H_
#define _GDS_H_

#include <stdint.h>
#include <stdbool.h>
#include "Configurator.h"

/* NOTE for drivers:
 The build-in DrawPixel(Fast), DrawCBR and ClearWindow have optimized for 1 bit 
 and 4 bits grayscale screen depth and 8, 16, 24 color. For any other type of screen, 
 DrawCBR and ClearWindow default to use DrawPixel, which is very sub-optimal. For 
 other depth, you  must supply the DrawPixelFast. The built-in 1 bit depth function 
 are only for screen with vertical framing (1 byte = 8 lines). For example SSD1326 in 
 monochrome mode is not such type of screen, SH1106 and SSD1306 are
*/ 

// this is an ordered enum, do not change!
enum { GDS_MONO = 0, GDS_GRAYSCALE, GDS_RGB332, GDS_RGB444, GDS_RGB555, GDS_RGB565, GDS_RGB666, GDS_RGB888 };
		
#define GDS_COLOR_BLACK (0)
#define GDS_COLOR_WHITE (-1)
#define GDS_COLOR_XOR 	(256)

struct GDS_Device;
struct GDS_FontDef;
struct GDS_BacklightPWM { 
	int Channel, Timer, Max;
	bool Init;
};
struct GDS_Layout {
	bool HFlip, VFlip;
	bool Rotate;
	bool Invert; 
	bool ColorSwap;
};

typedef struct GDS_Device* GDS_DetectFunc(sys_Display * Driver, struct GDS_Device *Device);

struct GDS_Device*	GDS_AutoDetect( sys_Display * Driver, GDS_DetectFunc* DetectFunc[], struct GDS_BacklightPWM *PWM );

void 	GDS_SetContrast( struct GDS_Device* Device, uint8_t Contrast );
void 	GDS_DisplayOn( struct GDS_Device* Device );
void 	GDS_DisplayOff( struct GDS_Device* Device ); 
void 	GDS_Update( struct GDS_Device* Device );
void 	GDS_SetLayout( struct GDS_Device* Device, struct GDS_Layout* Layout);
void 	GDS_SetDirty( struct GDS_Device* Device );
int 	GDS_GetWidth( struct GDS_Device* Device );
void 	GDS_SetTextWidth( struct GDS_Device* Device, int TextWidth );
int 	GDS_GetHeight( struct GDS_Device* Device );
int 	GDS_GetDepth( struct GDS_Device* Device );
int 	GDS_GetMode( struct GDS_Device* Device );
int 	GDS_GrayMap( struct GDS_Device* Device, uint8_t Level );
void 	GDS_ClearExt( struct GDS_Device* Device, bool full, ...);
void 	GDS_Clear( struct GDS_Device* Device, int Color );
void 	GDS_ClearWindow( struct GDS_Device* Device, int x1, int y1, int x2, int y2, int Color );

#endif
