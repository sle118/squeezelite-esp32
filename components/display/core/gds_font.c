/**
 * Copyright (c) 2017-2018 Tara Keeling
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "gds_private.h"
#include "gds.h"
#include "gds_font.h"
#include "gds_draw.h"
#include "gds_err.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "tools.h"
static const char * TAG = "gds_font";
struct GDS_FontDef * Font_droid_sans_fallback_11x13 = NULL;
struct GDS_FontDef * Font_line_1 = NULL;
struct GDS_FontDef * Font_line_2 = NULL;

// struct GDS_FontDef * Font_droid_sans_fallback_15x17 = NULL;
// struct GDS_FontDef * Font_droid_sans_fallback_24x28 = NULL;
// struct GDS_FontDef * Font_droid_sans_mono_7x13 = NULL;
// struct GDS_FontDef * Font_droid_sans_mono_13x24 = NULL;
// struct GDS_FontDef * Font_droid_sans_mono_16x31 = NULL;
// struct GDS_FontDef * Font_liberation_mono_9x15 = NULL;
// struct GDS_FontDef * Font_liberation_mono_13x21 = NULL;
// struct GDS_FontDef * Font_liberation_mono_17x30 = NULL;
// struct GDS_FontDef * Font_Tarable7Seg_16x32 = NULL;
// struct GDS_FontDef * Font_Tarable7Seg_32x64 = NULL;


static bool LoadFont(struct GDS_FontDef ** fontPtr, const char * fileName){
    if(!fontPtr){
        ESP_LOGE(TAG, "Invalid pointer for LoadFont");
        return false;
    }
    char font_file_name[CONFIG_SPIFFS_OBJ_NAME_LEN+1]={0};
    snprintf(font_file_name,sizeof(font_file_name),"/spiffs/fonts/%s",fileName);
    // Allocate DMA-capable memory for the font
    struct GDS_FontDef* loadedFont = load_file_dma(NULL,font_file_name);

    // Check if allocation succeeded
    if (loadedFont == NULL) {
        ESP_LOGE(TAG, "Failed to load font");
        return false;
    }
    // Update the pointer
    *fontPtr = loadedFont;

    ESP_LOGI(TAG, "Successfully loaded font: %s", fileName);
    return true;
}
bool gds_init_fonts() {
    bool success = true;

    // Load the Font_droid_sans_fallback_11x13
    if (!LoadFont(&Font_droid_sans_fallback_11x13, "droid_sans_fb_11x13.bin")) {
        success = false;
    }

    // Load the Font_line_1
    if (!LoadFont(&Font_line_1, "line_1.bin")) {
        success = false;
    }

    // Load the Font_line_2
    if (!LoadFont(&Font_line_2, "line_2.bin")) {
        success = false;
    }

    return success;
}
static int RoundUpFontHeight( const struct GDS_FontDef* Font ) {
    int Height = Font->Height;

    if ( ( Height % 8 ) != 0 ) {
        return ( ( Height + 7 ) / 8 ) * 8;
    }

    return Height;
}

static const uint8_t* GetCharPtr( const struct GDS_FontDef* Font, char Character ) {
    return &Font->FontData[( Character - Font->StartChar ) * ( ( Font->Width * ( RoundUpFontHeight( Font ) / 8 ) ) + 1 )];
}

void GDS_FontDrawChar( struct GDS_Device* Device, char Character, int x, int y, int Color ) {
    const uint8_t* GlyphData = NULL;
    int GlyphColumnLen = 0;
    int CharStartX =  0;
    int CharStartY = 0;
    int CharWidth = 0;
    int CharHeight = 0;
    int CharEndX = 0;
    int CharEndY = 0;
    int OffsetX = 0;
    int OffsetY = 0;
    int YByte = 0;
    int YBit = 0;
    int i = 0;

    NullCheck( ( GlyphData = GetCharPtr( Device->Font, Character ) ), return );

    if ( Character >= Device->Font->StartChar && Character <= Device->Font->EndChar ) {
        /* The first byte in the glyph data is the width of the character in pixels, skip over */
        GlyphData++;
        GlyphColumnLen = RoundUpFontHeight( Device->Font ) / 8;
        
        CharWidth = GDS_FontGetCharWidth( Device, Character );
        CharHeight = GDS_FontGetHeight( Device );

        CharStartX = x;
        CharStartY = y;
        
        CharEndX = CharStartX + CharWidth;
        CharEndY = CharStartY + CharHeight;

        /* If the character is partially offscreen offset the end by
        * distance between (coord) and 0.
        */
        OffsetX = ( CharStartX < 0 ) ? abs( CharStartX ) : 0;
        OffsetY = ( CharStartY < 0 ) ? abs( CharStartY ) : 0;

        /* This skips into the proper column within the glyph data */
        GlyphData+= ( OffsetX * GlyphColumnLen );

        CharStartX+= OffsetX;
        CharStartY+= OffsetY;

        /* Do not attempt to draw if this character is entirely offscreen */
        if ( CharEndX < 0 || CharStartX >= Device->TextWidth || CharEndY < 0 || CharStartY >= Device->Height ) {
            ClipDebug( x, y );
            return;
        }

        /* Do not attempt to draw past the end of the screen */
        CharEndX = ( CharEndX >= Device->TextWidth ) ? Device->TextWidth - 1 : CharEndX;
        CharEndY = ( CharEndY >= Device->Height ) ? Device->Height - 1 : CharEndY;
		Device->Dirty = true;

        for ( x = CharStartX; x < CharEndX; x++ ) {
            for ( y = CharStartY, i = 0; y < CharEndY && i < CharHeight; y++, i++ ) {
                YByte = ( i + OffsetY ) / 8;
                YBit = ( i + OffsetY ) & 0x07;

                if ( GlyphData[ YByte ] & BIT( YBit ) ) {
                    DrawPixel( Device, x, y, Color );
                }            
            }

            GlyphData+= GlyphColumnLen;
        }
    }
}

const struct GDS_FontDef* GDS_SetFont( struct GDS_Device* Display, const struct GDS_FontDef* Font ) {
	const struct GDS_FontDef* OldFont = Display->Font;

    Display->FontForceProportional = false;
    Display->FontForceMonospace = false;
    Display->Font = Font;

    return OldFont;
}

void GDS_FontForceProportional( struct GDS_Device* Display, bool Force ) {
    Display->FontForceProportional = Force;
}

void GDS_FontForceMonospace( struct GDS_Device* Display, bool Force ) {
    Display->FontForceMonospace = Force;
}

int GDS_FontGetWidth( struct GDS_Device* Display ) {
    return Display->Font->Width;
}

int GDS_FontGetHeight( struct GDS_Device* Display ) {
    return Display->Font->Height;
}

int GDS_FontGetCharWidth( struct GDS_Device* Display, char Character ) {
    const uint8_t* CharPtr = NULL;
    int Width = 0;

    if ( Character >= Display->Font->StartChar && Character <= Display->Font->EndChar ) {
        CharPtr = GetCharPtr( Display->Font, Character );
        Width = ( Display->Font->Monospace == true ) ? Display->Font->Width : *CharPtr;
        if ( Display->FontForceMonospace == true ) {
            Width = Display->Font->Width;
        }

        if ( Display->FontForceProportional == true ) {
            Width = *CharPtr;
        }
    }

    return Width;
}

int GDS_FontGetMaxCharsPerRow( struct GDS_Device* Display ) {
    return Display->TextWidth / Display->Font->Width;
}

int GDS_FontGetMaxCharsPerColumn( struct GDS_Device* Display ) {
    return Display->Height / Display->Font->Height;    
}

int GDS_FontGetCharHeight( struct GDS_Device* Display ) {
    return Display->Font->Height;
}

int GDS_FontMeasureString( struct GDS_Device* Display, const char* Text ) {
    int Width = 0;
    int Len = 0;

    NullCheck( Text, return 0 );

    for ( Len = strlen( Text ); Len >= 0; Len--, Text++ ) {
        if ( *Text >= Display->Font->StartChar && *Text <= Display->Font->EndChar ) {
            Width+= GDS_FontGetCharWidth( Display, *Text );
        }
    }
    return Width;
}

void GDS_FontDrawString( struct GDS_Device* Display, int x, int y, const char* Text, int Color ) {
    int Len = 0;
    int i = 0;

    NullCheck( Text, return );

    for ( Len = strlen( Text ), i = 0; i < Len; i++ ) {
        GDS_FontDrawChar( Display, *Text, x, y, Color );

        x+= GDS_FontGetCharWidth( Display, *Text );
        Text++;
    }
}

void GDS_FontDrawAnchoredString( struct GDS_Device* Display, TextAnchor Anchor, const char* Text, int Color ) {
    int x = 0;
    int y = 0;

    NullCheck( Text, return );

    GDS_FontGetAnchoredStringCoords( Display, &x, &y, Anchor, Text );
    GDS_FontDrawString( Display, x, y, Text, Color );
}

void GDS_FontGetAnchoredStringCoords( struct GDS_Device* Display, int* OutX, int* OutY, TextAnchor Anchor, const char* Text ) {
    int StringWidth = 0;
    int StringHeight = 0;

    NullCheck( OutX, return );
    NullCheck( OutY, return );
    NullCheck( Text, return );

    StringWidth = GDS_FontMeasureString( Display, Text );
    StringHeight = GDS_FontGetCharHeight( Display );

    switch ( Anchor ) {
        case TextAnchor_East: {
            *OutY = ( Display->Height / 2 ) - ( StringHeight / 2 );
            *OutX = ( Display->TextWidth - StringWidth );

            break;
        }
        case TextAnchor_West: {
            *OutY = ( Display->Height / 2 ) - ( StringHeight / 2 );
            *OutX = 0;

            break;
        }
        case TextAnchor_North: {
            *OutX = ( Display->TextWidth / 2 ) - ( StringWidth / 2 );
            *OutY = 0;

            break;
        }
        case TextAnchor_South: {
            *OutX = ( Display->TextWidth / 2 ) - ( StringWidth / 2 );
            *OutY = ( Display->Height - StringHeight );
            
            break;
        }
        case TextAnchor_NorthEast: {
            *OutX = ( Display->TextWidth - StringWidth );
            *OutY = 0;

            break;
        }
        case TextAnchor_NorthWest: {
            *OutY = 0;
            *OutX = 0;

            break;
        }
        case TextAnchor_SouthEast: {
            *OutY = ( Display->Height - StringHeight );
            *OutX = ( Display->TextWidth - StringWidth );

            break;
        }
        case TextAnchor_SouthWest: {
            *OutY = ( Display->Height - StringHeight );
            *OutX = 0;

            break;
        }
        case TextAnchor_Center: {
            *OutY = ( Display->Height / 2 ) - ( StringHeight / 2 );
            *OutX = ( Display->TextWidth / 2 ) - ( StringWidth / 2 );

            break;
        }
        default: {
            *OutX = 128;
            *OutY = 64;
            
            break;
        }
    };
}
