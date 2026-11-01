#include "core/command.h"
#include "core/config.h"
#include "core/jobs.h"
#include "core/files.h"
#include "core/log.h"
#include "renderer/renderer.h"
#include "ui/ui.h"
#include <stdio.h>
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    AxConfig config;
    AxWindow window;
    AxInput input;
    AxConsole console;
    AxCommands commands;
    AxRenderer renderer;
    AxUi ui;
    char settings_path[AX_PATH_CAPACITY];
} Application;
/** Applies validated global scale and hotbar rows to the current HUD. */
static bool configure_hud(Application *app)
{
    return ax_hud_configure(&app->ui.hud, ax_config_get(&app->config, "ui.scale")->value,
                            ax_config_get(&app->config, "ui.hotbar.rows")->value);
}
/** Applies live settings; restart-only settings remain pending until the next launch. */
static void config_changed(const AxEvent *event, void *user)
{
    Application *app = user;
    if (event->data.config.restart_required) {
        ax_log(AX_LOG_INFO, "%s changed; restart required", event->data.config.key);
    } else if (!strcmp(event->data.config.key, "log.level")) {
        ax_log_set_level((AxLogLevel)ax_config_get(&app->config, "log.level")->value);
    } else if (!strncmp(event->data.config.key, "ui.", 3)) {
        if (!configure_hud(app)) ax_log(AX_LOG_ERROR, "Invalid HUD configuration");
    } else if (app->window.handle && (!strcmp(event->data.config.key, "window.width") || !strcmp(event->data.config.key, "window.height"))) {
        glfwSetWindowSize(app->window.handle, ax_config_get(&app->config, "window.width")->value, ax_config_get(&app->config, "window.height")->value);
    }
}
/** Logs the successfully selected device after logical device creation. */
static void device_selected(const AxEvent *event, void *user)
{
    (void)user;
    ax_log(AX_LOG_INFO, "Selected GPU %u: %s", event->data.device.index, event->data.device.name);
}
/** Writes command results to the console and logger. */
static void output(Application *app, const char *message)
{
    ax_console_print(&app->console, message);
    ax_log(AX_LOG_INFO, "%s", message);
}
/** Dispatches dotted setting access and engine commands with explicit argument validation. */
static bool command(int argc, const char *const *argv, void *user)
{
    Application *app = user;
    const AxSetting *setting = ax_config_get(&app->config, argv[0]);
    if (setting) {
        if (argc == 2 && !ax_config_set(&app->config, argv[0], argv[1])) return false;
        if (argc != 1 && argc != 2) return false;
        char result[256];
        snprintf(result, sizeof(result), "%s = %d%s", setting->key, setting->value,
                 setting->restart_required && app->config.restart_pending ? " (restart pending)" : "");
        output(app, result);
        return true;
    }
    if (!strcmp(argv[0], "core.help") && argc == 1) {
        output(app, "name [value] reads/writes settings. Quotes and backslash escapes supported.");
        for (size_t i = 0; i < app->commands.count; ++i) output(app, app->commands.entries[i].name);
        return true;
    }
    if (!strcmp(argv[0], "core.config.save") && argc == 2) return ax_config_save(&app->config, argv[1]);
    if (!strcmp(argv[0], "core.config.load") && argc == 2) return ax_config_load(&app->config, argv[1]);
    if (!strcmp(argv[0], "core.quit") && argc == 1 && app->window.handle) {
        glfwSetWindowShouldClose(app->window.handle, GLFW_TRUE);
        return true;
    }
    if (!strcmp(argv[0], "renderer.nodes") && argc == 1) {
        for (size_t i = 0; i < app->renderer.node_count; ++i) output(app, app->renderer.nodes[i].name);
        output(app, "ui (always last)");
        return true;
    }
    if (!strcmp(argv[0], "renderer.node.enable") && argc == 3) {
        if (strcmp(argv[2], "true") && strcmp(argv[2], "false")) return false;
        return ax_renderer_enable_node(&app->renderer, argv[1], !strcmp(argv[2], "true"));
    }
    return false;
}
/** Starts core services and renderer; accepts --config path, --command text, and --smoke-test. */
int main(int argc, char **argv)
{
    AxEvents events = {0};
    Application app = {0};
    AxJobs *jobs = NULL;
    int status = EXIT_FAILURE;
    bool smoke = false, custom_config = false;
    ax_config_init(&app.config, &events);
    if (!ax_settings_path(app.settings_path, sizeof(app.settings_path))) ax_log(AX_LOG_WARN, "User settings directory is unavailable");
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--smoke-test")) smoke = true;
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) {
            const char *path = argv[++i];
            if (strlen(path) >= sizeof(app.settings_path) || !ax_config_load(&app.config, path)) { ax_log(AX_LOG_ERROR, "Invalid config: %s", path); goto cleanup; }
            strcpy(app.settings_path, path);
            custom_config = true;
        } else if (!strcmp(argv[i], "--command") && i + 1 < argc) ++i;
        else { ax_log(AX_LOG_ERROR, "Usage: axiovox [--config path] [--command text] [--smoke-test]"); goto cleanup; }
    }
    if (!custom_config && app.settings_path[0] && ax_file_exists(app.settings_path) && !ax_config_load(&app.config, app.settings_path))
        ax_log(AX_LOG_WARN, "Could not load user settings; using defaults");
    ax_log_set_level((AxLogLevel)ax_config_get(&app.config, "log.level")->value);
    if (!ax_event_subscribe(&events, AX_EVENT_CONFIG_CHANGED, config_changed, &app) ||
        !ax_event_subscribe(&events, AX_EVENT_DEVICE_SELECTED, device_selected, NULL)) goto cleanup;
    const char *names[] = {"core.help", "core.quit", "core.config.save", "core.config.load", "renderer.nodes", "renderer.node.enable"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!ax_command_register(&app.commands, names[i], command, &app)) goto cleanup;
    for (size_t i = 0; i < sizeof(app.config.settings) / sizeof(app.config.settings[0]); ++i)
        if (!ax_command_register(&app.commands, app.config.settings[i].key, command, &app)) goto cleanup;
    app.input.events = &events;
    if (!ax_console_init(&app.console, &app.commands, &app.input, &events)) goto cleanup;
    jobs = ax_jobs_create((size_t)ax_config_get(&app.config, "jobs.workers")->value);
    if (!jobs) { ax_log(AX_LOG_ERROR, "Worker pool creation failed"); goto cleanup; }
    if (!ax_window_create(&app.window, &events, ax_config_get(&app.config, "window.width")->value, ax_config_get(&app.config, "window.height")->value)) goto cleanup;
    ax_window_set_input(&app.window, &app.input);
    if (!ax_renderer_create(&app.renderer, &app.window, &events, ax_config_get(&app.config, "renderer.device")->value)) goto cleanup;
    if (!ax_ui_create(&app.ui, &app.renderer, &app.console)) { ax_log(AX_LOG_ERROR, "UI initialization failed"); goto cleanup; }
    if (!configure_hud(&app)) goto cleanup;
    ax_ui_bind_settings(&app.ui, &app.config, app.settings_path);
    ax_config_mark_applied(&app.config);
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--config")) ++i;
        else if (!strcmp(argv[i], "--command")) {
            if (!ax_command_execute(&app.commands, argv[++i])) { ax_log(AX_LOG_ERROR, "Command failed: %s", argv[i]); goto cleanup; }
        }
    }
    if (smoke) ax_console_open(&app.console, true);
    unsigned frames = 0;
    while (!glfwWindowShouldClose(app.window.handle)) {
        ax_input_begin_frame(&app.input);
        glfwPollEvents();
        if (smoke && frames == 2) {
            ax_console_open(&app.console, false);
            ax_input_key(&app.input, AX_KEY_ESCAPE, true, false, 0);
            ax_input_key(&app.input, AX_KEY_ESCAPE, false, false, 0);
        }
        if (smoke && frames == 4) ax_menu_open_settings(&app.ui.hud.menu);
        if (smoke && frames == 6) app.ui.hud.menu.category = AX_SETTINGS_VIDEO;
        ax_ui_update(&app.ui, glfwGetTime());
        if (!ax_renderer_draw(&app.renderer)) { ax_log(AX_LOG_ERROR, "Frame rendering failed"); goto cleanup; }
        if (smoke && ++frames == 8) break;
        int width, height;
        glfwGetFramebufferSize(app.window.handle, &width, &height);
        if (width <= 0 || height <= 0 || glfwGetWindowAttrib(app.window.handle, GLFW_ICONIFIED)) glfwWaitEventsTimeout(0.05);
    }
    status = EXIT_SUCCESS;
cleanup:
    ax_jobs_destroy(jobs);
    if (app.renderer.device) vkDeviceWaitIdle(app.renderer.device);
    ax_ui_destroy(&app.ui);
    ax_renderer_destroy(&app.renderer);
    ax_console_destroy(&app.console);
    ax_window_destroy(&app.window);
    return status;
}
