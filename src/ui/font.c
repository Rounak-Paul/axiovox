#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include "font.h"
#include <stdint.h>
extern const unsigned char ax_font_data[];
extern const size_t ax_font_size;
bool ax_font_bake(AxFont *font, unsigned char *pixels)
{
    if (!font || !pixels || !ax_font_size) return false;
    stbtt_bakedchar baked[AX_FONT_COUNT];
    if (stbtt_BakeFontBitmap(ax_font_data, 0, AX_FONT_BAKED_PIXELS, pixels, AX_FONT_ATLAS, AX_FONT_ATLAS, AX_FONT_FIRST, AX_FONT_COUNT, baked) <= 0) return false;
    for (size_t i = 0; i < AX_FONT_COUNT; ++i) {
        const stbtt_bakedchar *g = &baked[i];
        font->glyphs[i] = (AxGlyph){g->xoff, g->yoff, g->xoff + g->x1 - g->x0, g->yoff + g->y1 - g->y0,
            (float)g->x0 / AX_FONT_ATLAS, (float)g->y0 / AX_FONT_ATLAS,
            (float)g->x1 / AX_FONT_ATLAS, (float)g->y1 / AX_FONT_ATLAS, g->xadvance};
    }
    return true;
}
const AxGlyph *ax_font_glyph(const AxFont *font, const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    unsigned char first = *p++;
    uint32_t code = first;
    int extra = 0;
    if (first >= 0xc2 && first <= 0xdf) { code = first & 31u; extra = 1; }
    else if (first >= 0xe0 && first <= 0xef) { code = first & 15u; extra = 2; }
    else if (first >= 0xf0 && first <= 0xf4) { code = first & 7u; extra = 3; }
    else if (first >= 0x80) code = '?';
    for (int i = 0; i < extra; ++i) {
        if ((*p & 0xc0) != 0x80) { code = '?'; break; }
        code = (code << 6) | (*p++ & 63u);
    }
    *text = (const char *)p;
    if (code < AX_FONT_FIRST || code >= AX_FONT_FIRST + AX_FONT_COUNT) code = '?';
    return &font->glyphs[code - AX_FONT_FIRST];
}
