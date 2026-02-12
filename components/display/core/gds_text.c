/* 
 * (c) Philippe G. 2019, philippe_44@outlook.com
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 * 
 */

#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <arpa/inet.h>
#include "esp_log.h"

#include "gds_private.h"
#include "gds.h"
#include "gds_draw.h"
#include "gds_text.h"

#define max(a, b) (((a) > (b)) ? (a) : (b))

static char TAG[] = "gds";

/****************************************************************************************
 *  Set fonts for each line in text mode
 */
static const struct GDS_FontDef* GuessFont(struct GDS_Device* Device, int FontType) {
    switch(FontType) {
    case GDS_FONT_DEFAULT:
        return Device->Font;
    case GDS_FONT_LINE_1:
        return Font_line_1;
    case GDS_FONT_LINE_2:
        return Font_line_2;
    case GDS_FONT_MEDIUM:
        //return &Font_droid_sans_fallback_15x17;
    case GDS_FONT_SMALL:
    default:
        return Font_droid_sans_fallback_11x13;
#ifdef USE_LARGE_FONTS
    case GDS_FONT_LARGE:
        return &Font_droid_sans_fallback_24x28;
    case GDS_FONT_SEGMENT:
        if(Device->Height == 32)
            return &Font_Tarable7Seg_16x32;
        else
            return &Font_Tarable7Seg_32x64;
#else
    case GDS_FONT_LARGE:
    case GDS_FONT_SEGMENT:
        ESP_LOGW(TAG, "large fonts disabled");
        //return &Font_droid_sans_fallback_15x17;
        return Font_droid_sans_fallback_11x13;
#endif
    }
}

/****************************************************************************************
 *  Set fonts for each line in text mode
 */
bool GDS_TextSetFontAuto(struct GDS_Device* Device, int N, int FontType, int Space) {
    const struct GDS_FontDef* Font = GuessFont(Device, FontType);
    return GDS_TextSetFont(Device, N, Font, Space);
}

/****************************************************************************************
 *  Set fonts for each line in text mode
 */
bool GDS_TextSetFont(struct GDS_Device* Device, int N, const struct GDS_FontDef* Font, int Space) {
    if(--N >= MAX_LINES) return false;

    Device->Lines[N].Font = Font;

    // re-calculate lines absolute position
    Device->Lines[N].Space = Space;
    Device->Lines[0].Y = Device->Lines[0].Space;
    for(int i = 1; i <= N; i++) Device->Lines[i].Y = Device->Lines[i - 1].Y + Device->Lines[i - 1].Font->Height + Device->Lines[i].Space;

    ESP_LOGI(TAG, "Adding line %u at %d (height:%u)", N + 1, Device->Lines[N].Y, Device->Lines[N].Font->Height);

    if(Device->Lines[N].Y + Device->Lines[N].Font->Height > Device->Height) {
        ESP_LOGW(TAG, "line does not fit display");
        return false;
    }

    return true;
}

/****************************************************************************************
 * 
 */
bool GDS_TextLine(struct GDS_Device* Device, int N, int Pos, int Attr, char* Text) {
    int Width, X = Pos;

    // counting 1..n
    N--;

    GDS_SetFont(Device, Device->Lines[N].Font);
    if(Attr & GDS_TEXT_MONOSPACE) GDS_FontForceMonospace(Device, true);

    Width = GDS_FontMeasureString(Device, Text);

    // adjusting position, erase only EoL for rigth-justified
    if(Pos == GDS_TEXT_RIGHT)
        X = Device->TextWidth - Width - 1;
    else if(Pos == GDS_TEXT_CENTER)
        X = (Device->TextWidth - Width) / 2;

    // erase if requested
    if(Attr & GDS_TEXT_CLEAR) {
        int Y_min = max(0, Device->Lines[N].Y), Y_max = max(0, Device->Lines[N].Y + Device->Lines[N].Font->Height);
        for(int c = (Attr & GDS_TEXT_CLEAR_EOL) ? X : 0; c < Device->TextWidth; c++)
            for(int y = Y_min; y < Y_max; y++) DrawPixelFast(Device, c, y, GDS_COLOR_BLACK);
    }

    GDS_FontDrawString(Device, X, Device->Lines[N].Y, Text, GDS_COLOR_WHITE);

    ESP_LOGD(TAG, "displaying %s line %u (x:%d, attr:%u)", Text, N + 1, X, Attr);

    // update whole display if requested
    Device->Dirty = true;
    if(Attr & GDS_TEXT_UPDATE) GDS_Update(Device);

    return Width + X < Device->TextWidth;
}

/****************************************************************************************
 * 
 */
int GDS_GetTextWidth(struct GDS_Device* Device, int N, int Attr, char* Text) {
    const struct GDS_FontDef* Font = GDS_SetFont(Device, Device->Lines[N - 1].Font);

    if(Attr & GDS_TEXT_MONOSPACE) GDS_FontForceMonospace(Device, true);
    int Width = GDS_FontMeasureString(Device, Text);
    GDS_SetFont(Device, Font);

    return Width;
}

/****************************************************************************************
 * Try to align string for better scrolling visual. there is probably much better to do
 */
int GDS_TextStretch(struct GDS_Device* Device, int N, char* String, int Max) {
    char Space[] = "     ";
    int Len = strlen(String), Extra = 0, Boundary;

    N--;

    // we might already fit
    GDS_SetFont(Device, Device->Lines[N].Font);
    if(GDS_FontMeasureString(Device, String) <= Device->TextWidth) return 0;

    // add some space for better visual
    strncat(String, Space, Max - Len);
    String[Max] = '\0';
    Len = strlen(String);

    // mark the end of the extended string
    Boundary = GDS_FontMeasureString(Device, String);

    // add a full display width
    while(Len < Max && GDS_FontMeasureString(Device, String) - Boundary < Device->TextWidth) {
        String[Len++] = String[Extra++];
        String[Len] = '\0';
    }

    return Boundary;
}

/****************************************************************************************
 * 
 */
void GDS_TextPos(struct GDS_Device* Device, int FontType, int Where, int Attr, char* Text, ...) {
    va_list args;

    TextAnchor Anchor = TextAnchor_Center;

    if(Attr & GDS_TEXT_CLEAR) GDS_Clear(Device, GDS_COLOR_BLACK);

    if(!Text) return;

    va_start(args, Text);

    switch(Where) {
    case GDS_TEXT_TOP_LEFT:
    default:
        Anchor = TextAnchor_NorthWest;
        break;
    case GDS_TEXT_MIDDLE_LEFT:
        Anchor = TextAnchor_West;
        break;
    case GDS_TEXT_BOTTOM_LEFT:
        Anchor = TextAnchor_SouthWest;
        break;
    case GDS_TEXT_CENTERED:
        Anchor = TextAnchor_Center;
        break;
    }

    ESP_LOGD(TAG, "Displaying %s at %u with attribute %u", Text, Anchor, Attr);

    GDS_SetFont(Device, GuessFont(Device, FontType));
    GDS_FontDrawAnchoredString(Device, Anchor, Text, GDS_COLOR_WHITE);

    Device->Dirty = true;
    if(Attr & GDS_TEXT_UPDATE) GDS_Update(Device);

    va_end(args);
}
