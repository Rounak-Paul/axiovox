#pragma once
#include "draw.h"
#include "console.h"
#include "menu.h"
typedef struct {
    unsigned selected_slots[9], active_row, row_count, scale_level;
    bool paused;
    AxMenu menu;
    uint64_t output_revision;
    double output_time, scroll_remainder;
} AxHud;
/** Updates controls, pause, hotbar, and feedback; width/height are logical window pixels and time is monotonic seconds. */
void ax_hud_update(AxHud *hud, AxInput *input, AxConsole *console, double time, float width, float height);
/** Draws the voxel-game HUD or command overlay; requires no GPU calls or inventory state. */
void ax_hud_draw(const AxHud *hud, AxUiCanvas *canvas, const AxConsole *console, double time);

/** Applies UI scale level (1-5) and row count (1-9); preserves valid selections and wraps removed rows. */
bool ax_hud_configure(AxHud *hud, int scale_level, int rows);
/** Returns the global UI multiplier for a configured HUD (0.75x through 2x). */
float ax_hud_scale(const AxHud *hud);
