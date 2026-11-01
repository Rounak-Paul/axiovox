#include "draw.h"
#include <math.h>
#include <string.h>
/** Clips a textured quad while retaining proportional UV coordinates. */
static void quad(AxUiCanvas *canvas, float x0, float y0, float x1, float y1,
                 float u0, float v0, float u1, float v1, AxUiColor color)
{
    if (x1 <= x0 || y1 <= y0 || color.a <= 0 || canvas->capacity < 6 || canvas->count > canvas->capacity - 6) return;
    float left = fmaxf(0, x0), top = fmaxf(0, y0);
    float right = fminf(canvas->width, x1), bottom = fminf(canvas->height, y1);
    if (canvas->clipped) {
        left = fmaxf(left, canvas->clip_left); top = fmaxf(top, canvas->clip_top);
        right = fminf(right, canvas->clip_right); bottom = fminf(bottom, canvas->clip_bottom);
    }
    if (right <= left || bottom <= top) return;
    if (u0 >= 0) {
        float du = (u1 - u0) / (x1 - x0), dv = (v1 - v0) / (y1 - y0);
        u1 = u0 + (right - x0) * du;
        v1 = v0 + (bottom - y0) * dv;
        u0 += (left - x0) * du;
        v0 += (top - y0) * dv;
    }
    AxUiVertex vertices[] = {
        {left,top,u0,v0,color.r,color.g,color.b,color.a}, {right,top,u1,v0,color.r,color.g,color.b,color.a},
        {right,bottom,u1,v1,color.r,color.g,color.b,color.a}, {left,top,u0,v0,color.r,color.g,color.b,color.a},
        {right,bottom,u1,v1,color.r,color.g,color.b,color.a}, {left,bottom,u0,v1,color.r,color.g,color.b,color.a}};
    memcpy(canvas->vertices + canvas->count, vertices, sizeof(vertices));
    canvas->count += 6;
}
void ax_ui_rect(AxUiCanvas *canvas, float x, float y, float width, float height, AxUiColor color)
{
    quad(canvas, floorf(x), floorf(y), floorf(x + width), floorf(y + height), -1,-1,-1,-1, color);
}
void ax_ui_panel(AxUiCanvas *canvas, float x, float y, float width, float height, AxUiColor fill, AxUiColor border)
{
    ax_ui_rect(canvas, x, y, width, height, border);
    ax_ui_rect(canvas, x + 1, y + 1, width - 2, height - 2, fill);
}
float ax_ui_text_width(const AxFont *font, const char *string, size_t bytes)
{
    float width = 0;
    const char *p = string;
    while (*p && (size_t)(p - string) < bytes) width += ax_font_glyph(font, &p)->advance * AX_FONT_DRAW_SCALE;
    return width;
}
float ax_ui_text_scaled(AxUiCanvas *canvas, const char *string, size_t bytes, float x, float baseline, float right, float scale, AxUiColor color)
{
    const char *p = string;
    right = fminf(right, canvas->width);
    while (*p && (size_t)(p - string) < bytes) {
        const AxGlyph *g = ax_font_glyph(canvas->font, &p);
        if (x + g->advance * scale > right) break;
        quad(canvas, floorf(x + g->x0 * scale), floorf(baseline + g->y0 * scale),
            floorf(x + g->x1 * scale), floorf(baseline + g->y1 * scale),
            g->u0, g->v0, g->u1, g->v1, color);
        x += g->advance * scale;
    }
    return x;
}

float ax_ui_text(AxUiCanvas *canvas, const char *string, size_t bytes, float x, float baseline, float right, AxUiColor color)
{
    return ax_ui_text_scaled(canvas, string, bytes, x, baseline, right, AX_FONT_DRAW_SCALE, color);
}
void ax_ui_clip(AxUiCanvas *canvas, float x, float y, float width, float height)
{
    canvas->clipped = true;
    canvas->clip_left = fmaxf(0, x); canvas->clip_top = fmaxf(0, y);
    canvas->clip_right = fminf(canvas->width, x + fmaxf(0, width));
    canvas->clip_bottom = fminf(canvas->height, y + fmaxf(0, height));
}
void ax_ui_reset_clip(AxUiCanvas *canvas) { canvas->clipped = false; }
