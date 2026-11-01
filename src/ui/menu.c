#include "menu.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct { float x, y, w, h; } Rect;
typedef struct { Rect panel, tabs[3], fields[2], footer[4]; float body_top, body_bottom; } SettingsLayout;
static const AxUiColor ink = {0.055f,0.067f,0.065f,0.98f}, border = {0.23f,0.28f,0.24f,1};
static const AxUiColor paper = {0.91f,0.92f,0.83f,1}, muted = {0.53f,0.60f,0.53f,1};
static const AxUiColor accent = {0.77f,0.88f,0.51f,1}, selected = {0.15f,0.21f,0.13f,1};
static const int resolutions[][2] = {{640,360},{960,540},{1280,720},{1600,900},{1920,1080},{2560,1440},{3840,2160}};
/** Tests a half-open logical-pixel rectangle. */
static bool contains(Rect rect, float x, float y) { return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h; }
/** Draws a bounded label with the game's normal pixel font. */
static void label(AxUiCanvas *canvas, const char *value, float x, float y, float right, AxUiColor color)
{
    ax_ui_text(canvas, value, strlen(value), x, y, right, color);
}
/** Centers text using a chosen multiple of the normal font size. */
static void centered(AxUiCanvas *canvas, const char *value, Rect rect, float multiple, AxUiColor color)
{
    float width = ax_ui_text_width(canvas->font, value, strlen(value)) * multiple;
    float x = fmaxf(rect.x + 4, floorf(rect.x + (rect.w - width) / 2));
    ax_ui_text_scaled(canvas, value, strlen(value), x, floorf(rect.y + rect.h / 2 + 4 * multiple), rect.x + rect.w - 4, AX_FONT_DRAW_SCALE * multiple, color);
}
/** Draws a normal or focused button; disabled buttons retain their labels. */
static void button(AxUiCanvas *canvas, Rect rect, const char *value, bool focused, bool pressed, bool enabled)
{
    ax_ui_panel(canvas, rect.x, rect.y, rect.w, rect.h, focused && enabled ? selected : ink, focused && enabled ? accent : border);
    if (pressed && enabled) ax_ui_rect(canvas, rect.x + 2, rect.y + 2, rect.w - 4, 2, accent);
    centered(canvas, value, rect, 1, enabled ? paper : muted);
}
/** Builds the pause button layout from available space. */
static Rect pause_panel(float width, float height)
{
    float w = fminf(332, fmaxf(24, width - 24)), h = fminf(300, fmaxf(24, height - 24));
    return (Rect){floorf((width - w) / 2), floorf((height - h) / 2), w, h};
}
/** Returns one of the three pause buttons. */
static Rect pause_button(Rect panel, int index)
{
    float start = fminf(106, panel.h * 0.36f);
    float height = fmaxf(4, fminf(42, (panel.h - start - 32) / 3));
    return (Rect){panel.x + 20, panel.y + start + (float)index * (height + 8), fmaxf(4, panel.w - 40), height};
}
/** Builds the settings sheet and its scrollable content geometry once for drawing/hit testing. */
static SettingsLayout settings_layout(float width, float height, float scroll)
{
    float w = fminf(720, fmaxf(24, width - 24)), h = fminf(360, fmaxf(24, height - 24));
    SettingsLayout layout = {.panel = {floorf((width - w) / 2), floorf((height - h) / 2), w, h}};
    Rect p = layout.panel;
    for (int i = 0; i < 3; ++i) layout.tabs[i] = (Rect){p.x + 16 + (float)i * (w - 32) / 3, p.y + 66, (w - 32) / 3, 30};
    layout.body_top = p.y + 108;
    layout.body_bottom = fmaxf(layout.body_top, p.y + h - 76);
    float controls = fminf(252, fmaxf(0, (w - 32) * 0.48f));
    for (int i = 0; i < 2; ++i) layout.fields[i] = (Rect){p.x + w - 16 - controls, layout.body_top + (float)i * 76 + 8 - scroll, controls, 32};
    float bw = fmaxf(2, (w - 32 - 18) / 4);
    for (int i = 0; i < 4; ++i) layout.footer[i] = (Rect){p.x + 16 + (float)i * (bw + 6), p.y + h - 46, bw, 30};
    return layout;
}
/** Finds a stepper arrow or center value under the pointer, encoded as an action id. */
static int field_hit(Rect rect, int index, float x, float y)
{
    if (!contains(rect, x, y)) return -1;
    float arrow = fminf(28, rect.w / 4);
    if (x < rect.x + arrow) return 100 + index * 2;
    if (x >= rect.x + rect.w - arrow) return 101 + index * 2;
    return 200 + index;
}
/** Maps an action id to a keyboard focus index. */
static int focus_for(int id) { return id >= 200 ? 3 + id - 200 : id >= 100 ? 3 + (id - 100) / 2 : id; }
void ax_menu_bind(AxMenu *menu, AxConfig *config, const char *path, AxCommands *commands, const AxDeviceInfo *devices, size_t count)
{
    *menu = (AxMenu){.config = config, .settings_path = path, .commands = commands, .devices = devices, .device_count = devices ? count : 0, .pressed_id = -1};
}
void ax_menu_open_pause(AxMenu *menu)
{
    menu->page = AX_MENU_PAUSE; menu->focus = 0; menu->pressed_id = -1; menu->scroll = 0; menu->status[0] = 0;
}
bool ax_menu_open_settings(AxMenu *menu)
{
    if (!menu->config) return false;
    menu->draft = *menu->config; menu->draft.events = NULL;
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i) menu->original_values[i] = menu->config->settings[i].value;
    menu->page = AX_MENU_SETTINGS; menu->category = AX_SETTINGS_INTERFACE;
    menu->focus = 3; menu->pressed_id = -1; menu->scroll = 0; menu->status[0] = 0;
    return true;
}
bool ax_menu_dirty(const AxMenu *menu)
{
    if (!menu->config || menu->page != AX_MENU_SETTINGS) return false;
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i)
        if (menu->draft.settings[i].value != menu->config->settings[i].value) return true;
    return false;
}
/** Merges runtime/console changes into untouched draft fields while preserving local edits. */
static void sync_config(AxMenu *menu)
{
    if (!menu->config || menu->page != AX_MENU_SETTINGS) return;
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i) {
        int actual = menu->config->settings[i].value;
        if (actual == menu->original_values[i]) continue;
        if (menu->draft.settings[i].value == menu->original_values[i]) menu->draft.settings[i].value = actual;
        menu->original_values[i] = actual;
        menu->status[0] = 0;
    }
}
bool ax_menu_apply(AxMenu *menu)
{
    if (!menu->config || menu->page != AX_MENU_SETTINGS) return false;
    sync_config(menu);
    AxConfig validated = *menu->config;
    validated.events = NULL;
    if (!ax_config_apply(&validated, &menu->draft)) { snprintf(menu->status, sizeof(menu->status), "Invalid settings."); return false; }
    if (!menu->settings_path || !ax_config_save(&validated, menu->settings_path)) {
        snprintf(menu->status, sizeof(menu->status), "Could not save settings. Changes were not applied.");
        return false;
    }
    if (!ax_config_apply(menu->config, &validated)) return false;
    menu->draft = *menu->config; menu->draft.events = NULL;
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i) menu->original_values[i] = menu->config->settings[i].value;
    snprintf(menu->status, sizeof(menu->status), "%s", menu->config->restart_pending ? "Changes saved. Restart required." : "Changes saved.");
    return true;
}
/** Writes one validated draft integer through the same API used by commands. */
static void set_value(AxMenu *menu, const char *key, int value)
{
    char text[32]; snprintf(text, sizeof(text), "%d", value);
    if (ax_config_set(&menu->draft, key, text)) menu->status[0] = 0;
}
/** Reads an existing draft setting. */
static int value(const AxMenu *menu, const char *key) { return ax_config_get(&menu->draft, key)->value; }
/** Cycles only compatible graphics devices, with Automatic (-1) as a valid choice. */
static int cycle_device(const AxMenu *menu, int current, int direction)
{
    if (direction > 0) {
        for (size_t i = 0; i < menu->device_count; ++i)
            if (menu->devices[i].compatible && menu->devices[i].index <= 65535 && (int)menu->devices[i].index > current) return (int)menu->devices[i].index;
    } else {
        for (size_t i = menu->device_count; i > 0; --i) {
            const AxDeviceInfo *device = &menu->devices[i - 1];
            if (device->compatible && device->index <= 65535 && (current < 0 || (int)device->index < current)) return (int)device->index;
        }
    }
    return -1;
}
/** Changes one currently visible control, retaining out-of-range/custom resolutions until edited. */
static void adjust(AxMenu *menu, int field, int direction)
{
    if (menu->category == AX_SETTINGS_VIDEO) {
        if (field == 1) { set_value(menu, "renderer.device", cycle_device(menu, value(menu, "renderer.device"), direction)); return; }
        int current = -1;
        for (int i = 0; i < 7; ++i) if (value(menu, "window.width") == resolutions[i][0] && value(menu, "window.height") == resolutions[i][1]) current = i;
        int next = current < 0 ? 2 : (current + direction + 7) % 7;
        set_value(menu, "window.width", resolutions[next][0]); set_value(menu, "window.height", resolutions[next][1]);
        return;
    }
    const char *key = menu->category == AX_SETTINGS_INTERFACE ? (field ? "ui.hotbar.rows" : "ui.scale") : (field ? "log.level" : "jobs.workers");
    const AxSetting *setting = ax_config_get(&menu->draft, key);
    int next = setting->value + direction;
    if (next < setting->minimum) next = setting->minimum;
    if (next > setting->maximum) next = setting->maximum;
    set_value(menu, key, next);
}
/** Activates a menu action without synthesizing input or executing shell text. */
static AxMenuAction activate(AxMenu *menu, int id)
{
    if (menu->page == AX_MENU_PAUSE) {
        if (id == 0) { menu->page = AX_MENU_NONE; return AX_MENU_RESUME; }
        if (id == 1) ax_menu_open_settings(menu);
        if (id == 2 && menu->commands) {
            const char *argv[] = {"core.quit"};
            if (!ax_command_invoke(menu->commands, 1, argv)) snprintf(menu->status, sizeof(menu->status), "Could not quit the game.");
        }
        return AX_MENU_STAY;
    }
    if (id < 0) return AX_MENU_STAY;
    if (id < 3) { menu->category = (AxSettingsCategory)id; menu->scroll = 0; menu->focus = id; }
    else if (id >= 100 && id < 104) adjust(menu, (id - 100) / 2, id % 2 ? 1 : -1);
    else if (id == 3 || id == 4) adjust(menu, id - 3, 1);
    else if (id == 5) {
        AxConfig defaults; ax_config_init(&defaults, NULL);
        for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i) set_value(menu, defaults.settings[i].key, defaults.settings[i].value);
    } else if (id == 6) ax_menu_open_pause(menu);
    else if (id == 7 && ax_menu_dirty(menu)) ax_menu_apply(menu);
    else if (id == 8 && (!ax_menu_dirty(menu) || ax_menu_apply(menu))) ax_menu_open_pause(menu);
    return AX_MENU_STAY;
}
/** Returns the current pointer action id, respecting the content clip. */
static int hit(const AxMenu *menu, float width, float height, float x, float y)
{
    if (!isfinite(x) || !isfinite(y) || x < 0 || y < 0 || x >= width || y >= height) return -1;
    if (menu->page == AX_MENU_PAUSE) {
        Rect panel = pause_panel(width, height);
        for (int i = 0; i < 3; ++i) if (contains(pause_button(panel, i), x, y)) return i;
        return -1;
    }
    SettingsLayout layout = settings_layout(width, height, menu->scroll);
    for (int i = 0; i < 3; ++i) if (contains(layout.tabs[i], x, y)) return i;
    for (int i = 0; i < 4; ++i) if (contains(layout.footer[i], x, y)) return 5 + i;
    if (y >= layout.body_top && y < layout.body_bottom) for (int i = 0; i < 2; ++i) {
        int id = field_hit(layout.fields[i], i, x, y); if (id >= 0) return id;
    }
    return -1;
}
/** Keeps a keyboard-focused setting inside the scrollable content viewport. */
static void reveal_focus(AxMenu *menu, float width, float height, bool reveal)
{
    SettingsLayout layout = settings_layout(width, height, 0);
    float available = fmaxf(0, layout.body_bottom - layout.body_top);
    if (reveal && (menu->focus == 3 || menu->focus == 4)) {
        float top = (float)(menu->focus - 3) * 76;
        if (top < menu->scroll) menu->scroll = top;
        if (top + 64 > menu->scroll + available) menu->scroll = top + 64 - available;
    }
    menu->scroll = fmaxf(0, fminf(menu->scroll, fmaxf(0, 152 - available)));
}
/** Reads unconsumed UI transitions, optionally including OS key repeat. */
static bool key_pressed(const AxInput *input, AxKey key, bool repeat)
{
    return !input->consumed[key] && (input->pressed[key] || (repeat && input->repeated[key]));
}
AxMenuAction ax_menu_update(AxMenu *menu, AxInput *input, float width, float height, float mouse_x, float mouse_y)
{
    if (menu->page == AX_MENU_NONE) return AX_MENU_STAY;
    if (input->focus_lost) { menu->pressed_id = -1; return AX_MENU_STAY; }
    sync_config(menu);
    if (key_pressed(input, AX_KEY_ESCAPE, false)) {
        input->consumed[AX_KEY_ESCAPE] = true;
        if (menu->page == AX_MENU_SETTINGS) { ax_menu_open_pause(menu); return AX_MENU_STAY; }
        menu->page = AX_MENU_NONE; return AX_MENU_RESUME;
    }
    int hovered = hit(menu, width, height, mouse_x, mouse_y);
    if (input->mouse_pressed[0]) {
        menu->pressed_id = hovered;
        if (hovered >= 0) menu->focus = focus_for(hovered);
        input->mouse_pressed[0] = false;
    }
    if (input->mouse_released[0]) {
        int pressed = menu->pressed_id; menu->pressed_id = -1;
        input->mouse_released[0] = false;
        if (pressed >= 0 && pressed == hovered) return activate(menu, pressed);
    }
    int count = menu->page == AX_MENU_PAUSE ? 3 : 9;
    int direction = 0;
    if (key_pressed(input, AX_KEY_UP, true) || key_pressed(input, AX_KEY_DOWN, true) || key_pressed(input, AX_KEY_TAB, false)) {
        if (key_pressed(input, AX_KEY_UP, true)) direction = -1;
        else if (key_pressed(input, AX_KEY_TAB, false) && (input->down[AX_KEY_LEFT_SHIFT] || input->down[AX_KEY_RIGHT_SHIFT])) direction = -1;
        else direction = 1;
        menu->focus = (menu->focus + direction + count) % count;
        input->consumed[AX_KEY_UP] = input->consumed[AX_KEY_DOWN] = input->consumed[AX_KEY_TAB] = true;
    }
    if (menu->page == AX_MENU_SETTINGS && (key_pressed(input, AX_KEY_LEFT, true) || key_pressed(input, AX_KEY_RIGHT, true))) {
        direction = key_pressed(input, AX_KEY_LEFT, true) ? -1 : 1;
        if (menu->focus < 3) {
            menu->category = (AxSettingsCategory)(((int)menu->category + direction + 3) % 3);
            menu->focus = (int)menu->category; menu->scroll = 0;
        } else if (menu->focus == 3 || menu->focus == 4) adjust(menu, menu->focus - 3, direction);
        input->consumed[AX_KEY_LEFT] = input->consumed[AX_KEY_RIGHT] = true;
    }
    if (key_pressed(input, AX_KEY_ENTER, false) || key_pressed(input, AX_KEY_SPACE, false)) {
        input->consumed[AX_KEY_ENTER] = input->consumed[AX_KEY_SPACE] = true;
        return activate(menu, menu->focus);
    }
    if (menu->page == AX_MENU_SETTINGS) {
        SettingsLayout layout = settings_layout(width, height, menu->scroll);
        Rect body = {layout.panel.x, layout.body_top, layout.panel.w, layout.body_bottom - layout.body_top};
        if (contains(body, mouse_x, mouse_y) && isfinite(input->scroll_y)) menu->scroll -= (float)(input->scroll_y * 24);
        reveal_focus(menu, width, height, direction != 0);
    }
    return AX_MENU_STAY;
}
/** Formats the two controls for the active category with human-readable values. */
static void field_text(const AxMenu *menu, int index, const char **name, const char **description, char *text, size_t capacity)
{
    static const char *levels[] = {"75%", "100%", "125%", "150%", "200%"};
    static const char *logs[] = {"Debug", "Info", "Warning", "Error"};
    if (menu->category == AX_SETTINGS_INTERFACE) {
        *name = index ? "Hotbar rows" : "UI scale";
        *description = index ? "Independent rows of nine slots" : "Scales the entire game interface";
        if (index) snprintf(text, capacity, "%d", value(menu, "ui.hotbar.rows"));
        else snprintf(text, capacity, "%s", levels[value(menu, "ui.scale") - 1]);
    } else if (menu->category == AX_SETTINGS_VIDEO) {
        *name = index ? "Graphics device" : "Window resolution";
        *description = index ? "Restart required" : "Applies without restarting";
        if (!index) snprintf(text, capacity, "%d x %d", value(menu, "window.width"), value(menu, "window.height"));
        else {
            int device = value(menu, "renderer.device"); const char *label = device < 0 ? "Automatic" : "Unavailable device";
            for (size_t i = 0; i < menu->device_count; ++i) if ((int)menu->devices[i].index == device) label = menu->devices[i].name;
            snprintf(text, capacity, "%s", label);
        }
    } else {
        *name = index ? "Log verbosity" : "Worker threads";
        *description = index ? "Controls diagnostic output" : "Restart required";
        if (index) snprintf(text, capacity, "%s", logs[value(menu, "log.level")]);
        else snprintf(text, capacity, "%d", value(menu, "jobs.workers"));
    }
}
void ax_menu_draw(const AxMenu *menu, AxUiCanvas *canvas, float mouse_x, float mouse_y)
{
    int hovered = hit(menu, canvas->width, canvas->height, mouse_x, mouse_y);
    if (menu->page == AX_MENU_PAUSE) {
        Rect panel = pause_panel(canvas->width, canvas->height);
        ax_ui_panel(canvas, panel.x, panel.y, panel.w, panel.h, ink, border);
        ax_ui_rect(canvas, panel.x + 1, panel.y + 1, panel.w - 2, 2, accent);
        centered(canvas, "AXIOVOX", (Rect){panel.x, panel.y + 30, panel.w, 40}, 3, paper);
        centered(canvas, "PAUSED", (Rect){panel.x, panel.y + 76, panel.w, 14}, 1, muted);
        const char *names[] = {"Resume", "Settings", "Quit game"};
        for (int i = 0; i < 3; ++i) button(canvas, pause_button(panel, i), names[i], menu->focus == i || hovered == i, menu->pressed_id == i, i != 1 || menu->config);
        return;
    }
    if (menu->page != AX_MENU_SETTINGS || !menu->config) return;
    SettingsLayout layout = settings_layout(canvas->width, canvas->height, menu->scroll);
    Rect panel = layout.panel;
    ax_ui_panel(canvas, panel.x, panel.y, panel.w, panel.h, ink, border);
    ax_ui_rect(canvas, panel.x + 1, panel.y + 1, panel.w - 2, 2, accent);
    ax_ui_text_scaled(canvas, "SETTINGS", 8, panel.x + 16, panel.y + 35, panel.x + panel.w - 16, AX_FONT_DRAW_SCALE * 2, paper);
    const char *status = menu->status[0] ? menu->status : ax_menu_dirty(menu) ? "Unsaved changes" : menu->config->restart_pending ? "Restart required" : "";
    label(canvas, status, panel.x + 16, panel.y + 53, panel.x + panel.w - 16, muted);
    const char *categories[] = {"Interface", "Video", "System"};
    for (int i = 0; i < 3; ++i) button(canvas, layout.tabs[i], categories[i], menu->focus == i || hovered == i || (int)menu->category == i, menu->pressed_id == i, true);
    ax_ui_clip(canvas, panel.x + 8, layout.body_top, panel.w - 16, layout.body_bottom - layout.body_top);
    for (int i = 0; i < 2; ++i) {
        Rect field = layout.fields[i]; const char *name, *description; char text[AX_DEVICE_NAME_CAPACITY];
        field_text(menu, i, &name, &description, text, sizeof(text));
        label(canvas, name, panel.x + 16, field.y + 16, field.x - 10, paper);
        label(canvas, description, panel.x + 16, field.y + 36, field.x - 10, muted);
        float arrow = fminf(28, field.w / 4);
        button(canvas, (Rect){field.x,field.y,arrow,field.h}, "-", menu->focus == 3 + i || hovered == 100 + i * 2, menu->pressed_id == 100 + i * 2, true);
        button(canvas, (Rect){field.x + field.w - arrow,field.y,arrow,field.h}, "+", menu->focus == 3 + i || hovered == 101 + i * 2, menu->pressed_id == 101 + i * 2, true);
        centered(canvas, text, (Rect){field.x + arrow + 4,field.y,field.w - 2 * arrow - 8,field.h}, 1, paper);
        ax_ui_rect(canvas, panel.x + 16, field.y + 58, panel.w - 32, 1, border);
    }
    ax_ui_reset_clip(canvas);
    float available = layout.body_bottom - layout.body_top;
    if (available > 0 && available < 152) {
        float thumb = fmaxf(8, available * available / 152);
        float y = layout.body_top + (available - thumb) * menu->scroll / (152 - available);
        ax_ui_rect(canvas, panel.x + panel.w - 6, y, 2, thumb, muted);
    }
    const char *buttons[] = {"Reset all", "Cancel", "Apply", "Done"};
    for (int i = 0; i < 4; ++i) button(canvas, layout.footer[i], buttons[i], menu->focus == i + 5 || hovered == i + 5, menu->pressed_id == i + 5, i != 2 || ax_menu_dirty(menu));
}
