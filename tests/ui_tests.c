#include "ui/hud.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return EXIT_FAILURE; } } while (0)
/** Provides a registered command for suggestion layout tests. */
static bool command(int argc, const char *const *argv, void *user) { (void)argc; (void)argv; (void)user; return true; }
/** Verifies pixel font baking, clipped HUD layouts, selection/capture, and vertex backpressure. */
int main(void)
{
    AxFont font;
    unsigned char *bitmap = calloc(AX_FONT_ATLAS * AX_FONT_ATLAS, 1);
    CHECK(bitmap);
    CHECK(ax_font_bake(&font, bitmap));
    free(bitmap);
    AxUiVertex *vertices = calloc(65536, sizeof(*vertices));
    CHECK(vertices);
    AxEvents events = {0};
    AxInput input = {.events = &events};
    AxCommands commands = {0};
    CHECK(ax_command_register(&commands, "core.help", command, NULL));
    AxConsole console;
    CHECK(ax_console_init(&console, &commands, &input, &events));
    AxHud hud = {0};
    AxConfig settings;
    ax_config_init(&settings, &events);
    ax_menu_bind(&hud.menu, &settings, NULL, &commands, NULL, 0);
    CHECK(ax_hud_configure(&hud, 2, 3));
    ax_input_key(&input, AX_KEY_9, true, false, 0);
    ax_hud_update(&hud, &input, &console, 0, 1280, 720);
    CHECK(hud.selected_slots[hud.active_row] == 8);
    ax_input_begin_frame(&input);
    input.scroll_y = -1;
    ax_hud_update(&hud, &input, &console, 0, 1280, 720);
    CHECK(hud.selected_slots[hud.active_row] == 0);
    input.scroll_y = 1;
    ax_hud_update(&hud, &input, &console, 0, 1280, 720);
    CHECK(hud.selected_slots[hud.active_row] == 8);
    input.scroll_y = 9;
    ax_hud_update(&hud, &input, &console, 0, 1280, 720);
    CHECK(hud.selected_slots[hud.active_row] == 8);
    input.scroll_y = 0.25;
    ax_hud_update(&hud, &input, &console, 0, 1280, 720);
    CHECK(hud.selected_slots[hud.active_row] == 8);
    input.scroll_y = 0.75;
    ax_hud_update(&hud, &input, &console, 0, 1280, 720);
    CHECK(hud.selected_slots[hud.active_row] == 7);
    ax_console_open(&console, true);
    ax_hud_update(&hud, &input, &console, 0, 1280, 720);
    CHECK(hud.selected_slots[hud.active_row] == 7);
    ax_console_print(&console, "Command feedback");
    ax_hud_update(&hud, &input, &console, 1, 1280, 720);
    CHECK(hud.output_revision == console.output_revision && hud.output_time == 1);
    ax_console_open(&console, false);
    input.scroll_y = 0;
    ax_input_begin_frame(&input);
    ax_input_key(&input, AX_KEY_TAB, true, false, 0);
    ax_hud_update(&hud, &input, &console, 2, 1280, 720);
    CHECK(hud.active_row == 1 && hud.selected_slots[hud.active_row] == 0);
    ax_input_key(&input, AX_KEY_TAB, false, false, 0);
    ax_input_key(&input, AX_KEY_3, true, false, 0);
    ax_hud_update(&hud, &input, &console, 2, 1280, 720);
    CHECK(hud.selected_slots[1] == 2 && hud.selected_slots[0] == 7);
    ax_input_begin_frame(&input);
    ax_input_key(&input, AX_KEY_TAB, true, false, 0);
    ax_hud_update(&hud, &input, &console, 2, 1280, 720);
    CHECK(hud.active_row == 2);
    ax_input_key(&input, AX_KEY_TAB, false, false, 0);
    ax_input_begin_frame(&input);
    ax_input_key(&input, AX_KEY_TAB, true, false, 0);
    ax_hud_update(&hud, &input, &console, 2, 1280, 720);
    CHECK(hud.active_row == 0 && hud.selected_slots[0] == 7);
    CHECK(!ax_hud_configure(&hud, 0, 3));
    CHECK(!ax_hud_configure(&hud, 6, 3));
    CHECK(!ax_hud_configure(&hud, 2, 0));
    CHECK(!ax_hud_configure(&hud, 2, 10));
    CHECK(ax_hud_configure(&hud, 5, 1));
    CHECK(hud.active_row == 0 && ax_hud_scale(&hud) == 2.0f);
    CHECK(ax_hud_configure(&hud, 2, 3));
    ax_input_begin_frame(&input);
    input.mouse_x = 30; input.mouse_y = 680;
    ax_input_mouse_button(&input, 0, true);
    ax_hud_update(&hud, &input, &console, 2, 1280, 720);
    CHECK(console.open && input.captured);
    ax_input_begin_frame(&input);
    ax_input_key(&input, AX_KEY_ESCAPE, true, false, 0);
    ax_hud_update(&hud, &input, &console, 2, 1280, 720);
    CHECK(!console.open && !hud.paused && !input.captured);
    ax_input_key(&input, AX_KEY_ESCAPE, false, false, 0);
    ax_input_begin_frame(&input);
    input.mouse_x = 60;
    ax_input_mouse_button(&input, 0, true);
    ax_hud_update(&hud, &input, &console, 2, 1280, 720);
    CHECK(hud.paused && input.captured);
    ax_input_begin_frame(&input);
    ax_input_key(&input, AX_KEY_ESCAPE, true, false, 0);
    ax_hud_update(&hud, &input, &console, 2, 1280, 720);
    CHECK(!hud.paused && !input.captured);
    const float sizes[][2] = {{1280,720},{800,600},{320,200},{64,64}};
    for (int level = 1; level <= 5; ++level) {
        CHECK(ax_hud_configure(&hud, level, 9));
        float scale = ax_hud_scale(&hud);
        for (size_t i = 0; i < 4; ++i) for (int open = 0; open < 2; ++open) {
            ax_console_open(&console, open != 0);
            AxUiCanvas canvas = {vertices, 0, 65536, &font, sizes[i][0] / scale, sizes[i][1] / scale};
            ax_hud_draw(&hud, &canvas, &console, 2);
            CHECK(canvas.count && canvas.count <= canvas.capacity && canvas.count % 6 == 0);
            for (uint32_t v = 0; v < canvas.count; ++v) {
                CHECK(isfinite(vertices[v].x) && isfinite(vertices[v].y));
                CHECK(vertices[v].x >= 0 && vertices[v].x <= canvas.width);
                CHECK(vertices[v].y >= 0 && vertices[v].y <= canvas.height);
            }
        }
        ax_console_open(&console, false);
        ax_input_begin_frame(&input);
        float width = 1280 / scale, height = 720 / scale;
        float margin = fminf(20, fmaxf(4, width / 32));
        input.mouse_x = (margin + 14) * scale;
        input.mouse_y = (height - margin - 14) * scale;
        ax_input_mouse_button(&input, 0, true);
        ax_hud_update(&hud, &input, &console, 2, 1280, 720);
        CHECK(console.open);
    }
    ax_console_open(&console, false);
    hud.paused = true;
    for (int level = 1; level <= 5; ++level) {
        CHECK(ax_hud_configure(&hud, level, 3));
        float scale = ax_hud_scale(&hud);
        for (size_t i = 0; i < 4; ++i) for (int page = 0; page < 4; ++page) {
            ax_menu_open_pause(&hud.menu);
            if (page) { CHECK(ax_menu_open_settings(&hud.menu)); hud.menu.category = (AxSettingsCategory)(page - 1); }
            AxUiCanvas canvas = {.vertices = vertices, .capacity = 65536, .font = &font, .width = sizes[i][0] / scale, .height = sizes[i][1] / scale};
            ax_hud_draw(&hud, &canvas, &console, 2);
            CHECK(canvas.count && canvas.count <= canvas.capacity && canvas.count % 6 == 0);
            for (uint32_t v = 0; v < canvas.count; ++v) {
                CHECK(isfinite(vertices[v].x) && isfinite(vertices[v].y));
                CHECK(vertices[v].x >= 0 && vertices[v].x <= canvas.width);
                CHECK(vertices[v].y >= 0 && vertices[v].y <= canvas.height);
            }
        }
    }
    AxUiCanvas limited = {vertices, 0, 5, &font, 1280, 720};
    ax_hud_draw(&hud, &limited, &console, 2);
    CHECK(limited.count == 0);
    ax_console_destroy(&console);
    free(vertices);
    return EXIT_SUCCESS;
}
