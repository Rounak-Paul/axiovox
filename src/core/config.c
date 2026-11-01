#include "config.h"
#include "log.h"
#include "files.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void ax_config_init(AxConfig *config, AxEvents *events)
{
    *config = (AxConfig){.settings = {
        {"window.width", 1280, 64, 16384, false},
        {"window.height", 720, 64, 16384, false},
        {"renderer.device", -1, -1, 65535, false},
        {"jobs.workers", 4, 1, 64, false},
        {"log.level", AX_LOG_INFO, AX_LOG_DEBUG, AX_LOG_ERROR, false},
        {"ui.scale", 2, 1, 5, false},
        {"ui.hotbar.rows", 3, 1, 9, false}}, .events = events};
    ax_config_mark_applied(config);
}
const AxSetting *ax_config_get(const AxConfig *config, const char *key)
{
    if (!config || !key) return NULL;
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i) if (!strcmp(config->settings[i].key, key)) return &config->settings[i];
    return NULL;
}
/** Recomputes pending restart from desired values against startup-applied service values. */
static void update_restart(AxConfig *config)
{
    config->restart_pending = false;
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i)
        if (config->settings[i].restart_required && config->settings[i].value != config->active_values[i]) config->restart_pending = true;
}
void ax_config_mark_applied(AxConfig *config)
{
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i) config->active_values[i] = config->settings[i].value;
    config->restart_pending = false;
}
void ax_config_stage(AxConfig *draft, const AxConfig *config)
{
    *draft = *config;
    draft->events = NULL; draft->runtime = (AxConfigRuntime){0}; draft->runtime_user = NULL;
    draft->applying = false; draft->error[0] = 0;
}
bool ax_config_bind_runtime(AxConfig *config, AxConfigRuntime runtime, void *user)
{
    if (!config || config->applying || !runtime.prepare || !runtime.activate || !runtime.finish) return false;
    config->runtime = runtime; config->runtime_user = user;
    return true;
}
const char *ax_config_error(const AxConfig *config) { return config ? config->error : "Missing configuration"; }
bool ax_config_set(AxConfig *config, const char *key, const char *value)
{
    if (!config || config->applying) return false;
    config->error[0] = 0;
    const AxSetting *found = ax_config_get(config, key);
    if (!found || !value || !*value) {
        snprintf(config->error, sizeof(config->error), "Unknown setting or invalid value."); return false;
    }
    char *end; errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno || *end || parsed < found->minimum || parsed > found->maximum) {
        snprintf(config->error, sizeof(config->error), "%s requires an integer from %d to %d.", key, found->minimum, found->maximum);
        return false;
    }
    AxConfig draft; ax_config_stage(&draft, config);
    draft.settings[found - config->settings].value = (int)parsed;
    return ax_config_apply(config, &draft);
}
bool ax_config_load(AxConfig *config, const char *path)
{
    if (!config || !path) return false;
    FILE *file = ax_file_open(path, "r");
    if (!file) return false;
    AxConfig staged; ax_config_stage(&staged, config);
    char line[256];
    bool valid = true;
    while (fgets(line, sizeof(line), file)) {
        if (!strchr(line, '\n') && !feof(file)) { valid = false; break; }
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        char *equal = strchr(line, '=');
        if (!equal) { valid = false; break; }
        *equal++ = 0;
        if (!ax_config_set(&staged, line, equal)) { valid = false; break; }
    }
    if (ferror(file)) valid = false;
    if (fclose(file)) valid = false;
    if (!valid) return false;
    return ax_config_apply(config, &staged);
}
/** Coordinates runtime rollback and durable saving before publishing a coherent config snapshot. */
static bool apply(AxConfig *config, const AxConfig *values, const char *path)
{
    if (!config || !values || config->applying) return false;
    config->error[0] = 0;
    AxConfig staged; ax_config_stage(&staged, config);
    bool changed = false;
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i) {
        if (!values->settings[i].key || strcmp(config->settings[i].key, values->settings[i].key) ||
            values->settings[i].value < config->settings[i].minimum || values->settings[i].value > config->settings[i].maximum) {
            snprintf(config->error, sizeof(config->error), "Invalid settings."); return false;
        }
        staged.settings[i].value = values->settings[i].value;
        changed |= staged.settings[i].value != config->settings[i].value;
    }
    config->applying = true;
    void *change = NULL;
    bool runtime = changed && config->runtime.prepare;
    if (runtime && (!config->runtime.prepare(config->runtime_user, config, &staged, &change, config->error, sizeof(config->error)) ||
                    !config->runtime.activate(change, config->error, sizeof(config->error)))) goto fail;
    if (path && !ax_config_save(&staged, path)) {
        snprintf(config->error, sizeof(config->error), "Could not save settings. Changes were not applied."); goto fail;
    }
    int previous[AX_CONFIG_SETTING_COUNT];
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i) {
        previous[i] = config->settings[i].value;
        config->settings[i].value = staged.settings[i].value;
        if (!config->settings[i].restart_required) config->active_values[i] = staged.settings[i].value;
    }
    update_restart(config);
    if (runtime && !config->runtime.finish(change, true)) {
        snprintf(config->error, sizeof(config->error), "Runtime finalization failed."); config->applying = false; return false;
    }
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i) if (previous[i] != config->settings[i].value) {
        char text[32]; snprintf(text, sizeof(text), "%d", config->settings[i].value);
        AxEvent event = {.type = AX_EVENT_CONFIG_CHANGED, .data.config = {config->settings[i].key, text, config->settings[i].restart_required}};
        ax_event_emit(config->events, &event);
    }
    config->applying = false;
    return true;
fail:
    if (runtime && !config->runtime.finish(change, false)) snprintf(config->error, sizeof(config->error), "Could not restore the previous runtime.");
    if (!config->error[0]) snprintf(config->error, sizeof(config->error), "Could not apply settings.");
    config->applying = false;
    return false;
}
bool ax_config_apply(AxConfig *config, const AxConfig *values) { return apply(config, values, NULL); }
bool ax_config_apply_saved(AxConfig *config, const AxConfig *values, const char *path)
{
    if (!path) return false;
    return apply(config, values, path);
}
/** Serializes known desired settings to a temporary output stream. */
static bool write_config(FILE *file, const void *user)
{
    const AxConfig *config = user;
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i)
        if (fprintf(file, "%s=%d\n", config->settings[i].key, config->settings[i].value) < 0) return false;
    return true;
}
bool ax_config_save(const AxConfig *config, const char *path)
{
    return config && path && ax_file_write_atomic(path, write_config, config);
}
