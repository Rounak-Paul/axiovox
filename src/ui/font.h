#pragma once
#include <stdbool.h>
#include <stddef.h>
#define AX_FONT_ATLAS 1024
#define AX_FONT_BAKED_PIXELS 28.0f
#define AX_FONT_DRAW_SCALE 0.5f
#define AX_FONT_FIRST 32
#define AX_FONT_COUNT 224
typedef struct { float x0, y0, x1, y1, u0, v0, u1, v1, advance; } AxGlyph;
typedef struct { AxGlyph glyphs[AX_FONT_COUNT]; } AxFont;
/** Bakes the embedded Departure Mono Nerd Font Mono at 28 physical pixels into caller-owned R8 atlas. */
bool ax_font_bake(AxFont *font, unsigned char *pixels);
/** Decodes one validated UTF-8 sequence, advances text, and returns a supported glyph or replacement. */
const AxGlyph *ax_font_glyph(const AxFont *font, const char **text);
