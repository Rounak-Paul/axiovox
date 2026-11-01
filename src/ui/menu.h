#pragma once
#include "core/config.h"
#include "core/input.h"
#include "core/command.h"
#include "draw.h"
#include "renderer/device_info.h"
typedef enum { AX_MENU_NONE, AX_MENU_PAUSE, AX_MENU_SETTINGS } AxMenuPage;
typedef enum { AX_MENU_STAY, AX_MENU_RESUME } AxMenuAction;
typedef enum { AX_SETTINGS_INTERFACE, AX_SETTINGS_VIDEO, AX_SETTINGS_SYSTEM } AxSettingsCategory;
typedef struct {
    AxMenuPage page;
    AxSettingsCategory category;
    AxConfig *config;
    AxConfig draft;
    int original_values[AX_CONFIG_SETTING_COUNT];
    AxCommands *commands;
    const char *settings_path;
    const AxDeviceInfo *devices;
    size_t device_count;
    int focus, pressed_id;
    float scroll;
    char status[160];
} AxMenu;
/** Binds borrowed configuration/path/device data; all bound objects must outlive the menu. */
void ax_menu_bind(AxMenu *menu, AxConfig *config, const char *path, AxCommands *commands, const AxDeviceInfo *devices, size_t device_count);
/** Opens the pause menu, resetting focus without changing the game's pause state. */
void ax_menu_open_pause(AxMenu *menu);
/** Starts an isolated settings draft from current desired values. */
bool ax_menu_open_settings(AxMenu *menu);
/** Returns whether the draft differs from current desired settings. */
bool ax_menu_dirty(const AxMenu *menu);
/** Saves the draft atomically before applying validated changes; failure keeps the menu and draft intact. */
bool ax_menu_apply(AxMenu *menu);
/** Processes pointer and keyboard UI input in already-scaled logical coordinates. */
AxMenuAction ax_menu_update(AxMenu *menu, AxInput *input, float width, float height, float mouse_x, float mouse_y);
/** Draws the current menu into a scaled canvas, including hover/focus/pressed/disabled states. */
void ax_menu_draw(const AxMenu *menu, AxUiCanvas *canvas, float mouse_x, float mouse_y);
