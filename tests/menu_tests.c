#include "ui/menu.h"
#include "core/files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return EXIT_FAILURE; } } while (0)
/** Counts quit dispatches through the same argv route used by application commands. */
static bool quit(int argc, const char *const *argv, void *user) { (void)argv; ++*(int *)user; return argc == 1; }
/** Emits a keyboard press with frame-scoped transitions. */
static AxMenuAction key(AxMenu *menu, AxInput *input, AxKey key)
{
    ax_input_begin_frame(input);
    ax_input_key(input, key, true, false, 0);
    AxMenuAction action = ax_menu_update(menu, input, 1280, 720, 0, 0);
    ax_input_key(input, key, false, false, 0);
    return action;
}
/** Verifies that failed serialization never replaces a previously saved file. */
static bool fail_write(FILE *file, const void *user) { (void)user; fputs("partial write", file); return false; }
/** Exercises click release, drafts, persistence, live-setting metadata, GPU filtering, reset/cancel, and quit dispatch. */
int main(void)
{
    AxEvents events = {0}; AxConfig config; ax_config_init(&config, &events);
    AxCommands commands = {0}; int quit_count = 0;
    CHECK(ax_command_register(&commands, "core.quit", quit, &quit_count));
    AxInput input = {.events = &events};
    const AxDeviceInfo devices[] = {{0,false,"Unsupported"},{1,true,"First GPU"},{2,true,"Second GPU"}};
    AxMenu menu;
    ax_menu_bind(&menu, &config, "menu-test.cfg", &commands, devices, 3);
    ax_menu_open_pause(&menu);
    ax_input_mouse_button(&input, 0, true);
    CHECK(ax_menu_update(&menu, &input, 1280, 720, 640, 337) == AX_MENU_STAY);
    ax_input_begin_frame(&input);
    ax_input_mouse_button(&input, 0, false);
    CHECK(ax_menu_update(&menu, &input, 1280, 720, 10, 10) == AX_MENU_STAY);
    CHECK(menu.page == AX_MENU_PAUSE);
    ax_input_begin_frame(&input);
    ax_input_mouse_button(&input, 0, true);
    ax_menu_update(&menu, &input, 1280, 720, 640, 387);
    CHECK(menu.page == AX_MENU_PAUSE);
    ax_input_begin_frame(&input);
    ax_input_mouse_button(&input, 0, false);
    ax_menu_update(&menu, &input, 1280, 720, 640, 387);
    CHECK(menu.page == AX_MENU_SETTINGS && !ax_menu_dirty(&menu));
    key(&menu, &input, AX_KEY_RIGHT);
    CHECK(ax_config_get(&menu.draft, "ui.scale")->value == 3);
    CHECK(ax_config_get(&config, "ui.scale")->value == 2 && ax_menu_dirty(&menu));
    CHECK(ax_menu_apply(&menu));
    CHECK(!ax_menu_dirty(&menu) && ax_config_get(&config, "ui.scale")->value == 3);
    CHECK(ax_file_exists("menu-test.cfg"));
    CHECK(!ax_file_write_atomic("menu-test.cfg", fail_write, NULL));
    AxConfig loaded; ax_config_init(&loaded, NULL);
    CHECK(ax_config_load(&loaded, "menu-test.cfg"));
    CHECK(ax_config_get(&loaded, "ui.scale")->value == 3);
    key(&menu, &input, AX_KEY_DOWN);
    key(&menu, &input, AX_KEY_RIGHT);
    CHECK(ax_config_get(&menu.draft, "ui.hotbar.rows")->value == 4);
    key(&menu, &input, AX_KEY_ESCAPE);
    CHECK(menu.page == AX_MENU_PAUSE && ax_config_get(&config, "ui.hotbar.rows")->value == 3);
    CHECK(ax_menu_open_settings(&menu));
    menu.category = AX_SETTINGS_VIDEO; menu.focus = 4;
    key(&menu, &input, AX_KEY_RIGHT);
    CHECK(ax_config_get(&menu.draft, "renderer.device")->value == 1);
    key(&menu, &input, AX_KEY_RIGHT);
    CHECK(ax_config_get(&menu.draft, "renderer.device")->value == 2);
    key(&menu, &input, AX_KEY_RIGHT);
    CHECK(ax_config_get(&menu.draft, "renderer.device")->value == -1);
    key(&menu, &input, AX_KEY_LEFT);
    CHECK(ax_menu_apply(&menu) && !config.restart_pending);
    key(&menu, &input, AX_KEY_RIGHT);
    CHECK(ax_menu_apply(&menu) && !config.restart_pending);
    menu.focus = 3;
    key(&menu, &input, AX_KEY_RIGHT);
    CHECK(ax_config_get(&menu.draft, "window.width")->value == 1600);
    CHECK(ax_config_get(&menu.draft, "window.height")->value == 900);
    CHECK(ax_config_get(&config, "window.width")->value == 1280);
    CHECK(ax_menu_apply(&menu));
    menu.category = AX_SETTINGS_SYSTEM; menu.focus = 3;
    key(&menu, &input, AX_KEY_RIGHT);
    CHECK(ax_menu_apply(&menu) && !config.restart_pending);
    key(&menu, &input, AX_KEY_LEFT);
    CHECK(ax_menu_apply(&menu) && !config.restart_pending);
    menu.category = AX_SETTINGS_INTERFACE; menu.focus = 3;
    key(&menu, &input, AX_KEY_RIGHT);
    menu.settings_path = "";
    CHECK(!ax_menu_apply(&menu));
    CHECK(ax_config_get(&config, "ui.scale")->value == 3 && ax_menu_dirty(&menu));
    CHECK(strstr(menu.status, "not applied"));
    menu.settings_path = "menu-test.cfg";
    menu.focus = 5;
    key(&menu, &input, AX_KEY_ENTER);
    CHECK(ax_config_get(&menu.draft, "ui.scale")->value == 2);
    menu.focus = 6;
    key(&menu, &input, AX_KEY_ENTER);
    CHECK(menu.page == AX_MENU_PAUSE && ax_config_get(&config, "ui.scale")->value == 3);
    CHECK(ax_menu_open_settings(&menu));
    CHECK(ax_config_set(&config, "ui.hotbar.rows", "6"));
    key(&menu, &input, AX_KEY_RIGHT);
    CHECK(ax_config_get(&menu.draft, "ui.hotbar.rows")->value == 6);
    CHECK(ax_config_get(&menu.draft, "ui.scale")->value == 4);
    CHECK(ax_menu_apply(&menu));
    CHECK(ax_config_get(&config, "ui.hotbar.rows")->value == 6);
    CHECK(ax_config_get(&config, "ui.scale")->value == 4);
    ax_menu_open_pause(&menu);
    ax_input_begin_frame(&input);
    ax_input_key(&input, AX_KEY_ENTER, true, false, 0);
    input.consumed[AX_KEY_ENTER] = true;
    CHECK(ax_menu_update(&menu, &input, 1280, 720, 0, 0) == AX_MENU_STAY);
    CHECK(menu.page == AX_MENU_PAUSE);
    ax_input_key(&input, AX_KEY_ENTER, false, false, 0);
    ax_input_begin_frame(&input);
    ax_input_mouse_button(&input, 0, true);
    ax_menu_update(&menu, &input, 1280, 720, 640, 337);
    ax_input_focus(&input, false);
    CHECK(ax_menu_update(&menu, &input, 1280, 720, 640, 337) == AX_MENU_STAY && menu.page == AX_MENU_PAUSE);
    ax_input_begin_frame(&input);
    menu.focus = 2;
    key(&menu, &input, AX_KEY_ENTER);
    CHECK(quit_count == 1);
    CHECK(key(&menu, &input, AX_KEY_ESCAPE) == AX_MENU_RESUME && menu.page == AX_MENU_NONE);
    ax_config_mark_applied(&config);
    CHECK(!config.restart_pending);
    CHECK(remove("menu-test.cfg") == 0);
    return EXIT_SUCCESS;
}
