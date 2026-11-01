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
        {"renderer.device", -1, -1, 65535, true},
        {"jobs.workers", 4, 1, 64, true},
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
bool ax_config_set(AxConfig *config, const char *key, const char *value)
{
    const AxSetting *found = ax_config_get(config, key);
    if (!found || !value || !*value) return false;
    char *end;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno || *end || parsed < found->minimum || parsed > found->maximum) return false;
    AxSetting *setting = &config->settings[found - config->settings];
    if (setting->value == parsed) return true;
    setting->value = (int)parsed;
    update_restart(config);
    char normalized[32];
    snprintf(normalized, sizeof(normalized), "%d", setting->value);
    AxEvent event = {.type = AX_EVENT_CONFIG_CHANGED, .data.config = {setting->key, normalized, setting->restart_required}};
    ax_event_emit(config->events, &event);
    return true;
}
bool ax_config_load(AxConfig *config, const char *path)
{
    if (!config || !path) return false;
    FILE *file = ax_file_open(path, "r");
    if (!file) return false;
    AxConfig staged = *config;
    staged.events = NULL;
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
bool ax_config_apply(AxConfig *config, const AxConfig *values)
{
    if (!config || !values) return false;
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i)
        if (!values->settings[i].key || strcmp(config->settings[i].key, values->settings[i].key) ||
            values->settings[i].value < config->settings[i].minimum || values->settings[i].value > config->settings[i].maximum) return false;
    int previous[AX_CONFIG_SETTING_COUNT];
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i) {
        previous[i] = config->settings[i].value;
        config->settings[i].value = values->settings[i].value;
    }
    update_restart(config);
    for (size_t i = 0; i < AX_CONFIG_SETTING_COUNT; ++i) if (previous[i] != config->settings[i].value) {
        char value[32];
        snprintf(value, sizeof(value), "%d", config->settings[i].value);
        AxEvent event = {.type = AX_EVENT_CONFIG_CHANGED, .data.config = {config->settings[i].key, value, config->settings[i].restart_required}};
        ax_event_emit(config->events, &event);
    }
    return true;
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
