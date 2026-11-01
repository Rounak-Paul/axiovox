#pragma once
#include "event.h"
typedef struct { const char *key; int value, minimum, maximum; bool restart_required; } AxSetting;
#define AX_CONFIG_SETTING_COUNT 7
typedef struct { AxSetting settings[AX_CONFIG_SETTING_COUNT]; int active_values[AX_CONFIG_SETTING_COUNT]; AxEvents *events; bool restart_pending; } AxConfig;
/** Initializes known settings and their validation bounds. Owner-thread access only. */
void ax_config_init(AxConfig *config, AxEvents *events);
/** Gets a registered setting, or NULL for an unknown key. */
const AxSetting *ax_config_get(const AxConfig *config, const char *key);
/** Validates and changes a setting; emits CONFIG_CHANGED only on an actual change. */
bool ax_config_set(AxConfig *config, const char *key, const char *value);
/** Loads key=value lines transactionally; rejects unknown, oversized, or invalid entries. */
bool ax_config_load(AxConfig *config, const char *path);
/** Writes current settings; returns false on any I/O failure. */
bool ax_config_save(const AxConfig *config, const char *path);

/** Atomically commits validated values from a staged config before notifying observers. */
bool ax_config_apply(AxConfig *config, const AxConfig *values);
/** Records startup-applied values and clears restart status after service initialization. */
void ax_config_mark_applied(AxConfig *config);
