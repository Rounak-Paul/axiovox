#pragma once
#include "font.h"
#include <stdint.h>
typedef struct { float x, y, u, v, r, g, b, a; } AxUiVertex;
typedef struct { float r, g, b, a; } AxUiColor;
typedef struct {
    AxUiVertex *vertices;
    uint32_t count, capacity;
    const AxFont *font;
    float width, height;
    bool clipped;
    float clip_left, clip_top, clip_right, clip_bottom;
} AxUiCanvas;
/** Appends a clipped solid rectangle; empty rectangles and exhausted buffers are ignored. */
void ax_ui_rect(AxUiCanvas *canvas, float x, float y, float width, float height, AxUiColor color);
/** Draws a filled square-corner panel with a one-pixel border. */
void ax_ui_panel(AxUiCanvas *canvas, float x, float y, float width, float height, AxUiColor fill, AxUiColor border);
/** Measures a bounded UTF-8 text prefix in logical pixels. */
float ax_ui_text_width(const AxFont *font, const char *string, size_t bytes);
/** Draws colored UTF-8 text at a baseline, clips to right/canvas bounds, and returns the end position. */
float ax_ui_text(AxUiCanvas *canvas, const char *string, size_t bytes, float x, float baseline, float right, AxUiColor color);

/** Restricts subsequent primitives to a logical-pixel region; reset after drawing scrollable content. */
void ax_ui_clip(AxUiCanvas *canvas, float x, float y, float width, float height);
/** Restores clipping to the canvas bounds. */
void ax_ui_reset_clip(AxUiCanvas *canvas);
/** Draws text with an explicit font-atlas scale, for headings and other typographic sizes. */
float ax_ui_text_scaled(AxUiCanvas *canvas, const char *string, size_t bytes, float x, float baseline, float right, float scale, AxUiColor color);
