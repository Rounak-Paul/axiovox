#include "hud.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static const AxUiColor ink = {0.055f,0.067f,0.065f,0.97f};
static const AxUiColor border = {0.23f,0.28f,0.24f,0.9f};
static const AxUiColor paper = {0.91f,0.92f,0.83f,1};
static const AxUiColor muted = {0.53f,0.60f,0.53f,1};
static const AxUiColor accent = {0.77f,0.88f,0.51f,1};
static const AxUiColor selected = {0.15f,0.21f,0.13f,1};
/** Draws a short UI label with shared font metrics. */
static float label(AxUiCanvas *canvas, const char *string, float x, float y, float right, AxUiColor color)
{
    return ax_ui_text(canvas, string, strlen(string), x, y, right, color);
}
bool ax_hud_configure(AxHud *hud, int scale_level, int rows)
{
    if (!hud || scale_level < 1 || scale_level > 5 || rows < 1 || rows > 9) return false;
    hud->scale_level = (unsigned)scale_level;
    hud->row_count = (unsigned)rows;
    hud->active_row %= hud->row_count;
    return true;
}
float ax_hud_scale(const AxHud *hud)
{
    static const float scales[] = {0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
    return hud->scale_level >= 1 && hud->scale_level <= 5 ? scales[hud->scale_level - 1] : 1.0f;
}
/** Returns the hotbar slot size for the logical viewport width and outer margin. */
static float hotbar_slot(float width, float margin)
{
    return floorf(fminf(26, (width - 2 * (margin + 72) - 26) / 9));
}
/** Matches control edge spacing to the hotbar panel's bottom gap in logical pixels. */
static float control_margin(float width, float height)
{
    float margin = fminf(20, fmaxf(4, width / 32));
    float slot = hotbar_slot(width, margin);
    return width >= 240 && height >= 140 && slot >= 6 ? margin + 18 - slot : margin;
}
/** Returns which bottom-left icon contains the pointer, or -1 outside both icons. */
static int hit_icon(float x, float y, float width, float height)
{
    float margin = control_margin(width, height);
    if (y < height - margin - 28 || y >= height - margin || x < margin) return -1;
    float offset = x - margin;
    if (offset < 28) return 0;
    if (offset >= 34 && offset < 62) return 1;
    return -1;
}
/** Publishes pause transitions for future simulation consumers. */
static void toggle_pause(AxHud *hud, AxInput *input)
{
    hud->paused = !hud->paused;
    if (hud->paused) ax_menu_open_pause(&hud->menu);
    else hud->menu.page = AX_MENU_NONE;
    AxEvent event = {.type = AX_EVENT_PAUSE_CHANGED, .data.pause = {hud->paused}};
    ax_event_emit(input->events, &event);
}
void ax_hud_update(AxHud *hud, AxInput *input, AxConsole *console, double time, float width, float height)
{
    if (console->output_revision != hud->output_revision) {
        hud->output_revision = console->output_revision;
        hud->output_time = time;
    }
    float scale = ax_hud_scale(hud);
    if (hud->paused && console->open) hud->menu.pressed_id = -1;
    bool menu_input = hud->paused && !console->open;
    if (menu_input && ax_menu_update(&hud->menu, input, width / scale, height / scale,
                                    (float)input->mouse_x / scale, (float)input->mouse_y / scale) == AX_MENU_RESUME)
        toggle_pause(hud, input);
    if (!menu_input && input->mouse_pressed[0]) {
        int icon = hit_icon((float)input->mouse_x / scale, (float)input->mouse_y / scale, width / scale, height / scale);
        if (icon == 0) ax_console_open(console, !console->open);
        if (icon == 1) toggle_pause(hud, input);
        if (icon >= 0) { input->mouse_pressed[0] = false; input->mouse_down[0] = false; }
    }
    if (!console->open && input->pressed[AX_KEY_ESCAPE] && !input->consumed[AX_KEY_ESCAPE]) {
        toggle_pause(hud, input);
        input->consumed[AX_KEY_ESCAPE] = true;
    }
    input->captured = console->open || hud->paused;
    if (input->captured) { hud->scroll_remainder = 0; return; }
    if (hud->row_count && ax_input_pressed(input, AX_KEY_TAB)) {
        bool backwards = ax_input_down(input, AX_KEY_LEFT_SHIFT) || ax_input_down(input, AX_KEY_RIGHT_SHIFT);
        hud->active_row = (hud->active_row + (backwards ? hud->row_count - 1 : 1)) % hud->row_count;
        input->consumed[AX_KEY_TAB] = true;
    }
    unsigned *slot = &hud->selected_slots[hud->active_row];
    for (unsigned i = 0; i < 9; ++i)
        if (ax_input_pressed(input, (AxKey)(AX_KEY_1 + i))) *slot = i;
    if (isfinite(input->scroll_y) && input->scroll_y != 0) {
        hud->scroll_remainder += fmod(input->scroll_y, 9.0);
        int steps = (int)hud->scroll_remainder;
        hud->scroll_remainder -= steps;
        *slot = (unsigned)(((int)*slot - steps + 9) % 9);
    }
}
/** Draws a compact active hotbar row with unobtrusive row markers below it. */
static void hotbar(const AxHud *hud, AxUiCanvas *canvas, float margin)
{
    if (canvas->width < 240 || canvas->height < 140) return;
    float slot = hotbar_slot(canvas->width, margin);
    if (slot < 6) return;
    float width = slot * 9 + 16 + 10, x = floorf((canvas->width - width) / 2);
    float y = canvas->height - margin - 28;
    ax_ui_panel(canvas, x, y, width, slot + 10, ink, border);
    for (unsigned i = 0; i < 9; ++i) {
        float sx = x + 5 + (float)i * (slot + 2);
        bool active = i == hud->selected_slots[hud->active_row];
        ax_ui_panel(canvas, sx, y + 5, slot, slot, active ? selected : ink, active ? accent : border);
        if (active) ax_ui_rect(canvas, sx, y + 5, slot, 1, accent);
    }
    if (hud->row_count > 1) {
        float start = floorf((canvas->width - (float)hud->row_count * 9 + 3) / 2);
        for (unsigned row = 0; row < hud->row_count; ++row)
            ax_ui_rect(canvas, start + (float)row * 9, y + slot + 13, 6, 2, row == hud->active_row ? accent : border);
    }
}
/** Draws the two icon-only bottom-left controls and their hover/active states. */
static void controls(AxUiCanvas *canvas, const AxHud *hud, const AxConsole *console, float margin)
{
    margin = control_margin(canvas->width, canvas->height);
    float x = margin, y = canvas->height - margin - 28;
    const AxInput *input = console->input;
    float scale = ax_hud_scale(hud);
    int hover = hit_icon((float)input->mouse_x / scale, (float)input->mouse_y / scale, canvas->width, canvas->height);
    ax_ui_panel(canvas, x, y, 28, 28, console->open || hover == 0 ? selected : ink, console->open ? accent : border);
    label(canvas, "/", x + 11, y + 19, x + 24, console->open ? accent : paper);
    x += 34;
    ax_ui_panel(canvas, x, y, 28, 28, hud->paused || hover == 1 ? selected : ink, hud->paused ? accent : border);
    ax_ui_rect(canvas, x + 9, y + 8, 3, 12, hud->paused ? accent : paper);
    ax_ui_rect(canvas, x + 16, y + 8, 3, 12, hud->paused ? accent : paper);
}
/** Draws recent command feedback against individual translucent backplates. */
static void feedback(AxUiCanvas *canvas, const AxConsole *console, float x, float bottom, float width, size_t limit, float alpha)
{
    size_t rows = console->output_count < limit ? console->output_count : limit;
    size_t available = bottom > 52 ? (size_t)((bottom - 52) / 22) : 0;
    if (rows > available) rows = available;
    for (size_t i = 0; i < rows; ++i) {
        const char *line = console->output[console->output_count - rows + i];
        float y = bottom - (float)(rows - i) * 22;
        float length = fminf(width, ax_ui_text_width(canvas->font, line, strlen(line)) + 20);
        AxUiColor fill = ink, color = paper;
        fill.a *= alpha * 0.9f; color.a *= alpha;
        ax_ui_rect(canvas, x, y, length, 20, fill);
        label(canvas, line, x + 10, y + 14, x + length - 8, color);
    }
}
/** Draws the compact suggestion popup, returns the top available for command feedback. */
static float suggestions(AxUiCanvas *canvas, const AxConsole *console, float x, float bottom, float available_width)
{
    if (!console->suggestion_count) return bottom;
    float width = fminf(available_width, 440);
    size_t rows = bottom > 112 ? (size_t)((bottom - 112) / 28) : 0;
    if (rows > 8) rows = 8;
    if (rows > console->suggestion_count) rows = console->suggestion_count;
    if (!rows) return bottom;
    size_t first = console->selected_suggestion >= rows ? console->selected_suggestion - rows + 1 : 0;
    float height = (float)rows * 28 + 36, top = bottom - height;
    ax_ui_rect(canvas, x + 3, top + 3, width, height, (AxUiColor){0,0,0,0.22f});
    ax_ui_panel(canvas, x, top, width, height, ink, border);
    label(canvas, "COMMANDS", x + 14, top + 20, x + width - 76, muted);
    char position[32];
    snprintf(position, sizeof(position), "%zu / %zu", console->selected_suggestion + 1, console->suggestion_count);
    float position_width = ax_ui_text_width(canvas->font, position, strlen(position));
    label(canvas, position, x + width - position_width - 14, top + 20, x + width - 10, muted);
    ax_ui_rect(canvas, x + 1, top + 30, width - 2, 1, border);
    for (size_t i = 0; i < rows; ++i) {
        size_t index = first + i;
        float y = top + 32 + (float)i * 28;
        bool active = index == console->selected_suggestion;
        if (active) {
            ax_ui_rect(canvas, x + 4, y, width - 8, 27, selected);
            ax_ui_rect(canvas, x + 4, y, 2, 27, accent);
            label(canvas, ">", x + 14, y + 18, x + width - 14, accent);
        }
        label(canvas, console->suggestions[index]->name, x + 32, y + 18, x + width - 20, active ? accent : paper);
    }
    if (rows < console->suggestion_count) {
        float track = (float)rows * 28;
        float thumb = fmaxf(8, track * (float)rows / (float)console->suggestion_count);
        float y = top + 32 + (track - thumb) * (float)first / (float)(console->suggestion_count - rows);
        ax_ui_rect(canvas, x + width - 5, y, 2, thumb, muted);
    }
    return top - 10;
}
/** Draws the command field with horizontal cursor scrolling. */
static void command_overlay(AxUiCanvas *canvas, const AxConsole *console, float margin, double time)
{
    float x = margin, width = canvas->width - 2 * margin;
    float y = canvas->height - margin - 82;
    float output_bottom = suggestions(canvas, console, x, y - 10, width);
    bool unknown = !console->suggestion_count && !strchr(console->line, ' ') && console->length > 1;
    if (unknown) output_bottom -= 24;
    feedback(canvas, console, x, output_bottom, fminf(width, 640), 4, 1);
    ax_ui_panel(canvas, x, y, width, 44, ink, border);
    ax_ui_rect(canvas, x, y, 2, 44, accent);
    float right = x + width - 14;
    size_t start = 0;
    float cursor = ax_ui_text_width(canvas->font, console->line, console->cursor);
    while (cursor > fmaxf(0, right - x - 24) && start < console->cursor) {
        const char *p = console->line + start;
        cursor -= ax_font_glyph(canvas->font, &p)->advance * AX_FONT_DRAW_SCALE;
        start = (size_t)(p - console->line);
    }
    ax_ui_text(canvas, console->line + start, console->length - start, x + 14, y + 27, right, paper);
    if (fmod(time, 1.0) < 0.65)
        ax_ui_rect(canvas, x + 14 + cursor, y + 13, 1, 17, accent);
    if (unknown)
        label(canvas, "No matching command", x + 14, y - 20, x + width - 14, muted);
}
void ax_hud_draw(const AxHud *hud, AxUiCanvas *canvas, const AxConsole *console, double time)
{
    float margin = fminf(20, fmaxf(4, canvas->width / 32));
    if (hud->paused) {
        ax_ui_rect(canvas, 0, 0, canvas->width, canvas->height, (AxUiColor){0,0,0,0.35f});
        if (!console->open) {
            float scale = ax_hud_scale(hud);
            ax_menu_draw(&hud->menu, canvas, (float)console->input->mouse_x / scale, (float)console->input->mouse_y / scale);
        }
    }
    if (console->open) command_overlay(canvas, console, margin, time);
    else if (!hud->paused) {
        float cx = floorf(canvas->width / 2), cy = floorf(canvas->height / 2);
        AxUiColor shadow = {0,0,0,0.65f};
        ax_ui_rect(canvas, cx - 1, cy - 6, 3, 13, shadow);
        ax_ui_rect(canvas, cx - 6, cy - 1, 13, 3, shadow);
        ax_ui_rect(canvas, cx, cy - 5, 1, 11, paper);
        ax_ui_rect(canvas, cx - 5, cy, 11, 1, paper);
        hotbar(hud, canvas, margin);
        double age = time - hud->output_time;
        if (hud->output_revision && age < 8.0) {
            float alpha = age > 6.0 ? (float)((8.0 - age) / 2.0) : 1;
            feedback(canvas, console, margin, canvas->height - margin - 68, fminf(640, canvas->width - margin * 2), 3, alpha);
        }
    }
    if (!hud->paused || console->open) controls(canvas, hud, console, margin);
}
