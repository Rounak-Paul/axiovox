#pragma once
#include "event.h"
#define AX_CONFIG_SETTING_COUNT 7
typedef struct AxConfig AxConfig;
typedef struct { const char *key; int value, minimum, maximum; bool restart_required; } AxSetting;
typedef struct {
    bool (*prepare)(void *user, const AxConfig *current, const AxConfig *next, void **change, char *error, size_t capacity);
    bool (*activate)(void *change, char *error, size_t capacity);
    bool (*finish)(void *change, bool commit);
} AxConfigRuntime;
struct AxConfig {
    AxSetting settings[AX_CONFIG_SETTING_COUNT];
    int active_values[AX_CONFIG_SETTING_COUNT];
    AxEvents *events;
    bool restart_pending, applying;
    AxConfigRuntime runtime;
    void *runtime_user;
    char error[160];
};
/** Initializes known settings and validation bounds. Owner-thread access only. */
void ax_config_init(AxConfig *config, AxEvents *events);
/** Gets a registered setting, or NULL for an unknown key. */
const AxSetting *ax_config_get(const AxConfig *config, const char *key);
/** Creates an isolated draft without observers or runtime hooks. */
void ax_config_stage(AxConfig *draft, const AxConfig *config);
/** Binds main-thread runtime transaction hooks; activate/finish must handle NULL no-op tokens; finish releases its token. */
bool ax_config_bind_runtime(AxConfig *config, AxConfigRuntime runtime, void *user);
/** Validates and applies one setting through the runtime transaction; nested changes during notifications are rejected. */
bool ax_config_set(AxConfig *config, const char *key, const char *value);
/** Loads key=value lines transactionally; rejects unknown/invalid entries without changing running services. */
bool ax_config_load(AxConfig *config, const char *path);
/** Atomically writes desired settings without applying runtime changes. */
bool ax_config_save(const AxConfig *config, const char *path);
/** Prepares/activates runtime changes, commits all values, finalizes, and then notifies observers. */
bool ax_config_apply(AxConfig *config, const AxConfig *values);
/** Applies and atomically persists a draft; save/activation failure rolls runtime changes back before returning false. */
bool ax_config_apply_saved(AxConfig *config, const AxConfig *values, const char *path);
/** Returns the latest transaction error, or an empty string on success. */
const char *ax_config_error(const AxConfig *config);
/** Records startup-applied values after service initialization. */
void ax_config_mark_applied(AxConfig *config);
